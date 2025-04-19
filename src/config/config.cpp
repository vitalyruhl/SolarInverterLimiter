#include "config/config.h"
#include "logging/logging.h"
#include <Preferences.h>

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
    Preferences prefs;
    prefs.begin("config", true);

    wifi.ssid = prefs.getString("wifi_ssid", wifi.ssid);
    wifi.pass = prefs.getString("wifi_pass", wifi.pass);

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

    prefs.end();
}

void Config::saveSettingsToEEPROM()
{
    Preferences prefs;
    prefs.begin("config", false);

    prefs.putString("wifi_ssid", wifi.ssid);
    prefs.putString("wifi_pass", wifi.pass);

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

String Config::toJSON()
{
    JsonDocument doc;

    doc["wifi"]["ssid"] = wifi.ssid;
    doc["wifi"]["pass"] = wifi.pass;

    doc["mqtt"]["mqtt_server"] = mqtt.mqtt_server;
    doc["mqtt"]["mqtt_user"] = mqtt.mqtt_username;
    doc["mqtt"]["mqtt_pass"] = mqtt.mqtt_password;
    doc["mqtt"]["mqtt_host"] = mqtt.mqtt_hostname;
    doc["mqtt"]["mqtt_port"] = mqtt.mqtt_port;

    doc["general"]["enableController"] = general.enableController;
    doc["general"]["maxOutput"] = general.maxOutput;
    doc["general"]["minOutput"] = general.minOutput;
    doc["general"]["offset"] = general.inputCorrectionOffset;
    doc["general"]["Pub"] = general.MQTTPublischPeriod;
    doc["general"]["Listen"] = general.MQTTListenPeriod;
    doc["general"]["RS232Pub"] = general.RS232PublishPeriod;
    doc["general"]["smoothing"] = general.smoothingSize;

    String output;
    serializeJson(doc, output);
    return output;
}

void Config::fromJSON(const String &json)
{
    JsonDocument doc;
    deserializeJson(doc, json);

    wifi.ssid = doc["wifi"]["ssid"].as<String>();
    wifi.pass = doc["wifi"]["pass"].as<String>();

    mqtt.mqtt_server = doc["mqtt"]["mqtt_server"].as<String>();
    mqtt.mqtt_username = doc["mqtt"]["mqtt_user"].as<String>();
    mqtt.mqtt_password = doc["mqtt"]["mqtt_pass"].as<String>();
    mqtt.mqtt_hostname = doc["mqtt"]["mqtt_host"].as<String>();
    mqtt.mqtt_port = doc["mqtt"]["mqtt_port"].as<int>();

    general.enableController = doc["general"]["enableController"].as<bool>();
    general.maxOutput = doc["general"]["maxOutput"].as<int>();
    general.minOutput = doc["general"]["minOutput"].as<int>();
    general.inputCorrectionOffset = doc["general"]["offset"].as<int>();
    general.MQTTPublischPeriod = doc["general"]["Pub"].as<float>();
    general.MQTTListenPeriod = doc["general"]["Listen"].as<float>();
    general.RS232PublishPeriod = doc["general"]["RS232Pub"].as<float>();
    general.smoothingSize = doc["general"]["smoothing"].as<int>();
}

void Config::attachWebEndpoint(WebServer &server)
{
    server.on("/config", HTTP_GET, [&]()
              { server.send(200, "application/json", toJSON()); });

    server.on("/config", HTTP_POST, [&]()
              {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing body");
            return;
        }
        fromJSON(server.arg("plain"));
        save();
        server.send(200, "text/plain", "Config gespeichert."); });
} // Ende der Datei

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
    log("------------------");
    log("General Settings:");
    log("Version: %s", VERSION);
    log("Version_Date: %s", VERSION_DATE);
    log(" ");
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
    log("------------------");
    log("MQTT-Settings:");
    log("MQTT-Server: %s", mqtt.mqtt_server.c_str());
    log("MQTT-User: ****");
    log("MQTT-Password: ****");
    log("MQTT-Hostname: %s", mqtt.mqtt_hostname.c_str());
    log("MQTT-Port: %d", mqtt.mqtt_port);
    log("MQTT-Sensor for actual powerusage: %s", mqtt.mqtt_sensor_powerusage_topic.c_str());
    log("MQTT-Publish-SetValue-Topic: %s", mqtt.mqtt_publish_setvalue_topic.c_str());
    log("MQTT-Publish-GetValue-Topic: %s", mqtt.mqtt_publish_getvalue_topic.c_str());

    log(" ");
    log("------------------");
    log("WiFi-Settings:");
    log("WiFi-SSID: %s", wifi.ssid.c_str());
    log("WiFi-Pass:***");

    log("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false");

    log("------------------");
    log(" ");
}

//// - OLD Version::::

// void Config::loadSettings(GeneralSettings &generalSettings)
// {
//     logv("-------------> Loading configuration settings...");
//     loadSettingsFromEEPROM(generalSettings);
//     generalSettings.dirtybit = true;
//     delay(1000);
//     printSettings(generalSettings);
// }

// void Config::saveSettings(GeneralSettings &generalSettings)
// {
//     logv("-------------> Saving configuration settings...");
//     saveSettingsToEEPROM(generalSettings);
//     delay(2000);
//     printSettings(generalSettings);
// }

// //-----------------------------------------------------
// // private functions

// void Config::printSettings(GeneralSettings &generalSettings)
// {
//     log(" ");
//     log("------------------");
//     log("Settings:");
//     log("dirtybit: %s", generalSettings.dirtybit ? "true" : "false");
//     log("maxOutput: %d", generalSettings.maxOutput);
//     log("minOutput: %d", generalSettings.minOutput);
//     log("inputCorrectionOffset: %d", generalSettings.inputCorrectionOffset);
//     log("enableController: %s", generalSettings.enableController ? "true" : "false");
//     log("MQTT-Sensor for actual powerusage: %s", MQTT_SENSOR_POWERUSAGE_TOPIC);
//     log("MQTTPublischPeriod: %f", generalSettings.MQTTPublischPeriod);
//     log("MQTTListenPeriod: %f", generalSettings.MQTTListenPeriod);
//     log("RS232PublishPeriod: %f", generalSettings.RS232PublishPeriod);
//     log("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false");
//     log("------------------");
//     log(" ");
// }

// void Config::saveSettingsToEEPROM(GeneralSettings &settings)
// {
//     Preferences prefs;
//     if (prefs.begin("config", false)) // false = read & write mode
//     {
//         prefs.putBool("enableCtrl", settings.enableController);
//         prefs.putInt("maxOutput", settings.maxOutput);
//         prefs.putInt("minOutput", settings.minOutput);
//         prefs.putInt("offset", settings.inputCorrectionOffset);
//         prefs.putFloat("Pub", settings.MQTTPublischPeriod);
//         prefs.putFloat("Listen", settings.MQTTListenPeriod);
//         prefs.putFloat("RS232Pub", settings.RS232PublishPeriod);

//         prefs.end();
//         saveSettingsFlag = false; // reset the saveSettings flag after saving
//         log("Settings saved to EEPROM.");
//     }
//     else
//     {
//         log("Failed to open Preferences for writing.");
//     }
// }

// void Config::loadSettingsFromEEPROM(GeneralSettings &settings)
// {
//     Preferences prefs;
//     if (prefs.begin("config", true)) // true = read-only mode
//     {
//         settings.enableController = prefs.getBool("enableCtrl", settings.enableController);
//         settings.maxOutput = prefs.getInt("maxOutput", settings.maxOutput);
//         settings.minOutput = prefs.getInt("minOutput", settings.minOutput);
//         settings.inputCorrectionOffset = prefs.getInt("offset", settings.inputCorrectionOffset);

//         // settings.MQTTPublischPeriod = prefs.getFloat("MQTTPublischPeriod", settings.MQTTPublischPeriod);
//         // settings.MQTTListenPeriod = prefs.getFloat("MQTTListenPeriod", settings.MQTTListenPeriod);
//         // settings.RS232PublishPeriod = prefs.getFloat("RS232PublishPeriod", settings.RS232PublishPeriod);
//         if (prefs.isKey("Pub"))
//             settings.MQTTPublischPeriod = prefs.getFloat("Pub");

//         if (prefs.isKey("Listen"))
//             settings.MQTTListenPeriod = prefs.getFloat("Listen");

//         if (prefs.isKey("RS232Pub"))
//             settings.RS232PublishPeriod = prefs.getFloat("RS232Pub");
//         else {
//             saveSettingsToEEPROM(settings); // save the settings to EEPROM if not present
//         }
//         prefs.end();
//         log("Settings loaded from EEPROM.");
//     }
//     else
//     {
//         log("Failed to open Preferences for reading.");
//     }
// }

// void Config::removeSettings(char *Name)
// {
//     Preferences prefs;
//     prefs.begin("config", false);
//     prefs.remove(Name);
//     prefs.end();
//     log("Removed setting from EEPROM: %s", Name);
// }

// void Config::removeAllSettings()
// {
//     Preferences prefs;
//     prefs.begin("config", false);
//     prefs.clear(); // Löscht ALLE Werte im Namespace
//     prefs.end();
//     log("Removed ALL setting from EEPROM:");
// }
