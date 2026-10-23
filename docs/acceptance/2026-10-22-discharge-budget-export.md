# Battery discharge-budget export acceptance - 2026-10-22

The compatible 5.2.0 feature exports existing per-quarter AC battery allocations. It adds no optimizer and does not change current control decisions. The curve is planned battery contribution, not measured discharge or a complete future inverter SetValue.

## Frozen artifact and installation

- Branch: feature/battery-energy-control; baseline ec07fba2c42614e6fff01bd3d11094806eb547bf. Changes remain uncommitted.
- SHA-256: b5308d6b051fe5d5e8dbaee4004fde6c01d1260c60218cee554324fbdee439fb.
- Image 1,285,888 bytes; flash 1,279,309/1,310,720 (97.6%); static RAM 84,328/327,680 (25.7%).
- Owner-confirmed dedicated board COM7: ESP32-D0WD-V3 revision v3.1, MAC 3c:8a:1f:a7:22:7c.
- Read-only partition/selector checks identified app1 at 0x150000, valid sequence 2. Only that application bank was written by USB; esptool verified the image hash. No partition table, bootloader, boot selector or NVS partition image was transferred or erased. Normal firmware boot retains its existing stored-version synchronization.
- Local testesp uses mocked RS485 and a guarded local-only RAM network override before any MQTT connection. Product planner/export/controller headers are actual sources. No product isolation hook or CM change was introduced.
- No OTA, production SIL/SBT deployment, live HA change, physical MOS command, commit or push.

## Real-device evidence with synthetic inputs

The authenticated task-local broker used 192.168.2.220:18884 and SILBudgetTest20261022/in/ and /out/ only. ACLs blocked foreign topics and every /set component, restricted device output, and checked LastWill topics at CONNECT. No bridge or production broker/HA/Docker setting changed. All device publications stayed in the test output namespace. Inputs were [MOCKED!]; the firmware and MQTT outputs came from the real USB board.

| Check | Result |
| --- | --- |
| Observation | Initial actual MQTT window 315.786 seconds; verified System-Info window 522.923 seconds. Uptime 26,826 -> 549,695 ms, with no observed reset. |
| Export | Five valid complete 144-slot snapshots; all five matched time-aligned scalar battery ceilings. Partial current-quarter timestamps validated. |
| Discharge OFF | Runtime physical availability false, intent availability true; exported first allowance and mocked inverter demand reached 900 W. Hardware TX remained blocked. |
| Charge direction | Charge-ON usable PV credit 216.03..228.08 Wh. The first immediate OFF-transition poll preceded the next control tick; five settled OFF polls were zero. Zero was observed within the 10.33-second polling bound, not claimed as exact control latency. |
| Invalid inputs | Prompt valid=false, n=0, w=[]; no synthetic zero schedule. |
| Reconnect | Connected at Unix UTC 1792690105.466; forecast requalified at 1792690109.638; regular valid 144-slot snapshot generated 1792690165 and received 1792690166.268. |
| Memory | Free heap 81,728..85,128 bytes; smallest observed largest free block 42,996 bytes. No Guru, abort or backtrace in the serial observation. |

The original 315-second harness endpoint preceded the next qualified 60-second snapshot and remains FAILED in acceptance-result.json. It was not rewritten as a pass. The continuous broker audit captured the later valid snapshot, followed by expected source expiry when that fixture ended. A bounded 16.17-second continuation refreshed the same synthetic sources and observed another valid 144-slot snapshot (g=1792690353, forecast_hold) on the same unchanged image without reset. Separate acceptance-extended-result.json passes the bounded checks.

The strict harness ACL blocked ten noncritical empty retained cleanup messages on the exact /out/System-Info root within the accepted evidence window. Its /ESP and /WiFi JSON subtopics and all control/budget topics were delivered. This is a local harness limitation, not a firmware finding. Later broker-shutdown lifecycle events are outside the acceptance window.

## Local checks and resource limits

Host checks pass for scalar equivalence, chronological/partial-slot export, invalid empty output, 144-slot/serialization bounds, charge-OFF credit, reconnect/retry/cadence and failed-first-send invalidation. Existing SBT directional-MOS, current-control, actual PID and final command-application tests pass. HA YAML parsing and actual generators pass offline zero/missing/invalid/stale/unavailable, MOS independence, coverage and 24-hour/DST cases. No rendered-card or live HA acceptance is claimed.

The ESP32 export object uses 2,668 persistent bytes plus a 16-byte topic String object and its topic allocation. Requested MQTT heap grows by 1,280 bytes (1,024 -> 2,304). The 2,048-byte serializer buffer is persistent. The compiled publication function has a 752-byte fixed frame; whole call-chain worst-case stack use is unmeasured. There is no equivalent older testesp build, so no firmware-wide size delta is claimed.

The build used the parent-confirmed CM source freeze, prebuild HEAD a1a6ffc592d116e05882a87e0c19e4adb5d387c3 plus reviewed working changes. No compile-time CM content-hash manifest was captured; later hashes are not attributed to this image. Existing ArduinoJson deprecation and inherited testesp logging-definition warnings remain.

## Evidence and disposition

Private evidence and frozen firmware remain in .local/discharge-budget-export-20261022/hil/: final-artifact.json, upload.log, boot-bank-verification.json, usb-identity.json, broker-events.jsonl, device-mqtt*.jsonl, runtime-records.jsonl, serial-acceptance.log, original acceptance-result.json and separate acceptance-extended-result.json.

Broker/fixture processes stopped and serial closed. Frozen test firmware remains on the owner-authorized dedicated board and stays confined to the isolated broker after reboot. Production SIL and HA remain unchanged. These bounded checks do not validate physical inverter delivery, BMS MOS switching, HA rendering or long-run stability. Production installation is a separate owner decision.
## Later web-stability follow-up - not release acceptance

Subsequent read-only stability testing on the same frozen dedicated test ESP is **FAILED**. The bounded USB/export results and the original harness timing failure above remain historical evidence; they do not constitute release or long-run stability acceptance.

- One unexpected software boot occurred immediately before the first observer's HTTP request. Its cause remains unresolved. A later single-client baseline (`GET /runtime.json` every five seconds) completed 16 responses, then stopped after three consecutive five-second timeouts without completed response headers or body. Passive UART separately captured WLAN disconnection, weak priority-AP selection and reconnect/WiFi-stack-reset attempts. The later delivery outage recovered with continuous uptime, so it did not establish another reboot.
- Two actual GUI clients ran together for 131.307 seconds without an extra HTTP sampler. Live/BMS WebSocket data remained fresh, but Settings remained an empty shell for approximately 25 seconds after reload. No settings, reset, reboot or MOS controls were applied. The host-observed UART control-log interval remained approximately two seconds; it is not a CPU execution-time measurement.
- HTTP diagnostics reported a `MALLOC_CAP_DEFAULT` cumulative SDK low-water value of **1,152 bytes** near the reload. This can aggregate per-region historical minima and is not instantaneous remaining heap: sampled current default-capability free heap nearby was approximately 31-33 KB, with integrity checks reporting `ok`. The broader `MALLOC_CAP_INTERNAL` counters used by `ESP.getFreeHeap()` and `ESP.getMinFreeHeap()` are different; their later low-water value was 44,576 bytes. These observations establish allocator pressure, not a proven OOM or cause of the empty Settings page. Runtime task-stack high-water marks and true current/maximum CPU-loop execution times are not exposed by this artifact.

The verbose test artifact includes active HTTP heap diagnostics, whose integrity checks and UART output affect timing. Its incomplete compile-time CM source-hash provenance remains as documented above. No firmware rebuild, upload, OTA, device reset, configuration/NVS write, production firmware deployment or live HA change occurred during this follow-up. All temporary test observers and serial handles were closed; the separately authorized PC bridge for test-prefix MQTT visibility remained active.

Local-only, ignored evidence is under `.local/discharge-budget-export-20261022/concurrent-web-20261022/`: `acceptance.md`, `baseline-failure-summary.json`, `final-result.json`, `http-default-heap-diagnostics.json`, and `attempt-02/gui-ui-observations.json`. These private artifacts are not part of the repository checkpoint. The failed web-stability findings remain open; no fix or release acceptance is claimed.

## CM 4.9 settings-only USB acceptance

**PASS for the bounded dedicated-board test.** This later run preserves all
historical failures above. It does not establish their root cause, validate
weak-AP recovery, close an issue, or approve a production release.

### Frozen sources and installation

- SIL source: `feature/battery-energy-control`, checkpoint
  `440a8e7a54aaf2d3ee1cb60ec08331eb7e6fada7`, firmware version 5.2.0.
  Product sources were not edited for this test; the ignored fixture adds only
  test isolation and bounded serial instrumentation.
- CM: local uncommitted `4.9.x`, version **4.9.0 Unreleased**. All 120 files in
  the final CM source freeze matched the copied snapshot. Manifest SHA-256:
  `69ac09f1a7e265c98859665147208ffe658d14041081ee7881819de115d8587e`.
- Global flags: `CM_ENABLE_LIVE_UI=0`, `CM_ENABLE_SETTINGS_UI=1`. The actual
  compiled 01 frontend header SHA-256 is
  `daafc21e2cca6330bd728ae0a5e7b1ecbcde116d7ecf005cf3fcad08dee55a7f`.
- New binary SHA-256:
  `f9e562999307879d0766fd6cb8325475a2e669034ca8c10019989ac3a88f5013`;
  ELF SHA-256:
  `7576c35a039797ea0731823a98de2f8b137d017495494c07f820d838fdbb4b9a`.
  Binary size is 1,211,792 bytes; PlatformIO reports flash
  1,205,213/1,310,720 bytes (92.0%) and static RAM 84,000/327,680 bytes.
  All 437 prepared source/dependency inputs remained unchanged during build.
- The owner reassigned the physical port to **COM8**. MAC
  `3c:8a:1f:a7:22:7c` was read and verified before writing. Partition/selector
  inspection selected the existing `app1` at `0x150000`, size `0x140000`.
  Its entire previous image was backed up, SHA-256
  `74bccb8b8b18bcb732f20e8da8e57d74e7fbd9958f9d6959d721002ec071983a`.
- USB wrote only the selected application image and verified its hash. No OTA,
  bootloader/partition-table/selector/NVS image transfer or factory reset was
  performed. Fresh UART identified the expected MAC, Live=0, Settings=1 and
  mocked transport; completed HTTP capabilities confirmed OTA disabled.

The fixture uses `[MOCKED!]` RS485 (`SIL_MOCK_TRANSPORT=1`) and explicit local
isolation (`SIL_DISCHARGE_BUDGET_HIL=1`). It applies broker/test limits and MOS
manual mode in RAM without saving them. The broker is only
`192.168.2.220:18884`; plant inputs are relayed read-only. No physical inverter
or MOS operation, production firmware installation, HA edit, commit or push was
part of this phase.

### Actual rendered and runtime checks

The tested flow was: device page -> earliest enabled Settings click ->
authentication -> populated fields -> two simultaneous pages -> category
navigation and repeated reloads. Existing Chrome ran through the existing
Playwright installation because the Browser plugin was unavailable. The real
page was `http://192.168.2.126/`, desktop viewport 1280 x 900. Only reads and
required `/config/auth` authentication were allowed; settings mutation and
switch actions were blocked. Screenshots were visually checked.

| Check | Observed result |
| --- | --- |
| UART/HTTP window | 600.331 seconds, 120 diagnostic samples; no observed reset, panic, failed heap-integrity check or HTTP failure. |
| Actual browser | 360.212 seconds, two concurrent Settings pages, 32 state checks and 14 reloads; populated fields returned each time, no Live navigation, no framework overlay, zero JavaScript errors or console warnings. Flash stayed disabled as configured. |
| Complete HTTP responses | 139 sampler requests: 115 capabilities responses and 20 configuration responses returned 200; all four disabled Live resources returned 404. Configuration median/max: 0.252/0.601 seconds; capabilities median/max: 0.077/0.669 seconds. |
| Current internal heap samples | 101,260..151,272 bytes; sampled largest free block minimum 42,996 bytes. |
| Current default-capability heap samples | 57,380..107,392 bytes; sampled largest free block minimum 40,948 bytes. |
| Reported SDK low-water values | Internal 85,668 bytes; default-capability 41,808 bytes. These are historical capability low-water reports, not instantaneous free heap. |
| Stack reserve | Loop-task minimum 4,444 bytes; AsyncTCP-task minimum 12,824 bytes. The installed ESP32 FreeRTOS header defines these high-water values in bytes. |
| True main-loop wall time | 48,720 passes; work mean/max 12.224/153.978 ms, entry-period mean/max 12.328/154.031 ms. Measurements include preemption. Five-second diagnostic reporting is excluded from work timing but included in the next entry period. |
| Recovery after browser closure | Sixteen samples at least 30 seconds after closure; median free internal/default heap 149,856/105,988 bytes. |
| MQTT continuity | No device disconnect in the ten-minute window. 293 SetValue publications: median/max interval 2.040/2.490 seconds. Planning/State median/max interval 2.043/2.910 seconds. These are publication intervals, not CPU execution times. |
| Real-source plan | Five fresh valid plans in a separate 300.029-second exact-output subscription. Nine distinct complete device generations matched all fields and all 144 values at the normal-broker subscriber. The final raw normal-broker packet also matched device bytes; retained readback was 9.338 seconds old. Slot duration was 900 seconds and budgets stayed within the 0..900 W test limit. |
| Radio environment | RSSI -44..-41 dBm. The earlier -85/-88 dBm preferred-AP/recovery condition was not reproduced. |

The combined observer initially requested an overbroad subscription rejected by
the strict test ACL. Its UART/HTTP evidence remains valid; a separate observer
then verified SUBACK and the exact `/out/#` subscription for five minutes. Raw
broker events independently cover MQTT output during the complete browser run.
The existing, harmless empty retained `/out/System-Info` cleanup was rejected
14 times by this deliberately strict fixture; `/ESP`, `/WiFi`, control and plan
payloads were delivered. Source inspection confirms the empty cleanup. There
were no denied device command attempts or unexpected publication denials.

The control snapshot log was throttled to roughly ten seconds in this run; its
cadence is not a control execution-time measurement. The main-loop and MQTT
metrics above are measured separately. No long-run, physical-delivery, BMS
switching, production HA rendering or weak-Wi-Fi acceptance is implied.

### MQTT visibility, evidence and continuation

The normal broker remains `192.168.2.3`; the logical topic remains
`SILTest/COM7/Planning/DischargeBudget` despite the physical COM8 reassignment.
Only the previously allowed plan, `Bridge/status` and `Bridge/metadata` test
topics are published there. `active-test-image.json` now supplies the correct
new binary/source/UI-mode provenance through a narrow metadata override. The
bridge allowlist and real-source proof gates were preserved. No synthetic
schedule was injected during this run; natural 20:50/20:55 UTC real batches
qualified the new device output.

All private evidence is retained below `.local/cm49test-20261022c/`:
`final-artifact.json`, `source-manifest.json`, `cm-source-freeze.json`,
`boot-bank-verification.json`, `active-app-before.bin`, `usb-flash-result.json`,
`boot-verification.json`, `measurements.json`, `normal-mqtt-readback.json`,
`observation-01/`, `browser-01/` and `mqtt-01/`. Earlier preparation directories
without suffix and with suffix `b` were not flashed; suffix `c` is authoritative.
The CM four-mode builds, source freeze, symbol/route/graph checks and mocked
browser acceptance remain under the sibling CM `.local/ui-feature-matrix/`.

Observers, serial handles and test Chrome closed after the bounded runs. The
new isolated image remains on the dedicated test board; the hidden bridge
(PID 13552) and isolated broker (PID 38532) intentionally continue the requested
MQTT visibility. These PIDs are snapshots: verify them before reuse. Lifecycle
and stop instructions are in the existing `.local/.../live/README.md`.

The resumable handoff and test-tool commands are also in the current chat
workspace `cm49-autonomous-handoff.md` and `sil-cm49-test/`. No automatic commit,
push, integration or production rollout is queued. Further adoption of the SIL
build profile or CM publication remains a separate owner decision.

## Vite 8.3.4 settings-only USB smoke test — 2026-10-23

The owner explicitly authorized importing Dependabot PR #82, a short
settings-only test on the dedicated SIL test ESP, and a local checkpoint after
success. No push, PR update, production deployment or extended test was requested.

- CM `4.9.x` local merge commit: `9a0413f9c35622cdd31b45391e26e5e9e5a028b3`.
  Vite is 8.3.4; previously accepted shell-quote/brace-expansion updates remain.
  The default embedded page was regenerated with Live=1, Settings=1.
- Test image: current SIL 5.2.0 sources with CM 4.9.0, Live=0, Settings=1,
  existing RAM-only isolated broker, mocked RS485 and OTA disabled. No SIL
  product source, production setting or BMS/MOS control was changed.
- The same dedicated COM8 MAC `3c:8a:1f:a7:22:7c` and active application bank
  were reverified. The preceding application was backed up; USB wrote only
  `app1` at `0x150000` and verified the written hash. No NVS, selector,
  partition table or bootloader write was performed.
- Firmware SHA-256:
  `7a9581eaf270072dcfb25d2372c83ddb050f73b076e74f47eb7b397c315757ce`.
  Flash usage: 1,190,925/1,310,720 bytes; static RAM: 84,000 bytes.
- All 120 CM source-freeze entries and 437 prepared inputs matched; compilation
  succeeded without changing those inputs. All 42 JavaScript unit tests passed.
- Fresh UART/HTTP confirmed the MAC, new boot and required isolated UI modes.
  The 90.489-second observation had 18 diagnostic samples and 24 complete HTTP
  checks, all successful. No Guru Meditation, panic/assert, reset, heap-integrity
  failure or uptime regression was observed. Minimum sampled default free heap
  was 103,824 bytes; loop-stack reserve was 4,444 bytes.
- Actual Chrome at `http://192.168.2.126/`, 1280 x 900: 30.307 seconds, two tabs,
  four rendered-state checks. WiFi, MQTT and Limiter forms were populated;
  category navigation worked, Live was absent and Flash was disabled. The
  screenshot was visually inspected. No JavaScript errors, console warnings,
  framework overlay or unintended settings mutation was observed.
- Evidence: `.local/vite834-smoke-20261023/` in SIL, including build/USB/boot
  results, `observation/`, `browser/` and the prior application backup. Frontend
  build/source-freeze and unit-test evidence is in the same-named CM directory.
  Browser used existing Playwright/Chrome because the Browser plugin was absent.
- Existing test MQTT routing was preserved. The bridge restarted with updated
  verified image metadata; PID 35212 and broker PID 38532 are snapshots.
  Observers, serial handles and test browser closed after the short run.

Result: **PASS for the requested short Settings-only smoke test**. The other UI
feature combinations and long-run behavior were not revalidated for Vite 8.3.4.
