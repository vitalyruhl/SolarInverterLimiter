# SIL / SBT / HA integration specification

Date: 2026-10-20. Owner-approved requirements, not deployed behavior. Implementation starts only after the current planning closeout and explicit continuation. The [single active implementation plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) defines execution and acceptance. Later decisions in this document supersede conflicting Round 5 defaults; other accepted control semantics remain in force.

## 1. Authority and stop boundary

The current deliverable is repository documentation, diagram alignment, old-plan consolidation and local commits on existing work branches, followed by STOP. Do not start feature implementation, HA edits, builds or device tests during this closeout. No push, main merge, release or branch deletion is implied by a clean local checkpoint.

The following implementation-stage permissions were explicitly supplied by the owner and persist for the continuation; do not ask for them again within this scope:

| Surface | Approved actions | Limits |
| --- | --- | --- |
| SIL and SBT source | Implement the approved coordinated changes below, including focused tests/builds | Respect user edits; shared CM changes only when strictly needed and separately scoped; no unrelated refactor |
| Test ESP | Build and install BOTH SIL and SBT in succession; open Live and Settings; observe each for at least 5 minutes | Identify the actual port/IP/partition before writing; use isolated identities/topics; SIL mocked RS485; SBT no MOS/charger actuation; no production ESP uploads |
| Chrome | Use the owner's open authenticated tabs for HA, test-device UI and scoped verification | No credential export; no unrelated account/browser changes |
| Home Assistant | Replace obsolete Forecast.Solar entries with one correct entry named **Shed**; migrate affected consumers; correct solar/AC accounting and the 2 W offset path as specified | Snapshot old entries/consumers first, create and verify replacement before deleting exact obsolete entries; preserve location and statistics/history |
| HA automations | Create the PV forecast collection/calibration/MQTT automation; adjust the existing price automation where necessary | One owner per producer/topic; no duplicate competing regulator; no MOS/inverter control via HA |
| MQTT Explorer | Inspect topics, retained flags, cadence and health/value transitions; use the open authenticated session | No production publish/delete/retained clearing or control commands; injected fixtures only in an isolated test namespace |
| Git | Current local documentation commits and clean worktrees; later scoped commits according to continuation instructions | Never force-add secrets/local tooling, push/main-merge/release/delete branches implicitly |

MQTT Explorer was open and authorized but native app automation was unavailable in the discovery harness. This is an access-tool limitation, not missing user permission. A future agent may use a supported computer-use tool; if absent, use an authorized passive subscriber without exposing credentials. Never claim Explorer capture without using it.

The owner deploys production firmware personally. Test uploads do not authorize production IPs. The owner says SBT cannot receive real plant data at the test ESP: accept a **no-data UI/stability test** as partial evidence, with host fixtures for data handling. Do not claim live BLE or closed-loop acceptance.

## 2. Confirmed environment and evidence

- SIL root: `C:/Daten/_Codding/ESP32/SolarInverterLimiter`, work branch `feature/battery-energy-control`, starting HEAD `e155f4374a2feb7a40f1c56a979dc5942a3823ef`, firmware 5.0.2. Production UI read at `192.168.2.122`.
- SBT root: `C:/Daten/_Codding/ESP32/SolarBatteryTelemetry`, work branch `feature/move-sil-planning`, starting HEAD `0928008498a18f3dbd4cd0bd0bf3949b37b581c6`. Production reference `192.168.2.21`; do not upload there.
- Adjacent `ConfigurationsManager` is a local development dependency, not an immutable released package. Capture its HEAD and dirty state before builds; never rebuild or overwrite shared generated assets as a consumer side effect.
- Historical test references: COM7, test ESP IP `192.168.2.126`, SIL publish root `SILTest/COM7`; confirm identity and current profile before writing. The same test board may require sequential images rather than two simultaneous devices. Do not reuse production MQTT client IDs.
- SIL actual I2C: SDA21, SCL22, 400000 Hz. Actual MQTT publication period 5 seconds / listen interval 0 ms. Do not blame a hypothetical 500 ms receiver backlog as a current observation.
- Historical test board app0 starts at 0x10000, capacity 0x1c0000; normal repository partition layouts differ. Verify current target before upload; application-only upload, never casually write partition table/bootloader/NVS.
- `CURRENT_STATE` is summarized in the active plan and in [historical implementation evidence](implementation-follow-ups.md). Full CPU-crash attribution is still open.

## 3. MQTT reliability and immediate alarms

### Actual defect to repair

SBT main.cpp captures `now` before BLE event dispatch. BmsReadout.onEvent() obtains a later `millis()` and stamps a new sample. main.cpp then passes its older `now` with that newer snapshot into MqttService.health(). Unsigned subtraction can turn -1 ms into **4294967295 ms**. The exact huge age was seen live with BMS reason `ok`; concurrent availability was not captured. The code path was independently reviewed, not yet regression-tested.

SIL currently clears all source values when health fails or age exceeds 15000 ms. SBT still considers a source available for 30000 ms, sends scalars non-retained on delta (1 W / otherwise 0.1 units), health retained about every 5 s and a full valid-value refresh every 60 s. Cleared unchanged scalars can therefore remain missing until the full refresh. A mutex protects read/write access; no simple torn-read race was found. Separate MQTT topics are not an atomic whole-source frame.

### Required policy

- Setting **SBT Data Timeout (s)**, candidate key `sbtAgeS`, integer **1..9999**, default **180**. Validate stored and UI values; safe default on invalid persisted data. Use uint32 milliseconds with checked multiplication (9999000 ms fits).
- This deadline is NOT an alarm debounce. A received, validated real alarm/fault acts immediately according to its existing protection policy. No new wait equal to or double the timeout. Missing telemetry is **not** proof of an alarm. A previously known restriction is not cleared by silence.
- Keep last-good values, original source/session identity and measurement/receipt timing. Reject malformed candidate messages atomically without erasing or refreshing good data. Explicit unsupported/null values and authentic device faults are separate from malformed packets.
- During a recoverable gap within the configured deadline, preserve last approved strategy/ceiling without granting additional permission based on stale battery data; valid grid feedback may reduce demand. Do not immediately jump to MAX or zero just because one MQTT update is bad.
- Display **Old** only after the selected deadline; no age text requested in the compact card. Internal ages/reasons remain diagnostic. At 45 s an unchanged scalar is not old merely because it was not republished; advancing source-health samples keep the source current. Do not restart the age clock on retained replay or duplicate sequence.
- After expiry apply the configured Idle fallback where permitted (section 4), keeping stronger restrictions. Retain numeric values with Old instead of blanking them; startup/no received sample remains unavailable, never fake zero.
- Reconcile producer source availability with receiver timeout: SBT's existing 30 s `stale` is a collector acquisition diagnostic, not automatically a new alarm nor permission to erase SIL cache before its configured deadline. Keep acquisition status and consumer grace separate; do not extend MOS command readiness/protection sample limits to 9999 s. Explicit disconnect, restart/session change and fatal source errors must be distinguishable from temporary late samples.
- Require genuinely advancing samples for recovery, using the existing 5 stable seconds / >=2 samples qualification for normal control recovery only. An urgent alarm must bypass this qualification wait. Do not add a second recovery timer for the same decision.
- Only complete valid snapshots activate price/PV plans. Valid plan data may survive a 30-minute publisher outage and longer until its real expiry/coverage ends; do not impose an arbitrary new 30-minute expiry. Slot duration 900 s / horizon 36 h is unrelated to live-source freshness. Keep fast control, energy debit and restrictive actions running.
- Health age uses a timestamp captured AFTER copying the corresponding sample. Cover real uint32 rollover; do not hide true stale age by absolute-value or indiscriminate clamping.

## 4. Idle setting and control priorities

New **Idle Output (W)** setting, candidate key `LimiterIdleW`, default **500 W**, directly below **Max Output (W)** and above **Min Output (W)**. Actual household idle 630-680 W does not change the requested default.

Replace the normal BMS/Victron source-loss MAX release with direct `clamp(idleW,0,effectiveMaxW)` after applicable grace expires. Do not force configured MIN over Idle. Preserve reserve_source_loss=0, inverter OFF, transport-off, valid negative-price stop, normal missing-grid/AC zero and actual stronger protection restrictions. Existing intentional Limiter-OFF bypass is separate and unchanged; do not redefine all MAX branches. Max remains existing configured value (owner normally 900 W), not a new immutable hardware assumption.

The grid PID remains the existing demand controller; charge PI supplies a ceiling. Plan requests never force export. Full MOS protection coordinator remains outside this increment unless an explicitly already-approved baseline task is required; SIL currently locks MOS commands. Idle cannot guarantee safe unknown battery operation or a physical stop on controller failure.

## 5. Compact ESP visualization and I2C constants

- Keep Temperature live page/card behind build switch `SIL_DEACTIVATE_BME280_CARD` (1 = hidden, 0 = visible), default 1 in standard profiles. Compile out visualization-only registration when hidden, not the source implementation. Keep BME280 sampling, existing MQTT Temperature/Humidity/Dewpoint topics; pressure measurement and internal thermal consumers in both variants. This is independent of `FEATURE_BME280_ENABLED`, which still controls the actual hardware feature.
- Remove System live card. Put only **UsedHeap**, **FreeHeap**, **HeapFragmentation** into Limiter, using compact metadata. Preserve WiFi/System settings, OTA/NTP and separately used telemetry providers. Audit shared CM registration instead of disabling all system behavior blindly.
- Heap tuple must be sampled consistently; guard division by zero in fragmentation. Current CM built-in expression divides by getFreeHeap() without a guard; this is a separate extreme-memory candidate, not the proven nighttime crash.
- Hide BMS battery voltage/current, remaining/full Ah, min/max NTC display, min/max cell voltages, cell spread and cell count. Retain internal values and existing MQTT channels needed for planning/protection. Hide unsupported Victron PV voltage/current and battery power placeholders. Keep supported charger voltage/current and solar power.
- Publish and show **NTC 1 (Top)** and **NTC 2 (Bottom)** in original JBD sensor order. Do not infer position from minimum/maximum. Add topics `bms/temperature_1_c`, `bms/temperature_2_c`; retain current min/max topics. Missing sensor -> unavailable, no substitution. Physical labels can be swapped later, as the owner explicitly accepted. No extra device read query.
- Current SBT publisher has 24 descriptors; SIL receiver has 29 including legacy entries. Adding two channels gives 26 / 31; existing 32-bit masks still fit. Check arrays/indexes/contracts. Old HA cell/NTC counts may be cached: their presence does not prove fresh publication.
- Remove editable I2C settings; constants **SDA21 / SCL22 / 400000 Hz**. Preserve sensor operation. Optional OLED-only address must be reviewed if that profile is enabled; don't invent a new hardware address. Leave old NVS keys inert, do not erase NVS or repurpose them.

## 6. HA Shed forecast and actual energy

### Exact discovered entities

| Meaning | Entity |
| --- | --- |
| Inverter raw W | `sensor.tasmota_smartplug_12_energy_power` |
| Inverter raw cumulative kWh | `sensor.tasmota_smartplug_12_energy_total` |
| Inverter raw daily kWh | `sensor.tasmota_smartplug_12_energy_today` |
| Victron yield Wh | `sensor.sbt_victron_yield_today_wh` |
| Existing yield kWh wrapper | `sensor.sbt_solar_yield_today_kwh` |
| Victron PV W | `sensor.sbt_victron_solar_power_w` |
| Battery charged / discharged kWh | `sensor.sbt_battery_energy_charged` / `sensor.sbt_battery_energy_discharged` |
| Current-price source | `sensor.epex_spot_data_total_price_2`, `attributes.data` |
| Existing self-consumption helper | `sensor.solar_self_consumption_power`; formula must be inspected before reuse |

Home8: 1840 Wp / stored tilt30 / azimuth180, damping0/0, no inverter cap; config entry `8cf4575f65882f8089546283c402448e`, device `e6e85408a9b3372c2530ddc341a38420`, daily forecast `sensor.energy_production_today_6` and tomorrow `_6`. Home5 2:1150 Wp /25/180, damping0/0, no cap. Other historical entries are Home twice, Home1 and Home5. Owner says these were adjusted experimentally and authorizes replacing old forecasts.

Create one correct Forecast.Solar entry **Shed**, 8x230=**1840 Wp**, measured **14.8 degree** tilt, preserve qualified south azimuth180 and stored HA Home location. Location is populated in Settings -> System -> General -> Location -> Zones -> Home; exact private coordinates stay out of Git. Read back provider-stored location during migration; do not guess. For solar offer upstream of battery storage, do not clip forecast at the 900 W AC inverter. Start without arbitrary damping (0/0) and qualify later. Public tier likely supplies hourly data; verify actual response resolution and polling, never derive a fake 15-minute trace from a daily total.

Capture exact IDs/settings/automation consumers, create Shed, migrate consumers/Energy selection, verify, then delete the specifically inventoried obsolete Forecast.Solar entries. Delete no other integration or statistic history. Preserve a private rollback export. Creation before deletion avoids service gaps and lost location.

Current Energy PV source is inverter AC total: misses PV charging the battery and later includes battery discharge as new solar. Use ONE qualified Victron-yield statistic (raw Wh OR kWh wrapper), never both or inverter AC in addition. Keep inverter AC as delivery accounting and battery charge/discharge separate. DC/AC measurement-side mismatch and losses remain explicit.

October20 Victron history rose to about5.2 kWh, then reset near16:30 (visual estimate), later0.12 kWh. Reconcile reset-aware statistics/deltas and reset cause before using it as calibration reference or claiming exact day totals. Never use just the final counter/max after a reset. Do not alter recorder database or invent historical energy.

Owner requests corrected inverter W **max(0,rawW-2)**; invalid input remains unavailable. Integrate corrected power into a separate kWh delivery counter without bridging unavailable periods or changing raw counters. Live history confirmed2W from00:00-08:00 with raw energy increasing. Whether this is additive offset or unsigned standby import is not measured; retain the requested fixed2W as owner-selected approximation and flag it in acceptance. Never subtract2 from a kWh total. Do not also count corrected inverter output as PV beside Victron yield.

### Automation and MQTT contract

HA owns data retrieval, immutable pre-period snapshots, calibration and publication; SIL owns regulation. The owner explicitly authorizes creating the PV forecast automation and changing the existing price automation as necessary. Use an isolated package/reviewable export; no duplicate producers or HA actuator control.

Keep price v1 `{v,r,t,n,k,p}` with UTC900s slots, <=288 prices,24/chunk,768-byte packet and atomic activation. Current shared input default is `homeassistant/sensor/electricity_price/schedule`; installed automation and consumers must be inspected before changing topics. EPEX exposes multiple future days with no visible per-slot quality, so do not label all distant prices confirmed day-ahead.

Preferred PV extension uses compatible envelope semantics on configurable sibling topic, e.g. `homeassistant/sensor/solar_forecast/shed/schedule`, version2/kind pv; fields revision/start/duration/count/chunk, generated/expiry UTC, W scale1, <=144 slots (36h),24/chunk, raw offer, factor-thousandths default1000, source/quality and calibration age/sample count. Add per-slot missing/interpolated flags, reject conflicting chunks/old revisions, activate atomically. Binary only if measured benefit justifies explicit magic/version/little-endian/scaling/CRC/session/chunks; no Base64 requirement. Do not multiply already corrected forecasts twice or reuse price factor fcPriceF for PV.

For hourly ENERGY conserve Wh when distributing into four quarter-hours; mark hourly_disaggregated. Never fill missing periods or claim native15min precision. Preserve raw original forecast snapshots before target intervals. Calibration uses actual qualified Victron energy / original same-side forecast, median14-30 eligible days; candidate minimum14 days in30, forecast>=0.5kWh, candidate bounds0.5..1.2. Publish raw factor, count/quality/age; insufficient data neutral1.0 with uncalibrated state. Exclude curtailment/full battery/current limits/outages/reset-uncertain days. Do not infer module degradation from low energy.

Usable battery/household energy is separate from solar offer; respect MPPT current limit, battery voltage/SOC/headroom and qualified constraints. Retain unmet charge shortfall. One DC ledger reconciled to SOC; discharge efficiency once for already stored energy, roundtrip/wear/margin for prospective storage, no duplicate carried energy.

Budget target: PV double-buffer144x2x2=576 bytes, slot-quality72 bytes, metadata/bitmap136 =>784 bytes plus proven reusable parser scratch. Existing price buffers2304 bytes and JSON arena4096; TelemetryAge has a separate4096 arena. Reuse scratch only with proven sequential ownership; otherwise budget extra4096. Existing planner sort stack288 bytes. Compare REAL payload JSON/text/binary packet sizes, peak/min/largest-block heap, static RAM, stack and CPU under actual bursts. No unbounded String/JSON tree or silent MQTT buffer increase.

## 7. Acceptance and evidence limits

Focused host tests cover time ordering/rollover, packet validity/cache retention, timeout boundaries1/180/9999s, alarms immediate, missing-not-alarm, stale/session recovery, Idle priorities, NTC sensor identity, compact UI/MQTT preservation, HA snapshots/quality/reset accounting and DST. Compare flash/static/runtime memory on equivalent profiles.

For EACH firmware: identify isolated test target/profile, build, record hashes/dependencies/size, install application-only, verify boot and monotonic uptime, open all affected Live/Settings pages several times including reloads, observe >=300 continuous seconds after the last stressful interaction. Any panic/reboot/hang/partial response fails. Record screenshots/serial/panic/heap/HTTP results without credentials. A five-minute pass is a smoke result, not proof of no overnight crash. SBT no-data mode must still render unavailable safely; synthetic fixture tests provide data-path evidence but are not plant acceptance.

On failure stop progression, preserve matching ELF and complete trace, diagnose narrowly and repeat affected acceptance. No productive firmware upload. End with reviewable artifacts and handoff for owner deployment, not an automatic production rollout.