#pragma once

#include "ConfigManager.h"
#include "PlanLifecycleSettings.h"
#include <climits>

namespace control {

// CM persists each field separately. The control task always reads a complete,
// validated copy, including when a web batch stops after a partial update.
class PlanSettingsRuntime {
    using Member = uint32_t PlanLifecycleSettings::*;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    PlanLifecycleSettings active_;
    bool invalidLoad_ = false;
    ConfigManagerClass *config_ = nullptr;

    static bool matchesWebInput(const ConfigRequestContext &request, int number) {
        // CM converts request values through String before fromJSON. Its Vue number
        // inputs also submit strings. Accept only a decimal integer in either form.
        if (request.origin == ConfigRequestContext::Origin::None) return false;
        JsonDocument body;
        if (deserializeJson(body, request.payload.c_str())) return false;
        const JsonVariantConst original = body["value"];
        if (original.is<int>()) return original.as<int>() == number;
        if (!original.is<const char *>()) return false;
        const JsonString digits = original.as<JsonString>();
        if (!digits.size() || digits.size() > 10) return false;
        int decimal = 0;
        for (size_t index = 0; index < digits.size(); ++index) {
            const char c = digits.c_str()[index];
            if (c < '0' || c > '9') return false;
            const int digit = c - '0';
            if (decimal > (INT_MAX - digit) / 10) return false;
            decimal = decimal * 10 + digit;
        }
        return decimal == number;
    }

    class Setting : public Config<int> {
        PlanSettingsRuntime &owner_;
        Member member_;
        bool loaded_ = false;

    public:
        Setting(PlanSettingsRuntime &owner, Member member, const char *key,
                const char *label, const char *card, int cardOrder, int order = 1)
            : Config<int>(ConfigOptions<int>{.key = key, .name = label, .category = "Plan",
                            .defaultValue = static_cast<int>(PlanLifecycleSettings{}.*member),
                            .sortOrder = order, .card = card, .cardPretty = card, .cardOrder = cardOrder}),
              owner_(owner), member_(member) {}

        void load(Preferences &prefs) override {
            loaded_ = false;
            if (hasError()) return;
            if (prefs.isKey(getKey())) {
                // Wrong type, unreadable data and negative values all reject the profile.
                const int stored = prefs.getInt(getKey(), INT_MIN);
                if (stored < 0) return;
                set(stored);
                modified = false;
            } else {
                Config<int>::load(prefs);
                if (needsSave()) return;
            }
            loaded_ = true;
        }

        bool collect(PlanLifecycleSettings &values) const {
            values.*member_ = static_cast<uint32_t>(get());
            return loaded_;
        }

        void collectStored(Preferences &prefs, PlanLifecycleSettings &values) const {
            if (prefs.isKey(getKey()))
                values.*member_ = static_cast<uint32_t>(prefs.getInt(getKey(), INT_MIN));
        }

        void useRamDefault() {
            set(static_cast<int>(PlanLifecycleSettings{}.*member_));
            modified = false;
        }

        bool fromJSON(const JsonVariant &value) override {
            if (hasError() || !value.is<int>()) return false;
            const int number = value.as<int>();
            if (number < 0 || !owner_.config_) return false;
            const auto &request = owner_.config_->getCurrentRequestContext();
            if (!matchesWebInput(request, number)) return false;
            auto candidate = owner_.snapshot();
            candidate.*member_ = static_cast<uint32_t>(number);
            if (!candidate.valid()) return false;
            const bool save = request.origin == ConfigRequestContext::Origin::SaveSingle ||
                              request.origin == ConfigRequestContext::Origin::SaveAll;
            if (save) {
                // CM's virtual save returns void. Complete and verify this HTTP
                // save while failure can still reach its single/batch response.
                Preferences prefs;
                if (!prefs.begin("ConfigManager", false)) return false;
                // A prior Apply may have relaxed an upper limit only in RAM.
                // Also validate the combination that the next boot would read.
                PlanLifecycleSettings stored;
                for (const auto &field : owner_.fields_)
                    if (&field != this) field.collectStored(prefs, stored);
                stored.*member_ = static_cast<uint32_t>(number);
                bool saved = false;
                if (stored.valid()) {
                    saved = prefs.getInt(getKey(), INT_MIN) == number;
                    if (!saved && prefs.putInt(getKey(), number) == sizeof(int32_t))
                        saved = prefs.getInt(getKey(), INT_MIN) == number;
                }
                prefs.end();
                if (!saved) return false;
            }
            // AsyncTCP dispatches the synchronous HTTP callbacks on one task.
            // The control task only observes active_ via snapshot().
            portENTER_CRITICAL(&owner_.mux_);
            owner_.active_ = candidate;
            set(number);
            modified = false; // Apply is volatile; Save has already been verified.
            portEXIT_CRITICAL(&owner_.mux_);
            return true;
        }

        void setDefault() override {
            // CM's reset-all visits all entries sequentially. Expose one valid runtime generation.
            portENTER_CRITICAL(&owner_.mux_);
            Config<int>::setDefault();
            owner_.active_ = PlanLifecycleSettings{};
            portEXIT_CRITICAL(&owner_.mux_);
        }
    };

    Setting fields_[8] = {
        {*this, &PlanLifecycleSettings::planRecalcIntervalS, "planCalcS", "Plan Recalculation Interval (s)", "Planning and Recalculation", 1},
        {*this, &PlanLifecycleSettings::planStaleAfterS, "planStaleS", "Plan Stale After (s)", "Plan Validity and Retention", 2},
        {*this, &PlanLifecycleSettings::planMaxAgeS, "planMaxAgeS", "Maximum Executable Plan Age (s)", "Plan Validity and Retention", 2, 2},
        {*this, &PlanLifecycleSettings::planDiscardAfterS, "planHistS", "Plan History Retention (s)", "Plan Validity and Retention", 2, 3},
        {*this, &PlanLifecycleSettings::priceScheduleFreshnessS, "priceFreshS", "Price Schedule Freshness (s)", "Price Schedule Validity", 3},
        {*this, &PlanLifecycleSettings::pvForecastMaxAgeS, "pvFcAgeS", "PV Forecast Maximum Age (s)", "Forecast Freshness", 4},
        {*this, &PlanLifecycleSettings::planRetentionS, "planHoldS", "Plan Outage Retention (s)", "Communication Tolerance and Fallback", 5},
        {*this, &PlanLifecycleSettings::planPublishIntervalS, "planPubS", "Plan Publication Interval (s)", "Plan Diagnostics", 6},
    };

public:
    void attachTo(ConfigManagerClass &config) {
        config_ = &config;
        for (auto &setting : fields_) config.addSetting(&setting);
    }

    void finishLoad() {
        PlanLifecycleSettings loaded;
        bool readable = true;
        for (const auto &setting : fields_) {
            if (!setting.collect(loaded)) readable = false;
        }
        invalidLoad_ = !readable || !loaded.valid();
        if (invalidLoad_) {
            for (auto &setting : fields_) setting.useRamDefault();
        }
        active_ = invalidLoad_ ? PlanLifecycleSettings{} : loaded;
    }

    PlanLifecycleSettings snapshot() {
        portENTER_CRITICAL(&mux_);
        const auto values = active_;
        portEXIT_CRITICAL(&mux_);
        return values;
    }

    bool usingDefaultsAfterInvalidLoad() const { return invalidLoad_; }
};

} // namespace control
