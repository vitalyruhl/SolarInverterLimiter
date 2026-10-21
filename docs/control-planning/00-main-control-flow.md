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
  PLAN --> STOP{"Invalid settings, discharge alarm, inverter OFF, transport OFF or valid negative price?"}
  STOP -- Yes --> ZERO["Direct zero request"]
  STOP -- No --> BYPASS{"Limiter disabled?"}
  BYPASS -- Yes --> MAX["Intentional configured MAX bypass"]
  BYPASS -- No --> METERS{"Grid and inverter AC current?"}
  METERS -- No --> ZERO
  METERS -- Yes --> SRC{"BMS and charger usable?"}
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
  FINAL --> MODE{"Mock transport?"}
  MODE -- Yes --> MOCK["Record simulated demand; no RS485 hardware I/O"]
  MODE -- No --> TX["Issue existing RS485 request"]
  MOCK --> VIEW["Live / MQTT diagnostics; request is not acknowledgement"]
  TX --> VIEW
```

`main.cpp::processRS485Tick`, `CurrentControl::update`, `FuturePrices::plan` and `sbt::Service::controlSnapshot` own these boundaries. A held or recovering regulated decision still passes through the grid PID; a zero recovery decision is direct. The selected timeout is 1..9999 seconds, default 180. Collector/control acquisition freshness remains bounded separately at 30 seconds. Source loss no longer selects MAX; the intentional Limiter-OFF bypass remains distinct.

Missing telemetry creates no alarm and never clears a known restriction. A received JBD discharge restriction bypasses normal recovery qualification at the next control evaluation. Charge-only restrictions remain direction-specific diagnostics; this increment adds no MOS actuator. Inverter loss-of-command behavior and actual plant response remain separate hardware qualifications.
