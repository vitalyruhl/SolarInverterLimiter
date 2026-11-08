#pragma once
#include "PreparedControl.h"
#include "ShadowProfile.h"
#ifndef SIL_RELATIVE_TIME_POLICY
#define SIL_RELATIVE_TIME_POLICY 1
#endif
namespace control {
// Algorithm selection is independent of MQTT namespace and physical transport.
inline constexpr bool kRelativeTimePolicy=SIL_RELATIVE_TIME_POLICY!=0;
inline constexpr unsigned kPriceInputVersion=2;
inline constexpr const char* kDefaultPriceScheduleTopic=kPlannerShadowProfile?
    "SolarLimiterTest/Input/PriceSchedule":"SolarLimiter/Input/PriceSchedule";
inline constexpr bool priceInputRequiredForMode(bool automatic) {
    return kPriceInputVersion==2&&(kPlannerShadowProfile||automatic);
}
struct ApplicationDefaults {
    static constexpr float cheapTimePct=10,veryExpensiveTimePct=10,additionalExpensiveTimePct=20;
    static constexpr float storageMinimumNetAdvantageEurKwh=.05f,predictionPremiumPct=10;
    static constexpr int maxOutputW=900,idleOutputW=500,minOutputW=0,outputCorrectionW=-100,sourceTimeoutS=180;
    static constexpr float pidKp=.45f,pidKi=.05f,pidKd=.02f,rs485PeriodS=1;
    static constexpr float reserveOffSoc=5,reserveOnSoc=10,chargeThreshold=80;
    static constexpr int chargeInverterSharePct=10;
    static constexpr bool limiterEnabled=true,forceMinNegative=false,setZeroNegative=true,mosManual=true,inverterEnabled=true;
};
inline PreparedSettings applicationDefaultSettings() {
    PreparedSettings c;
    c.relativeTimePolicy=kRelativeTimePolicy;
    c.cheapTimePct=ApplicationDefaults::cheapTimePct;c.veryExpensiveTimePct=ApplicationDefaults::veryExpensiveTimePct;
    c.additionalExpensiveTimePct=ApplicationDefaults::additionalExpensiveTimePct;
    c.storageMinimumNetAdvantageEurKwh=ApplicationDefaults::storageMinimumNetAdvantageEurKwh;
    c.predictionPremiumPct=ApplicationDefaults::predictionPremiumPct;c.idleOutputW=ApplicationDefaults::idleOutputW;
    c.reserveOffSocPercent=ApplicationDefaults::reserveOffSoc;c.reserveOnSocPercent=ApplicationDefaults::reserveOnSoc;
    c.chargePriorityThreshold=ApplicationDefaults::chargeThreshold;c.chargeInverterSharePct=ApplicationDefaults::chargeInverterSharePct;
    c.mosManualMode=ApplicationDefaults::mosManual;c.inverterEnabled=ApplicationDefaults::inverterEnabled;
    return c; // Settings become valid only after the normal configuration validation.
}
}
