#pragma once

#include "PreparedControl.h"
#include <cstdint>

namespace control {
// Economic price classification without controller, reserve, or PID state.
class PriceBandState {
public:
    enum Band : uint8_t { Unknown, Cheap, Comfort, Expensive };

    bool expensivePrice(const PreparedSettings &cfg, bool valid, float price) {
        if (!cfg.valid) return false;
        classify(cfg, valid, price);
        return band_ == Expensive;
    }
    bool cheapPrice() const { return band_ == Cheap; }
    Band value() const { return band_; }

    void classify(const PreparedSettings &cfg, bool valid, float price) {
        if (!valid) { band_ = Unknown; return; }
        const float high = cfg.priceComfortEurPerKwh + cfg.priceDeadbandEurPerKwh;
        const float low = cfg.priceComfortEurPerKwh - cfg.priceDeadbandEurPerKwh;
        const float hysteresis = cfg.priceDeadbandEurPerKwh / 3.0f;
        if (band_ == Expensive && price > high - hysteresis) return;
        if (band_ == Cheap && price < low + hysteresis) return;
        band_ = price >= high ? Expensive : (price <= low ? Cheap : Comfort);
    }

private:
    Band band_ = Unknown;
};
} // namespace control
