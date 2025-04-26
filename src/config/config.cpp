#include <Preferences.h>
#include "config/config.h"
#include "logging/logging.h"

Config::Config()
{
}

void Config::load()
{
    wifi_config = default_wifi_settings; // set the default values for the wifi settings
    int currentMajor = 0, currentMinor = 0, currentPatch = 0;
    int major = 0, minor = 0, patch = 0;

    String version = Preferences().getString("version", "0.0.0");

    if (version == "0.0.0") // there is no version saved, set the default version
    {
        saveSettingsToEEPROM();
        saveSettingsFlag = false;
        return;
    }

    log("Current version: %s", VERSION);
    log("Current Version_Date: %s", VERSION_DATE);
    log("Saved version: %s", version.c_str());

    sscanf(version.c_str(), "%d.%d.%d", &major, &minor, &patch);
    sscanf(VERSION, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

    if (currentMajor != major || currentMinor != minor)
    {
        log("Version changed, removing all settings...");
        removeAllSettings();
        saveSettingsToEEPROM();
        saveSettingsFlag = false;
        delay(10000);  // Wait for 10 seconds to avoid multiple resets
        ESP.restart(); // Restart the ESP32
    }

    loadSettingsFromEEPROM();
}

void Config::save()
{
    saveSettingsToEEPROM();
    saveSettingsFlag = false;
}

void Config::loadSettingsFromEEPROM()
{
    Preferences prefs;
    prefs.begin("config", true);

    wifi_config.ssid = prefs.getString("wifi_ssid", wifi_config.ssid);
    wifi_config.pass = prefs.getString("wifi_pass", wifi_config.pass);
    wifi_config.failover_ssid = prefs.getString("wifi_fo_ssid", wifi_config.failover_ssid);
    wifi_config.failover_pass = prefs.getString("wifi_fo_pass", wifi_config.failover_pass);
    wifi_config.apSSID = prefs.getString("wifi_AP_ssid", wifi_config.apSSID);
    wifi_config.staticIP = prefs.getString("wifi_IP", wifi_config.staticIP);
    wifi_config.staticSubnet = prefs.getString("wifi_SN", wifi_config.staticSubnet);
    wifi_config.staticGateway = prefs.getString("wifi_GW", wifi_config.staticGateway);
    wifi_config.staticDNS = prefs.getString("wifi_DNS", wifi_config.staticDNS);
    wifi_config.use_static_ip = prefs.getBool("wifi_use_sp", wifi_config.use_static_ip);

    mqtt.mqtt_server = prefs.getString("mqtt_server", mqtt.mqtt_server);
    mqtt.mqtt_username = prefs.getString("mqtt_user", mqtt.mqtt_username);
    mqtt.mqtt_password = prefs.getString("mqtt_pass", mqtt.mqtt_password);
    mqtt.mqtt_hostname = prefs.getString("mqtt_host", mqtt.mqtt_hostname);
    mqtt.mqtt_port = prefs.getInt("mqtt_port", mqtt.mqtt_port);

    general.enableController = prefs.getBool("enableCtrl", general.enableController);
    general.maxOutput = prefs.getInt("maxOutput", general.maxOutput);
    general.minOutput = prefs.getInt("minOutput", general.minOutput);
    general.inputCorrectionOffset = prefs.getInt("offset", general.inputCorrectionOffset);
    general.MQTTPublischPeriod = prefs.getFloat("Pub", general.MQTTPublischPeriod);
    general.MQTTListenPeriod = prefs.getFloat("Listen", general.MQTTListenPeriod);
    general.RS232PublishPeriod = prefs.getFloat("RS232Pub", general.RS232PublishPeriod);
    general.smoothingSize = prefs.getFloat("smoothing", general.smoothingSize);

    prefs.end();
}

void Config::saveSettingsToEEPROM()
{
    Preferences prefs;
    prefs.begin("config", false);

    prefs.putString("wifi_ssid", wifi_config.ssid);
    prefs.putString("wifi_pass", wifi_config.pass);
    prefs.putString("wifi_fo_ssid", wifi_config.failover_ssid);
    prefs.putString("wifi_fo_pass", wifi_config.failover_pass);
    prefs.putString("wifi_AP_ssid", wifi_config.apSSID);
    prefs.putString("wifi_IP", wifi_config.staticIP);
    prefs.putString("wifi_SN", wifi_config.staticSubnet);
    prefs.putString("wifi_GW", wifi_config.staticGateway);
    prefs.putString("wifi_DNS", wifi_config.staticDNS);
    prefs.putBool("wifi_use_sp", wifi_config.use_static_ip);

    prefs.putString("mqtt_server", mqtt.mqtt_server);
    prefs.putString("mqtt_user", mqtt.mqtt_username);
    prefs.putString("mqtt_pass", mqtt.mqtt_password);
    prefs.putString("mqtt_host", mqtt.mqtt_hostname);
    prefs.putInt("mqtt_port", mqtt.mqtt_port);

    prefs.putBool("enableCtrl", general.enableController);
    prefs.putInt("maxOutput", general.maxOutput);
    prefs.putInt("minOutput", general.minOutput);
    prefs.putInt("offset", general.inputCorrectionOffset);
    prefs.putFloat("Pub", general.MQTTPublischPeriod);
    prefs.putFloat("Listen", general.MQTTListenPeriod);
    prefs.putFloat("RS232Pub", general.RS232PublishPeriod);
    prefs.putInt("smoothing", general.smoothingSize);

    prefs.end();
}

void Config::removeSettings(char *Name)
{
    Preferences prefs;
    prefs.begin("config", false);
    prefs.remove(Name);
    prefs.end();
    log("Removed setting from EEPROM: %s", Name);
}

void Config::removeAllSettings()
{
    Preferences prefs;
    prefs.begin("config", false);
    prefs.clear(); // Löscht ALLE Werte im Namespace
    prefs.end();
    log("Removed ALL setting from EEPROM:");
}

void Config::printSettings()
{
    log(" ");
    log("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    log(" ");
    log("Version: %s", VERSION);
    log("Version_Date: %s", VERSION_DATE);

    log(" ");
    log("General Settings:");
    log("------------------");
    log("maxOutput: %d", general.maxOutput);
    log("minOutput: %d", general.minOutput);
    log("inputCorrectionOffset: %d", general.inputCorrectionOffset);
    log("enableController: %s", general.enableController ? "true" : "false");
    log("MQTTPublischPeriod: %f", general.MQTTPublischPeriod);
    log("MQTTSettingsPublischPeriod: %f", general.MQTTSettingsPublischPeriod);
    log("MQTTListenPeriod: %f", general.MQTTListenPeriod);
    log("RS232PublishPeriod: %f", general.RS232PublishPeriod);
    log("smoothingSize: %d", general.smoothingSize);

    log(" ");
    log("WiFi-Settings:");
    log("------------------");
    log("WiFi-SSID: %s", wifi_config.ssid.c_str());
    log("WiFi-Password: ****");
    log("WiFi-Failover-SSID: %s", wifi_config.failover_ssid.c_str());
    log("WiFi-Failover-Password: ****");
    log("WiFi-AP-SSID: %s", wifi_config.apSSID.c_str());
    log("WiFi-Static-IP: %s", wifi_config.staticIP.c_str());
    log("WiFi-Static-Subnet: %s", wifi_config.staticSubnet.c_str());
    log("WiFi-Static-Gateway: %s", wifi_config.staticGateway.c_str());
    log("WiFi-Static-DNS: %s", wifi_config.staticDNS.c_str());
    log("WiFi-Use-Static-IP: %s", wifi_config.use_static_ip ? "true" : "false");

    log(" ");
    log("MQTT-Settings:");
    log("------------------");
    log("MQTT-Server: %s", mqtt.mqtt_server.c_str());
    log("MQTT-User: ****");
    log("MQTT-Password: ****");
    log("MQTT-Hostname: %s", mqtt.mqtt_hostname.c_str());
    log("MQTT-Port: %d", mqtt.mqtt_port);
    log("MQTT-Sensor for actual powerusage: %s", mqtt.mqtt_sensor_powerusage_topic.c_str());
    log("MQTT-Publish-SetValue-Topic: %s", mqtt.mqtt_publish_setvalue_topic.c_str());
    log("MQTT-Publish-GetValue-Topic: %s", mqtt.mqtt_publish_getvalue_topic.c_str());

    log(" ");
    log("Temp:");
    log("------------------");
    log("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false");
    log("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    log(" ");
}
