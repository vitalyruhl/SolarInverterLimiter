#pragma once
#include <array>
#include <cstring>
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
#if !defined(SIL_MOCK_TRANSPORT) || !SIL_MOCK_TRANSPORT
#error "Planner shadow requires hard mocked hardware transport"
#endif
#endif
namespace control {
#if defined(SIL_PLANNER_SHADOW) && SIL_PLANNER_SHADOW
inline constexpr bool kPlannerShadowProfile=true;
#else
inline constexpr bool kPlannerShadowProfile=false;
#endif
struct ShadowRouting {
 const char *server="",*root="",*client="",*expectedClient="",*price="",*pv="",*sbt="";
 int port=0;
 std::array<const char*,4> topics{},keys{};
};
inline bool shadowEqual(const char* a,const char* b){return a&&b&&!std::strcmp(a,b);}
inline bool validShadowRouting(const ShadowRouting& r) {
 if(!shadowEqual(r.server,"192.168.2.3")||r.port!=1883||!shadowEqual(r.root,"SolarLimiterTest")||
    !r.expectedClient||std::strncmp(r.expectedClient,"SolarLimiterTest-",17)||
    std::strlen(r.expectedClient)!=29||!shadowEqual(r.client,r.expectedClient)||
    !shadowEqual(r.price,"SolarLimiterTest/Input/PriceSchedule")||
    !shadowEqual(r.pv,"homeassistant/sensor/solar_forecast/shed/schedule")||
    !shadowEqual(r.sbt,"SolarBatteryTelemetry"))return false;
 constexpr const char* topics[]={"tele/powerMeter/powerMeter/SENSOR","tele/tasmota_1DEE45/SENSOR",
                                "SolarLimiter/Input/NegativePrice","SolarLimiter/Input/ElectricityPrice"};
 constexpr const char* keys[]={"EMH.Power_curr","ENERGY.Power","none","none"};
 for(unsigned i=0;i<4;++i)if(!shadowEqual(r.topics[i],topics[i])||!shadowEqual(r.keys[i],keys[i]))return false;
 return true;
}
enum class ShadowField { Server, Port, Root, Client, Price, Pv, Sbt, Topic0, Topic1, Topic2, Topic3, Key0, Key1, Key2, Key3, Enable };
inline bool shadowWriteAllowed(ShadowField field,const char* value,const char* client,bool routingValid) {
 const char* expected[]={"192.168.2.3","1883","SolarLimiterTest",client,
 "SolarLimiterTest/Input/PriceSchedule","homeassistant/sensor/solar_forecast/shed/schedule","SolarBatteryTelemetry",
 "tele/powerMeter/powerMeter/SENSOR","tele/tasmota_1DEE45/SENSOR","SolarLimiter/Input/NegativePrice","SolarLimiter/Input/ElectricityPrice",
 "EMH.Power_curr","ENERGY.Power","none","none"};
 if(field==ShadowField::Enable)return shadowEqual(value,"false")||shadowEqual(value,"0")||
   (routingValid&&(shadowEqual(value,"true")||shadowEqual(value,"1")));
 return shadowEqual(value,expected[unsigned(field)]);
}
inline bool shadowPublicationAllowed(const char* topic) {
 if(!topic||std::strncmp(topic,"SolarLimiterTest/",17))return false;
 const size_t n=std::strlen(topic);
 return n>17 && !(n>=4&&!std::strcmp(topic+n-4,"/set")) &&
        !std::strstr(topic,"/command")&&!std::strstr(topic,"/bms/");
}
}
