#ifndef WEBCONFIG_H
#define WEBCONFIG_H
#pragma once

#include <WebServer.h>
#include "config/settings.h"
#include "config/config.h"
#include "WiFiManager/WiFiManager.h"
#include "config/html/html_content.h"

class Webconfig
{
public:
    Webconfig();
    ~Webconfig() = default; // Destruktor

    void attachWebEndpoint(WebServer &server);
    String toJSON();
    void fromJSON(const String &json);

    void handleClient(WebServer &server);

private:
    Config configuration;
    config_mqtt mqttSettings;
    GeneralSettings generalSettings;
    Config_wifi wifi_configSettings;
    rs485Settings rs485config;
    bool saveSettingsFlag = false;
    WebHTML webhtml;
};

#endif // WEBCONFIG_H
