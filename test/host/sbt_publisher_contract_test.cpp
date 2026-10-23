#include "sbt/SbtReceiver.h"
#include "telemetry/BmsCommandEnvelope.h"
#include "telemetry/StatusPublisher.h"
#include "telemetry/TelemetryPublisher.h"
#include <cassert>
#include <cstring>
#include <iostream>

// [MOCKED!] Compile against the actual sibling publisher; no MQTT or devices.
struct ContractWire : telemetry::Transport {
  sbt::Receiver receiver;
  bool connected() const override { return true; }
  void disconnect() override {}
  void configure(const char *) override {}
  bool publish(const char *topic, const char *payload, bool) override {
    const char *suffix = topic + std::strlen("SolarBatteryTelemetry/");
    if (std::strcmp(suffix, "capabilities"))
      assert(receiver.receive(suffix, payload, std::strlen(payload), 100));
    return true;
  }
};
int main() {
  ContractWire wire;
  telemetry::Publisher publisher;
  publisher.setSourceAvailable(telemetry::Source::Bms, true);
  publisher.setSourceAvailable(telemetry::Source::Victron, true);
  for (size_t i = 0; i < telemetry::channelCount; ++i) {
    const auto type = telemetry::channels[i].type;
    publisher.setValue(
        i, {true, type == telemetry::FieldType::Boolean ? 1.0 : 2.0});
  }
  publisher.onConnected();
  for (unsigned i = 0; i < 20; ++i)
    publisher.tick(100, wire);
  for (const char *source : {"bms/health", "victron/health"}) {
    const char *first =
        R"({"available":true,"reason":"ok","sample_sequence":1,"sample_age_ms":0})";
    const char *second =
        R"({"available":true,"reason":"ok","sample_sequence":2,"sample_age_ms":0})";
    assert(wire.receiver.receive(source, first, std::strlen(first), 100));
    assert(wire.receiver.receive(source, second, std::strlen(second), 200));
  }
  unsigned valid = 0;
  for (const auto &channel : telemetry::channels) {
    const bool supported = channel.support != telemetry::Support::Unsupported &&
                           channel.support != telemetry::Support::DerivedLater;
    assert(wire.receiver.valid(channel.suffix, 200) == supported);
    valid += supported;
  }
  assert(valid == 26);
  assert(wire.receiver.commandsLocked());
  publisher.setControl(true, true);
  for (unsigned i = 0; i < 20; ++i)
    publisher.tick(100, wire);
  JsonDocument document;
  wire.receiver.writeRuntime(document.to<JsonObject>(), 200);
  assert(document["remote_control_available"].as<bool>());
  publisher.setControl(false, false);
  for (unsigned i = 0; i < 20; ++i)
    publisher.tick(100, wire);
  wire.receiver.writeRuntime(document.to<JsonObject>(), 200);
  assert(!document["remote_control_available"].as<bool>());
  publisher.setSourceAvailable(telemetry::Source::Bms, false);
  for (unsigned i = 0; i < 20; ++i)
    publisher.tick(100, wire);
  assert(!wire.receiver.fresh(0, 200));
  assert(wire.receiver.fresh(1, 200));
  std::cout << "SBT publisher-to-SIL contract passed: 26 supported values, 3 "
               "unavailable, exact availability states\n";
}
