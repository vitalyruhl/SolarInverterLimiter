#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "2.4.1"           // version of the software (major.minor.patch)
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
            .category = "wifi",
            .defaultValue = false,
            .prettyName = "Use DHCP",
            .prettyCat = "Network Settings"
        }),
        staticIp(ConfigOptions<String>{
            .keyName = "sIP",
            .category = "wifi",
            .defaultValue = String("192.168.2.126"),
            .prettyName = "Static IP",
            .prettyCat = "Network Settings",
            .showIf = [this](){ return !this->useDhcp.get(); }
        }),
        gateway(ConfigOptions<String>{
            .keyName = "GW",
            .category = "wifi",
            .defaultValue = String("192.168.2.250"),
            .prettyName = "Gateway",
            .prettyCat = "Network Settings",
            .showIf = [this](){ return !this->useDhcp.get(); }
        }),
        subnet(ConfigOptions<String>{
            .keyName = "subnet",
            .category = "wifi",
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
            .keyName = "MQTTP", .category = "MQTT", .defaultValue = 5.0f, .prettyName = "MQTT Publishing Period", .prettyCat = "MQTT-Section"
        }),
        MQTTListenPeriod(ConfigOptions<float>{
            .keyName = "MQTTL", .category = "MQTT", .defaultValue = 0.5f, .prettyName = "MQTT Listening Period", .prettyCat = "MQTT-Section"
        }),

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
        }),
        enableMQTT(ConfigOptions<bool>{
            .keyName = "enMQTT",
            .category = "MQTT",
            .defaultValue = true,
            .prettyName = "Enable MQTT Propagation",
            .prettyCat = "MQTT-Section"
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
        enableController({"enCtrl","Limiter",true,"Enable Limitation"}),
        maxOutput({"MaxO","Limiter",1100,"Max-Output"}),
        minOutput({"MinO","Limiter",500,"Min-Output"}),
        inputCorrectionOffset({"ICO","Limiter",50,"Correction-Offset"}),
        smoothingSize({"Smooth","Limiter",10,"Smoothing Level"}),
        RS232PublishPeriod({"RS232P","Limiter",2.0f,"RS232 Publish Period"})
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
        tempCorrection({"TCO","Temp",0.1f,"Temperature Correction","Temperature Settings"}),
        humidityCorrection({"HYO","Temp",0.1f,"Humidity Correction","Temperature Settings"}),
        seaLevelPressure({"SLP","Temp",1013,"Sea Level Pressure","Temperature Settings"}),
        readIntervalSec({"ReadTemp","Temp",30,"Read Temp/Humidity every (s)","Temperature Settings"})
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
        sdaPin({"I2CSDA","I2C",21,"I2C SDA Pin","I2C"}),
        sclPin({"I2CSCL","I2C",22,"I2C SCL Pin","I2C"}),
        busFreq({"I2CFreq","I2C",400000,"I2C Bus Freq","I2C"}),
        bmeFreq({"BMEFreq","I2C",400000,"BME280 Bus Freq","I2C"}),
        displayAddr({"DispAddr","I2C",0x3C,"Display I2C Address","I2C"})
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
            .keyName = "VentEn",
            .category = "FAN",
            .defaultValue = true,
            .prettyName = "Enable Fan Control",
            .prettyCat = "FAN Control"
        }),
        onThreshold(ConfigOptions<float>{
            .keyName = "VentOn",
            .category = "FAN",
            .defaultValue = 30.0f,
            .prettyName = "Fan On over",
            .prettyCat = "FAN Control",
            .showInWeb = true,
            .showIf = [this](){return this->enabled.get();}
        }),
        offThreshold(ConfigOptions<float>{
            .keyName = "VentOff",
            .category = "FAN",
            .defaultValue = 27.0f,
            .prettyName = "Fan Off under",
            .prettyCat = "FAN Control",
            .showInWeb = true,
            .showIf = [this](){return this->enabled.get();}
        }),
        relayPin(ConfigOptions<int>{
            .keyName = "RlfPin",
            .category = "FAN",
            .defaultValue = 23,
            .prettyName = "Fan Relay GPIO",
            .prettyCat = "FAN Control"
        }),
        activeLow(ConfigOptions<bool>{
            .keyName = "RlfLow",
            .category = "FAN",
            .defaultValue = true,
            .prettyName = "Fan Active LOW",
            .prettyCat = "FAN Control"
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
            .keyName = "HeatEn",
            .category = "Heater",
            .defaultValue = false,
            .prettyName = "Enable Heater Control",
            .prettyCat = "Heater Control"
        }),
        onTemp(ConfigOptions<float>{
            .keyName = "HOn",
            .category = "Heater",
            .defaultValue = 0.0f,
            .prettyName = "Heater ON below",
            .prettyCat = "Heater Control",
            .showIf = [this](){return this->enabled.get();}
        }),
        offTemp(ConfigOptions<float>{
            .keyName = "HOff",
            .category = "Heater",
            .defaultValue = 0.5f,
            .prettyName = "Heater OFF above",
            .prettyCat = "Heater Control",
            .showIf = [this](){return this->enabled.get();}
        }),
        
        relayPin(ConfigOptions<int>{
            .keyName = "RlhPin",
            .category = "Heater",
            .defaultValue = 33,
            .prettyName = "Heater Relay GPIO",
            .prettyCat = "Heater Control",
            .showIf = [this](){return this->enabled.get();}
        }),
        activeLow(ConfigOptions<bool>{
            .keyName = "RlhLow",
            .category = "Heater",
            .defaultValue = true,
            .prettyName = "Heater Relay Active LOW",
            .prettyCat = "Heater Control",
            .showInWeb = true,
            .isPassword = false,
            .cb = nullptr,
            .showIf = [this](){return this->enabled.get();}
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
        turnDisplayOff({"DispSave","Display",true,"Turn Display Off","Display Settings"}),
        onTimeSec({"DispTime","Display",60,"Display On-Time (s)","Display Settings"})
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
        allowOTA({"OTAEn","System",true,"Allow OTA Updates"}),
        otaPassword({"OTAPass","System",String("ota1234"),"OTA Password","System",true,true}),
        wifiRebootTimeoutMin(ConfigOptions<int>{
            .keyName = "WiFiRb",
            .category = "System",
            .defaultValue = 15,
            .prettyName = "Reboot if WiFi lost (min)",
            .prettyCat = "System",
            .showInWeb = true
        }),
        unconfigured({"Unconfigured","System",true,"ESP is unconfigured","System", false,false}),
        version({"Version","System",String(VERSION),"Program Version"})
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
        apModePin({"BtnAP","Buttons",13,"AP Mode Button GPIO","Buttons"}),
        resetDefaultsPin({"BtnRst","Buttons",15,"Reset Defaults Button GPIO","Buttons"})
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
