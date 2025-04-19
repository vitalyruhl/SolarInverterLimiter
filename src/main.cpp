#include <Arduino.h>
#include "config/config.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include <PubSubClient.h> // for MQTT
#include <ArduinoJson.h>  // for JSON parsing
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadische Funktionen (printf-Stil)
#include <Ticker.h>
#include "logging/logging.h" // for logging
#include "WiFiManager/WiFiManager.h"
#include "Smoother/Smoother.h"

/*
Todo:
 - Add periodicly send MQTT Settings (every 10 minutes or so), to get Values in HomeAssistant
 - Add Secondary WiFi-SSID and Password to connect to a second WiFi if the first one is not available
 - Add WiFI-Server to get settings from a webinterface if there is no Wifi available
 - Check Sending min-Value to RS485, if it is not set, or no connections available
 - make MQTT-Hostname and other settings configurable via MQTT, and or Webinterface

*/

#pragma region configuratio variables

RS485Settings rs485settings;
RS485Module rs485;
RS485Packet rs485packet;

Helpers helpers;
GeneralSettings generalSettings;
Config config; // create an instance of the config class

Ticker PublischMQTTTicker;
Ticker PublischMQTTTSettingsTicker;
Ticker ListenMQTTTicker;
Ticker RS485Ticker;


// WiFi-Setup
WiFiManager wifiManager;
WiFiClient espClient;
Config_wifi wifiConfig; // create an instance of the wifi config class

PubSubClient client(espClient);
config_mqtt mqtt; // create an instance of the mqtt config class

// Smoothing Values
Smoother powerSmoother(
  generalSettings.smoothingSize,
  generalSettings.inputCorrectionOffset,
  generalSettings.minOutput,
  generalSettings.maxOutput
);

// globale helpers variables
int AktualImportFromGrid = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)




#pragma endregion configuration variables

// predefine the callback function for MQTT messages
void reconnectMQTT();
void cb_MQTT(char *topic, byte *message, unsigned int length);
void publishToMQTT();
void cb_BlinkerPublishToMQTT();
void cb_BlinkerMQTTListener();
void cb_BlinkerRS485Listener();
void testRS232();

//----------------------------------------
// MAIN FUNCTIONS
//----------------------------------------

void setup()
{

#if defined(ENABLE_LOGGING) || defined(ENABLE_LOGGING_VERBOSE)
  Serial.begin(9600); // Debug-Modus
#endif
  logv("System setup start...");

  // check for pressed reset button
  pinMode(BUTTON_PIN_RESET_TO_DEFAULTS, INPUT_PULLUP); // importand: BUTTON is LOW aktiv!

  if (digitalRead(BUTTON_PIN_RESET_TO_DEFAULTS) == LOW)
  {
    Serial.println("Button gedrückt beim Start! Lösche Einstellungen...");
    config.removeAllSettings();
    delay(10000);  // Wait for 10 seconds to avoid multiple resets
    ESP.restart(); // Restart the ESP32
  }

  // init modules...
  helpers.blinkBuidInLEDsetpinMode(); // Initialize the built-in LED pin mode
  helpers.blinkBuidInLED(3, 100);     // Blink the built-in LED 3 times with a 100ms delay

  config.load(); // Load the configuration from the config file

  powerSmoother.fillBufferOnStart(generalSettings.minOutput);

  config.printSettings(); // Print the settings to the serial console

  
  testRS232();

  rs485.Init(rs485settings);

  wifiManager.begin(wifiConfig.ssid, wifiConfig.pass); // Start the WiFi manager with the stored SSID and password
  wifiManager.loop(); // Run the WiFi manager loop to handle WiFi connections


  logv("rs485 --> End rs485settings");
  //----------------------------------------

  //----------------------------------------
  // -- Setup MQTT connection --
  client.setServer(mqtt.mqtt_server.c_str(), static_cast<uint16_t>(mqtt.mqtt_port)); // Set the MQTT server and port
  client.setCallback(cb_MQTT);
  generalSettings.dirtybit = true; // send the settings to the mqtt broker on startup

  log("Attaching tickers...");
  PublischMQTTTicker.attach(generalSettings.MQTTPublischPeriod, cb_BlinkerPublishToMQTT);
  ListenMQTTTicker.attach(generalSettings.MQTTListenPeriod, cb_BlinkerMQTTListener);
  RS485Ticker.attach(generalSettings.RS232PublishPeriod, cb_BlinkerRS485Listener);
  reconnectMQTT(); // connect to MQTT broker
  logv("System setup completed.");
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
    log("Wifi Not Connected!");
    wifiManager.begin(wifiConfig.ssid, wifiConfig.pass); // Start the WiFi manager with the stored SSID and password
  }

  wifiManager.loop(); // Run the WiFi manager loop to handle WiFi connections


  if (!client.connected())
  {
    log("MQTT Not Connected!");
    reconnectMQTT();
  }

  delay(100);
}

//----------------------------------------
// MQTT FUNCTIONS
//----------------------------------------
void reconnectMQTT()
{

  int retry = 0;
  int maxRetry = 10; // max retry count

  while (!client.connected() && retry < maxRetry) // Retry 5 times before restarting
  {
    log("MQTT reconnect attempt %d...", retry + 1);
    helpers.blinkBuidInLED(5, 300);
    retry++;
    client.connect(mqtt.mqtt_hostname.c_str(), mqtt.mqtt_username.c_str(), mqtt.mqtt_password.c_str()); // Connect to the MQTT broker
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
    log("MQTT reconnect failed after %d attempts. go to next loop...", maxRetry);

    // if we dont get a actual value from the grid, reduce the acktual value step by 1 up to 0
    if (AktualImportFromGrid > 0)
    {
      AktualImportFromGrid = AktualImportFromGrid - 10;
      inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
    }
    // AktualImportFromGrid
    // delay(1000);   // Wait for 1 second before restarting
    // ESP.restart(); // Restart the ESP32
    return; // exit the function if max retry reached
  }

  else
  {
    logv("MQTT connected!");
    cb_BlinkerPublishToMQTT(); // publish the settings to the MQTT broker, before subscribing to the topics
    logv("trying to subscribe to topics...");

    if (client.subscribe(mqtt.mqtt_sensor_powerusage_topic.c_str()))
    {
      log("✅ Subscribed to topic: [%s]\n", mqtt.mqtt_sensor_powerusage_topic.c_str());
    }
    else
    {
      log("❌ Subscription to Topic[%s] failed!", mqtt.mqtt_sensor_powerusage_topic.c_str());
    }
   
  }
}

void publishToMQTT()
{
  // logv(" ");
  // logv("++++++++++++++++++++++++++++++++++++++");
  if (client.connected())
  {
    log("--> MQTT: Topic[%s] -> [%d]", mqtt.mqtt_publish_setvalue_topic.c_str(), inverterSetValue);
    client.publish(mqtt.mqtt_publish_setvalue_topic.c_str(), String(inverterSetValue).c_str());     // send to Mqtt
    client.publish(mqtt.mqtt_publish_getvalue_topic.c_str(), String(AktualImportFromGrid).c_str()); // send to Mqtt
  }
  else
  {
    logv("publishToMQTT: MQTT not connected!");
  }
  // logv("++++++++++++++++++++++++++++++++++++++");
  // logv(" ");
}

void cb_MQTT(char *topic, byte *message, unsigned int length)
{
  String messageTemp((char *)message, length); // Convert byte array to String using constructor
  messageTemp.trim();                          // Remove leading and trailing whitespace

  logv(" ");
  // logv("++++++++++++++++++++++++++++++++++++++");
  log("<-- MQTT: Topic[%s] <-- [%s]", topic, messageTemp.c_str());
  helpers.blinkBuidInLED(1, 100); // blink the LED once to indicate that the loop is running
  if (strcmp(topic, mqtt.mqtt_sensor_powerusage_topic.c_str()) == 0)
  {
    // check if it is a number, if not set it to 0
    if (messageTemp.equalsIgnoreCase("null") ||
        messageTemp.equalsIgnoreCase("undefined") ||
        messageTemp.equalsIgnoreCase("NaN") ||
        messageTemp.equalsIgnoreCase("Infinity") ||
        messageTemp.equalsIgnoreCase("-Infinity"))
    {
      log("Received invalid value from MQTT: %s", messageTemp.c_str());
      messageTemp = "0";
    }

    AktualImportFromGrid = messageTemp.toInt();
  }

  // logv("++++++++++++++++++++++++++++++++++++++");
  log(" ");
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
  // logv("cb_BlinkerRS485Listener()...");
  inverterSetValue = powerSmoother.smooth(AktualImportFromGrid);
  // logv("cb_BlinkerRS485Listener(%d)...", inverterSetValue);
  if (generalSettings.enableController)
  {
    rs485.sendToRS485(rs485settings, static_cast<uint16_t>(inverterSetValue));
  }
  else
  {
    logv("controller is didabled!");
    logl("MAX-POWER!");
    // rs485.sendToRS485(rs485settings, rs485packet, generalSettings.maxOutput); // send the maxOutput to the RS485 module
    rs485.sendToRS485(rs485settings, generalSettings.maxOutput); // send the maxOutput to the RS485 module
  }
}

void testRS232()
{
  // test the RS232 connection
  logv("Testing RS232 connection... shorting RX and TX pins!");
  logv("Baudrate: %d", rs485settings.baudRate);
  logv("RX Pin: %d", rs485settings.rxPin);
  logv("TX Pin: %d", rs485settings.txPin);
  logv("DE Pin: %d", rs485settings.dePin);

  Serial2.begin(rs485settings.baudRate, SERIAL_8N1, rs485settings.rxPin, rs485settings.txPin);
  Serial2.println("Hello RS485");
  delay(300);
  if (Serial2.available())
  {
    Serial.println("Received on Serial2!");
  }
}
