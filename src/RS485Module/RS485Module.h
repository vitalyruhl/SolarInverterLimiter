#ifndef RS485_MODULE_H
#define RS485_MODULE_H

#include <Arduino.h>
#include "config/config.h"

#pragma once

struct RS485Settings
{
    int baudRate = 4800;
    int rxPin = 16;
    int txPin = 17;
    int dePin = 4;
    bool enableRS485 = true; // set to false to disable RS485 communication
};

// 19.04.2025 viru - that will not working???
struct RS485Packet
{
    uint16_t header = 0x2456;
    uint16_t command = 0x0021;
    uint16_t power = 0;
    uint8_t checksum = 0;
};

class RS485Module
{
public:
    void Init(RS485Settings &settings);
    void sendToRS485(RS485Settings &settings, RS485Packet &packet, uint16_t demand);
    void sendToRS485(RS485Settings &settings, uint16_t demand); // 19.04.2025 viru - test, because below not working
    String reciveFromRS485();
    RS485Packet reciveFromRS485Packet();

    // -- Serial data --
    // 19.04.2025 viru - that will not working??? --> get it from internet
    byte byte0 = 36;
    byte byte1 = 86;
    byte byte2 = 0;
    byte byte3 = 33;
    byte byte4 = 0; //(2 byte watts as short integer xaxb)
    byte byte5 = 0; //(2 byte watts as short integer xaxb)
    byte byte6 = 128;
    byte byte7 = 8; // checksum

private:
    byte serialpacket[8];
};

#endif // RS485_MODULE_H
