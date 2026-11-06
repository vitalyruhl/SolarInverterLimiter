#if __has_include("control/ShadowProfile.h")
#include "control/ShadowProfile.h"
#else
#include <array>
namespace control {
struct ShadowRouting {const char *server="",*root="",*client="",*expectedClient="",*price="",*pv="",*sbt="";int port=0;std::array<const char*,4> topics{},keys{};};
inline bool validShadowRouting(const ShadowRouting&){return true;}
inline bool shadowPublicationAllowed(const char*){return true;}
}
#endif
#include <cassert>
#include <iostream>
// [MOCKED!] Routing snapshots use the production guard without a broker or hardware.
int main() {
 control::ShadowRouting r;
 r.server="192.168.2.3";r.port=1883;r.root="SolarLimiter";r.client=r.expectedClient="ESP32_PRODUCTION";
 assert(!control::validShadowRouting(r)); // Inherited production NVS may never connect.
 r.root="SolarLimiterTest";r.client=r.expectedClient="SolarLimiterTest-3C8A1FA7227C";
 r.price="SolarLimiterTest/Input/PriceSchedule";r.pv="homeassistant/sensor/solar_forecast/shed/schedule";r.sbt="SolarBatteryTelemetry";
 r.topics={"tele/powerMeter/powerMeter/SENSOR","tele/tasmota_1DEE45/SENSOR","SolarLimiter/Input/NegativePrice","SolarLimiter/Input/ElectricityPrice"};
 r.keys={"EMH.Power_curr","ENERGY.Power","none","none"};
 assert(control::validShadowRouting(r));
 auto bad=r;bad.server="192.168.2.220";assert(!control::validShadowRouting(bad));
 bad=r;bad.port=18884;assert(!control::validShadowRouting(bad));
 bad=r;bad.client="ESP32_PRODUCTION";assert(!control::validShadowRouting(bad));
 bad=r;bad.price="homeassistant/sensor/electricity_price/schedule";assert(!control::validShadowRouting(bad));
 bad=r;bad.topics[0]="SolarLimiterTestevil/meter";assert(!control::validShadowRouting(bad));
 bad=r;bad.keys[0]="E320.Power_in";assert(!control::validShadowRouting(bad));
 assert(control::shadowPublicationAllowed("SolarLimiterTest/Planning/State"));
 assert(control::shadowPublicationAllowed("SolarLimiterTest/System-Info/status"));
 for(const char* topic:{"SolarLimiter/SetValue","SolarBatteryTelemetry/bms/set","homeassistant/sensor/test/config",
                        "SolarLimiterTestevil/status","SolarLimiterTest/Control/Mode/set","SolarLimiterTest/bms/command"})
  assert(!control::shadowPublicationAllowed(topic));
 std::cout<<"[MOCKED!] Shadow inherited/reconfigured routing and publication isolation passed\n";
}
