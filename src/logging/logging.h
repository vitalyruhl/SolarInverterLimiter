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

//----------------------------------------------------------
// define old logging macros

#ifdef ENABLE_LOGGING
#define log(fmt, ...)                                     \
  do                                                      \
  {                                                       \
    char buffer[255];                                     \
    snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
    Serial.println(buffer);                               \
  } while (0)
#else
#define log(...) \
  do             \
  {              \
  } while (0) // do nothing if logging is disabled
#endif

#ifdef ENABLE_LOGGING_SETTINGS
#define logs(fmt, ...)                                    \
  do                                                      \
  {                                                       \
    char buffer[255];                                     \
    snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
    Serial.println(buffer);                               \
  } while (0)
#else
#define logs(...) \
  do              \
  {               \
  } while (0) // do nothing if logging is disabled
#endif

#ifdef ENABLE_LOGGING_VERBOSE
#define logv(fmt, ...)                                    \
  do                                                      \
  {                                                       \
    char buffer[255];                                     \
    snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
    Serial.println(buffer);                               \
  } while (0)
#else
#define logv(...) \
  do              \
  {               \
  } while (0) // do nothing if logging is disabled
#endif

#ifdef ENABLE_LOGGING_LCD
#define logl(fmt, ...)                                    \
  do                                                      \
  {                                                       \
    char buffer[255];                                     \
    snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
    Serial.println(buffer);                               \
  } while (0)
#else
#define logl(...) \
  do              \
  {               \
  } while (0) // do nothing if logging is disabled
#endif





#endif // LOGGING_H
