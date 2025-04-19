#ifndef CONFIG_H
#define CONFIG_H

#pragma once
#include <Arduino.h>

// Logging-Setup --> comment this out to disable logging to serial console
#define ENABLE_LOGGING
// #define ENABLE_LOGGING_VERBOSE
#define ENABLE_LOGGING_LCD


#define VERSION "0.1.0"           // version of the software
#define VERSION_DATE "2025.04.19" // date of the version


// WiFi configuration
#define WIFI_SSID "YourWifiSSID" // SSID of the WiFi network
#define WIFI_PASSWORD "YourWifiPassword" // Password of the WiFi network

// MQTT-Setup
#define MQTT_SERVER "192.178.1.30" // IP address of the MQTT broker (Mosquitto)
#define MQTT_USERNAME "mqttBrokerUsername"
#define MQTT_PASSWORD "mqttsecret"
#define MQTT_HOSTNAME "HouseBattery_Controller" // hostname (will appear in wifi router connected devices list etc)
#define MQTT_PORT 1883 // MQTT port number default is 1883
#define MQTT_WDT_TIMEOUT 20 // in seconds, if no MQTT message is received within this time, the ESP8266 will reboot

#define MQTT_SENSOR_POWERUSAGE_TOPIC "emon/emonpi/power1"
#define MQTT_PUBLISH_SETVALUE_TOPIC "HouseBattery_Controller/SetValue"
#define MQTT_PUBLISH_GETVALUE_TOPIC "HouseBattery_Controller/GetValue"
#define MQTT_PUBLISH_SETTINGS "HouseBattery_Controller/Settings"
#define MQTT_PUBLISH_SAVE_SETTINGS "HouseBattery_Controller/SaveSettings"

#define BUTTON_PIN_RESET_TO_DEFAULTS 15 // GPIO pin for the Reset to defaults (this Settings) button (D0 on ESP8266, GPIO 0 on ESP32) --> will be only checked on Startup

// General configuration (default Settings)
struct GeneralSettings
{
  bool dirtybit = false;           // dirty bit to indicate if the message has changed
  bool enableController = true;    // set to false to disable the controller and use Maximum power output
  int maxOutput = 1130;            // edit this to limit TOTAL power output in watts
  int minOutput = 500;             // minimum output power in watts
  int inputCorrectionOffset = 150; // ( -80) Current clamp sensors have poor accuracy at low load, a buffer ensures some current flow in the import direction to ensure no exporting. Adjust according to accuracy of meter.
  float MQTTPublischPeriod = 10.5;  // check all x seconds if there is a new MQTT message to publish
  float MQTTListenPeriod = 0.3;    // check x seconds if there is a new MQTT message to listen to
  float RS232PublishPeriod = 1.1;  // send the RS485 Data all x seconds
};

class Config
{
public:
  void loadSettings(GeneralSettings &generalSettings);  // Helper function to load settings from ESP32 flash memory
  void saveSettings(GeneralSettings &generalSettings);  // save settings to ESP32 flash memory
  bool saveSettingsFlag = false;                        // set to true to save settings to esp32 flash memory
  void removeAllSettings();                             // remove all settings from esp32 flash memory
  void removeSettings(char *Name);                      // remove a specific setting from esp32 flash memory (not used yet)
  void printSettings(GeneralSettings &generalSettings); // print settings if ENABLE_LOGGING_VERBOSE is defined

private:
  void saveSettingsToEEPROM(GeneralSettings &generalSettings);   // save settings to EEPROM
  void loadSettingsFromEEPROM(GeneralSettings &generalSettings); // load settings from EEPROM
};
#endif // CONFIG_H
