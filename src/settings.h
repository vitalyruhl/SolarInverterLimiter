#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "0.6.0"           // version of the software (major.minor.patch) (Version 0.6.0 Has Breaking Changes!)
#define VERSION_DATE "05.05.2025" // date of the version

//--------------------------------------------------------------------------------------------------------------
// set the I2C address for the BME280 sensor for temperature and humidity
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FREQUENCY 400000
#define BME280_FREQUENCY 400000
// #define BME280_ADDRESS 0x76 // I2C address for the BME280 sensor (default is 0x76) redefine, if needed

#define ReadTemperatureTicker 30.0 // time in seconds to read the temperature and humidity
//--------------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------------
// set the I2C address for the display (SSD1306)
#define I2C_DISPLAY_ADDRESS 0x3C
//--------------------------------------------------------------------------------------------------------------

#define BUTTON_PIN_AP_MODE 13           // GPIO pin for the button to start AP mode (check on boot)
#define BUTTON_PIN_RESET_TO_DEFAULTS 15 // GPIO pin for the button (check on boot)
#define WDT_TIMEOUT 60                  // in seconds, if esp32 is not responding within this time, the ESP32 will reboot automatically

Config<String> wifiSsid("ssid", "wifi", "MyWiFi");
Config<String> wifiPassword("password", "wifi", "secretpass", true, true);
Config<bool> useDhcp("dhcp", "network", true);

// mqtt-Setup
struct MQTT_Settings
{
    int mqtt_port = 1883;
    String mqtt_server = "192.168.2.3"; // IP address of the MQTT broker (Mosquitto)
    String mqtt_username = "housebattery";
    String mqtt_password = "mqttsecret";
    String mqtt_hostname = "SolarLimiter_test";
    String mqtt_sensor_powerusage_topic = "emon/emonpi/power1";
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;

    // constructor for setting dependent fields
    MQTT_Settings()
    {
        mqtt_publish_setvalue_topic = mqtt_hostname + "/SetValue";
        mqtt_publish_getvalue_topic = mqtt_hostname + "/GetValue";
        mqtt_publish_getvalue_topic = mqtt_hostname + "/Temperature"; // Todo: Implement it!
        mqtt_publish_getvalue_topic = mqtt_hostname + "/Humidity";    // Todo: Implement it!
    }
};

// General configuration (default Settings)
struct General_Settings
{
    General_Settings() // constructor
    {
        Config<bool> enableController("enCtrl", "GS", true); // set to false to disable the controller and use Maximum power output
        Config<bool> enableMQTT("enMQTT", "GS", true);       // set to false to disable the MQTT connection

        Config<int> maxOutput("MaxO", "GS", 1100);             // edit this to limit TOTAL power output in watts
        Config<int> minOutput("MinO", "GS", 500);              // minimum output power in watts
        Config<int> inputCorrectionOffset("ICO", "GS", 50);    // Adjust Correction Offset (Input + Offet + Smoothing --> Limmiter = Output). It can be a negative value, if you dont want spend solarpower and store all in the battery
        Config<float> MQTTPublischPeriod("MQTTP", "GS", 5.0);  // check all x seconds if there is a new MQTT message to publish
        Config<float> MQTTListenPeriod("MQTTL", "GS", 0.5);    // check x seconds if there is a new MQTT message to listen to
        Config<float> RS232PublishPeriod("RS232P", "GS", 2.0); // send the RS485 Data all x seconds
        Config<int> smoothingSize("Smooth", "GS", 10);         // size of the buffer for smoothing
        Config<String> Version("Version", "GS", VERSION);      // save the current version of the software (major.minor.patch) (will reset all settings to default if there are breacking changes)
    }
};

// RS485 configuration (default Settings)
struct RS485_Settings
{
    bool useExtraSerial = true; // set to true to use Serial2 for RS485 communication
    int baudRate = 4800;
    int rxPin = 18;          // only for Serial2, not used for Serial
    int txPin = 19;          // only for Serial2, not used for Serial
    int dePin = 4;           // DE pin for RS485 communication (direction control)
    bool enableRS485 = true; // set to false to disable RS485 communication
    // todo: add settings for Inverter eg, headder, checksum, etc.
};

class ProjectConfig
{
public:
    ConfigManagerClass configManager;

    WebServer server(80);
    MQTT_Settings mqttSettings;
    General_Settings generalSettings;
    RS485_Settings rs485settings;
    SigmaLogLevel logLevel = SIGMALOG_WARN;
    // SIGMALOG_OFF = 0,
    // SIGMALOG_INTERNAL,
    // SIGMALOG_FATAL,
    // SIGMALOG_ERROR,
    // SIGMALOG_WARN,
    // SIGMALOG_INFO,
    // SIGMALOG_DEBUG,
    // SIGMALOG_ALL

    
    ProjectConfig()
    {
        // Register settings
        configManager.addSetting(&wifiSsid);
        configManager.addSetting(&wifiPassword);
        configManager.addSetting(&useDhcp);

        // register General settings
        configManager.addSetting(&generalSettings.enableController);
        configManager.addSetting(&generalSettings.enableMQTT);
        configManager.addSetting(&generalSettings.maxOutput);
        configManager.addSetting(&generalSettings.minOutput);
        configManager.addSetting(&generalSettings.inputCorrectionOffset);
        configManager.addSetting(&generalSettings.MQTTPublischPeriod);
        configManager.addSetting(&generalSettings.MQTTListenPeriod);
        configManager.addSetting(&generalSettings.RS232PublishPeriod);
        configManager.addSetting(&generalSettings.smoothingSize);
        configManager.addSetting(&generalSettings.Version);
    }

};

#endif // SETTINGS_H
