#include <ctime>
static std::time_t testFixtureUtc = 1800000000;
static std::time_t testFixtureTime(std::time_t *out) {
    if (out) *out = testFixtureUtc;
    return testFixtureUtc;
}
static bool rs485TickDue = false;

// Reuse the production-tick fixture without running its unrelated P0a cases.
#define main p0aApplicationHarnessMain
#include "limiter_disabled_application_test.cpp"
#undef main

struct EconomicStatus {
    bool valid = false;
    int batteryLimitW = -1;
    std::string strategy, reason;
};

// [MOCKED!] A complete synthetic forecast feeds the real PV receiver.
static bool loadQualifiedPvForecast() {
    constexpr uint32_t utc = 1800000000u;
    for (unsigned part = 0; part < 6; ++part) {
        JsonDocument doc;
        doc["v"] = 2; doc["s"] = utc - 900; doc["r"] = utc; doc["g"] = utc;
        doc["t"] = utc; doc["d"] = 900; doc["n"] = 144; doc["k"] = part;
        doc["e"] = utc + 144 * 900; doc["f"] = 1000; doc["c"] = 0; doc["a"] = 0;
        doc["q"] = "uncalibrated"; doc["o"] = "forecast_solar";
        JsonArray watts = doc["p"].to<JsonArray>();
        JsonArray quality = doc["x"].to<JsonArray>();
        for (unsigned i = 0; i < 24; ++i) { watts.add(0); quality.add(2); }
        std::string packet;
        serializeJson(doc, packet);
        const char *result = pvForecast.receive(reinterpret_cast<const uint8_t *>(packet.data()),
            unsigned(packet.size()), utc, uint32_t(clockMs + part));
        const char *expected = part == 5 ? "accepted" : "pending";
        if (std::strcmp(result, expected)) {
            std::printf("FAIL real PV forecast fixture: expected %s, got %s\n", expected, result);
            return false;
        }
    }
    return pvForecast.valid(utc) && pvForecast.slots() == 144;
}

static bool hasField(JsonObjectConst object, const char *name) {
    for (JsonPairConst pair : object)
        if (!std::strcmp(pair.key().c_str(), name)) return true;
    return false;
}

static EconomicStatus readEconomicStatus() {
    EconomicStatus result;
    const String payload = buildPlanningStatePayload();
    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str())) {
        std::printf("FAIL independent recommendation: invalid JSON %s\n", payload.c_str());
        return result;
    }
    const char *strategy = doc["autoStrategy"] | "";
    const char *reason = doc["autoReason"] | "";
    const char *kind = doc["autoLimitKind"] | "";
    if (!strategy[0] || !reason[0] || !doc["autoLimitW"].is<int>() || std::strcmp(kind, "battery_ac")) {
        std::printf("FAIL independent recommendation: missing economic fields in %s\n", payload.c_str());
        return result;
    }
    result.valid = true;
    result.strategy = strategy;
    result.reason = reason;
    result.batteryLimitW = doc["autoLimitW"].as<int>();
    if (result.batteryLimitW != futurePlan.batteryCeilingW) {
        std::printf("FAIL independent recommendation: battery AC limit=%d W, economic plan=%d W\n",
                    result.batteryLimitW, futurePlan.batteryCeilingW);
        result.valid = false;
    }
    return result;
}

static int observedCommandW() {
    if (kRs485MockTransport) return lastMockCommandW;
    if (Serial2.frames.empty() || Serial2.frames.back().size() != 8) return -1;
    const auto &frame = Serial2.frames.back();
    return int((unsigned(frame[4]) << 8) | unsigned(frame[5]));
}

static bool checkExecutionDiagnostic(control::OperatingMode mode, const char *effective, bool manualOverride) {
    const String payload = buildPlanningStatePayload();
    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str())) {
        std::printf("FAIL execution diagnostics: invalid JSON %s\n", payload.c_str());
        return false;
    }
    const char *configured = doc["configuredMode"] | "";
    const char *actualEffective = doc["effectiveStrategy"] | "";
    const char *kind = doc["autoLimitKind"] | "";
    const int wire = observedCommandW();
    const bool passed = !std::strcmp(configured, control::operatingModeName(mode)) &&
        !std::strcmp(actualEffective, effective) && !std::strcmp(kind, "battery_ac") &&
        doc["economic"]["applied"].is<bool>() &&
        (!(manualOverride || !std::strcmp(effective, "bypass") || !std::strcmp(effective, "hold")) ||
         !doc["economic"]["applied"].as<bool>()) &&
        doc["manualOverride"].is<bool>() && doc["manualOverride"].as<bool>() == manualOverride &&
        doc["commandedW"].is<int>() && wire >= 0 && doc["commandedW"].as<int>() == wire;
    if (!passed) std::printf("FAIL %s execution diagnostics; expected effective=%s manual=%d actual wire/mock=%d: %s\n",
                             control::operatingModeName(mode), effective, manualOverride, wire, payload.c_str());
    return passed;
}

static bool noPlanReportsUnavailableRecommendation(control::OperatingMode mode) {
    resetFixture(mode);
    limiterSettings.enableController.value = true;
    processRS485Tick();
    const String payload = buildPlanningStatePayload();
    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str())) return false;
    const char *reason = doc["autoReason"] | "";
    const JsonObjectConst object = doc.as<JsonObjectConst>();
    const bool passed = hasField(object, "autoStrategy") && doc["autoStrategy"].isNull() &&
        hasField(object, "autoLimitW") && doc["autoLimitW"].isNull() && !std::strcmp(reason, "energy_missing") &&
        !std::strcmp(doc["autoLimitKind"] | "", "battery_ac") &&
        checkExecutionDiagnostic(mode, "hold", mode != control::OperatingMode::Automatic);
    if (!passed) std::printf("FAIL %s unavailable plan must report null automatic strategy/limit and a reason: %s\n",
                             control::operatingModeName(mode), payload.c_str());
    else std::printf("PASS %s unavailable plan reports null automatic strategy/limit (%s)\n",
                     control::operatingModeName(mode), reason);
    return passed;
}

static bool equalEconomicPlans(const control::DischargeBudget &expectedDischarge,
                               const control::ChargePlan &expectedCharge,
                               const char *modeName) {
    const auto &discharge = dischargeBudget.snapshot;
    const auto &charge = chargePlan.snapshot;
    if (!discharge.valid || !charge.valid || discharge.count != 144 || charge.count != 144) {
        std::printf("FAIL %s diagnostic plans: discharge valid=%d count=%u reason=%s; charge valid=%d count=%u reason=%s\n",
                    modeName, discharge.valid, discharge.count, discharge.reason,
                    charge.valid, charge.count, charge.reason);
        return false;
    }
    if (discharge.start != expectedDischarge.start || charge.start != expectedCharge.start ||
        discharge.priceRevision != expectedDischarge.priceRevision || charge.priceRevision != expectedCharge.priceRevision ||
        discharge.pvRevision != expectedDischarge.pvRevision || charge.pvRevision != expectedCharge.pvRevision) {
        std::printf("FAIL %s plan time grid or input revisions changed\n", modeName);
        return false;
    }
    for (unsigned slot = 0; slot < 144; ++slot) {
        if (discharge.watts[slot] != expectedDischarge.watts[slot] ||
            charge.watts[slot] != expectedCharge.watts[slot] ||
            charge.solarWatts[slot] != expectedCharge.solarWatts[slot]) {
            std::printf("FAIL %s economic plan differs at slot %u: discharge=%ld/%ld charge=%ld/%ld solar=%ld/%ld\n",
                        modeName, slot, long(discharge.watts[slot]), long(expectedDischarge.watts[slot]),
                        long(charge.watts[slot]), long(expectedCharge.watts[slot]),
                        long(charge.solarWatts[slot]), long(expectedCharge.solarWatts[slot]));
            return false;
        }
    }
    return true;
}

static bool modesKeepSamePlansAndIndependentRecommendation() {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    if (!loadQualifiedSchedule() || !loadQualifiedPvForecast()) return false;
    dischargeBudget.connection(true);
    chargePlan.connection(true);
    limiterSettings.enableController.value = true;

    control::DischargeBudget expectedDischarge;
    control::ChargePlan expectedCharge;
    EconomicStatus expectedRecommendation;
    bool baselineReady = false, manualCommandDiffers = false, passed = true;
    int automaticCommand = -1;
    unsigned sequence = 1;
    for (const auto mode : {control::OperatingMode::Automatic, control::OperatingMode::ChargePriority,
                            control::OperatingMode::PvFollow, control::OperatingMode::HouseholdFollow,
                            control::OperatingMode::Automatic}) {
        const char *name = control::operatingModeName(mode);
        control::ModeSettings settings;
        settings.mode = mode;
        settings.chargeThreshold = 95;
        settings.inverterSharePercent = 10;
        operatingModes.initialize(settings); // [MOCKED!] A confirmed mode switch, without NVS writes.
        for (unsigned sample = 0; sample < 3; ++sample) {
            sbtInputs.snapshot.bmsSequence = sbtInputs.snapshot.chargerSequence = sequence++;
            clockMs += 3000;
            if (sample == 2) {
                // Exercise the real capture path even when the periodic 60 s timer is not due.
                dischargeBudget.requestCapture(); chargePlan.requestCapture();
            }
            const uint32_t before = futurePrices.planningBuilds();
            processRS485Tick();
            const uint32_t builds = futurePrices.planningBuilds() - before;
            if (builds > 1) {
                std::printf("FAIL %s: %u full projections in one tick\n", name, unsigned(builds));
                passed = false;
            }
        }
        if (!futurePlan.valid || !dischargeBudget.snapshot.valid || !chargePlan.snapshot.valid ||
            dischargeBudget.snapshot.count != 144 || chargePlan.snapshot.count != 144) {
            std::printf("FAIL %s: economic=%d, discharge=%d/%u (%s), charge=%d/%u (%s)\n",
                        name, futurePlan.valid, dischargeBudget.snapshot.valid, dischargeBudget.snapshot.count,
                        dischargeBudget.snapshot.reason, chargePlan.snapshot.valid, chargePlan.snapshot.count,
                        chargePlan.snapshot.reason);
            passed = false;
        }
        const EconomicStatus recommendation = readEconomicStatus();
        if (!recommendation.valid) passed = false;
        const char *effective = mode == control::OperatingMode::Automatic ? "discharge" :
            (mode == control::OperatingMode::ChargePriority ? "hold" :
             (mode == control::OperatingMode::PvFollow ? "pv_follow" : "household_follow"));
        if (!checkExecutionDiagnostic(mode, effective, mode != control::OperatingMode::Automatic)) passed = false;
        if (!baselineReady) {
            if (mode == control::OperatingMode::Automatic && futurePlan.valid &&
                dischargeBudget.snapshot.valid && chargePlan.snapshot.valid) {
                expectedDischarge = dischargeBudget.snapshot;
                expectedCharge = chargePlan.snapshot;
                expectedRecommendation = recommendation;
                automaticCommand = inverterSetValue;
                baselineReady = true;
                if (recommendation.valid && (recommendation.strategy != "discharge" ||
                    recommendation.reason != "relative_release" || recommendation.batteryLimitW != 540)) {
                    std::printf("FAIL baseline economic recommendation: expected discharge/relative_release/540 W, got %s/%s/%d W\n",
                                recommendation.strategy.c_str(), recommendation.reason.c_str(), recommendation.batteryLimitW);
                    passed = false;
                }
            }
        } else {
            if (!equalEconomicPlans(expectedDischarge, expectedCharge, name)) passed = false;
            if (expectedRecommendation.valid && recommendation.valid &&
                (expectedRecommendation.strategy != recommendation.strategy ||
                 expectedRecommendation.reason != recommendation.reason ||
                 expectedRecommendation.batteryLimitW != recommendation.batteryLimitW)) {
                std::printf("FAIL %s: automatic economic recommendation changed across modes\n", name);
                passed = false;
            }
            if (mode != control::OperatingMode::Automatic && inverterSetValue != automaticCommand)
                manualCommandDiffers = true;
        }
    }
    if (!baselineReady) { std::puts("FAIL initial automatic 144-slot baseline unavailable"); passed = false; }
    if (!manualCommandDiffers) { std::puts("FAIL fixture did not exercise a distinct manual output"); passed = false; }
    if (operatingModes.confirmed().mode != control::OperatingMode::Automatic) {
        std::puts("FAIL final requested operating mode was not retained"); passed = false;
    }
    if (passed) std::puts("PASS automatic economic plans and recommendation survive four modes and return to automatic");
    return passed;
}

static bool protectionAndLimiterBypassAreDiagnosedSeparately() {
    resetFixture(control::OperatingMode::ChargePriority);
    provideHealthyInputs();
    if (!loadQualifiedSchedule() || !loadQualifiedPvForecast()) return false;
    dischargeBudget.connection(true); chargePlan.connection(true);
    limiterSettings.enableController.value = true;
    preparedSettings.inverterEnabled = false;
    processRS485Tick();
    bool passed = futurePlan.valid && inverterSetValue == 0 &&
        checkExecutionDiagnostic(control::OperatingMode::ChargePriority, "hold", true);
    if (!passed) std::printf("FAIL protected manual mode: economic plan=%d, command=%d, reason=%s\n",
                             futurePlan.valid, inverterSetValue, currentDecision.reason);
    preparedSettings.inverterEnabled = true;
    limiterSettings.enableController.value = false;
    clockMs += 1000;
    processRS485Tick();
    const bool bypass = futurePlan.valid && inverterSetValue == 900 &&
        checkExecutionDiagnostic(control::OperatingMode::ChargePriority, "bypass", true);
    if (!bypass) std::printf("FAIL manual setting with limiter OFF: economic plan=%d, command=%d, reason=%s\n",
                             futurePlan.valid, inverterSetValue, currentDecision.reason);
    if (passed && bypass) std::puts("PASS protected and bypass execution remain separate from the economic recommendation");
    return passed && bypass;
}

static bool legacyEconomicPriceHysteresisSurvivesManualReset() {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    if (!loadQualifiedSchedule()) return false;
    preparedSettings.relativeTimePolicy = false;
    sbtInputs.snapshot.soc = preparedSettings.reserveOffSocPercent;
    limiterSettings.enableController.value = true;
    telemetryAge.readings[control::TelemetryAge::Price] = {0.4f, true};
    processRS485Tick(); // Seed the expensive band above its entry threshold.
    telemetryAge.readings[control::TelemetryAge::Price] = {0.30f, true}; // Inside expensive hysteresis.
    control::ModeSettings settings;
    settings.mode = control::OperatingMode::ChargePriority;
    operatingModes.initialize(settings);
    ++sbtInputs.snapshot.bmsSequence; ++sbtInputs.snapshot.chargerSequence; clockMs += 1000;
    processRS485Tick();
    const bool firstValid = futurePlan.valid;
    const float firstCharge = futurePlan.chargeTargetW;
    ++sbtInputs.snapshot.bmsSequence; ++sbtInputs.snapshot.chargerSequence; clockMs += 1000;
    processRS485Tick();
    const bool passed = firstValid && futurePlan.valid && std::fabs(firstCharge - futurePlan.chargeTargetW) < 0.001f;
    if (!passed) std::printf("FAIL legacy hysteresis across manual reset: charge target %.1f W -> %.1f W, valid=%d/%d\n",
                              firstCharge, futurePlan.chargeTargetW, firstValid, futurePlan.valid);
    else std::printf("PASS legacy economic hysteresis survives manual reset: %.1f W\n", firstCharge);
    return passed;
}

static bool prepareModeChangeFixture() {
    resetFixture(control::OperatingMode::Automatic);
    testFixtureUtc = 1800000000;
    rs485TickDue = false;
    provideHealthyInputs();
    if (!loadQualifiedSchedule() || !loadQualifiedPvForecast()) return false;
    dischargeBudget.connection(true); chargePlan.connection(true);
    limiterSettings.enableController.value = true;
    refreshPreparedSettings(); // Extracted threshold/share assignments; other settings are unchanged in this fixture.
    for (unsigned i = 0; i < 3; ++i) {
        sbtInputs.snapshot.bmsSequence = sbtInputs.snapshot.chargerSequence = i + 1;
        clockMs = 1000 + i * 3000;
        processRS485Tick();
    }
    if (!futurePlan.valid || !dischargeBudget.snapshot.valid || !chargePlan.snapshot.valid ||
        dischargeBudget.retained() || chargePlan.retained()) {
        std::printf("FAIL mode-change fixture: baseline plan is not current and valid\n");
        return false;
    }
    return true;
}

static bool commitModeSettings(control::ModeSettings settings) {
    if (!operatingModes.request(settings)) return false;
    // [MOCKED!] NVS persistence succeeds; ModeTransaction and production branch are real.
    const bool changed = operatingModes.process([](const control::ModeSettings &) { return true; }, uint32_t(testFixtureUtc));
    applyConfirmedModeChange(changed);
    return changed && rs485TickDue;
}

static bool modeChangeKeepsCurrentPlan(bool sameUtcSecond) {
    if (!prepareModeChangeFixture()) return false;
    const auto oldRed = dischargeBudget.snapshot;
    const auto oldBlue = chargePlan.snapshot;
    control::ModeSettings next = operatingModes.confirmed();
    next.mode = control::OperatingMode::ChargePriority;
    if (!sameUtcSecond) ++testFixtureUtc;
    if (!commitModeSettings(next)) {
        std::puts("FAIL confirmed mode transaction did not reach the production branch");
        return false;
    }
    clockMs += 100;
    processRS485Tick();
    const bool current = !dischargeBudget.retained() && !chargePlan.retained() &&
        dischargeBudget.snapshot.valid && chargePlan.snapshot.valid;
    const bool generation = dischargeBudget.snapshot.generatedAt == oldRed.generatedAt &&
        chargePlan.snapshot.generatedAt == oldBlue.generatedAt;
    const bool sameEconomicContent = equalEconomicPlans(oldRed, oldBlue, "confirmed mode-only switch");
    const bool passed = current && generation && sameEconomicContent;
    if (!passed) std::printf("FAIL mode-only switch (%s): red retained=%d g=%lu/%lu, blue retained=%d g=%lu/%lu\n",
        sameUtcSecond ? "same UTC second" : "next UTC second", dischargeBudget.retained(),
        static_cast<unsigned long>(dischargeBudget.snapshot.generatedAt), static_cast<unsigned long>(oldRed.generatedAt),
        chargePlan.retained(), static_cast<unsigned long>(chargePlan.snapshot.generatedAt),
        static_cast<unsigned long>(oldBlue.generatedAt));
    else std::printf("PASS mode-only switch (%s) keeps both original current plans\n",
                     sameUtcSecond ? "same UTC second" : "next UTC second");
    return passed;
}

static bool economicModeSettingsTriggerNewCapture() {
    if (!prepareModeChangeFixture()) return false;
    uint32_t previousEpoch = planContextEpoch;
    control::ModeSettings next = operatingModes.confirmed();
    next.chargeThreshold = 85;
    ++testFixtureUtc;
    if (!commitModeSettings(next)) return false;
    clockMs += 100;
    uint32_t buildsBefore = futurePrices.planningBuilds();
    processRS485Tick();
    bool passed = dischargeBudget.snapshot.valid && chargePlan.snapshot.valid &&
        dischargeBudget.snapshot.generatedAt == uint32_t(testFixtureUtc) &&
        chargePlan.snapshot.generatedAt == uint32_t(testFixtureUtc) && planContextEpoch != previousEpoch &&
        futurePrices.planningBuilds() == buildsBefore + 1;
    if (!passed) std::puts("FAIL charge-threshold change did not refresh the economic plan/context");
    previousEpoch = planContextEpoch;
    rs485TickDue = false;
    next.inverterSharePercent = 20;
    ++testFixtureUtc;
    if (!commitModeSettings(next)) return false;
    clockMs += 100;
    buildsBefore = futurePrices.planningBuilds();
    processRS485Tick();
    const bool share = dischargeBudget.snapshot.valid && chargePlan.snapshot.valid &&
        dischargeBudget.snapshot.generatedAt == uint32_t(testFixtureUtc) &&
        chargePlan.snapshot.generatedAt == uint32_t(testFixtureUtc) && planContextEpoch != previousEpoch &&
        futurePrices.planningBuilds() == buildsBefore + 1;
    if (!share) std::puts("FAIL inverter-share change did not refresh the economic plan/context");
    if (passed && share) std::puts("PASS threshold/share changes refresh the economic plan and context");
    return passed && share;
}

int main() {
    testFixtureUtc = fakeTime(nullptr);
    std::printf("[MOCKED!] P1 independent planning profile=%s\n", kRs485MockTransport ? "planner_shadow" : "production_encoder");
    RS485begin();
    unsigned failures = 0;
    if (!modesKeepSamePlansAndIndependentRecommendation()) ++failures;
    if (!noPlanReportsUnavailableRecommendation(control::OperatingMode::Automatic)) ++failures;
    if (!noPlanReportsUnavailableRecommendation(control::OperatingMode::ChargePriority)) ++failures;
    if (!protectionAndLimiterBypassAreDiagnosedSeparately()) ++failures;
    if (!legacyEconomicPriceHysteresisSurvivesManualReset()) ++failures;
    if (!modeChangeKeepsCurrentPlan(true)) ++failures;
    if (!modeChangeKeepsCurrentPlan(false)) ++failures;
    if (!economicModeSettingsTriggerNewCapture()) ++failures;
    return failures ? 1 : 0;
}
