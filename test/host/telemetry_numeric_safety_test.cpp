#include "control/TelemetryAge.h"
#include <cassert>
#include <climits>
#include <cmath>
#include <cstring>
#include <iostream>

// [MOCKED!] Synthetic MQTT messages run through the real TelemetryAge parser.
struct Receiver {
    ConfigManagerClass cfg;
    Config<String> topic{String("meter")}, path{String("none")};
    control::TelemetryAge age;
    explicit Receiver(control::TelemetryAge::Input input = control::TelemetryAge::Grid) {
        const char *ids[] = {"grid_import_w", "solar_power_w", "electricity_price"};
        uint32_t hash = 2166136261u;
        for (const char *p = ids[input]; *p; ++p) { hash ^= uint8_t(*p); hash *= 16777619u; }
        char key[16];
        snprintf(key, sizeof(key), "MQTTRT_%08X", unsigned(hash));
        cfg.settings[key] = &topic;
        snprintf(key, sizeof(key), "MQTTRK_%08X", unsigned(hash));
        cfg.settings[key] = &path;
        age.attach(cfg);
    }
    bool send(const char *text, float &value, unsigned now = 100) {
        age.receive("meter", reinterpret_cast<const uint8_t *>(text), unsigned(std::strlen(text)), now);
        return age.read(control::TelemetryAge::Grid, now, 500, value);
    }
};

static void floatStorageCannotValidateAnUnrepresentableInteger() {
    Receiver receiver;
    float value = 0;
    assert(receiver.send("2147483520", value) && value == 2147483520.0f);
    assert(!receiver.send("2147483647", value));
    receiver.path.value = "w";
    assert(!receiver.send("{\"w\":2147483647}", value));
}

static void invalidMessagesCannotRefreshOrRestoreFreshness() {
    Receiver receiver;
    float value = 0;
    assert(receiver.send("-123.9", value, 100) && value == -123);
    for (const char *text : {"NaN", "Infinity", "1e309", "2147483648", "-2147483649",
                             "1junk", "--1", "1e", "0x10", "999999999999999999999999e-20"}) {
        assert(!receiver.send(text, value, 200));
        assert(!receiver.age.read(control::TelemetryAge::Grid, 201, 500, value));
        assert(receiver.send("-42", value, 300) && value == -42);
    }
    assert(!receiver.age.read(control::TelemetryAge::Grid, 800, 500, value));
}

static void ordinaryMappingAndSignedBoundsArePreserved() {
    Receiver receiver;
    float value = 0;
    for (const auto &sample : {std::pair<const char *, float>{"-2147483648", -2147483648.0f},
                              {"123.9", 123}, {"-123.9", -123}, {"1e3", 1},
                              {".5", 0}, {" +12.75 ", 12}, {"-0", 0}}) {
        assert(receiver.send(sample.first, value) && value == sample.second);
    }
    receiver.path.value = "w";
    assert(receiver.send("{\"w\":1e3}", value) && value == 1000);
    assert(receiver.send("{\"w\":-12.75}", value) && value == -12);
    assert(receiver.send("{\"w\":\"1e3\"}", value) && value == 1);
    assert(receiver.send("{\"w\":-2147483648}", value) && value == -2147483648.0f);
    for (const char *text : {"{\"w\":true}", "{\"w\":null}", "{\"w\":2147483648}",
                             "{\"w\":-2147483649}", "{\"w\":1e309}", "{\"w\":\"0x10\"}"}) {
        assert(!receiver.send(text, value));
    }
}

static void priceKeepsFiniteFractionsAndRejectsFloatOverflow() {
    Receiver receiver(control::TelemetryAge::Price);
    for (const char *text : {"-.123456", "1e-6", "0", "-99999.5"}) {
        receiver.age.receive("meter", reinterpret_cast<const uint8_t *>(text), unsigned(std::strlen(text)), 100);
        float value = 0;
        assert(receiver.age.read(control::TelemetryAge::Price, 100, 500, value));
        assert(std::isfinite(value));
    }
    for (const char *text : {"1e39", "NaN", "Inf", "1.2oops", "0x10"}) {
        receiver.age.receive("meter", reinterpret_cast<const uint8_t *>(text), unsigned(std::strlen(text)), 200);
        float value = 0;
        assert(!receiver.age.read(control::TelemetryAge::Price, 200, 500, value));
    }
}

static void embeddedNullCannotHideMalformedNumberSuffix() {
    Receiver receiver;
    const uint8_t packet[] = {'1', '2', '3', 0, 'b', 'a', 'd'};
    receiver.age.receive("meter", packet, sizeof(packet), 100);
    float value = 0;
    assert(!receiver.age.read(control::TelemetryAge::Grid, 100, 500, value));
    receiver.path.value = "w";
    assert(!receiver.send("{\"w\":\"123\\u0000bad\"}", value));
}

static void checkedConversionsAndArithmeticRespectIntegerBounds() {
    int integer = 17;
    for (float value : {std::nanf(""), INFINITY, -INFINITY, 2147483648.0f,
                        std::nextafter(-2147483648.0f, -INFINITY)}) {
        assert(!control::checkedFloatToInt(value, integer));
        assert(integer == 17);
    }
    assert(control::checkedFloatToInt(2147483520.0f, integer) && integer == 2147483520);
    assert(control::checkedFloatToInt(-2147483648.0f, integer) && integer == INT_MIN);
    assert(control::checkedFloatToInt(-123.75f, integer) && integer == -123);
    float stored = 17;
    assert(!control::checkedIntegerToFloat(2147483647.0, stored) && stored == 17);
    assert(!control::checkedIntegerToFloat(-2147483649.0, stored) && stored == 17);
    assert(!control::checkedIntegerToFloat(INFINITY, stored) && stored == 17);
    assert(control::checkedIntegerToFloat(-2147483648.0, stored) && stored == -2147483648.0f);
    assert(control::saturatingAdd(INT_MAX, 1) == INT_MAX);
    assert(control::saturatingAdd(INT_MIN, -1) == INT_MIN);
    assert(control::saturatingAdd(INT_MAX, INT_MAX) == INT_MAX);
    assert(control::saturatingAdd(INT_MIN, INT_MIN) == INT_MIN);
    assert(control::saturatingAdd(INT_MAX, INT_MIN) == -1);
    assert(control::saturatingAdd(600, -120) == 480);
    assert(control::nonnegativeExportMagnitude(INT_MIN) == INT_MAX);
    assert(control::nonnegativeExportMagnitude(INT_MAX) == 0);
    assert(control::nonnegativeExportMagnitude(-120) == 120);
    assert(control::nonnegativeExportMagnitude(0) == 0);
}

int main() {
    floatStorageCannotValidateAnUnrepresentableInteger();
    invalidMessagesCannotRefreshOrRestoreFreshness();
    ordinaryMappingAndSignedBoundsArePreserved();
    priceKeepsFiniteFractionsAndRejectsFloatOverflow();
    embeddedNullCannotHideMalformedNumberSuffix();
    checkedConversionsAndArithmeticRespectIntegerBounds();
    std::cout << "[I] telemetry numeric safety tests ok [MOCKED!]\n";
}
