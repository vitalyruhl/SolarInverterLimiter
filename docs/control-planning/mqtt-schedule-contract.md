# MQTT price and PV schedule contract

Updated 2026-10-22. Source of truth: `src/control/FuturePrices.h`, `src/control/PvForecast.h`, and the HA producers under `docs/home-assistant/` and `ha-price-schedule.yaml`. Schedules offer planning inputs only; they never command MOS or inverter hardware.

## Transport and ownership

| Stream | Default topic | Producer / receiver |
| --- | --- | --- |
| Price v1 | `homeassistant/sensor/electricity_price/schedule` | Existing single HA price automation / `FuturePrices` |
| PV v2 | `homeassistant/sensor/solar_forecast/shed/schedule` | Single Shed Pyscript service, triggered by one HA automation / `PvForecast` |

Both producers publish JSON chunks at QoS 1 with `retain: false`, normally every five minutes and on their configured startup/data triggers. SIL configures the two inputs independently and subscribes at QoS 1. The receivers can restore a still-valid retained snapshot from an external sender, but receipt time never extends its coverage or expiry. A cold receiver of the normal non-retained stream waits for the next complete publication. One MQTT topic carries all chunks of its stream; chunks are not retained individually.

Payloads must be 1..768 bytes without embedded NUL. All times are UTC Unix seconds; slots are fixed 900-second intervals, independent of local DST. Receiver UTC must be at least `1704067200`. Revision/session values and every chunk must describe one immutable snapshot. The parser commits only after every required part arrives and validates. Missing chunks preserve the previous active plan; a conflicting repeated part poisons that pending revision. Duplicate active data is idempotent and does not refresh any deadline. Pending assembly is restarted after more than 30 seconds unless the revision is already poisoned; a poisoned revision needs a newer revision.

## Price v1 fields

| Key | Type / constraint | Meaning |
| --- | --- | --- |
| `v` | Integer, exactly 1 | Wire version |
| `r` | uint32 | Generation/revision time; monotonic against active and pending revisions |
| `t` | uint32, divisible by 900 | First slot start; `t <= r < t + 900`, `t <= receiver UTC` |
| `n` | 2..288, or 0 for withdrawal | Total number of quarter-hour prices |
| `k` | 0..11, valid for `n` | Zero-based chunk index; offset `k * 24` |
| `p` | Integer array | Exactly `min(24, n - k * 24)` prices, each -2000000..10000000 micro EUR/kWh |

The receiver rejects generation more than 120 seconds in the future or more than 259200 seconds old. Coverage ends at `t + n * 900` and must fit uint32. A plan is unusable at or beyond that end; its receipt time is not an additional lifetime. A withdrawal uses `n=0`, `k=0`, `p=[]` and a new non-conflicting revision. It clears schedule storage without refilling measured battery energy.

The receiver stores up to 288 slots, while the planner considers at most 144 slots / 36 hours. Future wire prices receive the existing `fcPriceF` factor in the planner; the independently received current price remains the current-slot value. EPEX provenance is provider-unknown; the wire schema does not certify distant values as confirmed market prices.

## PV v2 fields

Every metadata field is repeated identically in every chunk.

| Key | Type / constraint | Meaning |
| --- | --- | --- |
| `v` | uint32, exactly 2 | Wire version |
| `s` | uint32, `1704067200 <= s <= g` | Producer ledger session timestamp, not an authentication token; cannot roll back against active/pending state |
| `r` | uint32, equal to `g` | Monotonic snapshot revision |
| `t` | uint32, divisible by 900 | First slot start; `t <= g < t + 900`, `t <= receiver UTC` |
| `d` | uint32, exactly 900 | Slot duration in seconds |
| `n` | uint32, 2..144 | Total slots, at most 36 hours |
| `k` | uint32, 0..5, valid for `n` | Zero-based chunk index, 24 slots per part |
| `g` | uint32 | Generation time; no more than 120 seconds ahead of receiver UTC |
| `e` | uint32 | Exclusive expiry; greater than generation and receipt UTC, no later than `t + n * 900` |
| `f` | uint32, 500..1200 | Calibration multiplier in thousandths; applied once on reading raw slot power |
| `c` | uint32, 0..30 | Eligible calibration-day count |
| `a` | uint32, 0..65535 | Age in days of newest eligible calibration day; 65535 represents no eligible history in the producer |
| `q` | String | `uncalibrated` requires `f=1000` and `c<14`; `calibrated` requires `c>=14` |
| `o` | String, exactly `forecast_solar` | Source identifier |
| `p` | Array | Raw integer average W, 0..10000, or `null` when corresponding quality is 1 |
| `x` | Integer array | Per-slot quality: 0 = known value, 1 = missing, 2 = hourly disaggregation |

The two arrays have exactly `min(24, n - k * 24)` entries. A missing slot requires `p=null` and `x=1`; it is never a forecast of zero W. The current Shed producer emits quality 2 for supplied hourly data and quality 1 for gaps. Before acceptance, the receiver also requires `receiver UTC - t < 144 * 900`. Active validity requires UTC within both coverage and explicit expiry. A requested energy interval fails if any contributing slot is missing or the interval exceeds those bounds. PV v2 has no zero-slot withdrawal; topic changes clear its local snapshot, otherwise immutable expiry applies.

Illustrative two-slot, single-chunk payload (not a live capture):

```json
{"v":2,"s":1792453500,"r":1792454400,"t":1792454400,"d":900,"n":2,"k":0,"g":1792454400,"e":1792456200,"f":1000,"c":0,"a":65535,"q":"uncalibrated","o":"forecast_solar","p":[1000,null],"x":[2,1]}
```

This example is usable only while the receiver's UTC falls in its stated interval. The calibration multiplier turns a known raw 1000 W slot into `1000 * f / 1000` W, once; the publisher does not pre-apply it.

## Bounded memory and planning boundary

PV uses two fixed snapshots with 720 bytes of snapshot/state budget, plus its own 4096-byte JSON arena and allocator/object overhead. The compile-time state limit is 784 bytes. Its arena remains separate from price and telemetry parsing. The planner uses fixed 144-slot price, PV, demand, reserve-debt and forward-flow arrays. Backward work is bounded to 20736 rank visits, with linear forward work on each valid control tick. The fixed workspace has no per-step heap allocation or large array stack copy; the measured memory and firmware limits are recorded in the 5.4.8 acceptance report.

Raw provider `wh_period` uses END timestamps. The producer validates previous-to-end intervals, assigns their Wh to the actual containing UTC hour, then distributes that hourly total over four quarters with energy conservation and explicit disaggregated quality. HA's built-in end-timestamp hour-floor aggregation is not used; [migration evidence](../home-assistant/shed-migration.md) records the source conflict and schema2 reference migration. No absent nighttime interval is filled with an assumed zero. Calibration stays neutral until at least 14 eligible completed days in 30 days pass whole-day original-forecast and actual-energy qualification. Calibration metadata is an asserted producer result; SIL validates its bounds/consistency rather than independently reading HA history.

Since 5.4.8, live reservation, discharge projection and charge projection use the same chronological flows. A backward marginal-demand calculation can account for PV after a morning discharge and before an evening discharge; the forward ledger credits that PV only at arrival and within physical headroom and qualified charging power. An initial Reserve OFF debt must be restored before any stored PV becomes dischargeable. Unknown intervals grant no assumed generation and do not erase later independently known sunlight. Predicted generation never increases the actual measured battery pool. Forecasts do not establish BMS freshness, relax reserve/fault policy, force export or prove physical delivery. See [planning](05-energy-planning.md) and [telemetry qualification](01-measurement-validation.md).


## Battery discharge-budget output v1

Introduced in 5.2.0 with [isolated USB acceptance](../acceptance/2026-10-22-discharge-budget-export.md). Version 5.4.4 retains the v1 envelope and extends `w` to a complete conditional chronological allowance projection. Source of truth: `src/control/DischargeBudget.h`, the optional projection in `FuturePrices::plan()`, and `src/main.cpp` publication. Device and HA acceptance remain separate from the wire definition.

SIL publishes one complete JSON snapshot to `<resolved publish base>/Planning/DischargeBudget`, QoS 0 and non-retained. This is a diagnostic output, never a control input. When capture is due (every 60 seconds while connected and after reconnect/topic change), the normal planning tick anchors the first value to its live allocation and projects later intervals with a private energy/headroom ledger. It does not call the live planner again, advance the measured pool, or change cache time. Since 5.4.6, both plan publications stage candidates separately: a brief source gap can keep the previous timely computed snapshot instead of replacing it with empty arrays. Failed serialization or send retries after five seconds without blocking existing scalar MQTT or control paths. Cadence follows normal control ticks, not a separate control scheduler.

Retained projections keep their original `g`, `t`, `r`, `pv` and arrays. They expire no later than 180 seconds from original generation, additionally bounded by monotonic time, held-source grace and current forecast/plan coverage. Repeated failures, retries, reconnects and input updates never renew this allowance. Since 5.4.9, a new current price or accepted price/PV content requests fresh capture without revoking an otherwise retainable display frame during source requalification. Relevant settings, configured input/output topic changes, price factor, PV validity changes, clock anomalies, loss of current time coverage and known genuine BMS/charger faults remain hard invalidations. Switching a configured forecast topic invalidates both publications before clear/resubscribe, even if its replacement data is complete before the next control tick. Poll-only revisions still preserve exact content identity; public revisions and live freshness advance normally. Unknown charger telemetry cannot clear a previously known charger fault. Expiry publishes an invalid snapshot; source recovery requests fresh capture promptly. Held diagnostics never relax control freshness, grant MOS permission or become control inputs. During SIL's MQTT disconnection, HA availability may independently hide a preserved snapshot.

| Key | Type / meaning |
| --- | --- |
| `v` | Integer 1 |
| `valid` | Boolean plan validity |
| `reason` | Existing planner reason |
| `g` | UTC Unix generation seconds |
| `r` | Accepted price revision |
| `pv` | Accepted PV revision, zero if absent |
| `t` | Current quarter-hour slot start, divisible by 900 |
| `d` | Integer 900 seconds |
| `n` | Actual considered interval count, 2..144 when valid; zero otherwise |
| `w` | Conditional chronological integer non-negative AC battery allowance in W, length `n`; empty when invalid |

The first value applies on `[g, t + d)`; later value `i` applies on `[t + i*d, t + (i+1)*d)`. Coverage ends at `t + n*d`. Each value is allocated AC battery energy divided by its interval duration and truncated to whole watts; the first interval uses only its remaining duration. Known intervals with no allocation are zero. Invalid, absent or expired planning is unavailable, never a synthetic zero schedule. At generation, `w[0]` equals the real scalar planner's `batteryCeilingW`; it is not the final protected, demand-limited or corrected inverter command.

Each projected interval uses existing relative-price deferral and profitable-charging thresholds. Higher future prices can protect stored energy, while surplus inventory can serve comparable or cheaper windows. Configured MAX is the AC allowance throughout; direct PV reduces required battery discharge. A charge target may retain PV below MAX / dischargeEfficiency when a later battery target justifies it. The previous fixed-MAX subtraction no longer discards all such recharge offers. This is a conditional power/energy model, not promised household demand or a forecast-yield calibration.

The private forward ledger debits usable AC discharge and books actual planned DC charging once, with the existing conversion efficiencies. Storage is bounded by available PV, charge permission, charge power and physical headroom. Initial Reserve OFF debt is non-dischargeable and is paid before usable inventory grows; price-prioritized future requirements include that prerequisite without treating it as free capacity. Later sunshine cannot fund an earlier interval. The future trace sum can include unreserved inventory and later recharge, so it is not the scalar reservedWh/allocWh. No forecast energy enters the live measured pool.

These are conditional upper bounds, not guaranteed discharge, household demand, or future SetValue. Actual demand, PV, reserve-ON hysteresis and its live latch, source qualification, faults, MOS capability, configured MIN, PID state, ramps and final output correction still determine actual inverter requests. Discharge MOS OFF does not zero this intent trace; charge MOS OFF continues to block storage credit. The projection does not reconstruct the controller's reserve latch or bypass its guards. Current protection/control precedence and all hardware paths are unchanged.

Serialization has a fixed 2048-byte buffer and fails with no publishable payload on truncation. The persistent publication object holds the 144-rate trace and buffer. SIL requests a 2304-byte PubSubClient packet buffer (formerly 1024); topic plus payload plus seven bytes must fit. CM checks buffer allocation internally but its setter returns no status; SIL separately checks every raw publication result. A failed enlargement/send affects this diagnostic and remains retryable. No CM source, receive schema, storage settings or existing scalar schemas change.

The [sensor example](../home-assistant/sil-discharge-budget-sensor.yaml) uses JSON attributes and `ready`/`invalid` state, existing `<base>/System-Info/status` availability, and 180-second expiry. It is a schedule container, not a measured power statistic. Merge it into the existing MQTT sensor section and confirm its entity registry name during separately authorized installation. The [overlay card](../home-assistant/sil-price-discharge-overlay.yaml) preserves the supplied price entity, thresholds and time formatting. Its 27-hour window starts three elapsed hours before the local current-hour boundary and retains the 24-hour forward endpoint. A translucent battery-budget area uses the right W axis; price remains on the left EUR/kWh axis. Both generators use these identical bounds. Price history is shown only where supplied; the budget starts at its actual generation time and does not reconstruct earlier plans. Data stops at real coverage; stale or invalid attributes produce no curve. No actual MOS-state filter is applied.

Card options were checked against the [official ApexCharts card documentation](https://github.com/RomRider/apexcharts-card#series-options), including `area`, `stepline`, `opacity`, `data_generator` and the [axis mapping](https://github.com/RomRider/apexcharts-card#yaxis-options-multi-y-axis). The existing experimental color thresholds are preserved; no incompatible `apex_config.fill` override is added. YAML/generator checks do not constitute rendered-card or live HA acceptance.

## Charge intent projection output v1 (5.4.0 test candidate)

Topic: `<base>/Planning/ChargePlan`, non-retained, using the same diagnostic capture cadence (60 seconds and after reconnect/topic change) as the unchanged discharge budget. Both snapshots come from one normal allocation tick; charge projection runs after the actual controller decision. It never feeds an inverter, MOS command, PID state or the measured-energy ledger.

This is a conditional 36-hour DC charging projection from the same forward ledger as live battery allowance and the discharge trace. It includes economically justified PV withholding, existing reserve requests and known PV surplus above the output ceiling, within charge permission, available PV and headroom. Later charge needs are recomputed along the projected timeline instead of holding only today's shortfall and rate. Current-interval power and Cheap/Expensive state follow qualified live inputs; the first displayed charge is also capped by the independently guarded controller decision. Unknown PV remains explicit. Scalar deadline and remaining-need metadata describe the current recognized request, while the per-slot arrays describe the conditional future flow. MIN, PID, safety, actual demand and future replanning retain live authority.

| Key | Meaning |
| --- | --- |
| `v`, `kind` | Integer 1 and literal `intent_projection` |
| `valid`, `reason` | Projection availability and reason; source loss is unavailable, confirmed control/charge blocking is known zero withholding |
| `g`, `r`, `pv` | UTC generation time and accepted price/PV revisions |
| `t`, `d`, `n` | UTC quarter-hour start, 900 seconds and interval count; same alignment as discharge budget |
| `target`, `unmetWh` | Existing economic deadline and remaining unfulfilled DC charging requirement |
| `w` | Deliberately withheld DC W, integer 0..12000 or null for unknown intervals |
| `s` | Expected forecast DC PV W, integer 0..12000 or null, independently of known withholding |

Both arrays have `n` entries, at most 144. Invalid snapshots have `n:0` and empty arrays. The first value applies from generation to the current interval end; later values use chronological quarter-hours. Missing future PV or uncertain projected SOC produces unknown withholding rather than invented zeros. A confirmed disabled charge direction zeros withholding without erasing known sunlight. Actual discharge MOS OFF does not erase red desired-discharge intent. Red remains AC allowance; blue and sunlight are DC and are labeled separately.

The new publication uses fixed 2048-byte serialization and the existing 2304-byte MQTT packet buffer. It does not enlarge the shared MQTT buffer or construct a heap-backed JSON document. Any serialization/packet overflow fails closed; publication retries remain diagnostic only. Application-level chunking is not introduced without evidence that this export causes the known runtime fault. TCP segmentation is separate from a chunked MQTT schema, which would require receiver reassembly.

Examples: [isolated test sensors](../home-assistant/sil-charge-plan-sensor.yaml) and [energy-plan overlay](../home-assistant/sil-energy-plan-overlay.yaml). Initial candidate acceptance used only `SILTest/COM7` on the isolated COM8 board. The owner subsequently authorized production 5.4.0 OTA and the same presentation in Home #3, using production entities and three hours of actual recorded history with 24 forward hours. See the [acceptance and rollout record](../acceptance/2026-10-23-charge-intent-projection.md) for the separate TEST/production boundary and single-card YAML guidance.
