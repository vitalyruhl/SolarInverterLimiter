# ESP32 RS485 Solar Inverter Limiter

This project is an ESP32-based solar inverter limiter that can be used to limit the power output of a solar inverter to a specific value. The project uses an ESP32 microcontroller and an RS485 module to communicate with the inverter.

## Features

- ESP32-based solar inverter limiter
- RS485 communication with solar inverter
- Publishes power and sensor values via MQTT
- Configurable power output limit
- Web-based settings UI (ConfigManager)
- BME280 sensor support; OLED and GUI/MQTT logging are disabled in the production profiles

## ConfigManager v4 note

- This project is aligned to `ESP32 Configuration Manager` `4.6.0`, pinned to commit `554869da7efc98e0c1874e74bf0c78d4bcd1664a` in `platformio.ini`.
- The pre-build step uses `tools/precompile_wrapper.py`.
- The wrapper delegates to the library script inside `.pio/libdeps/<env>/ESP32 Configuration Manager/tools/preCompile_script.py`.
- Link-time optimization preserves OTA slot headroom without changing the partition layout. Normal UART logging at Info level and ESP32 ROM/panic diagnostics remain available.
- The wrapper builds only the resolved dependency; it no longer replaces backend or WebUI files from a local checkout.
- SIL firmware version `4.3.2` adopts this pinned dependency without changing the inverter control algorithm.
- Memory decision (2026-10-16): remove OLED and GUI/MQTT logging now, retain normal UART Info logging and BME280 support. If a future build reaches a demonstrated memory limit, the maintainer has authorized removing BME280 support as a fallback; it is not removed preemptively.

Build and hardware acceptance evidence: [CM runtime validation](docs/cm-runtime-validation-2026-10-16.md).

## Maintenance notes 2026-05-15

- Governance was migrated from `.github/copilot-instructions.md` to the agent-based structure in `.github/AGENTS.md` and `.github/agents/*.agent.md`.
- The repository guidance now treats this project as a C++/ESP32/PlatformIO project. GitHub Projects are optional; the current project value is `none`.
- GitHub Issues, Pull Requests, and agent-created GitHub comments should be written in English. Normal user chat may remain informal German.
- The legacy Feierabend workflow trigger was removed in favor of explicit workflow shortcuts.
- PID inverter regulation now uses signed E320 grid power from `tele/powerMeter/powerMeter/SENSOR` at JSON path `E320.Power_in`.
- PID base target is `solarPowerW + signedGridPowerW + offset`; positive grid power means import and negative grid power means export.
- PID logging derives `gridIn = max(signedGridPowerW, 0)` and `gridOut = max(-signedGridPowerW, 0)` for live diagnostics.
- Home Assistant should publish electricity price inputs to `SolarLimiter/Input/ElectricityPrice` and `SolarLimiter/Input/NegativePrice`.
- Negative-price handling is available in the ESP32 settings: force output to configured minimum or explicitly set output to `0` when `NegativePrice` is `1`.
- Dependency maintenance updated `Adafruit GFX Library` to `1.12.6`, `ArduinoJson` to `7.4.3`, and PlatformIO platform `espressif32` to `platformio/espressif32@7.0.1`.
- `pio pkg outdated`, `pio run -e usb`, and `pio run -e ota` were successful after the dependency updates.
- Live OTA upload and boot smoke test succeeded after the `espressif32` update.

Open follow-up:
- Firmware flash usage remains tight at about `98.8%`.
- PID tuning may still require further live observation.
- Negative-price handling should be verified during an actual negative-price period or with a controlled MQTT test payload.

## Hardware Requirements

- ESP32 microcontroller
- RS485 module (e.g. MAX485 or similar)

## How to use

1. Build and upload with PlatformIO:
  - Build: `pio run -e usb`
  - Upload: `pio run -e usb -t upload`
2. On first boot (or when WiFi SSID is empty) the device starts an access point:
3. Connect to the AP and open the web UI (usually `http://192.168.4.1/`) to configure WiFi/MQTT and other settings.
4. After WiFi is configured the web UI will be available on the device IP in your LAN.

## Build variants

- `usb`: production profile without OLED or GUI/MQTT logging; BME280 remains enabled.
- `usb_bench`: COM7 smoke test with MQTT and RS485 disabled in memory before startup; existing WiFi/NVS network settings are preserved. Never use this variant for the production inverter.
- `ota`: the same lean production profile with OTA upload.
- `ota_no_oled`: compatibility alias for the production profile.
- `ota_no_oled_no_bme`: disables both OLED and BME280 so the example also builds without the I2C sensor/display stack.

When `FEATURE_BME280_ENABLED=0`, the temperature, humidity, and dewpoint runtime values and MQTT topics are omitted. When both OLED and BME280 are disabled, the example also omits the shared I2C settings page.

## Wiring Diagram

- connect the RS485 module to the ESP32 microcontroller as follows:
- see Wokwi for a wiring example

```
ESP32 Pin | RS485 Pin
--------- | ---------
GPIO 19   | TXD (Transmitter)
GPIO 18   | RXD (Receiver)
GND       | GND
VCC       | VCC (5V or 3.3V depending on the RS485 module)
```

## Home Assistant Integration

- See the examples in `docs/HomeAssistant.yaml` and `docs/HomeAssistant_Dashboard.yaml`.
- Minimal MQTT sensor example (topics depend on the configured MQTT base topic):

```yaml
mqtt:
  sensor:
    - name: "SolarLimiter_SetValue"
      state_topic: "SolarLimiter/SetValue"
      unique_id: SolarLimiter_SetValue
      device_class: power
      unit_of_measurement: "W"

    - name: "SolarLimiter_GetValue"
      state_topic: "SolarLimiter/GetValue"
      unique_id: SolarLimiter_GetValue
      device_class: power
      unit_of_measurement: "W"

    - name: "SolarLimiter_Temperature"
      state_topic: "SolarLimiter/Temperature"
      unique_id: SolarLimiter_Temperature
      device_class: temperature
      unit_of_measurement: "°C"

    - name: "SolarLimiter_Humidity"
      state_topic: "SolarLimiter/Humidity"
      unique_id: SolarLimiter_Humidity
      device_class: humidity
      unit_of_measurement: "%"

    - name: "SolarLimiter_Dewpoint"
      state_topic: "SolarLimiter/Dewpoint"
      unique_id: SolarLimiter_Dewpoint
      device_class: temperature
      unit_of_measurement: "°C"

```
