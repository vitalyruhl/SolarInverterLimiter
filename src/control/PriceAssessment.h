#pragma once
#include "PreparedControl.h"
#include <cmath>
#include <cstdint>
namespace control {
enum class PriceOrigin : uint8_t { Unknown=0, Market=1, Forecast=2 };
inline bool validPriceAssessment(const PreparedSettings& cfg) {
 return std::isfinite(cfg.deferralToleranceEurKwh) && cfg.deferralToleranceEurKwh>=0 &&
  std::isfinite(cfg.predictionPremiumPct) && cfg.predictionPremiumPct>=0 && cfg.predictionPremiumPct<=100;
}
inline float assessedPrice(float raw, PriceOrigin origin, float premium) {
 return origin==PriceOrigin::Forecast ? raw+std::fabs(raw)*premium*.01f : raw;
}
inline bool higherValue(float future,float current,float tolerance) {
 return tolerance==0 ? future>current : future>=current+tolerance;
}
}
