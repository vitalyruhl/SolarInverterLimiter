#include "WiFiManager/WiFiManager.h"

WiFiManager::WiFiManager(const char *apName)
    : _apName(apName), _server(80) {}



void WiFiManager::begin(const char *ssid, const char *password)
{
    if (ssid != nullptr && password != nullptr)
    {
        Serial.println("Trying to connect using provided credentials...");
        _storedSSID = ssid;
        _storedPassword = password;

        connectToWiFi();

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("Connected with provided credentials.");

            // Laden, um zu prüfen ob Änderungen vorliegen
            String oldSSID, oldPass;
            _preferences.begin("wifi", true); // read-only
            oldSSID = _preferences.getString("ssid", "");
            oldPass = _preferences.getString("password", "");
            _preferences.end();

            if (_storedSSID != oldSSID || _storedPassword != oldPass)
            {
                Serial.println("Credentials changed. Saving new ones.");
                saveWiFiCredentials(_storedSSID, _storedPassword);
            }
            else
            {
                Serial.println("Credentials unchanged. Skipping save.");
            }

            return;
        }
        else
        {
            Serial.println("Provided credentials failed. Starting AP...");
            startAccessPoint();
            return;
        }
    }

    // Try stored credentials
    if (!loadWiFiCredentials())
    {
        Serial.println("No stored credentials. Starting AP...");
        startAccessPoint();
        return;
    }

    connectToWiFi();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Stored credentials failed. Starting AP...");
        startAccessPoint();
    }
}

/// Overloaded method to accept String parameters
void WiFiManager::begin(const String& ssid, const String& password) {
    begin(ssid.c_str(), password.c_str());
}


void WiFiManager::loop()
{
    _server.handleClient();
}

void WiFiManager::connectToWiFi()
{
    WiFi.begin(_storedSSID.c_str(), _storedPassword.c_str());
    Serial.printf("Connecting to %s...\n", _storedSSID.c_str());

    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; ++i)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nConnected!");
        Serial.println(WiFi.localIP());
    }
}

bool WiFiManager::loadWiFiCredentials()
{
    _preferences.begin("wifi", true); // read-only
    _storedSSID = _preferences.getString("ssid", "");
    _storedPassword = _preferences.getString("password", "");
    _preferences.end();

    return !_storedSSID.isEmpty() && !_storedPassword.isEmpty();
}

void WiFiManager::saveWiFiCredentials(const String &ssid, const String &password)
{
    _preferences.begin("wifi", false); // write-mode
    _preferences.putString("ssid", ssid);
    _preferences.putString("password", password);
    _preferences.end();
}

void WiFiManager::startAccessPoint()
{
    WiFi.softAP(_apName);
    Serial.printf("AP started: %s\n", _apName);
    Serial.println(WiFi.softAPIP());

    _server.on("/", HTTP_GET, std::bind(&WiFiManager::handleRoot, this));
    _server.on("/submit", HTTP_POST, std::bind(&WiFiManager::handleFormSubmit, this));
    _server.begin();
}

void WiFiManager::handleRoot()
{
    String html = R"rawliteral(
        <html>
            <body>
                <h2>WiFi Config</h2>
                <form action="/submit" method="post">
                    SSID: <input name="ssid"><br>
                    Password: <input name="password" type="password"><br>
                    <input type="submit" value="Save">
                </form>
            </body>
        </html>
    )rawliteral";
    _server.send(200, "text/html", html);
}

void WiFiManager::handleFormSubmit()
{
    if (_server.hasArg("ssid") && _server.hasArg("password"))
    {
        String ssid = _server.arg("ssid");
        String password = _server.arg("password");

        saveWiFiCredentials(ssid, password);

        String response = "Saved. Restarting...";
        _server.send(200, "text/plain", response);

        delay(1000);
        ESP.restart();
    }
    else
    {
        _server.send(400, "text/plain", "Missing SSID or Password");
    }
}
