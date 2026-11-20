#pragma once

#include <cstdint>

namespace control {

struct PlanLifecycleSettings {
    uint32_t planRecalcIntervalS = 900;
    uint32_t planStaleAfterS = 1200;
    uint32_t planRetentionS = 300;
    uint32_t planMaxAgeS = 7200;
    uint32_t planDiscardAfterS = 86400;
    uint32_t priceScheduleFreshnessS = 180;
    uint32_t pvForecastMaxAgeS = 0;
    uint32_t planPublishIntervalS = 60;

    bool valid() const {
        return planRecalcIntervalS >= 60 && planRecalcIntervalS <= 3600 &&
               planStaleAfterS >= planRecalcIntervalS && planStaleAfterS <= 86400 &&
               planRetentionS >= 300 && planRetentionS <= 86400 &&
               planMaxAgeS >= 600 && planMaxAgeS >= planStaleAfterS &&
               planDiscardAfterS >= planMaxAgeS && planDiscardAfterS >= 3600 && planDiscardAfterS <= 604800 &&
               priceScheduleFreshnessS >= 60 && priceScheduleFreshnessS <= 3600 &&
               (pvForecastMaxAgeS == 0 || (pvForecastMaxAgeS >= 300 && pvForecastMaxAgeS <= 86400)) &&
               planPublishIntervalS >= 30 && planPublishIntervalS <= 600;
    }
};

} // namespace control
