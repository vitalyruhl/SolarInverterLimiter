#include "config/config.h"
#include "logging/logging.h"
#include <Preferences.h>

void Config::loadSettings(GeneralSettings &generalSettings)
{
    logv("-------------> Loading configuration settings...");
    loadSettingsFromEEPROM(generalSettings);
    generalSettings.dirtybit = true;
    delay(1000);
    printSettings(generalSettings);
}

void Config::saveSettings(GeneralSettings &generalSettings)
{
    logv("-------------> Saving configuration settings...");
    saveSettingsToEEPROM(generalSettings);
    delay(2000);
    printSettings(generalSettings);
}

//-----------------------------------------------------
// private functions

void Config::printSettings(GeneralSettings &generalSettings)
{
    log(" ");
    log("------------------");
    log("Settings:");
    log("dirtybit: %s", generalSettings.dirtybit ? "true" : "false");
    log("maxOutput: %d", generalSettings.maxOutput);
    log("minOutput: %d", generalSettings.minOutput);
    log("inputCorrectionOffset: %d", generalSettings.inputCorrectionOffset);
    log("enableController: %s", generalSettings.enableController ? "true" : "false");
    log("MQTT-Sensor for actual powerusage: %s", MQTT_SENSOR_POWERUSAGE_TOPIC);
    log("MQTTPublischPeriod: %f", generalSettings.MQTTPublischPeriod);
    log("MQTTListenPeriod: %f", generalSettings.MQTTListenPeriod);
    log("RS232PublishPeriod: %f", generalSettings.RS232PublishPeriod);
    log("saveSettingsFlag: %s", saveSettingsFlag ? "true" : "false");
    log("------------------");
    log(" ");
}

void Config::saveSettingsToEEPROM(GeneralSettings &settings)
{
    Preferences prefs;
    if (prefs.begin("config", false)) // false = read & write mode
    {
        prefs.putBool("enableCtrl", settings.enableController);
        prefs.putInt("maxOutput", settings.maxOutput);
        prefs.putInt("minOutput", settings.minOutput);
        prefs.putInt("offset", settings.inputCorrectionOffset);
        prefs.putFloat("Pub", settings.MQTTPublischPeriod);
        prefs.putFloat("Listen", settings.MQTTListenPeriod);
        prefs.putFloat("RS232Pub", settings.RS232PublishPeriod);

        prefs.end();
        saveSettingsFlag = false; // reset the saveSettings flag after saving
        log("Settings saved to EEPROM.");
    }
    else
    {
        log("Failed to open Preferences for writing.");
    }
}

void Config::loadSettingsFromEEPROM(GeneralSettings &settings)
{
    Preferences prefs;
    if (prefs.begin("config", true)) // true = read-only mode
    {
        settings.enableController = prefs.getBool("enableCtrl", settings.enableController);
        settings.maxOutput = prefs.getInt("maxOutput", settings.maxOutput);
        settings.minOutput = prefs.getInt("minOutput", settings.minOutput);
        settings.inputCorrectionOffset = prefs.getInt("offset", settings.inputCorrectionOffset);

        // settings.MQTTPublischPeriod = prefs.getFloat("MQTTPublischPeriod", settings.MQTTPublischPeriod);
        // settings.MQTTListenPeriod = prefs.getFloat("MQTTListenPeriod", settings.MQTTListenPeriod);
        // settings.RS232PublishPeriod = prefs.getFloat("RS232PublishPeriod", settings.RS232PublishPeriod);
        if (prefs.isKey("Pub"))
            settings.MQTTPublischPeriod = prefs.getFloat("Pub");

        if (prefs.isKey("Listen"))
            settings.MQTTListenPeriod = prefs.getFloat("Listen");

        if (prefs.isKey("RS232Pub"))
            settings.RS232PublishPeriod = prefs.getFloat("RS232Pub");
        else {
            saveSettingsToEEPROM(settings); // save the settings to EEPROM if not present
        }
        prefs.end();
        log("Settings loaded from EEPROM.");
    }
    else
    {
        log("Failed to open Preferences for reading.");
    }
}

void Config::removeSettings(char *Name)
{
    Preferences prefs;
    prefs.begin("config", false);
    prefs.remove(Name);
    prefs.end();
    log("Removed setting from EEPROM: %s", Name);
}

void Config::removeAllSettings()
{
    Preferences prefs;
    prefs.begin("config", false);
    prefs.clear(); // Löscht ALLE Werte im Namespace
    prefs.end();
    log("Removed ALL setting from EEPROM:");
}
