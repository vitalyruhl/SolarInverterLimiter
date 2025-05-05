#ifndef CONFIG_H
#define CONFIG_H

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "config/settings.h"
#include <SigmaLoger.h>

#define VERSION "0.5.1"           // version of the software
#define VERSION_DATE "05.05.2025" // date of the version

#define BUTTON_PIN_RESET_TO_DEFAULTS 15 // GPIO pin for the button (D0 on ESP8266, GPIO 0 on ESP32)
#define WDT_TIMEOUT 60                  // in seconds, if esp32 is not responding within this time, the ESP32 will reboot automatically

class Config
{
public:
  MQTT_Settings mqttSettings;
  General_Settings generalSettings;
  Wifi_Settings wifi_config;
  RS485_Settings rs485settings;
  SigmaLogLevel logLevel = SIGMALOG_WARN;
      // SIGMALOG_OFF = 0,
      // SIGMALOG_INTERNAL,
      // SIGMALOG_FATAL,
      // SIGMALOG_ERROR,
      // SIGMALOG_WARN,
      // SIGMALOG_INFO,
      // SIGMALOG_DEBUG,
      // SIGMALOG_ALL
  bool saveSettingsFlag = false;

  Config(); // constructor
  void load();
  void save();
  void removeSettings(char *Name);
  void removeAllSettings();
  void printSettings();

private:
  void loadSettingsFromEEPROM();
  void saveSettingsToEEPROM();
};

#endif // CONFIG_H
