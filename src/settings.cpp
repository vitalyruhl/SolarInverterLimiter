
#include "settings.h"


WebServer server(80);
ConfigManagerClass cfg;
WiFi_Settings wifiSettings;
MQTT_Settings mqttSettings;
General_Settings generalSettings;
RS485_Settings rs485settings;
// LDR_Settings ldrSettings;
SigmaLogLevel logLevel = SIGMALOG_WARN; // SIGMALOG_OFF = 0, SIGMALOG_INTERNAL, SIGMALOG_FATAL, SIGMALOG_ERROR, SIGMALOG_WARN, SIGMALOG_INFO, SIGMALOG_DEBUG, SIGMALOG_ALL
ConfigManagerClass::LogCallback ConfigManagerClass::logger = nullptr;
