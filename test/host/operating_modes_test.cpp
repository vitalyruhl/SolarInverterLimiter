#include "control/OperatingModes.h"
#include "control/ModeProfile.h"
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace control;
static ModeInputs healthy() {
    ModeInputs in; in.metersValid = in.bmsValid = in.dischargeAllowed = in.chargeKnown = in.chargeAllowed = true;
    in.soc = 60; in.householdW = 900; in.correctionW = -50; in.maximumW = 900;
    return in;
}
int main() {
    ModeSettings cfg; OperatingMode parsed;
    assert(parseOperatingMode("pv_follow",parsed) && static_cast<unsigned>(parsed)==3);
    {
        ModeSettings pv;pv.mode=parsed;OperatingModePolicy policy;auto sample=healthy();
        sample.chargerValid=true;sample.chargerW=100;
        assert(policy.update(pv,sample).watts==90);
        sample.chargerW=1000;sample.householdW=600;sample.correctionW=-130;
        assert(policy.update(pv,sample).watts==470);
        sample.householdW=2000;sample.correctionW=0;sample.chargerW=2000;
        assert(policy.update(pv,sample).watts==900);
        sample.householdW=200;assert(policy.update(pv,sample).watts==200);
        sample.soc=1;sample.dischargeAllowed=false;assert(policy.update(pv,sample).watts==200);
        sample.bmsValid=false;assert(policy.update(pv,sample).watts==200);
        assert(!std::strcmp(policy.update(pv,sample).reason,"pv_bms_unverified"));
        for(float bad:{0.0f,-1.0f,NAN,INFINITY,-INFINITY}) {sample.chargerW=bad;assert(policy.update(pv,sample).watts==0);}
        sample.chargerW=500;sample.chargerValid=false;assert(policy.update(pv,sample).watts==0);
        sample.chargerValid=true;sample.genuineFault=true;assert(policy.update(pv,sample).watts==0);
        sample.genuineFault=false;sample.chargerFault=true;assert(policy.update(pv,sample).watts==0);
        sample.chargerFault=false;sample.inverterEnabled=false;assert(policy.update(pv,sample).watts==0);
        sample.inverterEnabled=true;sample.transportEnabled=false;assert(policy.update(pv,sample).watts==0);
        sample.transportEnabled=true;sample.metersValid=false;assert(policy.update(pv,sample).watts==0);
        sample.metersValid=true;sample.chargerW=0;sample.correctionW=100;assert(policy.update(pv,sample).watts==0);
        ModeSettings charge;charge.mode=OperatingMode::ChargePriority;
        for(OperatingMode intermediate:{OperatingMode::HouseholdFollow,parsed}) {
            OperatingModePolicy latch;sample=healthy();sample.soc=100;
            assert(latch.update(charge,sample).watts==850);
            ModeSettings other;other.mode=intermediate;sample.soc=90;latch.update(other,sample);
            sample.soc=96;assert(latch.update(charge,sample).watts==0);
        }
    }
    assert(parseOperatingMode("automatic", parsed));
    assert(!parseOperatingMode("Automatic", parsed));
    assert(!parseOperatingMode(nullptr, parsed));
    assert(!parseOperatingMode("household_follow ", parsed));
    cfg.mode = OperatingMode::HouseholdFollow;
    OperatingModePolicy p; auto in = healthy();
    assert(p.update(cfg, in).watts == 850); // Correction also applies at raw MAX.
    in.householdW = 1000; assert(p.update(cfg, in).watts == 900);
    in.correctionW = -130;
    in.householdW = 600; assert(p.update(cfg,in).watts == 470);
    in.householdW = 1000; assert(p.update(cfg,in).watts == 870);
    in.householdW = 1200; assert(p.update(cfg,in).watts == 900);
    in.householdW = 0; in.correctionW = 50; assert(p.update(cfg, in).watts == 50);
    in.householdW = -100; assert(p.update(cfg, in).watts == 0);
    in.householdW = 900; in.correctionW = INT_MIN; assert(p.update(cfg, in).watts == 0);
    in.correctionW = INT_MAX; assert(p.update(cfg, in).watts == 900);
    in = healthy(); in.metersValid = false; assert(p.update(cfg,in).watts == 0);
    in = healthy(); in.inverterEnabled = false; assert(p.update(cfg,in).watts == 0);
    in = healthy(); in.transportEnabled = false; assert(p.update(cfg,in).watts == 0);
    in = healthy(); in.householdW = NAN; assert(p.update(cfg,in).watts == 0);
    in = healthy(); in.bmsValid = false;
    assert(p.update(cfg,in).watts == 850);
    assert(!std::strcmp(p.update(cfg,in).reason,"manual_bms_unverified"));
    cfg.mode = OperatingMode::Automatic;
    assert(p.update(cfg,in).handled && p.update(cfg,in).watts == 0);
    in = healthy(); assert(!p.update(cfg,in).handled);
    cfg.mode = OperatingMode::HouseholdFollow;
    in.soc = 1; assert(p.update(cfg,in).watts == 0);
    in.soc = 5; assert(p.update(cfg,in).watts == 0);
    in.soc = 10; assert(p.update(cfg,in).watts == 850);
    in.dischargeAllowed = false; assert(p.update(cfg,in).watts == 0);
    in.physicalDischargeOff = true; in.soc = 1; assert(p.update(cfg,in).watts == 850);
    in.genuineFault = true; assert(p.update(cfg,in).watts == 0);
    in = healthy(); cfg.mode = OperatingMode::ChargePriority;
    assert(p.update(cfg,in).watts == 0);
    in.soc = 95; assert(p.update(cfg,in).watts == 0);
    in.soc = 99; assert(p.update(cfg,in).watts == 0);
    cfg.inverterSharePercent = 25; in.chargerValid = true; in.chargerW = 400;
    assert(p.update(cfg,in).watts == 90);
    in.soc = 100; assert(p.update(cfg,in).watts == 850);
    in.soc = 96; assert(p.update(cfg,in).watts == 850);
    in.soc = 94; assert(p.update(cfg,in).watts == 0);
    in.bmsValid = false; assert(p.update(cfg,in).watts == 0);
    in = healthy(); in.chargeAllowed = false; assert(p.update(cfg,in).watts == 850);
    cfg.chargeThreshold = 100; assert(!validModeSettings(cfg));
    cfg.chargeThreshold = 0; assert(!validModeSettings(cfg));
    cfg.chargeThreshold = NAN; assert(!validModeSettings(cfg));
    cfg.chargeThreshold = 95; cfg.inverterSharePercent = 101; assert(!validModeSettings(cfg));
    cfg.inverterSharePercent = -1; assert(!validModeSettings(cfg));
    cfg.inverterSharePercent = 0; assert(validModeSettings(cfg));
    {
        OperatingModePolicy latches;ModeSettings follow;follow.mode=OperatingMode::HouseholdFollow;
        auto sample=healthy();assert(latches.update(follow,sample).watts==850);
        sample.inverterEnabled=false;sample.soc=1;assert(latches.update(follow,sample).watts==0);
        sample.inverterEnabled=true;sample.soc=5;assert(latches.update(follow,sample).watts==0);
        sample.soc=10;assert(latches.update(follow,sample).watts==850);
        follow.mode=OperatingMode::ChargePriority;sample.soc=100;assert(latches.update(follow,sample).watts==850);
        sample.metersValid=false;sample.soc=90;assert(latches.update(follow,sample).watts==0);
        sample.metersValid=true;sample.soc=96;assert(latches.update(follow,sample).watts==0);
    }
    ModeCommand command;
    const char *pvJson="{\"mode\":\"pv_follow\",\"expires_at\":1800000030}";
    assert(parseModeCommand(pvJson,std::strlen(pvJson),1800000000,command));
    assert(static_cast<unsigned>(command.mode)==3);
    assert(!parseModeCommand(pvJson,std::strlen(pvJson),1800000030,command));
    assert(!parseModeCommand("{}",2,1800000000,command));
    const char *json = "{\"mode\":\"household_follow\",\"expires_at\":1800000030}";
    assert(parseModeCommand(json,std::strlen(json),1800000000,command));
    assert(!parseModeCommand(json,std::strlen(json),0,command));
    assert(!parseModeCommand(json,std::strlen(json),1800000030,command));
    assert(!parseModeCommand(json,std::strlen(json),1799990000,command));
    const char *bad[] = {"{\"mode\":\"bogus\",\"expires_at\":1800000030}","{\"mode\":\"automatic\",\"expires_at\":\"1800000030\"}","{\"mode\":\"automatic\",\"expires_at\":1800000030.5}","{\"mode\":\"automatic\",\"expires_at\":1800000030,\"extra\":1}","{\"mode\":\"automatic\",\"mode\":\"household_follow\",\"expires_at\":1800000030}"};
    for(auto x:bad) assert(!parseModeCommand(x,std::strlen(x),1800000000,command));
    ModeTransaction tx; int writes = 0;
    auto persist = [&](const ModeSettings &) { ++writes; return false; };
    ModeSettings requested; requested.mode = OperatingMode::HouseholdFollow;
    tx.request(requested); assert(writes == 0 && tx.confirmed().mode == OperatingMode::Automatic);
    assert(!tx.process(persist)); assert(writes == 1 && tx.confirmed().mode == OperatingMode::Automatic);
    tx.request(requested); tx.request(requested);
    assert(tx.process([&](const ModeSettings &) { ++writes; return true; }));
    assert(writes == 2 && tx.confirmed().mode == OperatingMode::HouseholdFollow);
    tx.request(requested); assert(!tx.process(persist)); assert(writes == 2);
    requested.chargeThreshold = -5; assert(!tx.request(requested));
    requested.chargeThreshold = 95; requested.mode=OperatingMode::ChargePriority;
    tx.request(requested,1800000030);
    assert(!tx.process(persist,1800000030) && writes==2);
    tx.request(requested,1800000030);
    assert(!tx.process(persist,0) && writes==2);
    uint8_t record[kModeProfileBytes] = {}; ModeSettings restored;
    requested.chargeThreshold = 94.5f;
    encodeModeProfile(requested,record);
    assert(decodeModeProfile(record,sizeof(record),restored));
    assert(sameModeSettings(requested,restored));
    assert(!decodeModeProfile(record,sizeof(record)-1,restored));
    for(size_t i=0;i<sizeof(record);++i) {
        record[i]^=0x80; assert(!decodeModeProfile(record,sizeof(record),restored)); record[i]^=0x80;
    }
    // [MOCKED!] The persistence boundary only; encoding and verification are production code.
    uint8_t stored[kModeProfileBytes]={};
    auto write=[&](const uint8_t *data,size_t n) { std::memcpy(stored,data,n); return n; };
    auto read=[&](uint8_t *data,size_t n) { std::memcpy(data,stored,n); return n; };
    unsigned commits=0;
    auto commit=[&](const uint8_t *,size_t n) {++commits;return n;};
    assert(persistModeProfile(requested,write,read,commit));assert(commits==1);
    assert(!persistModeProfile(requested,[](const uint8_t *,size_t) { return size_t(0); },read,commit));
    assert(!persistModeProfile(requested,[](const uint8_t *,size_t n) { return n-1; },read,commit));
    assert(!persistModeProfile(requested,write,[](uint8_t *,size_t) { return size_t(0); },commit));
    assert(!persistModeProfile(requested,write,[](uint8_t *data,size_t n) { std::memset(data,0,n); return n; },commit));
    assert(commits==1);
    assert(!persistModeProfile(requested,write,read,[](const uint8_t *,size_t) {return size_t(0);}));
    ModeTransaction reboot; reboot.initialize(restored); assert(sameModeSettings(reboot.confirmed(),requested));
    ModeSettings invalid; invalid.mode=static_cast<OperatingMode>(99);
    reboot.initialize(invalid); assert(reboot.confirmed().mode == OperatingMode::Automatic);
    for(unsigned legacy=0;legacy<=3;++legacy) {
        requested.mode=static_cast<OperatingMode>(legacy);encodeModeProfile(requested,record);
        assert(record[5]==legacy && decodeModeProfile(record,sizeof(record),restored));
        assert(restored.mode==requested.mode);
    }
    std::puts("Operating modes policy and transaction tests passed");
}
