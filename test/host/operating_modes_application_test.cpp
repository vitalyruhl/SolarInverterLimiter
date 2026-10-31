#include "control/OperatingModes.h"
#include "control/CurrentControl.h"
#include "control/OutputCorrection.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
using std::max;
template<class T> T constrain(T value,T low,T high) {return std::max(low,std::min(value,high));}
// [MOCKED!] Only logging, hardware writes and transport setting are replaced.
enum class LL {Info};
static struct {template<class... T> void logTag(T...) {}} lmg;
static control::ModeDecision modeDecision;
static control::CurrentDecision currentDecision;
static bool currentDecisionAvailable=false,kRs485MockTransport=false,lastIssuedCommandKnown=false;
static int inverterCalculatedValue=0,inverterSetValue=0,configuredMax=900;
static int lastMockBaseCommandW=0,lastMockCommandW=0,lastIssuedBaseCommandW=0,lastIssuedCommandW=0;
static int wire=-1,resets=0;
static void resetPidController() {++resets;}
static void sendToRS485(uint16_t value) {wire=value;}
static struct {struct {bool get() const {return true;}} enableRS485;} rs485settings;
struct SourceFlags {bool genuineBmsFault=false,alarmRestricted=false,softwareMosLockOnly=false;};
#include "operating_modes_application.h"
int main() {
    SourceFlags source;source.genuineBmsFault=true;
    assert(!productionDischargeFault(source)); // A charge-only restriction does not forbid discharge.
    source.alarmRestricted=true;assert(productionDischargeFault(source));
    source.softwareMosLockOnly=true;assert(!productionDischargeFault(source));
    control::ModeSettings cfg;cfg.mode=control::OperatingMode::HouseholdFollow;
    control::ModeInputs in;in.metersValid=true;in.householdW=1000;in.correctionW=-130;in.maximumW=900;
    control::OperatingModePolicy policy;modeDecision=policy.update(cfg,in);
    applyModeDecision();assert(wire==870 && inverterCalculatedValue==870 && resets==1);
    assert(currentDecisionAvailable && lastIssuedBaseCommandW==870 && lastIssuedCommandW==870);
    in.metersValid=false;modeDecision=policy.update(cfg,in);applyModeDecision();assert(wire==0);
    in.metersValid=true;in.inverterEnabled=false;modeDecision=policy.update(cfg,in);applyModeDecision();assert(wire==0);
    modeDecision.handled=false;wire=777;applyModeDecision();assert(wire==777);
    kRs485MockTransport=true;modeDecision.handled=true;modeDecision.watts=100;applyModeDecision();
    assert(lastMockBaseCommandW==100 && lastMockCommandW==100 && wire==777);
    kRs485MockTransport=false;
    assert(control::parseOperatingMode("pv_follow",cfg.mode));
    in.inverterEnabled=true;in.metersValid=true;in.chargerValid=true;in.chargerW=1000;
    in.householdW=600;in.correctionW=-130;
    modeDecision=policy.update(cfg,in);applyModeDecision();assert(wire==470);
    in.chargerW=100;modeDecision=policy.update(cfg,in);applyModeDecision();assert(wire==90);
    in.chargerW=0;modeDecision=policy.update(cfg,in);applyModeDecision();assert(wire==0);
    std::puts("Production manual-mode output application tests passed");
}
