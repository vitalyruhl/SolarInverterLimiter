#include "control/ChargePlan.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

using control::ChargePlan;
using control::DischargeBudget;
using control::PlanPublication;

// [MOCKED!] Time, source availability and connection events; the production
// publication and plan serialization implementations are used unchanged.
static constexpr uint32_t kGenerated = 1793038500u;

struct Results {
    unsigned checks = 0;
    unsigned failures = 0;

    void expect(bool result, const std::string &label) {
        ++checks;
        if (!result) {
            ++failures;
            std::cerr << "FAIL " << label << '\n';
        }
    }
};

static DischargeBudget originalDischarge(unsigned count) {
    DischargeBudget plan;
    plan.reset(kGenerated, kGenerated - 60u, kGenerated - 120u);
    plan.start = kGenerated;
    plan.count = count;
    for (unsigned i = 0; i < count; ++i) plan.watts[i] = int32_t(100u + (i % 8u) * 100u);
    plan.finish(true, "forecast_release");
    return plan;
}

static ChargePlan originalCharge(unsigned count) {
    ChargePlan plan;
    plan.reset(kGenerated, kGenerated - 60u, kGenerated - 120u);
    plan.start = kGenerated;
    plan.count = count;
    for (unsigned i = 0; i < count; ++i) {
        plan.watts[i] = int32_t(100u + (i % 8u) * 25u);
        plan.solarWatts[i] = int32_t(300u + (i % 8u) * 50u);
    }
    plan.finish(true, "intent_projection");
    return plan;
}

template<class Plan>
static void unchangedSnapshot(Results &results, const PlanPublication<Plan> &publication,
                              const char *expected, const std::string &label) {
    results.expect(publication.snapshot.valid, label + " available");
    if (!publication.snapshot.valid) return;
    results.expect(publication.snapshot.generatedAt == kGenerated, label + " original generatedAt");
    std::array<char, Plan::kPayloadBytes> actual{};
    const std::size_t length = publication.snapshot.serialize(actual.data(), actual.size());
    results.expect(length > 0 && std::strcmp(actual.data(), expected) == 0, label + " original content");
}

template<class Plan>
static void outageRetention(Results &results, const Plan &original, uint32_t initialMs,
                            uint32_t secondsBeforeOutage, const std::string &label) {
    PlanPublication<Plan> publication;
    publication.connection(true);
    publication.observe(initialMs, kGenerated, 1, true, false, true);
    publication.select(original, initialMs, 1);
    publication.published(true, initialMs);

    std::array<char, Plan::kPayloadBytes> expected{};
    results.expect(original.serialize(expected.data(), expected.size()) > 0, label + " fixture serializes");
    unchangedSnapshot(results, publication, expected.data(), label + " accepted");
    results.expect(publication.planAvailable() && !publication.retained(), label + " initially available and current");

    Plan failed;
    const uint32_t degradedMs = initialMs + secondsBeforeOutage * 1000u;
    const uint32_t degradedUtc = kGenerated + secondsBeforeOutage;
    failed.reset(degradedUtc, degradedUtc, degradedUtc);
    failed.finish(false, "source_missing");
    publication.observe(degradedMs, degradedUtc, 1, false, false, true);
    publication.select(failed, degradedMs, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " first input outage");
    results.expect(publication.planAvailable() && publication.retained() && publication.degradedSince() == degradedMs,
                   label + " records first input outage");
    publication.observe(degradedMs + 1u, degradedUtc, 1, false, false, true);
    results.expect(!publication.captureDue(degradedMs + 1u), label + " outage does not request a rebuild every tick");

    publication.connection(false);
    publication.observe(degradedMs + 100u, degradedUtc, 1, false, false, true);
    publication.connection(true);
    publication.observe(degradedMs + 200u, degradedUtc, 1, false, false, true);
    publication.select(failed, degradedMs + 200u, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " reconnect and failed rebuild");
    results.expect(publication.retained() && publication.degradedSince() == degradedMs,
                   label + " reconnect does not renew outage time");
    results.expect(!publication.publishDue(degradedMs + 200u), label + " historical replay withheld");

    publication.observe(degradedMs + 300u, degradedUtc, 1, true, false, true);
    publication.qualification(true);
    publication.select(original, degradedMs + 300u, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " identical prior generation replay");
    results.expect(publication.retained() && publication.degradedSince() == degradedMs,
                   label + " prior generation cannot end outage");
    results.expect(!publication.publishDue(degradedMs + 300u), label + " prior generation remains withheld");

    Plan incomplete = original;
    incomplete.generatedAt = degradedUtc + 1u;
    incomplete.start = incomplete.generatedAt - incomplete.generatedAt % 900u;
    incomplete.count = 1;
    publication.observe(degradedMs + 1000u, degradedUtc + 1u, 1, true, false, true);
    publication.select(incomplete, degradedMs + 1000u, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " incomplete candidate");
    results.expect(publication.degradedSince() == degradedMs, label + " incomplete candidate retains outage time");

    Plan malformed = original;
    malformed.generatedAt = degradedUtc + 1u;
    malformed.start = malformed.generatedAt - malformed.generatedAt % 900u;
    malformed.watts[0] = -2;
    publication.select(malformed, degradedMs + 1001u, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " malformed candidate");
    results.expect(publication.degradedSince() == degradedMs, label + " malformed candidate retains outage time");

    Plan unqualified = original;
    unqualified.generatedAt = degradedUtc + 2u;
    unqualified.start = unqualified.generatedAt - unqualified.generatedAt % 900u;
    publication.observe(degradedMs + 2000u, degradedUtc + 2u, 1, false, false, true);
    publication.select(unqualified, degradedMs + 2000u, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " unqualified rebuild");
    results.expect(publication.degradedSince() == degradedMs, label + " unqualified rebuild retains outage time");

    // In the two-slot scenario, coverage ends five seconds into the outage.
    // A changed context and invalid source must not erase diagnostic history.
    publication.observe(degradedMs + 5000u, degradedUtc + 5u, 2, false, true, false);
    publication.select(failed, degradedMs + 5000u, 2);
    unchangedSnapshot(results, publication, expected.data(), label +
        (original.count == 2 ? " slot end and context change" : " context change"));

    for (const uint32_t elapsed : {299999u, 300000u}) {
        const uint32_t nowMs = degradedMs + elapsed;
        const uint32_t nowUtc = degradedUtc + elapsed / 1000u;
        publication.observe(nowMs, nowUtc, 2, false, true, false);
        publication.select(failed, nowMs, 2);
        unchangedSnapshot(results, publication, expected.data(), label + " outage " + std::to_string(elapsed) + "ms");
        results.expect(publication.retained() && publication.degradedSince() == degradedMs,
                       label + " original outage time " + std::to_string(elapsed) + "ms");
        results.expect(!publication.publishDue(nowMs), label + " historical replay blocked " + std::to_string(elapsed) + "ms");
    }

    // No final history expiry has been approved; do not invent a cutoff.
    const uint32_t afterMinimum = degradedMs + 300001u;
    publication.observe(afterMinimum, degradedUtc + 300u, 2, false, true, false);
    publication.select(failed, afterMinimum, 2);
    unchangedSnapshot(results, publication, expected.data(), label + " after 300s");
    results.expect(publication.retained() && publication.degradedSince() == degradedMs,
                   label + " remains retained after 300s");

    const uint32_t laterMs = degradedMs + 3600000u;
    const uint32_t laterUtc = degradedUtc + 3600u;
    publication.observe(laterMs, laterUtc, 2, false, true, false);
    publication.select(failed, laterMs, 2);
    unchangedSnapshot(results, publication, expected.data(), label + " after one hour");
    results.expect(!publication.publishDue(laterMs), label + " old valid payload still withheld");

    publication.observe(laterMs + 100u, laterUtc, 2, true, false, true);
    publication.qualification(true);
    publication.select(failed, laterMs + 100u, 2);
    results.expect(publication.retained() && publication.degradedSince() == degradedMs,
                   label + " partial recovery and failed rebuild do not end outage");
    results.expect(!publication.publishDue(laterMs + 100u), label + " partial recovery cannot replay history");
    Plan replacement = original;
    replacement.generatedAt = laterUtc + 1u;
    replacement.start = replacement.generatedAt - replacement.generatedAt % 900u;
    publication.observe(laterMs + 1000u, laterUtc + 1u, 3, true, false, true);
    publication.select(replacement, laterMs + 1000u, 3);
    results.expect(publication.planAvailable() && !publication.retained() && publication.degradedSince() == 0,
                   label + " only accepted rebuild ends outage");
    results.expect(publication.snapshot.generatedAt == laterUtc + 1u && publication.publishDue(laterMs + 1000u),
                   label + " new generation is publishable");
}

template<class Plan>
static void noPreviousPlanRemainsUnavailable(Results &results, const std::string &label) {
    PlanPublication<Plan> publication;
    Plan failed;
    failed.reset(kGenerated, 0, 0);
    failed.finish(false, "source_missing");
    publication.connection(true);
    publication.observe(0, kGenerated, 1, false, true, false);
    publication.select(failed, 0, 1);
    results.expect(!publication.planAvailable() && !publication.retained(), label + " uninitialized plan unavailable");
}

template<class Plan>
static void offlineCaptureAndZeroTimestamp(Results &results, const Plan &original, const std::string &label) {
    PlanPublication<Plan> publication;
    results.expect(publication.captureDue(1), label + " offline capture due");
    publication.observe(1, kGenerated, 1, true, false, true);
    publication.select(original, 1, 1);
    results.expect(publication.planAvailable() && !publication.publishDue(1), label + " offline capture is not published");
    publication.connection(true);
    publication.observe(2, kGenerated, 1, true, false, true);
    publication.select(original, 2, 1);
    results.expect(publication.publishDue(2) && publication.snapshot.generatedAt == kGenerated,
                   label + " fresh original generation published after reconnect");

    PlanPublication<Plan> wrapped;
    wrapped.observe(std::numeric_limits<uint32_t>::max() - 100u, kGenerated, 1, true, false, true);
    wrapped.select(original, std::numeric_limits<uint32_t>::max() - 100u, 1);
    wrapped.observe(0, kGenerated + 1u, 1, false, false, true);
    Plan failed;
    failed.reset(kGenerated + 1u, 0, 0);
    failed.finish(false, "source_missing");
    wrapped.select(failed, 0, 1);
    results.expect(wrapped.planAvailable() && wrapped.retained() && wrapped.degradedSince() == 0,
                   label + " zero millis outage is distinguishable by retained flag");
}

template<class Plan>
static void expiredSnapshotRequestsNewCapture(Results &results, const Plan &original, const std::string &label) {
    PlanPublication<Plan> publication;
    publication.connection(true);
    publication.observe(100, kGenerated, 1, true, false, true);
    publication.select(original, 100, 1);

    // A recent capture schedule must not suppress the slot-expiry trigger.
    constexpr uint32_t expiredMs = 1800100u;
    constexpr uint32_t expiredUtc = kGenerated + 1800u;
    publication.captured(expiredMs - 200u);
    publication.published(true, expiredMs - 200u);
    publication.observe(expiredMs, expiredUtc, 1, true, false, true);
    results.expect(publication.planAvailable() && publication.snapshot.generatedAt == kGenerated,
                   label + " expired snapshot remains available");
    results.expect(publication.retained() && publication.degradedSince() == expiredMs,
                   label + " expired snapshot is historical");
    results.expect(publication.captureDue(expiredMs), label + " slot expiry requests immediate capture");
    results.expect(!publication.publishDue(expiredMs), label + " expired snapshot is not replayed");

    Plan replacement = original;
    replacement.generatedAt = expiredUtc;
    replacement.start = expiredUtc;
    publication.select(replacement, expiredMs, 1);
    results.expect(publication.planAvailable() && !publication.retained() && publication.snapshot.generatedAt == expiredUtc,
                   label + " valid replacement clears historical status");
    results.expect(publication.publishDue(expiredMs), label + " valid replacement can be published");
}

template<class Plan>
static void sameSecondRecoveryIsRetriedOnce(Results &results, const Plan &original, const std::string &label) {
    for (unsigned outcome = 0; outcome < 3; ++outcome) {
        PlanPublication<Plan> publication;
        publication.connection(true);
        publication.observe(100, kGenerated, 1, true, false, true);
        publication.qualification(true);
        publication.select(original, 100, 1);
        publication.published(true, 100);

        Plan failed;
        failed.reset(kGenerated, 0, 0);
        failed.finish(false, "source_missing");
        publication.observe(200, kGenerated, 1, false, false, true);
        publication.qualification(false);
        publication.select(failed, 200, 1);
        publication.observe(400, kGenerated, 1, true, false, true);
        publication.qualification(true);
        publication.select(original, 400, 1); // Valid content, but no new generation token yet.
        results.expect(publication.retained() && publication.degradedSince() == 200,
                       label + " same-second recovery retains original outage");
        results.expect(!publication.captureDue(401), label + " no retry inside same UTC second");

        publication.observe(1000, kGenerated + 1u, 1, true, false, true);
        results.expect(publication.captureDue(1000), label + " new UTC second promptly requests capture");
        if (outcome == 0) {
            Plan replacement = original;
            replacement.generatedAt = kGenerated + 1u;
            publication.select(replacement, 1000, 1);
            results.expect(!publication.retained() && publication.snapshot.generatedAt == kGenerated + 1u,
                           label + " next valid generation is accepted");
        } else {
            publication.select(outcome == 1 ? failed : original, 1000, 1);
            results.expect(publication.retained() && publication.degradedSince() == 200,
                           label + " failed or stale retry preserves outage");
            publication.observe(2000, kGenerated + 2u, 1, true, false, true);
            results.expect(!publication.captureDue(2000), label + " failed or stale retry does not loop");
        }
    }
}

template<class Publication, class Plan>
static auto selectFresh(Publication &publication, const Plan &candidate, uint32_t now, uint32_t context, int)
    -> decltype(publication.select(candidate, now, context, true), void()) {
    publication.select(candidate, now, context, true);
}
template<class Publication, class Plan>
static void selectFresh(Publication &publication, const Plan &candidate, uint32_t now, uint32_t context, long) {
    publication.select(candidate, now, context);
}

template<class Plan>
static void clockRollbackRetainsThenAcceptsNewEpoch(Results &results, const Plan &original,
                                                   uint32_t initialMs, const std::string &label) {
    PlanPublication<Plan> publication;
    publication.connection(true);
    publication.observe(initialMs, kGenerated, 1, true, false, true);
    publication.qualification(true);
    publication.select(original, initialMs, 1);
    publication.published(true, initialMs);

    std::array<char, Plan::kPayloadBytes> expected{};
    results.expect(original.serialize(expected.data(), expected.size()) > 0, label + " fixture serializes");
    const uint32_t jumpMs = initialMs + 1000u;
    const uint32_t correctedUtc = kGenerated - 3600u;
    publication.observe(jumpMs, correctedUtc, 1, true, false, true);
    publication.qualification(false);
    Plan failed;
    failed.reset(correctedUtc, 0, 0);
    failed.finish(false, "clock_invalid");
    publication.select(failed, jumpMs, 1);
    unchangedSnapshot(results, publication, expected.data(), label + " rollback start");
    results.expect(publication.retained() && publication.degradedSince() == jumpMs,
                   label + " rollback outage start");

    const auto candidateAt = [&](uint32_t utc) {
        Plan next = original;
        next.generatedAt = utc;
        next.priceRevision = utc;
        next.pvRevision = utc;
        next.start = utc - utc % 900u;
        next.watts[0] = 333;
        return next;
    };

    publication.observe(jumpMs + 1000u, correctedUtc + 1u, 1, true, false, true);
    publication.qualification(true);
    auto candidate = candidateAt(correctedUtc + 1u);
    selectFresh(publication, candidate, jumpMs + 1000u, 1, 0);
    unchangedSnapshot(results, publication, expected.data(), label + " early corrected candidate");
    publication.observe(jumpMs + 2000u, correctedUtc + 2u, 1, true, false, true);
    results.expect(!publication.captureDue(jumpMs + 2000u), label + " no per-second rollback loop");

    for (const uint32_t elapsed : {299999u, 300000u}) {
        const uint32_t utc = correctedUtc + elapsed / 1000u;
        publication.observe(jumpMs + elapsed, utc, 1, true, false, true);
        candidate = candidateAt(utc);
        selectFresh(publication, candidate, jumpMs + elapsed, 1, 0);
        unchangedSnapshot(results, publication, expected.data(), label + " rollback retention " + std::to_string(elapsed));
        results.expect(publication.retained() && publication.degradedSince() == jumpMs,
                       label + " rollback original outage time " + std::to_string(elapsed));
        results.expect(!publication.publishDue(jumpMs + elapsed), label + " rollback history withheld");
    }

    publication.observe(jumpMs + 300001u, correctedUtc + 300u, 1, true, false, true);
    publication.select(candidate, jumpMs + 300001u, 1); // Replay of the pre-deadline candidate.
    unchangedSnapshot(results, publication, expected.data(), label + " old corrected candidate replay");
    results.expect(publication.retained() && publication.degradedSince() == jumpMs,
                   label + " replay does not end rollback retention");
    publication.observe(jumpMs + 300002u, correctedUtc + 300u, 1, true, false, true);
    results.expect(!publication.captureDue(jumpMs + 300002u), label + " no retry while UTC is unchanged");

    publication.observe(jumpMs + 301000u, correctedUtc + 301u, 1, true, false, true);
    results.expect(publication.captureDue(jumpMs + 301000u), label + " new corrected second requests one capture");
    // Even a claimed fresh projection must carry the current observed UTC.
    selectFresh(publication, candidate, jumpMs + 301000u, 1, 0);
    results.expect(publication.retained() && publication.degradedSince() == jumpMs,
                   label + " stale retry remains historical");
    publication.observe(jumpMs + 302000u, correctedUtc + 302u, 1, true, false, true);
    results.expect(!publication.captureDue(jumpMs + 302000u), label + " stale retry does not loop");
    candidate = candidateAt(correctedUtc + 302u);
    selectFresh(publication, candidate, jumpMs + 302000u, 1, 0);
    results.expect(publication.planAvailable() && !publication.retained() &&
                   publication.snapshot.generatedAt == correctedUtc + 302u,
                   label + " valid corrected generation accepted after minimum");
    results.expect(publication.publishDue(jumpMs + 302000u), label + " corrected new plan publishable");
}

int main() {
    Results results;
    noPreviousPlanRemainsUnavailable<DischargeBudget>(results, "discharge");
    noPreviousPlanRemainsUnavailable<ChargePlan>(results, "charge");
    offlineCaptureAndZeroTimestamp(results, originalDischarge(2), "discharge");
    offlineCaptureAndZeroTimestamp(results, originalCharge(2), "charge");
    expiredSnapshotRequestsNewCapture(results, originalDischarge(2), "discharge");
    expiredSnapshotRequestsNewCapture(results, originalCharge(2), "charge");
    sameSecondRecoveryIsRetriedOnce(results, originalDischarge(2), "discharge");
    sameSecondRecoveryIsRetriedOnce(results, originalCharge(2), "charge");
    for (const uint32_t start : {12345u, std::numeric_limits<uint32_t>::max() - 1500u}) {
        const std::string clock = start == 12345u ? "normal clock" : "millis wrap";
        clockRollbackRetainsThenAcceptsNewEpoch(results, originalDischarge(2), start, "discharge rollback " + clock);
        clockRollbackRetainsThenAcceptsNewEpoch(results, originalCharge(2), start, "charge rollback " + clock);
    }
    for (const unsigned count : {2u, 144u}) {
        const uint32_t secondsBeforeOutage = count == 2u ? 1795u : 1u;
        const uint32_t wrappedStart = std::numeric_limits<uint32_t>::max() - secondsBeforeOutage * 1000u - 500u;
        for (const uint32_t start : {12345u, wrappedStart}) {
            const std::string clock = start == 12345u ? "normal clock" : "millis wrap";
            const std::string scenario = std::to_string(count) + " slots " + clock;
            outageRetention(results, originalDischarge(count), start, secondsBeforeOutage, "discharge " + scenario);
            outageRetention(results, originalCharge(count), start, secondsBeforeOutage, "charge " + scenario);
        }
    }
    std::cout << "[MOCKED!] diagnostic plan retention: " << results.checks << " checks, "
              << results.failures << " failures\n";
    return results.failures ? 1 : 0;
}
