# Planner shadow comparison (5.7.0 candidate)

This experimental profile is diagnostic. It must not be uploaded to a production
inverter controller or interpreted as a future hardware command. Normal defaults
remain a 0.03 EUR/kWh held-energy tolerance and a 0% uncertainty premium. The
owner explicitly provisions the test ESP with 0 and 30%, and may persist those
visible settings for restart continuity.

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

Only forecast-origin tariffs receive the internal valuation
`raw + abs(raw) * premium / 100`. Market and unknown values stay unchanged.
The raw integer micro-EUR/kWh schedule and raw negative/zero-price physical gates
remain unchanged. Acquisition losses, wear and the independent 0.03 EUR/kWh
minimum acquisition benefit retain their existing meaning. A zero held-energy
tolerance protects only strictly higher values; equal prices remain usable early.

Assessment changes do not create measured battery energy or PV energy. Separate
production and shadow controllers can hold different conservative measured-energy
histories. Their budgets can therefore differ even with the same current SOC.
This experiment does not claim strict global optimality.

Settings in the Limiter category:

| Key hint | Label | Default |
| --- | --- | --- |
| `defTol` | Held Energy Deferral Tolerance (EUR/kWh) | 0.03 |
| `predPct` | Forecast Price Uncertainty Premium (%) | 0 |

Tolerance must be finite and nonnegative. Premium must be finite and within
0..100. Both invalidate the allocator and retained-plan context when changed.

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

The current planner slot continues using the received live tariff; future
scheduled prices use the existing forecast price factor. The scheduled diagnostic
assessment applies that same positive factor to the risk-adjusted raw schedule.
The comparison must bind raw/assessed curves to identical accepted input revision
and coverage, and visibly mark missing, old or unmatched data. Unknown origin is
never inferred from time.
