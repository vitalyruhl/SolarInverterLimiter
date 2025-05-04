#include "WiFiManager/WiFiManager.h"
#include "logging/logging.h"

WiFiManager::WiFiManager(Config *settings) : _config(settings)
{
    // do nothing, constructor initializes config
}

WiFiManager::~WiFiManager() = default;

void WiFiManager::begin()
{
    if (!_config)
    {
        sl->Printf("💣 ERROR!!! config pointer is null!").Error();
        return;
    }
    webconfig.reset(new Webconfig(_config));

    sl->Printf("Config-WiFi values:");
    sll->Printf("Connecting to Wifi...").Debug();
    sl->Printf("SSID: %s", _config->wifi_config.ssid.c_str());
    sll->Printf("SSID: %s", _config->wifi_config.ssid.c_str());
    sl->Printf("Use Static IP: %s", String(_config->wifi_config.use_static_ip));

    if (!connectToWiFi())
    {
        // try to connect with the failover credentials
        sl->Printf("Failover SSID: %s", _config->wifi_config.failover_ssid.c_str());
        sll->Printf("Failover SSID: %s", _config->wifi_config.failover_ssid.c_str());
        // startAccessPoint();
    }
}

bool WiFiManager::connectToWiFi() {
    const int MAX_RETRIES = 5; // Total attempts (primary + failover)
    bool connected = false;

    for (int attempt = 0; attempt < MAX_RETRIES && !connected; attempt++) {
        // Configure static IP if enabled
        if (_config->wifi_config.use_static_ip) {
            IPAddress ip, gateway, subnet, dns;
            bool ipValid = ip.fromString(_config->wifi_config.staticIP);
            bool gatewayValid = gateway.fromString(_config->wifi_config.staticGateway);
            bool subnetValid = subnet.fromString(_config->wifi_config.staticSubnet);
            bool dnsValid = dns.fromString(_config->wifi_config.staticDNS);

            if (ipValid && gatewayValid && subnetValid && dnsValid) {
                WiFi.config(ip, gateway, subnet, dns);
            } else {
                sl->Printf("Invalid static IP settings! Using DHCP.").Error();
                sll->Printf("Invalid IP settings! Using DHCP.").Error();
            }
        }

        // Attempt primary SSID
        WiFi.begin(_config->wifi_config.ssid.c_str(), _config->wifi_config.pass.c_str());
        sl->Printf("Connecting to primary SSID: %s", _config->wifi_config.ssid.c_str()).Info();
        sll->Printf("Connecting to primary SSID: %s", _config->wifi_config.ssid.c_str()).Info();
        connected = waitForConnection(10); // 10-second timeout

        // If primary fails, try failover
        if (!connected && _config->wifi_config.failover_ssid.length() > 0) {
            WiFi.disconnect();
            delay(500);
            WiFi.begin(_config->wifi_config.failover_ssid.c_str(), _config->wifi_config.failover_pass.c_str());
            sl->Printf("Connecting to failover SSID: %s", _config->wifi_config.failover_ssid.c_str()).Info();
            sll->Printf("Connecting to failover SSID: %s", _config->wifi_config.failover_ssid.c_str()).Info();
            connected = waitForConnection(10);
        }

        if (connected) {
            sl->Printf("Connected to WiFi! IP: %s", WiFi.localIP().toString().c_str()).Info();
            sll->Printf("Connected to WiFi! IP: %s", WiFi.localIP().toString().c_str()).Info();
            break;
        } else {
            sl->Printf("[Attempt %d/%d] Failed to connect", attempt + 1, MAX_RETRIES).Error();
            sll->Printf("[Attempt %d/%d] Failed to connect", attempt + 1, MAX_RETRIES).Error();
        }
    }

    if (!connected) {
        sl->Printf("All connection attempts failed. Starting AP mode...").Error();
        // startAccessPoint(); // Fallback to AP mode
        // StartWebApp();
        sll->Printf("Failed to connect to WiFi.").Error();
        sll->Printf("restart ESP!!!.").Error();
        delay(5000); // Wait for 5 second before restarting to show the message
        ESP.restart(); // Restart the ESP32
        return false;
    }

    StartWebApp();
    return true;
}

// Helper: Non-blocking connection check with timeout
bool WiFiManager::waitForConnection(uint16_t timeout_sec) {
    uint32_t start = millis();
    while (millis() - start < timeout_sec * 1000) {
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(500);
    }
    return false;
}

bool WiFiManager::hasAPServer()
{
    return _server != nullptr;
}

void WiFiManager::handleClient()
{
    if (_server)
        _server->handleClient();
}

void WiFiManager::startAccessPoint()
{
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(_config->wifi_config.apSSID.c_str()))
    {
        sl->Printf("AP-Mode: IP %s", WiFi.softAPIP().toString().c_str()).Debug();
        sll->Printf("AP-Mode: IP %s", WiFi.softAPIP().toString().c_str()).Debug();
        StartWebApp();
    }

    else
    {
        sl->Printf("Erron on starting Access Points.").Error();
        sll->Printf("Erron on starting Access Points.").Error();
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
    sl->Printf("Init Server...");
    _server.reset(new WebServer()); // no unique_ptr, because c++ v11

    sl->Printf("register endpoints ...");
    webconfig->attachWebEndpoint(*_server);

    sl->Printf("Start server on port 80 (http://) ...");
    _server->begin(); // Server starten
    sl->Printf("webserver up ...");
}


