# Implementation follow-ups and consolidated legacy requirements

Date: 2026-10-17. Current-price reserve and PV-following ceiling are connected to the existing PID/output path. Future-price energy scheduling and MOS actuation remain unimplemented. The [Round 5 baseline](round-5-decisions-and-defaults.md) and [charge-target/PV-ceiling addendum](10-charge-target-and-pv-ceiling.md) govern implementation. This document preserves useful details from the superseded 2026-10-16 strategy draft and TODO entries without reopening accepted decisions.

## Remove the legacy smoother, preserve the PID

Owner-approved cleanup: remove the old smoother control path and its exclusive settings, state, UI metadata and serialization during implementation. Preserve the proven grid PID and its tuning; the economic ceiling ramps and charge-loop PI correction are separate functions. Check all consumers before removing shared helpers. Define migration/handling of old persisted settings instead of changing storage keys silently. Compare equivalent-build flash, static/runtime RAM and payload sizes before claiming resource savings. Validate the PID path, bypass/stop precedence and removal of stale UI controls. This documentation change does not remove source code.

## Inputs and energy accounting

- The owner reports 1.8 kWp PV, a 14s LG M29 battery and an inverter currently limited to 900 W. Qualify actual inverter rating, low/zero output and loss-of-command behavior. Neither 14s nor Ah capacity establishes permitted pack current.
- The battery currently charges only through PV. Tasmota Smartplug 12 measures the combined PV/battery inverter AC output; never add battery discharge to that AC measurement again. Signed grid power is positive for import; signed BMS power is positive for charging.
- The historical BMS full-capacity reading was 131.25 Ah; HA's 6743.62 Wh used 51.38 V nominal voltage. These are nominal/reference estimates, not measured usable energy. Retain the last qualified capacity with explicit age/quality and reconcile one energy pool; do not spend both SOC energy and a second carried allocation.
- Use the actual Victron charger DC output for the charge/PV ceiling. Curtailed MPPT output does not reveal unconstrained array potential. Qualify full-battery/charge-limit behavior without assuming Victron write support or unapproved probing.
- Reconcile receiver freshness when implementing the new policy: the earlier receiver contract used 15 s, whereas Round 5 plans BMS/Victron age limits of 30 s. This is an explicit code/contract change to validate, not evidence that current code already uses 30 s. Retained receipt alone never refreshes a sample.

## Compact price transport and economics

Retain these implementation constraints from the older proposal; the exact encoding remains to be chosen and measured:

- Bounded maximum 72-hour cache, with 36-hour initial active look-ahead. A candidate representation has 72 hourly values plus 32 quarter-hour values for the first eight hours. Fine slots replace overlapping coarse slots: 96 active intervals, not 104 overlapping allocations. Preserve explicit published/forecast/missing quality.
- Use UTC start/duration and Europe/Berlin only for presentation; test 23/25-hour local days, clock uncertainty, gaps, overlap, expiry and revisions. Never fabricate later days by repeating today's prices.
- Bound count, bytes, numeric ranges and validity. The earlier receiver context recorded a 1024-byte MQTT buffer; recheck actual capacity before implementation. 104 four-byte values alone cost 416 bytes before timestamps, quality and framing. Prefer one bounded coherent snapshot if it fits, otherwise bounded chunks with a shared revision and atomic activation. Do not silently enlarge buffers or persist each refresh to flash.
- HA/Rabot is the initial price-source candidate. Define the marginal household tariff and settlement interval, and document whether the existing negative-price signal means exchange price or household price. Preserve the existing override until that contract is intentionally changed. Cached valid prices may outlive their publisher, but cannot substitute for live plant measurements.
- Keep price-class comfort/db and derived db/3 hysteresis as accepted. Efficiency, wear and required benefit are separate economic inputs, not a replacement for that price-class db. For new storage, compare forgone direct use/export against later delivered value after losses and wear. For already stored energy, do not charge sunk acquisition losses or cycle costs a second time. Display derived price differences as 0.00 EUR/kWh; a total EUR benefit also needs an energy amount. Do not use rounded display values for decisions.
- The earlier 15% SOC / EUR 0.39 rising to 0.42 / 1358 W load / 27 W PV example remains a replay case, not a 400-500 W fixed command. Support duration, usable capacity and reserve determine the ceiling.

No external optimizer, price PID, per-slot dynamic object tree or direct ESP cloud/TLS client is required for the first implementation. Optional HA-provided load profiles or a learned base load remain later options; start with the accepted 650 W fallback and actual demand feedback.

## HA, cooling and later capabilities

- Expand the SolarLimiter card only as corresponding telemetry is implemented: selected/effective strategy (charge, discharge, hybrid, preserve), decision/inhibit reason, requested/sent/measured output, active ceiling, reserve state, allocated evening/morning energy, price quality/next transition, source ages and fan reason. Clearly show when intentional limiter bypass or telemetry-loss MAX prevents an economic reserve guarantee.
- Use the existing overall MOS gate plus the separate Hand/Auto selector (`mosMan` candidate), not the superseded `mosAuto` contract. Display accepted mode, SIL protective request, actual MOS feedback and blocked/pending/failed status separately. Place actual charge/discharge MOS feedback beside/below its Allow control. HA sends desired settings and displays accepted state; a dashboard alone cannot enforce ownership. No request is not ON.
- Owner manual HA Allow acceptance on 2026-10-16 remains valid. Keep manual access available while automation is unfinished; this does not qualify automatic fault recovery. Wavering readback is accepted for now; overload is only a hypothesis. After isolated UART migration, compare stability, source age, free/minimum heap, stack headroom and cycle time. Distinguish runtime BLE disablement from excluding its stack in a build; measure savings.
- Add the native BMS cycle count through SBT/MQTT/HA. Verify the JBD basic-information response (0x03) field and reset semantics without adding a redundant query. Native cycles, equivalent full cycles and MOS toggles are different quantities.
- Combine independent battery and BME280 fan demand as planned. The current relay stays fan-only. Battery updates/freshness must trigger cooling evaluation independently of ambient updates. Qualify fan power while the inverter is off and summer intake temperatures of 35-40 C; airflow cannot cool below intake temperature.
- Keep a separate future grid-charger capability absent/disabled with no guessed GPIO. Later qualification covers boot-OFF, polarity, rated charger/contactor, power/current, target SOC, temperature inhibition, feedback/timeouts, failed-contact behavior and charge/discharge interlocks. Avoid routine charger-inverter circulation. Add grid-charge energy metering and revise today's PV-only HA savings/provenance assumptions when installed.

## Current HA MQTT publication contract

The firmware publishes the cached planning settings through the existing `MQTTManager::publishExtraTopicLazy()` path at the configured `MQTTPubPer` interval. The non-retained JSON message is sent to `<resolved MQTT publish base>/Planning/Config` with this compact schema:

| Key | Meaning | Unit / values |
| --- | --- | --- |
| `v` | Payload schema version | `1` |
| `valid` | Cached settings pass basic range checks | Boolean |
| `resOff`, `resOn` | Discharge reserve thresholds | % SOC |
| `pwrDb` | Signed battery-power direction band | W |
| `mosMan` | MOS mode setting | `true` = Hand, `false` = Auto; does not change MOS ownership or send commands |
| `invEn` | Independent inverter enable setting | Boolean; does not stop or enable the inverter |
| `priceC`, `priceDb`, `hystDb` | Price comfort, deadband, and derived deadband / 3 | EUR/kWh |

Existing `<base>/SetValue` (sent inverter target), `<base>/CalculatedValue` (calculated controller output), and `<base>/GetValue` (signed grid input) publications keep their current meanings.
A second non-retained payload at `<resolved MQTT publish base>/Planning/State` uses this initial state:

`{"v":1,"strategy":null,"reason":null,"limitW":null,"chargeW":null,"reserveOk":null,"allocWh":null}`

Uncomputed strategy, reason, ceiling, charge target, reserve permission, and allocation are explicitly `null`, never synthetic zero or healthy defaults. This preparation does not republish SBT measurements or publish computed strategy, reserve state, control reasons, charge targets, energy allocations, active limits, or protection/fan results; the State fields are placeholders because those runtime decisions are not implemented. MQTTManager's existing `<base>/System-Info/status` retained `online` / `offline` last-will remains the availability signal.
## Implementation acceptance sequence

1. Finish/review the existing bounded SBT receiver increment using its own acceptance evidence. Do not confuse its mock tests with live BMS/Victron or automatic MOS acceptance.
2. Implement the approved baseline with existing CM callbacks and one output arbiter; retain hardware qualification gates, Hand/Auto ownership and the new charging ceiling. Remove the old smoother as part of that scoped change.
3. Replay price/energy traces without actuating hardware: evening/morning allocation, rapidly changing PV during high house load, reserve 2%/10%, expired prices, source-loss combinations, retained faults, restart and negative price with Limiter OFF. Check actual actuator-command tracking and immediate restrictive ceilings.
4. Under separately authorized hardware testing, qualify inverter zero/low output, charge/discharge restrictions, topology/MOS effects, fan/manual precedence, missing temperatures, loss of SIL, and command timing. Preserve manual access. Release the tested CM development revision only after its agreed HITL acceptance.

## Consolidation record

The old `energy-strategy-options-2026-10-16.md` discussion text was removed after consolidation. Its tentative 5% reserve plus 3-point hysteresis, `mosAuto`, derived-economic-db-only policy, unresolved fallback matrix and generic configurable post-PID asymmetric ramp are superseded. Current settings remain 2%/10%, Hand/Auto, price db/3 hysteresis, adjustable battery power band, and separate 50 W/s economic ceiling constants with the accepted bypasses. Historical Round 4 records stay explicitly historical.

Retain `docs/sbt-receiver-validation-2026-10-16.md` and `docs/superpowers/plans/2026-10-16-sbt-telemetry-inputs.md`: they document a distinct receiver implementation/test scope, including limitations. Retain `docs/cm-runtime-validation-2026-10-16.md` as dependency-upgrade evidence. Historical branch names in these evidence records describe the work at that time. This consolidation does not stage or accept their associated unfinished product code.

## Current-price and PV-following implementation slice (2026-10-17)

- Active code integration now connects current-price cheap/comfort/expensive classification (db/3 hysteresis), reserve OFF/ON latch (defaults 2%/10%), charger DC feedforward and battery-power PI ceiling to the existing house PID. The PID receives the dynamic upper bound and a minimum that can fall below configured MIN. There is no future-price planning or MOS switching.
- Initial PI candidates: Kp = 0.20, Ki = 0.03/s. Positive battery power means charging; configured power deadband supplies the positive charging margin. Integration uses new BMS sequences and conditional anti-windup. PV ceiling rises and expensive-mode release rise at 50 W/s; preservation/reserve/PV-loss reductions bypass downward ramps. Full economic allocation and its downward ramp remain outside this slice.
- The SBT snapshot uses existing freshness checks. Existing CM receive mappings supply grid/inverter values (10 s freshness) and current price (20 min candidate freshness), with values and timestamps copied under a short critical section. No extra subscriptions or receive settings are created.
- Direct decisions reset PID. Negative-price override cannot exceed the active ceiling; an inverter-off decision stays at zero. Issued commands are tracked only with RS485 enabled, without claiming inverter acknowledgement. Existing Planning/State publication now carries actual decisions; allocWh remains null.
- The user built and flashed the integrated state: Flash 95.0% (1245113 / 1310720 bytes), with `-DCM_ENABLE_LOGGING=1` and `-DCM_ENABLE_VERBOSE_LOGGING=1` enabled. This confirms the user-reported build/flash, not hardware behavior or tuning.
- The user reported loop logging works when both logging flags are enabled; verbose logging alone did not produce the expected output. New per-RS485-tick control and source diagnostics were added after that build and have not been built or flashed by the agent.
- No tests, builds, uploads or Git operations were performed by the agents. Physical zero-discharge and the full protection coordinator are not implemented guarantees.

### Runtime diagnostics and resolved logging setup

Diagnostics require both logging flags and use the existing visible Info sink (the separate SIL Trace sink is filtered by setupLogging). The `verboseLogging()` function runs from the RS485 control path, not a timer callback: the full CTRL-IN/CFG/PRICE/OUT/CMD, BMS and VICTRON block is emitted on the first cycle and then at the configured MQTT publish interval, with a 10 s minimum. It remains available while MQTT is disconnected. Blocks have separators and include source validity/freshness, measured grid/inverter/BMS/charger power, calculated/requested output, ceiling, reserve and settings. `GetValue` is signed grid import, not inverter output.

Compact CTRL-STEP diagnostics immediately record reason or price-band changes and entry into zero/configured MAX, including input validity and power at that cycle, so short-lived decisions are not lost between full blocks. Each mock command also includes its reason, active price band and ceiling. `meters_missing` stops at zero; `source_loss` selects configured MAX unless a blocked reserve is latched (`reserve_source_loss`, zero); `recovery` holds zero during qualification. `expensive` may reach MAX through the economic ceiling and house PID; it does not itself imply source loss. No cause is inferred from opening a web page.

The active hysteresis-aware band is reported as `cheap`, `comfort`, `expensive` or `unknown`, separately from the existing negative-price signal. CTRL-PRICE includes cheap/expensive entry thresholds and hysteresis in EUR/kWh. With comfort 0.25 and deadband 0.06, entry thresholds are 0.19/0.31 and hysteresis is 0.02; an expensive band persists while price is strictly above 0.29. PV voltage/current and Victron battery-power channels remain marked unsupported. This logging refactor has only been statically inspected, not built or tested by the agent.

The owner's current configuration uses resOff=2 and resOn=8; these runtime settings were not changed. Diagnostic snapshots are copied under the existing SBT mutex and formatted after release, and are omitted when either logging flag is disabled. Invalid readings are printed as nan/NA; lastAttempt is not an inverter acknowledgement. No control behavior or MQTT payload fields were changed for this diagnostic addition.


### Test ESP: mocked transport with real telemetry

`[env:testesp]` now defines `SIL_MOCK_TRANSPORT=1` instead of `SIL_BENCH_TEST` and explicitly enables both logging flags. Do not add `-U` for these flags: PlatformIO appends `-U` options after all `-D` definitions, so the flags fell back to CM's default `0` and removed all verbose diagnostics from the first mock build. The former startup code that disabled MQTT and RS485 settings was removed. Existing NVS/network settings are preserved; MQTT must already be enabled with the isolated test topic/client identity.

Only transport availability is simulated for the controller. Source freshness, reserve, price logic, inverter enable and all other existing checks remain in effect. Commands update a separate mock-held value and emit `[MOCKED!]`; real issued-command state remains untouched. Planning/State carries `mock:true` for computed mock decisions, and CalculatedValue/SetValue describe simulated demand in this build. Every RS485 initialization, send and receive entry point returns without hardware access in a mock build, even if Enable RS485 is turned on.

This is a live-input dry run, not a simulated battery/inverter plant: measured power and SOC still come from the real system, so calculated commands do not affect the feedback. It qualifies decisions and logs, not closed-loop response. The owner subsequently reported a successful testesp build/flash (RAM 64288/327680 bytes, flash 1217761/1310720 bytes) and mock command logs. One abort following a WiFiClient write error remains unexplained; no crash fix or closed-loop acceptance is claimed. No agent tests, builds, uploads or Git operations were run.

A later abort during a web page reload was decoded against the matching ELF as `operator new` failure in `AsyncWebServerRequest::beginResponse` from a CM API route: heap allocation failure in the async web task, not in control code. To reduce loop-time heap churn, Planning payloads are formatted on the stack (unchanged JSON), MQTT publish checks run at most every 250 ms, and the publish base topic is re-resolved at most every 5 s. `TelemetryAge` no longer allocates per matching MQTT message: payload/path handling uses stack buffers, JSON is parsed with a filter for the configured path into a fixed 4 KB static arena (+4 KB static RAM), and arena exhaustion marks that sample invalid. Topic matching already compared CM's `const String&` without copying. Conversion semantics (strict numbers, integer truncation, 1024-byte limit, `none` path) are unchanged, except non-JSON payloads above 47 characters and paths above 63 characters or 16 segments are rejected. Verbose diagnostics add a HEAP line (free, minimum free, largest allocatable block) and the largest block in CTRL-STEP. These changes are not yet built or verified.
