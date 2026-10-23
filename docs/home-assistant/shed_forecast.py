"""HA Pyscript: one Shed forecast producer; no inverter or MOS actions.

Install core beside /config/pyscript/modules/shed_forecast_core.py.
Trigger this service using shed-forecast-package.yaml; do not add a second timer.
Existing Pyscript 2.1, allow_all_imports and hass_is_global are required.
"""
import datetime

SHED_ENTRY = '01M5D5VEHZR1GR88THXH8C8BFS'
SCHEDULE_TOPIC = 'homeassistant/sensor/solar_forecast/shed/schedule'
# Owner confirmed no configured Victron charge-current limit. Future evidence only.
OWNER_CURRENT_LIMIT_FROM_UTC = 1792518660


@pyscript_executor
def _prepare_forecast(raw_periods, observed, daily_totals, generated, timezone, confirmed_from):
    import datetime as dt
    import importlib.util
    import json
    import math
    import shutil
    from pathlib import Path
    from zoneinfo import ZoneInfo

    module_path = '/config/pyscript/modules/shed_forecast_core.py'
    spec = importlib.util.spec_from_file_location('shed_forecast_core', module_path)
    core = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    directory = Path('/config/data/shed_forecast')
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / 'ledger.json'
    if path.exists():
        if path.stat().st_size > 1000000:
            raise ValueError('Shed ledger exceeds bounded storage budget')
        ledger = json.loads(path.read_text())
        if ledger.get('schema') == 1:
            # Preserve the original ledger before invalidating wrong hour references.
            backup = directory / 'ledger.schema1-provider-timing.json'
            if not backup.exists():
                shutil.copyfile(path, backup)
            ledger['schema'] = 2
            ledger['snapshots'] = {}
            ledger['original_days'] = {}
            for row in ledger['records']:
                row['eligible'] = False
                row.setdefault('exclusions', []).append('legacy_period_semantics')
        if ledger.get('schema') != 2:
            raise ValueError('Unsupported Shed ledger schema')
    else:
        ledger = {'schema': 2, 'session': generated, 'snapshots': {}, 'days': {}, 'records': []}
    hourly = core.provider_hourly(raw_periods)
    ledger['period_semantics'] = 'provider_end_v1'
    local_zone = ZoneInfo(timezone)
    local = dt.datetime.fromtimestamp(generated, local_zone)
    day = local.date().isoformat()
    start = int(dt.datetime.combine(local.date(), dt.time(), local_zone).timestamp())
    core.freeze_periods(ledger['snapshots'], hourly, generated)
    native_days = {(local.date() + dt.timedelta(days=int(offset))).isoformat(): value
                   for offset, value in daily_totals.items()}
    core.freeze_daily(ledger.setdefault('original_days', {}), hourly, native_days, generated, timezone)
    for original_day, original in ledger['original_days'].items():
        if original['g'] == generated:
            original['raw_periods_wh'] = {stamp: value for stamp, value in raw_periods.items()
                if dt.datetime.fromisoformat(stamp).astimezone(local_zone).date().isoformat() == original_day}
    ledger['last_raw_periods_wh'] = raw_periods
    active = ledger['days'].setdefault(day, {
        'first': generated, 'last': generated, 'last_yield': None, 'actual_wh': 0.0,
        'start_zero': False, 'reset_free': True, 'outage_free': True,
        'not_full_or_curtailed': True, 'current_limit_unrestricted': generated >= confirmed_from})
    if generated - active['last'] > 600:  # Two missed 5-minute observations.
        active['outage_free'] = False
    active['last'] = generated
    # Missing source-liveness proof cannot qualify an otherwise numeric retained value.
    health = observed.get('health', {})
    live = core.source_fresh(ledger.get('source_sequences', {}), health, generated)
    ledger['source_sequences'] = {name: row.get('seq') for name, row in health.items()}
    if not live:
        active['outage_free'] = False
    if not core.bms_allows_charge(observed.get('charge_enabled'), observed.get('alarm_bits')):
        active['not_full_or_curtailed'] = False
    # Validate the number independently; corrected_power's 2 W offset is AC-only.
    value = observed.get('yield_wh')
    try:
        value = float(value)
        numeric = math.isfinite(value) and value >= 0
    except (TypeError, ValueError):
        numeric = False
    if not numeric:
        active['outage_free'] = False
        active['last_yield'] = None
    else:
        if active['last_yield'] is None:
            if generated - start <= 600 and value == 0:
                active['start_zero'] = True
        else:
            delta = value - active['last_yield']
            if delta < 0:
                active['reset_free'] = False
            else:
                active['actual_wh'] += delta
        active['last_yield'] = value
    try:
        soc = float(observed.get('soc'))
        if not math.isfinite(soc):
            active['outage_free'] = False
        elif soc >= 100:
            active['not_full_or_curtailed'] = False
    except (TypeError, ValueError):
        active['outage_free'] = False
    charge_state = str(observed.get('charge_state', '')).lower()
    if any(word in charge_state for word in ('float', 'absorption')):
        active['not_full_or_curtailed'] = False
    if not charge_state or charge_state in ('unknown', 'unavailable'):
        active['outage_free'] = False
    # Owner configuration evidence applies only from confirmed_from onward.
    try:
        charge_a = float(observed.get('charge_current_a'))
        if not math.isfinite(charge_a) or charge_a < 0:
            active['outage_free'] = False
        elif charge_a >= 35.0:  # Qualified MPPT 150/35 hardware ceiling.
            active['not_full_or_curtailed'] = False
    except (TypeError, ValueError):
        active['outage_free'] = False
    for old_day, observed_day in list(ledger['days'].items()):
        if old_day >= day:
            continue
        old_date = dt.date.fromisoformat(old_day)
        old_start = int(dt.datetime.combine(old_date, dt.time(), local_zone).timestamp())
        old_end = int(dt.datetime.combine(old_date + dt.timedelta(days=1), dt.time(), local_zone).timestamp())
        original = ledger['original_days'].get(old_day, {})
        proof = {name: observed_day[name] for name in
                 ('reset_free', 'outage_free', 'not_full_or_curtailed', 'current_limit_unrestricted')}
        proof['complete_observation'] = (observed_day['first'] <= old_start + 600 and
                                         observed_day['last'] >= old_end - 600 and observed_day['start_zero'])
        proof['pre_period_forecast'] = (original.get('complete') is True and original.get('g', old_start) < old_start)
        proof['observation_start'] = observed_day['first']
        proof['current_limit_confirmed_from'] = confirmed_from
        record = core.qualify_day(old_day, original.get('wh'), observed_day['actual_wh'], proof)
        ledger['records'] = [row for row in ledger['records'] if row['day'] != old_day] + [record]
        del ledger['days'][old_day]
    cutoff = (local.date() - dt.timedelta(days=35)).isoformat()
    ledger['records'] = [row for row in ledger['records'] if row['day'] >= cutoff]
    calibration = core.calibrate(ledger['records'], day)
    plan = core.normalize_hourly(hourly, generated)
    packets = core.packetize(plan, generated, ledger['session'], calibration)
    # Producer artifact for cross-language contract checks; not a broker capture.
    (directory / 'last-packets.json').write_text(json.dumps(packets, separators=(',', ':')))
    ledger['last_publication'] = generated
    ledger['last_calibration'] = calibration
    temporary = directory / 'ledger.next.json'
    temporary.write_text(json.dumps(ledger, separators=(',', ':'), allow_nan=False))
    temporary.replace(path)
    return {'packets': packets, 'calibration': calibration,
            'known_slots': sum(value is not None for value in plan['p']),
            'snapshot_periods': len(ledger['snapshots']), 'revision': generated}


@service(supports_response='optional')
def shed_forecast_publish():
    """Freeze original forecasts and publish raw PV offers with explicit quality."""
    task.unique('shed_forecast_publish')
    entry = hass.config_entries.async_get_entry(SHED_ENTRY)
    if entry is None or entry.domain != 'forecast_solar' or entry.title != 'Shed':
        raise ValueError('Shed forecast entry identity mismatch')
    plane = list(entry.subentries.values())[0]
    if len(entry.subentries) != 1 or plane.data.get('declination') != 14.8 or plane.data.get('modules_power') != 1840 or plane.data.get('azimuth') != 180:
        raise ValueError('Shed forecast geometry mismatch')
    result = service.call('forecast_solar', 'get_forecast', config_entry=SHED_ENTRY,
                          resolution='raw', blocking=True, return_response=True)
    if not isinstance(result, dict) or not isinstance(result.get('wh_period'), dict):
        raise ValueError('Shed forecast service did not return native period energy')
    generated = int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    observed = {}
    for key, entity_id in (('yield_wh', 'sensor.sbt_victron_yield_today_wh'),
                           ('soc', 'sensor.sbt_bms_state_of_charge_pct'),
                           ('charge_state', 'sensor.sbt_victron_charge_state'),
                           ('charge_current_a', 'sensor.sbt_victron_charge_current_a'),
                           ('charge_enabled', 'binary_sensor.sbt_bms_charge_enabled'),
                           ('alarm_bits', 'sensor.sbt_bms_alarm_bits')):
        entity = hass.states.get(entity_id)
        observed[key] = entity.state if entity else None
    observed['health'] = {}
    for name, entity_id in (('bms', 'sensor.sbt_bms_health'), ('victron', 'sensor.sbt_victron_health')):
        entity = hass.states.get(entity_id)
        if entity:
            observed['health'][name] = {'state': entity.state,
                'seq': entity.attributes.get('sample_sequence'),
                'age_ms': entity.attributes.get('sample_age_ms'),
                'updated': entity.last_updated.timestamp()}
    daily_totals = {}
    for offset, entity_id in ((0, 'sensor.energy_production_today_7'),
                              (1, 'sensor.energy_production_tomorrow_7')):
        entity = hass.states.get(entity_id)
        if entity and entity.attributes.get('unit_of_measurement') == 'kWh':
            try:
                daily_totals[offset] = float(entity.state) * 1000
            except (ValueError, TypeError):
                pass
    prepared = _prepare_forecast(result['wh_period'], observed, daily_totals, generated, hass.config.time_zone, OWNER_CURRENT_LIMIT_FROM_UTC)
    for payload in prepared['packets']:
        service.call('mqtt', 'publish', topic=SCHEDULE_TOPIC, payload=payload,
                     qos=1, retain=False, blocking=True)
    calibration = prepared['calibration']
    state.set('sensor.shed_pv_forecast_status', value=calibration['quality'], new_attributes={
        'friendly_name': 'Shed PV forecast status', 'factor': calibration['factor'] / 1000,
        'raw_factor': calibration['raw_factor'], 'eligible_days': calibration['count'],
        'calibration_age_days': calibration['age_days'], 'revision': prepared['revision'],
        'known_slots': prepared['known_slots'], 'snapshot_periods': prepared['snapshot_periods'],
        'native_resolution_minutes': 60, 'wire_quality': 'hourly_disaggregated',
        'period_semantics': 'provider_end_v1',
        'current_limit_proof': 'owner confirmed no configured limit; MPPT 150/35 hardware maximum 35 A',
        'current_limit_confirmed_from_utc': OWNER_CURRENT_LIMIT_FROM_UTC})
    return {'published_chunks': len(prepared['packets']),
            'max_payload_bytes': max([len(value.encode('utf8')) for value in prepared['packets']]),
            'known_slots': prepared['known_slots'], 'calibration': calibration}
