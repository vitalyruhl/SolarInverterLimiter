#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

// [MOCKED!] Only CM settings lookup and the ESP32 lock API are replaced.
class String : public std::string {
public:
    using std::string::string;
};
template <typename T> struct Config {
    T value;
    const T &get() const { return value; }
};
struct ConfigManagerClass {
    std::unordered_map<std::string, Config<String> *> settings;
    Config<String> *findSettingByKeyHint(const char *, const char *key) {
        const auto found = settings.find(key);
        return found == settings.end() ? nullptr : found->second;
    }
};
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
