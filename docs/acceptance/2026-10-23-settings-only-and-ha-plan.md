# Settings-only production deployment and HA plan view

## Scope and repository state

The owner requested a registry-backed Settings-only production build and a separate Home Assistant plan view. Live and Settings-only were build profiles, not separate development branches. All SIL work remains on `feature/battery-energy-control`; the long-lived `main` was preserved. CM remains clean on `main` at `2859d7e0a5e453587cd4bc1fe3cadd49f37c45af`. No commit, push, release, branch merge, or deletion was performed in this task.

## Artifact and deployment

- SIL version: `5.2.2`.
- Dependency: published `vitaly.ruhl/ESP32 Configuration Manager@4.9.0`, registry ID `18794`.
- Compiler and embedded UI: Live `0`, Settings `1`; production transport is real.
- Frozen artifact: `.local/production-settings-only-5.2.2-20261023/firmware.bin`.
- SHA256: `63608185f7a00076cb51d33c08ef7655454ef2b9963f8bbbe8454c22ffd834d1`.
- Frozen provenance, matching ELF, and build log are beside the artifact. Provenance verifies 294 unchanged published source files and the exact embedded Settings-only gzip.
- Cold Vue build from the published package and `platformio run -e usb` passed. Firmware flash usage was 1,162,177 bytes; static RAM was 83,848 bytes.
- Five consumer build-hook guard tests and the current-control/real PID/TX host suites passed independently. Wrong registry ownership, compiler flavor, embedded flavor, and stale asset proof are rejected.
- Production OTA to `192.168.2.122` used the unchanged frozen image through paced HTTP with keep-alive. The device acknowledged the complete 1,168,752-byte upload with HTTP 200 and reboot action.
- Post-boot `/appinfo` confirmed `5.2.2`; `/ui_capabilities` confirmed `live:false`, `settings:true`, `otaActive:false`. Limiter settings matched the pre-upload snapshot exactly.
- `/runtime.json` is intentionally absent with Live disabled. It is not a post-boot health endpoint for this profile. An already loaded browser may retain older Live-enabled JavaScript while its separately fetched version title updates.

## Home Assistant

- New standalone dashboard: `http://homeassistant.local:8123/dashboard-sil-planung/0` (`SIL Planung`).
- The existing Overview was not edited. Original `/config/.storage/lovelace.lovelace` SHA256: `7d6e0b1a70d8123a3ee98dd9b9dd6db5b0a5a2298f93fa782ff88b182fee6244`; equality was checked after creation.
- New display entity: `sensor.solarlimiter_discharge_budget`, unique ID `solarlimiter_discharge_budget`.
- Its retained MQTT discovery configuration uses `SolarLimiter/Planning/DischargeBudget` for state/attributes, the existing `SolarLimiter/System-Info/status` availability topic, and 180-second expiry. No actuator, MOS, input, or firmware command topic was written.
- The chart overlays the existing price sensor and planned battery AC budget on one 24-hour axis starting at the current hour. Price colors are preserved; W uses the right axis, currently bounded by the configured 900 W maximum. The status card identifies missing/stale/invalid plans and explains that MIN can override the economic plan.
- A genuine valid non-retained 144-slot/36-hour plan was received after source qualification. Its allocations were all zero (`forecast_hold`). No synthetic plan was published to production.
- Offline generator tests passed for nonzero allocations, zero allocations, partial slots, stale/invalid/unavailable data, MOS independence, 144 slots, 24-hour clipping, and DST alignment. Missing price data returns an empty series.
- Live HA state inspection confirmed both transitions: `invalid` with current `energy_missing`, then `ready` with a fresh valid `forecast_hold` plan containing 144 zero allocations. The rendered dashboard showed the matching generation time and explicitly stated that no battery discharge was scheduled. Its screenshot is saved as `ha-sil-plan-20261023/dashboard.png` in the task workspace.

## Runtime acceptance remains open

After the initial advancing production samples, MQTT indicated offline intervals and lower subsequent uptimes. The owner explicitly reported no manual restart. The cause is not established; do not describe this deployment as stable or attribute it to a watchdog without evidence. Intermittent complete-settings HTTP response timeouts were observed both before and after this deployment.

Known production identity is MAC `3C:8A:1F:A7:EB:68`, IP `192.168.2.122`. Passive identity/uptime observations are being used to distinguish actual resets, retained old samples, reconnects, and topic collisions. No further production upload is authorized by the diagnostic work itself.

A subsequent passive MQTT window, without agent HTTP requests to production, confirmed a live uptime decrease for that same MAC: non-retained ESP uptime was `587172` ms at Unix time `1792750101.294`, followed by `5197` ms at `1792750211.777`. The earlier WiFi sample was connected with RSSI `-54`; heap was `140860` bytes, with a recorded minimum of `104900`. This establishes an unintended restart during the quiet window, but does not establish its cause or exclude other clients accessing the device.

CM's loop watchdog can restart after 30 seconds of missing loop heartbeat, and WiFi autoreboot is a separate possible path. Existing Live-disabled HTTP/MQTT exposes no reset reason. A passive UART log can prove the loop watchdog using `[CM-LOOP-WDT] heartbeat expired; restarting` or its RTC-backed next-boot marker `[CM-LOOP-WDT] previous restart: expired loop heartbeat`.

The isolated COM8 board (MAC `3C:8A:1F:A7:22:7C`, IP `192.168.2.126`) was passively observed at approximately 57.6 minutes uptime before the diagnostic USB write. The current diagnostic image retains Settings-only UI, the isolated broker namespace, and mocked RS485. It does not write production MQTT commands. Natural complete price/PV publications reached this board and produced valid 144-slot plans; no replay was necessary.

During the test board's initial quiet window, without HTTP requests, UART measured a loop work maximum of `30195986` microseconds, followed by another approximately 15.3-second loop. Grid, inverter, BMS, and charger freshness temporarily became invalid after the gap. Heap integrity remained valid, with no panic or watchdog marker at that point. These are real blocking intervals, not proof of the production restart cause. Read-only source review identifies synchronous MQTT/network I/O before the loop watchdog heartbeat as a plausible candidate; stage timing is needed to localize it.

After the initial 12-minute quiet window, bounded test-board reads completed without timeout: `/ui_capabilities` HTTP 200 in 0.203 seconds (118 bytes), the embedded UI HTTP 200 in 0.312 seconds (45,917 bytes), and two `/config.json` HTTP 200 responses in 0.203/0.234 seconds (15,171 bytes each). The complete 840.187-second capture contained 152 UART samples, nine fresh valid 144-slot plans, no reset/panic/watchdog marker, and valid heap integrity throughout. Minimum observed stack watermarks were 4,444 bytes for the loop and 13,132 bytes for AsyncTCP. The test-board NVS hash was unchanged after this run.

A separate fixture-only stage-timing run used the same Settings-only registry dependency, with stack-only UART checkpoints around existing loop operations. It was bounded to eight minutes, beginning at `2026-10-23 10:20:06 UTC`; the last two minutes included bounded Settings reads. Natural price/PV publications produced three valid plans, providing approximately 169 seconds of active-plan coverage. No replay was sent. No completed stage above one second was recorded: the largest completed stage was `mqtt_loop` at 533 ms, with no recorded reset/panic/abort. The last UART diagnostic preceded closure by 9.57 seconds; completed-call timing cannot exclude an unfinished stall at that boundary. This run therefore does not localize the intermittent long block or prove its absence.

HTTP acceptance failed in that second run: capabilities, UI, and the first complete settings read passed in 0.094, 0.907, and 0.438 seconds respectively, but the second complete settings read raised `TimeoutError` after 5.016 seconds, without headers or status. No retry was issued. Advancing UART samples and sub-second loop stages during that interval do not turn this failed HTTP check into a pass.

Deployment and the separate HA view are implemented, but runtime acceptance remains **failed/open** because production restarted unexpectedly and complete HTTP delivery remains unreliable. Neither defect has a proven root cause. Production reset-reason evidence is still needed, through passive UART access or a separately reviewed diagnostic change; the existing Live-disabled API/MQTT does not expose it. No speculative product fix or additional production upload was performed during diagnosis.

Local deployment and dashboard evidence is stored in the task workspace under `sil522-production-ota-20261023/` and `ha-sil-plan-20261023/`. Test-board evidence is under `.local/reset-repro-5.2.2-20261023*`. Keep private configuration/UART logs out of Git and user-facing output.
