# Explicit operating modes (5.6.0)

The persisted default is `automatic`. Existing reserve, price, controller and
transport settings remain unchanged. Mode selection does not write MOS registers
or remove the existing automatic emergency-OFF policy.

| Mode | Behavior |
| --- | --- |
| `automatic` | Existing economic controller, PID, forecasts and output correction. Missing BMS feedback holds output rather than identifying a disconnected battery. |
| `household_follow` | `clamp(measured inverter AC + signed grid power + correction, 0, MAX)`, with correction applied once before clamping. Explicit selection permits missing BMS feedback and reports `manual_bms_unverified`. |
| `charge_priority` | Holds output below the configured charge threshold (default 95%). Between the threshold and 100%, default inverter share is 0%. At 100%, household following latches until SOC drops below the threshold. |
| `pv_follow` | Uses only fresh measured PV: `min(charger DC W * 0.90, clamp(measured household W + correction, 0, MAX))`. Excess PV remains available for battery charging; night-time zero PV produces zero output. |

Fresh grid and actual inverter measurements, enabled inverter, enabled transport
and valid settings are required. Configured MIN and negative-price overrides do
not lift a manual-mode STOP. Genuine discharge protection and charger faults
stop every mode. Household following and charge priority also retain fresh
connected-battery discharge permission and the existing reserve hysteresis.
Fresh healthy physical discharge MOS OFF permits household PV demand without a measured-PV cap; the
physical battery isolation is the boundary. Missing BMS feedback never establishes
that physical isolation. A remembered genuine discharge restriction still stops
manual following.

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
resets controller dynamics and requests fresh plan capture without changing
`resOff` or `resOn`.

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

The frozen CM 4.10.3 API's `Config<T>::save()` delegates to `updateSetting()`;
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

[The additive HA select example](home-assistant-operating-modes.yaml) uses the
owner-requested German labels, `optimistic: false`, `retain: false`, and the
existing `SolarLimiter/System-Info/status` availability. Availability requires
both the online status and a valid confirmed mode enum, so the selector stays
unavailable before firmware supporting this contract is active. Apply it by adding the entity to the
existing MQTT list, preserving other HA entities.

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
