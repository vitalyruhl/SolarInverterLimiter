#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "2.1.0"           // version of the software (major.minor.patch) (Version 0.6.0 Has Breaking Changes!)
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
    Config<String> staticIp;
    Config<String> gateway;
    Config<String> subnet;

    //todo: add static-IP settings
    WiFi_Settings() :

                    wifiSsid("ssid", "wifi", "WiFi SSID", "MyWiFi"),
                    wifiPassword("password", "wifi", "WiFi Password", "secretpass", true, true),
                    useDhcp("dhcp", "network", "Use DHCP", false),
                    staticIp("sIP", "network", "Static IP", "192.168.2.126"),
                    subnet("subnet", "network", "Subnet-Mask", "255.255.255.0"),
                    gateway("GW", "network", "Gateway", "192.168.2.250")

    {
        cfg.addSetting(&wifiSsid);
        cfg.addSetting(&wifiPassword);
        cfg.addSetting(&useDhcp);
        cfg.addSetting(&staticIp);
        cfg.addSetting(&gateway);
        cfg.addSetting(&subnet);
    }
};

    // String APName = "ESP32_Config";
    // String pwd = "config1234"; // Default AP password

// mqtt-Setup
struct MQTT_Settings {
    Config<int> mqtt_port;
    Config<String> mqtt_server;
    Config<String> mqtt_username;
    Config<String> mqtt_password;
    Config<String> mqtt_sensor_powerusage_topic;
    Config<String> Publish_Topic;

    // for dynamic topics based on Publish_Topic
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;
    String mqtt_publish_Temperature_topic;
    String mqtt_publish_Humidity_topic;
    String mqtt_publish_Dewpoint_topic;

    MQTT_Settings() :
        mqtt_port("Port", "MQTT", "MQTT-Port", 1883),
        mqtt_server("Server", "MQTT", "MQTT-Server-IP", "192.168.2.3"),
        mqtt_username("User", "MQTT", "MQTT-User", "housebattery"),
        mqtt_password("Pass", "MQTT", "MQTT-Passwort", "mqttsecret", true, true),
        mqtt_sensor_powerusage_topic("PUT", "MQTT", "Topic Powerusage", "emon/emonpi/power1"),
        Publish_Topic("MQTTT", "MQTT", "Publish-Topic", "SolarLimiter")
    {
        cfg.addSetting(&mqtt_port);
        cfg.addSetting(&mqtt_server);
        cfg.addSetting(&mqtt_username);
        cfg.addSetting(&mqtt_password);
        cfg.addSetting(&mqtt_sensor_powerusage_topic);
        cfg.addSetting(&Publish_Topic);

        // callback to update topics when Publish_Topic changes
        Publish_Topic.setCallback([this](String newValue) {
            this->updateTopics();
        });

        updateTopics(); // make sure topics are initialized
    }

    void updateTopics() {
        String hostname = Publish_Topic.get(); //you can trow an error here if its empty
        mqtt_publish_setvalue_topic = hostname + "/SetValue";
        mqtt_publish_getvalue_topic = hostname + "/GetValue";
        mqtt_publish_Temperature_topic = hostname + "/Temperature";
        mqtt_publish_Humidity_topic = hostname + "/Humidity";
        mqtt_publish_Dewpoint_topic = hostname + "/Dewpoint";
    }
};


// General configuration (default Settings)
struct General_Settings
{
    Config<bool> enableController;     // set to false to disable the controller and use Maximum power output
    Config<bool> enableMQTT;           // set to false to disable the MQTT connection

    Config<int> maxOutput;             // edit this to limit TOTAL power output in watts
    Config<int> minOutput;             // minimum output power in watts
    Config<int> inputCorrectionOffset; // Adjust Correction Offset (Input + Offet + Smoothing --> Limmiter = Output)

    Config<float> MQTTPublischPeriod;  // check all x seconds if there is a new MQTT message to publish
    Config<float> MQTTListenPeriod;    // check x seconds if there is a new MQTT message to listen to

    Config<float> TempCorrectionOffset;  // Offset for the temperature correction in Celsius (default is 0.0)
    Config<float> HumidityCorrectionOffset;  // Offset for the humidity correction in percent (default is 0.0)

    Config<float> VentilatorOn;
    Config<float> VentilatorOFF;
    Config<bool> VentilatorEnable;

    Config<bool> saveDisplay; // to turn off the display
    Config<int> displayShowTime; // time in seconds to show the display after boot or button press (default is 60 seconds, 0 = 10s)

    Config<bool> allowOTA; // allow OTA updates (default is true, set to false to disable OTA updates)
    Config<String> otaPassword; // password for OTA updates (default is "ota1234", change it to a secure password)

    Config<float> RS232PublishPeriod;  // send the RS485 Data all x seconds
    Config<int> smoothingSize;         // size of the buffer for smoothing

    Config<bool> unconfigured; // flag to indicate if the device is unconfigured (default is true)
    Config<String> Version;            // save the current version of the software

    General_Settings() :enableController("enCtrl", "Limiter","Enable Limitation", true),
                        enableMQTT("enMQTT", "Limiter","Enable MQTT Propagation", true),

                        maxOutput("MaxO", "Limiter","Max-Output", 1100),
                        minOutput("MinO", "Limiter","Min-Output", 500),
                        inputCorrectionOffset("ICO", "Limiter","Correction-Offset", 50),

                        MQTTPublischPeriod("MQTTP", "System","MQTT Publisching Periode", 5.0),
                        MQTTListenPeriod("MQTTL", "System","MQTT Listening Periode", 0.5),

                        TempCorrectionOffset("TCO", "Temp","Temperature Correction", 0.1),
                        HumidityCorrectionOffset("HYO", "Temp","Humidity Correction", 0.1),

                        VentilatorOn("VentOn", "Vent", "Ventilator On over", 30.0),
                        VentilatorOFF("VentOff", "Vent", "Ventilator Off under", 27.0),
                        VentilatorEnable("VentEn", "Vent", "Enable Ventilator Control", true),

                        saveDisplay("DispSave", "Display", "Turn Display Off", true),
                        displayShowTime("DispTime", "Display", "Display On-Time in Sec", 60),

                        allowOTA("OTAEn", "System", "Allow OTA Updates", true),
                        otaPassword("OTAPass", "System", "OTA Password", "ota1234", true, true),

                        RS232PublishPeriod("RS232P", "RS232","RS232 Publisching Periode", 2.0),
                        smoothingSize("Smooth", "RS232","Smoothing Level", 10),

                        unconfigured("Unconfigured", "GS","ESP is unconfigured", true, true), // flag to indicate if the device is unconfigured (default is true)
                        Version("Version", "System","Programm-Version", VERSION)
    {
        // Register settings with configManager
        cfg.addSetting(&enableController);
        cfg.addSetting(&enableMQTT);

        cfg.addSetting(&maxOutput);
        cfg.addSetting(&minOutput);
        cfg.addSetting(&inputCorrectionOffset);

        cfg.addSetting(&MQTTPublischPeriod);
        cfg.addSetting(&MQTTListenPeriod);

        cfg.addSetting(&TempCorrectionOffset);
        cfg.addSetting(&HumidityCorrectionOffset);

        cfg.addSetting(&VentilatorOn);
        cfg.addSetting(&VentilatorOFF);
        cfg.addSetting(&VentilatorEnable);

        cfg.addSetting(&saveDisplay);
        cfg.addSetting(&displayShowTime);

        cfg.addSetting(&allowOTA);
        cfg.addSetting(&otaPassword);

        cfg.addSetting(&RS232PublishPeriod);
        cfg.addSetting(&smoothingSize);

        cfg.addSetting(&unconfigured);

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
