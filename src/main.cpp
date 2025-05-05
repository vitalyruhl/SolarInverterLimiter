#include <Arduino.h>
#include <PubSubClient.h> // for MQTT
#include <ArduinoJson.h>  // for JSON parsing
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadische Funktionen (printf-Stil)
#include <Ticker.h>
#include "Wire.h"
#include <BME280_I2C.h>

#include "config/config.h"
#include "config/settings.h"
#include "logging/logging.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include "WiFiManager/WiFiManager.h"
#include "Smoother/Smoother.h"

/*
Todo:

*/

// predefine the callback function for MQTT messages
void reconnectMQTT();
void cb_MQTT(char *topic, byte *message, unsigned int length);
void publishToMQTT();
void cb_BlinkerPublishToMQTT();
void cb_BlinkerMQTTListener();
void cb_BlinkerRS485Listener();
void testRS232();

void readBme280();
void WriteToDisplay();

void SetupCheckForResetButton();
void SetupCheckForAPModeButton();
void SetupStartTemperatureMeasuring();

#pragma region configuratio variables

BME280_I2C bme280;

Helpers helpers;
Config config; // create an instance of the config class

Ticker PublischMQTTTicker;
Ticker PublischMQTTTSettingsTicker;
Ticker ListenMQTTTicker;
Ticker RS485Ticker;
Ticker temperatureTicker;

WiFiClient espClient;
PubSubClient client(espClient);

// Smoothing Values
Smoother powerSmoother(
    config.generalSettings.smoothingSize,
    config.generalSettings.inputCorrectionOffset,
    config.generalSettings.minOutput,
    config.generalSettings.maxOutput);

WiFiManager wifiManager(&config); // create an instance of the WiFiManager class

RS485Module rs485(&config);

// globale helpers variables
int AktualImportFromGrid = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)
float temperature = 0.0;      // current temperature in Celsius
float Dewpoint = 0.0;        // current dewpoint in Celsius
bool tickerActive = false;    // flag to indicate if the ticker is active

#pragma endregion configuration variables

//----------------------------------------
// MAIN FUNCTIONS
//----------------------------------------

void setup()
{

  Serial.begin(115200); // Debug-Mode: send to serial console

  sl->Printf("System setup start...").Debug();

  // init modules...
  SetupStartDisplay();
  SetupCheckForResetButton();
  SetupCheckForAPModeButton();

  helpers.blinkBuidInLEDsetpinMode(); // Initialize the built-in LED pin mode
  helpers.blinkBuidInLED(3, 100);     // Blink the built-in LED 3 times with a 100ms delay

  config.load(); // Load the configuration from the config file

  powerSmoother.fillBufferOnStart(config.generalSettings.minOutput);

  // debugging outputs:
  // config.printSettings(); // Print the settings to the serial console
  // testRS232();
  //------------------
  sl->Printf("⚠️ SETUP: Starting RS485...!").Debug();
  sll->Printf("Starting RS485...").Debug();
  rs485.begin();

  if (wifiManager.hasAPServer())
  {
    sl->Printf("⚠️ SETUP: Wifi run in AP-Mode!").Debug();
    sll->Printf("Wifi run in AP-Mode!").Debug();
  }
  else
  {
    sl->Printf("⚠️ SETUP: Starting WiFi-Client!").Debug();
    sll->Printf("Starting WiFi-Client!").Debug();
    wifiManager.begin(); // Connect to the WiFi network using the stored SSID and password
  }

  sl->Printf("SETUP: Check and start BME280!").Debug();
  sll->Printf("Check and start BME280!").Debug();
  SetupStartTemperatureMeasuring();

  //----------------------------------------

  //----------------------------------------
  // -- Setup MQTT connection --
  sl->Printf("⚠️ SETUP: Starting MQTT! [%s]", config.mqttSettings.mqtt_server.c_str()).Debug();
  sll->Printf("Starting MQTT! [%s]", config.mqttSettings.mqtt_server.c_str()).Debug();
  client.setServer(config.mqttSettings.mqtt_server.c_str(), static_cast<uint16_t>(config.mqttSettings.mqtt_port)); // Set the MQTT server and port
  client.setCallback(cb_MQTT);

  sl->Debug("Attaching tickers...");
  sll->Debug("Attaching tickers...");
  PublischMQTTTicker.attach(config.generalSettings.MQTTPublischPeriod, cb_BlinkerPublishToMQTT);
  ListenMQTTTicker.attach(config.generalSettings.MQTTListenPeriod, cb_BlinkerMQTTListener);
  RS485Ticker.attach(config.generalSettings.RS232PublishPeriod, cb_BlinkerRS485Listener);
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

  if (WiFi.status() != WL_CONNECTED)
  {
    // sl->Debug("Wifi Not Connected!");
    // wifiManager.begin(); // Start the WiFi manager with the stored SSID and password
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
      PublischMQTTTicker.attach(config.generalSettings.MQTTPublischPeriod, cb_BlinkerPublishToMQTT); // Reattach the ticker if WiFi is connected
      ListenMQTTTicker.attach(config.generalSettings.MQTTListenPeriod, cb_BlinkerMQTTListener);      // Reattach the ticker if WiFi is connected
      tickerActive = true;                                                                           // Set the flag to indicate that the ticker is active
    }
  }

  if (!client.connected())
  {
    // logv("MQTT Not Connected!");
    reconnectMQTT();
  }

  wifiManager.handleClient();
  delay(100);
  WriteToDisplay();
}

//----------------------------------------
// MQTT FUNCTIONS
//----------------------------------------
void reconnectMQTT()
{
  if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP)
  {
    // sl->Debug("WiFi not connected or in AP mode! Skipping mqttSettings.");
    return;
  }

  int retry = 0;
  int maxRetry = 10; // max retry count

  while (!client.connected() && retry < maxRetry) // Retry 5 times before restarting
  {
    sl->Printf("MQTT reconnect attempt %d...", retry + 1).Log(level);
    helpers.blinkBuidInLED(5, 300);
    retry++;
    client.connect(config.mqttSettings.mqtt_hostname.c_str(), config.mqttSettings.mqtt_username.c_str(), config.mqttSettings.mqtt_password.c_str()); // Connect to the MQTT broker
    delay(2000);
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
    cb_BlinkerPublishToMQTT(); // publish the settings to the MQTT broker, before subscribing to the topics
    sl->Debug("trying to subscribe to topics...");
    sll->Debug("subscribe to mqtt...");

    if (client.subscribe(config.mqttSettings.mqtt_sensor_powerusage_topic.c_str()))
    {
      sl->Printf("✅ Subscribed to topic: [%s]\n", config.mqttSettings.mqtt_sensor_powerusage_topic.c_str());
    }
    else
    {
      sl->Printf("❌ Subscription to Topic[%s] failed!", config.mqttSettings.mqtt_sensor_powerusage_topic.c_str());
    }
  }
}

void publishToMQTT()
{
  if (client.connected())
  {
    sl->Printf("--> MQTT: Topic[%s] -> [%d]", config.mqttSettings.mqtt_publish_setvalue_topic.c_str(), inverterSetValue);
    client.publish(config.mqttSettings.mqtt_publish_setvalue_topic.c_str(), String(inverterSetValue).c_str());     // send to Mqtt
    client.publish(config.mqttSettings.mqtt_publish_getvalue_topic.c_str(), String(AktualImportFromGrid).c_str()); // send to Mqtt
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

  sl->Printf("<-- MQTT: Topic[%s] <-- [%s]", topic, messageTemp.c_str());
  helpers.blinkBuidInLED(1, 100); // blink the LED once to indicate that the loop is running
  if (strcmp(topic, config.mqttSettings.mqtt_sensor_powerusage_topic.c_str()) == 0)
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

void cb_BlinkerPublishToMQTT()
{
  publishToMQTT(); // send to Mqtt
}

void cb_BlinkerMQTTListener()
{
  client.loop(); // process incoming MQTT messages
}

void cb_BlinkerRS485Listener()
{
  inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
  if (config.generalSettings.enableController)
  {
    // rs485.sendToRS485(static_cast<uint16_t>(inverterSetValue));
    powerSmoother.setCorrectionOffset(config.generalSettings.inputCorrectionOffset); // apply the correction offset to the smoother, if needed
    rs485.sendToRS485(static_cast<uint16_t>(inverterSetValue));
  }
  else
  {
    sl->Debug("controller is didabled!");
    sl->Debug("MAX-POWER!");
    sll->Debug("Limiter is didabled!");
    sll->Debug("MAX-POWER!");
    rs485.sendToRS485(config.generalSettings.maxOutput); // send the maxOutput to the RS485 module
  }
}

void testRS232()
{
  // test the RS232 connection
  sl->Printf("Testing RS232 connection... shorting RX and TX pins!");
  sl->Printf("Baudrate: %d", config.rs485settings.baudRate);
  sl->Printf("RX Pin: %d", config.rs485settings.rxPin);
  sl->Printf("TX Pin: %d", config.rs485settings.txPin);
  sl->Printf("DE Pin: %d", config.rs485settings.dePin);

  Serial2.begin(config.rs485settings.baudRate, SERIAL_8N1, config.rs485settings.rxPin, config.rs485settings.txPin);
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
  pinMode(BUTTON_PIN_RESET_TO_DEFAULTS, INPUT_PULLUP); // importand: BUTTON is LOW aktiv!

  if (digitalRead(BUTTON_PIN_RESET_TO_DEFAULTS) == LOW)
  {
    sl->Internal("Reset-Button pressed... -> Resett all settings...");
    sll->Internal("Reset-Button pressed!");
    sll->Internal("Resett all settings!");
    config.removeAllSettings();
    delay(10000);  // Wait for 10 seconds to avoid multiple resets
    config.save(); // Save the default settings to EEPROM
    delay(10000);  // Wait for 10 seconds to avoid multiple resets
    ESP.restart(); // Restart the ESP32
  }
}

void SetupCheckForAPModeButton()
{

  if (config.wifi_config.ssid.length() == 0)
  {
    sl->Printf("⚠️ SETUP: config.wifi_config.ssid ist empty! [%s]", config.wifi_config.ssid.c_str()).Error();
    wifiManager.startAccessPoint(); // Start the access point mode
  }

  // check for pressed AP-Mode button
  pinMode(BUTTON_PIN_AP_MODE, INPUT_PULLUP); // importand: BUTTON is LOW aktiv!

  if (digitalRead(BUTTON_PIN_AP_MODE) == LOW)
  {
    sl->Internal("AP-Mode-Button pressed... -> Start AP-Mode...");
    sll->Internal("AP-Mode-Button!");
    sll->Internal("-> Start AP-Mode...");
    wifiManager.startAccessPoint(); // Start the access point mode
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

void readBme280()
{
  //todo: add settings for correcting the values!!!
  //  set sea-level pressure
  bme280.setSeaLevelPressure(1010);

  bme280.read();

  temperature = bme280.data.temperature; // store the temperature value in the global variable

  //calculate drewpoint
  // Dewpoint = T - ((100 - RH) / 5.0)
  Dewpoint = bme280.data.temperature - ((100 - bme280.data.humidity) / 5.0);

  // output formatted values to serial console
  sl->Printf("-----------------------").Debug();
  sl->Printf("Temperature: %2.1lf °C", bme280.data.temperature).Debug();
  sl->Printf("Humidity   : %2.1lf %rH", bme280.data.humidity).Debug();
  sl->Printf("Pressure   : %4.0lf hPa", bme280.data.pressure).Debug();
  sl->Printf("Altitude   : %4.2lf m", bme280.data.altitude).Debug();
  sl->Printf("Dewpoint   : %2.1lf °C", Dewpoint).Debug();
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
