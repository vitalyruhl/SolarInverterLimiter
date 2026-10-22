---
puppeteer:
  landscape: true
  format: A4
  printBackground: true
---

# SIL - Implemented integration

Updated 2026-10-20. [Active plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) / [specification](2026-10-20-integration-spec.md). This describes source behavior, not completed plant acceptance.

```mermaid
flowchart TD
  START([Arduino loop]) --> MQTT["Receive SBT scalars and health; stage price and PV chunks"]
  MQTT --> CACHE["Preserve last-good cache; fence sessions; latch directional JBD faults"]
  CACHE --> TICK{"Control tick due?"}
  TICK -- No --> OTHER["Other loop work"]
  TICK -- Yes --> SNAP["Snapshot grid, inverter AC, current price and SBT quality"]
  SNAP --> PLAN["Update measured energy and slot time; reuse or rebuild bounded planning cache"]
  PLAN --> STOP{"Invalid settings, applicable discharge alarm, inverter OFF, transport OFF or valid negative price?"}
  STOP -- Yes --> ZERO["Direct zero request"]
  STOP -- No --> BMS{"Fresh usable BMS SOC/power and both actual MOS ON?"}
  BMS -- No --> PVINPUT{"Fresh charger DC and AC meters; no charger fault?"}
  PVINPUT -- No --> ZERO
  PVINPUT -- Yes --> PVQUAL{"Five seconds and two new Victron samples?"}
  PVQUAL -- No --> ZERO
  PVQUAL -- Yes --> PVONLY["PV-only ceiling: min of charger DC and measured household demand"]
  PVONLY --> PID
  BMS -- Yes --> BYPASS{"Limiter disabled?"}
  BYPASS -- Yes --> MAX["Intentional configured MAX bypass"]
  BYPASS -- No --> METERS{"Grid and inverter AC current?"}
  METERS -- No --> ZERO
  METERS -- Yes --> SRC{"Charger usable?"}
  SRC -- No --> GRACE{"Prior approval and both sources within grace; reserve not blocked?"}
  GRACE -- Yes --> HOLD["Hold ceiling no higher than actual held command; valid grid may reduce"]
  GRACE -- No --> IDLE["Direct bounded Idle; blocked reserve remains zero"]
  SRC -- Yes --> QUAL{"Five seconds and two new sample pairs qualified?"}
  QUAL -- No --> REC["Pause controller dynamics"]
  REC --> RECOK{"Prior approval with grace and reserve not blocked?"}
  RECOK -- Yes --> HOLD
  RECOK -- No --> ZERO
  QUAL -- Yes --> CEIL["Economic or measured-PV PI ceiling; restart paused dynamics from held command"]
  HOLD --> PID["Existing grid PID bounded by ceiling"]
  PID --> FINAL["Final bounds and existing negative-price override"]
  CEIL --> PID
  IDLE --> FINAL
  ZERO --> FINAL
  MAX --> FINAL
  FINAL --> OFFSET["Apply output offset once; preserve zero/MAX; clamp to 0..MAX"]
  OFFSET --> MODE{"Mock transport?"}
  MODE -- Yes --> MOCK["Record simulated demand; no RS485 hardware I/O"]
  MODE -- No --> TX["Issue existing RS485 request"]
  MOCK --> VIEW["Live / MQTT diagnostics; request is not acknowledgement"]
  TX --> VIEW
```

`main.cpp::processRS485Tick`, `CurrentControl::update`, `FuturePrices::plan` and `sbt::Service::controlSnapshot` own these boundaries. A held or recovering regulated decision still passes through the grid PID; a zero recovery decision is direct. The selected timeout is 1..9999 seconds, default 180. Collector/control acquisition freshness remains bounded separately at 30 seconds. Missing or invalid essential BMS data, or either actual MOS permission OFF/unknown, selects the PV-only branch before the intentional Limiter-OFF bypass. This branch ignores battery-power PI/forecast discharge permission, requires independent Victron qualification, and caps the PID result by measured household demand and charger DC power before the final output correction. Missing PV/meters or a known charger fault requests zero. A sole software MOS-lock discharge bit is exempt only in PV-only mode; other latched discharge faults remain stops. Missing charger data with valid BMS and both MOS ON retains the bounded grace/Idle policy.

Missing telemetry creates no alarm and never clears a known restriction. A received JBD discharge restriction bypasses normal recovery qualification at the next control evaluation. Charge-only restrictions remain direction-specific diagnostics; this increment adds no MOS actuator. Inverter loss-of-command behavior and actual plant response remain separate hardware qualifications.

`Output Correction Offset (W)` retains the persisted `LimiterCorrW` key and applies exactly once to the complete calculated command, immediately before transmission. It is no longer part of the PID input. An uncorrected zero remains zero and configured MAX remains MAX; every intermediate result is offset and then clamped to 0..MAX before conversion to the wire type. Thus 0 + 50 = 0 W, 1 + 50 = 51 W, 1 - 50 = 0 W, and 200 + 50 = 250 W. This intentional final bias may exceed the pre-correction PV or household ceiling; other battery and forecast efficiency models are unchanged.
