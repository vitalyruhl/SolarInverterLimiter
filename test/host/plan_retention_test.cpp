#include "control/ChargePlan.h"
#include "control/PlanCaptureContext.h"
#include "control/TelemetryAge.h"
#include "sbt/SbtReceiver.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace control;
static constexpr uint32_t t=1793038500;
// [MOCKED!] The fallback exercises the original destructive publication path before retention exists.
template<class P,class S> static auto step(P& p,const S& candidate,uint32_t now,uint32_t utc,uint32_t context,bool grace,bool hard,bool coverage,int)
 -> decltype(p.observe(now,utc,context,grace,hard,coverage),p.select(candidate,now,context),void()) {
 p.observe(now,utc,context,grace,hard,coverage);p.select(candidate,now,context);
}
template<class P,class S> static void step(P& p,const S& candidate,uint32_t now,uint32_t,uint32_t,bool,bool,bool,long) {
 p.snapshot=candidate;p.captured(now);
}
template<class P,class S> static void step(P& p,const S& candidate,uint32_t now,uint32_t utc,uint32_t context=1,bool grace=true,bool hard=false,bool coverage=true) {
 step(p,candidate,now,utc,context,grace,hard,coverage,0);
}
template<class R> static auto fault(const R& r,int)->decltype(r.genuineAlarmRestricted()) {return r.genuineAlarmRestricted();}
template<class R> static bool fault(const R& r,long) {return r.alarmRestricted()&&!r.softwareMosLockOnly();}
template<class C> static auto displayPrice(C& c,bool fresh,float measured,const C* old,int)->decltype(c.observePrice(fresh,measured,old),void()) {c.observePrice(fresh,measured,old);}
template<class C> static void displayPrice(C& c,bool,float measured,const C*,long) {c.currentPrice=measured;}
template<class C> static auto hardGap(const C& c,bool,bool fault,int)->decltype(c.hardInvalid(fault)) {return c.hardInvalid(fault);}
template<class C> static bool hardGap(const C& c,bool fresh,bool fault,long) {return !c.settings.valid||!fresh||fault;}
template<class P> static auto content(const P& p,int)->decltype(p.contentRevision()){return p.contentRevision();}
template<class P> static uint32_t content(const P& p,long){return p.revision();}
template<class C> static auto sameHard(const C& a,const C& b,int)->decltype(a.sameHard(b)){return a.sameHard(b);}
template<class C> static bool sameHard(const C& a,const C& b,long){return a.same(b);}
template<class P> static auto requestCapture(P& p,int)->decltype(p.requestCapture(),void()){p.requestCapture();}
template<class P> static void requestCapture(P&,long){}
// [MOCKED!] Apply the production context transition using actual semantic fields.
template<class P> static void changedContext(P& p,PlanCaptureContext& previous,const PlanCaptureContext& next,uint32_t& hardEpoch){
 if(previous.same(next))return;
 if(!sameHard(previous,next,0))++hardEpoch;
 previous=next;requestCapture(p,0);
}
static DischargeBudget red(){DischargeBudget p;p.reset(t,t,t);p.start=t;p.count=4;p.watts[0]=0;p.watts[1]=900;p.finish(true,"forecast_hold");return p;}
static ChargePlan blue(){ChargePlan p;p.reset(t,t,t);p.start=t;p.count=4;p.watts[0]=0;p.watts[1]=200;p.finish(true,"intent_projection");return p;}
template<class S> static void continuity(const S& original){
 PlanPublication<S> p;p.connection(true);step(p,original,100,t);p.published(true,100);
 char before[S::kPayloadBytes],after[S::kPayloadBytes];assert(p.snapshot.serialize(before,sizeof(before))>0);
 S missing;missing.reset(t+5,t,t);missing.finish(false,"energy_missing");
 step(p,missing,5100,t+5);assert(p.snapshot.valid&&p.snapshot.generatedAt==t);
 assert(!p.publishDue(5100));
 for(unsigned sec=10;sec<=124;sec+=6)step(p,missing,100+sec*1000,t+sec);
 assert(p.snapshot.valid&&p.snapshot.serialize(after,sizeof(after))>0&&!std::strcmp(before,after));
 p.connection(false);p.connection(true);step(p,missing,125100,t+125);assert(p.snapshot.valid&&p.publishDue(125100));
 p.published(false,125100);step(p,missing,126100,t+126);assert(!p.publishDue(129999)&&p.publishDue(130100));
 assert(!p.publishDue(180100)); // A queued valid retry cannot escape the original deadline between control ticks.
 step(p,missing,180100,t+180);assert(!p.snapshot.valid&&p.publishDue(180100));
 PlanPublication<S> changed;changed.connection(true);step(changed,original,100,t);changed.published(true,100);
 step(changed,missing,5100,t+5,2);assert(!changed.snapshot.valid);
 step(changed,missing,6100,t+6,1);assert(!changed.snapshot.valid);
 PlanPublication<S> stopped;stopped.connection(true);step(stopped,original,100,t);stopped.published(true,100);
 step(stopped,missing,5100,t+5,1,true,true);assert(!stopped.snapshot.valid);
 PlanPublication<S> grace;grace.connection(true);step(grace,original,100,t);grace.published(true,100);
 step(grace,missing,5100,t+5,1,false);assert(!grace.snapshot.valid);
 PlanPublication<S> clock;clock.connection(true);step(clock,original,100,t);clock.published(true,100);
 step(clock,missing,5100,t-6);assert(!clock.snapshot.valid);
 PlanPublication<S> wrapped;wrapped.connection(true);step(wrapped,original,UINT32_MAX-20000,t);wrapped.published(true,UINT32_MAX-20000);
 step(wrapped,missing,7999,t+28);assert(wrapped.snapshot.valid);
 step(wrapped,missing,159999,t+180);assert(!wrapped.snapshot.valid);
 PlanPublication<S> coverage;coverage.connection(true);step(coverage,original,100,t);coverage.published(true,100);
 step(coverage,missing,5100,t+5,1,true,false,false);assert(!coverage.snapshot.valid);
 PlanPublication<S> first;first.connection(true);step(first,missing,100,t);assert(!first.snapshot.valid);
 PlanPublication<S> recovery;recovery.connection(true);step(recovery,original,100,t);recovery.published(true,100);
 recovery.qualification(false);step(recovery,missing,124100,t+124);assert(recovery.snapshot.valid);
 recovery.qualification(true);assert(recovery.captureDue(124101));
 S fresh=original;fresh.generatedAt=t+125;
 step(recovery,fresh,125100,t+125);assert(recovery.snapshot.generatedAt==t+125&&recovery.publishDue(125100));
 PlanPublication<S> topic;topic.connection(true);step(topic,original,100,t);topic.published(true,100);topic.topicChanged();
 step(topic,missing,5100,t+5);assert(!topic.snapshot.valid);
}
static void receive(sbt::Receiver& r,const char* topic,const char* value,uint32_t now){assert(r.receive(topic,value,std::strlen(value),now));}
static void sourceGraceAndFault(){
 sbt::Receiver r;receive(r,"status","online",100);receive(r,"bms/availability","available",100);receive(r,"victron/availability","available",100);
 receive(r,"bms/health","{\"available\":true,\"reason\":\"ok\",\"sample_sequence\":1,\"sample_age_ms\":0}",100);
 receive(r,"victron/health","{\"available\":true,\"reason\":\"ok\",\"sample_sequence\":1,\"sample_age_ms\":0}",100);
 receive(r,"bms/state_of_charge_pct","94",100);receive(r,"bms/power_w","0",100);
 receive(r,"victron/battery_voltage_v","56",100);receive(r,"victron/charge_current_a","0",100);
 receive(r,"bms/health","{\"available\":true,\"reason\":\"ok\",\"sample_sequence\":2,\"sample_age_ms\":0}",1100);
 receive(r,"victron/health","{\"available\":true,\"reason\":\"ok\",\"sample_sequence\":2,\"sample_age_ms\":0}",1100);
 assert(r.fresh(0,1100)&&r.withinGrace(0,125100)&&r.withinGrace(1,125100));
 receive(r,"status","offline",2100);assert(!r.fresh(0,2100)&&r.withinGrace(0,125100));
 receive(r,"bms/control","{\"enabled\":false,\"ready\":false,\"pending\":false,\"session\":\"0123456789abcdef0123456789abcdef\",\"reason\":\"boot\"}",3100);
 assert(!r.fresh(0,3100)&&r.withinGrace(0,125100));
 receive(r,"bms/availability","unavailable",3200);
 receive(r,"bms/health","{\"available\":false,\"reason\":\"timeout\",\"sample_sequence\":0,\"sample_age_ms\":null}",3300);
 assert(!r.fresh(0,3300)&&r.withinGrace(0,125100));
 receive(r,"bms/alarm_bits","4097",4100);assert(fault(r,0)); // Charge-only cell overvoltage plus software MOS lock.
 CurrentControl control;PreparedSettings cfg;cfg.valid=true;cfg.idleOutputW=0;CurrentInputs unavailable;
 unavailable.sourcesWithinGrace=r.withinGrace(0,125100)&&r.withinGrace(1,125100);
 unavailable.bmsValid=r.valid("bms/state_of_charge_pct",125100)&&r.valid("bms/power_w",125100);
 unavailable.chargerValid=r.valid("victron/battery_voltage_v",125100)&&r.valid("victron/charge_current_a",125100);
 unavailable.batteryDischargeIntentAvailable=r.batteryDischargeIntentAvailable(125100);
 unavailable.batteryChargeAvailable=r.batteryChargeAvailable(125100);
 assert(unavailable.sourcesWithinGrace&&!unavailable.bmsValid&&!unavailable.chargerValid&&!unavailable.batteryDischargeIntentAvailable);
 unavailable.futureValid=true;unavailable.futureBatteryCeilingW=900;
 const auto stopped=control.update(cfg,unavailable,true,true,900,0,125100);assert(stopped.ceilingW==0);
 r.reset();assert(!r.withinGrace(0,125100)); // Root change is not a reconnect.
 sbt::Receiver software;receive(software,"bms/alarm_bits","4096",100);assert(!fault(software,0));
 receive(software,"bms/alarm_bits","4097",200);assert(fault(software,0));
 receive(software,"bms/alarm_bits","0",300);assert(fault(software,0)); // An unqualified clear cannot erase the latched fault.
}
static void exactContext(){
 PlanCaptureContext a;a.settings.valid=true;a.maximumW=900;a.priceFactor=.95f;a.prices=t;a.pv=t;
 auto b=a;assert(a.same(b));b.maximumW=500;assert(!a.same(b));b=a;b.priceFactor=1;assert(!a.same(b));
 b=a;b.settings.reserveOffSocPercent=20;assert(!a.same(b));b=a;b.prices++;assert(!a.same(b));
 b=a;b.pv++;assert(!a.same(b));b=a;b.limiter=true;assert(!a.same(b));b=a;b.currentPrice=.4f;assert(!a.same(b));
}
static void knownChargerFaultSurvivesPartialRecovery(){
 PlanCaptureFaults faults;DischargeBudgetPublication p;p.connection(true);auto frame=red();
 step(p,frame,100,t);p.published(true,100);
 faults.observe(true,true);frame.generatedAt=t+5;step(p,frame,5100,t+5,1,true,faults.blocked(false));
 assert(!p.snapshot.valid);p.qualification(false);
 faults.observe(false,false);assert(faults.blocked(false));
 frame.generatedAt=t+10;step(p,frame,10100,t+10,1,true,faults.blocked(false));
 assert(!p.snapshot.valid); // New valid energy alone cannot clear an unknown charger error state.
 frame.generatedAt=t+20;step(p,frame,20100,t+20,2,false,faults.blocked(false));
 assert(!p.snapshot.valid&&faults.blocked(false)); // Context/root change is not a known error clear.
 faults.observe(true,false);assert(!faults.blocked(false)&&faults.blocked(true));
 p.qualification(true);assert(p.captureDue(20101));
 frame.generatedAt=t+30;step(p,frame,30100,t+30,2,true,faults.blocked(false));
 assert(p.snapshot.valid&&p.snapshot.generatedAt==t+30&&p.publishDue(30100));
}
static void ownReconnectPreservesDisplayWithoutControlPrice(){
 ConfigManagerClass cfg;Config<String> topic{String("price")},path{String("none")};TelemetryAge age;
 uint32_t hash=2166136261u;for(const char* p="electricity_price";*p;++p){hash^=uint8_t(*p);hash*=16777619u;}
 char key[16];std::snprintf(key,sizeof(key),"MQTTRT_%08X",unsigned(hash));cfg.settings[key]=&topic;
 std::snprintf(key,sizeof(key),"MQTTRK_%08X",unsigned(hash));cfg.settings[key]=&path;age.attach(cfg);
 const char* price="0.4";age.receive("price",reinterpret_cast<const uint8_t*>(price),3,100);
 float measured=0;bool fresh=age.read(TelemetryAge::Price,100,1200000,measured);assert(fresh&&measured==.4f);
 PlanCaptureContext original;original.settings.valid=true;displayPrice(original,fresh,measured,static_cast<const PlanCaptureContext*>(nullptr),0);
 DischargeBudgetPublication pub;pub.connection(true);auto frame=red();step(pub,frame,100,t);pub.published(true,100);
 age.reset();measured=0;fresh=age.read(TelemetryAge::Price,5100,1200000,measured);assert(!fresh&&measured==0);
 auto missingContext=original;displayPrice(missingContext,fresh,measured,&original,0);
 DischargeBudget missing;missing.reset(t+5,t,t);missing.finish(false,"current_price_missing");
 pub.connection(false);pub.connection(true);
 step(pub,missing,5100,t+5,original.same(missingContext)?1:2,true,hardGap(missingContext,fresh,false,0));
 assert(pub.snapshot.valid&&pub.snapshot.generatedAt==t&&pub.snapshot.watts[1]==900);
 CurrentControl control;PreparedSettings settings;settings.valid=true;settings.idleOutputW=0;CurrentInputs inputs;
 inputs.sourcesWithinGrace=true;inputs.priceValid=fresh;inputs.price=measured;inputs.futureValid=true;inputs.futureBatteryCeilingW=900;
 assert(control.update(settings,inputs,true,true,900,0,5100).ceilingW==0);
 price="0.42";age.receive("price",reinterpret_cast<const uint8_t*>(price),4,6100);fresh=age.read(TelemetryAge::Price,6100,1200000,measured);
 auto changed=original;displayPrice(changed,fresh,measured,&original,0);assert(fresh&&!original.same(changed));
 uint32_t hardEpoch=1;changedContext(pub,original,changed,hardEpoch);
 step(pub,missing,6100,t+6,hardEpoch,true,hardGap(changed,fresh,false,0));
 if(!pub.snapshot.valid){std::cerr<<"FAIL: fresh current price during source recovery must retain the original display frame\n";std::abort();}
 inputs.priceValid=fresh;inputs.price=measured;
 assert(control.update(settings,inputs,true,true,900,0,6100).ceilingW==0);
}
static std::string pollPacket(bool solar,uint32_t revision,int wattsOrPrice){
 JsonDocument d;d["v"]=solar?2:1;d["r"]=revision;d["t"]=t;d["n"]=4;d["k"]=0;
 auto p=d["p"].to<JsonArray>();for(unsigned i=0;i<4;++i)p.add(wattsOrPrice);
 if(solar){d["s"]=t-900;d["g"]=revision;d["d"]=900;d["e"]=t+3600;d["f"]=1000;d["c"]=0;d["a"]=0;d["q"]="uncalibrated";d["o"]="forecast_solar";auto x=d["x"].to<JsonArray>();for(unsigned i=0;i<4;++i)x.add(2);}
 std::string value;serializeJson(d,value);return value;
}
template<class S> static void fiveMinutePollDuringSourceGap(const S& originalFrame){
 for(bool changePv:{false,true}){
 FuturePrices prices;PvForecast pv;auto price=pollPacket(false,t,400000),solar=pollPacket(true,t,0);
 assert(!std::strcmp(prices.receive(reinterpret_cast<const uint8_t*>(price.data()),unsigned(price.size()),t,100),"accepted"));
 assert(!std::strcmp(pv.receive(reinterpret_cast<const uint8_t*>(solar.data()),unsigned(solar.size()),t,100),"accepted"));
 PlanCaptureContext original;original.settings.valid=true;original.prices=content(prices,0);original.pv=content(pv,0);
 PlanPublication<S> pub;pub.connection(true);auto frame=originalFrame;frame.generatedAt=t+245;step(pub,frame,245100,t+245);pub.published(true,245100);
 char before[S::kPayloadBytes],after[S::kPayloadBytes];assert(pub.snapshot.serialize(before,sizeof(before))>0);
 price=pollPacket(false,t+300,400000);solar=pollPacket(true,t+300,0);
 assert(!std::strcmp(prices.receive(reinterpret_cast<const uint8_t*>(price.data()),unsigned(price.size()),t+300,300100),"accepted"));
 assert(!std::strcmp(pv.receive(reinterpret_cast<const uint8_t*>(solar.data()),unsigned(solar.size()),t+300,300100),"accepted"));
 auto polled=original;polled.prices=content(prices,0);polled.pv=content(pv,0);
 S missing;missing.reset(t+302,prices.revision(),pv.revision());missing.finish(false,"energy_missing");
 step(pub,missing,302100,t+302,original.same(polled)?1:2);
 assert(pub.snapshot.valid&&pub.snapshot.generatedAt==t+245&&pub.snapshot.priceRevision==t&&pub.snapshot.pvRevision==t);
 assert(pub.snapshot.serialize(after,sizeof(after))>0&&!std::strcmp(before,after));
 assert(prices.revision()==t+300&&pv.revision()==t+300);
 if(changePv){solar=pollPacket(true,t+303,1);assert(!std::strcmp(pv.receive(reinterpret_cast<const uint8_t*>(solar.data()),unsigned(solar.size()),t+303,303100),"accepted"));}
 else {price=pollPacket(false,t+303,400001);assert(!std::strcmp(prices.receive(reinterpret_cast<const uint8_t*>(price.data()),unsigned(price.size()),t+303,303100),"accepted"));}
 polled.prices=content(prices,0);polled.pv=content(pv,0);assert(!original.same(polled));
 uint32_t hardEpoch=1;changedContext(pub,original,polled,hardEpoch);
 pub.observe(303100,t+303,hardEpoch,true,false,true);
 assert(pub.captureDue(303100));
 pub.select(missing,303100,hardEpoch);
 if(!pub.snapshot.valid){std::cerr<<"FAIL: accepted changed forecast content during a source gap must retain the original display frame\n";std::abort();}
 assert(pub.snapshot.serialize(after,sizeof(after))>0&&!std::strcmp(before,after));
 // Another soft update must not renew g, r, pv, t, arrays, or the original deadline.
 polled.currentPrice=.43f;changedContext(pub,original,polled,hardEpoch);
 step(pub,missing,424100,t+424,hardEpoch);assert(pub.snapshot.valid&&pub.snapshot.generatedAt==t+245);
 assert(pub.snapshot.serialize(after,sizeof(after))>0&&!std::strcmp(before,after));
 step(pub,missing,425100,t+425,hardEpoch);assert(!pub.snapshot.valid);
 // Recovery replaces the held generation promptly, rather than waiting for a minute tick.
 PlanPublication<S> recovered;recovered.connection(true);step(recovered,frame,245100,t+245);recovered.published(true,245100);
 recovered.qualification(false);hardEpoch=1;auto prior=original;polled.currentPrice=.44f;
 changedContext(recovered,prior,polled,hardEpoch);step(recovered,missing,303100,t+303,hardEpoch);
 recovered.qualification(true);assert(recovered.captureDue(303101));
 S fresh=originalFrame;fresh.generatedAt=t+304;fresh.priceRevision=prices.revision();fresh.pvRevision=pv.revision();
 step(recovered,fresh,304100,t+304,hardEpoch);
 assert(recovered.publishDue(304100)&&recovered.snapshot.generatedAt==t+304&&recovered.snapshot.priceRevision==prices.revision());
 }
}
template<class S> static void hardContextStillInvalidates(const S& originalFrame){
 for(unsigned change=0;change<8;++change){
  PlanCaptureContext original;original.settings.valid=true;original.maximumW=900;original.pvValid=true;
  auto next=original;
  switch(change){
   case 0:next.maximumW=500;break;
   case 1:next.settings.reserveOffSocPercent=20;break;
   case 2:next.settings.inverterEnabled=!next.settings.inverterEnabled;break;
   case 3:next.transport=!next.transport;break;
   case 4:next.negativeZero=!next.negativeZero;break;
   case 5:next.priceFactor=.95f;break;
   case 6:next.pvValid=false;break;
   default:next.sourceTimeoutS=60;break;
  }
  PlanPublication<S> pub;pub.connection(true);step(pub,originalFrame,100,t);pub.published(true,100);
  S missing;missing.reset(t+5,t,t);missing.finish(false,"energy_missing");
  uint32_t hardEpoch=1;changedContext(pub,original,next,hardEpoch);step(pub,missing,5100,t+5,hardEpoch);
  assert(!pub.snapshot.valid&&pub.snapshot.count==0&&pub.publishDue(5100));
 }
}
static void configuredSourceChangeBeforeControl(){
 // [MOCKED!] Check the actual production configuration branches, then the publication transition.
 std::string path=__FILE__;std::replace(path.begin(),path.end(),'\\','/');
 path.resize(path.rfind("/test/host/"));std::ifstream file(path+"/src/main.cpp");assert(file);
 const std::string source{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
 for(bool solar:{false,true}){
  const auto begin=source.find(solar?"if (pvScheduleTopic != pvTopic)":"if (priceScheduleTopic != inputTopic)");
  assert(begin!=std::string::npos);
  const auto clear=source.find(solar?"pvForecast.clear();":"futurePrices.clear();",begin);assert(clear!=std::string::npos);
  for(const char* action:{"dischargeBudget.topicChanged();","chargePlan.topicChanged();"}){
   const auto invalidate=source.find(action,begin);
   if(invalidate==std::string::npos||invalidate>=clear){std::cerr<<"FAIL: configured forecast source change must invalidate both publications before accepting new data\n";std::abort();}
  }
  FuturePrices prices;PvForecast pv;auto price=pollPacket(false,t,400000),sun=pollPacket(true,t,0);
  assert(!std::strcmp(prices.receive(reinterpret_cast<const uint8_t*>(price.data()),unsigned(price.size()),t,100),"accepted"));
  assert(!std::strcmp(pv.receive(reinterpret_cast<const uint8_t*>(sun.data()),unsigned(sun.size()),t,100),"accepted"));
  DischargeBudgetPublication r;ChargePlanPublication b;r.connection(true);b.connection(true);
  step(r,red(),100,t);step(b,blue(),100,t);r.published(true,100);b.published(true,100);
  r.topicChanged();b.topicChanged(); // The configured source identity changes before the next control tick.
  if(solar)pv.clear();else prices.clear();
  const auto packet=pollPacket(solar,t+5,solar?100:420000);
  const auto accepted=solar?pv.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),t+5,5100):
   prices.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),t+5,5100);
  assert(!std::strcmp(accepted,"accepted"));float value=0;
  assert(prices.priceAt(t+5,1,value)&&pv.valid(t+5)); // Coverage is already restored before observe.
  auto missingRed=red();missingRed.reset(t+5,prices.revision(),pv.revision());missingRed.finish(false,"energy_missing");
  auto missingBlue=blue();missingBlue.reset(t+5,prices.revision(),pv.revision());missingBlue.finish(false,"source_missing");
  step(r,missingRed,5100,t+5);step(b,missingBlue,5100,t+5);
  assert(!r.snapshot.valid&&!b.snapshot.valid&&r.snapshot.count==0&&b.snapshot.count==0);
 }
}
int main(){configuredSourceChangeBeforeControl();fiveMinutePollDuringSourceGap(red());fiveMinutePollDuringSourceGap(blue());ownReconnectPreservesDisplayWithoutControlPrice();hardContextStillInvalidates(red());hardContextStillInvalidates(blue());continuity(red());continuity(blue());sourceGraceAndFault();exactContext();knownChargerFaultSurvivesPartialRecovery();std::cout<<"[MOCKED!] Original-age plan retention, changing price/PV during source gaps, hard invalidation and independent control guards passed\n";
 std::cout<<"Retention publication bytes: red="<<sizeof(DischargeBudgetPublication)<<", blue="<<sizeof(ChargePlanPublication)<<", context="<<sizeof(PlanCaptureContext)<<"\n";
}
