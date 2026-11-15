#include "control/CurrentControl.h"
#include "control/PriceBand.h"
using PlannerBand = control::PriceBandState;

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static control::PreparedSettings settings() {
    control::PreparedSettings cfg;
    cfg.valid = true;
    cfg.priceComfortEurPerKwh = 6.0f;
    cfg.priceDeadbandEurPerKwh = 3.0f;
    return cfg;
}

template<class Band> static void boundariesAndInvalidInputs() {
    auto cfg = settings();
    Band band;
    assert(!band.expensivePrice(cfg, true, 3.0f) && band.cheapPrice());
    assert(!band.expensivePrice(cfg, true, std::nextafter(4.0f, 0.0f)) && band.cheapPrice());
    assert(!band.expensivePrice(cfg, true, 4.0f) && !band.cheapPrice());
    assert(band.expensivePrice(cfg, true, 9.0f) && !band.cheapPrice());
    assert(band.expensivePrice(cfg, true, std::nextafter(8.0f, 9.0f)));
    assert(!band.expensivePrice(cfg, true, 8.0f) && !band.cheapPrice());

    assert(!band.expensivePrice(cfg, true, 3.0f) && band.cheapPrice());
    cfg.valid = false;
    assert(!band.expensivePrice(cfg, true, 20.0f) && band.cheapPrice());
    cfg.valid = true;
    assert(!band.expensivePrice(cfg, true, 3.5f) && band.cheapPrice());
    assert(!band.expensivePrice(cfg, false, 3.5f) && !band.cheapPrice());
    assert(!band.expensivePrice(cfg, true, 3.5f) && !band.cheapPrice());

    assert(band.expensivePrice(cfg, true, 9.0f));
    cfg.valid = false;
    assert(!band.expensivePrice(cfg, false, 1.0f) && !band.cheapPrice());
    cfg.valid = true;
    assert(band.expensivePrice(cfg, true, 8.5f));

    assert(!band.expensivePrice(cfg, true, 3.0f) && band.cheapPrice());
    assert(!band.expensivePrice(cfg, true, std::numeric_limits<float>::quiet_NaN()) && !band.cheapPrice());
    assert(band.expensivePrice(cfg, true, std::numeric_limits<float>::infinity()));
    assert(!band.expensivePrice(cfg, true, -std::numeric_limits<float>::infinity()) && band.cheapPrice());
}

static void plannerBandSurvivesControllerReset() {
    auto cfg = settings();
    PlannerBand planner;
    control::CurrentControl active;
    assert(!planner.expensivePrice(cfg, true, 3.0f) && planner.cheapPrice());
    assert(!active.expensivePrice(cfg, true, 3.0f) && active.cheapPrice());
    active.resetTransient();
    assert(!active.cheapPrice() && planner.cheapPrice());
    assert(!active.expensivePrice(cfg, true, 3.5f) && !active.cheapPrice());
    assert(!planner.expensivePrice(cfg, true, 3.5f) && planner.cheapPrice());
}

int main() {
    if (sizeof(PlannerBand) >= sizeof(control::CurrentControl)) {
        std::fputs("Planner price-band state must not copy the full controller\n", stderr);
        return 1;
    }
    boundariesAndInvalidInputs<PlannerBand>();
    boundariesAndInvalidInputs<control::CurrentControl>();
    plannerBandSurvivesControllerReset();
    std::puts("[MOCKED!] Independent price-band state and active reset boundaries passed");
}
