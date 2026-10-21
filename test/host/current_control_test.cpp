#include "control/CurrentControl.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace control;
static void recoveryRestartsDynamicsAtHeldCommand() {
  for (bool economic : {true, false}) {
    PreparedSettings cfg;cfg.valid=true;
    CurrentInputs in;in.bmsValid=in.chargerValid=in.metersValid=in.priceValid=true;
    in.soc=50;in.chargerW=2000;in.batteryW=1000;in.price=economic?.4f:.1f;
    CurrentControl c;
    for(unsigned i=0;i<3;++i){in.bmsSequence=in.chargerSequence=i+1;c.update(cfg,in,true,true,900,900,100+i*3000);}
    assert(c.update(cfg,in,true,true,900,900,6200).ceilingW==900);
    in.bmsValid=false;in.sourcesWithinGrace=true;
    auto gap=c.update(cfg,in,true,true,900,100,12000);
    assert(!std::strcmp(gap.reason,"source_grace")&&gap.ceilingW==100&&gap.reserveAllowed);
    in.bmsValid=true;
    for(unsigned i=0;i<2;++i){in.bmsSequence=in.chargerSequence=4+i;
      auto recovery=c.update(cfg,in,true,true,900,100,22000+i*3000);
      assert(!std::strcmp(recovery.reason,"source_recovery")&&recovery.ceilingW==100);}
    in.bmsSequence=in.chargerSequence=6;
    auto qualified=c.update(cfg,in,true,true,900,100,28000);
    assert(qualified.ceilingW==100); // No outage dt/integral or obsolete 900-W ceiling.
    in.bmsSequence=in.chargerSequence=7;
    assert(c.update(cfg,in,true,true,900,100,29000).ceilingW==150);
    in.bmsSequence=in.chargerSequence=8;
    assert(c.update(cfg,in,true,true,900,150,30000).ceilingW==200);
  }
}
int main() {
  recoveryRestartsDynamicsAtHeldCommand();
  PreparedSettings cfg; cfg.valid = true;
  CurrentInputs in; in.metersValid = true;
  for (int idle : {500, 0, 2000, 100}) {
    cfg.idleOutputW = idle;
    for (unsigned missing : {1u, 2u, 3u}) {
      CurrentControl c;
      in.bmsValid = !(missing & 1); in.chargerValid = !(missing & 2);
      in.soc = 50; in.chargerW = 300;
      auto d = c.update(cfg, in, true, true, 900, 200, 100);
      assert(d.direct && d.ceilingW == (idle > 900 ? 900 : idle));
      assert(!std::strcmp(d.reason, "source_loss"));
    }
  }
  cfg.idleOutputW = 500;
  CurrentControl c;
  in = CurrentInputs{}; in.metersValid = true; in.bmsValid = true; in.chargerValid = true;
  in.soc = 50; in.chargerW = 700; in.batteryW = 50;
  in.priceValid = true; in.price = .4;
  for (unsigned i=0;i<3;++i) {
    in.bmsSequence = in.chargerSequence = i+1;
    c.update(cfg,in,true,true,900,200,100+i*3000);
  }
  auto approved = c.update(cfg,in,true,true,900,200,6200);
  in.bmsValid = false; in.sourcesWithinGrace = true;
  auto hold = c.update(cfg,in,true,true,900,200,6300);
  assert(!hold.direct && hold.ceilingW <= approved.ceilingW);
  assert(!std::strcmp(hold.reason, "source_grace"));
  // Repeated stale input cannot raise the prior approved ceiling.
  assert(c.update(cfg,in,true,true,900,200,6400).ceilingW <= hold.ceilingW);
  in.bmsValid = true; in.bmsSequence = in.chargerSequence = 4;
  auto recovering = c.update(cfg,in,true,true,900,200,6450);
  assert(!std::strcmp(recovering.reason, "source_recovery"));
  in.bmsValid = false;
  in.alarmRestricted = true;
  assert(c.update(cfg,in,true,true,900,200,6500).ceilingW == 0);
  in.alarmRestricted = false;
  in.sourcesWithinGrace = false;
  in.price = -.01f;
  assert(c.update(cfg,in,true,true,900,200,6600).ceilingW == 0);
  in.price = .4f;
  assert(c.update(cfg,in,false,true,900,200,6700).ceilingW == 900);
  cfg.inverterEnabled = false;
  assert(c.update(cfg,in,false,true,900,200,6800).ceilingW == 0);
  cfg.inverterEnabled = true;
  assert(c.update(cfg,in,true,false,900,200,6900).ceilingW == 0);
  in.metersValid = false;
  assert(c.update(cfg,in,true,true,900,200,7000).ceilingW == 0);
  in.metersValid = true; in.bmsValid = true; in.soc = 0;
  assert(c.update(cfg,in,true,true,900,200,7100).ceilingW == 0);
  in.bmsValid = false;
  assert(c.update(cfg,in,true,true,900,200,7200).ceilingW == 0);
  std::cout << "Current control Idle, grace and priority tests passed\n";
}
