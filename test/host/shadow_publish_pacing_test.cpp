#include "control/ShadowProfile.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <map>
#include <vector>
#include <iostream>
using String=std::string;
using ulong=unsigned long;
static struct {unsigned calls=0;template<class... A>void printf(const char*,A...){++calls;}} Serial [[maybe_unused]];
[[maybe_unused]] static uint32_t shadowRxCallbacks=0;
static uint32_t nowMs=1000;
[[maybe_unused]] static uint32_t millis(){return nowMs;}
[[maybe_unused]] static bool enforceShadowIsolation(){return true;}
static struct {
 struct Stats {uint32_t txFailures=0,txTimeouts=0,txOverflows=0,txBackpressure=0,txPartialWrites=0;} stats;
 const Stats& getBoundedIoStats(){return stats;}
 uint32_t reconnects=1;uint32_t getReconnectCount(){return reconnects;}
 unsigned calls=0,builders=0;bool result=true;uint32_t lazyInterval=3000;
 std::map<std::string,uint32_t> lazyAt;
 std::vector<std::pair<uint32_t,std::string>> accepted;
 bool publishRaw(const char* topic,const char*,bool){++calls;if(result)accepted.emplace_back(nowMs,topic);else ++stats.txFailures;return result;}
 template<class F>bool publishExtraTopicLazy(const char* id,const char* topic,F factory,bool){
  ++calls;if(!result||(lazyAt.count(id)&&uint32_t(nowMs-lazyAt[id])<lazyInterval))return false;
  factory();++builders;lazyAt[id]=nowMs;accepted.emplace_back(nowMs,topic);return true;
 }
} mqtt;
#include "shadow_publish_wrappers.h"
int main(){
 // [MOCKED!] CM enqueue outcomes, per-topic 3s stamps and time; actual app wrappers/progress are compiled.
 constexpr auto root="SolarLimiterTest/SetValue";
#if defined(SIL_SHADOW_STAGE_TRACE) && SIL_SHADOW_STAGE_TRACE
 (void)shadowTraceStage;
#endif
 assert(publishCheckedRaw(root,"1",false));auto calls=mqtt.calls;bool deferred=false;
 assert(!publishCheckedRaw(root,"1",false,&deferred)&&deferred&&mqtt.calls==calls);
 nowMs+=249;assert(!publishCheckedExtra("a",root,[]{return "1";},false));assert(mqtt.builders==0);
 ++nowMs;assert(publishCheckedExtra("a",root,[]{return "1";},false));
 nowMs+=250;assert(!publishCheckedExtra("a",root,[]{return "1";},false)); // interval no-op consumes no slot
 assert(publishCheckedRaw(root,"1",false));
 nowMs+=250;mqtt.result=false;deferred=true;calls=mqtt.calls;
 assert(!publishCheckedRaw(root,"1",false,&deferred)&&!deferred&&mqtt.calls==calls+1);
 #if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
 assert(Serial.calls==1);
#else
 assert(Serial.calls==0);
#endif
 mqtt.result=true;assert(publishCheckedRaw(root,"1",false)); // genuine failure consumes no slot
 ShadowModeProgress mode;
 unsigned completed=0;
 for(unsigned i=0;i<20;++i){
  nowMs+=250;bool dirty=true; // final stale gate sets dirty every control tick
  if(dirty&&publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","automatic","stale_hold")){dirty=false;++completed;}
 }
 assert(completed==10); // status cannot starve behind repeated dirty state frames
 nowMs+=250;assert(!publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","automatic","old"));
 nowMs+=250;assert(!publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","pv_follow","new"));
 nowMs+=250;assert(publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","pv_follow","new"));
 // An accepted state can be dropped with the FIFO on disconnect; same payload needs a fresh pair.
 nowMs+=250;assert(!publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","automatic","reconnect"));
 ++mqtt.reconnects;nowMs+=250;
 assert(!publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","automatic","reconnect"));
 nowMs+=250;assert(publishShadowModePair(mode,"SolarLimiterTest/Control/Mode","SolarLimiterTest/Control/ModeStatus","automatic","reconnect"));
 // Fixed-order callers become offset through actual per-topic stamps; all periodic topics advance.
 std::map<std::string,unsigned> counts;
 for(unsigned tick=0;tick<80;++tick){
  nowMs+=250;
  for(unsigned i=0;i<10;++i){auto id="topic"+std::to_string(i);auto topic="SolarLimiterTest/"+id;
   if(publishCheckedExtra(id.c_str(),topic.c_str(),[]{return "1";},false))++counts[id];}
 }
 for(unsigned i=0;i<10;++i)assert(counts["topic"+std::to_string(i)]>=5);
 // The actual rotating helper must also serve every topic when intervals are 0 or 1 second.
 for(uint32_t interval:{0u,1000u}){
  mqtt.lazyInterval=interval;mqtt.lazyAt.clear();counts.clear();unsigned next=0;
  for(unsigned tick=0;tick<80;++tick){nowMs+=250;publishAppExtrasFair(next,10,[&](unsigned item){
   auto id="fair"+std::to_string(item);auto topic="SolarLimiterTest/"+id;
   if(!publishCheckedExtra(id.c_str(),topic.c_str(),[]{return "1";},false))return false;
   ++counts[id];return true;
  });}
  for(unsigned i=0;i<10;++i)assert(counts["fair"+std::to_string(i)]>=7);
 }
 for(size_t i=1;i<mqtt.accepted.size();++i)assert(uint32_t(mqtt.accepted[i].first-mqtt.accepted[i-1].first)>=250);

 std::cout<<"[MOCKED!] App publication rate, defer/no-op, mode dirty fairness and normal compatibility passed\n";
}
