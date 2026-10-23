#include "control/FuturePrices.h"
#include "control/PvForecast.h"
#include "control/CurrentControl.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace control;
static constexpr uint32_t t=1792454400;
static void near(float actual,float expected,float tolerance=.002f){
 if(std::fabs(actual-expected)>tolerance){std::cerr<<"expected "<<expected<<", received "<<actual<<"\n";std::abort();}}
// [MOCKED!] Public synthetic UTC schedules; actual receivers and planner run unchanged.
static std::string prices(const std::vector<int>& values,uint32_t revision=t,unsigned part=0){
 JsonDocument d;d["v"]=1;d["r"]=revision;d["t"]=t;d["n"]=values.size();d["k"]=part;
 auto p=d["p"].to<JsonArray>();for(unsigned i=part*24;i<values.size()&&i<(part+1)*24;++i)p.add(values[i]);
 std::string out;serializeJson(d,out);return out;
}
static const char* send(FuturePrices& p,const std::string& packet,uint32_t utc=t,uint32_t ms=100){
 return p.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),utc,ms);
}
static void load(FuturePrices& p,const std::vector<int>& values,uint32_t revision=t,uint32_t utc=t){
 for(unsigned part=0;part*24<values.size();++part){const auto result=send(p,prices(values,revision,part),utc,100+part);
 assert(!std::strcmp(result,(part+1)*24>=values.size()?"accepted":"pending"));}
}
static void solar(PvForecast& p,const std::vector<int>& watts,uint32_t revision=t+1,int missing=-1){
 for(unsigned part=0;part*24<watts.size();++part){
  JsonDocument d;d["v"]=2;d["s"]=t-900;d["r"]=revision;d["g"]=revision;d["t"]=t;
  d["d"]=900;d["n"]=watts.size();d["k"]=part;d["e"]=t+unsigned(watts.size())*900;
  d["f"]=1000;d["c"]=0;d["a"]=0;d["q"]="uncalibrated";d["o"]="forecast_solar";
  auto a=d["p"].to<JsonArray>(),q=d["x"].to<JsonArray>();
  for(unsigned i=part*24;i<watts.size()&&i<(part+1)*24;++i){if(int(i)==missing)a.add(nullptr);else a.add(watts[i]);q.add(int(i)==missing?1:2);}
  std::string packet;serializeJson(d,packet);
  const auto result=p.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),revision,100+part);
  assert(!std::strcmp(result,(part+1)*24>=watts.size()?"accepted":"pending"));
 }
}
static FuturePlan plan(FuturePrices& p,const PreparedSettings& cfg,const PvForecast* pv=nullptr,
                      float cap=0,float soc=5,float ah=100,uint32_t utc=t,uint32_t ms=100,
                      uint32_t seq=1,float power=0,float price=.1f,bool expensive=false,bool valid=true){
 return p.plan(utc,ms,cfg,valid,soc,power,ah,seq,true,price,expensive,1,900,pv,cap);
}
// [MOCKED!] Synthetic tariffs/PV and battery samples; real fixed-size allocator.
static void freeGridDoesNotSpendStoredEnergy(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices p;load(p,{0,0,0});DischargeBudget trace;
 const auto result=p.plan(t,100,cfg,true,20,0,10,1,true,0,false,1,900,nullptr,0,&trace);
 assert(result.valid&&result.availableWh>0&&result.batteryCeilingW==0&&result.reservedWh==0);
 assert(result.chargeTargetW==0&&trace.watts[0]==0&&trace.watts[1]==0&&trace.watts[2]==0);
}
static void ownerTariffsKeepStoredAndNewEnergySeparate(){
 PreparedSettings cfg;cfg.valid=true;cfg.priceComfortEurPerKwh=.25f;cfg.priceDeadbandEurPerKwh=.20f;
 std::vector<int> curve(144,200000);curve[0]=245000;curve[22]=curve[23]=309000;
 FuturePrices p;load(p,curve);DischargeBudget trace;
 const auto result=p.plan(t,100,cfg,true,6,0,131,1,true,.245f,false,1,900,nullptr,0,&trace);
 assert(result.valid&&result.availableWh>240&&result.reservedWh>240&&result.batteryCeilingW==0);
 assert(result.shortfallWh==0&&result.chargeTargetW==0&&result.targetAt==0);
 assert(trace.watts[22]==650&&trace.watts[23]>300&&trace.watts[23]<400);
 for(unsigned i=0;i<trace.count;++i)if(i!=22&&i!=23)assert(trace.watts[i]==0);
}
static void relativePricesAndExistingEnergy(){
 PreparedSettings cfg;cfg.valid=true;unsigned failures=0;
 const auto expect=[&](bool value,const char* label){if(!value){std::cerr<<"FAIL: "<<label<<"\n";++failures;}};
 const auto close=[](float actual,float expected){return std::fabs(actual-expected)<.003f;};
 DischargeBudget trace;
 FuturePrices useful;load(useful,{100000,140000});
 auto held=useful.plan(t,100,cfg,true,20,0,10,1,true,.1f,false,1,900,nullptr,0,&trace);
 expect(held.valid&&held.reservedWh>80&&held.batteryCeilingW==0,"below-comfort relative opportunity reserves existing energy");
 expect(held.shortfallWh==0&&held.chargeTargetW==0,"held energy does not repay acquisition losses/wear; unprofitable new charge stays off");
 expect(trace.watts[0]==0&&trace.watts[1]>300,"trace records useful below-comfort future allocation");
 FuturePrices charge;load(charge,{100000,200000});
 auto fresh=plan(charge,cfg,nullptr,0,20,10);
 expect(fresh.shortfallWh>70&&fresh.chargeTargetW>0&&fresh.targetAt==t+900,"new charging still requires profitable round-trip spread");
 for(const auto& curve:{std::vector<int>{120000,120000,120000},std::vector<int>{140000,130000,120000},std::vector<int>{100000,129999,100000}}){
  FuturePrices p;load(p,curve);const float current=float(curve[0])*.000001f;
  auto now=p.plan(t,100,cfg,true,20,0,10,1,true,current,false,1,900,nullptr,0,&trace);
  expect(now.valid&&now.batteryCeilingW>300&&now.reservedWh==0,"flat/declining/sub-benefit tariffs release current energy without a terminal stash");
  expect(trace.watts[0]==now.batteryCeilingW&&trace.watts[1]==0&&trace.watts[2]==0,"non-useful future slots do not steal current allocation");
 }
 FuturePrices boundary;load(boundary,{100000,130000});
 auto exact=plan(boundary,cfg,nullptr,0,20,10);
 expect(exact.reservedWh>80&&exact.batteryCeilingW==0&&exact.chargeTargetW==0,"the exact minimum-benefit boundary can defer held energy without authorizing new charge");
 FuturePrices capped;load(capped,{100000,140000});
 auto limited=capped.plan(t,100,cfg,true,20,0,100,1,true,.1f,false,1,500,nullptr,0,&trace);
 expect(limited.batteryCeilingW==500&&trace.watts[1]==500,"a configured MAX below planning load remains authoritative");
 FuturePrices tied;load(tied,{100000,140000,140000});
 auto earlier=tied.plan(t,100,cfg,true,20,0,10,1,true,.1f,false,1,900,nullptr,0,&trace);
 expect(earlier.reservedWh>80&&trace.watts[1]>300&&trace.watts[2]==0,"equally useful future prices prefer the earlier slot");
 FuturePrices surplus;load(surplus,{100000,140000,140000});
 auto extra=plan(surplus,cfg,nullptr,0,20,100);
 expect(extra.batteryCeilingW==650&&close(extra.reservedWh,325),"energy left after useful future slots serves current load, capped at the independent 650 W planning load");
 PreparedSettings different=cfg;different.priceComfortEurPerKwh=1;different.priceDeadbandEurPerKwh=.2f;
 auto same=plan(surplus,different,nullptr,0,20,100,t,100,1,0,.1f,true);
 expect(same.batteryCeilingW==extra.batteryCeilingW&&close(same.reservedWh,extra.reservedWh),"comfort/deadband/expensive classification cannot change economic allocation");
 FuturePrices solarPlan;load(solarPlan,{100000,100000,200000,200000});PvForecast pv;solar(pv,{1500,1500,0,0});
 auto first=solarPlan.plan(t,100,cfg,true,20,-360,10,1,true,.1f,false,1,900,&pv,1500,&trace);
 expect(close(first.usablePvWh,325)&&first.batteryCeilingW>300&&trace.watts[2]==0&&trace.watts[3]==0,"PV credit is bounded by future need and never inserted into current stored-energy trace");
 FuturePlan partial;
 for(unsigned step=1;step<=45;++step)
  partial=solarPlan.plan(t+step*10,100+step*10000,cfg,true,20,-360,10,1,true,.1f,false,1,900,&pv,1500,&trace);
 expect(close(first.availableWh-partial.availableWh,40.5f),"held signed power debits the actual pool throughout a partial slot");
 expect(close(partial.usablePvWh,262.5f)&&partial.batteryCeilingW==0&&partial.reservedWh<=partial.availableWh+.003f,"partial-slot PV credit decreases without refilling measured energy");
 float allocated=trace.watts[0]*.125f;for(unsigned i=1;i<trace.count;++i)allocated+=trace.watts[i]*.25f;
 expect(allocated<=partial.availableWh+.003f,"trace remains bounded by measured stored AC energy after PV credit and debits");
 assert(failures==0);
 std::cout<<"Relative tariffs, held-vs-new energy costs, flat/declining/zero curves, exact benefit/MAX and partial-slot PV debits passed\n";
}
static void economicTraces(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices pricesA;load(pricesA,{100000,100000,600000,500000});
 PvForecast pv;solar(pv,{1500,1500,0,0});
 auto baseline=plan(pricesA,cfg);assert(baseline.valid&&baseline.shortfallWh>180);
 auto unknown=plan(pricesA,cfg,&pv);near(unknown.offeredPvWh,750);assert(!unknown.pvQualified&&unknown.usablePvWh==0);
 near(unknown.availableWh,baseline.availableWh);near(unknown.reservedWh,baseline.reservedWh);
 auto sun=plan(pricesA,cfg,&pv,1500);assert(sun.pvQualified);near(sun.offeredPvWh,750);near(sun.usablePvWh,325);
 near(sun.shortfallWh,0);near(sun.reservedWh,0);near(sun.availableWh,baseline.availableWh);assert(sun.batteryCeilingW==554); // PV covers future 650 W slots; stored remainder is usable now.
 FuturePrices low;load(low,{100000,100000,600000,500000});PvForecast weak;solar(weak,{500,500,0,0});
 auto lowSun=plan(low,cfg,&weak,1500);near(lowSun.usablePvWh,0);assert(lowSun.shortfallWh>180);
 FuturePrices full;load(full,{100000,100000,600000,500000});auto fullBattery=plan(full,cfg,&pv,1500,100);
 assert(fullBattery.offeredPvWh>0&&fullBattery.usablePvWh==0);
 FuturePrices curtailed;load(curtailed,{100000,100000,600000,500000});PvForecast large;solar(large,{5000,5000,0,0});
 auto limit=plan(curtailed,cfg,&large,1000);near(limit.offeredPvWh,2500);near(limit.usablePvWh,125);
 FuturePrices headroom;load(headroom,{100000,100000,600000,500000});
 auto almostFull=plan(headroom,cfg,&large,5000,99,10);assert(almostFull.usablePvWh<=4.625f);
 const auto builds=pricesA.planningBuilds();solar(pv,{0,0,0,0},t+2);auto cloud=plan(pricesA,cfg,&pv,1500);
 assert(pricesA.planningBuilds()==builds+1);assert(cloud.usablePvWh==0&&cloud.shortfallWh>180);
 near(cloud.availableWh,baseline.availableWh);
 FuturePrices absent;load(absent,{100000,100000,600000,500000});PvForecast incomplete;solar(incomplete,{1500,1500,0,0},t+1,0);
 auto missing=plan(absent,cfg,&incomplete,1500);assert(!missing.pvQualified&&missing.usablePvWh==0&&missing.shortfallWh>180);
 FuturePrices noPrices;assert(!plan(noPrices,cfg,&pv,1500).valid);
}
static void creditNeverCreatesCurrentEnergy(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices p;load(p,{400000,100000,100000,600000});PvForecast pv;solar(pv,{1500,1500,0,0});
 auto a=plan(p,cfg,nullptr,0,20,10,t,100,1,0,.4f,true);assert(a.batteryCeilingW==0);
 auto b=plan(p,cfg,&pv,1500,20,10,t,100,1,0,.4f,true);assert(b.usablePvWh>0&&b.batteryCeilingW>0);
 assert(float(b.batteryCeilingW)*.25f<=b.availableWh+.001f);near(b.availableWh,a.availableWh);
 FuturePrices empty;load(empty,{400000,100000,100000,600000});auto noStored=plan(empty,cfg,&pv,1500,2,10,t,100,1,0,.4f,true);
 assert(noStored.availableWh==0&&noStored.batteryCeilingW==0);
 CurrentInputs in;in.bmsValid=in.batteryDischargeIntentAvailable=in.chargerValid=in.metersValid=in.priceValid=true;in.soc=20;in.chargerW=1500;
 in.price=-.01f;in.futureValid=b.valid;in.futureBatteryCeilingW=b.batteryCeilingW;
 CurrentControl control;assert(control.update(cfg,in,true,true,900,100,100).ceilingW==0);
 cfg.inverterEnabled=false;in.price=.4f;assert(control.update(cfg,in,true,true,900,100,200).ceilingW==0);
 cfg.inverterEnabled=true;in.soc=0;assert(control.update(cfg,in,true,true,900,100,300).ceilingW==0);
}
static void pvCoverageBoundaries(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices p;load(p,{100000,100000,600000,500000,100000,100000,100000,100000});
 PvForecast shorter;solar(shorter,{1500,1500,0,0});auto before=plan(p,cfg,&shorter,1500);
 assert(before.pvQualified);near(before.usablePvWh,325);near(before.offeredPvWh,750);
 const auto builds=p.planningBuilds();auto partial=plan(p,cfg,&shorter,1500,5,100,t+450,1000);
 assert(p.planningBuilds()==builds);near(partial.usablePvWh,262.5f);near(partial.offeredPvWh,562.5f);
 FuturePrices later;load(later,{100000,100000,600000,500000});PvForecast lateSun;solar(lateSun,{0,0,3000,3000});
 auto late=plan(later,cfg,&lateSun,3000);assert(late.offeredPvWh>0&&late.usablePvWh==0&&late.shortfallWh>180);
 PvForecast laterMissing;solar(laterMissing,{1500,1500,0,0},t+1,3);FuturePrices missing;
 load(missing,{100000,100000,600000,500000});auto knownBefore=plan(missing,cfg,&laterMissing,1500);
 assert(knownBefore.pvQualified);near(knownBefore.usablePvWh,325);
}
static void energyDebitAndGaps(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices p;load(p,{600000,100000});
 const auto initial=plan(p,cfg,nullptr,0,20,10,t,100,1,-360,.6f,true);
 auto debited=plan(p,cfg,nullptr,0,20,10,t+10,10100,1,-360,.6f,true);
 near(initial.availableWh-debited.availableWh,.9f); // Same acquisition sequence, actual held power debited each call.
 auto invalid=plan(p,cfg,nullptr,0,20,10,t+20,20100,1,0,.6f,true,false);assert(!invalid.valid);
 auto resumed=plan(p,cfg,nullptr,0,20,10,t+600,600100,2,360,.6f,true);
 assert(resumed.availableWh<=debited.availableWh+.001f); // No SOC reseed or outage charging credit.
 auto charged=plan(p,cfg,nullptr,0,20,10,t+610,610100,3,360,.6f,true);
 near(charged.availableWh-resumed.availableWh,.9f);
 auto silent=plan(p,cfg,nullptr,0,20,10,t+1000,1000100,3,360,.6f,true);
 assert(silent.availableWh<=charged.availableWh+.001f);
}
static void currentSlotAndCache(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices p;load(p,{600000,100000,100000,100000});
 auto a=plan(p,cfg,nullptr,0,5,5,t,100,1,0,.6f,true);const auto builds=p.planningBuilds();assert(builds==1);
 auto b=plan(p,cfg,nullptr,0,5,5,t+450,1000,1,0,.6f,true);
 assert(b.batteryCeilingW>a.batteryCeilingW);assert(p.planningBuilds()==builds);
 assert(float(b.batteryCeilingW)*.125f<=b.availableWh+.001f);
 auto c=plan(p,cfg,nullptr,0,5,5,t+901,2000,1,0,.1f,false);assert(c.valid&&p.planningBuilds()==builds+1);
 FuturePrices cached;load(cached,{100000,100000,600000,500000});PvForecast pv;solar(pv,{1500,1500,0,0});
 plan(cached,cfg,&pv,1500.1f);const auto n=cached.planningBuilds();
 plan(cached,cfg,&pv,1500.9f,5,100,t+1,200);assert(cached.planningBuilds()==n);
 plan(cached,cfg,&pv,1501,5,100,t+2,300);assert(cached.planningBuilds()==n+1);
 plan(cached,cfg,&pv,1501,5,100,t+2,1800301);assert(cached.planningBuilds()==n+2);
 cfg.priceComfortEurPerKwh=.26f;plan(cached,cfg,&pv,1501,5,100,t+2,1800400);assert(cached.planningBuilds()==n+2); // Comfort classification no longer changes the cached economic order.
}
static void snapshotGapsAndConflicts(){
 PreparedSettings cfg;cfg.valid=true;const std::vector<int> values{100000,100000,400000,500000,100000,100000,100000,100000};
 FuturePrices p;load(p,values);auto first=plan(p,cfg);assert(first.valid);
 auto gap=plan(p,cfg,nullptr,0,5,100,t+1900,1900100,2,0,.4f,true);assert(gap.valid);
 FuturePrices restored;load(restored,values,t,t+1900);assert(plan(restored,cfg,nullptr,0,5,100,t+1900,100,1,0,.4f,true).valid);
 auto expired=plan(p,cfg,nullptr,0,5,100,t+7200,7200100,3,0,.1f,false);assert(!expired.valid);
 assert(!std::strcmp(send(p,prices(values),t+7200),"packet_range")); // Replay never extends immutable end.
 auto missingCurrent=restored.plan(t+1900,200,cfg,true,5,0,100,2,false,.4f,true,1,900);
 assert(!missingCurrent.valid&&!std::strcmp(missingCurrent.reason,"current_price_missing"));
 JsonDocument overflow;deserializeJson(overflow,prices(values));const uint32_t last=(UINT32_MAX/900)*900;
 overflow["r"]=last;overflow["t"]=last;std::string bad;serializeJson(overflow,bad);
 assert(!std::strcmp(send(restored,bad,last),"packet_range"));
 std::vector<int> many(48,100000);FuturePrices pending;
 assert(!std::strcmp(send(pending,prices(many,t,0)),"pending"));
 many[0]=200000;assert(!std::strcmp(send(pending,prices(many,t,0)),"packet_conflict"));
 assert(!std::strcmp(send(pending,prices(many,t,1)),"packet_conflict"));assert(pending.slots()==0);
 assert(!std::strcmp(send(pending,prices({},t)),"packet_conflict")); // Same revision cannot turn conflicting data into withdrawal.
}

// [MOCKED!] The trace must expose the actual allocator without changing scalar decisions.
static void dischargeBudgetTrace(){
 PreparedSettings cfg;cfg.valid=true;FuturePrices plain,traced;
 const std::vector<int> values{400000,100000,600000,500000};load(plain,values);load(traced,values);
 PvForecast pv;solar(pv,{1500,1500,0,0});DischargeBudget trace;
 for(unsigned step=0;step<3;++step){
  const uint32_t at=t+step*120,ms=100+step*120000;
  auto a=plain.plan(at,ms,cfg,true,20,0,10,1,true,.4f,true,1,900,&pv,1500);
  auto b=traced.plan(at,ms,cfg,true,20,0,10,1,true,.4f,true,1,900,&pv,1500,&trace);
  assert(a.valid&&b.valid&&trace.valid&&trace.count==4&&trace.start==t&&trace.generatedAt==at);
  assert(trace.priceRevision==t&&trace.pvRevision==t+1&&trace.watts[0]==b.batteryCeilingW);
  assert(a.batteryCeilingW==b.batteryCeilingW);near(a.reservedWh,b.reservedWh);near(a.availableWh,b.availableWh);
  near(a.usablePvWh,b.usablePvWh);near(a.chargeTargetW,b.chargeTargetW);assert(a.targetAt==b.targetAt);
  float future=0;for(unsigned i=1;i<trace.count;++i)future+=trace.watts[i]*.25f;
  assert(future<=b.reservedWh+.001f&&b.reservedWh-future<.75f);
 }
 FuturePrices limited;load(limited,{100000,100000,400000,500000});
 auto b=limited.plan(t,100,cfg,true,10,0,100,1,true,.1f,false,1,900,nullptr,0,&trace);
 assert(b.valid&&trace.watts[0]==179&&trace.watts[1]==0&&trace.watts[2]==650&&trace.watts[3]==650);
 char payload[DischargeBudget::kPayloadBytes];auto length=trace.serialize(payload,sizeof(payload));assert(length>0);
 JsonDocument decoded;assert(!deserializeJson(decoded,payload));assert(decoded["valid"]==true&&decoded["w"].size()==4);
 assert(decoded["w"][0]==179&&decoded["w"][2]==650&&decoded["w"][3]==650); // 369.936 Wh: reserve 325 Wh, then use 44.936 Wh now.
 char small[64];assert(trace.serialize(small,sizeof(small))==0&&small[0]=='\0');
 FuturePrices partial;load(partial,{600000,100000});
 auto c=partial.plan(t+450,100,cfg,true,5,0,50,1,true,.6f,true,1,900,nullptr,0,&trace);
 assert(c.valid&&trace.generatedAt==t+450&&trace.start==t&&trace.count==2&&trace.watts[0]==554);
 auto missing=partial.plan(t+450,200,cfg,false,5,0,50,2,true,.6f,true,1,900,nullptr,0,&trace);
 assert(!missing.valid&&!trace.valid&&trace.count==0&&!std::strcmp(trace.reason,"energy_missing"));
 length=trace.serialize(payload,sizeof(payload));assert(length>0);decoded.clear();assert(!deserializeJson(decoded,payload));
 assert(decoded["valid"]==false&&decoded["n"]==0&&decoded["w"].size()==0);
 FuturePrices large;load(large,std::vector<int>(288,600000));
 auto full=large.plan(t,100,cfg,true,100,0,389,1,true,.6f,true,1,2147483647,nullptr,0,&trace);
 assert(full.valid&&trace.count==144&&trace.serialize(payload,sizeof(payload))>0);
 for(unsigned i=0;i<144;++i)trace.watts[i]=2147483647;
 length=trace.serialize(payload,sizeof(payload));assert(length>1584&&length<2048);
 assert(DischargeBudget::packetFits(128,length));assert(!DischargeBudget::packetFits(2304,length));
 assert(!DischargeBudget::packetFits(1,2048));
 FuturePrices charging;load(charging,{100000,100000,600000,500000});
 auto on=charging.plan(t,100,cfg,true,5,0,100,1,true,.1f,false,1,900,&pv,1500,&trace);
 assert(on.usablePvWh==325&&trace.watts[2]==0&&trace.watts[3]==0&&trace.watts[0]==554);
 auto off=charging.plan(t,100,cfg,true,5,0,100,1,true,.1f,false,1,900,&pv,0,&trace);
 assert(off.usablePvWh==0&&trace.watts[2]==554&&trace.watts[3]==0);
 DischargeBudgetPublication publication;
 assert(!publication.captureDue(0));publication.connection(true);assert(publication.captureDue(0));
 publication.snapshot.finish(true,"forecast_release");publication.captured(10);assert(publication.publishDue(10));
 publication.published(false,10);assert(!publication.publishDue(5009)&&publication.publishDue(5010));
 publication.published(true,5010);assert(!publication.publishDue(6000));
 assert(!publication.captureDue(60009)&&publication.captureDue(60010));
 assert(publication.invalidationDue(false)&&!publication.invalidationDue(true));
 publication.snapshot.finish(false,"energy_missing");publication.captured(6020);
 assert(!publication.invalidationDue(false));publication.published(true,6020);
 assert(!publication.invalidationDue(false));
 publication.connection(false);assert(!publication.captureDue(90000)&&!publication.publishDue(90000));
 publication.connection(true);assert(publication.captureDue(90000));
 publication.captured(UINT32_MAX-10000);assert(!publication.captureDue(49998)&&publication.captureDue(49999));
 publication.topicChanged();assert(publication.captureDue(0)&&!publication.publishDue(0));
 publication.snapshot.finish(true,"forecast_release");publication.captured(10);publication.published(false,10);
 assert(publication.invalidationDue(false)); // A failed first valid send must not later publish a revoked plan.
 std::cout<<"[MOCKED!] Discharge budget trace, scalar equivalence, chronological allocation, partial slot, invalidity, PV charge veto and packet bounds passed; trace="<<sizeof(trace)<<" bytes, publication="<<sizeof(publication)<<" bytes, maximum packet="<<length<<" bytes\n";
}
int main(){freeGridDoesNotSpendStoredEnergy();ownerTariffsKeepStoredAndNewEnergySeparate();relativePricesAndExistingEnergy();dischargeBudgetTrace();economicTraces();creditNeverCreatesCurrentEnergy();pvCoverageBoundaries();energyDebitAndGaps();currentSlotAndCache();snapshotGapsAndConflicts();
 std::cout<<"Future price/PV traces, bounded credit, signed pool, snapshot gaps and cached planning passed; object="<<sizeof(FuturePrices)<<" bytes\n";}
