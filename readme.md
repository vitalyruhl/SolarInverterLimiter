# ESP32 - RS485 Solarinverter Limiter

This project is a simple ESP32-based solar inverter limiter that can be used to limit the power output of a solar inverter to a specific value. The project uses an ESP32 microcontroller, a current sensor, and a relay module to control the power output of the inverter. The project is designed to be easy to use and can be configured to work with a variety of solar inverters with RS485 communication.

This is an Project, that not ready yet. Wait to Version 1.0.0 for the first stable release.

## Features

- ESP32-based solar inverter limiter
- RS485 communication with solar inverter
- sends power output data to a MQTT-server
- configurable power output limit

## Hardware Requirements

- ESP32 microcontroller
- RS485 module (e.g. MAX485 or similar)

## How to use

1. copy the config_example.h to config.h and edit the values to your needs
2. upload the code to your ESP32 microcontroller
3. connect the RS485 module to the ESP32 microcontroller (see wiring diagram below)

## Wiring Diagram

- connect the RS485 module to the ESP32 microcontroller as follows:
- see Wokwi for a wiring example

```
ESP32 Pin  | RS485 Pin
-----------|----------
  GPIO 19   | TXD (Transmitter)
  GPIO 18   | RXD (Receiver) don't use this pin, only for RXD
  GND       | GND
  VCC       | VCC (5V or 3.3V depending on the RS485 module)
```

## Home Assistant Integration

- Add the following to your configuration.yaml file:

```yaml
mqtt:
  sensor:
    - name: "SolarLimiter_SetValue"
      state_topic: "SolarLimiter/SetValue"
      unique_id: SolarLimiter_SetValue
      device_class: energy
      unit_of_measurement: "W"

    - name: "SolarLimiter_GetValue"
      state_topic: "SolarLimiter/GetValue"
      unique_id: SolarLimiter_GetValue
      device_class: energy
      unit_of_measurement: "W"
```
