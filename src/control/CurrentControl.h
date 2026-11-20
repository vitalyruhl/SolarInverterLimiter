#pragma once

#include "PreparedControl.h"
#include "PriceBand.h"
#include <cmath>
#include <algorithm>

namespace control {

// Standalone decision logic; no MQTT, RS485 or MOS side effects.
// Commissioning candidates, to be qualified on the installed system.
constexpr float kPvKp = 0.20f;
constexpr float kPvKiPerSecond = 0.03f;
// Without a battery budget, direct PV passthrough is trimmed until the BMS stops discharging.
constexpr float kRelativeHoldTrimKiPerSecond = 0.1f;
constexpr float kRelativeHoldBatteryMarginW = 5.0f;

struct CurrentInputs {
    bool sourcesWithinGrace = false;
    bool bmsWithinGrace = false; // Original receiver deadline; reconnect never renews it.
    uint32_t bmsSampleAgeMs = UINT32_MAX;
    bool fallbackProtectionKnown = false;
    bool knownSourceFault = false;
    bool alarmRestricted = false;
    bool bmsValid = false;
    bool batteryDischargeIntentAvailable = false; // Fresh healthy source permits inverter intent, even with actual MOS OFF.
    bool batteryChargeAvailable = false; // Fresh, qualified charge permission independent of discharge.
    bool softwareMosLockOnly = false; // The sole latched discharge bit is JBD bit 12.
    bool chargerFault = false;
    bool chargerValid = false;
    bool chargerStandby = false; // Fresh actual discharge MOS OFF explains an unavailable MPPT.
    bool metersValid = false;
    bool priceValid = false;
    float soc = 0;
    float batteryW = 0; // Positive means charging.
    float householdW = 0; // Measured inverter AC + signed grid power, before correction offset.
    int correctionW = 0;
    float chargerW = 0; // Victron battery voltage * charger output current.
    float price = 0;
    uint32_t bmsSequence = 0;
    uint32_t chargerSequence = 0;
    bool futureValid = false;
    bool relativePricePositive = false;
    int futurePvCeilingW = 0;
    const char* futurePriceTier = "unknown";
    int futureBatteryCeilingW = 0;
    float futureChargeTargetW = 0;
    const char *futureReason = "forecast_missing";
};

struct CurrentDecision {
    int ceilingW = 0;
    bool economicPlanApplied = false; // This qualified decision uses the current economic slot.
    bool sourceGrace = false; // Previously qualified battery permission only; never fresh telemetry.
    bool fullBatteryPv = false; // Approval requires fresh full-battery and charger feedback.
    bool pvOnly = false; // Always use the household PID, including with Limiter OFF.
    bool minimumOverrideAllowed = false; // Qualified discharge intent and released reserve; economics may yield to configured MIN.
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
    void resetTransient() {
        const bool known = reserveKnown_, allowed = reserveAllowed_;
        *this = CurrentControl{};
        reserveKnown_ = known;
        reserveAllowed_ = allowed;
    }
    bool expensivePrice(const PreparedSettings &cfg, bool valid, float price) {
        return priceBand_.expensivePrice(cfg, valid, price);
    }
    bool cheapPrice() const { return priceBand_.cheapPrice(); }
    // heldCommandW is the last issued command before output correction, not a new demand.
    // The caller retains responsibility for transport and higher-priority protection.
    CurrentDecision update(const PreparedSettings &cfg, const CurrentInputs &in,
                           bool limiterEnabled, bool transportEnabled,
                           int maximumW, int heldCommandW, uint32_t now) {
        CurrentDecision out;
        const float maximum = maximumW > 0 ? float(maximumW) : 0.0f;
        const int forecastLimit = in.futureValid
            ? std::max(0, std::min(in.futureBatteryCeilingW, int(maximum))) : int(maximum);
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
        if(cfg.relativeTimePolicy&&(!price||!in.relativePricePositive))return stop(out,"relative_nonpositive_price");
        priceBand_.classify(cfg, price, in.price);
        out.priceBand = priceBand_.value() == PriceBandState::Expensive ? "expensive" :
            (priceBand_.value() == PriceBandState::Cheap ? "cheap" :
             (priceBand_.value() == PriceBandState::Comfort ? "comfort" : "unknown"));
        const bool chargeAvailable = bms && in.batteryChargeAvailable;
        const bool dischargeAvailable = bms && in.batteryDischargeIntentAvailable;
        // These gates also precede held permission and partial source recovery.
        if (in.alarmRestricted && !in.softwareMosLockOnly) return stop(out, "source_alarm");
        if (!cfg.inverterEnabled) return stop(out, "inverter_off");
        if (!transportEnabled) return stop(out, "transport_off");
        if (price && (in.price < 0 || (cfg.relativeTimePolicy && in.price == 0))) return stop(out, "negative_price");
        if (!in.metersValid || !std::isfinite(in.householdW)) return stop(out, "meters_missing");
        if (in.chargerFault) return stop(out, "charger_fault");
        if (batteryGraceActive_ && !in.bmsWithinGrace) return stop(out, "source_timeout");
        const bool planMissing = cfg.relativeTimePolicy && !in.futureValid;
        const bool holdingBattery = !dischargeAvailable && batteryGraceAllowed(in);
        if (in.knownSourceFault && (!bms || planMissing || batteryGraceActive_ || holdingBattery))
            return stop(out, "source_alarm");
        if (holdingBattery && !planMissing)
            return holdBattery(out, cfg, forecastLimit, maximumW, heldCommandW, now, "source_grace", true);
        const bool pvOnly = !chargeAvailable && !dischargeAvailable;
        const bool batteryOnly = bms && dischargeAvailable && !charger;
        if (pvOnly != pvOnly_ || chargeAvailable != chargeAvailable_ || dischargeAvailable != dischargeAvailable_) {
            resetDynamics(batteryGraceActive_); pvOnly_ = pvOnly; chargeAvailable_ = chargeAvailable; dischargeAvailable_ = dischargeAvailable;
        }
        if (batteryOnly != batteryOnly_) {
            qualified_ = qualifying_ = false; pairs_ = 0;
            pauseDynamics(); batteryOnly_ = batteryOnly;
        }
        if (bms && !charger && in.chargerStandby) {
            if (in.chargerFault) return stop(out, "charger_fault");
            resetDynamics();
            out.direct = true; out.strategy = "preserve"; out.reason = "mppt_standby";
            return out;
        }
        if (pvOnly || planMissing) {
            out.pvOnly = true; out.reserveAllowed = false;
            out.strategy = "pv_only";
            if (!in.metersValid || !std::isfinite(in.householdW)) return stop(out, "meters_missing");
            if (in.chargerFault) return stop(out, "charger_fault");
            if (!charger || in.chargerW <= 0) return stop(out, "pv_missing");
            const bool fallback = !bms || planMissing;
            if (fallback && (!in.fallbackProtectionKnown || !std::isfinite(cfg.outage.pvAcFraction) ||
                cfg.outage.pvAcFraction <= 0 || cfg.outage.pvAcFraction > 1))
                return stop(out, "pv_fallback_unverified");
            if (fallback != fallbackActive_) {
                resetDynamics();
                fallbackActive_ = fallback;
            }
            // Fresh PV can qualify without a missing battery sequence ever advancing.
            if (!qualify(in, now, true)) {
                out.direct = true; out.reason = "pv_recovery"; return out;
            }
            const float demand = clamp(in.householdW + float(in.correctionW), 0, maximum);
            out.ceilingW = int(clamp(in.chargerW * (fallback ? cfg.outage.pvAcFraction : 1.0f), 0, demand));
            if (fallback) {
                out.strategy = out.reason = "pv_limited_fallback";
            } else {
                out.reason = "pv_only_mos";
            }
            return out; // No battery-power PI, forecast discharge budget or old reserve permission.
        }
        if (fallbackActive_) {
            resetDynamics();
            fallbackActive_ = false;
        }
        // Keep the household PID active for charge-only operation, including Limiter OFF.
        out.pvOnly = !dischargeAvailable;
        if (!cfg.relativeTimePolicy && !limiterEnabled && dischargeAvailable && !batteryGraceActive_) {
            resetDynamics();
            out.direct = true; out.ceilingW = int(maximum);
            out.strategy = "bypass"; out.reason = "limiter_off";
            return out;
        }
        if (!in.metersValid) return stop(out, "meters_missing");
        if (in.chargerFault) return stop(out, "charger_fault");
        if (!bms || (!charger && !batteryOnly)) return stop(out, "pv_missing");
        if (!qualify(in, now, false, batteryOnly)) {
            pauseDynamics();
            if (batteryGraceActive_ && batteryGraceAllowed(in))
                return holdBattery(out, cfg, forecastLimit, maximumW, heldCommandW, now, "source_recovery", false);
            if (approved_ && in.sourcesWithinGrace && !in.knownSourceFault && !(reserveKnown_ && !reserveAllowed_) &&
                (!lastApproved_.fullBatteryPv || (bms && in.soc >= 100 && charger))) {
                out = lastApproved_;
                out.economicPlanApplied = false;
                out.minimumOverrideAllowed = false; out.direct = false;
                out.ceilingW = std::max(0, std::min({out.ceilingW, forecastLimit, heldCommandW}));
                out.chargeTargetValid = false; out.reason = batteryOnly ? "source_grace" : "source_recovery";
                return out;
            }
            out.direct = true; out.reason = "recovery";
            return out;
        }
        // Grace must complete source qualification before Limiter OFF may bypass it.
        if (!cfg.relativeTimePolicy && !limiterEnabled && dischargeAvailable) {
            resetDynamics();
            out.direct = true; out.ceilingW = int(maximum);
            out.strategy = "bypass"; out.reason = "limiter_off";
            return out;
        }
        if (charger && dischargeAvailable && out.reserveAllowed && in.soc >= 100) {
            // A full battery curtails MPPT output without an AC load. Measured
            // charger power cannot bound the PV that a new load will release.
            pauseDynamics();
            if (!std::isfinite(in.householdW)) return stop(out, "meters_missing");
            out.pvOnly = true; out.minimumOverrideAllowed = false;
            out.fullBatteryPv = true;
            out.ceilingW = int(clamp(in.householdW + float(in.correctionW), 0, maximum));
            out.chargeTargetValid = true; out.chargeTargetW = 0;
            out.strategy = "pv_only"; out.reason = "full_battery_pv";
            return approve(out);
        }
        if(cfg.relativeTimePolicy) {
            if(!in.relativePricePositive)return stop(out,"relative_nonpositive_price");
            pauseDynamics();out.minimumOverrideAllowed=false;
            const int battery=dischargeAvailable&&out.reserveAllowed?forecastLimit:0;
            int direct=charger?std::max(0,std::min(in.futurePvCeilingW,int(maximum))):0;
            if(battery>0||direct==0){relativeTrimActive_=false;relativeTrimW_=0;}
            else direct=trimRelativeHold(in,direct,now);
            out.ceilingW=std::min(int(maximum),battery+direct);
            out.pvOnly=battery==0;out.strategy=battery>0?"discharge":"preserve";
            out.priceBand=in.futurePriceTier;out.reason=in.futureReason;
            out.chargeTargetValid=true;out.chargeTargetW=chargeAvailable?std::max(0.0f,in.futureChargeTargetW):0;
            out.economicPlanApplied = in.futureValid &&
                (in.futureBatteryCeilingW <= 0 || (dischargeAvailable && out.reserveAllowed)) &&
                (in.futureChargeTargetW <= 0 || chargeAvailable);
            batteryGraceBudgetW_=battery;
            if (battery > 0) recordReserve(in, now);
            return approve(out,battery>0);
        }
        // Missing MPPT measurements cannot add direct PV or charging power.
        const float chargerW = charger ? in.chargerW : 0.0f;
        out.minimumOverrideAllowed = dischargeAvailable && out.reserveAllowed;
        const bool reserveOffCharging = chargeAvailable && price &&
            in.soc <= cfg.reserveOffSocPercent && priceBand_.value() != PriceBandState::Expensive;
        const bool reserveOnCharging = chargeAvailable && price &&
            in.soc > cfg.reserveOffSocPercent && in.soc < cfg.reserveOnSocPercent && priceBand_.value() == PriceBandState::Cheap;
        const bool reserveCharging = reserveOffCharging || reserveOnCharging;
        const float reserveChargeW = reserveOffCharging ? chargerW :
            (reserveOnCharging ? chargerW * 0.5f : 0.0f);
        if (!reserveCharging && dischargeAvailable && out.reserveAllowed &&
            (in.futureValid ? in.futureBatteryCeilingW > 0 : priceBand_.value() == PriceBandState::Expensive)) {
            const float economicTarget = in.futureValid
                ? clamp(float(in.futureBatteryCeilingW) +
                    clamp(chargerW, 0, maximum) * kDischargeEfficiency, 0, maximum)
                : maximum;
            piActive_ = false; integral_ = 0;
            if (!economicActive_) {
                economicActive_ = true;
                economicCeiling_ = clamp(float(heldCommandW), 0, maximum);
                economicAt_ = now;
            }
            const float dt = clamp(float(uint32_t(now - economicAt_)) * 0.001f, 0, 10);
            economicAt_ = now;
            economicCeiling_ = economicTarget >= economicCeiling_
                ? clamp(economicTarget, 0, economicCeiling_ + kEconomicCeilingRampUpWPerSecond * dt)
                : clamp(economicTarget, economicCeiling_ - kEconomicCeilingRampDownWPerSecond * dt, maximum);
            // Fresh PV loss and a shrinking available-energy ceiling remain restrictive immediately.
            if (in.futureValid) economicCeiling_ = std::min(economicCeiling_, economicTarget);
            out.ceilingW = int(economicCeiling_);
            out.strategy = "discharge"; out.reason = in.futureValid ? in.futureReason : "expensive";
            batteryGraceBudgetW_ = in.futureValid ? forecastLimit : int(maximum);
            recordReserve(in, now);
            return approve(out, true);
        }
        economicActive_ = false;
        if (!chargeAvailable) {
            // Consume available PV without reserving power for an inaccessible battery.
            if (!std::isfinite(in.householdW)) return stop(out, "meters_missing");
            piActive_ = false; integral_ = 0;
            out.pvOnly = true; out.reserveAllowed = false; out.strategy = "pv_only";
            out.ceilingW = int(clamp(chargerW, 0, clamp(in.householdW + float(in.correctionW), 0, maximum)));
            out.reason = "pv_only_charge_disabled";
            return approve(out);
        }
        out.strategy = "preserve";
        out.reason = reserveOffCharging ? "reserve_off_charge" :
            (reserveOnCharging ? "reserve_on_charge" : (!out.reserveAllowed ? "reserve" :
            (in.futureValid ? in.futureReason :
             (priceBand_.value() == PriceBandState::Unknown ? "price_missing" :
              (priceBand_.value() == PriceBandState::Cheap ? "cheap" : "comfort")))));
        out.chargeTargetValid = true;
        // Reserve and economic charging share one target; neither counts the same energy twice.
        out.chargeTargetW = std::max(reserveChargeW,
            in.futureValid ? std::max(0.0f, in.futureChargeTargetW) : 0.0f);
        if (reserveOffCharging) {
            piActive_ = false; integral_ = 0;
            out.ceilingW = 0;
            return approve(out);
        }
        const float upper = clamp(chargerW * kDischargeEfficiency, 0, maximum);
        const float feedforward = clamp((chargerW - out.chargeTargetW) * kDischargeEfficiency, 0, upper);
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
            const bool demandLimited = float(heldCommandW) < ceiling_;
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
        return approve(out);
    }

private:
    CurrentDecision lastApproved_;
    bool approved_ = false;
    int batteryGraceBudgetW_ = 0;
    bool batteryGraceApproved_ = false, batteryGraceActive_ = false;
    bool fallbackActive_ = false, reserveSampleKnown_ = false;
    uint32_t reserveSequence_ = 0, reserveSeenAt_ = 0, reserveSourceAge_ = UINT32_MAX;
    float reserveSoc_ = 0;
    void recordReserve(const CurrentInputs &in, uint32_t now) {
        if (reserveSampleKnown_ && in.bmsSequence == reserveSequence_) return;
        reserveSampleKnown_ = true;
        reserveSequence_ = in.bmsSequence;
        reserveSeenAt_ = now;
        reserveSourceAge_ = in.bmsSampleAgeMs;
        reserveSoc_ = in.soc;
    }
    int reserveLimit(const PreparedSettings &cfg, int maximumW, uint32_t now) const {
        const auto &l = cfg.outage;
        if (!reserveSampleKnown_ || reserveSourceAge_ == UINT32_MAX ||
            !(std::isfinite(l.capacityAhLower) && l.capacityAhLower > 0 &&
              std::isfinite(l.voltageVLower) && l.voltageVLower > 0 &&
              std::isfinite(l.maxOtherLoadW) && l.maxOtherLoadW >= 0 &&
              std::isfinite(l.socMarginPct) && l.socMarginPct >= 0 &&
              std::isfinite(l.uncertaintyWh) && l.uncertaintyWh >= 0 &&
              l.efficiencyLower > 0 && l.efficiencyLower <= 1 &&
              std::isfinite(l.maxDischargeAcW) && l.maxDischargeAcW >= maximumW &&
              std::isfinite(l.nextCheckSeconds) && l.nextCheckSeconds > 0)) return 0;
        const double ageMs = double(reserveSourceAge_) + uint32_t(now - reserveSeenAt_);
        const double usableWh = (reserveSoc_ - cfg.reserveOffSocPercent - l.socMarginPct) * 0.01 *
            l.capacityAhLower * l.voltageVLower - l.uncertaintyWh;
        const double spentWh = (double(l.maxDischargeAcW) / l.efficiencyLower + l.maxOtherLoadW) * ageMs / 3600000;
        const double allowedW = ((usableWh - spentWh) * 3600 / l.nextCheckSeconds - l.maxOtherLoadW) * l.efficiencyLower;
        return int(std::max(0.0, std::min(double(maximumW), allowedW)));
    }
    bool batteryGraceAllowed(const CurrentInputs &in) const {
        return batteryGraceApproved_ && batteryGraceBudgetW_ > 0 && in.bmsWithinGrace &&
            reserveKnown_ && reserveAllowed_;
    }
    CurrentDecision holdBattery(CurrentDecision out, const PreparedSettings &cfg, int forecastLimit, int maximumW,
                                int heldCommandW, uint32_t now, const char *reason, bool restartQualification) {
        const int energyLimit = reserveLimit(cfg, maximumW, now);
        if (!energyLimit) return stop(out, "reserve_grace_unavailable");
        out.ceilingW = std::max(0, std::min({batteryGraceBudgetW_, lastApproved_.ceilingW,
            forecastLimit, energyLimit, heldCommandW}));
        out.sourceGrace = true; out.direct = false;
        out.strategy = "hold"; out.reason = reason;
        out.minimumOverrideAllowed = false; out.chargeTargetValid = false;
        pauseDynamics(); batteryGraceActive_ = true;
        if (restartQualification) { qualifying_ = qualified_ = false; pairs_ = 0; }
        return out;
    }
    CurrentDecision approve(CurrentDecision out, bool batteryApproval = false) {
        batteryGraceApproved_ = batteryApproval && out.reserveAllowed && !out.pvOnly && !out.fullBatteryPv;
        batteryGraceActive_ = false;
        if (!batteryGraceApproved_) batteryGraceBudgetW_ = 0;
        lastApproved_ = out; approved_ = true; return out;
    }
    PriceBandState priceBand_;
    bool reserveKnown_ = false, reserveAllowed_ = false;
    bool qualifying_ = false, qualified_ = false, piActive_ = false;
    uint8_t pairs_ = 0;
    uint32_t recoveryAt_ = 0, bmsSequence_ = 0, chargerSequence_ = 0;
    uint32_t lastTick_ = 0, lastSampleAt_ = 0, lastPiSequence_ = 0;
    float integral_ = 0, ceiling_ = 0, economicCeiling_ = 0;
    bool economicActive_ = false, pvOnly_ = false, batteryOnly_ = false, chargeAvailable_ = false, dischargeAvailable_ = false;
    uint32_t economicAt_ = 0;
    bool relativeTrimActive_ = false;
    float relativeTrimW_ = 0;
    uint32_t relativeTrimAt_ = 0, relativeTrimSequence_ = 0;

    // Fixed standby loads and low-load inverter losses are not part of the PV model.
    int trimRelativeHold(const CurrentInputs &in, int direct, uint32_t now) {
        if (!relativeTrimActive_) {
            relativeTrimActive_ = true; relativeTrimW_ = 0;
            relativeTrimAt_ = now; relativeTrimSequence_ = in.bmsSequence;
        } else if (in.bmsSequence != relativeTrimSequence_) {
            const float dt = clamp(float(uint32_t(now - relativeTrimAt_)) * 0.001f, 0, 10);
            relativeTrimAt_ = now; relativeTrimSequence_ = in.bmsSequence;
            relativeTrimW_ += kRelativeHoldTrimKiPerSecond * (kRelativeHoldBatteryMarginW - in.batteryW) * dt;
        }
        relativeTrimW_ = clamp(relativeTrimW_, 0, float(direct));
        return std::max(0, direct - int(relativeTrimW_ + 0.5f));
    }

    static float clamp(float value, float low, float high) {
        return value < low ? low : (value > high ? high : value);
    }
    bool qualify(const CurrentInputs &in, uint32_t now, bool chargerOnly = false, bool batteryOnly = false) {
        if (qualified_) return true;
        if (!qualifying_) {
            qualifying_ = true; recoveryAt_ = now; pairs_ = 0;
            bmsSequence_ = in.bmsSequence; chargerSequence_ = in.chargerSequence;
        } else if (chargerOnly ? in.chargerSequence != chargerSequence_ :
                   (batteryOnly ? in.bmsSequence != bmsSequence_ :
                    in.bmsSequence != bmsSequence_ && in.chargerSequence != chargerSequence_)) {
            bmsSequence_ = in.bmsSequence; chargerSequence_ = in.chargerSequence;
            if (pairs_ < kRecoveryNewSamples) ++pairs_;
        }
        qualified_ = pairs_ >= kRecoveryNewSamples && uint32_t(now - recoveryAt_) >= kRecoveryQualificationMs;
        return qualified_;
    }
    // Keep the prior bounded permission/reserve latch while restarting dynamics from live held output.
    void pauseDynamics() {
        piActive_ = economicActive_ = false;
        integral_ = ceiling_ = economicCeiling_ = 0;
        lastTick_ = lastSampleAt_ = economicAt_ = 0;
        lastPiSequence_ = 0;
    }
    void resetDynamics(bool keepBatteryGrace = false) {
        approved_ = false;
        if (!keepBatteryGrace) {
            batteryGraceApproved_ = batteryGraceActive_ = false;
            batteryGraceBudgetW_ = 0;
        }
        qualifying_ = qualified_ = false; pairs_ = 0;
        relativeTrimActive_ = false; relativeTrimW_ = 0;
        pauseDynamics();
    }
    CurrentDecision stop(CurrentDecision out, const char *reason) {
        resetDynamics(); out.minimumOverrideAllowed = false; out.direct = true; out.reason = reason;
        return out;
    }
};

} // namespace control
