#include "control/ShadowProfile.h"
#include "control/FuturePrices.h"
#include "control/CurrentControl.h"
#include "control/OperatingModes.h"
#include <string>
#include <cassert>
#include <iostream>
using String=std::string;
// [MOCKED!] Only HTTP persistence/mutation side effects are replaced. Gate and source hold are extracted production code.
static int mutations=0;
static String stored="SolarLimiterTest";
static bool routingValid=true;
static struct {
 bool applySetting(const String&,const String&,const String& value){++mutations;stored=value;return true;}
 bool updateSetting(const String& a,const String& b,const String& value){return applySetting(a,b,value);}
} ConfigManager;
static bool shadowWebWriteAllowed(const String&,const String& key,const String& value) {
 using F=control::ShadowField;
 return control::shadowWriteAllowed(key=="root"?F::Root:F::Enable,value.c_str(),"SolarLimiterTest-3C8A1FA7227C",routingValid);
}
static control::CurrentControl currentControl;
static control::CurrentDecision currentDecision;
static control::FuturePrices futurePrices;
static control::ModeDecision modeDecision;
static bool modePublishDirty=false;
#include "shadow_http_gate.h"
int main() {
 for(bool save:{false,true}) {
  assert(!shadowWriteConfig("MQTT","root","SolarLimiter",save));
  assert(mutations==0&&stored=="SolarLimiterTest");
 }
 assert(shadowWriteConfig("MQTT","root","SolarLimiterTest",false));assert(mutations==1);
 routingValid=false;assert(!shadowWriteConfig("MQTT","enable","true",false));assert(mutations==1);
 assert(shadowWriteConfig("MQTT","enable","false",false));assert(mutations==2);
 constexpr uint32_t t=1792454400;
 currentDecision.ceilingW=900;applyShadowSourceGate(t);
 assert(currentDecision.direct&&currentDecision.ceilingW==0);
 JsonDocument d;d["v"]=2;d["r"]=t;d["t"]=t;d["n"]=2;d["k"]=0;d["d"]=900;
 auto p=d["p"].to<JsonArray>(),o=d["o"].to<JsonArray>();p.add(400000);p.add(500000);o.add(1);o.add(2);
 std::string text;serializeJson(d,text);
 assert(!std::strcmp(futurePrices.receive(reinterpret_cast<const uint8_t*>(text.data()),text.size(),t,100,true),"accepted"));
 currentDecision={};currentDecision.ceilingW=900;applyShadowSourceGate(t+179);assert(currentDecision.ceilingW==900);
 applyShadowSourceGate(t+180);assert(currentDecision.ceilingW==0&&currentDecision.direct);
 assert(!std::strcmp(currentDecision.reason,"test_price_stale"));
 // [MOCKED!] A real manual policy demand must also be stopped by the final feed gate.
 for(auto mode:{control::OperatingMode::HouseholdFollow,control::OperatingMode::PvFollow}) {
  control::OperatingModePolicy policy;control::ModeSettings settings;settings.mode=mode;
  control::ModeInputs input;input.metersValid=input.chargerValid=input.bmsValid=true;
  input.dischargeAllowed=true;input.chargeKnown=input.chargeAllowed=true;
  input.householdW=900;input.chargerW=1000;input.maximumW=900;input.soc=80;
  modeDecision=policy.update(settings,input);assert(modeDecision.handled&&modeDecision.watts>0);
  currentDecision.ceilingW=modeDecision.watts;applyShadowSourceGate(t+180);
  assert(modeDecision.handled&&modeDecision.watts==0&&currentDecision.ceilingW==0);
  assert(!std::strcmp(modeDecision.reason,"test_price_stale")&&modePublishDirty);
 }

 std::cout<<"[MOCKED!] Actual HTTP mutation gate and missing/stale shadow-price output hold passed\n";
}
