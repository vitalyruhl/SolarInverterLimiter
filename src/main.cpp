#include <Arduino.h>
#include <PubSubClient.h> // for MQTT
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadische Funktionen (printf-Stil)
#include <Ticker.h>
#include "Wire.h"
#include <BME280_I2C.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);

#include "settings.h"
#include "logging/logging.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include "Smoother/Smoother.h"

// predefine the functions
void SetupStartDisplay(); //setup the display
void reconnectMQTT(); //reconnect to the MQTT broker
void cb_MQTT(char *topic, byte *message, unsigned int length); //callback function for incoming MQTT messages
void publishToMQTT(); //publish the current values to the MQTT broker
void cb_PublishToMQTT(); //ticker callback function to publish the current values to the MQTT broker
void cb_MQTTListener(); //ticker callback function to listen for incoming MQTT messages
void cb_RS485Listener(); //ticker callback function to read/write RS485
void testRS232(); //test the RS232 communication (not used in normal operation)

void readBme280(); //read the values from the BME280 (Temperature, Humidity) and calculate the dewpoint
void WriteToDisplay();//write the values to the display

void SetupCheckForResetButton();//check if button is pressed on boot for reset to defaults
void SetupCheckForAPModeButton(); //check if button is pressed on boot for starting AP mode
void SetupStartTemperatureMeasuring(); //setup the BME280 temperature and humidity sensor
bool SetupStartWebServer(); //setup and start the web server
void ProjectConfig(); //project specific configuration
void PinSetup(); //setup the pins
void CheckButtons(); //check if buttons are pressed after boot
void ShowDisplay(); //start the display for a defined time
void ShowDisplayOff(); //turn off the display
void CheckVentilator(float aktualTemperature); //check if the ventilator should be turned on or off
static float computeDewPoint(float temperatureC, float relHumidityPct); //compute the dewpoint from temperature and humidity
void HeaterControl(bool heaterOn); //control the heater based on settings
//--------------------------------------------------------------------------------------------------------------

#pragma region configuratio variables

BME280_I2C bme280;

Helpers helpers;

Ticker PublischMQTTTicker;
Ticker PublischMQTTTSettingsTicker;
Ticker ListenMQTTTicker;
Ticker RS485Ticker;
Ticker temperatureTicker;
Ticker displayTicker;

WiFiClient espClient;
PubSubClient client(espClient);

Smoother* powerSmoother = nullptr; //there is a memory allocation in setup, better use a pointer here

// globale helpers variables
int AktualImportFromGrid = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)
float temperature = 0.0;      // current temperature in Celsius
float Dewpoint = 0.0;         // current dewpoint in Celsius
float Humidity = 0.0;         // current humidity in percent
float Pressure = 0.0;         // current pressure in hPa

bool tickerActive = false;    // flag to indicate if the ticker is active
bool displayActive = true;   // flag to indicate if the display is active

#pragma endregion configuration variables

//----------------------------------------
// MAIN FUNCTIONS
//----------------------------------------

void setup()
{

  LoggerSetupSerial(); // Initialize the serial logger

  sl->Printf("System setup start...").Debug();

  PinSetup();
  sl->Printf("Check for reset/AP button...").Debug();
  SetupCheckForResetButton();
  SetupCheckForAPModeButton();

  // sl->Printf("Clear Settings...").Debug();
  // cfg.clearAllFromPrefs();

  sl->Printf("Load configuration...").Debug();
  cfg.loadAll();

  //04.09.2025 new function to check all settings with errors
  cfg.checkSettingsForErrors();

  //05.09.2025 add new updateTopics function to the mqttSettings
  mqttSettings.updateTopics(); // better restart the device after changing the Publish_Topic to prevent fragmentation of the memory and overflow of the heap

  // init modules...
  sl->Printf("init modules...").Debug();
  SetupStartDisplay();
  ShowDisplay();

  helpers.blinkBuidInLEDsetpinMode(); // Initialize the built-in LED pin mode
  helpers.blinkBuidInLED(3, 100);     // Blink the built-in LED 3 times with a 100ms delay

  sl->Printf("Init buffer...!").Debug();
  // powerSmoother.fillBufferOnStart(generalSettings.minOutput.get());
  powerSmoother = new Smoother(
        generalSettings.smoothingSize.get(),
        generalSettings.inputCorrectionOffset.get(),
        generalSettings.minOutput.get(),
        generalSettings.maxOutput.get()
      );
  powerSmoother->fillBufferOnStart(generalSettings.minOutput.get());

  sl->Printf("Configuration printout:").Debug();
  Serial.println(cfg.toJSON(false)); // Print the configuration to the serial monitor
  // testRS232();

  //------------------
  sl->Printf("⚠️ SETUP: Starting RS485...!").Debug();
  sll->Printf("Starting RS485...").Debug();
  RS485begin();

  sl->Printf("SETUP: Check and start BME280!").Debug();
  sll->Printf("Check and start BME280!").Debug();
  SetupStartTemperatureMeasuring(); //also starts the temperature ticker, its allways active

  //----------------------------------------

  bool isStartedAsAP = SetupStartWebServer();

  //----------------------------------------
  // -- Setup MQTT connection --
  sl->Printf("⚠️ SETUP: Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  sll->Printf("Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  client.setServer(mqttSettings.mqtt_server.get().c_str(), static_cast<uint16_t>(mqttSettings.mqtt_port.get())); // Set the MQTT server and port
  client.setCallback(cb_MQTT);

  //set rs232 ticker, its always active
  sl->Debug("Setup RS485 ticker...");
  sll->Debug("Setup RS485 ticker...");
  RS485Ticker.attach(generalSettings.RS232PublishPeriod.get(), cb_RS485Listener);

  sl->Debug("System setup completed.");
  sll->Debug("Setup completed.");

  // Register system runtime provider
    cfg.addRuntimeProvider({
        .name = "system",
        .fill = [](JsonObject &o){
            o["freeHeap"] = ESP.getFreeHeap();
            o["rssi"] = WiFi.RSSI();
        }
    });
    cfg.defineRuntimeField("system", "freeHeap", "Free Heap", "B", 0);
    cfg.defineRuntimeField("system", "rssi", "WiFi RSSI", "dBm", 0);
}

void loop()
{
  CheckButtons();
  if (WiFi.status() == WL_CONNECTED && client.connected())
  {
    digitalWrite(LED_BUILTIN, LOW); // show that we are connected
  }
  else
  {
    digitalWrite(LED_BUILTIN, HIGH); // disconnected
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP)
  {
    if (tickerActive) // Check if the ticker is already active
    {
      ShowDisplay(); // Show the display to indicate WiFi is lost
      sl->Debug("WiFi not connected or in AP mode! deactivate ticker.");
      sll->Debug("WiFi lost connection!");
      sll->Debug("or run in AP mode!");
      sll->Debug("deactivate mqtt ticker.");
      PublischMQTTTicker.detach(); // Stop the ticker if WiFi is not connected or in AP mode
      ListenMQTTTicker.detach();   // Stop the ticker if WiFi is not connected or in AP mode
      tickerActive = false;        // Set the flag to indicate that the ticker is not active

      // check if ota is active and settings is off, reboot device, to stop ota
      if (generalSettings.allowOTA.get() == false && cfg.isOTAInitialized())
      {
        sll->Debug("Stop OTA-Modeule");
        cfg.stopOTA();//TODO: implemented, but not tested yet
        // cfg.reboot();
      }
    }
    //reconnect, if not in ap mode
    if (WiFi.getMode() != WIFI_AP){
      sl->Debug("reconnect to WiFi...");
      sll->Debug("reconnect to WiFi...");
      // WiFi.reconnect(); // try to reconnect to WiFi
      bool isStartedAsAP = SetupStartWebServer();
    }
  }
  else
  {
    if (!tickerActive) // Check if the ticker is not already active
    {
      ShowDisplay(); // Show the display
      sl->Debug("WiFi connected! Reattach ticker.");
      sll->Debug("WiFi reconnected!");
      sll->Debug("Reattach ticker.");
      PublischMQTTTicker.attach(mqttSettings.MQTTPublischPeriod.get(), cb_PublishToMQTT); // Reattach the ticker if WiFi is connected
      ListenMQTTTicker.attach(mqttSettings.MQTTListenPeriod.get(), cb_MQTTListener);      // Reattach the ticker if WiFi is connected
      if(generalSettings.allowOTA.get()){
        sll->Debug("Start OTA-Modeule");
        cfg.setupOTA("Ota-esp32-device", generalSettings.otaPassword.get().c_str());
      }
      ShowDisplay();               // Show the display
      tickerActive = true; // Set the flag to indicate that the ticker is active
    }
  }

  WriteToDisplay();

  if (WiFi.getMode() == WIFI_AP) {
    helpers.blinkBuidInLED(5, 50); // show we are in AP mode
    sll->Debug("or run in AP mode!");
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      sl->Debug("❌ WiFi not connected!");
      sll->Debug("reconnect to WiFi...");
      // cfg.reconnectWifi();
      SetupStartWebServer();
      delay(1000);
      return;
    }
    // blinkBuidInLED(1, 100); // not used here, because blinker is used if we get a message from MQTT
  }



  if (!client.connected())
  {
    sll->Debug("MQTT Not Connected! -> reconnecting...");
    reconnectMQTT();
  }


  CheckVentilator(temperature);
  cfg.handleClient();
  cfg.handleOTA();
  delay(100);
}

//----------------------------------------
// MQTT FUNCTIONS
//----------------------------------------
void reconnectMQTT()
{
  IPAddress mqttIP;
  if (mqttIP.fromString(mqttSettings.mqtt_server.get()))
  {
    client.setServer(mqttIP, static_cast<uint16_t>(mqttSettings.mqtt_port.get()));
  }
  else
  {
    sl->Printf("Invalid MQTT IP: %s", mqttSettings.mqtt_server.get().c_str()).Error();
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP)
  {
    // sl->Debug("WiFi not connected or in AP mode! Skipping mqttSettings.");
    return;
  }

  int retry = 0;
  int maxRetry = 10; // max retry count

  while (!client.connected() && retry < maxRetry) // Retry 5 times before restarting
  {
    // sl->Printf("MQTT reconnect attempt %d...", retry + 1).Log(level);

    sl->Printf("Attempting MQTT connection to %s:%d...",
               mqttSettings.mqtt_server.get().c_str(),
               mqttSettings.mqtt_port.get())
        .Debug();

    helpers.blinkBuidInLED(5, 300);
    retry++;

    // print the mqtt settings
    sl->Printf("Connecting to MQTT broker...").Debug();
    sl->Printf("MQTT Hostname: %s", mqttSettings.Publish_Topic.get().c_str()).Debug();
    sl->Printf("MQTT Server: %s", mqttSettings.mqtt_server.get().c_str()).Debug();
    sl->Printf("MQTT Port: %d", mqttSettings.mqtt_port.get()).Debug();
    sl->Printf("MQTT User: %s", mqttSettings.mqtt_username.get().c_str()).Debug();
    // sl->Printf("MQTT Password: %s", mqttSettings.mqtt_password.get().c_str()).Debug();
    sl->Printf("MQTT Password: ***").Debug();
    sl->Printf("MQTT Sensor Power Usage Topic: %s", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str()).Debug();

    client.connect(mqttSettings.Publish_Topic.get().c_str(), mqttSettings.mqtt_username.get().c_str(), mqttSettings.mqtt_password.get().c_str()); // Connect to the MQTT broker
    delay(2000);

    if (client.connected())
    {
      sl->Debug("Connected!");
    }
    else
    {
      sl->Printf("Failed, rc=%d", client.state()).Error();
    }

    if (client.connected())
      break; // Exit the loop if connected successfully

    // disconnect from MQTT broker if not connected
    client.disconnect();
    delay(500);
    esp_task_wdt_reset(); // Reset watchdog to prevent reboot
  }

  if (retry >= maxRetry)
  {
    sl->Printf("MQTT reconnect failed after %d attempts. go to next loop...", maxRetry);

    // if we dont get a actual value from the grid, reduce the acktual value step by 1 up to 0
    if (AktualImportFromGrid > 0)
    {
      AktualImportFromGrid = AktualImportFromGrid - 10;
      // inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
      inverterSetValue = powerSmoother->smooth(AktualImportFromGrid);
    }
    // AktualImportFromGrid
    // delay(1000);   // Wait for 1 second before restarting
    // todo: add logic to restart the ESP32 if no connection is available after 10 minutes
    // ESP.restart(); // Restart the ESP32
    return; // exit the function if max retry reached
  }

  else
  {
    sl->Debug("MQTT connected!");
    cb_PublishToMQTT(); // publish the settings to the MQTT broker, before subscribing to the topics
    sl->Debug("trying to subscribe to topics...");
    sll->Debug("subscribe to mqtt...");

    if (client.subscribe(mqttSettings.mqtt_sensor_powerusage_topic.get().c_str()))
    {
      sl->Printf("✅ Subscribed to topic: [%s]\n", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str());
    }
    else
    {
      sl->Printf("❌ Subscription to Topic[%s] failed!", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str());
    }
  }
}

void publishToMQTT()
{
  if (client.connected())
  {
    sl->Printf("--> MQTT: Topic[%s] -> [%d]", mqttSettings.mqtt_publish_setvalue_topic.c_str(), inverterSetValue).Debug();
    client.publish(mqttSettings.mqtt_publish_setvalue_topic.c_str(), String(inverterSetValue).c_str());
    client.publish(mqttSettings.mqtt_publish_getvalue_topic.c_str(), String(AktualImportFromGrid).c_str());
    client.publish(mqttSettings.mqtt_publish_Temperature_topic.c_str(), String(temperature).c_str());
    client.publish(mqttSettings.mqtt_publish_Humidity_topic.c_str(), String(Humidity).c_str());
    client.publish(mqttSettings.mqtt_publish_Dewpoint_topic.c_str(), String(Dewpoint).c_str());
  }
  else
  {
    sl->Debug("publishToMQTT: MQTT not connected!");
  }
}

void cb_MQTT(char *topic, byte *message, unsigned int length)
{
  String messageTemp((char *)message, length); // Convert byte array to String using constructor
  messageTemp.trim();                          // Remove leading and trailing whitespace

  sl->Printf("<-- MQTT: Topic[%s] <-- [%s]", topic, messageTemp.c_str()).Debug();
  helpers.blinkBuidInLED(1, 100); // blink the LED once to indicate that the loop is running
  if (strcmp(topic, mqttSettings.mqtt_sensor_powerusage_topic.get().c_str()) == 0)
  {
    // check if it is a number, if not set it to 0
    if (messageTemp.equalsIgnoreCase("null") ||
        messageTemp.equalsIgnoreCase("undefined") ||
        messageTemp.equalsIgnoreCase("NaN") ||
        messageTemp.equalsIgnoreCase("Infinity") ||
        messageTemp.equalsIgnoreCase("-Infinity"))
    {
      sl->Printf("Received invalid value from MQTT: %s", messageTemp.c_str());
      messageTemp = "0";
    }

    AktualImportFromGrid = messageTemp.toInt();
  }
}

//----------------------------------------
// HELPER FUNCTIONS
//----------------------------------------

void cb_PublishToMQTT()
{
  publishToMQTT(); // send to Mqtt
}

void cb_MQTTListener()
{
  client.loop(); // process incoming MQTT messages
}

void cb_RS485Listener()
{
  // inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
  inverterSetValue = powerSmoother->smooth(AktualImportFromGrid);
  // if (generalSettings.enableController.get())
  if (generalSettings.enableController.get())
  {
    // rs485.sendToRS485(static_cast<uint16_t>(inverterSetValue));
    // powerSmoother.setCorrectionOffset(generalSettings.inputCorrectionOffset.get()); // apply the correction offset to the smoother, if needed
    powerSmoother->setCorrectionOffset(generalSettings.inputCorrectionOffset.get()); // apply the correction offset to the smoother, if needed
    sendToRS485(static_cast<uint16_t>(inverterSetValue));
    sl->Printf("controller is enabled! Set inverter to %d W", inverterSetValue).Debug();
  }
  else
  {
    sl->Debug("controller is didabled!");
    sl->Debug("MAX-POWER!");
    sll->Debug("Limiter is didabled!");
    sll->Debug("MAX-POWER!");
    sendToRS485(generalSettings.maxOutput.get()); // send the maxOutput to the RS485 module");
  }
}

void testRS232()
{
  // test the RS232 connection
  sl->Printf("Testing RS232 connection... shorting RX and TX pins!");
  sl->Printf("Baudrate: %d", rs485settings.baudRate);
  sl->Printf("RX Pin: %d", rs485settings.rxPin);
  sl->Printf("TX Pin: %d", rs485settings.txPin);
  sl->Printf("DE Pin: %d", rs485settings.dePin);

  Serial2.begin(rs485settings.baudRate, SERIAL_8N1, rs485settings.rxPin, rs485settings.txPin);
  Serial2.println("Hello RS485");
  delay(300);
  if (Serial2.available())
  {
    Serial.println("Received on Serial2!");
  }
}

void SetupCheckForResetButton()
{

  // check for pressed reset button

  if (digitalRead(BUTTON_PIN_RESET_TO_DEFAULTS) == LOW)
  {
    sl->Internal("Reset-Button pressed... -> Resett all settings...");
    sll->Internal("Reset-Button pressed!");
    sll->Internal("Resett all settings!");
    cfg.clearAllFromPrefs(); // Clear all settings from EEPROM
    delay(10000);            // Wait for 10 seconds to avoid multiple resets
    generalSettings.unconfigured.set(true); // Set the unconfigured flag to true
    cfg.saveAll();           // Save the default settings to EEPROM
    delay(10000);            // Wait for 10 seconds to avoid multiple resets
    ESP.restart();           // Restart the ESP32
  }
}

void SetupCheckForAPModeButton()
{
  String APName = "ESP32_Config";
  String pwd = "config1234"; // Default AP password

  // if (wifiSettings.wifiSsid.get().length() == 0 || generalSettings.unconfigured.get())
  if (wifiSettings.wifiSsid.get().length() == 0 )
  {
    sl->Printf("⚠️ SETUP: wifiSsid.get() ist empty? [%s], or unconfigured flag is active", wifiSettings.wifiSsid.get().c_str()).Error();
    cfg.startAccessPoint("192.168.4.1", "255.255.255.0", APName, "");
    generalSettings.unconfigured.set(false); // Set the unconfigured flag to false after starting the access point
    cfg.saveAll(); // Save the settings to EEPROM
  }

  // check for pressed AP-Mode button

  if (digitalRead(BUTTON_PIN_AP_MODE) == LOW)
  {
    sl->Internal("AP-Mode-Button pressed... -> Start AP-Mode...");
    sll->Internal("AP-Mode-Button!");
    sll->Internal("-> Start AP-Mode...");
    cfg.startAccessPoint("192.168.4.1", "255.255.255.0", APName, "");
  }
}

void SetupStartTemperatureMeasuring()
{
  // init BME280 for temperature and humidity sensor
  bme280.setAddress(BME280_ADDRESS, I2C_SDA, I2C_SCL);
  bool isStatus = bme280.begin(
      bme280.BME280_STANDBY_0_5,
      bme280.BME280_FILTER_16,
      bme280.BME280_SPI3_DISABLE,
      bme280.BME280_OVERSAMPLING_2,
      bme280.BME280_OVERSAMPLING_16,
      bme280.BME280_OVERSAMPLING_1,
      bme280.BME280_MODE_NORMAL);
  if (!isStatus)
  {
    sl->Printf("can NOT initialize for using BME280.").Debug();
    sll->Printf("No BME280 detected!").Debug();
  }
  else
  {
    sl->Printf("ready to using BME280. Sart Ticker...").Debug();
    sll->Printf("BME280 detected!").Debug();

 // Sensor data provider
    cfg.addRuntimeProvider({
        .name = "sensors",
        .fill = [](JsonObject &o){
            // Primary short keys expected by frontend
            o["temp"] = temperature;
            o["hum"] = Humidity;
            o["dew"] = Dewpoint;
            o["Pressure"] = Pressure;
        }
    });

    // Runtime field metadata for dynamic UI
    // With thresholds: warn (yellow) and alarm (red). Example ranges; adjust as needed.
    cfg.defineRuntimeFieldThresholds("sensors", "temp", "Temperature", "°C", 1,
        1.0f, 30.0f,   // warnMin / warnMax
        0.0f, 32.0f,   // alarmMin / alarmMax
         true,true,true,true
    );

    cfg.defineRuntimeFieldThresholds("sensors", "hum", "Humidity", "%", 1,
        30.0f, 70.0f,
        15.0f, 90.0f,
        false,false,false,true
    );

    // only basic field, no thresholds
    cfg.defineRuntimeField("sensors", "dew", "Dewpoint", "°C", 1);
    cfg.defineRuntimeField("sensors", "Pressure", "Pressure", "hPa", 1);

    // Cross-field alarm: temperature within 1.0°C above dewpoint (risk of condensation)
    cfg.defineRuntimeAlarm(
        "dewpoint_risk",
        [](const JsonObject &root){
            if(!root.containsKey("sensors")) return false;
            const JsonObject sensors = root["sensors"].as<JsonObject>();
            if(!sensors.containsKey("temp") || !sensors.containsKey("dew")) return false;
            float t = sensors["temp"].as<float>();
            float d = sensors["dew"].as<float>();
            return (t - d) <= 1.2f; // risk window
        },
        [](){ 
          sl->Printf("[ALARM] Dewpoint proximity risk ENTER, set heater ON").Debug();
          sll->Printf("[ALARM] Dewpoint").Debug();
          HeaterControl(true);
        }, 
        [](){ 
          sl->Printf("[ALARM] Dewpoint proximity risk EXIT, set heater OFF").Debug(); 
          sll->Printf("[Info] Alarm Dewpoint gone").Debug();
          HeaterControl(false);
        }
    );

    // Runtime boolean alarm
    cfg.defineRuntimeBool("alarms", "dewpoint_risk", "Dewpoint Risk", true); // show as bool alarm when true


    // Temperature MIN alarm -> Heater relay ON when temperature below alarmMin (0.0°C) and OFF when recovered.
    // Uses a little hysteresis (enter < 0.0, exit > 0.5) to avoid fast toggling.
    cfg.defineRuntimeAlarm(
        "temp_low",
        [](const JsonObject &root){
            static bool lastState = false; // for hysteresis
            if(!root.containsKey("sensors")) return false;
            const JsonObject sensors = root["sensors"].as<JsonObject>();
            if(!sensors.containsKey("temp")) return false;
            float t = sensors["temp"].as<float>();
            // Hysteresis: once active keep it on until t > 0.5
            if(lastState){ // currently active -> wait until we are clearly above release threshold
                lastState = (t < 0.5f);
            } else {       // currently inactive -> trigger when below entry threshold
                lastState = (t < 0.0f);
            }
            return lastState;
        },
        [](){
            sl->Printf("[ALARM] Temperature too low -> HEATER ON");
            HeaterControl(true);
        },
        [](){
            sl->Printf ("[ALARM] Temperature recovered -> HEATER OFF");
            HeaterControl(false);
        }
    );
    cfg.defineRuntimeBool("alarms", "temp_low", "too low temperature", true); // show as bool alarm when true in UI



    temperatureTicker.attach(generalSettings.ReadTemperatureTicker.get(), readBme280); // Attach the ticker to read BME280 every 5 seconds
    readBme280();                                                // Read the BME280 sensor data once at startup
  }
}

bool SetupStartWebServer()
{
  sl->Printf("⚠️ SETUP: Starting Webserver...!").Debug();
  sll->Printf("Starting Webserver...!").Debug();
  
  if (wifiSettings.wifiSsid.get().length() == 0)
  {
    sl->Printf("No SSID! --> Start AP!").Debug();
    sll->Printf("No SSID!").Debug();
    sll->Printf("Start AP!").Debug();
    cfg.startAccessPoint();
    delay(1000);
    return true; // Skip webserver setup if no SSID is set
  }

  if (WiFi.getMode() == WIFI_AP) {
    sl->Printf("🖥️ Run in AP Mode! ");
    sll->Printf("Run in AP Mode! ");
    return false; // Skip webserver setup in AP mode
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiSettings.useDhcp.get())
    {
      sl->Printf("startWebServer: DHCP enabled\n");
      cfg.startWebServer(wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
    }
    else
    {
      sl->Printf("startWebServer: DHCP disabled\n");
      // cfg.startWebServer("192.168.2.127", "192.168.2.250", "255.255.255.0", wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
      cfg.startWebServer(wifiSettings.staticIp.get(), wifiSettings.gateway.get(), wifiSettings.subnet.get(), 
                  wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
    }
    // cfg.reconnectWifi();
    WiFi.setSleep(false);
    delay(1000);
  }
  sl->Printf("\n\nWebserver running at: %s\n", WiFi.localIP().toString().c_str());
  sll->Printf("Web: %s\n\n", WiFi.localIP().toString().c_str());
  sl->Printf("WLAN-Strength: %d dBm\n", WiFi.RSSI());
  sl->Printf("WLAN-Strength is: %s\n\n", WiFi.RSSI() > -70 ? "good" : (WiFi.RSSI() > -80 ? "ok" : "weak"));
  sll->Printf("WLAN: %s\n", WiFi.RSSI() > -70 ? "good" : (WiFi.RSSI() > -80 ? "ok" : "weak"));




  return true; // Webserver setup completed
}

static float computeDewPoint(float temperatureC, float relHumidityPct) {
    if (isnan(temperatureC) || isnan(relHumidityPct)) return NAN;
    if (relHumidityPct <= 0.0f) relHumidityPct = 0.1f;       // Unterlauf abfangen
    if (relHumidityPct > 100.0f) relHumidityPct = 100.0f;    // Clamp
    const float a = 17.62f;
    const float b = 243.12f;
    float rh = relHumidityPct / 100.0f;
    float gamma = (a * temperatureC) / (b + temperatureC) + log(rh);
    float dew = (b * gamma) / (a - gamma);
    return dew;
}

void readBme280()
{
  // todo: add settings for correcting the values!!!
  //   set sea-level pressure
  bme280.setSeaLevelPressure(1010);

  bme280.read();

  temperature = bme280.data.temperature + generalSettings.TempCorrectionOffset.get(); // store the temperature value in the global variable
  Humidity = bme280.data.humidity + generalSettings.HumidityCorrectionOffset.get();   // store the temperature value in the global variable
  Pressure = bme280.data.pressure;
  Dewpoint = computeDewPoint(temperature, Humidity);

  // output formatted values to serial console
  sl->Printf("-----------------------").Debug();
  sl->Printf("Temperature: %2.1lf °C", temperature).Debug();
  sl->Printf("Humidity   : %2.1lf %rH", Humidity).Debug();
  sl->Printf("Dewpoint   : %2.1lf °C", Dewpoint).Debug();
  sl->Printf("Pressure   : %4.0lf hPa", Pressure).Debug();
  sl->Printf("Altitude   : %4.2lf m", bme280.data.altitude).Debug();
  sl->Printf("-----------------------").Debug();
}

void HeaterControl(bool heaterOn){
  if (!generalSettings.enableHeater.get()) {
    digitalWrite(RELAY_HEATER_PIN, HIGH); // turn heater off
    return; // exit the function if the heater control is disabled
  }
  if (heaterOn){
    digitalWrite(RELAY_HEATER_PIN, LOW); // turn heater on
  } else {
    digitalWrite(RELAY_HEATER_PIN, HIGH); // turn heater off
  }
}

void WriteToDisplay()
{
  // display.clearDisplay();
  display.fillRect(0, 0, 128, 24, BLACK); // Clear the previous message area

  if (displayActive == false)
  {
    return; // exit the function if the display is not active
  }

  display.drawRect(0, 0, 128, 24, WHITE);

  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(3, 3);
  if (temperature > 0)
  {
    display.printf("<- %d W|Temp: %2.1f", AktualImportFromGrid, temperature);
  }
  else
  {
    display.printf("<- %d W", AktualImportFromGrid);
  }

  display.setCursor(3, 13);
  if (Dewpoint != 0)
  {
    display.printf("-> %d W|DP-T: %2.1f", inverterSetValue, Dewpoint);
  }
  else
  {
    display.printf("-> %d W", inverterSetValue);
  }

  display.display();
}

void PinSetup()
{
  analogReadResolution(12);  // Use full 12-bit resolution

  pinMode(BUTTON_PIN_RESET_TO_DEFAULTS, INPUT_PULLUP); // importand: BUTTON is LOW aktiv!
  pinMode(BUTTON_PIN_AP_MODE, INPUT_PULLUP);           // importand: BUTTON is LOW aktiv!

  pinMode(RELAY_VENTILATOR_PIN, OUTPUT);
  digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // set the ventilator relay to HIGH (off) at startup
}

void CheckVentilator(float aktualTemperature)
{
  // sl->Printf("Check Ventilator...\nCurrent Temperature: %2.3f °C", aktualTemperature);
  // Check if ventilator control is enabled
  if (!generalSettings.VentilatorEnable.get())
  {
    // sl->Debug("Ventilator control is disabled.");
    digitalWrite(RELAY_VENTILATOR_PIN, LOW); // Deactivate ventilator relay if control is disabled
    return;                                   // Exit if ventilator control is disabled
  }

  // Check if the temperature exceeds the ON threshold
  if (aktualTemperature >= generalSettings.VentilatorOn.get())
  {
    // sl->Debug("Ventilator ON - Temperature threshold exceeded.");
    digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // Activate ventilator relay
  }
  else if (aktualTemperature <= generalSettings.VentilatorOFF.get())
  {
    // sl->Debug("Ventilator OFF - Temperature below OFF threshold.");
    digitalWrite(RELAY_VENTILATOR_PIN, LOW); // Deactivate ventilator relay
  }
}

void CheckButtons()
{
  // sl->Debug("Check Buttons...");
  if (digitalRead(BUTTON_PIN_RESET_TO_DEFAULTS) == LOW)
  {
    sl->Internal("Reset-Button pressed after reboot... -> Start Display Ticker...");
    ShowDisplay();
  }

  if (digitalRead(BUTTON_PIN_AP_MODE) == LOW)
  {
    sl->Internal("AP-Mode-Button pressed after reboot... -> Start Display Ticker...");
    ShowDisplay();
  }
}

void ShowDisplay()
{
  displayTicker.detach(); // Stop the ticker to prevent multiple calls
  display.ssd1306_command(SSD1306_DISPLAYON); // Turn on the display
  displayTicker.attach(generalSettings.displayShowTime.get(), ShowDisplayOff); // Reattach the ticker to turn off the display after the specified time
  displayActive = true;
}

void ShowDisplayOff()
{
  displayTicker.detach(); // Stop the ticker to prevent multiple calls
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Turn off the display
  // display.fillRect(0, 0, 128, 24, BLACK); // Clear the previous message area

  if (generalSettings.saveDisplay.get()){
    displayActive = false;
  }
}