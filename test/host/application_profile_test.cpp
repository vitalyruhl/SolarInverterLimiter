#include "control/ApplicationProfile.h"
#include "control/FuturePrices.h"
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
using namespace control;
static constexpr uint32_t now=1792454400;
static void load(FuturePrices& p, uint32_t revision = now, unsigned first = 0, unsigned end = 6){
 for(unsigned k=first;k<end;++k){
  JsonDocument d;d["v"]=kPriceInputVersion;d["r"]=revision;d["t"]=now;d["d"]=900;d["n"]=144;d["k"]=k;
  auto prices=d["p"].to<JsonArray>(),origin=d["o"].to<JsonArray>();
  for(unsigned i=0;i<24;++i){prices.add(400000);origin.add(1);}
  std::string s;serializeJson(d,s);const auto result=p.receive(reinterpret_cast<const uint8_t*>(s.data()),s.size(),revision,100+k,kPriceInputVersion==2);
  assert(!std::strcmp(result,k==5?"accepted":"pending"));
 }
}
int main(){
 // [MOCKED!] Real application defaults, receiver and allocator; no hardware/network.
 auto c=applicationDefaultSettings();assert(kRelativeTimePolicy&&c.relativeTimePolicy&&!c.valid);
 assert(c.cheapTimePct==10&&c.veryExpensiveTimePct==10&&c.additionalExpensiveTimePct==20);
 assert(c.predictionPremium12hPct==3&&c.predictionPremium24hPct==8&&c.predictionPremiumPct==10&&c.storageMinimumNetAdvantageEurKwh==.05f);
 assert(c.reserveOffSocPercent==5&&c.reserveOnSocPercent==10&&c.chargePriorityThreshold==80&&c.chargeInverterSharePct==10);
 assert(ApplicationDefaults::maxOutputW==900&&ApplicationDefaults::minOutputW==0&&ApplicationDefaults::outputCorrectionW==-100);
 assert(ApplicationDefaults::pidKp==.45f&&ApplicationDefaults::pidKi==.05f&&ApplicationDefaults::pidKd==.02f&&ApplicationDefaults::rs485PeriodS==1);
 assert(ApplicationDefaults::setZeroNegative&&!ApplicationDefaults::forceMinNegative&&ApplicationDefaults::sourceTimeoutS==180);
 assert(kPriceInputVersion==2&&priceInputRequiredForMode(true));
 assert(c.replanSocDeltaPct == 1.0f);
 assert(c.replanEnergyDeltaWh == 50.0f);
 assert(c.replanMinimumIntervalS == 60u);
 assert(c.replanPvDeviationW == 100.0f);
 assert(c.replanPvDeviationDurationS == 60u);
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
 assert(kPlannerShadowProfile&&priceInputRequiredForMode(false));
 assert(!std::strcmp(kDefaultPriceScheduleTopic,"SolarLimiterTest/Input/PriceSchedule"));
#else
 assert(!kPlannerShadowProfile&&!priceInputRequiredForMode(false));
 assert(!std::strcmp(kDefaultPriceScheduleTopic,"SolarLimiter/Input/PriceSchedule"));
#endif
 c.valid=validRelativeTiers(c);FuturePrices prices;load(prices);DischargeBudget trace;
 const auto before=prices.plan(now,100,c,true,50,0,100,1,true,.4f,false,.01f,900,nullptr,0,&trace);
 assert(before.valid&&prices.inputVersion()==2);for(unsigned i=0;i<trace.count;++i)assert(trace.watts[i]==0);
 assert(prices.priceFresh(now+179)&&!prices.priceFresh(now+180));
 assert(prices.priceFresh(now+299,300)&&!prices.priceFresh(now+300,300));
 assert(prices.plan(now+180,180,c,true,50,0,100,2,true,.4f,false,1,900,nullptr,0,&trace,-1,false,300).valid);
 assert(!prices.plan(now+180,200,c,true,50,0,100,2,true,.4f,false,1,900,nullptr,0,&trace).valid);
 const std::string withdrawal="{\"v\":2,\"r\":"+std::to_string(now+181)+",\"t\":"+std::to_string(now)+",\"d\":900,\"n\":0,\"k\":0,\"p\":[],\"o\":[]}";
 FuturePrices replay;
 const auto *bytes = reinterpret_cast<const uint8_t *>(withdrawal.data());
 const std::string schedule = "{\"v\":2,\"r\":" + std::to_string(now) + ",\"t\":" + std::to_string(now) +
     ",\"d\":900,\"n\":2,\"k\":0,\"p\":[400000,400000],\"o\":[1,1]}";
 FuturePrices ttl;
 const auto *scheduleBytes = reinterpret_cast<const uint8_t *>(schedule.data());
 assert(!std::strcmp(ttl.receive(scheduleBytes, schedule.size(), now+60, 10, true, 60), "test_price_stale"));
 assert(!std::strcmp(ttl.receive(scheduleBytes, schedule.size(), now+60, 11, true, 61), "accepted"));
 load(replay);
 load(replay, now+400, 0, 1);
 assert(!std::strcmp(replay.receive(bytes, withdrawal.size(), now+400, 0, true, 60), "withdrawn"));
 assert(!replay.slots() && !replay.economicValid());
 load(replay, now+400, 1, 6);
 assert(replay.slots()==144 && replay.revision()==now+400);
 assert(!std::strcmp(replay.receive(bytes, withdrawal.size(), now+400, 0, true, 60), "old_revision"));
 assert(!std::strcmp(prices.receive(reinterpret_cast<const uint8_t*>(withdrawal.data()),withdrawal.size(),now+181,300,true),"withdrawn"));
 const auto after=prices.plan(now+181,301,c,true,80,0,100,3,true,.4f,false,1,900,nullptr,0,&trace);
 assert(!after.valid&&!prices.priceFresh(now+181)&&std::abs(after.availableWh-before.availableWh)<.01f);
 std::cout<<"[MOCKED!] Application defaults, independent normal/shadow policy, explicit V2 TTL/withdraw and earned ledger passed\n";
}
