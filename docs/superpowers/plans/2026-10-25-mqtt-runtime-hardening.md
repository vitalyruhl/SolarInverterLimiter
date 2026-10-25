# MQTT Runtime Hardening Implementation Plan

> **For agentic workers:** Use scoped subagents for CM transport and numeric handling; the root agent owns integration, review and all firmware/device operations. No automatic commits or publication.

**Goal:** Keep SIL responsive during broken or delayed MQTT traffic, reject unsafe numeric inputs, add bounded serial diagnostics, and deploy the reviewed candidate after a maximum 15 minute isolated device pretest.

**Architecture:** An opt-in CM Client adapter assembles complete MQTT frames without waiting for the network, with a 30000 ms absolute frame lifetime. Immediate bounded transport writes and short numeric-IP connection attempts replace long synchronous waits. SIL keeps accepted forecast objects across MQTT reconnection, applies numeric safety before arithmetic, and records fixed-size stage/reset diagnostics.

**Tech Stack:** ESP32 Arduino, PlatformIO, CM based on published 4.9.0, PubSubClient 2.8, ArduinoJson 7, Python host capture.

**Spec:** Owner instructions in this chat on 2026-10-25: robustness first, targeted logs, build and isolated pretest at COM8 for at most 15 minutes, production-like test settings with all MQTT paths isolated, production OTA, then a test restart close to the production OTA reboot. The owner authorized implementation and these device operations; commits, public CM release and GitHub publication remain outside scope.

## Global Constraints

- Preserve production settings and NVS; do not erase devices.
- Retain Settings UI only and disabled generic verbose/MQTT logging.
- A 30 second frame lifetime is elapsed across loop iterations, never a blocking receive wait.
- Preserve valid price/PV schedules on reconnect without extending their existing expiry.
- Keep all fixture publications and possible BMS commands inside the isolated broker and prefixes.
- Verify actual COM8 USB/MAC identity before upload; never identify the production target by IP alone.
- A failing focused check, panic, unexplained reset or frozen control cycle blocks production OTA.
- Freeze and hash the exact real-transport artifact; reconcile an ambiguous OTA result before retrying.
- Keep source and evidence English and secrets out of logs/repository artifacts.

## Review Focus

- A trickled MQTT frame must not extend its first-byte deadline or block control.
- Malformed lengths, QoS/topic lengths, oversized frames and partial writes must close the transport cleanly.
- INT32 limits, non-finite numbers, negative grid values and arithmetic overflow must be deterministic.
- A test build must never publish real SBT or inverter commands even if fault data arrives.
- Diagnostics and host capture must remain bounded and preserve evidence across resets and silence.

## Tasks

- [ ] CM transport: add opt-in bounded frame adapter, focused RED/GREEN tests and failure counters; preserve existing default consumers.
- [ ] SIL numeric handling: add checked conversions and saturating arithmetic, targeted RED/GREEN tests, then integrate actual final control path.
- [ ] SIL integration: use the local reviewed CM development dependency, opt into bounded I/O, preserve forecast objects on reconnect, prevent mock hardware/control writes, and avoid synchronous roaming in the active control loop.
- [ ] Diagnostics: fixed storage for stage/loop/reset records, serial summaries and stall evidence without a line on every loop; include minimal reset identity over MQTT for production correlation.
- [ ] Metadata/docs: scan version sources, bump SIL patch and CM development patch as appropriate, synchronize required CM mirrors and record unpublished changes; make duplicate WiFi uptime optional and disable it for SIL.
- [ ] Build and review: run only affected host suites plus relevant production/test PlatformIO builds; independent integration review; inspect final diff and artifact hashes.
- [ ] Fixture: preserve a private production settings snapshot, map every incoming/outgoing topic to isolated equivalents, verify COM8 identity and collect at most 900 seconds of serial/MQTT evidence with natural inputs and bounded transport fault cases.
- [ ] Production: after passing all prior gates, transfer the exact real-transport artifact by OTA, reconcile reboot/version/MAC/settings/profile/uptime, restart the fixture close to the actual production reboot and record host timestamps and boot epochs.

## Decisions and Progress

- 2026-10-25: Both source repositories started clean on main. Work branches are feature/mqtt-runtime-hardening; no commit is authorized.
- The local CM development dependency is used for this deployment; no public package release is inferred.
- Bounded mode initially requires a numeric broker IP so a hidden synchronous DNS path cannot defeat the deadline. Verify the actual broker before deployment.
- WiFi uptime removal is opt-in for SIL, preserving the public CM default schema for other consumers.
- A real ESP reset still loses the in-memory plan. Durable forecast persistence and invented fallback economics are outside this change.
- Existing fixture logging is reusable in part, but old settings, verbose flags, 8 second failure tails and 45 second silence termination are unsuitable for this run.
