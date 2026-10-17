#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace sbt {
enum class Kind { Real, Boolean, Unsigned, Text, Unsupported };
struct Channel {
  const char *topic;
  const char *key;
  Kind kind;
  uint8_t source;
};
inline constexpr Channel channels[] = {
    {"bms/charging", "bms_charging", Kind::Boolean, 0},
    {"bms/discharging", "bms_discharging", Kind::Boolean, 0},
    {"bms/state_of_charge_pct", "bms_soc_pct", Kind::Real, 0},
    {"bms/voltage_v", "bms_voltage_v", Kind::Real, 0},
    {"bms/current_a", "bms_current_a", Kind::Real, 0},
    {"bms/power_w", "bms_power_w", Kind::Real, 0},
    {"bms/remaining_capacity_ah", "bms_remaining_ah", Kind::Real, 0},
    {"bms/full_capacity_ah", "bms_full_ah", Kind::Real, 0},
    {"bms/charge_enabled", "bms_charge_enabled", Kind::Boolean, 0},
    {"bms/discharge_enabled", "bms_discharge_enabled", Kind::Boolean, 0},
    {"bms/temperature_max_c", "bms_temperature_max_c", Kind::Real, 0},
    {"bms/cell_voltage_min_v", "bms_cell_min_v", Kind::Real, 0},
    {"bms/cell_voltage_max_v", "bms_cell_max_v", Kind::Real, 0},
    {"bms/cell_voltage_delta_v", "bms_cell_delta_v", Kind::Real, 0},
    {"bms/alarm_bits", "bms_alarm_bits", Kind::Unsigned, 0},
    {"victron/solar_power_w", "victron_solar_power_w", Kind::Real, 1},
    {"victron/pv_voltage_v", "victron_pv_voltage_v", Kind::Unsupported, 1},
    {"victron/pv_current_a", "victron_pv_current_a", Kind::Unsupported, 1},
    {"victron/battery_voltage_v", "victron_battery_voltage_v", Kind::Real, 1},
    {"victron/charge_current_a", "victron_charge_current_a", Kind::Real, 1},
    {"victron/battery_power_w", "victron_battery_power_w", Kind::Unsupported,1},
    {"victron/load_current_a", "victron_load_current_a", Kind::Real, 1},
    {"victron/charge_state", "victron_charge_state", Kind::Text, 1},
    {"victron/error_code", "victron_error_code", Kind::Unsigned, 1},
    {"victron/yield_today_wh", "victron_yield_today_wh", Kind::Real, 1},
    {"bms/temperature_min_c", "bms_temperature_min_c", Kind::Real, 0},
    {"bms/cell_count", "bms_cell_count", Kind::Unsigned, 0},
    {"bms/temperature_sensor_count", "bms_temperature_sensor_count",
     Kind::Unsigned, 0},
    {"bms/protection", "bms_protection", Kind::Text, 0}};
inline constexpr size_t channelCount = sizeof(channels) / sizeof(channels[0]);
class Receiver {
public:
  static constexpr uint32_t timeoutMs = 15000;
  struct Health {
    bool online = false, available = false, seeded = false, advanced = false;
    uint32_t sequence = 0, received = 0, age = 0;
    char reason[64] = "not_received";
  };
  void reset() { *this = Receiver{}; }
  static constexpr bool commandsLocked() { return true; }
  bool fresh(unsigned source, uint32_t now) const {
    if (source > 1)
      return false;
    const auto &h = health_[source];
    return online_ && h.online && h.available && h.advanced &&
           uint32_t(now - h.received) < timeoutMs && h.age < timeoutMs &&
           uint32_t(now - h.received) < timeoutMs - h.age;
  }
  bool valid(const char *topic, uint32_t now) const {
    int i = index(topic);
    return i >= 0 && values_[i].seen && fresh(channels[i].source, now);
  }
  uint32_t sequence(unsigned source) const {
    return source < 2 ? health_[source].sequence : 0;
  }
  Health sourceHealth(unsigned source) const {
    return source < 2 ? health_[source] : Health{};
  }
  void diagnosticValues(uint32_t now, double *values, uint32_t &validMask) const {
    validMask = 0;
    for (size_t i = 0; i < channelCount; ++i) {
      values[i] = 0;
      if (channels[i].kind == Kind::Text || channels[i].kind == Kind::Unsupported ||
          !valid(channels[i].topic, now)) continue;
      values[i] = values_[i].number;
      validMask |= uint32_t(1) << i;
    }
  }
  bool copyDiagnosticText(const char *topic, uint32_t now, char *out, size_t capacity) const {
    const int i = index(topic);
    if (i < 0 || !out || !capacity || !valid(topic, now) || channels[i].kind != Kind::Text) return false;
    const char *value = i == 28 ? protection_ : chargeState_;
    const int n = std::snprintf(out, capacity, "%s", value);
    return n >= 0 && size_t(n) < capacity;
  }
  double numeric(const char *topic) const {
    int i = index(topic);
    return i >= 0 ? values_[i].number : 0;
  }
  bool receive(const char *topic, const char *payload, size_t length,
               uint32_t now) {
    if (!topic || !payload)
      return false;
    if (length > 1024) {
      const int i = index(topic);
      if (i >= 0)
        values_[i].seen = false;
      return false;
    }
    char text[1025];
    std::memcpy(text, payload, length);
    text[length] = '\0';
    if (std::strlen(text) != length) {
      const int i = index(topic);
      if (i >= 0)
        values_[i].seen = false;
      return false;
    }
    if (!std::strcmp(topic, "status")) {
      if (std::strcmp(text, "online") && std::strcmp(text, "offline"))
        return false;
      if (!std::strcmp(text, "offline")) {
        reset();
        return true;
      }
      online_ = true;
      return true;
    }
    for (unsigned s = 0; s < 2; ++s) {
      const char *availability =
          s ? "victron/availability" : "bms/availability";
      if (!std::strcmp(topic, availability)) {
        if (std::strcmp(text, "available") && std::strcmp(text, "unavailable"))
          return false;
        if (!std::strcmp(text, "unavailable")) {
          clearSource(s);
          health_[s].online = false;
        } else
          health_[s].online = true;
        return true;
      }
      if (!std::strcmp(topic, s ? "victron/health" : "bms/health"))
        return receiveHealth(s, text, now);
    }
    if (!std::strcmp(topic, "bms/control/availability")) {
      if (std::strcmp(text, "available") && std::strcmp(text, "unavailable") &&
          std::strcmp(text, "disabled"))
        return false;
      controlAvailable_ = !std::strcmp(text, "available");
      return true;
    }
    if (!std::strcmp(topic, "bms/control")) {
      JsonDocument d;
      auto error = deserializeJson(d, text);
      const char *session = d["session"] | "";
      if (error || !d["enabled"].is<bool>() || !d["ready"].is<bool>() ||
          !d["pending"].is<bool>() || !hexSession(session) ||
          !d["reason"].is<const char *>()) {
        session_[0] = 0;
        remoteReady_ = false;
        return false;
      }
      std::snprintf(session_, sizeof(session_), "%s", session);
      remoteEnabled_ = d["enabled"];
      remoteReady_ = d["ready"];
      remotePending_ = d["pending"];
      std::snprintf(controlReason_, sizeof(controlReason_), "%s",
                    d["reason"].as<const char *>());
      return true;
    }
    if (!std::strcmp(topic, "bms/control/result")) {
      JsonDocument d;
      if (deserializeJson(d, text) || !d["request_id"].is<const char *>() ||
          !d["phase"].is<const char *>() || !d["reason"].is<const char *>() ||
          length >= sizeof(result_))
        return false;
      std::memcpy(result_, text, length + 1);
      return true;
    }
    const int i = index(topic);
    if (i < 0)
      return false;
    auto &v = values_[i];
    v.seen = false;
    const auto kind = channels[i].kind;
    if (!std::strcmp(text, "null") || kind == Kind::Unsupported)
      return true;
    if (kind == Kind::Text) {
      char *output = i == 28 ? protection_ : chargeState_;
      const size_t capacity =
          i == 28 ? sizeof(protection_) : sizeof(chargeState_);
      if (length >= capacity)
        return false;
      if (i == 22) {
        JsonDocument d;
        if (deserializeJson(d, text) || !d.is<const char *>())
          return false;
        std::snprintf(output, capacity, "%s", d.as<const char *>());
      } else
        std::memcpy(output, text, length + 1);
      v.seen = true;
      return true;
    }
    JsonDocument d;
    if (deserializeJson(d, text))
      return false;
    if (kind == Kind::Boolean) {
      if (!d.is<bool>())
        return false;
      v.number = d.as<bool>() ? 1 : 0;
    } else {
      if (!d.is<double>() || d.is<bool>())
        return false;
      v.number = d.as<double>();
      if (!std::isfinite(v.number))
        return false;
      if (kind == Kind::Unsigned && (v.number < 0 || v.number > 4294967295.0 ||
                                     std::floor(v.number) != v.number))
        return false;
    }
    v.seen = true;
    return true;
  }
  // Preview only: deliberately has no MQTT transport or publish dependency.
  bool previewDisable(bool charge, const char *requestId, uint64_t now,
                      char *output, size_t capacity) const {
    (void)charge;
    if (!output || !capacity || !hexSession(session_) || !requestId ||
        now < 1700000000ULL || now > 4294967265ULL)
      return false;
    const size_t n = std::strlen(requestId);
    if (!n || n > 32)
      return false;
    for (size_t i = 0; i < n; ++i) {
      char c = requestId[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-'))
        return false;
    }
    const int nout = std::snprintf(
        output, capacity,
        "{\"state\":false,\"request_id\":\"%s\",\"session\":\"%s\","
        "\"expires_at\":%llu}",
        requestId, session_, static_cast<unsigned long long>(now + 30));
    return nout >= 0 && size_t(nout) < capacity;
  }
  static const char *disableTopic(bool charge) {
    return charge ? "bms/charge_enabled/set" : "bms/discharge_enabled/set";
  }
  void writeRuntime(JsonObject out, uint32_t now) const {
    out["online"] = online_;
    out["commands_locked"] = true;
    out["remote_control_ready"] =
        remoteReady_ && controlAvailable_ && fresh(0, now);
    out["remote_control_enabled"] = remoteEnabled_;
    out["remote_control_pending"] = remotePending_;
    out["remote_control_available"] = controlAvailable_;
    // ArduinoJson treats const arrays as borrowed literals. Force copies while
    // the service mutex is held; HTTP serialization runs after this method.
    out["control_reason"] = static_cast<const char *>(controlReason_);
    out["control_result"] = static_cast<const char *>(result_);
    out["charge_disable_topic"] = disableTopic(true);
    out["discharge_disable_topic"] = disableTopic(false);
    for (unsigned s = 0; s < 2; ++s) {
      const auto &h = health_[s];
      out[s ? "victron_fresh" : "bms_fresh"] = fresh(s, now);
      out[s ? "victron_reason" : "bms_reason"] =
          static_cast<const char *>(h.reason);
      out[s ? "victron_sequence" : "bms_sequence"] = h.sequence;
    }
    unsigned received = 0;
    for (size_t i = 0; i < channelCount; ++i) {
      if (values_[i].seen)
        ++received;
      if (!values_[i].seen || !fresh(channels[i].source, now)) {
        out[channels[i].key] = nullptr;
        continue;
      }
      if (channels[i].kind == Kind::Text)
        out[channels[i].key] = i == 28 ? protection_ : chargeState_;
      else if (channels[i].kind == Kind::Boolean)
        out[channels[i].key] = values_[i].number != 0;
      else
        out[channels[i].key] = values_[i].number;
    }
    out["received_fields"] = received;
  }

private:
  struct Value {
    double number = 0;
    bool seen = false;
  };
  std::array<Value, channelCount> values_{};
  Health health_[2];
  bool online_ = false, remoteReady_ = false, remoteEnabled_ = false,
       remotePending_ = false, controlAvailable_ = false;
  char protection_[512] = "", chargeState_[32] = "", session_[33] = "",
       controlReason_[64] = "not_received", result_[512] = "";
  static int index(const char *topic) {
    for (size_t i = 0; i < channelCount; ++i)
      if (!std::strcmp(topic, channels[i].topic))
        return int(i);
    return -1;
  }
  static bool hexSession(const char *s) {
    if (!s || std::strlen(s) != 32)
      return false;
    for (unsigned i = 0; i < 32; ++i)
      if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
            (s[i] >= 'A' && s[i] <= 'F')))
        return false;
    return true;
  }
  void clearValues(unsigned s) {
    for (size_t i = 0; i < channelCount; ++i)
      if (channels[i].source == s)
        values_[i].seen = false;
  }
  void clearSource(unsigned s) {
    clearValues(s);
    health_[s].seeded = false;
    health_[s].advanced = false;
    health_[s].available = false;
  }
  bool receiveHealth(unsigned s, const char *text, uint32_t now) {
    JsonDocument d;
    auto &h = health_[s];
    if (deserializeJson(d, text) || !d["available"].is<bool>() ||
        !d["reason"].is<const char *>() ||
        !d["sample_sequence"].is<uint32_t>() ||
        (!d["sample_age_ms"].is<uint32_t>() && d["available"].as<bool>())) {
      clearSource(s);
      return false;
    }
    const bool available = d["available"];
    const uint32_t seq = d["sample_sequence"],
                   age = d["sample_age_ms"] | timeoutMs;
    if (!available || age >= timeoutMs) {
      clearSource(s);
      std::snprintf(h.reason, sizeof(h.reason), "%s",
                    d["reason"].as<const char *>());
      return true;
    }
    if (h.seeded && (uint32_t(now - h.received) >= timeoutMs ||
                     int32_t(seq - h.sequence) < 0))
      clearSource(s);
    if (!h.seeded) {
      h.seeded = true;
      h.sequence = seq;
      h.received = now;
      h.age = age;
      h.advanced = false;
    } else if (int32_t(seq - h.sequence) > 0) {
      h.sequence = seq;
      h.received = now;
      h.age = age;
      h.advanced = true;
    }
    h.available = true;
    std::snprintf(h.reason, sizeof(h.reason), "%s",
                  d["reason"].as<const char *>());
    return true;
  }
};
} // namespace sbt
