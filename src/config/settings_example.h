#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

// Logging-Setup --> comment this out to disable logging to serial console
#define ENABLE_LOGGING
#define ENABLE_LOGGING_VERBOSE
#define ENABLE_LOGGING_LCD

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


struct rs485Settings
{
    bool useExtraSerial = true; // set to true to use Serial2 for RS485 communication
    int baudRate = 4800;
    int rxPin = 16; // onl<y for Serial2, not used for Serial
    int txPin = 17; // onl<y for Serial2, not used for Serial
    int dePin = 4; // DE pin for RS485 communication (direction control)
    bool enableRS485 = true; // set to false to disable RS485 communication
};


#endif // CONFIG_H
