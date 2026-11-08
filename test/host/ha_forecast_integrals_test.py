"""Offline regression checks for the curated future DC energy comparison."""
import importlib.util
from pathlib import Path
import unittest
import ast
import sys
import json
import tempfile
from unittest.mock import patch
from types import SimpleNamespace
from datetime import datetime
from zoneinfo import ZoneInfo

sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('core',Path(__file__).resolve().parents[2]/'docs'/'home-assistant'/'sil-planner-shadow-core.py')
core=importlib.util.module_from_spec(spec);spec.loader.exec_module(core)

class IntegralTests(unittest.TestCase):
    def test_partial_edges_and_energy(self):
        result=core.integrate_future(0,900,[1000]*4,450,3150)
        self.assertAlmostEqual(result['kwh'],.75)
        self.assertEqual(result['coveredSeconds'],2700)
    def test_null_is_missing_but_zero_is_known(self):
        result=core.integrate_future(0,900,[0,None,1000],0,3600)
        self.assertEqual(result['coveredSeconds'],1800)
        self.assertEqual(result['missingSeconds'],1800)
        self.assertEqual(result['kwh'],.25)
    def test_partial_horizon_keeps_covered_energy(self):
        result=core.integrate_future(0,900,[1000]*144,123,123+36*3600)
        self.assertAlmostEqual(result['kwh'],36-123/3600,places=6)
        self.assertEqual(result['missingSeconds'],123)
    def test_day_boundaries_follow_dst(self):
        zone=ZoneInfo('Europe/Berlin')
        for day,hours in [('2026-03-29',23),('2026-10-25',25)]:
            start=int(datetime.fromisoformat(day).replace(tzinfo=zone).timestamp())
            result=core.integrate_future(start,900,[1000]*int(hours*4),start,start+hours*3600)
            self.assertEqual(len(result['days']),1)
            self.assertEqual(result['days'][0]['kwh'],hours)
            self.assertTrue(result['days'][0]['fullDay'])
    def test_invalid_values_do_not_become_zero(self):
        result=core.integrate_future(0,900,[True,-1,float('nan'),None],0,3600)
        self.assertIsNone(result['kwh'])
        self.assertEqual(result['coveredSeconds'],0)
    def test_wire_requires_same_complete_snapshot_and_applies_calibration(self):
        start=1793997000
        frame={'v':2,'s':start,'r':start,'t':start,'d':900,'n':2,'k':0,'g':start,'e':start+1800,'f':1100,'c':14,'a':0,'q':'calibrated','o':'forecast_solar','p':[1000,None],'x':[0,1]}
        result=core.pv_snapshot([frame],start+100)
        self.assertEqual(result['values'],[1100,None])
        with self.assertRaises(ValueError):core.pv_snapshot([frame],start+1800)
        with self.assertRaises(ValueError):core.pv_snapshot([dict(frame,n=25)],start+100)
        with self.assertRaises(ValueError):core.pv_snapshot([dict(frame,x=[False,True])],start+100)
    def test_stale_or_wrong_pv_plan_withheld(self):
        plan={'v':1,'valid':True,'g':1000,'r':123,'pv':900,'t':900,'d':900,'n':2,'w':[1000,1000]}
        self.assertIsNone(core.charge_snapshot(plan,1181,900,plan))
        self.assertIsNone(core.charge_snapshot(plan,1000,901,plan))
        self.assertEqual(core.charge_snapshot(plan,1000,900,plan)['values'],[1000,1000])

    def test_all_integrals_share_window_and_independent_plan_validity(self):
        start=1793997000
        frame={'v':2,'s':start,'r':start,'t':start,'d':900,'n':2,'k':0,'g':start,'e':start+1800,'f':1000,'c':0,'a':0,'q':'uncalibrated','o':'forecast_solar','p':[1000,0],'x':[0,0]}
        plan={'v':1,'valid':True,'g':start,'r':123,'pv':start,'t':start,'d':900,'n':2,'w':[500,0]}
        result=core.future_energy([frame],plan,{},start+100,plan,{})
        for name in ('pv','production'):
            self.assertEqual(result[name]['start'],start+100)
            self.assertEqual(result[name]['end'],start+100+129600)
        self.assertIsNone(result['test'])
        self.assertAlmostEqual(result['pv']['kwh'],2*result['production']['kwh'],places=6)

    def test_whole_day_is_only_claimed_with_complete_coverage(self):
        zone=ZoneInfo('Europe/Berlin')
        start=int(datetime(2026,11,7,tzinfo=zone).timestamp())
        day=core.integrate_future(start,900,[0]*96,start,start+86400)['days'][0]
        self.assertTrue(day['fullDay'] and day['complete'])
        self.assertEqual(day['kwh'],0)
        gap=core.integrate_future(start,900,[0]*95,start,start+86400)['days'][0]
        self.assertTrue(gap['fullDay'])
        self.assertFalse(gap['complete'])
        self.assertEqual(gap['missingSeconds'],900)

    def test_charge_requires_own_price_context_not_other_controller(self):
        plan={'v':1,'valid':True,'g':1000,'r':123,'pv':900,'t':900,'d':900,'n':2,'w':[1000,1000]}
        for context in (None,dict(plan,r=124),dict(plan,g=800),dict(plan,pv=901),dict(plan,valid=False)):
            self.assertIsNone(core.charge_snapshot(plan,1000,900,context))
        missing=dict(plan);missing.pop('r')
        self.assertIsNone(core.charge_snapshot(missing,1000,900,plan))

class EnergyLifecycleTests(unittest.TestCase):
    def run_tick(self,withdraw=False,invalid_budget=False):
        """Execute the actual timer with a synthetic HA state store and no transport."""
        root=Path(__file__).resolve().parents[2]
        tree=ast.parse((root/'docs/home-assistant/sil-planner-shadow-pyscript.py').read_text(encoding='utf-8'))
        tick=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='sil_shadow_energy_tick')
        tick.decorator_list=[]
        now=1793997000
        plan={'v':1,'valid':True,'g':now,'r':now-60,'pv':now,'t':now,'d':900,'n':2,'w':[500,0]}
        packet={'v':2,'s':now,'r':now,'t':now,'d':900,'n':2,'k':0,'g':now,'e':now+1800,'f':1000,'c':0,'a':0,'q':'uncalibrated','o':'forecast_solar','p':[1000,0],'x':[0,0]}
        entity=lambda status,attributes:SimpleNamespace(state=status,attributes=attributes)
        store={'sensor.sil_shadow_charge_plan':entity('input_blocked',plan),
               'sensor.sil_shadow_discharge_budget':entity('input_blocked',dict(plan,valid=not invalid_budget)),
               'sensor.sil_shadow_price_input':entity('blocked' if withdraw else 'ready',{'r':now,'lastPublishedAt':now}),
               'sensor.sil_shadow_assessment':entity('ready',{'valid':True,'g':now,'priceRevision':now})}
        result=[]
        ns={'datetime':SimpleNamespace(datetime=SimpleNamespace(now=lambda zone:SimpleNamespace(timestamp=lambda:now)),timezone=SimpleNamespace(utc=None)),
            'task':SimpleNamespace(unique=lambda *args,**kwargs:None),
            'hass':SimpleNamespace(states=SimpleNamespace(get=store.get)),
            'state':SimpleNamespace(set=lambda entity_id,**kwargs:result.append(kwargs)),
            '_shadow_energy_snapshot':lambda prod,test,pc,tc,t:core.future_energy([packet],prod,test,t,pc,tc)}
        exec(compile(ast.fix_missing_locations(ast.Module(body=[tick],type_ignores=[])),'energy_tick','exec'),ns)
        ns['sil_shadow_energy_tick']()
        return result[0]['new_attributes']

    def test_newer_soft_heartbeat_does_not_erase_valid_own_plan(self):
        result=self.run_tick()
        self.assertEqual(result['test']['kwh'],.125)
        self.assertEqual(result['test']['priceRevision'],1793996940)

    def test_withdrawal_or_invalid_own_budget_withholds_energy(self):
        self.assertIsNone(self.run_tick(withdraw=True)['test'])
        self.assertIsNone(self.run_tick(invalid_budget=True)['test'])

class ProductionCacheTests(unittest.TestCase):
    def test_production_reader_never_fetches_or_changes_request_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cache.json'
            cache={'response':{},'fetched_at':1000,'next_request_at':4600,'refresh_failed':False}
            path.write_text(json.dumps(cache));before=path.read_bytes()
            with patch.object(core,'prepare',return_value={'frames':['unchanged']}),patch.object(core,'fetch_provider',side_effect=AssertionError('No extra provider request')):
                result=core.production_from_cache([],{},1100,path)
            self.assertEqual(result['frames'],['unchanged'])
            self.assertEqual(result['providerAgeS'],100)
            self.assertEqual(path.read_bytes(),before)

    def test_production_reader_rejects_expired_and_failed_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cache.json'
            with self.assertRaises(ValueError):core.production_from_cache([],{},5501,path)
            for cache in ({'response':{},'fetched_at':1000},{'response':{},'fetched_at':5500,'refresh_failed':True},
                          {'response':{},'fetched_at':6000},[]):
                path.write_text(json.dumps(cache))
                with self.assertRaises(ValueError):core.production_from_cache([],{},5501,path)

    def test_production_reader_keeps_exact_tariff_alignment_requirement(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'cache.json'
            path.write_text(json.dumps({'response':{},'fetched_at':1000,'next_request_at':4600}))
            with patch.object(core,'prepare',side_effect=ValueError('tariff_alignment_mismatch')):
                with self.assertRaisesRegex(ValueError,'tariff_alignment_mismatch'):
                    core.production_from_cache([],{},1100,path)

class ProductionLifecycleTests(unittest.TestCase):
    def namespace(self,blocked=False,overlap=False):
        root=Path(__file__).resolve().parents[2]
        tree=ast.parse((root/'docs/home-assistant/sil-planner-shadow-pyscript.py').read_text(encoding='utf-8'))
        functions=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in ('_production_withdraw','sil_production_publish_prices','_shadow_withdraw','sil_shadow_publish_prices')]
        for node in functions:node.decorator_list=[]
        now=1793997000;calls=[]
        entity=lambda value,attributes:SimpleNamespace(state=value,attributes=attributes)
        store={'sensor.sil_shadow_price_input':entity('blocked',{}),'sensor.sil_shadow_config':entity('invalid',{}),
               'sensor.sil_shadow_assessment':entity('unavailable',{})}
        def prepared(*args):
            if blocked:raise ValueError('provider_cache_expired')
            return {'frames':['origin-aware raw v2'],'n':288,'r':now,'providerAgeS':20,'originMarket':1,'originForecast':287,'originUnknown':0}
        entry=SimpleNamespace(domain='epex_spot',data={'source':'Energyforecast.de','market_area':'de','token':'synthetic_token'},options={})
        def unique(name,**kwargs):
            self.assertTrue(kwargs.get('kill_me'))
            if overlap:raise RuntimeError('Overlapping invocation cancelled')
        ns={'datetime':SimpleNamespace(datetime=SimpleNamespace(now=lambda zone:SimpleNamespace(timestamp=lambda:now)),timezone=SimpleNamespace(utc=None)),
            'json':json,'task':SimpleNamespace(unique=unique),
            'SOURCE_ENTRY':'synthetic_entry','REFERENCE_ENTITY':'synthetic_reference',
            'PRODUCTION_TOPIC':'SolarLimiter/Input/PriceSchedule','TEST_TOPIC':'SolarLimiterTest/Input/PriceSchedule',
            'hass':SimpleNamespace(config_entries=SimpleNamespace(async_get_entry=lambda _:entry),
                states=SimpleNamespace(get=lambda entity_id:entity('ready',{'data':[]}) if entity_id=='synthetic_reference' else store.get(entity_id)),
                data={'epex_spot':{'synthetic_entry':SimpleNamespace(last_update_success=True)}}),
            'state':SimpleNamespace(set=lambda entity_id,value,new_attributes:store.update({entity_id:entity(value,new_attributes)})),
            'service':SimpleNamespace(call=lambda *args,**kwargs:calls.append(kwargs)),
            '_production_prepare':prepared,'_shadow_prepare':prepared,'_shadow_cache_evidence':lambda now:{}}
        exec(compile(ast.fix_missing_locations(ast.Module(body=functions,type_ignores=[])),'production_lifecycle','exec'),ns)
        return ns,store,calls

    def test_test_offline_or_invalid_does_not_block_production(self):
        ns,store,calls=self.namespace()
        result=ns['sil_production_publish_prices']()
        self.assertEqual(result['published_chunks'],1)
        self.assertEqual(store['sensor.sil_production_price_input'].state,'ready')
        self.assertEqual(calls[0]['topic'],'SolarLimiter/Input/PriceSchedule')
        self.assertEqual(calls[0]['qos'],1)
        self.assertFalse(calls[0]['retain'])
        self.assertEqual(calls[0]['payload'],'origin-aware raw v2')

    def test_production_expiry_withdraws_only_production(self):
        ns,store,calls=self.namespace(blocked=True)
        original=store['sensor.sil_shadow_price_input']
        result=ns['sil_production_publish_prices']()
        self.assertEqual(result['withdrawMessages'],1)
        self.assertIs(store['sensor.sil_shadow_price_input'],original)
        self.assertEqual(calls[0]['topic'],'SolarLimiter/Input/PriceSchedule')
        self.assertEqual(json.loads(calls[0]['payload'])['n'],0)

    def test_test_withdraw_cannot_withdraw_production(self):
        ns,store,calls=self.namespace()
        ns['sil_production_publish_prices']();original=store['sensor.sil_production_price_input'];calls.clear()
        ns['_shadow_withdraw'](1793997001)
        self.assertIs(store['sensor.sil_production_price_input'],original)
        self.assertEqual([call['topic'] for call in calls],['SolarLimiterTest/Input/PriceSchedule'])

    def test_production_overlap_guard_precedes_any_publish(self):
        ns,_,calls=self.namespace(overlap=True)
        with self.assertRaises(RuntimeError):ns['sil_production_publish_prices']()
        self.assertEqual(calls,[])

    def test_existing_cache_maintenance_path_ignores_test_device_health(self):
        ns,store,calls=self.namespace()
        result=ns['sil_shadow_publish_prices']()
        self.assertEqual(result['published_chunks'],1)
        self.assertEqual(store['sensor.sil_shadow_price_input'].state,'ready')
        self.assertEqual(calls[0]['topic'],'SolarLimiterTest/Input/PriceSchedule')

if __name__=='__main__':unittest.main()
