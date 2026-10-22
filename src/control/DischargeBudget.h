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
class DischargeBudgetPublication {
public:
    DischargeBudget snapshot;
    char payload[DischargeBudget::kPayloadBytes] = {};
    void connection(bool connected) {
        if (connected && !connected_) { requested_ = true; pending_ = false; }
        connected_ = connected;
    }
    void topicChanged() { requested_ = true; pending_ = false; publishedValid_ = false; }
    bool captureDue(uint32_t now) const {
        return connected_ && (requested_ || !captured_ || uint32_t(now - captureAt_) >= 60000UL);
    }
    bool invalidationDue(bool planValid) const {
        return connected_ && !planValid &&
            ((pending_ && snapshot.valid) || (publishedValid_ && !pending_));
    }
    void captured(uint32_t now) {
        captured_ = pending_ = true; requested_ = attempted_ = false; captureAt_ = now;
    }
    bool publishDue(uint32_t now) const {
        return connected_ && pending_ && (!attempted_ || uint32_t(now - attemptAt_) >= 5000UL);
    }
    void published(bool success, uint32_t now) {
        attempted_ = true; attemptAt_ = now;
        if (success) { pending_ = false; publishedValid_ = snapshot.valid; }
    }
private:
    uint32_t captureAt_ = 0, attemptAt_ = 0;
    bool connected_ = false, captured_ = false, requested_ = true, pending_ = false;
    bool attempted_ = false, publishedValid_ = false;
};
} // namespace control
