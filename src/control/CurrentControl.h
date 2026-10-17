#pragma once

#include "PreparedControl.h"
#include <cmath>

namespace control {

// Standalone decision logic; no MQTT, RS485 or MOS side effects.
// Commissioning candidates, to be qualified on the installed system.
constexpr float kPvKp = 0.20f;
constexpr float kPvKiPerSecond = 0.03f;

struct CurrentInputs {
    bool bmsValid = false;
    bool chargerValid = false;
    bool metersValid = false;
    bool priceValid = false;
    float soc = 0;
    float batteryW = 0; // Positive means charging.
    float chargerW = 0; // Victron battery voltage * charger output current.
    float price = 0;
    uint32_t bmsSequence = 0;
    uint32_t chargerSequence = 0;
};

struct CurrentDecision {
    int ceilingW = 0;
    bool direct = false; // A fallback/stop/bypass decision, not a demand request.
    bool reserveKnown = false;
    bool reserveAllowed = false;
    bool chargeTargetValid = false;
    float chargeTargetW = 0;
    const char *strategy = "hold";
    const char *reason = "startup";
    const char *priceBand = "unknown";
};

class CurrentControl {
public:
    // heldCommandW must describe the actual held output command, not a new demand.
    // The caller retains responsibility for transport and higher-priority protection.
    CurrentDecision update(const PreparedSettings &cfg, const CurrentInputs &in,
                           bool limiterEnabled, bool transportEnabled,
                           int maximumW, int heldCommandW, uint32_t now) {
        CurrentDecision out;
        const float maximum = maximumW > 0 ? float(maximumW) : 0.0f;
        const bool bms = in.bmsValid && std::isfinite(in.soc) &&
            in.soc >= 0 && in.soc <= 100 && std::isfinite(in.batteryW);
        const bool charger = in.chargerValid && std::isfinite(in.chargerW) && in.chargerW >= 0;
        const bool price = in.priceValid && std::isfinite(in.price);
        if (!cfg.valid) return stop(out, "settings");
        if (bms) {
            if (in.soc <= cfg.reserveOffSocPercent) { reserveKnown_ = true; reserveAllowed_ = false; }
            else if (in.soc >= cfg.reserveOnSocPercent) { reserveKnown_ = true; reserveAllowed_ = true; }
        }
        out.reserveKnown = reserveKnown_;
        out.reserveAllowed = reserveKnown_ && reserveAllowed_;
        classify(cfg, price, in.price);
        out.priceBand = band_ == Expensive ? "expensive" :
            (band_ == Cheap ? "cheap" : (band_ == Comfort ? "comfort" : "unknown"));
        if (!cfg.inverterEnabled) return stop(out, "inverter_off");
        if (!transportEnabled) return stop(out, "transport_off");
        if (price && in.price < 0) return stop(out, "negative_price");
        if (!limiterEnabled) {
            resetDynamics();
            out.direct = true; out.ceilingW = int(maximum);
            out.strategy = "bypass"; out.reason = "limiter_off";
            return out;
        }
        if (!in.metersValid) return stop(out, "meters_missing");
        if (!bms || !charger) {
            resetDynamics();
            out.direct = true;
            // Missing-source fallback must not clear an already latched restriction.
            out.ceilingW = reserveKnown_ && !reserveAllowed_ ? 0 : int(maximum);
            out.strategy = "fallback";
            out.reason = reserveKnown_ && !reserveAllowed_ ? "reserve_source_loss" : "source_loss";
            return out;
        }
        if (!qualify(in, now)) {
            out.direct = true; out.reason = "recovery";
            return out;
        }
        if (band_ == Expensive && out.reserveAllowed) {
            piActive_ = false; integral_ = 0;
            if (!economicActive_) {
                economicActive_ = true;
                economicCeiling_ = clamp(float(heldCommandW), 0, maximum);
                economicAt_ = now;
            }
            const float dt = clamp(float(uint32_t(now - economicAt_)) * 0.001f, 0, 10);
            economicAt_ = now;
            economicCeiling_ = clamp(economicCeiling_ + kEconomicCeilingRampUpWPerSecond * dt, 0, maximum);
            out.ceilingW = int(economicCeiling_);
            out.strategy = "discharge"; out.reason = "expensive";
            return out;
        }
        economicActive_ = false;
        out.strategy = "preserve";
        out.reason = !out.reserveAllowed ? "reserve" :
            (band_ == Unknown ? "price_missing" : (band_ == Cheap ? "cheap" : "comfort"));
        out.chargeTargetValid = true;
        out.chargeTargetW = float(cfg.batteryPowerDeadbandW);
        const float upper = clamp(in.chargerW * kDischargeEfficiency, 0, maximum);
        const float feedforward = clamp((in.chargerW - out.chargeTargetW) * kDischargeEfficiency, 0, upper);
        const float error = (in.batteryW - out.chargeTargetW) * kDischargeEfficiency;
        if (!piActive_) {
            piActive_ = true; integral_ = 0;
            ceiling_ = clamp(float(heldCommandW), 0, feedforward);
            lastTick_ = lastSampleAt_ = now;
            lastPiSequence_ = in.bmsSequence;
        }
        const float dt = clamp(float(uint32_t(now - lastTick_)) * 0.001f, 0, 10);
        lastTick_ = now;
        if (lastPiSequence_ != in.bmsSequence) {
            const float sampleDt = clamp(float(uint32_t(now - lastSampleAt_)) * 0.001f, 0, 10);
            lastSampleAt_ = now; lastPiSequence_ = in.bmsSequence;
            const bool demandLimited = float(heldCommandW) + cfg.batteryPowerDeadbandW < ceiling_;
            const float next = clamp(integral_ + kPvKiPerSecond * error * sampleDt, -maximum, maximum);
            const float candidate = feedforward + kPvKp * error + next;
            // No positive windup while the house PID requests less than this ceiling.
            if ((!demandLimited || error < 0) &&
                !((candidate > upper && error > 0) || (candidate < 0 && error < 0))) integral_ = next;
        }
        const float target = clamp(feedforward + kPvKp * error + integral_, 0, upper);
        // A falling PV ceiling takes effect immediately; increases remain bounded.
        ceiling_ = target < ceiling_ ? target : clamp(target, 0, ceiling_ + kEconomicCeilingRampUpWPerSecond * dt);
        out.ceilingW = int(clamp(ceiling_, 0, maximum));
        return out;
    }

private:
    enum Band { Unknown, Cheap, Comfort, Expensive };
    Band band_ = Unknown;
    bool reserveKnown_ = false, reserveAllowed_ = false;
    bool qualifying_ = false, qualified_ = false, piActive_ = false;
    uint8_t pairs_ = 0;
    uint32_t recoveryAt_ = 0, bmsSequence_ = 0, chargerSequence_ = 0;
    uint32_t lastTick_ = 0, lastSampleAt_ = 0, lastPiSequence_ = 0;
    float integral_ = 0, ceiling_ = 0, economicCeiling_ = 0;
    bool economicActive_ = false;
    uint32_t economicAt_ = 0;

    static float clamp(float value, float low, float high) {
        return value < low ? low : (value > high ? high : value);
    }
    void classify(const PreparedSettings &cfg, bool valid, float price) {
        if (!valid) { band_ = Unknown; return; }
        const float high = cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh;
        const float low = cfg.priceComfortEurPerKwh - cfg.priceDeadbandEurPerKwh;
        const float hysteresis = cfg.priceDeadbandEurPerKwh / 3.0f;
        if (band_ == Expensive && price > high - hysteresis) return;
        if (band_ == Cheap && price < low + hysteresis) return;
        band_ = price >= high ? Expensive : (price <= low ? Cheap : Comfort);
    }
    bool qualify(const CurrentInputs &in, uint32_t now) {
        if (qualified_) return true;
        if (!qualifying_) {
            qualifying_ = true; recoveryAt_ = now; pairs_ = 0;
            bmsSequence_ = in.bmsSequence; chargerSequence_ = in.chargerSequence;
        } else if (in.bmsSequence != bmsSequence_ && in.chargerSequence != chargerSequence_) {
            bmsSequence_ = in.bmsSequence; chargerSequence_ = in.chargerSequence;
            if (pairs_ < kRecoveryNewSamples) ++pairs_;
        }
        qualified_ = pairs_ >= kRecoveryNewSamples && uint32_t(now - recoveryAt_) >= kRecoveryQualificationMs;
        return qualified_;
    }
    void resetDynamics() {
        qualifying_ = qualified_ = piActive_ = false;
        pairs_ = 0; integral_ = ceiling_ = 0; economicActive_ = false;
    }
    CurrentDecision stop(CurrentDecision out, const char *reason) {
        resetDynamics(); out.direct = true; out.reason = reason;
        return out;
    }
};

} // namespace control
