# Planner shadow comparison (5.8.0 candidate)

This is an isolated, unpublished test profile. Do not merge the relative policy
into production or upload this profile to a production inverter controller.
The normal build retains the legacy allocation algorithm and normal settings.
The owner's existing test uncertainty premium is **10%**; provisioning the new
three time shares must not overwrite it or other stored settings.

## Assessment and physical accounting

### Checkpoint verification

The 5.7.0 checkpoint completed a 28.3-minute COM8 observation without Guru,
panic, or unexpected reset. Test-only price withdrawal produced hold mode,
virtual zero output and an invalid budget within 3.578 seconds; the persistent
HA minute timer restored a complete valid plan without a Windows publisher.

The combined stability gate did not pass: a 310-second qualified window had
one HTTP timeout among eleven checks. Real transport error counters were stable
inside that window, but earlier in the same boot six TX timeouts and eleven
connection failures occurred. The new assessment is not accepted for production.
The owner may change the test settings; the initially provisioned 0/30 values
are not a claim about its current persisted settings.

### Relative time groups

Only the shadow profile selects `relative_time_tiers_v1`. All 144 quarter-hour
prices use the accepted v2 schedule on the same basis: unchanged market/unknown
prices and `raw + abs(raw) * premium / 100` for forecast-origin prices. The live
current tariff independently gates physical output; it is not substituted into
the ranking. Legacy comfort/deadband, held tolerance and future price factor do
not control this policy and are hidden in the shadow settings UI.

| Key hint / API key in Limiter | Label | New default |
| --- | --- | --- |
| `cheapPct` / `7483E2523305` | Cheap Time Share (%) | 5 |
| `veryPct` / `38A4CC72FD22` | Very Expensive Time Share (%) | 10 |
| `expPct` / `DBDB92234F99` | Additional Expensive Time Share (%) | 30 |
| `storAdv` / `0464F65720FE` | Storage Minimum Net Advantage (EUR/kWh) | 0.05 |

Shares must be finite, nonnegative and total at most 100%. Each setting write is
validated before mutation. CM bulk routes process settings individually, so a
rejected bulk request can leave earlier valid writes applied; they are not an
all-or-nothing transaction. They express time shares, not price or
energy percentages. Whole equal-price blocks stay together. The cheapest and
highest boundaries include the entire boundary block; conflicting middle space
is reduced without splitting ties or overlapping groups. Positive cheap/high
shares retain the bottom/top block of a nonflat curve. Additional-expensive can
be empty, particularly with two price levels. Actual slot counts are published
because plateaus can expand a group beyond its target. Additional-expensive time
is counted below the actual expanded very-expensive boundary; a top plateau
does not silently consume its additional 30% quota. Cheap-boundary conflicts can
still leave less space. A fully flat curve is
neutral and creates no artificial peaks.

### New PV acquisition economics

The storage allowance is finite and nonnegative. Its default 0.05 EUR/kWh combines
the old 0.02 wear and 0.03 minimum-gain allowances per later delivered AC kWh; it
is configurable and does not claim the owner's exact cell replacement cost.
It applies only to acquiring/shifting new PV energy, never to deferring use
of already held inventory. Large margins must not erase or release that inventory.

The relative test model fixes charging at 90% and discharge at 90%:
100 Wh PV offer -> 90 Wh stored battery DC -> 81 Wh later AC. Actual measured BMS
energy and power enter the real ledger unchanged; they are not charged another
90% factor. Normal legacy efficiency/economic behavior is unchanged.

For discretionary storage, require:

minimumTargetTierAssessedPrice - storageIntervalAssessedPrice / 0.90 >= storageMinimumNetAdvantageEurKwh

Both prices use the accepted schedule and the same origin-premium rule. The
0.90 divisor is direct-route efficiency (0.90) divided by cycle efficiency (0.81),
so the direct-route common loss is not billed twice. Evaluate the target tier's
minimum assessed price among remaining raw-positive intervals, a conservative
acquisition-price hurdle compatible with uniform allocation and PV caps on an
isolated peak. One profitable peak cannot authorize the entire tier. This is
not a guarantee of realized profit or that the acquired energy will all be
discharged in the forecast horizon. Only strictly higher-tier, unfunded demand
with possible battery acceptance qualifies. Forecast direct PV already covering
every target output interval creates no additional battery demand. Funding still follows very-expensive before additional-expensive.
Physical reserve restoration and otherwise curtailed PV are separate needs;
zero/negative raw-price hard gates still apply.

### Chronological energy and priorities

Very-expensive demand has first claim on measured inventory. Additional-expensive
can use only inventory above the remaining very-expensive MAX demand. Each tier
spreads its usable budget over its remaining duration; the partially elapsed
current quarter uses its actual remaining seconds. Neutral and cheap slots have
no economic battery discharge. No absolute-price fallback replaces an invalid
relative plan; full 144-slot coverage and the test feed lifetime are required.

PV storage enters only at its chronological arrival, bounded by charging power,
physical headroom and reserve restoration. Neutral PV can fund future expensive
and very-expensive deficits when that acquisition passes the economic gate;
expensive PV can fund a profitable strictly higher future tier.
High-tier PV is used directly rather than charged merely for another same-tier
slot. A charging slot never also discharges, and stored PV is removed from direct
PV. ChargePlan.w is stored battery-side DC, after the charge loss. Direct PV
subtracts the diverted PV input (stored battery DC / 0.90) before its own
0.90 direct-output factor. Forecast PV never lowers today's measured-inventory reservation for a future
higher tier. Unknown PV adds no energy; reported SOC is never assumed to be 100%.

Cheap periods with solar use the existing charge-priority threshold/share
semantics (normally 95% / 0%), and discretionary storage must also pass the same
acquisition economics gate. This does not change the explicit manual
charge_priority operating mode. The real current cheap-window full latch survives
replanning until SOC falls below the threshold; it ends with that cheap window.
Only the actual current latch is retained, never a full SOC projected in the
future. Other explicit operating modes retain their existing behavior.

Raw zero/negative intervals create no reservable demand or allocation duration.
Raw zero/negative scheduled prices and the fresh current tariff gate automatic
shadow output independently of assessment. Current source qualification, MOS,
faults, reserves and correction/MAX limits remain authoritative. The relative
path disables the legacy MIN economic override, and final corrected output stays
within its current approved ceiling. No battery permission follows from a PV
fallback. Separate production/test measured ledgers can differ despite equal SOC.
The allocator is bounded and conservative, not a claim of global optimality.

## Isolated profile

Build with `pio run -e planner_shadow`. It requires `SIL_MOCK_TRANSPORT=1`
at compile time, rejects physical relay output features, and retains the RS485
module and final command boundary's unconditional mock return. The existing
private fixture environment and its broker guard are unchanged.

The shadow profile requires broker `192.168.2.3:1883`, publish root
`SolarLimiterTest`, and client `SolarLimiterTest-<12 uppercase MAC hex digits>`.
It uses DHCP. Inherited invalid routing disables MQTT before the first loop.
HTTP apply/save/config_raw, including bulk forms, validate protected values
before changing the actual setting. The shadow default-reset API is unavailable.
Other assessment and planning settings remain configurable.

Read inputs are fixed to the currently verified production sources:

| Source | Topic or root | JSON path |
| --- | --- | --- |
| SBT | `SolarBatteryTelemetry` | SBT protocol |
| Grid | `tele/powerMeter/powerMeter/SENSOR` | `EMH.Power_curr` |
| Inverter AC | `tele/tasmota_1DEE45/SENSOR` | `ENERGY.Power` |
| Negative price | `SolarLimiter/Input/NegativePrice` | `none` |
| Current price | `SolarLimiter/Input/ElectricityPrice` | `none` |
| PV forecast | `homeassistant/sensor/solar_forecast/shed/schedule` | Existing v2 PV contract |
| Test prices | `SolarLimiterTest/Input/PriceSchedule` | Atomic v2 below |

Application publications, status and last will stay under `SolarLimiterTest`.
Shadow app publications are paced to at most one accepted frame per 250 ms.
Lazy interval no-ops do not consume that slot; deferred plans remain pending, and
mode state/status retain their partial progress. CM core telemetry is unchanged.
A `SHADOW_TX` UART line identifies the tag and length only when TX failure counters
increase within an app publish attempt. `SHADOW_STAGE` separately reports failure
counter changes only in the optional `pio run -e planner_shadow_trace` build
(`SIL_SHADOW_STAGE_TRACE=1`) across WiFi update, MQTT loop and CM client handling, with numeric
RX callback deltas only. It identifies the triggering stage, not the previous
contents of the TX queue. This is an A/B transport experiment, not a proven cure
for all TCP stalls.

No MOS, physical inverter, production mode or discovery command is published.
There is no production-price topic fallback.

## Price/origin v2

Production v1 remains unchanged. Only the shadow parser accepts the test v2
contract on the fixed test topic:

```json
{"v":2,"r":1792454400,"t":1792454400,"d":900,"n":2,"k":0,"p":[-200000,500000],"o":[2,1]}
```

`p` contains unchanged integer micro-EUR/kWh tariffs. `o` has equal length:
0 unknown, 1 market, 2 forecast. Each chunk covers at most 24 slots, with
288 slots/12 chunks maximum. All chunks in one revision share start/count and
must arrive within the existing 30-second assembly lifetime. Conflicts and
malformed origin/type/length values are rejected. Only a complete revision is
activated. An origin-only change also invalidates content and cache identity.

A fresh revision with `n:0,k:0,p:[],o:[]` explicitly withdraws the test feed.
Withdrawal does not refill the measured battery ledger. Shadow revisions expire
after 180 seconds without an accepted fresh heartbeat; duplicate packets do not
renew this lifetime. Stale or missing test prices invalidate retained budgets and
hold virtual output at zero. The production v1 lifetime is unchanged.

## Diagnostic contract

Existing `Planning/DischargeBudget`, `ChargePlan`, `State`, `Config`,
`Diagnostics/Runtime` and `System-Info/ESP` remain available under the test root.
Config adds `deferralToleranceEurKwh`, `predictionPremiumPct`,
`shadowProfile` and `priceInputVersion`.

`Planning/Assessment` is a bounded v1 summary with generation, validity/reason,
accepted price revision/start/count/age, origin counts, assessment settings,
raw and assessed current scheduled prices, the independently received current
tariff, measured energy/SOC/capacity/power and source sequences/validity.

For the relative profile, current and future scheduled assessment use the same
basis without the legacy price factor. `currentMeasuredEurKwh` remains a separate
physical input. Config and Assessment add `allocationPolicy`, `priceBasis`,
`legacyPriceControlsActive`, `cheapTimePct`, `veryExpensiveTimePct` and
`additionalExpensiveTimePct`, `storageMinimumNetAdvantageEurKwh`,
`chargeEfficiencyPct` (90) and `dischargeEfficiencyPct` (90). Assessment adds `tiersValid`, `currentPriceTier`,
`tierStart`, `tierSlots`, actual `tierCheapSlots`, `tierNeutralSlots`,
`tierExpensiveSlots`, `tierVeryExpensiveSlots`, and three assessed-price boundaries:
`cheapMaxAssessedEurKwh`, `expensiveMinAssessedEurKwh`,
`veryExpensiveMinAssessedEurKwh`. Empty/flat groups have null boundaries. Consumers
must bind the actual accepted price revision and coverage before rendering tiers.

ChargePlan v1 remains DC: `w` is intended storage power. `s[0]` is the current
measured MPPT override; later `s` slots are qualified forecast inputs. It must not
be presented as a pure original-forecast curve or an AC inverter command.

### Candidate verification boundary

The uploaded 5.8.0 test image completed 1200 seconds without Guru, panic or an
unexpected reset; 238 advancing serial diagnostics reported zero monitored MQTT
error and flow counters on the final boot. All 80 returned configuration values
matched the post-save reference after a deliberate reset. The new shares were
5/10/30%, storage allowance 0.05 EUR/kWh, and the owner's 10% forecast premium and
80/10 charge-priority settings were preserved. Nondefault new settings were also
saved and verified across a reset before restoring these final values.

A qualified 310-second real-source window passed functional planning checks.
Overall stability still failed because one HTTP verification timed out, including
its HA fallback; earlier provisioning also recorded MQTT connection/overflow
errors. Test-only withdrawal reached hold/virtual zero within 3.656 seconds and
natural HA recovery completed within 104.718 seconds. A subsequent real-PV plan
had matching budget/charge cohorts and spread energy across the selected high
tier. A separate observer incorrectly required an unchanged mode to arrive as
nonretained MQTT; its failed gate is preserved independently of the device
evidence. None of these results authorizes production adoption.

The 5.8.0 host regressions cover measured priority before uncertain sunlight,
fair duration/partial quarters, future PV arrival and headroom, strictly higher
storage, equal-price blocks/flat curves, raw stops, legacy-knob independence,
origin/premium changes, source gaps, cheap-window latch continuity, exact/below
storage-margin boundaries, conservative minimum-tier acquisition valuation and direct-PV acceptance caps, both forecast-price
sides, two physical loss factors and unchanged measured-energy integration.
An integration test compiles the actual main price/mode/feed bindings: positive
fresh tariffs preserve the original 180-second BMS grace, then qualified PV
fallback; live negative prices or missing test prices stop output. Hardware
acceptance is separate. Existing MQTT backpressure/overflow/timeout observations
remain a known limitation; pacing and the optional stage trace are diagnostic
experiments, not proof that network stalls are solved. No CM dependency changes
or production adoption are part of this candidate.
