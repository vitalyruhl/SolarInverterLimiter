# MPPT standby and independent source capabilities

## Scope

SIL 5.4.10 corrects an economic hold being replaced by a 500 W idle fallback when the battery-powered MPPT disappears with actual discharge MOS OFF. It also keeps battery planning available from qualified BMS data independently of MPPT telemetry, and preserves qualified direct-PV use when BMS telemetry is missing. CM remains 4.10.3. Physical MOS permissions, settings schema and SBT firmware are unchanged.

## Behavior

- Fresh actual discharge MOS OFF with an unavailable MPPT is contextual `standby_discharge_mos_off`, not a fabricated current voltage/current sample. Current output is zero in this standby, including with Limiter OFF.
- With healthy BMS discharge intent and an unavailable MPPT, advancing BMS samples qualify battery-only control. Normal future-price/current-price decisions continue; unknown charger power cannot supply direct PV. The fixed idle-output fallback no longer replaces the calculated battery allowance.
- With missing BMS data, qualified MPPT power and AC meters can supply the household through PV-only control. No battery-energy permission is invented.
- Fresh finite positive BMS voltage can bound future capacity of the qualified installed 35 A MPPT. A retained known source fault disables this forecast recharge capacity. Forecast energy never replenishes the measured battery ledger.
- BMS display grace remains independently bounded by its original evidence and the existing maximum plan age. MQTT reconnect or soft input changes do not renew the retained frame's generation time or TTL.

## Deterministic verification

Regression failures were observed before their fixes for economic-hold fallback, expected MPPT standby, Limiter-OFF priority, charge-plan production qualification, retained-fault future recharge credit, and independent BMS display grace. Battery-only control was verified at 0/120/900 W with a frozen MPPT sequence and invalid nonzero MPPT power. Existing measured-PV-only behavior without BMS, alarms/reserve, MIN/correction, sequence recovery, chronological PV energy accounting and plan retention remain covered.

Affected current-control/application, SBT receiver/publisher contract, production planning composition, future-price, charge-plan and plan-retention tests passed. USB and isolated mock test-ESP builds passed. No unrelated complete suite was required.

## Dashboard

The price legend previously displayed the last sample of the plotted series. Both responsive charts now show `Price maximum (shown period)`; the verified displayed maximum was 0.846 EUR/kWh. The live graph contains three hours of history plus 36 future hours.

The battery energy is a nominal estimate: BMS full capacity 131.25 Ah times the existing 51.38 V reference is 6743.625 Wh, or 5462.336 Wh at 81% SOC. A footer now identifies the estimate and displays remaining/full Ah. Reserve and conversion losses are excluded. No physical 5 kWh capacity, BMS recalibration or changed energy scaling was assumed. Exact dashboard backup/readback, structural/numeric/JavaScript checks and desktop/mobile screenshots are preserved in the local `ha-display-20261028` evidence folder.

## Hardware acceptance

The identity-verified COM8 test board passed 196.429 seconds of qualified observation with three matching paired plans, zero current output, advancing control/uptime, one boot, adequate heap and unchanged 75-setting readback. The final frozen test image SHA256 is `5fb9da324aa1afbb2ccb84b470b9651e380a94f2fe13fb5426ed18af1c7dbf03`.

Production accepted the single paced OTA at 2026-10-28 19:10:22 UTC. The 1,189,168-byte production image SHA256 is `2e7b39a4d1624be9099986f2c5e1a40c229e70abae5bf15d0ce30a9281827099`. A preceding HTTP timeout occurred before any durable upload intent or POST; no accepted upload was replayed. Post-boot exact MAC/IP, version 5.4.10, Live0/Settings1, OTA inactive and all 75 settings with zero differences were verified. Home Assistant independently showed inverter SetValue0 W, BMS power0 W and actual discharge MOS OFF after refresh.

The production gate passed at 2026-10-28 19:24:58 UTC after 236.546 qualified seconds. Three paired 144-slot plans matched their price/PV revisions and chronological charge/discharge bounds. The current command remained zero through standby and any explicitly guarded source gap; generation age and delivery gaps remained within 180 seconds. Control counters and uptime advanced, one boot was observed, heap margins passed and the physical discharge MOS remained OFF. Final state was `preserve / mppt_standby / forecast_hold`, with 3823.6 Wh reserved and available. The final observed transport-counter deltas were zero for transmit failures, connect failures, transmit timeouts and overflows.

Cold-start schedule acquisition initially lagged despite successful native Home Assistant producer traces. The existing PV and price publish-current-data automations were each run once, without modifying inputs/configuration or sending actuator commands. Passive broker capture verified all six native PV chunks at revision `1793215241` and all twelve native price chunks at revision `1793215295`; SIL then published matching valid plans and HA displayed a current plan. This is operational evidence of the accepted input set, not a claim that the previously open general MQTT transport issue is repaired.

Complete baselines/readbacks, frozen hashes, observer records and upload markers are retained in local `sil-night-standby-20261028` and the referenced `sil-hardening-20261025/sil5410-production-20261028-final` deployment directory. Physical nighttime standby is exercised on hardware; BMS loss with available PV is covered by deterministic control/application tests rather than a nighttime claim of real solar production.

## Repository state and limits

At hardware acceptance the work was uncommitted on `fix/mqtt-session-recovery`; no commit, push, merge or branch switch was part of that hardware task. The owner subsequently authorized a separate commit/push/main-integration workflow. Applicable root/canonical governance and the control-plane, refactor, workflow and documentation role files were read. Changed runtime files are `src/control/CurrentControl.h`, `src/main.cpp`, `src/sbt/SbtReceiver.h`, `src/sbt/SbtService.h` and `platformio.ini`; affected host tests and the production-expression test runner were updated/added, alongside the relevant README/planning documents and this acceptance record. No changelog file exists in this repository.

The acceptance does not establish actual solar yield tomorrow or a measured physical battery capacity. The battery estimate follows the reported BMS Ah and existing nominal reference. Masked secrets compare as masked; the deployment never writes settings. The discharge MOS was left OFF for the owner's later manual re-enable. Task-owned test support and passive observers are stopped after their accepted observation windows; no production service is stopped.
