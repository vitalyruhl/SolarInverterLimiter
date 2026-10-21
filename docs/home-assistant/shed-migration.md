# Shed Home Assistant migration — 2026-10-20



## Executed scope



Executed against Home Assistant Core 2026.10.1 with the existing Pyscript 2.1.0 installation. Only forecast configuration, HA templates/helpers, Energy mapping, and schedule production changed. No inverter, BMS MOS, charger, or production control command was sent. Recorder data and original counters were preserved. No commit or push was made by the HA implementation agent.



## Forecast configuration and cleanup



The sole surviving Forecast.Solar entry is **Shed**, ID `01M5D5VEHZR1GR88THXH8C8BFS`, with one plane: **14.8 degrees inclination, 180 degrees azimuth, 1840 Wp**. It uses the actual HA Home location; coordinates and credentials are deliberately absent here. Morning/evening damping are zero and no inverter power cap is configured.



The installed plane form coerces inclination to an integer. Saving 14.8 through that form initially produced 14. The final float was persisted through HA's supported `ConfigEntries.async_update_subentry` API, and location/options through `async_update_entry`. Runtime and storage readbacks confirmed 14.8 and location equality with HA Home. The native provider subsequently returned forecasts successfully. Do not save this plane through an integer-only form without rechecking the stored float. No `.storage` files or integration source code were edited directly.



Private rollback files were saved on HA under `/config/.codex-backups/sil-shed-20261020/`, including the six original forecast entries, Energy settings, entity registry and affected YAML. Exact old IDs and entity IDs were searched in root YAML, stored Lovelace dashboards and Energy configuration after consumers migrated; no references remained. The following entries were then removed through the supported HA config-entry API; all returned `require_restart: false`:



| Original title | Removed entry ID |

| --- | --- |

| Home | `38bc2a118c9116ca19ea7bc2915672bc` |

| Home | `ef49d7701f81326fd646616163a4fbb9` |

| Home 1 | `91987a6403efe0e98c813357a9dc6851` |

| Home 5 | `542d64f7a9a7b86962fa64e9e847699b` |

| Home 5 2 | `e8863677369c60c016778b6355ef875f` |

| Home 8 | `8cf4575f65882f8089546283c402448e` |



Final config-entry readback contains only Shed. These were removals, not merely disabled entries. Existing recorder/statistic history was not purged. Temporary commissioning services were archived into the private rollback folder and unloaded.



## Energy and corrected delivery



Energy now has exactly one PV source:



- Energy: `sensor.sbt_victron_yield_today_wh`, unit Wh, `total_increasing`.

- Power: `sensor.sbt_victron_solar_power_w`, unit W, DC at the Victron MPPT.

- Display name: `Shed PV (Victron DC)`.

- Forecast: only the Shed entry.



Previously Energy used `sensor.tasmota_smartplug_12_energy_total` as PV and referenced Home 5 2 plus Home 8. The inverter AC total is no longer counted as additional PV. Battery charged/discharged helpers remain separate and unchanged: `sensor.sbt_battery_energy_charged` and `sensor.sbt_battery_energy_discharged`.



The original AC power/cumulative history remains available. New `sensor.shed_inverter_corrected_power` uses `max(0, raw_AC_W - 2)` and retains unavailable/unknown input as unavailable. The live native Integral helper is `sensor.shed_inverter_corrected_delivery`, entry `01M5D773R3YBD3XMJ9XQDRCHBC`, with left integration, kWh, six decimals and one-minute maximum subinterval. The export is [shed-energy.yaml](shed-energy.yaml); do not install a duplicate beside the existing live helper.



Installed-version Integral source was checked for outage behavior: an unavailable source cancels the maximum-subinterval callback; recovery has an unavailable previous state, which left integration rejects. The unavailable interval is therefore not bridged. No production source states were spoofed to simulate an outage. The existing self-consumption template now uses corrected AC power in `max(corrected_AC_W - max(-grid_W, 0), 0)`; battery power is not added again. Existing savings calculations continue to consume that self-consumption value. These are forward changes, without retrospective statistic edits.



Read-only recorder evidence found a mid-afternoon Victron yield reset on October 20 (5220 to 40 Wh, later 150 Wh). The raw and kWh wrapper metadata have consistent units; their recorded increments agree after unit conversion. The completed-hour statistic increment observed during inspection was 5380 Wh, not a final daily total. Reset cause and a trustworthy final October 20 total remain unresolved, so that day is excluded from calibration.



## Provider timing: implementation takes precedence over contradictory comments



The producer requests `forecast_solar.get_forecast` with `resolution: raw`. The official provider defines period energy between the preceding timestamp and the timestamp in the key. Library `forecast-solar==5.0.1` passes those timestamps unchanged. HA 2026.10.1 `_aggregate_hourly` merely floors each end timestamp; its action documentation/docstring describes a start timestamp, but the implementation performs no end-to-start shift. Consequently using that hourly service output directly would place energy one hour late.



`provider_hourly` validates a zero-energy sunrise boundary, then assigns each subsequent endpoint interval to its actual containing UTC hour. A partial sunrise/sunset interval keeps its Wh in that hour; overnight gaps produce missing coverage. Nonzero energy without a preceding boundary, duplicate timestamps, intervals over an hour, or intervals crossing an hourly boundary are rejected. Each resulting hourly Wh total is distributed across four 900-second slots with explicit `hourly_disaggregated` quality. No missing nighttime zeros or additional forecast precision are invented. Fractional Wh is quantized to at most 0.125 Wh error per hourly total; integer Wh is conserved exactly.



The timing correction upgraded the ledger to schema 2 (`provider_end_v1`). The previous ledger was copied intact to `/config/data/shed_forecast/ledger.schema1-provider-timing.json` before invalidating old hour/day references. Earlier calibration records are marked ineligible. A corrected full-day October 21 reference was frozen before midnight: 11 hourly periods total 1735 Wh, exactly equal to the independent native day-energy total. This is a forecast, not an actual yield claim.



## Producer, storage and wire contract



Reviewed exports are [shed_forecast.py](shed_forecast.py), [shed_forecast_core.py](shed_forecast_core.py) and [shed-forecast-package.yaml](shed-forecast-package.yaml). The first two are deployed at `/config/pyscript/shed_forecast.py` and `/config/pyscript/modules/shed_forecast_core.py`. Existing Pyscript import/hass permissions were reused; no new integration or permission was installed.



There is exactly one automation, ID `shed_publish_pv_forecast`, triggered on HA start and every five minutes. Automation reload succeeded; a timer publication at 18:20:00 UTC independently demonstrated the periodic trigger. The service also passed manual live execution after the endpoint correction. Storage is bounded in `/config/data/shed_forecast/ledger.json` with atomic replacement, 35-day retention and a 1 MB read limit. Pre-period hourly/day references are immutable. Original native endpoint values are retained with newly frozen daily records and in the latest raw snapshot.



PV schedule topic: `homeassistant/sensor/solar_forecast/shed/schedule`, QoS 1, **nonretained**. Six complete chunks cover 144 UTC slots at 900 seconds; each chunk carries at most 24 slots and 768 UTF-8 bytes. The agreed independent v2 envelope uses `v,s,r,t,d,n,k,g,e,f,c,a,q,o,p,x`, with `g == r`, current-slot `t`, source `forecast_solar`, raw integer W and flags 2 for hourly disaggregation or 1 for missing/null. Only the consumer applies factor `f / 1000`. Expiry is immutable for that revision. Empty or wholly out-of-coverage provider input produces no new offer; the previous receiver offer can survive only until its original expiry. Failed provider calls likewise cannot rejuvenate old data.



Actual corrected HA publications measured six chunks, maximum 349 bytes, 44 known slots and 1735 Wh. The first covered October 21 hour is 08:00 local time, containing 42 Wh. The ignored local fixture `.superpowers/sdd/2026-10-20-sil-sbt-ha-integration/ha-pv-wire.json` contains exact produced packet strings. It is a producer artifact read from `last-packets.json`, not a broker capture or proof of retain state. Receiver acceptance and device deployment are recorded by the main integration agent.



The shared price automation `sil_publish_price_schedule` remains the sole price producer on `homeassistant/sensor/electricity_price/schedule`, independent of the PV v2 stream. Its description and [ha-price-schedule.yaml](../control-planning/ha-price-schedule.yaml) now label provenance `provider-unknown`: the observed entity is named EPEX, but that name alone does not establish economic provenance. Its existing v1 payload/units and consumer ownership remain intact. PV publishes do not establish price-source provenance. A later passive broker check of the exact shared price topic received the complete natural 19:20 UTC batch: 12 valid compact JSON v1 packets, revision 1792524000, start 1792523700, 288 slots, chunks 0 through 11, 24 values each, maximum 226 UTF-8 bytes. Every received message had retain=false. The listener requested QoS 0 while the producer trace used QoS 1; the received QoS 0 is the negotiated minimum. The immediately preceding 19:15 UTC automation trace likewise showed all twelve publish iterations executed. Thus the HA price feed was nonempty and operating, not withdrawn or stopped. This proves broker reception by HA, not arrival at the test SIL or economic provider identity. Exact broker-received strings are preserved in the ignored local ha-price-wire.json fixture beside ha-pv-wire.json. The passive subscription was stopped and its diagnostic tab closed; no manual publish or configuration change was performed.



## Conservative calibration



Current live status is `uncalibrated`, factor 1.0, zero eligible days, unknown age 65535 and no raw factor. Historical final-day estimates were not fabricated. The median actual/original ratio uses only the last 30 completed days with at least 500 Wh original forecast and at least 14 eligible days; only then is its operational factor clamped to 0.5–1.2. Today cannot leak into the result.



A full-day original reference must be captured before local midnight and its hourly sum must equal the independent native day-energy total within floating arithmetic tolerance. Partial original-day coverage cannot qualify full actual-day energy. Observations must cover the day, begin near zero, remain monotonic, have no two missed five-minute samples, and show no full battery/absorption/float or MPPT 35 A ceiling. The owner confirmed no configured Victron current limit; that evidence applies conservatively only from `2026-10-20T17:51:00Z` onward, never retroactively.



Source liveness uses actual `sensor.sbt_bms_health` and `sensor.sbt_victron_health`, not scalar `last_updated` values. Both must report ok/fresh, have sample age at most 15 seconds, HA update age at most 60 seconds, and an advancing acquisition sequence since the prior observation. First/unknown/stale/replayed observations exclude the day. The verified SBT acquisition deadline is 30 seconds, so the calibration bound is deliberately more conservative. BMS permission/alarm inputs are the inventoried `binary_sensor.sbt_bms_charge_enabled` and `sensor.sbt_bms_alarm_bits`: only on plus zero alarms allows qualification, tied to the same fresh BMS acquisition proof. Disabled/unknown permission or nonzero/unknown alarms excludes the day; no switch is operated.



## Verification and rollback



The final focused suite passed **19 tests**. Exact deployed SHA-256 hashes matched the reviewed exports: `shed_forecast.py` = `4c97d0d75a51b0257ca600130e55ec50d8181a3e21675eed04169ed81e894c1c`; `shed_forecast_core.py` = `c7c3af85f1f1f1e7df909edbb2f7ca4d0578ceea3a84db85c360f2644219d3d0`.



A read-only probe of the installed modules independently confirmed library 5.0.1, unchanged raw endpoint parsing, and hour-floor aggregation without subtraction. The runtime aggregation function began at line 50 with SHA-256 `c39bf929c9232dc2981d4c92556fd658250838825227f030c5f14104e2901940`. Live BMS permission was on and alarm bits were zero; the automation was on and its last periodic trigger was 18:30 UTC. The temporary probe was archived/unloaded. Final post-cleanup publication again returned six chunks, maximum 349 bytes, 44 known slots and neutral calibration.



Focused tests cover Wh conservation, fractional quantization, missing coverage, DST, raw provider endpoints/daylight edges/overnight, rejected ambiguous intervals, immutable references, partial-day rejection, 13/14-day calibration behavior, wire bounds, offsets/unavailability, unknown or stale health, owner confirmation boundaries and BMS charge restrictions. Live reload/publication, exact deployment hashes, sole forecast entry, corrected Energy mapping and helper identity were checked separately from those offline tests. Fourteen eligible live days and a completed future actual day remain future evidence, not completed validation.



Rollback is available through the private HA backups. Restore affected YAML/consumer mappings through supported reloads/UI; recreate removed forecast entries from the private parameters through supported config flows/APIs if needed. Do not overwrite `.storage` in a running HA process, modify original recorder rows, restore obsolete runtime references over the new producer, or deploy duplicate helper/automation copies.



Primary implementation references:



- [Provider estimate contract](https://doc.forecast.solar/api:estimate)

- [HA 2026.10.1 Forecast.Solar service implementation](https://github.com/home-assistant/core/blob/2026.10.1/homeassistant/components/forecast_solar/services.py)

- [Exact dependency release, forecast-solar 5.0.1](https://pypi.org/project/forecast-solar/5.0.1/)

- [HA 2026.10.1 plane config flow](https://github.com/home-assistant/core/blob/2026.10.1/homeassistant/components/forecast_solar/config_flow.py)

- [HA 2026.10.1 config-entry API](https://github.com/home-assistant/core/blob/2026.10.1/homeassistant/config_entries.py)

- [HA 2026.10.1 Integral implementation](https://github.com/home-assistant/core/blob/2026.10.1/homeassistant/components/integration/sensor.py)
