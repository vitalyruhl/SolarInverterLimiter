#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// [MOCKED!] Host-only Arduino hardware boundary; no serial port is opened.
using byte = uint8_t;
using String = std::string;

constexpr int SERIAL_8N1 = 0;
constexpr int OUTPUT = 1;
constexpr int LOW = 0;
constexpr int HIGH = 1;

struct PinOperation {
    int pin;
    int value;
};

inline std::vector<PinOperation> pinModes;
inline std::vector<PinOperation> pinWrites;

inline void pinMode(int pin, int mode) { pinModes.push_back({pin, mode}); }
inline void digitalWrite(int pin, int value) { pinWrites.push_back({pin, value}); }
inline void delayMicroseconds(unsigned int) {}

class HardwareSerial {
public:
    unsigned beginCalls = 0;
    unsigned flushCalls = 0;
    bool driverInstalled = false;
    std::size_t writeLimit = std::numeric_limits<std::size_t>::max();
    std::vector<std::vector<uint8_t>> frames;

    void begin(int, int, int = -1, int = -1) { ++beginCalls; driverInstalled = true; }
    explicit operator bool() const { return driverInstalled; }
    std::size_t write(const uint8_t *data, std::size_t count) {
        writeDriver(data, count);
        return count;
    }
    int writeDriver(const uint8_t *data, std::size_t count) {
        if (!driverInstalled) return -1;
        const std::size_t accepted = std::min(count, writeLimit);
        frames.emplace_back(data, data + accepted);
        return static_cast<int>(accepted);
    }
    void flush() { ++flushCalls; }
    int available() { return 0; }
    int read() { return -1; }
    std::size_t readBytes(char *, std::size_t) { return 0; }
};

inline HardwareSerial Serial;
inline HardwareSerial Serial2;
