# Isolated SIL planner comparison in Home Assistant

This diagnostic example preserves production firmware, tariffs, price/PV automations, and the original Test-Vivil Home view. It requires a separately reviewed hardware-mocked shadow firmware profile. It does not send inverter or BMS commands.

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

## Comparison entities and view

The script observes an explicit diagnostic topic allowlist and creates only `sensor.sil_shadow_*` and `sensor.sil_comparison_production_*` states. Test budget, charge intent, config, state, assessment and runtime are read from the `SolarLimiterTest` namespace. Only production Planning/State and Planning/Config are observed. There are no actuator subscriptions or trigger publications.

`sil-planner-shadow-view.yaml` is one view to append at index 1 of the existing Test-Vivil dashboard; it is not a replacement dashboard. Keep the existing first view exactly unchanged. The view shows 36 hours forward plus three hours of history and overlays production/test budgets on one W axis, anchored to their validated configured maximum values plus 100 W.

The raw tariff remains distinct from the orange planning-assessment curve. The latter is shown only when the test input revision/start/count match the firmware's accepted Assessment summary and the shadow configuration is available. Forecast-only risk assessment uses the configured percentage, including `raw + abs(raw) * percentage / 100` for negative prices, then the existing `fcPriceF` planning factor. Unknown origins receive no premium.

Freshness, origin counts, alignment evidence, assessment configuration, sample sequences, available battery Wh and material input differences are visible. Missing output remains missing until the actual shadow ESP publishes it. Separate controllers have separate measured energy/cache histories; a visual difference alone is not proof of a better algorithm or a hardware command.

The short main summary uses readable local timestamps and counts origins only within the next 36 hours (up to 144 slots). Full 288-slot input counts and technical revision/alignment details remain separate below the charts.

## Checks and rollback

Run the focused pure-adapter checks with mocked fixtures before deployment. Verify exact unchanged first-view structure, matching tariffs, complete non-retained test frames and live test output after firmware deployment. Do not claim the end-to-end comparison passed before the real shadow device is running.

For rollback, remove only the added comparison view and the two newly installed files after preserving any desired test evidence. Stop the isolated shadow firmware separately. Do not remove or modify the production Shed forecast adapter, existing price/PV automations, source integration or original dashboard content.

Official source semantics: [Energyforecast FAQ](https://energyforecast.de/faq?locale=de), [V1 schema](https://energyforecast.de/api-docs/v1/swagger.yaml), [V2 schema](https://energyforecast.de/api-docs/v2/swagger.yaml).
