# SIL / SBT / HA integration acceptance — 2026-10-20

## Scope and current gate

Owner-authorized implementation of the [single integration plan](../superpowers/plans/2026-10-20-sil-sbt-ha-integration.md). The prior documentation-only stop was explicitly superseded. No production ESP upload, NVS/partition erase, physical MOS command, automatic commit, push, merge, release or branch cleanup is authorized or performed.

Firmware implementation, focused host tests and four current consumer builds passed; HA migration and corrected publication are live verified. **Final device acceptance remains incomplete:** the fresh SBT normal-UI observation still had HTTP timeouts. The additional concurrent overload test also failed. Exact per-image outcomes are recorded below. No production readiness, complete HTTP repair or overnight stability is claimed. A bounded Windows packet capture is prepared as the next diagnostic step; Windows denied capture without administrator rights.

## Repository and dependency identity

| Repository | Branch | Pre-checkpoint base HEAD | Working version |
| --- | --- | --- | --- |
| SolarInverterLimiter | `feature/battery-energy-control` | `223c04040ce5809f2a6e91818b0a5496c1cd4820` | 5.1.0 |
| SolarBatteryTelemetry | `feature/move-sil-planning` | `cbe245fd1379f758eaa0166e9976e8e89cfa96f3` | 1.2.0 |
| ConfigurationsManager | `feature/ble-compact-runtime` | `dc40899f42f0ea76f7b5db6cfd03520f822eab15` | 4.7.1 |

On2026-10-21 the owner requested one local intermediate commit per repository to preserve the coordinated integration state. These checkpoints do not clear the failed SBT device gate or authorize publication. The table records the base HEADs used before those checkpoints. CM 4.7.1 includes the narrow HTTP sender correction explicitly authorized by the owner after transport evidence was obtained. The sibling CM symlink is a local development dependency, not a reproducible released pin. Existing checkpoint branches were deliberately reused to preserve owner context.

## Build artifacts

SHA256 values identify the exact local artifacts; they do not imply production deployment. All sizes are bytes.

| Image | Flash | Static RAM | BIN SHA256 | ELF SHA256 |
| --- | ---: | ---: | --- | --- |
| SIL normal usb | 1,231,957 | 81,232 | `85EBA6FA9B4F094F0A96ADDCB278CE0F6456FAA5F26A00A45EA72D2BF96E9241` | `B18D2F799CC434B8E20876213270BF6A85E84415852CFDC931385C1F8C31EE60` |
| SIL isolated mock | 1,230,897 | 81,208 | `75AE5654DFE8DA31DBF730F1A9871B9DD18D23C57D00CF9D7FDF76F13195635D` | `E2A34623B66E2873D43CFC6E5F0C14C226C80F098214564B8C9D2F10B70D32A3` |
| SBT normal usb | 1,378,725 | 80,720 | `11E260E5DC1F7473BB92300C8F491093FD005941371AB0E95F5DFAECE0C32FF4` | `014B43C27737F29EDE2D967EFE4958FB1C03A2721B83A7C364640A61C93FFC30` |
| SBT diagnostic acceptance | 1,380,129 | 80,992 | `010B4123ABA1B6350DCD72C63EA2BE94CC634876D615385413F0C3DB6A5690A9` | `F9F2F867ECFBA436614AE7BCAB35148A3CBD1994DAB9DFF87DBD5A6A2621033E` |

Equivalent SIL pre-integration usb baseline was flash 1,240,381 / static RAM 75,304. Final normal usb is -8,424 flash / +5,928 static RAM. SBT baseline was 1,374,129 / 80,464; normal SBT adds 4,596 flash / 256 static RAM. Diagnostic SBT has a separate cost and matching ELF. SIL normal default app slot is 1,310,720 bytes; the actual test board has 1,835,008-byte app slots. No partition table was written.

All four listed consumer builds passed against the same frozen CM 4.7.1 source before the requested checkpoints. Before the CM4.7.1 transport/runtime corrections, the same final SIL main translation unit also compiled with `SIL_DEACTIVATE_BME280_CARD=0`; this proves the alternate branch compiles, not a second physical-device acceptance.

## Focused verification and independent review

Changed SIL receiver/sibling publisher contract, CurrentControl, FuturePrices/PV, PvForecast and compact metadata suites passed. SBT production health-loop, JBD and changed publisher/runtime suites passed. Host ArduinoJson pool capacity128 matches ESP32 geometry; the product's 4096-byte arena was not enlarged to accommodate the host ABI.

Independent review reported three Important and two Minor issues, all corrected in the bounded fix pass: recovery now pauses PI/economic dynamics and restarts from actual held output; semantically invalid core measurements cannot renew grace or overwrite last-good values; the first authenticated collector session fences previously unowned sequence state; explicitly absent NTCs become unavailable; invalid timeout settings normalize visibly to180. Parent reran affected host suites and inspected the corrections. Regression traces cover 900→held100→qualified100→150→200 W, finite-but-invalid SOC/voltage/current, retained high-sequence startup, and missing NTC with otherwise fresh BMS data.

The CM sender's focused host suite passed all 13 scenarios (six existing ownership/lifetime cases and seven MSS/event/timeout/pipeline cases). Independent review found no Critical or Important issue in the correction; the upstream same-tick timeout limitation is documented below.

No claim is made that this fixes historical overnight CPU resets. Full MOS/protection coordination and independent hardware behavior after controller failure remain outside this increment; reviewer declined to judge those unimplemented or unproven capabilities.

## Physical test target and initial SBT result

Each write was preceded by target verification: COM7 CH340 VID1A86:7523, location1-6.1, ESP32-D0WD-V3 rev3.1, MAC `3c:8a:1f:a7:22:7c`, matching the test IP192.168.2.126. Production addresses192.168.2.122 and192.168.2.21 were excluded. Existing partition/OTA data were read; app0 at0x10000 was active. The old entire app0 image and matching known local ELF were retained privately for rollback. Application-only writes at0x10000 were hash-verified; no NVS, bootloader, partition or OTA-data write occurred.

SBT test publication root `SILTest`; control enabled/ready/pending all false. No plant source was available: BMS `disconnected_or_timeout`, Victron `waiting_for_advertisement`. All Live and Settings pages rendered; NTC1/2 were unavailable rather than fabricated. Reload readiness was slow (>15s on some attempts). Screenshot evidence is stored outside Git in the task visualization directory.

Initial SBT diagnostic observation ran 830.313 seconds, ending18:19:39.449 UTC (20:19:39.449 Europe/Berlin), including305.029 seconds after final UI stress at18:14:34.424 UTC. 162 complete HTTP observations and monotonic uptime were recorded, with resetreason1 and workerReady true. However, two additional HTTP reads failed at18:07:59.873 and18:08:06.106 UTC: `IncompleteRead(512 bytes read, 2434 more expected)` and `IncompleteRead(145 bytes read, 2801 more expected)`. This is a failed acceptance run, not a pass based on uptime. The serial log has no observed Guru/Backtrace/assert/corrupt/HEAP-FAIL marker; minimum reported heap reached7972 bytes. Continued diagnosis retains that evidence and exact ELF.

## Historical initial one-MSS CM4.7.1 candidate and HTTP probe

The private CM 4.7.0 SBT probe ended at 18:43:28.703 UTC after 614.891 seconds,
including 305.259 seconds after the final UI stress. Of 122 HTTP attempts,
121 completed and one timed out after 9.5 seconds; the longest successful request
was 5.781 seconds. Of 145 bounded responses, 144 completed and one closed early.
For that `/runtime.json` response, 2946 body bytes were declared and 1536 admitted;
1024 body bytes were acknowledged, leaving the next 512-byte block pending.
At device millisecond 483508, actual AsyncTCP ACK timeout and the HTTP timeout
callback closed it after 5025 ms with 5248 bytes of TCP send space, no pending
flush and no output failure. Heap was 26124–26840 bytes with an 18420-byte largest
block and WiFi status 3. This proves one server timeout/partial-response path;
it does not establish why the ACK was absent or directly identify the original
two EOF events. Five additional RX timeouts belonged to requestless idle
connections. The probe minimum heap was 11396 bytes; final free/largest heap was
33292/29684 bytes, with no observed panic or heap-failure marker. The 61598-byte
UI response required 122 admissions and took 8.25–22.40 seconds.

The initial one-MSS CM4.7.1 candidate set a 15000 ms ACK budget once per bounded HTTP response and uses its
negotiated MSS for the outstanding/admission window: 1436 bytes on the observed
connection, at most 1460 bytes, with a 512-byte fallback for zero MSS. It retains
accepted-byte accounting and final-ACK ownership. At MSS 1436 the 61598-byte host
fixture needs 44 output operations, compared with 122 at 512 bytes. The maximum
additional queued TCP payload is 924 bytes per active response at that MSS,
plus one size_t in the response object. It does not change MQTT, source freshness,
MOS or OTA flow timeout policy, third-party transport code or WebUI retries.

This is not a universal 15-second cleanup guarantee: pinned AsyncTCP checks ACK
timeout only when the last TX timestamp is strictly later than the last ACK.
Same-millisecond ACK/send events can be ineligible; this upstream condition was
not changed. The host expiry fixture models one millisecond between those
events and covers the eligible branch. The root cause of missing ACKs and the
historical panics remains open. Final CM 4.7.1 SBT/SIL device gates are pending
and will be appended by the leading agent; no production or overnight-readiness
claim follows from the host tests or the four successful builds.

## Planning and wire resource ruling

The PV receiver owns independent 4096-byte JSON scratch plus712-byte active/pending state; host object4824 bytes. FuturePrices host object7424 bytes includes868 bytes of sort/prefix arrays. Expensive ordering/PV-prefix rebuilding is event/slot/revision/settings/material-cap/30-minute driven; signed measured-energy debits and current-slot time remain live in bounded O(144) arithmetic. Forecast credit is conservative: only known PV before the earliest future reserved target, capped by MPPT total output and remaining SOC headroom, after household load and conversion losses. It never increases the measured battery pool.

The owner confirmed no configured Victron current limit. Installed MPPT150/35 maximum35A is verified in the [official Victron technical specifications](https://www.victronenergy.com/media/pg/Manual_SmartSolar_MPPT_150-35__150-45/en/technical-specifications.html). SIL's qualified planning ceiling is35A times fresh measured battery voltage; no charger setting was changed. Unknown qualification can still supply0 through the planner API.

Corrected actual HA producer artifact (generation1792521000): six JSON chunks319..349 bytes,44 known slots,1735Wh. Mixed-order C++ activation was atomic and retained fixed expiry. Host optimized repeat parse measured approximately8.91 microseconds/packet; this is not ESP32 CPU timing. Comparative delimiter candidates203..249 bytes and hypothetical binary122 bytes/chunk omit a finalized binary framing/CRC contract; neither is deployed. JSON remains comfortably below768 bytes and avoids an unproven new binary protocol. Actual device peak parser CPU/heap has not been isolated from UI load.

## Home Assistant and remaining plant qualification

The new Shed Forecast.Solar entry is `01M5D5VEHZR1GR88THXH8C8BFS`:1840Wp,14.8-degree tilt, south180, existing Home location, damping0/0, no AC clip. A supported config-entry API corrected native UI integer truncation of14.8; no direct `.storage` edit was used. Exact six obsolete entries were removed after consumer/Energy migration and private rollback export. Energy has exactly one raw Victron PV yield source plus DC power; battery charge/discharge remain separate and only Shed forecasting is selected. See [migration evidence](../home-assistant/shed-migration.md).

Corrected inverter AC power is max(0,raw−2), unavailable propagates; a separate left-method integral with one-minute maximum subinterval records delivery without rewriting raw history. Installed HA integral behavior was inspected: unavailable cancels timed integration and recovery from unavailable is not bridged. A live induced outage, next sunrise and next-night acceptance were not performed.

Calibration requires an immutable complete original day forecast, matched reset-free full-day actual yield, live advancing acquisition proof, known unrestricted current configuration, and no full/float/absorption/current-ceiling curtailment. At least14 eligible completed days in30 are required; until then factor1.0/uncalibrated is explicit. Oct20 is excluded. Its reset-aware recorder increment was investigated, but the physical reset cause and a final exact whole-day actual total remain unqualified. No invented total or retrospective calibration permission is used.

Owner plant checks remain: actual BLE samples/NTC positioning, production timing and command response, source-loss/recovery and directional alarms on the plant, charger curtailment validity, day/night Energy behavior, calibration learning, and sustained overnight stability. No real hardware commissioning result is inferred from synthetic host fixtures or an unavailable-source test board.

## Owner handoff and rollback

Do not deploy the mock or diagnostic artifacts to production. After all device gates pass, the normal usb BIN/ELF pair above is the candidate for the owner to deploy separately per repository, retaining existing device-specific NVS and partition layouts. Confirm SBT input root, forecast topics, MAX/Idle/MIN and180-second timeout before plant use. SIL has no new physical MOS publishing path.

Private baseline app0 and per-image ELF artifacts are retained under ignored `.pio`/`.superpowers/sdd` directories; HA rollback exports remain under `/config/.codex-backups/sil-shed-20261020`. Rollback must select the matching app slot/partition and retained settings; no erase or blind restore across unrelated devices. At the initial device handoff no commit or Git integration had been performed; the later owner-requested local checkpoints are recorded above.

HA correction evidence: raw provider END timestamps are converted from validated previous-to-end intervals into actual UTC hours, including partial daylight edges. Schema2 migration privately preserves the old ledger and invalidates wrong-period references. BMS charge permission must be on and alarm bits zero, coupled to advancing source-health evidence. Python19tests pass; wrapper SHA256 `4C97D0D75A51B0257CA600130E55EC50D8181A3E21675EED04169ED81E894C1C`, core `C7C3AF85F1F1F1E7DF909EDBB2F7CA4D0578CEEA3A84DB85C360F2644219D3D0`, both independently compared with the deployed copies.

## Historical one-MSS failures and USB interruption

The unchanged application-only target identity/partition preflight was repeated before every image swap. The four historical one-MSS pairs are preserved privately in `cm471-0` through `cm471-3`; their hashes differ from the current table. SIL normal candidate is `.pio/build/usb/firmware.{bin,elf}`; SBT normal candidate is `.pio/ble-smoke/usb/firmware.{bin,elf}` (explicit isolated normal build directory). Do not select an older SBT `.pio/build/usb` output by filename alone.

- **SBT8s passed:** 120 complete responses, 0 recorded failures, 604.484s observed, 305.122s after last stress marker, ending 2026-10-20T19:27:01.029175+00:00 (UTC; Europe/Berlin UTC+02:00).
- **SIL8s failed:** 55 complete responses, 2 recorded failures, 304.906s observed, 305.110s after last stress marker, ending 2026-10-20T19:35:34.183916+00:00 (UTC; Europe/Berlin UTC+02:00).
- **SIL20s failed:** 55 complete responses, 1 recorded failures, 312.047s observed, 312.225s after last stress marker, ending 2026-10-20T19:44:29.554005+00:00 (UTC; Europe/Berlin UTC+02:00).

SBT: no observed panic/reset/heap-failure marker, monotonic uptime, lifetime heap minimum9988B, sampled free19688..35008B, largest block13300..32756B. All control gates false; BMS/Victron unavailable. This is no-data firmware acceptance, not real BLE positioning/plant commissioning.

SIL8s failures were at19:31:56.009UTC (10.344s total) and19:32:16.603UTC (9.406s), with stable uptime/resetreason1 and ample heap. An earlier recorder stopped on a marker-file read/write race; that partial trace was preserved separately and did not count as a continuous gate. A20-request diagnostic with20s client budget completed20/20, maximum0.803s, without reproducing the fault. The subsequent20s completion gate was declared before execution, exceeding the15s server ACK budget. It nevertheless failed one `urlopen error timed out` at19:44:09.547UTC (20.015s); another complete response took16.750s. Neither longer client waiting nor the CM correction is claimed to resolve the remaining delivery problem.

Actual SIL UI verified all Settings pages, MAX/Idle/MIN900/500/3 in order, timeout180 and normalization of invalid0/10000 to180, both NTC labels, no System/Temperature Live page, three Limiter heap fields and mock hardware output. Repeated normal-CM4.7.1 reloads reached the live display in approximately4.2..4.6s. Actual MQTT listen interval was0ms; default500ms throttling did not apply. Natural price and PV batches activated288 and144 slots on the device; PV revision1792524600, calibrated=false, nighttime offered/usable0Wh. The unchanged C++ price parser independently accepted all12 actual broker chunks in mixed order. Successful reception does not prove finite daytime PV credit or source-loss plant behavior.

The behavior-preserving SIL HTTP probe used private CM4.7.1/AsyncTCP/WebServer copies and unchanged shared product sources. BIN SHA256 `29B10150B3FF0684949A94D54BE6631ECC0E17750BDEC2F1CFB6C5A70E9EB7B8`, ELF `F70DA21C744FBC527A86FA60B87F715A9A8603639C304240F9FC0473309B98CF`; static RAM81208B, flash1229181B. Application-only upload ended19:45:39.948UTC. Early probe responses completed, including61598B UI in3.428s/44 admissions, negotiated window1436B and ACK budget15000ms. One requestless idle RX timeout was benign. The trace stops in a metadata ACK log at bootms34278; no classification of that incomplete observation is justified.

The host recorder then raised `SerialException: GetOverlappedResult failed (PermissionError(13, 'Zugriff verweigert', None, 5))`. Windows no longer listed COM7 as present (CH340 retained as nonpresent/Unknown); test192.168.2.126 became unreachable. This is an external test-connection blocker, not evidence of firmware panic or a proven cause of the earlier HTTP failures. Before that event, a separate90-ping comparison delivered90/90 replies to the ESP (3..460ms, average65ms) and gateway (0..1ms); ICMP does not exonerate TCP or establish a root cause. The owner was asked to check the cable/power; no further upload or continuous observation is possible until the target returns.

**At that interrupted checkpoint the physical image was the private probe.** The subsequent COM8 evidence below supersedes that physical-state statement. On reconnect, reverify identity/partition/slot and preserve trace boundaries before continuing diagnosis. Do not use this diagnostic image for production. Owner handoff is provisional pending the SIL gate; earlier failed runs remain failures. No production ESP, commit, push, merge or release was performed.

## COM8 resolution and subsequent matched CM faults — 2026-10-21

The owner explained that switching off the monitor removed USB power, then moved the board to COM8. The same ESP32 MAC `3c:8a:1f:a7:22:7c` was verified. This explains the external USB interruption, not the earlier HTTP failures. A stable-power one-MSS SIL probe still produced82 complete responses plus two partial EOFs over455.625s (305.072s after stress). For a2803B body,1436B remained unacknowledged while4324B TCP send space was free; the extra application window withheld the last298B until the15.007s ACK timeout. The peer received all admitted bytes. No add/flush/counting failure was observed; the missing-ACK cause remains unknown.

The current sender admits two negotiated MSS, at most2920B outstanding including headers, with512B fallback for zero MSS. This adds at most1460B per active response over the first candidate (1436B observed), plus TCP/pbuf overhead. Relative to512B, the observed increase is2360B, maximum2408B. Thirteen sender tests passed, including second-segment and complete2803B-body progress while body ACKs are withheld. Ownership/accounting/15s policy remain intact. The upstream same-tick timeout condition still prevents a universal15s cleanup guarantee.

The two-MSS-only SBT run exhausted heap and panicked. The preserved64KiB core SHA256 is `1ED8FED7B7B1BA572029E8BA8D8D3105CAA99B59D64331F7E1777ABA13243AB1`; matching ELF `958042BE00309985341C0881B428B39D51FB6BDE3DD458A728018CC0CCE1A7D3`. Embedded digest and independently computed CRC32 `8EE39DE8` matched. Immediate esp_timer panic was failed newlib/UART recursive-mutex creation while logging. Concurrent loopTask was failing an8B WebSocket close/control-node allocation. The actual client heap was outside the core, so complete retained-heap attribution is unavailable.

Matching code requested stale-client close on every loop after120s. Pinned ESPAsyncWebServer queues uncapped CLOSE controls without immediately changing CONNECTED status. CM now claims/removes expiry under its mutex using current activity/time, then closes outside it. Actual maintenance-body RED→GREEN covers10000 passes→one close, late Pong/deadline, removed-ID rejection, reconnect, callback reentry and rollover. Parent reran the suite; independent review found no Critical/Important defect in this correction.

A later concurrent test returned HTTP200 with zero bytes after a2960B allocation failed. Runtime bodies were2944B; actual WString rounds their copied allocation to2960B. The conditional String/const-char return forced that copy while the original/document remained alive. CM now directly returns the local String, detects document overflow/incomplete serialization, and returns HTTP503 for an empty provider instead of200/empty. Production-method/route RED→GREEN tests cover these paths, including disabled optional copy elision.

Subsequent runs still failed2308B allocations under concurrency. Runtime publication previously permitted32 queued payload copies per client. CM now reserves one pending runtime message per client before building data; saturated clients skip ticks. A public message wrapper delegates the existing transport and releases the shared atomic reservation after payload destruction on ACK cleanup/rejection/disconnect. Tests cover10000 stalled ticks, two-thread CAS, mixed healthy/stalled clients, fresh data after release, lifetime and failure paths. General event/text behavior is unchanged; this is not a global byte budget.

Backpressure alone still failed1622B/2308B allocations with largest block1524B, despite approximately7KiB free. The final candidate serializes HTTP/WS generation with a recursive mutex: HTTP waits only during provider/JSON construction; the WS/main loop uses try-lock and skips busy ticks. Documents and guard are released before transport handoff. One eligible Selected client uses the already-selected snapshot without a second projection document; multi-client/custom behavior remains. Real-thread, recursion, selection-equivalence (including OTA, empty, nested and null data) and document/guard lifetime tests passed. Parent reran these; independent review found no Critical/Important defect in the narrow source changes. These fixes do not constitute a successful device gate.

All four final pairs were rebuilt and preserved as `cm471-final4-0` through `cm471-final4-3`, matching table order. A frozen product-source hash manifest was retained and checked after testing. Every write verified test identity, unchanged partition/OTA data and exact application hash; only app0 at0x10000 was written. Earlier failed candidate/probe pairs remain separate. Production devices are unchanged.

## Exact final observations: original UI scope versus extra overload

Fresh normal-UI cohorts were declared before execution: all Live/Settings pages, three reloads and a runtime observer every5s, followed by at least300s after the last UI action. No additional manual parallel request pairs were injected into these fresh cohorts. The HTTP socket idle timeout was8s, not a total-request deadline. A progressing request may take longer; any recorded timeout still fails the cohort.

- **SBT normal UI FAILED:** 22 complete observations, 3 recorded failures. 2026-10-21T05:08:53.756963+00:00 to 2026-10-21T05:14:49.047465+00:00; 355.296s total, 305.109s after last UI stress. Maximum request duration8.453s. Sampled free/min/largest ranges (bytes): `{"heap":[6384,37080],"min":[2864,28136],"largest":[1716,32756]}`.
  HTTP failure 2026-10-21T05:10:37.488088+00:00: `timed out` after8.453s.
  HTTP failure 2026-10-21T05:10:57.668745+00:00: `timed out` after8.172s. The third failure is the later HEAP-FAIL marker; no new panic/reboot was observed in this cohort.
- **SIL normal UI PASSED:** 77 complete observations, 0 recorded failures. 2026-10-21T05:18:34.013156+00:00 to 2026-10-21T05:25:05.122004+00:00; 391.110s total, 305.076s after last UI stress. Maximum request duration3.500s. Sampled free/min/largest ranges (bytes): `{"heap":[107660,118836],"min":[90108,113868],"largest":[61428,69620]}`.

Times are UTC; Europe/Berlin UTC+02:00. SBT control gates remained false with unavailable BMS/Victron sources; this is not BLE/NTC commissioning. SIL remained in mock transport, no hardware TX/MOS actuation. Current test image is isolated SIL. A completed300s interval does not convert failures into a pass.

The separate final4 overload cohort remains failed and preserved: normal UI plus the5s observer and three extra concurrent UI/runtime GET pairs. One of six stress requests timed out after12.846s; two observer reads also timed out. First2308B failure had12788B free but2292B largest block, followed by1460B failures and thousands of retries. Diagnostic count reached at least n4480; sampled log lines are not the total count. Minimum free heap4080B. No new panic was observed; blocks returned after quarantine, which does not identify the exact releasing subsystem or prove a leak. The fresh normal SBT timeout occurred with approximately22KiB free/18KiB largest block and no heap-failure marker, so low contiguous memory alone does not explain every delivery failure.

**Device acceptance and production readiness remain open.** The normal pairs identify the reviewable implementation, not cleared production releases: SIL `.pio/build/usb/firmware.{bin,elf}`, SBT `.pio/ble-smoke/usb/firmware.{bin,elf}`. Do not deploy mock/diagnostic images. Preserve device-specific partitions/NVS, input/forecast topics, MAX/Idle/MIN and180s deadline. Existing rollback app/ELF and HA exports remain available. No automatic commit, push, merge, release or branch cleanup occurred.

Next diagnosis needs packet/ACK evidence and actual allocation callers/living responses rather than another guessed limit. A bounded Windows PktMon helper is prepared for only test192.168.2.126 TCP80, nominal90s (configuration capped at110s), private truncated packet output that may include HTTP fragments, refusing foreign capture/filter state. The timer does not guarantee a hard stop if the process hangs or its console is terminated. Status/filter operations initially returned AccessDenied without administrator rights. The owner subsequently authorized elevation. The UAC launch was canceled at05:51:37 UTC on2026-10-21 (`Der Vorgang wurde durch den Benutzer abgebrochen.`); no elevated helper output, packet file or capture start was observed. The owner is away from the PC and cannot confirm UAC, so elevation is not retried. Authorization is present; the remaining prerequisite is the physical UAC confirmation. Subsequent resource admission must account for full response lifetimes, receive/control headroom and recovery after overload. Public AsyncTCP close(true) ignores its argument and is not abort; callback/queue lifetime risks prevent a blind abort substitution.

Real-plant source-loss/alarms, NTC positioning, sunrise/night Energy, Oct20 reset/full-day accounting,14-day calibration and overnight stability remain separate. Isolated ESP32 parser CPU/peakheap and equivalent runtime baseline are still unmeasured. HA calibration stays neutral until qualified; no retrospective energy total is invented.


## No-admin diagnostic continuation: allocation callers and new probe panic (2026-10-21)

The approved Windows elevation attempt did not start a capture. A quiet final4 SBT comparison ran05:49:03.205–05:53:03.321 UTC:240.125s,48 complete HTTP responses, zero recorded failures or critical serial markers. It had no completed UI-stress protocol and is not the required device gate.

A separate ignored private source/profile copy preserved the frozen final4 SBT/CM and pinned SDK sources. It added16 immutable allocation records (four each for2308/1460/1622/other), eight PCs per record, immediate guarded stack walking and later bounded ROM output. It adds1168B static RAM and1576B flash versus final4 diagnostic; this changes memory/timing and is not acceptance firmware. Independent source review found no Critical/Important issue. BIN SHA256 `A64732ACCA2F73EEBC2A753A25D7E4ADBC34D2556A8C4AB91EFBD767E93482F1`; matching ELF `F97B16FDA7D7FC197F9E52A2F06CB5DE72D0410F6E4B307E66BFD3B1CA089C36`.

On COM8 the private probe's normal UI cohort ran06:06:26.883–06:12:54.061 UTC:387.172s, including305.029s after all Live/Settings pages and three reloads.78 complete HTTP observations, zero failures/critical markers; sampled minimum heap4804B. Reloads took2.053/1.421/1.441s. BMS and Victron remained unavailable, not physically accepted. This bounded diagnostic cohort does not supersede the failed final4 normal-UI gate.

A separate same-boot overload cohort began06:13:02.275 UTC. Six extra simultaneous GETs (three root UI, three runtime) started06:13:26.547 UTC; five timed out and one received a connection reset. The recorder stopped further periodic HTTP after the first critical marker and retained serial through06:15:02.395 UTC. A StoreProhibited panic and reboot were captured. Additional requests ended and the browser was disconnected; no sustained retry was started.

Five intact allocation records, decoded with the exact private ELF, establish these producers:

| Failed size | Actual decoded path | Evidence boundary |
| --- | --- | --- |
|2308B|`malloc -> wifi_malloc -> esf_buf_alloc_dynamic -> esf_buf_alloc`|WiFi buffer, not a proven WebSocket payload allocation in this event|
|1622B|`malloc_internal_wrapper -> esf_buf_alloc_dynamic -> esf_buf_alloc -> ic_ebuf_alloc -> ieee80211_output_process`|WiFi output path|
|1536B|`mem_malloc -> pbuf_alloc -> tcp_pbuf_prealloc / tcp_write`|Higher HTTP/WS/MQTT caller is outside the eight captured frames|
|1460B|No intact captured record|Unassigned; historical size alone is insufficient|

All five records were depth-limited; four interleaved/corrupt UART records were excluded rather than reconstructed. The earlier2307+1 size match was a hypothesis, not evidence that historical2308B failures came from WebSocket messages. The current decoded2308B record specifically identifies WiFi. These stacks identify failing allocations, not all live resources that exhausted/fragmented the heap.

The new64KiB raw core SHA256 is `AA1CC8543708F51D3F6B53EC4F20731EE975E2E238DAB30597EFF7B5237F89B3`; its CRC32 `C57516F0` and ELF digest match the private probe. Independent decoding places the immediate fault in pinned AsyncTCP `_tcp_poll` at0x400dae00: `malloc(36)` returns zero, then the next store writes `LWIP_TCP_POLL` through that null pointer. The source allocates `lwip_event_packet_t` and dereferences it without checking. This is a distinct, evidenced third-party low-memory crash path, not a fault in the probe's stack walker. It does not establish the preceding heap-retention cause or prove that a null guard alone repairs HTTP delivery/overload recovery. The probe's additional RAM cost limits direct production comparisons.

The exact final4 diagnostic SBT artifact from the build table was restored application-only on COM8 at06:17:13.871 UTC, after identity/partition/OTA/hash preflight. A complete2944B runtime read confirmed root `SILTest`, worker ready and control enabled/ready/pending all false. The browser remains disconnected. No production upload, NVS/partition erase, commit, push or release occurred. Product source hashes still match the frozen12 SIL/4 SBT/11 CM file manifest. Packet/ACK evidence, full retained-resource accounting and a passing SBT device gate remain open.
