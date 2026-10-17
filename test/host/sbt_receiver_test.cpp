#include "sbt/SbtReceiver.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
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
  assert(!r.valid("bms/current_a", 10000));
  assert(send("bms/current_a", "null"));
  assert(!r.valid("bms/current_a", 5100));
  assert(!send("bms/cell_count", "14.5"));
  assert(!send("bms/charge_enabled", "1"));
  assert(!send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":null})"));
  assert(!r.fresh(0, 5100));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":3,"sample_age_ms":0})",
      6000));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":4,"sample_age_ms":0})",
      7000));
  assert(!r.fresh(0, 23000));
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
  assert(!r.valid("bms/current_a", 201));
  assert(send("bms/current_a", "3", 202));
  const char embeddedNull[] = {'4', '\0', '5'};
  assert(!r.receive("bms/current_a", embeddedNull, sizeof(embeddedNull), 203));
  assert(!r.valid("bms/current_a", 203));
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":11,"sample_age_ms":0})",
      10000));
  assert(!r.fresh(
      0, 16000)); // Replayed identical sequence cannot renew freshness.
  assert(send(
      "bms/health",
      R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})",
      16001));
  assert(!r.valid("bms/current_a", 16001));
  assert(send("status", "offline", 17000));
  assert(!r.fresh(0, 17000));
  std::cout << "SBT receiver boundary tests passed\n";
}
