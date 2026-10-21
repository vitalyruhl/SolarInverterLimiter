"""Pure Shed forecast normalization/calibration. No network, files, or actuators."""
import datetime as dt
import json
import math
import statistics

SLOT_SECONDS = 900
MAX_SLOTS = 144
CHUNK_SLOTS = 24


def _hourly_rows(wh_period):
    if not isinstance(wh_period, dict) or len(wh_period) > 96:
        raise ValueError('Invalid bounded hourly series')
    rows = []
    for stamp, value in wh_period.items():
        if not isinstance(stamp, str):
            raise ValueError('Invalid hourly timestamp')
        when = dt.datetime.fromisoformat(stamp)
        if when.tzinfo is None:
            raise ValueError('Timezone required')
        epoch = int(when.timestamp())
        if epoch % 3600 or isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError('Invalid hourly period')
        if not math.isfinite(value) or not 0 <= value <= 10000:
            raise ValueError('Hourly energy outside supported range')
        rows.append((epoch, float(value)))
    if len({t for t, _ in rows}) != len(rows):
        raise ValueError('Conflicting equivalent timestamps')
    return sorted(rows)


def provider_hourly(wh_period):
    """Adapt provider END timestamps; never trust HA's hour-floor aggregation.

    A zero sunrise boundary starts each observed daylight sequence. Each
    subsequent interval must fit one real UTC hour. Partial daylight edge
    intervals contribute their Wh to that containing hour, then quarter-hour
    spreading remains explicitly hourly_disaggregated. Overnight is missing.
    """
    if not isinstance(wh_period, dict) or len(wh_period) > 96:
        raise ValueError('Invalid bounded provider series')
    rows = []
    for stamp, energy in wh_period.items():
        when = dt.datetime.fromisoformat(stamp)
        if when.tzinfo is None or isinstance(energy, bool) or not isinstance(energy, (int, float)):
            raise ValueError('Invalid provider period')
        if not math.isfinite(energy) or not 0 <= energy <= 10000:
            raise ValueError('Invalid provider energy')
        epoch = when.timestamp()
        if epoch != int(epoch):
            raise ValueError('Fractional provider timestamp')
        rows.append((int(epoch), energy))
    rows.sort()
    if len({epoch for epoch, _ in rows}) != len(rows):
        raise ValueError('Duplicate provider timestamp')
    previous, hours = None, {}
    for end, energy in rows:
        if previous is None or end - previous > 3600:
            if energy != 0:
                raise ValueError('Missing zero-energy provider boundary')
            previous = end
            continue
        start = previous
        hour = start // 3600 * 3600
        if not start < end <= hour + 3600:
            raise ValueError('Provider interval crosses an hourly boundary')
        key = dt.datetime.fromtimestamp(hour, dt.timezone.utc).isoformat()
        hours[key] = hours.get(key, 0) + energy
        previous = end
    _hourly_rows(hours)
    return hours


def normalize_hourly(wh_period, now):
    """Conserve Wh; round fractional totals to the nearest 0.25 Wh."""
    start = int(now) // SLOT_SECONDS * SLOT_SECONDS
    power, flags = [None] * MAX_SLOTS, [1] * MAX_SLOTS
    for epoch, energy in _hourly_rows(wh_period):
        # Four constant-power quarter hours conserve an integer hourly Wh total.
        # Split the quantization remainder deterministically for fractional Wh.
        total_w = int(math.floor(energy * 4 + 0.5))
        base, remainder = divmod(total_w, 4)
        for quarter in range(4):
            index = (epoch + quarter * SLOT_SECONDS - start) // SLOT_SECONDS
            if 0 <= index < MAX_SLOTS:
                power[index] = base + (1 if quarter >= 4 - remainder else 0)
                flags[index] = 2  # Explicitly hourly_disaggregated.
    if all(value is None for value in power):
        raise ValueError('Forecast has no covered provider periods')
    return {'t': start, 'p': power, 'x': flags}


def freeze_periods(snapshots, wh_period, generated):
    """First pre-period forecast wins; later revisions never rewrite the reference."""
    for epoch, energy in _hourly_rows(wh_period):
        key = str(epoch)
        if generated <= epoch and key not in snapshots:
            snapshots[key] = {'g': int(generated), 'wh': energy}
    for key in list(snapshots):
        if int(key) < generated - 35 * 86400:
            del snapshots[key]


def freeze_daily(days, wh_period, daily_totals, generated, timezone):
    """Require agreement with an independent native day total before freezing."""
    from zoneinfo import ZoneInfo
    zone = ZoneInfo(timezone)
    grouped = {}
    for epoch, energy in _hourly_rows(wh_period):
        day = dt.datetime.fromtimestamp(epoch, zone).date().isoformat()
        grouped.setdefault(day, []).append(energy)
    for day, expected in daily_totals.items():
        start = int(dt.datetime.combine(dt.date.fromisoformat(day), dt.time(), zone).timestamp())
        values = grouped.get(day, [])
        if day in days or generated >= start or not values:
            continue
        if isinstance(expected, bool) or not isinstance(expected, (int, float)):
            continue
        total = math.fsum(values)
        if math.isfinite(expected) and expected >= 0 and math.isclose(total, expected, rel_tol=0, abs_tol=1e-6):
            days[day] = {'g': int(generated), 'wh': total, 'hourly_count': len(values),
                         'native_day_total_wh': expected, 'complete': True}
    cutoff = dt.datetime.fromtimestamp(generated - 35 * 86400, zone).date().isoformat()
    for day in list(days):
        if day < cutoff:
            del days[day]


def calibrate(records, today):
    """Median eligible same-side actual/original forecasts over 30 completed days."""
    today = dt.date.fromisoformat(today)
    days = {}
    for row in records:
        day = dt.date.fromisoformat(row['day'])
        if not 1 <= (today - day).days <= 30 or row.get('eligible') is not True:
            continue
        forecast, actual = row.get('forecast_wh'), row.get('actual_wh')
        if isinstance(forecast, bool) or isinstance(actual, bool):
            continue
        if not isinstance(forecast, (int, float)) or not isinstance(actual, (int, float)):
            continue
        if not math.isfinite(forecast) or not math.isfinite(actual) or forecast < 500 or actual < 0:
            continue
        if day in days and days[day] != (forecast, actual):
            days[day] = None  # Ambiguous duplicate qualification is excluded.
        elif day not in days:
            days[day] = (forecast, actual)
    good = [(day, value) for day, value in days.items() if value is not None]
    count = len(good)
    age = min(65535, (today - max(day for day, _ in good)).days) if good else 65535
    raw = statistics.median(actual / forecast for _, (forecast, actual) in good) if good else None
    qualified = count >= 14
    factor = int(math.floor(min(1.2, max(0.5, raw)) * 1000 + 0.5)) if qualified else 1000
    return {'factor': factor, 'count': count, 'age_days': age,
            'quality': 'calibrated' if qualified else 'uncalibrated', 'raw_factor': raw}


def packetize(plan, generated, session, calibration):
    count = len(plan['p'])
    if not 2 <= count <= MAX_SLOTS or len(plan['x']) != count:
        raise ValueError('Invalid forecast coverage')
    if plan['t'] % 900 or not plan['t'] <= generated < plan['t'] + 900:
        raise ValueError('Generation outside current slot')
    if not 0 < session <= generated <= 4294967295:
        raise ValueError('Invalid producer session')
    packets = []
    for chunk in range((count + CHUNK_SLOTS - 1) // CHUNK_SLOTS):
        begin, end = chunk * CHUNK_SLOTS, min(count, (chunk + 1) * CHUNK_SLOTS)
        packet = {'v': 2, 's': session, 'r': generated, 't': plan['t'], 'd': 900,
                  'n': count, 'k': chunk, 'g': generated, 'e': plan['t'] + count * 900,
                  'f': calibration['factor'], 'c': calibration['count'],
                  'a': calibration['age_days'], 'q': calibration['quality'],
                  'o': 'forecast_solar', 'p': plan['p'][begin:end], 'x': plan['x'][begin:end]}
        text = json.dumps(packet, separators=(',', ':'), allow_nan=False)
        if len(text.encode('utf8')) > 768:
            raise ValueError('Forecast packet exceeds 768 bytes')
        packets.append(text)
    return packets


def corrected_power(value):
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return max(0, number - 2) if math.isfinite(number) else None
def bms_allows_charge(enabled, alarm_bits):
    """Read-only HA binary-sensor permission and a clean uint32 alarm value."""
    if enabled != 'on' or isinstance(alarm_bits, bool):
        return False
    try:
        value = int(alarm_bits)
    except (TypeError, ValueError):
        return False
    return str(alarm_bits) == '0' and value == 0


def source_fresh(previous, health, generated):
    """Require live transport and advancing acquisition sequences for both sources."""
    for name in ('bms', 'victron'):
        row = health.get(name, {})
        seq, age, updated = row.get('seq'), row.get('age_ms'), row.get('updated')
        old = previous.get(name)
        if row.get('state') not in ('ok', 'fresh'):
            return False
        if any(isinstance(value, bool) or not isinstance(value, (int, float)) or
               not math.isfinite(value) for value in (seq, age, updated, old)):
            return False
        if not (seq > old >= 0 and 0 <= age <= 15000 and 0 <= generated - updated <= 60):
            return False
    return True


def qualify_day(day, forecast_wh, actual_wh, proof):
    """Unknown physical qualification is excluded, never treated as a passed day."""
    required = ('complete_observation', 'pre_period_forecast', 'reset_free',
                'outage_free', 'not_full_or_curtailed', 'current_limit_unrestricted')
    reasons = [name for name in required if proof.get(name) is not True]
    if proof.get('observation_start', 0) < proof.get('current_limit_confirmed_from', 0):
        reasons.append('current_limit_confirmation_later_than_observation')
    if isinstance(forecast_wh, bool) or not isinstance(forecast_wh, (int, float)) or not math.isfinite(forecast_wh) or forecast_wh < 500:
        reasons.append('insufficient_forecast')
    if isinstance(actual_wh, bool) or not isinstance(actual_wh, (int, float)) or not math.isfinite(actual_wh) or actual_wh < 0:
        reasons.append('actual_unavailable')
    return {'day': day, 'forecast_wh': forecast_wh, 'actual_wh': actual_wh,
            'eligible': not reasons, 'exclusions': reasons}
