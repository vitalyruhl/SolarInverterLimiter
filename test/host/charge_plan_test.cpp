#include "control/ChargePlan.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace control;
static constexpr uint32_t t = 1792454400;

// [MOCKED!] Synthetic accepted UTC schedules exercise the real receivers and projection.
struct Fixture {
    PreparedSettings cfg;
    FuturePlan future;
    FuturePrices prices;
    PvForecast pv;
    CurrentDecision decision;
    DischargeBudget red;
    ChargePlanInputs inputs;
    ChargePlan blue;
    Fixture(unsigned slots = 144, int missingSolar = -1, int missingPricesFrom = -1) {
        cfg.valid = true;
        inputs.sourcesQualified = inputs.chargeAvailable = inputs.controlEnabled = true;
        inputs.soc = 20; inputs.fullCapacityAh = 100; inputs.maxChargeW = 1200;
        inputs.currentChargerW = 1000;
        future.valid = true;
        decision.chargeTargetValid = true; decision.priceBand = "cheap";
        red.reset(t, t, t); red.start = t; red.count = slots; red.finish(true, "forecast_hold");
        const unsigned priceSlots = missingPricesFrom < 0 ? slots : unsigned(missingPricesFrom);
        for (unsigned part = 0; part * 24 < priceSlots; ++part) {
            JsonDocument d; d["v"] = 1; d["r"] = t; d["t"] = t; d["n"] = priceSlots; d["k"] = part;
            auto a = d["p"].to<JsonArray>();
            for (unsigned i = part * 24; i < priceSlots && i < (part + 1) * 24; ++i) a.add(100000);
            std::string packet; serializeJson(d, packet);
            const auto result = prices.receive(reinterpret_cast<const uint8_t *>(packet.data()), unsigned(packet.size()), t, 100 + part);
            assert(!std::strcmp(result, (part + 1) * 24 >= priceSlots ? "accepted" : "pending"));
        }
        for (unsigned part = 0; part * 24 < slots; ++part) {
            JsonDocument d; d["v"] = 2; d["s"] = t - 900; d["r"] = t; d["g"] = t; d["t"] = t;
            d["d"] = 900; d["n"] = slots; d["k"] = part; d["e"] = t + slots * 900;
            d["f"] = 1000; d["c"] = 0; d["a"] = 0; d["q"] = "uncalibrated"; d["o"] = "forecast_solar";
            auto a = d["p"].to<JsonArray>(), q = d["x"].to<JsonArray>();
            for (unsigned i = part * 24; i < slots && i < (part + 1) * 24; ++i) {
                if (int(i) == missingSolar) a.add(nullptr); else a.add(1000);
                q.add(int(i) == missingSolar ? 1 : 2);
            }
            std::string packet; serializeJson(d, packet);
            const auto result = pv.receive(reinterpret_cast<const uint8_t *>(packet.data()), unsigned(packet.size()), t, 100 + part);
            assert(!std::strcmp(result, (part + 1) * 24 >= slots ? "accepted" : "pending"));
        }
    }
    void tariffs(const std::vector<int> &values) {
        assert(values.size() == red.count);
        for (unsigned part = 0; part * 24 < values.size(); ++part) {
            JsonDocument d; d["v"] = 1; d["r"] = t + 1; d["t"] = t; d["n"] = values.size(); d["k"] = part;
            auto a = d["p"].to<JsonArray>();
            for (unsigned i = part * 24; i < values.size() && i < (part + 1) * 24; ++i) a.add(values[i]);
            std::string packet; serializeJson(d, packet);
            const auto result = prices.receive(reinterpret_cast<const uint8_t *>(packet.data()), unsigned(packet.size()), t + 1, 100 + part);
            assert(!std::strcmp(result, (part + 1) * 24 >= values.size() ? "accepted" : "pending"));
        }
    }
    void project(uint32_t utc = t) { blue.project(utc, cfg, future, prices, pv, decision, red, inputs); }
};

static void anchorAndDeadline() {
    Fixture f;
    f.future.chargeTargetW = 400; f.future.shortfallWh = 180; f.future.targetAt = t + 1800;
    f.decision.chargeTargetW = 200;
    f.project();
    assert(f.blue.valid && f.blue.count == 144 && f.blue.start == t);
    assert(f.blue.watts[0] == 200 && f.blue.watts[1] == 400 && f.blue.watts[2] == 0);
    assert(std::fabs(f.blue.remainingWhDC - 50) < .01f);
    assert(f.blue.solarWatts[143] == 1000);
    f.future.targetAt = t + 1200;
    f.project();
    assert(f.blue.watts[1] == 133); // Mean across the whole interval; deadline is inside it.
    assert(f.blue.watts[2] == 0);
}

static void partialSlotAndSharedEnergy() {
    Fixture f;
    f.inputs.soc = 1; f.inputs.fullCapacityAh = 100;
    f.future.chargeTargetW = 400; f.future.shortfallWh = 90; f.future.targetAt = t + 1800;
    f.decision.chargeTargetW = 1000;
    f.project(t + 600);
    assert(f.blue.watts[0] == 1000);
    assert(f.blue.watts[1] == 500); // Reserve/economic targets share max(), not their sum.
    assert(f.blue.remainingWhDC == 0);
    Fixture tiny;
    tiny.inputs.soc = 99; tiny.inputs.fullCapacityAh = 1;
    tiny.future.chargeTargetW = 1000; tiny.future.shortfallWh = 100; tiny.future.targetAt = t + 1800;
    tiny.decision.chargeTargetW = 1000;
    tiny.project(t + 600);
    assert(tiny.blue.watts[0] == 6 && tiny.blue.watts[1] == 0); // 0.5138 Wh / 300 s.
}

static void scarcePvBeforePartialDeadline() {
    Fixture f;
    f.inputs.maxChargeW = 500; f.inputs.currentChargerW = 500;
    f.future.chargeTargetW = 1000; f.future.shortfallWh = 900;
    f.future.targetAt = t + 1200; f.decision.chargeTargetW = 0;
    f.project();
    assert(f.blue.watts[0] == 0);
    assert(f.blue.watts[1] == 166); // 500 W is available for 300 s, not throughout the interval.
    assert(f.blue.watts[2] == 0);
    assert(std::fabs(f.blue.remainingWhDC - (1000.0f - 500.0f / 12)) < .01f);
    Fixture reserve(3);
    reserve.inputs.soc = 5; reserve.inputs.maxChargeW = 500;
    reserve.future = f.future; reserve.decision.chargeTargetW = 0;
    reserve.project();
    assert(reserve.blue.watts[1] == 333); // 500 W before the deadline, 250 W afterwards.
    assert(reserve.blue.watts[2] == 250);
    assert(std::fabs(reserve.blue.remainingWhDC - f.blue.remainingWhDC) < .01f);
    Fixture headroom;
    headroom.inputs.soc = 50; headroom.inputs.fullCapacityAh = 1;
    headroom.inputs.maxChargeW = 500; headroom.future = f.future;
    headroom.decision.chargeTargetW = 0; headroom.project();
    assert(headroom.blue.watts[1] == 102); // The 25.69 Wh headroom is consumed before the deadline.
    assert(std::fabs(headroom.blue.remainingWhDC - (1000 - 25.69f)) < .01f);
}

static void reservesAndIndependentDischargeIntent() {
    Fixture boundary;
    boundary.inputs.soc = boundary.cfg.reserveOffSocPercent;
    boundary.decision.chargeTargetW = 1000;
    boundary.project();
    assert(boundary.blue.watts[0] == 1000);
    Fixture f;
    f.inputs.soc = 1; f.decision.chargeTargetW = 1000;
    f.red.watts[0] = f.red.watts[1] = 650;
    f.project();
    assert(f.blue.watts[0] == 1000 && f.blue.watts[1] == 500);
    assert(f.red.watts[0] == 650 && f.red.watts[1] == 650);
    Fixture half;
    half.inputs.soc = 5; half.decision.chargeTargetW = 500;
    half.project();
    assert(half.blue.watts[1] == 500);
    Fixture full;
    full.inputs.soc = 100; full.future.chargeTargetW = 1000; full.future.shortfallWh = 900;
    full.future.targetAt = t + 1800; full.decision.chargeTargetW = 1000;
    full.project();
    assert(full.blue.watts[0] == 0 && full.blue.watts[1] == 0);
    Fixture capped;
    capped.inputs.maxChargeW = 300; capped.inputs.currentChargerW = 250;
    capped.inputs.soc = 0; capped.decision.chargeTargetW = 1000;
    capped.project();
    assert(capped.blue.watts[0] == 250 && capped.blue.watts[1] == 300);
}

static void unknownAndBlocking() {
    Fixture unknown(144, 1);
    unknown.future.chargeTargetW = 100; unknown.future.shortfallWh = 900; unknown.future.targetAt = t + 7200;
    unknown.decision.chargeTargetW = 100;
    unknown.project();
    assert(unknown.blue.watts[0] == 100 && unknown.blue.watts[1] == -1 && unknown.blue.watts[2] == -1);
    assert(unknown.blue.solarWatts[1] == -1 && unknown.blue.solarWatts[2] == 1000);
    unknown.inputs.chargeAvailable = false; unknown.project();
    assert(unknown.blue.valid && unknown.blue.watts[1] == 0 && unknown.blue.watts[143] == 0);
    unknown.inputs.sourcesQualified = false; unknown.project();
    assert(!unknown.blue.valid && unknown.blue.count == 0);
    Fixture tariff(144, -1, 2);
    tariff.project();
    assert(tariff.blue.watts[2] == -1 && tariff.blue.solarWatts[2] == 1000);
    Fixture off; off.inputs.controlEnabled = false; off.project();
    assert(off.blue.valid && off.blue.watts[143] == 0);
}

static void noIntentAndChronologicalBands() {
    Fixture discharging;
    discharging.decision.strategy = "discharge"; discharging.decision.chargeTargetValid = false;
    discharging.project();
    assert(discharging.blue.watts[0] == 0 && discharging.blue.watts[1] == 0);
    Fixture waiting;
    waiting.decision.direct = true; waiting.decision.reason = "source_recovery";
    waiting.decision.chargeTargetValid = false;
    waiting.project();
    assert(waiting.blue.watts[0] == -1 && waiting.blue.watts[1] == -1);
    Fixture bands;
    bands.inputs.soc = 5; bands.decision.chargeTargetW = 500;
    std::vector<int> curve(144, 250000); curve[1] = 200000; curve[2] = 220000;
    bands.tariffs(curve); bands.project();
    assert(bands.blue.watts[1] == 500); // Cheap remains latched below low + deadband/3.
    assert(bands.blue.watts[2] == 0); // Comfort has no upper reserve-band request.
    Fixture expensive;
    expensive.inputs.soc = 0; expensive.inputs.fullCapacityAh = 389;
    expensive.decision.priceBand = "expensive";
    curve[1] = 300000; curve[2] = 280000;
    expensive.tariffs(curve); expensive.project();
    assert(expensive.blue.watts[1] == 0 && expensive.blue.watts[2] == 1000);
    Fixture negative;
    negative.inputs.soc = 1; negative.decision.chargeTargetW = 0;
    curve[1] = -100000;
    negative.tariffs(curve); negative.project();
    assert(negative.blue.watts[1] == 0);
}

static void invalidInputsAndReadOnlyState() {
    Fixture f;
    const auto builds = f.prices.planningBuilds();
    const auto revision = f.prices.revision();
    f.project();
    assert(f.prices.planningBuilds() == builds && f.prices.revision() == revision);
    assert(f.inputs.soc == 20 && f.future.shortfallWh == 0 && f.red.count == 144 && f.red.watts[0] == 0);
    f.inputs.soc = NAN; f.project(); assert(!f.blue.valid && f.blue.count == 0);
    f.inputs.soc = 20; f.red.start = 0; f.project(0);
    assert(!f.blue.valid && f.blue.count == 0);
}

static void serializationBounds() {
    ChargePlan p;
    p.reset(UINT32_MAX, UINT32_MAX, UINT32_MAX); p.start = p.targetAt = UINT32_MAX;
    p.remainingWhDC = 20000; p.count = 144; p.finish(true, "intent_projection");
    for (unsigned i = 0; i < 144; ++i) p.watts[i] = p.solarWatts[i] = 12000;
    char buffer[ChargePlan::kPayloadBytes];
    const auto length = p.serialize(buffer, sizeof(buffer));
    assert(length > 0 && length < sizeof(buffer));
    assert(ChargePlan::packetFits(128, length));
    assert(!ChargePlan::packetFits(2300, length));
    JsonDocument d; assert(!deserializeJson(d, buffer));
    assert(d["w"].size() == 144 && d["s"].size() == 144 && d["kind"] == "intent_projection");
    p.watts[1] = p.solarWatts[1] = -1;
    assert(p.serialize(buffer, sizeof(buffer)) > 0); assert(!deserializeJson(d, buffer));
    assert(d["w"][1].isNull() && d["s"][1].isNull());
    assert(!p.serialize(buffer, 30) && buffer[0] == '\0');
    p.watts[1] = 12001; assert(!p.serialize(buffer, sizeof(buffer)) && !buffer[0]);
    p.finish(false, "source_missing"); assert(p.serialize(buffer, sizeof(buffer)) > 0);
    assert(!deserializeJson(d, buffer)); assert(d["n"] == 0 && d["w"].size() == 0);
    std::cout << "ChargePlan bytes=" << sizeof(ChargePlan) << ", publication bytes=" << sizeof(ChargePlanPublication)
              << ", worst payload=" << length << ", packet with 128-byte topic=" << length + 128 + 7 << "\n";
}

int main() {
    anchorAndDeadline(); partialSlotAndSharedEnergy(); scarcePvBeforePartialDeadline(); reservesAndIndependentDischargeIntent();
    unknownAndBlocking(); noIntentAndChronologicalBands(); invalidInputsAndReadOnlyState(); serializationBounds();
    std::cout << "Charge intent projection tests passed\n";
}
