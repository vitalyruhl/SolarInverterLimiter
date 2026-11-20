#pragma once
#include "ConfigManager.h"
#include "JsonArena.h"
#include "NumericSafety.h"
#include <ArduinoJson.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <strings.h>

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
        refreshMappings();
    }
    // Called by the settings writer after loading or applying CM mappings.
    // The telemetry task only accesses our copy of the topic and JSON key.
    void refreshMappings() {
        std::lock_guard<std::mutex> lock(mappingMutex_);
        for (unsigned i = 0; i < Count; ++i) {
            if (!topic_[i] || !path_[i]) continue;
            auto &binding = bindings_[i];
            const auto &topic = topic_[i]->get();
            const auto &key = path_[i]->get();
            const char *begin = key.c_str(), *end = begin + key.length();
            trim(begin, end);
            const size_t length = size_t(end - begin);
            const bool usable = topic.length() && length < sizeof(binding.path);
            if (binding.usable == usable && binding.topic == topic &&
                (!usable || (std::strlen(binding.path) == length && !std::memcmp(binding.path, begin, length)))) continue;
            binding.topic = topic;
            binding.usable = usable && binding.topic == topic;
            if (binding.usable) {
                std::memcpy(binding.path, begin, length);
                binding.path[length] = 0;
            }
            portENTER_CRITICAL(&mux_);
            invalidate(i);
            portEXIT_CRITICAL(&mux_);
        }
    }
    void reset() {
        portENTER_CRITICAL(&mux_);
        for (unsigned i = 0; i < Count; ++i) invalidate(i);
        portEXIT_CRITICAL(&mux_);
    }
    void detach() {
        std::lock_guard<std::mutex> lock(mappingMutex_);
        for (unsigned i = 0; i < Count; ++i) {
            topic_[i] = path_[i] = nullptr;
            bindings_[i].usable = false;
        }
        reset();
    }
    // now is monotonic uptime in milliseconds, not the 32-bit millis() value.
    bool read(Input i, uint64_t now, uint32_t timeout, float &value) const {
        portENTER_CRITICAL(&mux_);
        const Stamp stamp = stamps_[i];
        portEXIT_CRITICAL(&mux_);
        value = stamp.value;
        // A caller may have captured now just before a newer sample arrived.
        return stamp.valid && now >= stamp.at && now - stamp.at < timeout;
    }
    void receive(const char *topic, const uint8_t *payload, unsigned length, uint64_t now) {
        if (!topic || !payload) return;
        for (unsigned i = 0; i < Count; ++i) {
            char path[sizeof(Binding::path)];
            uint32_t generation;
            {
                std::lock_guard<std::mutex> lock(mappingMutex_);
                const auto &binding = bindings_[i];
                if (!binding.usable || binding.topic != topic) continue;
                std::memcpy(path, binding.path, sizeof(path));
                portENTER_CRITICAL(&mux_);
                stamps_[i].valid = false;
                generation = generations_[i];
                portEXIT_CRITICAL(&mux_);
            }
            if (!length || length > 1024 || std::memchr(payload, 0, length)) continue;
            float parsed = 0;
            if (!parse(i, path, reinterpret_cast<const char *>(payload), length, parsed)) continue;
            portENTER_CRITICAL(&mux_);
            if (generation == generations_[i]) stamps_[i] = {now, parsed, true};
            portEXIT_CRITICAL(&mux_);
        }
    }
private:
    static bool isSpace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
    static void trim(const char *&begin, const char *&end) {
        while (begin < end && isSpace(*begin)) ++begin;
        while (end > begin && isSpace(end[-1])) --end;
    }
    static bool decimalNumber(const char *text) {
        const char *p = text;
        while (isSpace(*p)) ++p;
        if (*p == '+' || *p == '-') ++p;
        bool digits = false;
        while (*p >= '0' && *p <= '9') { digits = true; ++p; }
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') { digits = true; ++p; }
        }
        if (!digits) return false;
        if (*p == 'e' || *p == 'E') {
            ++p;
            if (*p == '+' || *p == '-') ++p;
            const char *exponent = p;
            while (*p >= '0' && *p <= '9') ++p;
            if (p == exponent) return false;
        }
        while (isSpace(*p)) ++p;
        return !*p;
    }
    // CM text integers use the decimal prefix ("1e3" becomes 1); JSON numbers
    // use their numeric value. Validate the full text and both integer ranges.
    static bool convertText(unsigned i, const char *text, float &out) {
        if (!decimalNumber(text)) return false;
        char *end = nullptr;
        const double number = std::strtod(text, &end);
        while (end && isSpace(*end)) ++end;
        if (end == text || !end || *end || !std::isfinite(number)) return false;
        if (i == Price) { out = float(number); return std::isfinite(out); }
        if (number < double(INT_MIN) || number > double(INT_MAX)) return false;
        errno = 0;
        const long prefix = std::strtol(text, nullptr, 10);
        if (errno == ERANGE) return false;
        return checkedIntegerToFloat(double(prefix), out);
    }
    bool parse(unsigned i, char *path, const char *data, unsigned length, float &out) {
        const char *begin = data, *end = data + length;
        trim(begin, end);
        const size_t keyLength = std::strlen(path);
        const bool json = begin < end && (*begin == '{' || *begin == '[');
        if (!json) {
            if (keyLength && !(keyLength == 4 && strncasecmp(path, "none", 4) == 0)) return false;
            char text[48];
            const size_t n = size_t(end - begin);
            if (n >= sizeof(text)) return false;
            std::memcpy(text, begin, n);
            text[n] = 0;
            return convertText(i, text, out);
        }
        // An empty path selects the whole object/array, which is never numeric.
        if (!keyLength) return false;
        size_t pathLength = keyLength;
        // CM ignores one trailing separator.
        if (path[pathLength - 1] == '.') --pathLength;
        path[pathLength] = 0;
        const char *segments[16];
        unsigned segmentCount = 0;
        for (char *segment = path;;) {
            if (segmentCount == 16) return false;
            segments[segmentCount++] = segment;
            char *dot = std::strchr(segment, '.');
            if (!dot) break;
            *dot = 0;
            segment = dot + 1;
        }
        arena_.reset();
        JsonDocument filter(&arena_);
        JsonObject node = filter.to<JsonObject>();
        for (unsigned s = 0; s + 1 < segmentCount; ++s) node = node[segments[s]].to<JsonObject>();
        node[segments[segmentCount - 1]] = true;
        if (filter.overflowed()) return false;
        JsonDocument doc(&arena_);
        if (deserializeJson(doc, begin, size_t(end - begin), DeserializationOption::Filter(filter))) return false;
        JsonVariantConst value = doc.as<JsonVariantConst>();
        for (unsigned s = 0; s < segmentCount; ++s) value = value[segments[s]];
        if (value.isNull() || value.is<bool>()) return false;
        if (value.is<const char *>()) {
            const JsonString text = value.as<JsonString>();
            if (std::strlen(text.c_str()) != text.size()) return false;
            return convertText(i, text.c_str(), out);
        }
        if (!value.is<double>()) return false;
        const double number = value.as<double>();
        if (!std::isfinite(number)) return false;
        if (i == Price) { out = float(number); return std::isfinite(out); }
        return checkedIntegerToFloat(number, out);
    }
    struct Stamp { uint64_t at = 0; float value = 0; bool valid = false; };
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Stamp stamps_[Count];
    void invalidate(unsigned i) {
        stamps_[i] = Stamp{};
        ++generations_[i];
    }
    uint32_t generations_[Count] = {};
    struct Binding { String topic; char path[64] = {}; bool usable = false; };
    Binding bindings_[Count];
    std::mutex mappingMutex_;
    Config<String> *topic_[Count] = {}, *path_[Count] = {};
    JsonArena arena_;
};
} // namespace control
