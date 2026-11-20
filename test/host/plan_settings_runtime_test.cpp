#include "control/PlanSettingsRuntime.h"

#include <cassert>
#include <cstdio>
#include <set>

using control::PlanLifecycleSettings;
using control::PlanSettingsRuntime;

// [MOCKED!] CM registration, NVS and RTOS boundaries. The settings adapter,
// numerical validation and ArduinoJson parser are the firmware implementations.
struct Boot {
    ConfigManagerClass cm;
    PlanSettingsRuntime runtime;

    Boot() {
        runtime.attachTo(cm);
        Preferences prefs;
        for (auto *setting : cm.settings) setting->load(prefs);
        runtime.finishLoad();
    }
    bool edit(const char *key, const char *json,
              ConfigRequestContext::Origin origin = ConfigRequestContext::Origin::SaveSingle,
              const char *wire = nullptr) {
        for (size_t i = 0; i < cm.settings.size(); ++i) {
            auto *field = static_cast<Config<int> *>(cm.settings[i]);
            if (std::string(key) != field->getKey()) continue;
            cm.context.origin = origin;
            cm.context.payload = std::string("{\"value\":") + (wire ? wire : json) + "}";
            JsonDocument parsed;
            assert(!deserializeJson(parsed,json));
            const bool accepted = field->fromJSON(parsed.as<JsonVariant>());
            if (accepted && (origin == ConfigRequestContext::Origin::SaveSingle ||
                             origin == ConfigRequestContext::Origin::SaveAll)) {
                Preferences prefs;
                field->save(prefs);
            }
            cm.context = {};
            return accepted;
        }
        assert(false);
        return false;
    }
};

int main() {
    Boot fresh;
    assert(!fresh.runtime.usingDefaultsAfterInvalidLoad());
    assert(fresh.runtime.snapshot().valid());
    std::set<std::string> keys, cards;
    for (auto *setting : fresh.cm.settings) {
        const auto &options = static_cast<Config<int> *>(setting)->options();
        assert(std::string(options.category) == "Plan" && keys.insert(options.key).second);
        cards.insert(options.card);
    }
    assert(keys.size() == 8 && cards.size() == 6 && Preferences::integers.size() == 8);
    assert(fresh.runtime.snapshot().planRecalcIntervalS == 900);
    assert(fresh.runtime.snapshot().priceScheduleFreshnessS == 180);
    assert(fresh.runtime.snapshot().pvForecastMaxAgeS == 0);
    assert(fresh.runtime.snapshot().planPublishIntervalS == 60);
    assert(fresh.runtime.snapshot().planMaxAgeS == 7200);
    using Origin = ConfigRequestContext::Origin;
    assert(fresh.edit("planMaxAgeS", "7300", Origin::ApplySingle, "\"7300\""));
    assert(fresh.runtime.snapshot().planMaxAgeS == 7300 && Preferences::integers["planMaxAgeS"] == 7200);
    assert(fresh.edit("planMaxAgeS", "7300", Origin::SaveAll, "\"7300\""));
    assert(Preferences::integers["planMaxAgeS"] == 7300);
    assert(fresh.edit("planHistS", "200000", Origin::ApplySingle));
    assert(!fresh.edit("planMaxAgeS", "100000", Origin::SaveSingle));
    assert(Preferences::integers["planMaxAgeS"] == 7300 && fresh.runtime.snapshot().planMaxAgeS == 7300);
    assert(!fresh.edit("pvFcAgeS", "0", Origin::SaveSingle, "\"\""));
    assert(!fresh.edit("planMaxAgeS", "7200", Origin::ApplyAll, "\"+7200\""));
    assert(!fresh.edit("planMaxAgeS", "7200", Origin::ApplyAll, "\"7200.0\""));
    assert(!fresh.edit("planMaxAgeS", "7200", Origin::ApplyAll, "\"7200\\u0000extra\""));
    assert(!fresh.edit("planMaxAgeS", "7200", Origin::None));
    Preferences::failWrite = true;
    assert(!fresh.edit("planMaxAgeS", "7200", Origin::SaveAll, "\"7200\""));
    assert(fresh.runtime.snapshot().planMaxAgeS == 7300 && Preferences::integers["planMaxAgeS"] == 7300);
    Preferences::failWrite = false;
    for (const char *json : {"null", "true", "60.1", "\"60\"", "-1", "99999999999999"})
        assert(!fresh.edit("planPubS", json));
    assert(fresh.edit("planPubS", "30") && fresh.edit("planPubS", "600"));
    assert(!fresh.edit("planPubS", "29") && !fresh.edit("planPubS", "601"));
    assert(fresh.edit("pvFcAgeS", "0") && !fresh.edit("pvFcAgeS", "1"));
    assert(!fresh.edit("pvFcAgeS", "299") && fresh.edit("pvFcAgeS", "300"));
    assert(fresh.edit("pvFcAgeS", "86400") && !fresh.edit("pvFcAgeS", "86401"));
    assert(!fresh.edit("priceFreshS", "59") && fresh.edit("priceFreshS", "60"));
    assert(fresh.edit("priceFreshS", "3600") && !fresh.edit("priceFreshS", "3601"));
    assert(!fresh.edit("planHoldS", "299") && fresh.edit("planHoldS", "300"));
    assert(fresh.edit("planHoldS", "86400") && !fresh.edit("planHoldS", "86401"));
    assert(fresh.edit("planCalcS", "60") && !fresh.edit("planCalcS", "59"));
    assert(fresh.edit("planStaleS", "60") && !fresh.edit("planStaleS", "59"));
    assert(fresh.edit("planMaxAgeS", "600") && !fresh.edit("planMaxAgeS", "599"));
    assert(fresh.edit("planHistS", "3600") && !fresh.edit("planHistS", "3599"));
    assert(fresh.runtime.snapshot().planRetentionS > fresh.runtime.snapshot().planDiscardAfterS);
    assert(!fresh.edit("planMaxAgeS", "7200") && fresh.runtime.snapshot().planMaxAgeS == 600);
    assert(fresh.edit("planHistS", "604800") && !fresh.edit("planHistS", "604801"));
    assert(fresh.edit("planMaxAgeS", "604800") && !fresh.edit("planMaxAgeS", "604801"));
    assert(fresh.edit("planStaleS", "86400") && !fresh.edit("planStaleS", "86401"));
    assert(fresh.edit("planCalcS", "3600") && !fresh.edit("planCalcS", "3601"));
    Boot reboot;
    assert(reboot.runtime.snapshot().planStaleAfterS == 86400);
    assert(reboot.runtime.snapshot().planRetentionS == 86400);
    assert(!reboot.runtime.usingDefaultsAfterInvalidLoad());
    Preferences::integers["planHistS"] = 3600; // Inconsistent saved combination.
    const auto damaged = Preferences::integers;
    Boot fallback;
    assert(fallback.runtime.usingDefaultsAfterInvalidLoad() && fallback.runtime.snapshot().valid());
    assert(fallback.runtime.snapshot().planStaleAfterS == 1200 && fallback.runtime.snapshot().planPublishIntervalS == 60);
    Preferences prefs;
    for (auto *setting : fallback.cm.settings) setting->save(prefs);
    assert(Preferences::integers == damaged);
    assert(!fallback.edit("planPubS", "60") && Preferences::integers == damaged);
    assert(fallback.edit("planHistS", "604800") && fallback.edit("planPubS", "60"));
    assert(Preferences::integers["planPubS"] == 60);
    Preferences::failRead = true;
    Boot unreadable;
    assert(unreadable.runtime.usingDefaultsAfterInvalidLoad());
    Preferences::failRead = false;
    Preferences::integers.clear();
    Preferences::failWrite = true;
    Boot full;
    assert(full.runtime.usingDefaultsAfterInvalidLoad() && Preferences::integers.empty());
    Preferences::failWrite = false;
    for (auto *setting : fresh.cm.settings) setting->setDefault();
    assert(fresh.runtime.snapshot().planMaxAgeS == 7200 && fresh.runtime.snapshot().planPublishIntervalS == 60);
    std::puts("Plan settings layout, input, persistence and runtime snapshot tests passed");
}
