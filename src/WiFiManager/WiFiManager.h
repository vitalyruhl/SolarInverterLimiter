#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#pragma once
#include <WebServer.h>
#include <Arduino.h>
// #include "config/config.h"
#include "WiFiManager/cConfig_wifi.h"

struct Config_wifi 
{
    String ssid = "YourSSID";     // WiFi SSID
    String pass = "YourPassword"; // WiFi password
    String failover_ssid = "YourFailoverSSID";
    String failover_pass = "YourFailoverPassword";
    String apSSID = "HouseBattery_AP"; // Access Point SSID
    String staticIP = "192.168.0.22";
    String staticSubnet = "255.255.255.0";
    String staticGateway = "192.168.0.1";
    String staticDNS = "192.168.0.1";
    bool use_static_ip = false;
    // virtual ~Config_wifi() = default; // Virtual Destructor to make it polymorphic
};

class WiFiManager
{
public:
    // WiFiManager(Config_wifi &config) : _config(config) {}
    WiFiManager(Config_wifi *config);
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
    
    Config_wifi* _config = nullptr;
    WebServer* _server = nullptr;

};

#endif
