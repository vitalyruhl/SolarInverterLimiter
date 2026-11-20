#pragma once

#include "Arduino.h"

// [MOCKED!] ESP-IDF write result is independent of the Arduino wrapper's return value.
enum uart_port_t { UART_NUM_0, UART_NUM_2 = 2 };

inline int uart_write_bytes(uart_port_t port, const void *source, std::size_t count) {
    auto &serial = port == UART_NUM_2 ? Serial2 : Serial;
    return serial.writeDriver(static_cast<const uint8_t *>(source), count);
}
