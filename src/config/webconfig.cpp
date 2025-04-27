
#include <ArduinoJson.h>
#include "config/webconfig.h"
#include "logging/logging.h"

// Webconfig::Webconfig() //: configuration()
// {
// }
Webconfig::Webconfig(Config *configuration) : _config(configuration)
{
    // do nothing, constructor initializes _config
}

String Webconfig::toJSON()
{
    logs("enter toJSON()...");
    JsonDocument doc;
    logs("Generiere JSON...");
    doc["wifi"]["ssid"] = _config->wifi_config.ssid;
    doc["wifi"]["pass"] = _config->wifi_config.pass;
    doc["wifi"]["fo_ssid"] = _config->wifi_config.failover_ssid;
    doc["wifi"]["fo_pass"] = _config->wifi_config.failover_pass;
    doc["wifi"]["AP_ssid"] = _config->wifi_config.apSSID;
    doc["wifi"]["IP"] = _config->wifi_config.staticIP;
    doc["wifi"]["SN"] = _config->wifi_config.staticSubnet;
    doc["wifi"]["GW"] = _config->wifi_config.staticGateway;
    doc["wifi"]["DNS"] = _config->wifi_config.staticDNS;
    doc["wifi"]["use_sp"] = _config->wifi_config.use_static_ip;

    doc["mqtt"]["mqtt_server"] = _config->mqttSettings.mqtt_server;
    doc["mqtt"]["mqtt_user"] = _config->mqttSettings.mqtt_username;
    doc["mqtt"]["mqtt_pass"] = _config->mqttSettings.mqtt_password;
    doc["mqtt"]["mqtt_host"] = _config->mqttSettings.mqtt_hostname;
    doc["mqtt"]["mqtt_port"] = _config->mqttSettings.mqtt_port;

    doc["general"]["enableController"] = _config->generalSettings.enableController;
    doc["general"]["maxOutput"] = _config->generalSettings.maxOutput;
    doc["general"]["minOutput"] = _config->generalSettings.minOutput;
    doc["general"]["offset"] = _config->generalSettings.inputCorrectionOffset;
    doc["general"]["Pub"] = _config->generalSettings.MQTTPublischPeriod;
    doc["general"]["Listen"] = _config->generalSettings.MQTTListenPeriod;
    doc["general"]["RS232Pub"] = _config->generalSettings.RS232PublishPeriod;
    doc["general"]["smoothing"] = _config->generalSettings.smoothingSize;

    String output;
    serializeJson(doc, output);
    // serializeJsonPretty(doc, Serial); // Debug output to Serial - uncomment after debugging
    return output;
}

void Webconfig::fromJSON(const String &json)
{
    logs("enter fromJSON()...");
    JsonDocument doc;
    deserializeJson(doc, json);
    logs("JSON recived: %s", json.c_str());

    _config->wifi_config.ssid = doc["wifi"]["ssid"].as<String>();
    _config->wifi_config.pass = doc["wifi"]["pass"].as<String>();
    _config->wifi_config.failover_ssid = doc["wifi"]["fo_ssid"].as<String>();
    _config->wifi_config.failover_pass = doc["wifi"]["fo_pass"].as<String>();
    _config->wifi_config.apSSID = doc["wifi"]["AP_ssid"].as<String>();
    _config->wifi_config.staticIP = doc["wifi"]["IP"].as<String>();
    _config->wifi_config.staticSubnet = doc["wifi"]["SN"].as<String>();
    _config->wifi_config.staticGateway = doc["wifi"]["GW"].as<String>();
    _config->wifi_config.staticDNS = doc["wifi"]["DNS"].as<String>();
    _config->wifi_config.use_static_ip = doc["wifi"]["use_sp"].as<bool>();

    _config->mqttSettings.mqtt_server = doc["mqtt"]["mqtt_server"].as<String>();
    _config->mqttSettings.mqtt_username = doc["mqtt"]["mqtt_user"].as<String>();
    _config->mqttSettings.mqtt_password = doc["mqtt"]["mqtt_pass"].as<String>();
    _config->mqttSettings.mqtt_hostname = doc["mqtt"]["mqtt_host"].as<String>();
    _config->mqttSettings.mqtt_port = doc["mqtt"]["mqtt_port"].as<int>();

    _config->generalSettings.enableController = doc["general"]["enableController"].as<bool>();
    _config->generalSettings.maxOutput = doc["general"]["maxOutput"].as<int>();
    _config->generalSettings.minOutput = doc["general"]["minOutput"].as<int>();
    _config->generalSettings.inputCorrectionOffset = doc["general"]["offset"].as<int>();
    _config->generalSettings.MQTTPublischPeriod = doc["general"]["Pub"].as<float>();
    _config->generalSettings.MQTTListenPeriod = doc["general"]["Listen"].as<float>();
    _config->generalSettings.RS232PublishPeriod = doc["general"]["RS232Pub"].as<float>();
    _config->generalSettings.smoothingSize = doc["general"]["smoothing"].as<int>();
}

void Webconfig::saveSettings(const String &json)
{
    logs("enter saveSettings()...");
    fromJSON(json); // Convert JSON string to settings
    _config->saveSettingsFlag = true;
    _config->save(); // Save the settings to EEPROM
    toJSON();
    logs("done saveSettings()...");
}

void Webconfig::applySettings(const String &json)
{
    logs("enter applySettings()...");
    fromJSON(json);
    toJSON(); // Convert settings to JSON format
    logs("done applySettings()...");
}

void Webconfig::attachWebEndpoint(WebServer &server)
{
    logs("enter attachWebEndpoint()...");

    logs("Apply Route \\ ...");
    server.on("/", HTTP_GET, [this, &server]()
              {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send_P(200, "text/html", webhtml.getWebHTML()); });

    logs("Apply Route /config/json ...");
    server.on("/config/json", HTTP_GET, [this, &server]()
              {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", this->toJSON()); });

    logs("Apply Route /config/apply ...");
    server.on("/config/apply", HTTP_POST, [this, &server]()
              {
                if (!server.hasArg("plain")) {
                    server.send(400, "text/plain", "Missing body");
                    return;
                }
        this->applySettings(server.arg("plain"));
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "Apply settings done."); });

    // apply save settings route
    logs("Apply Route /config/save ...");
    server.on("/config/save", HTTP_POST, [this, &server]()
              {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing body");
            return;
        }
        // this->fromJSON(server.arg("plain"));
        this->saveSettings(server.arg("plain"));
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "Settings saved"); });

    logs("Apply Route /config/reset ...");
    server.on("/config/reset", HTTP_POST, [this, &server]()
              {
        this->_config->removeAllSettings();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "Settings reset"); });

    server.on("/reboot", HTTP_POST, [this, &server]()
              {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "Rebooting...");
        delay(1000);
        ESP.restart(); });

    // POST-Handler mit safe Capture

    server.on("/", HTTP_POST, [this, &server]()
              {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing body");
            return;
        }
        this->fromJSON(server.arg("plain"));
        this->_config->save();
        server.send(200, "text/plain", "Configuration saved."); });

    logs("done attachWebEndpoint()...");
}