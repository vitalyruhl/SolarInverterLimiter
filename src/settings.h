#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "0.8.0"           // version of the software (major.minor.patch) (Version 0.6.0 Has Breaking Changes!)
#define VERSION_DATE "02.09.2025" // date of the version

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

#define RELAY_VENTILATOR_PIN 23 // GPIO pin for the ventilator (if used, otherwise not needed)
// #define RELAY_HEATER_PIN 34 // GPIO pin for the Heater (if used, otherwise not needed)


extern ConfigManagerClass cfg;// store it globaly before using it in the settings

//--------------------------------------------------------------------------------------------------------------

struct WiFi_Settings //wifiSettings
{
    Config<String> wifiSsid;
    Config<String> wifiPassword;
    Config<bool> useDhcp;
    WiFi_Settings() :

                      wifiSsid("ssid", "wifi", "WiFi SSID", "MyWiFi"),
                      wifiPassword("password", "wifi", "WiFi Password", "secretpass", true, true),
                      useDhcp("dhcp", "network", "Use DHCP", false)

    {
        cfg.addSetting(&wifiSsid);
        cfg.addSetting(&wifiPassword);
        cfg.addSetting(&useDhcp);
    }
};

    // String APName = "ESP32_Config";
    // String pwd = "config1234"; // Default AP password

// mqtt-Setup
struct MQTT_Settings //mqttSettings
{
    Config<int> mqtt_port; // port for the MQTT broker (default is 1883)

    Config<String> mqtt_server;                  // IP address of the MQTT broker (Mosquitto)
    Config<String> mqtt_username;                // username for the MQTT broker
    Config<String> mqtt_password;                // password for the MQTT broker
    Config<String> mqtt_sensor_powerusage_topic; // topic for the power usage sensor

    String mqtt_hostname = "SolarLimiter";
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;
    String mqtt_publish_Temperature_topic;
    String mqtt_publish_Humidity_topic;
    String mqtt_publish_Dewpoint_topic;

    // constructor for setting dependent fields
    MQTT_Settings() : mqtt_port("Port", "MQTT","MQTT-Port", 1883),
                      mqtt_server("Server", "MQTT","MQTT-Server-IP", "192.168.2.3"),
                      mqtt_username("User", "MQTT","MQTT-User", "housebattery"),
                      mqtt_password("Pass", "MQTT","MQTT-Passwort", "mqttsecret", true, true),
                      mqtt_sensor_powerusage_topic("PowerUsage", "MQTT","Topic Powerusage" ,"emon/emonpi/power1")
    {
        cfg.addSetting(&mqtt_port);
        cfg.addSetting(&mqtt_server);
        cfg.addSetting(&mqtt_username);
        cfg.addSetting(&mqtt_password);
        cfg.addSetting(&mqtt_sensor_powerusage_topic);

        // set the mqtt topics
        mqtt_publish_setvalue_topic = mqtt_hostname + "/SetValue";
        mqtt_publish_getvalue_topic = mqtt_hostname + "/GetValue";
        mqtt_publish_Temperature_topic = mqtt_hostname + "/Temperature";
        mqtt_publish_Humidity_topic = mqtt_hostname + "/Humidity";
        mqtt_publish_Dewpoint_topic = mqtt_hostname + "/Dewpoint";
    }
};

// General configuration (default Settings)
struct General_Settings
{
    Config<float> VentilatorOn;
    Config<float> VentilatorOFF;
    Config<bool> VentilatorEnable;
    Config<bool> enableController;     // set to false to disable the controller and use Maximum power output
    Config<bool> enableMQTT;           // set to false to disable the MQTT connection
    Config<int> maxOutput;             // edit this to limit TOTAL power output in watts
    Config<int> minOutput;             // minimum output power in watts
    Config<int> inputCorrectionOffset; // Adjust Correction Offset (Input + Offet + Smoothing --> Limmiter = Output)
    Config<float> MQTTPublischPeriod;  // check all x seconds if there is a new MQTT message to publish
    Config<float> MQTTListenPeriod;    // check x seconds if there is a new MQTT message to listen to
    Config<float> RS232PublishPeriod;  // send the RS485 Data all x seconds
    Config<float> TempCorrectionOffset;  // Offset for the temperature correction in Celsius (default is 0.0)
    Config<float> HumidityCorrectionOffset;  // Offset for the humidity correction in percent (default is 0.0)
    Config<int> smoothingSize;         // size of the buffer for smoothing
    Config<String> Version;            // save the current version of the software
    Config<bool> unconfigured; // flag to indicate if the device is unconfigured (default is true)
    Config<bool> saveDisplay; // to turn off the display
    Config<int> displayShowTime; // time in seconds to show the display after boot or button press (default is 60 seconds, 0 = 10s)
    Config<bool> allowOTA; // allow OTA updates (default is true, set to false to disable OTA updates)
    Config<String> otaPassword; // password for OTA updates (default is "ota1234", change it to a secure password)

    General_Settings() :enableController("enCtrl", "GS","Enable Limitation", true),
                        enableMQTT("enMQTT", "GS","Enable MQTT Propagation", true),

                        maxOutput("MaxO", "GS","Max-Output", 1100),
                        minOutput("MinO", "GS","Min-Output", 500),
                        inputCorrectionOffset("ICO", "GS","Correction-Offset", 50),

                        MQTTPublischPeriod("MQTTP", "GS","MQTT Publisching Periode", 5.0),
                        MQTTListenPeriod("MQTTL", "GS","MQTT Listening Periode", 0.5),

                        TempCorrectionOffset("TCO_TempratureCorrectionOffset","Temperature Correction", "GS", 0.0),
                        HumidityCorrectionOffset("HYO_HumidityCorrectionOffset","Humidity Correction", "GS", 0.0),

                        VentilatorOn("VentOn", "Vent", "Ventilator On over", 30.0),
                        VentilatorOFF("VentOff", "Vent", "Ventilator Off under", 27.0),
                        VentilatorEnable("VentEn", "Vent", "Enable Ventilator Control", true),

                        saveDisplay("DispSave", "GS", "Turn Display Off", true),
                        displayShowTime("DispTime", "GS", "Display On-Time in Sec", 60),

                        allowOTA("OTAEn", "GS", "Allow OTA Updates", true),
                        otaPassword("OTAPass", "GS", "OTA Password", "ota1234", true, true),
                        
                        RS232PublishPeriod("RS232P", "GS","RS232 Publisching Periode", 2.0),
                        smoothingSize("Smooth", "GS","Smoothing Level", 10),

                        unconfigured("Unconfigured", "GS","ESP is unconfigured", true, false), // flag to indicate if the device is unconfigured (default is true)
                        Version("Version", "GS","Programm-Version", VERSION)
    {
        // Register settings with configManager
        cfg.addSetting(&enableController);
        cfg.addSetting(&enableMQTT);
        cfg.addSetting(&maxOutput);
        cfg.addSetting(&minOutput);
        cfg.addSetting(&inputCorrectionOffset);
        cfg.addSetting(&MQTTPublischPeriod);
        cfg.addSetting(&MQTTListenPeriod);
        cfg.addSetting(&RS232PublishPeriod);
        cfg.addSetting(&TempCorrectionOffset);
        cfg.addSetting(&HumidityCorrectionOffset);
        
        cfg.addSetting(&VentilatorOn);
        cfg.addSetting(&VentilatorOFF);
        cfg.addSetting(&VentilatorEnable);
        cfg.addSetting(&unconfigured);
        cfg.addSetting(&saveDisplay);
        cfg.addSetting(&displayShowTime);
        cfg.addSetting(&allowOTA);
        cfg.addSetting(&otaPassword);
        
        cfg.addSetting(&smoothingSize);
        
        cfg.addSetting(&Version);
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

extern WebServer server;
extern MQTT_Settings mqttSettings;
extern General_Settings generalSettings;
extern RS485_Settings rs485settings;
extern SigmaLogLevel logLevel;
extern WiFi_Settings wifiSettings;
// extern LDR_Settings ldrSettings;

#endif // SETTINGS_H
