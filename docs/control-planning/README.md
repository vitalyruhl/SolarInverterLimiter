# SIL control integration and reference diagrams

Updated 2026-10-20. The [SIL / SBT / HA implementation plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md) remains the only active work queue. The owner authorized its implementation after the earlier documentation closeout. The [consolidated specification](2026-10-20-integration-spec.md) records the accepted scope and production-upload prohibition.

The implementation diagrams below describe the current source: bounded source grace with a default 180-second deadline, Idle fallback at 500 W by default, positioned NTC channels, atomic price/PV schedules, and cached energy planning. They are not production-deployment or physical closed-loop acceptance evidence.

## Diagrams and contracts

- [Implemented main integration](00-main-control-flow.md)
- [Telemetry validation, held values and immediate alarms](01-measurement-validation.md)
- [Fan reference design](02-fan-control.md)
- [Directional protection reference design](03-protection-check.md)
- [Protection-response reference design](04-protection-response.md)
- [Cached planning and fast regulation](05-energy-planning.md)
- [Energy budget reference](06-energy-budget.md)
- [Grid PID and arbitration reference](07-grid-pid.md)
- [Actual command / mock / feedback path](08-command-and-feedback.md)
- [Settings and visualization switch](09-settings-callbacks.md)
- [Accepted charge-target / PV-ceiling reference](10-charge-target-and-pv-ceiling.md)
- [Implemented MQTT price and PV schedule contract](mqtt-schedule-contract.md)

The protection, fan, energy-budget and charge-target reference documents retain accepted requirements beyond this increment. Several `PreparedControl` module boundaries remain stubs. SIL latches existing directional JBD faults and keeps its MOS commands locked; it does not implement the full automatic MOS protection coordinator or new numeric battery protection thresholds.

## Preserved decisions and evidence

- [Round 5 decisions](round-5-decisions-and-defaults.md): accepted baseline; later specification decisions override conflicts.
- [Historical implementation / crash / MQTT ledger](implementation-follow-ups.md): evidence, not another work queue.
- [Receiver implementation evidence](../acceptance/2026-10-16-sbt-telemetry-inputs-evidence.md)
- [Compact-runtime implementation evidence](../acceptance/2026-10-18-compact-runtime-metadata-evidence.md)

Historical acceptance does not validate a new firmware image. Test-board smoke observation, synthetic fixtures, real BLE acquisition, Home Assistant configuration and production plant response are separate evidence classes. The health timestamp correction alone does not resolve the historical overnight crash investigation. The owner deploys production firmware.

## VS Code / PDF

Open the canonical plan or diagram directly in VS Code. Export through an existing Mermaid-capable Markdown preview and inspect page breaks; previously exported PDFs are not current source.
