# Implementation follow-ups and consolidated legacy requirements

Date: 2026-10-17. Planning only; no firmware or device setting changed. The [Round 5 baseline](round-5-decisions-and-defaults.md) and [charge-target/PV-ceiling addendum](10-charge-target-and-pv-ceiling.md) govern implementation. This document preserves useful details from the superseded 2026-10-16 strategy draft and TODO entries without reopening accepted decisions.

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

## Implementation acceptance sequence

1. Finish/review the existing bounded SBT receiver increment using its own acceptance evidence. Do not confuse its mock tests with live BMS/Victron or automatic MOS acceptance.
2. Implement the approved baseline with existing CM callbacks and one output arbiter; retain hardware qualification gates, Hand/Auto ownership and the new charging ceiling. Remove the old smoother as part of that scoped change.
3. Replay price/energy traces without actuating hardware: evening/morning allocation, rapidly changing PV during high house load, reserve 2%/10%, expired prices, source-loss combinations, retained faults, restart and negative price with Limiter OFF. Check actual actuator-command tracking and immediate restrictive ceilings.
4. Under separately authorized hardware testing, qualify inverter zero/low output, charge/discharge restrictions, topology/MOS effects, fan/manual precedence, missing temperatures, loss of SIL, and command timing. Preserve manual access. Release the tested CM development revision only after its agreed HITL acceptance.

## Consolidation record

The old `energy-strategy-options-2026-10-16.md` discussion text was removed after consolidation. Its tentative 5% reserve plus 3-point hysteresis, `mosAuto`, derived-economic-db-only policy, unresolved fallback matrix and generic configurable post-PID asymmetric ramp are superseded. Current settings remain 2%/10%, Hand/Auto, price db/3 hysteresis, adjustable battery power band, and separate 50 W/s economic ceiling constants with the accepted bypasses. Historical Round 4 records stay explicitly historical.

Retain `docs/sbt-receiver-validation-2026-10-16.md` and `docs/superpowers/plans/2026-10-16-sbt-telemetry-inputs.md`: they document a distinct receiver implementation/test scope, including limitations. Retain `docs/cm-runtime-validation-2026-10-16.md` as dependency-upgrade evidence. Historical branch names in these evidence records describe the work at that time. This consolidation does not stage or accept their associated unfinished product code.
