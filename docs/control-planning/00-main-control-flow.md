---
puppeteer:
  landscape: true
  format: A4
  printBackground: true
---

# SIL - Actual integration and approved next increment

Updated2026-10-20. [Active plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) and [specification](2026-10-20-integration-spec.md) are authoritative for the new increment. Diagrams below deliberately distinguish current source from target behavior; no firmware was changed by this documentation update.

## Current source: implemented path

```mermaid
flowchart TD
  START([Arduino loop]) --> MQTT["MQTT callbacks: price chunks / SBT scalars and health"]
  MQTT --> CACHE["SBT mutex-protected cache; current receiver age limit15s"]
  CACHE --> TICK{RS485 control tick due?}
  TICK -- No --> OTHER["Other loop work; next cycle"]
  TICK -- Yes --> SNAP["Read grid/AC/current price and SBT snapshot"]
  SNAP --> FUT["FuturePrices.plan each control tick;36h bounded price allocation"]
  FUT --> DEC["CurrentControl: reserve and price band"]
  DEC --> STOP{Settings/OFF/transport/negative-price stop?}
  STOP -- Yes --> ZERO["Direct zero request"]
  STOP -- No --> BYPASS{Limiter disabled?}
  BYPASS -- Yes --> MAX["Direct configured MAX bypass"]
  BYPASS -- No --> METER{Grid or inverter AC missing?}
  METER -- Yes --> ZERO
  METER -- No --> SRC{BMS or charger invalid?}
  SRC -- Yes --> FALL["Direct source_loss MAX; latched blocked reserve gives0"]
  SRC -- No --> REC{Normal recovery qualified?}
  REC -- No --> ZERO
  REC -- Yes --> CEIL["Economic ceiling or measured-PV charge PI ceiling"]
  CEIL --> PID["Existing grid PID within ceiling; normal MIN may reduce"]
  ZERO --> FINAL["Recheck final limits and negative-price override"]
  MAX --> FINAL
  FALL --> FINAL
  PID --> FINAL
  FINAL --> MODE{Mock transport?}
  MODE -- Yes --> MOCK["Record mock demand; no UART/RS485 hardware I/O"]
  MODE -- No --> TX["Issue RS485 request at configured cadence"]
  MOCK --> VIEW["Planning state / Live / MQTT; request is not acknowledgement"]
  TX --> VIEW
```

Source anchors: main.cpp processRS485Tick/FuturePrices.plan; CurrentControl.h update. Full ProtectionCoordinator and several PreparedControl boundaries remain stubs; SIL MOS commands remain locked. Do not interpret the older protection diagrams as already wired into this path.

## Approved next integration: target, not yet implemented

```mermaid
flowchart TD
  IN([New event or control cycle]) --> PARSE["Validate incoming candidate; preserve last-good cache on malformed update"]
  PARSE --> ALARM{Valid real alarm received?}
  ALARM -- Yes --> URGENT["Apply existing alarm restriction immediately; no new waiting period"]
  ALARM -- No --> AGE{Last valid source evidence within configured1..9999s?}
  AGE -- Yes --> HOLD["Use valid/held strategy; no added stale-data permission"]
  AGE -- No --> OLD["Keep numeric values marked Old; bounded Idle fallback default500W"]
  HOLD --> EVENT{Snapshot/slot/settings/material event or30min replan due?}
  EVENT -- Yes --> PLAN["Replan qualified prices/PV, usable energy and charge target"]
  EVENT -- No --> USE["Reuse valid allocations; update energy debit/time cheaply"]
  PLAN --> FAST["Fast PID + charge PI; combine all stronger stops/ceilings"]
  USE --> FAST
  OLD --> FAST
  URGENT --> FAST
  FAST --> SEND["Single bounded request; test mock or existing RS485 transport"]
  SEND --> UI["Compact Limiter + NTC1 Top / NTC2 Bottom; existing raw MQTT channels preserved"]
```

Missing telemetry never creates an alarm. A known restriction is not cleared by silence. Plan validity and measurement tolerance are separate clocks. No second alarm delay or unapproved MOS actuator is introduced. Inverter command loss may leave the last setpoint active: independent hardware behavior remains unqualified.