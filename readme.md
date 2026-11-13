# ESP32 RS485 Solar Inverter Limiter

SolarInverterLimiter (SIL) is ESP32 firmware that calculates and sends a power
limit to a grid-tie inverter over RS485. It follows household demand from MQTT
meter data, plans battery discharge against electricity prices and PV forecasts,
and consumes BMS/MPPT telemetry from SolarBatteryTelemetry (SBT).

Current firmware version: **5.9.6** (`VERSION` in `src/main.cpp`).

## Recent changes

- **5.9.6**: Builds against the released ConfigurationsManager 4.12.0 from the
  PlatformIO registry instead of the local test packages; all profiles,
  including `testesp_mqtt_drop_oldest`, now use the registry package.
- **5.9.5**: Without a planned battery budget, the relative policy trims direct
  PV passthrough on BMS feedback until the battery no longer discharges; fixed
  standby loads and low-load inverter losses are no longer covered by the
  battery. Adds the `testesp_mqtt_drop_off` comparison profile.
- **5.9.4**: Adds an isolated, opt-in CM MQTT drop-oldest trial. Normal profiles
  retain their previous CM package and queue policy; there is no production rollout.
- **5.9.3**: Builds against the local ConfigurationsManager (CM) 4.12.0 package
  with `ESP32Async/ESPAsyncWebServer@3.12.1` and `ESP32Async/AsyncTCP@3.5.0`
  (replacing the esphome web server fork). After stored settings load, SIL
  raises the bounded MQTT TX queue to at least 8192 bytes and the TX stall
  timeout to at least 30000 ms; higher stored values are kept.
- **5.9.2**: After a short loss of SIL's own MQTT broker session (at most 60 s),
  SBT values that were fresh at the loss are re-armed by the first newly
  advancing SBT health sequence, so change-only values such as SOC and alarm
  bits need not be republished. Deadlines are not renewed; collector `offline`,
  source `unavailable`, a session change or a newer packet for the value take
  precedence.
- **5.9.1**: Large application MQTT frames are deferred for 1 s after CM reports
  TCP backpressure, so the bounded TX queue does not overflow and drop the
  session during WiFi stalls. Healthy links are not delayed.
- **5.9.0**: Forecast prices use horizon-based premiums: up to 12 h, 12-24 h and
  beyond 24 h (defaults 3 / 8 / 10%, keys `pred12Pct`, `pred24Pct`, `predPct`).
- **5.8.3**: `Output Correction Offset (W)` biases household demand before the
  PID or manual-mode calculation instead of the final command (see
  [Control behavior](#control-behavior)).

The Home Assistant planner-shadow scripts additionally hold last-good prices
across short tariff alignment mismatches and accept the previous price revision
briefly after a republish; that logic lives under `docs/home-assistant/`, not in
the firmware.

## Features

- RS485 inverter limit output (Serial2, 4800 baud default)
- Household-demand PID from MQTT grid and PV meter values
- Relative-price battery planning with price schedule and PV forecast inputs
- Explicit operating modes, see [operating modes](docs/operating-modes.md)
- SBT BMS/MPPT telemetry receiver with freshness and fault gating
- MQTT publication of setpoint, planning and diagnostics
- Settings-only web UI (CM), OTA updates
- Optional BME280 temperature/humidity/dewpoint sensor; OLED, fan/heater relays
  and GUI/MQTT logging are disabled in all PlatformIO profiles

## Hardware and wiring

- ESP32 development board (`nodemcu-32s` in PlatformIO)
- RS485 module (e.g. MAX485 or similar)
- Optional BME280 on I2C

```
ESP32 Pin | Function
--------- | ------------------------------------------------
GPIO 19   | RS485 TX (setting RS485Tx)
GPIO 18   | RS485 RX (setting RS485Rx; received data unused)
GPIO 4    | RS485 DE/RE direction (setting RS485DE)
GPIO 21   | I2C SDA (fixed)
GPIO 22   | I2C SCL (fixed, 400 kHz)
GPIO 14   | Reset button (held at boot restores defaults)
GPIO 13   | AP mode button (held at boot starts AP mode)
GND / VCC | RS485 module supply (5 V or 3.3 V depending on module)
```

The cooling fan (GPIO 23) and heater (GPIO 27) relay outputs exist only when
`FEATURE_FAN_ENABLED` / `FEATURE_HEATER_ENABLED` are set; both default to 0. See
the [GPIO reference](docs/esp32-connection.md) for pin constraints.

## Build and upload

Common settings live in `[env]` of `platformio.ini` (platform
`platformio/espressif32@7.0.1`, Arduino, `-std=gnu++17`, LTO,
`CM_ENABLE_LIVE_UI=0`, `CM_ENABLE_SETTINGS_UI=1`, `CM_MQTT_BOUNDED_IO=1`).
Build outputs use `.pio/operating-modes-5.6.0`; library dependencies use
`.pio/plan-retention-5.4.7-final/libdeps`.

| Environment | Purpose |
| --- | --- |
| `usb` | Production profile, upload/monitor on COM8 |
| `ota` | Production profile, OTA upload (`espota`) |
| `ota_no_oled` | Compatibility alias of `ota` |
| `ota_no_oled_no_bme` | `ota` without BME280 (no temperature/humidity/dewpoint values or topics) |
| `testesp` | Mock RS485 transport (`SIL_MOCK_TRANSPORT=1`) on COM8 |
| `testesp_recovery` | `testesp` plus exactly one logged stack reset after healthy mock planning |
| `testesp_mqtt_drop_oldest` | Temporary CM drop-oldest trial on COM7, with isolated fixture routing and no hardware TX |
| `testesp_mqtt_drop_off` | `testesp_mqtt_drop_oldest` with drop-oldest disabled (comparison baseline) |
| `planner_shadow` | Isolated planner comparison: mocked transport, fan/heater disabled, guarded `SolarLimiterTest` routing |
| `planner_shadow_trace` | `planner_shadow` plus numeric TX failure stage diagnostics |

```powershell
pio run -e usb                # build
pio run -e usb -t upload      # USB upload
pio run -e ota -t upload      # OTA upload
pio device monitor -e usb     # serial monitor (115200)
```

Mock images (`testesp*`) never write RS485 hardware. They force DHCP, the MQTT
root `SILHardening/COM8/out` and client ID `SILHardening-COM8-device`, and
disable MQTT if fixture isolation is incomplete. `planner_shadow*` requires mock
transport at compile time and only publishes below `SolarLimiterTest/`. Never
upload mock or shadow images to the production inverter.

### ConfigurationsManager dependency

Published builds consume ConfigurationsManager (CM) 4.12.0 from the PlatformIO
registry (`vitaly.ruhl/ESP32 Configuration Manager@4.12.0`).

`tools/precompile_wrapper.py` accepts exactly one explicit CM dependency,
verifies its version and builds the Settings-only WebUI inside the consumed
copy. Node.js/npm are required when the UI asset must be regenerated.

The temporary `testesp_mqtt_drop_oldest` profile uses the same registry
package. It enables
`setDropOldestQueuedPublishes(true)` and registers `onDroppedMessage` after
settings load, retaining the 8192-byte queue / 30000-ms stall minimums.
The callback logs dropped message/encoded-byte deltas and cumulative totals
to serial, without publishing recursively. `testesp_mqtt_drop_off` builds the
same image and probe with drop-oldest disabled as a comparison baseline.

This trial requires the existing isolated fixture at `192.168.2.220:18884`
and `SILHardening/COM8` routing. After 30 seconds connected, it sends one
non-retained burst of 96 x 512-byte probe payloads to its own diagnostic topic
to exercise queue pressure and deferred callbacks. It then resumes ordinary
operation. Only completely unsent eligible publishes can be discarded,
including retained ones; a successful publish is not a delivery guarantee.
The option's retention and any production use remain undecided.

## First start

1. Build and upload (`pio run -e usb -t upload`).
2. On first boot, or when the WiFi SSID is empty, the device starts an access
   point. Holding the AP button (GPIO 13) at boot also starts AP mode.
3. Connect to the AP and open the web UI (usually `http://192.168.4.1/`) to
   configure WiFi, MQTT and the limiter settings.
4. Afterwards the Settings UI is available on the device IP in the LAN.

Optional defaults for WiFi/MQTT can be provided in `src/secret/secrets.h` (see
`src/secret/secrets.example.h`). The web UI is Settings-only; Live views are
compiled out (`CM_ENABLE_LIVE_UI=0`), runtime values are published via MQTT.

## Control behavior

### Relative planner

Normal and shadow builds use the relative-time-tier planner
(`relative_time_tiers_v1`). It classifies 144 accepted quarter-hour prices into
cheap / very expensive / additional expensive shares and applies a configurable
minimum storage net advantage. Forecast prices are valued as
`raw + abs(raw) * premium / 100`, with the premium selected by slot horizon
(up to 12 h, up to 24 h, beyond). A nonpositive or missing current price stops
output in this policy.

| Setting | Default |
| --- | --- |
| Cheap / very expensive / additional expensive time | 10 / 10 / 20% |
| Storage net advantage | 0.05 EUR/kWh |
| Forecast premium up to 12 h / 24 h / beyond | 3 / 8 / 10% |
| Reserve OFF / ON | 5 / 10% |
| Charge priority threshold / inverter share | 80 / 10% |
| MAX / MIN / Idle | 900 / 0 / 500 W |
| Output correction | -100 W |
| SBT data timeout | 180 s (1..9999) |

`Idle Output (W)` is retained as a setting but does not select an output.

### Operating modes and battery

Modes `automatic`, `household_follow`, `charge_priority` and `pv_follow` are
described in [operating modes](docs/operating-modes.md). At a fresh, qualified
100% SOC with a valid positive-price plan, household demand may release PV that
a full battery has curtailed, even when measured MPPT output is zero.

SIL never publishes BMS MOS commands. It consumes directional BMS restrictions
and stops inverter output when discharge protection requires it; manual MOS
control remains in SBT and the BMS keeps its own protection. The legacy `mosMan`
setting is preserved without granting MOS ownership. Fresh actual MOS flags and
genuine directional alarms govern physical battery capability. Actual discharge
MOS OFF does not cap the calculated request; charge MOS OFF disables PV storage
reservation.

The SBT receiver retains last-good values through transient source loss within
`SBT Data Timeout (s)`; invalid entries normalize to 180. Values become
available only when root/source availability and an advancing heartbeat agree.
BMS and MPPT freshness are checked independently.

### Output correction, reserve and minimum

`Output Correction Offset (W)` (key `LimiterCorrW`) biases household demand
before automatic PID regulation or manual following. It is not deducted from the
battery plan. For 600 W demand and -100 W offset, a 200 W battery ceiling stays
200 W, while a 900 W ceiling is limited to 500 W.

The reserve latch closes at or below Reserve OFF and opens at or above Reserve
ON. After a restart it starts blocked until fresh SOC reaches Reserve ON.

`Min Output (W)` overrides price limits only in the legacy (non-relative)
policy, when qualified sources permit discharge and the reserve latch allows it.
The relative-time policy does not apply this override.

Negative-price handling (`LimiterNegPrice` / `LimNegZero`) is mutually
exclusive: force MIN or set output to 0 (default) while
`SolarLimiter/Input/NegativePrice` is true.

## MQTT

Topics are relative to the configured publish root (default `SolarLimiter`).

| Direction | Topic | Content |
| --- | --- | --- |
| Out | `<root>/SetValue` | Sent inverter limit (W) |
| Out | `<root>/CalculatedValue` | Calculated output before the final send decision (W) |
| Out | `<root>/GetValue` | Received signed grid power (W) |
| Out | `<root>/Temperature`, `/Humidity`, `/Dewpoint` | BME280 values (if enabled) |
| Out | `<root>/Planning/Config`, `/Assessment`, `/State` | Planner configuration and state |
| Out | `<root>/Planning/DischargeBudget`, `/ChargePlan` | Projected schedules |
| Out | `<root>/Diagnostics/Runtime` | Runtime/transport diagnostics |
| Out/In | `<root>/Control/Mode`, `/Mode/set`, `/ModeStatus` | Operating mode, see [operating modes](docs/operating-modes.md) |
| In | `tele/powerMeter/powerMeter/SENSOR` (`E320.Power_in`) | Signed grid power, positive = import |
| In | `tele/tasmota_1DEE45/SENSOR` (`ENERGY.Power`) | PV power |
| In | `SolarLimiter/Input/NegativePrice`, `SolarLimiter/Input/ElectricityPrice` | Price flags/value |
| In | `SolarLimiter/Input/PriceSchedule` | Price schedule (input version 2) |
| In | `homeassistant/sensor/solar_forecast/shed/schedule` | PV forecast schedule |
| In | `SolarBatteryTelemetry/...` | SBT BMS/MPPT telemetry (`SBT MQTT Input Root`) |

Input topics are configurable under Settings > MQTT-Topics. The bounded MQTT TX
queue settings (`txQueueBytes`, `txStallTimeoutMs`) are enforced to at least
8192 bytes / 30000 ms at boot.

## Home Assistant

All Home Assistant files live in `docs/home-assistant/`; see
[docs/home-assistant/README.md](docs/home-assistant/README.md) for the file map
(MQTT entities, retained discovery configs, templates, automations, UI helpers,
pyscripts and the Overview/Power dashboard views).

## Tests

Host test scripts live in `tools/test/` (for example
`run-operating-modes-tests.ps1`, `run-sbt-receiver-tests.ps1`,
`run-future-prices-tests.ps1`). They require LLVM Clang on Windows and the
resolved `usb` dependencies. Host results do not establish hardware acceptance.

## Version history (condensed)

Detailed acceptance reports and plans for these versions were removed from
`docs/` in commit `53537d0`; they remain available in git history.

- **5.8.2**: Full-battery PV restart preserved with the relative planner.
- **5.8.1**: Relative planner promoted to normal builds with the defaults above.
- **5.6.1**: Full-battery PV release; SIL stops sending automatic MOS commands.
- **5.6.0**: Explicit operating modes (`pv_follow` added; earlier charge
  priority defaults were 95% / 0%).
- **5.4.x**: Independent BMS/MPPT qualification (5.4.10), plan retention across
  short source gaps (5.4.6/5.4.9), chronological PV recharge planning (5.4.8),
  WiFi recovery with CM 4.10.3 (5.4.7), MQTT send-progress deadline with CM
  4.10.2 (5.4.5), complete discharge projection (5.4.4), WiFi power-save setting
  `WiFiPowerSave` with CM 4.10.0 (5.4.3), bounded MQTT TX FIFO (5.4.2), runtime
  diagnostics and fail-closed numerics (5.4.1), charge-intent projection (5.4.0).
- **5.3.0**: Relative-price allocation and reserve charging; `pwrDb` removed.
- **5.2.x**: Discharge-budget schedule (5.2.0); qualified MIN override (5.2.1).
- **5.1.x / 5.0.x**: SBT/HA integration and compact runtime metadata.
- **4.4.0**: First SBT receiver; **4.3.2**: CM v4 adoption.
