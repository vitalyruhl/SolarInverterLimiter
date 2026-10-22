#include "control/CurrentControl.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace control;
static void recoveryRestartsDynamicsAtHeldCommand() {
  for (bool economic : {true, false}) {
    PreparedSettings cfg;cfg.valid=true;
    CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;in.batteryDischargeIntentAvailable=true;
    in.soc=50;in.chargerW=2000;in.batteryW=1000;in.price=economic?.4f:.1f;
    CurrentControl c;
    for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;c.update(cfg,in,true,true,900,900,100+i*3000);}
    assert(c.update(cfg,in,true,true,900,900,6200).ceilingW==900);
    in.chargerValid=false;in.sourcesWithinGrace=true;
    auto gap=c.update(cfg,in,true,true,900,100,12000);
    assert(!std::strcmp(gap.reason,"source_grace")&&gap.ceilingW==100&&gap.reserveAllowed);
    in.chargerValid=true;
    for(unsigned i=0;i<2;++i){in.bmsSequence=in.chargerSequence=4+i;
      auto recovery=c.update(cfg,in,true,true,900,100,22000+i*3000);
      assert(!std::strcmp(recovery.reason,"source_recovery")&&recovery.ceilingW==100);}
    in.bmsSequence=in.chargerSequence=6;
    auto qualified=c.update(cfg,in,true,true,900,100,28000);
    assert(qualified.ceilingW==100); // No outage dt/integral or obsolete 900-W ceiling.
    in.bmsSequence=in.chargerSequence=7;
    assert(c.update(cfg,in,true,true,900,100,29000).ceilingW==150);
    in.bmsSequence=in.chargerSequence=8;
    assert(c.update(cfg,in,true,true,900,150,30000).ceilingW==200);
  }
}

static void batteryUnavailablePvOnly() {
  for (unsigned invalid=0; invalid<4; ++invalid) {
    PreparedSettings cfg;cfg.valid=true;
    CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=true;
    in.soc=invalid==0?-1:invalid==1?101:invalid==2?NAN:50;
    in.batteryW=invalid==3?NAN:0;in.chargerW=200;in.householdW=500;
    CurrentControl c;CurrentDecision d;
    for(unsigned i=0;i<3;++i){in.chargerSequence=i+1;d=c.update(cfg,in,false,true,900,900,100+i*3000);}
    assert(d.pvOnly&&!d.direct&&d.ceilingW==200);
  }
  {
    PreparedSettings cfg;cfg.valid=true;CurrentControl c;
    CurrentInputs in;in.chargerValid=in.metersValid=true;in.chargerW=200;in.householdW=500;
    in.chargerSequence=1;
    assert(c.update(cfg,in,true,true,900,900,100).ceilingW==0);
    assert(c.update(cfg,in,true,true,900,900,10000).ceilingW==0);
    in.chargerSequence=2;assert(c.update(cfg,in,true,true,900,900,11000).ceilingW==0);
    in.chargerSequence=3;assert(c.update(cfg,in,true,true,900,900,12000).ceilingW==200);
  }
  for (bool bmsValid : {false, true}) {
    for (bool limiterEnabled : {false, true}) {
      for (bool grace : {false, true}) {
        PreparedSettings cfg; cfg.valid = true;
        CurrentInputs in; in.metersValid = in.chargerValid = true;
        in.bmsValid = bmsValid; in.batteryDischargeIntentAvailable = false;
        in.soc = 100; in.chargerW = 200; in.householdW = 500;
        in.batteryW = 10000; in.priceValid = true; in.price = .8f;
        in.futureValid = true; in.futureBatteryCeilingW = 900; in.sourcesWithinGrace = grace;
        CurrentControl control;
        CurrentDecision decision;
        for (unsigned i=0; i<3; ++i) {
          in.chargerSequence = i + 1; // No BMS sequence advancement is required.
          decision = control.update(cfg, in, limiterEnabled, true, 900, 900, 100+i*3000);
          if (i<2) assert(decision.direct && decision.ceilingW==0);
        }
        assert(decision.pvOnly && !decision.direct && decision.ceilingW==200);
        assert(!decision.reserveAllowed && !decision.chargeTargetValid);
        // A stale/extreme BMS value cannot become PI feedback or discharge permission.
        in.batteryW = -10000;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6200).ceilingW==200);
        in.batteryW = NAN;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6300).ceilingW==200);
        in.householdW = 80;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6400).ceilingW==80);
        in.chargerW = 50;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6500).ceilingW==50);
        in.householdW = -10;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6600).ceilingW==0);
        in.householdW = 500; in.chargerValid = false;
        const auto missing = control.update(cfg,in,limiterEnabled,true,900,900,6700);
        assert(missing.direct && missing.ceilingW==0);
        in.chargerValid=true; in.metersValid=false;
        assert(control.update(cfg,in,limiterEnabled,true,900,900,6800).ceilingW==0);
      }
    }
  }
  PreparedSettings cfg;cfg.valid=true;
  CurrentInputs in;in.bmsValid=in.metersValid=in.chargerValid=true;
  in.soc=90;in.chargerW=200;in.householdW=500;
  in.alarmRestricted=true;in.softwareMosLockOnly=true;
  CurrentControl control;CurrentDecision d;
  for(unsigned i=0;i<3;++i){in.chargerSequence=i+1;d=control.update(cfg,in,true,true,900,900,100+i*3000);}
  assert(d.pvOnly&&!d.direct&&d.ceilingW==200); // Deliberate MOS lock alone permits PV-only.
  in.softwareMosLockOnly=false; // Any other latched discharge alarm retains its stop.
  assert(control.update(cfg,in,true,true,900,900,6200).ceilingW==0);
  in.alarmRestricted=false;in.chargerFault=true;
  assert(!std::strcmp(control.update(cfg,in,true,true,900,900,6300).reason,"charger_fault"));
  in.chargerFault=false;in.batteryDischargeIntentAvailable=true;in.batteryW=0;
  const auto recovering=control.update(cfg,in,true,true,900,0,6400);
  assert(recovering.direct&&recovering.ceilingW==0&&!std::strcmp(recovering.reason,"recovery"));
}
static void missingBmsCanFollowFreshPv() {
  PreparedSettings cfg; cfg.valid = true;
  CurrentInputs in; in.metersValid = in.chargerValid = true;
  in.bmsValid = false; in.chargerW = 200; in.householdW = 500;
  CurrentControl control;
  CurrentDecision decision;
  for (unsigned i = 0; i < 3; ++i) {
    in.chargerSequence = i + 1;
    decision = control.update(cfg, in, true, true, 900, 0, 100 + i * 3000);
  }
  assert(!decision.direct && decision.ceilingW > 0 && decision.ceilingW <= 200);
}

static void chargeOnlyHonorsFutureStorage() {
  for(bool limiterEnabled:{false,true}) {
    PreparedSettings cfg;cfg.valid=true;
    CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
    in.batteryDischargeIntentAvailable=false;in.softwareMosLockOnly=in.alarmRestricted=true;
    in.soc=23;in.price=.2078f;in.chargerW=216;in.householdW=800;in.batteryW=0;
    in.futureValid=true;in.futureBatteryCeilingW=900;in.futureChargeTargetW=400;in.futureReason="forecast_charge";
    CurrentControl control;CurrentDecision decision;
    for(unsigned i=0;i<3;++i) {
      in.bmsSequence=in.chargerSequence=i+1;
      decision=control.update(cfg,in,limiterEnabled,true,900,900,100+i*3000);
    }
    assert(decision.pvOnly&&!decision.direct&&decision.chargeTargetValid);
    assert(!std::strcmp(decision.strategy,"preserve")&&!std::strcmp(decision.reason,"forecast_charge"));
    assert(decision.chargeTargetW==400&&decision.ceilingW==0);
    // Unqualified discharge intent cannot be bypassed by a forecast release or Limiter OFF.
    in.futureReason="forecast_release";in.batteryW=1000;in.price=.8f;
    in.bmsSequence=in.chargerSequence=4;
    decision=control.update(cfg,in,limiterEnabled,true,900,900,9200);
    assert(!std::strcmp(decision.strategy,"preserve")&&decision.ceilingW<=194);
    in.chargerValid=false;in.sourcesWithinGrace=true;
    decision=control.update(cfg,in,limiterEnabled,true,900,900,9300);
    assert(decision.direct&&decision.ceilingW==0&&!decision.chargeTargetValid);
  }
}
static void chargeOnlyPriorityAndRecovery() {
  PreparedSettings cfg;cfg.valid=true;
  CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=23;in.price=.2f;in.chargerW=216;in.householdW=800;in.batteryW=50;
  in.futureValid=true;in.futureChargeTargetW=50;in.futureReason="forecast_charge";
  CurrentControl control;CurrentDecision decision;
  for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;decision=control.update(cfg,in,true,true,900,900,100+i*3000);}
  assert(decision.chargeTargetValid&&decision.ceilingW>0&&decision.ceilingW<=149);
  in.alarmRestricted=true;in.softwareMosLockOnly=false;
  assert(!std::strcmp(control.update(cfg,in,true,true,900,900,6200).reason,"source_alarm"));
  in.softwareMosLockOnly=true;in.chargerFault=true;
  assert(!std::strcmp(control.update(cfg,in,true,true,900,900,6300).reason,"charger_fault"));
  in.chargerFault=false;in.price=-.01f;
  assert(control.update(cfg,in,true,true,900,900,6400).ceilingW==0);
  in.price=.2f;cfg.inverterEnabled=false;
  assert(control.update(cfg,in,true,true,900,900,6500).ceilingW==0);
  cfg.inverterEnabled=true;in.metersValid=false;
  assert(control.update(cfg,in,true,true,900,900,6600).ceilingW==0);
  in.metersValid=true;
  assert(control.update(cfg,in,true,false,900,900,6700).ceilingW==0);
  // Source validity cannot be replaced by a previously true charge capability.
  in.bmsValid=false;in.batteryW=-10000;
  CurrentControl missing;
  for(unsigned i=0;i<3;++i){in.chargerSequence=i+1;decision=missing.update(cfg,in,true,true,900,900,100+i*3000);}
  assert(decision.pvOnly&&!decision.chargeTargetValid&&decision.ceilingW==216);
}
static void manualChargeBlockingDoesNotReserveUnavailableStorage() {
  PreparedSettings cfg;cfg.valid=true;
  CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.batteryChargeAvailable=false;in.alarmRestricted=in.softwareMosLockOnly=true;
  in.soc=23;in.price=.2f;in.chargerW=216;in.householdW=800;
  in.futureValid=true;in.futureChargeTargetW=400;in.futureReason="forecast_charge";
  CurrentControl control;CurrentDecision decision;
  for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;decision=control.update(cfg,in,true,true,900,0,100+i*3000);}
  assert(decision.pvOnly&&!decision.chargeTargetValid&&decision.ceilingW==216);
  // A permitted discharge path still serves a genuine future/current economic allocation.
  in.futureBatteryCeilingW=400;in.futureReason="forecast_release";in.price=.8f;
  for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=4+i;decision=control.update(cfg,in,true,true,900,400,9200+i*3000);}
  assert(!decision.chargeTargetValid&&!std::strcmp(decision.strategy,"discharge")&&decision.ceilingW>=400);
}
static void capabilityChangesDiscardOldBatteryDynamics() {
  PreparedSettings cfg;cfg.valid=true;
  CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=50;in.price=.4f;in.chargerW=2000;in.batteryW=1000;in.householdW=1000;
  CurrentControl control;CurrentDecision decision;
  for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;decision=control.update(cfg,in,true,true,900,900,100+i*3000);}
  assert(decision.ceilingW==900);
  in.batteryDischargeIntentAvailable=false;in.chargerW=216;in.batteryW=0;
  in.futureValid=true;in.futureChargeTargetW=400;in.futureReason="forecast_charge";
  for(unsigned i=0;i<3;++i){
    in.bmsSequence=in.chargerSequence=4+i;decision=control.update(cfg,in,true,true,900,900,9200+i*3000);
    assert(decision.ceilingW==0);
  }
  assert(decision.chargeTargetValid&&decision.pvOnly);
}

int main() {
  chargeOnlyHonorsFutureStorage(); chargeOnlyPriorityAndRecovery(); manualChargeBlockingDoesNotReserveUnavailableStorage(); capabilityChangesDiscardOldBatteryDynamics();
  missingBmsCanFollowFreshPv();
  batteryUnavailablePvOnly();
  recoveryRestartsDynamicsAtHeldCommand();
  PreparedSettings cfg; cfg.valid = true;
  CurrentInputs in; in.metersValid = true; in.batteryDischargeIntentAvailable = true;
  for (int idle : {500, 0, 2000, 100}) {
    cfg.idleOutputW = idle;
    for (unsigned missing : {1u, 2u, 3u}) {
      CurrentControl c;
      in.bmsValid = !(missing & 1); in.chargerValid = !(missing & 2);
      in.soc = 50; in.chargerW = 300;
      auto d = c.update(cfg, in, true, true, 900, 200, 100);
      assert(d.direct && d.ceilingW == ((missing & 1) ? 0 : (idle > 900 ? 900 : idle)));
      assert(!std::strcmp(d.reason, (missing & 1) ? ((missing & 2) ? "pv_missing" : "pv_recovery") : "source_loss"));
    }
  }
  cfg.idleOutputW = 500;
  CurrentControl c;
  in = CurrentInputs{}; in.batteryDischargeIntentAvailable = in.batteryChargeAvailable = true; in.metersValid = true; in.bmsValid = true; in.chargerValid = true;
  in.soc = 50; in.chargerW = 700; in.batteryW = 50;
  in.priceValid = true; in.price = .4;
  for (unsigned i=0;i<3;++i) {
    in.bmsSequence = in.chargerSequence = i+1;
    c.update(cfg,in,true,true,900,200,100+i*3000);
  }
  auto approved = c.update(cfg,in,true,true,900,200,6200);
  in.chargerValid = false; in.sourcesWithinGrace = true;
  auto hold = c.update(cfg,in,true,true,900,200,6300);
  assert(!hold.direct && hold.ceilingW <= approved.ceilingW);
  assert(!std::strcmp(hold.reason, "source_grace"));
  // Repeated stale input cannot raise the prior approved ceiling.
  assert(c.update(cfg,in,true,true,900,200,6400).ceilingW <= hold.ceilingW);
  in.chargerValid = true; in.bmsSequence = in.chargerSequence = 4;
  auto recovering = c.update(cfg,in,true,true,900,200,6450);
  assert(!std::strcmp(recovering.reason, "source_recovery"));
  in.bmsValid = false;
  in.alarmRestricted = true;
  assert(c.update(cfg,in,true,true,900,200,6500).ceilingW == 0);
  in.alarmRestricted = false;
  in.sourcesWithinGrace = false;
  in.price = -.01f;
  assert(c.update(cfg,in,true,true,900,200,6600).ceilingW == 0);
  in.price = .4f; in.bmsValid = true;
  assert(c.update(cfg,in,false,true,900,200,6700).ceilingW == 900);
  cfg.inverterEnabled = false;
  assert(c.update(cfg,in,false,true,900,200,6800).ceilingW == 0);
  cfg.inverterEnabled = true;
  assert(c.update(cfg,in,true,false,900,200,6900).ceilingW == 0);
  in.metersValid = false;
  assert(c.update(cfg,in,true,true,900,200,7000).ceilingW == 0);
  in.metersValid = true; in.bmsValid = true; in.soc = 0;
  assert(c.update(cfg,in,true,true,900,200,7100).ceilingW == 0);
  in.bmsValid = false;
  assert(c.update(cfg,in,true,true,900,200,7200).ceilingW == 0);
  std::cout << "Current control Idle, grace and priority tests passed\n";
}
