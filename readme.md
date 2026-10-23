# ESP32 RS485 Solar Inverter Limiter

## Control correction (5.3.0)

Stored energy is allocated by relative useful prices within the existing 36-hour window, independently of the comfort/deadband bands. New charging retains the existing round-trip efficiency, wear, and minimum-benefit checks. Reserve OFF charging uses all available PV when the price is not expensive; between OFF and ON, cheap prices reserve half of charger DC power. Economic charging may request a higher common target. Reserve discharge hysteresis and the existing qualified MIN override remain in force.

The obsolete `Battery Power Deadband (W)` / `pwrDb` setting and positive preservation charging floor are removed. The owner-configured output correction remains unchanged and is still applied exactly once. Existing stored keys need no NVS rewrite; the removed key is ignored. The published CM 4.9.0 dependency and Settings-only build profile are unchanged. Owner-authorized production OTA on 2026-10-23 confirmed version 5.3.0 and preserved all remaining Limiter settings. See the [verification and deployment record](docs/acceptance/2026-10-23-relative-price-and-reserve-control.md); the earlier intermittent reset/HTTP investigation remains open.

## Settings-only production build

Production profiles use the published `vitaly.ruhl/ESP32 Configuration Manager@4.9.0` package, with `CM_ENABLE_LIVE_UI=0` and `CM_ENABLE_SETTINGS_UI=1` in the shared PlatformIO configuration. Both backend presentation code and embedded Vue assets use this Settings-only flavor. The consumer hook builds the UI from the registry package's published sources in its project-local dependency directory and verifies registry provenance, compiler flags, generated-header flavor, and source/asset hashes. An adjacent CM checkout or a stale Live-enabled header is rejected. Node.js/npm are needed when the selected UI asset requires regeneration. `usb` and `ota` use real transport; only `testesp` enables the mock transport.

## Historical integration handoff (5.1.1 development)

The [integration plan](docs/superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) has been explicitly continued. See the [acceptance and owner handoff](docs/acceptance/2026-10-20-integration-acceptance.md) for exact build/device results and remaining gates. Production ESP deployment remains with the owner; the requested local intermediate commits preserve the current development state without clearing device acceptance. The current candidate uses local ConfigurationsManager 4.8.0; final device acceptance remains open due to SBT HTTP timeouts and added overload failures. See exact per-image evidence before any rollout.

The receiver retains last-good values through transient source loss. `SBT Data Timeout (s)` is 1..9999 seconds, default 180; invalid entries normalize visibly to 180. Since 5.1.3, fresh actual MOS flags and genuine directional alarms govern physical battery capability; historical acknowledged requests have no control authority. The global software-lock bit cannot identify a direction and remains diagnostic. Actual discharge MOS OFF does not cap the calculated or transmitted inverter request: SIL continues to plan and send ordinary desired discharge under existing economics, reserve, protection, settings and transport rules. Physical discharge capability is reported separately; SIL never enables a MOS to satisfy that intent. Charge MOS OFF disables PV storage reservation. With charging enabled, cheap/forecast-charge periods still reserve PV normally. Missing PV/meters and genuine charger/protection faults retain restrictive behavior.

SIL sends only a one-shot emergency both-off request on `bms/emergency_off/set` for fresh JBD hard faults (bits 0..11). SBT's shared persistent control gate governs this path; disabled, unavailable, busy or stale state cannot consume an unsent episode. No economic MOS action, automatic ON or retry is implemented. Reconnect/session changes never rearm an attempted fault; only a fresh hard-fault clear does. SBT independently checks its gate and current fault before writing. A local MQTT send or E1 ACK does not prove physical isolation. The legacy `mosMan` setting is preserved but does not authorize or suppress this emergency-only path. This supersedes earlier Hand/Auto and always-locked planning text; local build validation is separate from hardware acceptance.

SBT 1.2.0 adds independent `bms/temperature_1_c` and `bms/temperature_2_c`; SIL has 31 receiver slots and 29 visible SBT fields. NTC identities use raw JBD order, labeled Top/Bottom; a missing sensor is unavailable. The System live card is removed; only UsedHeap, FreeHeap and HeapFragmentation appear under Limiter. `SIL_DEACTIVATE_BME280_CARD=1` hides the BME280 card, while 0 shows it; sampling/MQTT remain enabled independently. I2C uses SDA21/SCL22/400000 Hz; old pin/frequency NVS keys are inert.

`Output Correction Offset (W)` retains the persisted `LimiterCorrW` key and applies exactly once to the complete calculated command, immediately before transmission. It is no longer part of the PID input. Before the qualified minimum-output override below, an uncorrected zero remains zero and configured MAX remains MAX; every intermediate result is offset and then clamped to 0..MAX before conversion to the wire type. Thus 0 + 50 = 0 W, 1 + 50 = 51 W, 1 - 50 = 0 W, and 200 + 50 = 250 W. This intentional final bias may exceed the pre-correction PV or household ceiling; other battery and forecast efficiency models are unchanged.

Since 5.2.1, `Min Output (W)` overrides price and charging-plan limits when fresh, qualified sources permit discharge intent and the reserve latch allows it. The final command, including output correction, cannot fall below MIN, bounded by MAX: MIN 700 W and offset -50 W still request at least 700 W. This also applies to an economic zero and when charge MOS is OFF; actual discharge MOS OFF retains the desired-output behavior above. Reserve restrictions, genuine faults, negative-price stops, missing meters, transport/inverter OFF, and source grace/recovery cannot gain a minimum override. Existing source-loss fallback and PV-only behavior remain unchanged. Set MIN to 0 for unrestricted economic planning; existing persisted nonzero values adopt this precedence. `minimum_output` identifies a minimum-determined command, while the discharge-budget schedule remains the planner's original allocation. No MOS command is introduced.

Independent atomic price-v1/PV-v2 schedules feed bounded cached planning. Forecast energy never increases the measured battery pool. Qualified PV before the earliest future reserved target can reduce that future reservation, bounded by MPPT power, household demand, losses and SOC headroom. See the [wire contract](docs/control-planning/mqtt-schedule-contract.md) and [HA migration](docs/home-assistant/shed-migration.md). Calibration remains neutral until 14 eligible completed days in 30; missing intervals remain missing.

Version 5.2.0 adds a non-retained diagnostic [battery discharge-budget schedule](docs/control-planning/mqtt-schedule-contract.md#battery-discharge-budget-output-v1). The existing per-quarter allocations can be overlaid on the same 24-hour price chart with a right W axis using the [MQTT sensor](docs/home-assistant/sil-discharge-budget-sensor.yaml) and [card example](docs/home-assistant/sil-price-discharge-overlay.yaml). The budget is planned battery AC contribution, not measured discharge or a complete future inverter SetValue. Actual discharge MOS OFF does not suppress the budget; charge MOS OFF retains the existing PV-storage veto. This version was tested by USB only on the isolated dedicated test ESP; production SIL and Home Assistant remain unchanged. See the [bounded acceptance evidence](docs/acceptance/2026-10-22-discharge-budget-export.md).

This project is an ESP32-based solar inverter limiter that can be used to limit the power output of a solar inverter to a specific value. The project uses an ESP32 microcontroller and an RS485 module to communicate with the inverter.

## Features

- ESP32-based solar inverter limiter
- RS485 communication with solar inverter
- Publishes power and sensor values via MQTT
- Configurable power output limit
- Web-based settings UI (ConfigManager)
- BME280 sensor support; OLED and GUI/MQTT logging are disabled in the production profiles

## Historical compact metadata checkpoint (5.0.2)

At that checkpoint, CM >=4.7.0 supported 19 enabled GUI descriptors and
40 SBT receiver descriptors use compact Float/Int/Bool/Text storage. Keys,
placement, getters, individual provider order, units, precision and control
algorithms remain unchanged. Optional fan/heater controls retain their existing
builders; those features are disabled in the COM7 comparison profile.

The normal profiles now use `symlink://../ConfigurationsManager`: the adjacent
local CM >=4.7.0 checkout is required, including any uncommitted development
changes. No stale Git pin or unpublished release reference is used. This is an
explicit development binding, not a reproducible released dependency; pin a
published compatible CM commit when this coordinated work is released.

All existing profiles disable OLED. Their unused SSD1306, GFX and BusIO package
declarations have been removed. Re-enabling OLED also requires declaring its
display dependencies; BME280 support is retained.

The explicitly authorized COM7 comparison uses the ignored local configuration
`.pio/compact-runtime-metadata/platformio-test.ini`, binding
`C:/Daten/_Codding/ESP32/ConfigurationsManager` via symlink and a read-only asset
preflight. Build it with:

```powershell
pio run -e testesp -c .pio/compact-runtime-metadata/platformio-test.ini
./tools/test/run-compact-runtime-metadata-tests.ps1
```

This local profile retains BME280 and mocked RS485 output, omits BLE/NimBLE,
disables CM logging like the SBT comparison, and preserves the test ESP's existing
OTA password. Production-profile provisioning behavior is unchanged. Parent
uploads only the application to COM7; the repository partition table must not be
written to the test device. The [portable plan and evidence](docs/acceptance/2026-10-18-compact-runtime-metadata-evidence.md)
records the unresolved HTTP crash context and hardware acceptance boundary.

Common always-used PlatformIO settings now live in `[env]`; the five existing
profiles initially resolved to exactly the same option values after that
organization-only change. Following owner feedback, the common dependency was
then switched to local CM >=4.7.0 and unused OLED packages were removed. These
are intentional dependency changes. The ignored comparison configuration still
selects its own logging and build-partition choices.

## ConfigManager v4 note

- Current development dependency: adjacent local `ConfigurationsManager` >=4.7.0 via symlink. The earlier published 4.6.0 pin is historical.
- The pre-build step uses `tools/precompile_wrapper.py`.
- The wrapper resolves the declared local symlink, checks CM version mirrors and
  the existing WebUI asset, and never modifies the shared checkout.
- Link-time optimization preserves OTA slot headroom without changing the partition layout. Normal UART logging at Info level and ESP32 ROM/panic diagnostics remain available.
- Prepare changed WebUI assets in ConfigurationsManager itself before consuming
  them; SIL builds use the reviewed generated asset without rebuilding it.
- Historical SIL firmware `4.3.2` adopted this pinned dependency without changing the inverter control algorithm.
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

## Historical SBT receiver preparation (4.4.0, test branch)

The following describes the earlier 4.4.0 checkpoint; current behavior is documented above. Its `sbt` runtime group received the 29 documented SBT channels and separate source-health/control feedback. Set **SBT MQTT Input Root** to the collector root (default `SolarBatteryTelemetry`). Values remain unavailable until root/source availability and a fresh advancing heartbeat agree. Unchanged numeric values remain valid while health advances; unsupported, malformed, absent or stale values appear as `null`.

MOS shutdown envelopes are prepared as pure previews only. `commands_locked` is always true; this version has no command publishing path, control action or activation setting. The new inputs do not feed the inverter limiter or price logic.

For the COM7 receiver test, MQTT uses a unique client ID, publish root `SILTest/COM7`, and a 50 ms listen interval; RS485 stays disabled. The previous 500 ms interval processes bursts too slowly for a 15-second health deadline. Any future deployment needs an explicit receiving-throughput review. The extension has not been deployed to the production SIL.

See [receiver plan](docs/acceptance/2026-10-16-sbt-telemetry-inputs-evidence.md) and [acceptance evidence](docs/sbt-receiver-validation-2026-10-16.md). Focused host tests: `./tools/test/run-sbt-receiver-tests.ps1` after the USB dependencies have been resolved; the script requires LLVM Clang on Windows. Add `-SbtRepository ../SolarBatteryTelemetry` to verify the actual sibling publisher-to-receiver contract. Corrected hardware MQTT acceptance passed on COM7. The later UI check also received real BMS/Victron samples after the collector returned to its normal site; long-term source/network stability remains separate. See the evidence document for exact results and limits.

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
- `testesp`: mock RS485 profile; the bounded integration acceptance uses a private derivative with CM logging disabled and the actual test-board partition layout. Mock images force the MQTT publication root `SILTest` and a test client identity. Never upload them to the production inverter.
- `ota`: the same lean production profile with OTA upload.
- `ota_no_oled`: compatibility alias for the production profile.
- `ota_no_oled_no_bme`: disables both OLED and BME280 so the example also builds without the I2C sensor/display stack.

When `FEATURE_BME280_ENABLED=0`, the temperature, humidity, and dewpoint runtime values and MQTT topics are omitted. I2C pins/frequency are fixed; an editable OLED address exists only when OLED is enabled.

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
      unit_of_measurement: "Â°C"

    - name: "SolarLimiter_Humidity"
      state_topic: "SolarLimiter/Humidity"
      unique_id: SolarLimiter_Humidity
      device_class: humidity
      unit_of_measurement: "%"

    - name: "SolarLimiter_Dewpoint"
      state_topic: "SolarLimiter/Dewpoint"
      unique_id: SolarLimiter_Dewpoint
      device_class: temperature
      unit_of_measurement: "Â°C"

```


### Live telemetry pages

The application Live views are Temperature Sensor (BME280 temperature, humidity,
dewpoint and pressure), Limiter, and BMS / MPPT 150/35. The collector view includes
source freshness/reasons, received BMS/MPPT channels and read-only MOS status;
SIL command transmission remains locked. Unavailable measurements remain empty.
The core System page is retained. BMS/MPPT measurements use two decimal places;
counts and bit fields use integers.

SBT MQTT Input Root is under Settings > MQTT Topics > MQTT-Received. The explicit
storage key is unchanged; its Settings API category is now MQTT-Topics. I2C settings
remain visible because the existing BME280 uses SDA/SCL (default GPIO21/GPIO22).
