#pragma once

// The web page the ESP32 serves. Kept out of main.cpp so the layout can be
// changed without touching the code. main.cpp sends it as it is.

static const char PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart fan</title>
<style>
:root{--bg:#0e1014;--card:#171a20;--line:#252a33;--text:#eef0f4;--muted:#8a90a0;
--temp:#ff8a4c;--hum:#4cb3ff;--fan:#3ddc97;--warn:#f5b941;--bad:#ff5d5d}
@media (prefers-color-scheme:light){:root{--bg:#f3f4f7;--card:#fff;--line:#e3e6ec;
--text:#16181d;--muted:#6b7180;--temp:#e8692a;--hum:#1d8fe0;--fan:#14a86c}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);
font:16px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:980px;margin:0 auto;padding:28px 16px 40px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:22px}
h1{font-size:22px;font-weight:650;margin:0;letter-spacing:-.01em}
.pill{display:flex;align-items:center;gap:8px;font-size:14px;color:var(--muted);
background:var(--card);border:1px solid var(--line);border-radius:999px;padding:6px 12px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}
.ok .dot{background:var(--fan);box-shadow:0 0 0 4px color-mix(in srgb,var(--fan) 20%,transparent)}
.warn .dot{background:var(--warn)}.bad .dot{background:var(--bad)}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:20px;
display:flex;flex-direction:column;gap:6px;min-height:200px}
.label{font-size:14px;color:var(--muted);display:flex;align-items:center;gap:8px}
.swatch{width:10px;height:10px;border-radius:3px}
.value{font-size:52px;font-weight:650;letter-spacing:-.03em;line-height:1.05;
font-variant-numeric:tabular-nums}
.unit{font-size:22px;font-weight:500;color:var(--muted);margin-left:4px}
.sub{font-size:14px;color:var(--muted);font-variant-numeric:tabular-nums}
svg.spark{width:100%;height:56px;margin-top:auto;display:block}
svg.spark path{fill:none;stroke-width:2;vector-effect:non-scaling-stroke;
stroke-linejoin:round;stroke-linecap:round}
.fanrow{display:flex;align-items:center;justify-content:space-between;gap:12px}
.rotor{width:72px;height:72px;color:var(--fan);flex:none}
.rotor g{transform-origin:50% 50%;animation:spin 1s linear infinite;animation-play-state:paused}
@keyframes spin{to{transform:rotate(360deg)}}
.bar{height:8px;border-radius:99px;background:var(--line);overflow:hidden;margin-top:auto}
.bar i{display:block;height:100%;width:0;background:var(--fan);border-radius:99px;
transition:width .6s ease}
footer{margin-top:18px;font-size:13px;color:var(--muted);text-align:center;
font-variant-numeric:tabular-nums}
.stale .value{color:var(--muted)}
.wide{grid-column:1/-1;min-height:0}
.seg{display:flex;gap:6px;background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:4px}
.seg button{flex:1;font:inherit;font-weight:600;color:var(--muted);background:none;border:0;
border-radius:9px;padding:10px 0;cursor:pointer}
.seg button.on{background:var(--card);color:var(--text);box-shadow:0 1px 3px rgba(0,0,0,.25)}
.slider{display:flex;align-items:center;gap:14px;margin-top:14px}
.slider input{flex:1;accent-color:var(--fan);height:28px}
.slider span{min-width:48px;text-align:right;font-variant-numeric:tabular-nums}
.slider.off{opacity:.45}
</style></head><body><main>
<header><h1>Smart fan</h1>
<div class="pill" id="pill"><span class="dot"></span><span id="state">Connecting</span></div>
</header>
<div class="grid">
 <section class="card" id="ct">
  <div class="label"><span class="swatch" style="background:var(--temp)"></span>Temperature</div>
  <div class="value"><span id="t">--</span><span class="unit">°C</span></div>
  <div class="sub">last 5 minutes</div>
  <svg class="spark" viewBox="0 0 100 30" preserveAspectRatio="none"><path id="tp" stroke="var(--temp)"/></svg>
 </section>
 <section class="card" id="ch">
  <div class="label"><span class="swatch" style="background:var(--hum)"></span>Humidity</div>
  <div class="value"><span id="h">--</span><span class="unit">%</span></div>
  <div class="sub">last 5 minutes</div>
  <svg class="spark" viewBox="0 0 100 30" preserveAspectRatio="none"><path id="hp" stroke="var(--hum)"/></svg>
 </section>
 <section class="card">
  <div class="label"><span class="swatch" style="background:var(--fan)"></span>Fan</div>
  <div class="fanrow">
   <div><div class="value"><span id="d">--</span><span class="unit">%</span></div>
   <div class="sub" id="r">-- rpm</div></div>
   <svg class="rotor" viewBox="0 0 100 100" aria-hidden="true"><g id="rot">
    <circle cx="50" cy="50" r="9" fill="currentColor"/>
    <path fill="currentColor" opacity=".85" d="M50 41C48 25 54 10 66 8c8-1 12 8 6 14-6 6-14 12-22 19z"/>
    <path fill="currentColor" opacity=".85" d="M50 41C48 25 54 10 66 8c8-1 12 8 6 14-6 6-14 12-22 19z" transform="rotate(120 50 50)"/>
    <path fill="currentColor" opacity=".85" d="M50 41C48 25 54 10 66 8c8-1 12 8 6 14-6 6-14 12-22 19z" transform="rotate(240 50 50)"/>
   </g></svg>
  </div>
  <div class="bar"><i id="db"></i></div>
 </section>
 <section class="card wide">
  <div class="label">Mode</div>
  <div class="seg" id="seg">
   <button data-m="A">Auto</button><button data-m="M">Manual</button><button data-m="O">Off</button>
  </div>
  <div class="slider" id="sl"><span class="sub">Speed</span>
   <input type="range" id="duty" min="30" max="100" step="5" value="50">
   <span id="dv">50 %</span></div>
 </section>
</div>
<footer id="foot">waiting for the first reading</footer>
</main>
<script>
const $ = id => document.getElementById(id);

// Mode buttons and the manual speed slider.
let shown = '', holdUntil = 0, synced = false;
function showMode(m) {
  shown = m;
  document.querySelectorAll('#seg button')
    .forEach(b => b.classList.toggle('on', b.dataset.m === m));
  $('sl').classList.toggle('off', m !== 'M');
}
async function setMode(m) {
  showMode(m);
  holdUntil = Date.now() + 3000; // the fan needs a moment to report it back
  let q = 'm=' + m;
  if (m === 'M') q += '&duty=' + $('duty').value;
  try { await fetch('/mode?' + q, { method: 'POST' }); } catch (e) {}
}
document.querySelectorAll('#seg button')
  .forEach(b => b.onclick = () => setMode(b.dataset.m));
$('duty').oninput = () => $('dv').textContent = $('duty').value + ' %';
$('duty').onchange = () => setMode('M'); // moving the slider means manual
const N = 300;              // 5 minutes at one reading a second
const temps = [], hums = [];

// Draw the stored values as a line across the whole box.
function spark(el, a) {
  if (a.length < 2) { el.setAttribute('d', ''); return; }
  let lo = Math.min(...a), hi = Math.max(...a);
  if (hi - lo < 1) { const m = (hi + lo) / 2; lo = m - .5; hi = m + .5; }
  el.setAttribute('d', a.map((v, i) =>
    (i ? 'L' : 'M') + (i * 100 / (a.length - 1)).toFixed(2) + ' ' +
    (28 - (v - lo) / (hi - lo) * 26).toFixed(2)).join(''));
}

function state(cls, text) {
  $('pill').className = 'pill ' + cls;
  $('state').textContent = text;
}

async function tick() {
  let s;
  try { s = await (await fetch('/status.json')).json(); }
  catch (e) { state('bad', 'ESP32 not reachable'); return; }

  const link = s.ok > 0 && s.age_ms < 3000;
  if (!link) state('bad', 'No data from the STM32');
  else if (!s.fresh) state('warn', 'Sensor not answering');
  else state('ok', 'Live');

  $('ct').classList.toggle('stale', !s.fresh);
  $('ch').classList.toggle('stale', !s.fresh);
  if (s.fresh) {
    $('t').textContent = s.t.toFixed(1);
    $('h').textContent = s.h.toFixed(1);
    temps.push(s.t); hums.push(s.h);
    if (temps.length > N) { temps.shift(); hums.shift(); }
  } else {
    $('t').textContent = '--'; $('h').textContent = '--';
  }
  spark($('tp'), temps); spark($('hp'), hums);

  $('d').textContent = s.duty;
  $('r').textContent = s.rpm + ' rpm';
  $('db').style.width = s.duty + '%';
  // One turn of the drawing per 20 real turns, so it is visible but calm.
  const g = $('rot').style;
  if (s.rpm > 0) {
    g.animationDuration = (60 / (s.rpm / 20)).toFixed(2) + 's';
    g.animationPlayState = 'running';
  } else g.animationPlayState = 'paused';

  if (Date.now() > holdUntil && s.mode !== shown) showMode(s.mode);
  if (!synced && s.mode === 'M') { // start the slider where the fan already is
    $('duty').value = s.duty; $('dv').textContent = s.duty + ' %'; synced = true;
  }

  $('foot').textContent = 'lines from the STM32: ' + s.ok + ' good, ' + s.bad + ' bad';
}
setInterval(tick, 1000);
tick();
</script></body></html>
)html";