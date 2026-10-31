# Explicit operating modes implementation plan

> **For agentic workers:** Execute with focused red/green host tests and a fresh review. No commit or deployment is authorized.

**Goal:** Add the three owner-approved operating modes with confirmed Home Assistant control.

**Architecture:** SIL owns one persistent validated mode, exposed through the existing CM settings and MQTT. A portable manual-mode policy produces a power demand before the existing output boundary. Automatic retains the existing economic strategy and never interprets stale BMS feedback as confirmed battery absence.

**Tech stack:** C++17, PlatformIO, current CM 4.10.3 freeze, MQTT, HA select.

**Spec:** Owner refinements in this chat on 2026-10-31. Mode labels: Automatik, Hausverbrauch folger, Batterie-lade-Vorrang.

## Global constraints

- No firmware upload, commit, push, merge, or automatic MOS-policy removal.
- Live UI stays disabled; retain the exact CM dependency and existing telemetry topics.
- New default is `automatic`; existing saved settings must remain valid.
- Mode enum: `automatic`, `household_follow`, `charge_priority`.
- The manual household request is `clamp(measuredHouseholdW + correctionW, 0, maximumW)`; apply correction exactly once, including at MAX. Do not change automatic endpoint correction semantics incidentally.
- Fresh grid and actual inverter measurements are mandatory. The request is not evidence that the battery is absent.
- Explicit inverter OFF / disabled transport remains authoritative. No mode writes MOS registers.
- Known connected-battery discharge restrictions and the existing SOC reserve remain applicable. Fresh confirmed discharge/both MOS OFF permits direct-PV demand without depending on the load-limited PV reading.
- A deliberately selected manual household mode may operate without BMS feedback, explicitly reporting `manual_bms_unverified`; automatic never gains this permission from stale data.
- `charge_priority` holds inverter output at zero below an adjustable 95% threshold. Default policy between 95% and 100% continues full charging (0% inverter share); an optional validated PV share can be configured. At full charge use household following with a release latch back at the configured threshold.
- A missing BMS in charge priority is unknown SOC and holds output, rather than inventing a full/absent battery. A confirmed non-chargeable path may select the documented manual fallback while preserving connected-battery restrictions.
- HA displays confirmed retained mode state; expired or malformed commands cannot change settings. Replayed duplicate selection must not cause flash writes.

## Tasks

- [ ] **1. Portable mode decision and command contracts.** Add bounded enum parsing, valid charge threshold/share, explicit mode inputs and source-quality results. Test household correction/MAX examples, stale meters, stale BMS distinction, connected reserve, physical MOS-off, charge-priority thresholds/latch, invalid settings, expired commands and duplicate handling before implementation.
- [ ] **2. Integrate settings and MQTT.** Add persistent CM mode/charge threshold/share settings, one loop-owned change path for UI and MQTT, root-derived command/state/status topics, confirmed publication only after successful save, reconnection replay of state only, and no new MOS writes. Reset old PI/plan state on mode transitions. Preserve automatic behavior in regression tests.
- [ ] **3. HA contract, documentation and build verification.** Provide an MQTT select with the three requested labels and availability, confirmed state and non-retained commands. Bump compatible minor firmware version to 5.5.0, run relevant host tests plus USB/OTA build without uploading. Document that runtime activation and multi-day acceptance are separate.

## Review focus

- Stale BMS never turns automatic into an unqualified follower.
- Manual mode never uses inverter demand as household measurement and applies offset once.
- An inverter OFF or stale meter cannot be bypassed by MIN, remembered mode or full-charge latch.
- Failure to save settings cannot produce false confirmed mode feedback.
- Root changes, reconnects, invalid clock and duplicate commands do not replay stale choices or touch MOS.

## Decisions

- Two independent worktrees keep SBT and SIL file ownership separate; their production roots remain unchanged.
- Automatic MOS removal remains a conditional plan until actual BMS protection settings are verified. No recovery supervisor is implemented.
- Keep current reserve values unchanged; 31% was an investigation example, not an authorized runtime change.
- Between the configurable 95% threshold and 100%, the conservative default is still battery-only; optional PV sharing is explicit configuration rather than an inferred solar forecast.
