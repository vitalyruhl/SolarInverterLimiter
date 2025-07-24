#include <Arduino.h>
#include <PubSubClient.h> // for MQTT
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadische Funktionen (printf-Stil)
#include <Ticker.h>
#include "Wire.h"
#include <BME280_I2C.h>
#include <WebServer.h>

#include "settings.h"
#include "logging/logging.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include "Smoother/Smoother.h"

/*
Todo:
mqtt reconnect on display
*/

// predefine the functions
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
// void CalibrateJoystick();
// void ReadJoystick();
// void SetRalays(int x, int y);
void CheckVentilator(float aktualTemperature);
// void readLDRs();
//--------------------------------------------------------------------------------------------------------------

#pragma region configuratio variables

BME280_I2C bme280;

Helpers helpers;

Ticker PublischMQTTTicker;
Ticker PublischMQTTTSettingsTicker;
Ticker ListenMQTTTicker;
Ticker RS485Ticker;
Ticker temperatureTicker;

WiFiClient espClient;
PubSubClient client(espClient);

// Smoothing Values
Smoother powerSmoother(
    generalSettings.smoothingSize.get(),
    generalSettings.inputCorrectionOffset.get(),
    generalSettings.minOutput.get(),
    generalSettings.maxOutput.get());

// globale helpers variables
int AktualImportFromGrid = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)
float temperature = 0.0;      // current temperature in Celsius
float Dewpoint = 0.0;         // current dewpoint in Celsius
float Humidity = 0.0;         // current humidity in percent
bool tickerActive = false;    // flag to indicate if the ticker is active

#pragma endregion configuration variables

//----------------------------------------
// MAIN FUNCTIONS
//----------------------------------------

void setup()
{

  LoggerSetupSerial(); // Initialize the serial logger
  sl->Printf("System setup start...").Debug();
  
  cfg.loadAll();
  PinSetup();

  // init modules...
  SetupStartDisplay();
  SetupCheckForResetButton();
  SetupCheckForAPModeButton();
  // CalibrateJoystick();

  helpers.blinkBuidInLEDsetpinMode(); // Initialize the built-in LED pin mode
  helpers.blinkBuidInLED(3, 100);     // Blink the built-in LED 3 times with a 100ms delay

  powerSmoother.fillBufferOnStart(generalSettings.minOutput.get());

  sl->Printf("Configuration printout:").Debug();
  // sl->Printf("/s", cfg.toJSON(false)).Debug();
  Serial.println(cfg.toJSON(false)); // Print the configuration to the serial monitor
  // testRS232();

  //------------------
  sl->Printf("⚠️ SETUP: Starting RS485...!").Debug();
  sll->Printf("Starting RS485...").Debug();
  RS485begin();

  sl->Printf("SETUP: Check and start BME280!").Debug();
  sll->Printf("Check and start BME280!").Debug();
  SetupStartTemperatureMeasuring();

  //----------------------------------------

  bool isStartedAsAP = SetupStartWebServer();

  //----------------------------------------
  // -- Setup MQTT connection --
  sl->Printf("⚠️ SETUP: Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  sll->Printf("Starting MQTT! [%s]", mqttSettings.mqtt_server.get().c_str()).Debug();
  client.setServer(mqttSettings.mqtt_server.get().c_str(), static_cast<uint16_t>(mqttSettings.mqtt_port.get())); // Set the MQTT server and port
  client.setCallback(cb_MQTT);

  sl->Debug("Attaching tickers...");
  sll->Debug("Attaching tickers...");
  PublischMQTTTicker.attach(generalSettings.MQTTPublischPeriod.get(), cb_PublishToMQTT);
  ListenMQTTTicker.attach(generalSettings.MQTTListenPeriod.get(), cb_MQTTListener);
  RS485Ticker.attach(generalSettings.RS232PublishPeriod.get(), cb_RS485Listener);
  tickerActive = true; // Set the flag to indicate that the ticker is active
  reconnectMQTT();     // connect to MQTT broker
  sl->Debug("System setup completed.");
  sll->Debug("Setup completed.");
}

void loop()
{
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
      sl->Debug("WiFi not connected or in AP mode! deactivate ticker.");
      sll->Debug("WiFi lost connection!");
      sll->Debug("or run in AP mode!");
      sll->Debug("deactivate mqtt ticker.");
      PublischMQTTTicker.detach(); // Stop the ticker if WiFi is not connected or in AP mode
      ListenMQTTTicker.detach();   // Stop the ticker if WiFi is not connected or in AP mode
      tickerActive = false;        // Set the flag to indicate that the ticker is not active
    }
  }
  else
  {
    if (!tickerActive) // Check if the ticker is not already active
    {
      sl->Debug("WiFi connected! Reattach ticker.");
      sll->Debug("WiFi reconnected!");
      sll->Debug("Reattach ticker.");
      PublischMQTTTicker.attach(generalSettings.MQTTPublischPeriod.get(), cb_PublishToMQTT); // Reattach the ticker if WiFi is connected
      ListenMQTTTicker.attach(generalSettings.MQTTListenPeriod.get(), cb_MQTTListener);      // Reattach the ticker if WiFi is connected
      tickerActive = true;                                                                   // Set the flag to indicate that the ticker is active
    }
  }

  WriteToDisplay();

  if (WiFi.getMode() == WIFI_AP)
  {
    helpers.blinkBuidInLED(5, 50); // show we are in AP mode
    sll->Debug("or run in AP mode!");
  }

  else
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      sl->Debug("❌ WiFi not connected!");
      sll->Debug("reconnect to WiFi...");
      cfg.reconnectWifi();
      delay(1000);
      return;
    }
    // blinkBuidInLED(1, 100); // not used here, because blinker is used if we get a message from MQTT
  }

  cfg.handleClient();

  if (!client.connected())
  {
    // logv("MQTT Not Connected!");
    reconnectMQTT();
  }

  // ReadJoystick(); // Read the joystick input
  CheckVentilator(temperature);
  // readLDRs();
  delay(10);
}

void yield()
{
  // This function is called to yield control to other tasks
  // It can be used to prevent watchdog timer resets
  delay(1);
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
    sl->Printf("MQTT Hostname: %s", mqttSettings.mqtt_hostname.c_str()).Debug();
    sl->Printf("MQTT Server: %s", mqttSettings.mqtt_server.get().c_str()).Debug();
    sl->Printf("MQTT Port: %d", mqttSettings.mqtt_port.get()).Debug();
    sl->Printf("MQTT User: %s", mqttSettings.mqtt_username.get().c_str()).Debug();
    // sl->Printf("MQTT Password: %s", mqttSettings.mqtt_password.get().c_str()).Debug();
    sl->Printf("MQTT Password: ***").Debug();
    sl->Printf("MQTT Sensor Power Usage Topic: %s", mqttSettings.mqtt_sensor_powerusage_topic.get().c_str()).Debug();

    client.connect(mqttSettings.mqtt_hostname.c_str(), mqttSettings.mqtt_username.get().c_str(), mqttSettings.mqtt_password.get().c_str()); // Connect to the MQTT broker
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
      inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
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
  inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
  if (generalSettings.enableController.get())
  {
    // rs485.sendToRS485(static_cast<uint16_t>(inverterSetValue));
    powerSmoother.setCorrectionOffset(generalSettings.inputCorrectionOffset.get()); // apply the correction offset to the smoother, if needed
    sendToRS485(static_cast<uint16_t>(inverterSetValue));
  }
  else
  {
    sl->Debug("controller is didabled!");
    sl->Debug("MAX-POWER!");
    sll->Debug("Limiter is didabled!");
    sll->Debug("MAX-POWER!");
    sendToRS485(generalSettings.maxOutput.get()); // send the maxOutput to the RS485 module
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

  if (wifiSettings.wifiSsid.get().length() == 0 || generalSettings.unconfigured.get())
  {
    sl->Printf("⚠️ SETUP: wifiSsid.get() ist empty! [%s]", wifiSettings.wifiSsid.get().c_str()).Error();
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
    temperatureTicker.attach(ReadTemperatureTicker, readBme280); // Attach the ticker to read BME280 every 5 seconds
    readBme280();                                                // Read the BME280 sensor data once at startup
  }
}

bool SetupStartWebServer()
{

  if (wifiSettings.wifiSsid.get().length() == 0)
  {
    sl->Printf("No SSID!").Debug();
    sll->Printf("No SSID!").Debug();
    sll->Printf("Start AP!").Debug();
    cfg.startAccessPoint();
  }

  if (WiFi.getMode() == WIFI_AP)
  {
    sl->Printf("🖥️ Run in AP Mode! ");
    sll->Printf("Run in AP Mode! ");
    return false; // Skip webserver setup in AP mode
  }

  cfg.reconnectWifi();
  delay(1000);

  if (wifiSettings.useDhcp.get())
  {
    sl->Printf("DHCP enabled");
    cfg.startWebServer(wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
  }
  else
  {
    sl->Printf("DHCP disabled");
    cfg.startWebServer("192.168.2.122", "255.255.255.0", "192.168.2.250", wifiSettings.wifiSsid.get(), wifiSettings.wifiPassword.get());
  }
  sl->Printf("🖥️ Webserver running at: %s", WiFi.localIP().toString().c_str());
  sll->Printf("Web: %s", WiFi.localIP().toString().c_str());
  return true; // Webserver setup completed
}

void readBme280()
{
  // todo: add settings for correcting the values!!!
  //   set sea-level pressure
  bme280.setSeaLevelPressure(1010);

  bme280.read();

  temperature = bme280.data.temperature + generalSettings.TempCorrectionOffset.get(); // store the temperature value in the global variable
  Humidity = bme280.data.humidity + generalSettings.HumidityCorrectionOffset.get();   // store the temperature value in the global variable

  // calculate drewpoint
  //  Dewpoint = T - ((100 - RH) / 5.0)
  Dewpoint = bme280.data.temperature - ((100 - bme280.data.humidity) / 5.0);

  // output formatted values to serial console
  sl->Printf("-----------------------").Debug();
  sl->Printf("Temperature: %2.1lf °C", temperature).Debug();
  sl->Printf("Humidity   : %2.1lf %rH", Humidity).Debug();
  sl->Printf("Dewpoint   : %2.1lf °C", Dewpoint).Debug();
  sl->Printf("Pressure   : %4.0lf hPa", bme280.data.pressure).Debug();
  sl->Printf("Altitude   : %4.2lf m", bme280.data.altitude).Debug();
  sl->Printf("-----------------------").Debug();
}

void WriteToDisplay()
{
  // display.clearDisplay();
  display.fillRect(0, 0, 128, 24, BLACK); // Clear the previous message area
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

  // pinMode(RELAY_MOTOR_UP_PIN, OUTPUT);
  // digitalWrite(RELAY_MOTOR_UP_PIN, HIGH); // set the relay to HIGH (off) at startup

  // pinMode(RELAY_MOTOR_DOWN_PIN, OUTPUT);
  // digitalWrite(RELAY_MOTOR_DOWN_PIN, HIGH); // set the relay to HIGH (off) at startup

  // pinMode(RELAY_MOTOR_LEFT_PIN, OUTPUT);
  // digitalWrite(RELAY_MOTOR_LEFT_PIN, HIGH); // set the relay to HIGH (off) at startup

  // pinMode(RELAY_MOTOR_RIGHT_PIN, OUTPUT);
  // digitalWrite(RELAY_MOTOR_RIGHT_PIN, HIGH); // set the relay to HIGH (off) at startup

  pinMode(RELAY_VENTILATOR_PIN, OUTPUT);
  digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // set the ventilator relay to HIGH (off) at startup
}

// void CalibrateJoystick()
// {
//   // Read the current raw joystick values
//   int rawX = analogRead(JOYSTICK_X_PIN);
//   int rawY = analogRead(JOYSTICK_Y_PIN);

//   // Store the offset to subtract later
//   generalSettings.XjoystickOffset.set(rawX);
//   generalSettings.YjoystickOffset.set(rawY);

//   int joystickOffsetX = generalSettings.XjoystickOffset.get();
//   int joystickOffsetY = generalSettings.YjoystickOffset.get();

//   sl->Printf("Joystick calibrated: OffsetX = %d, OffsetY = %d", joystickOffsetX, joystickOffsetY).Debug();
// }

// void ReadJoystick()
// {
//   int rawX = analogRead(JOYSTICK_X_PIN);
//   int rawY = analogRead(JOYSTICK_Y_PIN);
//   // sl->Printf("Joystick X: %04d, Y: %04d", rawX, rawY).Debug();

//   int joystickOffsetX = generalSettings.XjoystickOffset.get();
//   int joystickOffsetY = generalSettings.YjoystickOffset.get();
//   // Subtract offset to center the joystick
//   int adjustedX = rawX - joystickOffsetX;
//   int adjustedY = rawY - joystickOffsetY;

//   // Map adjusted values to -100 to 100
//   int mappedX = map(adjustedX, -joystickOffsetX, 4095 - joystickOffsetX, -100, 100);
//   int mappedY = map(adjustedY, -joystickOffsetY, 4095 - joystickOffsetY, -100, 100);

//   // sl->Printf("Joystick X: %04d, Y: %04d", mappedX, mappedY).Debug();

//   SetRalays(mappedX, mappedY); // Set the relays based on joystick input
// }

// void SetRalays(int x, int y)
// {
//   int Threshold = generalSettings.OnOffThreshold.get(); // Threshold to determine if the joystick is moved enough to activate relays
//   // Set the motor direction based on joystick input
//   if (y > Threshold) // Joystick up
//   {
//     digitalWrite(RELAY_MOTOR_UP_PIN, LOW);    // Activate UP relay
//     digitalWrite(RELAY_MOTOR_DOWN_PIN, HIGH); // Deactivate DOWN relay
//   }
//   else if (y < -Threshold) // Joystick down
//   {
//     digitalWrite(RELAY_MOTOR_UP_PIN, HIGH);  // Deactivate UP relay
//     digitalWrite(RELAY_MOTOR_DOWN_PIN, LOW); // Activate DOWN relay
//   }
//   else
//   {
//     digitalWrite(RELAY_MOTOR_UP_PIN, HIGH);   // Deactivate UP relay
//     digitalWrite(RELAY_MOTOR_DOWN_PIN, HIGH); // Deactivate DOWN relay
//   }

//   if (x > Threshold) // Joystick right
//   {
//     digitalWrite(RELAY_MOTOR_RIGHT_PIN, LOW); // Activate RIGHT relay
//     digitalWrite(RELAY_MOTOR_LEFT_PIN, HIGH); // Deactivate LEFT relay
//   }
//   else if (x < -Threshold) // Joystick left
//   {
//     digitalWrite(RELAY_MOTOR_RIGHT_PIN, HIGH); // Deactivate RIGHT relay
//     digitalWrite(RELAY_MOTOR_LEFT_PIN, LOW);   // Activate LEFT relay
//   }
//   else
//   {
//     digitalWrite(RELAY_MOTOR_RIGHT_PIN, HIGH); // Deactivate RIGHT relay
//     digitalWrite(RELAY_MOTOR_LEFT_PIN, HIGH);  // Deactivate LEFT relay
//   }
// }

void CheckVentilator(float aktualTemperature)
{
  // Check if ventilator control is enabled
  if (!generalSettings.VentilatorEnable.get())
  {
    digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // Deactivate ventilator relay if control is disabled
    return;                                   // Exit if ventilator control is disabled
  }

  // Check if the temperature exceeds the ON threshold
  if (aktualTemperature >= generalSettings.VentilatorOn.get())
  {
    digitalWrite(RELAY_VENTILATOR_PIN, LOW); // Activate ventilator relay
  }
  else if (aktualTemperature <= generalSettings.VentilatorOFF.get())
  {
    digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // Deactivate ventilator relay
  }
}

// void readLDRs()
// {
//   // Read the LDR values
//     // Read all sensors
//   int ldr1 = analogRead(LDR_PIN1);
//   int ldr2 = analogRead(LDR_PIN2);
//   int ldr3 = analogRead(LDR_PIN3);
//   int ldr4 = analogRead(LDR_PIN4);


//   // print the raw LDR values to the serial monitor
//   // sl->Printf("LDR1: %04d, LDR2: %04d, LDR3: %04d, LDR4: %04d", ldr1, ldr2, ldr3, ldr4).Debug();

//   ldrSettings.ldr1.set(ldr1);
//   ldrSettings.ldr2.set(ldr2);
//   ldrSettings.ldr3.set(ldr3);
//   ldrSettings.ldr4.set(ldr4);

//    int verticalDiff = (ldr1 + ldr2) - (ldr3 + ldr4);   // Top vs Bottom
//   int horizontalDiff = (ldr1 + ldr3) - (ldr2 + ldr4);  // Left vs Right

//     // Calculate total light for normalization
//   int totalLight = ldr1 + ldr2 + ldr3 + ldr4;

//  // Calculate percentages (-100 to 100)
//   int x = 0, y = 0;
//   if (totalLight > 0) {  // Prevent division by zero
//     x = constrain((horizontalDiff * 100) / totalLight, -100, 100);
//     y = constrain((verticalDiff * 100) / totalLight, -100, 100);
//   }

//   // Debug output
//   // sl->Printf("X: %04d Y: %04d | Raw: %04d %04d %04d %40d", x, y, ldr1, ldr2, ldr3, ldr4).Debug();

  
//    SetRalays(x, y); // Set the relays based on joystick input
// }