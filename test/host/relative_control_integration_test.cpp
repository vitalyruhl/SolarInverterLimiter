#include "control/FuturePrices.h"
#include "control/CurrentControl.h"
#include "control/OperatingModes.h"
#include "control/ShadowProfile.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using namespace control;
static constexpr uint32_t start=1792454400;
static FuturePrices futurePrices;
static CurrentControl currentControl;
static CurrentDecision currentDecision;
static CurrentInputs inputs;
static ModeInputs modeInputs;
static OperatingModePolicy modePolicy;
static ModeDecision modeDecision;
static bool modePublishDirty=false;
static struct {ModeSettings settings;const ModeSettings& confirmed(){return settings;}} operatingModes;
#include "relative_control_bindings.h"
static void freshPrices(uint32_t utc){
 for(unsigned k=0;k<6;++k){JsonDocument d;d["v"]=2;d["r"]=utc;d["t"]=start;d["d"]=900;d["n"]=144;d["k"]=k;auto p=d["p"].to<JsonArray>(),o=d["o"].to<JsonArray>();for(unsigned i=0;i<24;++i){p.add(400000);o.add(1);}std::string text;serializeJson(d,text);const auto r=futurePrices.receive(reinterpret_cast<const uint8_t*>(text.data()),text.size(),utc,100+k,true);assert(!std::strcmp(r,"pending")||!std::strcmp(r,"accepted")||!std::strcmp(r,"duplicate"));}
}
static void step(PreparedSettings& cfg,uint32_t ms){
 const uint32_t utc=start+ms/1000;freshPrices(utc);
 bindActualRelativePrice(utc,true,.4f);
 currentDecision=currentControl.update(cfg,inputs,true,true,900,400,ms);
 applyActualRelativeMode();applyActualRelativeFeedHold(utc);
}
int main(){
 // [MOCKED!] Real receiver/control/policy and extracted main bindings; no hardware or broker.
 PreparedSettings cfg;cfg.valid=true;cfg.relativeTimePolicy=true;
 inputs.bmsValid=inputs.chargerValid=inputs.metersValid=inputs.priceValid=true;
 inputs.batteryChargeAvailable=inputs.batteryDischargeIntentAvailable=true;
 inputs.soc=50;inputs.householdW=900;inputs.price=.4f;
 inputs.futureValid=true;inputs.futureBatteryCeilingW=400;inputs.futurePriceTier="very_expensive";
 modeInputs.metersValid=modeInputs.chargerValid=modeInputs.bmsValid=modeInputs.dischargeAllowed=true;
 modeInputs.chargeKnown=modeInputs.chargeAllowed=true;modeInputs.soc=50;modeInputs.householdW=900;modeInputs.maximumW=900;
 for(unsigned i=0;i<3;++i){inputs.bmsSequence=inputs.chargerSequence=i+1;step(cfg,100+i*3000);}
 assert(currentDecision.ceilingW==400&&!currentDecision.direct);
 const uint32_t lastBms=6100;
 inputs.bmsValid=inputs.batteryChargeAvailable=inputs.batteryDischargeIntentAvailable=false;
 inputs.futureValid=false;inputs.chargerW=300;modeInputs.bmsValid=false;modeInputs.chargerW=300;
 FuturePlan unavailable;assert(!unavailable.valid&&!unavailable.relativePricePositive);
 for(uint32_t now:{10000u,120000u,lastBms+179999}){
  inputs.bmsWithinGrace=inputs.sourcesWithinGrace=true;++inputs.chargerSequence;step(cfg,now);
  assert(inputs.relativePricePositive&&currentDecision.sourceGrace&&currentDecision.ceilingW==400&&!modeDecision.handled);
 }
 inputs.bmsWithinGrace=inputs.sourcesWithinGrace=false;step(cfg,lastBms+180000);
 assert(currentDecision.ceilingW==0&&!currentDecision.sourceGrace); // fresh prices never renew the BMS deadline
 for(unsigned i=0;i<3;++i){++inputs.chargerSequence;step(cfg,lastBms+180010+i*3000);}
 assert(modeDecision.automaticPvFallback&&!std::strcmp(modeDecision.effective,"pv_follow")&&modeDecision.watts==270);
 bindActualRelativePrice(start+193,true,-.01f);assert(!inputs.relativePricePositive);
 inputs.price=-.01f;currentDecision=currentControl.update(cfg,inputs,true,true,900,270,200000);assert(currentDecision.ceilingW==0);
 inputs.price=.4f;futurePrices.clear();bindActualRelativePrice(start+200,true,.4f);assert(!inputs.relativePricePositive);
 currentDecision=currentControl.update(cfg,inputs,true,true,900,270,200010);applyActualRelativeMode();applyActualRelativeFeedHold(start+200);
 assert(currentDecision.direct&&currentDecision.ceilingW==0&&modeDecision.watts==0);
 std::cout<<"[MOCKED!] Actual main positive-price binding preserves original BMS grace, qualified PV fallback, live negative and missing-feed zero passed\n";
}
