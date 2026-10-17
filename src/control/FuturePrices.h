#pragma once

#include "PreparedControl.h"
#include "TelemetryAge.h"
#include <algorithm>
#include <cstring>

namespace control {

struct FuturePlan {
    bool valid = false;
    int batteryCeilingW = 0;
    float chargeTargetW = 0;
    float availableWh = 0;
    float reservedWh = 0;
    float shortfallWh = 0;
    float nextPrice = 0;
    uint32_t targetAt = 0;
    const char *reason = "forecast_missing";
};

// Published quarter-hour prices in micro EUR/kWh. No persistence or heap cache.
class FuturePrices {
public:
    static constexpr unsigned kSlots = 288, kChunk = 24;
    void clear() {
        activeCount_ = pendingCount_ = 0; parts_ = 0;
        activeRevision_ = pendingRevision_ = 0;
    }

    const char *receive(const uint8_t *payload, unsigned length, uint32_t utc, uint32_t ms) {
        if (utc < 1704067200UL) return "clock_invalid";
        if (!length || length > 768) return "packet_size";
        arena_.reset();
        JsonDocument doc(&arena_);
        if (deserializeJson(doc, payload, length)) return "packet_json";
        if (!doc["v"].is<unsigned>() || doc["v"].as<unsigned>() != 1 ||
            !doc["r"].is<uint32_t>() || !doc["t"].is<uint32_t>() ||
            !doc["n"].is<unsigned>() || !doc["k"].is<unsigned>() ||
            !doc["p"].is<JsonArray>()) return "packet_schema";
        const uint32_t revision = doc["r"], start = doc["t"];
        const unsigned count = doc["n"], part = doc["k"];
        const JsonArrayConst prices = doc["p"].as<JsonArrayConst>();
        if (revision > utc + 120 || utc > revision + 259200UL ||
            start % 900 || start > utc || utc - start >= 900 ||
            count > kSlots || part >= 12) return "packet_range";
        if (revision < activeRevision_ || revision < pendingRevision_) return "old_revision";
        if (revision == activeRevision_) return "duplicate";
        if (!count) {
            if (part || prices.size()) return "packet_empty";
            clear(); activeRevision_ = revision;
            return "withdrawn";
        }
        const unsigned offset = part * kChunk;
        if (count < 2 || offset >= count || prices.size() != std::min(kChunk, count - offset))
            return "packet_count";
        for (JsonVariantConst value : prices) {
            if (!value.is<int32_t>() || value.as<int32_t>() < -2000000 || value.as<int32_t>() > 10000000)
                return "packet_price";
        }
        if (revision != pendingRevision_ || uint32_t(ms - pendingAt_) > 30000UL) {
            pendingRevision_ = revision; pendingStart_ = start; pendingCount_ = count;
            parts_ = 0; pendingAt_ = ms;
        }
        if (start != pendingStart_ || count != pendingCount_) return "packet_conflict";
        if (parts_ & (1u << part)) {
            for (unsigned i = 0; i < prices.size(); ++i)
                if (pending_[offset + i] != prices[i].as<int32_t>()) return "packet_conflict";
        }
        for (unsigned i = 0; i < prices.size(); ++i) pending_[offset + i] = prices[i];
        parts_ |= uint16_t(1u << part);
        const unsigned chunks = (count + kChunk - 1) / kChunk;
        if (parts_ != (1u << chunks) - 1) return "pending";
        std::memcpy(active_, pending_, count * sizeof(active_[0]));
        activeStart_ = start; activeCount_ = count; activeRevision_ = revision;
        return "accepted";
    }

    FuturePlan plan(uint32_t utc, uint32_t ms, const PreparedSettings &cfg,
                    bool bmsValid, float soc, float batteryW, float fullAh,
                    uint32_t sequence, bool currentPriceValid, float currentPrice, bool currentExpensive,
                    float factor, int maximumW) {
        FuturePlan out;
        const bool energyValid = bmsValid && std::isfinite(fullAh) && fullAh > 0 && fullAh <= 389 &&
            std::isfinite(soc) && soc >= 0 && soc <= 100 && std::isfinite(batteryW);
        if (!energyValid) {
            energyKnown_ = false;
            out.reason = "energy_missing";
            return out;
        }
        // Nominal 14s reference, not measured usable energy. Reconcile one pool:
        // repeated SOC must never replenish energy already debited by signed power.
        const float fullWh = fullAh * 51.38f;
        const float socWh = std::max(0.0f, (soc - cfg.reserveOffSocPercent) * 0.01f * fullWh);
        if (!energyKnown_ || uint32_t(ms - energyAt_) > kSourceStaleMs) {
            energyWh_ = socWh; energyKnown_ = true;
            energySequence_ = sequence; energyAt_ = ms; lastBatteryW_ = batteryW;
        } else if (sequence != energySequence_) {
            const float dt = float(uint32_t(ms - energyAt_)) / 3600000.0f;
            // Integrate every signed sample, including inside the direction deadband.
            energyWh_ += (batteryW + lastBatteryW_) * 0.5f * dt;
            energyWh_ = std::max(0.0f, std::min(energyWh_, socWh));
        }
        if (sequence != energySequence_ || !energyAt_) {
            energySequence_ = sequence; energyAt_ = ms; lastBatteryW_ = batteryW;
        }
        // A reserve/capacity change restricts the same pool even before the next sample.
        energyWh_ = std::max(0.0f, std::min(energyWh_, socWh));
        out.availableWh = energyWh_ * kDischargeEfficiency;
        if (!cfg.valid || !std::isfinite(factor) || factor <= 0 || factor > 5) {
            out.reason = "forecast_settings"; return out;
        }
        if (utc < 1704067200UL || (lastUtc_ && utc + 5 < lastUtc_)) {
            out.reason = "clock_invalid"; clear(); lastUtc_ = utc; return out;
        }
        lastUtc_ = utc;
        if (!activeCount_ || utc < activeStart_ || utc >= activeStart_ + activeCount_ * 900UL ||
            utc > activeRevision_ + 259200UL) return out;
        const unsigned first = (utc - activeStart_) / 900;
        const unsigned end = std::min(unsigned(activeCount_), first + unsigned(kLookAheadHours) * 4);
        if (end <= first + 1) return out;
        if (!currentPriceValid || !std::isfinite(currentPrice)) {
            out.reason = "current_price_missing"; return out;
        }
        // Allocate remaining AC energy to the highest priced published slots.
        // UTC-anchored slots never slide their end forward on a replan.
        uint16_t order[144];
        const unsigned count = end - first;
        for (unsigned i = 0; i < count; ++i) order[i] = uint16_t(first + i);
        const auto price = [&](unsigned i) {
            return i == first ? currentPrice : float(active_[i]) * 0.000001f * factor;
        };
        std::sort(order, order + count, [&](unsigned a, unsigned b) {
            return price(a) == price(b) ? a < b : price(a) > price(b);
        });
        out.nextPrice = price(first + 1);
        const float threshold = cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh;
        float pool = out.availableWh, currentAllocation = 0, futureNeed = 0;
        uint32_t firstTarget = 0;
        for (unsigned j = 0; j < count; ++j) {
            const unsigned i = order[j];
            const float p = price(i);
            if (p < 0 || (i == first ? !currentExpensive : p < threshold)) continue;
            // Already stored energy has no second acquisition/round-trip charge.
            // Preserve it for a materially better later price, not tiny noise.
            if (i != first && currentExpensive && p < currentPrice + kMinimumBenefitEurPerKwh)
                continue;
            const uint32_t start = activeStart_ + i * 900UL;
            const float hours = float(start + 900UL - (i == first ? utc : start)) / 3600.0f;
            const float load = p >= cfg.priceComfortEurPerKwh + 2 * cfg.priceDeadbandEurPerKwh
                ? float(maximumW) : std::min(float(maximumW), float(kPlanningLoadW));
            const float need = std::max(0.0f, load) * hours;
            const float allocation = std::min(pool, need);
            pool -= allocation;
            if (i == first) currentAllocation = allocation;
            else {
                out.reservedWh += allocation;
                // Only request new storage if the future return covers losses/wear.
                if (p * kRoundTripEfficiency - currentPrice >= kWearEurPerKwh + kMinimumBenefitEurPerKwh) {
                    futureNeed += need;
                    if (!firstTarget || start < firstTarget) firstTarget = start;
                }
            }
        }
        const uint32_t slotEnd = activeStart_ + (first + 1) * 900UL;
        out.batteryCeilingW = int(std::min(float(maximumW), currentAllocation * 3600.0f / float(slotEnd - utc)));
        const float capacityAc = (100 - cfg.reserveOffSocPercent) * 0.01f * fullWh * kDischargeEfficiency;
        out.shortfallWh = std::max(0.0f, std::min(capacityAc, futureNeed) - out.availableWh);
        out.targetAt = firstTarget;
        if (firstTarget > utc && out.shortfallWh > 0)
            out.chargeTargetW = out.shortfallWh / kDischargeEfficiency * 3600.0f / float(firstTarget - utc);
        out.valid = true;
        out.reason = out.batteryCeilingW > 0 ? "forecast_release" :
            (out.shortfallWh > 0 ? "forecast_charge" : "forecast_hold");
        return out;
    }
    uint32_t revision() const { return activeRevision_; }
    unsigned slots() const { return activeCount_; }
private:
    int32_t active_[kSlots] = {}, pending_[kSlots] = {};
    uint32_t activeStart_ = 0, activeRevision_ = 0, pendingStart_ = 0, pendingRevision_ = 0;
    uint32_t pendingAt_ = 0, lastUtc_ = 0;
    unsigned activeCount_ = 0, pendingCount_ = 0;
    uint16_t parts_ = 0;
    bool energyKnown_ = false;
    float energyWh_ = 0, lastBatteryW_ = 0;
    uint32_t energyAt_ = 0, energySequence_ = 0;
    JsonArena arena_;
};
} // namespace control
