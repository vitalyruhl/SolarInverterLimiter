#include "control/CurrentControl.h"
#include "control/OutputCorrection.h"
#include "control/NumericSafety.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
using std::min;
using std::max;
template<class T> T constrain(T n,T low,T high){return std::max(low,std::min(n,high));}
// [MOCKED!] Arduino time, settings, logging and the physical RS485 write only.
// Production PID, final branch application and central issueInverterCommand are extracted unchanged.
static unsigned long clockMs=100;
static unsigned long millis(){return clockMs;}
template<class T> struct Setting {T value;T get()const{return value;}};
static struct {
 Setting<float> RS232PublishPeriod{2},pidKp{.45f},pidKi{.05f},pidKd{.02f};
 Setting<bool> enableController{true};
} limiterSettings;
static struct {
 bool initialized=false;float output=0,integral=0,previousError=0;unsigned long lastUpdateMs=0;
} pidController;
enum class LL {Info,Trace};
static struct {template<class... T> void logTag(T...) {}} lmg;
static control::CurrentDecision currentDecision;
static int configuredMin=500,configuredMax=900,negativePriceOverrideTarget=-1;
static int inverterCalculatedValue=0,inverterSetValue=0,issued=-1;
static int currentInverterOutputW=20,signedGridPowerW=60,offset=50;
static int pidBaseTarget=0,pidClampedBaseTarget=0,pidInput=0,gridImportW=60,gridExportW=0;
static bool kRs485MockTransport=false,lastIssuedCommandKnown=false;
static int lastMockCommandW=0,lastIssuedCommandW=0;
static int lastMockBaseCommandW=0,lastIssuedBaseCommandW=0;
static struct {Setting<bool> enableRS485{true};} rs485settings;
static void sendToRS485(uint16_t watts){
 // Observe the signed command before its wire cast as well as the actual wire value.
 assert(inverterSetValue>=0&&inverterSetValue<=configuredMax);
 assert(int(watts)==inverterSetValue);
 issued=watts;
}
#include "current_control_application.h"

static void exactOutputBoundaries(){
 using control::correctOutputCommand;
 assert(correctOutputCommand(0,50,900)==0);
 assert(correctOutputCommand(1,50,900)==51);
 assert(correctOutputCommand(1,-50,900)==0);
 assert(correctOutputCommand(50,-100,900)==0);
 assert(correctOutputCommand(0,-50,900)==0);
 assert(correctOutputCommand(900,50,900)==900);
 assert(correctOutputCommand(900,-50,900)==900);
 assert(correctOutputCommand(200,-50,900)==150);
 assert(correctOutputCommand(80,-50,900)==30);
 assert(correctOutputCommand(1,INT_MAX,900)==900);
 assert(correctOutputCommand(899,INT_MIN,900)==0);
 assert(correctOutputCommand(-1,50,900)==0);
 assert(correctOutputCommand(1,50,0)==0);
 issueInverterCommand(0,900,50);assert(issued==0&&inverterSetValue==0);
 issueInverterCommand(1,900,50);assert(issued==51&&inverterSetValue==51);
 assert(lastIssuedCommandKnown&&lastIssuedCommandW==51);
 issueInverterCommand(1,900,-50);assert(issued==0);
 issueInverterCommand(50,900,-100);assert(issued==0);
 issueInverterCommand(0,900,-50);assert(issued==0);
 issueInverterCommand(1,900,INT_MAX);assert(issued==900&&lastIssuedBaseCommandW==1);
 issueInverterCommand(899,900,INT_MIN);assert(issued==0&&lastIssuedBaseCommandW==899);
 issueInverterCommand(900,900,-50);assert(issued==900&&lastIssuedCommandW==900&&lastIssuedBaseCommandW==900);
 kRs485MockTransport=true;issueInverterCommand(1,900,50);
 assert(lastMockCommandW==51&&lastMockBaseCommandW==1&&inverterSetValue==51&&issued==900);
 kRs485MockTransport=false;
 rs485settings.enableRS485.value=false;
 issueInverterCommand(123,900,50);
 assert(lastIssuedBaseCommandW==900&&lastIssuedCommandW==900);
 rs485settings.enableRS485.value=true;
}
static void allFinalBranches(){
 offset=50;configuredMin=0;negativePriceOverrideTarget=-1;
 currentDecision={};currentDecision.direct=true;currentDecision.ceilingW=0;
 applyDecision();assert(issued==0);
 currentDecision.ceilingW=1;applyDecision();assert(issued==51);
 currentDecision.ceilingW=900;applyDecision();assert(issued==900);
 currentDecision={};currentDecision.ceilingW=900;
 currentInverterOutputW=20;signedGridPowerW=80;limiterSettings.enableController.value=true;
 resetPidController();applyDecision();
 assert(pidBaseTarget==100&&inverterCalculatedValue==100&&issued==150);
 // Existing negative-price override passes the same single final correction point.
 currentDecision.ceilingW=200;negativePriceOverrideTarget=100;
 applyDecision();assert(inverterCalculatedValue==100&&issued==150);
 negativePriceOverrideTarget=0;applyDecision();assert(issued==0);
 negativePriceOverrideTarget=-1;currentDecision={};
 limiterSettings.enableController.value=false;applyDecision();assert(issued==900);
}
static void pvOnlyCorrectionOnce(){
 for(bool enabled:{true,false})for(int correction:{-50,0,50}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.chargerValid=in.metersValid=true;
  in.chargerW=200;in.householdW=80;
  control::CurrentControl c;
  currentInverterOutputW=20;signedGridPowerW=60;offset=correction;configuredMin=500;
  limiterSettings.enableController.value=enabled;resetPidController();
  for(unsigned i=0;i<3;++i){
   in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=c.update(cfg,in,enabled,true,configuredMax,0,clockMs);applyDecision();
   if(i<2)assert(issued==0);
  }
  assert(currentDecision.pvOnly&&inverterCalculatedValue==80&&issued==80+correction);
  assert(pidBaseTarget==80); // No offset in the input or PV budget.
  in.chargerW=50;clockMs+=2000;
  currentDecision=c.update(cfg,in,enabled,true,configuredMax,issued,clockMs);
  applyDecision();assert(inverterCalculatedValue==50&&issued==std::max(0,50+correction));
  in.chargerW=0;clockMs+=2000;
  currentDecision=c.update(cfg,in,enabled,true,configuredMax,issued,clockMs);
  applyDecision();assert(issued==0); // Positive bias never lifts a missing-PV stop.
 }
 for(bool houseLimited:{true,false}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.chargerValid=in.metersValid=true;
  in.chargerW=houseLimited?1000:200;in.householdW=houseLimited?100:1000;
  currentInverterOutputW=0;signedGridPowerW=int(in.householdW);
  offset=houseLimited?-50:50;configuredMin=0;resetPidController();
  limiterSettings.enableController.value=true;control::CurrentControl c;
  for(unsigned i=0;i<3;++i){
   in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=c.update(cfg,in,true,true,900,0,clockMs);applyDecision();
  }
  assert(issued==(houseLimited?50:250)); // 100-50 and 200+50, after the complete calculation.
 }
}
static void heldBaseSurvivesRepeatedGraceAndRecovery(){
 for(bool mock:{false,true})for(bool economic:{false,true})for(int correction:{-50,50}){
  control::PreparedSettings cfg;cfg.valid=true;control::CurrentControl c;
  control::CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=50;in.chargerW=2000;in.batteryW=1000;in.price=economic?.4f:.1f;
  for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;c.update(cfg,in,true,true,900,900,100+i*3000);}
  assert(c.update(cfg,in,true,true,900,900,6200).ceilingW==900);
  kRs485MockTransport=mock;offset=correction;configuredMin=0;configuredMax=900;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=true;
  currentInverterOutputW=200;signedGridPowerW=0;resetPidController();
  issueInverterCommand(200,900,offset);
  assert(inverterSetValue==200+offset);
  in.chargerValid=false;in.sourcesWithinGrace=true;
  for(unsigned i=0;i<3;++i){
   clockMs=8000+i*2000;
   currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);
   assert(!std::strcmp(currentDecision.reason,"source_grace"));
   applyDecision();assert(inverterSetValue==200+offset);
  }
  in.chargerValid=true;
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=4+i;clockMs=16000+i*3000;
   currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);
   if(i<2)assert(!std::strcmp(currentDecision.reason,"source_recovery"));
   assert(currentDecision.ceilingW==200);
   applyDecision();assert(inverterSetValue==200+offset);
  }
  // Qualified PI/economic ramps also restart from the raw held command.
  in.bmsSequence=in.chargerSequence=7;clockMs+=1000;
  currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);
  assert(currentDecision.ceilingW==250);
 }
 kRs485MockTransport=false;
}

static void chargeOnlyForecastAppliedThroughRealPid(){
 for(bool enabled:{false,true})for(int correction:{-50,0,50}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=23;in.price=.2078f;in.chargerW=216;in.batteryW=0;in.householdW=800;
  in.alarmRestricted=in.softwareMosLockOnly=true;
  in.futureValid=true;in.futureChargeTargetW=400;in.futureReason="forecast_charge";
  control::CurrentControl control;offset=correction;configuredMin=500;configuredMax=900;
  currentInverterOutputW=179;signedGridPowerW=621;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=enabled;resetPidController();
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=control.update(cfg,in,enabled,true,900,900,clockMs);applyDecision();
   assert(issued==0); // No output bias or Limiter OFF bypass consumes the reserved PV.
  }
  assert(!currentDecision.direct&&currentDecision.pvOnly&&currentDecision.chargeTargetValid);
  assert(currentDecision.chargeTargetW==400&&!std::strcmp(currentDecision.reason,"forecast_charge"));
 }
}

static void dischargeOffStillSendsPlannedInverterRequest(){
 for(bool chargeAvailable:{false,true}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  // [MOCKED!] Actual discharge MOS OFF, known healthy BMS: intent remains eligible.
  in.batteryChargeAvailable=chargeAvailable;in.soc=50;in.price=.8f;in.chargerW=100;in.householdW=800;
  in.alarmRestricted=in.softwareMosLockOnly=true;
  in.futureValid=true;in.futureBatteryCeilingW=600;in.futureReason="forecast_release";
  control::CurrentControl controller;offset=0;configuredMin=0;configuredMax=900;
  currentInverterOutputW=100;signedGridPowerW=700;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=true;resetPidController();
  for(unsigned i=0;i<4;++i){
   in.bmsSequence=in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=controller.update(cfg,in,true,true,900,600,clockMs);applyDecision();
  }
  assert(!currentDecision.pvOnly&&!std::strcmp(currentDecision.strategy,"discharge"));
  assert(currentDecision.ceilingW>=600&&issued>in.chargerW); // Real extracted PID and TX path, not display-only.
  in.futureBatteryCeilingW=0;in.futureChargeTargetW=400;in.futureReason="forecast_charge";
  in.chargerW=216;in.batteryW=0;in.price=.2f;in.bmsSequence=in.chargerSequence=5;
  currentDecision=controller.update(cfg,in,true,true,900,600,clockMs+1);applyDecision();
  assert(currentDecision.chargeTargetValid==chargeAvailable);
  if(chargeAvailable)assert(currentDecision.ceilingW==0&&issued==0); // Cheap period still reserves PV.
  else assert(currentDecision.ceilingW==216); // Charge OFF cannot reserve unavailable storage.
  in.softwareMosLockOnly=false;
  currentDecision=controller.update(cfg,in,true,true,900,600,clockMs+2);applyDecision();
  assert(issued==0&&!std::strcmp(currentDecision.reason,"source_alarm"));
 }
}
static void configuredMinimumOverridesEconomicsAfterCorrection(){
 // [MOCKED!] Healthy source snapshots and physical transport; real control/PID/application.
 for(bool chargeAvailable:{false,true})for(int correction:{-50,0,50})for(int pv:{0,129}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.batteryChargeAvailable=chargeAvailable;in.soc=12;in.price=.346f;in.chargerW=float(pv);in.batteryW=33;in.householdW=100;
  in.futureValid=true;in.futureReason="forecast_hold";
  in.alarmRestricted=in.softwareMosLockOnly=true; // Healthy actual MOS OFF retains discharge intent.
  control::CurrentControl c;offset=correction;configuredMin=700;configuredMax=900;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=true;
  currentInverterOutputW=65;signedGridPowerW=35;resetPidController();
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=c.update(cfg,in,true,true,900,0,clockMs);applyDecision();
   if(i<2)assert(issued==0);
  }
  assert(issued==(correction==50?750:700));
  assert(currentDecision.ceilingW<700); // Original planner/PV ceiling remains observable.
  assert(!std::strcmp(currentDecision.reason,"minimum_output"));
  assert(currentDecision.reserveAllowed&&!currentDecision.pvOnly);
  assert(!std::strcmp(currentDecision.strategy,"minimum"));
  assert(!currentDecision.chargeTargetValid);
  configuredMin=0;resetPidController();
  currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),++clockMs);applyDecision();
  assert(issued<700); // A previous floor is not cached as a new configured minimum.
 }
}

static void minimumNeverOverridesRestrictions(){
 // [MOCKED!] Source state/clock/transport; each case reuses real decision and final application.
 for(unsigned fault=0;fault<10;++fault){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=50;in.price=.2f;in.chargerW=129;in.batteryW=33;in.householdW=100;
  in.futureValid=true;in.futureReason="forecast_hold";
  if(fault==0)in.soc=1; // Reserve blocks economic MIN, but qualified PV can remain available.
  if(fault==1)in.alarmRestricted=true;
  if(fault==2)in.chargerFault=true;
  if(fault==3)in.price=-.1f;
  if(fault==4)in.metersValid=false;
  if(fault==5)cfg.inverterEnabled=false;
  if(fault==6)cfg.valid=false;
  if(fault==7)in.bmsValid=false;
  if(fault==8)in.batteryDischargeIntentAvailable=false;
  control::CurrentControl c;configuredMin=700;configuredMax=900;offset=50;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=true;resetPidController();
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=c.update(cfg,in,true,fault!=9,900,0,clockMs);applyDecision();
   assert(issued<700);
   assert(!currentDecision.minimumOverrideAllowed);
  }
  if((fault>=1&&fault<=6) || fault==9)assert(issued==0);
 }
}

static void minimumPermissionDoesNotSurviveSourceGrace(){
 for(int correction:{-50,50}){
  control::PreparedSettings cfg;cfg.valid=true;
  control::CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;
  in.soc=50;in.price=.2f;in.chargerW=129;in.batteryW=33;in.householdW=100;
  in.futureValid=true;in.futureReason="forecast_hold";
  control::CurrentControl c;configuredMin=700;configuredMax=900;offset=correction;
  negativePriceOverrideTarget=-1;limiterSettings.enableController.value=true;resetPidController();
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=i+1;clockMs=100+i*3000;
   currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);applyDecision();
  }
  assert(issued>=700);
  in.chargerValid=false;in.sourcesWithinGrace=true;
  for(unsigned i=0;i<3;++i){
   clockMs+=100;currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);applyDecision();
   assert(!currentDecision.minimumOverrideAllowed&&issued<700);
   assert(!std::strcmp(currentDecision.reason,"source_grace"));
  }
  in.chargerValid=true;
  for(unsigned i=0;i<3;++i){
   in.bmsSequence=in.chargerSequence=i+4;clockMs+=3000;
   currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),clockMs);applyDecision();
   if(i<2)assert(!currentDecision.minimumOverrideAllowed&&issued<700);
   else assert(currentDecision.minimumOverrideAllowed&&issued>=700);
  }
  in.chargerValid=false;in.sourcesWithinGrace=false;
  currentDecision=c.update(cfg,in,true,true,900,heldControlCommand(),++clockMs);applyDecision();
  assert(!currentDecision.minimumOverrideAllowed&&issued<700); // Direct Idle is not lifted.
 }
}

static void minimumPreservesMaxAndNegativeOverrides(){
 configuredMax=900;configuredMin=700;offset=-50;
 currentDecision={};currentDecision.minimumOverrideAllowed=true;currentDecision.ceilingW=0;
 limiterSettings.enableController.value=true;negativePriceOverrideTarget=0;resetPidController();
 applyDecision();assert(issued==0); // Explicit zero never inherits economic eligibility.
 negativePriceOverrideTarget=-1;currentDecision={};currentDecision.minimumOverrideAllowed=true;
 currentDecision.direct=true;currentDecision.ceilingW=900;applyDecision();assert(issued==900);
 currentDecision.direct=false;currentDecision.ceilingW=0;configuredMin=900;
 resetPidController();applyDecision();assert(issued==900); // MIN=MAX and negative offset preserve MAX.
 configuredMin=INT_MAX;resetPidController();applyDecision();assert(issued==900); // Final floor cannot exceed MAX.
 configuredMin=-1;resetPidController();applyDecision();assert(issued==0);
 configuredMin=0;offset=50;currentDecision.ceilingW=0;resetPidController();applyDecision();assert(issued==0);
 configuredMin=700;currentDecision={};currentDecision.ceilingW=0;
 limiterSettings.enableController.value=false;applyDecision();assert(issued==900);
}

static void invalidPidArithmeticStopsWithoutConversionOverflow(){
 // [MOCKED!] Corrupt numeric settings; execute the extracted production PID.
 const float kp=limiterSettings.pidKp.value;
 limiterSettings.pidKp.value=NAN;resetPidController();
 assert(computePidStabilizedTarget(500,0,900)==0);
 assert(!pidController.initialized);
 configuredMin=700;configuredMax=900;offset=0;negativePriceOverrideTarget=-1;
 limiterSettings.enableController.value=true;
 currentDecision={};currentDecision.ceilingW=900;currentDecision.minimumOverrideAllowed=true;
 applyDecision();assert(issued==0&&currentDecision.direct);
 configuredMin=0;
 limiterSettings.pidKp.value=kp;
 resetPidController();
 assert(computePidStabilizedTarget(INT_MAX,0,INT_MAX)==0);
 assert(!pidController.initialized);
}

int main(){
 invalidPidArithmeticStopsWithoutConversionOverflow();
 minimumNeverOverridesRestrictions();
 minimumPermissionDoesNotSurviveSourceGrace();
 minimumPreservesMaxAndNegativeOverrides();
 configuredMinimumOverridesEconomicsAfterCorrection();
 dischargeOffStillSendsPlannedInverterRequest();
 chargeOnlyForecastAppliedThroughRealPid();
 exactOutputBoundaries();allFinalBranches();pvOnlyCorrectionOnce();heldBaseSurvivesRepeatedGraceAndRecovery();
 std::puts("PASS: real control/PID/TX MIN override, economic zero, intent, restrictions, grace/recovery, correction once and MAX");
}
