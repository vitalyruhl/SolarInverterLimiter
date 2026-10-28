#include "control/ChargePlan.h"
#include <cassert>
#include <iostream>

// [MOCKED!] Boundary facts feed statements extracted from the actual production composition.
struct StandbySource {
    bool sourcesWithinGrace = false, bmsWithinGrace = true, bmsValid = true, chargerValid = false, chargerStandby = true;
    bool batteryChargeConfirmed = true, batteryChargeStateKnown = true;
    bool softwareMosLockOnly = false;
    float chargerVoltageV = 0, batteryVoltageV = 54;
};

#include "standby_planning_application.h"

int main() {
    StandbySource source;
    source.bmsValid = false;
    assert(productionPlanGrace(source)); // A real MPPT outage cannot erase the BMS display grace on reconnect.
    source.bmsWithinGrace = false;
    assert(!productionPlanGrace(source));
    source.bmsWithinGrace = source.bmsValid = true;
    control::CurrentInputs inputs;
    inputs.bmsValid = inputs.priceValid = inputs.metersValid = true;
    control::CurrentDecision decision; decision.direct = true;
    const auto standby = productionProjection(source, inputs, decision);
    assert(standby.sourcesQualified); // Cold startup or MPPT silence longer than its grace deadline.
    assert(standby.maxChargeW == 1890); // 54 V times the qualified installed 35 A limit.
    assert(productionProjection(source, inputs, decision, true).maxChargeW == 0); // A retained charger fault forbids forecast recharge credit.
    source.batteryVoltageV = 0;
    assert(productionProjection(source, inputs, decision).maxChargeW == 0);
    source.batteryVoltageV = -1;
    assert(productionProjection(source, inputs, decision).maxChargeW == 0);
    source.batteryVoltageV = NAN;
    assert(productionProjection(source, inputs, decision).maxChargeW == 0);
    source.batteryVoltageV = 54;
    source.batteryChargeConfirmed = false;
    assert(productionProjection(source, inputs, decision).maxChargeW == 0);
    source.batteryChargeConfirmed = true;
    inputs.chargerFault = true;
    assert(!productionProjection(source, inputs, decision).sourcesQualified);
    inputs.chargerFault = false;
    inputs.bmsValid = false;
    assert(!productionProjection(source, inputs, decision).sourcesQualified);
    inputs.bmsValid = true;
    source.chargerStandby = false;
    assert(!productionProjection(source, inputs, decision).sourcesQualified);
    decision.direct = false;
    const auto batteryOnly = productionProjection(source, inputs, decision);
    assert(batteryOnly.sourcesQualified && batteryOnly.maxChargeW == 1890);
    decision.direct = true;
    source.chargerValid = source.sourcesWithinGrace = true;
    source.chargerVoltageV = 53;
    assert(!productionProjection(source, inputs, decision).sourcesQualified); // Still recovering.
    decision.direct = false;
    const auto recovered = productionProjection(source, inputs, decision);
    assert(recovered.sourcesQualified && recovered.maxChargeW == 1855);
    std::cout << "[MOCKED!] Production standby plan composition, expiry/startup, fault and recovery gates passed\n";
}
