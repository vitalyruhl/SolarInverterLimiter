# SIL Control Planning - Round 4

Historical Round 4 snapshot. See [Round 5 decisions and defaults](round-5-decisions-and-defaults.md) for later resolutions and current policy.

Status: 2026-10-17. These are planning drafts only. Nothing here claims implementation, hardware qualification, or approval to operate devices. See [README](README.md) and the [main flow](<00-main-control-flow.md>).

## Decisions and unresolved points

1. **Accepted:** Evaluate battery protection continuously, including when the inverter is disabled.
2. **Accepted escalation requirement:** If inverter OFF or negative-price control prevents charge/heat mitigation, raise a Home Assistant app alarm and turn the fan ON. This does not establish a physical charge cutoff; no charger control is assumed.
3. **Accepted fallback:** Missing BMS/Victron only -> configured MAX. Missing grid/AC with healthy BMS -> 0 W inverter and store. Combined missing BMS plus grid/AC -> 0 W. Retain known protection.
4. **Accepted priority:** Protection is at the top of the flow and has a Home Assistant app alarm popup. Limiter OFF bypasses economics only, not protection or measurement fallback.
5. **Accepted constants:** Initially use programmed freshness defaults; optional freshness settings may be considered if ESP resources permit. Five-second recovery plus two new samples remains fixed. Three expected sample intervals is not validated.
6. **Accepted:** Map fault source, affected direction, response, and recovery; evaluate charge and discharge separately.
7. **Accepted reuse direction; integration open:** Reuse CM warning and main-alarm callbacks for analog signals. The sibling ConfigManager checkout exposes `AlarmSeverity::{Alarm,Warning}`, `addAnalogAlarm`, `addAnalogWarning`, `onAlarmCome`, `onAlarmGone`, `onAlarmStay`, `onStateChanged`, and `setUpdateInterval`. Verify the pinned SIL dependency and alarm hysteresis semantics before implementation.
8. **Accepted boundary:** Automatic MOS action only for fault handling, never reserve or economics. Keep Home Assistant manual controls enabled and do not disable automation after manual action. The whole-control write gate still applies. `mosAuto` ownership, manual operation during active fault, and restoration after fault need review.
9. **Accepted:** Do not assume a PV-only path when discharge MOS is off; qualify the hardware path.
10. **Tentative preference:** Comfort price €0.25/kWh with €0.06 band: cheap below €0.19, expensive above €0.31. Not final. Classification offsets are distinct from transition hysteresis.
11. **Tentative; semantics open:** Additional €0.10 hold offset above comfort, i.e. above €0.35, for future high-price windows. Clarify whether energy is held now for later or spent during the high-price slot.
12. **Fallback proposal:** When forecast is unavailable, classify using a valid current price. Behavior when current price is also missing is undefined. Ordinary self-consumption is not an accepted fallback.
13. **Accepted:** 650 W constant planning load; no firm promise of future PV. May revisit later.
14. **Open question:** Is 1% operational SOC reserve acceptable given the user's unverified understanding that the BMS already holds reserve? Not accepted as safe. Prior 5% plus 3 percentage-point hysteresis remains a proposal. Separate voltage trip protection from SOC calibration and storage reserve.
15. **Accepted:** Budget against remaining energy and remaining window time, add PV, and carry unused energy forward.
16. **Proposal/open:** Consider synchronized Victron/BMS DC power with small hysteresis instead of probing or seasonal adjustment. Candidate inputs are fresh charger DC power and signed battery DC power (positive means charging), with conversion losses and AC measurements accounted for. Avoid double counting and do not treat inferred PV headroom as proven. Formula and MIN 0 remain proposals, not approved implementation.
17. **Accepted approach:** Start with constants for efficiency, wear, and minimum advantage, structured for SIL settings if memory permits. 90%, €0.02/kWh, and €0.03 are provisional. Keep round-trip and discharge efficiency separate; price band is not economic break-even.
18. **Accepted:** Full plan every 30 minutes plus events; apply 15-minute prices on time; use hysteresis for strategy switches; protocol timing remains code constants.
19. **Accepted fan behavior:** Battery OR ambient request; expected sensor failure requests ON; Auto/On modes and a separate explicit maintenance OFF concept. Thresholds 40/35 C and 3 K are provisional. Resolve any conflict between protection override and maintenance OFF if needed.
20. **Accepted:** Reuse existing grid PID, anti-windup based on actual cap, immediate cap decreases, controlled normal increases.
21. **Accepted:** No blind MOS retry; distinguish request, acknowledgement, and observed state. One-second tick is a proposal for pending checks, not every control cycle; no blocking delay. Protection events are urgent.
22. **Accepted:** Read actual state on boot, do not replay MOS commands, update internal variables through callbacks, and recheck before writes. Inverter behavior after complete SIL failure remains open.
23. **Accepted:** Expose HA strategy, reason, limiting factor, budget, and data quality. BMS cycles remain TODO. Future grid/charger control starts disabled; current relay fan behavior remains.

## Evidence and review limits

The API names in item 7 were observed in a sibling ConfigManager checkout only; pinned dependency availability and hysteresis behavior are not verified. No firmware implementation, threshold qualification, device operation, or hardware-path acceptance is implied.
