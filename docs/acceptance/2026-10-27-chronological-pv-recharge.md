# Chronological PV recharge and input-update continuity

Final deployed firmware: **5.4.9**, with unchanged CM **4.10.3**, Live disabled and Settings enabled. Work continues on `fix/mqtt-session-recovery`; prior dirty changes were preserved. No commit, push, merge, settings/schema migration or SBT change was performed for this task.

## Resulting behavior

PV arriving after a morning discharge can refill the battery before a later expensive discharge. Live battery allowance, deliberate charge target and both published schedules share one bounded chronological AC/DC balance. Direct PV, physical headroom, qualified charge power, existing price economics, reserve debt and conversion losses are accounted for once. Forecast energy never enters the measured battery ledger. Current Cheap hysteresis is read from the actual controller before planning; all live safety and output guards retain authority.

During temporary source requalification, changed current prices or accepted forecast content request a fresh capture without deleting a still-retainable display snapshot. Its original `g`, `r`, `pv`, `t`, arrays and 180-second deadline remain unchanged. Actual settings, configured source/output topics, known faults, validity/clock failures and loss of current time coverage remain hard invalidations. Configured forecast topic changes invalidate both publications before receiver clear/resubscribe. A held diagnostic is never used as a control input.

Changed product files: `src/control/FuturePrices.h`, `ChargePlan.h`, `CurrentControl.h`, `PlanCaptureContext.h`, `DischargeBudget.h`, `src/main.cpp` and `platformio.ini`. Changed tests: `test/host/future_prices_test.cpp`, `charge_plan_test.cpp` and `plan_retention_test.cpp`. Existing planning/MQTT-contract documentation and `readme.md` were synchronized; this acceptance report records the rollout. Unrelated working-tree changes were retained.

## Targeted deterministic verification

- Four affected 5.4.8 host runners passed: FuturePrices, coupled ChargePlan, CurrentControl including application, and PlanRetention.
- Real captured wire packets, common revision 1793119200, were replayed through old and new planners with identical SOC 97%, full capacity 131.25 Ah, current price 0.4242, factor 0.95 and MAX 900 W initialization. This comparison does not reconstruct historical live-ledger debits.
- Battery discharge allowance: **5765.75 -> 7511.33 Wh**, a **1745.58 Wh** increase. Planned retained PV: 1941.50 Wh DC. Every time-prefix stayed within energy/headroom bounds, with no simultaneous net charging/discharging or credit before arrival.
- Regression RED/GREEN includes weak later PV, PV arriving too late, reserve debt plus later demand, sufficiently strong later cheap PV, reserve debt larger than usable capacity, actual Cheap latch, partial first intervals, MAX 500/900/1200 W, unknown night slots, zero charge permission, source guards and serialization.
- Final 5.4.9 retention runner independently passed real accepted price/PV changes during a source gap, original payload-byte retention, recovery capture, original 180-second expiry, hard settings/fault/coverage changes and configured source switches with new data already accepted before the next control tick.
- Fixed planner workspace grew by 3760 bytes in the host check; bounded backward rank visits 20736, linear forward work, no per-step heap allocation or large planner stack copy. Maximum tested charge payload 1918 bytes fits the 2048-byte packet budget.

## Builds and frozen artifacts

Both `pio run -e usb -e testesp` profiles passed for both versions; the final 5.4.9 build was repeated after the source-switch fence. The affected host runner and focused diff check passed afterwards. PlatformIO test discovery reports zero configured device cases; the explicit C++ host runners provide the relevant core tests. Hosted CI was not run because no commit was published. Frozen images contain no recovery injection; `testesp` has compile-time mock RS485 and isolated MQTT outputs.

| Version | Profile | Bytes | SHA256 |
| --- | --- | ---: | --- |
| 5.4.8 | usb.bin | 1188352 | `e68933e865aa73b726c52ae5842e812c36e905e4d8db57417320f8949335e6f8` |
| 5.4.8 | testesp.bin | 1187696 | `006f5f2a574c83e3f1bed6b117ffacaec5676800b24c2c12ddc2001bbb5b2fb4` |
| 5.4.9 | usb.bin | 1188608 | `f95345dcdb7f025285adb3713ca51928a6823fb4ebee65b81b63173257f1b2c3` |
| 5.4.9 | testesp.bin | 1187856 | `72ce009554e93568ef9dcb14a4d4a286aa394e4c0fcc8a6e435667736bf3997c` |

Code, ELF files, source archives and CM provenance are retained in the task-local artifact manifests. CM archive SHA256 is `495cc64a52311567586fc9a14951aa67a72185d4fbcbc19bf41cc43a7c5baa4a`. The PV mathematics and CurrentControl header are unchanged between 5.4.8 and 5.4.9.

## COM8 and rollout

- Both versions completed a 600-second serial/MQTT hardware capture on the verified isolated test ESP. 5.4.8 had 474.328 seconds after complete qualification; 5.4.9 had 283.637 seconds. The latter exceeds one full 180-second retention lifetime, while deterministic tests cover exact expiry and changing-content gaps. Both stayed within the owner's 15-minute pre-production bound.
- Both COM8 gates passed identity, installed version, expected reset reason, control/uptime progress, heap margin, complete qualified plans and matching bounded red/blue flows. All 75 readback settings remained unchanged. Cold source acquisition is explicitly excluded from the qualified window; no physical RS485 output is claimed from the mock profile.
- 5.4.8 OTA accepted **2026-10-27T18:51:09.651122+00:00**. Its first 900-second production window had no reboot but reproduced one invalid red and three invalid blue publications during source requalification plus changing input content. The old frame was only 82 seconds old. That evidence motivated the narrow 5.4.9 correction rather than being reported as uninterrupted-plan acceptance.
- 5.4.9 OTA accepted **2026-10-27T19:44:19.941032+00:00**. Exact device identity, installed 5.4.9, OTA inactive, Live0/Settings1 and all 75 unchanged settings were verified. The COM8 observer/reset was started immediately after acceptance for comparable uptime evidence. Masked secrets compare as returned; the uploader never writes settings.

## Final fixed production observation

Window: **2026-10-27T19:45:02.974003+00:00 to 2026-10-27T20:00:02.974003+00:00**, 900 seconds after the first fresh, matching complete plan generation. Polls never restart this timer.

- Runtime records: 259; boot IDs: [9]; expected OTA reset reason 3 only. Control loops 3576 -> 79046; maximum observed control age 1981 ms.
- ESP uptime 65073 -> 871150 ms; minimum free heap 119024 bytes; minimum largest allocatable block 69620 bytes.
- Red publications: 15, invalid/incomplete 0. Blue publications: 13, invalid/incomplete 0.
- Matching complete bounded-flow generations: 12. Future PV charging is present. All final functional checks passed.
- Transport counter deltas: `{"connectFailures": 8, "rxMalformed": 0, "rxOversize": 0, "rxTimeouts": 0, "txFailures": 2, "txOverflows": 0, "txTimeouts": 2}`. Transport-clean flag: **False**; this is reported separately from functional plan continuity.

## Limits and evidence

This is bounded rollout evidence, not a long-term crash-free claim. Actual next-day PV charging was not physically observed at night. Known MQTT reconnects remain an open diagnosis: bounded broker logs show same-IP reconnections replacing still-open sessions and timeouts on other clients; they do not prove the original cause or a second owner of the client ID. HA desktop/mobile rendering after the PV deployment showed the single thin-line36-hour chart and the new blue future charging area; its endpoint legend can show N/A while future curve data is present. No dashboard configuration was changed here.

An additional read-only tail check through 20:05:05 UTC, including the next quarter-hour input update, found 13 further plan records and no invalid or incomplete plan after the fixed observation window.

Local task evidence: `sil-pv-recharge-20261027/` (original packets, comparison, first hardware/OTA/window, UI/broker evidence) and `sil-plan-hold-20261027/` (causal reproduction, final source hashes, host/build logs, COM8 gate and final production window). Private configs and credentials remain outside repository artifacts. Governance read from `origin/main`: `.github/AGENTS.md` and the control-plane, refactor and docs roles. No changelog exists in this checkout.
