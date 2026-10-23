# SBT receiver validation - 2026-10-16

## Scope and publication boundary

The completed SIL 4.3.2 / ConfigurationsManager 4.6.0 upgrade was published separately in PR #14, https://github.com/vitalyruhl/SolarInverterLimiter/pull/14. Upgrade commit `2e7455df241df866e101676e9442e1b77ffb9d8b` was merged into local and remote `main` at `97f4fdc45550bd2dff264731d22acee88fe09602`. The merged upgrade branch and the already-merged local `feature/update-config-manager-4.2.2` branch were removed after ancestry checks; stale remote tracking refs were pruned. Main was preserved.

The new receiver work is uncommitted on `feature/sbt-telemetry-inputs`, based on that main revision. This phase is authorized for reception and locked MOS preparation on COM7 only. No production OTA, real MOS command, new control policy, commit or push is included. Production SIL remains 4.3.2 at `192.168.2.122`.

## Hardware isolation

Test target: COM7 / static `192.168.2.126`; WiFi credentials, static network configuration and partition/NVS layout are preserved. RS485 is saved disabled. Before enabling MQTT, the saved client ID was set to `SIL-test-COM7-20261016` and publish root to `SILTest/COM7`. Existing inverter telemetry can therefore only publish under the test root. SBT is a separate subscribe-only input, default root `SolarBatteryTelemetry`.

MQTT listen interval is saved as 50 ms on this test device. The initial 500 ms setting services one PubSubClient packet per interval and delays multi-topic bursts. The shared CM source and production-device settings were not changed. Receiving throughput remains a configuration requirement before any future production rollout.

## Host and current artifact checks

- Focused C++17 receiver tests pass under LLVM Clang with `-Wall -Wextra -Werror` using the resolved ArduinoJson dependency.
- Coverage: all 29 channel types, negative current, strict booleans/unsigned values, malformed/null/oversized/embedded-NUL payloads, health baseline/sequence progress/replay/restart/timeout, offline/reset, and locked disable-envelope preview with session/ID/expiry validation.
- The embedded-NUL regression was observed failing before correction and passing after correction.
- USB build succeeds with unchanged CM pin `554869da7efc98e0c1874e74bf0c78d4bcd1664a` (4.6.0), LTO, no OLED or GUI/MQTT logging, normal UART Info and BME280 retained.
- Program sections: 1,210,277 bytes; static RAM: 63,416 bytes. Firmware binary: 1,216,848 bytes. OTA-slot binary reserve: 93,872 bytes of 1,310,720.
- Current corrected artifact SHA-256: `09976373fde1b5b7af259b6f4ce8647796081acc838200b59f07c346f4fd6650`, saved locally as `.pio/sil-4.4.0-receiver-snapshot-fixed.bin`. This exact artifact is restored on COM7 and passed the final hardware checks below.
- Earlier artifact `4a9a0a59f106acf97bd9cf9ed46052b922a5e82d3079ced2318e41c173151d93` was USB-written at `0x10000` with flash hash verification. No NVS erase or partition change. Its stability evidence does not validate the later availability correction.

## Observed intermediate failures

The first mocked burst check incorrectly assumed delivery within 500 ms; the stored CM listen interval was itself 500 ms. The bounded acceptance helper now waits for the expected state, and the isolated test device uses 50 ms.

An initial concurrent 180-second HTTP/serial probe had 14/19 successful HTTP samples, with a roughly 50-second HTTP gap. Uptime remained monotonic when the device returned; UART showed one controlled boot and no Guru Meditation or backtrace. This is an observed failed stability run, not a crash diagnosis. A later sequential acceptance run and final stability results are recorded separately below; the cause of that earlier gap has not been established.

## Superseded mocked hardware acceptance

The isolated MQTT helper can publish only under `SILTest/Mock20261016` and rejects `/set` suffixes. The test temporarily switches only the test ESP input root, then restores `SolarBatteryTelemetry`. Mock payloads are not real BMS measurements or MOS operations.

Nine checks passed on COM7: initial health cannot prove freshness; advancing health validates signed measurements; independent Victron health/state; malformed measurement unavailable; null is not zero; even ready remote control leaves SIL locked; unchanged fields survive deadband silence; missing heartbeat expires all source values and readiness; offline source unavailable.

The initial successful mock run preceded the embedded-NUL correction. The same nine checks passed again on the final artifact, including all four Required additions and control-result observation. Both test runs restored the real input root.

## Earlier artifact stability and current acceptance gap

Earlier artifact (hash `4a9a0a59...`), standalone 180-second boot/HTTP/MQTT probe: **19/19 HTTP samples passed**, one controlled boot, zero Guru Meditation messages and zero backtraces. Uptime increased from 1,145 to 179,634 ms; sampled free heap ranged from 107,256 to 114,220 bytes and ended at 109,472 bytes. Five normal UART Info messages included reset reason, setup start, WiFi setup, BME280 readiness and setup completion. MQTT connected after startup and remained connected in subsequent samples. The previous intermittent HTTP gap did not recur during this run; its cause remains unproven.

Earlier mocked hardware acceptance: **9/9 checks passed against an incorrect availability fixture**, superseded by the contract correction below. A fresh configuration read confirmed RS485=false, MQTT=true, the unique test client and publish root, 50 ms listen interval, and input root `SolarBatteryTelemetry`. Live SBT health publication is pending; the updated SBT source stayed offline after its separate OTA attempt. SIL correctly observed the actual root offline transition. The subsequent live observation was interrupted to release COM7 for the SBT startup investigation.

## Independent review correction

Independent review found that the initial source/control availability fixtures used the root-status vocabulary incorrectly. SBT `src/telemetry/TelemetryPublisher.h` publishes source `available`/`unavailable` and control `available`/`unavailable`/`disabled`; only root `status` uses `online`/`offline`. The initial parser would therefore never establish source freshness with the real source. Earlier mocked passes are not evidence of that compatibility.

Corrected both parser and fixtures, with the exact source vocabulary first failing a host test and then passing after the fix. Added a cross-repository host integration test that compiles against the actual sibling SBT publisher, without modifying SBT or using any network/device transport. Its real serialized output is passed directly to the SIL receiver: all 26 supported fields accepted, three unsupported/derived fields unavailable, source availability changes and control available/disabled transitions accepted. Both disable previews also pass the actual SBT command-envelope parser in memory with distinct IDs and correct charge/discharge targets; duplicate replay is rejected, and real SBT result serialization with unknown values is accepted. No command is queued, transmitted or applied. SIL remains command-locked. Run with `./tools/test/run-sbt-receiver-tests.ps1 -SbtRepository ../SolarBatteryTelemetry`.

At that stage, the availability-corrected firmware built successfully but hardware acceptance was pending. COM7 was handed back for the SBT startup investigation before the subsequent restore and snapshot-lifetime correction described below.

Current SIL test NVS was saved before handoff as ignored `.pio/com7-sil-receiver-nvs.bin` (20,480 bytes from `0x9000`, SHA-256 `e3358f435616f30cb26fea203ec411e861629f0b707ef2e5af2aa29ed1988acd`). The original full-flash backup and earlier SIL image remain available locally. Backups contain private configuration and must not be committed or printed.

## Restore after the SBT diagnostic handoff

COM7 was released by the coordinator for SIL restoration. Before writing, all three inputs were checked against their recorded SHA-256 hashes: original 4 MiB flash backup, saved current SIL NVS, and corrected application `495c9995...`. The interim SBT diagnostic NVS was preserved locally as `.pio/com7-sbt-diagnostic-nvs.bin`, SHA-256 `f6e3e78ec901bd4e222cc37357ca9b4d33c133b4bcb59458e24e28141186fdcf`; no private values were printed.

A 4 MiB restore image was composed from the original flash backup, replacing only NVS bytes `0x9000..0xDFFF` with the saved SIL NVS and the application at `0x10000` with the corrected firmware. This restores the original bootloader, partition table and boot selection together with the isolated SIL test settings. The full USB write completed with flash hash verification. Initial readback over HTTP reported SIL 4.4.0, static IP `.126`, WiFi and MQTT connected, free heap 110,424 bytes, and MOS commands locked. Corrected firmware stability and MQTT acceptance results follow below.

## Repeated hardware failure and owned snapshot correction

The restored availability-corrected artifact `495c9995...` completed the 180-second boot probe with 17/19 HTTP successes. Only the initial 0/10-second startup probes timed out; all 17 probes from 20 to 180 seconds passed, MQTT was connected, minimum sampled free heap was 108,616 bytes and final heap was 110,196 bytes. UART showed one controlled boot, normal Info setup completion, no Guru Meditation, backtrace or watchdog message. These results establish startup and steady operation for that window, not overall acceptance.

The corrected MQTT mock then failed during the deadband check, after the control-feedback/locked-SIL check had passed. HTTP timed out again and the final attempt to restore the input root also timed out. The test did not pass. There was no concurrent UART capture during this failed mock; no crash/lockup cause can be inferred. The previous `.pio/sbt-mock-acceptance.json` file is from 11:57 and is explicitly superseded, not evidence of this run. COM7 was released again; its saved SIL NVS remains the authoritative restore source with the real input root.

Static investigation reproduced an independent SIL snapshot-lifetime bug: `Receiver::writeRuntime` passed const member arrays to ArduinoJson, which borrowed their storage. After the receiver mutex was released, a receiver reset changed `control_reason` in the already-created JSON snapshot from `unique_before_reset` to `not_received`. HTTP serialization occurs after the provider returns. The same issue affected control result and source reason arrays. Three assignments now explicitly pass pointers so ArduinoJson copies the text while the existing receiver mutex is held. A repository regression test fails before this correction and passes afterward, including control reason, control result and health reason preservation across receiver reset. Other contract tests and the firmware build also pass.

This proves and fixes the SIL aliasing defect; it does not yet prove that this defect caused every observed HTTP gap. A separate potential race exists in the unchanged CM MQTT diagnostics provider: it reads `lastTopic_`/`lastPayload_` while the main-loop MQTT callback replaces those Arduino Strings without a common lock. No shared CM source was changed. The next hardware comparison will isolate selected `sbt`/`system` runtime groups before a targeted full-runtime comparison.

The diagnostic helper now persists UART and per-request JSONL progressively, including request size/duration, uptime, heap, MQTT publish steps and successful checks. Future failed runs therefore retain their partial evidence rather than only a final success file.

## Final snapshot-fixed acceptance

The exact final artifact `09976373fde1b5b7af259b6f4ce8647796081acc838200b59f07c346f4fd6650` was restored using the independently hash-verified original boot/partition image and saved SIL NVS. USB flash hash verification passed. No shared CM source, production SIL firmware/settings or control policy was changed.

After a separately identified 20-second startup allowance, the selected `sbt`/`system` endpoint passed **19/19 HTTP samples over 180 seconds**. Uptime increased from 19,707 to 199,814 ms, MQTT was connected throughout, and UART showed one controlled boot with normal Info setup completion and no Guru Meditation/backtrace. Final sampled free heap was 109,148 bytes. The selected response was approximately 1,720 bytes.

The corrected MQTT fixture uses the actual `available`/`unavailable`/`disabled` vocabulary. It passed **9/9 checks** with selected runtime groups (10 successful requests, 1,720-1,907 bytes, maximum request duration 2.670 s, minimum sampled heap 109,092 bytes). A subsequent targeted comparison using full `/runtime.json` also passed **9/9 checks** (11 requests, 2,168-2,444 bytes, maximum 1.622 s, minimum heap 108,332 bytes). Both runs had zero HTTP failures and zero UART error output, included the previously failing deadband step, and restored the real input root. No MOS command was published; the helper hard-limits mocked publications to the isolated input root and rejects `/set` suffixes.

The final readback confirmed all six isolation/input settings: RS485=false, MQTT=true, client `SIL-test-COM7-20261016`, publish root `SILTest/COM7`, listen interval 50 ms and input root `SolarBatteryTelemetry`. Production SIL `.122` was independently read as 4.3.2 and remains unchanged. COM7 UART handles and observation clients are closed; COM7 remains on the tested SIL 4.4.0 artifact at static `.126`.

A later real-root observation was intentionally stopped after the owner confirmed that the PC location has no BMS/Victron Bluetooth reception. Its partial evidence contained 10/10 additional HTTP samples and a retained real-root `offline` status. This interrupted observation is not claimed as a second complete stability run. Actual BMS/Victron sample reception and the collector's live 60-second refresh remain site-validation work after the collector returns to its normal location. Missing/unavailable sources remain `null`, not synthetic zero values. The host integration test against the actual SBT publisher and command parser covers the exact serialized contract, and the completed hardware mock covers real MQTT transport independently of physical Bluetooth reception.

The old incorrect-fixture tests and repeated HTTP failures remain documented above. The final passes support this bounded test outcome after the proven SIL lifetime fix; they do not establish a general fix for the separate potential CM MQTT-diagnostics race.

Local evidence (ignored): `.pio/sbt-snapshot-grouped-{serial.log,http.json}`, `.pio/sbt-snapshot-grouped-mock-{trace.jsonl,serial.log,acceptance.json}`, `.pio/sbt-snapshot-full-comparison-{trace.jsonl,serial.log,acceptance.json}`, `.pio/sil-snapshot-restore.log` and `.pio/sil-sbt-snapshot-fixed-build.log`. Repository tests, source and English evidence remain uncommitted on `feature/sbt-telemetry-inputs` as requested.


## UI follow-up: settings placement and three application Live pages

The user requested Temperature Sensor, Limiter and BMS / MPPT 150/35 views. The
core System page remains available. Explicit page/card/sourceGroup metadata now
exposes all 29 received channels plus 11 health/control fields (40 in total), with
units, readable labels and two decimal places for measurements. Unsupported
channels stay null; MOS fields are status indicators only, with no buttons or
command activation added. Temperature readings remain from the BME280.

The proposed I2C removal was withdrawn by the user after sensor verification.
All three I2C settings remain visible and unchanged. The resolved BME280 library
calls Wire.begin(SDA,SCL,400000) and checks chip ID 0x60 before reporting success
(BME280_I2C/src/BME280_I2C.hpp:210-216). UART confirms BME280 ready; browser readings
include temperature, humidity, dewpoint and pressure.

The current CM Settings UI consumes Config.category/card directly; an initial
addToSettingsGroup-only attempt did not move the field, as confirmed in the
browser. The final Config category is MQTT-Topics and card key MQTT-Topics (display title MQTT-Received). The
explicit SBTInputRoot key remains unchanged. At pinned CM 554869d,
ConfigManager.h:427-436 derives storageKey from the explicit key alone, while
preferences use the shared ConfigManager namespace (lines 1897 and 1940). toJSON uses
getCategory/getCard (lines2726,2753). This preserves NVS key 776A7FB565CE and its
value; the Settings HTTP category changes from SBT to MQTT-Topics. Final HTTP
readback confirmed SolarBatteryTelemetry, no SBT category, visible I2C settings,
RS485=false, isolated client SIL-test-COM7-20261016 and publish root SILTest/COM7.

The first UI artifact 9f544e3d... passed 13/19 HTTP probes over 180 s. UART reported a
WiFi disconnection after the first 13 successful probes (0..120s), with no Guru,
backtrace or reboot. Heap before the interruption was100,140 bytes; navigation had
sampled a minimum 87,528 bytes. A follow-up passed 1/7 probes over 60 s. A separate
read returned SIL 4.4.0, uptime 261,983 ms, heap 53,756 bytes and MAC 3C:8A:1F:A7:22:7C,
then another WiFi disconnection occurred. No IP collision was observed; SBT was
independently confirmed at .21. Runtime metadata count changed 69 -> 72, so this did
not newly cross the 64 -> 65 vector capacity boundary. The exact WLAN cause remains
unproven; no CM/network policy change was made.

A controlled reboot of that same artifact then passed 19/19 HTTP probes over 180 s,
including Settings navigation, with MQTT connected throughout and no UART
WiFi disconnect/Guru/backtrace. Thus the settings action did not reproduce the
failure in that run. Later probes received real-root fresh BMS and MPPT data
(25 fields, progressing source sequences) without publishing any mock data.
Final category and precision corrections were subsequently built and flashed;
the final artifact results below supersede this intermediate artifact.


An additional browser check identified that the existing received-topic card key
is MQTT-Topics, while its display title is MQTT-Received. The final field uses
that same key to avoid creating a second card with the same title. Intermediate
artifact 8c59... passed 9/10 samples over 90 s; the last request timed out despite
prior heap 96,780..102,176 bytes and MQTT connected. Its UART showed no disconnect,
panic or reboot. This is a failed complete-window check, not a stability pass.

The pre-unit-encoding UI artifact is
`8d71b388d644eb163aa6097c0cca0657b1c95b0932266d19497cb4d9447947c8`.
Source changes for this follow-up are limited to the main sensor page and the SBT
service settings/metadata; the receiver protocol and control policy are unchanged.
Existing protocol tests were not repeated for these metadata-only changes.
The USB release build and static channel-label/unit count assertions pass.


Final artifact size is 1,217,952 bytes (program sections 1,211,373 bytes, static RAM
63,416 bytes), leaving 92,768 bytes of actual binary reserve in the unchanged
1,310,720-byte OTA slot. USB application-only write at 0x10000 passed flash hash
verification; bootloader, partitions and NVS were not rewritten.

That artifact's 60-second gate passed **7/7 HTTP samples**, with MQTT connected in all
samples, uptime 19,637 -> 79,543 ms, minimum sampled free heap 99,332 bytes and
one controlled boot. UART showed normal Info startup/BME280 success, no WiFi
disconnect, Guru Meditation or backtrace. A real source sequence advanced from
15 -> 26 (BMS) and 26 -> 46 (MPPT); received fields grew from 4 -> 25 as the normal
non-retained scalar refresh arrived. SIL commands remained locked. This confirms
bounded real reception; it does not erase the earlier intermittent WLAN failures
or establish unrestricted long-term availability.

The browser verified all application pages, the single received-topic card with
SBT MQTT Input Root, and the unchanged I2C card. Final API checks assert exactly
one MQTT-Topics card, no SBT category, the persisted input root value, the test
isolation settings, page field counts 4/7/40/21, sourceGroup=sbt and two decimal
places for BMS/MPPT measurements. Existing receiver protocol tests were unchanged;
no MOS commands, inverter commands or mock publications were sent in this UI run.

Evidence: `.pio/sil-ui-final-build.log`, `.pio/sil-ui-final-upload.log`,
`.pio/sil-ui-card-final-gate-{serial.log,http.json}`, `.pio/sil-ui-final-check.json`,
and `.pio/sil-ui-*-final.png` screenshots. Earlier failed/intermediate evidence is
retained under `.pio/sil-ui-gate-*`, `.pio/sil-ui-disconnect-followup-*`,
`.pio/sil-ui-settings-first-*` and `.pio/sil-ui-final-gate-*`.
COM7 remains on the final artifact at .126; production SIL .122 was not touched.
All UI changes remain uncommitted on feature/sbt-telemetry-inputs. The independently
created energy-strategy draft was preserved and not modified.


The final screenshot exposed double-encoded degree symbols in the new BMS unit
strings. Both literals were changed to unambiguous C++ Unicode escapes; all other
source and runtime behavior remained unchanged. The exact final artifact is
`847f207aebc011cc0797bdc2a086df1c1a218c2fbc661e03af1debab7126f10e`.
It supersedes the previous UI artifact. The targeted metadata check additionally
asserts the two temperature units equal the Unicode string U+00B0 followed by C.

The exact final artifact passed **7/7 HTTP samples over 60 seconds**, MQTT connected
throughout, uptime 19,666 -> 79,590 ms and minimum sampled heap 88,936 bytes.
UART recorded one controlled boot, normal Info/BME280 startup and no disconnect,
Guru Meditation or backtrace. Real source sequences advanced 55 -> 66 (BMS) and
99 -> 119 (MPPT), with 25 received fields by the final sample. The final browser
screenshot shows two-decimal measurements and correct degree symbols, plus the
read-only source and MOS status. All source-data freshness and SIL reachability
claims are limited to their recorded observation windows.
Final run evidence is `.pio/sil-ui-encoding-final-gate-{http.json,serial.log}`;
final image, size and build evidence are unchanged except for the final hash above.
COM7 serial is closed and available; the final SIL image remains running at .126.
