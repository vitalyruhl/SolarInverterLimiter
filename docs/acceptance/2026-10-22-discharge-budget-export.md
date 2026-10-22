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
