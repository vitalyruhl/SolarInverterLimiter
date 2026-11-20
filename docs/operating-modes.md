# Explicit operating modes

The persisted default is `automatic`. Existing reserve, price, controller and
transport settings remain unchanged. SIL does not publish BMS MOS commands in any
mode. Manual MOS control remains with SBT and the BMS retains its own protection.

With `Limiter Enabled` on, the modes operate as follows:

| Mode | Behavior |
| --- | --- |
| `automatic` | Economic controller, PID, forecasts and output correction. Battery grace and automatic PV fallback require commissioned installation limits. Their default ceiling is zero. |
| `household_follow` | `clamp(measured inverter AC + signed grid power + correction, 0, MAX)`, with correction applied once before clamping. Explicit selection permits missing BMS feedback and reports `manual_bms_unverified`. |
| `charge_priority` | Holds output below the configured charge threshold (default 80% since 5.8.1, previously 95%). Between the threshold and 100%, the default inverter share is 10% (previously 0%). At 100%, household following latches until SOC drops below the threshold. |
| `pv_follow` | Uses only fresh measured PV: `min(charger DC W * 0.90, clamp(measured household W + correction, 0, MAX))`. Excess PV remains available for battery charging; night-time zero PV produces zero output. |

With valid planning inputs, economic planning continues in every mode. The
additive `Planning/State` fields `autoStrategy`, `autoLimitW` and `autoReason`
describe that economic recommendation independently of the selected mode.
`autoLimitKind=battery_ac` identifies `autoLimitW` as the planned AC battery
discharge limit, not a complete inverter or PID target; unavailable plans
report a null strategy and limit. `configuredMode`, `manualOverride`,
`effectiveStrategy` and `commandedW` distinguish the selected mode and current
output path. `manualOverride` identifies a selected manual mode even when a
separate stop or limiter-disabled path takes precedence. `commandedW` is a
requested value and does not confirm delivery or physical output.

`diagnosticPlansAvailable` reports whether both diagnostic snapshots exist;
`diagnosticPlansRetained` reports whether either snapshot is being kept after
its inputs or context became unsuitable. `diagnosticRetentionAgeS` is the age
of the longest current retention period, or null when neither is retained. The
original plan timestamps remain unchanged. On MQTT reconnect the ESP sends
the stored graphs once without broker retain. Consumers match their original
generation times against `dischargeG` and `chargeG` in `Planning/State`.

The `economic` object describes the relative planner's authoritative plan.
`available`, `valid`, `stale` and `retained` report existence, economic
eligibility, age classification and retention through an input outage.
`g` is the original generation time in UTC seconds. `ageS` is its age at
the last control tick. Both are null when no economic plan exists.
`applied` reports a qualified automatic slot decision used for the local
command, including a planned hold. Overrides, recovery, local write errors
and protection that blocks a requested battery action clear it. The shadow
profile evaluates the same decision with its mock transport. Use
`transportWriteStatus` for the separate local write result and measured
power to observe the inverter response.

## Limiter disabled

When `Limiter Enabled` is off, SIL requests the configured MAX on every
scheduled RS485 control tick in all four modes. Household PID, output
correction, prices, forecasts and missing MQTT measurements do not limit this
request. This setting explicitly permits grid export; it does not establish
physical battery isolation.

The inverter enable and RS485 enable settings, known genuine discharge faults
and confirmed charger faults retain their stop behavior. A confirmed charger
fault remains a stop when its telemetry becomes unavailable until a fresh clear
is received. An invalid MAX outside the 16-bit command range requests zero;
the actual inverter's safe power range must still be configured separately.
The selected operating mode is unchanged.

`Planning/State.transportWriteStatus` and the serial diagnostic `rs485_tx`
report `unknown`, `mock`, `disabled`, `accepted` or `write_failed`. `accepted`
means that the local serial interface accepted all eight bytes, not that the
inverter received or applied the request. MQTT transport error counters are
separate from this RS485 status.

With the automatic limiter enabled, a fresh, qualified 100% battery permits the
household PID to request the measured household demand up to MAX. A full battery
can curtail MPPT output until a load appears, so the present charger output does
not cap this request. This grants no charging reservation and does not force MIN
above household demand. Normal economic planning resumes below 100%; a source
gap cannot preserve an old full-battery exception. Existing discharge, charger,
reserve, enable and negative-price gates remain authoritative. The physical
battery may briefly supply power while the charger responds or until SOC falls
below 100%. The explicit Limiter-OFF maximum-output bypass is unchanged.

With the limiter enabled, fresh grid and actual inverter measurements, enabled inverter, enabled transport
and valid settings are required. Configured MIN and negative-price overrides do
not lift a manual-mode STOP. Genuine discharge protection and charger faults
stop every mode. Household following and charge priority also retain fresh
connected-battery discharge permission and the existing reserve hysteresis.
Fresh healthy physical discharge MOS OFF permits household PV demand without a measured-PV cap; the
physical battery isolation is the boundary. Missing BMS feedback never establishes
that physical isolation. A remembered genuine discharge restriction still stops
manual following.

In automatic mode, a battery telemetry outage can carry a previously approved
discharge only within the original source deadline and a conservative energy
budget. The calculation starts at the source sample time and deducts
worst-case discharge and other battery loads. It also reserves energy for the
next control interval. The previous budget, command and current plan limit
still cap output. Commissioning requires lower bounds for capacity, voltage
and efficiency, upper bounds for discharge and other loads, SOC and energy
margins, and an upper bound on the control interval.

When BMS feedback is missing or the relative plan is unavailable, the
`pv_limited_fallback` strategy requires fresh Victron and AC measurements,
qualified protection inputs and a commissioned AC fraction of measured charger
power. It caps output at corrected household demand and the inverter maximum.
The default firmware has no commissioned outage limits and requests zero from
these strategies. Normal automatic operation requalifies its sources after a
fallback.

The explicit PV follower uses no stored-battery discharge budget or forecast
credit. Its measured-PV cap permits solar use below the reserve threshold or
without ordinary battery discharge permission, while keeping genuine fault
gates. Missing/stale/negative/non-finite charger power produces zero. With fresh
PV and AC meters but unverified BMS, it reports `pv_bms_unverified`; it never
claims that the battery is physically absent. Correction is applied exactly
once to household demand before the PV cap, and the final output path adds
neither MIN nor another correction/PID action. This is a bounded solar demand,
not a new MOS or hardware battery-isolation policy.

Charge priority with missing SOC holds output. A fresh confirmed unavailable
charge path permits household following, subject to the same battery limits.
An optional inverter share (0..100%) uses fresh charger output power, multiplied
by 0.90 discharge efficiency; the final request is capped by corrected household
demand. It applies only from the threshold up to full charge. Threshold values
must be finite and strictly between 0% and 100%.
Leaving charge priority clears its full-charge latch, so returning at 96% after
another mode observed 90% does not resurrect an earlier full-battery state.

## Configuration and persistence

CM exposes `opMode`, `opChargeSoc` and `opPvShare` in the Limiter settings. The
mode text must be exactly one of the enum values above. UI requests and MQTT
requests enter the same loop-owned change path. The displayed settings remain
confirmed values while a request is pending. Changing mode or its parameters
resets controller dynamics without changing `resOff` or `resOn`. A mode-only
change keeps the existing economic plan; changes to the economic threshold or
share request an updated plan through the planning context.

The three values are stored atomically in a separate `SILModes` NVS namespace,
under the seven-character `profile` key, as a 16-byte versioned/checksummed record.
No existing CM key is migrated or rewritten. A missing, truncated, corrupt or
unsupported record selects the automatic defaults without a flash write.
Legacy enum values remain `automatic=0`, `household_follow=1`, and
`charge_priority=2`; `pv_follow=3` is appended without a profile-format change.
Firmware 5.5.0 rejects the new enum as unsupported and starts with automatic
defaults (`profile_invalid`), leaving the record untouched. Select a supported
mode before downgrading when the existing charge parameters must stay active.
Requests matching confirmed values are coalesced without writes. New bytes are
written to a separate `staged` key and completely read back and validated before
an atomic write promotes them to the active `profile` key. Boot ignores `staged`.
Only the complete successful active NVS write acknowledges activation; there is
no ambiguous post-commit readback. Failed staging, readback or promotion keeps
the old live, displayed and boot mode. A changed profile needs two bounded
writes; duplicate selection needs none. No blind retry loop writes flash.

CM's `Config<T>::save()` (4.10.3 and the current 4.12.0 package) delegates to `updateSetting()`;
its synchronous setting save does not return the underlying NVS byte count.
SIL therefore uses local presentation adapters and explicit profile readback,
without changing that dependency. CM's immediate UI response acknowledges a
queued valid request; the confirmed settings and MQTT state report activation.

## MQTT and Home Assistant

All topics derive from the current SIL publish root (shown as `SolarLimiter`).

| Topic | Payload | Retained |
| --- | --- | --- |
| `SolarLimiter/Control/Mode` | Confirmed plain enum | Yes |
| `SolarLimiter/Control/Mode/set` | `{"mode":"household_follow","expires_at":1800000030}` | Never |
| `SolarLimiter/Control/ModeStatus` | JSON `configured`, `effective`, `reason`, `storage`, `charge_threshold`, `inverter_share_percent` | Yes |

A command requires a synchronized UTC clock, an expiry strictly in the future,
and at most 60 seconds remaining. HA issues 30-second commands. Invalid types,
extra/duplicate keys, malformed JSON and expired commands are rejected. Expiry
is checked again at the loop boundary. Reconnects republish confirmed state,
never a command. A root change unsubscribes the old command topic and discards
pending mode requests; configure HA's topic prefix consistently. Old retained
commands cannot revive after their expiry. The MQTT library does not expose a
retained-packet flag, so expiry is mandatory even for apparently live commands.

The HA select is a retained MQTT discovery entity
(`homeassistant/select/energy_review_20261031/solar_limiter_operating_mode/config`,
payload in [mqtt-discovery.json](home-assistant/mqtt-discovery.json)). It uses the
owner-requested German labels, `optimistic: false`, `retain: false`, and the
existing `SolarLimiter/System-Info/status` availability. Availability requires
both the online status and a valid confirmed mode enum, so the selector stays
unavailable before firmware supporting this contract is active. Commands carry
`expires_at = now + 30 s`.

## Verification and activation

Run `tools/test/run-operating-modes-tests.ps1` for portable policy, command,
transaction, profile and deferred-settings tests. Existing controller and
planning tests cover the unchanged automatic engine. `pio run -e usb` and
`pio run -e ota` build without uploading. Live UI remains disabled.

Firmware activation, settings persistence on the actual ESP32, HA confirmation,
real protection configuration, safe battery isolation and multi-day acceptance
remain separate owner-operated verification. No new recovery supervisor or
conditional MOS-policy removal is included.

## Battery energy calibration boundary

The owner-provided label identifies a 50.4 V nominal, 114 Ah, 14S40P pack
(560 LG M29 lithium-ion cells, not LFP), with a 60 A BMS and a stated 30 mOhm
internal resistance. The `11.19` label likely denotes November 2019; that date
interpretation remains unverified. Fourteen reported cell voltages represent
the fourteen series groups, each containing forty cells in parallel.

The existing planner's 51.38 V energy basis and the observed BMS-reported
131.25 Ah capacity imply approximately 6.744 kWh, compared with 5.746 kWh
from the rated 50.4 V and 114 Ah. These are different bases, not interchangeable
capacity measurements. This change documents the discrepancy only: voltage,
capacity, SOC and reserve defaults are unchanged. Future energy calibration
requires separately verified battery/BMS data and explicit authorization.
