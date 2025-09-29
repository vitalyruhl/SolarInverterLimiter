#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

// -------------------------------------------------------------------------------------------------------------
// Migrated to ConfigManager V2.3.1 (breaking changes: legacy Config constructors replaced by ConfigOptions)
// -------------------------------------------------------------------------------------------------------------
// NOTES ABOUT NEW API:
//  - Use designated initialization with ConfigOptions<T>{ .keyName = "...", .category = "...", .defaultValue = ... }
//  - Optional UI fields: .prettyName, .prettyCat
//  - Visibility / secrets: .showInWeb, .isPassword
//  - Dynamic visibility: .showIf (std::function<bool()>)
//  - Callback (primitive pointer) .cb OR after construction: instance.setCallback(std::function<>)
//  - Key length limit (category + '_' + key) <= 15 chars (storage). Longer parts are truncated automatically.
// -------------------------------------------------------------------------------------------------------------

#define VERSION "2.3.1"           // version of the software (major.minor.patch)
#define VERSION_DATE "13.09.2025" // date of the version

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

struct WiFi_Settings // wifiSettings (migrated to new ConfigOptions API)
{
    Config<String> wifiSsid;
    Config<String> wifiPassword;
    Config<bool>   useDhcp;
    Config<String> staticIp;
    Config<String> gateway;
    Config<String> subnet;

    WiFi_Settings() :
        wifiSsid(ConfigOptions<String>{
            .keyName = "ssid",
            .category = "wifi",
            .defaultValue = String("MyWiFi"),
            .prettyName = "WiFi SSID",
            .prettyCat = "Network Settings"
        }),
        wifiPassword(ConfigOptions<String>{
            .keyName = "password",
            .category = "wifi",
            .defaultValue = String("secretpass"),
            .prettyName = "WiFi Password",
            .prettyCat = "Network Settings",
            .showInWeb = true,
            .isPassword = true
        }),
        useDhcp(ConfigOptions<bool>{
            .keyName = "dhcp",
            .category = "network",
            .defaultValue = false,
            .prettyName = "Use DHCP",
            .prettyCat = "Network Settings"
        }),
        staticIp(ConfigOptions<String>{
            .keyName = "sIP",
            .category = "network",
            .defaultValue = String("192.168.2.126"),
            .prettyName = "Static IP",
            .prettyCat = "Network Settings",
            .showIf = [this](){ return !this->useDhcp.get(); }
        }),
        gateway(ConfigOptions<String>{
            .keyName = "GW",
            .category = "network",
            .defaultValue = String("192.168.2.250"),
            .prettyName = "Gateway",
            .prettyCat = "Network Settings",
            .showIf = [this](){ return !this->useDhcp.get(); }
        }),
        subnet(ConfigOptions<String>{
            .keyName = "subnet",
            .category = "network",
            .defaultValue = String("255.255.255.0"),
            .prettyName = "Subnet-Mask",
            .prettyCat = "Network Settings",
            .showIf = [this](){ return !this->useDhcp.get(); }
        })
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
    Config<int>    mqtt_port;
    Config<String> mqtt_server;
    Config<String> mqtt_username;
    Config<String> mqtt_password;
    Config<String> mqtt_sensor_powerusage_topic;
    Config<String> Publish_Topic;

    // dynamic topics (derived)
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;
    String mqtt_publish_Temperature_topic;
    String mqtt_publish_Humidity_topic;
    String mqtt_publish_Dewpoint_topic;

    MQTT_Settings() :
        mqtt_port(ConfigOptions<int>{
            .keyName = "Port",
            .category = "MQTT",
            .defaultValue = 1883,
            .prettyName = "Port",
            .prettyCat = "MQTT-Section"
        }),
        mqtt_server(ConfigOptions<String>{
            .keyName = "Server",
            .category = "MQTT",
            .defaultValue = String("192.168.2.3"),
            .prettyName = "Server-IP",
            .prettyCat = "MQTT-Section"
        }),
        mqtt_username(ConfigOptions<String>{
            .keyName = "User",
            .category = "MQTT",
            .defaultValue = String("housebattery"),
            .prettyName = "User",
            .prettyCat = "MQTT-Section"
        }),
        mqtt_password(ConfigOptions<String>{
            .keyName = "Pass",
            .category = "MQTT",
            .defaultValue = String("mqttsecret"),
            .prettyName = "Password",
            .prettyCat = "MQTT-Section",
            .showInWeb = true,
            .isPassword = true
        }),
        mqtt_sensor_powerusage_topic(ConfigOptions<String>{
            .keyName = "PUT",
            .category = "MQTT",
            .defaultValue = String("emon/emonpi/power1"),
            .prettyName = "Powerusage Topic",
            .prettyCat = "MQTT-Section"
        }),
        Publish_Topic(ConfigOptions<String>{
            .keyName = "MQTTT",
            .category = "MQTT",
            .defaultValue = String("SolarLimiter"),
            .prettyName = "Publish-Topic",
            .prettyCat = "MQTT-Section"
        })
    {
        cfg.addSetting(&mqtt_port);
        cfg.addSetting(&mqtt_server);
        cfg.addSetting(&mqtt_username);
        cfg.addSetting(&mqtt_password);
        cfg.addSetting(&mqtt_sensor_powerusage_topic);
        cfg.addSetting(&Publish_Topic);

        // capturing lambda requires std::function path -> use setCallback after construction
        Publish_Topic.setCallback([this](String){ this->updateTopics(); });
        updateTopics();
    }

    void updateTopics() {
        String hostname = Publish_Topic.get();
        mqtt_publish_setvalue_topic      = hostname + "/SetValue";
        mqtt_publish_getvalue_topic      = hostname + "/GetValue";
        mqtt_publish_Temperature_topic   = hostname + "/Temperature";
        mqtt_publish_Humidity_topic      = hostname + "/Humidity";
        mqtt_publish_Dewpoint_topic      = hostname + "/Dewpoint";
    }
};


// General configuration (default Settings)
struct General_Settings
{
    Config<bool>  enableController;
    Config<bool>  enableMQTT;
    Config<int>   maxOutput;
    Config<int>   minOutput;
    Config<int>   inputCorrectionOffset;
    Config<float> MQTTPublischPeriod;
    Config<float> MQTTListenPeriod;
    Config<float> TempCorrectionOffset;
    Config<float> HumidityCorrectionOffset;
    Config<float> VentilatorOn;
    Config<float> VentilatorOFF;
    Config<bool>  VentilatorEnable;
    Config<bool>  saveDisplay;
    Config<int>   displayShowTime;
    Config<bool>  allowOTA;
    Config<String> otaPassword;
    Config<float> RS232PublishPeriod;
    Config<int>   smoothingSize;
    Config<bool>  unconfigured;
    Config<String> Version;

    General_Settings() :
        enableController(ConfigOptions<bool>{
            .keyName = "enCtrl", .category = "Limiter", .defaultValue = true, .prettyName = "Enable Limitation"
        }),
        enableMQTT(ConfigOptions<bool>{
            .keyName = "enMQTT", .category = "Limiter", .defaultValue = true, .prettyName = "Enable MQTT Propagation", .prettyCat = "MQTT-Section"
        }),
        maxOutput(ConfigOptions<int>{
            .keyName = "MaxO", .category = "Limiter", .defaultValue = 1100, .prettyName = "Max-Output"
        }),
        minOutput(ConfigOptions<int>{
            .keyName = "MinO", .category = "Limiter", .defaultValue = 500, .prettyName = "Min-Output"
        }),
        inputCorrectionOffset(ConfigOptions<int>{
            .keyName = "ICO", .category = "Limiter", .defaultValue = 50, .prettyName = "Correction-Offset"
        }),
        MQTTPublischPeriod(ConfigOptions<float>{
            .keyName = "MQTTP", .category = "System", .defaultValue = 5.0f, .prettyName = "MQTT Publishing Period", .prettyCat = "MQTT-Section"
        }),
        MQTTListenPeriod(ConfigOptions<float>{
            .keyName = "MQTTL", .category = "System", .defaultValue = 0.5f, .prettyName = "MQTT Listening Period", .prettyCat = "MQTT-Section"
        }),
        TempCorrectionOffset(ConfigOptions<float>{
            .keyName = "TCO", .category = "Temp", .defaultValue = 0.1f, .prettyName = "Temperature Correction", .prettyCat = "Temperature Settings"

        }),
        HumidityCorrectionOffset(ConfigOptions<float>{
            .keyName = "HYO", .category = "Temp", .defaultValue = 0.1f, .prettyName = "Humidity Correction", .prettyCat = "Temperature Settings"
        }),

        VentilatorOn(ConfigOptions<float>{
            .keyName = "VentOn", .category = "FAN", .defaultValue = 30.0f, .prettyName = "Fan On over", .prettyCat = "FAN Control",
            .showIf = [this](){ return this->VentilatorEnable.get();}
        }),
        VentilatorOFF(ConfigOptions<float>{
            .keyName = "VentOff", .category = "FAN", .defaultValue = 27.0f, .prettyName = "Fan Off under", .prettyCat = "FAN Control",
            .showIf = [this](){ return this->VentilatorEnable.get();}
        }),
        VentilatorEnable(ConfigOptions<bool>{
            .keyName = "VentEn", .category = "FAN", .defaultValue = true, .prettyName = "Enable Fan Control", .prettyCat = "FAN Control"
        }),


        saveDisplay(ConfigOptions<bool>{
            .keyName = "DispSave", .category = "Display", .defaultValue = true, .prettyName = "Turn Display Off", .prettyCat = "Display Settings"
        }),
        displayShowTime(ConfigOptions<int>{
            .keyName = "DispTime", .category = "Display", .defaultValue = 60, .prettyName = "Display On-Time (s)", .prettyCat = "Display Settings"
        }),
        allowOTA(ConfigOptions<bool>{
            .keyName = "OTAEn", .category = "System", .defaultValue = true, .prettyName = "Allow OTA Updates"
        }),
        otaPassword(ConfigOptions<String>{
            .keyName = "OTAPass", .category = "System", .defaultValue = String("ota1234"), .prettyName = "OTA Password", .showInWeb = true, .isPassword = true
        }),
        RS232PublishPeriod(ConfigOptions<float>{
            .keyName = "RS232P", .category = "RS232", .defaultValue = 2.0f, .prettyName = "RS232 Publish Period"
        }),
        smoothingSize(ConfigOptions<int>{
            .keyName = "Smooth", .category = "RS232", .defaultValue = 10, .prettyName = "Smoothing Level"
        }),
        unconfigured(ConfigOptions<bool>{
            .keyName = "Unconfigured", .category = "GS", .defaultValue = true, .prettyName = "ESP is unconfigured", .showInWeb = false, .isPassword = false
        }),
        Version(ConfigOptions<String>{
            .keyName = "Version", .category = "System", .defaultValue = String(VERSION), .prettyName = "Program Version"
        })
    {
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

extern MQTT_Settings mqttSettings;
extern General_Settings generalSettings;
extern RS485_Settings rs485settings;
extern SigmaLogLevel logLevel;
extern WiFi_Settings wifiSettings;
// extern LDR_Settings ldrSettings;

#endif // SETTINGS_H
