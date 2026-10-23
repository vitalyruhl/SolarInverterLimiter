---
puppeteer:
  landscape: true
  format: A4
  printBackground: true
---

# SIL - One inverter request and truthful feedback

[Active plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) / [specification](2026-10-20-integration-spec.md). The existing RS485/mock path is implemented. SIL MOS command previews remain locked; no complete automatic MOS protection coordinator or generic inverter acknowledgement layer was added.

```mermaid
flowchart TD
  TICK([Scheduled control evaluation]) --> DEC["Evaluate current sources, grace, recovery, reserve, price and applicable alarms"]
  DEC --> PATH{"Direct or regulated decision?"}
  PATH -- Direct --> DIRECT["Zero, bounded Idle or intentional Limiter-OFF MAX bypass"]
  PATH -- Regulated --> HOLD{"Grace or recovery hold?"}
  HOLD -- Yes --> CAP["Ceiling no higher than prior approval and actual held command; pause dynamics"]
  HOLD -- No --> NORMAL["Qualified economic or charge-PI ceiling"]
  CAP --> PID["Existing grid PID within current ceiling"]
  NORMAL --> PID
  DIRECT --> LIMIT["Final output bounds and existing negative-price override"]
  PID --> LIMIT
  LIMIT --> MODE{"Mock transport?"}
  MODE -- Yes --> MOCK["Record mock demand; no UART or RS485 operation"]
  MODE -- No --> TX["Issue request through existing RS485 transport"]
  TX --> VIEW["Expose calculated, requested, issued and measured values separately"]
  MOCK --> VIEW
  VIEW --> END([Next loop])
```

`processRS485Tick` runs at the existing configured control/publication cadence. Missing/invalid BMS data or either actual MOS permission OFF/unknown uses the regulated PV-only path: the PID result is capped by both fresh charger DC and measured household demand, including configured MIN, before the final output correction. Limiter-OFF cannot route this mode to MAX. No fresh PV/meters or a known charger fault gives zero. With valid BMS, both MOS ON and missing charger data, a direct Idle fallback is clamped to 0..effective MAX without enforcing configured MIN. Blocked reserve remains zero on source loss. Valid negative price, inverter OFF, transport OFF and applicable discharge alarms retain their stronger restrictions; normal missing grid/AC data stops regulated operation. Limiter-OFF bypass remains available only with valid BMS data and both actual MOS permissions ON.

During an approved grace/recovery hold, fresh grid feedback may reduce demand, but stale source data cannot increase the permitted ceiling. Paused PI/economic dynamics restart from the actual held command after qualification, without integrating the outage or spending accumulated outage time as a ramp allowance. The normal ceiling-increase ramp is 50 W/s; restrictive PV/energy changes remain immediate at control evaluation.

Receiving a valid JBD fault updates the appropriate latch without waiting for the configurable data timeout or normal recovery qualification. Applying an inverter restriction still follows the actual control/transport execution path; this is not a promise of zero transport latency. Charge-only faults remain direction-specific, and no automatic MOS command or retry is added.

An issued request is not inverter acknowledgement or guaranteed physical zero. If SIL fails, the actuator may retain its last setpoint. Device loss-of-command behavior, real inverter response, BLE behavior and closed-loop plant performance require their own evidence. Test-board mock/no-data acceptance does not establish those properties or authorize production uploads.

`Output Correction Offset (W)` retains the persisted `LimiterCorrW` key and applies exactly once to the complete calculated command, immediately before transmission. It is no longer part of the PID input. Before the qualified minimum-output override below, an uncorrected zero remains zero and configured MAX remains MAX; every intermediate result is offset and then clamped to 0..MAX before conversion to the wire type. Thus 0 + 50 = 0 W, 1 + 50 = 51 W, 1 - 50 = 0 W, and 200 + 50 = 250 W. This intentional final bias may exceed the pre-correction PV or household ceiling; other battery and forecast efficiency models are unchanged.

Since 5.2.1, configured MIN takes precedence over economic preservation, forecast charging, and discharge-allocation ceilings only after fresh BMS/charger samples and meters have qualified, discharge intent is available, and reserve is released. The working PID ceiling may rise to MIN; the final command is floored at MIN after offset and remains bounded by MAX. For example, a planning ceiling of 112 W, MIN 700 W, and offset -50 W produce a 700 W request. An economic zero can also rise to MIN. The floor is immediate; economic ramps still govern output above it. Actual discharge MOS OFF does not cancel discharge intent, and charge MOS OFF does not prevent this override when discharge remains permitted.

Hard stops, reserve restrictions, genuine faults, missing meters, source grace/recovery, and unavailable discharge intent cannot acquire or retain the override. Source-loss fallback and battery-unavailable PV-only control retain their existing bounds. MIN 0 restores the previous economic behavior, including offset handling. Existing nonzero settings now intentionally allow battery consumption during a planned charging period. The `minimum_output` control reason identifies a command determined by MIN; forecast status and the diagnostic discharge-budget schedule retain the original economic plan. Measured battery consumption continues to debit the energy pool. This changes no MOS actuator or setting-storage contract.

Grace, recovery, PI initialization and economic ramps retain the last issued pre-correction command separately. The corrected wire value is used for SetValue/attempt diagnostics, never fed back as an uncorrected command; repeated source-loss ticks therefore do not apply the offset cumulatively. The raw value is recorded directly, not reconstructed from a saturated zero or MAX command.

## Relative-price and reserve charging correction (5.3.0)

The obsolete `Battery Power Deadband (W)` / `pwrDb` setting is no longer registered, validated, reported, or used as a positive charging floor. An old persisted key is ignored; no NVS rewrite is required. Without an actual reserve or economic charging request, the battery PI target is zero. The existing final output correction remains unchanged and can intentionally cause net charging.

Reserve charging is independent of economic charging. At or below Reserve OFF, a valid non-expensive price prioritizes all available charger DC power for the battery and immediately sets the inverter ceiling to zero. Between OFF and ON, a cheap price requests half of available charger DC power as the reserve charging target. At ON this additional target ends. A higher economic charging requirement raises the common target; the same energy is not added twice. Charge-unavailable and existing stop/qualification rules retain priority. The existing qualified MIN override still wins over normal charging while the hard reserve latch is already released; it cannot override the hard reserve restriction.

Within the unchanged 36-hour, quarter-hour window, stored energy no longer requires an absolute comfort/deadband price before allocation. It may be deferred for a useful relative future price, with earlier equal prices preferred and remaining usable energy available now. Existing acquisition losses and wear are not charged again to that stored pool. Additional charging retains the existing round-trip-efficiency, wear, and minimum-benefit test. Free current grid energy does not justify intentionally spending stored energy. Planning demand is bounded by the configured maximum and the existing 650 W planning load, independently of the comfort band. The diagnostic budget still shows allocations from measured stored energy, not future PV credit or a complete inverter SetValue schedule.

See [local verification](../acceptance/2026-10-23-relative-price-and-reserve-control.md). This candidate has not been uploaded to production; earlier runtime reset and HTTP-delivery acceptance failures remain open.
