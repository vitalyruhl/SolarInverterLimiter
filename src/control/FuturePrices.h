#pragma once

#include "PreparedControl.h"
#include "PriceAssessment.h"
#include "RelativeTiers.h"
#include "ApplicationProfile.h"
#include "DischargeBudget.h"
#include "JsonArena.h"
#include "PvForecast.h"
#include "PlanCaptureContext.h"
#include "PlanLifecycleSettings.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace control {
struct FuturePlan {
    bool relativePricePositive=false;
    bool retained = false;
    int directPvCeilingW=0;
    bool valid = false, pvQualified = false, coupledProjection = false;
    float offeredPvWh = 0; // Sum of known DC offers; unknown intervals contribute no invented energy.
    float usablePvWh = 0;  // AC-deliverable energy actually stored in the private chronological projection.
    float projectedUnmetWhDC = 0;
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
private:
    void clearActiveSchedule() {
        projectionCount_ = 0;
#if SIL_RELATIVE_TIME_POLICY
        relativeCheapFull_=relativeWasCheap_=false;
        economicQualified_ = false;
        economicValid_ = false;
#endif
        if (activeCount_) ++contentRevision_;
        activeCount_ = 0;
        activeRevision_ = 0;
        cacheValid_ = false;
        // Losing a schedule must never replenish the independently measured energy pool.
    }
public:
    void clear() {
        clearActiveSchedule();
        pendingCount_ = parts_ = 0;
        pendingRevision_ = 0;
        pendingPoisoned_ = false;
    }
    const char *receive(const uint8_t *payload, unsigned length, uint32_t utc, uint32_t ms,
                        bool originV2 = false, uint32_t freshnessS = 180) {
        if (utc < 1704067200UL) return "clock_invalid";
        if (!payload || !length || length > 768) return "packet_size";
        if (std::memchr(payload, 0, length)) return "packet_json";
        arena_.reset();
        JsonDocument doc(&arena_);
        if (deserializeJson(doc, payload, length)) return "packet_json";
        if (!doc["v"].is<unsigned>() || doc["v"].as<unsigned>() != (originV2 ? 2u : 1u) ||
            !doc["r"].is<uint32_t>() || !doc["t"].is<uint32_t>() ||
            !doc["n"].is<unsigned>() || !doc["k"].is<unsigned>() ||
            !doc["p"].is<JsonArray>()) return "packet_schema";
        if (originV2 && (!doc["d"].is<unsigned>() || doc["d"].as<unsigned>() != 900 ||
            !doc["o"].is<JsonArray>())) return "packet_origin";
        const uint32_t revision = doc["r"], start = doc["t"];
        const unsigned count = doc["n"], part = doc["k"];
        const JsonArrayConst prices = doc["p"].as<JsonArrayConst>();
        const JsonArrayConst origins = doc["o"].as<JsonArrayConst>();
        const uint64_t coverageEnd = uint64_t(start) + uint64_t(count) * 900;
        // Retained recovery keeps original generation/coverage; receipt time is not expiry.
        if (uint64_t(revision) > uint64_t(utc) + 120 || uint64_t(utc) > uint64_t(revision) + 259200 ||
            start % 900 || start > revision || revision - start >= 900 || start > utc ||
            count > kSlots || part >= 12 || coverageEnd > UINT32_MAX ||
            (count && uint64_t(utc) >= coverageEnd)) return "packet_range";
        if (revision < activeRevision_ || (count && revision < pendingRevision_)) return "old_revision";
        if (!count) {
            if (part || prices.size()) return "packet_empty";
            if (revision == pendingRevision_ && pendingCount_) {
                pendingPoisoned_ = true; return "packet_conflict";
            }
            if (revision == activeRevision_) return activeCount_ ? "packet_conflict" : "duplicate";
            if (originV2 && origins.size()) return "packet_origin";
            // Keep newer staged chunks. They convey no authority until the complete schedule arrives.
            if (revision < pendingRevision_) clearActiveSchedule();
            else clear();
            activeRevision_ = revision; activeAcceptedUtc_ = utc; activeVersion_ = originV2 ? 2 : 1;
            return "withdrawn";
        }
        // A newer valid withdrawal may revoke an older plan after its feed has expired.
        if (originV2 && uint64_t(utc) >= uint64_t(revision) + freshnessS) return "test_price_stale";
        const unsigned offset = part * kChunk;
        if (count < 2 || offset >= count || prices.size() != std::min(kChunk, count - offset))
            return "packet_count";
        for (JsonVariantConst value : prices)
            if (!value.is<int32_t>() || value.as<int32_t>() < -2000000 || value.as<int32_t>() > 10000000)
                return "packet_price";
        if (originV2) {
            if (origins.size() != prices.size()) return "packet_origin";
            for (JsonVariantConst value : origins)
                if (!value.is<unsigned>() || value.as<unsigned>() > 2) return "packet_origin";
        }
        if (revision == activeRevision_) {
            if (start != activeStart_ || count != activeCount_) return "packet_conflict";
            for (unsigned i = 0; i < prices.size(); ++i)
                if (active_[offset + i] != prices[i].as<int32_t>() ||
                    origins_[offset + i] != (originV2 ? origins[i].as<uint8_t>() : 0)) return "packet_conflict";
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
                if (pending_[offset + i] != prices[i].as<int32_t>() ||
                    pendingOrigins_[offset + i] != (originV2 ? origins[i].as<uint8_t>() : 0)) {
                    pendingPoisoned_ = true; return "packet_conflict";
                }
        }
        for (unsigned i = 0; i < prices.size(); ++i) {
            pending_[offset + i] = prices[i];
            pendingOrigins_[offset + i] = originV2 ? origins[i].as<uint8_t>() : 0;
        }
        parts_ |= uint16_t(1u << part);
        const unsigned chunks = (count + kChunk - 1) / kChunk;
        if (parts_ != (1u << chunks) - 1) return "pending";
        if (activeStart_ != start || activeCount_ != count || activeVersion_ != (originV2 ? 2 : 1) ||
            std::memcmp(active_, pending_, count * sizeof(active_[0])) ||
            std::memcmp(origins_, pendingOrigins_, count)) ++contentRevision_;
        std::memcpy(active_, pending_, count * sizeof(active_[0]));
        std::memcpy(origins_, pendingOrigins_, count); activeVersion_ = originV2 ? 2 : 1;
        activeStart_ = start; activeCount_ = count; activeRevision_ = revision; activeAcceptedUtc_ = utc; cacheValid_ = false;
        return "accepted";
    }

    FuturePlan plan(uint32_t utc, uint32_t ms, const PreparedSettings &cfg,
                    bool bmsValid, float soc, float batteryW, float fullAh,
                    uint32_t sequence, bool currentPriceValid, float currentPrice, bool currentExpensive,
                    float factor, int maximumW, const PvForecast *pv = nullptr, float qualifiedMaxChargeW = 0, DischargeBudget *trace = nullptr,
                    float currentChargerW = -1, bool currentCheap = false, uint32_t priceFreshnessS = 180) {
#if SIL_RELATIVE_TIME_POLICY
        const uint32_t elapsed = ms - lastObservationMs_;
        lastObservationMs_ = ms;
        if (economicBudget_.valid) {
            ageMs_ += elapsed;
            if (utc >= projectionUtc_) ageMs_ = std::max<uint64_t>(ageMs_, uint64_t(utc - projectionUtc_) * 1000);
            if (degraded_) degradedMs_ += elapsed;
            if (utc < 1704067200UL || (lastUtc_ && uint64_t(utc) + 5 < lastUtc_)) economicQualified_ = false;
        }
#endif
        FuturePlan result = calculate(utc, ms, cfg, bmsValid, soc, batteryW, fullAh, sequence,
            currentPriceValid, currentPrice, currentExpensive, factor, maximumW, pv, qualifiedMaxChargeW,
            trace, currentChargerW, currentCheap, priceFreshnessS);
#if SIL_RELATIVE_TIME_POLICY
        if (economicBudget_.valid) {
            economicValid_ = cfg.relativeTimePolicy && economicQualified_ && utc >= projectionUtc_ &&
                appliedContext_ == relativeContext_ && relativeKey_.prices == contentRevision_ &&
                relativePv_ == pv && relativeKey_.pv == (pv ? pv->contentRevision() : 0) &&
                utc >= economicBudget_.start && (utc - economicBudget_.start) / 900 < economicBudget_.count &&
                ageMs_ < uint64_t(timing_.planMaxAgeS) * 1000;
            if (!result.valid || result.retained) degraded_ = true;
            if (ageMs_ >= uint64_t(timing_.planDiscardAfterS) * 1000 &&
                (!degraded_ || degradedMs_ > uint64_t(timing_.planRetentionS) * 1000)) {
                economicBudget_.reset(0, 0, 0);
                economicValid_ = economicQualified_ = degraded_ = false;
            }
        }
#endif
        return result;
    }
private:
    FuturePlan calculate(uint32_t utc, uint32_t ms, const PreparedSettings &cfg, bool bmsValid,
                         float soc, float batteryW, float fullAh, uint32_t sequence, bool currentPriceValid,
                         float currentPrice, bool currentExpensive, float factor, int maximumW, const PvForecast* pv,
                         float qualifiedMaxChargeW, DischargeBudget* trace, float currentChargerW,
                         bool currentCheap, uint32_t priceFreshnessS) {
        FuturePlan out;
#if SIL_RELATIVE_TIME_POLICY
        const bool previousTickQualified = projectionCount_ == kPlanSlots && relativeActive_;
#endif
        projectionCount_ = 0;
        if (trace) trace->reset(utc, activeRevision_, pv ? pv->revision() : 0);
        const bool energyValid = bmsValid && std::isfinite(fullAh) && fullAh > 0 && fullAh <= 389 &&
            std::isfinite(soc) && soc >= 0 && soc <= 100 && std::isfinite(batteryW);
        if (!energyValid) {
#if SIL_RELATIVE_TIME_POLICY
            relativeCheapFull_=relativeWasCheap_=false;
#endif
            energyTickReady_ = false; // Preserve known debits; never integrate an unknown outage.
            out.reason = "energy_missing"; return finish(out, trace);
        }
        if (!std::isfinite(cfg.reserveOffSocPercent) || !std::isfinite(cfg.reserveOnSocPercent) ||
            cfg.reserveOffSocPercent < 0 || cfg.reserveOnSocPercent > 100 ||
            cfg.reserveOffSocPercent >= cfg.reserveOnSocPercent || (!cfg.relativeTimePolicy && (!std::isfinite(cfg.priceComfortEurPerKwh) ||
            !std::isfinite(cfg.priceDeadbandEurPerKwh) || cfg.priceDeadbandEurPerKwh < 0))) {
            out.reason = "forecast_settings"; return finish(out, trace);
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
#if SIL_RELATIVE_TIME_POLICY
        lastSoc_=soc;
#endif
        if (!cfg.valid || (cfg.relativeTimePolicy ? !validRelativeTiers(cfg) : (!validPriceAssessment(cfg) || !std::isfinite(factor) || factor <= 0 || factor > 5))) {
            out.reason = "forecast_settings"; return finish(out, trace);
        }
        if (utc < 1704067200UL || (lastUtc_ && uint64_t(utc) + 5 < lastUtc_)) {
            out.reason = "clock_invalid"; clear(); lastUtc_ = utc; return finish(out, trace);
        }
        lastUtc_ = utc;
#if SIL_RELATIVE_TIME_POLICY
        if (cfg.relativeTimePolicy && !generationQualified_) {
            out.reason = "sources_missing";
            return finish(out, trace);
        }
#endif
        if (activeVersion_==2 && !priceFresh(utc, priceFreshnessS)) {
            out.reason = testPriceStatus(utc, priceFreshnessS);
#if SIL_RELATIVE_TIME_POLICY
            if (cfg.relativeTimePolicy && currentPriceValid && activeCount_)
                return heldSlot(utc, cfg, currentPrice, maximumW, pv, qualifiedMaxChargeW,
                    currentChargerW, fullWh, out);
#endif
            return finish(out,trace);
        }
        if (!activeCount_ || utc < activeStart_ || uint64_t(utc) >= uint64_t(activeStart_) + activeCount_ * 900ULL ||
            uint64_t(utc) > uint64_t(activeRevision_) + 259200) return finish(out, trace);
        const unsigned first = (utc - activeStart_) / 900;
        const unsigned end = std::min(unsigned(activeCount_), first + kPlanSlots);
        if (end <= first + 1) return finish(out, trace);
        if (!currentPriceValid || !std::isfinite(currentPrice)) {
            out.reason = "current_price_missing"; return finish(out, trace);
        }
#if SIL_RELATIVE_TIME_POLICY
        if(cfg.relativeTimePolicy) {
            if (end-first!=kPlanSlots) {
                out.reason="relative_coverage";
                return heldSlot(utc, cfg, currentPrice, maximumW, pv, qualifiedMaxChargeW,
                    currentChargerW, fullWh, out);
            }
            PlanCaptureContext key;
            key.settings = cfg;
            key.maximumW = maximumW;
            key.currentPrice = currentPrice;
            key.prices = contentRevision_;
            key.pv = pv ? pv->contentRevision() : 0;
            key.pvValid = pv && pv->valid(utc);
            const bool chargePossible = std::isfinite(qualifiedMaxChargeW) && qualifiedMaxChargeW > 0;
            const bool socChanged = cfg.replanSocDeltaPct > 0 && std::fabs(soc - socAtBuild_) >= cfg.replanSocDeltaPct;
            const bool energyChanged = cfg.replanEnergyDeltaWh > 0 &&
                std::fabs(out.availableWh - relativeTotals_.availableWh) >= cfg.replanEnergyDeltaWh;
            const bool pvDeviation = sustainedPvDeviation(ms, utc, cfg, currentChargerW, pv,
                previousTickQualified && relativePv_ == pv && relativeKey_.pv == key.pv);
            const bool softRebuild = cfg.replanMinimumIntervalS && (socChanged || energyChanged || pvDeviation) &&
                ageMs_ >= uint64_t(cfg.replanMinimumIntervalS) * 1000;
            const bool priorityChanged = (soc < cfg.chargePriorityThreshold) != (socAtBuild_ < cfg.chargePriorityThreshold);
            const bool reserveChanged = (soc <= cfg.reserveOffSocPercent) != (socAtBuild_ <= cfg.reserveOffSocPercent) ||
                (soc >= cfg.reserveOnSocPercent) != (socAtBuild_ >= cfg.reserveOnSocPercent);
            const bool rebuildDue = forceRebuild_ || !previousTickQualified || !economicBudget_.valid ||
                projectionStart_ != activeStart_ + first * 900UL || !relativeKey_.same(key) ||
                relativeFullWh_ != fullWh || relativeChargePossible_ != chargePossible ||
                relativePv_ != pv || utc < projectionUtc_ || ageMs_ >= uint64_t(timing_.planRecalcIntervalS) * 1000 ||
                relativeContext_ != appliedContext_ || (soc >= 100 && socAtBuild_ < 100) ||
                priorityChanged || reserveChanged || softRebuild;
            FuturePlan result = relativeProject(first, utc, cfg, currentPrice, maximumW, pv, qualifiedMaxChargeW,
                currentChargerW, fullWh, out, rebuildDue);
            if (result.valid && rebuildDue) {
                relativeKey_ = key;
                relativeFullWh_ = fullWh;
                socAtBuild_ = soc;
                if (pvDeviation) pvDeviationApplied_ = true;
                relativeChargePossible_ = chargePossible;
                relativePv_ = pv;
                ageMs_ = degradedMs_ = 0;
                degraded_ = false;
                appliedContext_ = relativeContext_;
                relativeTotals_ = result;
                economicQualified_ = generationQualified_;
            }
            if (trace && result.valid) *trace = economicBudget_;
            return result;
        }
        relativeActive_ = false;
#endif
        // Cap resolution follows the whole-watt forecast wire contract, not arbitrary noise percentages.
        // 12000 W is the maximum encoded offer (10000 W * maximum calibration 1.2), not a plant limit.
        const int chargeCapW = std::isfinite(qualifiedMaxChargeW) && qualifiedMaxChargeW > 0
            ? int(std::min(qualifiedMaxChargeW, 12000.0f)) : 0;
        const uint32_t pvRevision = pv ? pv->revision() : 0;
        const bool pvValid = pv && pv->valid(utc);
        const float capacityAc = (100 - cfg.reserveOffSocPercent) * .01f * fullWh * kDischargeEfficiency;
        const float initialReserveDeficitDc = std::max(0.0f, (cfg.reserveOffSocPercent - soc) * .01f * fullWh);
        if (!cacheValid_ || cacheFirst_ != first || cachePriceRevision_ != activeRevision_ ||
            cachePv_ != pv || cachePvRevision_ != pvRevision || cachePvValid_ != pvValid ||
            cachePrice_ != currentPrice || cacheFactor_ != factor || cacheMaximumW_ != maximumW ||
            cacheChargeCapW_ != chargeCapW || cacheCapacityAc_ != capacityAc ||
            cacheReserveDebtAc_ != initialReserveDeficitDc * kDischargeEfficiency ||
            cacheReserve_ != cfg.reserveOffSocPercent || cacheTolerance_ != cfg.deferralToleranceEurKwh ||
            cachePremium12h_ != cfg.predictionPremium12hPct || cachePremium24h_ != cfg.predictionPremium24hPct ||
            cachePremium_ != cfg.predictionPremiumPct || uint32_t(ms - cacheAt_) >= kReplanMs)
            rebuild(first, end, utc, ms, cfg, currentPrice, factor, maximumW, pv, chargeCapW, capacityAc,
                initialReserveDeficitDc * kDischargeEfficiency);

        const float elapsedHours = float(utc - (activeStart_ + first * 900UL)) / 3600.0f;
        out.offeredPvWh = std::max(0.0f, cacheOfferWh_ - cacheFirstOfferW_ * elapsedHours);
        out.pvQualified = cachePvValid_ && chargeCapW > 0;
        const auto price = [&](unsigned i) { return i == first ? currentPrice : float(active_[i]) * .000001f * factor; };
        out.nextPrice = price(first + 1);
        if (trace) { trace->start = activeStart_ + first * 900UL; trace->count = end - first; }
        project(trace, first, end, utc, factor, maximumW, out, capacityAc,
            std::max(0.0f, (100 - soc) * .01f * fullWh),
            initialReserveDeficitDc,
            fullWh, cfg, currentExpensive, currentCheap, currentChargerW, chargeCapW);
        out.valid = true;
        out.reason = out.batteryCeilingW > 0 ? "forecast_release" : (out.shortfallWh > 0 ? "forecast_charge" : "forecast_hold");
        return finish(out, trace);
    }
public:
    bool priceFresh(uint32_t utc, uint32_t freshnessS = 180) const {
        return activeCount_ && utc>=activeStart_ && uint64_t(utc)<uint64_t(activeStart_)+activeCount_*900ULL &&
            (activeVersion_!=2 || (uint64_t(utc)<uint64_t(activeRevision_)+freshnessS && uint64_t(utc)<uint64_t(activeAcceptedUtc_)+freshnessS));
    }
    const char* testPriceStatus(uint32_t utc, uint32_t freshnessS = 180) const {
        if (!activeCount_) return activeVersion_==2 && activeRevision_ ? "test_price_withdrawn" : "test_price_missing";
        return priceFresh(utc, freshnessS) ? "ok" : "test_price_stale";
    }
    unsigned inputVersion() const { return activeVersion_; }
    uint32_t start() const { return activeStart_; }
    unsigned originAt(uint32_t utc) const {
        return utc>=activeStart_ && uint64_t(utc)<uint64_t(activeStart_)+activeCount_*900ULL
            ? origins_[(utc-activeStart_)/900] : 0;
    }
    bool assessedPriceAt(uint32_t utc,float factor,const PreparedSettings& cfg,float& value) const {
        if ((cfg.relativeTimePolicy?!validRelativeTiers(cfg):!validPriceAssessment(cfg)) || !priceAt(utc,factor,value)) return false;
        value=assessedPrice(value,PriceOrigin(originAt(utc)),cfg.predictionPremium12hPct);
        return std::isfinite(value);
    }
    uint32_t planningBuilds() const { return planningBuilds_; }
    void setProjectionTiming(const PlanLifecycleSettings &settings, uint32_t context,
                             bool qualified, bool liveReady, bool forceRebuild, bool pvMeasured = true) {
#if SIL_RELATIVE_TIME_POLICY
        timing_ = settings;
        relativeContext_ = context;
        generationQualified_ = qualified;
        liveReady_ = liveReady;
        forceRebuild_ = forceRebuild;
        pvMeasured_ = pvMeasured;
#else
        (void)settings; (void)context; (void)qualified; (void)liveReady; (void)forceRebuild; (void)pvMeasured;
#endif
    }
    void copyEconomicBudget(DischargeBudget &trace) const {
#if SIL_RELATIVE_TIME_POLICY
        trace = economicBudget_;
#else
        (void)trace;
#endif
    }
#if SIL_RELATIVE_TIME_POLICY
    bool economicAvailable() const { return economicBudget_.valid; }
    bool economicRetained() const { return economicAvailable() && degraded_; }
    bool economicStale() const { return economicAvailable() && economicAgeS() >= timing_.planStaleAfterS; }
    bool economicValid() const { return economicValid_; }
    uint32_t economicGeneratedAt() const { return economicAvailable() ? economicBudget_.generatedAt : 0; }
    uint32_t economicAgeS() const { return economicAvailable() ? uint32_t(std::min<uint64_t>(UINT32_MAX, ageMs_ / 1000)) : 0; }
#else
    bool economicAvailable() const { return false; }
    bool economicRetained() const { return false; }
    bool economicStale() const { return false; }
    bool economicValid() const { return false; }
    uint32_t economicGeneratedAt() const { return 0; }
    uint32_t economicAgeS() const { return 0; }
#endif
    uint32_t revision() const { return activeRevision_; }
    uint32_t contentRevision() const { return contentRevision_; }
    unsigned slots() const { return activeCount_; }
    // Diagnostic lookup only; never advances the measured-energy ledger or planner cache.
    bool priceAt(uint32_t utc, float factor, float &price) const {
        if (!activeCount_ || !std::isfinite(factor) || factor <= 0 || factor > 5 ||
            utc < activeStart_ || uint64_t(utc) >= uint64_t(activeStart_) + activeCount_ * 900ULL ||
            uint64_t(utc) > uint64_t(activeRevision_) + 259200ULL) return false;
        price = float(active_[(utc - activeStart_) / 900UL]) * .000001f * factor;
        return std::isfinite(price);
    }
    const char* currentPriceTier()const {
#if SIL_RELATIVE_TIME_POLICY
        return priceTierName(relative_.tier(liveTierIndex_));
#else
        return "unknown";
#endif
    }
    unsigned tierCount(PriceTier tier)const {
#if SIL_RELATIVE_TIME_POLICY
        return relative_.count(tier);
#else
        (void)tier;return 0;
#endif
    }
    double tierBoundary(PriceTier tier)const {
#if SIL_RELATIVE_TIME_POLICY
        return relative_.boundary(tier);
#else
        (void)tier;return NAN;
#endif
    }
    uint32_t tierStart()const{return projectionStart_;}
    uint32_t tierAt()const{return projectionUtc_;}
    bool projectionMatches(uint32_t utc, uint32_t start, unsigned count) const {
        return projectionCount_ && projectionUtc_ == utc && projectionStart_ == start && projectionCount_ == count;
    }
    float projectedChargeW(unsigned i) const { return i < projectionCount_ ? projectedChargeW_[i] : 0; }
    int32_t projectedSolarW(unsigned i) const {
        if (i >= projectionCount_) return -1;
        return !i ? int32_t(projectionFirstSolarW_) : (storageKnown_[i] ? int32_t(solarW_[i]) : -1);
    }
    uint32_t planningSteps() const { return planningSteps_; }
private:
    static FuturePlan finish(FuturePlan out, DischargeBudget *trace) {
        if (trace) trace->finish(out.valid, out.reason);
        return out;
    }
#if SIL_RELATIVE_TIME_POLICY
    bool sustainedPvDeviation(uint32_t ms, uint32_t utc, const PreparedSettings& cfg, float measuredW,
                              const PvForecast* forecast, bool continuous) {
        float predictedW = 0;
        const bool comparable = pvMeasured_ && std::isfinite(measuredW) && measuredW >= 0 &&
            cfg.replanPvDeviationW > 0 && cfg.replanPvDeviationDurationS && forecast && forecast->wattsAt(utc, predictedW);
        const float difference = measuredW - predictedW;
        const int direction = comparable && std::fabs(difference) >= cfg.replanPvDeviationW ? (difference > 0 ? 1 : -1) : 0;
        if (!direction || direction != pvDeviationDirection_ || !continuous || uint32_t(ms - pvObservedMs_) > kSourceStaleMs) {
            pvDeviationDirection_ = direction;
            pvDeviationAtMs_ = ms;
            pvDeviationApplied_ = false;
        }
        pvObservedMs_ = ms;
        return direction && !pvDeviationApplied_ &&
            uint32_t(ms - pvDeviationAtMs_) >= uint64_t(cfg.replanPvDeviationDurationS) * 1000;
    }
    FuturePlan heldSlot(uint32_t utc, const PreparedSettings& cfg, float currentPrice, int maximumW,
                        const PvForecast* pv, float chargeCap, float solarW, float fullWh, FuturePlan out) {
        if (!relativeActive_ || !economicBudget_.valid || !economicQualified_ || !liveReady_ ||
            !generationQualified_ || appliedContext_ != relativeContext_ || !std::isfinite(currentPrice) ||
            currentPrice <= 0 || utc < projectionUtc_ || ageMs_ >= uint64_t(timing_.planMaxAgeS) * 1000 ||
            utc < economicBudget_.start || relativeKey_.prices != contentRevision_ ||
            relativePv_ != pv || relativeKey_.pv != (pv ? pv->contentRevision() : 0) ||
            relativeKey_.pvValid != bool(pv && pv->valid(utc)) ||
            fullWh != relativeFullWh_ || maximumW != relativeKey_.maximumW || maximumW < 0 ||
            !std::isfinite(solarW) || solarW < 0 || !std::isfinite(chargeCap) || chargeCap < 0 ||
            relativeChargePossible_ != (chargeCap > 0)) return out;
        PlanCaptureContext key = relativeKey_;
        key.settings = cfg;
        if (!key.sameHard(relativeKey_)) return out;
        const uint32_t elapsed = utc - economicBudget_.start;
        const unsigned slot = elapsed / 900;
        float rawPrice = 0;
        if (slot >= economicBudget_.count || !priceAt(utc, 1, rawPrice) || rawPrice <= 0) return out;
        const float remainingHours = float(900 - elapsed % 900) / 3600.0f;
        const float storeW = std::min({projectedChargeW_[slot], chargeCap, solarW * float(kRelativeChargeEfficiency)});
        // A held allocation cannot create energy or exceed the current inverter limit.
        out.directPvCeilingW = int(std::clamp((solarW - storeW / float(kRelativeChargeEfficiency)) *
                                            kDischargeEfficiency, 0.0f, float(maximumW)));
        out.batteryCeilingW = int(std::min({float(economicBudget_.watts[slot]),
            out.availableWh / remainingHours, float(maximumW - out.directPvCeilingW)}));
        out.chargeTargetW = storeW;
        out.valid = out.retained = out.relativePricePositive = true;
        out.reason = "retained_plan";
        liveTierIndex_ = slot;
        return out;
    }
    FuturePlan relativeProject(unsigned first,uint32_t utc,const PreparedSettings& cfg,float currentPrice,int maximumW,
        const PvForecast* pv,float qualifiedChargeW,float currentSolarW,float fullWh,FuturePlan out,bool rebuildDue) {
        const float maximum=std::max(0.0f,float(maximumW));
        const float chargeCap=std::isfinite(qualifiedChargeW)?std::max(0.0f,std::min(qualifiedChargeW,12000.0f)):0;
        liveTierIndex_ = 0;
        if (rebuildDue) {
            projectionStart_=activeStart_+first*900UL;projectionUtc_=utc;
            economicBudget_.reset(utc, activeRevision_, pv ? pv->revision() : 0);
            economicBudget_.start = projectionStart_;
            economicBudget_.count = kPlanSlots;
            for(unsigned k=0;k<kPlanSlots;++k){
                const double micro=double(active_[first+k]);
                const double assessed=PriceOrigin(origins_[first+k])==PriceOrigin::Forecast?
                    micro+std::fabs(micro)*double(forecastPremium(cfg,utc,projectionStart_+k*900UL))/100:micro;
                relative_.set(k,assessed*.000001);
            }
            if(!relative_.classify(cfg)){out.reason="relative_settings";return out;}
            ++planningBuilds_;
            highHoursAtBuild_=mediumHoursAtBuild_=0;
            for(unsigned k=0;k<kPlanSlots;++k){
                const float hours=float(projectionStart_+(k+1)*900UL-(k?projectionStart_+k*900UL:utc))/3600;
                // A stopped raw-price interval has no demand to reserve or spread over.
                if(active_[first+k]<=0||(!k&&currentPrice<=0))continue;
                if(relative_.tier(k)==PriceTier::VeryExpensive)highHoursAtBuild_+=hours;
                if(relative_.tier(k)==PriceTier::Expensive)mediumHoursAtBuild_+=hours;
            }
        }
        float highHours = highHoursAtBuild_, mediumHours = mediumHoursAtBuild_;
        if (active_[first] > 0 && currentPrice > 0) {
            const float elapsedHours = float(utc - projectionUtc_) / 3600.0f;
            if (relative_.tier(0) == PriceTier::VeryExpensive) highHours = std::max(0.0f, highHours - elapsedHours);
            if (relative_.tier(0) == PriceTier::Expensive) mediumHours = std::max(0.0f, mediumHours - elapsedHours);
        }
        float pool=out.availableWh;
        const float capacity=std::max(0.0f,(100-cfg.reserveOffSocPercent)*.01f*fullWh*kDischargeEfficiency);
        float headroom=std::max(0.0f,(100-lastSoc_)*.01f*fullWh);
        float debt=std::max(0.0f,(cfg.reserveOffSocPercent-lastSoc_)*.01f*fullWh);
        const bool continuous=relativeWasCheap_&&utc>=relativeLiveAt_&&utc-relativeLiveAt_<=180;
        bool cheapFull=continuous&&relativeCheapFull_,previousCheap=continuous;
        const bool pvCurrentValid=pv&&pv->valid(utc);
        out.offeredPvWh=0;out.pvQualified=pvCurrentValid&&chargeCap>0;
        // Every tick evaluates the live slot against measured energy and PV. The
        // remaining 143 allocations and the published generation change only on rebuild.
        const unsigned slotsToEvaluate = rebuildDue ? kPlanSlots : 1;
        for(unsigned k=0;k<slotsToEvaluate;++k){
            const uint32_t start=projectionStart_+k*900UL;
            const float hours=float(start+900UL-(k?start:utc))/3600;
            const auto tier=relative_.tier(k);
            const bool cheap=tier==PriceTier::Cheap;
            if(!cheap||!previousCheap)cheapFull=false;
            previousCheap=cheap;
            float forecast=0;
            const bool known=pvCurrentValid&&pv->valid(start+899)&&pv->wattsAt(start,forecast);
            if(known)out.offeredPvWh+=forecast*hours;
            const bool measured=!k&&std::isfinite(currentSolarW)&&currentSolarW>=0;
            const float solar=measured?std::min(currentSolarW,12000.0f):(k&&known?std::min(forecast,chargeCap/float(kRelativeChargeEfficiency)):0);
            if (rebuildDue) {
                storageKnown_[k]=measured||(k&&known);solarW_[k]=solar;
                if(!k)projectionFirstSolarW_=storageKnown_[0]?solar:-1;
            }
            const float raw=float(active_[first+k])*.000001f;
            const bool positive=raw>0&&(k||currentPrice>0);
            const float physicalSoc=fullWh>0?(fullWh-headroom)/fullWh*100:0;
            if(cheap){if(physicalSoc<cfg.chargePriorityThreshold)cheapFull=false;if(physicalSoc>=100)cheapFull=true;}
            // Persist only the real current window, never a full SOC simulated later in the forecast.
            if(!k){relativeWasCheap_=cheap;relativeCheapFull_=cheapFull;relativeLiveAt_=utc;}
            float desiredStore=0;
            if(positive){
                // Only otherwise curtailed input and physical reserve restoration bypass acquisition economics.
                desiredStore=std::max(0.0f,solar-maximum/kDischargeEfficiency);
                desiredStore=std::max(desiredStore,std::min(solar,debt/float(kRelativeChargeEfficiency)/hours));
                float eligibleAc=0;
                const float highFunded=highHours>0?std::min(maximum,pool/highHours):0;
                const float mediumFunded=mediumHours>0?std::min(maximum,std::max(0.0f,pool-maximum*highHours)/mediumHours):0;
                double highMinimum=NAN,mediumMinimum=NAN;
                float highDeficit=0,mediumDeficit=0;
                for(unsigned j=k+1;j<kPlanSlots;++j){
                    const auto targetTier=relative_.tier(j);
                    if(active_[first+j]<=0||targetTier<=tier||targetTier<PriceTier::Expensive)continue;
                    const double valuation=relative_.assessed(j);
                    float targetPv=0;
                    const uint32_t targetAt=projectionStart_+j*900UL;
                    const bool targetKnown=pvCurrentValid&&pv->valid(targetAt+899)&&pv->wattsAt(targetAt,targetPv);
                    const float targetSolar=targetKnown?std::min(targetPv,chargeCap/float(kRelativeChargeEfficiency)):0;
                    const float possibleBattery=std::max(0.0f,maximum-targetSolar*kDischargeEfficiency);
                    if(targetTier==PriceTier::VeryExpensive){
                        highMinimum=std::isfinite(highMinimum)?std::min(highMinimum,valuation):valuation;
                        highDeficit+=std::max(0.0f,possibleBattery-highFunded)*.25f;
                    }else{
                        mediumMinimum=std::isfinite(mediumMinimum)?std::min(mediumMinimum,valuation):valuation;
                        mediumDeficit+=std::max(0.0f,possibleBattery-mediumFunded)*.25f;
                    }
                }
                // A conservative acquisition hurdle, not a promise of future realized profit.
                // The minimum tier value survives uniform spreading and direct-PV caps on an isolated peak.
                const bool highProfitable=std::isfinite(highMinimum)&&relativeStorageProfitable(highMinimum,
                    relative_.assessed(k),cfg.storageMinimumNetAdvantageEurKwh);
                if(highProfitable)eligibleAc=highDeficit;
                if((highDeficit<=0||highProfitable)&&std::isfinite(mediumMinimum)&&
                    relativeStorageProfitable(mediumMinimum,relative_.assessed(k),cfg.storageMinimumNetAdvantageEurKwh))
                    eligibleAc+=mediumDeficit;
                const float economicPv=eligibleAc>0?
                    std::min(solar,(debt+eligibleAc/kDischargeEfficiency)/float(kRelativeChargeEfficiency)/hours):0;
                if(tier==PriceTier::Neutral||tier==PriceTier::Expensive)desiredStore=std::max(desiredStore,economicPv);
                if(cheap&&!cheapFull){
                    const float share=physicalSoc<cfg.chargePriorityThreshold?0:float(cfg.chargeInverterSharePct)/100;
                    desiredStore=std::max(desiredStore,std::min(economicPv,solar*(1-share)));
                }
            }
            // Charge power/headroom are battery DC. The diverted PV input includes charge losses.
            float stored=std::max(0.0f,std::min({desiredStore*hours*float(kRelativeChargeEfficiency),chargeCap*hours,headroom,
                debt+std::max(0.0f,capacity-pool)/kDischargeEfficiency}));
            float allocation=0;
            // A charging slot never also discharges. Direct PV is exactly the non-stored part.
            const float direct=positive?std::min(maximum,std::max(0.0f,solar-stored/float(kRelativeChargeEfficiency)/hours)*kDischargeEfficiency):0;
            if(positive&&stored<=0&&debt<=0){
                float level=0;
                if(tier==PriceTier::VeryExpensive&&highHours>0)level=std::min(maximum,pool/highHours);
                if(tier==PriceTier::Expensive&&mediumHours>0)
                    level=std::min(maximum,std::max(0.0f,pool-maximum*highHours)/mediumHours);
                allocation=std::min({pool,std::max(0.0f,maximum-direct)*hours,level*hours});
            }
            pool=std::max(0.0f,pool-allocation);headroom+=allocation/kDischargeEfficiency;
            const float restored=std::min(debt,stored);debt=std::max(0.0f,debt-restored);
            pool=std::min(capacity,pool+(stored-restored)*kDischargeEfficiency);headroom=std::max(0.0f,headroom-stored);
            if (rebuildDue) projectedChargeW_[k]=stored/hours;
            out.usablePvWh+=(stored-restored)*kDischargeEfficiency;
            const int watts=int(std::max(0.0f,std::min(maximum,allocation/hours)));
            if (rebuildDue) economicBudget_.watts[k]=watts;
            if(!k){out.batteryCeilingW=watts;out.chargeTargetW=stored/hours;out.directPvCeilingW=int(direct);
                out.relativePricePositive=positive;out.reservedWh=std::min(pool,maximum*highHours);
                out.shortfallWh=std::max(0.0f,maximum*highHours-pool);out.projectedUnmetWhDC=out.shortfallWh/kDischargeEfficiency;}
            if(positive&&tier==PriceTier::VeryExpensive)highHours=std::max(0.0f,highHours-hours);
            if(positive&&tier==PriceTier::Expensive)mediumHours=std::max(0.0f,mediumHours-hours);
        }
        projectionCount_=kPlanSlots;out.valid=true;out.coupledProjection=true;
        out.reason=out.batteryCeilingW>0?"relative_release":(out.chargeTargetW>0?"relative_charge":"relative_hold");
        if (rebuildDue) economicBudget_.finish(true, out.reason);
        else {
            out.offeredPvWh = relativeTotals_.offeredPvWh;
            out.usablePvWh = relativeTotals_.usablePvWh;
        }
        relativeActive_ = true;
        return out;
    }
    RelativeTiers relative_;
    DischargeBudget economicBudget_;
    PlanCaptureContext relativeKey_;
    FuturePlan relativeTotals_;
    const PvForecast *relativePv_ = nullptr;
    float highHoursAtBuild_ = 0, mediumHoursAtBuild_ = 0, relativeFullWh_ = 0, socAtBuild_ = 0;
    PlanLifecycleSettings timing_;
    uint32_t relativeContext_ = 0, appliedContext_ = 0, lastObservationMs_ = 0;
    uint64_t ageMs_ = 0, degradedMs_ = 0;
    bool degraded_ = false, economicValid_ = false;
    unsigned liveTierIndex_ = 0;
    bool economicQualified_ = false, generationQualified_ = true, liveReady_ = false, forceRebuild_ = false;
    uint32_t pvObservedMs_ = 0, pvDeviationAtMs_ = 0;
    int pvDeviationDirection_ = 0;
    bool pvDeviationApplied_ = false, pvMeasured_ = true;
    bool relativeChargePossible_ = false, relativeActive_ = false;
    float lastSoc_=0;
    bool relativeCheapFull_=false,relativeWasCheap_=false;
    uint32_t relativeLiveAt_=0;
#endif
    static bool eligible(unsigned i, unsigned first, float p, float currentPrice, float tolerance) {
        // Held energy has already paid acquisition losses/cycle costs. Defer its
        // current use only for a materially better future price; flat/cheaper slots cannot hoard it.
        return p > 0 && (i == first || higherValue(p, currentPrice, tolerance));
    }
    void project(DischargeBudget *trace, unsigned first, unsigned end, uint32_t utc,
                 float factor, int maximumW, FuturePlan &out, float capacityAc, float headroomDc,
                 float reserveDeficitDc, float fullWh, const PreparedSettings &cfg, bool currentExpensive, bool currentCheap,
                 float currentChargerW, int chargeCapW) {
        const unsigned count = end - first;
        const float maximum = std::max(0.0f, float(maximumW));
        float pool = out.availableWh;
        int band = currentExpensive ? 2 : (currentCheap ||
            cachePrice_ <= cfg.priceComfortEurPerKwh - cfg.priceDeadbandEurPerKwh ? 0 : 1);
        projectionStart_ = activeStart_ + first * 900UL; projectionUtc_ = utc;
        // One private physical ledger: a slot either charges or discharges the battery.
        // Unknown PV grants no inflow, but cannot erase later independently known sunlight.
        for (unsigned k = 0; k < count; ++k) {
            const uint32_t start = projectionStart_ + k * 900UL;
            const float hours = float(start + 900UL - (k ? start : utc)) / 3600.0f;
            const float price = k ? float(active_[first + k]) * .000001f * factor : cachePrice_;
            if (k) {
                const float high = cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh;
                const float low = cfg.priceComfortEurPerKwh - cfg.priceDeadbandEurPerKwh;
                const float hysteresis = cfg.priceDeadbandEurPerKwh / 3.0f;
                if (!(band == 2 && price > high - hysteresis) && !(band == 0 && price < low + hysteresis))
                    band = price >= high ? 2 : (price <= low ? 0 : 1);
            }
            const bool measured = !k && std::isfinite(currentChargerW) && currentChargerW >= 0;
            const bool legacyOffer = k || currentChargerW == -1;
            const float solarW = measured ? std::min(currentChargerW, 12000.0f) :
                (legacyOffer ? std::min(solarW_[k], float(chargeCapW)) : 0);
            if (!k) projectionFirstSolarW_ = measured ? std::min(currentChargerW, 12000.0f) :
                (legacyOffer && storageKnown_[0] ? solarW_[0] : -1);
            const float solarDc = std::max(0.0f, solarW) * hours;
            const float restoredDebtAc = std::max(0.0f, cacheReserveDebtAc_ - reserveDeficitDc * kDischargeEfficiency);
            const float economicMissing = std::max(0.0f,
                economicWh_[k] - std::min(restoredDebtAc, economicDebtWh_[k]) - pool);
            const bool backwardDebt = cacheDebtTarget_ < count && k < cacheDebtTarget_;
            float desiredDc = price >= 0 && economicMissing > 0 ? economicMissing / kDischargeEfficiency +
                (backwardDebt ? 0 : reserveDeficitDc) : 0;
            const float physicalDc = fullWh - headroomDc;
            const float soc = fullWh > 0 ? physicalDc / fullWh * 100 : 0;
            const float reserveDc = soc <= cfg.reserveOffSocPercent && band != 2 ? solarDc :
                (soc > cfg.reserveOffSocPercent && soc < cfg.reserveOnSocPercent && band == 0 ? solarDc * .5f : 0);
            desiredDc = std::max(desiredDc, reserveDc);
            // Surplus beyond the AC output limit can charge without foregoing direct PV use.
            if (price >= 0) desiredDc = std::max(desiredDc, std::max(0.0f, solarDc - maximum * hours / kDischargeEfficiency));
            else desiredDc = 0;
            float storedDc = std::max(0.0f, std::min({desiredDc, solarDc, float(chargeCapW) * hours, headroomDc,
                reserveDeficitDc + std::max(0.0f, capacityAc - pool) / kDischargeEfficiency}));
            float allocation = 0, allocatedW = 0;
            if (storedDc <= 0 && price > 0 && reserveDeficitDc <= 0) {
                const float batteryLimit = std::max(0.0f, maximum * hours - solarDc * kDischargeEfficiency);
                allocation = std::min(batteryLimit, std::max(0.0f,
                    pool - std::max(0.0f, protectedWh_[k] - std::min(restoredDebtAc, protectedDebtWh_[k]))));
                // A fully funded power cap is already known; a Wh/W round trip
                // can turn exact MAX into MAX-1 through floating-point truncation.
                allocatedW = allocation == batteryLimit
                    ? std::max(0.0f, maximum - solarW * kDischargeEfficiency)
                    : allocation / hours;
            }
            pool = std::max(0.0f, pool - allocation);
            headroomDc += allocation / kDischargeEfficiency;
            const float restoredDc = std::min(reserveDeficitDc, storedDc);
            reserveDeficitDc = std::max(0.0f, reserveDeficitDc - restoredDc);
            pool = std::min(capacityAc, pool + (storedDc - restoredDc) * kDischargeEfficiency);
            headroomDc = std::max(0.0f, headroomDc - storedDc);
            projectedChargeW_[k] = storedDc / hours;
            out.usablePvWh += (storedDc - restoredDc) * kDischargeEfficiency;
            const int watts = int(std::max(0.0f, std::min(maximum, allocatedW)));
            if (trace) trace->watts[k] = watts;
            if (!k) {
                out.batteryCeilingW = watts; out.chargeTargetW = storedDc / hours;
                out.reservedWh = std::min(std::max(0.0f, out.availableWh - allocation), protectedWh_[0]);
                out.shortfallWh = economicMissing;
                out.projectedUnmetWhDC = std::max(0.0f, economicMissing / kDischargeEfficiency -
                    (backwardDebt ? storedDc : storedDc - restoredDc));
                out.targetAt = targetIndex_[0] < count ? projectionStart_ + targetIndex_[0] * 900UL : 0;
            }
        }
        projectionCount_ = count; out.coupledProjection = true;
    }
    void rebuild(unsigned first, unsigned end, uint32_t utc, uint32_t ms, const PreparedSettings &cfg,
                 float currentPrice, float factor, int maximumW, const PvForecast *pv, int chargeCapW,
                 float capacityAc, float reserveDebtAc) {
        cacheFirst_ = first; cachePriceRevision_ = activeRevision_; cachePv_ = pv;
        cachePvRevision_ = pv ? pv->revision() : 0; cachePvValid_ = pv && pv->valid(utc);
        cachePrice_ = currentPrice; cacheFactor_ = factor; cacheMaximumW_ = maximumW;
        cacheChargeCapW_ = chargeCapW; cacheReserve_ = cfg.reserveOffSocPercent; cacheCapacityAc_ = capacityAc;
        cacheReserveDebtAc_ = reserveDebtAc;
        cacheTolerance_ = cfg.deferralToleranceEurKwh; cachePremium12h_ = cfg.predictionPremium12hPct;
        cachePremium24h_ = cfg.predictionPremium24hPct; cachePremium_ = cfg.predictionPremiumPct;
        cacheAt_ = ms; cacheValid_ = true; ++planningBuilds_;
        const unsigned count = end - first;
        const auto rawPrice = [&](unsigned i) { return i == first ? currentPrice : float(active_[i]) * .000001f * factor; };
        const auto price = [&](unsigned i) { return assessedPrice(rawPrice(i), PriceOrigin(origins_[i]),
            forecastPremium(cfg,utc,activeStart_+i*900UL)); };
        for (unsigned i = 0; i < count; ++i) order_[i] = uint16_t(first + i);
        std::sort(order_, order_ + count, [&](unsigned a, unsigned b) { return price(a) == price(b) ? a < b : price(a) > price(b); });
        cacheOfferWh_ = cacheFirstOfferW_ = 0;
        for (unsigned j = 0; j < count; ++j) {
            const uint32_t at = activeStart_ + (first + j) * 900UL;
            float watts = 0;
            const bool known = cachePvValid_ && pv->wattsAt(at, watts) && pv->valid(at + 899UL);
            storageKnown_[j] = known;
            solarW_[j] = known ? watts : 0;
            if (known) cacheOfferWh_ += watts * .25f;
            if (!j) cacheFirstOfferW_ = known ? watts : 0;
        }
        std::fill(pendingWh_, pendingWh_ + count, 0);
        // The initial reserve debt is a priced prerequisite of one real future battery
        // target. It consumes PV but is never usable inventory or pruned as usable capacity.
        cacheDebtTarget_ = count;
        if (reserveDebtAc > 0) for (unsigned rank = 0; rank < count; ++rank) {
            const unsigned j = order_[rank] - first;
            const float direct = std::min(solarW_[j], float(chargeCapW)) * kDischargeEfficiency;
            if (j && price(first + j) > 0 && float(maximumW) > direct) {
                cacheDebtTarget_ = j; break;
            }
        }
        float pendingDebt = 0;
        planningSteps_ = 0;
        // Backward marginal demand, capped by physical storage. The strict price order is
        // transitive; the 3-cent deferral gate is applied only when measuring protected demand.
        for (unsigned reverse = count; reverse > 0; --reverse) {
            const unsigned k = reverse - 1;
            const float current = price(first + k);
            protectedWh_[k] = economicWh_[k] = protectedDebtWh_[k] = economicDebtWh_[k] = 0;
            targetIndex_[k] = uint16_t(count);
            const float solarAc = rawPrice(first + k) >= 0 ? std::min(solarW_[k], float(chargeCapW)) * .25f * kDischargeEfficiency : 0;
            const float direct = std::min(solarAc, std::max(0.0f, float(maximumW)) * .25f);
            float freePv = std::max(0.0f, solarAc - direct), shiftablePv = direct, remainingCapacity = capacityAc;
            for (unsigned rank = 0; rank < count; ++rank) {
                ++planningSteps_;
                const unsigned j = order_[rank] - first;
                if (j == k) {
                    pendingWh_[j] = rawPrice(first + k) > 0 ? std::max(0.0f, float(maximumW)) * .25f - shiftablePv : 0;
                    if (j == cacheDebtTarget_) { pendingDebt = reserveDebtAc; pendingWh_[j] += pendingDebt; }
                }
                else if (j > k && pendingWh_[j] > 0) {
                    const float future = price(first + j), need = pendingWh_[j];
                    if (eligible(j, k, future, current, cfg.deferralToleranceEurKwh)) {
                        protectedWh_[k] += need;
                        if (j == cacheDebtTarget_) protectedDebtWh_[k] = pendingDebt;
                    }
                    const bool profitable = future * kRoundTripEfficiency - current >= kWearEurPerKwh + kMinimumBenefitEurPerKwh;
                    if (profitable) {
                        economicWh_[k] += need;
                        if (j == cacheDebtTarget_) economicDebtWh_[k] = pendingDebt;
                        targetIndex_[k] = std::min(targetIndex_[k], uint16_t(j));
                    }
                    const float freeCredit = std::min(freePv, pendingWh_[j]);
                    if (j == cacheDebtTarget_) pendingDebt = std::max(0.0f, pendingDebt - freeCredit);
                    freePv -= freeCredit; pendingWh_[j] -= freeCredit;
                    if (profitable) {
                        const float credit = std::min(shiftablePv, pendingWh_[j]);
                        if (j == cacheDebtTarget_) pendingDebt = std::max(0.0f, pendingDebt - credit);
                        shiftablePv -= credit; pendingWh_[j] -= credit;
                    }
                }
                const float debt = j == cacheDebtTarget_ ? pendingDebt : 0;
                const float usable = std::min(std::max(0.0f, pendingWh_[j] - debt), remainingCapacity);
                pendingWh_[j] = debt + usable;
                remainingCapacity -= usable;
            }
        }
    }
    uint8_t origins_[kSlots] = {}, pendingOrigins_[kSlots] = {}, activeVersion_ = 1;
    float cacheTolerance_ = .03f, cachePremium12h_ = 0, cachePremium24h_ = 0, cachePremium_ = 0;
    int32_t active_[kSlots] = {}, pending_[kSlots] = {};
    uint32_t activeAcceptedUtc_ = 0;
    uint32_t activeStart_ = 0, activeRevision_ = 0, pendingStart_ = 0, pendingRevision_ = 0;
    uint32_t pendingAt_ = 0, lastUtc_ = 0, contentRevision_ = 0;
    unsigned activeCount_ = 0, pendingCount_ = 0;
    uint16_t parts_ = 0;
    bool pendingPoisoned_ = false, energyKnown_ = false, energyTickReady_ = false;
    float energyWh_ = 0, lastBatteryW_ = 0;
    uint32_t energyAt_ = 0, energySequence_ = 0;
    // Fixed BSS workspace; no per-control-step allocation or large stack copies.
    uint16_t order_[kPlanSlots] = {};
    float solarW_[kPlanSlots] = {}, pendingWh_[kPlanSlots] = {};
    float protectedWh_[kPlanSlots] = {}, economicWh_[kPlanSlots] = {}, projectedChargeW_[kPlanSlots] = {};
    float protectedDebtWh_[kPlanSlots] = {}, economicDebtWh_[kPlanSlots] = {};
    uint16_t targetIndex_[kPlanSlots] = {};
    bool storageKnown_[kPlanSlots] = {};
    uint32_t projectionUtc_ = 0, projectionStart_ = 0, planningSteps_ = 0;
    unsigned projectionCount_ = 0;
    float projectionFirstSolarW_ = -1;
    const PvForecast *cachePv_ = nullptr;
    uint32_t cachePriceRevision_ = 0, cachePvRevision_ = 0, cacheAt_ = 0, planningBuilds_ = 0;
    unsigned cacheFirst_ = 0, cacheDebtTarget_ = 0;
    int cacheMaximumW_ = 0, cacheChargeCapW_ = 0;
    float cachePrice_ = 0, cacheFactor_ = 0, cacheReserve_ = 0, cacheCapacityAc_ = 0, cacheReserveDebtAc_ = 0;
    float cacheOfferWh_ = 0, cacheFirstOfferW_ = 0;
    bool cacheValid_ = false, cachePvValid_ = false;
    JsonArena arena_;
};
} // namespace control
