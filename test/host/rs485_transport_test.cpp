#include "RS485Module/RS485Module.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

// [MOCKED!] Arduino serial, GPIO and settings only; RS485Module.cpp is unchanged.
RS485_Settings rs485settings;

#if !defined(SIL_MOCK_TRANSPORT) || !SIL_MOCK_TRANSPORT
static void assertFrame(const std::vector<uint8_t> &actual, const std::array<uint8_t, 8> &expected) {
    assert(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) assert(actual[i] == expected[i]);
}

static void repeatedFramesUseTheCurrentDemand() {
    // Literal fixtures characterize the existing frame, including its checksum byte.
    constexpr std::array<uint8_t, 8> frame900 = {0x24, 0x56, 0x00, 0x21, 0x03, 0x84, 0x80, 0x81};
    constexpr std::array<uint8_t, 8> frame800 = {0x24, 0x56, 0x00, 0x21, 0x03, 0x20, 0x80, 0xE5};

    RS485begin();
    assert(sendToRS485(900));
    assert(sendToRS485(900));
    assert(sendToRS485(800));
    assert(sendToRS485(800));

    assert(Serial.frames.empty());
    assert(Serial2.frames.size() == 4);
    assertFrame(Serial2.frames[0], frame900);
    assertFrame(Serial2.frames[1], frame900);
    assertFrame(Serial2.frames[2], frame800);
    assertFrame(Serial2.frames[3], frame800);
    assert(Serial2.flushCalls == 4);
}

static void disabledTransportDoesNotWrite() {
    const std::size_t writesBefore = Serial2.frames.size();
    const std::size_t pinWritesBefore = pinWrites.size();
    rs485settings.enableRS485.set(false);
    assert(!sendToRS485(900));
    assert(Serial2.frames.size() == writesBefore);
    assert(pinWrites.size() == pinWritesBefore);

    rs485settings.enableRS485.set(true);
    assert(sendToRS485(800));
    constexpr std::array<uint8_t, 8> frame800 = {0x24, 0x56, 0x00, 0x21, 0x03, 0x20, 0x80, 0xE5};
    assert(Serial2.frames.size() == writesBefore + 1);
    assertFrame(Serial2.frames.back(), frame800);
}

static void shortWritesAreReportedAndReturnToReceive() {
    constexpr std::array<uint8_t, 8> frame900 = {0x24, 0x56, 0x00, 0x21, 0x03, 0x84, 0x80, 0x81};
    for (std::size_t accepted = 0; accepted < frame900.size(); ++accepted) {
        Serial2.writeLimit = accepted;
        const unsigned previousFlushes = Serial2.flushCalls;
        const std::size_t previousWrites = Serial2.frames.size();
        const std::size_t previousPinWrites = pinWrites.size();
        assert(!sendToRS485(900));
        assert(Serial2.frames.size() == previousWrites + 1);
        assert(Serial2.frames.back().size() == accepted);
        for (std::size_t i = 0; i < accepted; ++i) assert(Serial2.frames.back()[i] == frame900[i]);
        assert(Serial2.flushCalls == previousFlushes + 1);
        assert(pinWrites.size() == previousPinWrites + 2);
        assert(pinWrites[previousPinWrites].pin == rs485settings.dePin.get());
        assert(pinWrites[previousPinWrites].value == HIGH);
        assert(pinWrites.back().pin == rs485settings.dePin.get());
        assert(pinWrites.back().value == LOW);
    }
    Serial2.writeLimit = 8;
    assert(sendToRS485(900));
    assertFrame(Serial2.frames.back(), frame900);
}
#endif

int main() {
#if defined(SIL_MOCK_TRANSPORT) && SIL_MOCK_TRANSPORT
    RS485begin();
    assert(!sendToRS485(900));
    assert(!sendToRS485(800));
    assert(Serial.beginCalls == 0 && Serial2.beginCalls == 0);
    assert(Serial.frames.empty() && Serial2.frames.empty());
    assert(pinModes.empty() && pinWrites.empty());
    std::cout << "[MOCKED!] Hardware mock blocks RS485 initialization and writes\n";
#else
    repeatedFramesUseTheCurrentDemand();
    disabledTransportDoesNotWrite();
    shortWritesAreReportedAndReturnToReceive();
    std::cout << "[MOCKED!] Production RS485 frames and local 0-8 byte write results passed\n";
#endif
}
