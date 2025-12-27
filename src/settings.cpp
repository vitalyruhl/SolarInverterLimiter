
#include "settings.h"

namespace {
	bool prefsHasKey(Preferences &prefs, const char *key)
	{
		return key && *key && prefs.isKey(key);
	}

	void migrateBool(Preferences &prefs, const char *oldKey, const char *newKey)
	{
		if (!prefsHasKey(prefs, oldKey) || prefsHasKey(prefs, newKey))
			return;

		const bool value = prefs.getBool(oldKey, false);
		prefs.putBool(newKey, value);
		Serial.printf("[MIGRATION] Copied bool key '%s' -> '%s'\n", oldKey, newKey);
	}

	void migrateInt(Preferences &prefs, const char *oldKey, const char *newKey)
	{
		if (!prefsHasKey(prefs, oldKey) || prefsHasKey(prefs, newKey))
			return;

		const int value = prefs.getInt(oldKey, 0);
		prefs.putInt(newKey, value);
		Serial.printf("[MIGRATION] Copied int key '%s' -> '%s'\n", oldKey, newKey);
	}

	void migrateFloat(Preferences &prefs, const char *oldKey, const char *newKey)
	{
		if (!prefsHasKey(prefs, oldKey) || prefsHasKey(prefs, newKey))
			return;

		const float value = prefs.getFloat(oldKey, 0.0f);
		prefs.putFloat(newKey, value);
		Serial.printf("[MIGRATION] Copied float key '%s' -> '%s'\n", oldKey, newKey);
	}

	void migrateString(Preferences &prefs, const char *oldKey, const char *newKey)
	{
		if (!prefsHasKey(prefs, oldKey) || prefsHasKey(prefs, newKey))
			return;

		const String value = prefs.getString(oldKey, "");
		prefs.putString(newKey, value);
		Serial.printf("[MIGRATION] Copied string key '%s' -> '%s' (len=%d)\n", oldKey, newKey, value.length());
	}
}

void migrateConfigManagerPrefsKeys()
{
	Preferences prefs;
	if (!prefs.begin("ConfigManager", false))
	{
		Serial.println("[MIGRATION] Failed to open Preferences namespace 'ConfigManager'");
		return;
	}

	// WiFi
	migrateString(prefs, "ssid", "WiFiSSID");
	migrateString(prefs, "pwd", "WiFiPassword");
	migrateBool(prefs, "dhcp", "WiFiUseDHCP");
	migrateString(prefs, "ip", "WiFiStaticIP");
	migrateString(prefs, "gw", "WiFiGateway");
	migrateString(prefs, "mask", "WiFiSubnet");

	// MQTT
	migrateFloat(prefs, "pubPer", "MQTTPubPer");
	migrateFloat(prefs, "subPer", "MQTTSubPer");
	migrateInt(prefs, "port", "MQTTPort");
	migrateString(prefs, "host", "MQTTHost");
	migrateString(prefs, "user", "MQTTUser");
	migrateString(prefs, "pass", "MQTTPass");
	migrateString(prefs, "pwrTop", "MQTTPwrTopic");
	migrateString(prefs, "base", "MQTTBaseTopic");
	migrateBool(prefs, "enable", "MQTTEnable");

	// Limiter (note: previous key 'enable' collided with MQTT 'enable')
	migrateBool(prefs, "enable", "LimiterEnable");
	migrateInt(prefs, "maxW", "LimiterMaxW");
	migrateInt(prefs, "minW", "LimiterMinW");
	migrateInt(prefs, "corr", "LimiterCorrW");
	migrateInt(prefs, "smooth", "LimiterSmooth");
	migrateFloat(prefs, "rs485Per", "LimiterRS485P");

	// Temp
	migrateFloat(prefs, "tCorr", "TempTCorr");
	migrateFloat(prefs, "hCorr", "TempHCorr");
	migrateInt(prefs, "slp", "TempSLP");
	migrateInt(prefs, "readS", "TempReadS");

	// I2C
	migrateInt(prefs, "sda", "I2CSDA");
	migrateInt(prefs, "scl", "I2CSCL");
	migrateInt(prefs, "freq", "I2CFreq");
	migrateInt(prefs, "bmeHz", "I2CBmeHz");
	migrateInt(prefs, "disp", "I2CDisp");

	// Fan
	migrateBool(prefs, "en", "FanEnable");
	migrateFloat(prefs, "on", "FanOn");
	migrateFloat(prefs, "off", "FanOff");
	migrateInt(prefs, "pin", "FanPin");
	migrateBool(prefs, "low", "FanLow");

	// Heater
	migrateBool(prefs, "en", "HeatEnable");
	migrateFloat(prefs, "on", "HeatOn");
	migrateFloat(prefs, "off", "HeatOff");
	migrateInt(prefs, "pin", "HeatPin");
	migrateBool(prefs, "low", "HeatLow");

	// Display
	migrateBool(prefs, "sleep", "DispSleep");
	migrateInt(prefs, "onS", "DispOnS");

	// System
	migrateBool(prefs, "ota", "SysOTA");
	migrateString(prefs, "otaPwd", "SysOtaPwd");
	migrateInt(prefs, "rbMin", "SysRbMin");
	migrateBool(prefs, "unconf", "SysUnconf");
	migrateString(prefs, "ver", "SysVer");

	// Buttons
	migrateInt(prefs, "ap", "BtnAP");
	migrateInt(prefs, "rst", "BtnRst");

	prefs.end();
}

ConfigManagerClass cfg;
WiFi_Settings wifiSettings;
MQTT_Settings mqttSettings;
LimiterSettings limiterSettings;
TempSettings tempSettings;
I2CSettings i2cSettings;
FanSettings fanSettings;
HeaterSettings heaterSettings;
DisplaySettings displaySettings;
SystemSettings systemSettings;
ButtonSettings buttonSettings;
RS485_Settings rs485settings;
SigmaLogLevel logLevel = SIGMALOG_WARN; // SIGMALOG_OFF = 0, SIGMALOG_INTERNAL, SIGMALOG_FATAL, SIGMALOG_ERROR, SIGMALOG_WARN, SIGMALOG_INFO, SIGMALOG_DEBUG, SIGMALOG_ALL
