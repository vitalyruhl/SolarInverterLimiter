#pragma once
#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstdlib>

namespace control {
// Observe existing CM mappings without replacing the SBT message callback.
class TelemetryAge {
public:
    enum Input { Grid, Inverter, Price, Count };
    void attach(ConfigManagerClass &cfg) {
        const char *ids[] = {"grid_import_w", "solar_power_w", "electricity_price"};
        for (unsigned i = 0; i < Count; ++i) {
            uint32_t h = 2166136261u;
            for (const char *p = ids[i]; *p; ++p) { h ^= uint8_t(*p); h *= 16777619u; }
            char key[16];
            snprintf(key, sizeof(key), "MQTTRT_%08X", unsigned(h));
            topic_[i] = static_cast<Config<String> *>(cfg.findSettingByKeyHint("MQTT-Topics", key));
            snprintf(key, sizeof(key), "MQTTRK_%08X", unsigned(h));
            path_[i] = static_cast<Config<String> *>(cfg.findSettingByKeyHint("MQTT-Topics", key));
        }
    }
    void reset() {
        portENTER_CRITICAL(&mux_);
        for (auto &s : stamps_) s = Stamp{};
        portEXIT_CRITICAL(&mux_);
    }
    bool read(Input i, uint32_t now, uint32_t timeout, float &value) const {
        portENTER_CRITICAL(&mux_);
        const Stamp stamp = stamps_[i];
        portEXIT_CRITICAL(&mux_);
        value = stamp.value;
        return stamp.valid && uint32_t(now - stamp.at) < timeout;
    }
    void receive(const char *topic, const uint8_t *payload, unsigned length, uint32_t now) {
        if (!topic || !payload) return;
        for (unsigned i = 0; i < Count; ++i) {
            if (!topic_[i] || !path_[i] || topic_[i]->get() != topic) continue;
            portENTER_CRITICAL(&mux_);
            stamps_[i].valid = false;
            portEXIT_CRITICAL(&mux_);
            if (!length || length > 1024) continue;
            String raw;
            raw.reserve(length);
            for (unsigned n = 0; n < length; ++n) raw += char(payload[n]);
            raw.trim();
            String key = path_[i]->get(); key.trim();
            if (raw.startsWith("{") || raw.startsWith("[")) {
                JsonDocument doc;
                if (deserializeJson(doc, raw)) continue;
                JsonVariantConst value = doc.as<JsonVariantConst>();
                unsigned start = 0;
                while (start < key.length()) {
                    int end = key.indexOf('.', start);
                    if (end < 0) end = key.length();
                    value = value[key.substring(start, end)];
                    start = unsigned(end) + 1;
                }
                if (value.isNull() || value.is<bool>()) continue;
                if (value.is<const char *>()) raw = value.as<const char *>();
                else if (value.is<double>()) raw = String(value.as<double>(), 8);
                else continue;
            } else if (key.length() && !key.equalsIgnoreCase("none")) continue;
            char *end = nullptr;
            const double number = std::strtod(raw.c_str(), &end);
            while (end && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
            if (end == raw.c_str() || !end || *end || !std::isfinite(number)) continue;
            // Match CM integer conversion (fractional input is truncated).
            if (i != Price && (number < INT32_MIN || number > INT32_MAX)) continue;
            portENTER_CRITICAL(&mux_);
            stamps_[i].value = i == Price ? float(number) : float(raw.toInt());
            stamps_[i].at = now; stamps_[i].valid = true;
            portEXIT_CRITICAL(&mux_);
        }
    }
private:
    struct Stamp { uint32_t at = 0; float value = 0; bool valid = false; };
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Stamp stamps_[Count];
    Config<String> *topic_[Count] = {}, *path_[Count] = {};
};
} // namespace control
