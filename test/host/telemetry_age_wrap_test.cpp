#include "control/TelemetryAge.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

// [MOCKED!] Only CM setting lookup and ESP32 critical sections use host stubs.
// The MQTT text parser and all freshness decisions are the production implementation.
class Receiver {
public:
    explicit Receiver(control::TelemetryAge::Input input) : input_(input) {
        const char *ids[] = {"grid_import_w", "solar_power_w", "electricity_price"};
        uint32_t hash = 2166136261u;
        for (const char *p = ids[input]; *p; ++p) { hash ^= uint8_t(*p); hash *= 16777619u; }
        char key[16];
        std::snprintf(key, sizeof(key), "MQTTRT_%08X", unsigned(hash));
        config_.settings[key] = &topic_;
        std::snprintf(key, sizeof(key), "MQTTRK_%08X", unsigned(hash));
        config_.settings[key] = &path_;
        age_.attach(config_);
    }

    void send(const char *value, uint64_t now) {
        age_.receive("meter", reinterpret_cast<const uint8_t *>(value), unsigned(std::strlen(value)), now);
    }

    bool read(uint64_t now, uint32_t timeout, float &value) {
        return age_.read(input_, now, timeout, value);
    }

private:
    control::TelemetryAge::Input input_;
    ConfigManagerClass config_;
    Config<String> topic_{String("meter")}, path_{String("none")};
    control::TelemetryAge age_;
};

struct InputCase {
    control::TelemetryAge::Input input;
    const char *name;
    uint32_t timeout;
};

static bool unknownAndExactExpiry(const InputCase &test) {
    Receiver receiver(test.input);
    float value = 0;
    bool ok = !receiver.read(1000, test.timeout, value);
    receiver.send("900", 1000);
    ok = ok && receiver.read(1000, test.timeout, value) && value == 900;
    ok = ok && receiver.read(1000 + test.timeout - 1, test.timeout, value) && value == 900;
    ok = ok && !receiver.read(1000 + test.timeout, test.timeout, value);
    ok = ok && !receiver.read(1001 + test.timeout, test.timeout, value);
    std::printf("%s %s unknown, fresh and exact expiry\n", ok ? "PASS" : "FAIL", test.name);
    return ok;
}

static bool ordinaryWrapAndRecovery(const InputCase &test) {
    Receiver receiver(test.input);
    constexpr uint64_t sampleAt = uint64_t(UINT32_MAX) - 15;
    float value = 0;
    receiver.send("900", sampleAt);
    bool ok = receiver.read(sampleAt, test.timeout, value) && value == 900;
    ok = ok && receiver.read((uint64_t(1) << 32) + 32, test.timeout, value) && value == 900; // 48 ms after sample.
    const uint64_t deadline = sampleAt + test.timeout;
    ok = ok && !receiver.read(deadline, test.timeout, value);
    ok = ok && !receiver.read(deadline + 1, test.timeout, value);
    receiver.send("700", deadline + 10);
    ok = ok && receiver.read(deadline + 10, test.timeout, value) && value == 700;
    std::printf("%s %s normal clock wrap and fresh-sample recovery\n", ok ? "PASS" : "FAIL", test.name);
    return ok;
}

static bool readCapturedBeforeNewerSampleDoesNotPoisonIt(const InputCase &test) {
    Receiver receiver(test.input);
    float value = 0;
    receiver.send("900", 1000);
    bool ok = receiver.read(1000, test.timeout, value) && value == 900;
    // A caller captured 19999 before another task delivered the new sample at 20000.
    receiver.send("700", 20000);
    receiver.read(19999, test.timeout, value); // Do not mandate a result for the old caller time.
    ok = ok && receiver.read(20000, test.timeout, value) && value == 700;
    ok = ok && receiver.read(20001, test.timeout, value) && value == 700;
    Receiver acrossWrap(test.input);
    acrossWrap.send("800", uint64_t(1) << 32);
    acrossWrap.read(UINT32_MAX, test.timeout, value); // Captured 1 ms before the wrap.
    ok = ok && acrossWrap.read(uint64_t(1) << 32, test.timeout, value) && value == 800;
    std::printf("%s %s reader time just before new sample does not poison it\n", ok ? "PASS" : "FAIL", test.name);
    return ok;
}

static bool staleSampleNeverReappearsAfterFullCycle(const InputCase &test) {
    Receiver receiver(test.input);
    constexpr uint32_t sampleAt = 1000;
    constexpr uint64_t fullCycle = uint64_t(1) << 32;
    float value = 0;
    receiver.send("900", sampleAt);
    bool becameStale = false, staleBecameFresh = false;
    uint64_t firstResurrection = 0;
    for (uint64_t elapsed = 0; elapsed < fullCycle + test.timeout; elapsed += 1000) {
        const uint64_t now = uint64_t(sampleAt) + elapsed;
        const bool fresh = receiver.read(now, test.timeout, value);
        if (elapsed == test.timeout) becameStale = !fresh;
        if (elapsed >= test.timeout && fresh) {
            staleBecameFresh = true;
            firstResurrection = elapsed;
            break;
        }
    }
    const uint64_t recoveryAt = uint64_t(sampleAt) + fullCycle + test.timeout;
    receiver.send("700", recoveryAt);
    const bool recovered = receiver.read(recoveryAt, test.timeout, value) && value == 700;
    const bool ok = becameStale && !staleBecameFresh && recovered;
    std::printf("%s %s full 2^32-ms cycle: stale_at_deadline=%d resurrected=%d elapsed_ms=%llu recovery=%d\n",
        ok ? "PASS" : "FAIL", test.name, becameStale, staleBecameFresh,
        static_cast<unsigned long long>(firstResurrection), recovered);
    return ok;
}

static bool lateFirstReadAndDifferentTimeouts(const InputCase &test) {
    constexpr uint64_t fullCycle = uint64_t(1) << 32;
    Receiver late(test.input);
    float value = 0;
    late.send("900", 1000);
    const bool expiredWithoutReads = !late.read(1001 + fullCycle, test.timeout, value);

    Receiver differentTimeouts(test.input);
    differentTimeouts.send("700", 1000);
    const bool shortExpired = !differentTimeouts.read(1050, 40, value);
    const bool longerFresh = differentTimeouts.read(1050, 200, value) && value == 700;
    const bool ok = expiredWithoutReads && shortExpired && longerFresh;
    std::printf("%s %s first read after full cycle and independent timeouts: expired=%d short=%d long=%d\n",
                ok ? "PASS" : "FAIL", test.name, expiredWithoutReads, shortExpired, longerFresh);
    return ok;
}

int main() {
    constexpr InputCase cases[] = {
        {control::TelemetryAge::Grid, "grid", 20000},
        {control::TelemetryAge::Inverter, "inverter", 20000},
        {control::TelemetryAge::Price, "price", 1200000},
    };
    unsigned failures = 0;
    for (const InputCase &test : cases) {
        failures += !unknownAndExactExpiry(test);
        failures += !ordinaryWrapAndRecovery(test);
        failures += !readCapturedBeforeNewerSampleDoesNotPoisonIt(test);
        failures += !staleSampleNeverReappearsAfterFullCycle(test);
        failures += !lateFirstReadAndDifferentTimeouts(test);
    }
    std::printf("TelemetryAge wrap tests: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
