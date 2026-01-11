#include <Arduino.h>
#include <PubSubClient.h> // for MQTT
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadic functions (printf-style)
#include <Ticker.h>
#include "Wire.h"
#include <BME280_I2C.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
// AsyncWebServer instance is provided by ConfigManager library (extern AsyncWebServer server)

#include "settings.h"
#include "logging/logging.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include "Smoother/Smoother.h"
#include "helpers/relays.h"

#if __has_include("secret/wifiSecret.h")
#include "secret/wifiSecret.h"
#endif

#ifndef APMODE_SSID
#define APMODE_SSID "ESP32_Config"
#endif

#ifndef APMODE_PASSWORD
#define APMODE_PASSWORD "config1234"
#endif

// predeclare the functions (prototypes)
void SetupStartDisplay();
void reconnectMQTT();
void cb_MQTT(char *topic, byte *message, unsigned int length);
void publishToMQTT();
void cb_PublishToMQTT();
void cb_MQTTListener();
void cb_RS485Listener();
void testRS232();
void readBme280();
void WriteToDisplay();
void SetupCheckForResetButton();
void SetupCheckForAPModeButton();
void SetupStartTemperatureMeasuring();
bool SetupStartWebServer();
void ProjectConfig();
void PinSetup();
void CheckButtons();
void CheckVentilator(float currentTemperature);
void EvaluateHeater(float currentTemperature);
void ShowDisplay();
void ShowDisplayOff();
static float computeDewPoint(float temperatureC, float relHumidityPct);
void HeaterControl(bool heaterOn);
static void logNetworkIpInfo(const char *context);
void setupGUI();
//--------------------------------------------------------------------------------------------------------------

#pragma region configuratio variables

BME280_I2C bme280;

Helpers helpers;

Ticker publishMqttTicker;
Ticker publishMqttSettingsTicker;
Ticker ListenMQTTTicker;
Ticker RS485Ticker;
Ticker temperatureTicker;
Ticker displayTicker;

WiFiClient espClient;
PubSubClient client(espClient);

Smoother* powerSmoother = nullptr; //there is a memory allocation in setup, better use a pointer here

// global helper variables
int currentGridImportW = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)
float temperature = 0.0;      // current temperature in Celsius
float Dewpoint = 0.0;         // current dewpoint in Celsius
float Humidity = 0.0;         // current humidity in percent
float Pressure = 0.0;         // current pressure in hPa

bool tickerActive = false;    // flag to indicate if the ticker is active
bool displayActive = true;   // flag to indicate if the display is active
static bool dewpointRiskActive = false; // tracks dewpoint alarm state
static bool heaterLatchedState = false; // hysteresis latch for heater

// WiFi downtime tracking for auto reboot
static unsigned long wifiLastGoodMillis = 0;     // last time WiFi was connected (and not AP)
static bool wifiAutoRebootArmed = false;         // becomes true after initial setup completion

static inline ConfigManagerRuntime &CRM() { return ConfigManager.getRuntime(); } // Shorthand helper for RuntimeManager access

#pragma endregion configuration variables

//----------------------------------------
// MAIN FUNCTIONS
//----------------------------------------

static void logNetworkIpInfo(const char *context)
{
  const WiFiMode_t mode = WiFi.getMode();
  const bool apActive = (mode == WIFI_AP) || (mode == WIFI_AP_STA);
  const bool staConnected = (WiFi.status() == WL_CONNECTED);

  if (apActive)
  {
    const IPAddress apIp = WiFi.softAPIP();
    sl->Printf("%s: AP IP: %s", context, apIp.toString().c_str()).Debug();
    sl->Printf("%s: AP SSID: %s", context, WiFi.softAPSSID().c_str()).Debug();
    sll->Printf("AP: %s", apIp.toString().c_str()).Debug();
  }

  if (staConnected)
  {
    const IPAddress staIp = WiFi.localIP();
    sl->Printf("%s: STA IP: %s", context, staIp.toString().c_str()).Debug();
    sll->Printf("IP: %s", staIp.toString().c_str()).Debug();
  }
}

void setup()
{

  //-----------------------------------------------------------------
    cfg.setAppName(APP_NAME); // Set an application name, used for SSID in AP mode and as a prefix for the hostname
    cfg.setVersion(VERSION); // Set the application version for web UI display
    // Optional demo: global CSS override
    // cfg.setCustomCss(GLOBAL_THEME_OVERRIDE, sizeof(GLOBAL_THEME_OVERRIDE) - 1); // Register global CSS override
    // cfg.setSettingsPassword(SETTINGS_PASSWORD); // Set the settings password from wifiSecret.h
    cfg.enableBuiltinSystemProvider(); // enable the builtin system provider (uptime, freeHeap, rssi etc.)
  //----------------------------------------------------------------------------------------------------------------------------------

systemSettings.init();    // System settings (OTA, version, etc.)
buttonSettings.init();    // GPIO button configuration
tempSettings.init();      // BME280 temperature sensor settings
ntpSettings.init();       // NTP time synchronization settings
wifiSettings.init();      // WiFi connection settings

LoggerSetupSerial(); // Initialize the serial logger

  sl->Printf("System setup start...").Debug();

  PinSetup();
  sl->Printf("Check for reset/AP button...").Debug();
  SetupCheckForResetButton();
  SetupCheckForAPModeButton();

  sl->Printf("Load configuration...").Debug();
  cfg.loadAll();
  delay(100); // Small delay
  
  Relays::initPins();// Re-apply relay pin modes with loaded settings (pins/polarity may differ from defaults)

  if (wifiSettings.wifiSsid.get().isEmpty())
  {
      sl->Printf("[MAIN] -------------------------------------------------------------").Debug();
      sl->Printf("[MAIN] SETUP: *** SSID is empty, setting My values *** ").Debug();
      sl->Printf("[MAIN] -------------------------------------------------------------").Debug();
      wifiSettings.wifiSsid.set(MY_WIFI_SSID);
      wifiSettings.wifiPassword.set(MY_WIFI_PASSWORD);
      wifiSettings.staticIp.set(MY_WIFI_IP);
      wifiSettings.useDhcp.set(false);
      ConfigManager.saveAll();
      delay(100); // Small delay
  }

  mqttSettings.updateTopics(); // Consider restarting after changing MQTT base topic to avoid heap fragmentation

  // init modules...
  sl->Printf("[SETUP] init modules...").Debug();
  sll->Printf("init modules...").Debug();
  SetupStartDisplay();
  ShowDisplay();

  helpers.blinkBuidInLEDsetpinMode(); // Initialize the built-in LED pin mode
  helpers.blinkBuidInLED(3, 100);     // Blink the built-in LED 3 times with a 100ms delay

  sl->Printf("[SETUP] Init buffer...!").Debug();
  powerSmoother = new Smoother(
        limiterSettings.smoothingSize.get(),
        limiterSettings.inputCorrectionOffset.get(),
        limiterSettings.minOutput.get(),
        limiterSettings.maxOutput.get()
      );
  powerSmoother->fillBufferOnStart(limiterSettings.minOutput.get());

  //------------------
  sl->Printf("[SETUP] Starting RS485...").Debug();
  sll->Printf("Starting RS485...").Debug();
  RS485begin();

  sl->Printf("[SETUP] Check and start BME280!").Debug();
  sll->Printf("Starting BME280!").Debug();
  SetupStartTemperatureMeasuring(); //also starts the temperature ticker, its allways active

  //----------------------------------------

  bool isStartedAsAP = SetupStartWebServer();

  //----------------------------------------
  // -- Setup MQTT connection --
  sl->Printf("[SETUP] Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  sll->Printf("Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  client.setServer(mqttSettings.mqtt_server.get().c_str(), static_cast<uint16_t>(mqttSettings.mqtt_port.get())); // Set the MQTT server and port
  client.setCallback(cb_MQTT);

  //set rs232 ticker, its always active
  sl->Printf("[SETUP] Setup RS485 ticker...").Debug();
  sll->Printf("att. RS485 ticker...").Debug();
  RS485Ticker.attach(limiterSettings.RS232PublishPeriod.get(), cb_RS485Listener);

  setupGUI();

  sl->Printf("[SETUP] System setup completed.").Debug();
  sll->Printf("Setup completed.").Debug();

  // initialize WiFi tracking
  if(WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP){
    wifiLastGoodMillis = millis();
  } else {
    wifiLastGoodMillis = millis(); // start timer now; will reboot later if never connects
  }
  wifiAutoRebootArmed = true; // after setup we start watching

  HeaterControl(false); // make sure heater is off at startup
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
      sl->Debug("WiFi not connected or in AP mode. Deactivating tickers.");
      sll->Debug("WiFi lost connection!");
      sll->Debug("Running in AP mode.");
      sll->Debug("Deactivating MQTT tickers.");
      publishMqttTicker.detach(); // Stop the ticker if WiFi is not connected or in AP mode
      ListenMQTTTicker.detach();   // Stop the ticker if WiFi is not connected or in AP mode
      tickerActive = false;        // Set the flag to indicate that the ticker is not active

      // If OTA is active but the setting is disabled, stop OTA
      if (!systemSettings.allowOTA.get() && cfg.isOTAInitialized())
      {
        sll->Debug("Stopping OTA module...");
        cfg.enableOTA(false);
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
    // Auto reboot logic: only if not AP mode, feature enabled and timeout exceeded
    if (wifiAutoRebootArmed && WiFi.getMode() != WIFI_AP){
  int timeoutMin = systemSettings.wifiRebootTimeoutMin.get();
        if(timeoutMin > 0){
            unsigned long now = millis();
            unsigned long elapsedMs = now - wifiLastGoodMillis;
            unsigned long thresholdMs = (unsigned long)timeoutMin * 60000UL;
            if(elapsedMs > thresholdMs){
                sl->Printf("[WiFi] Lost for > %d min -> reboot", timeoutMin).Error();
                sll->Printf("WiFi lost -> reboot").Error();
                delay(200);
                ESP.restart();
            }
        }
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
      publishMqttTicker.attach(mqttSettings.mqttPublishPeriodSec.get(), cb_PublishToMQTT); // Reattach the ticker if WiFi is connected
      ListenMQTTTicker.attach(mqttSettings.mqttListenPeriodSec.get(), cb_MQTTListener);    // Reattach the ticker if WiFi is connected
      if(systemSettings.allowOTA.get()){
        sll->Debug("Starting OTA module...");
        cfg.setupOTA("Ota-esp32-device", systemSettings.otaPassword.get().c_str());
      }
      ShowDisplay();               // Show the display
      tickerActive = true; // Set the flag to indicate that the ticker is active
    }
    // Update last good WiFi timestamp when connected (station mode only)
    if(WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP){
        wifiLastGoodMillis = millis();
    }
  }

  WriteToDisplay();

  if (WiFi.getMode() == WIFI_AP) {
    // Non-blocking AP heartbeat to avoid starving networking/web server.
    static unsigned long apBlinkLastMs = 0;
    static bool apBlinkOn = false;
    const unsigned long now = millis();
    if (now - apBlinkLastMs >= 250) {
      apBlinkLastMs = now;
      apBlinkOn = !apBlinkOn;
      digitalWrite(LED_BUILTIN, apBlinkOn ? HIGH : LOW);
    }
  } else {
    if (WiFi.status() != WL_CONNECTED) {
      sl->Debug("[ERROR] WiFi not connected!");
      sll->Debug("reconnect to WiFi...");
      // cfg.reconnectWifi();
      SetupStartWebServer();
      delay(1000);
      return;
    }
    else {
      // refresh last good timestamp here as safeguard
      wifiLastGoodMillis = millis();
    }
    // blinkBuidInLED(1, 100); // not used here, because blinker is used if we get a message from MQTT
  }



  if (!client.connected())
  {
    sll->Debug("MQTT Not Connected! -> reconnecting...");
    reconnectMQTT();
  }


  CheckVentilator(temperature);
  EvaluateHeater(temperature);
  cfg.handleClient();
  cfg.handleOTA();
  delay(100);
}

void setupGUI()
{

  // region sensor fields BME280
      CRM().addRuntimeProvider("sensors", [](JsonObject &data)
      {
          // Apply precision to sensor values to reduce JSON size
          data["temp"] = roundf(temperature * 10.0f) / 10.0f;     // 1 decimal place
          data["hum"] = roundf(Humidity * 10.0f) / 10.0f;        // 1 decimal place
          data["dew"] = roundf(Dewpoint * 10.0f) / 10.0f;        // 1 decimal place
          data["pressure"] = roundf(Pressure * 10.0f) / 10.0f;   // 1 decimal place
      });

      
      // Define sensor display fields using addRuntimeMeta
      RuntimeFieldMeta tempMeta;
      tempMeta.group = "sensors";
      tempMeta.key = "temp";
      tempMeta.label = "Temperature";
      tempMeta.unit = "°C";
      tempMeta.precision = 1;
      tempMeta.order = 2;
      CRM().addRuntimeMeta(tempMeta);

      RuntimeFieldMeta humMeta;
      humMeta.group = "sensors";
      humMeta.key = "hum";
      humMeta.label = "Humidity";
      humMeta.unit = "%";
      humMeta.precision = 1;
      humMeta.order = 11;
      CRM().addRuntimeMeta(humMeta);

      RuntimeFieldMeta dewMeta;
      dewMeta.group = "sensors";
      dewMeta.key = "dew";
      dewMeta.label = "Dewpoint";
      dewMeta.unit = "°C";
      dewMeta.precision = 1;
      dewMeta.order = 12;
      CRM().addRuntimeMeta(dewMeta);

      RuntimeFieldMeta pressureMeta;
      pressureMeta.group = "sensors";
      pressureMeta.key = "pressure";
      pressureMeta.label = "Pressure";
      pressureMeta.unit = "hPa";
      pressureMeta.precision = 1;
      pressureMeta.order = 13;
      CRM().addRuntimeMeta(pressureMeta);


      RuntimeFieldMeta rangeMeta;
      rangeMeta.group = "sensors";
      rangeMeta.key = "range";
      rangeMeta.label = "Sensor Range";
      rangeMeta.unit = "V";
      rangeMeta.precision = 1;
      rangeMeta.order = 14;
      CRM().addRuntimeMeta(rangeMeta);
    // endregion sensor fields


  //region Limiter
      CRM().addRuntimeProvider("Limiter", [](JsonObject &data)
      {
          // Apply precision to sensor values to reduce JSON size
          data["gridIn"] = currentGridImportW;
          data["invSet"] = inverterSetValue;
          data["enabled"] = limiterSettings.enableController.get();
      });

      // Define sensor display fields using addRuntimeMeta
      RuntimeFieldMeta gridInMeta;
      gridInMeta.group = "Limiter";
      gridInMeta.key = "gridIn";
      gridInMeta.label = "Grid Import";
      gridInMeta.unit = "W";
      gridInMeta.precision = 0;
      gridInMeta.order = 1;
      CRM().addRuntimeMeta(gridInMeta);

      RuntimeFieldMeta invSetMeta;
      invSetMeta.group = "Limiter";
      invSetMeta.key = "invSet";
      invSetMeta.label = "Inverter Setpoint";
      invSetMeta.unit = "W";
      invSetMeta.precision = 0;
      invSetMeta.order = 2;
      CRM().addRuntimeMeta(invSetMeta);

      RuntimeFieldMeta limiterEnabledMeta;
      limiterEnabledMeta.group = "Limiter";
      limiterEnabledMeta.key = "enabled";
      limiterEnabledMeta.label = "Limiter Enabled";
      limiterEnabledMeta.isBool = true;
      limiterEnabledMeta.order = 3;
      CRM().addRuntimeMeta(limiterEnabledMeta);
  //endregion Limiter


  //region relay outputs
      CRM().addRuntimeProvider("Outputs", [](JsonObject &data)
      {
          data["ventilator"] = Relays::getVentilator();
          data["heater"] = Relays::getHeater();
          data["dewpoint_risk"] = dewpointRiskActive;
      });

      RuntimeFieldMeta ventilatorMeta;
      ventilatorMeta.group = "Outputs";
      ventilatorMeta.key = "ventilator";
      ventilatorMeta.label = "Ventilator Relay Active";
      ventilatorMeta.isBool = true;
      ventilatorMeta.order = 1;
      CRM().addRuntimeMeta(ventilatorMeta);

      RuntimeFieldMeta heaterMeta;
      heaterMeta.group = "Outputs";
      heaterMeta.key = "heater";
      heaterMeta.label = "Heater Relay Active";
      heaterMeta.isBool = true;
      heaterMeta.order = 2;
      CRM().addRuntimeMeta(heaterMeta);

      RuntimeFieldMeta dewpointRiskMeta;
      dewpointRiskMeta.group = "Outputs";
      dewpointRiskMeta.key = "dewpoint_risk";
      dewpointRiskMeta.label = "Dewpoint Risk";
      dewpointRiskMeta.isBool = true;
      dewpointRiskMeta.order = 3;
      CRM().addRuntimeMeta(dewpointRiskMeta);

      static bool heaterState = false;
      cfg.defineRuntimeCheckbox("Outputs", "heater", "Heater", []()
        {
            return heaterState;
        }, [](bool state)
        {
            heaterState = Relays::getHeater();
            Relays::setHeater(state);
        }, "", 21);

      static bool ventilatorState = false;
      cfg.defineRuntimeCheckbox("Outputs", "ventilator", "Ventilator", []()
        {
            return ventilatorState;
        }, [](bool state)
        {
            ventilatorState = Relays::getVentilator();
            Relays::setVentilator(state);
        }, "", 22);

        // Alarm: temperature is close to dewpoint (risk of condensation)
        cfg.defineRuntimeAlarm(
          "Outputs",
          "dewpoint_risk",
          "Dewpoint Risk",
          [](){
            return (temperature - Dewpoint) <= 1.2f;
          },
          [](){
            dewpointRiskActive = true;
            sl->Printf("[ALARM] Dewpoint risk ENTER").Debug();
            EvaluateHeater(temperature);
          },
          [](){
            dewpointRiskActive = false;
            sl->Printf("[ALARM] Dewpoint risk EXIT").Debug();
            EvaluateHeater(temperature);
          }
        );
  //endregion relay outputs

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
    sl->Printf("MQTT Hostname: %s", mqttSettings.publishTopicBase.get().c_str()).Debug();
    sl->Printf("MQTT Server: %s", mqttSettings.mqtt_server.get().c_str()).Debug();
    sl->Printf("MQTT Port: %d", mqttSettings.mqtt_port.get()).Debug();
    sl->Printf("MQTT User: %s", mqttSettings.mqtt_username.get().c_str()).Debug();
    // sl->Printf("MQTT Password: %s", mqttSettings.mqtt_password.get().c_str()).Debug();
    sl->Printf("MQTT Password: ***").Debug();
    sl->Printf("MQTT Sensor Power Usage Topic: %s", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str()).Debug();

    client.connect(mqttSettings.publishTopicBase.get().c_str(), mqttSettings.mqtt_username.get().c_str(), mqttSettings.mqtt_password.get().c_str()); // Connect to the MQTT broker
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
    if (currentGridImportW > 0)
    {
      currentGridImportW = currentGridImportW - 10;
      // inverterSetValue = powerSmoother.smooth(currentGridImportW);
      inverterSetValue = powerSmoother->smooth(currentGridImportW);
    }
    // currentGridImportW
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
      sl->Printf("[SUCCESS] Subscribed to topic: [%s]\n", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str());
    }
    else
    {
      sl->Printf("[ERROR] Subscription to topic [%s] failed!", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str());
    }
  }
}

void publishToMQTT()
{
  if (client.connected())
  {
    sl->Printf("--> MQTT: Topic[%s] -> [%d]", mqttSettings.mqtt_publish_setvalue_topic.c_str(), inverterSetValue).Debug();
    client.publish(mqttSettings.mqtt_publish_setvalue_topic.c_str(), String(inverterSetValue).c_str());
    client.publish(mqttSettings.mqtt_publish_getvalue_topic.c_str(), String(currentGridImportW).c_str());
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

    currentGridImportW = messageTemp.toInt();
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
  // inverterSetValue = powerSmoother.smooth(currentGridImportW);
  inverterSetValue = powerSmoother->smooth(currentGridImportW);
  // (legacy) previously: if (generalSettings.enableController.get())
  if (limiterSettings.enableController.get())
  {
    // rs485.sendToRS485(static_cast<uint16_t>(inverterSetValue));
  // legacy comment: powerSmoother.setCorrectionOffset(generalSettings.inputCorrectionOffset.get());
  powerSmoother->setCorrectionOffset(limiterSettings.inputCorrectionOffset.get()); // apply the correction offset to the smoother, if needed
    sendToRS485(static_cast<uint16_t>(inverterSetValue));
    sl->Printf("controller is enabled! Set inverter to %d W", inverterSetValue).Debug();
  }
  else
  {
    sl->Debug("Controller is disabled.");
    sl->Debug("Using MAX output.");
    sll->Debug("Limiter is disabled.");
    sll->Debug("Using MAX output.");
    sendToRS485(limiterSettings.maxOutput.get()); // send the maxOutput to the RS485 module
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
    sl->Printf("[MAIN] Received on Serial2!").Debug();
  }
}

void SetupCheckForResetButton()
{

  // check for pressed reset button

  if (digitalRead(buttonSettings.resetDefaultsPin.get()) == LOW)
  {
  sl->Internal("Reset button pressed -> Reset all settings...");
  sll->Internal("Reset button pressed!");
  sll->Internal("Reset all settings!");
    cfg.clearAllFromPrefs(); // Clear all settings from EEPROM
    delay(10000);            // Wait for 10 seconds to avoid multiple resets
  systemSettings.unconfigured.set(true); // Set the unconfigured flag to true
    cfg.saveAll();           // Save the default settings to EEPROM
    delay(10000);            // Wait for 10 seconds to avoid multiple resets
    ESP.restart();           // Restart the ESP32
  }
}

void SetupCheckForAPModeButton()
{
  String APName = APMODE_SSID;
  String pwd = APMODE_PASSWORD; // Default AP password

  // if (wifiSettings.wifiSsid.get().length() == 0 || systemSettings.unconfigured.get())
  if (wifiSettings.wifiSsid.get().length() == 0 )
  {
  sl->Printf("[WARNING] SETUP: WiFi SSID is empty [%s] (fresh/unconfigured)", wifiSettings.wifiSsid.get().c_str()).Error();
    cfg.initializeEncryption();
    cfg.startAccessPoint(APName, pwd);
    delay(250);
    logNetworkIpInfo("AP started (no SSID)");
  systemSettings.unconfigured.set(false); // Set the unconfigured flag to false after starting the access point
    cfg.saveAll(); // Save the settings to EEPROM
  }

  // check for pressed AP mode button

  if (digitalRead(buttonSettings.apModePin.get()) == LOW)
  {
  sl->Internal("AP mode button pressed -> starting AP mode...");
  sll->Internal("AP mode button!");
  sll->Internal("-> starting AP mode...");
    cfg.initializeEncryption();
    cfg.startAccessPoint(APName, pwd);
    delay(250);
    logNetworkIpInfo("AP started (button)");
  }
}

void SetupStartTemperatureMeasuring()
{
  // init BME280 for temperature and humidity sensor
  bme280.setAddress(BME280_ADDRESS, i2cSettings.sdaPin.get(), i2cSettings.sclPin.get());
  bool isStatus = bme280.begin(
      bme280.BME280_STANDBY_0_5,
      bme280.BME280_FILTER_16,
      bme280.BME280_SPI3_DISABLE,
      bme280.BME280_OVERSAMPLING_2,
      bme280.BME280_OVERSAMPLING_16,
      bme280.BME280_OVERSAMPLING_1,
      bme280.BME280_MODE_NORMAL);
  if (!isStatus) {
    sl->Printf("can NOT initialize for using BME280.").Debug();
    sll->Printf("No BME280 detected!").Debug();
  }
  else {
    sl->Printf("BME280 ready. Start measurement ticker...").Debug();
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
    cfg.defineRuntimeField("sensors", "temp", "Temperature", "°C", -20.0f, 60.0f);
    cfg.defineRuntimeField("sensors", "hum", "Humidity", "%", 0.0f, 100.0f);
    cfg.defineRuntimeField("sensors", "dew", "Dewpoint", "°C", -20.0f, 60.0f);
    cfg.defineRuntimeField("sensors", "Pressure", "Pressure", "hPa", 800.0f, 1200.0f);

    temperatureTicker.attach(tempSettings.readIntervalSec.get(), readBme280); // Attach the ticker to read BME280
    readBme280(); // initial read
  }
}

bool SetupStartWebServer()
{
  sl->Printf("[WARNING] SETUP: Starting web server...").Debug();
  sll->Printf("Starting Webserver...!").Debug();

  if (wifiSettings.wifiSsid.get().length() == 0)
  {
    sl->Printf("No SSID! --> Start AP!").Debug();
    sll->Printf("No SSID!").Debug();
    sll->Printf("Start AP!").Debug();
    cfg.initializeEncryption();
    cfg.startAccessPoint(APMODE_SSID, APMODE_PASSWORD);
    delay(1000);
    logNetworkIpInfo("SetupStartWebServer");
    return true; // Skip webserver setup if no SSID is set
  }

  if (WiFi.getMode() == WIFI_AP) {
    sl->Printf("[INFO] Running in AP mode.");
    sll->Printf("Running in AP mode.");
    logNetworkIpInfo("SetupStartWebServer");
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
      IPAddress staticIP;
      IPAddress gateway;
      IPAddress subnet;

      bool ipOk = staticIP.fromString(wifiSettings.staticIp.get());
      bool gwOk = gateway.fromString(wifiSettings.gateway.get());
      bool snOk = subnet.fromString(wifiSettings.subnet.get());

      if (!ipOk || !gwOk || !snOk)
      {
        sl->Printf("[ERROR] Invalid static IP configuration. Falling back to DHCP.\n");
        cfg.startWebServer(wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
      }
      else
      {
        cfg.startWebServer(staticIP, gateway, subnet, wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
      }
    }
    // cfg.reconnectWifi();
    WiFi.setSleep(false);
    delay(1000);
  }
  logNetworkIpInfo("Web server started");
  if (WiFi.status() == WL_CONNECTED)
  {
    sl->Printf("\n\nWebserver running at: %s\n", WiFi.localIP().toString().c_str());
    sll->Printf("Web: %s\n\n", WiFi.localIP().toString().c_str());
    sl->Printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    sl->Printf("WiFi RSSI quality: %s\n\n", WiFi.RSSI() > -70 ? "good" : (WiFi.RSSI() > -80 ? "ok" : "weak"));
    sll->Printf("WiFi: %s\n", WiFi.RSSI() > -70 ? "good" : (WiFi.RSSI() > -80 ? "ok" : "weak"));
  }




  return true; // Webserver setup completed
}

static float computeDewPoint(float temperatureC, float relHumidityPct) {
    if (isnan(temperatureC) || isnan(relHumidityPct)) return NAN;
  if (relHumidityPct <= 0.0f) relHumidityPct = 0.1f;       // Avoid divide-by-zero
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

  temperature = bme280.data.temperature + tempSettings.tempCorrection.get(); // apply correction
  Humidity = bme280.data.humidity + tempSettings.humidityCorrection.get();   // apply correction
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
  // Feature & battery-save guards
  if(!heaterSettings.enabled.get()){
    if(Relays::getHeater()){
      sl->Debug("HeaterControl: disabled or battery-save active -> force OFF");
    }
    Relays::setHeater(false);
    return;
  }
  Relays::setHeater(heaterOn);
}

// Limiter provider moved into setup() for clarity

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

  // When running in AP mode, show connection info prominently.
  if (WiFi.getMode() == WIFI_AP && WiFi.status() != WL_CONNECTED)
  {
    const IPAddress apIp = WiFi.softAPIP();
    const String apSsid = WiFi.softAPSSID();

    display.setCursor(3, 3);
    display.printf("AP: %s", apIp.toString().c_str());
    display.setCursor(3, 13);
    display.printf("SSID: %s", apSsid.c_str());
    display.display();
    return;
  }

  display.setCursor(3, 3);
  if (temperature > 0)
  {
    display.printf("<- %d W|Temp: %2.1f", currentGridImportW, temperature);
  }
  else
  {
    display.printf("<- %d W", currentGridImportW);
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
  pinMode(buttonSettings.resetDefaultsPin.get(), INPUT_PULLUP);
  pinMode(buttonSettings.apModePin.get(), INPUT_PULLUP);
  Relays::initPins();
  // Force known OFF state
  Relays::setVentilator(false);
  Relays::setHeater(false);
  // Simple validation: warn if same pin used for both or invalid
  int fanPin = fanSettings.relayPin.get();
  int heaterPin = heaterSettings.relayPin.get();
  if(fanPin == heaterPin && heaterSettings.enabled.get()){
    sl->Error("Relay config: Fan and Heater share same GPIO! This may cause conflicts.");
  }
}

void CheckVentilator(float currentTemperature)
{
  if (!fanSettings.enabled.get()) {
    Relays::setVentilator(false);
    return;
  }
  if (currentTemperature >= fanSettings.onThreshold.get()) {
    Relays::setVentilator(true);
  } else if (currentTemperature <= fanSettings.offThreshold.get()) {
    Relays::setVentilator(false);
  }
}

void EvaluateHeater(float currentTemperature){

  if(dewpointRiskActive){
    heaterLatchedState = true;
  }

  if(heaterSettings.enabled.get()){
    // Priority 4: Threshold hysteresis (normal mode)
    float onTh = heaterSettings.onTemp.get();
    float offTh = heaterSettings.offTemp.get();
    if(offTh <= onTh) offTh = onTh + 0.3f; // enforce separation

    if(currentTemperature < onTh){
      heaterLatchedState = true;
    } 
    if(currentTemperature > offTh){
      heaterLatchedState = false;
    }

  }
  Relays::setHeater(heaterLatchedState);
}

void CheckButtons()
{
  // sl->Debug("Check Buttons...");
  if (digitalRead(buttonSettings.resetDefaultsPin.get()) == LOW)
  {
    sl->Internal("Reset-Button pressed after reboot... -> Start Display Ticker...");
    ShowDisplay();
  }

  if (digitalRead(buttonSettings.apModePin.get()) == LOW)
  {
    sl->Internal("AP-Mode-Button pressed after reboot... -> Start Display Ticker...");
    ShowDisplay();
  }
}

void ShowDisplay()
{
  displayTicker.detach(); // Stop the ticker to prevent multiple calls
  display.ssd1306_command(SSD1306_DISPLAYON); // Turn on the display
  displayTicker.attach(displaySettings.onTimeSec.get(), ShowDisplayOff); // Reattach the ticker to turn off the display after the specified time
  displayActive = true;
}

void ShowDisplayOff()
{
  displayTicker.detach(); // Stop the ticker to prevent multiple calls
  display.ssd1306_command(SSD1306_DISPLAYOFF); // Turn off the display
  // display.fillRect(0, 0, 128, 24, BLACK); // Clear the previous message area

  if (displaySettings.turnDisplayOff.get()){
    displayActive = false;
  }
}