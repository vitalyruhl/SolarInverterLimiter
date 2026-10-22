import datetime as dt
from pathlib import Path
import unittest
from zoneinfo import ZoneInfo
import jinja2
import yaml

PACKAGE = yaml.safe_load(Path(__file__).with_name('victron-display.yaml').read_text(encoding='utf-8-sig'))
SENSORS = {row['unique_id']: row for row in PACKAGE['template'][0]['sensor']}
UTC = dt.timezone.utc
LOCAL = ZoneInfo('Europe/Berlin')

class DisplayTests(unittest.TestCase):
    def setUp(self):
        self.clock = dt.datetime(2026, 10, 21, 22, 1, tzinfo=UTC)
        # [MOCKED!] HA states and clock; production YAML templates are rendered unchanged.
        self.states = {'binary_sensor.sbt_victron_availability': 'on', 'sensor.sbt_victron_health': 'fresh',
                       'sensor.sbt_victron_solar_power_w': '42.5', 'sensor.sbt_victron_error_code': '0',
                       'sensor.sbt_victron_yield_today_wh': '0'}
        self.attrs = {('sensor.sbt_victron_health', 'sample_age_ms'): 5000}
        self.env = jinja2.Environment()
        self.env.globals.update(states=lambda entity: self.states.get(entity, 'unknown'),
            is_state=lambda entity, value: self.states.get(entity) == value,
            state_attr=lambda entity, attr: self.attrs.get((entity, attr)),
            is_number=self.numeric, now=lambda: self.clock.astimezone(LOCAL),
            as_timestamp=lambda value: value.timestamp(),
            as_datetime=lambda value: dt.datetime.fromtimestamp(value, UTC),
            as_local=lambda value: value.astimezone(LOCAL))
    @staticmethod
    def numeric(value):
        try: return float(value) == float(value) and abs(float(value)) != float('inf')
        except (ValueError, TypeError): return False
    def render(self, key, field='state'):
        value = SENSORS[key]
        for part in field.split('.'): value = value[part]
        return self.env.from_string(value).render().strip()
    def retained(self, key, value, timestamp):
        entity = 'sensor.sbt_victron_' + key
        self.states[entity] = str(value)
        self.attrs[(entity, 'received_at')] = timestamp
    def test_current_power_and_missing_ble_fallback(self):
        key = 'sbt_victron_solar_power_display_w'
        self.assertEqual(float(self.render(key)), 42.5)
        self.states['binary_sensor.sbt_victron_availability'] = 'off'
        self.assertEqual(float(self.render(key)), 0)
        self.assertIn('No current BLE data', self.render(key, 'attributes.source_status'))
        self.assertEqual(self.states['sensor.sbt_victron_solar_power_w'], '42.5')
    def test_health_expiry_and_old_sample_reject_retained_power(self):
        key = 'sbt_victron_solar_power_display_w'
        self.states['sensor.sbt_victron_health'] = 'unavailable'
        self.assertEqual(float(self.render(key)), 0)
        self.states['sensor.sbt_victron_health'] = 'fresh'
        self.attrs[('sensor.sbt_victron_health', 'sample_age_ms')] = 30001
        self.assertEqual(float(self.render(key)), 0)
    def test_last_error_survives_ble_loss_and_is_explicitly_stale(self):
        self.retained('error_code_last_received', 7, int(self.clock.timestamp()) - 60)
        self.states['binary_sensor.sbt_victron_availability'] = 'off'
        result = self.render('sbt_victron_error_code_display')
        self.assertIn('7 (stale; last received;', result)
        self.assertIn('2026-10-22', result)
    def test_pending_firmware_fallback_only_uses_live_raw(self):
        result = self.render('sbt_victron_error_code_display')
        self.assertIn('0 (current BLE; receipt date unavailable)', result)
        self.states['binary_sensor.sbt_victron_availability'] = 'off'
        self.assertEqual(self.render('sbt_victron_error_code_display'), 'No retained valid error received')
    def test_local_midnight_rejects_previous_day_without_zero(self):
        stamp = int(dt.datetime(2026, 10, 21, 21, 59, tzinfo=UTC).timestamp())
        self.retained('yield_last_received_wh', 1735, stamp)
        self.assertEqual(self.render('sbt_victron_yield_today_display_kwh', 'availability'), 'False')
        result = self.render('sbt_victron_yield_last_received_display')
        self.assertIn('1735 Wh (stale;', result)
        self.assertIn('2026-10-21 23:59:00', result)
        self.clock = dt.datetime(2026, 10, 21, 21, 59, 20, tzinfo=UTC)
        self.assertEqual(self.render('sbt_victron_yield_today_display_kwh', 'availability'), 'True')
        self.assertEqual(float(self.render('sbt_victron_yield_today_display_kwh')), 1.735)
    def test_unknown_and_future_receipt_dates_are_not_today(self):
        for stamp in [None, 0, int(self.clock.timestamp()) + 121]:
            self.retained('yield_last_received_wh', 123, stamp)
            self.assertEqual(self.render('sbt_victron_yield_today_display_kwh', 'availability'), 'False')
            self.assertIn('receipt date unavailable', self.render('sbt_victron_yield_last_received_display'))
    def test_same_day_stale_yield_remains_visible_with_status(self):
        self.retained('yield_last_received_wh', 123, int(self.clock.timestamp()) - 10)
        self.states['binary_sensor.sbt_victron_availability'] = 'unavailable'
        self.assertEqual(self.render('sbt_victron_yield_today_display_kwh', 'availability'), 'True')
        self.assertIn('stale', self.render('sbt_victron_yield_last_received_display'))
        self.assertIn('stale', self.render('sbt_victron_yield_today_display_kwh', 'attributes.source_status'))
    def test_history_helpers_cannot_masquerade_as_control_or_statistics(self):
        for row in PACKAGE['mqtt']['sensor']:
            self.assertNotIn('availability', row)
            self.assertNotIn('expire_after', row)
            self.assertNotIn('state_class', row)
            self.assertIn('/victron/last_valid/', row['state_topic'])
        for row in SENSORS.values(): self.assertNotIn('state_class', row)

if __name__ == '__main__': unittest.main()
