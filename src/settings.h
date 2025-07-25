#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

/*
                                  __________________________ 
                                  | .--------------------. |
                                  | .   ~~~~~~~~~~~~~~   . |
                                  | .   ~~~~~~~~~~~~~~   . |
VP   ✅ ADC1 (RO)                | [ ]                 [x] | D23 <— Used for Fan-Relay
VN   ✅ ADC1 (RO)                | [ ]                 [x] | D22 <— I2C_SCL
D34  ✅ ADC1 (RO)                | [ ]                 [ ] | GPIO1 (TXD) <— UART TX (⚠️ avoid if possible)
D35  ✅ ADC1 (RO)                | [ ]                 [ ] | GPIO3 (RXD) <— UART RX (⚠️ avoid if possible)
D32  ✅ ADC1                     | [ ]                 [x] | D21 <— I2C_SDA
D33  ✅ ADC1                     | [ ]                 [x] | D19 ❌ no ADC (Tx Pin for Serial2 = RS232-to-RS485)
D25  ⚠️ ADC2                     | [ ]                 [x] | D18 ❌ no ADC (Rx Pin for Serial2 = RS232-to-RS485)
D26  ⚠️ ADC2                     | [ ]                 [ ] | D5  ❌ no ADC
D27  ⚠️ ADC2                     | [ ]                 [ ] | D17 ❌ no ADC
D14  ⚠️ ADC2                     | [ ]                 [ ] | D16 ❌ no ADC
D12  ⚠️ ADC2 (boot pin)          | [ ]                 [ ] | D4  ⚠️ ADC2 (boot pin)
D13  ❌ no ADC (Btn-AP-Mode)     | [x]                 [x] | D15 ⚠️ ADC2 (boot pin - must be LOW on boot) (Used for Reset Button)
EN                                | [ ]                 [ ] | GND
VIN (5V!)                         | [ ]                 [ ] | 3V3
                                  |                         |
                                  |   [pwr-LED]  [D2-LED]   |               
                                  |        _______          |
                                  |        |     |          |
                                  '--------|-----|---------'

Legend:
[x] in use    - Pin is already used in your project
✅ ADC1        - 12-bit ADC, usable even with WiFi
✅ ADC1 (RO)   - Input-only pins with ADC1 (GPIO36–39)
⚠️ ADC2        - 12-bit ADC, unusable when WiFi is active
❌ no ADC     - No analog capability
⚠️ Boot pin   - Must be LOW or unconnected at boot to avoid boot failure

Notes:
- GPIO0, GPIO2, GPIO12, GPIO15 are boot strapping pins — avoid pulling HIGH at boot.
- GPIO6–11 are used for internal flash – **never use**.
- GPIO1 (TX) and GPIO3 (RX) are used for serial output – use only if UART0 not needed.

- EN Turn to Low to reset the ESP32. On goint High, the ESP32 boots.
- VIN (5V!) is the power input pin, connect to 5V.
- VP (GPIO36) ADC1 (RO) No Pull-up/down possible.
- VN (GPIO39) ADC1 (RO) No Pull-up/down possible.

UART	TX	RX
UART0	GPIO1	GPIO3
UART1	GPIO10	GPIO9
UART2	GPIO17	GPIO16


*/



#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SigmaLoger.h>

#include "ConfigManager.h"

#define VERSION "0.7.0"           // version of the software (major.minor.patch) (Version 0.6.0 Has Breaking Changes!)
#define VERSION_DATE "23.05.2025" // date of the version

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

// #define RELAY_MOTOR_UP_PIN 14
// #define RELAY_MOTOR_DOWN_PIN 27
// #define RELAY_MOTOR_LEFT_PIN 26
// #define RELAY_MOTOR_RIGHT_PIN 25

// #define JOYSTICK_X_PIN 35 // GPIO pin for the joystick X-axis
// #define JOYSTICK_Y_PIN 32 // GPIO pin for the joystick Y-axis

#define RELAY_VENTILATOR_PIN 23 // GPIO pin for the ventilator (if used, otherwise not needed)
// #define RELAY_HEATER_PIN 34 // GPIO pin for the Heater (if used, otherwise not needed)

// #define LDR_PIN1 33 //Top left      //analog pin for the first LDR
// #define LDR_PIN2 34 //Top right     //analog pin for the second LDR   
// #define LDR_PIN3 36 //Bottom left   //vp
// #define LDR_PIN4 39 //Bottom right  //vn


extern ConfigManagerClass cfg;// store it globaly before using it in the settings
//--------------------------------------------------------------------------------------------------------------

struct WiFi_Settings //wifiSettings
{
    Config<String> wifiSsid;
    Config<String> wifiPassword;
    Config<bool> useDhcp;
    WiFi_Settings() :

                      wifiSsid("ssid", "wifi", "MyWiFi"),
                      wifiPassword("password", "wifi", "secretpass", true, true),
                      useDhcp("dhcp", "network", false)

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
    MQTT_Settings() : mqtt_port("Port", "MQTT", 1883),
                      mqtt_server("Server", "MQTT", "192.168.2.3"),
                      mqtt_username("User", "MQTT", "housebattery"),
                      mqtt_password("Pass", "MQTT", "mqttsecret", true, true),
                      mqtt_sensor_powerusage_topic("PowerUsage", "MQTT", "emon/emonpi/power1")
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
    // Config<int> XjoystickOffset;
    // Config<int> YjoystickOffset;
    // Config<int> OnOffThreshold;
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

    General_Settings() : enableController("enCtrl", "GS", true),
                         enableMQTT("enMQTT", "GS", true),
                         maxOutput("MaxO", "GS", 1100),
                         minOutput("MinO", "GS", 500),
                         inputCorrectionOffset("ICO", "GS", 50),
                         MQTTPublischPeriod("MQTTP", "GS", 5.0),
                         MQTTListenPeriod("MQTTL", "GS", 0.5),
                         RS232PublishPeriod("RS232P", "GS", 2.0),
                         smoothingSize("Smooth", "GS", 10),
                         TempCorrectionOffset("TCO_TempratureCorrectionOffset", "GS", 0.0),
                         HumidityCorrectionOffset("HYO_HumidityCorrectionOffset", "GS", 0.0),
                        //  XjoystickOffset("X_Joystick-Offset-X", "Joystick", 0),
                        //  YjoystickOffset("Y_Joystick-Offset-Y", "Joystick", 0),
                        //  OnOffThreshold("Joystick-OnOffThreshold", "Joystick", 80),
                            VentilatorOn("VentilatorOn", "Ventilator", 30.0), // Ventilator ON threshold in watts
                            VentilatorOFF("VentilatorOFF", "Ventilator", 27.0), // Ventilator OFF threshold in watts
                            VentilatorEnable("VentilatorEnable", "Ventilator", true), // Enable or disable the ventilator control
                            unconfigured("Unconfigured", "GS", true, false,false), // flag to indicate if the device is unconfigured (default is true)
                         Version("Version", "GS", VERSION)
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
        cfg.addSetting(&smoothingSize);
        cfg.addSetting(&TempCorrectionOffset);
        cfg.addSetting(&HumidityCorrectionOffset);
        // cfg.addSetting(&XjoystickOffset);
        // cfg.addSetting(&YjoystickOffset);
        // cfg.addSetting(&OnOffThreshold);
        cfg.addSetting(&VentilatorOn);
        cfg.addSetting(&VentilatorOFF);
        cfg.addSetting(&VentilatorEnable);
        cfg.addSetting(&Version);
        cfg.addSetting(&unconfigured);
    }
};


// struct LDR_Settings // ldrSettings
// {
//     Config<int> ldr1; // LDR 1
//     Config<int> ldr2; // LDR 2
//     Config<int> ldr3; // LDR 3
//     Config<int> ldr4; // LDR 4

//     LDR_Settings() : ldr1("LDR1", "LDR", 0),
//                      ldr2("LDR2", "LDR", 0),
//                      ldr3("LDR3", "LDR", 0),
//                      ldr4("LDR4", "LDR", 0)
//     {
//         cfg.addSetting(&ldr1);
//         cfg.addSetting(&ldr2);
//         cfg.addSetting(&ldr3);
//         cfg.addSetting(&ldr4);
//     }
// };

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
