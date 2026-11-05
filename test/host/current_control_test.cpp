#include "control/CurrentControl.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace control;
// [MOCKED!] Synthetic source samples exercise the production decision logic without device I/O.
static CurrentInputs qualifiedStorageInputs() {
  CurrentInputs in;
  in.bmsValid = in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = true;
  in.chargerValid = in.metersValid = in.priceValid = true;
  in.soc = 50; in.chargerW = 300; in.householdW = 1000; in.price = .25f;
  in.futureValid = true; in.futureReason = "forecast_hold";
  return in;
}

static CurrentDecision qualifyStorage(CurrentControl &control, const PreparedSettings &cfg,
                                      CurrentInputs &in, unsigned firstSequence = 1,
                                      uint32_t started = 100) {
  CurrentDecision out;
  for (unsigned i = 0; i < 3; ++i) {
    in.bmsSequence = in.chargerSequence = firstSequence + i;
    out = control.update(cfg, in, true, true, 900, 270, started + i * 3000);
  }
  return out;
}

static void reconnectKeepsQualifiedBatteryPermission() {
  PreparedSettings cfg; cfg.valid = true;
  auto in = qualifiedStorageInputs(); CurrentControl control;
  in.chargerW = 0; in.futureBatteryCeilingW = 900;
  for (unsigned i = 0; i < 3; ++i) {
    in.bmsSequence = in.chargerSequence = i + 1;
    control.update(cfg, in, true, true, 900, 900, 100 + i * 3000);
  }
  assert(control.update(cfg, in, true, true, 900, 900, 6200).ceilingW == 900);
  in.bmsValid = in.chargerValid = false;
  in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = false;
  in.sourcesWithinGrace = true;
  in.bmsWithinGrace = true;
  const auto held = control.update(cfg, in, true, true, 900, 900, 8000);
  assert(held.ceilingW == 900 && !held.direct);
}

static void limiterOffWaitsForCompleteBatteryGraceRecovery() {
  PreparedSettings cfg; cfg.valid = true; CurrentControl control;
  auto in = qualifiedStorageInputs(); in.chargerW = 0; in.futureBatteryCeilingW = 900;
  for (unsigned i = 0; i < 3; ++i) {
    in.bmsSequence = in.chargerSequence = i + 1;
    control.update(cfg, in, true, true, 900, 900, 100 + i * 3000);
  }
  in.bmsValid = in.chargerValid = in.batteryDischargeIntentAvailable = in.batteryChargeAvailable = false;
  in.bmsWithinGrace = true;
  assert(control.update(cfg, in, false, true, 900, 200, 8000).sourceGrace);
  in.bmsValid = in.chargerValid = in.batteryDischargeIntentAvailable = in.batteryChargeAvailable = true;
  for (unsigned i = 0; i < 3; ++i) {
    in.bmsSequence = in.chargerSequence = i + 4;
    const auto recovery = control.update(cfg, in, false, true, 900, 200, 10000 + i * 3000);
    if (i < 2) assert(recovery.sourceGrace && recovery.ceilingW == 200 && !recovery.direct);
    else assert(!recovery.sourceGrace && recovery.ceilingW == 900 && recovery.direct && !std::strcmp(recovery.reason,"limiter_off"));
  }
  CurrentControl ordinary;
  const auto bypass = ordinary.update(cfg, in, false, true, 900, 200, 20000);
  assert(bypass.direct && bypass.ceilingW == 900 && !bypass.sourceGrace);
}

static void batteryGraceCannotCreatePermissionOrSurviveAStop() {
  PreparedSettings cfg; cfg.valid = true;
  for (int fault = 0; fault < 8; ++fault) {
    CurrentControl control; auto in = qualifiedStorageInputs();
    in.chargerW = 0; in.futureBatteryCeilingW = 900;
    for (unsigned i = 0; i < 3; ++i) {
      in.bmsSequence = in.chargerSequence = i + 1;
      control.update(cfg, in, true, true, 900, 900, 100 + i * 3000);
    }
    in.bmsValid = in.chargerValid = false;
    in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = false;
    in.bmsWithinGrace = true; in.futureValid = false;
    assert(control.update(cfg, in, true, true, 900, 900, 8000).sourceGrace);
    auto settings = cfg; bool transport = true;
    if (fault == 0) in.bmsWithinGrace = false;
    if (fault == 1) in.alarmRestricted = true;
    if (fault == 2) settings.inverterEnabled = false;
    if (fault == 3) transport = false;
    if (fault == 4) in.price = -.01f;
    if (fault == 5) in.metersValid = false;
    if (fault == 6) in.chargerFault = true;
    if (fault == 7) { in.bmsValid = true; in.soc = 1; }
    auto stopped = control.update(settings, in, true, transport, 900, 900, 10000);
    assert(stopped.ceilingW == 0 && !stopped.sourceGrace);
    in = qualifiedStorageInputs(); in.bmsValid = in.chargerValid = false;
    in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = false;
    in.bmsWithinGrace = true;
    assert(!control.update(cfg, in, true, true, 900, 900, 12000).sourceGrace);
  }
  CurrentControl startup; auto absent = qualifiedStorageInputs();
  absent.bmsValid = absent.chargerValid = false; absent.bmsWithinGrace = true;
  assert(!startup.update(cfg, absent, true, true, 900, 900, 100).sourceGrace);
  for (bool fullBattery : {false, true}) {
    CurrentControl pv; auto in = qualifiedStorageInputs();
    in.soc = fullBattery ? 100 : 50;
    in.batteryDischargeIntentAvailable = fullBattery;
    in.chargerW = 1000;
    qualifyStorage(pv, cfg, in);
    in.bmsValid = in.chargerValid = false;
    in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = false;
    in.bmsWithinGrace = true;
    assert(!pv.update(cfg, in, true, true, 900, 900, 8000).sourceGrace);
  }
}

static void batteryGraceExcludesOldPvAndAllowsPartialRecovery() {
  PreparedSettings cfg; cfg.valid = true; CurrentControl control;
  auto in = qualifiedStorageInputs();
  in.chargerW = 1000; in.futureBatteryCeilingW = 50;
  for (unsigned i = 0; i < 3; ++i) {
    in.bmsSequence = in.chargerSequence = i + 1;
    control.update(cfg, in, true, true, 900, 900, 100 + i * 3000);
  }
  in.bmsValid = in.chargerValid = false;
  in.batteryChargeAvailable = in.batteryDischargeIntentAvailable = false;
  in.bmsWithinGrace = true; in.futureValid = false;
  auto held = control.update(cfg, in, true, true, 900, 900, 8000);
  assert(held.sourceGrace && held.ceilingW == 50 && !held.minimumOverrideAllowed && !held.chargeTargetValid);
  // SOC/power can arrive before MOS/permission fields; this must not discard grace.
  in.bmsValid = true;
  held = control.update(cfg, in, true, true, 900, 50, 9000);
  assert(held.sourceGrace && held.ceilingW == 50);
  in.batteryDischargeIntentAvailable = true; in.bmsSequence = 4;
  held = control.update(cfg, in, true, true, 900, 50, 10000);
  assert(held.sourceGrace && held.ceilingW == 50);
  in.bmsSequence = 5;
  assert(control.update(cfg, in, true, true, 40, 50, 13000).ceilingW == 40);
  in.bmsSequence = 6; in.price = .4f;
  const auto recovered = control.update(cfg, in, true, true, 900, 40, 16000);
  assert(!recovered.sourceGrace && recovered.ceilingW == 40);
  // A later new, restrictive forecast still takes effect within a held interval.
  in.bmsValid = false; in.batteryDischargeIntentAvailable = false;
  in.futureValid = true; in.futureBatteryCeilingW = 20;
  assert(control.update(cfg, in, true, true, 900, 40, 18000).ceilingW == 20);
}

static void noUnplannedChargingFloor() {
  PreparedSettings cfg; cfg.valid = true;
  auto in = qualifiedStorageInputs(); CurrentControl control;
  const auto out = qualifyStorage(control, cfg, in);
  assert(out.chargeTargetValid && out.chargeTargetW == 0);
  assert(out.ceilingW == 270);
}

static void reserveChargingUsesPriceAndSoc() {
  for (bool releasedBefore : {false, true}) {
    for (float soc : {1.9f, 2.0f, 2.1f, 5.9f, 6.0f, 6.1f}) {
      for (float price : {.10f, .25f, .50f}) {
        PreparedSettings cfg; cfg.valid = true;
        cfg.reserveOffSocPercent = 2; cfg.reserveOnSocPercent = 6;
        CurrentControl control; auto in = qualifiedStorageInputs();
        if (releasedBefore) qualifyStorage(control, cfg, in);
        in.soc = soc; in.price = price;
        const auto out = qualifyStorage(control, cfg, in, 4, 10100);
        const bool critical = soc <= 2 && price < .5f;
        const bool refill = soc > 2 && soc < 6 && price == .10f;
        const float target = critical ? 300.0f : (refill ? 150.0f : 0.0f);
        assert(out.chargeTargetValid && out.chargeTargetW == target);
        assert(out.reserveAllowed == (soc >= 6 || (releasedBefore && soc > 2)));
        if (critical) assert(out.ceilingW == 0);
        if (refill) assert(!std::strcmp(out.reason, "reserve_on_charge"));
        if (soc <= 2) assert(!out.minimumOverrideAllowed);
      }
    }
  }
}

static void economicChargingAddsToReservePolicy() {
  PreparedSettings cfg; cfg.valid = true;
  cfg.reserveOffSocPercent = 2; cfg.reserveOnSocPercent = 6;
  for (float soc : {1.9f, 4.0f, 50.0f}) {
    CurrentControl control; auto in = qualifiedStorageInputs();
    in.soc = soc; in.price = .10f;
    in.futureChargeTargetW = 400; in.futureReason = "forecast_charge";
    const auto out = qualifyStorage(control, cfg, in);
    assert(out.chargeTargetValid && out.chargeTargetW == 400);
    assert(out.ceilingW == 0);
  }
}

static void criticalReserveResetsPriorPositiveFeedback() {
  PreparedSettings cfg; cfg.valid = true;
  cfg.reserveOffSocPercent = 2; cfg.reserveOnSocPercent = 6;
  CurrentControl control; auto in = qualifiedStorageInputs();
  in.batteryW = 1000;
  assert(qualifyStorage(control, cfg, in).ceilingW > 0);
  in.soc = 1.9f;
  const auto out = qualifyStorage(control, cfg, in, 4, 10100);
  assert(out.chargeTargetW == 300 && out.ceilingW == 0);
}

static void halfReserveChargingEndsAtReserveOn() {
  PreparedSettings cfg; cfg.valid = true;
  cfg.reserveOffSocPercent = 2; cfg.reserveOnSocPercent = 6;
  CurrentControl control; auto in = qualifiedStorageInputs();
  in.batteryW = 500; in.futureChargeTargetW = 400;
  qualifyStorage(control, cfg, in);
  in.soc = 4; in.price = .10f; in.batteryW = 100;
  in.futureChargeTargetW = 0;
  const auto half = qualifyStorage(control, cfg, in, 4, 10100);
  assert(half.chargeTargetW == 150 && half.ceilingW <= 270);
  assert(half.reserveAllowed && half.minimumOverrideAllowed);
  in.soc = 6;
  const auto released = qualifyStorage(control, cfg, in, 7, 20100);
  assert(released.reserveAllowed && released.chargeTargetW == 0);
  assert(std::strcmp(released.reason, "reserve_on_charge"));
}

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

static void batteryPlanningSurvivesMissingMppt() {
  PreparedSettings cfg; cfg.valid = true; cfg.idleOutputW = 500;
  for (int futureW : {0, 120, 900}) {
    CurrentControl control; auto in = qualifiedStorageInputs();
    in.chargerValid = false; in.chargerSequence = 1;
    in.chargerW = 3000; // Stale/invalid charger power must not become direct PV credit.
    in.futureBatteryCeilingW = futureW;
    control::CurrentDecision out;
    for (unsigned i = 0; i < 3; ++i) {
      in.bmsSequence = i + 1;
      out = control.update(cfg, in, true, true, 900, 900, 100 + i * 3000);
    }
    assert(out.ceilingW == futureW);
    assert(!std::strcmp(out.strategy, futureW ? "discharge" : "preserve"));
  }
}

static void forecastCeilingSurvivesSourceLoss() {
  PreparedSettings cfg; cfg.valid = true; cfg.idleOutputW = 500;
  for (int futureW : {0, 120, 900}) {
    CurrentControl control; auto in = qualifiedStorageInputs();
    in.chargerValid = false; in.futureBatteryCeilingW = futureW;
    const auto out = control.update(cfg, in, true, true, 900, 500, 100);
    assert(out.ceilingW <= futureW && out.ceilingW != 500);
  }
  CurrentControl control; auto in = qualifiedStorageInputs();
  in.futureBatteryCeilingW = 600; in.futureReason = "forecast_release";
  const auto approved = qualifyStorage(control, cfg, in);
  assert(approved.ceilingW > 0);
  in.chargerValid = false; in.sourcesWithinGrace = true;
  in.futureBatteryCeilingW = 0; in.futureReason = "forecast_hold";
  assert(control.update(cfg, in, true, true, 900, 500, 7000).ceilingW == 0);
  in.chargerValid = true; in.bmsSequence = in.chargerSequence = 4;
  assert(control.update(cfg, in, true, true, 900, 500, 7100).ceilingW == 0);
}

static void unavailableMpptWithDischargeMosOffHoldsOutput() {
  PreparedSettings cfg; cfg.valid = true; cfg.idleOutputW = 500;
  for (bool limiterEnabled : {false, true}) for (bool forecast : {false, true}) {
    CurrentControl control; auto in = qualifiedStorageInputs();
    in.chargerValid = false; in.chargerStandby = true;
    in.futureValid = forecast; in.futureBatteryCeilingW = 900;
    const auto out = control.update(cfg, in, limiterEnabled, true, 900, 500, 100);
    assert(out.direct && out.ceilingW == 0);
    assert(!std::strcmp(out.reason, "mppt_standby"));
    in.chargerFault = true;
    assert(!std::strcmp(control.update(cfg, in, limiterEnabled, true, 900, 500, 200).reason, "charger_fault"));
  }
}

static void fullBatteryRestartsCurtailedPv() {
  PreparedSettings cfg; cfg.valid = true;
  auto in = qualifiedStorageInputs(); CurrentControl control;
  in.soc = 100; in.chargerW = 0; in.batteryW = 0;
  const auto out = qualifyStorage(control, cfg, in);
  assert(!out.direct && out.ceilingW > 0);
  assert(!std::strcmp(out.reason, "full_battery_pv"));
  assert(out.pvOnly && !out.minimumOverrideAllowed);
  assert(out.chargeTargetValid && out.chargeTargetW == 0);
  assert(out.ceilingW == 900);
  in.soc = 99;
  auto resumed = qualifyStorage(control, cfg, in, 4, 10100);
  assert(std::strcmp(resumed.reason, "full_battery_pv"));
  assert(resumed.ceilingW == 0);
  in.soc = 100; in.futureChargeTargetW = 500;
  assert(qualifyStorage(control, cfg, in, 7, 20100).chargeTargetW == 0);
  auto chargeProtected = in; chargeProtected.batteryChargeAvailable = false;
  CurrentControl chargeOnlyFault;
  assert(!std::strcmp(qualifyStorage(chargeOnlyFault, cfg, chargeProtected).reason, "full_battery_pv"));
  auto badMeter = in; badMeter.householdW = NAN; CurrentControl meterFault;
  assert(qualifyStorage(meterFault, cfg, badMeter).ceilingW == 0);
  for (unsigned gate = 0; gate < 7; ++gate) {
    auto blocked = in; auto settings = cfg; CurrentControl guarded;
    if (gate == 0) blocked.alarmRestricted = true;
    if (gate == 1) blocked.chargerFault = true;
    if (gate == 2) settings.inverterEnabled = false;
    if (gate == 3) blocked.metersValid = false;
    if (gate == 4) blocked.price = -.01f;
    if (gate == 5) blocked.batteryDischargeIntentAvailable = false;
    if (gate == 6) settings.valid = false;
    const auto stopped = qualifyStorage(guarded, settings, blocked);
    assert(stopped.ceilingW == 0);
    assert(std::strcmp(stopped.reason, "full_battery_pv"));
  }
  CurrentControl recovering;
  auto first = recovering.update(cfg, in, true, true, 900, 0, 100);
  assert(first.direct && first.ceilingW == 0);
  assert(recovering.update(cfg, in, true, false, 900, 0, 6200).ceilingW == 0);
  CurrentControl grace;
  in.futureValid = false;
  assert(qualifyStorage(grace, cfg, in).ceilingW == 900);
  in.soc = 99; in.chargerValid = false; in.sourcesWithinGrace = true;
  assert(grace.update(cfg, in, true, true, 900, 500, 6300).ceilingW == 0);
  in.chargerValid = true;
  assert(grace.update(cfg, in, true, true, 900, 500, 6400).ceilingW == 0);
}

int main() {
  fullBatteryRestartsCurtailedPv();
  batteryPlanningSurvivesMissingMppt();
  forecastCeilingSurvivesSourceLoss();
  unavailableMpptWithDischargeMosOffHoldsOutput();
  noUnplannedChargingFloor(); reserveChargingUsesPriceAndSoc(); economicChargingAddsToReservePolicy();
  criticalReserveResetsPriorPositiveFeedback();
  halfReserveChargingEndsAtReserveOn();
  chargeOnlyHonorsFutureStorage(); chargeOnlyPriorityAndRecovery(); manualChargeBlockingDoesNotReserveUnavailableStorage(); capabilityChangesDiscardOldBatteryDynamics();
  missingBmsCanFollowFreshPv();
  batteryUnavailablePvOnly();
  reconnectKeepsQualifiedBatteryPermission();
  limiterOffWaitsForCompleteBatteryGraceRecovery();
  batteryGraceCannotCreatePermissionOrSurviveAStop();
  batteryGraceExcludesOldPvAndAllowsPartialRecovery();
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
      assert(d.direct && d.ceilingW == 0);
      assert(!std::strcmp(d.reason, (missing & 1) ? ((missing & 2) ? "pv_missing" : "pv_recovery") : "recovery"));
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
