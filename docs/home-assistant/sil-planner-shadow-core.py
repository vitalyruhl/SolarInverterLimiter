"""Pure preparation and bounded provider cache for the isolated SIL shadow feed."""
import datetime as dt
from contextlib import contextmanager
import json
import math
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen

ORIGINS={'market':1,'forecast':2}
REQUEST_INTERVAL=3600
MAX_SOURCE_AGE=4500

@contextmanager
def cache_guard(path):
    """Serialize gate read/write across module reloads; never hold during HTTP."""
    lock_path=Path(str(path)+'.lock')
    lock_path.parent.mkdir(parents=True,exist_ok=True)
    with lock_path.open('a+b') as handle:
        try:
            import fcntl
        except ImportError:
            import msvcrt
            handle.seek(0,2)
            if handle.tell()==0:handle.write(b'0');handle.flush()
            handle.seek(0)
            msvcrt.locking(handle.fileno(),msvcrt.LK_LOCK,1)
            try:yield
            finally:
                handle.seek(0);msvcrt.locking(handle.fileno(),msvcrt.LK_UNLCK,1)
        else:
            fcntl.flock(handle.fileno(),fcntl.LOCK_EX)
            try:yield
            finally:fcntl.flock(handle.fileno(),fcntl.LOCK_UN)

def _persist_cache(path,cache):
    temporary=path.with_suffix('.next.json')
    temporary.write_text(json.dumps(cache,separators=(',',':'),allow_nan=False),encoding='utf-8')
    temporary.replace(path)

def epoch(value):
    if not isinstance(value,str): raise ValueError('provider_timestamp')
    stamp=dt.datetime.fromisoformat(value)
    if stamp.tzinfo is None: raise ValueError('provider_timezone')
    number=stamp.timestamp()
    if not number.is_integer(): raise ValueError('provider_timestamp')
    return int(number)

def number(value):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):
        raise ValueError('provider_price')
    return float(value)

def total_price(raw,options):
    # Match installed EPEX Spot 4.1.0 Marketprice and SourceShell rounding.
    raw=round(number(raw),6)
    absolute=number(options['absolute_surcharge'])
    percentage=number(options['percentage_surcharge'])
    tax=number(options['tax'])
    return round((raw+abs(raw)*percentage/100+absolute)*(1+tax/100),6)

def prepare(response,reference,options,now):
    if options.get('duration')!=15: raise ValueError('provider_resolution')
    rows=response.get('forecast',{}).get('data')
    if not isinstance(rows,list) or not 2<=len(rows)<=8192: raise ValueError('provider_rows')
    start=int(now)//900*900
    selected={}
    for row in rows:
        stamp=epoch(row.get('start'))
        if stamp<start or stamp>=start+288*900: continue
        if stamp%900 or epoch(row.get('end'))!=stamp+900: raise ValueError('provider_resolution')
        if stamp in selected: raise ValueError('provider_duplicate')
        selected[stamp]=row
    references={}
    for row in reference:
        stamp=epoch(row.get('start_time'))
        if stamp in references: raise ValueError('reference_duplicate')
        references[stamp]=row
    prices=[];origins=[]
    for stamp in sorted(selected):
        if stamp!=start+len(prices)*900: raise ValueError('provider_gap')
        row=selected[stamp]
        price=total_price(row.get('price'),options)
        if not -2<=price<=10: raise ValueError('provider_price_range')
        source=references.get(stamp)
        if source is None or epoch(source.get('end_time'))!=stamp+900:
            raise ValueError('tariff_alignment_missing')
        actual=number(source.get('price_per_kwh'))
        if round(actual*1000000)!=round(price*1000000):
            raise ValueError(f'tariff_alignment_mismatch:{stamp}:{actual:.6f}:{price:.6f}')
        prices.append(round(price*1000000))
        origins.append(ORIGINS.get(row.get('price_origin'),0))
    if len(prices)<2: raise ValueError('provider_coverage')
    frames=[]
    for chunk,index in enumerate(range(0,len(prices),24)):
        packet={'v':2,'r':int(now),'t':start,'d':900,'n':len(prices),'k':chunk,
                'p':prices[index:index+24],'o':origins[index:index+24]}
        frames.append(json.dumps(packet,separators=(',',':'),allow_nan=False))
    return {'r':int(now),'t':start,'d':900,'n':len(prices),'p':prices,'o':origins,
            'originUnknown':origins.count(0),'originMarket':origins.count(1),
            'originUnknown36h':origins[:144].count(0),'originMarket36h':origins[:144].count(1),
            'originForecast36h':origins[:144].count(2),
            'originForecast':origins.count(2),'frames':frames,'tariff_aligned':True}

def fetch_provider(token):
    query=urlencode({'token':token,'fixed_cost_cent':0,'vat':0,
                     'resolution':'QUARTER_HOURLY','market_zone':'DE-LU'})
    try:
        with urlopen('https://www.energyforecast.de/api/v1/predictions/prices_for_ha?'+query,timeout=20) as response:
            body=response.read(2000001)
        if len(body)>2000000: raise ValueError('provider_size')
        raw=json.loads(body)
        rows=raw['forecast']['data']
        # Persist only price fields; never persist request URLs or authentication.
        return {'forecast':{'data':[{key:row[key] for key in ('start','end','price','price_origin') if key in row}
                                    for row in rows]}}
    except Exception:
        raise ValueError('provider_request_failed') from None

def cached_provider(token,now,path,fetcher=None):
    path=Path(path)
    path.parent.mkdir(parents=True,exist_ok=True)
    gate=int(now)+REQUEST_INTERVAL
    with cache_guard(path):
        cache=json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
        if cache.get('next_request_at',0)>now:
            if 'response' not in cache or cache.get('refresh_failed'):
                raise ValueError('provider_retry_limited')
            if not 0<=now-cache.get('fetched_at',0)<=MAX_SOURCE_AGE:
                raise ValueError('provider_cache_expired')
            return cache
        # Claim the request before releasing the OS lock, including failed requests.
        cache['next_request_at']=gate
        cache['refresh_failed']=True
        _persist_cache(path,cache)
    try:
        response=(fetcher or fetch_provider)(token)
    except Exception:
        raise ValueError('provider_request_failed') from None
    with cache_guard(path):
        cache=json.loads(path.read_text(encoding='utf-8'))
        if cache.get('next_request_at')!=gate:raise ValueError('provider_request_superseded')
        cache.update(response=response,fetched_at=int(now),refresh_failed=False)
        _persist_cache(path,cache)
    return cache

def run(token,reference,options,now,cache_path):
    cache=cached_provider(token,now,cache_path)
    out=prepare(cache['response'],reference,options,now)
    out['providerFetchedAt']=cache['fetched_at']
    out['providerAgeS']=int(now)-cache['fetched_at']
    out['nextProviderRequestAt']=cache['next_request_at']
    return out

def cache_evidence(path,now):
    path=Path(path)
    if not path.exists(): return {'providerCache':'not_fetched'}
    cache=json.loads(path.read_text())
    rows=cache.get('response',{}).get('forecast',{}).get('data',[])
    selected=[row for row in rows if int(now)//900*900<=epoch(row['start'])<int(now)//900*900+288*900]
    origins=[ORIGINS.get(row.get('price_origin'),0) for row in selected]
    return {'providerFetchedAt':cache.get('fetched_at'),
            'providerAgeS':int(now)-cache.get('fetched_at',int(now)),
            'nextProviderRequestAt':cache.get('next_request_at'),
            'providerRefreshFailed':cache.get('refresh_failed'),
            'originMarket':origins.count(1),'originForecast':origins.count(2),
            'originUnknown':origins.count(0),'providerSlots':len(selected),
            'originUnknown36h':origins[:144].count(0),'originMarket36h':origins[:144].count(1),
            'originForecast36h':origins[:144].count(2)}


def production_from_cache(reference,options,now,path):
    """Consume the shared validated cache without HTTP or request-gate changes."""
    path=Path(path)
    try:
        with cache_guard(path):
            cache=json.loads(path.read_text(encoding='utf-8'))
    except (OSError,ValueError,TypeError):
        raise ValueError('provider_cache_unavailable') from None
    if not isinstance(cache,dict):raise ValueError('provider_cache_unavailable')
    fetched=cache.get('fetched_at')
    if (type(fetched) is not int or not isinstance(cache.get('response'),dict)
        or cache.get('refresh_failed')):raise ValueError('provider_cache_unavailable')
    if not 0<=now-fetched<=MAX_SOURCE_AGE:raise ValueError('provider_cache_expired')
    prepared=prepare(cache['response'],reference,options,now)
    prepared.update(providerFetchedAt=fetched,providerAgeS=int(now)-fetched,
                    nextProviderRequestAt=cache.get('next_request_at'),providerCacheReadOnly=True)
    return prepared


def integrate_future(start, duration, values, now, end, timezone='Europe/Berlin'):
    """Integrate known future DC step intervals, retaining explicit unknown coverage."""
    from zoneinfo import ZoneInfo
    from datetime import datetime, timedelta
    zone=ZoneInfo(timezone)
    days=[]
    cursor=now
    while cursor<end:
        local=datetime.fromtimestamp(cursor,zone)
        midnight=datetime.combine(local.date()+timedelta(days=1),datetime.min.time(),zone)
        stop=min(end,int(midnight.timestamp()))
        covered=0;energy=0.0
        for index,value in enumerate(values):
            seconds=max(0,min(stop,start+(index+1)*duration)-max(cursor,start+index*duration))
            if seconds and not isinstance(value,bool) and isinstance(value,(int,float)) and math.isfinite(value) and value>=0:
                covered+=seconds;energy+=value*seconds/3600000
        full_day=cursor==int(datetime.combine(local.date(),datetime.min.time(),zone).timestamp()) and stop==int(midnight.timestamp())
        days.append({'date':str(local.date()),'start':cursor,'end':stop,'kwh':round(energy,6) if covered else None,
                     'coveredSeconds':covered,'missingSeconds':stop-cursor-covered,'fullDay':full_day,
                     'complete':covered==stop-cursor})
        cursor=stop
    covered=sum(day['coveredSeconds'] for day in days)
    return {'start':now,'end':end,'kwh':round(sum(day['kwh'] or 0 for day in days),6) if covered else None,
            'coveredSeconds':covered,'missingSeconds':end-now-covered,'days':days}


def pv_snapshot(packets,now):
    """Validate one complete producer snapshot; never request or repair forecasts."""
    rows=[json.loads(row) if isinstance(row,str) else row for row in packets]
    if not rows:raise ValueError('pv_missing')
    first=rows[0]
    keys=('v','s','r','t','d','n','g','e','f','c','a','q','o')
    if any(type(first.get(key)) is not int for key in ('v','s','r','t','d','n','g','e','f','c','a')):
        raise ValueError('pv_metadata')
    if (first.get('v')!=2 or first.get('d')!=900 or first.get('o')!='forecast_solar'
        or first.get('q') not in ('calibrated','uncalibrated') or first.get('g')!=first.get('r')
        or not 2<=first.get('n',0)<=144 or not 500<=first.get('f',0)<=1200
        or not first['t']<=now<first['e']<=first['t']+first['n']*900
        or not first['t']<=first['g']<first['t']+900 or first['g']>now+120):
        raise ValueError('pv_invalid_or_expired')
    if (first['t']%900 or not 1704067200<=first['s']<=first['g'] or not 0<=first['c']<=30
        or not 0<=first['a']<=65535 or first['e']<=first['g']
        or first['q']=='uncalibrated' and (first['f']!=1000 or first['c']>=14)
        or first['q']=='calibrated' and first['c']<14):raise ValueError('pv_metadata')
    parts={}
    for row in rows:
        if any(row.get(key)!=first.get(key) for key in keys):raise ValueError('pv_mixed_snapshot')
        k=row.get('k');expected=min(24,first['n']-k*24) if type(k) is int and 0<=k<6 else 0
        if expected<=0 or len(row.get('p',[]))!=expected or len(row.get('x',[]))!=expected:raise ValueError('pv_chunk')
        if k in parts and parts[k]!=row:raise ValueError('pv_conflicting_chunk')
        parts[k]=row
    if set(parts)!=set(range((first['n']+23)//24)):raise ValueError('pv_incomplete')
    values=[]
    for k in sorted(parts):
        for power,quality in zip(parts[k]['p'],parts[k]['x']):
            if type(quality) is not int:raise ValueError('pv_quality')
            if quality==1 and power is None:values.append(None)
            elif quality in (0,2) and type(power) is int and 0<=power<=10000:values.append(power*first['f']/1000)
            else:raise ValueError('pv_quality')
    return {'start':first['t'],'duration':900,'values':values,'revision':first['r'],'generatedAt':first['g'],
            'expiresAt':first['e'],'calibrationFactor':first['f']/1000}


def charge_snapshot(plan,now,pv_revision,price_context=None):
    """Accept only fresh valid DC intent bound to the same published PV snapshot."""
    if any(type(plan.get(key)) is not int for key in ('v','g','r','pv','t','d','n')):return None
    if not isinstance(plan.get('w'),list):return None
    if (plan.get('v')!=1 or plan.get('valid') is not True or not -120<=now-plan.get('g',0)<=180
        or plan.get('pv')!=pv_revision or plan.get('d')!=900 or plan.get('n')!=len(plan.get('w',[]))):return None
    context=price_context or {}
    if (context.get('valid') is not True or type(context.get('g')) is not int
        or not -120<=now-context['g']<=180
        or any(type(context.get(key)) is not int or context[key]!=plan[key] for key in ('r','pv','t','d','n'))):return None
    return {'start':plan['t'],'duration':900,'values':plan['w'],'generatedAt':plan['g'],
            'priceRevision':plan['r'],'pvRevision':plan['pv']}


def future_energy(packets,production,test,now,production_context=None,test_context=None):
    """Use a single common now-to-36h window for shared PV and both DC intents."""
    result={'start':now,'end':now+36*3600,'unit':'kWh DC','planned_not_measured':True}
    try:pv=pv_snapshot(packets,now)
    except (ValueError,KeyError,TypeError):
        result.update(status='unavailable',reason='shared_pv_invalid_or_expired');return result
    result.update(status='ready',pvRevision=pv['revision'],pvGeneratedAt=pv['generatedAt'],
                  pvExpiresAt=pv['expiresAt'],calibrationFactor=pv['calibrationFactor'])
    result['pv']=integrate_future(pv['start'],900,pv['values'],now,result['end'])
    for name,plan,context in [('production',production,production_context),('test',test,test_context)]:
        selected=charge_snapshot(plan,now,pv['revision'],context)
        result[name]=integrate_future(selected['start'],900,selected['values'],now,result['end']) if selected else None
        if selected:
            result[name].update({key:selected[key] for key in ('generatedAt','priceRevision','pvRevision')})
        result[name+'Reason']='current_own_price_and_shared_pv_cohort' if selected else 'invalid_stale_or_unbound_own_price_pv_cohort'
    return result
