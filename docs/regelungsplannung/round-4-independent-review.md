# SIL Control Planning - Independent Round 4 Review

Historical Round 4 snapshot. See [Round 5 decisions and defaults](round-5-decisions-and-defaults.md) for later resolutions and current policy.

Date: 2026-10-17. Scope: all twelve Round 4 Markdown documents in this folder, read as draft requirements after the English translation and fidelity corrections. Branch: `feature/mos-acceptance-notes`. This review changes no decision, firmware, setting, or device state. Findings below are next-round decisions, not authorization to implement.

The accepted MAX fallback, manual-control availability, alarm-plus-fan escalation, and separation of fault control from economics are preserved. Explicitly deferred hardware capabilities are not classified as defects. Line references describe the reviewed draft snapshot.

## 1. High - Intersect protection with all applicable limits instead of replacing the normal path

**Scenario:** A charge warning exists while grid/AC data is unavailable and BMS data remains healthy. The main flow enters protection before reaching the accepted 0 W fallback. The protection subprogram may increase inverter use. Separately, a hard discharge block with MOS automation unavailable reaches only an alarm/fan result: no explicit inverter reduction is specified unless inverter OFF or the negative-price stop also applies. A simultaneous charge warning and discharge block has no combined action rule.

**Evidence:** `SIL - Ablaufdiagramm für die gemeinsame Diskussion.md:22-35,50-56`; `04-schutzreaktion.md:18-35,38`; `03-schutzpruefung.md:20-31`.

**Next-round decision:** Define one arbitration table that intersects directional protection, measurement fallback, OFF/negative-price requirements, and device limits. Decide the explicit inverter request for every hard discharge block, including a denied or failed MOS action. A charge-mitigation increase must not defeat an applicable discharge restriction or the accepted missing-grid zero request. This is a blocker before protection firmware, not a request to replace the accepted fallback policy.

## 2. High - Specify manual/automatic ownership and fault recovery per MOS direction

**Scenario:** Automation turns charge MOS off for a fault; the user turns it on manually while the fault remains active. Both keeping manual controls usable and keeping automation enabled are accepted, but neither defines the resulting state. Conversely, a user turns a MOS off during the fault, and subsequent automatic recovery could undo that deliberate action.

**Evidence:** `04-schutzreaktion.md:29-38`; `round-4-decisions.md:14,28`; `08-senden-ruecklesen.md:18-24,37`.

**Next-round decision:** Choose whether a conflicting manual ON is rejected with a reason, accepted as an explicitly qualified override, or immediately subject to a new protective decision. Keep the controls available and do not silently disable automation. Define restoration ownership separately for charge and discharge: which automatic change may be reversed, and which later manual action invalidates that permission. Define the whole-control gate's effect on these cases. This acknowledged open choice blocks automatic MOS implementation.

## 3. High - Give retained faults and observation trust an explicit reboot contract

**Scenario:** A confirmed protection fault is followed by lost BMS telemetry. SIL then restarts. Reading actual MOS state is unavailable, and RAM-held fault history may be gone. The draft can consequently reach the accepted BMS-only-loss MAX branch even though the fault was previously known. Reading an old retained MQTT report also must not establish recovery merely because SIL has just received it.

**Evidence:** `01-messwerte.md:23-30`; `03-schutzpruefung.md:31`; `09-einstellungen-callback.md:18-20,32`; `SIL - Ablaufdiagramm für die gemeinsame Diskussion.md:32-33`; `round-4-decisions.md:9,28`.

**Next-round decision:** State whether fault retention survives reboot and, if so, how fault identity and release evidence survive it. Define boot-time unknown state, source/session identity, and the evidence required to distinguish a new observation from a replay. Preserve five stable seconds plus two new samples; do not interpret that as permission to clear an underlying fault. The accepted MAX policy needs a deliberate boundary for unknown-at-boot versus previously known fault, rather than an accidental RAM-reset exception.

## 4. High - Define command replacement and mismatch behavior before fault-driven writes

**Scenario:** An ON request is pending when a new fault requires OFF. The pending-feedback loop can continue waiting for the old target, while a late acknowledgement or old matching MOS state could be attributed to the newer request. A persistent fault evaluated each cycle could also cause repeated sends unless pending and failed states suppress them explicitly. 'No blind MOS retry' alone does not define this lifecycle.

**Evidence:** `08-senden-ruecklesen.md:18-37`; `04-schutzreaktion.md:30-35`; `round-4-decisions.md:27`.

**Next-round decision:** Specify one active request per affected actuator, target revision/correlation, supersession by an urgent protection demand, and treatment of late feedback. Define what the controller requests and reports when acknowledgement succeeds but observed state or effect does not. Select bounded expiry and authorized retry behavior after protocol qualification. Keep the tentative one-second feedback tick separate from urgent fault handling. This blocks MOS command execution, not the rest of read-only planning.

## 5. Medium - Carry incomplete-plan status to an explicit main-flow decision

**Scenario:** Prices or usable-energy inputs are insufficient at startup or after a plan expires. The planner returns incomplete, and the budget subprogram returns incomplete, but the main flow still proceeds to PID and setpoint generation without a defined ceiling. At a 15-minute price boundary between 30-minute replans, the treatment of a now-invalid plan is likewise unspecified.

**Evidence:** `05-energieplanung.md:18-28,31`; `06-energiebudget.md:18-29`; `SIL - Ablaufdiagramm für die gemeinsame Diskussion.md:36-42`; `round-4-decisions.md:16-18,24`.

**Next-round decision:** Add an explicit status branch before PID. Decide separately: valid current price with incomplete future prices, absent current price, absent PV forecast, and invalid energy estimate. Define whether an old plan may remain usable and its expiry conditions. Then settle comfort classification, boundary equality, transition hysteresis, and the future hold offset. No ordinary-self-consumption fallback is assumed. This blocks the economic control path until an owner-approved result exists.

## 6. Medium - Define an energy ledger and its reference side before using carry-forward budgets

**Scenario:** A window is assigned energy, partly used, and replanned after 30 minutes. If unused allocation is carried forward while fresh SOC is also converted into a new available-energy pool, the same energy can be allocated twice. PV supplying the inverter must not consume the battery allocation, and DC energy must not be treated as AC energy before applying conversion efficiency.

**Evidence:** `05-energieplanung.md:20-27,31`; `06-energiebudget.md:12,20-34`; `round-4-decisions.md:21-24`.

**Next-round decision:** Define a single accounting invariant: remaining allocations cannot exceed currently usable energy on the chosen AC or DC reference side. Specify which measured energy debits a window, how fresh SOC reconciles that ledger, and how replanning replaces or transfers allocations without duplication. Bound the result by actual reserve and device limits. Keep 650 W as the accepted planning load; it does not answer this accounting question. This blocks a trustworthy economic budget, not basic telemetry.

## 7. Medium - Turn callback reuse into a qualified protection contract

**Scenario:** An analog signal fluctuates around a threshold, becomes invalid, or recovers while a fault is latched. A callback indicating alarm disappearance is insufficient evidence for a physical release unless hysteresis, invalid-sample treatment, and the five-second recovery rule are composed explicitly. A fan request and HA app popup provide no bounded physical charge interruption; HA delivery may be unavailable.

**Evidence:** `02-luefter.md:18-30`; `03-schutzpruefung.md:31`; `01-messwerte.md:30`; `round-4-decisions.md:11,13,25,33`. Targeted sibling evidence: `ConfigurationsManager/src/alarm/AlarmManager.h:45-57` exposes thresholds and a getter/source, but no hysteresis field in that public configuration; `:64-68` exposes callbacks. `SolarInverterLimiter/platformio.ini:44,79` pins CM commit `554869da7efc98e0c1874e74bf0c78d4bcd1664a`; availability and implementation semantics at that commit were not verified.

**Next-round decision:** Preserve CM reuse, then qualify its pinned behavior and define which layer owns threshold hysteresis, latching, invalid samples, release, and event timing. Decide protection versus maintenance-fan-OFF priority. Distinguish a local active fault, attempted HA notification, and confirmed actuator effect; never wait for HA delivery before a permitted local response. No worst-case protection latency or guaranteed charge cutoff can currently be claimed. This is a qualification gate before claiming operational protection, not a proposal for a new charger actuator.

## Decisions that remain legitimate open work

The 1% reserve question is not answered by an assumed internal BMS reserve. Neither 1% nor the earlier 5% plus 3 percentage-point hysteresis is approved; no actual battery protection thresholds are approved. These need battery-specific evidence and an explicit decision before reserve/protection operation. The proposed PV contribution calculation likewise needs time-aligned measured power, loss handling, and a defined response when reserve is reached during a PV drop. No unused PV headroom or PV-only path is established here.

The tentative 36-hour look-ahead, economic constants, one-second pending tick, and optional settings can remain tentative while the above contracts are resolved. No firmware build or hardware test was appropriate for this Markdown-only review.
