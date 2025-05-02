#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

// set the I2C address for the BME280 sensor for temperature and humidity
#define I2C_SDA    21
#define I2C_SCL    22
#define I2C_FREQUENCY  400000
#define BME280_FREQUENCY  400000

// WiFi-Setup
struct Wifi_Settings
{
    String ssid = "YourSSID";     // WiFi SSID
    String pass = "YourPassword"; // WiFi password
    String failover_ssid = "YourFailoverSSID";
    String failover_pass = "YourFailoverPassword";
    String apSSID = "HouseBattery_AP"; // Access Point SSID
    String staticIP = "192.168.2.105";
    String staticSubnet = "255.255.255.0";
    String staticGateway = "192.168.2.250";
    String staticDNS = "192.168.2.250";
    bool use_static_ip = false;
    // virtual ~Config_wifi() = default; // Virtual Destructor to make it polymorphic
};


//mqtt-Setup
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
    }
};

// General configuration (default Settings)
struct General_Settings
{
    bool dirtybit = false;                    // dirty bit to indicate if the message has changed
    bool enableController = true;             // set to false to disable the controller and use Maximum power output
    int maxOutput = 1100;                     // edit this to limit TOTAL power output in watts
    int minOutput = 500;                      // minimum output power in watts
    int inputCorrectionOffset = 10;          // ( -80) Current clamp sensors have poor accuracy at low load, a buffer ensures some current flow in the import direction to ensure no exporting. Adjust according to accuracy of meter.
    float MQTTPublischPeriod = 5.0;           // check all x seconds if there is a new MQTT message to publish
    float MQTTListenPeriod = 0.5;             // check x seconds if there is a new MQTT message to listen to
    float RS232PublishPeriod = 2.0;           // send the RS485 Data all x seconds
    int smoothingSize = 10;                    // size of the buffer for smoothing
    String Version;
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
    //todo: add settings for Inverter eg, headder, checksum, etc.
};



#endif // SETTINGS_H
