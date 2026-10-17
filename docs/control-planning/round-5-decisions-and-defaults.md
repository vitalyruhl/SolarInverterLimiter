# SIL control planning - Round 5 decisions and initial constants

Date: 2026-10-17. Owner-approved first-design baseline; planning closed. This incorporates the owner's Round 5 replies and the latest clarification. It supersedes conflicting Round 4 proposals; the Round 4 ledger and independent review remain historical evidence. This is documentation, not firmware, device configuration, or a passed HITL test.

## Confirmed control ownership

- Keep `switch.sbt_bms_control_enabled` as the existing overall write gate. OFF blocks both manual and automatic MOS commands.
- Add a separate Hand/Auto control. Proposed compact internal key: `mosMan`; true = Hand, false = Auto. In Hand, manual control wins and SIL only publishes its requested protective action. In Auto, SIL may operate MOS only for fault handling, never for price scheduling or SOC reserve.
- Enforce ownership in SBT at command admission and again before transmission, not just in HA. Commands need an identifiable origin. HA shows mode, SIL request (including no request), actual MOS states, and blocked/pending/failed status separately.
- Changing back to Auto explicitly returns control to SIL and triggers fresh evaluation, not replay. No request is not an ON command. Automatic release is limited to a still-owned fault block with valid release evidence; a later manual command invalidates that ownership.
- Retain the overall gate, selected mode, and necessary manual/fault ownership across restart. Suggested initial mode is Hand. Never persist a queue of commands to replay. MQTT failure does not imply a physical MOS transition and prevents new MQTT commands from reaching SBT.
- Existing `BmsControl` is not repurposed. Current firmware gates all commands with it; `mosMan` and command-origin arbitration are planned additions.

## Recovery and protection arbitration

Use a shared startup/recovery path: evaluate retained faults -> establish current measurement validity -> determine price status or fallback -> recalculate -> give a bounded request to the existing controller. Request inverter 0 W during initial qualification. Five stable seconds and at least two new source samples are required; an old retained MQTT message is not a new sample. Recovery of communication does not itself clear a protection fault.

Persist latched fault/manual state on transitions only. Unknown or invalid stored state starts as unknown, not healthy. Fresh source/session evidence is needed before release. A zero request is not a guaranteed physical stop if SIL or the command transport has failed.

Protection always runs, including inverter OFF and Limiter OFF. Combine both directions and all active restrictions into one final decision. Normal missing-grid/AC fallback with healthy BMS is 0 W/store; combined BMS and grid/AC loss is 0 W; BMS/Victron-only loss with usable grid/AC requests configured MAX unless a known protection restriction applies.

Owner-approved exception: with missing grid/AC measurement, a charge warning may raise inverter output only while fresh BMS evidence confirms reduced charging, no discharge restriction applies, and inverter OFF/negative-price stop is absent. Missing grid telemetry is distinct from loss of grid voltage. The response must stop increasing if it starts additional battery discharge. Requested protective discharge blocking must reduce inverter output even if a MOS command is denied or fails. Inverter OFF and valid negative-price stop still request zero. Unresolved charge/heat mitigation raises the HA alarm and requests fan ON; no new charger actuator or guaranteed physical charge cutoff is assumed.

The protection request, available measurements, ownership, and pending commands are rechecked before each physical write. A new urgent restriction supersedes an older request. Use existing SBT correlation/expiry facilities where applicable; do not create conflicting independent retry loops.

## Price-dependent ceiling, not a forced output

Confirmed semantics: price planning calculates a maximum permitted PID output. The PID may request less according to actual demand. A very expensive price may permit up to the configured inverter maximum (currently discussed as 900 W), but never overrides reserve, directional protection, the overall gate, OFF/negative-price zero, or device capability. It does not intentionally discharge the battery merely to export surplus.

Current-price fallback when future prices are unavailable:

| Class | Entry at default values | Battery behavior |
| --- | --- | --- |
| Cheap | Price <= comfort - db = EUR 0.19/kWh | Prefer charging; no economic discharge allocation |
| Comfort | Between cheap and expensive, subject to hysteresis | Use available PV; preserve battery |
| Expensive | Price >= comfort + db = EUR 0.31/kWh | Distribute usable battery energy over a three-hour support window |
| Very expensive | Price >= comfort + 2*db = EUR 0.37/kWh | Lift the time-budget ceiling up to otherwise allowed PID maximum |

Initial comfort = EUR 0.25/kWh; db = EUR 0.06/kWh. Agent-selected initial transition hysteresis is `db / 3`, currently EUR 0.02/kWh, with no additional setting. Enter expensive at 0.31, leave at or below 0.29; enter very expensive at 0.37, leave at or below 0.35. Enter cheap at 0.19, leave at or above 0.21. Evaluate the highest qualifying price class first and use current thresholds for a new startup state. Reject a negative db; db zero yields zero hysteresis. Negative-price stop has separate priority.

The three-hour fallback window is anchored when entered. Thirty-minute replans must not repeatedly move its end three hours into the future. Debit real battery use, recompute from remaining time, and carry only genuinely unused allocation. At window end evaluate a new window from the remaining usable energy. If both current and future prices are unavailable, the initial fallback preserves battery while using measured PV; display unavailable pricing. Invalid battery energy is not replaced by a fabricated budget; apply the applicable measurement/protection policy.

The earlier extra EUR 0.10 / 0.35 hold threshold is not an additional active setting in this initial baseline. Future-price planning must reserve energy for a later target window and release it within that window. Whether it uses a separate hold threshold remains a later tuning decision; no extra price parameter is silently introduced.

## Initial constants and settings

Owner-selected values are marked explicitly. Other numbers below are agent-selected commissioning candidates, not measured optimal values or qualified hardware protection settings. Centralize typed constants so optional CM settings can override selected defaults later if flash, heap, JSON size, and runtime latency permit. Do not load settings during each control cycle.

| Item | Initial value | Meaning |
| --- | --- | --- |
| Discharge OFF SOC | 2%, adjustable setting | Economic discharge floor; not a substitute for cell protection |
| Discharge ON SOC | 10%, independently adjustable setting | Resume economic discharge; OFF/ON form the hysteresis |
| Price transition hysteresis | db / 3 | Derived, no extra setting |
| Expensive fallback support window | 3 hours (owner-selected) | Remaining energy divided by remaining anchored window time |
| Full replanning | 30 minutes plus material events | Current 15-minute price boundaries still apply on time |
| Look-ahead / planning load | 36 hours / 650 W | Initial planning assumptions; unavailable future prices/PV are not invented |
| BMS / Victron stale age | 30 seconds each | Matches current SBT availability limits; requires measurement age, not merely MQTT receipt age |
| Grid / inverter AC stale age | 10 seconds each | Initial assumption for frequently updated meters; confirm actual reporting cadence before enabling control |
| Ambient temperature stale age | 60 seconds | Initial assumption; must exceed qualified normal sampling interval |
| Recovery | 5 seconds and >=2 new samples | Fixed qualification constant; never clears a fault by itself |
| Pending command evaluation | 1 second | Nonblocking timer; urgent fault actions are event-driven and do not wait for this tick |
| Automatic blind MOS retries | 0 | Keep existing command expiry/correlation; qualify cancellation of already transmitted writes |
| Sustained fault notification reminder | 5 minutes | Initial HA policy; notify on entry, deduplicate repeats, and report clear once |
| Battery fan / ambient fan | ON 40 C / ON 35 C, hysteresis 3 K | OR demands; expected sensor loss requests ON; protection demand overrides normal fan OFF |
| Battery power direction band | 50 W initial default, mandatory adjustable setting | Schmitt classification on battery DC power; qualify actual noise and timing |
| Economic ceiling ramp up / down | 50 W/s / 50 W/s | Separate constants; optional separate settings if resources permit |
| Inverter publication cadence | Existing RS485 Publish Period (s), currently 2 s per owner | No extra 10 W command-change threshold |
| Discharge efficiency / round-trip efficiency | 0.90 / 0.90 | Separate provisional estimates, not multiplied together for the same already-stored energy |
| Wear / required economic benefit | EUR 0.02 / 0.03 per delivered kWh | Initial constants, separate from price-class db |

For first commissioning, use these additional conservative SIL supervisory temperature/voltage candidates only after confirming sensor identity, calibration, and the installed BMS limits:

| Signal and affected direction | Warning | Main protective request | Release hysteresis |
| --- | --- | --- | --- |
| Battery high temperature, charging | >=40 C | >=45 C: block charging request | 3 K below each threshold |
| Battery high temperature, discharging | >=50 C | >=55 C: block discharging request | 3 K below each threshold |
| Battery low temperature, charging | <=5 C | <=2 C: block charging request | 3 K above each threshold |
| Battery low temperature, discharging | <=-10 C | <=-15 C: block discharging request | 3 K above each threshold |
| Highest individual cell voltage | >=4.10 V | >=4.15 V: block charging request | 0.05 V below each threshold |
| Lowest individual cell voltage | <=3.10 V | <=3.00 V: block discharging request | 0.05 V above each threshold |

These are deliberately conservative agent proposals for SIL, not manufacturer-specified pack limits and not permission to rewrite BMS thresholds. A lower-temperature warning requests reduced current in the affected direction; warming the battery by deliberate discharge is not assumed. The BMS's own protection remains active and takes effect independently. Do not apply cell-temperature thresholds to an unidentified BMS power-stage or charger sensor.

The LG M29 specification gives standard charge voltage 4.20 V, normal charging from 0 to 50 C, limited-current charging from -10 to 0 C, and discharge operation from -30 to 60 C. The initial SIL plan does not use the subzero charging allowance because the required pack current qualification is absent. Source: [LG INR18650M29 specification, Rev. 0, page 4, distributor-hosted manufacturer document](https://www.nkon.nl/amfile/file/download/file/211/product/3501/).

No numeric pack-current, charger-stage, or BMS-power-stage protection limits are invented from the model name or Ah capacity. Start from the actual device alarm flags and approved installed ratings; verify those before claiming an additional software current/temperature trip. Physical protection and sensor qualification cannot be replaced by tuning during normal operation.

## Integration and remaining verification

- Use the newest suitable unpublished CM development revision, pinned explicitly for each test build. Verify and fix warning/main-alarm callbacks, hysteresis, invalid-source handling, and timing there where generic; SIL owns control policy.
- Release CM only after the agreed hardware-in-the-loop acceptance evidence passes. No such acceptance is claimed by this document.
- Keep one energy accounting reference side and never add carried allocation to a newly calculated SOC pool. Fresh SOC reconciles the same pool; replanning replaces allocations.
- Runtime checks must verify the Hand/Auto source arbitration, fault retention across restart, actual mode feedback, urgent command supersession, power signs/timing, PID anti-windup, and notification behavior.
- Device-side inverter behavior on complete SIL failure remains to be measured/qualified. Until then, document the last setpoint as potentially retained; do not claim a working watchdog or emergency stop.

## Final planning acceptance and stabilization

The owner accepted this first design and closed planning on 2026-10-17. Remaining hardware/API qualification and numerical tuning are implementation acceptance work, not evidence of a deployed controller. The accepted plan does not authorize flashing devices or claim an already passed HITL test.

### Reserve state

Provide two directly adjustable settings: discharge OFF at SOC <=2%, discharge ON at SOC >=10%. Reject configurations unless 0 <= OFF < ON <=100. This is an eight-percentage-point hysteresis at the initial values; do not add a third reserve-hysteresis setting. Between thresholds retain the prior economic discharge permission. On restart use a qualified retained reserve state; when unknown, keep economic discharge inhibited until release evidence exists. This state does not command MOS and does not prevent qualified direct PV use.

### Adjustable battery power band

The battery charge/discharge direction band must be an exposed setting, not a resource-dependent optional setting. Proposed compact key: `pwrDb`, unit W referred to signed battery DC power, initial agent-selected default 50 W. The owner's reported inverter error of about 15 W motivates commissioning margin but is not a calibrated DC noise measurement. Use fresh time-aligned source data and qualify this default under temperature and load changes.

Suggested Schmitt classification: enter charging at >=+pwrDb and leave charging at <=+pwrDb/2; enter discharging at <=-pwrDb and leave discharging at >=-pwrDb/2. A strong opposite-direction sample can switch directly; otherwise retain the current state until its release threshold, then use neutral. Derive the half-band; do not add another setting. Validate pwrDb as positive. This is classification stability, not permission to ignore real energy flow: integrate valid signed battery power including values inside the band. Hard protection uses its qualified thresholds and cannot be suppressed by this band.

### Transmission cadence and ramps

Do not add the previously suggested 10 W command-change suppression. Reuse the existing `RS485 Publish Period (s)` setting, owner-reported as currently 2 seconds. Keep the latest permitted request and recheck priority/limits at transmission. This publication cadence and the proposed one-second pending-feedback evaluation are different clocks.

Use two independent initial code constants for the economic ceiling:
- upward rate: 50 W/s;
- downward rate: 50 W/s.

Expose them as two optional lightweight settings if measured ESP resource headroom allows; do not couple the two rates merely because defaults match. Calculate each permitted change from actual elapsed time, not loop count. At a regular two-second publication interval, a 50 W/s ceiling ramp corresponds to at most 100 W of ceiling change per interval. The PID may request less; this does not promise that the physical inverter tracks that slope exactly.

Ramp the normal economic ceiling, not every change in measured household demand. A sudden load decrease or falling charge/PV ceiling may immediately lower the requested output. Reserve reached, directional protection, explicit inverter OFF and valid negative-price stop bypass the economic ramp and impose the new restriction at the next permitted command opportunity. Never delay such a request solely to finish a ramp. Actual RS485 scheduling, inverter latency and whether urgent transmission is supported remain HITL checks; no instant physical shutdown is claimed.

The effective hard limit is applied after economic smoothing. Maintain anti-windup/tracking against the final, actually transmitted/held actuator command, with the installed PID's sign and scaling conventions. Feeding back only the nominal economic ceiling does not account for ramping, command arbitration, or the held command between publications. Account for both clipping and communication timing. The accepted charge-loop PI correction below supplies a ceiling to the same arbiter; it never writes an independent competing actuator command.

Hysteresis, ramps, and timing have different roles: use SOC thresholds, price db/3, temperature hysteresis, and the adjustable power band in their respective units. Do not apply a generic 5-10% deadband to every signal, and do not use broad filtering to mask a poorly tuned PID.

## Accepted addendum: charge target and dynamic PV ceiling

The owner accepted the additional [charge-target and PV-ceiling subprogram](10-charge-target-and-pv-ceiling.md). Zero economic discharge allocation alone is insufficient when household demand remains high and PV falls. Distinguish preserving the battery from actively building an energy allocation for a later expensive window.

Use measured Victron charger DC output as feed-forward and signed BMS battery power as feedback for a small charge-loop PI correction. This loop supplies an inverter ceiling, not a second independent actuator command. Combine applicable ceilings with household demand in one arbiter. Falling available PV bypasses the normal economic downward ramp; upward recovery remains bounded. Both controllers need anti-windup/tracking against the selected and actually transmitted/held command.

The owner explicitly accepts that strictly preventing all battery discharge is impossible with this hardware and sampled control path. Measurement delay, the existing RS485 publication period, communication delay and inverter response can cause brief discharge after a PV drop. Normal storage/preservation does not switch MOS. Neither this control design nor a zero request guarantees instantaneous zero battery current; independent BMS hardware protection remains unchanged. This acceptance does not waive the existing source-validity or protection rules.

## Accepted implementation cleanup: retain PID, remove legacy smoother

Remove the obsolete smoother control path and its exclusive settings, state and UI/serialization fields during implementation. Keep the existing, owner-accepted grid PID and its established tuning/behavior. The new economic ceiling ramps and charge-loop correction are separate planned functions, not a reason to retain the old smoother. Remove shared helpers only if all remaining consumers are accounted for. Measure flash, RAM and payload changes before claiming savings; this document does not change firmware. See [implementation follow-ups](implementation-follow-ups.md).
