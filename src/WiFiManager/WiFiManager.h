#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#pragma once

#include <Arduino.h>

struct Config_wifi
{
    String ssid = "YourSSID";     // WiFi SSID
    String pass = "YourPassword"; // WiFi password

    String failover_ssid = "YourFailoverSSID";
    String failover_pass = "YourFailoverPassword";

    String apSSID = "HouseBattery_AP"; // Access Point SSID

    bool use_static_ip = false;
    String staticIP = "192.168.0.22";
    String staticSubnet = "255.255.255.0";
    String staticGateway = "192.168.0.1";
    String staticDNS = "192.168.0.1";
};

class WiFiManager
{
public:
    WiFiManager(Config_wifi &config) : _config(config) {}
    void begin();
    bool isConnected();
    String getLocalIP();
    String getSSID();
    bool connectToWiFi();
    void startAccessPoint();

private:
    Config_wifi &_config;
    bool connected;
};

#endif
