# SBT telemetry receiver and locked MOS preparation

Status: implementation authorized by the owner request on 2026-10-16 after publishing the completed CM upgrade. Branch `feature/sbt-telemetry-inputs`, baseline `97f4fdc45550bd2dff264731d22acee88fe09602`.

## Design and boundaries

Receive the existing 25 SBT channels plus four approved Required additions under a configurable input root, default `SolarBatteryTelemetry`. Keep SBT inputs separate from existing limiter/grid/price state. Show raw received values, validity, source health and control feedback in a dedicated runtime group; no tariff algorithm or output policy is added.

Availability requires root status `online`, source availability `available`, an advancing health sequence after the initial retained baseline, a health sample age below 15 seconds and a local heartbeat age below 15 seconds. Numeric deadband silence alone never invalidates a previously received value. Reconnect, root changes and offline transitions invalidate cached measurements. Unsupported values and malformed input stay unavailable, never synthetic zero.

Prepare only the two documented charge/discharge disable envelope contracts. A pure formatter can construct a preview from a current session, valid request ID and 30-second expiry. There is no MQTT publish path, runtime action, activation setting or automatic control call; `commands_locked` is always true. SBT remote readiness is observed separately and never unlocks SIL.

Test only COM7 / `192.168.2.126`. Keep RS485 disabled. Configure a distinct test client ID and test publish root before enabling MQTT. Observe live SBT topics, then inject explicitly mocked boundary cases only beneath an isolated test input root. Never publish production setpoints or MOS commands. No production OTA or publication of this new branch.

## Implementation and verification

- [x] Add focused host tests for strict typed payload parsing, unknown/null/oversized input, negative signed current, all Required fields, advancing health, deadband silence, timeout/offline/reconnect and permanently locked command previews.
- [x] Implement bounded receiver/state and integrate MQTT connection/message callbacks plus a dedicated settings/runtime group. Bump SIL to 4.4.0 for the new compatible receive functionality.
- [x] Run focused host tests and the USB firmware build; check that the existing limiter algorithm and production output topics are unchanged.
- [x] Read-only verify test NVS values, set only unique MQTT client/root and enable MQTT after output isolation is confirmed; USB flash COM7 only.
- [x] Observe three minutes of stable boot/HTTP/MQTT and test the corrected MQTT contract with mocked Required telemetry/control metadata, malformed/null/offline/stale/sequence cases, then restore the real input root. Actual Bluetooth-backed values require the normal site; the owner confirmed no BMS/Victron reception at the PC.
- [x] Review the final diff and record exact artifact, limits, remaining unavailable source fields, actual versus mocked evidence and zero command publication. Leave the branch uncommitted.

## Source contract

Read-only references: sibling SBT `docs/telemetry-value-inventory.md`, `docs/superpowers/specs/2026-10-16-required-bms-telemetry-controls.md`, its implementation plan and `src/telemetry/TelemetryChannels.h`. Source-health and control topic schemas were confirmed with the SBT implementation owner. Health is independent of measurement deadbands; retained-only startup must not establish freshness.

## Review correction and handoff

The exact availability vocabulary was corrected after independent review. Cross-repository publisher-to-receiver host tests now use the actual sibling SBT implementation. Corrected hardware acceptance was initially pending because COM7 was released for the separate SBT startup investigation; earlier mock tests used the wrong availability vocabulary and do not establish live compatibility.

## Final outcome

The availability contract and const-array snapshot lifetime defects were corrected and reviewed. Host tests plus the actual sibling publisher/command-parser contract pass. Final artifact `09976373fde1b5b7af259b6f4ce8647796081acc838200b59f07c346f4fd6650` passed selected-group 180-second stability (19/19 HTTP) and both selected/full-runtime MQTT mock runs (9/9 each) on restored COM7/.126. Test isolation is verified and real input root restored. No MOS commands, production SIL OTA, commit or push occurred in this phase. Actual Bluetooth-backed sample/60-second refresh validation remains for the normal site because the PC has no BMS/Victron radio reception, as confirmed by the owner. See the acceptance document for superseded failures and residual limitations.


## Authorized UI follow-up

- Move SBT MQTT Input Root into the existing MQTT Topics received-input group,
  preserving its explicit storage key and value. The Settings API category changes
  from SBT to MQTT-Topics; NVS remains ConfigManager/776A7FB565CE.
- Keep I2C settings fully visible: the user withdrew their removal after the
  existing BME280 I2C sensor was confirmed (chip ID 0x60, SDA/SCL).
- Separate the application Live views into Temperature Sensor, Limiter, and
  BMS / MPPT 150/35. Explicitly map received fields to the SBT provider and expose
  source health and read-only MOS control status. Preserve the core system views.
- Validate the build, page-scoped metadata/runtime, browser rendering, and a
  bounded COM7 stability probe. Preserve all test-device isolation settings.
  No production deployment or actuator commands are authorized for this follow-up.
