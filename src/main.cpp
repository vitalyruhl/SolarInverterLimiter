#include <Arduino.h>
#include "config/config.h"
#include "RS485Module/RS485Module.h"
#include "helpers/helpers.h"
#include <WiFi.h>         // for WiFi connection
#include <PubSubClient.h> // for MQTT
#include <ArduinoJson.h>  // for JSON parsing
#include <esp_task_wdt.h> // for watchdog timer
#include <stdarg.h>       // for variadische Funktionen (printf-Stil)
#include <Ticker.h>
#include "logging/logging.h" // for logging

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
Ticker ListenMQTTTicker;
Ticker RS485Ticker;

// WiFi-Setup
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;

struct MQTTMessage // MQTT Message structure
{
  bool dirtybit; // dirty bit to indicate if the message has changed
  bool enableController;
  int maxOutput;
  int minOutput;
  int inputCorrectionOffset;
  float MQTTPublischPeriod; // seconds
  float MQTTListenPeriod;   // seconds
  float RS232PublishPeriod; // seconds
  String Version;           // version of the firmware
  String Version_Date;      // date of the firmware
};

MQTTMessage mqttMessage; // declare the variable here to make it accessible in other files
MQTTMessage jsonDoc;

// Smoothing Values
const int SMOOTHING_BUFFER_SIZE = 5;               // size of the buffer for smoothing
static int SmoothingBuffer[SMOOTHING_BUFFER_SIZE]; // make a static array to hold the last measurements
static int bufferIndex = 0;

// globale helpers variables
int AktualImportFromGrid = 0; // amount of electricity being imported from grid
int inverterSetValue = 0;     // current power inverter should deliver (default to zero)

#pragma endregion configuration variables

// predefine the callback function for MQTT messages
void setup_wifi();
void reconnectMQTT();
void cb_MQTT(char *topic, byte *message, unsigned int length);
void publishToMQTT(MQTTMessage msg);
int limiter(int usage);
int smoothValue(int actualValue);
void cb_BlinkerPublishToMQTT();
void cb_BlinkerMQTTListener();
void cb_BlinkerRS485Listener();
void testRS232();
void fillBufferOnStart(int newValue);
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

  config.loadSettings(generalSettings); // Load the configuration from the config file

  // map the settings to the jsonDoc --> MQTT message
  jsonDoc.enableController = generalSettings.enableController;
  jsonDoc.maxOutput = generalSettings.maxOutput;
  jsonDoc.minOutput = generalSettings.minOutput;
  jsonDoc.inputCorrectionOffset = generalSettings.inputCorrectionOffset;
  jsonDoc.MQTTPublischPeriod = generalSettings.MQTTPublischPeriod;
  jsonDoc.MQTTListenPeriod = generalSettings.MQTTListenPeriod;
  jsonDoc.RS232PublishPeriod = generalSettings.RS232PublishPeriod;
  jsonDoc.Version = VERSION;
  jsonDoc.Version_Date = VERSION_DATE;

  fillBufferOnStart(generalSettings.minOutput);

  setup_wifi(); // Connect to WiFi

  config.printSettings(generalSettings); // Print the settings to the serial console

  //----------------------------------------
  //  -- Initialize the RS485 module --

  rs485settings.baudRate = 4800; // Optional
  rs485settings.rxPin = 16;      // 25 Optional
  rs485settings.txPin = 17;      // 27 Optional
  // rs485settings.dePin = 4; // Optional

  // rs485packet.header(0x1234); // Optional
  // rs485packet.command(0x0033); // Optional
  testRS232();

  // logv("rs485 --> begin rs485settings");
  // logv("Testing RS232 connection... shorting RX and TX pins!");
  // logv("Baudrate: %d", rs485settings.baudRate);
  // logv("RX Pin: %d", rs485settings.rxPin);
  // logv("TX Pin: %d", rs485settings.txPin);
  // logv("DE Pin: %d", rs485settings.dePin);
  // logv("now get initialized...[rs485.Init(rs485settings);]");
  rs485.Init(rs485settings);
  logv("rs485 --> End rs485settings");
  //----------------------------------------

  //----------------------------------------
  // -- Setup MQTT connection --
  client.setServer(MQTT_SERVER, MQTT_PORT);
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
    setup_wifi();
  }

  if (!client.connected())
  {
    log("MQTT Not Connected!");
    reconnectMQTT();
  }
  delay(200);
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
    client.connect(MQTT_HOSTNAME, MQTT_USERNAME, MQTT_PASSWORD);
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
      inverterSetValue = smoothValue(AktualImportFromGrid);
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
    if (client.subscribe(MQTT_SENSOR_POWERUSAGE_TOPIC))
    {
      log("✅ Subscribed to topic: [%s]\n", MQTT_SENSOR_POWERUSAGE_TOPIC);
    }
    else
    {
      log("❌ Subscription to Topic[%s] failed!", MQTT_SENSOR_POWERUSAGE_TOPIC);
    }

    if (client.subscribe(MQTT_PUBLISH_SETTINGS))
    {
      log("✅ Subscribed to topic: [%s]\n", MQTT_PUBLISH_SETTINGS);
    }
    else
    {
      log("❌ Subscription to Topic[%s] failed!", MQTT_PUBLISH_SETTINGS);
    }

    if (client.subscribe(MQTT_PUBLISH_SAVE_SETTINGS))
    {
      log("✅ Subscribed to topic: [%s]\n", MQTT_PUBLISH_SAVE_SETTINGS);
    }
    else
    {
      log("❌ Subscription to Topic[%s] failed!", MQTT_PUBLISH_SAVE_SETTINGS);
    }
  }
}

void publishToMQTT(MQTTMessage msg)
{
  // logv(" ");
  // logv("++++++++++++++++++++++++++++++++++++++");
  if (client.connected())
  {
    // logv("publishToMQTT: MQTT connected!");
    // logv("publishToMQTT: inverterSetValue: %d", inverterSetValue);
    log("--> MQTT: Topic[%s] -> [%d]", MQTT_PUBLISH_SETVALUE_TOPIC, inverterSetValue);
    client.publish(MQTT_PUBLISH_SETVALUE_TOPIC, String(inverterSetValue).c_str());     // send to Mqtt
    client.publish(MQTT_PUBLISH_GETVALUE_TOPIC, String(AktualImportFromGrid).c_str()); // send to Mqtt

    // publish the inverter set value to the MQTT broker
    if (generalSettings.dirtybit)
    {
      JsonDocument jsonDoc;

      logv("----");
      logv("enableController: %s ->", msg.enableController ? "true" : "false");
      logv("maxOutput: %d ->", msg.maxOutput);
      logv("minOutput: %d ->", msg.minOutput);
      logv("inputCorrectionOffset: %d ->", msg.inputCorrectionOffset);
      logv("MQTTPublischPeriod: %.2f ->", msg.MQTTPublischPeriod);
      logv("MQTTListenPeriod: %.2f ->", msg.MQTTListenPeriod);
      logv("RS232PublishPeriod: %.2f ->", msg.RS232PublishPeriod);
      logv("----");

      jsonDoc["enable"] = msg.enableController;
      jsonDoc["maxOutput"] = msg.maxOutput;
      jsonDoc["minOutput"] = msg.minOutput;
      jsonDoc["Offset"] = msg.inputCorrectionOffset;
      jsonDoc["PubDelay"] = msg.MQTTPublischPeriod;
      jsonDoc["ListenDelay"] = msg.MQTTListenPeriod;
      jsonDoc["RS485Delay"] = msg.RS232PublishPeriod;
      jsonDoc["Version"] = msg.Version;
      jsonDoc["V_Date"] = msg.Version_Date;


      char buffer[200]; // max 200 bytes for for mqtt!!!!
      size_t bytesWritten = serializeJson(jsonDoc, buffer, sizeof(buffer));
      // logv("Bytes written to buffer: %d", bytesWritten);
      // logv("JSON Buffer Content:[%s]",buffer);
      // logv(buffer);
      client.publish(MQTT_PUBLISH_SETTINGS, buffer);
      generalSettings.dirtybit = false; // reset the dirty bit after sending the message

      client.publish(MQTT_PUBLISH_SAVE_SETTINGS, "false"); // send to Mqtt
    }
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
  if (strcmp(topic, MQTT_SENSOR_POWERUSAGE_TOPIC) == 0)
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
  else if (strcmp(topic, MQTT_PUBLISH_SETTINGS) == 0)
  {
    JsonDocument jsonDoc;
    deserializeJson(jsonDoc, message, length);

    generalSettings.enableController = jsonDoc["enable"];
    generalSettings.maxOutput = jsonDoc["maxOutput"];
    generalSettings.minOutput = jsonDoc["minOutput"];
    generalSettings.inputCorrectionOffset = jsonDoc["Offset"];
    generalSettings.MQTTPublischPeriod = jsonDoc["PubDelay"];
    generalSettings.MQTTListenPeriod = jsonDoc["ListenDelay"];
    generalSettings.RS232PublishPeriod = jsonDoc["RS485Delay"];

    // logv("----");
    logv("--------------------------------------------------------->");
    logv("✅ settingsvalues received from MQTT-Topic: [%s]", topic);
    logv("enableController: <--  %s", generalSettings.enableController ? "enable" : "disable");
    logv("maxOutput: <--  %d", generalSettings.maxOutput);
    logv("minOutput: <--  %d", generalSettings.minOutput);
    logv("inputCorrectionOffset: <--  %d", generalSettings.inputCorrectionOffset);
    logv("dirtybit: <--  %s", generalSettings.dirtybit ? "true" : "false");
    logv("MQTTPublischPeriod: <--  %.2f", generalSettings.MQTTPublischPeriod);
    logv("MQTTListenPeriod: <--  %.2f", generalSettings.MQTTListenPeriod);
    logv("RS232PublishPeriod: <--  %.2f", generalSettings.RS232PublishPeriod);
    logv("<---------------------------------------------------------");
  }
  else if (strcmp(topic, MQTT_PUBLISH_SAVE_SETTINGS) == 0)
  {
    config.saveSettingsFlag = messageTemp.equalsIgnoreCase("true") ? true : false; // set the saveSettingsFlag to true or false depending on the message received
    logv("saveSettingsFlag-recived: <--- %s", config.saveSettingsFlag ? "true" : "false");

    if (config.saveSettingsFlag)
    {
      client.publish(MQTT_PUBLISH_SAVE_SETTINGS, "false"); // send to Mqtt
      config.saveSettings(generalSettings);                // save the settings to the config file

      MQTTMessage msg;
      msg.enableController = generalSettings.enableController;
      msg.maxOutput = generalSettings.maxOutput;
      msg.minOutput = generalSettings.minOutput;
      msg.inputCorrectionOffset = generalSettings.inputCorrectionOffset;
      generalSettings.MQTTPublischPeriod = generalSettings.MQTTPublischPeriod;
      generalSettings.MQTTListenPeriod = generalSettings.MQTTListenPeriod;
      generalSettings.RS232PublishPeriod = generalSettings.RS232PublishPeriod;

      generalSettings.dirtybit = true; // set the dirty bit to true if the message is received

      publishToMQTT(msg); // send the settings to the mqtt broker
    }
  }
  // logv("++++++++++++++++++++++++++++++++++++++");
  log(" ");
}

//----------------------------------------
// WiFi FUNCTIONS
//----------------------------------------

void setup_wifi()
{
  // log("entring setup_wifi()...");
  int retry = 1;

  while (WiFi.status() != WL_CONNECTED && retry < 20) // Retry 20 times before restarting
  {
    log("Connecting to WiFi... %d", retry);
    log("Connecting to %s...", ssid);
    logv("setup_wifi: attempt to connect to Wifi network:");
    WiFi.disconnect(); // Disconnect from WiFi network
    delay(1000);       // Wait for 1 second before reconnecting
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(MQTT_HOSTNAME); // define hostname
    WiFi.begin(ssid, pass);
    helpers.blinkBuidInLED(3, 700); // blink the LED 3 times to indicate wifi is not connected
    delay(5000);
    retry++;
  }

  if (WiFi.status() != WL_CONNECTED)
  // todo: add a heandler to connect for second wifi, otherwise restart the ESP32
  { // failed to connect
    log("Wifi failed after trying %d times! --> Restart", retry);
    ESP.restart();
  }
}

//----------------------------------------
// HELPER FUNCTIONS
//----------------------------------------

void cb_BlinkerPublishToMQTT()
{
  // logv("cb_BlinkerPublishToMQTT()...");
  publishToMQTT(jsonDoc); // send to Mqtt
}

void cb_BlinkerMQTTListener()
{
  client.loop(); // process incoming MQTT messages
}

void cb_BlinkerRS485Listener()
{
  // logv("cb_BlinkerRS485Listener()...");
  inverterSetValue = smoothValue(AktualImportFromGrid);
  if (generalSettings.enableController)
  {
    logv("cb_BlinkerRS485Listener(%d)...", inverterSetValue);
    // rs485.sendToRS485(rs485settings, rs485packet, static_cast<uint16_t>(inverterSetValue));
    rs485.sendToRS485(rs485settings, static_cast<uint16_t>(inverterSetValue));
    // RS485Packet incoming;
    // incoming = rs485.reciveFromRS485Packet(); // read the data from RS485
    // logv("Received-Packet.power: %d", incoming.power);
    // logv("Received-Packet.checksum: %d", incoming.checksum);

    // String rs485Data = rs485.reciveFromRS485(); // read the data from RS485
    // logv("Received: %s", rs485Data);
  }
  else
  {
    logv("controller is didabled!");
    logl("MAX-POWER!");
    // rs485.sendToRS485(rs485settings, rs485packet, generalSettings.maxOutput); // send the maxOutput to the RS485 module
    rs485.sendToRS485(rs485settings, generalSettings.maxOutput); // send the maxOutput to the RS485 module
  }
}

int smoothValue(int newValue)
{
  int returnValue = 0;

  SmoothingBuffer[bufferIndex] = newValue + generalSettings.inputCorrectionOffset;
  bufferIndex = (bufferIndex + 1) % SMOOTHING_BUFFER_SIZE;

  // calculate the average of the buffer
  long sum = 0;
  for (int i = 0; i < SMOOTHING_BUFFER_SIZE; i++)
  {
    sum += SmoothingBuffer[i];
  }
  returnValue = sum / SMOOTHING_BUFFER_SIZE;
  // logv("smoothValue: %d", returnValue);
  returnValue = limiter(returnValue);
  // logv("Limited: %d", returnValue);
  return returnValue;
}

int limiter(int usage)
{
  int dem = 0;

  if (usage >= generalSettings.maxOutput)
  {
    dem = generalSettings.maxOutput;
  }
  else if (usage <= generalSettings.minOutput)
  {
    dem = generalSettings.minOutput;
  }
  else
  {
    dem = usage;
  }

  return dem;
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

void fillBufferOnStart(int newValue)
{
  for (int i = 0; i < SMOOTHING_BUFFER_SIZE; i++)
  {
    SmoothingBuffer[i] = newValue + generalSettings.inputCorrectionOffset;
  }
}