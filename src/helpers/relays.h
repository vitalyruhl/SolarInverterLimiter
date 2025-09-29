#pragma once
#include <Arduino.h>
#include "settings.h"

namespace Relays {

inline bool isValidPin(int pin){
    return pin >= 0 && pin <= 39; // basic range check for ESP32 GPIO
}

inline void initPins(){
    int fanPin = generalSettings.relayFanPin.get();
    int heaterPin = generalSettings.relayHeaterPin.get();
    bool fanLow = generalSettings.relayFanActiveLow.get();
    bool heaterLow = generalSettings.relayHeaterActiveLow.get();

    if(isValidPin(fanPin)){
        digitalWrite(fanPin, fanLow ? HIGH : LOW); // inactive
        pinMode(fanPin, OUTPUT);
    }
    if(generalSettings.enableHeater.get() && isValidPin(heaterPin)){
        digitalWrite(heaterPin, heaterLow ? HIGH : LOW); // inactive
        pinMode(heaterPin, OUTPUT);
    }
}

inline void setVentilator(bool on){
    int fanPin = generalSettings.relayFanPin.get();
    if(!isValidPin(fanPin)) return;
    bool fanLow = generalSettings.relayFanActiveLow.get();
    if(fanLow){
        digitalWrite(fanPin, on ? LOW : HIGH);
    } else {
        digitalWrite(fanPin, on ? HIGH : LOW);
    }
}

inline bool getVentilator(){
    int fanPin = generalSettings.relayFanPin.get();
    if(!isValidPin(fanPin)) return false;
    bool fanLow = generalSettings.relayFanActiveLow.get();
    int val = digitalRead(fanPin);
    return fanLow ? (val == LOW) : (val == HIGH);
}

inline void setHeater(bool on){
#ifdef RELAY_HEATER_PIN
    int heaterPin = generalSettings.relayHeaterPin.get();
    if(!generalSettings.enableHeater.get() || !isValidPin(heaterPin)) return;
    bool heaterLow = generalSettings.relayHeaterActiveLow.get();
    if(heaterLow){
        digitalWrite(heaterPin, on ? LOW : HIGH);
    } else {
        digitalWrite(heaterPin, on ? HIGH : LOW);
    }
#else
    (void)on;
#endif
}

inline bool getHeater(){
#ifdef RELAY_HEATER_PIN
    int heaterPin = generalSettings.relayHeaterPin.get();
    if(!generalSettings.enableHeater.get() || !isValidPin(heaterPin)) return false;
    bool heaterLow = generalSettings.relayHeaterActiveLow.get();
    int val = digitalRead(heaterPin);
    return heaterLow ? (val == LOW) : (val == HIGH);
#else
    return false;
#endif
}

} // namespace Relays
