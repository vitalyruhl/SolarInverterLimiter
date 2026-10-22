#pragma once

#include "PreparedControl.h"
#include "DischargeBudget.h"
#include "JsonArena.h"
#include "PvForecast.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace control {
struct FuturePlan {
    bool valid = false, pvQualified = false;
    float offeredPvWh = 0; // Continuous known DC offer in the available window, before storage limits.
    float usablePvWh = 0;  // AC-deliverable PV credit actually assigned to future demand, once.
    int batteryCeilingW = 0;
    float chargeTargetW = 0, availableWh = 0, reservedWh = 0, shortfallWh = 0, nextPrice = 0;
    uint32_t targetAt = 0;
    const char *reason = "forecast_missing";
};

// Published quarter-hour prices in micro EUR/kWh. No persistence or heap cache.
class FuturePrices {
public:
    static constexpr unsigned kSlots = 288, kChunk = 24, kPlanSlots = 144;
    static_assert(kPlanSlots == DischargeBudget::kSlots, "Trace must cover the entire allocation window");
    void clear() {
        activeCount_ = pendingCount_ = 0; parts_ = 0;
        activeRevision_ = pendingRevision_ = 0;
        pendingPoisoned_ = cacheValid_ = false;
        // Losing a schedule must never replenish the independently measured energy pool.
    }
    const char *receive(const uint8_t *payload, unsigned length, uint32_t utc, uint32_t ms) {
        if (utc < 1704067200UL) return "clock_invalid";
        if (!payload || !length || length > 768) return "packet_size";
        if (std::memchr(payload, 0, length)) return "packet_json";
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
        const uint64_t coverageEnd = uint64_t(start) + uint64_t(count) * 900;
        // Retained recovery keeps original generation/coverage; receipt time is not expiry.
        if (uint64_t(revision) > uint64_t(utc) + 120 || uint64_t(utc) > uint64_t(revision) + 259200 ||
            start % 900 || start > revision || revision - start >= 900 || start > utc ||
            count > kSlots || part >= 12 || coverageEnd > UINT32_MAX ||
            (count && uint64_t(utc) >= coverageEnd)) return "packet_range";
        if (revision < activeRevision_ || revision < pendingRevision_) return "old_revision";
        if (!count) {
            if (part || prices.size()) return "packet_empty";
            if (revision == pendingRevision_ && pendingCount_) {
                pendingPoisoned_ = true; return "packet_conflict";
            }
            if (revision == activeRevision_) return activeCount_ ? "packet_conflict" : "duplicate";
            clear(); activeRevision_ = revision;
            return "withdrawn";
        }
        const unsigned offset = part * kChunk;
        if (count < 2 || offset >= count || prices.size() != std::min(kChunk, count - offset))
            return "packet_count";
        for (JsonVariantConst value : prices)
            if (!value.is<int32_t>() || value.as<int32_t>() < -2000000 || value.as<int32_t>() > 10000000)
                return "packet_price";
        if (revision == activeRevision_) {
            if (start != activeStart_ || count != activeCount_) return "packet_conflict";
            for (unsigned i = 0; i < prices.size(); ++i)
                if (active_[offset + i] != prices[i].as<int32_t>()) return "packet_conflict";
            return "duplicate";
        }
        if (revision == pendingRevision_ && pendingPoisoned_) return "packet_conflict";
        if (revision != pendingRevision_ || uint32_t(ms - pendingAt_) > 30000UL) {
            pendingRevision_ = revision; pendingStart_ = start; pendingCount_ = count;
            parts_ = 0; pendingAt_ = ms; pendingPoisoned_ = false;
        }
        if (start != pendingStart_ || count != pendingCount_) {
            pendingPoisoned_ = true; return "packet_conflict";
        }
        if (parts_ & (1u << part)) {
            for (unsigned i = 0; i < prices.size(); ++i)
                if (pending_[offset + i] != prices[i].as<int32_t>()) {
                    pendingPoisoned_ = true; return "packet_conflict";
                }
        }
        for (unsigned i = 0; i < prices.size(); ++i) pending_[offset + i] = prices[i];
        parts_ |= uint16_t(1u << part);
        const unsigned chunks = (count + kChunk - 1) / kChunk;
        if (parts_ != (1u << chunks) - 1) return "pending";
        std::memcpy(active_, pending_, count * sizeof(active_[0]));
        activeStart_ = start; activeCount_ = count; activeRevision_ = revision; cacheValid_ = false;
        return "accepted";
    }

    FuturePlan plan(uint32_t utc, uint32_t ms, const PreparedSettings &cfg,
                    bool bmsValid, float soc, float batteryW, float fullAh,
                    uint32_t sequence, bool currentPriceValid, float currentPrice, bool currentExpensive,
                    float factor, int maximumW, const PvForecast *pv = nullptr, float qualifiedMaxChargeW = 0, DischargeBudget *trace = nullptr) {
        FuturePlan out;
        if (trace) trace->reset(utc, activeRevision_, pv ? pv->revision() : 0);
        const bool energyValid = bmsValid && std::isfinite(fullAh) && fullAh > 0 && fullAh <= 389 &&
            std::isfinite(soc) && soc >= 0 && soc <= 100 && std::isfinite(batteryW);
        if (!energyValid) {
            energyTickReady_ = false; // Preserve known debits; never integrate an unknown outage.
            out.reason = "energy_missing"; return finish(out, trace);
        }
        const float fullWh = fullAh * 51.38f;
        const float socWh = std::max(0.0f, (soc - cfg.reserveOffSocPercent) * .01f * fullWh);
        if (!energyKnown_) {
            energyWh_ = socWh; energyKnown_ = true;
        } else if (energyTickReady_ && uint32_t(ms - energyAt_) <= kSourceStaleMs) {
            // Every qualified control call debits held signed power; fresh samples update the endpoint.
            const float watts = sequence == energySequence_ ? lastBatteryW_ : (batteryW + lastBatteryW_) * .5f;
            energyWh_ += watts * float(uint32_t(ms - energyAt_)) / 3600000.0f;
        }
        energySequence_ = sequence; energyAt_ = ms; lastBatteryW_ = batteryW; energyTickReady_ = true;
        energyWh_ = std::max(0.0f, std::min(energyWh_, socWh));
        out.availableWh = energyWh_ * kDischargeEfficiency;
        if (!cfg.valid || !std::isfinite(factor) || factor <= 0 || factor > 5) {
            out.reason = "forecast_settings"; return finish(out, trace);
        }
        if (utc < 1704067200UL || (lastUtc_ && uint64_t(utc) + 5 < lastUtc_)) {
            out.reason = "clock_invalid"; clear(); lastUtc_ = utc; return finish(out, trace);
        }
        lastUtc_ = utc;
        if (!activeCount_ || utc < activeStart_ || uint64_t(utc) >= uint64_t(activeStart_) + activeCount_ * 900ULL ||
            uint64_t(utc) > uint64_t(activeRevision_) + 259200) return finish(out, trace);
        const unsigned first = (utc - activeStart_) / 900;
        const unsigned end = std::min(unsigned(activeCount_), first + kPlanSlots);
        if (end <= first + 1) return finish(out, trace);
        if (!currentPriceValid || !std::isfinite(currentPrice)) {
            out.reason = "current_price_missing"; return finish(out, trace);
        }
        // Cap resolution follows the whole-watt forecast wire contract, not arbitrary noise percentages.
        // 12000 W is the maximum encoded offer (10000 W * maximum calibration 1.2), not a plant limit.
        const int chargeCapW = std::isfinite(qualifiedMaxChargeW) && qualifiedMaxChargeW > 0
            ? int(std::min(qualifiedMaxChargeW, 12000.0f)) : 0;
        const uint32_t pvRevision = pv ? pv->revision() : 0;
        const bool pvValid = pv && pv->valid(utc);
        if (!cacheValid_ || cacheFirst_ != first || cachePriceRevision_ != activeRevision_ ||
            cachePv_ != pv || cachePvRevision_ != pvRevision || cachePvValid_ != pvValid ||
            cachePrice_ != currentPrice || cacheFactor_ != factor || cacheMaximumW_ != maximumW ||
            cacheExpensive_ != currentExpensive || cacheChargeCapW_ != chargeCapW ||
            cacheComfort_ != cfg.priceComfortEurPerKwh || cacheDeadband_ != cfg.priceDeadbandEurPerKwh ||
            cacheReserve_ != cfg.reserveOffSocPercent || uint32_t(ms - cacheAt_) >= kReplanMs)
            rebuild(first, end, utc, ms, cfg, currentPrice, currentExpensive, factor, maximumW, pv, chargeCapW);

        const float elapsedHours = float(utc - (activeStart_ + first * 900UL)) / 3600.0f;
        out.offeredPvWh = std::max(0.0f, cacheOfferWh_ - cacheFirstOfferW_ * elapsedHours);
        out.pvQualified = cachePvBeforeTarget_ && chargeCapW > 0;
        float pvBudget = 0;
        if (out.pvQualified && cacheFirstFuture_ < end - first) {
            const float generatedBeforeTarget = std::max(0.0f,
                pvPrefixWh_[cacheFirstFuture_] - cacheFirstStorageW_ * elapsedHours);
            const float headroomWh = std::max(0.0f, (100 - soc) * .01f * fullWh);
            pvBudget = std::min(headroomWh, generatedBeforeTarget) * kDischargeEfficiency;
        }
        const auto price = [&](unsigned i) { return i == first ? currentPrice : float(active_[i]) * .000001f * factor; };
        out.nextPrice = price(first + 1);
        if (trace) { trace->start = activeStart_ + first * 900UL; trace->count = end - first; }
        float pool = out.availableWh, currentAllocation = 0, futureNeed = 0, economicPvCredit = 0;
        uint32_t firstTarget = 0;
        // O(144) bounded arithmetic remains live: current-slot time and measured energy never freeze in the cache.
        for (unsigned j = 0; j < end - first; ++j) {
            const unsigned i = order_[j];
            const float p = price(i);
            if (!eligible(i, first, p, currentPrice, currentExpensive, cfg)) continue;
            const uint32_t start = activeStart_ + i * 900UL;
            const float hours = float(start + 900UL - (i == first ? utc : start)) / 3600.0f;
            const float load = p >= cfg.priceComfortEurPerKwh + 2 * cfg.priceDeadbandEurPerKwh
                ? float(maximumW) : std::min(float(maximumW), float(kPlanningLoadW));
            const float need = std::max(0.0f, load) * hours;
            const float pvCredit = i == first ? 0 : std::min(pvBudget, need);
            pvBudget -= pvCredit; out.usablePvWh += pvCredit;
            const float allocation = std::min(pool, need - pvCredit);
            pool -= allocation;
            if (trace) trace->watts[i - first] = int(std::max(0.0f, std::min(float(maximumW),
                allocation * 3600.0f / float(start + 900UL - (i == first ? utc : start)))));
            if (i == first) currentAllocation = allocation;
            else {
                out.reservedWh += allocation;
                if (p * kRoundTripEfficiency - currentPrice >= kWearEurPerKwh + kMinimumBenefitEurPerKwh) {
                    futureNeed += need; economicPvCredit += pvCredit;
                    if (!firstTarget || start < firstTarget) firstTarget = start;
                }
            }
        }
        const uint32_t slotEnd = activeStart_ + (first + 1) * 900UL;
        out.batteryCeilingW = int(std::max(0.0f, std::min(float(maximumW),
            currentAllocation * 3600.0f / float(slotEnd - utc))));
        const float capacityAc = (100 - cfg.reserveOffSocPercent) * .01f * fullWh * kDischargeEfficiency;
        out.shortfallWh = std::max(0.0f, std::min(capacityAc, futureNeed) - out.availableWh - economicPvCredit);
        out.targetAt = firstTarget;
        if (firstTarget > utc && out.shortfallWh > 0)
            out.chargeTargetW = out.shortfallWh / kDischargeEfficiency * 3600.0f / float(firstTarget - utc);
        out.valid = true;
        out.reason = out.batteryCeilingW > 0 ? "forecast_release" : (out.shortfallWh > 0 ? "forecast_charge" : "forecast_hold");
        return finish(out, trace);
    }
    uint32_t planningBuilds() const { return planningBuilds_; }
    uint32_t revision() const { return activeRevision_; }
    unsigned slots() const { return activeCount_; }
private:
    static FuturePlan finish(FuturePlan out, DischargeBudget *trace) {
        if (trace) trace->finish(out.valid, out.reason);
        return out;
    }
    static bool eligible(unsigned i, unsigned first, float p, float currentPrice,
                         bool expensive, const PreparedSettings &cfg) {
        if (p < 0 || (i == first ? !expensive : p < cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh)) return false;
        return i == first || !expensive || p >= currentPrice + kMinimumBenefitEurPerKwh;
    }
    void rebuild(unsigned first, unsigned end, uint32_t utc, uint32_t ms, const PreparedSettings &cfg,
                 float currentPrice, bool expensive, float factor, int maximumW, const PvForecast *pv, int chargeCapW) {
        cacheFirst_ = first; cachePriceRevision_ = activeRevision_; cachePv_ = pv;
        cachePvRevision_ = pv ? pv->revision() : 0; cachePvValid_ = pv && pv->valid(utc);
        cachePrice_ = currentPrice; cacheFactor_ = factor; cacheMaximumW_ = maximumW; cacheExpensive_ = expensive;
        cacheChargeCapW_ = chargeCapW; cacheComfort_ = cfg.priceComfortEurPerKwh;
        cacheDeadband_ = cfg.priceDeadbandEurPerKwh; cacheReserve_ = cfg.reserveOffSocPercent;
        cacheAt_ = ms; cacheValid_ = true; ++planningBuilds_;
        const unsigned count = end - first;
        const auto price = [&](unsigned i) { return i == first ? currentPrice : float(active_[i]) * .000001f * factor; };
        for (unsigned i = 0; i < count; ++i) order_[i] = uint16_t(first + i);
        std::sort(order_, order_ + count, [&](unsigned a, unsigned b) { return price(a) == price(b) ? a < b : price(a) > price(b); });
        cacheFirstFuture_ = count;
        for (unsigned i = first + 1; i < end; ++i)
            if (eligible(i, first, price(i), currentPrice, expensive, cfg)) { cacheFirstFuture_ = i - first; break; }
        cacheOfferWh_ = cacheFirstOfferW_ = cacheFirstStorageW_ = 0;
        cachePvBeforeTarget_ = cachePvValid_ && cacheFirstFuture_ < count; pvPrefixWh_[0] = 0;
        bool continuousOffer = true;
        constexpr float chargeEfficiency = kRoundTripEfficiency / kDischargeEfficiency;
        for (unsigned j = 0; j < count; ++j) {
            const uint32_t at = activeStart_ + (first + j) * 900UL;
            float watts = 0;
            const bool known = pv && pv->wattsAt(at, watts) && pv->valid(at + 899UL);
            if (j < cacheFirstFuture_) cachePvBeforeTarget_ = cachePvBeforeTarget_ && known;
            const float storageW = known ? std::max(0.0f,
                std::min(watts, float(chargeCapW)) - float(kPlanningLoadW) / kDischargeEfficiency) * chargeEfficiency : 0;
            continuousOffer = continuousOffer && known;
            if (continuousOffer) cacheOfferWh_ += watts * .25f;
            pvPrefixWh_[j + 1] = pvPrefixWh_[j] + storageW * .25f;
            if (!j) { cacheFirstOfferW_ = continuousOffer ? watts : 0; cacheFirstStorageW_ = storageW; }
        }
        // Conservative: only energy already forecast before the earliest reserved target is credited.
        // Later sunshine is deliberately unused here; a single shared credit cannot be spent twice.
    }
    int32_t active_[kSlots] = {}, pending_[kSlots] = {};
    uint32_t activeStart_ = 0, activeRevision_ = 0, pendingStart_ = 0, pendingRevision_ = 0;
    uint32_t pendingAt_ = 0, lastUtc_ = 0;
    unsigned activeCount_ = 0, pendingCount_ = 0;
    uint16_t parts_ = 0;
    bool pendingPoisoned_ = false, energyKnown_ = false, energyTickReady_ = false;
    float energyWh_ = 0, lastBatteryW_ = 0;
    uint32_t energyAt_ = 0, energySequence_ = 0;
    // 288-byte order + 580-byte DC storage prefix replace the old 288-byte sort stack.
    uint16_t order_[kPlanSlots] = {};
    float pvPrefixWh_[kPlanSlots + 1] = {};
    const PvForecast *cachePv_ = nullptr;
    uint32_t cachePriceRevision_ = 0, cachePvRevision_ = 0, cacheAt_ = 0, planningBuilds_ = 0;
    unsigned cacheFirst_ = 0, cacheFirstFuture_ = 0;
    int cacheMaximumW_ = 0, cacheChargeCapW_ = 0;
    float cachePrice_ = 0, cacheFactor_ = 0, cacheComfort_ = 0, cacheDeadband_ = 0, cacheReserve_ = 0;
    float cacheOfferWh_ = 0, cacheFirstOfferW_ = 0, cacheFirstStorageW_ = 0;
    bool cacheValid_ = false, cachePvValid_ = false, cachePvBeforeTarget_ = false, cacheExpensive_ = false;
    JsonArena arena_;
};
} // namespace control
