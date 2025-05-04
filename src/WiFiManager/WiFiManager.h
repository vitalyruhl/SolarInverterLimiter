#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <memory> // For std::unique_ptr
#include "config/config.h"
#include "config/webconfig.h"

class Webconfig;
class Config;

class WiFiManager
{
public:
    // WiFiManager(Config_wifi &config) : _config(config) {}
    WiFiManager(Config *settings);
    ~WiFiManager();
    void begin();
    bool isConnected();
    String getLocalIP();
    String getSSID();
    bool connectToWiFi();
    void startAccessPoint();
    bool hasAPServer();
    void handleClient();

private:
    // Config_wifi &_config;
    bool connected;
    SigmaLogLevel level = SIGMALOG_WARN; // Set the log level from the config settings
   
    // id do not knowing what this is, but it was in examples... todo: learn more about this
    std::unique_ptr<Webconfig> webconfig; // Use smart pointer to manage memory automatically
    std::unique_ptr<WebServer> _server;

    Config *_config = nullptr;
    void StartWebApp();
    bool waitForConnection(uint16_t timeout_sec);
};

#endif
