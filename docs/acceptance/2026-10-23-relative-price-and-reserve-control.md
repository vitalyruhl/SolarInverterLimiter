# Relative-price and reserve charging verification

## Scope

Owner-authorized correction on `feature/battery-energy-control`, preserving existing uncommitted MIN/build changes and the owner's seven-line `docs/todo.md` addition. Firmware version source is `src/main.cpp`; the version is 5.3.0. The owner subsequently explicitly authorized the production OTA recorded below. No commit, push, manual MOS command, production protection-threshold change, or production setting write was performed. Isolated Test-ESP verification uses a separate diagnostic build and the existing USB authorization.

The separate Home chart request was applied directly in HA: only `data.config.views[0].cards[2]` changed. The chart now covers 25 hours from the previous full hour to the existing future endpoint. Past prices use existing EPEX attributes. The subsequent history extension adds a raw step-line history up to now and clips the current future battery budget to now, with no invented historical forecast. Fresh before/after comparisons guard the other dashboard configuration and ordering. Evidence is in the task workspace under `ha-sil-plan-20261023/`.

The subsequent owner-authorized history extension adds the HA-only template sensor `sensor.sil_current_discharge_plan`, created at 2026-10-23 14:28:22.567 +02:00. It selects the current quarter-hour from the existing production `sensor.solarlimiter_discharge_budget` attributes, updates on source changes and each minute, and returns unavailable for invalid schema, invalid time coverage, or plans older than 180 seconds. This matches the existing plan expiry and future-chart freshness checks. It records a scalar planned battery-discharge value in W, not actual inverter output or a reconstructed historical forecast. Its actual HA History UI showed the real 0 W value after a normal recorder commit; no synthetic changes or backfill were used. Template and registry evidence are `ha-sil-plan-20261023/current-plan-template.json` and `current-plan-entity.json`; the chart is `home-card-plan-history.json`. Existing production firmware and MQTT commands are unaffected.

Final saved-dashboard comparison passed: only Home card #3 changed; the price series and every other dashboard value remained identical. Evidence is `ha-sil-plan-20261023/plan-history-verification.json` and `home-plan-history.png`. At the final screenshot the production plan was no longer current/valid, so its future area was correctly absent. This is an availability snapshot, not a new diagnosis of the existing production runtime issue.

## Implemented rules

| Condition, with charging available and qualified sources | Reserve charging request |
| --- | --- |
| SOC at or below OFF, valid price not expensive | All available charger DC power; inverter ceiling immediately zero |
| SOC above OFF and below ON, cheap price | Half of available charger DC power |
| Otherwise | No additional reserve charging target |

Economic charging can raise the common charging target above that reserve request. The obsolete `pwrDb` setting and 20/50 W charging floor are removed; no replacement margin is introduced. Output correction is unchanged. Hard reserve discharge hysteresis, qualified MIN precedence, source-loss/recovery behavior, negative-price/explicit-OFF/protection stops, and actual MOS handling retain their existing contracts.

Stored energy is allocated by relative useful prices, without the old absolute comfort/deadband admission or absolute load tier. The existing future advantage threshold is 0.03 EUR/kWh; new charging separately requires `futurePrice * 0.90 - currentPrice >= 0.02 + 0.03` EUR/kWh. These are retained model constants, not newly calibrated hardware losses. Stored energy is not charged those acquisition costs again. The planning load is `min(MAX, 650 W)`, and the bounded allocator retains at most 144 quarter-hours. The final single-slot case still uses the existing fallback. Expected PV never increases the measured energy pool; the diagnostic trace remains limited to that measured pool.

## Verification

- RED was observed for the unwanted positive charging floor, missing reserve price/SOC targets, positive PI history defeating full reserve priority, relative below-comfort allocation, flat/falling curves, and spending stored energy at fully free tariffs. The corresponding corrections passed.
- `tools/test/run-current-control-tests.ps1` passed: reserve boundaries 1.9/2/2.1/5.9/6/6.1 with cheap/comfort/expensive prices and both latch histories; economic charging above reserve targets; critical and half-reserve PI transitions; existing actual PID/TX, MIN, offset-once, MAX, restrictions and recovery regressions.
- `tools/test/run-future-prices-tests.ps1` passed: relative prices, held-versus-new energy costs, flat/falling/free tariffs, exact minimum-benefit boundary, maximum limits, partial slots, PV credits, signed energy debits, parser/snapshot/cache/packet/trace regressions.
- An owner-like tariff case (0.245 EUR/kWh now, 0.309 later, 0.25 comfort, 0.20 deadband, SOC 6%, 131 Ah) reserves the existing approximately 242 Wh for the higher-priced slots, with no additional economic charging request. Synthetic timestamps and inputs test the real allocator; they were not published to production.
- Five `test/host/precompile_wrapper_test.py` tests passed.
- `platformio run -e usb` passed using published `vitaly.ruhl/ESP32 Configuration Manager@4.9.0`, Settings-only compiler/UI flags, and the matching embedded asset hash `a8f3231977aca38dcba661790eafbdec46c85a369357f4a53b8507cc6655124d`.
- Static RAM: 83,704 bytes. Flash program size: 1,162,053 bytes (88.7% of the app partition). The price planner remains 7,416 bytes in the host check; no new dynamic planning arrays were introduced.
- Firmware BIN: 1,168,624 bytes; SHA256 `18c7e219a90aaf1aa42e97c8d9278a0d2c35e369816f5c03a7459eac3f641f18`.
- Matching ELF SHA256: `5d371f8852dc27a5a9d67714e777bfae56578950ccc229c95c9af0aa6c420a68`.

## Isolated Test-ESP feature smoke

The separate diagnostic 5.3.0 fixture was built with published CM 4.9.0 and written only to the active app partition of the owner-confirmed COM8 Test-ESP (MAC `3C:8A:1F:A7:22:7C`). The real-transport candidate above was not flashed. Existing isolated MQTT wiring and NVS were preserved; fixture version persistence was suppressed. RAM-only fixture reserve values were 2/6, with its existing MIN 0, MAX 900 and offset 0. No production values were changed.

- Observation: 2026-10-23 12:18:20.457 to 12:23:20.723 UTC, 300.25 seconds; serial observer closed afterward.
- Exactly three HTTP attempts completed: appinfo 5.3.0, Live disabled / Settings enabled, and one complete 14,986-byte config response in 0.328 seconds. The removed deadband field was absent; fixture offset 0 remained unchanged.
- 59 UART samples covered uptime 5,000 to 295,351 ms, with no recorded reset or panic and heap integrity always valid. Maximum observed loop work was 157.982 ms across 24,034 captured loop iterations.
- Sampled free internal/default heap minima were 147,332 / 103,452 bytes; lifetime low-water readings were 118,528 / 74,668 bytes. Loop/Async stack high-water free-space minima were 4,460 / 13,132 bytes.
- Three fresh valid 144-slot plans arrived from natural inputs (revision 1792758000), with approximately 173 seconds of active-plan coverage. No replay or synthetic production publication was used.
- Diagnostic BIN SHA256: `830d58f6bf55514b16bf19dd42372ec309f317ae7f36dc42c8616a3fa557dec1`; matching ELF: `2f72b3aef9f678f88e1559cd4a17618637aacba7c8b5d0f706d97564afee5c19`.
- Evidence: `.local/feature-acceptance-5.3.0-com8-20261023/feature-acceptance-summary.json` and its associated capture files. Classification: bounded feature smoke PASS.

## Owner-authorized production OTA

On 2026-10-23 the owner explicitly requested production OTA of 5.3.0. The unchanged real-transport BIN above was rechecked independently against its matching ELF, Settings-only asset, published CM 4.9.0 sources, and test evidence. The diagnostic fixture image was not used.

The confirmed target was `192.168.2.122`, MAC `3C:8A:1F:A7:EB:68`, with fresh non-retained MQTT telemetry before upload. One paced HTTP multipart upload sent all 1,168,624 bytes. At 12:47:20.686 UTC the device replied HTTP 200 with `{"status":"ok","action":"reboot"}`. No POST retry was issued.

Post-boot appinfo confirmed 5.3.0. Capabilities confirmed Live disabled, Settings enabled, and OTA inactive. A complete settings read confirmed that all remaining Limiter settings matched the immediately preceding snapshot exactly; only `Battery Power Deadband (W)` was absent as intended. Preserved values include MIN 0 W, MAX 900 W, offset -50 W and reserve OFF/ON 2/6 percent. Evidence is in the task workspace under `sil530-production-ota-20261023/` (`before.json`, `upload-attempt.json`, `upload-result.json`, `after.json`, and passive MQTT captures).

The 300-second capture included both pre-upload and post-upload telemetry. The expected OTA boot was followed by one additional startup boot approximately 94 seconds later: fresh non-retained uptime reports were 5,082 ms and then 5,105 ms, with the estimated boot epoch moving forward by 94.36 seconds. A simple uptime-decrease counter alone misses this case because the second reported uptime is slightly larger. Matching fresh WiFi reports identified the same production MAC/IP. A subsequent read confirmed that 5.3.0 remained installed; no upload was repeated.

The owner accepted a possible startup/AP transition provided it did not recur continuously. No AP change was independently established as the cause. An additional 150-second MQTT-only follow-up, without agent HTTP traffic, showed no further observed restart. The latest fresh uptime reached 259,042 ms (4 minutes 19 seconds); its report preceded the end of that capture by approximately 47 seconds. Two genuine valid post-upload 144-slot plans were received across the combined captures (`forecast_hold`). This supports bounded startup follow-up only. Evidence and classification are `sil530-production-ota-20261023/acceptance.json` and `quiet-follow-up/`.

Governance was loaded from fetched `origin/main`: root `AGENTS.md`, `.github/AGENTS.md`, and the control-plane, refactor and docs role files. Deployment documentation changed this record and `readme.md` on `feature/battery-energy-control`; product source and the frozen BIN/ELF were not changed. No commit or push was requested or performed. `git diff --check` passed; no rebuild or repeated host tests were needed for this deployment of the already verified unchanged image.

## Limits

The isolated five-minute feature smoke and bounded production follow-up do not establish long-term stability. The earlier unintended production resets, intermittent long loop intervals, and incomplete HTTP responses are not fixed or reclassified by these economic-control changes. Existing frozen 5.2.2 diagnostic evidence remains separate.
