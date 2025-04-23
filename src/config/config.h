#ifndef CONFIG_H
#define CONFIG_H

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "WiFiManager/WiFiManager.h"
#include "config/settings.h"

#define VERSION "0.2.0"           // version of the software
#define VERSION_DATE "2025.04.19" // date of the version

#define BUTTON_PIN_RESET_TO_DEFAULTS 15 // GPIO pin for the button (D0 on ESP8266, GPIO 0 on ESP32)
#define WDT_TIMEOUT 60                  // in seconds, if esp32 is not responding within this time, the ESP32 will reboot automatically

class Config
{
public:
  config_mqtt mqtt;
  GeneralSettings general;
  Config_wifi wifi_config;

  rs485Settings rs485settings;

  bool saveSettingsFlag = false;
  Config(); // constructor
  void load();
  void save();
  void removeSettings(char *Name);
  void removeAllSettings();
  void printSettings();

  String toJSON();
  void fromJSON(const String &json);

  void attachWebEndpoint(WebServer &server);

private:
  void loadSettingsFromEEPROM();
  void saveSettingsToEEPROM();
};

#endif // CONFIG_H
