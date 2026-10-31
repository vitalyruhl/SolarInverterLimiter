#include "control/ModeSettingsRuntime.h"
#include <cstdio>
using namespace control;
int main() {
    ConfigManagerClass cm;ModeSettingsRuntime runtime;runtime.attachTo(cm);runtime.begin();
    assert(runtime.confirmed().mode==OperatingMode::Automatic && Preferences::writes==0);
    assert(cm.displayedMode()=="automatic");
    assert(cm.edit(0,"\"household_follow\""));
    assert(runtime.confirmed().mode==OperatingMode::Automatic && cm.displayedMode()=="automatic" && Preferences::writes==0);
    assert(runtime.loop(1800000000));
    assert(runtime.confirmed().mode==OperatingMode::HouseholdFollow && cm.displayedMode()=="household_follow" && Preferences::writes==2);
    assert(cm.edit(0,"\"household_follow\""));assert(!runtime.loop(1800000000));assert(Preferences::writes==2);
    assert(cm.edit(1,"94.5"));assert(cm.edit(2,"20"));assert(runtime.loop(1800000000));assert(Preferences::writes==4);
    assert(runtime.confirmed().chargeThreshold==94.5f && runtime.confirmed().inverterSharePercent==20);
    assert(!cm.edit(0,"\"bogus\""));assert(!cm.edit(1,"100"));assert(!cm.edit(1,"0"));assert(!cm.edit(2,"101"));
    assert(!runtime.loop(1800000000));assert(Preferences::writes==4);
    Preferences::failWrite=true;assert(cm.edit(0,"\"charge_priority\""));assert(!runtime.loop(1800000000));
    assert(runtime.confirmed().mode==OperatingMode::HouseholdFollow && cm.displayedMode()=="household_follow");
    assert(!std::strcmp(runtime.storageReason(),"save_failed"));Preferences::failWrite=false;
    Preferences::failRead=true;assert(cm.edit(0,"\"charge_priority\""));assert(!runtime.loop(1800000000));
    assert(cm.displayedMode()=="household_follow");Preferences::failRead=false;
    ConfigManagerClass rebootCm;ModeSettingsRuntime reboot;reboot.attachTo(rebootCm);reboot.begin();
    assert(reboot.confirmed().mode==OperatingMode::HouseholdFollow); // Failed readback must preserve the old boot mode.
    for(bool *failure:{&Preferences::failPromote,&Preferences::corruptRead,&Preferences::shortWrite,&Preferences::failOpen}) {
        *failure=true;assert(cm.edit(0,"\"charge_priority\""));assert(!runtime.loop(1800000000));*failure=false;
        ConfigManagerClass checkCm;ModeSettingsRuntime check;check.attachTo(checkCm);check.begin();
        assert(check.confirmed().mode==OperatingMode::HouseholdFollow && cm.displayedMode()=="household_follow");
    }
    const auto bytes=Preferences::stored;
    for(size_t n:{size_t(0),size_t(5),size_t(15),size_t(17)}) {
        Preferences::stored=bytes;Preferences::stored.resize(n);
        ConfigManagerClass brokenCm;ModeSettingsRuntime broken;broken.attachTo(brokenCm);broken.begin();
        assert(broken.confirmed().mode==OperatingMode::Automatic);
    }
    Preferences::stored=bytes;Preferences::stored[4]=99;
    ConfigManagerClass oldCm;ModeSettingsRuntime old;old.attachTo(oldCm);old.begin();assert(old.confirmed().mode==OperatingMode::Automatic);
    Preferences::stored=bytes;
    const unsigned writes=Preferences::writes;
    reboot.queueMode(OperatingMode::ChargePriority,1800000030);
    assert(!reboot.loop(1800000030) && Preferences::writes==writes);
    reboot.queueMode(OperatingMode::ChargePriority,1800000030);
    assert(!reboot.loop(0) && Preferences::writes==writes);
    reboot.queueMode(OperatingMode::ChargePriority,1800000030);reboot.discardPendingMode();
    assert(!reboot.loop(1800000000));
    assert(cm.edit(0,"\"charge_priority\""));runtime.discardPendingMode();
    assert(runtime.loop(1800000000));assert(runtime.confirmed().mode==OperatingMode::ChargePriority);
    oldCm.settings[0]->setDefault();assert(!old.loop(1800000000));
    assert(cm.edit(0,"\"pv_follow\""));assert(runtime.loop(1800000000));
    assert(cm.displayedMode()=="pv_follow");
    ConfigManagerClass pvBootCm;ModeSettingsRuntime pvBoot;pvBoot.attachTo(pvBootCm);pvBoot.begin();
    assert(static_cast<unsigned>(pvBoot.confirmed().mode)==3);
    std::puts("Deferred CM mode settings and persistence boundary tests passed");
}
