#ifndef LOGGING_H
#define LOGGING_H

#pragma once
#include <stdarg.h>        // for variadische Funktionen (printf-Stil)
#include "config/config.h" // for config settings

// #define ENABLE_LOGGING
// #define ENABLE_LOGGING_VERBOSE

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
