#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "2.4.2"           // version of the software (major.minor.patch)
#define VERSION_DATE "29.09.2025" // date of the version

//--------------------------------------------------------------------------------------------------------------
// set the I2C address for the BME280 sensor for temperature and humidity
// (defaults now moved into I2CSettings as Config defaults)

//--------------------------------------------------------------------------------------------------------------
// set the I2C address for the display (SSD1306)
// Display address moved into settings (I2CSettings)
//--------------------------------------------------------------------------------------------------------------

// Watchdog timeout remains compile-time constant
#define WDT_TIMEOUT 60


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
            .key = "ssid",
            .name = "WiFi SSID",
            .category = "wifi",
            .defaultValue = String(""),
            .categoryPretty = "Network"
        }),
        wifiPassword(ConfigOptions<String>{
            .key = "pwd",
            .name = "WiFi Password",
            .category = "wifi",
            .defaultValue = String(""),
            .isPassword = true,
            .categoryPretty = "Network"
        }),
        useDhcp(ConfigOptions<bool>{
            .key = "dhcp",
            .name = "Use DHCP",
            .category = "wifi",
            .defaultValue = true,
            .categoryPretty = "Network"
        }),
        staticIp(ConfigOptions<String>{
            .key = "ip",
            .name = "Static IP",
            .category = "wifi",
            .defaultValue = String("192.168.2.126"),
            .showIf = [this](){ return !this->useDhcp.get(); },
            .categoryPretty = "Network"
        }),
        gateway(ConfigOptions<String>{
            .key = "gw",
            .name = "Gateway",
            .category = "wifi",
            .defaultValue = String("192.168.2.250"),
            .showIf = [this](){ return !this->useDhcp.get(); },
            .categoryPretty = "Network"
        }),
        subnet(ConfigOptions<String>{
            .key = "mask",
            .name = "Subnet Mask",
            .category = "wifi",
            .defaultValue = String("255.255.255.0"),
            .showIf = [this](){ return !this->useDhcp.get(); },
            .categoryPretty = "Network"
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
    Config<float> MQTTPublischPeriod;
    Config<float> MQTTListenPeriod;
    Config<bool>  enableMQTT; // moved from LimiterSettings

    // dynamic topics (derived)
    String mqtt_publish_setvalue_topic;
    String mqtt_publish_getvalue_topic;
    String mqtt_publish_Temperature_topic;
    String mqtt_publish_Humidity_topic;
    String mqtt_publish_Dewpoint_topic;

    MQTT_Settings() :
        MQTTPublischPeriod(ConfigOptions<float>{
            .key = "pubPer",
            .name = "Publish Period (s)",
            .category = "MQTT",
            .defaultValue = 5.0f,
            .categoryPretty = "MQTT"
        }),
        MQTTListenPeriod(ConfigOptions<float>{
            .key = "subPer",
            .name = "Listen Period (s)",
            .category = "MQTT",
            .defaultValue = 0.5f,
            .categoryPretty = "MQTT"
        }),
        mqtt_port(ConfigOptions<int>{
            .key = "port",
            .name = "Port",
            .category = "MQTT",
            .defaultValue = 1883,
            .categoryPretty = "MQTT"
        }),
        mqtt_server(ConfigOptions<String>{
            .key = "host",
            .name = "Server",
            .category = "MQTT",
            .defaultValue = String("192.168.2.3"),
            .categoryPretty = "MQTT"
        }),
        mqtt_username(ConfigOptions<String>{
            .key = "user",
            .name = "Username",
            .category = "MQTT",
            .defaultValue = String(""),
            .categoryPretty = "MQTT"
        }),
        mqtt_password(ConfigOptions<String>{
            .key = "pass",
            .name = "Password",
            .category = "MQTT",
            .defaultValue = String(""),
            .isPassword = true,
            .categoryPretty = "MQTT"
        }),
        mqtt_sensor_powerusage_topic(ConfigOptions<String>{
            .key = "pwrTop",
            .name = "Power Usage Topic",
            .category = "MQTT",
            .defaultValue = String("emon/emonpi/power1"),
            .categoryPretty = "MQTT"
        }),
        Publish_Topic(ConfigOptions<String>{
            .key = "base",
            .name = "Base Topic",
            .category = "MQTT",
            .defaultValue = String("SolarLimiter"),
            .categoryPretty = "MQTT"
        }),
        enableMQTT(ConfigOptions<bool>{
            .key = "enable",
            .name = "Enable MQTT",
            .category = "MQTT",
            .defaultValue = true,
            .categoryPretty = "MQTT"
        })
    {
        cfg.addSetting(&mqtt_port);
        cfg.addSetting(&mqtt_server);
        cfg.addSetting(&mqtt_username);
        cfg.addSetting(&mqtt_password);
        cfg.addSetting(&mqtt_sensor_powerusage_topic);
        cfg.addSetting(&Publish_Topic);
        cfg.addSetting(&MQTTPublischPeriod);
        cfg.addSetting(&MQTTListenPeriod);
        cfg.addSetting(&enableMQTT);

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
struct LimiterSettings {
    Config<bool> enableController;
    Config<int>  maxOutput;
    Config<int>  minOutput;
    Config<int>  inputCorrectionOffset;
    Config<int>  smoothingSize;
    Config<float> RS232PublishPeriod; // keep here for now
    LimiterSettings() :
        enableController(ConfigOptions<bool>{
            .key = "enable",
            .name = "Limiter Enabled",
            .category = "Limiter",
            .defaultValue = true,
            .categoryPretty = "Limiter"
        }),
        maxOutput(ConfigOptions<int>{
            .key = "maxW",
            .name = "Max Output (W)",
            .category = "Limiter",
            .defaultValue = 1100,
            .categoryPretty = "Limiter"
        }),
        minOutput(ConfigOptions<int>{
            .key = "minW",
            .name = "Min Output (W)",
            .category = "Limiter",
            .defaultValue = 500,
            .categoryPretty = "Limiter"
        }),
        inputCorrectionOffset(ConfigOptions<int>{
            .key = "corr",
            .name = "Input Correction Offset (W)",
            .category = "Limiter",
            .defaultValue = 50,
            .categoryPretty = "Limiter"
        }),
        smoothingSize(ConfigOptions<int>{
            .key = "smooth",
            .name = "Smoothing Level",
            .category = "Limiter",
            .defaultValue = 10,
            .categoryPretty = "Limiter"
        }),
        RS232PublishPeriod(ConfigOptions<float>{
            .key = "rs485Per",
            .name = "RS485 Publish Period (s)",
            .category = "Limiter",
            .defaultValue = 2.0f,
            .categoryPretty = "Limiter"
        })
    {
        cfg.addSetting(&enableController);
        cfg.addSetting(&maxOutput);
        cfg.addSetting(&minOutput);
        cfg.addSetting(&inputCorrectionOffset);
        cfg.addSetting(&smoothingSize);
        cfg.addSetting(&RS232PublishPeriod);
    }
};

struct TempSettings {
    Config<float> tempCorrection;
    Config<float> humidityCorrection;
    Config<int>   seaLevelPressure;
    Config<int>   readIntervalSec;
    TempSettings():
        tempCorrection(ConfigOptions<float>{
            .key = "tCorr",
            .name = "Temperature Correction",
            .category = "Temp",
            .defaultValue = 0.1f,
            .categoryPretty = "Temperature"
        }),
        humidityCorrection(ConfigOptions<float>{
            .key = "hCorr",
            .name = "Humidity Correction",
            .category = "Temp",
            .defaultValue = 0.1f,
            .categoryPretty = "Temperature"
        }),
        seaLevelPressure(ConfigOptions<int>{
            .key = "slp",
            .name = "Sea Level Pressure (hPa)",
            .category = "Temp",
            .defaultValue = 1013,
            .categoryPretty = "Temperature"
        }),
        readIntervalSec(ConfigOptions<int>{
            .key = "readS",
            .name = "Read Interval (s)",
            .category = "Temp",
            .defaultValue = 30,
            .categoryPretty = "Temperature"
        })
    {
        cfg.addSetting(&tempCorrection);
        cfg.addSetting(&humidityCorrection);
        cfg.addSetting(&seaLevelPressure);
        cfg.addSetting(&readIntervalSec);
    }
};

struct I2CSettings {
    Config<int> sdaPin;
    Config<int> sclPin;
    Config<int> busFreq;
    Config<int> bmeFreq;
    Config<int> displayAddr;
    I2CSettings():
        sdaPin(ConfigOptions<int>{
            .key = "sda",
            .name = "SDA Pin",
            .category = "I2C",
            .defaultValue = 21,
            .categoryPretty = "I2C"
        }),
        sclPin(ConfigOptions<int>{
            .key = "scl",
            .name = "SCL Pin",
            .category = "I2C",
            .defaultValue = 22,
            .categoryPretty = "I2C"
        }),
        busFreq(ConfigOptions<int>{
            .key = "freq",
            .name = "Bus Frequency (Hz)",
            .category = "I2C",
            .defaultValue = 400000,
            .categoryPretty = "I2C"
        }),
        bmeFreq(ConfigOptions<int>{
            .key = "bmeHz",
            .name = "BME280 Frequency (Hz)",
            .category = "I2C",
            .defaultValue = 400000,
            .categoryPretty = "I2C"
        }),
        displayAddr(ConfigOptions<int>{
            .key = "disp",
            .name = "Display Address",
            .category = "I2C",
            .defaultValue = 0x3C,
            .categoryPretty = "I2C"
        })
    {
        cfg.addSetting(&sdaPin);
        cfg.addSetting(&sclPin);
        cfg.addSetting(&busFreq);
        cfg.addSetting(&bmeFreq);
        cfg.addSetting(&displayAddr);
    }
};

struct FanSettings {
    Config<bool>  enabled;
    Config<float> onThreshold;
    Config<float> offThreshold;
    Config<int>   relayPin;
    Config<bool>  activeLow;
    FanSettings():
        enabled(ConfigOptions<bool>{
            .key = "en",
            .name = "Enable Fan Control",
            .category = "Fan",
            .defaultValue = true,
            .categoryPretty = "Fan"
        }),
        onThreshold(ConfigOptions<float>{
            .key = "on",
            .name = "Fan ON above (°C)",
            .category = "Fan",
            .defaultValue = 30.0f,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Fan"
        }),
        offThreshold(ConfigOptions<float>{
            .key = "off",
            .name = "Fan OFF below (°C)",
            .category = "Fan",
            .defaultValue = 27.0f,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Fan"
        }),
        relayPin(ConfigOptions<int>{
            .key = "pin",
            .name = "Relay GPIO",
            .category = "Fan",
            .defaultValue = 23,
            .categoryPretty = "Fan"
        }),
        activeLow(ConfigOptions<bool>{
            .key = "low",
            .name = "Active LOW",
            .category = "Fan",
            .defaultValue = true,
            .categoryPretty = "Fan"
        })
    {
        cfg.addSetting(&enabled);
        cfg.addSetting(&onThreshold);
        cfg.addSetting(&offThreshold);
        cfg.addSetting(&relayPin);
        cfg.addSetting(&activeLow);
    }
};

struct HeaterSettings {
    Config<bool>  enabled;
    Config<float> onTemp;
    Config<float> offTemp;
    Config<int>   relayPin;
    Config<bool>  activeLow;
    HeaterSettings():
        enabled(ConfigOptions<bool>{
            .key = "en",
            .name = "Enable Heater Control",
            .category = "Heater",
            .defaultValue = false,
            .categoryPretty = "Heater"
        }),
        onTemp(ConfigOptions<float>{
            .key = "on",
            .name = "Heater ON below (°C)",
            .category = "Heater",
            .defaultValue = 0.0f,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Heater"
        }),
        offTemp(ConfigOptions<float>{
            .key = "off",
            .name = "Heater OFF above (°C)",
            .category = "Heater",
            .defaultValue = 0.5f,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Heater"
        }),
        relayPin(ConfigOptions<int>{
            .key = "pin",
            .name = "Relay GPIO",
            .category = "Heater",
            .defaultValue = 33,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Heater"
        }),
        activeLow(ConfigOptions<bool>{
            .key = "low",
            .name = "Active LOW",
            .category = "Heater",
            .defaultValue = true,
            .showIf = [this](){return this->enabled.get();},
            .categoryPretty = "Heater"
        })
    {
        cfg.addSetting(&enabled);
        cfg.addSetting(&onTemp);
        cfg.addSetting(&offTemp);
        cfg.addSetting(&relayPin);
        cfg.addSetting(&activeLow);
    }
};

struct DisplaySettings {
    Config<bool> turnDisplayOff;
    Config<int>  onTimeSec;
    DisplaySettings():
        turnDisplayOff(ConfigOptions<bool>{
            .key = "sleep",
            .name = "Turn Display Off",
            .category = "Display",
            .defaultValue = true,
            .categoryPretty = "Display"
        }),
        onTimeSec(ConfigOptions<int>{
            .key = "onS",
            .name = "On Time (s)",
            .category = "Display",
            .defaultValue = 60,
            .categoryPretty = "Display"
        })
    {
        cfg.addSetting(&turnDisplayOff);
        cfg.addSetting(&onTimeSec);
    }
};

struct SystemSettings {
    Config<bool> allowOTA;
    Config<String> otaPassword;
    Config<int> wifiRebootTimeoutMin;
    Config<bool> unconfigured;
    Config<String> version;
    SystemSettings():
        allowOTA(ConfigOptions<bool>{
            .key = "ota",
            .name = "Allow OTA Updates",
            .category = "System",
            .defaultValue = true,
            .categoryPretty = "System"
        }),
        otaPassword(ConfigOptions<String>{
            .key = "otaPwd",
            .name = "OTA Password",
            .category = "System",
            .defaultValue = String(""),
            .isPassword = true,
            .categoryPretty = "System"
        }),
        wifiRebootTimeoutMin(ConfigOptions<int>{
            .key = "rbMin",
            .name = "Reboot if WiFi lost (min)",
            .category = "System",
            .defaultValue = 15,
            .categoryPretty = "System"
        }),
        unconfigured(ConfigOptions<bool>{
            .key = "unconf",
            .name = "Unconfigured",
            .category = "System",
            .defaultValue = true,
            .showInWeb = false,
            .categoryPretty = "System"
        }),
        version(ConfigOptions<String>{
            .key = "ver",
            .name = "Program Version",
            .category = "System",
            .defaultValue = String(VERSION),
            .showInWeb = true,
            .categoryPretty = "System"
        })
    {
        cfg.addSetting(&allowOTA);
        cfg.addSetting(&otaPassword);
        cfg.addSetting(&wifiRebootTimeoutMin);
        cfg.addSetting(&unconfigured);
        cfg.addSetting(&version);
    }
};

struct ButtonSettings {
    Config<int> apModePin;
    Config<int> resetDefaultsPin;
    ButtonSettings():
        apModePin(ConfigOptions<int>{
            .key = "ap",
            .name = "AP Mode Button GPIO",
            .category = "Buttons",
            .defaultValue = 13,
            .categoryPretty = "Buttons"
        }),
        resetDefaultsPin(ConfigOptions<int>{
            .key = "rst",
            .name = "Reset Defaults Button GPIO",
            .category = "Buttons",
            .defaultValue = 15,
            .categoryPretty = "Buttons"
        })
    {
        cfg.addSetting(&apModePin);
        cfg.addSetting(&resetDefaultsPin);
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
extern LimiterSettings limiterSettings;
extern TempSettings tempSettings;
extern I2CSettings i2cSettings;
extern FanSettings fanSettings;
extern HeaterSettings heaterSettings;
extern DisplaySettings displaySettings;
extern SystemSettings systemSettings;
extern ButtonSettings buttonSettings;
extern RS485_Settings rs485settings;
extern SigmaLogLevel logLevel;
extern WiFi_Settings wifiSettings;
// extern LDR_Settings ldrSettings;

#endif // SETTINGS_H
