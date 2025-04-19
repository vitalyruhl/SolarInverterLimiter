#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config/config.h"
#include "logging/logging.h"

class WiFiManager
{
public:
    WiFiManager(const char *apName = "ESP32_ConfigAP");
    void begin(const char *ssid = nullptr, const char *password = nullptr);
    void begin(const String &ssid, const String &password);
    void loop();

private:
    void startAccessPoint();
    void handleRoot();
    void handleFormSubmit();
    void connectToWiFi();
    bool loadWiFiCredentials();
    void saveWiFiCredentials(const String &ssid, const String &password);

    const char *_apName;
    WebServer _server;
    Preferences _preferences;

    String _storedSSID;
    String _storedPassword;
};
