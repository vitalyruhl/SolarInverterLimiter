# SIL control integration and reference diagrams

The **[2026-10-20 SIL / SBT / HA implementation plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md)** is the only active work queue. Read its [consolidated specification](2026-10-20-integration-spec.md) for the latest owner decisions, approvals and production-upload prohibition. Current closeout is documentation and local commits, then STOP before implementation.

The October20 requirements supersede earlier defaults for source-loss MAX,15s receiver tolerance, measured tilt30degrees and temperature-card removal: target is Idle500W, configurable1..9999s/default180,14.8degrees and reversible PIO visualization switch. Actual source remains unchanged until implementation.

## Diagrams

- [Actual main integration and next target](00-main-control-flow.md)
- [Telemetry validation, held values and immediate alarms](01-measurement-validation.md)
- [Fan reference design](02-fan-control.md)
- [Directional protection reference design](03-protection-check.md)
- [Protection-response reference design](04-protection-response.md)
- [Slow planning and fast regulation](05-energy-planning.md)
- [Energy budget reference](06-energy-budget.md)
- [Existing grid PID and target arbitration](07-grid-pid.md)
- [Actual command / mock / feedback path](08-command-and-feedback.md)
- [Settings and visualization switch](09-settings-callbacks.md)
- [Accepted charge-target / PV-ceiling reference](10-charge-target-and-pv-ceiling.md)

The full protection/fan/MOS diagrams contain accepted requirements beyond current implementation; they are not passed device evidence. Current `PreparedControl` stubs and locked SIL MOS commands must not be presented as deployed protection.

## Preserved reference and evidence

- [Round5 decisions](round-5-decisions-and-defaults.md): accepted baseline; later decisions in the current specification override conflicts.
- [Historical implementation / crash / MQTT ledger](implementation-follow-ups.md): evidence, not another task queue.
- [Receiver implementation evidence](../acceptance/2026-10-16-sbt-telemetry-inputs-evidence.md)
- [Compact-runtime implementation evidence](../acceptance/2026-10-18-compact-runtime-metadata-evidence.md)

Old mixed work plans were moved to acceptance evidence, preserving recorded outcomes. Superseded Round4 discussion/review files were removed; original history is recoverable through Git. Remaining hardware qualifications (device loss-of-command behavior, source identity, protection limits, actual plant response) remain explicit in the current plan/specification.

## VS Code / PDF

Open the canonical plan directly in VS Code. The reference diagrams retain print frontmatter where useful. Export through an existing Mermaid-capable Markdown preview and review page breaks; old PDFs must not be treated as current source.