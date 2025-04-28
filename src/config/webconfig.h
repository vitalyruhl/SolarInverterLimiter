#ifndef WEBCONFIG_H
#define WEBCONFIG_H
#pragma once

#include <WebServer.h>
#include "config/settings.h"
#include "config/config.h"
#include "WiFiManager/WiFiManager.h"
#include "config/html/html_content.h"

class Config;

class Webconfig
{
public:
    // Webconfig();
    Webconfig(Config *configuration);
    ~Webconfig() = default; // Destruktor

    void attachWebEndpoint(WebServer &server);
    String toJSON();
    void fromJSON(const String &json);
    void saveSettings(const String &json);
    void applySettings(const String &json);
    void handleClient(WebServer &server);

private:
    std::unique_ptr<Config> configuration;
    WebHTML webhtml;
    Config* _config = nullptr;
};

#endif // WEBCONFIG_H
