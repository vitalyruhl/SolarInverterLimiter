#ifndef RS485_MODULE_H
#define RS485_MODULE_H

#pragma once

#include <Arduino.h>
#include "settings.h"

class Config;


struct RS485Packet// 19.04.2025 viru - that will not working???
{
    uint16_t header = 0x2456;
    uint16_t command = 0x0021;
    uint16_t power = 0;
    uint8_t checksum = 0;
};

class RS485Module
{
public:
    RS485Module(Config *settings);
    ~RS485Module();
    void begin();
    void sendToRS485(uint16_t demand);
    void sendToRS485Packet(uint16_t demand);
    String reciveFromRS485();
    RS485Packet reciveFromRS485Packet();

    // -- Serial data --
    // 19.04.2025 viru - packet will not working??? --> get this from internet
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
    HardwareSerial *RS485serial = &Serial; // Zeiger auf die serielle Schnittstelle // default to Serial, can be changed to Serial2 if needed
    Config *_config = nullptr;
    RS485Packet packet; // RS485 packet structure
};

#endif // RS485_MODULE_H
