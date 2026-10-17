#pragma once
#include "ConfigManager.h"
#include "SbtReceiver.h"
#include "mqtt/MQTTManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sbt {
class Service {
  public:
    void attach(ConfigManagerClass &cfg, cm::MQTTManager &mqtt) {
        mqtt_ = &mqtt;
        mutex_ = xSemaphoreCreateMutex();
        cfg.addSetting(&inputRoot_);
        // The explicit key preserves NVS storage when changing the UI category.
        mqtt.onMQTTConnect([this]() {
            reset();
            subscribed_ = false;
        });
        mqtt.onMQTTDisconnect([this]() {
            reset();
            subscribed_ = false;
        });
        mqtt.onNewMQTTMessage(
            [this](const cm::MQTTManager::MqttMessageView &m) {
                if (!mutex_ || !m.topic)
                    return;
                const size_t n = root_.length();
                if (!n || std::strncmp(m.topic, root_.c_str(), n) ||
                    m.topic[n] != '/')
                    return;
                xSemaphoreTake(mutex_, portMAX_DELAY);
                receiver_.receive(m.topic + n + 1,
                                  reinterpret_cast<const char *>(m.payload),
                                  m.length, millis());
                xSemaphoreGive(mutex_);
            });
        cfg.getRuntime().addRuntimeProvider(
            "sbt",
            [this](JsonObject &data) {
                if (!mutex_)
                    return;
                xSemaphoreTake(mutex_, portMAX_DELAY);
                receiver_.writeRuntime(data, millis());
                xSemaphoreGive(mutex_);
            },
            4);
        const char *page = "BMS / MPPT 150/35";
        cfg.addLivePage(page, 30);
        cfg.addLiveCard(page, "Source Health", 10);
        cfg.addLiveCard(page, "BMS", 20);
        cfg.addLiveCard(page, "MPPT 150/35", 30);
        cfg.addLiveCard(page, "MOS Control Status", 40);
        auto meta = [&cfg, page](const char *key, const char *label,
                                const char *card, Kind kind, int order,
                                const char *unit = "", int precision = 2) {
            RuntimeFieldMeta m;
            m.sourceGroup = "sbt";
            m.page = page;
            m.card = card;
            m.group = card;
            m.key = key;
            m.label = label;
            m.unit = unit;
            m.order = order;
            m.isBool = kind == Kind::Boolean;
            m.isString = kind == Kind::Text;
            m.precision = precision;
            cfg.getRuntime().addRuntimeMeta(m);
        };
        meta("online", "SBT online", "Source Health", Kind::Boolean, 0);
        meta("bms_fresh", "BMS data fresh", "Source Health", Kind::Boolean, 1);
        meta("bms_reason", "BMS source reason", "Source Health", Kind::Text, 2);
        meta("victron_fresh", "MPPT data fresh", "Source Health", Kind::Boolean, 3);
        meta("victron_reason", "MPPT source reason", "Source Health", Kind::Text, 4);
        meta("commands_locked", "SIL MOS commands locked", "MOS Control Status", Kind::Boolean, 0);
        meta("remote_control_enabled", "SBT control enabled", "MOS Control Status", Kind::Boolean, 1);
        meta("remote_control_ready", "SBT control ready", "MOS Control Status", Kind::Boolean, 2);
        meta("remote_control_pending", "SBT command pending", "MOS Control Status", Kind::Boolean, 3);
        meta("control_reason", "SBT control reason", "MOS Control Status", Kind::Text, 4);
        meta("control_result", "Last SBT control result", "MOS Control Status", Kind::Text, 5);
        // Entries follow the receiver contract, including explicitly unsupported
        // channels which remain null rather than displaying invented values.
        static const char *const labels[] = {
            "Charging", "Discharging", "State of charge", "Battery voltage",
            "Battery current", "Battery power", "Remaining capacity", "Full capacity",
            "Charge MOS enabled", "Discharge MOS enabled", "Maximum temperature",
            "Minimum cell voltage", "Maximum cell voltage", "Cell voltage difference",
            "Alarm bits", "Solar power", "PV voltage (unsupported)",
            "PV current (unsupported)", "Battery voltage", "Charge current",
            "Battery power (unsupported)", "Load current", "Charge state", "Error code",
            "Yield today", "Minimum temperature", "Cell count", "Temperature sensor count",
            "Protection status"};
        static const char *const units[] = {
            "", "", "%", "V", "A", "W", "Ah", "Ah", "", "", "\u00B0C",
            "V", "V", "V", "", "W", "V", "A", "V", "A", "W", "A",
            "", "", "Wh", "\u00B0C", "", "", ""};
        static_assert(sizeof(labels) / sizeof(labels[0]) == channelCount,
                      "Every SBT channel needs a display label");
        static_assert(sizeof(units) / sizeof(units[0]) == channelCount,
                      "Every SBT channel needs a unit");
        for (size_t i = 0; i < channelCount; ++i)
            meta(channels[i].key, labels[i], channels[i].source ? "MPPT 150/35" : "BMS",
                 channels[i].kind, int(i) + 10, units[i],
                 channels[i].kind == Kind::Unsigned ? 0 : 2);

    }
    void loop() {
        if (!mqtt_ || !mutex_)
            return;
        const String next = inputRoot_.get();
        if (next != root_) {
            if (subscribed_)
                mqtt_->unsubscribe((root_ + "/#").c_str());
            root_ = next;
            subscribed_ = false;
            reset();
        }
        if (!mqtt_->isConnected()) {
            if (subscribed_) {
                reset();
                subscribed_ = false;
            }
            return;
        }
        if (!subscribed_ && validRoot(root_) &&
            uint32_t(millis() - lastSubscribe_) >= 1000) {
            lastSubscribe_ = millis();
            subscribed_ = mqtt_->subscribe((root_ + "/#").c_str());
        }
    }

    struct ControlSnapshot {
        bool bmsValid = false, chargerValid = false;
        float soc = 0, batteryW = 0, chargerW = 0, fullCapacityAh = NAN;
        uint32_t bmsSequence = 0, chargerSequence = 0;
    };
    struct DiagnosticSnapshot {
        bool online = false, fresh[2] = {}, chargeStateValid = false, protectionValid = false;
        uint32_t sequence[2] = {}, validMask = 0;
        double values[channelCount] = {};
        char reason[2][64] = {{0}, {0}};
        char chargeState[32] = {}, protection[128] = {};
    };
    DiagnosticSnapshot diagnosticSnapshot(uint32_t now) {
        DiagnosticSnapshot s;
        if (!mutex_) return s;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const auto bms = receiver_.sourceHealth(0);
        const auto victron = receiver_.sourceHealth(1);
        s.online = bms.online || victron.online;
        s.fresh[0] = receiver_.fresh(0, now);
        s.fresh[1] = receiver_.fresh(1, now);
        s.sequence[0] = bms.sequence; s.sequence[1] = victron.sequence;
        std::snprintf(s.reason[0], sizeof(s.reason[0]), "%s", bms.reason);
        std::snprintf(s.reason[1], sizeof(s.reason[1]), "%s", victron.reason);
        receiver_.diagnosticValues(now, s.values, s.validMask);
        s.chargeStateValid = receiver_.copyDiagnosticText("victron/charge_state", now, s.chargeState, sizeof(s.chargeState));
        s.protectionValid = receiver_.copyDiagnosticText("bms/protection", now, s.protection, sizeof(s.protection));
        xSemaphoreGive(mutex_);
        return s;
    }
    ControlSnapshot controlSnapshot(uint32_t now) {
        ControlSnapshot s;
        if (!mutex_ || !mqtt_ || !mqtt_->isConnected()) return s;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        s.bmsValid = receiver_.valid("bms/state_of_charge_pct", now) &&
                     receiver_.valid("bms/power_w", now);
        s.chargerValid = receiver_.valid("victron/battery_voltage_v", now) &&
                         receiver_.valid("victron/charge_current_a", now);
        if (s.bmsValid) {
            s.soc = receiver_.numeric("bms/state_of_charge_pct");
            s.batteryW = receiver_.numeric("bms/power_w");
            s.bmsValid = s.soc >= 0 && s.soc <= 100 && std::isfinite(s.batteryW);
        }
        if (s.chargerValid) {
            const double v = receiver_.numeric("victron/battery_voltage_v");
            const double a = receiver_.numeric("victron/charge_current_a");
            s.chargerW = v * a;
            s.chargerValid = v > 0 && a >= 0 && std::isfinite(s.chargerW);
        }
        if (receiver_.valid("bms/full_capacity_ah", now))
            s.fullCapacityAh = receiver_.numeric("bms/full_capacity_ah");
        s.bmsSequence = receiver_.sequence(0);
        s.chargerSequence = receiver_.sequence(1);
        xSemaphoreGive(mutex_);
        return s;
    }
  private:
    Config<String> inputRoot_{
        ConfigOptions<String>{.key = "SBTInputRoot",
                              .name = "SBT MQTT Input Root",
                              .category = "MQTT-Topics",
                              .defaultValue = String("SolarBatteryTelemetry"),
                              .sortOrder = 1,
                              .categoryPretty = "MQTT Topics",
                              .card = "MQTT-Topics",
                              .cardPretty = "MQTT-Received",
                              .cardOrder = 50}};
    Receiver receiver_;
    cm::MQTTManager *mqtt_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    String root_;
    bool subscribed_ = false;
    uint32_t lastSubscribe_ = 0;
    void reset() {
        if (mutex_) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            receiver_.reset();
            xSemaphoreGive(mutex_);
        }
    }
    static bool validRoot(const String &root) {
        if (root.isEmpty() || root.length() > 63 || root[0] == '/' ||
            root[root.length() - 1] == '/')
            return false;
        for (unsigned i = 0; i < root.length(); ++i) {
            const char c = root[i];
            if (c <= 32 || c >= 127 || c == '+' || c == '#' || c == '"' ||
                c == '\\' || (c == '/' && i && root[i - 1] == '/'))
                return false;
        }
        return true;
    }
};
} // namespace sbt
