#include "RS485Module/RS485Module.h"
#include "logging/logging.h"



RS485Module::RS485Module(Config *settings) : _config(settings)
{
    // do nothing, constructor initializes config
}

RS485Module::~RS485Module() = default;


void RS485Module::begin()
{
    logv("logv... starting RS485Module:Init()");
    logv("rs485settings.enableRS485 = %d", _config->rs485settings.enableRS485);

    if (_config->rs485settings.enableRS485 == false)
    {
        log("RS485Module: RS485 communication disabled.");
        return;
    }

    serialpacket[0] = byte0;
    serialpacket[1] = byte1;
    serialpacket[2] = byte2;
    serialpacket[3] = byte3;
    serialpacket[4] = byte4;
    serialpacket[5] = byte5;
    serialpacket[6] = byte6;
    serialpacket[7] = byte7;

    
    if (_config->rs485settings.useExtraSerial) {
        RS485serial = &Serial2;
        RS485serial->begin(_config->rs485settings.baudRate, SERIAL_8N1, _config->rs485settings.rxPin, _config->rs485settings.txPin);
        pinMode(_config->rs485settings.dePin, OUTPUT);
        digitalWrite(_config->rs485settings.dePin, LOW);
    } else {
        RS485serial = &Serial; // Optional fallback
    }
}

void RS485Module::sendToRS485Packet(uint16_t demand)
{
    //todo:Fix it --> dont sen the header, there is an logical error!!! 
    //21:00:FF:02:64:00 instead of 
    //24:56:00:21:02:FE:80:09
    packet.power = demand;
    logv("");
    logv("-------------------------");
    logv("RS485Module::sendToRS485: %d", demand);

    if (_config->rs485settings.enableRS485 == false)
    {
        log("RS485Module: RS485 communication disabled.");
        return;
    }

    uint16_t sum = (packet.header >> 8) + (packet.header & 0xFF) +
                   (packet.command >> 8) + (packet.command & 0xFF) +
                   (packet.power >> 8) + (packet.power & 0xFF);

    packet.checksum = 256 - (sum & 0xFF);

    digitalWrite(_config->rs485settings.dePin, HIGH); // Aktivate send mode
    delayMicroseconds(100);
    RS485serial->write((uint8_t *)&packet, sizeof(packet));
    RS485serial->flush(); // wait for send to finish
    delayMicroseconds(100);
    digitalWrite(_config->rs485settings.dePin, LOW); // aktivate recive mode

    logv("-------------------------");
    // logv("RS485Module: SetValue(demand(%d))[limited]:%d", demand, packet.power);
    log("--> RS485: Headder:%04X, Command:%04X, Power:%04X, Checksum:%02X", packet.header, packet.command, packet.power, packet.checksum);
    logv("");
}

void RS485Module::sendToRS485(uint16_t demand)
{

    logv("");
    logv("-------------------------");
    logv("RS485Module::sendToRS485: %d", demand);

    if (_config->rs485settings.enableRS485 == false)
    {
        log("RS485Module: RS485 communication disabled.");
        return;
    }

    // -- Compute serial packet to inverter (just the 3 bytes that change) --
    byte4 = int(demand / 256); // (2 byte watts as short integer xaxb)
    if (byte4 < 0 or byte4 > 256)
    {
        byte4 = 0;
    }
    byte5 = int(demand) - (byte4 * 256); // (2 byte watts as short integer xaxb)
    if (byte5 < 0 or byte5 > 256)
    {
        byte5 = 0;
    }
    byte7 = (264 - byte4 - byte5); // checksum calculation
    if (byte7 > 256)
    {
        byte7 = 8;
    }

    serialpacket[4] = byte4;
    serialpacket[5] = byte5;
    serialpacket[7] = byte7;

    digitalWrite(_config->rs485settings.dePin, HIGH); // Aktivate send mode
    delayMicroseconds(100);
    RS485serial->write(serialpacket, 8);
    RS485serial->flush(); // wait for send to finish
    delayMicroseconds(100);
    digitalWrite(_config->rs485settings.dePin, LOW); // aktivate recive mode

    logv("--> RS485: Headder:%02X,%02X,%02X, Command:%02X, Power:%02X,%02X Byte6:%02X Checksum:%02X",
         serialpacket[0], serialpacket[1], serialpacket[2],
         serialpacket[3],
         serialpacket[4], serialpacket[5],
         serialpacket[6],
         serialpacket[7]);
    logv("-------------------------");
    logv("");
}

String RS485Module::reciveFromRS485()
{
    String recivedData;
    while (RS485serial->available())
    {
        uint8_t byte = RS485serial->read();
        char hexByte[4];
        snprintf(hexByte, sizeof(hexByte), "%02X ", byte);
        recivedData += hexByte;
    }
    // logv("RS485Module: Recived HEX: %s", recivedData.c_str());
    return recivedData;
}

RS485Packet RS485Module::reciveFromRS485Packet()
{
    RS485Packet incoming;
    if (RS485serial->available() >= sizeof(RS485Packet))
    {
        RS485serial->readBytes((char *)&incoming, sizeof(RS485Packet));
        // logv("Received Packet - Header: %04X, Command: %04X, Power: %04X, Checksum: %02X",
        //      incoming.header, incoming.command, incoming.power, incoming.checksum);
    }
    return incoming;
}
