#include <Preferences.h>
#include "config/config.h"
#include "logging/logging.h"

Config::Config()
{
    int currentMajor = 0, currentMinor = 0, currentPatch = 0;
    int major = 0, minor = 0, patch = 0;

    String version = Preferences().getString("version", "0.0.0");

    if (version == "0.0.0") // there is no version saved, set the default version
    {
        saveSettingsFlag = true;
        saveSettingsToEEPROM();
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
        saveSettingsFlag = true;
        saveSettingsToEEPROM();
        
        delay(10000);  // Wait for 10 seconds to avoid multiple resets
        ESP.restart(); // Restart the ESP32
    }

}

void Config::load()
{
  
    loadSettingsFromEEPROM();
}

void Config::save()
{
    saveSettingsToEEPROM();
    saveSettingsFlag = false;
}

void Config::loadSettingsFromEEPROM()
{
    log(" ");
    log("<----------------------------------");
    log("Loading settings from EEPROM...");
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

    mqttSettings.mqtt_server = prefs.getString("mqtt_server", mqttSettings.mqtt_server);
    mqttSettings.mqtt_username = prefs.getString("mqtt_user", mqttSettings.mqtt_username);
    mqttSettings.mqtt_password = prefs.getString("mqtt_pass", mqttSettings.mqtt_password);
    mqttSettings.mqtt_hostname = prefs.getString("mqtt_host", mqttSettings.mqtt_hostname);
    mqttSettings.mqtt_port = prefs.getInt("mqtt_port", mqttSettings.mqtt_port);

    generalSettings.enableController = prefs.getBool("enableCtrl", generalSettings.enableController);
    generalSettings.maxOutput = prefs.getInt("maxOutput", generalSettings.maxOutput);
    generalSettings.minOutput = prefs.getInt("minOutput", generalSettings.minOutput);
    generalSettings.inputCorrectionOffset = prefs.getInt("offset", generalSettings.inputCorrectionOffset);
    generalSettings.MQTTPublischPeriod = prefs.getFloat("Pub", generalSettings.MQTTPublischPeriod);
    generalSettings.MQTTListenPeriod = prefs.getFloat("Listen", generalSettings.MQTTListenPeriod);
    generalSettings.RS232PublishPeriod = prefs.getFloat("RS232Pub", generalSettings.RS232PublishPeriod);
    generalSettings.smoothingSize = prefs.getFloat("smoothing", generalSettings.smoothingSize);
    generalSettings.Version = prefs.getString("version", generalSettings.Version);

    prefs.end();
    printSettings(); // print the settings to the serial monitor
    log("<----------------------------------");
    log(" ");
}

void Config::saveSettingsToEEPROM()
{
    log(" ");
    log("---------------------------------->");
    log("Saving settings to EEPROM...");
    Preferences prefs;
    prefs.begin("config", false);
    //todo: check if the settings are changed before saving them to EEPROM
    if (!saveSettingsFlag)
        {
           log("Settings not changed, skipping save to EEPROM.");
           prefs.end();
              return;
        }
    prefs.putString("version", VERSION); // save the version to EEPROM
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

    prefs.putString("mqtt_server", mqttSettings.mqtt_server);
    prefs.putString("mqtt_user", mqttSettings.mqtt_username);
    prefs.putString("mqtt_pass", mqttSettings.mqtt_password);
    prefs.putString("mqtt_host", mqttSettings.mqtt_hostname);
    prefs.putInt("mqtt_port", mqttSettings.mqtt_port);

    prefs.putBool("enableCtrl", generalSettings.enableController);
    prefs.putInt("maxOutput", generalSettings.maxOutput);
    prefs.putInt("minOutput", generalSettings.minOutput);
    prefs.putInt("offset", generalSettings.inputCorrectionOffset);
    prefs.putFloat("Pub", generalSettings.MQTTPublischPeriod);
    prefs.putFloat("Listen", generalSettings.MQTTListenPeriod);
    prefs.putFloat("RS232Pub", generalSettings.RS232PublishPeriod);
    prefs.putInt("smoothing", generalSettings.smoothingSize);

    prefs.end();
    saveSettingsFlag = false;
    printSettings(); // print the settings to the serial monitor
    log("done Saving settings to EEPROM...");
    log("---------------------------------->");
    log(" ");
}

void Config::removeSettings(char *Name)
{
    log(" ");
    log("removeSettings(%s)...", Name);
    log("---------------------------------->");
    Preferences prefs;
    prefs.begin("config", false);
    prefs.remove(Name);
    prefs.end();
    log("Removed setting from EEPROM: %s", Name);
    printSettings(); // print the settings to the serial monitor
    log("---------------------------------->");
    log(" ");
}

void Config::removeAllSettings()
{
    log(" ");
    log("removeAllSettings()...");
    log("---------------------------------->");
    Preferences prefs;
    prefs.begin("config", false);
    prefs.clear(); // Löscht ALLE Werte im Namespace
    prefs.end();
    log("Removed ALL setting from EEPROM:");
    saveSettingsFlag = false;
    log("---------------------------------->");
    log(" ");
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
    log("maxOutput: %d", generalSettings.maxOutput);
    log("minOutput: %d", generalSettings.minOutput);
    log("inputCorrectionOffset: %d", generalSettings.inputCorrectionOffset);
    log("enableController: %s", generalSettings.enableController ? "true" : "false");
    log("MQTTPublischPeriod: %f", generalSettings.MQTTPublischPeriod);
    log("MQTTListenPeriod: %f", generalSettings.MQTTListenPeriod);
    log("RS232PublishPeriod: %f", generalSettings.RS232PublishPeriod);
    log("smoothingSize: %d", generalSettings.smoothingSize);

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
    log("MQTT-Server: %s", mqttSettings.mqtt_server.c_str());
    log("MQTT-User: ****");
    log("MQTT-Password: ****");
    log("MQTT-Hostname: %s", mqttSettings.mqtt_hostname.c_str());
    log("MQTT-Port: %d", mqttSettings.mqtt_port);
    log("MQTT-Sensor for actual powerusage: %s", mqttSettings.mqtt_sensor_powerusage_topic.c_str());
    log("MQTT-Publish-SetValue-Topic: %s", mqttSettings.mqtt_publish_setvalue_topic.c_str());
    log("MQTT-Publish-GetValue-Topic: %s", mqttSettings.mqtt_publish_getvalue_topic.c_str());

    log(" ");
    log("Temp:");
    log("------------------");
    log("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false");
    log("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    log(" ");
}
