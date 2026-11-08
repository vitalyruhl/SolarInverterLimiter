# Relative planner and isolated shadow profile (5.8.2)

The owner-authorized 5.8.1 promotion enables the reviewed relative algorithm in
normal firmware as well as the isolated shadow image. Deployment is gated on
source/build review, a fresh 15-minute test-ESP observation, then the owner's
production upload/runtime checks. Source/build success is not runtime acceptance.

Version 5.8.2 preserves the existing full-battery PV restart in both policies.
At 100% SOC, fresh qualified BMS/charger samples, healthy discharge permission,
valid meters and a valid positive-price relative plan allow household demand
within MAX to release curtailed PV even when the MPPT reports zero. Charging
intent remains zero and the configured MIN cannot force output. Source faults,
missing plans, nonpositive prices and source requalification still restrict output;
below 100% SOC the relative allocation is authoritative again. This correction
has host regression coverage; no new device acceptance is implied.

## Defaults and persisted values

Compiled control defaults match the selected owner test configuration:

| Control | Default |
| --- | --- |
| Cheap / very expensive / additional expensive time | 10 / 10 / 20% |
| Storage net advantage / forecast-origin premium | 0.05 EUR/kWh / 10% |
| Reserve OFF / ON | 5 / 10% |
| Charge threshold / inverter PV share | 80 / 10% |
| MAX / MIN / idle | 900 / 0 / 500 W |
| Output correction | -100 W |
| PID Kp / Ki / Kd | 0.45 / 0.05 / 0.02 |
| RS485 period / SBT timeout | 1 / 180 s |

Limiter/inverter enable and MOS hand mode default true; negative-price zero
defaults true and forced MIN false. Stored values survive; the authorized rollout
explicitly migrates selected controls. Test identity, network settings, credentials
and pins must not be copied into production. The price-input default is explicitly
profile-specific as described below.

## Native configuration layout

All explicit key hints and storage hashes are unchanged. API category paths
change for the following six settings:

| Setting | Category | Stable key |
| --- | --- | --- |
| Force Min On Negative Price | I/O | 382A70FD672A |
| Set 0 On Negative Price | I/O | 9919E46444B5 |
| MOS Hand Mode | I/O | 1321EC7734F0 |
| Inverter Enabled | I/O | EC88007B13C1 |
| RS485 Publish Period | RS485 | 3AEBE26CE0F7 |
| SBT Data Timeout | MQTT | 7D8C55B500DE |

The four I/O controls use Controller Policy card order -10. Limiter Enabled is
order 1, Operating Mode 2 (key DF90D18170DF), MAX 3. PID parameters use the separate
Automatic Household PID card. Native cards are exported under
category.cards[card].settings; readers must recurse and use the unchanged global
key with its current category. The unused Settings placement registry is not used.

This PID smooths the automatic household target from measured inverter AC plus
signed grid power within approved PV/battery limits. Its error is target minus
previous command. It is not PV forecast or storage economics. Explicit
household_follow, charge_priority, pv_follow and qualified automatic PV fallback
bypass it.

## Relative time groups

Algorithm selection is independent of transport. Both profiles select
relative_time_tiers_v1. Classification uses 144 accepted quarter-hour prices:
market/unknown prices unchanged; forecast valuation is
raw + abs(raw) * premium / 100. The fresh live tariff is a separate physical gate,
not a replacement current-slot price for ranking. Legacy comfort/deadband,
held tolerance and future price factor are inactive and hidden in this policy.

| Key hint / stable API key in Limiter | Label | Default |
| --- | --- | --- |
| cheapPct / 7483E2523305 | Cheap Time Share (%) | 10 |
| veryPct / 38A4CC72FD22 | Very Expensive Time Share (%) | 10 |
| expPct / DBDB92234F99 | Additional Expensive Time Share (%) | 20 |
| storAdv / 0464F65720FE | Storage Minimum Net Advantage (EUR/kWh) | 0.05 |

Shares are finite, nonnegative and total at most 100%. Relative numeric values
are validated before JSON mutation in both profiles. Shares describe time,
not energy or price percentages.

Equal-price blocks stay together. Positive cheap/high shares retain the bottom/top
block of a nonflat curve. Additional expensive time starts below the actual
expanded high boundary. Groups cannot overlap; cheap conflicts can reduce
available additional space, which may be empty with two price levels. Actual
counts are published because plateaus can expand groups. Flat prices are neutral.
Allocation weights the partly elapsed first quarter by its remaining seconds.

## Energy, priorities and acquisition economics

Very-expensive demand has first claim on measured inventory. Additional-expensive
can use only inventory above remaining nominal high-tier MAX demand. Each tier
spreads its budget over remaining duration. Cheap/neutral have no economic
battery discharge. Forecast PV never lowers today's measured-inventory reservation
for a future higher tier.

Storage enters only at chronological arrival, within charging power, headroom,
reserves and permissions. A charging slot never also discharges. Actual measured
BMS energy/power enter the real ledger unchanged, without another charge loss.
The relative model fixes charging/discharge at 90/90%:
100 Wh PV offer -> 90 Wh stored battery DC -> 81 Wh later AC. ChargePlan.w is
battery-side DC after charge loss. Direct PV subtracts diverted input
(stored battery DC / 0.90) before applying its direct-output factor 0.90.

New discretionary storage requires:

    minimumTargetTierAssessedPrice - storageIntervalAssessedPrice / 0.90 >= storageMinimumNetAdvantageEurKwh

Both sides use the same accepted origin-assessed basis. The divisor represents
direct-route 0.90 divided by cycle efficiency 0.81, without billing the common
direct loss twice. The minimum remaining positive-raw target price is a
conservative acquisition hurdle, not a guarantee of realized profit or complete
future discharge. Only strictly higher-tier, unfunded demand with possible
battery acceptance counts. Direct PV already covering every target output
creates no battery demand.

The adjustable 0.05 EUR/kWh combines the earlier 0.02 wear and 0.03 minimum-gain
allowances per later delivered AC kWh. It does not claim the owner's exact cell
replacement cost and never defers already-held energy. Physical reserve
restoration and otherwise curtailed overflow are separate needs. Zero/negative
raw-price hard gates remain authoritative.

Cheap sunlight uses the existing threshold/share behavior, also subject to this
acquisition hurdle. The actual current cheap-window full latch survives replans
until SOC falls below threshold, ends with its cheap window, and never persists
a simulated future full SOC. Explicit manual modes retain their existing behavior.

Source qualification, faults, MOS feedback, reserves and corrected MAX remain
authoritative. PV fallback grants no battery permission; the application sends no
automatic MOS commands. Different measured ledgers can yield different budgets
at equal SOC. This bounded conservative allocator does not claim global optimality.

## Input wire and independent lifecycle

The normal application explicitly accepts V2 on SolarLimiter/Input/PriceSchedule.
Shadow accepts it only on SolarLimiterTest/Input/PriceSchedule. The generic V1
receiver remains available to explicit legacy callers; the old shared HA V1
publisher/topic is unchanged.

    {"v":2,"r":1792454400,"t":1792454400,"d":900,"n":2,"k":0,"p":[-200000,500000],"o":[2,1]}

Prices remain integer micro-EUR/kWh; origins are 0 unknown, 1 market, 2 forecast.
There are at most 24 slots/chunk and 288 slots/12 chunks, identical r/t/n and a
30-second assembly lifetime. Malformed/conflicting chunks cannot activate a
partial revision. Origin-only changes invalidate semantic content identity.

Fresh n:0,k:0,p:[],o:[] withdraws only that input and does not replenish measured
energy. Accepted V2 expires after 180 seconds by generation and receipt;
duplicates do not renew it. Independent HA producers heartbeat each minute.
Test withdrawal cannot withdraw production.

Missing/stale V2 revokes automatic budgets and automatic output. Explicit
production manual modes retain their existing price-input independence. Shadow
retains its mandatory feed guard for every mode. Generic V1 lifetime is unchanged.
BMS grace retains its original deadline while both tariff sources are fresh and
positive; qualified PV-only fallback follows expiry.

## Isolation and publication

Build normal with pio run -e usb or pio run -e ota; isolated with
pio run -e planner_shadow. Shadow requires SIL_MOCK_TRANSPORT=1, no relay outputs,
broker 192.168.2.3:1883, root SolarLimiterTest and unique client
SolarLimiterTest-<12 uppercase MAC digits>. Exact original read-topic whitelist
and pre-mutation routing guards remain. Invalid inherited routing disables MQTT;
default reset is unavailable in shadow. Normal keeps configured production
routing and physical transport.

App publications share at most one accepted frame per 250 ms. Lazy interval
no-ops consume no slot; deferred plans remain pending without renewing their
generation/TTL. Mode state/status retain partial progress, reset after reconnect.
Extra-topic rotation advances only after acceptance to prevent starvation at
short intervals. CM telemetry, configured intervals and physical control/TX
cadence are unchanged.

Planning/DischargeBudget, ChargePlan, State, Config, Assessment and
Diagnostics/Runtime use the configured normal root or guarded shadow root.
Config/Assessment report allocationPolicy, priceBasis, legacyPriceControlsActive,
time shares, storageMinimumNetAdvantageEurKwh, chargeEfficiencyPct,
dischargeEfficiencyPct, source freshness and profile flags. Config priceInputVersion
and accepted Assessment inputVersion are 2. Assessment also reports tiersValid,
currentPriceTier, tierStart/tierSlots, actual counts and assessed boundaries;
empty groups have null boundaries. Bind revision and coverage before rendering.

ChargePlan v1 remains DC: w is storage intent; s[0] is measured MPPT override,
later s slots are qualified forecast inputs. It is neither a pure original-PV
forecast nor a future AC inverter command. HA integrates original shared PV and
actual production/test storage intentions separately.

Optional planner_shadow_trace enables SIL_SHADOW_STAGE_TRACE=1 for numeric
failure-stage/RX-callback deltas. Traces remain shadow-only. Earlier network
backpressure/overflow/timeouts remain known limitations; source review, pacing
and build success do not establish that transport stalls are solved. No CM or
dependency upgrades are included. Persistence, actual JSON/UI, the 15-minute test
and production runtime acceptance remain distinct rollout gates.
