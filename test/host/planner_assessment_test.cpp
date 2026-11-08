#include "control/FuturePrices.h"
#include "control/PlanCaptureContext.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
using namespace control;
static constexpr uint32_t t=1792454400;
// [MOCKED!] UTC tariffs, origins, PV and battery samples exercise the real receivers/allocator.
template<class P> static auto receive2(P& p,const std::string& s,uint32_t now,uint32_t ms,int)
 ->decltype(p.receive(reinterpret_cast<const uint8_t*>(s.data()),unsigned(s.size()),now,ms,true)) {
 return p.receive(reinterpret_cast<const uint8_t*>(s.data()),unsigned(s.size()),now,ms,true);
}
template<class P> static const char* receive2(P& p,const std::string& s,uint32_t now,uint32_t ms,long) {
 return p.receive(reinterpret_cast<const uint8_t*>(s.data()),unsigned(s.size()),now,ms);
}
template<class C> static auto settings(C& c,float tolerance,float premium,int)
 ->decltype(c.deferralToleranceEurKwh=tolerance,c.predictionPremiumPct=premium,void()) {
 c.deferralToleranceEurKwh=tolerance;c.predictionPremiumPct=premium;
}
template<class C> static void settings(C&,float,float,long) {}
template<class P> static auto origin(const P& p,uint32_t at,int)->decltype(p.originAt(at)){return p.originAt(at);}
template<class P> static unsigned origin(const P&,uint32_t,long){return 0;}
template<class P> static auto assessed(const P& p,uint32_t at,const PreparedSettings& cfg,float& value,int)
 ->decltype(p.assessedPriceAt(at,1,cfg,value)){return p.assessedPriceAt(at,1,cfg,value);}
template<class P> static bool assessed(const P& p,uint32_t at,const PreparedSettings&,float& value,long){return p.priceAt(at,1,value);}
static void near(float a,float b){assert(std::fabs(a-b)<.000002f);}
static std::string packet(const std::vector<int>& values,const std::vector<int>& origins,uint32_t rev=t,unsigned part=0,int version=2) {
 JsonDocument d;d["v"]=version;d["r"]=rev;d["t"]=t;d["n"]=values.size();d["k"]=part;
 if(version==2)d["d"]=900;
 auto p=d["p"].to<JsonArray>(),o=d["o"].to<JsonArray>();
 for(unsigned i=part*24;i<values.size()&&i<(part+1)*24;++i)p.add(values[i]);
 for(unsigned i=part*24;i<origins.size()&&i<(part+1)*24;++i)o.add(origins[i]);
 std::string s;serializeJson(d,s);return s;
}
static void load(FuturePrices& p,const std::vector<int>& prices,const std::vector<int>& origins,uint32_t rev=t) {
 for(unsigned k=0;k*24<prices.size();++k) {
  const auto s=packet(prices,origins,rev,k);
  const char* result=receive2(p,s,rev,100+k,0);
  assert(!std::strcmp(result,(k+1)*24>=prices.size()?"accepted":"pending"));
 }
}
static FuturePlan plan(FuturePrices& p,const PreparedSettings& cfg,DischargeBudget& trace,const PvForecast* pv=nullptr,float cap=0,float current=.4f,float soc=50) {
 return p.plan(t,100,cfg,true,soc,0,10,1,true,current,false,1,900,pv,cap,&trace);
}
static void atomicOrigins() {
 FuturePrices p;std::vector<int> prices(25,400000),flags(25,2);
 auto a=packet(prices,flags);
 assert(!std::strcmp(receive2(p,a,t,100,0),"pending"));
 assert(p.slots()==0);
 assert(!std::strcmp(receive2(p,packet(prices,flags,t,1),t,101,0),"accepted"));
 assert(p.slots()==25&&origin(p,t,0)==2);
 auto changed=flags;changed[0]=1;
 assert(!std::strcmp(receive2(p,packet(prices,changed),t,102,0),"packet_conflict"));
 const auto before=p.contentRevision();
 load(p,prices,changed,t+1);
 assert(p.contentRevision()>before&&origin(p,t,0)==1);
 // A mixed/incomplete newer revision must not replace the last complete active frame.
 auto n=packet(prices,flags,t+2);
 assert(!std::strcmp(receive2(p,n,t+2,200,0),"pending"));
 std::vector<int> other(26,400000),otherFlags(26,2);
 assert(!std::strcmp(receive2(p,packet(other,otherFlags,t+2,1),t+2,201,0),"packet_conflict"));
 assert(p.revision()==t+1&&origin(p,t,0)==1);
 assert(!std::strcmp(receive2(p,n,t+2,202,0),"packet_conflict"));
 // Timed-out partial assembly needs every chunk again.
 assert(!std::strcmp(receive2(p,packet(prices,flags,t+3),t+3,1000,0),"pending"));
 assert(!std::strcmp(receive2(p,packet(prices,flags,t+3,1),t+3,32001,0),"pending"));
 assert(p.revision()==t+1);
}
static void strictHeldPricesAndCache() {
 PreparedSettings cfg;cfg.valid=true;DischargeBudget trace;
 FuturePrices defaults;load(defaults,{400000,420000},{1,1});
 assert(plan(defaults,cfg,trace).batteryCeilingW>0);
 FuturePrices strict;load(strict,{400000,420000},{1,1});settings(cfg,0,0,0);
 assert(plan(strict,cfg,trace).batteryCeilingW==0&&trace.watts[1]>0);
 auto builds=strict.planningBuilds();settings(cfg,.03f,0,0);
 assert(plan(strict,cfg,trace).batteryCeilingW>0&&strict.planningBuilds()>builds);
 settings(cfg,0,0,0);FuturePrices equal;load(equal,{400000,400000},{1,1});
 assert(plan(equal,cfg,trace).batteryCeilingW>0&&trace.watts[1]==0);
 FuturePrices tiny;load(tiny,{400000,400001},{1,1});
 assert(plan(tiny,cfg,trace).batteryCeilingW==0&&trace.watts[1]>0); // One micro-EUR is still strictly higher.
 PlanCaptureContext before,after;before.settings=cfg;after=before;settings(after.settings,.03f,0,0);
 assert(!before.sameHard(after));
 after=before;settings(after.settings,0,30,0);assert(!before.sameHard(after));
}
static void premiumsAreValuationOnly() {
 PreparedSettings cfg;cfg.valid=true;settings(cfg,0,30,0);
 FuturePrices p;load(p,{200000,-200000,0,200000,200000},{2,2,2,1,0});
 float v;
 assert(assessed(p,t,cfg,v,0));near(v,.26f);
 assert(assessed(p,t+900,cfg,v,0));near(v,-.14f);
 assert(assessed(p,t+1800,cfg,v,0));near(v,0);
 assert(assessed(p,t+2700,cfg,v,0));near(v,.2f);
 assert(assessed(p,t+3600,cfg,v,0));near(v,.2f);
 assert(p.priceAt(t+900,1,v));near(v,-.2f);
 FuturePrices baseline,risk;load(baseline,{500000,450000},{1,2});load(risk,{500000,450000},{1,2});
 PreparedSettings normal;normal.valid=true;settings(normal,0,0,0);DischargeBudget a,b;
 const auto plain=plan(baseline,normal,a,nullptr,0,.5f),adjusted=plan(risk,cfg,b,nullptr,0,.5f);
 assert(plain.batteryCeilingW>0&&adjusted.batteryCeilingW==0);
 near(plain.availableWh,adjusted.availableWh);
 auto builds=risk.planningBuilds();settings(cfg,0,0,0);
 assert(plan(risk,cfg,b,nullptr,0,.5f).batteryCeilingW>0&&risk.planningBuilds()>builds);
}
static void solar(PvForecast& pv) {
 JsonDocument d;d["v"]=2;d["s"]=t-900;d["r"]=t;d["g"]=t;d["t"]=t;d["d"]=900;d["n"]=2;d["k"]=0;
 d["e"]=t+1800;d["f"]=1000;d["c"]=0;d["a"]=0;d["q"]="uncalibrated";d["o"]="forecast_solar";
 auto p=d["p"].to<JsonArray>(),x=d["x"].to<JsonArray>();p.add(1000);p.add(0);x.add(2);x.add(2);
 std::string s;serializeJson(d,s);assert(!std::strcmp(pv.receive(reinterpret_cast<const uint8_t*>(s.data()),s.size(),t,100),"accepted"));
}
static void rawNegativeAndAcquisitionGates() {
 PreparedSettings cfg;cfg.valid=true;settings(cfg,0,100,0);PvForecast pv;solar(pv);DischargeBudget red;
 FuturePrices negative;load(negative,{-100000,500000},{2,1});
 const auto result=plan(negative,cfg,red,&pv,1000,-.1f,2);
 assert(result.chargeTargetW==0&&red.watts[0]==0&&red.watts[1]==0);
 // Strict holding does not remove the independent loss/wear/minimum acquisition benefit.
 FuturePrices marginal;load(marginal,{410000,450000},{1,1});settings(cfg,0,0,0);
 assert(plan(marginal,cfg,red,&pv,1000,.41f,2).chargeTargetW==0&&red.watts[1]==0);
 FuturePrices empty;load(empty,{400000,500000},{2,2});settings(cfg,0,30,0);
 const auto noEnergy=plan(empty,cfg,red,nullptr,0,.4f,2);
 assert(noEnergy.availableWh==0&&red.watts[0]==0&&red.watts[1]==0);
}
static void shadowExpiryAndWithdrawalPreserveEnergy() {
 PreparedSettings cfg;cfg.valid=true;settings(cfg,0,0,0);DischargeBudget red;FuturePrices p;
 load(p,{400000,500000},{1,2});const auto initial=plan(p,cfg,red);
 const auto stale=p.plan(t+180,200,cfg,true,50,0,10,2,true,.4f,false,1,900,nullptr,0,&red);
 assert(!stale.valid && !red.valid);
 JsonDocument withdrawal;withdrawal["v"]=2;withdrawal["r"]=t+181;withdrawal["t"]=t;withdrawal["d"]=900;withdrawal["n"]=0;withdrawal["k"]=0;
 withdrawal["p"].to<JsonArray>();withdrawal["o"].to<JsonArray>();std::string w;serializeJson(withdrawal,w);
 assert(!std::strcmp(receive2(p,w,t+181,300,0),"withdrawn"));assert(p.slots()==0);
 const auto empty=p.plan(t+181,301,cfg,true,50,0,10,3,true,.4f,false,1,900,nullptr,0,&red);
 assert(!empty.valid);near(initial.availableWh,empty.availableWh);
 load(p,{400000,500000},{1,2},t+182);
 const auto restored=p.plan(t+182,302,cfg,true,50,0,10,4,true,.4f,false,1,900,nullptr,0,&red);
 assert(restored.valid);near(initial.availableWh,restored.availableWh);
 FuturePrices normal;auto legacy=packet({400000,500000},{},t,0,1);
 assert(!std::strcmp(normal.receive(reinterpret_cast<const uint8_t*>(legacy.data()),legacy.size(),t,100),"accepted"));
 assert(normal.plan(t+180,200,cfg,true,50,0,10,1,true,.4f,false,1,900,nullptr,0,&red).valid);
}
static void malformedAndDefaultWire() {
 for(auto flags:std::vector<std::vector<int>>{{2},{-1,1},{3,1}}) {
  FuturePrices p;assert(std::strcmp(receive2(p,packet({400000,500000},flags),t,100,0),"accepted"));
 }
 FuturePrices p;auto valid=packet({400000,500000},{1,2});
 assert(std::strcmp(p.receive(reinterpret_cast<const uint8_t*>(valid.data()),valid.size(),t,100),"accepted")); // Normal parser remains v1-only.
 std::vector<int> a(288,10000000),o(288,2);FuturePrices max;
 for(int k=11;k>=0;--k) {
  auto s=packet(a,o,t,unsigned(k));assert(s.size()<=768);
  const char* result=receive2(max,s,t,100+unsigned(11-k),0);
  assert(!std::strcmp(result,k==0?"accepted":"pending"));
 }
 assert(max.slots()==288);
}
int main() {
 shadowExpiryAndWithdrawalPreserveEnergy();atomicOrigins();strictHeldPricesAndCache();premiumsAreValuationOnly();
 rawNegativeAndAcquisitionGates();malformedAndDefaultWire();
 std::cout<<"[MOCKED!] Assessment origins, raw gates, strict holding, cache and atomic wire tests passed\n";
}
