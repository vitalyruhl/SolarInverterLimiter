"""Check prepared HA schedule examples offline (requires PyYAML and Node.js)."""
import json
import os
from pathlib import Path
import subprocess

import yaml

root = Path(__file__).resolve().parents[2]
card = yaml.safe_load((root / "docs/home-assistant/sil-price-discharge-overlay.yaml").read_text(encoding="utf-8"))
sensor = yaml.safe_load((root / "docs/home-assistant/sil-discharge-budget-sensor.yaml").read_text(encoding="utf-8"))
assert card["graph_span"] == "24h" and card["span"] == {"start": "hour"}
assert len(card["series"]) == 2 and card["series"][1]["entity"] == "sensor.epex_spot_data_total_price_2"
assert card["series"][0]["yaxis_id"] == "discharge_w" and card["yaxis"][1]["opposite"] is True
assert sensor["mqtt"]["sensor"][0]["expire_after"] == 180
assert [x["value"] for x in card["series"][1]["color_threshold"]] == [0, .155, .2, .25, .3, .35, .4, .5]
script = "const budget = " + json.dumps(card["series"][0]["data_generator"]) + ";\n"
script += "const price = " + json.dumps(card["series"][1]["data_generator"]) + ";\n"
script += r"""
const assert = require('node:assert/strict');
let now=Date.parse('2026-10-22T09:07:00Z');
const RealDate=Date;
class MockDate extends RealDate { constructor(...args) { super(...(args.length?args:[now])); } static now(){return now;} }
const run=(source,entity)=>new Function('entity','Date',source)(entity,MockDate);
const slot=Math.floor(now/900000)*900;
const sample={state:'ready',attributes:{v:1,valid:true,g:now/1000,t:slot,d:900,n:4,w:[0,250,650,0]}};
const points=run(budget,sample);
assert.equal(points[0][0],now);assert.equal(points[0][1],0);
assert.equal(points.at(-1)[0],(slot+3600)*1000);assert.equal(points.at(-1)[1],0);assert.equal(points.length,5);
for (const state of ['unavailable','unknown','invalid']) assert.deepEqual(run(budget,{...sample,state}),[]);
for (const attrs of [{valid:false},{w:[]},{w:[0,null,650,0]},{w:[0,-1,650,0]},{g:sample.attributes.g-181}])
  assert.deepEqual(run(budget,{...sample,attributes:{...sample.attributes,...attrs}}),[]);
assert.deepEqual(run(budget,{...sample,attributes:{...sample.attributes,discharge_enabled:false}}),points);
for(const at of ['2026-10-22T09:07:00Z','2026-10-25T01:30:00Z']) {
  now=RealDate.parse(at);
  const s=Math.floor(now/900000)*900;
  const long={state:'ready',attributes:{v:1,valid:true,g:now/1000,t:s,d:900,n:144,w:Array(144).fill(650)}};
  const hour=new MockDate();hour.setMinutes(0,0,0);
  const windowStart=hour.getTime(),windowEnd=windowStart+24*3600000;
  const prices={attributes:{data:Array.from({length:30},(_,i)=>({start_time:new RealDate(windowStart+(i-1)*3600000).toISOString(),price_per_kwh:i/100}))}};
  const pp=run(price,prices),bb=run(budget,long);
  assert.equal(pp.length,24);assert.equal(pp[0][0],windowStart);assert.equal(pp.at(-1)[0],windowEnd-3600000);
  assert.equal(bb.at(-1)[0],windowEnd);assert(bb.every(p=>p[0]>=windowStart&&p[0]<=windowEnd));
}
console.log('[MOCKED!] HA YAML/generators passed: partial slot, zero, invalid/missing/stale/unavailable, MOS independence, coverage, 144 slots, 24h/DST alignment.');
"""
env = os.environ.copy()
env["TZ"] = "Europe/Berlin"
subprocess.run(["node", "-e", script], check=True, env=env)
