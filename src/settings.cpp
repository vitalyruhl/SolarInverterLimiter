
#include "settings.h"

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
