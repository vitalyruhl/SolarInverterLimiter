# Manual MOS and forecast charging correction

## 5.1.3 actual readback and emergency-only follow-up

The owner's later redesign supersedes the historical 5.1.2 ACK-based capability rules below. Fresh actual charge/discharge flags plus known genuine directional faults now govern physical battery capability, including after collector restart. Historical software-request fields remain optional diagnostics. Global software-lock bit 12 cannot identify a direction and is not an emergency trigger.

The owner subsequently clarified that discharge MOS OFF must not suppress the ordinary desired inverter setpoint. Separate `battery_discharge_intent_available` qualification requires fresh known BMS/discharge feedback and genuine directional fault clearance, but ignores the actual discharge flag's boolean value. Existing economics, reserve, settings, transport and negative-price rules still apply to both calculation and transmission. Runtime `battery_discharge_available` continues to describe the physical path. Charge MOS OFF still disables PV storage reservation; cheap/forecast-charge periods reserve PV while charging is available. No automatic MOS ON is introduced. Focused tests cover a nonzero costly-period request through the extracted real PID/issue path with discharge physically unavailable, preserved cheap-period charging, charge-off behavior and genuine-fault stops.

SIL now publishes only `bms/emergency_off/set`, a non-retained four-field command (`state=false`, current collector `session`, unique `request_id`, `expires_at` at most 30 seconds ahead). Admission requires a fresh BMS sample with hard-fault bits 0..11, enabled/ready/not-pending collector status and control availability. SBT independently rechecks its persisted gate and current fault. The existing `mosMan` stored setting is unchanged and is not a second gate.

The fault episode is consumed immediately before a single send attempt, including failed/uncertain sends; no automatic retry or ON action exists. Gate-off, unavailable source, invalid clock or busy collector does not consume an unsent episode. Disconnect, collector session change and local receiver reset do not rearm an attempted episode; only a fresh hard-fault clear does. MQTT publication occurs after releasing the receiver mutex. Economic/reserve decisions do not reach this path.

E1 is an absolute two-direction disable request. Both-off avoids clearing a hidden opposite lock during emergency shutdown. Conducting flags cannot independently reveal retained software locks or prove electrical isolation; manual operations accepting actual flags may replace such hidden intent. This is firmware implementation, not production deployment or physical acceptance. No thresholds, settings schema, NVS, OTA or Git publication changes are included.

The one-shot latch is volatile. An actual SIL firmware restart starts a new observation and can issue one new emergency request after the shared gate and current hard-fault freshness qualify again. This is an accepted limitation; no persistent fault latch or boot replay is introduced.

Local validation: receiver boundaries/emergency episodes, real sibling SBT publisher contract, current control/extracted output path, future price/PV planning and compact runtime metadata all passed, including the later discharge-off inverter-intent correction. The actual-readback regression failed before implementation; the new discharge-intent API was absent before its regression. Final USB build passed: static RAM 81,560/327,680 bytes, flash 1,244,497/1,310,720 bytes; binary 1,251,072 bytes, SHA256 `CCAF802D9995F43CD6EDEEFCD3576632E61BAB3BE98B97BE80ADA6D15064DCF3`. The preceding 5.1.3 binary was superseded before deployment. Whitespace check passed. Existing dirty work on `feature/battery-energy-control` was preserved; no commit, push, upload or device acceptance was performed.

## Owner-approved scope

On 2026-10-22 the owner requested manual MOS operation through the existing HA switches while SIL remains under development. Code correction and focused validation were approved first. The owner subsequently explicitly approved OTA of the exact tested SIL 5.1.2 artifact. No MOS command, automatic replay, protection threshold, settings schema or NVS change is part of the SIL correction.

## Reproduced cause

Live SIL reported `forecastStatus=forecast_charge`, price 0.2078 EUR/kWh, 23% SOC, 192 W PV and about 15 W battery charging, but applied `controlReason=pv_only_mos`. The receiver required both actual FET flags on for battery operation. A discharge-off flag therefore selected the early PV-only return before the forecast charge target and battery-power PI could apply. The global JBD software-lock bit was separately reported as both directional restrictions, although it cannot identify the locked direction.

## Corrective design and implementation

- Parse acknowledged charge/discharge software requests from SBT control status; never derive those requests from conducting FET flags.
- Derive fresh possible charging, confirmed charging for future PV credit, and safe discharge independently. Unknown software state may reserve PV conservatively but cannot grant future stored-energy credit or discharge under a software lock.
- Keep genuine directional protection masks/latches restrictive. Explicit acknowledged discharge-off remains binding even when actual discharge FET reports on or the global lock bit is temporarily absent.
- Charge-only operation reaches the existing future charge target and battery-power PI. Discharge contribution remains zero and Limiter-OFF cannot bypass this boundary.
- Explicit charge-off does not reserve energy for inaccessible storage; an independently authorized discharge direction remains usable under existing economics/reserve rules.

## Validation and rollout

Host regression first reproduced the old receiver rejecting a usable discharge path solely because charging was off. Current-control regressions separately reproduced the missing charge target for charge-only operation. Focused receiver, publisher-contract and current-control tests pass. The extracted real PID/output path verifies 216 W PV with a 400 W charge target gives 0 W inverter output, including Limiter-OFF and output corrections of -50/0/+50 W.

Firmware 5.1.2 USB build passed: binary 1,249,904 bytes, SHA256 `E8F95AE9B438F3E0223952A1B4BB58B0998CE1EEC2324D9F79BF1B76311D8E90`; RAM 81,552/327,680 bytes, flash 1,243,321/1,310,720 bytes. Receiver/publisher-contract, current-control, extracted PID/output-application and future-price suites all passed. Independent review found rejected malformed permissions could discard an explicit discharge block; this was corrected to leave prior state untouched, with an alarm_bits=0 regression passing. Missing alarm data cannot grant discharge or confirmed future-charge credit. Final diff whitespace checks passed.

The exact frozen artifact was deployed to `192.168.2.122` on 2026-10-22 after explicit owner authorization. Native ArduinoOTA first failed to bind locally before contacting the device; a second attempt progressed but returned `Error Uploading`. Read-only reconciliation confirmed 5.1.1 with OTA inactive after each failure. A marked fallback sent the unchanged artifact through `/ota_update`; HTTP 200 returned `{"status":"ok","action":"reboot"}`. Installed version 5.1.2 was then confirmed, and all 395 compared configuration leaves were unchanged.

Fourteen post-boot samples confirmed increasing uptime and fresh BMS/Victron sources. Price/PV schedules initially remained absent because their HA publishers run every five minutes. At the next normal publication SIL received 288 price slots and 144 PV slots. Six subsequent live samples confirmed both `forecastStatus=forecast_charge` and applied `controlReason=forecast_charge`, inverter requested/calculated output 0 W, fresh sources, confirmed charging capability and unavailable discharge capability. Battery power was positive during this bounded observation. PV calibration remained unqualified (`pv_calibrated=false`, `pv_usable_wh=0`); no uncalibrated future PV credit is claimed.

SBT remained on the existing acknowledged request `ui-1`: charging allowed, discharging blocked, no command pending. No MOS command was sent during the SIL rollout. Local evidence is stored under `.local/mos-forecast-512-20261022/` (`before.json`, sender outcomes, `after.json`, `forecast-live.json`). This verifies bounded post-OTA behavior, not long-term stability. No automatic commit/push.

## Working state

Branch: `feature/battery-energy-control`, preserving the existing dirty integration work. Governance: current `origin/main:.github/AGENTS.md` and applicable refactor rules; no workflow.begin, commit, branch switch or push. Changed implementation: `src/control/CurrentControl.h`, `src/sbt/SbtReceiver.h`, `src/sbt/SbtService.h`, `src/main.cpp`; focused tests under `test/host/current_control_test.cpp`, `current_control_application_test.cpp` and `sbt_receiver_test.cpp`; readme/control-planning notes and this acceptance record. No suitable embedded-device test environment is used for these pure host contracts; the existing focused runners and the required USB firmware build provide validation. Owner-authorized SIL OTA and bounded live acceptance are recorded above.
