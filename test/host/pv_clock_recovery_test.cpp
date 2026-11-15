#include <ctime>

static std::time_t testFixtureUtc = 1800003600;
static std::time_t testFixtureTime(std::time_t *out) {
    if (out) *out = testFixtureUtc;
    return testFixtureUtc;
}

// Reuse only the host boundaries and the extracted, unmodified production tick.
#define main p0aApplicationHarnessMain
#include "limiter_disabled_application_test.cpp"
#undef main

namespace {
constexpr uint32_t correctedUtc = 1800000000u;
constexpr uint32_t oldUtc = correctedUtc + 3600u;
// A producer session can predate both readings and remains unchanged across polls.
constexpr uint32_t session = correctedUtc - 86400u;
constexpr unsigned slots = 144;

std::string pricePacket(uint32_t utc, unsigned part) {
    JsonDocument doc;
    doc["v"] = 2; doc["r"] = utc; doc["t"] = utc - utc % 900; doc["d"] = 900;
    doc["n"] = slots; doc["k"] = part;
    JsonArray prices = doc["p"].to<JsonArray>();
    JsonArray origins = doc["o"].to<JsonArray>();
    for (unsigned i = part * 24; i < (part + 1) * 24; ++i) {
        prices.add(i >= 10 && i < 20 ? 600000 : 100000);
        origins.add(1);
    }
    std::string packet;
    serializeJson(doc, packet);
    return packet;
}

std::string pvPacket(uint32_t utc, unsigned part, unsigned power) {
    JsonDocument doc;
    const uint32_t start = utc - utc % 900;
    doc["v"] = 2; doc["s"] = session; doc["r"] = utc; doc["g"] = utc;
    doc["t"] = start; doc["d"] = 900; doc["n"] = slots; doc["k"] = part;
    doc["e"] = start + slots * 900; doc["f"] = 1000; doc["c"] = 0; doc["a"] = 0;
    doc["q"] = "uncalibrated"; doc["o"] = "forecast_solar";
    JsonArray values = doc["p"].to<JsonArray>();
    JsonArray quality = doc["x"].to<JsonArray>();
    for (unsigned i = 0; i < 24; ++i) { values.add(power); quality.add(2); }
    std::string packet;
    serializeJson(doc, packet);
    return packet;
}

const char *sendPrice(control::FuturePrices &receiver, const std::string &packet, uint32_t utc) {
    return receiver.receive(reinterpret_cast<const uint8_t *>(packet.data()),
                            unsigned(packet.size()), utc, uint32_t(clockMs), true);
}

const char *sendPv(control::PvForecast &receiver, const std::string &packet, uint32_t utc) {
    return receiver.receive(reinterpret_cast<const uint8_t *>(packet.data()),
                            unsigned(packet.size()), utc, uint32_t(clockMs));
}

bool loadPricesTo(control::FuturePrices &receiver, uint32_t utc) {
    for (unsigned part = 0; part < 6; ++part) {
        const std::string packet = pricePacket(utc, part);
        const char *result = sendPrice(receiver, packet, utc);
        const char *expected = part == 5 ? "accepted" : "pending";
        if (std::strcmp(result, expected)) {
            std::printf("FAIL price fixture part %u: expected %s, got %s\n", part, expected, result);
            return false;
        }
    }
    return true;
}

bool loadPrices(uint32_t utc) { return loadPricesTo(futurePrices, utc); }

bool loadPvAt(control::PvForecast &receiver, uint32_t generation, uint32_t utc, unsigned power, const char *label) {
    for (unsigned part = 0; part < 6; ++part) {
        const std::string packet = pvPacket(generation, part, power);
        const char *result = sendPv(receiver, packet, utc);
        const char *expected = part == 5 ? "accepted" : "pending";
        if (std::strcmp(result, expected)) {
            std::printf("FAIL %s part %u: expected %s, got %s\n", label, part, expected, result);
            return false;
        }
    }
    return true;
}

bool loadPv(control::PvForecast &receiver, uint32_t utc, unsigned power, const char *label) {
    return loadPvAt(receiver, utc, utc, power, label);
}

void tick() {
    ++sbtInputs.snapshot.bmsSequence;
    ++sbtInputs.snapshot.chargerSequence;
    clockMs += 3000;
    processRS485Tick();
}

void setup(bool legacy) {
    resetFixture(control::OperatingMode::Automatic);
    provideHealthyInputs();
    preparedSettings.relativeTimePolicy = !legacy;
    limiterSettings.enableController.value = true;
    telemetryAge.readings[control::TelemetryAge::Price] = {0.1f, true};
    sbtInputs.snapshot.soc = preparedSettings.reserveOffSocPercent;
    sbtInputs.snapshot.chargerW = 0;
    dischargeBudget.connection(true);
    chargePlan.connection(true);
}

bool clockRecovery(bool legacy) {
    setup(legacy);
    testFixtureUtc = oldUtc;
    if (!loadPrices(oldUtc) || !loadPv(pvForecast, oldUtc, 1200, "initial PV")) return false;
    for (unsigned i = 0; i < 3; ++i) tick();
    if (!futurePlan.valid || !futurePlan.pvQualified || !pvForecast.valid(oldUtc)) {
        std::printf("FAIL initial fixture: plan=%d pvQualified=%d reason=%s\n",
                    futurePlan.valid, futurePlan.pvQualified, futurePlan.reason);
        return false;
    }
    std::printf("PASS initial producer packets: valid 144-slot price and PV, revision=%lu\n",
                static_cast<unsigned long>(pvForecast.revision()));

    testFixtureUtc = correctedUtc;
    tick();
    if (futurePlan.valid || std::strcmp(futurePlan.reason, "clock_invalid") || futurePrices.slots() != 0) {
        std::printf("FAIL rollback fixture: plan=%d reason=%s priceSlots=%u\n",
                    futurePlan.valid, futurePlan.reason, futurePrices.slots());
        return false;
    }
    const std::string oldPacket = pvPacket(oldUtc, 0, 1200);
    const char *replay = sendPv(pvForecast, oldPacket, correctedUtc);
    if (std::strcmp(replay, "packet_range")) {
        std::printf("FAIL future-dated packet contract: expected packet_range, got %s\n", replay);
        return false;
    }
    std::printf("PASS UTC rollback: price schedule cleared; replay of future-dated PV packet rejected (%s)\n", replay);

    if (!loadPrices(correctedUtc)) return false;
    for (unsigned i = 0; i < 3; ++i) tick();
    if (!futurePlan.valid) {
        std::printf("FAIL fresh-price fixture: plan invalid (%s)\n", futurePlan.reason);
        return false;
    }
    const int32_t projectedSolar = futurePrices.projectedSolarW(4);
    const bool projectedFuturePv = projectedSolar >= 0;
    std::printf("OBSERVE corrected UTC: pvValid=%d pvQualified=%d planValid=%d slot4Solar=%ld W offered=%.1f Wh usable=%.1f Wh chargePlanValid=%d chargeRetained=%d chargeSlot4Solar=%ld W\n",
                pvForecast.valid(correctedUtc), futurePlan.pvQualified, futurePlan.valid,
                static_cast<long>(projectedSolar), double(futurePlan.offeredPvWh), double(futurePlan.usablePvWh),
                chargePlan.snapshot.valid, chargePlan.retained(), static_cast<long>(chargePlan.snapshot.solarWatts[4]));
    if (projectedFuturePv)
        std::printf("FAIL after UTC rollback: future-dated PV must be unknown (-1 W), got %ld W\n",
                    static_cast<long>(projectedSolar));
    else
        std::printf("PASS after UTC rollback: future-dated PV is not projected\n");

    // Same producer session, new UTC generation; arbitrary older packets are not accepted
    // unless the actual clock-recovery path has first invalidated its previous PV epoch.
    const bool replacementAccepted = loadPv(pvForecast, correctedUtc, 600, "corrected PV recovery");
    if (replacementAccepted) {
        tick();
        if (!futurePlan.valid || !futurePlan.pvQualified || futurePrices.projectedSolarW(4) != 600) {
            std::printf("FAIL corrected PV activation: valid=%d qualified=%d slot4=%ld W\n",
                        futurePlan.valid, futurePlan.pvQualified, static_cast<long>(futurePrices.projectedSolarW(4)));
            return false;
        }
        std::printf("PASS corrected PV epoch accepted and projected at 600 W\n");
    }

    // Independent contract control: a new receiver accepts the same corrected
    // revision, while no receiver has to relax its old-revision replay check.
    control::PvForecast fresh;
    const bool correctedPacketValid = loadPv(fresh, correctedUtc, 600, "fresh receiver control") && fresh.valid(correctedUtc);
    std::printf("%s corrected packet/session accepted by fresh receiver\n", correctedPacketValid ? "PASS" : "FAIL");
    return !projectedFuturePv && replacementAccepted && correctedPacketValid;
}

bool cumulativeClockRecovery(bool legacy) {
    setup(legacy);
    const uint32_t initialUtc = oldUtc + 600;
    testFixtureUtc = initialUtc;
    if (!loadPrices(initialUtc) || !loadPv(pvForecast, initialUtc, 1200, "initial PV")) return false;
    for (unsigned i = 0; i < 3; ++i) tick();
    if (!futurePlan.valid || !pvForecast.valid(initialUtc)) {
        std::printf("FAIL cumulative fixture: initial plan=%d PV valid=%d\n", futurePlan.valid, pvForecast.valid(initialUtc));
        return false;
    }

    testFixtureUtc = initialUtc - 3;
    tick();
    const bool tolerated = pvForecast.valid(uint32_t(testFixtureUtc)) && pvForecast.revision() == initialUtc;
    std::printf("%s first 3 s rollback remains within tolerance\n", tolerated ? "PASS" : "FAIL");
    testFixtureUtc = initialUtc - 6;
    tick();
    const bool reset = pvForecast.slots() == 0 && !pvForecast.valid(uint32_t(testFixtureUtc));
    std::printf("%s cumulative 3 s + 3 s rollback invalidates PV epoch: slots=%u valid=%d\n",
                reset ? "PASS" : "FAIL", pvForecast.slots(), pvForecast.valid(uint32_t(testFixtureUtc)));

    // Exercise receiver-first ordering independently: an MQTT callback can run
    // between ticks, so its clock observation must precede revision comparison.
    control::PvForecast receiver;
    if (!loadPv(receiver, initialUtc, 1200, "receiver initial PV")) return false;
    const std::string withinTolerance = pvPacket(initialUtc - 3, 0, 600);
    const char *first = sendPv(receiver, withinTolerance, initialUtc - 3);
    const bool preserves = !std::strcmp(first, "old_revision");
    const std::string beyondTolerance = pvPacket(initialUtc - 6, 0, 600);
    const char *second = sendPv(receiver, beyondTolerance, initialUtc - 6);
    const bool resets = !std::strcmp(second, "pending");
    std::printf("%s receiver-first cumulative rollback: first=%s second=%s (expected old_revision, pending)\n",
                preserves && resets ? "PASS" : "FAIL", first, second);
    return tolerated && reset && preserves && resets;
}

bool shortRollbackReplay(bool legacy) {
    setup(legacy);
    const uint32_t initialUtc = oldUtc + 600;
    const uint32_t afterRollback = initialUtc - 30;
    testFixtureUtc = initialUtc;
    if (!loadPrices(initialUtc) || !loadPv(pvForecast, initialUtc, 1200, "initial PV")) return false;
    for (unsigned i = 0; i < 3; ++i) tick();
    if (!futurePlan.valid || !pvForecast.valid(initialUtc)) {
        std::printf("FAIL short rollback fixture: initial plan=%d PV valid=%d\n", futurePlan.valid, pvForecast.valid(initialUtc));
        return false;
    }

    testFixtureUtc = afterRollback;
    tick();
    const bool invalidated = pvForecast.slots() == 0 && !pvForecast.valid(afterRollback);
    const char *first = nullptr;
    const char *last = nullptr;
    for (unsigned part = 0; part < 6; ++part) {
        const std::string packet = pvPacket(initialUtc, part, 1200);
        const char *result = sendPv(pvForecast, packet, afterRollback);
        if (!part) first = result;
        last = result;
    }
    const bool replayRejected = !pvForecast.valid(afterRollback) && pvForecast.slots() == 0;
    std::printf("%s 30 s rollback then six old retained PV chunks: invalidated=%d first=%s last=%s slots=%u valid=%d\n",
                invalidated && replayRejected ? "PASS" : "FAIL", invalidated, first, last,
                pvForecast.slots(), pvForecast.valid(afterRollback));

    const bool recovery = loadPv(pvForecast, afterRollback, 600, "30 s corrected PV recovery");
    const bool recoveryQualified = recovery && pvForecast.valid(afterRollback) && pvForecast.revision() == afterRollback;
    std::printf("%s 30 s rollback accepts corrected forecast from same session\n", recoveryQualified ? "PASS" : "FAIL");

    bool staysRecovered = false;
    if (recoveryQualified) {
        for (unsigned part = 0; part < 6; ++part) {
            const std::string packet = pvPacket(initialUtc, part, 1200);
            sendPv(pvForecast, packet, afterRollback);
        }
        float power = 0;
        staysRecovered = pvForecast.revision() == afterRollback && pvForecast.wattsAt(afterRollback, power) && power == 600;
        std::printf("%s old retained chunks cannot replace corrected recovery epoch\n", staysRecovered ? "PASS" : "FAIL");
    }

    // The normal producer clock-skew allowance is 120 s. Its original boundary
    // must be restored once the observed clock has reached the old high-water mark.
    testFixtureUtc = initialUtc;
    tick();
    const std::string tooFar = pvPacket(initialUtc + 121, 0, 700);
    const char *beyondResult = sendPv(pvForecast, tooFar, initialUtc);
    const bool rejects121 = !std::strcmp(beyondResult, "packet_range");
    const bool accepts120 = loadPvAt(pvForecast, initialUtc + 120, initialUtc, 700, "120 s producer lead");
    float finalPower = 0;
    const bool clockLeadRestored = accepts120 && pvForecast.wattsAt(initialUtc, finalPower) && finalPower == 700;
    std::printf("%s producer lead boundary after high-water recovery: +121 s=%s, +120 s=%s\n",
                rejects121 && clockLeadRestored ? "PASS" : "FAIL", beyondResult,
                clockLeadRestored ? "accepted" : "rejected");
    return invalidated && replayRejected && recoveryQualified && staysRecovered && rejects121 && clockLeadRestored;
}

bool futureOnlyPvIsIgnoredByPlanner(bool legacy) {
    setup(legacy);
    control::FuturePrices referencePrices;
    control::PvForecast futureOnlyPv;
    if (!loadPrices(correctedUtc) || !loadPricesTo(referencePrices, correctedUtc) ||
        !loadPv(futureOnlyPv, oldUtc, 1200, "future-only PV")) return false;
    float futurePower = 0;
    if (futureOnlyPv.valid(correctedUtc) || !futureOnlyPv.wattsAt(oldUtc, futurePower) || futurePower != 1200) {
        std::printf("FAIL future-only fixture: now=%d futurePower=%.1f W\n",
                    futureOnlyPv.valid(correctedUtc), double(futurePower));
        return false;
    }
    control::DischargeBudget actualTrace, referenceTrace;
    // A real current 800 W observation creates meaningful current charging
    // demand. The future-only 1200 W is not an observation at the current UTC.
    const auto actual = futurePrices.plan(correctedUtc, uint32_t(clockMs), preparedSettings,
        true, preparedSettings.reserveOffSocPercent, 0, 100, 1, true, .1f, false,
        1, 900, &futureOnlyPv, 1820, &actualTrace, 800, false);
    const auto reference = referencePrices.plan(correctedUtc, uint32_t(clockMs), preparedSettings,
        true, preparedSettings.reserveOffSocPercent, 0, 100, 1, true, .1f, false,
        1, 900, nullptr, 1820, &referenceTrace, 800, false);
    if (!actual.valid || !reference.valid || !actualTrace.valid || !referenceTrace.valid ||
        actualTrace.count != slots || referenceTrace.count != slots) {
        std::printf("FAIL direct planner fixture: actual=%d/%s reference=%d/%s traces=%d/%d\n",
                    actual.valid, actual.reason, reference.valid, reference.reason, actualTrace.valid, referenceTrace.valid);
        return false;
    }
    bool passed = !actual.pvQualified && actual.offeredPvWh == 0 &&
        actual.batteryCeilingW == reference.batteryCeilingW && actual.directPvCeilingW == reference.directPvCeilingW &&
        std::fabs(actual.chargeTargetW - reference.chargeTargetW) < .001f &&
        std::fabs(actual.usablePvWh - reference.usablePvWh) < .001f;
    unsigned firstMismatch = slots;
    for (unsigned i = 0; i < slots; ++i) {
        const bool equal = futurePrices.projectedSolarW(i) == referencePrices.projectedSolarW(i) &&
            (!i || futurePrices.projectedSolarW(i) == -1) &&
            std::fabs(futurePrices.projectedChargeW(i) - referencePrices.projectedChargeW(i)) < .001f &&
            actualTrace.watts[i] == referenceTrace.watts[i];
        if (!equal && firstMismatch == slots) firstMismatch = i;
        passed = passed && equal;
    }
    // This fixture must actually exercise the relative planner's future target
    // lookup: without qualified future PV it chooses to store measured solar.
    const bool meaningfulTarget = legacy || reference.chargeTargetW > 0;
    passed = passed && meaningfulTarget;
    std::printf("%s direct future-only PV: offered=%.1f Wh qualified=%d slot4=%ld W target=%.1f/%.1f W firstMismatch=%u meaningfulTarget=%d\n",
                passed ? "PASS" : "FAIL", double(actual.offeredPvWh), actual.pvQualified,
                static_cast<long>(futurePrices.projectedSolarW(4)), double(actual.chargeTargetW),
                double(reference.chargeTargetW), firstMismatch, meaningfulTarget);
    return passed;
}
} // namespace

int main(int argc, char **argv) {
    (void)fakeTime; // The shared fixture's fixed clock is intentionally unused here.
    if (argc != 3) {
        std::printf("Usage: pv_clock_recovery_test <relative|legacy> <hour|cumulative|replay|direct>\n");
        return 2;
    }
    const bool legacy = !std::strcmp(argv[1], "legacy");
    if (!legacy && std::strcmp(argv[1], "relative")) return 2;
    if (!std::strcmp(argv[2], "hour")) return clockRecovery(legacy) ? 0 : 1;
    if (!std::strcmp(argv[2], "cumulative")) return cumulativeClockRecovery(legacy) ? 0 : 1;
    if (!std::strcmp(argv[2], "replay")) return shortRollbackReplay(legacy) ? 0 : 1;
    if (!std::strcmp(argv[2], "direct")) return futureOnlyPvIsIgnoredByPlanner(legacy) ? 0 : 1;
    return 2;
}
