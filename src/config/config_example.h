#ifndef CONFIG_H
#define CONFIG_H

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "WiFiManager/WiFiManager.h"

#define VERSION "0.2.1"           // version of the software
#define VERSION_DATE "2025.04.19" // date of the version

// Logging-Setup --> comment this out to disable logging to serial console
#define ENABLE_LOGGING
#define ENABLE_LOGGING_VERBOSE
#define ENABLE_LOGGING_LCD

#define BUTTON_PIN_RESET_TO_DEFAULTS 15 // GPIO pin for the button (D0 on ESP8266, GPIO 0 on ESP32)

#define WDT_TIMEOUT 60 // in seconds, if esp32 is not responding within this time, the ESP32 will reboot automatically

// WiFi-Setup

struct wifi_config
{
    String ssid = "YourSSID";     // WiFi SSID
    String pass = "YourPassword"; // WiFi password
    String failover_ssid = "YourFailoverSSIS";     // WiFi SSID
    String failover_pass = "YourFailoverPasswort"; // WiFi password
    String apSSID = "HouseBattery_AP"; // Access Point SSID
    bool use_static_ip = false;
    String staticIP = "192.178.0.22";      // Static IP address
    String staticSubnet = "255.255.255.0";  // Static subnet mask
    String staticGateway = "192.168.0.1"; // Static gateway
    String staticDNS = "192.168.0.1";     // Static DNS server
};

// MQTT-Setup
// String mqtt_username = "thomas";
// String mqtt_password = "Molly#TJ2005";

struct config_mqtt
{
  int mqtt_port = 1883;
  String mqtt_server = "192.168.2.3"; // IP address of the MQTT broker (Mosquitto)
  String mqtt_username = "mqttuser";
  String mqtt_password = "password";
  String mqtt_hostname = "HouseBattery_Controller_Test";
  String mqtt_sensor_powerusage_topic = "emon/emonpi/power1";
  String mqtt_publish_setvalue_topic = "HouseBattery_Controller_Test/SetValue"; // topic to publish the set value to
  String mqtt_publish_getvalue_topic = "HouseBattery_Controller_Test/GetValue"; // topic to publish the get value to
};

// General configuration (default Settings)
struct GeneralSettings
{
  bool dirtybit = false;                    // dirty bit to indicate if the message has changed
  bool enableController = true;             // set to false to disable the controller and use Maximum power output
  int maxOutput = 1100;                     // edit this to limit TOTAL power output in watts
  int minOutput = 500;                      // minimum output power in watts
  int inputCorrectionOffset = 150;          // ( -80) Current clamp sensors have poor accuracy at low load, a buffer ensures some current flow in the import direction to ensure no exporting. Adjust according to accuracy of meter.
  float MQTTPublischPeriod = 5.5;           // check all x seconds if there is a new MQTT message to publish
  float MQTTSettingsPublischPeriod = 300.0; // send the settings to the MQTT broker every x seconds
  float MQTTListenPeriod = 0.5;             // check x seconds if there is a new MQTT message to listen to
  float RS232PublishPeriod = 1.5;           // send the RS485 Data all x seconds
  int smoothingSize = 8;                    // size of the buffer for smoothing
};


class Config
{
public:
  Config_wifi wifi_config;
  config_mqtt mqtt;
  GeneralSettings general;

  bool saveSettingsFlag = false;

  void load();
  void save();
  void removeSettings(char *Name);
  void removeAllSettings();
  void printSettings();

  String toJSON();
  void fromJSON(const String &json);

  void attachWebEndpoint(WebServer &server);

private:
  void loadSettingsFromEEPROM();
  void saveSettingsToEEPROM();
};

#endif // CONFIG_H
