#ifndef RS485_MODULE_H
#define RS485_MODULE_H

#include <Arduino.h>
#include "config/config.h" 

struct RS485Settings {
    int baudRate = 4800;
    int rxPin = 16;
    int txPin = 17;
    int dePin = 4;
    bool enableRS485 = true; // set to false to disable RS485 communication
};

struct RS485Packet {
    uint16_t header = 0x2456;
    uint16_t command = 0x0021;
    uint16_t power = 0;
    uint8_t checksum = 0;
};

class RS485Module {
public:
    void Init(RS485Settings &settings);
    void sendToRS485(RS485Settings &settings,RS485Packet &packet, uint16_t demand);
    String reciveFromRS485();
    RS485Packet reciveFromRS485Packet ();

private:

};

#endif // RS485_MODULE_H
