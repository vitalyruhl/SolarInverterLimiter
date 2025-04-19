#include "WiFiManager/WiFiManager.h"
#include <WiFi.h>
#include <WebServer.h>
#include "config/config.h"
#include "logging/logging.h"


#include "WiFiManager.h"

void WiFiManager::begin() {
    if (!connectToWiFi()) {
        startAccessPoint();
    }
}

bool WiFiManager::connectToWiFi() {
    if (_config.use_static_ip) {
        IPAddress ip, gateway, subnet, dns;
        ip.fromString(_config.staticIP);
        gateway.fromString(_config.staticGateway);
        subnet.fromString(_config.staticSubnet);
        dns.fromString(_config.staticDNS);
        WiFi.config(ip, gateway, subnet, dns);
    }

    WiFi.begin(_config.ssid.c_str(), _config.pass.c_str());
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        WiFi.begin(_config.failover_ssid.c_str(), _config.failover_pass.c_str());
        return WiFi.waitForConnectResult() == WL_CONNECTED;
    }

    return true;
}


void WiFiManager::startAccessPoint() {
    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(_config.apSSID.c_str());

    if (result) {
        log("Access Point gestartet:");
        log("SSID: %s", _config.apSSID.c_str());
        log("IP-Adresse: %s", WiFi.softAPIP());
    } else {
        log("Fehler beim Starten des Access Points.");
    }
}

bool WiFiManager::isConnected() {
    return connected;
}

String WiFiManager::getLocalIP() {
    return connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String WiFiManager::getSSID() {
    return connected ? WiFi.SSID() : WiFi.softAPSSID();
}
