import datetime as dt
import importlib.util
import json
from pathlib import Path
import unittest

path = Path(__file__).with_name('shed_forecast_core.py')
spec = importlib.util.spec_from_file_location('shed_forecast_core', path)
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)

class ForecastTests(unittest.TestCase):
    def test_hourly_energy_is_conserved_without_filling_missing_night(self):
        now = int(dt.datetime(2026, 10, 21, 6, 0, tzinfo=dt.timezone.utc).timestamp())
        plan = core.normalize_hourly({'2026-10-21T09:00:00+02:00': 117}, now)
        self.assertEqual(plan['p'][4:8], [117] * 4)
        self.assertEqual(sum(plan['p'][4:8]) / 4, 117)
        self.assertEqual(plan['x'][4:8], [2] * 4)
        self.assertEqual(plan['p'][:4], [None] * 4)
        self.assertEqual(plan['x'][:4], [1] * 4)

    def test_fractional_energy_has_bounded_quarter_wh_quantization(self):
        now = 1792566000
        plan = core.normalize_hourly({'2026-10-21T09:00:00+02:00': 42.25}, now)
        self.assertEqual(sum(v for v in plan['p'][:4]), 169)

    def test_dst_repeated_local_hour_has_distinct_utc_slots(self):
        now = int(dt.datetime(2026, 10, 25, 0, 0, tzinfo=dt.timezone.utc).timestamp())
        plan = core.normalize_hourly({'2026-10-25T02:00:00+02:00': 100,
                                      '2026-10-25T02:00:00+01:00': 200}, now)
        self.assertEqual(plan['p'][:8], [100] * 4 + [200] * 4)

    def test_provider_endpoints_keep_sunrise_sunset_in_correct_hour(self):
        raw = {'2026-10-21T08:17:00+02:00': 0,
               '2026-10-21T09:00:00+02:00': 42,
               '2026-10-21T10:00:00+02:00': 129,
               '2026-10-21T10:27:00+02:00': 40,
               '2026-10-22T08:18:00+02:00': 0,
               '2026-10-22T09:00:00+02:00': 50}
        hours = core.provider_hourly(raw)
        self.assertEqual(list(hours.values()), [42, 129, 40, 50])
        self.assertEqual(list(hours)[0], '2026-10-21T06:00:00+00:00')
        plan = core.normalize_hourly(hours, 1792562400)
        self.assertEqual(plan['p'][:12], [42] * 4 + [129] * 4 + [40] * 4)
        self.assertTrue(all(v is None for v in plan['p'][12:96]))
        self.assertEqual(sum(v for v in plan['p'] if v is not None) / 4, 261)

    def test_provider_endpoints_reject_unknown_or_crossing_intervals(self):
        cases = ({'2026-10-21T09:00:00+02:00': 42},
                 {'2026-10-21T08:17:00+02:00': 0, '2026-10-21T09:17:00+02:00': 42},
                 {'2026-10-21T08:00:00+02:00': 0, '2026-10-21T10:00:00+02:00': 42})
        for raw in cases:
            with self.assertRaises(ValueError):
                core.provider_hourly(raw)

    def test_provider_dst_endpoint_intervals_are_distinct_utc_hours(self):
        raw = {'2026-10-25T02:00:00+02:00': 0,
               '2026-10-25T02:00:00+01:00': 100,
               '2026-10-25T03:00:00+01:00': 200}
        self.assertEqual(core.provider_hourly(raw),
                         {'2026-10-25T00:00:00+00:00': 100,
                          '2026-10-25T01:00:00+00:00': 200})

    def test_no_covered_periods_cannot_replace_active_offer(self):
        for rows in ({}, {'2026-10-20T09:00:00+02:00': 100}):
            with self.assertRaises(ValueError):
                core.normalize_hourly(rows, 1792566000)

    def test_invalid_energy_is_rejected(self):
        for value in (-1, True, 'bad', float('nan'), 10001):
            with self.assertRaises(ValueError):
                core.normalize_hourly({'2026-10-21T09:00:00+02:00': value}, 1792566000)

    def test_immutable_snapshot_never_uses_post_period_forecast(self):
        snapshots = {}
        hourly = {'2026-10-21T09:00:00+02:00': 100}
        core.freeze_periods(snapshots, hourly, 1792565900)
        core.freeze_periods(snapshots, {'2026-10-21T09:00:00+02:00': 900}, 1792565950)
        self.assertEqual(snapshots['1792566000']['wh'], 100)
        late = {}
        core.freeze_periods(late, hourly, 1792566001)
        self.assertEqual(late, {})

    def test_partial_original_day_cannot_qualify_full_actual_day(self):
        days = {}
        hourly = {'2026-10-21T09:00:00+02:00': 300}
        core.freeze_daily(days, hourly, {'2026-10-21': 600}, 1792519000, 'Europe/Berlin')
        self.assertEqual(days, {})
        hourly['2026-10-21T10:00:00+02:00'] = 300
        core.freeze_daily(days, hourly, {'2026-10-21': 600}, 1792519000, 'Europe/Berlin')
        self.assertEqual(days['2026-10-21']['wh'], 600)
        core.freeze_daily(days, {'2026-10-21T09:00:00+02:00': 900},
                          {'2026-10-21': 900}, 1792519500, 'Europe/Berlin')
        self.assertEqual(days['2026-10-21']['wh'], 600)
        late = {}
        core.freeze_daily(late, hourly, {'2026-10-21': 600}, 1792533601, 'Europe/Berlin')
        self.assertEqual(late, {})

    def test_insufficient_or_excluded_days_stay_neutral(self):
        rows = [{'day': f'2026-10-{d:02}', 'forecast_wh': 1000,
                 'actual_wh': 500, 'eligible': True} for d in range(1, 14)]
        self.assertEqual(core.calibrate(rows, '2026-10-21')['factor'], 1000)
        rows.append({'day': '2026-10-14', 'forecast_wh': 1000,
                     'actual_wh': 500, 'eligible': False})
        self.assertEqual(core.calibrate(rows, '2026-10-21')['count'], 13)

    def test_calibration_requires_fourteen_past_days_and_clamps(self):
        rows = [{'day': f'2026-10-{d:02}', 'forecast_wh': 1000,
                 'actual_wh': 1400, 'eligible': True} for d in range(1, 15)]
        rows.append({'day': '2026-10-21', 'forecast_wh': 1000,
                     'actual_wh': 1, 'eligible': True})
        c = core.calibrate(rows, '2026-10-21')
        self.assertEqual((c['factor'], c['count'], c['quality'], c['age_days']),
                         (1200, 14, 'calibrated', 7))
        self.assertEqual(c['raw_factor'], 1.4)

    def test_wire_chunks_are_complete_bounded_and_keep_raw_power(self):
        plan = {'t': 1792566000, 'p': [10000] * 144, 'x': [2] * 144}
        cal = {'factor': 1200, 'count': 14, 'age_days': 1, 'quality': 'calibrated'}
        chunks = core.packetize(plan, 1792566050, 1792500000, cal)
        self.assertEqual(len(chunks), 6)
        for k, payload in enumerate(chunks):
            self.assertLessEqual(len(payload.encode('utf8')), 768)
            packet = json.loads(payload)
            self.assertEqual((packet['k'], packet['n'], packet['p']), (k, 144, [10000] * 24))
            self.assertEqual(packet['g'], packet['r'])
            self.assertEqual(packet['f'], 1200)

    def test_power_offset_preserves_unavailability(self):
        self.assertEqual([core.corrected_power(x) for x in (0, 1, 2, 10, 682)], [0, 0, 0, 8, 680])
        for value in (None, 'unknown', 'unavailable', float('nan'), True):
            self.assertIsNone(core.corrected_power(value))

class QualificationTests(unittest.TestCase):
    def test_bms_charge_disabled_or_alarm_is_excluded(self):
        self.assertTrue(core.bms_allows_charge('on', '0'))
        for enabled, alarms in [('off', '0'), ('on', '1'), ('unknown', '0'),
                                ('on', 'unavailable'), ('on', '-1'), ('on', True)]:
            self.assertFalse(core.bms_allows_charge(enabled, alarms))

    def test_health_requires_fresh_advancing_acquisition_proof(self):
        health = {'bms': {'state': 'ok', 'seq': 12, 'age_ms': 2000, 'updated': 1000},
                  'victron': {'state': 'fresh', 'seq': 22, 'age_ms': 3000, 'updated': 1000}}
        self.assertTrue(core.source_fresh({'bms': 11, 'victron': 21}, health, 1005))
        self.assertFalse(core.source_fresh({}, health, 1005))
        self.assertFalse(core.source_fresh({'bms': 12, 'victron': 22}, health, 1005))
        self.assertFalse(core.source_fresh({'bms': 11, 'victron': 21}, health, 1061))
        health['victron']['age_ms'] = 15001
        self.assertFalse(core.source_fresh({'bms': 11, 'victron': 21}, health, 1005))

    def test_unknown_current_limit_is_not_eligible_proof(self):
        proof = dict.fromkeys(('complete_observation', 'pre_period_forecast', 'reset_free',
                               'outage_free', 'not_full_or_curtailed'), True)
        record = core.qualify_day('2026-10-20', 1000, 900, proof)
        self.assertFalse(record['eligible'])
        self.assertIn('current_limit_unrestricted', record['exclusions'])

    def test_owner_limit_confirmation_cannot_qualify_earlier_observations(self):
        proof = dict.fromkeys(('complete_observation', 'pre_period_forecast', 'reset_free',
                               'outage_free', 'not_full_or_curtailed', 'current_limit_unrestricted'), True)
        proof.update(current_limit_confirmed_from=1792518660, observation_start=1792447200)
        self.assertFalse(core.qualify_day('2026-10-20', 1000, 900, proof)['eligible'])
        proof['observation_start'] = 1792533600
        self.assertTrue(core.qualify_day('2026-10-21', 1000, 900, proof)['eligible'])

    def test_explicit_complete_qualification_can_be_eligible(self):
        proof = dict.fromkeys(('complete_observation', 'pre_period_forecast', 'reset_free',
                               'outage_free', 'not_full_or_curtailed', 'current_limit_unrestricted'), True)
        self.assertTrue(core.qualify_day('2026-10-20', 1000, 900, proof)['eligible'])
        proof['reset_free'] = False
        self.assertFalse(core.qualify_day('2026-10-20', 1000, 900, proof)['eligible'])

if __name__ == '__main__':
    unittest.main()
