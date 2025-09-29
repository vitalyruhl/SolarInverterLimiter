#pragma once
#include <Arduino.h>
#include "settings.h"

namespace Relays {

inline void initPins(){
    // Drive lines to a known INACTIVE level BEFORE switching to OUTPUT to avoid relay pulse
#if VENTILATOR_RELAY_ACTIVE_LOW
    digitalWrite(RELAY_VENTILATOR_PIN, HIGH); // inactive
#else
    digitalWrite(RELAY_VENTILATOR_PIN, LOW);  // inactive
#endif
    pinMode(RELAY_VENTILATOR_PIN, OUTPUT);

#if defined(RELAY_HEATER_PIN)
#if HEATER_RELAY_ACTIVE_LOW
    digitalWrite(RELAY_HEATER_PIN, HIGH); // inactive
#else
    digitalWrite(RELAY_HEATER_PIN, LOW);  // inactive
#endif
    pinMode(RELAY_HEATER_PIN, OUTPUT);
#endif
}

inline void setVentilator(bool on){
#if VENTILATOR_RELAY_ACTIVE_LOW
    digitalWrite(RELAY_VENTILATOR_PIN, on ? LOW : HIGH);
#else
    digitalWrite(RELAY_VENTILATOR_PIN, on ? HIGH : LOW);
#endif
}

inline bool getVentilator(){
#if VENTILATOR_RELAY_ACTIVE_LOW
    return digitalRead(RELAY_VENTILATOR_PIN) == LOW;
#else
    return digitalRead(RELAY_VENTILATOR_PIN) == HIGH;
#endif
}

#if defined(RELAY_HEATER_PIN)
inline void setHeater(bool on){
#if HEATER_RELAY_ACTIVE_LOW
    digitalWrite(RELAY_HEATER_PIN, on ? LOW : HIGH);
#else
    digitalWrite(RELAY_HEATER_PIN, on ? HIGH : LOW);
#endif
}
inline bool getHeater(){
#if HEATER_RELAY_ACTIVE_LOW
    return digitalRead(RELAY_HEATER_PIN) == LOW;
#else
    return digitalRead(RELAY_HEATER_PIN) == HIGH;
#endif
}
#else
inline void setHeater(bool){ /* heater not compiled in */ }
inline bool getHeater(){ return false; }
#endif

} // namespace Relays
