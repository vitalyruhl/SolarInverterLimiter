# Charge Intent Projection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax.

**Goal:** Show coarse charging intent and forecast sunshine beside the existing discharge budget.

**Architecture:** A side-effect-free bounded projection holds the current economic request until its existing deadline and adds existing reserve intent. Separate MQTT diagnostics share publication policy; no control feedback. HA preview uses only the isolated Test-ESP namespace.

**Tech Stack:** ESP32 C++17, published CM4.9.0/PubSubClient, fixed buffers, host Clang tests, HA ApexCharts.

**Spec:** `docs/superpowers/specs/2026-10-23-charge-intent-projection.md`

## Global Constraints

- Continue `feature/battery-energy-control` after checkpoint `583557e`; no parallel branch/worktree line.
- Sole version source `src/main.cpp`, candidate5.4.0. Production stayed5.3.0 during isolated acceptance; the owner subsequently authorized the verified5.4.0 OTA.
- Preserve control/NVS/MOS and discharge wire semantics; no final commit/push.
- COM8 MAC `3C:8A:1F:A7:22:7C`; isolated broker `192.168.2.220:18884`; public test prefix `SILTest/COM7`.

## Review Focus

- Unknown source/PV must not masquerade as zero intent.
- No duplicate economic energy/PV credit or post-deadline extension.
- Reserve/economics share one bounded target/headroom.
- DC intent and AC allowance must not imply exact inverter output.
- Diagnostics must remain bounded and not stall control.

### Task1: Projection and serializer

**Files:** create `src/control/ChargePlan.h`, `test/host/charge_plan_test.cpp`, `tools/test/run-charge-plan-tests.ps1`; add read-only `FuturePrices::priceAt(uint32_t utc, float factor, float &price) const` in `src/control/FuturePrices.h`.

**Interface:** `ChargePlan::project` consumes PreparedSettings/FuturePlan, accepted prices/PV, actual CurrentDecision, a fresh DischargeBudget and qualified ChargePlanInputs. It produces fixed w/s arrays and serialization, with no mutation of consumed control/planning objects.

- [x] Write tests for current anchoring, fixed economic request/deadline, reserve full/half, shared energy/headroom, source/MOS/unknown behavior,144slots and packet bounds; observe RED.
- [x] Implement minimal projection/serializer; observe GREEN.
- [x] Run existing future/current-control suites to preserve outputs/contracts.

### Task2: Export and isolated HA preview

**Files:** `src/control/DischargeBudget.h`, `src/main.cpp`, examples under `docs/home-assistant/`, `docs/control-planning/mqtt-schedule-contract.md`.

**Interface:** generalize the existing state machine to `PlanPublication<Plan>` while retaining `DischargeBudgetPublication`; add ChargePlanPublication/topic. Capture both on the same normal planning tick; project after the actual controller decision and promptly invalidate source loss.

- [x] Pin generic publication/capture/retry/overflow behavior with focused tests; preserve discharge alias/semantics.
- [x] Integrate source gating/current anchoring, no actuator changes.
- [x] Check test-message HA generators for real/null/stale/partial/DST data, DC/AC labels and shared time axis; production dashboard untouched except the separately authorized uptime row.

### Task3: Build, isolated device acceptance and review

**Files:** `src/main.cpp`, `readme.md`, new acceptance record.

- [x] Bump to5.4.0, run targeted suites and `pio run -e usb` against registry CM.
- [x] Freeze BIN/ELF/provenance and memory/packet measurements.
- [x] Build a separate isolated mock fixture, guard COM8 identity/app bank, privately back up NVS and USB-flash only that fixture. No NVS erase was issued; byte changes require the offline comparison below.
- [x] Observe 900.296s plus a 110.750s passive tail with natural plans and four complete Settings reads; capture heap/stack/loop/reset data and inspect actual test MQTT/HA preview.
- [x] Independent final diff review; focused RED/GREEN fixes only; update evidence, leave changes uncommitted.

## Ledger

- Baseline583557e clean; existing future/current-control host suites passed. Independent read-only review supports frozen current-intent projection instead of a new optimizer.
- Ruling: continue this checkout/branch as explicitly requested, rather than create another worktree.
- Ruling: owner clarification narrows blue to deliberate withholding; natural surplus is excluded and sunshine is a separate line.
- Ruling: complete integration first, then extend isolated acceptance to include the owner's reset/MQTT/NVS suspicion. Preserve NVS and collect staged timing/heap/reset evidence before considering reset or chunking; neither production erase nor protocol chunking is authorized as an assumed fix.
- Read-only tariff lookup observed RED (missing method) then GREEN. Generic publication retains existing discharge behavior and unchanged 608/2672-byte trace/publication sizes. Existing future/current-control and SBT receiver suites passed after diagnostic-only integration.
- Independent review found a partial-deadline/PV-cap averaging issue. A focused test observed RED at expected166W, then GREEN after per-phase PV/headroom integration; fresh focused review closed the finding.
- Final real candidate source aggregate05904be2d7cb9b6eb3ae76ede3ef0696fd558b929a8984c117770ccf26ddb338; BINf4e9bc5f75eb325c108e954209a24b30170410351281699bf84840aeb6f345c0. StaticRAM86976B/program1166013B. Separate instrumented no-erase fixture build passed; COM8 acceptance follows integration, not before it.
- Isolated acceptance completed without an unexpected reset, panic or unmatched stage; four HTTP reads passed. The 3.040s peak loop work coincided with the intentional broker outage. Three private broker/bridge defects were repaired and ten focused harness guards passed; no product source changed after freeze. Real red/solar and known-zero/unknown blue were verified in the separate HA TEST dashboard. See the [acceptance record](../../acceptance/2026-10-23-charge-intent-projection.md) for memory, stack, timing, NVS limitations and the owner's separate CM datalist/timer findings.
- Owner follow-up authorized production OTA after the successful test and stopped further NVS work. Frozen real firmware transferred once with HTTP200; post-boot5.4.0/Live0/Settings1 and every captured setting value were verified. The owner separately requested extending HA plan history from1h to3h while retaining24h forward coverage. No final commit or push is authorized.
- Production MQTT confirmed two valid matching red/blue snapshots and no extra boot through a sampled13m50s uptime; source-qualification gaps also occurred without reboot and remain a separate open finding. The owner explicitly requested the TEST presentation in Home#3 with production sources: the passive production ChargePlan entity and blue/solar curves are installed, three-hour history is preserved, and exact target-card-only config comparison plus screenshot were independently checked. Copy-ready single-card YAML excludes the view-level panel wrapper that caused the owner's editor error.
