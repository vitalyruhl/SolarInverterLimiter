#pragma once
#include "PreparedControl.h"
#include <cmath>
#include <cstdint>
namespace control {
enum class PriceOrigin : uint8_t { Unknown=0, Market=1, Forecast=2 };
inline bool validForecastPremiums(const PreparedSettings& c) {
 const auto valid=[](float v){return std::isfinite(v)&&v>=0&&v<=100;};
 return valid(c.predictionPremium12hPct)&&valid(c.predictionPremium24hPct)&&valid(c.predictionPremiumPct);
}
inline bool validPriceAssessment(const PreparedSettings& cfg) {
 return std::isfinite(cfg.deferralToleranceEurKwh) && cfg.deferralToleranceEurKwh>=0 &&
  validForecastPremiums(cfg);
}
inline float forecastPremium(const PreparedSettings& cfg, uint32_t now, uint32_t slotStart) {
 const uint32_t horizon=slotStart>now?slotStart-now:0;
 return horizon<=12UL*3600 ? cfg.predictionPremium12hPct :
     horizon<=24UL*3600 ? cfg.predictionPremium24hPct : cfg.predictionPremiumPct;
}
inline float assessedPrice(float raw, PriceOrigin origin, float premium) {
 return origin==PriceOrigin::Forecast ? raw+std::fabs(raw)*premium*.01f : raw;
}
inline bool higherValue(float future,float current,float tolerance) {
 return tolerance==0 ? future>current : future>=current+tolerance;
}
}
