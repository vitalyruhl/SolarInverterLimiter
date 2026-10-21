#include "sbt/SbtReceiver.h"
#include <cassert>
#include <cstring>
#include <iostream>

static void cacheBoundaries() {
  for (unsigned seconds : {1u, 180u, 9999u}) {
    sbt::Receiver r;
    r.setTimeoutSeconds(seconds);
    auto send = [&](const char* topic, const char* value, uint32_t now) {
      return r.receive(topic, value, std::strlen(value), now);
    };
    assert(send("status", "online", 100));
    assert(send("bms/availability", "available", 100));
    assert(send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})", 100));
    assert(send("bms/current_a", "-2.5", 101));
    assert(send("bms/state_of_charge_pct", "50", 101));
    assert(send("bms/power_w", "100", 101));
    assert(send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":10})", 200));
    assert(r.valid("bms/current_a", 201));
    assert(!send("bms/current_a", "garbage", 210));
    assert(!send("bms/health", "{bad", 211));
    assert(r.numeric("bms/current_a") == -2.5);
    assert(send("bms/current_a", "null", 212));
    assert(!r.valid("bms/current_a", 212));
    assert(r.withinGrace(0, 190 + seconds * 1000 - 1));
    assert(!r.old(0, 190 + seconds * 1000 - 1));
    assert(!r.withinGrace(0, 190 + seconds * 1000));
    assert(r.old(0, 190 + seconds * 1000));
    JsonDocument doc;
    r.writeRuntime(doc.to<JsonObject>(), 190 + seconds * 1000);
    assert(doc["bms_current_a"].as<double>() == -2.5);
    assert(std::strcmp(doc["bms_age"].as<const char*>(), "Old") == 0);
    r.disconnect();
    assert(r.withinGrace(0, 220)); // Holds prior permission without renewing it.
    assert(send("status", "online", 220));
    assert(send("bms/availability", "available", 220));
    assert(send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":0})", 220));
    assert(!r.valid("bms/current_a", 220));
    assert(send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":3,"sample_age_ms":0})", 230));
    assert(!r.valid("bms/current_a", 230)); // Prior-session scalars are display only.
    assert(send("bms/current_a", "-2.5", 231));
    assert(r.valid("bms/current_a", 231));
    assert(send("bms/availability", "unavailable", 30231));
    assert(send("bms/health", R"({"available":false,"reason":"stale","sample_sequence":3,"sample_age_ms":30001})", 30231));
    assert(r.withinGrace(0, 30231) == (seconds > 30));
    assert(!r.valid("bms/current_a", 30231));
    assert(send("bms/alarm_bits", "2", 30232));
    assert(r.alarmRestricted());
    assert(!send("bms/alarm_bits", "invalid", 30233));
    assert(r.alarmRestricted());
    r.disconnect();
    assert(r.alarmRestricted());
  }
  sbt::Receiver r;
  r.setTimeoutSeconds(0);
  assert(r.configuredTimeoutMs() == 180000);
  r.setTimeoutSeconds(10000);
  assert(r.configuredTimeoutMs() == 180000);
  r.setTimeoutSeconds(180);
  auto send = [&](const char* topic, const char* value, uint32_t now) {
    return r.receive(topic, value, std::strlen(value), now);
  };
  send("status", "online", 0xfffffff0);
  send("bms/availability", "available", 0xfffffff0);
  send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})", 0xfffffff0);
  send("bms/current_a", "3", 0xfffffff0);
  send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":0})", 0x10);
  assert(r.valid("bms/current_a", 0x20));
  send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":3,"sample_age_ms":0})", 60016);
  assert(r.valid("bms/current_a", 60017)); // Unchanged scalar, advancing source.
  send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":3,"sample_age_ms":0})", 120016);
  assert(r.old(0, 240016)); // Duplicate cannot rejuvenate age.
  assert(!send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})", 240017));
  assert(!send("bms/health", R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":0})", 240018));
  assert(r.old(0, 240018));
  assert(!r.alarmRestricted()); // Silence does not create an alarm.
  assert(send("bms/alarm_bits", "1", 240020));
  assert(!r.alarmRestricted()); // A charge-only fault must not block permitted discharge.
  assert(r.chargeRestricted());
}
static bool receive(sbt::Receiver& r,const char* topic,const char* value,uint32_t now){return r.receive(topic,value,std::strlen(value),now);}
static void health(sbt::Receiver& r,unsigned source,unsigned sequence,uint32_t now){
 char text[160];std::snprintf(text,sizeof(text),"{\"available\":true,\"reason\":\"ok\",\"sample_sequence\":%u,\"sample_age_ms\":0}",sequence);
 assert(receive(r,source?"victron/health":"bms/health",text,now));
}
static void essential(sbt::Receiver& r,unsigned source,uint32_t now){
 assert(receive(r,source?"victron/battery_voltage_v":"bms/state_of_charge_pct",source?"50":"50",now));
 assert(receive(r,source?"victron/charge_current_a":"bms/power_w",source?"10":"100",now));
}
static void qualified(sbt::Receiver& r,unsigned source){
 assert(receive(r,"status","online",100));assert(receive(r,source?"victron/availability":"bms/availability","available",100));
 health(r,source,1000,100);essential(r,source,101);health(r,source,1001,200);
 assert(r.withinGrace(source,200));
}
static void invalidEssentialKeepsCacheAndOriginalDeadline(){
 struct Invalid {const char* topic;const char* value;unsigned source;double previous;};
 for(const auto& bad : {Invalid{"bms/state_of_charge_pct","-1",0,50},
     Invalid{"bms/state_of_charge_pct","101",0,50},Invalid{"victron/battery_voltage_v","0",1,50},
     Invalid{"victron/battery_voltage_v","-1",1,50},Invalid{"victron/charge_current_a","-1",1,10}}){
  sbt::Receiver r;qualified(r,bad.source);
  assert(!receive(r,bad.topic,bad.value,250));
  assert(r.cachedNumeric(bad.topic)==bad.previous&&!r.hasSample(bad.topic));
  health(r,bad.source,1002,90000); // Valid heartbeat cannot approve the rejected essential scalar.
  assert(!r.valid(bad.topic,90000));
  assert(r.withinGrace(bad.source,180199)&&!r.withinGrace(bad.source,180200));
  assert(r.old(bad.source,180200));
  const auto h=r.sourceHealth(bad.source);assert(h.heldReceived==200&&h.displayReceived==200);
 }
 // These are the established controller boundaries, not new hardware limits.
 sbt::Receiver r;qualified(r,0);qualified(r,1);
 for(const char* soc:{"0","100"})assert(receive(r,"bms/state_of_charge_pct",soc,250));
 assert(receive(r,"victron/battery_voltage_v","0.01",250));
 assert(receive(r,"victron/charge_current_a","0",250));
}
static void firstSessionFencesRetainedHealthHighWater(){
 sbt::Receiver r;qualified(r,0);qualified(r,1);
 assert(!receive(r,"bms/control",R"({"enabled":false,"ready":false,"pending":false,"session":"","reason":"disabled"})",80000));
 assert(r.sourceHealth(0).seeded&&r.hasSample("bms/state_of_charge_pct"));
 assert(receive(r,"bms/control",R"({"enabled":false,"ready":false,"pending":false,"session":"0123456789abcdef0123456789abcdef","reason":"disabled"})",90000));
 for(unsigned source:{0u,1u}){
  assert(!r.sourceHealth(source).seeded);
  assert(!r.hasSample(source?"victron/battery_voltage_v":"bms/state_of_charge_pct"));
  health(r,source,1,90001);assert(!r.fresh(source,90001));
  health(r,source,2,90002);assert(r.fresh(source,90002));
  assert(!r.hasSample(source?"victron/battery_voltage_v":"bms/state_of_charge_pct"));
  assert(!r.withinGrace(source,180200)&&r.old(source,180200));
  essential(r,source,90003);assert(r.valid(source?"victron/battery_voltage_v":"bms/state_of_charge_pct",90003));
 }
 // An unchanged session must not keep fencing every retained/control heartbeat.
 assert(receive(r,"bms/control",R"({"enabled":false,"ready":false,"pending":false,"session":"0123456789abcdef0123456789abcdef","reason":"disabled"})",90004));
 assert(r.hasSample("bms/state_of_charge_pct"));
}
static void explicitMissingNtcIsUnavailableWithoutOldBms(){
 for(const char* topic:{"bms/temperature_1_c","bms/temperature_2_c"}){
  sbt::Receiver r;qualified(r,0);assert(receive(r,topic,"21.5",210));
  assert(receive(r,topic,"null",250));assert(!r.hasSample(topic)&&std::isnan(r.cachedNumeric(topic)));
  health(r,0,1002,90000);health(r,0,1003,170000);
  assert(!r.old(0,180200)&&r.fresh(0,180200));
  JsonDocument doc;r.writeRuntime(doc.to<JsonObject>(),180200);
  assert(doc[std::strcmp(topic,"bms/temperature_1_c")?"bms_temperature_2_c":"bms_temperature_1_c"].isNull());
  assert(std::strcmp(doc["bms_age"].as<const char*>(),"")==0);
 }
}
int main() {
  invalidEssentialKeepsCacheAndOriginalDeadline();
  firstSessionFencesRetainedHealthHighWater();
  explicitMissingNtcIsUnavailableWithoutOldBms();
  cacheBoundaries();
  {
    sbt::Receiver ntc;
    const char* a="17.5"; const char* b="21.5";
    assert(ntc.receive("bms/temperature_1_c",a,4,100));
    assert(ntc.receive("bms/temperature_2_c",b,4,100));
    assert(ntc.numeric("bms/temperature_1_c")==17.5);
    assert(ntc.numeric("bms/temperature_2_c")==21.5);
    assert(ntc.receive("bms/temperature_1_c",b,4,101));
    assert(ntc.receive("bms/temperature_2_c",a,4,101));
    assert(ntc.numeric("bms/temperature_1_c")==21.5);
    assert(ntc.numeric("bms/temperature_2_c")==17.5);
  }
  sbt::Receiver r;
  auto send = [&](const char *topic, const char *value, uint32_t now = 100) {
    return r.receive(topic, value, std::strlen(value), now);
  };
  assert(send("status", "online"));
  assert(send("bms/availability", "available"));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":10})"));
  assert(send("bms/current_a", "-2.5"));
  assert(!r.fresh(0, 100));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":10})",
      5100));
  assert(r.fresh(0, 5100));
  assert(r.numeric("bms/current_a") == -2.5);
  assert(r.valid("bms/current_a", 5100));
  assert(r.valid("bms/current_a", 10000)); // Deadband silence is not staleness.
  assert(!send("bms/current_a", "garbage"));
  assert(r.valid("bms/current_a", 10000));
  assert(send("bms/current_a", "null"));
  assert(!r.valid("bms/current_a", 5100));
  assert(!send("bms/cell_count", "14.5"));
  assert(!send("bms/charge_enabled", "1"));
  assert(!send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":null})"));
  assert(r.fresh(0, 5100));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":3,"sample_age_ms":0})",
      6000));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":4,"sample_age_ms":0})",
      7000));
  assert(r.fresh(0, 23000));
  assert(send("bms/availability", "unavailable", 23000));
  assert(!r.fresh(0, 23000));
  r.reset();
  assert(!r.valid("bms/current_a", 25000));
  assert(!r.fresh(0, 25000));
  assert(!send("bms/current_a/set", "0"));
  assert(!send("bms/temperature_min_c", "NaN"));
  assert(send("bms/protection", "charge_undertemperature, unknown_15"));
  assert(send(
      "bms/control",
      R"({"enabled":false,"ready":false,"pending":false,"session":"0123456789abcdef0123456789abcdef","reason":"disabled"})"));
  assert(send("bms/control/availability", "disabled"));
  assert(send("bms/control/availability", "unavailable"));
  assert(send("bms/control/availability", "available"));
  assert(!send("bms/control/availability", "online"));
  assert(!send("bms/availability", "online"));
  assert(send("victron/availability", "available"));
  assert(send("victron/availability", "unavailable"));
  const char *result =
      R"({"request_id":"snapshot","phase":"rejected","reason":"disabled","charge_enabled":null,"discharge_enabled":null,"observed_current_a":null})";
  assert(send("bms/control/result", result));
  const char *health =
      R"({"available":true,"reason":"snapshot_health","sample_sequence":1,"sample_age_ms":0})";
  assert(send("bms/health", health));
  JsonDocument snapshot;
  r.writeRuntime(snapshot.to<JsonObject>(), 100);
  sbt::Receiver copied = r;
  r.reset();
  assert(
      !std::strcmp(snapshot["control_reason"].as<const char *>(), "disabled"));
  assert(!std::strcmp(snapshot["control_result"].as<const char *>(), result));
  assert(!std::strcmp(snapshot["bms_reason"].as<const char *>(),
                      "snapshot_health"));
  r = copied;
  char out[256];
  assert(r.previewDisable(true, "sil-test_1", 2000000000, out, sizeof(out)));
  assert(std::strstr(out, "\"state\":false"));
  assert(std::strstr(out, "2000000030"));
  assert(!r.previewDisable(true, "bad id", 2000000000, out, sizeof(out)));
  assert(r.commandsLocked());
  r.reset();
  assert(!r.previewDisable(false, "id", 2000000000, out, sizeof(out)));
  for (const auto &c : sbt::channels) {
    const char *payload =
        c.kind == sbt::Kind::Boolean ? "true"
        : c.kind == sbt::Kind::Text
            ? (std::strcmp(c.topic, "bms/protection") == 0 ? "none"
                                                           : "\"bulk\"")
        : c.kind == sbt::Kind::Unsupported ? "null"
                                           : "14";
    assert(send(c.topic, payload));
  }
  r.reset();
  assert(send("status", "online"));
  assert(send("bms/availability", "available"));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":10,"sample_age_ms":0})",
      100));
  assert(send("bms/current_a", "-2"));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":11,"sample_age_ms":0})",
      200));
  assert(r.valid("bms/current_a", 200));
  char large[1025];
  std::memset(large, '1', sizeof(large));
  assert(!r.receive("bms/current_a", large, sizeof(large), 201));
  assert(r.valid("bms/current_a", 201));
  assert(send("bms/current_a", "3", 202));
  const char embeddedNull[] = {'4', '\0', '5'};
  assert(!r.receive("bms/current_a", embeddedNull, sizeof(embeddedNull), 203));
  assert(r.valid("bms/current_a", 203));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":11,"sample_age_ms":0})",
      10000));
  assert(!r.fresh(
      0, 180200)); // Replayed identical sequence cannot renew freshness.
  assert(!send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})",
      16001));
  assert(r.valid("bms/current_a", 16001));
  assert(send("status", "offline", 17000));
  assert(!r.fresh(0, 17000));
  std::cout << "SBT receiver boundary tests passed\n";
}
