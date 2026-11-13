"""Separate production/test price lifecycles and read-only comparison entities.

Install as /config/pyscript/sil_planner_shadow.py with the core module beside
/config/pyscript/modules/sil_planner_shadow_core.py. Production publishes only
its explicit dedicated v2 input topic; existing shared v1 publishers are untouched.
"""
import datetime
import json

SOURCE_ENTRY='d1df01d001352968344ba5b5c6a9ad23'
REFERENCE_ENTITY='sensor.epex_spot_data_total_price_2'
TEST_TOPIC='SolarLimiterTest/Input/PriceSchedule'
PRODUCTION_TOPIC='SolarLimiter/Input/PriceSchedule'
REVISION_GRACE_S=60
OBSERVERS={
    'SolarLimiterTest/Planning/DischargeBudget':'sensor.sil_shadow_discharge_budget',
    'SolarLimiterTest/Planning/ChargePlan':'sensor.sil_shadow_charge_plan',
    'SolarLimiterTest/Planning/Config':'sensor.sil_shadow_config',
    'SolarLimiterTest/Planning/State':'sensor.sil_shadow_state',
    'SolarLimiterTest/Planning/Assessment':'sensor.sil_shadow_assessment',
    'SolarLimiterTest/System-Info/ESP':'sensor.sil_shadow_runtime',
    'SolarLimiter/Planning/State':'sensor.sil_comparison_production_state',
    'SolarLimiter/Planning/Config':'sensor.sil_comparison_production_config',
    'SolarLimiter/Planning/Assessment':'sensor.sil_comparison_production_assessment',
}

@pyscript_executor
def _production_prepare(reference,options,now):
    import importlib.util
    spec=importlib.util.spec_from_file_location('sil_production_price_core','/config/pyscript/modules/sil_planner_shadow_core.py')
    core=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    return core.with_last_good(
        lambda:core.production_from_cache(reference,options,now,'/config/data/sil_planner_shadow/provider-cache.json'),
        now,'/config/data/sil_planner_shadow/last-good-production.json')

def _held_attributes(prepared):
    """Expose only the fixed reason code of a held snapshot."""
    error=prepared.pop('heldError','')
    if prepared.get('held'):
        prepared['heldReason']=error.split(':')[0] if error.startswith(('provider_','tariff_','reference_','source_')) else 'adapter_failed'
    return error

def _production_withdraw(now):
    frame={'v':2,'r':now,'t':now//900*900,'d':900,'n':0,'k':0,'p':[],'o':[]}
    sent=False
    try:
        service.call('mqtt','publish',topic=PRODUCTION_TOPIC,payload=json.dumps(frame,separators=(',',':')),
                     qos=1,retain=False,blocking=True)
        sent=True
    except Exception:
        pass
    return {'withdrawMessages':1 if sent else 0,'withdrawSent':sent,'withdrawRevision':now}

@service(supports_response='optional')
def sil_production_publish_prices():
    """Publish production v2 from the shared cache; never depend on test telemetry."""
    task.unique('sil_production_publish_prices',kill_me=True)
    now=int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    try:
        entry=hass.config_entries.async_get_entry(SOURCE_ENTRY)
        if entry is None or entry.domain!='epex_spot' or entry.data.get('source')!='Energyforecast.de' or entry.data.get('market_area')!='de':
            raise ValueError('source_identity')
        reference=hass.states.get(REFERENCE_ENTITY)
        if reference is None or reference.state in ('unknown','unavailable'):
            raise ValueError('reference_unavailable')
        source=hass.data.get('epex_spot',{}).get(SOURCE_ENTRY)
        if source is None or not source.last_update_success:raise ValueError('reference_coordinator')
        prepared=_production_prepare(list(reference.attributes.get('data',[])),dict(entry.options),now)
        _held_attributes(prepared)
        for payload in prepared['frames']:
            service.call('mqtt','publish',topic=PRODUCTION_TOPIC,payload=payload,qos=1,retain=False,blocking=True)
        attributes={key:value for key,value in prepared.items() if key!='frames'}
        attributes.update(friendly_name='SIL production raw price input',source='Energyforecast.de / DE-LU',
                          raw_tariff_unchanged=True,lastPublishedAt=now,production_only=True,
                          origin_codes='0 unknown / 1 market / 2 forecast')
        state.set('sensor.sil_production_price_input',value='ready',new_attributes=attributes)
        return {'published_chunks':len(prepared['frames']),'slots':prepared['n'],'revision':prepared['r'],
                'tariff_aligned':True,'providerAgeS':prepared.get('providerAgeS'),'providerCacheReadOnly':True,
                'held':prepared.get('held',False)}
    except Exception as error:
        reason=str(error) if isinstance(error,ValueError) and str(error).startswith(('provider_','tariff_','reference_','source_')) else 'production_adapter_failed'
        # Detailed prices are private evidence; only a fixed reason is exposed here.
        reason=reason.split(':')[0]
        attributes={'friendly_name':'SIL production raw price input','production_only':True,'reason':reason,'checkedAt':now}
        attributes.update(_production_withdraw(now))
        state.set('sensor.sil_production_price_input',value='blocked',new_attributes=attributes)
        return {'published_chunks':0,'reason':reason,'withdrawMessages':attributes['withdrawMessages'],
                'withdrawSent':attributes['withdrawSent']}

@time_trigger('startup','period(now + 45 sec, 1 min)')
def sil_production_price_tick(**kwargs):
    sil_production_publish_prices()

@pyscript_executor
def _shadow_prepare(token,reference,options,now):
    import importlib.util
    spec=importlib.util.spec_from_file_location('sil_planner_shadow_core','/config/pyscript/modules/sil_planner_shadow_core.py')
    core=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    return core.with_last_good(
        lambda:core.run(token,reference,options,now,'/config/data/sil_planner_shadow/provider-cache.json'),
        now,'/config/data/sil_planner_shadow/last-good-test.json')

@pyscript_executor
def _shadow_cache_evidence(now):
    import importlib.util
    spec=importlib.util.spec_from_file_location('sil_planner_shadow_core','/config/pyscript/modules/sil_planner_shadow_core.py')
    core=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    return core.cache_evidence('/config/data/sil_planner_shadow/provider-cache.json',now)

@pyscript_executor
def _shadow_energy_snapshot(production,test,production_context,test_context,now):
    import importlib.util
    import json
    from pathlib import Path
    spec=importlib.util.spec_from_file_location('sil_planner_shadow_energy_core','/config/pyscript/modules/sil_planner_shadow_core.py')
    core=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(core)
    try:
        packets=json.loads(Path('/config/data/shed_forecast/last-packets.json').read_bytes())
    except Exception:
        return {'status':'unavailable','reason':'shared_pv_snapshot_unavailable','start':now,'end':now+129600}
    try:
        return core.future_energy(packets,production,test,now,production_context,test_context)
    except Exception:
        return {'status':'unavailable','reason':'energy_snapshot_invalid','start':now,'end':now+129600}

@time_trigger('startup','period(now + 35 sec, 1 min)')
def sil_shadow_energy_tick(**kwargs):
    """Read existing forecast artifacts only; never fetch providers or publish MQTT."""
    task.unique('sil_shadow_energy_tick',kill_me=True)
    now=int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    production=hass.states.get('sensor.solarlimiter_charge_plan')
    test=hass.states.get('sensor.sil_shadow_charge_plan')
    production_context=hass.states.get('sensor.solarlimiter_discharge_budget')
    test_context=hass.states.get('sensor.sil_shadow_discharge_budget')
    source=hass.states.get('sensor.sil_shadow_price_input')
    assessment=hass.states.get('sensor.sil_shadow_assessment')
    test_current=(source is not None and source.state=='ready' and 0<=now-source.attributes.get('lastPublishedAt',0)<=180
                  and assessment is not None and assessment.attributes.get('valid') is True
                  and -120<=now-assessment.attributes.get('g',0)<=180)
    prod_data=dict(production.attributes) if production and production.state not in ('unknown','unavailable','invalid','stale') else {}
    # Display plans retain their own valid cohort across soft price heartbeats.
    # The orange assessed-price curve keeps its separate strict latest-input binding.
    test_data=dict(test.attributes) if test and test_current else {}
    prod_context=dict(production_context.attributes) if production_context and production_context.state not in ('unknown','unavailable','invalid','stale') else {}
    test_context_data=dict(test_context.attributes) if test_context and test_current else {}
    result=_shadow_energy_snapshot(prod_data,test_data,prod_context,test_context_data,now)
    result.update(friendly_name='SIL forecast DC energy comparison',test_comparison=True,
                  source='Existing shared PV wire snapshot and DC charge intents')
    state.set('sensor.sil_comparison_future_energy',value=result['status'],new_attributes=result)

def _shadow_input_matches(payload,now):
    source=hass.states.get('sensor.sil_shadow_price_input')
    if source is None or source.state!='ready': return False
    attributes=source.attributes
    revision=payload.get('priceRevision',payload.get('r',payload.get('revision')))
    age=now-attributes.get('lastPublishedAt',0)
    if not 0<=age<=180: return False
    # Every tick re-issues prices with a new revision; a plan computed just before
    # the new frames arrived still carries the immediately preceding revision.
    return revision==attributes.get('r') or (revision is not None and revision==attributes.get('previousR')
                                             and age<=REVISION_GRACE_S)

def _shadow_withdraw(now):
    frame={'v':2,'r':now,'t':now//900*900,'d':900,'n':0,'k':0,'p':[],'o':[]}
    sent=False
    try:
        service.call('mqtt','publish',topic=TEST_TOPIC,payload=json.dumps(frame,separators=(',',':')),
                     qos=1,retain=False,blocking=True)
        sent=True
    except Exception:
        pass
    for entity_id in ('sensor.sil_shadow_discharge_budget','sensor.sil_shadow_charge_plan','sensor.sil_shadow_assessment'):
        entity=hass.states.get(entity_id)
        attributes=dict(entity.attributes) if entity else {}
        attributes['inputBlockedAt']=now
        state.set(entity_id,value='input_blocked',new_attributes=attributes)
    return {'withdrawMessages':1 if sent else 0,'withdrawSent':sent,'withdrawRevision':now}

@service(supports_response='optional')
def sil_shadow_publish_prices():
    """Publish only the isolated v2 topic after exact existing-tariff alignment."""
    task.unique('sil_shadow_publish_prices',kill_me=True)
    now=int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    try:
        entry=hass.config_entries.async_get_entry(SOURCE_ENTRY)
        if entry is None or entry.domain!='epex_spot' or entry.data.get('source')!='Energyforecast.de' or entry.data.get('market_area')!='de':
            raise ValueError('source_identity')
        reference=hass.states.get(REFERENCE_ENTITY)
        if reference is None or reference.state in ('unknown','unavailable'):
            raise ValueError('reference_unavailable')
        source=hass.data.get('epex_spot',{}).get(SOURCE_ENTRY)
        if source is None or not source.last_update_success:
            raise ValueError('reference_coordinator')
        prepared=_shadow_prepare(entry.data['token'],list(reference.attributes.get('data',[])),dict(entry.options),now)
        held_error=_held_attributes(prepared)
        parts=held_error.split(':')
        if prepared.get('held') and len(parts)==4 and parts[0]=='tariff_alignment_mismatch':
            prepared['firstMismatch']={'startUTC':int(parts[1]),'haTariff':float(parts[2]),'providerTariff':float(parts[3])}
        for payload in prepared['frames']:
            service.call('mqtt','publish',topic=TEST_TOPIC,payload=payload,qos=1,retain=False,blocking=True)
        attributes={key:value for key,value in prepared.items() if key!='frames'}
        attributes.update({'friendly_name':'SIL shadow raw price input','source':'Energyforecast.de / DE-LU',
            'origin_codes':'0 unknown / 1 market / 2 forecast','raw_tariff_unchanged':True,
            'test_only':True,'maxProviderRequestsPerDay':24,'lastPublishedAt':now})
        previous=hass.states.get('sensor.sil_shadow_price_input')
        if previous is not None and previous.state=='ready' and previous.attributes.get('r')!=prepared['r']:
            attributes['previousR']=previous.attributes.get('r')
        state.set('sensor.sil_shadow_price_input',value='ready',new_attributes=attributes)
        return {'published_chunks':len(prepared['frames']),'slots':prepared['n'],
                'originMarket':prepared['originMarket'],'originForecast':prepared['originForecast'],
                'originUnknown':prepared['originUnknown'],'providerAgeS':prepared.get('providerAgeS'),
                'tariff_aligned':True,'revision':prepared['r'],'held':prepared.get('held',False)}
    except Exception as error:
        # Core reasons are fixed codes; never expose URLs, token values or provider bodies.
        reason=str(error) if isinstance(error,ValueError) and str(error).startswith(('provider_','tariff_','reference_','source_')) else 'adapter_failed'
        evidence=_shadow_cache_evidence(now)
        parts=reason.split(':')
        if len(parts)==4 and parts[0]=='tariff_alignment_mismatch':
            evidence['firstMismatch']={'startUTC':int(parts[1]),'haTariff':float(parts[2]),'providerTariff':float(parts[3])}
            reason=parts[0]
        evidence.update({'friendly_name':'SIL shadow raw price input','test_only':True,'reason':reason,'checkedAt':now})
        evidence.update(_shadow_withdraw(now))
        state.set('sensor.sil_shadow_price_input',value='blocked',new_attributes=evidence)
        return {'published_chunks':0,'reason':reason,'withdrawMessages':evidence['withdrawMessages'],
                'withdrawSent':evidence['withdrawSent'],'evidence':evidence}

@time_trigger('startup','period(now + 20 sec, 1 min)')
def sil_shadow_tick(**kwargs):
    now=int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    source=hass.states.get('sensor.sil_shadow_price_input')
    if source is not None and source.state=='ready' and now-source.attributes.get('lastPublishedAt',0)>180:
        withdrawal=_shadow_withdraw(now)
        attributes=dict(source.attributes)
        attributes.update(withdrawal)
        attributes['reason']='adapter_heartbeat_expired'
        state.set('sensor.sil_shadow_price_input',value='stale',new_attributes=attributes)
    for topic,entity_id in OBSERVERS.items():
        entity=hass.states.get(entity_id)
        if entity is None:
            state.set(entity_id,value='unavailable',new_attributes={'test_comparison':True,'source_topic':topic})
        elif entity.state=='ready' and now-entity.attributes.get('received_at',0)>180:
            state.set(entity_id,value='stale',new_attributes=dict(entity.attributes))
    sil_shadow_publish_prices()

@mqtt_trigger('SolarLimiterTest/Planning/+')
@mqtt_trigger('SolarLimiterTest/System-Info/ESP')
@mqtt_trigger('SolarLimiter/Planning/State')
@mqtt_trigger('SolarLimiter/Planning/Config')
@mqtt_trigger('SolarLimiter/Planning/Assessment')
def sil_shadow_observe(topic=None,payload_obj=None,retain=False,**kwargs):
    """Observe only the explicit diagnostic allowlist; never publish from a trigger."""
    entity_id=OBSERVERS.get(topic)
    if entity_id is None or not isinstance(payload_obj,dict): return
    now=int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    attributes=dict(payload_obj)
    attributes.update({'source_topic':topic,'received_at':now,'retained':retain,
                       'test_comparison':True,'friendly_name':entity_id.split('.')[1].replace('_',' ')})
    status='retained' if retain else 'ready'
    if 'g' in payload_obj:
        generated=payload_obj.get('g')
        if isinstance(generated,bool) or not isinstance(generated,int) or not -120<=now-generated<=180:
            status='stale'
    if payload_obj.get('valid') is False: status='invalid'
    if topic in ('SolarLimiterTest/Planning/DischargeBudget','SolarLimiterTest/Planning/ChargePlan','SolarLimiterTest/Planning/Assessment'):
        if not _shadow_input_matches(payload_obj,now): status='input_blocked'
    state.set(entity_id,value=status,new_attributes=attributes)
    if topic=='SolarLimiterTest/Planning/ChargePlan':
        sil_shadow_energy_tick()
