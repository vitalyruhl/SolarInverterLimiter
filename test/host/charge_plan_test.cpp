#include "control/ChargePlan.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using namespace control;
static constexpr uint32_t t = 1792454400;

// [MOCKED!] Synthetic source packets enter the actual receivers, planner and guarded control.
struct Fixture {
    PreparedSettings cfg;
    FuturePrices prices;
    PvForecast pv;
    DischargeBudget red;
    FuturePlan future;
    ChargePlan blue;
    ChargePlanInputs inputs;
    CurrentDecision decision;
    float soc = 50, ah = 10, currentPrice = .1f, currentSolar = 400, chargeCap = 1500;
    int maximum = 900;
    Fixture(const std::vector<int>& tariff = {100000,600000,600000,600000},
            const std::vector<int>& solar = {400,0,0,0}) {
        assert(tariff.size() == solar.size());
        cfg.valid = true; cfg.reserveOffSocPercent = 2; cfg.reserveOnSocPercent = 10;
        cfg.priceComfortEurPerKwh = .2f; cfg.priceDeadbandEurPerKwh = .15f;
        inputs.sourcesQualified = inputs.chargeAvailable = inputs.controlEnabled = true;
        for (unsigned part = 0; part * 24 < tariff.size(); ++part) {
            JsonDocument d; d["v"]=1; d["r"]=t; d["t"]=t; d["n"]=tariff.size(); d["k"]=part;
            auto a=d["p"].to<JsonArray>();
            for(unsigned i=part*24;i<tariff.size()&&i<(part+1)*24;++i)a.add(tariff[i]);
            std::string packet; serializeJson(d,packet);
            const auto result=prices.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),t,100+part);
            assert(!std::strcmp(result,(part+1)*24>=tariff.size()?"accepted":"pending"));
        }
        for (unsigned part = 0; part * 24 < solar.size(); ++part) {
            JsonDocument d; d["v"]=2; d["s"]=t-900; d["r"]=t; d["g"]=t; d["t"]=t;
            d["d"]=900; d["n"]=solar.size(); d["k"]=part; d["e"]=t+unsigned(solar.size())*900;
            d["f"]=1000; d["c"]=0; d["a"]=0; d["q"]="uncalibrated"; d["o"]="forecast_solar";
            auto a=d["p"].to<JsonArray>(), x=d["x"].to<JsonArray>();
            for(unsigned i=part*24;i<solar.size()&&i<(part+1)*24;++i){
                if(solar[i]<0)a.add(nullptr);else a.add(solar[i]);
                x.add(solar[i]<0?1:2);
            }
            std::string packet; serializeJson(d,packet);
            const auto result=pv.receive(reinterpret_cast<const uint8_t*>(packet.data()),unsigned(packet.size()),t,100+part);
            assert(!std::strcmp(result,(part+1)*24>=solar.size()?"accepted":"pending"));
        }
    }
    void run(uint32_t utc=t) {
        future=prices.plan(utc,100,cfg,true,soc,0,ah,1,true,currentPrice,currentPrice>=.35f,
                           1,maximum,&pv,chargeCap,&red,currentSolar);
        CurrentInputs in;
        in.bmsValid=in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=true;
        in.chargerValid=in.metersValid=in.priceValid=true; in.sourcesWithinGrace=true;
        in.soc=soc; in.price=currentPrice; in.chargerW=currentSolar; in.householdW=2000;
        in.futureValid=future.valid; in.futureBatteryCeilingW=future.batteryCeilingW;
        in.futureChargeTargetW=future.chargeTargetW; in.futureReason=future.reason;
        CurrentControl control;
        for(unsigned i=0;i<3;++i){
            in.bmsSequence=in.chargerSequence=i+1;
            decision=control.update(cfg,in,true,true,maximum,0,100+i*3000);
        }
        inputs.soc=soc; inputs.fullCapacityAh=ah; inputs.currentChargerW=currentSolar;
        inputs.maxChargeW=chargeCap; inputs.priceFactor=1;
        blue.project(utc,cfg,future,prices,pv,decision,red,inputs);
    }
    // Independent prefix balance, using DC charge and AC battery output exactly once.
    void balance(uint32_t utc=t) const {
        assert(red.valid&&blue.valid&&red.count==blue.count);
        const float full=ah*51.38f, capacity=(100-cfg.reserveOffSocPercent)*.01f*full*.9f;
        float pool=future.availableWh, headroom=(100-soc)*.01f*full;
        float debt=std::max(0.0f,(cfg.reserveOffSocPercent-soc)*.01f*full);
        for(unsigned i=0;i<red.count;++i){
            const float hours=float(red.start+(i+1)*900-(i?red.start+i*900:utc))/3600;
            const float discharge=red.watts[i]*hours;
            const float charge=prices.projectedChargeW(i)*hours;
            const float solar=std::max(0,prices.projectedSolarW(i))*hours;
            assert(discharge>=0&&discharge<=pool+.002f);
            assert(charge>=0&&charge<=solar+.002f&&charge<=headroom+.002f);
            assert(discharge<=.002f||charge<=.002f);
            if(discharge>0)assert(float(red.watts[i])+(i?std::min(solar/hours,chargeCap):solar/hours)*.9f<=maximum+.01f);
            assert(blue.solarWatts[i]==prices.projectedSolarW(i));
            if(blue.solarWatts[i]<0)assert(blue.watts[i]==-1&&charge==0);
            else assert(blue.watts[i]>=0&&blue.watts[i]<=prices.projectedChargeW(i)+.001f);
            pool-=discharge;headroom+=discharge/.9f;
            const float restored=std::min(debt,charge);debt-=restored;
            pool+=(charge-restored)*.9f;headroom-=charge;
            assert(pool<=capacity+.002f&&pool>=-.002f&&headroom>=-.002f);
        }
    }
};

static void coupledMorningSolarAndEvening() {
    Fixture f({400000,500000,100000,100000,100000,100000,600000,600000},
              {0,-1,400,400,400,400,0,0});
    f.soc=98;f.currentPrice=.4f;f.currentSolar=0;f.run();f.balance();
    assert(f.future.batteryCeilingW>0&&f.red.watts[0]==f.future.batteryCeilingW);
    assert(f.red.watts[1]==900&&f.red.watts[6]==900&&f.red.watts[7]==900);
    assert(f.blue.watts[0]==0&&f.blue.watts[1]==-1);
    float charge=0;for(unsigned i=2;i<6;++i)charge+=f.blue.watts[i]*.25f;
    assert(charge>399&&charge<=400);
}

template<class P> static auto planWithCheapState(P& p,Fixture& f,bool cheap,int)
    ->decltype(p.plan(t,100,f.cfg,true,f.soc,0,f.ah,1,true,f.currentPrice,false,1,f.maximum,&f.pv,f.chargeCap,&f.red,f.currentSolar,cheap)) {
    return p.plan(t,100,f.cfg,true,f.soc,0,f.ah,1,true,f.currentPrice,false,1,f.maximum,&f.pv,f.chargeCap,&f.red,f.currentSolar,cheap);
}
template<class P> static FuturePlan planWithCheapState(P& p,Fixture& f,bool,long) {
    return p.plan(t,100,f.cfg,true,f.soc,0,f.ah,1,true,f.currentPrice,false,1,f.maximum,&f.pv,f.chargeCap,&f.red,f.currentSolar);
}
static void actualCheapHysteresisIsShared() {
    Fixture f({80000,80000,80000,80000},{400,0,0,0});f.currentPrice=.08f;f.soc=5;
    CurrentInputs in;in.bmsValid=in.batteryChargeAvailable=in.batteryDischargeIntentAvailable=true;
    in.chargerValid=in.metersValid=in.priceValid=true;in.soc=f.soc;in.chargerW=400;in.price=.01f;in.householdW=1000;
    CurrentControl control;
    for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;control.update(f.cfg,in,true,true,900,0,100+i*3000);}
    assert(!control.expensivePrice(f.cfg,true,.08f));
    assert(control.cheapPrice());
    f.future=planWithCheapState(f.prices,f,control.cheapPrice(),0);
    in.price=.08f;in.futureValid=f.future.valid;in.futureBatteryCeilingW=f.future.batteryCeilingW;
    in.futureChargeTargetW=f.future.chargeTargetW;in.futureReason=f.future.reason;
    in.bmsSequence=in.chargerSequence=4;
    f.decision=control.update(f.cfg,in,true,true,900,0,9100);
    f.inputs.soc=f.soc;f.inputs.fullCapacityAh=f.ah;f.inputs.currentChargerW=400;f.inputs.maxChargeW=1500;
    f.blue.project(t,f.cfg,f.future,f.prices,f.pv,f.decision,f.red,f.inputs);
    assert(!std::strcmp(f.decision.priceBand,"cheap")&&f.decision.chargeTargetW==200);
    if(f.red.watts[0]!=0||f.blue.watts[0]!=200){std::cerr<<"FAIL: actual latched cheap band must govern the shared current battery flow\n";std::abort();}
    f.balance();
}
static void currentMeasurementAndPartialSlot() {
    Fixture f({100000,600000,600000,600000},{1500,0,0,0});
    f.run();f.balance();
    assert(f.red.watts[0]==0&&f.blue.watts[0]==400&&f.future.chargeTargetW==400);
    assert(f.decision.chargeTargetValid&&f.decision.chargeTargetW==400);
    Fixture half({100000,600000,600000,600000},{1500,0,0,0});
    half.run(t+450);half.balance(t+450);
    assert(half.blue.watts[0]==400);
    assert(half.prices.projectedChargeW(0)*.125f==50);
    assert(half.future.availableWh==f.future.availableWh);
}

static void directPvAndConfiguredAcLimit() {
    for(int maximum:{500,900,1200}){
        Fixture f({400000,400000,400000,400000},{900,0,0,0});
        f.currentPrice=.4f;f.currentSolar=900;f.soc=94;f.ah=100;f.maximum=maximum;
        f.run();f.balance();
        assert(f.red.watts[0]==std::max(0,maximum-810));
        if(maximum>810)assert(f.blue.watts[0]==0);
        else assert(f.blue.watts[0]>=344&&f.blue.watts[0]<=345);
    }
    Fixture capped; capped.chargeCap=100;capped.run();capped.balance();
    assert(capped.blue.watts[0]<=100&&capped.blue.solarWatts[0]==400);
    Fixture blocked;blocked.chargeCap=0;blocked.run();blocked.balance();
    for(unsigned i=0;i<blocked.blue.count;++i)assert(blocked.blue.watts[i]==0);
    assert(blocked.blue.solarWatts[0]==400);
}

static void reserveDebtAndEconomicOpportunity() {
    Fixture insufficient({400000,600000},{400,0});
    insufficient.cfg.reserveOffSocPercent=20;insufficient.cfg.reserveOnSocPercent=25;
    insufficient.currentPrice=.4f;insufficient.soc=10;insufficient.ah=100;
    insufficient.run();insufficient.balance();
    assert(insufficient.red.watts[0]==0&&insufficient.red.watts[1]==0);
    Fixture paid({400000,600000},{400,0});
    paid.cfg.reserveOffSocPercent=20;paid.cfg.reserveOnSocPercent=25;
    paid.currentPrice=.4f;paid.soc=20-50.0f/5138*100;paid.ah=100;paid.maximum=120;
    paid.run();paid.balance();assert(paid.red.watts[1]>=119);
    assert(paid.blue.remainingWhDC<.01f);
    for(int later:{268,468}){
        Fixture staged({400000,100000,600000},{200,later,0});
        staged.cfg.reserveOffSocPercent=20;staged.cfg.reserveOnSocPercent=25;
        staged.currentPrice=.4f;staged.currentSolar=200;staged.soc=20-50.0f/5138*100;staged.ah=100;staged.maximum=240;
        staged.run();staged.balance();assert(staged.red.watts[2]>=239);
        if(later==268)assert(staged.blue.watts[0]>=198&&staged.blue.watts[0]<200);
        else assert(staged.blue.watts[0]==0);
    }
    Fixture high({400000,100000,600000},{2100,268,0});
    high.cfg.reserveOffSocPercent=95;high.cfg.reserveOnSocPercent=98;
    high.currentPrice=.4f;high.currentSolar=2100;high.soc=85;high.ah=100;high.maximum=240;high.chargeCap=3000;
    high.run();high.balance();assert(high.red.watts[2]>=239);
    Fixture unprofitable({100000,140000},{400,0});
    unprofitable.run();unprofitable.balance();
    assert(unprofitable.future.shortfallWh==0&&unprofitable.blue.watts[0]==0);
    Fixture full;full.soc=100;full.run();full.balance();
    assert(full.blue.watts[0]==0);
}

static void negativeAndSourceGates() {
    Fixture negative({-100000,600000,-100000,600000},{400,0,400,0});
    negative.currentPrice=-.1f;negative.run();negative.balance();
    assert(negative.red.watts[0]==0&&negative.blue.watts[0]==0&&negative.blue.watts[2]==0);
    assert(negative.decision.ceilingW==0&&!std::strcmp(negative.decision.reason,"negative_price"));
    Fixture f;f.run();
    f.inputs.sourcesQualified=false;f.blue.project(t,f.cfg,f.future,f.prices,f.pv,f.decision,f.red,f.inputs);
    assert(!f.blue.valid&&f.blue.count==0);
    f.inputs.sourcesQualified=true;f.inputs.chargeAvailable=false;
    f.blue.project(t,f.cfg,f.future,f.prices,f.pv,f.decision,f.red,f.inputs);
    assert(f.blue.valid&&!std::strcmp(f.blue.reason,"charge_blocked"));
    for(unsigned i=0;i<f.blue.count;++i)assert(f.blue.watts[i]==0);
}

static void generationAndLiveWithoutCapture() {
    Fixture f;f.run();const auto builds=f.prices.planningBuilds();
    const auto live=f.prices.plan(t,100,f.cfg,true,f.soc,0,f.ah,1,true,f.currentPrice,false,
                                  1,f.maximum,&f.pv,f.chargeCap,nullptr,f.currentSolar);
    assert(live.coupledProjection&&f.prices.planningBuilds()==builds);
    assert(live.chargeTargetW==f.future.chargeTargetW&&live.batteryCeilingW==f.future.batteryCeilingW);
    f.blue.project(t,f.cfg,live,f.prices,f.pv,f.decision,f.red,f.inputs);assert(f.blue.valid);
    f.prices.plan(t+1,100,f.cfg,true,f.soc,0,f.ah,1,true,f.currentPrice,false,1,f.maximum,&f.pv,f.chargeCap,nullptr,f.currentSolar);
    f.blue.project(t,f.cfg,f.future,f.prices,f.pv,f.decision,f.red,f.inputs);
    assert(!f.blue.valid&&f.blue.count==0);
    f.prices.clear();assert(!f.prices.projectionMatches(t+1,t,4));
}

static void invalidInputsAndSerialization() {
    Fixture invalid;invalid.soc=std::numeric_limits<float>::quiet_NaN();invalid.run();
    assert(!invalid.future.valid&&!invalid.blue.valid&&invalid.blue.count==0);
    Fixture nonfinite;nonfinite.currentSolar=std::numeric_limits<float>::infinity();nonfinite.run();
    assert(nonfinite.future.batteryCeilingW>=0&&nonfinite.future.chargeTargetW==0);
    assert(!nonfinite.blue.valid&&nonfinite.blue.count==0);
    Fixture large(std::vector<int>(144,400000),std::vector<int>(144,1000));
    large.currentPrice=.4f;large.currentSolar=1000;large.run();large.balance();
    assert(large.prices.planningSteps()==144*144);
    for(unsigned i=0;i<144;++i){large.blue.watts[i]=12000;large.blue.solarWatts[i]=12000;}
    char payload[ChargePlan::kPayloadBytes];const auto size=large.blue.serialize(payload,sizeof(payload));assert(size>0);
    JsonDocument decoded;assert(!deserializeJson(decoded,payload));
    assert(decoded["w"].size()==144&&decoded["s"].size()==144&&decoded["kind"]=="intent_projection");
    assert(ChargePlan::packetFits(128,size)&&!ChargePlan::packetFits(2304,size));
    char tiny[32];assert(large.blue.serialize(tiny,sizeof(tiny))==0);
    std::cout<<"[MOCKED!] Coupled plan workspace="<<sizeof(FuturePrices)<<", charge trace="<<sizeof(ChargePlan)
             <<", backward rank visits="<<large.prices.planningSteps()<<", maximum payload="<<size<<"\n";
}
int main(){
    actualCheapHysteresisIsShared();coupledMorningSolarAndEvening();currentMeasurementAndPartialSlot();directPvAndConfiguredAcLimit();
    reserveDebtAndEconomicOpportunity();negativeAndSourceGates();generationAndLiveWithoutCapture();
    invalidInputsAndSerialization();
    std::cout<<"[MOCKED!] Shared live/red/blue flow, reserve, PV arrival, direct use, source gates and serialization passed\n";
}
