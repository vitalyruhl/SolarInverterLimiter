# SolarInverterLimiter TODO

The [control-planning baseline](control-planning/README.md) owns battery-control decisions. This file tracks implementation work; it does not define a second strategy. [Implementation follow-ups](control-planning/implementation-follow-ups.md) retain details consolidated from the older strategy/TODO drafts. Firmware implementation and hardware acceptance are still separate.

## P1 - Complete the existing telemetry receiver increment

- [CURRENT] Review and finish the existing uncommitted SBT receiver/UI work against its implementation and acceptance evidence. The 5.1.3 emergency-only path uses the shared SBT gate; physical acceptance remains pending.
- [NEXT] Reconcile the receiver's earlier 15-second freshness contract with the planned BMS/Victron policy; do not assume the planning constants are already deployed.

## P1 - Implement the approved battery control plan

- [NEXT] Preserve the proven grid PID; remove the legacy smoother and only its exclusive settings/state/UI fields. Validate persisted-setting handling and measure flash/RAM/payload savings.
- [NEXT] Implement protection-first arbitration, shared startup/recovery, explicit inverter enable, existing limiter bypass and negative-price zero.
- [NEXT] Implement the bounded energy/price planner, reserve OFF/ON 2%/10%, adjustable battery power band and charge-target/PV ceiling. Use the current baseline for price classes, source-loss precedence and ownership.
- [NEXT] Add separate economic ceiling ramp constants, initially 50 W/s up and down, optionally exposed later if resources permit. Do not ramp every demand-PID response; restrictive PV/load/protection/stop changes bypass the economic ramp. Reuse RS485 publication timing and actuator-command anti-windup tracking.
- [CURRENT] Validate the shared SBT gate and one-shot emergency both-off path. The earlier separate Hand/Auto proposal is superseded; retain `mosMan` only for stored-setting compatibility. No economic/reserve MOS switching or automatic ON.
- [NEXT] Use the qualified CM development revision and warning/main-alarm callbacks. Record exact test versions; publish a release only after actual agreed HITL acceptance.

## P1 - HA telemetry and cooling

- [NEXT] Expand the SolarLimiter card with selected/effective strategy, decision reason, desired/sent/measured power, active limits, reserve/allocation, price quality/next transition and source ages as implemented.
- [NEXT] Show the shared MOS gate, SIL emergency request, actual MOS feedback and pending/blocked/failed status distinctly. The Allow controls follow actual MOS feedback.
- [NEXT] Add independent battery-driven fan demand alongside BME280, using the accepted OR/hysteresis/failure policy. Keep the existing relay fan-only.
- [NEXT] Decode native JBD cycle count in SBT, verify its meaning, publish it, and add it to the HA BMS card without redundant polling.

## P1 - Qualification and UART transition

- [COMPLETED] The owner accepted manual HA charge/discharge Allow operation on 2026-10-16. This does not qualify automatic MOS recovery.
- [NEXT] Keep manual access available while SIL automation is unfinished. Compare telemetry stability, source ages, heap, stack and cycle time after the separately qualified BMS/Victron UART migration. Existing readback fluctuation is accepted; its cause and resource gains remain unproven.
- [NEXT] Execute the replay and hardware-acceptance scenarios in the [follow-ups](control-planning/implementation-follow-ups.md#implementation-acceptance-sequence), with hardware actions separately authorized.

## P1 - Independent firmware-size work

- [NEXT] Evaluate replacing Adafruit SSD1306 + Adafruit GFX while preserving the current mostly text display and frame. Older unmeasured estimates were 12-16 KB flash and 1024 B RAM for a text-only library, or 6-10 KB flash for a lighter graphics/text library. Establish comparable builds before accepting any saving.
- [LATER] Evaluate embedded ConfigManager WebUI size separately; do not combine that larger change with the battery-control implementation by default.

## Later - Grid charging and optional planning inputs

- [LATER] Prepare a separate absent/disabled grid-charger capability; never reuse the fan relay or choose an unverified pin. Qualify charger hardware, interlocks and metering before activation; update HA energy provenance when grid charging is introduced.
- [LATER] Consider compact HA-supplied load profiles or an external optimizer only if measured benefit justifies the extra complexity. Neither is required for the first controller.
