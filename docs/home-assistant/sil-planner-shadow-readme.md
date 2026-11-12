# Isolated SIL planner comparison in Home Assistant

This example keeps production and hardware-mocked test price lifecycles separate. Existing tariffs, shared v1 price/PV automations and the Test-Vivil Home view remain unchanged. Device profile promotion and input-topic selection require their separate firmware gate. The HA adapter sends price input only, never inverter or BMS commands.

## Persistent adapter

Install `sil-planner-shadow-core.py` as `/config/pyscript/modules/sil_planner_shadow_core.py` and `sil-planner-shadow-pyscript.py` as `/config/pyscript/sil_planner_shadow.py`. Existing Pyscript with `allow_all_imports` and `hass_is_global` is required. Only reload the new file context when necessary; no HA restart or production automation edit is required.

The example's source entry and reference entity must identify the same existing Energyforecast.de / DE-LU EPEX Spot source. Authentication is read from that existing HA entry at runtime and is never placed in an example, state attribute, log, or cache. Adapt deployment-specific entry IDs without publishing credentials.

`pyscript.sil_shadow_publish_prices` is also triggered at startup and every minute. It publishes **only** `SolarLimiterTest/Input/PriceSchedule` through the existing MQTT integration. V2 contains `v,r,t,d,n,k,p,o`, 900-second slots, at most 24 slots per chunk and 288 total slots. `p` remains the original total tariff in integer micro-EUR/kWh. Origin codes are 0 unknown, 1 market, 2 forecast. Reassembly and atomic activation belong to the shadow firmware.

Both test price chunks and test withdrawals use QoS 1, non-retained, matching the inspected production HA publishers' requested baseline. A controlled QoS-0 A/B experiment did not remove the field-reported timeouts, so the exact original QoS-1 producer was restored. This negative result is not a root-cause finding. The minute heartbeat repeats complete idempotent revisions; missing chunks time out after 30 seconds and missing test-feed heartbeats fail closed after 180 seconds. Effective device delivery QoS still depends on subscription/granted QoS. No tariff, origin, rate gate, production publisher or firmware changed during this transport experiment.

The adapter uses a persisted request gate and response cache under `/config/data/sil_planner_shadow`. It requests the provider no more often than hourly, including across reloads and failed requests. Scheduled publication does not imply a new provider request. The installed production EPEX Spot source independently fetches hourly; account limits still apply. Provider request failures are redacted and delayed until the next allowed request time.

An OS file lock covers cache read and request-gate persistence across module reloads; the HTTP request runs outside that short lock. Linux uses `fcntl.flock` and Windows host checks use `msvcrt.locking`. Overlapping Pyscript invocations cancel the new invocation with `task.unique(..., kill_me=True)`, so an existing HTTP operation or chunk sequence is not cancelled by a second timer/service call.

The existing total-price calculation is reproduced exactly, including the provider's six-decimal raw-price rounding, percentage surcharge on the absolute raw price, fixed surcharge, tax, and final six-decimal rounding. Every selected quarter-hour must match the existing HA total-price sensor in integer micro-EUR/kWh before any test frame is published. A mismatch, missing interval, expired cache, unsupported resolution, or invalid numeric value blocks the feed visibly. There is no production-price fallback and no clock-based origin inference.

A blocked feed sends a separate v2 withdrawal (`n=0`, `k=0`, empty `p`/`o`) and immediately hides old test output locally. Status distinguishes zero published price chunks from a withdrawal message. The shadow firmware independently expires test input after 180 seconds without a fresh revision, preserving its measured-energy ledger. Diagnostic output alone cannot make an expired or mismatched price revision appear current. Retained configuration is marked retained rather than described as newly measured telemetry.

Current upstream schemas define `market` as actual EPEX SPOT data and `forecast` as a prediction. Unknown/missing strings remain unknown. The provider origin is separate from the fetch time: a fresh request does not prove market confirmation.

## Dedicated production v2 input

`pyscript.sil_production_publish_prices` publishes only `SolarLimiter/Input/PriceSchedule`, non-retained QoS 1, on its own minute timer. Its own `sensor.sil_production_price_input`, task guard, revision and withdrawal are independent of test configuration, Assessment validity and device availability. Test withdrawals cannot address the production topic; production failures cannot withdraw the test topic. Both consumers require matching v2 firmware with a 180-second input TTL. Do not point production at `SolarLimiterTest` or send v2 into the existing shared v1 topic.

The production reader calls `production_from_cache`, which reads the existing provider cache under its OS lock, checks source age (at most 4500 seconds), rejects failed refreshes and repeats exact HA tariff alignment. It neither calls the provider nor changes the persisted request gate. Raw micro-EUR/kWh and per-slot origins remain unchanged; the device applies its reported forecast-only premium. Missing, expired or mismatched data sends only an isolated production withdrawal. Source-alignment gaps remain visible rather than being filled or relabeled.

The existing minute cache-maintenance path remains required even if the test ESP is unplugged. That path checks only the real EPEX entry identity, coordinator and reference tariff; it does not depend on the test device, test config or test Assessment. Its hourly persisted request gate and authentication handling remain unchanged. Removing that maintenance path would eventually expire both cached consumers; it must remain installed independently of whether the test device is used.

Production Assessment is observed separately as `sensor.sil_comparison_production_assessment`. Its assessment curve requires its own fresh accepted price revision/start/count to match `sensor.sil_production_price_input`, plus actual relative-policy metadata. Production and test accepted revisions are never equated. Raw tariff curves and the configured maximum plus 100 W anchor are preserved.

## Comparison entities and view

The script observes an explicit diagnostic topic allowlist and creates only `sensor.sil_shadow_*` and `sensor.sil_comparison_production_*` states. Test budget, charge intent, config, state, assessment and runtime are read from the `SolarLimiterTest` namespace. Only production Planning/State and Planning/Config are observed. There are no actuator subscriptions or trigger publications.

`sil-planner-shadow-view.yaml` is one view to append at index 1 of the existing Test-Vivil dashboard; it is not a replacement dashboard. Keep the existing first view exactly unchanged. The view shows 36 hours forward plus three hours of history and overlays production/test budgets on one W axis, anchored to their validated configured maximum values plus 100 W.

The raw tariff remains distinct from the orange planning-assessment curve. The latter is shown only when the test input revision/start/count match the firmware's accepted Assessment summary and the shadow configuration is available. Forecast-only risk assessment uses `raw + abs(raw) * percentage / 100`, including for negative prices. The `horizon_steps_v1` model uses the reported projection time and separate 12-hour, 24-hour and later percentages; configuration and assessment values must match. Older firmware uses its reported uniform percentage. Legacy 5.7 applies `fcPriceF`; relative planning does not. Unknown origins receive no premium. The view never writes settings.

## Future DC energy

The additive `sensor.sil_comparison_future_energy` reads the existing producer's `/config/data/shed_forecast/last-packets.json` artifact without requesting or modifying any provider data. It requires a complete, consistently bound v2 shared-PV snapshot and its original validity window. Effective DC power is `p * f / 1000`; quality-unknown intervals remain missing. ChargePlan `w` is planned stored DC power, not AC or measured storage. ChargePlan `s` is deliberately not integrated as a pure forecast because its first slot can contain measured charger power.

All three integrals use one captured `now` and the same exact next 36 hours. Each step contributes `W * overlap_seconds / 3600000` kWh, including partial boundary slots. Local calendar-day splits use Europe/Berlin timezone rules, including 23/25-hour DST days. The view reports covered energy, covered duration and missing duration separately. Missing tails never erase valid covered energy and never become zero-energy assumptions. A complete covered calendar day includes its entire forecast sunlight period; partial days remain explicitly labeled and are not compared as full days.

Both charge intents must be valid, generated within 180 seconds and bound to the same published PV revision. Each must also match its own controller's fresh valid DischargeBudget cohort (`r,pv,t,d,n`); production and test price revisions are never compared with each other. Test energy requires a current ready feed and fresh valid Assessment, but does not require a retained display plan's price revision to equal the latest soft heartbeat. The orange price-assessment curve retains its separate strict latest-revision binding. Each energy row reports the actual plan generation time and age. Unusable intent is withheld independently. The card stops presenting cached calculations as current after 180 seconds or PV expiry. This is an energy forecast/intent comparison, not a full-battery guarantee or measured-energy recorder.

Freshness, origin counts, alignment evidence, assessment configuration, sample sequences, available battery Wh and material input differences are visible. Missing output remains missing until the actual shadow ESP publishes it. Separate controllers have separate measured energy/cache histories; a visual difference alone is not proof of a better algorithm or a hardware command.

The short main summary uses readable local timestamps and counts origins only within the next 36 hours (up to 144 slots). Full 288-slot input counts and technical revision/alignment details remain separate below the charts.

When the firmware actually reports `allocationPolicy=relative_time_tiers_v1`, the main settings tile shows its reported tier percentages, forecast premium, `storageMinimumNetAdvantageEurKwh`, `chargeEfficiencyPct` and `dischargeEfficiencyPct`. Missing diagnostics remain unknown. Legacy deferral tolerance and `fcPriceF` move to explicitly inactive detail rows for that test policy. The storage margin applies only to newly stored energy and is distinct from the old held-energy deferral tolerance. A reported 90% charge and 90% discharge efficiency means 100 Wh PV DC becomes 90 Wh stored battery DC and 81 Wh delivered AC. The shared PV integral is unchanged; ChargePlan `w` is integrated as already projected battery-side DC without applying efficiency a second time. Production retains its existing charge-loss model.

Below the energy table, the test's reserved PV input is derived from its covered battery-DC charge integral divided by its explicitly reported charge efficiency. For example, 0.9 kWh stored DC at 90% efficiency reserves 1.0 kWh solar DC over the same covered plan intervals. Missing/invalid efficiency or an unavailable charge plan withholds the conversion. No production efficiency is inferred.

## Checks and rollback

Run the focused pure-adapter checks with mocked fixtures before deployment. Verify exact unchanged first-view structure, matching tariffs, complete non-retained test frames and live test output after firmware deployment. Do not claim the end-to-end comparison passed before the real shadow device is running.

Run the maintained offline energy regressions from the repository root with `python test/host/ha_forecast_integrals_test.py`. They import the curated core and execute the actual energy timer against synthetic states, without Home Assistant or network access. Coverage includes DC units, boundary overlaps, DST days, missing versus zero values, own-controller cohorts, soft price heartbeats and withdrawals.

For rollback, remove only the added comparison view and the two newly installed files after preserving any desired test evidence. Stop the isolated shadow firmware separately. Do not remove or modify the production Shed forecast adapter, existing price/PV automations, source integration or original dashboard content.

Official source semantics: [Energyforecast FAQ](https://energyforecast.de/faq?locale=de), [V1 schema](https://energyforecast.de/api-docs/v1/swagger.yaml), [V2 schema](https://energyforecast.de/api-docs/v2/swagger.yaml).
