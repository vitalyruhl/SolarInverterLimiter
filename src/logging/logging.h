#ifndef LOGGING_H
#define LOGGING_H

#pragma once

#include <Wire.h>
#include <Arduino.h>
#include <stdarg.h>
#include "SigmaLoger.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>
#include <queue>
#include <Ticker.h>

// Declare extern variables
extern Adafruit_SSD1306 display;
extern SigmaLoger *sl;
extern SigmaLoger *sll;
extern SigmaLogLevel level;

// Function declarations
void SetupStartDisplay();
const char *sl_timestamp();
void SerialLoggerPublisher(SigmaLogLevel level, const char *message);
void LCDLoggerPublisher(SigmaLogLevel level, const char *message);
void LCDLoggerPublisherBuffered(SigmaLogLevel level, const char *message);
void LCDUpdate();
void splitIntoLines(const String& msg, size_t lineLength, std::vector<String>& out);


#endif // LOGGING_H
