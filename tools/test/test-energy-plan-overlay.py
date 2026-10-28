#!/usr/bin/env python3
"""Verify the single 36-hour preview and complete bounded plan intervals."""
import json
import os
from pathlib import Path
import subprocess
import yaml

ROOT = Path(__file__).resolve().parents[2]
OVERLAY = ROOT / 'docs/home-assistant/sil-energy-plan-overlay.yaml'

def main():
    cards = yaml.safe_load(OVERLAY.read_text(encoding='utf-8'))['cards']
    assert len(cards) == 2 and cards[1]['type'] == 'markdown'
    preview = cards[0]
    assert preview['type'] == 'custom:apexcharts-card'
    assert preview['header']['title'] == 'Test energy plan (36 h)'
    assert preview['graph_span'] == '39h'
    assert preview['span'] == {'start': 'minute', 'offset': '-3h'}
    assert preview['apex_config']['stroke']['width'] == [1, 1, 1, 1]
    assert len(preview['series']) == 4
    assert all(series['stroke_width'] == 1 for series in preview['series'])
    assert [series['yaxis_id'] for series in preview['series']] == [
        'discharge_ac', 'solar_dc', 'solar_dc', 'price']
    assert [axis['id'] for axis in preview['yaxis']] == ['price', 'discharge_ac', 'solar_dc']
    assert all(series['extend_to'] is False for series in preview['series'])
    js = r'''
const assert = require('node:assert/strict');
const generators = JSON.parse(require('node:fs').readFileSync(0,'utf8'));
const RealDate = Date;
let now;
global.Date = class extends RealDate { constructor(...a) { super(...(a.length?a:[now])); } static now(){return now;} };
let cases=0;
// Include both repeated DST-hour offsets and the spring transition.
for (const base of ['2026-10-27T18:00:00+01:00','2026-10-25T02:00:00+02:00','2026-10-25T02:00:00+01:00','2026-03-29T03:00:00+02:00']) {
 for(const minutes of [0,15,30,45,59]) {
  now = new RealDate(base).getTime()+minutes*60000+59123;
  const start = new Date(Math.floor(now/60000)*60000-3*3600000);
  const end = new Date(start.getTime()+39*3600000);
  const t = Math.floor(now/900000)*900;
  const a = {v:1,valid:true,g:Math.floor(now/1000),t,d:900,n:144,kind:'intent_projection',w:Array(144).fill(900),s:Array(144).fill(1200)};
  for(let i=0;i<3;i++) {
   const run = entity => new Function('entity','start','end',generators[i])(entity,start,end);
   let points=run({state:'ready',attributes:a});
   assert.equal(points.length,288);
   assert.equal(points[0][0],now);
   assert.equal(points.at(-1)[0],(t+144*900)*1000);
   assert(points.at(-1)[0]<=end.getTime());
   assert(points.every(p=>p[1]===(i===2?1200:900)));
   assert.deepEqual(run({state:'unavailable',attributes:a}),[]);
   assert.deepEqual(run({state:'ready',attributes:{...a,g:a.g-181}}),[]);
   const zero={...a,w:Array(144).fill(0),s:Array(144).fill(0)};
   assert(run({state:'ready',attributes:zero}).every(p=>p[1]===0));
   if(i>0) {
    const gap={...a,w:[...a.w],s:[...a.s]};gap.w[143]=null;gap.s[143]=null;
    points=run({state:'ready',attributes:gap});
    assert.equal(points.at(-1)[1],null);
    assert.equal(points.at(-1)[0],(t+144*900)*1000);
   }
   cases++;
  }
  const price=new Function('entity','start','end',generators[3]);
  const times=[start.getTime()-1,start.getTime(),end.getTime()-1,end.getTime()];
  assert.deepEqual(price({attributes:{data:times.map(at=>({start_time:new Date(at).toISOString(),price_per_kwh:-0.1}))}},start,end),[[times[1],-0.1],[times[2],-0.1]]);
 }
}
console.log(`PASS: single 36-hour card; ${cases} complete 144-slot cases, DST, zeros, nulls, freshness and price boundaries`);
'''
    result = subprocess.run(['node', '-e', js], input=json.dumps([s['data_generator'] for s in preview['series']]), text=True, env={**os.environ, 'TZ':'Europe/Berlin'}, check=True, capture_output=True)
    print(result.stdout.strip())

if __name__ == '__main__':
    main()
