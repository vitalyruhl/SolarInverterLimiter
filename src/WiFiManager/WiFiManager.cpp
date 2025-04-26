#include "WiFiManager/WiFiManager.h"
#include "logging/logging.h"
#include "config/webconfig.h"

WiFiManager::WiFiManager(Config_wifi *config) : _config(config)
{
    // do nothing, constructor initializes _config
}

void WiFiManager::begin()
{
    if (!_config)
    {
        log("💣 ERROR!!! _config pointer is null!");
        return;
    }

    webconfig.reset(new Webconfig());

    log("Config-WiFi values:");
    log("SSID: %s", _config->ssid);
    log("Use Static IP: %s", String(_config->use_static_ip));

    if (!connectToWiFi())
    {
        startAccessPoint();
    }
}

bool WiFiManager::connectToWiFi()
{
    // todo: add try it 3 times
    // todo: add check and try to connect with the failover credentials

    if (_config->use_static_ip)
    {
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(_config->staticIP);
        gateway.fromString(_config->staticGateway);
        subnet.fromString(_config->staticSubnet);
        dns.fromString(_config->staticDNS);
        WiFi.config(ip, gateway, subnet, dns);
    }

    WiFi.begin(_config->ssid.c_str(), _config->pass.c_str());
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        WiFi.begin(_config->failover_ssid.c_str(), _config->failover_pass.c_str());
        return WiFi.waitForConnectResult() == WL_CONNECTED;
    }

    connected = (WiFi.waitForConnectResult() == WL_CONNECTED);
    StartWebApp();
    return connected;
}

bool WiFiManager::hasAPServer()
{
    return _server != nullptr;
}

void WiFiManager::handleClient()
{
    // todo: add check for avaibility of wifi stored credentials, and restart, if there if we are not connected to the wifi, or running in AP mode
    if (_server)
        _server->handleClient();
}

void WiFiManager::startAccessPoint()
{
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(_config->apSSID.c_str()))
    {
        logs("AP-Mode: IP %s", WiFi.softAPIP().toString().c_str());
        StartWebApp();
    }

    else
    {
        logs("Erron on starting Access Points.");
    }
}

bool WiFiManager::isConnected()
{
    return connected;
}

String WiFiManager::getLocalIP()
{
    return connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String WiFiManager::getSSID()
{
    return connected ? WiFi.SSID() : WiFi.softAPSSID();
}

void WiFiManager::StartWebApp()
{
    logs("Init Server...");
    _server.reset(new WebServer()); // no unique_ptr, because c++ v11

    logs("register endpoints ...");
    webconfig->attachWebEndpoint(*_server);

    logs("Start server on port 80 (http://) ...");
    _server->begin(); // Server starten
    logs("webserver up ...");
}
