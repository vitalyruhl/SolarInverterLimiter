# Home Assistant integration

This folder mirrors the live Home Assistant setup that feeds and visualizes
SolarInverterLimiter (SIL) and SolarBatteryTelemetry (SBT). The files are
exported from the live system; keep them in sync when the live configuration
changes. No credentials, tokens or mobile device IDs are stored here; replace
the `<...>` placeholders with local values.

## File map

| Repository file | Live location | Content |
| --- | --- | --- |
| `sil-mqtt.yaml` | `/config/mqttsensors.yaml` | SIL MQTT sensors and negative-price binary sensor |
| `sbt-mqtt.yaml` | `/config/mqttsensors.yaml` | SBT BMS/Victron sensors, binary sensors, MOS permission switches and buttons |
| `mqtt-discovery.json` | retained `homeassistant/.../config` topics | Discovery payloads: SIL operating-mode select, ChargePlan, DischargeBudget, configured maximum output, uptime; SBT cell voltages and diagnostics |
| `energy-templates.yaml` | `/config/templates.yaml` | Meter, power, self-consumption, money-saved, current price and SBT yield templates; SBT BMS control switch |
| `energy-meters.yaml` | `/config/sensors.yaml`, `/config/meterconfig.yaml`, `/config/configuration.yaml` | `money_saved_total` integral, utility meters, SBT BMS control REST commands |
| `energy-automations.yaml` | `/config/automations.yaml` | Current price and price-schedule publishers, SBT full-energy estimate, price notifications |
| `ui-helpers.yaml` | UI helpers (config entries) | SBT charge/discharge power templates, SBT energy integrals, current discharge plan, `input_number.sbt_battery_full_energy_wh` |
| `shed-forecast-package.yaml` | `/config/automations.yaml` | `shed_publish_pv_forecast` automation |
| `shed_forecast.py`, `shed_forecast_core.py` | `/config/pyscript/shed_forecast.py`, `/config/pyscript/modules/shed_forecast_core.py` | Shed PV forecast producer |
| `shed-energy.yaml` | `/config/templates.yaml` + UI integral helper | Shed inverter corrected power and delivery |
| `sil-planner-shadow-pyscript.py`, `sil-planner-shadow-core.py` | `/config/pyscript/sil_planner_shadow.py`, `/config/pyscript/modules/sil_planner_shadow_core.py` | v2 price-schedule publisher and production/test comparison entities |
| `victron-display.yaml` | `/config/templates.yaml` | Victron display helpers |
| `dashboard-overview-home.yaml` | `.storage/lovelace.lovelace`, view `default_view` | Overview Home view including the 36 h discharge plan with solar forecast |
| `dashboard-overview-power.yaml` | `.storage/lovelace.lovelace`, view `preise` | Overview Power view |
| `dashboard-power-home.yaml` | `.storage/lovelace.dashboard_power`, view 0 | Power dashboard Home view |

Background documents: `shed-migration.md` (PV forecast pipeline),
`sil-planner-shadow-readme.md` (price adapter and comparison entities),
`victron-display.md`.

## Required integrations

- MQTT (broker shared with SIL and SBT).
- EPEX Spot (`sensor.epex_spot_data_total_price_2`, Energyforecast.de source).
  The provider token stays in the HA config entry only.
- Forecast.Solar entry "Shed" (damping 0/0).
- Pyscript with `allow_all_imports` and `hass_is_global`.
- HACS frontend cards: `apexcharts-card`, `mini-graph-card`,
  `sunsynk-power-flow-card`.

## Installing discovery entities

Publish each entry of `mqtt-discovery.json` retained to its topic, for example:

```powershell
mosquitto_pub -h <broker> -u <user> -P <password> -r -t "<topic>" -m "<json payload>"
```

Do not additionally define the same entities in YAML, or HA creates
duplicates with a `_2` suffix.

## Tests

```powershell
python docs/home-assistant/test_shed_forecast_core.py
python docs/home-assistant/test_victron_display.py
python test/host/ha_forecast_integrals_test.py
```
