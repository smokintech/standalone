#pragma once
// The phone settings page. One self-contained HTML document: no external
// scripts, fonts or images (the phone has no internet while it is on the
// gauge's Wi-Fi). Values are set with .value / .textContent only - nothing from
// the gauge is ever inserted as HTML.
#include <pgmspace.h>

static const char OIL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>370Z Gauge</title>
<style>
:root{--bg:#0d0d0f;--card:#17171b;--line:#2a2a31;--txt:#e8e8ee;--dim:#8a8a94;--ok:#1db954;--warn:#f5a623;--bad:#e2213b}
@media (prefers-color-scheme:light){:root{--bg:#f2f2f5;--card:#fff;--line:#d8d8e0;--txt:#17171b;--dim:#666670}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:16px/1.4 system-ui,-apple-system,sans-serif;padding:12px 16px 40px;max-width:560px;margin-inline:auto}
h1{font-size:20px;margin:6px 0 2px}h2{font-size:15px;margin:0 0 10px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em}
h3{font-size:14px;margin:16px 0 4px;color:var(--dim)}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
details.card>summary{cursor:pointer;list-style:none;font-size:15px;color:var(--dim);text-transform:uppercase;letter-spacing:.06em;font-weight:600}
details.card>summary::-webkit-details-marker{display:none}
details.card>summary::after{content:"\25BE";float:right}
details.card[open]>summary{margin-bottom:10px}
.sub{color:var(--dim);font-size:13px}
.tiles{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.tile{background:var(--bg);border:1px solid var(--line);border-radius:10px;padding:8px 6px;text-align:center;min-width:0}
.tile.off{opacity:.35}
.tile .k{font-size:12px;color:var(--dim);letter-spacing:.06em}
.tile .v{font-size:34px;font-weight:700;line-height:1.15;font-variant-numeric:tabular-nums}
.tile .u{font-size:13px;color:var(--dim)}
.tile .s{font-size:12px;min-height:1.2em;font-weight:600}
.row{display:grid;grid-template-columns:1fr 150px;gap:8px;align-items:center;margin:8px 0}
.row.wide{grid-template-columns:1fr}
.row label{font-size:15px}.row small{display:block;color:var(--dim);font-size:12px}
input,select,button{font:inherit;color:inherit;background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:9px 10px;width:100%}
input[type=file]{padding:7px}
input[type=range]{padding:0;height:34px}
button{background:var(--ok);color:#06210f;border:0;font-weight:700;cursor:pointer}
button.sec{background:var(--bg);color:var(--txt);border:1px solid var(--line);font-weight:500}
button.dng{background:var(--bad);color:#fff}
button:disabled{opacity:.5}
select.bad{border-color:var(--bad)}
.btns{display:flex;gap:8px;margin-top:12px}.btns button{flex:1}
.msg{min-height:1.3em;margin-top:8px;font-size:14px}.ok{color:var(--ok)}.err{color:var(--bad)}
.active{border-color:var(--ok)}
[hidden]{display:none!important}
.note{font-size:13px;color:var(--dim);margin:6px 0}
.pv{font-size:13px;color:var(--dim);margin:6px 0;font-variant-numeric:tabular-nums}
progress{width:100%;height:10px;margin-top:8px}
</style></head><body>
<h1>370Z Gauge</h1>
<div class="sub" id="info">&nbsp;</div>

<div class="card"><h2>Live</h2>
 <div class="tiles">
  <div class="tile" id="t_o"><div class="k">OIL</div><div class="v" id="v_o">--</div><div class="u" id="u_o">psi</div><div class="s" id="s_o"></div></div>
  <div class="tile" id="t_b"><div class="k">BOOST</div><div class="v" id="v_b">--</div><div class="u" id="u_b">psi</div><div class="s" id="s_b"></div></div>
  <div class="tile" id="t_a"><div class="k">AFR</div><div class="v" id="v_a">--</div><div class="u">&nbsp;</div><div class="s" id="s_a"></div></div>
 </div>
 <div class="sub" style="margin-top:8px"><span id="sensor">--</span> &middot; <span id="armed"></span></div>
</div>

<div class="card"><h2>Demo</h2>
 <div class="note">Shows made-up readings for oil, boost and AFR on the gauge's screens - every screen, light and alert - with no engine running. The gauge shows DEMO while it runs. It stops by itself after two minutes, and a real low-oil alarm ends it at once. The numbers above stay the real sensors.</div>
 <div class="btns"><button type="button" id="demo_btn" onclick="demoToggle()">Run demo</button></div>
 <div class="msg" id="m_demo"></div>
</div>

<form id="f" onsubmit="return saveSettings(event)">

<div class="card"><h2>Screens</h2>
 <div class="note">Swipe left / right on the gauge to move between these. A split screen shows two gauges, one above the other. Leave a slot unused to skip it.</div>
 <div class="row"><label>Screen 1</label><select data-f="page1" class="pgsel"></select></div>
 <div class="row"><label>Screen 2</label><select data-f="page2" class="pgsel"></select></div>
 <div class="row"><label>Screen 3</label><select data-f="page3" class="pgsel"></select></div>
 <div class="row"><label>Screen 4</label><select data-f="page4" class="pgsel"></select></div>
 <div class="msg err" id="m_scr"></div>
</div>

<div class="card"><h2>Inputs</h2>
 <div class="row"><label>Board</label><select data-f="sensor_mode" id="sensor_mode" onchange="modeUi(false)"><option value="0">Auto-detect</option><option value="1">M5 voltmeter (oil only)</option><option value="2">INA3221</option><option value="3">ADS1115</option><option value="4">M5 PaHUB</option><option value="5">M5 PbHUB</option></select></div>
 <div class="note" id="n_board"></div>
 <div class="row" id="r_addr"><label>Address<small>as set on the board</small></label><select data-f="hub_addr" id="hub_addr"></select></div>
 <div class="row"><label id="l_oil">Oil pressure sender</label><select data-f="ina_channel" id="ina_channel"></select></div>
 <div class="row"><label id="l_boost">Boost sensor</label><select data-f="boost_ch" id="boost_ch" onchange="refreshScreens()"></select></div>
 <div class="row"><label id="l_afr">AFR controller</label><select data-f="afr_ch" id="afr_ch" onchange="refreshScreens()"></select></div>
 <div class="row" id="r_div"><label>Input divider<small>sender volts = measured volts &times; this; 1 = none</small></label><input data-f="in_div" type="number" step="any" inputmode="decimal"></div>
 <div class="row" id="r_pb"><label>PbHUB reading<small>what its analog number means</small></label><select data-f="pb_adc" id="pb_adc"><option value="0">10-bit, 0&ndash;5 V</option><option value="1">12-bit, 0&ndash;3.3 V</option><option value="2">12-bit, 0&ndash;5 V</option></select></div>
</div>

<div class="card"><h2>Display &amp; alerts</h2>
 <div class="row"><label>Units<small>oil and boost; tap the gauge to switch</small></label><select data-f="units_bar"><option value="0">psi</option><option value="1">bar</option></select></div>
 <div class="row"><label>Backlight <b id="bl_v"></b>%<small>applies when you let go</small></label><input data-f="backlight" id="backlight" type="range" min="5" max="100" step="5"></div>
 <div class="row"><label>Oil warning below (psi)<small>amber</small></label><input data-f="warn_psi" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Oil critical below (psi)<small>red + alert</small></label><input data-f="crit_psi" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Oil alert arms at (psi)<small>engine-has-run level</small></label><input data-f="arm_psi" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Buzzer<small>low oil pressure only</small></label><select data-f="buzzer"><option value="1">On</option><option value="0">Off</option></select></div>
 <div class="row"><label>Wi-Fi auto-off (min)<small>0 = never</small></label><input data-f="wifi_auto_off_min" type="number" min="0" max="120" step="1" inputmode="numeric"></div>
</div>

<details class="card" id="d_boost"><summary>Boost sensor</summary>
 <div class="note" id="n_boost"></div>
 <div class="row wide"><label>Sensor type<small>choose a preset, or Custom</small></label><select data-f="boost_preset" id="boost_preset" onchange="applyPreset('boost')"></select></div>
 <div class="note">Presets are published or stated ranges, not a guarantee for your part number: check them against your sensor's datasheet. With the engine off the reading below should be about 101 kPa.</div>
 <div class="row"><label>Reads<small>absolute (MAP) or gauge pressure</small></label><select data-f="boost_abs" id="boost_abs" onchange="curveEdited('boost')"><option value="1">Absolute (MAP)</option><option value="0">Gauge pressure</option></select></div>
 <div class="row"><label>Point 1: volts</label><input data-f="boost_v1" type="number" step="any" oninput="curveEdited('boost')"></div>
 <div class="row"><label>Point 1: kPa</label><input data-f="boost_kpa1" type="number" step="any" oninput="curveEdited('boost')"></div>
 <div class="row"><label>Point 2: volts</label><input data-f="boost_v2" type="number" step="any" oninput="curveEdited('boost')"></div>
 <div class="row"><label>Point 2: kPa</label><input data-f="boost_kpa2" type="number" step="any" oninput="curveEdited('boost')"></div>
 <div class="pv" id="pv_boost">--</div>
 <div class="row"><label>Atmosphere (kPa)<small>absolute sensors only</small></label><input data-f="boost_atm_kpa" type="number" step="any" inputmode="decimal"></div>
 <div class="btns" style="margin-top:4px"><button type="button" class="sec" onclick="zeroBoost()">Zero boost now (engine off)</button></div>
 <div class="msg" id="m_zero"></div>
 <div class="row"><label>Boost warning at (psi)<small>amber</small></label><input data-f="boost_warn_psi" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Boost critical at (psi)<small>red</small></label><input data-f="boost_crit_psi" type="number" step="any" inputmode="decimal"></div>
</details>

<details class="card" id="d_afr"><summary>AFR controller</summary>
 <div class="note" id="n_afr"></div>
 <div class="row wide"><label>Controller<small>choose a preset, or Custom</small></label><select data-f="afr_preset" id="afr_preset" onchange="applyPreset('afr')"></select></div>
 <div class="note">Match this to the analogue output your controller is set to (see its manual). A wrong curve gives a wrong AFR with no warning.</div>
 <div class="row"><label>Point 1: volts</label><input data-f="afr_v1" type="number" step="any" oninput="curveEdited('afr')"></div>
 <div class="row"><label>Point 1: AFR</label><input data-f="afr_afr1" type="number" step="any" oninput="curveEdited('afr')"></div>
 <div class="row"><label>Point 2: volts</label><input data-f="afr_v2" type="number" step="any" oninput="curveEdited('afr')"></div>
 <div class="row"><label>Point 2: AFR</label><input data-f="afr_afr2" type="number" step="any" oninput="curveEdited('afr')"></div>
 <div class="pv" id="pv_afr">--</div>
 <div class="row"><label>Stoichiometric AFR<small>marked on the dial</small></label><input data-f="afr_stoich" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Lean warning above<small>amber, under load only</small></label><input data-f="afr_lean_warn" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Lean critical above<small>red, under load only</small></label><input data-f="afr_lean_crit" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>Rich shown below<small>blue, information only</small></label><input data-f="afr_rich_info" type="number" step="any" inputmode="decimal"></div>
 <div class="row"><label>"Under load" at boost of (psi)<small>manifold pressure; vacuum is negative; -15 = always</small></label><input data-f="afr_gate_psi" type="number" step="any" inputmode="decimal"></div>
 <div class="note">The lean limits only apply while the boost sensor reads at or above that pressure, because lean is normal at cruise. With no working boost sensor they stay off, unless this is set to -15.</div>
</details>

<details class="card"><summary>Calibration</summary>
 <div class="sub">Compare the live volts with a multimeter on the sender wire. Changes apply when you press Save. The board's scale and offset apply to all its inputs.</div>
 <div class="row"><label>Meter reading (V)<small>on the oil sender wire; "Match" fixes the active board's scale</small></label><div style="display:flex;gap:6px"><input id="meter" type="number" step="any" inputmode="decimal"><button type="button" class="sec" style="width:auto" onclick="matchMeter()">Match</button></div></div>
 <div id="g_adc"><div class="row"><label>ADS1115 / PbHUB scale</label><input data-f="adc_scale" type="number" step="any"></div>
 <div class="row"><label>ADS1115 / PbHUB offset (V)</label><input data-f="adc_offset" type="number" step="any"></div></div>
 <div id="g_m5"><div class="row"><label>M5 scale</label><input data-f="m5_scale" type="number" step="any"></div>
 <div class="row"><label>M5 offset (V)</label><input data-f="m5_offset" type="number" step="any"></div></div>
 <div id="g_ina"><div class="row"><label>INA3221 scale</label><input data-f="ina_scale" type="number" step="any"></div>
 <div class="row"><label>INA3221 offset (V)</label><input data-f="ina_offset" type="number" step="any"></div></div>
 <h3>Oil pressure sender</h3>
 <div class="row"><label>Volts at 0 bar</label><input data-f="v_zero" type="number" step="any"></div>
 <div class="row"><label>Volts at full scale</label><input data-f="v_full" type="number" step="any"></div>
 <div class="row"><label>Full scale (bar)</label><input data-f="bar_full" type="number" step="any"></div>
 <div class="row"><label>Pressure scale<small>trim vs a mechanical gauge</small></label><input data-f="sender_scale" type="number" step="any"></div>
 <div class="row"><label>Pressure offset (psi)</label><input data-f="sender_offset" type="number" step="any"></div>
</details>
<div class="btns"><button type="submit" id="save">Save settings</button><button type="button" class="sec" onclick="resetDefaults()">Defaults</button></div>
<div class="msg" id="m1"></div>
</form>

<div class="card" id="c_lay"><h2>Designed screens</h2>
 <div class="sub">Screens you designed in the layout editor. Export the layouts file from the editor, choose it here and upload. The gauge checks it first and keeps the old one if the new file is not accepted. Once uploaded, the views appear in the Screens list above.</div>
 <div class="note" id="lay_state">--</div>
 <ul id="lay_list" style="margin:6px 0 0 18px;padding:0;font-size:14px"></ul>
 <div id="lay_ctl">
  <div class="row" style="grid-template-columns:1fr"><input type="file" id="layf" accept=".json,application/json"></div>
  <div class="btns"><button type="button" id="layb" onclick="layUpload()">Upload layout</button><button type="button" class="sec" id="layc" onclick="layClear()">Remove</button></div>
  <div class="sub" style="margin-top:8px" id="lay_dl"><a href="/api/layout.json" style="color:var(--txt)">Download the layout on the gauge</a></div>
 </div>
 <div class="msg" id="m_lay"></div>
</div>

<div class="card"><h2>Wi-Fi</h2>
 <div class="sub">Currently: <b id="cur_ssid">--</b>. After saving, the gauge restarts its Wi-Fi in a few seconds and you must rejoin with the new details. Forgotten the password? It is not shown on the gauge. Hold a finger on the dial for 5 s and press RESET to put the name and password back to the factory ones.</div>
 <div class="row"><label>New name</label><input id="ssid" maxlength="32" autocomplete="off" autocapitalize="off"></div>
 <div class="row"><label>New password<small>8 to 63 characters, blank = keep</small></label><input id="pass" maxlength="63" autocomplete="off" autocapitalize="off"></div>
 <div class="btns"><button type="button" onclick="wifi()">Save Wi-Fi</button></div>
 <div class="msg" id="m2"></div>
</div>

<div class="card"><h2>Firmware update</h2>
 <div class="sub">Choose the compiled <b>OilGauge370Z.ino.bin</b> (Arduino IDE: Sketch &rarr; Export Compiled Binary). Not the file with &ldquo;bootloader&rdquo; or &ldquo;merged&rdquo; in its name. The gauge restarts when done, and it <b>stops reading while it installs</b> - only do this when parked.</div>
 <div class="row" style="grid-template-columns:1fr"><input type="file" id="bin" accept=".bin"></div>
 <div class="btns"><button type="button" class="dng" id="upb" onclick="upload()">Upload &amp; install</button></div>
 <progress id="prog" value="0" max="100" hidden></progress>
 <div class="msg" id="m3"></div>
</div>

<script>
const $=id=>document.getElementById(id);
const PG=[[0,'Not used'],[1,'Oil'],[2,'Boost'],[3,'AFR'],
  [33,'Split: Oil above, Boost below'],[18,'Split: Boost above, Oil below'],
  [49,'Split: Oil above, AFR below'],[19,'Split: AFR above, Oil below'],
  [50,'Split: Boost above, AFR below'],[35,'Split: AFR above, Boost below']];
const GN={1:'Oil',2:'Boost',3:'AFR'};
let cur={},unitBar=0,activeM5=true,presets={boost:[],afr:[]},live=null,lay={available:false,loaded:false,views:[]};
function say(id,t,ok){const e=$(id);e.textContent=t;e.className='msg '+(ok?'ok':'err')}
async function jget(u){const r=await fetch(u,{cache:'no-store'});if(!r.ok)throw new Error(r.status);return r.json()}
function fields(){return document.querySelectorAll('[data-f]')}
function F(n){return document.querySelector('[data-f='+n+']')}
function num(n){return parseFloat(F(n).value)}

// ---- inputs: the lists and notes follow the chosen board ----
const BOARD_NOTE=[
 "Boost and AFR need the INA3221. Each sensor's signal goes to its channel's Vin- terminal, with a common ground. The three must use different channels.",
 "The M5 voltmeter has one input (oil only). Boost and AFR need another board.",
 "Each sensor's signal goes to its channel's Vin- terminal, with a common ground. The three must use different channels.",
 "A plain ADS1115 breakout. Each sensor's signal goes to its input (A0 to A3) with a common ground. It can only measure up to its own supply voltage (about 3.3 V from this gauge), so a 0.5 to 4.5 V sensor needs a voltage divider: enter its ratio below (e.g. 2 for two equal resistors). Not found by Auto-detect.",
 "An M5 PaHUB with an M5 voltmeter plugged into each port you use. All the voltmeters have the same address - the hub keeps them apart - so give oil, boost and AFR a different port each. Not found by Auto-detect.",
 "An M5 PbHUB: each sensor's signal goes to its port's IO0 pin (the yellow wire of the M5 cable) with a common ground. Its analog input is coarser than the other boards (10-bit). Check the live volts against a multimeter; if they are wrong, try another PbHUB reading. Not found by Auto-detect."];
function addrList(m){
  const lo=m==3?72:m==4?112:97,n=m==3?4:8,hex=v=>'0x'+v.toString(16).toUpperCase();
  const o=[[0,'Usual ('+hex(lo)+')']];for(let i=0;i<n;i++)o.push([lo+i,hex(lo+i)]);return o}
function fillSel(id,list,keep){
  const s=$(id),old=keep?s.value:null;s.textContent='';
  list.forEach(([v,t])=>{const o=document.createElement('option');o.value=v;o.textContent=t;s.appendChild(o)});
  if(old!=null&&list.some(x=>String(x[0])==old))s.value=old}
function modeUi(initial){
  const m=+$('sensor_mode').value,mx=m==3?4:m>=4?6:3;
  const name=n=>m==3?'Input A'+(n-1):m>=4?'Port '+n:'Channel '+n;
  const inputs=[];for(let n=1;n<=mx;n++)inputs.push([n,name(n)]);
  fillSel('ina_channel',inputs,!initial);fillSel('boost_ch',[[0,'Not fitted']].concat(inputs),!initial);fillSel('afr_ch',[[0,'Not fitted']].concat(inputs),!initial);
  if(!initial&&+$('ina_channel').value>mx)$('ina_channel').value=1;
  fillSel('hub_addr',m>=3?addrList(m):[[0,'']],false);$('hub_addr').value=initial&&cur.hub_addr!=null&&m>=3?cur.hub_addr:0;
  $('r_addr').hidden=m<3;$('r_pb').hidden=m!=5;$('r_div').hidden=!(m==3||m==5);
  $('n_board').textContent=BOARD_NOTE[m];
  if(!initial)refreshScreens()}

// ---- screens ----
function needs(code){return code>=64?[]:[code&15,code>>4].filter(x=>x>0)}
function isCustom(code){return code>=64}
function customUsable(code){return lay.views.some(v=>v.code==code&&v.usable)}
function gaugeOn(g){return g==1||(g==2&&F('boost_ch').value!='0')||(g==3&&F('afr_ch').value!='0')}
function buildScreens(want){
  document.querySelectorAll('.pgsel').forEach((s,i)=>{
    s.textContent='';PG.forEach(p=>{const o=document.createElement('option');o.value=p[0];o.textContent=p[1];s.appendChild(o)});
    lay.views.filter(v=>v.usable).forEach(v=>{const o=document.createElement('option');o.value=v.code;o.textContent='Designed: '+v.name;s.appendChild(o)});
    const w=want&&want[i];                       // a saved designed screen whose view is not on the gauge now
    if(w&&isCustom(w)&&!customUsable(w)){const o=document.createElement('option');o.value=w;o.textContent='Designed view '+(w-63)+' (not on the gauge)';s.appendChild(o)}
  })
}
function refreshScreens(){
  const bad=[];
  document.querySelectorAll('.pgsel').forEach((s,i)=>{
    let off=false;
    [...s.options].forEach(o=>{const miss=needs(+o.value).filter(g=>!gaugeOn(g));o.disabled=miss.length>0&&o.value!=s.value})
    const miss=needs(+s.value).filter(g=>!gaugeOn(g));
    const gone=isCustom(+s.value)&&!customUsable(+s.value);
    s.classList.toggle('bad',miss.length>0||gone);
    if(miss.length)bad.push('Screen '+(i+1)+' shows '+miss.map(g=>GN[g]).join(' and ')+', which has no input (not fitted).');
    if(gone)bad.push('Screen '+(i+1)+' is a designed view that is not on the gauge (upload the layout again, or pick another screen). It is skipped until then.')
  });
  $('m_scr').textContent=bad.join(' ');
  $('d_boost').dataset.on=F('boost_ch').value!='0'?1:0;$('d_afr').dataset.on=F('afr_ch').value!='0'?1:0;
  $('n_boost').textContent=F('boost_ch').value=='0'?'Boost is not fitted. Give it an input under Inputs to use it.':'';
  $('n_afr').textContent=F('afr_ch').value=='0'?'AFR is not fitted. Give it an input under Inputs to use it.':'';
}

// ---- presets ----
function fillPresets(){
  [['boost',presets.boost],['afr',presets.afr]].forEach(([k,list])=>{
    const s=$(k+'_preset');s.textContent='';
    list.forEach((p,i)=>{const o=document.createElement('option');o.value=i;o.textContent=p.n;s.appendChild(o)});
    const c=document.createElement('option');c.value=255;c.textContent='Custom';s.appendChild(c)})
}
function applyPreset(k){
  const i=+$(k+'_preset').value;if(i==255){return}
  const p=presets[k][i];if(!p)return;
  if(k=='boost'){F('boost_v1').value=p.v1;F('boost_kpa1').value=p.k1;F('boost_v2').value=p.v2;F('boost_kpa2').value=p.k2;F('boost_abs').value=p.abs}
  else{F('afr_v1').value=p.v1;F('afr_afr1').value=p.a1;F('afr_v2').value=p.v2;F('afr_afr2').value=p.a2}
  preview()
}
function same(a,b){return Math.abs(parseFloat(a)-b)<0.0006}
function curveEdited(k){
  const i=+$(k+'_preset').value;
  if(i!=255){const p=presets[k][i];
    const m=k=='boost'?(p&&same(F('boost_v1').value,p.v1)&&same(F('boost_kpa1').value,p.k1)&&same(F('boost_v2').value,p.v2)&&same(F('boost_kpa2').value,p.k2)&&F('boost_abs').value==p.abs)
                      :(p&&same(F('afr_v1').value,p.v1)&&same(F('afr_afr1').value,p.a1)&&same(F('afr_v2').value,p.v2)&&same(F('afr_afr2').value,p.a2));
    if(!m)$(k+'_preset').value=255}
  preview()
}
// What the live volts mean on the curve currently in the form (including unsaved edits)
function preview(){
  const lb=live&&live.boost,la=live&&live.afr;
  if(lb&&lb.volts!=null){const v=lb.volts,k=num('boost_kpa1')+(v-num('boost_v1'))*(num('boost_kpa2')-num('boost_kpa1'))/(num('boost_v2')-num('boost_v1'));
    $('pv_boost').textContent='Now: '+v.toFixed(3)+' V = '+(isFinite(k)?k.toFixed(1):'--')+' kPa at the sensor'+(isFinite(k)&&F('boost_abs').value=='1'?' ('+((k-num('boost_atm_kpa'))*0.145038).toFixed(1)+' psi boost)':'')}
  else $('pv_boost').textContent='Now: no reading'
  if(la&&la.volts!=null){const v=la.volts,a=num('afr_afr1')+(v-num('afr_v1'))*(num('afr_afr2')-num('afr_afr1'))/(num('afr_v2')-num('afr_v1'));
    $('pv_afr').textContent='Now: '+v.toFixed(3)+' V = AFR '+(isFinite(a)?a.toFixed(2):'--')}
  else $('pv_afr').textContent='Now: no reading'
}

async function load(){
  const [s,i,p,l]=await Promise.all([jget('/api/settings'),jget('/api/info'),jget('/api/presets'),jget('/api/layout').catch(()=>null)]);
  presets=p;fillPresets();
  if(l)lay=l;
  buildScreens([s.page1,s.page2,s.page3,s.page4]);showLayout();
  cur=s;unitBar=s.units_bar;
  $('sensor_mode').value=s.sensor_mode;modeUi(true);
  fields().forEach(e=>e.value=s[e.dataset.f]);
  $('bl_v').textContent=s.backlight;
  $('info').textContent=i.version+' · built '+i.build;
  $('cur_ssid').textContent=i.ssid;$('ssid').value=i.ssid;
  $('u_o').textContent=$('u_b').textContent=unitBar?'bar':'psi';
  $('d_boost').open=s.boost_ch!=0;$('d_afr').open=s.afr_ch!=0;
  refreshScreens();preview();
  if(i.defaultPass)say('m2','Still on the factory password - please set your own below.',0);
}
async function post(url,p){const r=await fetch(url,{method:'POST',body:p});return r.json()}
async function saveSettings(ev){
  ev.preventDefault();$('save').disabled=true;
  const p=new URLSearchParams();fields().forEach(e=>p.set(e.dataset.f,e.value));
  try{const j=await post('/api/settings',p);
    if(j.ok){say('m1','Saved.',1);await load()}else say('m1',j.error||'Not saved',0)}
  catch(e){say('m1','No answer from the gauge',0)}
  $('save').disabled=false;return false}
async function resetDefaults(){
  if(!confirm('Put every setting except Wi-Fi back to the factory defaults?'))return;
  try{const j=await post('/api/defaults',new URLSearchParams());
    if(j.ok){say('m1','Defaults restored.',1);await load()}else say('m1',j.error||'Failed',0)}
  catch(e){say('m1','No answer from the gauge',0)}}
async function zeroBoost(){
  if(!confirm('Engine OFF, ignition on? This stores the current reading as 0 psi (atmospheric pressure).'))return;
  try{const j=await post('/api/zero',new URLSearchParams());
    if(j.ok){say('m_zero','Zeroed: atmosphere is '+j.kpa.toFixed(1)+' kPa.',1);await load()}else say('m_zero',j.error||'Could not zero',0)}
  catch(e){say('m_zero','No answer from the gauge',0)}}
let demoLeft=0,demoSeen=false;
function demoShow(left){
  demoLeft=left||0;const b=$('demo_btn');
  b.textContent=demoLeft>0?'Stop demo ('+demoLeft+' s left)':'Run demo';b.className=demoLeft>0?'dng':'';
  if(demoLeft>0){demoSeen=true;say('m_demo','Demo running - the gauge shows invented readings.',1)}
  else if(demoSeen){demoSeen=false;say('m_demo','Demo ended.',1)}}
async function demoToggle(){
  const p=new URLSearchParams();p.set('on',demoLeft>0?'0':'1');
  try{const j=await post('/api/demo',p);
    if(j.ok)demoShow(j.demo);else say('m_demo',j.error||'Could not start the demo',0)}
  catch(e){say('m_demo','No answer from the gauge',0)}}
async function wifi(){
  const p=new URLSearchParams();p.set('ssid',$('ssid').value);p.set('pass',$('pass').value);
  try{const j=await post('/api/wifi',p);
    if(j.ok)say('m2',j.msg,1);else say('m2',j.error||'Not saved',0)}
  catch(e){say('m2','No answer from the gauge (it may already be restarting its Wi-Fi)',0)}}
// Brightness applies as soon as you let go of the slider (just that one setting is sent)
$('backlight').addEventListener('input',()=>{$('bl_v').textContent=$('backlight').value});
$('backlight').addEventListener('change',async()=>{
  const p=new URLSearchParams();p.set('backlight',$('backlight').value);
  try{const j=await post('/api/settings',p);
    if(j.ok)say('m1','Brightness set.',1);else say('m1',j.error||'Not saved',0)}
  catch(e){say('m1','No answer from the gauge',0)}});
// ---- designed screens ----
function showLayout(){
  const ul=$('lay_list');ul.textContent='';
  const st=$('lay_state');
  $('lay_ctl').style.display=lay.available?'':'none';
  if(!lay.available){st.textContent='This firmware has no space to keep layouts (it needs the app3M_fat9M_16MB partition scheme).';return}
  const named=lay.views||[];
  if(lay.loaded){
    st.textContent=named.length+' designed view'+(named.length==1?'':'s')+' on the gauge ('+(lay.bytes/1024).toFixed(1)+' KB).';
    named.forEach(v=>{const li=document.createElement('li');
      li.textContent=v.name+(v.usable?'':' - hidden (switched off in the editor, or not set for screen 1)');ul.appendChild(li)});
  }else st.textContent=lay.error?('The stored layout cannot be used: '+lay.error):'No designed screens on the gauge yet.';
  $('layc').style.display=lay.bytes?'':'none';$('lay_dl').style.display=lay.bytes?'':'none';
}
function layUpload(){
  const f=$('layf').files[0];if(!f){say('m_lay','Choose the layouts file first',0);return}
  if(lay.maxBytes&&f.size>lay.maxBytes){say('m_lay','That file is too large ('+Math.round(f.size/1024)+' KB, limit '+Math.floor(lay.maxBytes/1024)+' KB)',0);return}
  const fd=new FormData();fd.append('layout',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/api/layout');
  $('layb').disabled=true;say('m_lay','Sending...',1);
  x.onload=async()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
    $('layb').disabled=false;
    if(x.status==200&&j.ok){await load();
      say('m_lay',j.saved?'Layout installed and saved. Pick its screens in the Screens list above, then Save settings.':'Layout is active but could NOT be saved to flash - it will be lost at power-off.',j.saved?1:0)}
    else say('m_lay',j.error||('Upload failed ('+x.status+')'),0)};
  x.onerror=()=>{$('layb').disabled=false;say('m_lay','No answer from the gauge',0)};
  x.send(fd)}
async function layClear(){
  if(!confirm('Remove the designed screens from the gauge? Any screen using one is skipped.'))return;
  try{const j=await post('/api/layout/clear','');
    if(j.ok){await load();say('m_lay','Removed.',1)}else say('m_lay',j.error||'Not removed',0)}
  catch(e){say('m_lay','No answer from the gauge',0)}}
function upload(){
  const f=$('bin').files[0];if(!f){say('m3','Choose the .bin file first',0);return}
  if(!/\.bin$/i.test(f.name)){say('m3','That is not a .bin file',0);return}
  if(!confirm('Install '+f.name+' ('+Math.round(f.size/1024)+' KB)? The gauge will restart.'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/update');
  $('prog').hidden=false;$('upb').disabled=true;say('m3','Uploading - keep this page open',1);
  x.upload.onprogress=e=>{if(e.lengthComputable)$('prog').value=100*e.loaded/e.total};
  x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
    if(x.status==200&&j.ok)say('m3','Installed. The gauge is restarting - rejoin its Wi-Fi in about 20 seconds.',1);
    else{say('m3',j.error||('Update failed ('+x.status+')'),0);$('upb').disabled=false}};
  x.onerror=()=>{say('m3','Connection lost during the upload. Check the gauge, then try again.',0);$('upb').disabled=false};
  x.send(fd)}
function matchMeter(){
  const m=parseFloat($('meter').value);if(!(m>0)||!live||live.volts==null||!(live.volts>0.5)){say('m1','Need a meter reading and a live oil reading above 0.5 V',0);return}
  const kind=live.kind,k=kind==2?'ina_scale':(kind==3||kind==5)?'adc_scale':'m5_scale',e=F(k);
  const ns=parseFloat(e.value)*m/live.volts;e.value=ns.toFixed(4);
  say('m1','New '+(kind==2?'INA3221':kind==3?'ADS1115':kind==5?'PbHUB':'M5')+' scale '+ns.toFixed(4)+' - press Save to keep it',1)}
function tile(id,ch,text,unit){
  const t=$('t_'+id);t.classList.toggle('off',ch.fault==6);
  $('v_'+id).textContent=ch.fault==6?'off':text;
  const s=$('s_'+id);s.textContent=ch.fault==6?'':ch.status;s.style.color=ch.fault?'var(--warn)':'var(--ok)'
}
async function tick(){
  if(document.hidden)return;
  try{const j=await jget('/api/live');live=j;
    tile('o',j,j.psi==null?'--':(unitBar?(j.psi/14.5038).toFixed(1):Math.round(j.psi)));
    const b=j.boost,a=j.afr;
    tile('b',b,b.psi==null?'--':(unitBar?(b.psi/14.5038).toFixed(2):b.psi.toFixed(1)));
    tile('a',a,a.afr==null?'--':a.afr.toFixed(1));
    demoShow(j.demo);
    $('sensor').textContent=j.sensor;$('armed').textContent=j.armed?'oil alert armed':'oil alert not armed yet';
    activeM5=j.kind==1||j.kind==4;
    $('g_m5').classList.toggle('active',j.kind==1||j.kind==4);$('g_ina').classList.toggle('active',j.kind==2);$('g_adc').classList.toggle('active',j.kind==3||j.kind==5);
    preview()
  }catch(e){['o','b','a'].forEach(k=>{$('s_'+k).textContent='gauge not answering';$('s_'+k).style.color='var(--bad)'})}
}
buildScreens();
load().catch(()=>say('m1','Could not read settings from the gauge',0));
document.querySelectorAll('.pgsel').forEach(s=>s.addEventListener('change',refreshScreens));
setInterval(tick,600);tick();
</script></body></html>)HTML";
