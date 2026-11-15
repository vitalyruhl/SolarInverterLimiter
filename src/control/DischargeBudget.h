#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace control {
// Diagnostic AC battery allowance; never a complete future inverter command.
struct DischargeBudget {
    static constexpr unsigned kSlots = 144;
    static constexpr size_t kPayloadBytes = 2048, kMqttPacketBytes = 2304;
    uint32_t generatedAt = 0, priceRevision = 0, pvRevision = 0, start = 0;
    unsigned count = 0;
    bool valid = false;
    const char *reason = "forecast_missing";
    int32_t watts[kSlots] = {};

    void reset(uint32_t utc, uint32_t prices, uint32_t pv) {
        generatedAt = utc; priceRevision = prices; pvRevision = pv; start = 0;
        count = 0; valid = false; reason = "forecast_missing";
        std::fill(watts, watts + kSlots, 0);
    }
    void finish(bool accepted, const char *why) {
        valid = accepted; reason = why;
        if (!valid) count = 0;
    }
    static bool packetFits(size_t topicBytes, size_t payloadBytes) {
        return payloadBytes > 0 && payloadBytes < kPayloadBytes &&
            topicBytes <= kMqttPacketBytes - 7 &&
            payloadBytes <= kMqttPacketBytes - 7 - topicBytes;
    }
    // Return zero and an empty buffer on any overflow; a partial plan is never publishable.
    size_t serialize(char *buffer, size_t capacity) const {
        if (!buffer || !capacity) return 0;
        buffer[0] = '\0';
        if (count > kSlots || (valid && count < 2)) return 0;
        int written = std::snprintf(buffer, capacity,
            "{\"v\":1,\"valid\":%s,\"reason\":\"%s\",\"g\":%lu,\"r\":%lu,\"pv\":%lu,\"t\":%lu,\"d\":900,\"n\":%u,\"w\":[",
            valid ? "true" : "false", reason, static_cast<unsigned long>(generatedAt),
            static_cast<unsigned long>(priceRevision), static_cast<unsigned long>(pvRevision),
            static_cast<unsigned long>(start), valid ? count : 0);
        if (written < 0 || size_t(written) >= capacity) { buffer[0] = '\0'; return 0; }
        size_t used = size_t(written);
        for (unsigned i = 0; valid && i < count; ++i) {
            if (watts[i] < 0) { buffer[0] = '\0'; return 0; }
            written = std::snprintf(buffer + used, capacity - used, "%s%ld", i ? "," : "", static_cast<long>(watts[i]));
            if (written < 0 || size_t(written) >= capacity - used) { buffer[0] = '\0'; return 0; }
            used += size_t(written);
        }
        if (capacity - used < 3) { buffer[0] = '\0'; return 0; }
        buffer[used++] = ']'; buffer[used++] = '}'; buffer[used] = '\0';
        return used;
    }
};

// Publication timing is isolated from allocation and scalar/control publication.
template <typename Plan>
class PlanPublication {
public:
    Plan snapshot;
    Plan candidate; // Fixed publication staging; never copy the planner or renew a held frame.
    char payload[Plan::kPayloadBytes] = {};
    void connection(bool connected) {
        if (connected && !connected_) { requested_ = true; replay_ = true; pending_ = false; }
        connected_ = connected;
    }
    void topicChanged() {
        requested_ = true; pending_ = false; publishedValid_ = false; replay_ = false;
        sourceChanged_ = snapshot.valid;
    }
    // New accepted price/forecast content requests capture without renewing the held frame.
    void requestCapture() { requested_ = true; }
    // Diagnostic presence does not grant economic or inverter execution permission.
    bool planAvailable() const { return snapshot.valid; }
    bool retained() const { return snapshot.valid && degraded_; }
    // Monotonic millis at the first observed planning interruption; valid only while retained().
    uint32_t degradedSince() const { return retained() ? degradedAt_ : 0; }
    void observe(uint32_t now, uint32_t utc, uint32_t context, bool sourceGrace, bool hardInvalid, bool forecastCoverage) {
        const bool utcValid = utc >= 1704067200UL;
        const bool clockRollback = utcValid && lastValidUtc_ && uint64_t(utc) + 5 < lastValidUtc_;
        const bool clock = utcValid && !clockRollback;
        if (utcValid) lastValidUtc_ = utc;
        if (clockRollback && snapshot.valid) {
            rollbackEpoch_ = true; rollbackHeld_ = false; waitingForNewUtc_ = false;
        }
        lastUtc_ = utc; observedAt_ = now;
        blocked_ = hardInvalid || !clock || !forecastCoverage;
        sourceReady_ = sourceGrace && !blocked_;
        const bool snapshotCoverage = snapshot.count >= 2 && snapshot.count <= Plan::kSlots &&
            utc >= snapshot.start && uint64_t(utc) < uint64_t(snapshot.start) + uint64_t(snapshot.count) * 900;
        if (snapshot.valid && (!sourceReady_ || sourceChanged_ || context != context_ || !snapshotCoverage)) {
            if (!degraded_) requested_ = true;
            markDegraded(now);
        }
        if (rollbackEpoch_ && degraded_ && !rollbackHeld_ && uint32_t(now - degradedAt_) > 300000UL) {
            // Keep the original generation through the full minimum, then permit
            // one new projection when the observed UTC second advances.
            rollbackHeld_ = true; waitingForNewUtc_ = true; retryAfterUtc_ = utc;
        }
        sourceChanged_ = false;
    }
    void qualification(bool ready) {
        if (ready && !qualified_) requested_ = true;
        if (!ready && snapshot.valid) markDegraded(observedAt_);
        qualificationKnown_ = true;
        qualified_ = ready;
    }
    // freshlyBuilt is true only when the caller has just completed this candidate's
    // full projection. Replay and invalidation callers must keep the default false.
    void select(const Plan &next, uint32_t now, uint32_t context, bool freshlyBuilt = false) {
        const bool current = next.generatedAt && uint64_t(lastUtc_) + 5 >= next.generatedAt &&
            uint64_t(lastUtc_) < uint64_t(next.generatedAt) + 180;
        const bool coverage = next.count >= 2 && next.count <= Plan::kSlots &&
            lastUtc_ >= next.start && uint64_t(lastUtc_) < uint64_t(next.start) + uint64_t(next.count) * 900;
        const bool qualified = !qualificationKnown_ || qualified_;
        const bool newGeneration = rollbackEpoch_
            ? rollbackHeld_ && freshlyBuilt && next.generatedAt == lastUtc_
            : !retained() || next.generatedAt > snapshot.generatedAt;
        const bool validCandidate = next.valid && sourceReady_ && qualified && current && coverage &&
            next.serialize(payload, sizeof(payload));
        if (validCandidate && newGeneration) {
            snapshot = next; context_ = context;
            const uint32_t age = lastUtc_ > next.generatedAt ? (lastUtc_ - next.generatedAt) * 1000UL : 0;
            validAt_ = now - age; timed_ = true; degraded_ = sourceChanged_ = replay_ = false;
            waitingForNewUtc_ = rollbackEpoch_ = rollbackHeld_ = false; degradedAt_ = 0;
            captured(now);
        } else if (snapshot.valid) {
            // Keep the original diagnostic generation, including its original timestamp.
            markDegraded(now);
            // A full same-second rebuild cannot prove a new generation yet. Retry
            // once only after UTC advances; an invalid/stale retry disarms it.
            waitingForNewUtc_ = validCandidate && !newGeneration && next.generatedAt == lastUtc_ &&
                (!rollbackEpoch_ || (rollbackHeld_ && !freshlyBuilt));
            if (waitingForNewUtc_) retryAfterUtc_ = lastUtc_;
            if (replay_) { pending_ = true; attempted_ = false; }
            replay_ = false; captured_ = true; requested_ = false; captureAt_ = now;
        } else {
            snapshot = next;
            if (snapshot.valid) snapshot.finish(false, "plan_context_invalid");
            timed_ = degraded_ = sourceChanged_ = replay_ = waitingForNewUtc_ = rollbackEpoch_ = rollbackHeld_ = false;
            degradedAt_ = 0;
            captured(now);
        }
    }
    bool captureDue(uint32_t now) const {
        const bool retry = waitingForNewUtc_ && sourceReady_ && (!qualificationKnown_ || qualified_) &&
            lastUtc_ > retryAfterUtc_;
        return requested_ || !captured_ || retry || uint32_t(now - captureAt_) >= 60000UL;
    }
    bool invalidationDue(bool planValid) const {
        return connected_ && !planValid && !retained() &&
            ((pending_ && snapshot.valid) || (publishedValid_ && !pending_));
    }
    void captured(uint32_t now) {
        captured_ = pending_ = true; requested_ = attempted_ = false; captureAt_ = now;
    }
    bool publishDue(uint32_t now) const {
        // Until the independent status reaches consumers, never replay a historical
        // valid payload that could otherwise be mistaken for a current plan.
        return connected_ && pending_ && !sourceChanged_ && !retained() &&
            !(snapshot.valid && timed_ && uint32_t(now - validAt_) >= 180000UL) &&
            (!attempted_ || uint32_t(now - attemptAt_) >= 5000UL);
    }
    void published(bool success, uint32_t now) {
        attempted_ = true; attemptAt_ = now;
        if (success) { pending_ = false; publishedValid_ = snapshot.valid; }
    }
private:
    void markDegraded(uint32_t now) {
        if (snapshot.valid && !degraded_) { degraded_ = true; degradedAt_ = now; }
    }
    uint32_t captureAt_ = 0, attemptAt_ = 0;
    uint32_t validAt_ = 0, lastUtc_ = 0, lastValidUtc_ = 0, context_ = 0, observedAt_ = 0, degradedAt_ = 0, retryAfterUtc_ = 0;
    bool connected_ = false, captured_ = false, requested_ = true, pending_ = false;
    bool attempted_ = false, publishedValid_ = false;
    bool degraded_ = false, sourceChanged_ = false, replay_ = false, blocked_ = false;
    bool sourceReady_ = false, qualified_ = false, qualificationKnown_ = false, timed_ = false, waitingForNewUtc_ = false;
    bool rollbackEpoch_ = false, rollbackHeld_ = false;
};
using DischargeBudgetPublication = PlanPublication<DischargeBudget>;
} // namespace control
