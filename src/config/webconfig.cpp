
#include <ArduinoJson.h>
#include "config/webconfig.h"
#include "logging/logging.h"

Webconfig::Webconfig() {
    mqttSettings = configuration.mqtt;
    generalSettings = configuration.general;
    wifi_configSettings = configuration.wifi_config;
    rs485config = configuration.rs485settings;
    saveSettingsFlag = false;
}

String Webconfig::toJSON() {
    JsonDocument doc;
    logv("Generiere JSON...");
    doc["wifi"]["ssid"] = wifi_configSettings.ssid;
    doc["wifi"]["pass"] = wifi_configSettings.pass;
    doc["wifi"]["fo_ssid"] = wifi_configSettings.failover_ssid;
    doc["wifi"]["fo_pass"] = wifi_configSettings.failover_pass;
    doc["wifi"]["AP_ssid"] = wifi_configSettings.apSSID;
    doc["wifi"]["IP"] = wifi_configSettings.staticIP;
    doc["wifi"]["SN"] = wifi_configSettings.staticSubnet;
    doc["wifi"]["GW"] = wifi_configSettings.staticGateway;
    doc["wifi"]["DNS"] = wifi_configSettings.staticDNS;
    doc["wifi"]["use_sp"] = wifi_configSettings.use_static_ip;

    doc["mqtt"]["mqtt_server"] = mqttSettings.mqtt_server;
    doc["mqtt"]["mqtt_user"] = mqttSettings.mqtt_username;
    doc["mqtt"]["mqtt_pass"] = mqttSettings.mqtt_password;
    doc["mqtt"]["mqtt_host"] = mqttSettings.mqtt_hostname;
    doc["mqtt"]["mqtt_port"] = mqttSettings.mqtt_port;

    doc["general"]["enableController"] = generalSettings.enableController;
    doc["general"]["maxOutput"] = generalSettings.maxOutput;
    doc["general"]["minOutput"] = generalSettings.minOutput;
    doc["general"]["offset"] = generalSettings.inputCorrectionOffset;
    doc["general"]["Pub"] = generalSettings.MQTTPublischPeriod;
    doc["general"]["Listen"] = generalSettings.MQTTListenPeriod;
    doc["general"]["RS232Pub"] = generalSettings.RS232PublishPeriod;
    doc["general"]["smoothing"] = generalSettings.smoothingSize;

    String output;
    serializeJson(doc, output);
    serializeJsonPretty(doc, Serial); // Debug output to Serial - uncomment after debugging
    return output;
}

void Webconfig::fromJSON(const String &json)
{
    JsonDocument doc;
    deserializeJson(doc, json);
    logv("JSON recived: %s", json.c_str());


    wifi_configSettings.ssid = doc["wifi"]["ssid"].as<String>();
    wifi_configSettings.pass = doc["wifi"]["pass"].as<String>();
    wifi_configSettings.failover_ssid = doc["wifi"]["fo_ssid"].as<String>();
    wifi_configSettings.failover_pass = doc["wifi"]["fo_pass"].as<String>();
    wifi_configSettings.apSSID = doc["wifi"]["AP_ssid"].as<String>();
    wifi_configSettings.staticIP = doc["wifi"]["IP"].as<String>();
    wifi_configSettings.staticSubnet = doc["wifi"]["SN"].as<String>();
    wifi_configSettings.staticGateway = doc["wifi"]["GW"].as<String>();
    wifi_configSettings.staticDNS = doc["wifi"]["DNS"].as<String>();
    wifi_configSettings.use_static_ip = doc["wifi"]["use_sp"].as<bool>();

    mqttSettings.mqtt_server = doc["mqtt"]["mqtt_server"].as<String>();
    mqttSettings.mqtt_username = doc["mqtt"]["mqtt_user"].as<String>();
    mqttSettings.mqtt_password = doc["mqtt"]["mqtt_pass"].as<String>();
    mqttSettings.mqtt_hostname = doc["mqtt"]["mqtt_host"].as<String>();
    mqttSettings.mqtt_port = doc["mqtt"]["mqtt_port"].as<int>();

    generalSettings.enableController = doc["general"]["enableController"].as<bool>();
    generalSettings.maxOutput = doc["general"]["maxOutput"].as<int>();
    generalSettings.minOutput = doc["general"]["minOutput"].as<int>();
    generalSettings.inputCorrectionOffset = doc["general"]["offset"].as<int>();
    generalSettings.MQTTPublischPeriod = doc["general"]["Pub"].as<float>();
    generalSettings.MQTTListenPeriod = doc["general"]["Listen"].as<float>();
    generalSettings.RS232PublishPeriod = doc["general"]["RS232Pub"].as<float>();
    generalSettings.smoothingSize = doc["general"]["smoothing"].as<int>();
}

void Webconfig::attachWebEndpoint(WebServer &server) {
    // GET-Handler mit explizitem Capture
    server.on("/", HTTP_GET, [this, &server]() {
        server.send(200, "application/json", this->toJSON());
    });

    // server.on("/", HTTP_GET, [this](WebServer* srv) {
    //     srv->send(200, "application/json", this->toJSON());
    // });

    // POST-Handler mit safe Capture
    server.on("/", HTTP_POST, [this, &server]() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing body");
            return;
        }
        this->fromJSON(server.arg("plain"));
        this->configuration.save();
        server.send(200, "text/plain", "Configuration saved.");
    });
}