#include "control/PvForecast.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
using control::PvForecast;
template<class P> static auto content(const P& p,int)->decltype(p.contentRevision()){return p.contentRevision();}
template<class P> static uint32_t content(const P& p,long){return p.revision();}
static constexpr uint32_t t=1792454400; // UTC quarter-hour, independent of local DST.
static std::string packet(uint32_t revision=t, unsigned count=48, unsigned part=0,
                          int watts=1000, uint32_t session=t-900) {
  JsonDocument d;
  d["v"]=2; d["s"]=session; d["r"]=revision; d["g"]=revision;
  d["t"]=t; d["d"]=900; d["n"]=count; d["k"]=part;
  d["e"]=t+count*900; d["f"]=1000; d["c"]=0; d["a"]=0;
  d["q"]="uncalibrated"; d["o"]="forecast_solar";
  auto p=d["p"].to<JsonArray>(); auto x=d["x"].to<JsonArray>();
  for(unsigned i=part*24;i<count && i<(part+1)*24;++i){p.add(watts);x.add(2);}
  std::string out; serializeJson(d,out); return out;
}
static const char* send(PvForecast& p,const std::string& s,uint32_t now=t,uint32_t ms=100){
  const char* result=p.receive(reinterpret_cast<const uint8_t*>(s.data()),unsigned(s.size()),now,ms); std::cerr << result << " size=" << s.size() << "\n"; return result;
}
static void contentRevisionIgnoresPollingTime(){
 assert(PvForecast::stateBytes()==720); // Includes the new counter and alignment before the fixed JSON arena.
 PvForecast p;assert(!std::strcmp(send(p,packet()),"pending"));assert(!std::strcmp(send(p,packet(t,48,1)),"accepted"));
 const auto original=content(p,0);
 assert(!std::strcmp(send(p,packet(t+300),t+300),"pending"));assert(content(p,0)==original&&p.revision()==t);
 assert(!std::strcmp(send(p,packet(t+300,48,1),t+300),"accepted"));assert(p.revision()==t+300&&content(p,0)==original);
 assert(!std::strcmp(send(p,packet(t+301,48,0,1001),t+301),"pending"));assert(content(p,0)==original);
 assert(!std::strcmp(send(p,packet(t+301,48,1,1001),t+301),"accepted"));const auto changed=content(p,0);assert(changed!=original);
 assert(!std::strcmp(send(p,packet(t+302,24,0,1001),t+302),"accepted"));const auto coverage=content(p,0);assert(coverage!=changed);
 JsonDocument trust;deserializeJson(trust,packet(t+303,24,0,1001));trust["e"]=t+24*900-1;
 std::string value;serializeJson(trust,value);assert(!std::strcmp(send(p,value,t+303),"accepted"));assert(content(p,0)!=coverage);
 const auto expiry=content(p,0);assert(!std::strcmp(send(p,packet(t+304,24,0,1001,t-899),t+304),"accepted"));assert(content(p,0)!=expiry);
 const auto session=content(p,0);deserializeJson(trust,packet(t+305,24,0,1001,t-899));trust["f"]=800;trust["c"]=14;trust["q"]="calibrated";
 value.clear();serializeJson(trust,value);assert(!std::strcmp(send(p,value,t+305),"accepted"));assert(content(p,0)!=session);
 const auto calibrated=content(p,0);trust["r"]=t+306;trust["g"]=t+306;trust["a"]=1;
 value.clear();serializeJson(trust,value);assert(!std::strcmp(send(p,value,t+306),"accepted"));assert(content(p,0)!=calibrated);
 const auto quality=content(p,0);trust["r"]=t+307;trust["g"]=t+307;trust["x"][0]=0;
 value.clear();serializeJson(trust,value);assert(!std::strcmp(send(p,value,t+307),"accepted"));assert(content(p,0)!=quality);
 const auto rawQuality=content(p,0);trust["r"]=t+308;trust["g"]=t+308;trust["x"][0]=1;trust["p"][0]=nullptr;
 value.clear();serializeJson(trust,value);assert(!std::strcmp(send(p,value,t+308),"accepted"));assert(content(p,0)!=rawQuality);
 const auto missing=content(p,0);p.clear();assert(content(p,0)!=missing);
 std::cout<<"[MOCKED!] PV content identity ignores r/g-only polls, retaining exact values/coverage/trust/session checks\n";
}
int main(){
  contentRevisionIgnoresPollingTime();
  PvForecast p;
  assert(!p.valid(t));
  assert(!std::strcmp(send(p,packet(t,48,1)),"pending"));
  assert(!p.valid(t)); // Shuffled chunks activate only together.
  assert(!std::strcmp(send(p,packet()),"accepted"));
  assert(p.valid(t) && p.revision()==t);
  float energy=0;
  assert(p.energyWh(t,t+3600,energy) && energy==1000);
  assert(p.energyWh(t+450,t+900,energy) && energy==125);
  assert(p.valid(t+4000)); // Publisher silence does not expire a complete plan.
  assert(!std::strcmp(send(p,packet(),t+4000),"duplicate"));
  assert(!p.valid(t+48*900));
  assert(std::strcmp(send(p,packet(t-1)),"accepted"));
  assert(std::strcmp(send(p,packet(t+1,48,0,1000,t-901)),"accepted"));
  assert(std::strcmp(send(p,packet(t+1,145)),"accepted"));
  assert(!std::strcmp(send(p,"{"),"packet_json"));
  assert(!std::strcmp(send(p,std::string(769,'x')),"packet_size"));
  assert(!std::strcmp(send(p,packet(),0),"clock_invalid"));
  // Conflicting duplicate must poison its pending revision, preserving active.
  assert(!std::strcmp(send(p,packet(t+1)),"pending"));
  assert(!std::strcmp(send(p,packet(t+1,48,0,999)),"packet_conflict"));
  assert(!std::strcmp(send(p,packet(t+1,48,1)),"packet_conflict"));
  assert(p.revision()==t);
  // Per-slot missing stays missing; it never becomes invented zero energy.
  JsonDocument missing; deserializeJson(missing,packet(t+2,24));
  missing["p"][0]=nullptr; missing["x"][0]=1;
  std::string payload; serializeJson(missing,payload);
  assert(!std::strcmp(send(p,payload),"accepted"));
  assert(!p.energyWh(t,t+900,energy));
  assert(p.energyWh(t+900,t+1800,energy) && energy==250);
  // Calibration applies once to raw forecast and must be supported by eligible days.
  JsonDocument calibrated; deserializeJson(calibrated,packet(t+3,24));
  calibrated["q"]="calibrated"; calibrated["c"]=14; calibrated["f"]=800;
  payload.clear(); serializeJson(calibrated,payload);
  assert(!std::strcmp(send(p,payload),"accepted"));
  assert(p.energyWh(t,t+3600,energy) && energy==800);
  calibrated["r"]=t+4;calibrated["g"]=t+4;calibrated["c"]=13;
  payload.clear();serializeJson(calibrated,payload);
  assert(std::strcmp(send(p,payload),"accepted"));
  assert(p.revision()==t+3);
  // A fresh receiver may restore a still valid retained snapshot; expiry is immutable.
  PvForecast restored;
  assert(!std::strcmp(send(restored,packet(t,24),t+4000),"accepted"));
  assert(restored.valid(t+4000) && !restored.valid(t+24*900));
  std::cout<<"PV atomic chunks, replay/expiry, missing quality and calibration passed; state="
           <<PvForecast::stateBytes()<<" bytes, object="<<sizeof(PvForecast)<<" bytes\n";
}
