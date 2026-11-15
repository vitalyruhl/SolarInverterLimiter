#include "RS485Module/RS485Module.h"
#include "control/ChargePlan.h"
#include "control/NumericSafety.h"
#include "control/OperatingModes.h"
#include "control/OutputCorrection.h"
#include "control/PlanCaptureContext.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>

using std::max;
using std::min;

template <class T> static T constrain(T value, T lower, T upper) {
    return std::max(lower, std::min(value, upper));
}

// [MOCKED!] Only the Arduino clock, received telemetry, settings, logging,
// and hardware serial/GPIO boundaries are supplied by the host fixture.
static unsigned long clockMs = 1000;
static unsigned long millis() { return clockMs; }
static std::time_t fakeTime(std::time_t *out) {
    const std::time_t now = 1800000000;
    if (out) *out = now;
    return now;
}

template <class T> struct Setting {
    T value;
    T get() const { return value; }
};

static struct {
    Setting<int> minOutput{0}, maxOutput{900}, inputCorrectionOffset{-100}, sbtTimeout{180};
    Setting<float> RS232PublishPeriod{1}, pidKp{0.45f}, pidKi{0.05f}, pidKd{0.02f};
    Setting<bool> enableController{false}, forceMinOnNegativePrice{false}, setZeroOnNegativePrice{true};
} limiterSettings;

namespace control {
// [MOCKED!] A received-value boundary; no MQTT broker or network is opened.
class TelemetryAge {
public:
    enum Input { Grid, Inverter, Price, Count };
    struct Reading { float value = 0; bool valid = false; };
    Reading readings[Count];
    bool read(Input input, uint32_t, uint32_t, float &value) const {
        value = readings[input].value;
        return readings[input].valid;
    }
};
} // namespace control

struct SourceSnapshot {
    bool bmsValid = false, chargerValid = false, chargerStandby = false;
    bool bmsWithinGrace = false, sourcesWithinGrace = false, alarmRestricted = false;
    bool genuineBmsFault = false, chargerFaultKnown = false;
    bool batteryPathAvailable = false, batteryChargeAvailable = false, batteryChargeConfirmed = false;
    bool softwareMosLockOnly = false, chargerFault = false, batteryChargeStateKnown = false;
    bool batteryDischargeIntentAvailable = false;
    float soc = 0, batteryW = 0, chargerW = 0, chargerVoltageV = 0, batteryVoltageV = 0;
    float fullCapacityAh = NAN;
    uint32_t bmsSequence = 0, chargerSequence = 0;
};
static struct {
    SourceSnapshot snapshot;
    SourceSnapshot controlSnapshot(uint32_t) const { return snapshot; }
} sbtInputs;

struct PidControllerState {
    bool initialized = false;
    float output = 0, integral = 0, previousError = 0;
    unsigned long lastUpdateMs = 0;
};
static PidControllerState pidController;
enum class LL { Info, Trace };
static struct {
    unsigned mockSends = 0;
    template <class... T> void logTag(LL, const char *tag, const char *, T...) {
        if (!std::strcmp(tag, "MOCK")) ++mockSends;
    }
} lmg;

RS485_Settings rs485settings;
static control::PreparedSettings preparedSettings = control::applicationDefaultSettings();
static control::CurrentControl currentControl;
static control::PriceBandState plannerPriceBand;
static control::ModeTransaction operatingModes;
static control::OperatingModePolicy modePolicy;
static control::ModeDecision modeDecision;
static control::CurrentDecision currentDecision;
static control::TelemetryAge telemetryAge;
static control::FuturePrices futurePrices;
static control::PvForecast pvForecast;
static control::FuturePlan futurePlan;
static control::DischargeBudgetPublication dischargeBudget;
static control::ChargePlanPublication chargePlan;
static control::PlanCaptureContext planCaptureContext;
static control::PlanCaptureFaults planCaptureFaults;
static bool planCaptureContextKnown = false;
static uint32_t planContextEpoch = 0;
static float forecastPriceFactor = 0.93f;
static bool currentDecisionAvailable = false, modePublishDirty = false;
static int lastIssuedCommandW = 0, lastIssuedBaseCommandW = 0;
static int lastMockCommandW = 0, lastMockBaseCommandW = 0;
static bool lastIssuedCommandKnown = false;
static const char *lastTransportWriteStatus = "unknown";
static int inverterSetValue = 0, inverterCalculatedValue = 0;
static bool negativePriceActive = false;
static float electricityPriceEurKwh = 0;
static int lastNegativePriceOverrideTarget = -1;

// Both functions and the entire processRS485Tick body are extracted unchanged
// from src/main.cpp by run-limiter-disabled-tests.ps1, not reimplemented here.
#define time fakeTime
#include "limiter_disabled_production.h"
#undef time

static void resetFixture(control::OperatingMode mode) {
    clockMs = 1000;
    limiterSettings.enableController.value = false;
    limiterSettings.maxOutput.value = 900;
    limiterSettings.minOutput.value = 0;
    limiterSettings.inputCorrectionOffset.value = -100;
    preparedSettings = control::applicationDefaultSettings();
    preparedSettings.valid = true;
    preparedSettings.inverterEnabled = true;
    currentControl = control::CurrentControl{};
    plannerPriceBand = control::PriceBandState{};
    modePolicy = control::OperatingModePolicy{};
    modeDecision = control::ModeDecision{};
    currentDecision = control::CurrentDecision{};
    currentDecisionAvailable = modePublishDirty = false;
    control::ModeSettings settings;
    settings.mode = mode;
    settings.chargeThreshold = 95;
    settings.inverterSharePercent = 10;
    operatingModes.initialize(settings);
    telemetryAge = control::TelemetryAge{};
    sbtInputs.snapshot = SourceSnapshot{};
    futurePrices.clear();
    pvForecast.clear();
    dischargeBudget = control::DischargeBudgetPublication{};
    chargePlan = control::ChargePlanPublication{};
    planCaptureContextKnown = false;
    planCaptureFaults = control::PlanCaptureFaults{};
    planContextEpoch = 0;
    negativePriceActive = false;
    lastNegativePriceOverrideTarget = -1;
    lastTransportWriteStatus = "unknown";
    lastIssuedBaseCommandW = lastIssuedCommandW = 0;
    lastMockBaseCommandW = lastMockCommandW = 0;
    lastIssuedCommandKnown = false;
    lmg.mockSends = 0;
    resetPidController();
    rs485settings.enableRS485.set(true);
    Serial2.writeLimit = std::numeric_limits<size_t>::max();
    Serial2.frames.clear();
}

static void provideHealthyInputs() {
    telemetryAge.readings[control::TelemetryAge::Grid] = {400, true};
    telemetryAge.readings[control::TelemetryAge::Inverter] = {200, true};
    telemetryAge.readings[control::TelemetryAge::Price] = {0.4f, true};
    auto &source = sbtInputs.snapshot;
    source.bmsValid = source.chargerValid = true;
    source.sourcesWithinGrace = source.bmsWithinGrace = true;
    source.batteryDischargeIntentAvailable = source.batteryPathAvailable = true;
    source.batteryChargeAvailable = source.batteryChargeConfirmed = source.batteryChargeStateKnown = true;
    source.chargerFaultKnown = true;
    source.soc = 50;
    source.fullCapacityAh = 100;
    source.batteryVoltageV = source.chargerVoltageV = 52;
    source.chargerW = 400;
    source.bmsSequence = source.chargerSequence = 1;
}

static bool expectPower(const char *name, unsigned expected, unsigned ticks = 2) {
    bool passed = true;
    for (unsigned tick = 0; tick < ticks; ++tick) {
        const size_t before = Serial2.frames.size();
        const unsigned beforeMockSends = lmg.mockSends;
        processRS485Tick();
        clockMs += 1000;
        if (kRs485MockTransport) {
            if (Serial2.frames.size() != before || !Serial.frames.empty() || Serial.beginCalls || Serial2.beginCalls ||
                !pinModes.empty() || !pinWrites.empty() || lmg.mockSends != beforeMockSends + 1) {
                std::printf("FAIL %s tick %u: expected one mock command and no serial frame\n", name, tick + 1);
                passed = false;
            } else if (lastMockCommandW != int(expected)) {
                std::printf("FAIL %s tick %u: expected mock %u W, received %d W (reason=%s)\n",
                            name, tick + 1, expected, lastMockCommandW, currentDecision.reason);
                passed = false;
            }
            continue;
        }
        if (Serial2.frames.size() != before + 1 || Serial2.frames.back().size() != 8) {
            std::printf("FAIL %s tick %u: expected one RS485 frame, received %zu\n",
                        name, tick + 1, Serial2.frames.size() - before);
            passed = false;
            continue;
        }
        const auto &frame = Serial2.frames.back();
        const unsigned actual = (unsigned(frame[4]) << 8) | unsigned(frame[5]);
        if (actual != expected) {
            std::printf("FAIL %s tick %u: expected %u W, received %u W (reason=%s)\n",
                        name, tick + 1, expected, actual, currentDecision.reason);
            passed = false;
        }
    }
    if (passed) std::printf("PASS %s: %u W\n", name, expected);
    return passed;
}

static bool expectDisabledTransport() {
    if (kRs485MockTransport) {
        std::puts("SKIP disabled physical transport: the shadow profile always uses the hard mock");
        return true;
    }
    const size_t before = Serial2.frames.size();
    rs485settings.enableRS485.set(false);
    processRS485Tick();
    const bool passed = Serial2.frames.size() == before &&
        std::strcmp(lastTransportWriteStatus, "disabled") == 0;
    std::printf("%s disabled transport: no write, local status=%s\n", passed ? "PASS" : "FAIL", lastTransportWriteStatus);
    return passed;
}

static bool expectShortWrite() {
    if (kRs485MockTransport) {
        std::puts("SKIP short physical write: the shadow profile never opens the transport");
        return true;
    }
    const size_t before = Serial2.frames.size();
    Serial2.writeLimit = 3;
    processRS485Tick();
    const bool passed = Serial2.frames.size() == before + 1 && Serial2.frames.back().size() == 3 &&
        std::strcmp(lastTransportWriteStatus, "write_failed") == 0 && !pinWrites.empty() && pinWrites.back().value == LOW;
    std::printf("%s short local write: status=%s, receive direction restored\n", passed ? "PASS" : "FAIL", lastTransportWriteStatus);
    return passed;
}

// [MOCKED!] Packet input is synthetic; the real chunk receiver and planner run.
static bool loadQualifiedSchedule() {
    constexpr uint32_t utc = 1800000000u; // Exactly aligned to a 900-second slot.
    for (unsigned part = 0; part < 6; ++part) {
        JsonDocument doc;
        doc["v"] = 2; doc["r"] = utc; doc["t"] = utc;
        doc["d"] = 900; doc["n"] = 144; doc["k"] = part;
        JsonArray prices = doc["p"].to<JsonArray>();
        JsonArray origins = doc["o"].to<JsonArray>();
        for (unsigned i = 0; i < 24; ++i) {
            prices.add(part == 0 && i == 0 ? 400000 : 100000);
            origins.add(0);
        }
        std::string packet;
        serializeJson(doc, packet);
        const char *result = futurePrices.receive(reinterpret_cast<const uint8_t *>(packet.data()),
            unsigned(packet.size()), utc, uint32_t(clockMs + part), true);
        const char *expected = part == 5 ? "accepted" : "pending";
        if (std::strcmp(result, expected)) {
            std::printf("FAIL real price schedule fixture: expected %s, got %s\n", expected, result);
            return false;
        }
    }
    return true;
}

static bool expectPlanningPayload(const char *label, const char *reason, const char *transport, int limit) {
    const String payload = buildPlanningStatePayload();
    JsonDocument doc;
    if (deserializeJson(doc, payload.c_str())) {
        std::printf("FAIL %s: invalid planning state JSON: %s\n", label, payload.c_str());
        return false;
    }
    const char *actualReason = doc["reason"] | "missing";
    const char *actualTransport = doc["transportWriteStatus"] | "missing";
    const bool passed = !std::strcmp(actualReason, reason) && !std::strcmp(actualTransport, transport) &&
        doc["limitW"].is<int>() && doc["limitW"].as<int>() == limit && !doc["confirmed"].is<bool>();
    if (!passed) std::printf("FAIL %s: unexpected planning state %s\n", label, payload.c_str());
    else std::printf("PASS %s: reason=%s, transport=%s, limit=%d W\n", label, reason, transport, limit);
    return passed;
}

static bool maximumBoundary(int maximum, unsigned expected, const char *reason) {
    resetFixture(control::OperatingMode::Automatic);
    limiterSettings.maxOutput.value = maximum;
    char label[80];
    std::snprintf(label, sizeof(label), "configured MAX=%d", maximum);
    const bool power = expectPower(label, expected, 1);
    const bool diagnostic = expectPlanningPayload(label, reason, kRs485MockTransport ? "mock" : "accepted", int(expected));
    return power && diagnostic;
}

static bool quietStableModeStatus(control::OperatingMode mode) {
    resetFixture(mode);
    processRS485Tick();
    modePublishDirty = false; // [MOCKED!] The previous status pair has been delivered.
    clockMs += 1000;
    processRS485Tick();
    const bool passed = !modePublishDirty && !std::strcmp(currentDecision.reason, "limiter_disabled_max");
    std::printf("%s %s stable OFF status: dirty=%d, reason=%s\n", passed ? "PASS" : "FAIL",
                control::operatingModeName(mode), modePublishDirty, currentDecision.reason);
    return passed;
}

static bool confirmedChargerFaultSurvivesMissingTelemetry() {
    resetFixture(control::OperatingMode::HouseholdFollow);
    provideHealthyInputs();
    sbtInputs.snapshot.chargerFault = sbtInputs.snapshot.chargerFaultKnown = true;
    bool passed = expectPower("confirmed charger fault before outage", 0, 1);
    // The real SBT snapshot reports chargerFaultKnown=false and chargerFault=false
    // when the last error-code value becomes unavailable; no clearing sample arrived.
    telemetryAge = control::TelemetryAge{};
    sbtInputs.snapshot = SourceSnapshot{};
    if (!expectPower("confirmed charger fault after telemetry loss", 0, 1)) passed = false;
    provideHealthyInputs(); // A new, qualified error-code value explicitly clears the prior fault.
    if (!expectPower("charger fault cleared by fresh healthy sample", 900, 1)) passed = false;
    return passed;
}

static bool modeStatusTransitions() {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    bool passed = expectPower("status transition: initial OFF", 900, 1);
    modePublishDirty = false; // [MOCKED!] Previous mode status was sent.
    sbtInputs.snapshot.chargerFault = true;
    if (!expectPower("status transition: charger fault", 0, 1)) passed = false;
    if (!modePublishDirty || std::strcmp(currentDecision.reason, "charger_fault")) {
        std::printf("FAIL charger fault must mark changed status dirty (dirty=%d, reason=%s)\n",
                    modePublishDirty, currentDecision.reason);
        passed = false;
    }
    modePublishDirty = false;
    if (!expectPower("status transition: unchanged fault", 0, 1)) passed = false;
    if (modePublishDirty) { std::puts("FAIL unchanged charger fault marked status dirty"); passed = false; }
    sbtInputs.snapshot.chargerFault = false; // chargerFaultKnown is still true.
    if (!expectPower("status transition: cleared fault", 900, 1)) passed = false;
    if (!modePublishDirty || std::strcmp(currentDecision.reason, "limiter_disabled_max")) {
        std::printf("FAIL fault clearance must mark changed status dirty (dirty=%d, reason=%s)\n",
                    modePublishDirty, currentDecision.reason);
        passed = false;
    }
    if (passed) std::puts("PASS mode status transitions are emitted once per actual change");
    return passed;
}

static bool faultReasonBeatsMissingPrice(bool charger) {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs(); // The price schedule itself remains absent.
    const char *expected = charger ? "charger_fault" : "source_alarm";
    if (charger) sbtInputs.snapshot.chargerFault = sbtInputs.snapshot.chargerFaultKnown = true;
    else sbtInputs.snapshot.alarmRestricted = sbtInputs.snapshot.genuineBmsFault = true;
    const bool stopped = expectPower(charger ? "charger fault plus absent schedule" : "BMS fault plus absent schedule", 0, 1);
    const bool visible = currentDecisionAvailable && !std::strcmp(currentDecision.reason, expected);
    if (!visible) std::printf("FAIL known %s diagnosis: expected %s, got %s\n",
                              charger ? "charger fault" : "BMS fault", expected, currentDecision.reason);
    return stopped && visible;
}

static bool transportDiagnosticRecovery() {
    if (kRs485MockTransport) {
        std::puts("SKIP physical write recovery: the shadow profile never opens the transport");
        return true;
    }
    resetFixture(control::OperatingMode::Automatic);
    Serial2.writeLimit = 3;
    processRS485Tick();
    bool passed = expectPlanningPayload("short write diagnostic", "limiter_disabled_max", "write_failed", 900);
    Serial2.writeLimit = std::numeric_limits<size_t>::max();
    if (!expectPower("local transport recovery", 900, 1)) passed = false;
    if (!expectPlanningPayload("recovered write diagnostic", "limiter_disabled_max", "accepted", 900)) passed = false;
    return passed;
}

static bool modeAfterReconnectSimulation() {
    resetFixture(control::OperatingMode::ChargePriority);
    if (!expectPower("before reconnect simulation", 900, 1)) return false;
    dischargeBudget.connection(false);
    chargePlan.connection(false);
    modePublishDirty = true; // [MOCKED!] The production disconnect callback requests a status replay.
    dischargeBudget.connection(true);
    chargePlan.connection(true);
    const bool output = expectPower("after reconnect simulation", 900, 1);
    const bool unchanged = operatingModes.confirmed().mode == control::OperatingMode::ChargePriority &&
        modePublishDirty && !std::strcmp(currentDecision.reason, "limiter_disabled_max");
    if (!unchanged) std::printf("FAIL reconnect simulation: mode or status changed unexpectedly\n");
    return output && unchanged;
}

static bool economicPlanKeepsItsValuesAcrossLimiterChange() {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    if (!loadQualifiedSchedule()) return false;
    dischargeBudget.connection(true);
    chargePlan.connection(true);
    limiterSettings.enableController.value = true;
    for (unsigned i = 0; i < 3; ++i) {
        sbtInputs.snapshot.bmsSequence = sbtInputs.snapshot.chargerSequence = i + 1;
        clockMs = 1000 + i * 3000;
        processRS485Tick();
    }
    const control::FuturePlan before = futurePlan;
    const uint32_t revision = futurePrices.revision();
    limiterSettings.enableController.value = false;
    clockMs += 1000;
    const bool output = expectPower("valid plan after OFF transition", 900, 1);
    const bool unchanged = before.valid && futurePlan.valid && futurePrices.revision() == revision &&
        before.batteryCeilingW == futurePlan.batteryCeilingW && before.directPvCeilingW == futurePlan.directPvCeilingW &&
        std::fabs(before.chargeTargetW - futurePlan.chargeTargetW) < 0.001f &&
        !std::strcmp(before.reason, futurePlan.reason);
    if (!unchanged) std::printf("FAIL economic plan changed across OFF transition: valid=%d/%d, battery=%d/%d, PV=%d/%d, reason=%s/%s\n",
        before.valid, futurePlan.valid, before.batteryCeilingW, futurePlan.batteryCeilingW,
        before.directPvCeilingW, futurePlan.directPvCeilingW, before.reason, futurePlan.reason);
    else std::puts("PASS economic plan remains valid with identical scalar values across OFF transition");
    return output && unchanged;
}

static bool onOffOnRecovery() {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    if (!loadQualifiedSchedule()) return false;
    dischargeBudget.connection(true);
    chargePlan.connection(true);
    limiterSettings.enableController.value = true;
    bool passed = true;
    // Establish the existing 5 s/two-new-sample qualification before the outage.
    for (unsigned i = 0; i < 3; ++i) {
        sbtInputs.snapshot.bmsSequence = sbtInputs.snapshot.chargerSequence = i + 1;
        clockMs = 1000 + i * 3000;
        processRS485Tick();
    }
    if (!futurePlan.valid || inverterSetValue <= 0 || chargePlan.snapshot.generatedAt != 1800000000u) {
        std::printf("FAIL ON/OFF/ON fixture: prior ON qualification failed (plan=%d, output=%d, reason=%s)\n",
                    futurePlan.valid, inverterSetValue, currentDecision.reason);
        return false;
    }
    std::printf("PASS ON/OFF/ON: initial ON qualified, output=%d W\n", inverterSetValue);
    limiterSettings.enableController.value = false;
    telemetryAge = control::TelemetryAge{};
    sbtInputs.snapshot = SourceSnapshot{};
    clockMs = 8000;
    if (!expectPower("ON/OFF/ON: OFF with lost inputs", 900, 1)) passed = false;
    limiterSettings.enableController.value = true;
    if (!expectPower("ON/OFF/ON: ON still missing inputs", 0, 1)) passed = false;
    provideHealthyInputs();
    for (unsigned i = 0; i < 3; ++i) {
        sbtInputs.snapshot.bmsSequence = sbtInputs.snapshot.chargerSequence = i + 10;
        clockMs = 10000 + i * 3000;
        processRS485Tick();
        if (i < 2 && inverterSetValue != 0) {
            std::printf("FAIL ON/OFF/ON: premature restart at sample %u (%d W)\n", i + 1, inverterSetValue);
            passed = false;
        }
    }
    if (inverterSetValue <= 0) {
        std::printf("FAIL ON/OFF/ON: did not resume after qualified recovery (reason=%s)\n", currentDecision.reason);
        passed = false;
    } else std::printf("PASS ON/OFF/ON: recovered after 5 s/two new samples, output=%d W\n", inverterSetValue);
    if (passed) std::puts("PASS ON/OFF/ON: bounded output and qualified recovery");
    return passed;
}

int main() {
    std::printf("[MOCKED!] profile=%s\n", kRs485MockTransport ? "planner_shadow" : "production_encoder");
    RS485begin();
    unsigned failures = 0;

    for (const auto mode : {control::OperatingMode::Automatic, control::OperatingMode::HouseholdFollow,
                            control::OperatingMode::ChargePriority, control::OperatingMode::PvFollow}) {
        const char *name = control::operatingModeName(mode);
        char label[128];
        resetFixture(mode);
        std::snprintf(label, sizeof(label), "%s, missing meter/price/SOC", name);
        if (!expectPower(label, 900)) ++failures;
        limiterSettings.maxOutput.value = 800;
        std::snprintf(label, sizeof(label), "%s, changed MAX=800", name);
        if (!expectPower(label, 800)) ++failures;
        limiterSettings.minOutput.value = 850;
        std::snprintf(label, sizeof(label), "%s, MAX=800 with MIN=850", name);
        if (!expectPower(label, 800)) ++failures;

        resetFixture(mode);
        provideHealthyInputs();
        if (!loadQualifiedSchedule()) { ++failures; continue; }
        dischargeBudget.connection(true);
        chargePlan.connection(true);
        std::snprintf(label, sizeof(label), "%s, fresh meter/SOC and valid schedule", name);
        if (!expectPower(label, 900)) ++failures;

        resetFixture(mode);
        provideHealthyInputs();
        if (!loadQualifiedSchedule()) { ++failures; continue; }
        negativePriceActive = true;
        telemetryAge.readings[control::TelemetryAge::Price] = {-0.05f, true};
        std::snprintf(label, sizeof(label), "%s, negative price with valid schedule", name);
        if (!expectPower(label, 900)) ++failures;
    }

    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    sbtInputs.snapshot.bmsValid = false;
    if (!expectPower("OFF with missing SOC but fresh meters", 900)) ++failures;

    resetFixture(control::OperatingMode::ChargePriority);
    provideHealthyInputs();
    preparedSettings.inverterEnabled = false;
    if (!expectPower("explicit inverter stop", 0)) ++failures;

    resetFixture(control::OperatingMode::ChargePriority);
    provideHealthyInputs();
    if (!expectDisabledTransport()) ++failures;

    resetFixture(control::OperatingMode::HouseholdFollow);
    provideHealthyInputs();
    sbtInputs.snapshot.alarmRestricted = sbtInputs.snapshot.genuineBmsFault = true;
    if (!expectPower("confirmed BMS discharge fault", 0)) ++failures;

    resetFixture(control::OperatingMode::HouseholdFollow);
    provideHealthyInputs();
    sbtInputs.snapshot.chargerFault = true;
    if (!expectPower("confirmed charger fault", 0)) ++failures;

    resetFixture(control::OperatingMode::HouseholdFollow);
    provideHealthyInputs();
    sbtInputs.snapshot.alarmRestricted = sbtInputs.snapshot.softwareMosLockOnly = true;
    if (!expectPower("software MOS lock only", 900)) ++failures;

    resetFixture(control::OperatingMode::Automatic);
    if (!expectShortWrite()) ++failures;
    if (!onOffOnRecovery()) ++failures;
    if (!maximumBoundary(0, 0, "limiter_disabled_max")) ++failures;
    if (!maximumBoundary(65535, 65535, "limiter_disabled_max")) ++failures;
    if (!maximumBoundary(65536, 0, "limiter_max_invalid")) ++failures;
    if (!maximumBoundary(-1, 0, "limiter_max_invalid")) ++failures;
    if (!quietStableModeStatus(control::OperatingMode::Automatic)) ++failures;
    if (!quietStableModeStatus(control::OperatingMode::HouseholdFollow)) ++failures;
    if (!confirmedChargerFaultSurvivesMissingTelemetry()) ++failures;
    if (!modeStatusTransitions()) ++failures;
    if (!faultReasonBeatsMissingPrice(false)) ++failures;
    if (!faultReasonBeatsMissingPrice(true)) ++failures;
    if (!transportDiagnosticRecovery()) ++failures;
    if (!modeAfterReconnectSimulation()) ++failures;
    if (!economicPlanKeepsItsValuesAcrossLimiterChange()) ++failures;

    return failures ? 1 : 0;
}
