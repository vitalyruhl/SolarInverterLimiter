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

    sl->Printf("Current version: %s", VERSION).Debug();
    sll->Printf("Cur. Version: %s", VERSION).Debug();
    sl->Printf("Current Version_Date: %s", VERSION_DATE).Debug();
    sll->Printf("from: %s", VERSION_DATE).Debug();
    sl->Printf("Saved version: %s", version.c_str()).Debug();

    sscanf(version.c_str(), "%d.%d.%d", &major, &minor, &patch);
    sscanf(VERSION, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

    if (currentMajor != major || currentMinor != minor)
    {
        sl->Printf("Version changed, removing all settings...").Debug();
        sll->Printf("Version changed, removing all settings...").Debug();
        removeAllSettings();
        saveSettingsFlag = true;
        saveSettingsToEEPROM();
        
        sll->Printf("restarting...").Debug();
        delay(10000);  // Wait for 10 seconds to avoid multiple resets

        ESP.restart(); // Restart the ESP32
    }

}

void Config::load()
{
  sll->Printf("Config::load()...").Debug();
    loadSettingsFromEEPROM();
}

void Config::save()
{
    sll->Printf("Config::save()...").Debug();
    saveSettingsToEEPROM();
    saveSettingsFlag = false;
}

void Config::loadSettingsFromEEPROM()
{
    sl->Printf("<----------------------------------").Debug();
    sl->Printf("Loading settings from EEPROM...").Debug();
    sll->Printf("Loading from EEPROM...").Debug();
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
    sl->Printf("<----------------------------------").Debug();
}

void Config::saveSettingsToEEPROM()
{
    sll->Printf("saveSettingsToEEPROM").Debug();
    sl->Printf("---------------------------------->").Debug();
    sl->Printf("Saving settings to EEPROM...").Debug();
    Preferences prefs;
    prefs.begin("config", false);
    //todo: check if the settings are changed before saving them to EEPROM
    if (!saveSettingsFlag)
        {
           sl->Printf("Settings not changed, skipping save to EEPROM.saveSettingsFlag:0").Debug();
           sll->Printf("No Savingflag!").Debug();
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
    sl->Printf("done Saving settings to EEPROM...").Debug();
    sl->Printf("---------------------------------->").Debug();
    sl->Printf(" ").Debug();
    sll->Printf("Saved to EEPROM").Debug();
}

void Config::removeSettings(char *Name)
{
    sl->Printf(" ").Debug();
    sl->Printf("removeSettings(%s)...", Name).Debug();
    sl->Printf("---------------------------------->").Debug();
    Preferences prefs;
    prefs.begin("config", false);
    prefs.remove(Name);
    prefs.end();
    sl->Printf("Removed setting from EEPROM: %s", Name).Debug();
    printSettings(); // print the settings to the serial monitor
    sl->Printf("---------------------------------->").Debug();
    sl->Printf(" ").Debug();
}

void Config::removeAllSettings()
{
    sl->Printf(" ").Debug();
    sl->Printf("removeAllSettings()...").Debug();
    sl->Printf("---------------------------------->").Debug();
    Preferences prefs;
    prefs.begin("config", false);
    prefs.clear(); // Löscht ALLE Werte im Namespace
    prefs.end();
    sl->Printf("Removed ALL setting from EEPROM:").Debug();
    saveSettingsFlag = false;
    sl->Printf("---------------------------------->").Debug();
    sl->Printf(" ").Debug();
}

void Config::printSettings()
{
    sl->Printf(" ").Debug();
    sl->Printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++").Debug();
    sl->Printf(" ").Debug();
    sl->Printf("Version: %s", VERSION).Debug();
    sl->Printf("Version_Date: %s", VERSION_DATE).Debug();

    sl->Printf(" ").Debug();
    sl->Printf("General Settings:").Debug();
    sl->Printf("------------------").Debug();
    sl->Printf("maxOutput: %d", generalSettings.maxOutput).Debug();
    sl->Printf("minOutput: %d", generalSettings.minOutput).Debug();
    sl->Printf("inputCorrectionOffset: %d", generalSettings.inputCorrectionOffset).Debug();
    sl->Printf("enableController: %s", generalSettings.enableController ? "true" : "false").Debug();
    sl->Printf("MQTTPublischPeriod: %f", generalSettings.MQTTPublischPeriod).Debug();
    sl->Printf("MQTTListenPeriod: %f", generalSettings.MQTTListenPeriod).Debug();
    sl->Printf("RS232PublishPeriod: %f", generalSettings.RS232PublishPeriod).Debug();
    sl->Printf("smoothingSize: %d", generalSettings.smoothingSize).Debug();

    sl->Printf(" ").Debug();
    sl->Printf("WiFi-Settings:").Debug();
    sl->Printf("------------------").Debug();
    sl->Printf("WiFi-SSID: %s", wifi_config.ssid.c_str()).Debug();
    sl->Printf("WiFi-Password: ****").Debug();
    sl->Printf("WiFi-Failover-SSID: %s", wifi_config.failover_ssid.c_str()).Debug();
    sl->Printf("WiFi-Failover-Password: ****").Debug();
    sl->Printf("WiFi-AP-SSID: %s", wifi_config.apSSID.c_str()).Debug();
    sl->Printf("WiFi-Static-IP: %s", wifi_config.staticIP.c_str()).Debug();
    sl->Printf("WiFi-Static-Subnet: %s", wifi_config.staticSubnet.c_str()).Debug();
    sl->Printf("WiFi-Static-Gateway: %s", wifi_config.staticGateway.c_str()).Debug();
    sl->Printf("WiFi-Static-DNS: %s", wifi_config.staticDNS.c_str()).Debug();
    sl->Printf("WiFi-Use-Static-IP: %s", wifi_config.use_static_ip ? "true" : "false").Debug();

    sl->Printf(" ").Debug();
    sl->Printf("MQTT-Settings:").Debug();
    sl->Printf("------------------").Debug();
    sl->Printf("MQTT-Server: %s", mqttSettings.mqtt_server.c_str()).Debug();
    sl->Printf("MQTT-User: ****").Debug();
    sl->Printf("MQTT-Password: ****").Debug();
    sl->Printf("MQTT-Hostname: %s", mqttSettings.mqtt_hostname.c_str()).Debug();
    sl->Printf("MQTT-Port: %d", mqttSettings.mqtt_port).Debug();
    sl->Printf("MQTT-Sensor for actual powerusage: %s", mqttSettings.mqtt_sensor_powerusage_topic.c_str()).Debug();
    sl->Printf("MQTT-Publish-SetValue-Topic: %s", mqttSettings.mqtt_publish_setvalue_topic.c_str()).Debug();
    sl->Printf("MQTT-Publish-GetValue-Topic: %s", mqttSettings.mqtt_publish_getvalue_topic.c_str()).Debug();

    sl->Printf(" ").Debug();
    sl->Printf("Temp:").Debug();
    sl->Printf("------------------").Debug();
    sl->Printf("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false").Debug();
    sl->Printf("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++").Debug();
    sl->Printf(" ").Debug();
}
