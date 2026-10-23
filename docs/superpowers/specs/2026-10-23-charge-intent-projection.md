# Charge intent projection

## Owner intent

Provide a coarse visual explanation of when SIL deliberately withholds PV for charging, so a small or zero inverter request is understandable. Blue means charging intention, red keeps the existing AC battery-discharge allowance, and a separate line shows forecast DC PV. This is not a new optimizer or exact future inverter commands. Implementation and isolated COM8 testing are authorized; production remains 5.3.0. The requested local checkpoint is `583557e`.

## Model and boundaries

- Preserve controller decisions, settings/NVS, MOS/safety paths and existing discharge MQTT format.
- Project the CURRENT economic request at its existing `chargeTargetW` until `targetAt`, capped by the remaining `shortfallWh / kDischargeEfficiency` once. Do not subtract its already credited PV again, extend the deadline or invent another economic target.
- Reserve intent uses estimated SOC and chronological accepted tariff bands: full PV at/below OFF when not expensive, half PV between OFF/ON when cheap. Combine reserve/economics using max, never addition.
- Cap blue by known PV, qualified MPPT power and headroom. Anchor the first interval to the actual CurrentDecision. The local coarse SOC estimate advances from projected charging and existing measured-energy discharge allowance; do not charge and discharge it simultaneously. No feedback to the real controller/energy state.
- Blue excludes natural surplus/offset trickle. It is an intent projection, not a complete physical energy forecast. MIN, PID, offset, future measurements and safety can alter actual inverter output.
- Confirmed charge-direction blocking gives zero blue. Missing source qualification invalidates the projection. Unknown future PV is null, not zero; after an unknown SOC transition later reserve intent remains unknown until replanning. Known solar intervals may remain visible independently.
- Discharge MOS OFF must not erase red intent. Explicitly label red AC W versus blue/PV DC W.

## Export and UI

New non-retained `<base>/Planning/ChargePlan` v1 at the existing 60-second diagnostic cadence and on reconnect/topic changes. Keys: `v`, `valid`, `reason`, `kind:"intent_projection"`, `g`, `r`, `pv`, `t`, `d:900`, `n`, `target`, `unmetWh`, `w` (withheld DC W), `s` (forecast DC W). Arrays have n entries, at most144, integer0..12000 or null. Invalid means n0/empty arrays. Fixed2048-byte serializer and existing2304-byte MQTT buffer must fail closed on overflow; no heap JSON construction.

Prepare examples and an isolated HA preview sourced only from `SILTest/COM7`. Do not change production Home/discovery or existing history. Use red discharge, blue withholding, a distinct solar line, one time axis, null gaps and explicit projection labels.

## Acceptance

Targeted RED/GREEN host tests; published CM4.9.0, Live off/Settings on, version5.4.0; report flash/RAM/object/packet costs. USB only the separate mock fixture to COM8 MAC `3C:8A:1F:A7:22:7C`, preserving NVS and isolated broker/bridge `192.168.2.220:18884`. Observe at least300s, natural feed, bounded Settings reads and heap/stack/loop/reset evidence. No production OTA/control topics, final commit or push. Preserve the earlier reset/HTTP issue classification and the owner's70-percent usage stop condition.
