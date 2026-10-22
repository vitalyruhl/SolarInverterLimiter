# Victron display-only fallback — 2026-10-21

The Home dashboard uses dedicated display entities. The original MQTT measurement,
source-health and availability entities retain their strict validity contract for
SIL, automations, forecast calibration and Energy statistics.

## MQTT contract

Raw power/error/yield entities use `availability_mode: all`: collector
`SolarBatteryTelemetry/status` (`online`/`offline`) plus source
`SolarBatteryTelemetry/victron/availability` (`available`/`unavailable`).
`binary_sensor.sbt_victron_availability` has the collector gate. The health sensor
`sensor.sbt_victron_health` expires after 15 seconds and carries `reason`,
`sample_sequence`, and `sample_age_ms`.

The display power helper uses zero only when no current raw BLE power is valid.
Its `source_status` attribute and the Power Usage card explicitly identify this
as a display fallback. It is not a measurement and has no statistics state class.

New SBT 1.4.0 last-valid topics are retained JSON:

- `SolarBatteryTelemetry/victron/last_valid/error_code`
- `SolarBatteryTelemetry/victron/last_valid/yield_today_wh`

Payload: `{"value": 0, "received_at": 1792522200}`. The UTC Unix timestamp belongs
to the last valid acquisition; retransmission/reconnect does not advance it.
A null timestamp means that the acquisition date is unknown.

Historical display MQTT entities deliberately have no source/collector
availability, expiry or statistics metadata. Their values remain visible during
BLE/collector loss, with an explicit stale/last-received date indication. No
production fixture is published to initialize these topics.

Before matching firmware is deployed, error/yield text can show a valid raw value
only while BLE is current and explicitly says that the receipt date is unavailable.
If neither retained history nor current raw data exists, the display says no valid
value has been received; it does not invent a zero. The today-only display remains
unavailable until a trustworthy receipt date exists. After local midnight, a
previous-day yield remains in the dated last-received row, and the today-only row
becomes unavailable. Existing Energy/statistics inputs are not replaced.

## Scope and verification

`victron-display.yaml` is an exact package export. The live installation merges
its MQTT sensors into `/config/mqttsensors.yaml` and its template group into
`/config/templates.yaml`; do not install a second copy.

Private rollback files: `/config/.codex-backups/victron-display-20261021/`, including
the original two YAML includes and the original Lovelace configuration. Dashboard
changes use the supported HA editor, not direct `.storage` writes.

Focused offline check: `python docs/home-assistant/test_victron_display.py`.
Eight tests render the actual YAML templates with explicitly mocked HA states and
clock, covering live power, BLE/health loss, stale values, pending firmware,
local-midnight rollover, absent/future dates and exclusion of statistics metadata.
Affected-integration reload and visible dashboard checks are separate live proof;
SBT 1.4.0 firmware deployment and a real future BLE outage remain rollout evidence.

## Live readback

Both affected reloads (`mqtt.reload`, `template.reload`) returned HTTP 200. All six
new entities were present in HA state readback. The saved Home cards at indices
4 (Power Usage), 6 (Energy Today), and 13 (Victron stack) matched the exact reviewed
YAML exports. A full before/after dashboard comparison confirmed that only these
three Home cards changed. The two include additions matched the package export;
all original include structures were preserved.

At verification time BLE was current: display power was 0 W, error was
`0 (current BLE; receipt date unavailable)`, and yield was
`0 Wh (current BLE; receipt date unavailable)`. The today-only display was
unavailable. Both new last-valid MQTT entities were unknown because no matching
SBT 1.4.0 retained topic had yet arrived. This is a rollout dependency, not a
verified historical-value delivery or real outage test.

Local ignored evidence is under
`.superpowers/sdd/2026-10-20-sil-sbt-ha-integration/`:
`ha-victron-display.yaml`, `ha-victron-cards-after.json`, and
`ha-victron-template-tests.txt`. No credentials or HA location are included.

## Corrected inverter AC displays

Home power displays use the existing `sensor.shed_inverter_corrected_power`,
which computes `max(0, raw_AC_W - 2)` and propagates unavailable input. The four
bindings are the Power Usage AC series, both conditional battery-flow inverter
power fields, and the PV/AC 24-hour graph's AC series. Victron PV and BMS power
are not offset. No duplicate helper or new energy counter was created. The
cumulative corrected delivery Integral remains distinct from a daily counter;
the existing raw Smartplug daily energy row was not remapped to that cumulative
value.

A single supported `lovelace/config/save` call was followed by full API readback.
The saved Home view contains all 16 cards, including the restored grid 24-hour
and Powermeter cards and both existing explanatory Markdown blocks. The exact
final difference from the reconstructed previously verified 16-card reference
is four entity values, each changing raw Smartplug AC power to the existing
corrected power entity. All other views, cards and fields are equal.

Ignored local evidence: `ha-ac-reconstructed-before.json`,
`ha-ac-saved-after.json`, `ha-ac-proof.json`, and `ha-ac-local-diff-check.json` in
the integration evidence directory above. Live readback confirmed raw 2 W and
corrected 0 W. No source states, devices, source availability or histories were
modified. The separate `ha-yield-readonly-proof.json` records that the raw yield
was 0 Wh with fresh available BLE, while the new last-valid entity lacked a
receipt timestamp and the today-only display correctly remained unavailable.
