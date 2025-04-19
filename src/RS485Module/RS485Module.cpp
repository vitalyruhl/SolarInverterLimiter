#include "RS485Module/RS485Module.h"
#include "logging/logging.h"

void RS485Module::Init(RS485Settings &settings) {
    logv("logv... starting RS485Module:Init()");
    logv("rs485settings.enableRS485 = %d", settings.enableRS485);

    if (settings.enableRS485 == false) {
        log("RS485Module: RS485 communication disabled.");
        return;
    }
    Serial2.begin(settings.baudRate, SERIAL_8N1, settings.rxPin, settings.txPin);
    pinMode(settings.dePin, OUTPUT);
    digitalWrite(settings.dePin, LOW); // recivemod (DE = LOW)
}

void RS485Module::sendToRS485(RS485Settings &settings, RS485Packet &packet, uint16_t demand) {
    packet.power = demand;
    logv("");
    logv("-------------------------");
    logv("RS485Module::sendToRS485: %d", demand);

     if (settings.enableRS485 == false) {
        log("RS485Module: RS485 communication disabled.");
        return;
    }

    uint16_t sum = (packet.header >> 8) + (packet.header & 0xFF) +
                   (packet.command >> 8) + (packet.command & 0xFF) +
                   (packet.power >> 8) + (packet.power & 0xFF);

    packet.checksum = 256 - (sum & 0xFF);

    digitalWrite(settings.dePin, HIGH); // Aktivate send mode
    delayMicroseconds(100);
    Serial2.write((uint8_t *)&packet, sizeof(packet));
    Serial2.flush(); // wait for send to finish
    delayMicroseconds(100);
    digitalWrite(settings.dePin, LOW); // aktivate recive mode

    logv("-------------------------");
    // logv("RS485Module: SetValue(demand(%d))[limited]:%d", demand, packet.power);
    log("--> RS485: Headder:%04X, Command:%04X, Power:%04X, Checksum:%02X", packet.header, packet.command, packet.power, packet.checksum);
    logv("");
}

String RS485Module::reciveFromRS485 () {
    String recivedData;
    while (Serial2.available()) {
        uint8_t byte = Serial2.read();
        char hexByte[4];
        snprintf(hexByte, sizeof(hexByte), "%02X ", byte);
        recivedData += hexByte;
    }
    // logv("RS485Module: Recived HEX: %s", recivedData.c_str());
    return recivedData;
}


RS485Packet RS485Module::reciveFromRS485Packet () {
    RS485Packet incoming;
    if (Serial2.available() >= sizeof(RS485Packet)) {
        Serial2.readBytes((char*)&incoming, sizeof(RS485Packet));
        // logv("Received Packet - Header: %04X, Command: %04X, Power: %04X, Checksum: %02X",
        //      incoming.header, incoming.command, incoming.power, incoming.checksum);
    }
    return incoming;
}





