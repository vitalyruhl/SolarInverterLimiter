#include "control/FuturePrices.h"
#include "control/CurrentControl.h"
#include "control/ChargePlan.h"
#include "control/PlanCaptureContext.h"
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <iostream>
using namespace control;
constexpr uint32_t at=1792454400;
template<class C>static auto relative(C& c,int)->decltype(c.relativeTimePolicy=true,void()){c.relativeTimePolicy=true;c.cheapTimePct=5;c.veryExpensiveTimePct=10;c.additionalExpensiveTimePct=30;c.chargePriorityThreshold=95;c.chargeInverterSharePct=0;}
template<class C>static void relative(C&,long){}
template<class C>static auto margin(C& c,float v,int)->decltype(c.storageMinimumNetAdvantageEurKwh=v,void()){c.storageMinimumNetAdvantageEurKwh=v;}
template<class C>static void margin(C&,float,long){}
static PreparedSettings config(){PreparedSettings c;c.valid=true;relative(c,0);return c;}
static void load(FuturePrices& f,const std::vector<int>& prices,uint32_t rev=at,const std::vector<int>& origins={}){
 for(unsigned k=0;k<(prices.size()+23)/24;++k){JsonDocument d;d["v"]=2;d["r"]=rev;d["t"]=at;d["d"]=900;d["n"]=prices.size();d["k"]=k;auto p=d["p"].to<JsonArray>(),o=d["o"].to<JsonArray>();for(unsigned i=k*24;i<prices.size()&&i<(k+1)*24;++i){p.add(prices[i]);o.add(origins.empty()?1:origins[i]);}std::string text;serializeJson(d,text);auto result=f.receive(reinterpret_cast<const uint8_t*>(text.data()),text.size(),rev,100+k,true);assert(!std::strcmp(result,k+1==(prices.size()+23)/24?"accepted":"pending"));}
}
static FuturePlan plan(FuturePrices& f,const PreparedSettings& c,DischargeBudget& red,float wh=400,const PvForecast* pv=nullptr,float cap=0,float measuredSolar=0,uint32_t now=at,float factor=.95f){
 const float soc=c.reserveOffSocPercent+wh/(51.38f*100*.9f)*100;
 return f.plan(now,100,c,true,soc,0,100,1,true,.4f,false,factor,900,pv,cap,&red,measuredSolar,false);
}
static void solar(PvForecast& pv,unsigned slot,int watts,unsigned span=1,unsigned extraSlot=0,int extraWatts=0,unsigned extraSpan=0){
 for(unsigned k=0;k<6;++k){JsonDocument d;d["v"]=2;d["s"]=at-900;d["r"]=at;d["g"]=at;d["t"]=at;d["d"]=900;d["n"]=144;d["k"]=k;d["e"]=at+129600;d["f"]=1000;d["c"]=0;d["a"]=0;d["q"]="uncalibrated";d["o"]="forecast_solar";
 auto p=d["p"].to<JsonArray>(),x=d["x"].to<JsonArray>();for(unsigned i=k*24;i<(k+1)*24;++i){p.add(i>=slot&&i<slot+span?watts:(i>=extraSlot&&i<extraSlot+extraSpan?extraWatts:0));x.add(2);}std::string text;serializeJson(d,text);const auto status=pv.receive(reinterpret_cast<const uint8_t*>(text.data()),text.size(),at,100+k);assert(!std::strcmp(status,k==5?"accepted":"pending"));}
}
static void fairHighAndPartial(){
 auto c=config();c.veryExpensiveTimePct=1;std::vector<int> p(144,300000);for(unsigned i=0;i<4;++i)p[i]=800000;
 FuturePrices f;load(f,p);DischargeBudget red;auto result=plan(f,c,red);assert(result.valid);
 for(unsigned i=0;i<4;++i)assert(red.watts[i]>=399&&red.watts[i]<=400);
 for(unsigned i=4;i<144;++i)assert(red.watts[i]==0);
 FuturePrices partial;load(partial,p,at+600);result=plan(partial,c,red,400,nullptr,0,0,at+600);
 float energy=red.watts[0]*5.0f/60;for(unsigned i=1;i<4;++i)energy+=red.watts[i]*.25f;
 assert(result.valid&&energy<=400.01f);for(unsigned i=1;i<4;++i)assert(std::abs(red.watts[i]-red.watts[0])<=1);
}
static void protectMeasuredBeforeWeather(){
 auto c=config();c.veryExpensiveTimePct=1;c.additionalExpensiveTimePct=3;
 std::vector<int> p(144,300000);for(unsigned i:{0u,3u,4u,5u})p[i]=400000;p[6]=p[7]=800000;p[2]=100000;
 FuturePrices without;load(without,p);DischargeBudget red;auto base=plan(without,c,red);
 assert(base.valid&&red.watts[0]==0&&red.watts[6]>=799&&red.watts[6]<=800&&red.watts[7]>=799);
 PvForecast pv;solar(pv,2,2000);FuturePrices predicted;load(predicted,p);auto result=plan(predicted,c,red,400,&pv,2000);
 assert(result.valid&&red.watts[0]==0);assert(result.usablePvWh>404&&result.usablePvWh<406);
 float medium=0;for(unsigned i:{3u,4u,5u})medium+=red.watts[i]*.25f;
 assert(medium>354&&medium<=355.01f&&red.watts[6]>=899&&red.watts[7]>=899);
}
static void tiesAndConfiguration(){
 auto c=config();RelativeTiers tiers;
 for(unsigned i=0;i<144;++i)tiers.set(i,i<36?.8:.3);assert(tiers.classify(c));
 assert(tiers.count(PriceTier::VeryExpensive)==36&&tiers.count(PriceTier::Cheap)==108&&tiers.count(PriceTier::Expensive)==0);
 for(unsigned i=0;i<144;++i)assert(tiers.tier(i)==(i<36?PriceTier::VeryExpensive:PriceTier::Cheap));
 for(unsigned i=0;i<144;++i)tiers.set(i,.4);assert(tiers.classify(c)&&tiers.count(PriceTier::Neutral)==144);
 for(float bad:{-1.f,101.f,NAN,INFINITY}){auto invalid=c;invalid.cheapTimePct=bad;assert(!validRelativeTiers(invalid));}
 auto overlap=c;overlap.cheapTimePct=90;assert(!validRelativeTiers(overlap));
 for(float bad:{-1.f,NAN,INFINITY}){auto invalid=c;margin(invalid,bad,0);assert(!validRelativeTiers(invalid));}
 PlanCaptureContext before,after;before.settings=c;after=before;after.settings.cheapTimePct=6;assert(!before.sameHard(after));
 after=before;after.settings.chargeInverterSharePct=50;assert(!before.sameHard(after));
}
static void physicalSplitsAndCheapExit(){
 auto c=config();c.veryExpensiveTimePct=.1f;std::vector<int> p(144,300000);p[0]=100000;for(unsigned i=1;i<=24;++i)p[i]=800000;
 FuturePrices f;load(f,p);DischargeBudget red;auto result=plan(f,c,red,400,nullptr,2000,1000);
 assert(result.valid&&result.chargeTargetW>899&&result.directPvCeilingW==0&&red.watts[0]==0&&red.watts[1]>0);
 auto share=c;share.chargeInverterSharePct=50;FuturePrices g;load(g,p);const float wh=(96-share.reserveOffSocPercent)*.01f*5138*.9f;
 result=plan(g,share,red,wh,nullptr,2000,1000);assert(std::abs(result.chargeTargetW-450)<.1f&&result.directPvCeilingW>=449&&result.directPvCeilingW<=450&&red.watts[0]==0);
 p[0]=800000;p[1]=100000;FuturePrices direct;load(direct,p);result=plan(direct,c,red,400,nullptr,2000,1000);
 assert(result.chargeTargetW==0&&result.directPvCeilingW==900&&red.watts[0]==0); // all PV is direct, never also stored
 std::vector<int> flat(144,400000);PvForecast pv;solar(pv,2,2000);FuturePrices full;load(full,flat);
 result=plan(full,c,red,(99-c.reserveOffSocPercent)*.01f*5138*.9f,&pv,2000);
 assert(result.usablePvWh<=51.38f*.9f+.1f);for(unsigned i=0;i<144;++i)assert(red.watts[i]==0);
 std::vector<int> origins(144,1);p[0]=-100000;origins[0]=2;c.predictionPremiumPct=100;FuturePrices negative;load(negative,p,at,origins);
 result=plan(negative,c,red,400,nullptr,2000,1000);assert(red.watts[0]==0&&result.chargeTargetW==0&&result.directPvCeilingW==0&&!result.relativePricePositive);
}
static CurrentDecision controlDecision(const PreparedSettings& c,const FuturePlan& plan){
 CurrentControl current;CurrentInputs in;in.bmsValid=in.chargerValid=in.metersValid=in.priceValid=true;
 in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=true;in.soc=50;in.householdW=900;in.chargerW=1000;in.price=.4f;
 in.futureValid=plan.valid;in.relativePricePositive=plan.relativePricePositive;in.futureBatteryCeilingW=plan.batteryCeilingW;
 in.futurePvCeilingW=plan.directPvCeilingW;in.futureChargeTargetW=plan.chargeTargetW;in.futurePriceTier="cheap";in.futureReason=plan.reason;
 CurrentDecision out;for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;out=current.update(c,in,true,true,900,0,100+i*3000);}return out;
}
static void legacyDoesNotControlRelative(){
 auto c=config();c.veryExpensiveTimePct=.1f;std::vector<int> prices(144,300000);prices[0]=100000;for(unsigned i=1;i<=24;++i)prices[i]=800000;
 DischargeBudget baseline;FuturePrices first;load(first,prices);auto base=plan(first,c,baseline,400,nullptr,2000,1000);const auto decision=controlDecision(c,base);
 assert(decision.ceilingW==0&&decision.chargeTargetW>899&&!decision.minimumOverrideAllowed);
 for(float legacy:{.001f,.95f,5.f}){auto varied=c;varied.priceComfortEurPerKwh=legacy*10;varied.priceDeadbandEurPerKwh=legacy;varied.deferralToleranceEurKwh=legacy;
  FuturePrices f;load(f,prices);DischargeBudget red;auto result=plan(f,varied,red,400,nullptr,2000,1000,at,legacy);
  assert(result.valid&&result.chargeTargetW==base.chargeTargetW&&result.directPvCeilingW==base.directPvCeilingW);
  for(unsigned i=0;i<144;++i)assert(red.watts[i]==baseline.watts[i]);
  const auto current=controlDecision(varied,result);assert(current.ceilingW==decision.ceilingW&&current.chargeTargetW==decision.chargeTargetW);
  ChargePlan blue;ChargePlanInputs input;input.sourcesQualified=input.controlEnabled=input.chargeAvailable=true;input.soc=50;input.fullCapacityAh=100;input.maxChargeW=2000;input.currentChargerW=1000;input.priceFactor=legacy;
  PvForecast empty;blue.project(at,varied,result,f,empty,current,red,input);assert(blue.valid&&blue.watts[0]>=899);
 }
 auto ignored=c;ignored.priceComfortEurPerKwh=NAN;ignored.priceDeadbandEurPerKwh=-1;ignored.deferralToleranceEurKwh=NAN;
 FuturePrices independent;load(independent,prices);DischargeBudget same;assert(plan(independent,ignored,same,400,nullptr,2000,1000,at,NAN).valid);
 FuturePrices gap;load(gap,std::vector<int>(24,400000));DischargeBudget red;auto missing=plan(gap,c,red);assert(!missing.valid&&!std::strcmp(missing.reason,"relative_coverage"));
 FuturePrices stale;load(stale,prices);assert(!plan(stale,c,red,400,nullptr,0,0,at+180).valid);
}
static void nonpositiveSlotsCreateNoDemand(){
 auto c=config();c.cheapTimePct=0;c.veryExpensiveTimePct=90;c.additionalExpensiveTimePct=0;
 std::vector<int> p(144,-100000);p[0]=400000;FuturePrices f;load(f,p);DischargeBudget red;
 const auto result=plan(f,c,red);assert(result.valid&&red.watts[0]==900);
 for(unsigned i=1;i<144;++i)assert(red.watts[i]==0);
}
static void premiumOriginsAndCurrentGates(){
 auto c=config();c.veryExpensiveTimePct=.5f;c.additionalExpensiveTimePct=0;
 std::vector<int> prices(144,300000),origins(144,1);prices[0]=480000;prices[1]=500000;origins[0]=2;
 FuturePrices f;load(f,prices,at,origins);DischargeBudget red;
 auto first=plan(f,c,red);assert(first.batteryCeilingW==0);
 c.predictionPremiumPct=10;auto raised=plan(f,c,red);assert(raised.batteryCeilingW>0&&!std::strcmp(f.currentPriceTier(),"very_expensive"));
 assert(std::abs(first.availableWh-raised.availableWh)<.01f);
 const auto identity=f.contentRevision();origins[0]=1;load(f,prices,at+1,origins);
 const auto changed=plan(f,c,red);assert(f.contentRevision()>identity&&changed.batteryCeilingW==0);
 CurrentControl control;CurrentInputs in;in.bmsValid=in.chargerValid=in.metersValid=in.priceValid=true;
 in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=true;in.soc=50;in.householdW=900;in.chargerW=0;in.price=.4f;
 in.relativePricePositive=true;in.futureValid=true;in.futureBatteryCeilingW=400;in.futurePriceTier="very_expensive";
 CurrentDecision out;for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;out=control.update(c,in,true,true,900,0,100+i*3000);}
 assert(out.ceilingW==400&&!out.minimumOverrideAllowed);
 in.price=0;assert(control.update(c,in,true,true,900,0,6200).ceilingW==0);
 in.price=.4f;in.relativePricePositive=false;assert(control.update(c,in,true,true,900,0,6300).ceilingW==0);
 in.relativePricePositive=true;in.futureValid=false;assert(control.update(c,in,true,true,900,0,6400).ceilingW==0);
 in.futureValid=true;in.chargerFault=true;assert(control.update(c,in,true,true,900,0,6500).ceilingW==0);
}
static void neutralSolarFundsHigherOnly(){
 auto c=config();c.veryExpensiveTimePct=1;c.additionalExpensiveTimePct=0;
 std::vector<int> prices(144,300000);for(unsigned i=0;i<8;++i)prices[i]=100000;prices[10]=prices[11]=800000;
 FuturePrices f;load(f,prices);PvForecast pv;solar(pv,9,200);DischargeBudget red;
 auto result=plan(f,c,red,100,&pv,2000);assert(result.valid&&red.watts[9]==0);
 assert(result.usablePvWh>40&&result.usablePvWh<41); // neutral PV below MAX is reserved for the higher future tier
 assert(red.watts[10]>=280&&red.watts[10]<=281&&red.watts[11]>=280);
 // No same-tier charging loop: high PV remains direct even if later high demand is underfunded.
 prices[9]=800000;FuturePrices same;load(same,prices);result=plan(same,c,red,100,&pv,2000);
 assert(result.usablePvWh==0);
}
static void cheapFullLatchSurvivesReplan(){
 auto c=config();c.veryExpensiveTimePct=.1f;std::vector<int> p(144,300000);p[0]=100000;for(unsigned i=1;i<=24;++i)p[i]=800000;
 FuturePrices f;load(f,p);DischargeBudget red;
 auto result=plan(f,c,red,(100-c.reserveOffSocPercent)*.01f*5138*.9f,nullptr,2000,1000);
 assert(result.chargeTargetW==0&&result.directPvCeilingW==900);
 result=plan(f,c,red,(99-c.reserveOffSocPercent)*.01f*5138*.9f,nullptr,2000,1000);
 assert(result.chargeTargetW==0&&result.directPvCeilingW==900); // full latch is real, not reset by a projection rebuild
 result=plan(f,c,red,(94-c.reserveOffSocPercent)*.01f*5138*.9f,nullptr,2000,1000);
 assert(result.chargeTargetW>899&&result.directPvCeilingW==0);
}
static void additionalStartsAfterActualHigh(){
 auto c=config();RelativeTiers tiers;for(unsigned i=0;i<144;++i)tiers.set(i,i<36?.8:.1+double(i-36)*.001);
 assert(tiers.classify(c));assert(tiers.count(PriceTier::VeryExpensive)==36);
 assert(tiers.count(PriceTier::Expensive)>=43&&tiers.count(PriceTier::Expensive)<=44);
}
static void storageEconomicsAndTwoLosses(){
 auto c=config();c.veryExpensiveTimePct=1;c.additionalExpensiveTimePct=30;
 std::vector<int> prices(144,300000);for(unsigned i=0;i<8;++i)prices[i]=100000;prices[10]=prices[11]=800000;
 PvForecast pv;solar(pv,8,100,1);FuturePrices f;load(f,prices);DischargeBudget red;auto result=plan(f,c,red,100,&pv,2000);
 assert(std::abs(f.projectedChargeW(8)-90)<.01f&&std::abs(result.usablePvWh-20.25f)<.01f);
 assert(red.watts[8]==0); // 100 PV W -> 90 stored battery DC W -> 81 usable AC W
 const float original=result.availableWh;margin(c,99,0);result=plan(f,c,red,100,&pv,2000);
 assert(result.usablePvWh==0&&std::abs(original-result.availableWh)<.01f&&red.watts[10]>=199); // held inventory is not reacquired
 margin(c,.05f,0);result=plan(f,c,red,100,&pv,2000);assert(result.usablePvWh>20&&std::abs(original-result.availableWh)<.01f);
 for(int target:{500000,499999}){
  auto p=prices;p[8]=405000;p[10]=p[11]=target;FuturePrices exact;load(exact,p);auto r=plan(exact,c,red,0,&pv,2000);
  assert((r.usablePvWh>0)==(target==500000)); // exact net margin accepted; one micro-EUR below rejected
 }
 auto near=prices;near[8]=300000;near[10]=near[11]=360000;FuturePrices unprofitable;load(unprofitable,near);
 assert(plan(unprofitable,c,red,0,&pv,2000).usablePvWh==0);
 // Both forecast sides use the same assessed basis: .495 - .44/.9 < .05, not .495 - .40/.9.
 auto forecast=prices;forecast[8]=400000;forecast[10]=forecast[11]=450000;std::vector<int> origins(144,1);origins[8]=origins[10]=origins[11]=2;
 c.predictionPremiumPct=10;FuturePrices sameBasis;load(sameBasis,forecast,at,origins);assert(plan(sameBasis,c,red,0,&pv,2000).usablePvWh==0);
 PlanCaptureContext before,after;before.settings=c;after=before;margin(after.settings,.1f,0);assert(!before.sameHard(after));
 // Net measured BMS input is earned once: 100 W for 30 s -> .8333 Wh DC -> .75 Wh AC, not .675.
 FuturePrices measured;load(measured,std::vector<int>(144,400000));
 auto first=measured.plan(at,100,c,true,50,100,100,1,true,.4f,false,1,900);
 auto second=measured.plan(at+30,30100,c,true,51,100,100,2,true,.4f,false,1,900);
 assert(std::abs(second.availableWh-first.availableWh-.75f)<.002f);
}
static void economicsValuesActualUniformTier(){
 auto c=config();std::vector<int> prices(144,300000);for(unsigned i=0;i<8;++i)prices[i]=100000;
 prices[8]=450000;prices[15]=1000000;for(unsigned i=16;i<30;++i)prices[i]=500000;
 FuturePrices f;load(f,prices);PvForecast pv;solar(pv,8,100);DischargeBudget red;
 auto result=plan(f,c,red,0,&pv,2000);assert(f.tierCount(PriceTier::VeryExpensive)==15);
 assert(result.usablePvWh==0); // (1 + 14*.5)/15 - .45/.9 = .03333 < .05
 margin(c,0,0);result=plan(f,c,red,0,&pv,2000);assert(result.usablePvWh>20&&result.availableWh==0);
}
static void minimumHurdleSurvivesDirectPvCaps(){
 auto c=config();std::vector<int> prices(144,300000);for(unsigned i=0;i<8;++i)prices[i]=100000;
 prices[8]=420000;prices[15]=1000000;for(unsigned i=16;i<30;++i)prices[i]=500000;
 PvForecast pv;solar(pv,8,100,1,15,1000,1);FuturePrices f;load(f,prices);DischargeBudget red;
 auto result=plan(f,c,red,0,&pv,2000);assert(result.valid&&result.usablePvWh==0);
 margin(c,.03f,0);result=plan(f,c,red,0,&pv,2000);assert(result.usablePvWh>20); // min .50 - .42/.9 = .03333
 // Profitable nominal tier capacity is not demand if every target is already covered by direct PV.
 c=config();c.additionalExpensiveTimePct=0;prices[8]=400000;PvForecast covered;solar(covered,8,100,1,15,1000,15);
 FuturePrices fullPv;load(fullPv,prices);result=plan(fullPv,c,red,0,&covered,2000);assert(result.valid&&result.usablePvWh==0);
}
int main(){
 auto c=config();FuturePrices f;load(f,std::vector<int>(144,400000));DischargeBudget red;const auto result=plan(f,c,red);assert(result.valid);
 for(unsigned i=0;i<red.count;++i)assert(red.watts[i]==0);
 additionalStartsAfterActualHigh();minimumHurdleSurvivesDirectPvCaps();economicsValuesActualUniformTier();storageEconomicsAndTwoLosses();nonpositiveSlotsCreateNoDemand();neutralSolarFundsHigherOnly();fairHighAndPartial();protectMeasuredBeforeWeather();tiesAndConfiguration();physicalSplitsAndCheapExit();legacyDoesNotControlRelative();premiumOriginsAndCurrentGates();cheapFullLatchSurvivesReplan();
 std::cout<<"[MOCKED!] Relative tiers: measured priority, fair duration, PV prefix/storage, raw stops, whole ties, legacy independence and coverage passed\n";
}
