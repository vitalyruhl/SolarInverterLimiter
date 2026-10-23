#pragma once

#include "CurrentControl.h"
#include "FuturePrices.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace control {
struct ChargePlanInputs {
    bool sourcesQualified = false, chargeAvailable = false, controlEnabled = false;
    float soc = 0, fullCapacityAh = 0, maxChargeW = 0, currentChargerW = 0, priceFactor = 1;
};

// Diagnostic DC withholding intention; never a complete future inverter command.
struct ChargePlan {
    static constexpr unsigned kSlots = 144;
    static constexpr size_t kPayloadBytes = 2048, kMqttPacketBytes = 2304;
    uint32_t generatedAt = 0, priceRevision = 0, pvRevision = 0, start = 0;
    unsigned count = 0;
    bool valid = false;
    const char *reason = "source_missing";
    int32_t watts[kSlots], solarWatts[kSlots];
    uint32_t targetAt = 0;
    float remainingWhDC = 0;

    ChargePlan() { reset(0, 0, 0); }
    void reset(uint32_t utc, uint32_t prices, uint32_t pv) {
        generatedAt = utc; priceRevision = prices; pvRevision = pv; start = targetAt = 0;
        count = 0; valid = false; reason = "source_missing"; remainingWhDC = 0;
        std::fill(watts, watts + kSlots, -1); std::fill(solarWatts, solarWatts + kSlots, -1);
    }
    void finish(bool accepted, const char *why) { valid = accepted; reason = why; if (!valid) count = 0; }
    static bool packetFits(size_t topicBytes, size_t payloadBytes) {
        return payloadBytes > 0 && payloadBytes < kPayloadBytes && topicBytes <= kMqttPacketBytes - 7 &&
            payloadBytes <= kMqttPacketBytes - 7 - topicBytes;
    }
    void project(uint32_t utc, const PreparedSettings &cfg, const FuturePlan &future,
                 const FuturePrices &prices, const PvForecast &pv, const CurrentDecision &decision,
                 const DischargeBudget &red, const ChargePlanInputs &in) {
        reset(utc, prices.revision(), pv.revision());
        if (utc < 1704067200UL) { reason = "clock_invalid"; return; }
        if (!in.sourcesQualified || !cfg.valid || !std::isfinite(in.soc) || in.soc < 0 || in.soc > 100 ||
            !std::isfinite(in.fullCapacityAh) || in.fullCapacityAh <= 0 || in.fullCapacityAh > 389 ||
            !std::isfinite(in.currentChargerW) || in.currentChargerW < 0 ||
            !std::isfinite(in.maxChargeW) || in.maxChargeW < 0 ||
            !std::isfinite(in.priceFactor) || in.priceFactor <= 0 || in.priceFactor > 5) return;
        if (!future.valid || !red.valid || red.count < 2 || red.count > kSlots || red.start % 900 ||
            utc < red.start || uint64_t(utc) >= uint64_t(red.start) + 900 ||
            uint64_t(red.start) + red.count * 900ULL > UINT32_MAX ||
            !std::isfinite(future.shortfallWh) || future.shortfallWh < 0 ||
            !std::isfinite(future.chargeTargetW) || future.chargeTargetW < 0 ||
            !std::isfinite(cfg.reserveOffSocPercent) || !std::isfinite(cfg.reserveOnSocPercent) ||
            cfg.reserveOffSocPercent < 0 || cfg.reserveOnSocPercent > 100 ||
            cfg.reserveOffSocPercent >= cfg.reserveOnSocPercent ||
            !std::isfinite(cfg.priceComfortEurPerKwh) || !std::isfinite(cfg.priceDeadbandEurPerKwh) ||
            cfg.priceDeadbandEurPerKwh < 0) {
            reason = "forecast_missing"; return;
        }
        start = red.start; count = red.count; targetAt = future.targetAt;
        // shortfallWh already excludes accepted future PV credit. Never subtract it again.
        remainingWhDC = future.shortfallWh / kDischargeEfficiency;
        const float capacityWh = in.fullCapacityAh * 51.38f;
        float energyWh = capacityWh * in.soc * .01f;
        const float chargeCap = std::min(in.maxChargeW, 12000.0f);
        const bool currentNoCharge = !decision.direct && decision.strategy &&
            !std::strcmp(decision.strategy, "discharge");
        const float currentTargetW = currentNoCharge ? 0 : decision.chargeTargetW;
        Band band = bandOf(decision.priceBand);
        bool socKnown = true;
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t at = i ? start + i * 900UL : utc;
            const uint32_t end = start + (i + 1) * 900UL;
            const float hours = float(end - at) / 3600.0f;
            float solarWh = 0;
            const bool solarKnown = pv.energyWh(at, end, solarWh);
            if (solarKnown) solarWatts[i] = int32_t(std::min(12000.0f, std::max(0.0f, solarWh / hours)));
            if (!in.controlEnabled || !in.chargeAvailable) { watts[i] = 0; continue; }
            float price = 0;
            const bool priceKnown = i == 0 ? band != Unknown : prices.priceAt(at, in.priceFactor, price);
            if (i && priceKnown) classify(cfg, price, band);
            const bool anchored = i != 0 || currentNoCharge || (decision.chargeTargetValid &&
                std::isfinite(currentTargetW) && currentTargetW >= 0);
            if (!socKnown || !priceKnown || !anchored || (i && !solarKnown)) {
                socKnown = false; continue;
            }
            // A negative-price stop has no explicit charging request. Do not invent one.
            if (i && price < 0) { watts[i] = 0; continue; }
            const float pvW = std::min(chargeCap, i ? float(solarWatts[i]) : in.currentChargerW);
            const float soc = energyWh / capacityWh * 100.0f;
            float reserveW = soc <= cfg.reserveOffSocPercent && band != Expensive ? pvW :
                (soc > cfg.reserveOffSocPercent && soc < cfg.reserveOnSocPercent && band == Cheap ? pvW * .5f : 0);
            float economicRate = future.chargeTargetW;
            if (!i) {
                reserveW = std::min(reserveW, currentTargetW);
                economicRate = std::min(economicRate, currentTargetW);
            }
            const float activeHours = targetAt > at ? float(std::min(end, targetAt) - at) / 3600.0f : 0;
            const float economicWh = std::min(remainingWhDC, economicRate * activeHours);
            const float effectiveRate = activeHours > 0 ? economicWh / activeHours : 0;
            // Cap each chronological rate before averaging a partial deadline interval.
            const float beforeRate = std::min(pvW, std::max(reserveW, effectiveRate));
            const float afterRate = std::min(pvW, reserveW);
            const float headroomWh = std::max(0.0f, capacityWh - energyWh);
            const float beforeTargetWh = std::min(headroomWh, beforeRate * activeHours);
            const float afterTargetWh = std::min(std::max(0.0f, headroomWh - beforeTargetWh),
                afterRate * (hours - activeHours));
            const float chargeWh = beforeTargetWh + afterTargetWh;
            watts[i] = int32_t(std::max(0.0f, chargeWh / hours));
            energyWh += chargeWh;
            remainingWhDC = std::max(0.0f, remainingWhDC - beforeTargetWh);
            if (chargeWh == 0) {
                // Coarse intent ledger only: do not model simultaneous battery charge/discharge.
                const float usableWh = std::max(0.0f, energyWh - capacityWh * cfg.reserveOffSocPercent * .01f);
                const float dischargeWh = std::max(0.0f, float(red.watts[i])) * hours / kDischargeEfficiency;
                energyWh -= std::min(usableWh, dischargeWh);
            }
        }
        finish(true, !in.controlEnabled ? "control_off" : (!in.chargeAvailable ? "charge_blocked" : "intent_projection"));
    }
    // Fixed buffers only. Unknown values are null; any malformed/overflowed plan fails closed.
    size_t serialize(char *buffer, size_t capacity) const {
        if (!buffer || !capacity) return 0;
        buffer[0] = '\0';
        if (count > kSlots || (valid && count < 2) || !std::isfinite(remainingWhDC) || remainingWhDC < 0) return 0;
        int written = std::snprintf(buffer, capacity,
            "{\"v\":1,\"valid\":%s,\"reason\":\"%s\",\"kind\":\"intent_projection\",\"g\":%lu,\"r\":%lu,\"pv\":%lu,\"t\":%lu,\"d\":900,\"n\":%u,\"target\":%lu,\"unmetWh\":%.1f,\"w\":[",
            valid ? "true" : "false", reason, static_cast<unsigned long>(generatedAt),
            static_cast<unsigned long>(priceRevision), static_cast<unsigned long>(pvRevision),
            static_cast<unsigned long>(start), valid ? count : 0, static_cast<unsigned long>(targetAt),
            double(remainingWhDC));
        if (written < 0 || size_t(written) >= capacity) { buffer[0] = '\0'; return 0; }
        size_t used = size_t(written);
        for (unsigned array = 0; array < 2; ++array) {
            for (unsigned i = 0; valid && i < count; ++i) {
                const int32_t value = array ? solarWatts[i] : watts[i];
                if (value < -1 || value > 12000) { buffer[0] = '\0'; return 0; }
                if (value == -1) written = std::snprintf(buffer + used, capacity - used, "%snull", i ? "," : "");
                else written = std::snprintf(buffer + used, capacity - used, "%s%ld", i ? "," : "", static_cast<long>(value));
                if (written < 0 || size_t(written) >= capacity - used) { buffer[0] = '\0'; return 0; }
                used += size_t(written);
            }
            const char *tail = array ? "]}" : "],\"s\":[";
            const size_t size = std::strlen(tail);
            if (capacity - used <= size) { buffer[0] = '\0'; return 0; }
            std::memcpy(buffer + used, tail, size + 1); used += size;
        }
        return used;
    }
private:
    enum Band { Unknown, Cheap, Comfort, Expensive };
    static Band bandOf(const char *value) {
        if (!value) return Unknown;
        if (!std::strcmp(value, "cheap")) return Cheap;
        if (!std::strcmp(value, "comfort")) return Comfort;
        if (!std::strcmp(value, "expensive")) return Expensive;
        return Unknown;
    }
    static void classify(const PreparedSettings &cfg, float price, Band &band) {
        const float high = cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh;
        const float low = cfg.priceComfortEurPerKwh - cfg.priceDeadbandEurPerKwh;
        const float hysteresis = cfg.priceDeadbandEurPerKwh / 3.0f;
        if (band == Expensive && price > high - hysteresis) return;
        if (band == Cheap && price < low + hysteresis) return;
        band = price >= high ? Expensive : (price <= low ? Cheap : Comfort);
    }
};
using ChargePlanPublication = PlanPublication<ChargePlan>;
} // namespace control
