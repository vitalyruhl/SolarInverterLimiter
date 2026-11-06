#include <cstdint>
#include <cstdio>
#include <string>
#include <cassert>
#include <iostream>
using ulong=unsigned long;
static uint32_t shadowRxCallbacks=0;
[[maybe_unused]] static uint32_t millis(){return 123456;}
static struct {unsigned calls=0;std::string text;template<class... A>void printf(const char* format,A... args){char line[512];snprintf(line,sizeof(line),format,args...);text=line;++calls;}} Serial;
static struct {struct Stats {uint32_t txFailures=0,txTimeouts=0,txOverflows=0,txBackpressure=0,txPartialWrites=0;} stats;[[maybe_unused]] const Stats& getBoundedIoStats(){return stats;}} mqtt;
#include "shadow_stage_trace.h"
#ifndef SHADOW_STAGE_TRACE_PRESENT
static void shadowTraceStage(const char*,uint32_t,uint32_t){}
#endif
int main(){
 // [MOCKED!] Only stats and UART sink are replaced; actual diagnostic function is extracted.
 shadowTraceStage("mqtt",0,0);assert(Serial.calls==0);
 mqtt.stats.txBackpressure=25;shadowTraceStage("mqtt",0,0);assert(Serial.calls==0);
 mqtt.stats.txFailures=1;mqtt.stats.txOverflows=1;shadowRxCallbacks=2;
 shadowTraceStage("mqtt",0,0);assert(Serial.calls==1);
 assert(Serial.text.find("stage=mqtt")!=std::string::npos&&Serial.text.find("rx_delta=2")!=std::string::npos);
 assert(Serial.text.find("before=0 after=1")!=std::string::npos);
 shadowTraceStage("config",1,2);assert(Serial.calls==1);
 mqtt.stats.txFailures=2;mqtt.stats.txTimeouts=1;shadowTraceStage("wifi",1,2);
 assert(Serial.calls==2&&Serial.text.find("stage=wifi")!=std::string::npos);
 assert(mqtt.stats.txFailures==2&&mqtt.stats.txBackpressure==25); // observation does not clear counters
 std::cout<<"[MOCKED!] Shadow stage failure delta and RX callback correlation passed\n";
}
