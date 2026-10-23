# Charge intent projection: isolated acceptance

## Authorization and baseline

The owner requested a local intermediate commit, implementation of a coarse visual charging-intent preview with expected sunlight, and testing first on the isolated Test-ESP. The owner later requested more detailed reset/MQTT/calculation/NVS diagnostics AFTER integration, plus display of existing production uptime in HA. After the successful isolated test, the owner explicitly stopped further NVS analysis and authorized production 5.4.0 OTA. A subsequent request extends the HA energy-plan history from one to three hours. The owner then explicitly authorized committing, pushing and integrating SIL and SBT into their respective `main` branches. No NVS erase is authorized.

Checkpoint `583557eddc64d19d08b4ccc774ce20f78b1d18fb` on `feature/battery-energy-control` preserves the previously verified/deployed 5.3.0 work, Settings-only build and owner TODO additions. The 5.4.0 feature was uncommitted during the hardware acceptance below; the later commit/push/integration authorization does not extend that acceptance or resolve its open limitations. Governance was read from fetched `origin/main`: root and canonical AGENTS plus control-plane, workflow, refactor and docs roles.

## Implemented scope

- A separate, read-only `ChargePlan` projects the existing economic request only until its current deadline and adds existing full/half reserve intent with one shared target. It exports deliberately withheld DC power and forecast solar DC power; unknown intervals remain null.
- The current interval is anchored to the actual controller decision. Later intervals are coarse projections, not a new optimizer, total physical charging forecast or exact future inverter command. Existing red discharge remains AC allowance from the measured pool.
- A read-only accepted-tariff lookup and a generic publication state machine preserve existing discharge behavior. Both plans are captured on a normal control tick; the new MQTT topic is `<base>/Planning/ChargePlan`, non-retained.
- `SbtService` adds only a diagnostic validity bit so absent charge-MOS telemetry is not mislabeled as confirmed blocking. Actual controller, PID, TX, MOS/NVS paths and dependency versions are unchanged.
- HA examples use dedicated test entities/topics and a separate preview; production planning/history remain unchanged. The owner's separate uptime request authorizes only the new uptime row on the existing production Solarlimiter card.

## Main integration verification

Following the owner's explicit commit/push/main request, all six affected host suites passed again: charge projection, future prices, current control including the actual PID/TX application, PV forecast, SBT receiver and compact runtime metadata. Five precompile-wrapper tests and 27 HA helper tests also passed. The existing discharge-overlay test initially still expected the retired 24-hour/current-hour window; its assertions were updated to the implemented three-hour history plus 24-hour forward window, and the generator checks passed again, including DST alignment.

`pio run -e usb` passed with published CM 4.9.0, using 86,976 bytes static RAM and 1,166,013 bytes program flash. The resulting BIN SHA256 is exactly `f4e9bc5f75eb325c108e954209a24b30170410351281699bf84840aeb6f345c0`, identical to the previously frozen and deployed real-transport artifact. The final corrections affect documentation and an offline chart test only. Hardware tests were not repeated for this Git integration; the isolated acceptance and production observations below remain bounded evidence. Host tests were used instead of a device-based `pio test` run because these suites are standalone host executables and no hardware access is part of the integration request. No configured firmware CI workflow exists in this repository; available GitHub review checks are checked separately before merge.

## Local verification and review

- New charge-plan tests observed RED/GREEN for missing projection, fixed-buffer fail-closed serialization, actual discharge/zero intent, invalid clock, and a partial-deadline/PV-cap regression found by independent review.
- The deadline regression now integrates PV-limited rates before/after the deadline separately, consumes headroom chronologically, and keeps SOC/remaining energy consistent: 500 W for 300 seconds is 41.667 Wh / 166 W over a full quarter-hour; reserve-after-deadline and small-headroom cases also pass.
- Existing FuturePrices/PV, CurrentControl and actual PID/TX/MIN/offset, and SBT receiver suites passed. The real energy pool and control outputs are not changed by the projection.
- The initial 19 HA example/generator checks passed independently. After the three-hour history change, all 20 updated checks passed independently: current/partial intervals, real zero versus null, independently known sunlight, blocking, invalid/stale/future frames, bounded 144 slots, expiry, shared 27 elapsed hours, actual three-hour price history and both DST changes. These checks are synthetic and explicitly `[MOCKED!]`.
- Host sizes: `ChargePlan` 1,192 bytes and its publication 3,256 bytes. Worst tested payload is 1,931 / 2,048 bytes; including a 128-byte topic and seven-byte packet overhead is 2,066 / 2,304 bytes. The existing shared MQTT buffer is not enlarged.
- Independent review closed after the focused deadline correction. It found no other concrete controller-feedback, source/unknown or boundedness defect; dynamic device behavior remains unproven by static review.

## Frozen real-transport candidate (not a Test-ESP fixture)

Final `pio run -e usb` passed against published `vitaly.ruhl/ESP32 Configuration Manager@4.9.0`, Live disabled and Settings enabled, with asset SHA256 `a8f3231977aca38dcba661790eafbdec46c85a369357f4a53b8507cc6655124d`.

- Version 5.4.0; static RAM 86,976 bytes, program flash 1,166,013 bytes (89.0 percent).
- Compared with 5.3.0: +3,272 bytes static RAM and +3,960 bytes program flash.
- BIN 1,172,592 bytes, SHA256 `f4e9bc5f75eb325c108e954209a24b30170410351281699bf84840aeb6f345c0`.
- ELF SHA256 `71a2287747ac70b27670c1cb244bba647a3232b06b31bcb27e679b6ceb07f751`.
- Frozen under `.local/charge-intent-5.4.0-20261023/` with source aggregate/provenance. No product-source changes are allowed during its isolated fixture copy/test without a new freeze.

## Isolated device acceptance

The separate instrumented fixture was USB-flashed to COM8 MAC `3C:8A:1F:A7:22:7C`, IP `192.168.2.126`, with mock RS485, private broker `192.168.2.220:18884` and public `SILTest/COM7` namespace. The real-transport image above was not flashed. The fixture BIN SHA256 is `5f88d9c4ae35e443f7df10c5c98d6ab1899f70901cab3c1e9acff0d73f0e0338`, with 87,128 bytes static RAM and 1,195,245 bytes program flash. Root independently recomputed the unchanged 22-file product-source aggregate after observation.

The main observation ran 900.296 seconds from 13:44:39 UTC, followed by a 110.750-second passive tail. The first 12 minutes contained no HTTP requests. No unexpected reset, panic, abort, failed heap-integrity sample or unmatched stage BEGIN occurred. The final NVS read intentionally reset the test board after capture; that reset is excluded. This is bounded isolated acceptance, not a production or long-term stability claim.

Root independently derived these main-window results from the local capture:

| Measurement | Result |
| --- | --- |
| Diagnostic samples | 179, plus 23 in the passive tail |
| Minimum internal free heap | 112,384 bytes |
| Minimum default-capability heap | 68,524 bytes |
| Smallest observed largest internal block | 90,100 bytes |
| Minimum unused loop / async stack | 4,444 / 13,132 bytes |
| Median of sampled mean loop periods | 12.283 ms |
| Peak loop work | 3.040 s during the intentional test-broker outage |
| Longest completed instrumented stage | 27.768 ms, charge publication, including UART marker overhead |
| HTTP acceptance | 4 / 4 complete HTTP 200 responses |

The HTTP reads were `/appinfo` (5.4.0, 125 ms), `/ui_capabilities` (Live false / Settings true, 141 ms), and two `/config.json` reads (14,986 bytes, 313 / 219 ms). Both complete configuration responses had identical SHA256 `018b0796242c039861a8cd0666996fcd70a6a7b7c8d901afd7913d80cca699f9`. Fixture offset 0, reserve 2/6 and absence of obsolete `pwrDb` were checked. This fixture offset differs deliberately from the production setting.

Three test-transport defects were corrected without editing frozen product sources: the private broker omitted the new ChargePlan output from its exact allowlist; bridge status queuing used non-JSON-serializable bytes; and simultaneous bridge connections could drop the real retained source snapshot before private connectivity. The bridge now connects privately before subscribing to natural sources. All ten focused offline harness guards passed. The broker reconnect at 13:52:12.742-13:52:16.833 UTC was intentional; the single 3.040-second loop-work peak falls inside it. There was no MCU reset. No saved-packet replay, synthetic plan or production command was sent.

NVS was backed up privately before flashing, and only the verified active app bank was written. Existing production startup may erase NVS on selected initialization errors and may persist conflicting negative-price flags; the diagnostic fixture therefore fails closed on those init errors and suppresses normalization/version persistence. RAM configuration overlays isolate the fixture. Healthy operation with these departures cannot establish that production persisted configuration is healthy. No factory reset or explicit NVS erase/write command was performed.

The raw 20,480-byte NVS hashes differ: before `bf10ebe5a4662d57e5acac3668cf17e68eb13188cbfb3b65d8a0d423f661b0e0`, after `d8b714475642ccf2cf6ad1f7cdea7a9717df6f0732892dcce5f6ddaff8caeee8`. An offline CRC32/live-header/span and value-hash comparison found 5,937 changed raw bytes on pages 0/2/4, with 233 logical hashed entries on each side and no ConfigManager logical entry changes. Only `nvs.net80211` keys `sta.apinfo` and `sta.apsw` changed, each in blob-data/index types 0x42/0x48; raw layout also reflects page recycling. Root inspected the bounded, value-redacted result in `nvs-offline-diff.json`. This limited structural/hash comparison is not full semantic validation, does not prove corruption, and cannot diagnose production NVS. It does not justify a production factory reset.

## HA acceptance

The new passive test entities and separate [TEST dashboard](http://homeassistant.local:8123/dashboard-sil-test-energy/energy) received an authentic non-retained ChargePlan at 13:56:23.994 UTC: generation `1792763783`, price/PV revisions `1792763700`, 144 intervals. The existing red budget had matching revisions and generation. Root independently inspected the screenshot and derived acceptance artifact.

- Red: three positive quarter-hour allocations, up to 650 W AC; current allowance 0 W.
- Blue: 13 known intervals at 0 W DC and 131 unknown intervals. No positive live blue request occurred in this snapshot, so positive-area rendering is covered by offline examples rather than claimed as a live hardware observation.
- Sunlight: 57 known positive intervals, 4-249 W DC, and 87 unknown intervals; current forecast 180 W DC. The dashed solar line and red allowance render without configuration errors. The legend shows the last future point, so blue N/A at the unknown horizon end does not mean all earlier intervals are missing.

The final ordered bridge also observed the next natural 14:00 UTC revision. The public transport and valid-plan observation continued beyond five minutes after the first valid ChargePlan. The initial isolated acceptance did not change production plan/history cards. The separately authorized passive `sensor.solarlimiter_uptime_human` reads existing `SolarLimiter/System-Info/ESP` telemetry, expires after 180 seconds and adds only the `Laufzeit seit Neustart` row to the Home Solarlimiter card; firmware uptime publishing already existed.

Local private device evidence: `.local/charge-intent-5.4.0-com8-20261023/`. HA screenshot and derived proof: task-workspace `ha-sil-charge-plan-20261023/test-view-live.png` and `ha-live-test-acceptance.json`. Raw NVS/config captures can contain credentials and must not be committed.

## Production HA rollout and three-hour history

The owner subsequently requested three hours of history and explicitly requested the same TEST presentation in Home card #3, using production data. A new passive `sensor.solarlimiter_charge_plan` reads `SolarLimiter/Planning/ChargePlan`; its discovery publication received QoS 1 PUBACK. The plan itself remains non-retained. Home #3 now contains its existing real recorded discharge history, red production AC discharge allowance, blue production DC withholding, dashed production DC sunlight and the existing price series. No TEST entity is used in Home. Existing history/red data logic and the price series are unchanged; the discharge color now matches the requested TEST red.

The energy-plan windows use `graph_span: 27h`, current-hour start and `offset: -3h`, retaining the existing 24-hour forward endpoint. Home uses only actually recorded history, never reconstructed past plans. Standalone and TEST windows match these bounds; their history availability still depends on their existing series. Root independently checked the saved Home configuration against the expected full dashboard: exact match, with all changed leaves confined to `views[0].cards[2]`. The previous history/red series differ only in color, and the price series is identical to its three-hour baseline. Root also inspected the actual `home-production.png` screenshot, which shows the forecast sunlight and production uptime 21m50s.

The owner's `Unknown type encountered: panel` error was caused by pasting an entire view (`title`, `path`, `type: panel`, outer `cards`) into a card editor. Complete single-card exports start with `type: vertical-stack`; production and TEST references remain distinct. Both copy-ready YAML files were parsed and checked independently. They are saved as task-workspace `ha-sil-production-energy-20261023/PRODUCTION-Home-single-card.yaml` and `TEST-single-card.yaml`; root also verified that the Home YAML equals the installed card exactly. The matching standalone production card was installed and deeply verified as well. Production backups, expected/actual full configuration, discovery PUBACK and screenshot are in the same directory. The user's unsaved editor was not used or overwritten.

The earlier production reset/HTTP cause remains open. A plan arriving shortly before restart is a timing correlation, not proof of heap exhaustion or MQTT packet size. Application-level chunking is not introduced as an assumed fix; staged measurements and the last unmatched BEGIN marker are needed to localize a stall. A successful short test must not be described as proof of long-term production stability.

## Settings display and restart-timer follow-up

The owner's screenshot of `Connectivity check target` was checked against the published CM 4.9.0 package actually installed in SIL. `webui/src/components/Setting.vue:59-78` uses a text input with a browser datalist, containing both static suggestions `off` / `Off` and `gateway` / `Standard gateway`. Both strings are also present in the embedded Settings-only gzip. Entered `off` filters browser suggestions; its value and label appear together. The options are not loaded from NVS. The owner's second screenshot confirmed both suggestions after clearing only the draft field. No Apply/Save or NVS reset was necessary for this check. The owner considers this interaction confusing; a separate CM usability improvement could use an explicit Off / Standard gateway / Custom IP selector with an IP field. Such a UI change is not included in the frozen SIL candidate.

The owner changed the production WiFi-loss reboot threshold from 5 to 15 minutes during observation. Published CM `ConnectivityCheck.h:194-198` resets its timer reference whenever the station is healthy with probing off; `WiFiManager.cpp:792-797` changes the threshold without resetting the elapsed timer. This timeout measures time since last healthy connectivity, not total uptime. `off` disables the additional target probe, not the WiFi-loss reboot. A longer observed uptime alone therefore cannot identify the previous restart cause. The explicit WiFi restart marker/log would distinguish this path, but the production CM logging flags are disabled. No agent changed this setting.

Production HA uptime changes are preserved as observations rather than an attributed root cause. The isolated fixture configures its private MQTT server before connecting, and the bridge restricts all exports to `SILTest/COM7`; it does not export `System-Info/ESP`. It cannot write the production `SolarLimiter/System-Info/ESP` uptime topic. No other producer identity or restart cause is inferred from the text sensor alone.

The focused CM audit found no demonstrated stale-expiry defect after switching to `off`: changing target resets the policy reference and pending probe; healthy `off` updates refresh the reference; AP/OTA suspend expiration and restart the grace period on resume. An actual WiFi loss can still invoke the separate configured auto-reboot. A distinct 30-second loop watchdog also remains active, independently of the target probe. Its unconditional UART markers are `[CM-LOOP-WDT] heartbeat expired; restarting` and `[CM-LOOP-WDT] previous restart: expired loop heartbeat` (`src/core/LoopWatchdog.cpp`). Existing focused policy/update/mailbox test binaries passed again against the verified unchanged extracted functions.

One ordering limitation was identified: SIL calls `wifi.update()` before `handleClient()` applies pending settings. At the exact timeout boundary, an HTTP-acknowledged target/threshold change can therefore be one loop late for the manager. This does not explain repeated resets with a long-established `off` value and was not changed in the frozen artifact. MQTT SystemInfo currently lacks reset cause; the WiFi auto-reboot marker is consumed at startup even if CM logging suppresses its output. The historic production reset cause therefore remains unconfirmed. Current values must be checked rather than assuming `off`: the production OTA baseline later showed `gateway` and 15 minutes.

## Owner-authorized production deployment

After isolated acceptance the owner explicitly requested production OTA. The frozen real-transport BIN above was verified again and transferred once to MAC `3C:8A:1F:A7:EB:68`, IP `192.168.2.122`; no instrumented/mock fixture was used. HTTP 200 returned `{"status":"ok","action":"reboot"}` after all 1,172,592 firmware bytes were sent. No automatic upload replay occurred.

Post-boot HTTP verification confirmed 5.4.0, Live false, Settings true and OTA inactive. Comparing every captured config entry's value before/after found no changed setting names. Connectivity target `gateway` and WiFi-loss threshold 15 minutes were preserved. The passive production MQTT observer checks boot epochs as well as decreasing uptime, so two similarly short startup uptimes cannot hide an additional boot. Across the deployment observation, passive follow-up and bounded source correlation, exactly one post-upload boot epoch was observed: the expected OTA reboot. Latest sampled uptime was 830,666 ms (13m50s), with no additional observed reset. This is a bounded snapshot, not long-term acceptance.

An authentic 14:20 UTC source batch contained all 12 price chunks and six PV chunks, revision `1792765200`. Production emitted matching valid 144-interval discharge/charge plans at generation `1792765210`; both were again valid at generation `1792765446`. No schedule was requested, synthesized or replayed. The new production topic is `SolarLimiter/Planning/ChargePlan`.

Plan qualification also changed to `energy_missing` / `source_missing` between these valid snapshots while SIL uptime continued increasing. Thus a disappearing curve is not by itself proof of an SIL reset. The precise input-qualification cause remains open. A later, separate 90-second passive source observation received 20 available BMS health messages, fresh Victron health and continuous SBT uptime; this does not prove source health at the earlier failure instant. No SBT setting or MOS command was changed. The deployment summary and value-limited source evidence are under task-workspace `sil540-production-ota-20261023/acceptance.json` and `source-continuity.json`.
