#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once
#include "WiFiManager/WiFiManager.h"

// Logging-Setup --> comment this out to disable logging to serial console
#define ENABLE_LOGGING
#define ENABLE_LOGGING_VERBOSE
#define ENABLE_LOGGING_LCD

// WiFi-Setup
const Config_wifi default_wifi_settings = []
{
    Config_wifi cfg;
    cfg.ssid = "HausNetz";
    cfg.pass = "Geheim123";
    cfg.failover_ssid = "HausNetz2";
    cfg.failover_pass = "FailoverPass";
    cfg.apSSID = "SolarLimiterAP";
    cfg.staticIP = "192.168.0.22";
    cfg.staticSubnet = "255.255.255.0";
    cfg.staticGateway = "192.168.0.1";
    cfg.staticDNS = "192.168.0.1";
    cfg.use_static_ip = true;
    return cfg;
}();

//mqtt-Setup
struct config_mqtt
{
    int mqtt_port = 1883;
    String mqtt_server = "192.168.2.3"; // IP address of the MQTT broker (Mosquitto)
    String mqtt_username = "housebattery";
    String mqtt_password = "mqttsecret";
    String mqtt_hostname = "SolarLimiter";
    String mqtt_sensor_powerusage_topic = "emon/emonpi/power1";
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;

    // constructor for setting dependent fields
    config_mqtt()
    {
        mqtt_publish_setvalue_topic = mqtt_hostname + "/SetValue";
        mqtt_publish_getvalue_topic = mqtt_hostname + "/GetValue";
    }
};

// General configuration (default Settings)
struct GeneralSettings
{
    bool dirtybit = false;                    // dirty bit to indicate if the message has changed
    bool enableController = true;             // set to false to disable the controller and use Maximum power output
    int maxOutput = 1100;                     // edit this to limit TOTAL power output in watts
    int minOutput = 500;                      // minimum output power in watts
    int inputCorrectionOffset = 50;          // ( -80) Current clamp sensors have poor accuracy at low load, a buffer ensures some current flow in the import direction to ensure no exporting. Adjust according to accuracy of meter.
    float MQTTPublischPeriod = 5.0;           // check all x seconds if there is a new MQTT message to publish
    float MQTTSettingsPublischPeriod = 300.0; // send the settings to the MQTT broker every x seconds
    float MQTTListenPeriod = 0.5;             // check x seconds if there is a new MQTT message to listen to
    float RS232PublishPeriod = 2.0;           // send the RS485 Data all x seconds
    int smoothingSize = 8;                    // size of the buffer for smoothing
};

// RS485 configuration (default Settings)
struct rs485Settings
{
    bool useExtraSerial = true; // set to true to use Serial2 for RS485 communication
    int baudRate = 4800;
    int rxPin = 18;          // only for Serial2, not used for Serial
    int txPin = 19;          // only for Serial2, not used for Serial
    int dePin = 4;           // DE pin for RS485 communication (direction control)
    bool enableRS485 = true; // set to false to disable RS485 communication
    //todo: add settings for Inverter eg, headder, checksum, etc.
};

#endif // SETTINGS_H
