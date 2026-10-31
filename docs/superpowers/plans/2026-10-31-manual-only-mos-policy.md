# Conditional manual-only MOS policy

Status: investigation and removal plan only. Not implemented or deployed.

## Evidence and prerequisite

The installed BMS reports `15660030-SP15S020-P14S-60A-60AH`, firmware 2.4, 14 series cells. The 2026-10-29 event proves autonomous discharge cutoff on a cell-undervoltage alarm before SIL's E1 request. The SP15S020-UART model-family page documents independent voltage, current, short-circuit and temperature protection inside the BMS. Its use of the word "software" describes the BMS microcontroller, not SIL or SBT.

The available owner screenshot ends immediately after the Protections heading. The 4150/3000 mV entries above it are capacity parameters, not verified protection thresholds. Model-family support does not prove the installed trigger/release/delay values or sensor enablement.

Before removing the application MOS policy, capture the installed app's read-only Protections, Hardware Protection, NTC and Load Detect settings. Record charge/discharge over/undertemperature triggers, releases and delays, voltage/current protections and enabled sensor mapping. Do not induce a dangerous physical fault as an acceptance test. No threshold or chemistry is inferred from a product-family example.

Primary references:

- [SP15S020-UART model family](https://www.lithiumbatterypcb.com/product/12s-13s-14s-15s-36v-48v-58-8v-smart-bluetooth-bms-with-20a-30a-40a-to-60a-constant-discharge-current-with-uart-and-rs485-double-communication/)
- [JBD protocol](https://github.com/FurTrader/OverkillSolarBMS/blob/master/Comm_Protocol_Documentation/JBD%20Protocol%20English%20version.pdf)
- [JBD family manual](https://github.com/FurTrader/OverkillSolarBMS/blob/master/Overkill_Solar_BMS_Instruction_Manual.pdf)

## Removal tasks after verification

1. Remove SIL's automatic emergency eligibility, request construction and publication. Any BMS alarm, source loss, SOC change, mode change, reconnect or startup must produce no MOS command.
2. Remove the SBT automatic-emergency command vocabulary and admission path. Keep explicit manual HA charge/discharge/joint requests and the common operator gate. Preserve strict envelopes, expiry/replay checks, fresh pre-read, uncertain-write cancellation and OTA/peer-change fences.
3. Preserve all native BMS protection configuration and all alarm/temperature/MOS telemetry. Inverter power restrictions and source qualification remain separate from MOS writes. Do not remove transport safety fences as if they were battery protection.
4. Add no automatic MOS re-enable, retry loop, fault counter or recovery supervisor. The owner's recovery proposal is deferred.
5. Audit the HA command examples/automations for startup or state-change replay. Keep manual controls non-optimistic and non-retained; feedback is observation, not a command.
6. A pre-existing software MOS lock is not erased by deleting the sender. Any one-time release must be an explicit operator action after checking native protection status.

## Verification gates

- Every hardware alarm bit, software-lock bit, stale source, reconnect and boot produces zero automatic command publications in a real receiver/service test.
- Manual requests still work through the existing guarded transaction; disabling the operator gate fences queued writes.
- A manual enable request never modifies native thresholds or clears native alarm registers. Native BMS protection remains authoritative over physical conduction.
- USB/OTA builds and isolated transport tests pass. Production upload remains separately authorized.
- If installed hardware protection cannot be verified or is absent, stop this removal plan and design a complete protection architecture instead of piecemeal exception logic.
