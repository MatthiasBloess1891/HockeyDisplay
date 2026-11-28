#include "HD_Web.h"
#include "HD_Protocol.h"
#include "WS_RS485.h"

static WebServer *web = NULL;

/********************************************************  Seite: Anzeige  ******************************************************/
static const char PAGE_BOARD[] = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<title>HockeyDisplay</title>
<style>
  :root { --bg:#0b0d10; --panel:#000; --frame:#2a2f36; --label:#c9d1d9; --dim:#5b6470; --off:#1b1410; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--label); font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif; }
  nav { display:flex; gap:18px; padding:10px 16px; font-size:14px; border-bottom:1px solid var(--frame); }
  nav a { color:var(--dim); text-decoration:none; }
  nav a.on { color:#fff; }
  .wrap { max-width:1100px; margin:0 auto; padding:16px; }
  .board { background:var(--panel); border:6px solid var(--frame); border-radius:14px; padding:3.5vmin 3vmin; }
  .row { display:grid; grid-template-columns:1fr auto 1fr; align-items:end; gap:2vmin; }
  .cell { text-align:center; }
  .cap { font-size:clamp(14px,2.6vmin,28px); letter-spacing:.25em; font-weight:600; margin-bottom:1.2vmin; }
  .digits { display:inline-flex; gap:.9vmin; align-items:center; justify-content:center; }
  .digits svg { height:clamp(26px,min(17vmin,11vw),190px); width:auto; }
  .small .digits svg { height:clamp(18px,min(9vmin,6vw),100px); }
  .seg { fill:var(--off); }
  .red .seg.on { fill:#ff2a1a; filter:drop-shadow(0 0 6px #ff2a1a); }
  .amber .seg.on { fill:#ffb000; filter:drop-shadow(0 0 6px #ffb000); }
  .green .seg.on { fill:#35e06a; filter:drop-shadow(0 0 6px #35e06a); }
  .colon { display:flex; flex-direction:column; gap:3.2vmin; padding:0 .4vmin; }
  .colon i { width:1.9vmin; height:1.9vmin; border-radius:50%; background:#ffb000; box-shadow:0 0 8px #ffb000; }
  .colon.off i { background:var(--off); box-shadow:none; }
  .sub { display:grid; grid-template-columns:1fr auto 1fr; gap:2vmin; margin-top:3.5vmin; align-items:center; }
  .pens { display:flex; flex-direction:column; gap:1.2vmin; align-items:center; }
  .horn { display:inline-block; margin-top:1.6vmin; padding:.5vmin 1.6vmin; border-radius:99px; border:2px solid var(--frame); color:var(--dim); font-size:clamp(12px,2vmin,18px); letter-spacing:.2em; }
  .horn.on { background:#ff2a1a; border-color:#ff2a1a; color:#fff; }
  .status { display:flex; flex-wrap:wrap; gap:8px 22px; margin-top:14px; font-size:14px; color:var(--dim); }
  .status b { color:var(--label); font-weight:600; }
  .dot { display:inline-block; width:10px; height:10px; border-radius:50%; background:#5b6470; margin-right:6px; }
  .dot.ok { background:#35e06a; } .dot.warn { background:#ffb000; } .dot.bad { background:#ff2a1a; }
  .hint { margin-top:10px; font-size:13px; color:var(--dim); }
</style>
</head>
<body>
<nav><a class="on" href="/">Anzeige</a><a href="/monitor">RS485-Monitor</a><a href="/uhr">Uhr stellen</a><a href="/relay">Relais</a></nav>
<div class="wrap">
  <div class="board">
    <div class="row">
      <div class="cell red"><div class="cap">HEIM</div><div class="digits" id="home"></div></div>
      <div class="cell amber"><div class="cap" id="timecap">SPIELZEIT</div><div class="digits" id="time"></div></div>
      <div class="cell red"><div class="cap">GAST</div><div class="digits" id="guest"></div></div>
    </div>
    <div class="sub">
      <div class="pens small amber"><div class="digits" id="pen0"></div><div class="digits" id="pen1"></div></div>
      <div class="cell small green"><div class="cap">VIERTEL</div><div class="digits" id="period"></div><br><span class="horn" id="horn">HORN</span></div>
      <div class="pens small amber"><div class="digits" id="pen2"></div><div class="digits" id="pen3"></div></div>
    </div>
  </div>
  <div class="status">
    <span><span class="dot" id="dot"></span><b id="mode">–</b></span>
    <span>Letztes Telegramm: <b id="age">–</b></span>
    <span>Telegramme: <b id="frames">0</b></span>
    <span>Prüfsummenfehler: <b id="err">0</b></span>
    <span>Baudrate: <b id="baud">–</b></span>
  </div>
  <div class="hint" id="hint"></div>
</div>
<script>
const SEG = {'0':'abcdef','1':'bc','2':'abged','3':'abgcd','4':'fgbc','5':'afgcd','6':'afgedc','7':'abc','8':'abcdefg','9':'abcdfg','-':'g',' ':''};
const SHAPE = {a:[12,4,36,10],b:[48,12,10,34],c:[48,54,10,34],d:[12,86,36,10],e:[2,54,10,34],f:[2,12,10,34],g:[12,45,36,10]};
function digit(ch) {
  const on = SEG[ch] !== undefined ? SEG[ch] : 'adg';
  let s = '<svg viewBox="0 0 60 100" aria-hidden="true">';
  for (const k in SHAPE) { const r = SHAPE[k]; s += '<rect class="seg' + (on.includes(k) ? ' on' : '') + '" x="' + r[0] + '" y="' + r[1] + '" width="' + r[2] + '" height="' + r[3] + '" rx="4"/>'; }
  return s + '</svg>';
}
const last = {};
function show(id, text, colonAt, colonOn) {
  const key = text + '|' + colonAt + '|' + colonOn;
  if (last[id] === key) return;
  last[id] = key;
  let s = '';
  for (let i = 0; i < text.length; i++) {
    if (i === colonAt) s += '<span class="colon' + (colonOn ? '' : ' off') + '"><i></i><i></i></span>';
    s += digit(text[i]);
  }
  document.getElementById(id).innerHTML = s;
}
function pad(t, n) { t = String(t == null ? '' : t); while (t.length < n) t += ' '; return t.slice(0, n); }
function two(n) { return (n < 10 ? '0' : '') + n; }
let failures = 0;
function render(st) {
  const game = st && st.game;
  document.getElementById('timecap').textContent = game ? 'SPIELZEIT' : 'UHRZEIT';
  if (game) {
    const sc = pad(st.score, 6), pen = pad(st.pen, 16);
    show('home', sc.slice(0, 3).trim().padStart(2, ' '), -1, false);
    show('guest', sc.slice(3, 6).trim().padStart(2, ' '), -1, false);
    show('time', pad(st.time, 4), 2, true);
    show('period', pad(st.period, 1), -1, false);
    for (let i = 0; i < 4; i++) show('pen' + i, pen.slice(1 + 4 * i, 4 + 4 * i), -1, false);
  } else {
    const d = new Date();
    show('home', '  ', -1, false);
    show('guest', '  ', -1, false);
    show('time', two(d.getHours()) + two(d.getMinutes()), 2, d.getSeconds() % 2 === 0);
    show('period', ' ', -1, false);
    for (let i = 0; i < 4; i++) show('pen' + i, '   ', -1, false);
  }
  document.getElementById('horn').className = 'horn' + (game && st.horn ? ' on' : '');
  const dot = document.getElementById('dot'), mode = document.getElementById('mode'), hint = document.getElementById('hint');
  if (!st) { dot.className = 'dot bad'; mode.textContent = 'Keine Verbindung zum ESP32'; hint.textContent = ''; return; }
  if (game) { dot.className = 'dot ok'; mode.textContent = 'Spielmodus'; hint.textContent = ''; }
  else if (st.online) { dot.className = 'dot warn'; mode.textContent = 'Uhrzeit (Spielmodus beendet)'; hint.textContent = ''; }
  else {
    dot.className = 'dot'; mode.textContent = 'Uhrzeit (Bediengerät sendet nicht)';
    hint.textContent = st.rx > 0 && st.frames === 0 ? 'Es kommen Bytes an, aber keine gültigen Telegramme. Bitte Verkabelung (A/B) und Baudrate prüfen.' : '';
  }
  document.getElementById('age').textContent = st.seen ? (st.age / 1000).toFixed(1).replace('.', ',') + ' s' : 'noch keines';
  document.getElementById('frames').textContent = st.frames;
  document.getElementById('err').textContent = st.chkErr;
  document.getElementById('baud').textContent = st.baud + ' 8N1';
}
async function poll() {
  try {
    const r = await fetch('/api/state', {cache: 'no-store'});
    render(await r.json());
    failures = 0;
  } catch (e) {
    if (++failures > 3) render(null);
  }
  setTimeout(poll, 250);
}
render({game: false, online: false, seen: false, age: 0, frames: 0, chkErr: 0, baud: '–', rx: 0});
poll();
</script>
</body>
</html>
)HTML";

/*******************************************************  Seite: Monitor  *******************************************************/
static const char PAGE_MONITOR[] = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<title>HockeyDisplay – RS485-Monitor</title>
<style>
  :root { --bg:#0b0d10; --frame:#2a2f36; --label:#c9d1d9; --dim:#7d8792; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--label); font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif; font-size:14px; }
  nav { display:flex; gap:18px; padding:10px 16px; border-bottom:1px solid var(--frame); }
  nav a { color:var(--dim); text-decoration:none; }
  nav a.on { color:#fff; }
  .wrap { max-width:1200px; margin:0 auto; padding:16px; }
  .bar { display:flex; flex-wrap:wrap; gap:10px 18px; align-items:center; margin-bottom:12px; }
  button, select { background:#1a1f26; color:var(--label); border:1px solid var(--frame); border-radius:6px; padding:6px 12px; font-size:14px; cursor:pointer; }
  button:hover { border-color:#58a6ff; }
  label { display:inline-flex; align-items:center; gap:6px; cursor:pointer; }
  .stats { color:var(--dim); margin-bottom:10px; }
  .stats b { color:var(--label); }
  .scroll { overflow:auto; max-height:72vh; border:1px solid var(--frame); border-radius:8px; }
  table { border-collapse:collapse; width:100%; font-family:ui-monospace,Menlo,Consolas,monospace; font-size:13px; }
  th { position:sticky; top:0; background:#161b22; text-align:left; font-family:inherit; color:var(--dim); font-weight:600; }
  th, td { padding:4px 10px; border-bottom:1px solid #1c2128; white-space:nowrap; }
  td.num { text-align:right; color:var(--dim); }
  tr.unk td.txt { color:#ffb000; }
  tr.bad td { color:#ff6b5e; }
  tr.tx td { color:#58a6ff; }
  td.asc { color:var(--dim); }
  .empty { padding:24px; color:var(--dim); }
</style>
</head>
<body>
<nav><a href="/">Anzeige</a><a class="on" href="/monitor">RS485-Monitor</a><a href="/uhr">Uhr stellen</a><a href="/relay">Relais</a></nav>
<div class="wrap">
  <div class="bar">
    <button id="pause">Pause</button>
    <button id="clear">Liste leeren</button>
    <button id="save">Als Textdatei speichern</button>
    <label><input type="checkbox" id="chg" checked> nur Änderungen</label>
    <label><input type="checkbox" id="unk"> nur unbekannte</label>
  </div>
  <div class="stats">Baudrate: <b id="baud">–</b> · empfangene Bytes: <b id="rx">0</b> · gültige Telegramme: <b id="frames">0</b> · Prüfsummenfehler: <b id="err">0</b> · unvollständig: <b id="inc">0</b> · angezeigt: <b id="shown">0</b></div>
  <div class="scroll" id="scroll">
    <table>
      <thead><tr><th>Nr.</th><th>Zeit [s]</th><th>Δ [ms]</th><th>Hex</th><th>ASCII</th><th>Bedeutung</th></tr></thead>
      <tbody id="rows"></tbody>
    </table>
    <div class="empty" id="empty">Noch keine Telegramme empfangen.</div>
  </div>
</div>
<script>
const MAX_ROWS = 2000;
let since = 0, paused = false, prevT = null, all = [], lastByKey = {};
const rows = document.getElementById('rows'), scroll = document.getElementById('scroll');
function ascii(hex) {
  return hex.split(' ').map(h => { const c = parseInt(h, 16); return c >= 32 && c < 127 ? String.fromCharCode(c) : '.'; }).join('');
}
function addRow(f) {
  const tr = document.createElement('tr');
  if (f.f & 64) tr.className = 'tx'; else if (!(f.f & 1)) tr.className = 'bad'; else if (!(f.f & 32)) tr.className = 'unk';
  const cells = [[f.s, 'num'], [(f.t / 1000).toFixed(3), 'num'], [f.dt == null ? '' : f.dt, 'num'], [f.h, ''], [ascii(f.h), 'asc'], [f.d, 'txt']];
  for (const c of cells) { const td = document.createElement('td'); td.textContent = c[0]; td.className = c[1]; tr.appendChild(td); }
  rows.appendChild(tr);
  while (rows.children.length > MAX_ROWS) rows.removeChild(rows.firstChild);
}
function visible(f) {
  if (document.getElementById('unk').checked && (f.f & 32) && (f.f & 1)) return false;
  if (document.getElementById('chg').checked && !f.chg) return false;
  return true;
}
function redraw() {
  rows.innerHTML = '';
  let n = 0;
  for (const f of all) if (visible(f)) { addRow(f); n++; }
  document.getElementById('shown').textContent = n;
  document.getElementById('empty').style.display = all.length ? 'none' : '';
}
function take(frames) {
  const stick = scroll.scrollTop + scroll.clientHeight >= scroll.scrollHeight - 30;
  let n = Number(document.getElementById('shown').textContent);
  for (const f of frames) {
    f.dt = prevT == null ? null : f.t - prevT;
    prevT = f.t;
    const key = f.h.split(' ').slice(0, 7).join(' ');      // Kopf bis Feld und Position
    f.chg = lastByKey[key] !== f.h;
    lastByKey[key] = f.h;
    all.push(f);
    if (all.length > MAX_ROWS) all.shift();
    if (visible(f)) { addRow(f); n++; }
  }
  if (frames.length) {
    document.getElementById('shown').textContent = n;
    document.getElementById('empty').style.display = 'none';
    if (stick) scroll.scrollTop = scroll.scrollHeight;
  }
}
async function poll() {
  try {
    const st = await (await fetch('/api/state', {cache: 'no-store'})).json();
    document.getElementById('rx').textContent = st.rx;
    document.getElementById('frames').textContent = st.frames;
    document.getElementById('err').textContent = st.chkErr;
    document.getElementById('inc').textContent = st.incomplete;
    document.getElementById('baud').textContent = st.baud + ' 8N1';
    if (!paused) {
      const r = await (await fetch('/api/log?since=' + since, {cache: 'no-store'})).json();
      if (r.head < since) { since = 0; }                    // ESP32 wurde neu gestartet
      else { take(r.frames); since = r.next; }
    }
  } catch (e) {}
  setTimeout(poll, 300);
}
document.getElementById('pause').onclick = e => { paused = !paused; e.target.textContent = paused ? 'Weiter' : 'Pause'; };
document.getElementById('clear').onclick = () => { all = []; lastByKey = {}; prevT = null; redraw(); };
document.getElementById('chg').onchange = redraw;
document.getElementById('unk').onchange = redraw;
document.getElementById('save').onclick = () => {
  const lines = all.filter(visible).map(f => [f.s, (f.t / 1000).toFixed(3), f.h, f.d].join('\t'));
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([lines.join('\n') + '\n'], {type: 'text/plain'}));
  a.download = 'rs485-mitschnitt.txt';
  a.click();
};
poll();
</script>
</body>
</html>
)HTML";

/******************************************************  Seite: Uhr stellen  ****************************************************/
static const char PAGE_CLOCK[] = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<title>HockeyDisplay – Uhr stellen</title>
<style>
  :root { --bg:#0b0d10; --frame:#2a2f36; --label:#c9d1d9; --dim:#7d8792; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--label); font-family:-apple-system,"Segoe UI",Roboto,Arial,sans-serif; font-size:15px; }
  nav { display:flex; gap:18px; padding:10px 16px; font-size:14px; border-bottom:1px solid var(--frame); }
  nav a { color:var(--dim); text-decoration:none; }
  nav a.on { color:#fff; }
  .wrap { max-width:640px; margin:0 auto; padding:16px; }
  h1 { font-size:20px; margin:8px 0 4px; }
  p { color:var(--dim); line-height:1.5; margin:6px 0 0; }
  .card { border:1px solid var(--frame); border-radius:10px; padding:16px; margin-top:16px; }
  .card h2 { font-size:16px; margin:0 0 4px; }
  .row { display:flex; flex-wrap:wrap; gap:10px; align-items:center; margin-top:14px; }
  .now { font-family:ui-monospace,Menlo,Consolas,monospace; font-size:28px; color:#ffb000; margin-top:12px; }
  button, input { background:#1a1f26; color:var(--label); border:1px solid var(--frame); border-radius:6px; padding:9px 14px; font-size:15px; font-family:inherit; }
  input { color-scheme:dark; }
  button { cursor:pointer; }
  button.primary { background:#1f6feb; border-color:#1f6feb; color:#fff; }
  button:disabled { opacity:.5; cursor:default; }
  .msg { margin-top:16px; padding:12px 14px; border-radius:8px; display:none; line-height:1.5; }
  .msg.ok { display:block; background:#12261a; border:1px solid #238636; }
  .msg.err { display:block; background:#2d1414; border:1px solid #da3633; }
  .msg.wait { display:block; background:#161b22; border:1px solid var(--frame); }
  code { font-family:ui-monospace,Menlo,Consolas,monospace; font-size:13px; color:var(--dim); word-break:break-all; }
</style>
</head>
<body>
<nav><a href="/">Anzeige</a><a href="/monitor">RS485-Monitor</a><a class="on" href="/uhr">Uhr stellen</a><a href="/relay">Relais</a></nav>
<div class="wrap">
  <h1>Uhr der Anzeige stellen</h1>
  <p>Der ESP32 sendet Datum und Uhrzeit über RS485 an die Anzeige. Das Telegramm enthält keine Sekunden. Solange das Bediengerät im Spielmodus sendet, wird nichts gesendet.</p>

  <div class="card">
    <h2>Mit diesem Gerät synchronisieren</h2>
    <p>Sendet die Uhrzeit dieses Browsers genau beim nächsten Minutenwechsel.</p>
    <div class="now" id="now">--:--:--</div>
    <div class="row"><button class="primary" id="sync">Zur vollen Minute senden</button><button id="cancel" style="display:none">Abbrechen</button></div>
  </div>

  <div class="card">
    <h2>Datum und Uhrzeit von Hand</h2>
    <p>Wird sofort gesendet.</p>
    <div class="row"><input type="datetime-local" id="manual"><button id="send">Senden</button></div>
  </div>

  <div class="msg" id="msg"></div>
</div>
<script>
const two = n => (n < 10 ? '0' : '') + n;
const msg = document.getElementById('msg');
let timer = null;
function say(kind, text, frame) {
  msg.className = 'msg ' + kind;
  msg.textContent = text;
  if (frame) { msg.appendChild(document.createElement('br')); const c = document.createElement('code'); c.textContent = frame; msg.appendChild(c); }
}
function code(d) { return two(d.getDate()) + two(d.getMonth() + 1) + two(d.getFullYear() % 100) + two(d.getHours()) + two(d.getMinutes()); }
function text(d) { return two(d.getDate()) + '.' + two(d.getMonth() + 1) + '.' + d.getFullYear() + ' ' + two(d.getHours()) + ':' + two(d.getMinutes()); }
async function send(d) {
  try {
    const r = await fetch('/api/clock?d=' + code(d), {cache: 'no-store'});
    const j = await r.json();
    if (j.ok) say('ok', 'Gesendet: ' + text(d) + ' Uhr', j.frame);
    else say('err', j.error);
  } catch (e) {
    say('err', 'Keine Verbindung zum ESP32.');
  }
}
function stopSync() {
  if (timer) clearInterval(timer);
  timer = null;
  document.getElementById('sync').disabled = false;
  document.getElementById('cancel').style.display = 'none';
}
document.getElementById('sync').onclick = () => {
  const target = new Date();
  target.setSeconds(0, 0);
  target.setMinutes(target.getMinutes() + 1);
  document.getElementById('sync').disabled = true;
  document.getElementById('cancel').style.display = '';
  timer = setInterval(() => {
    const left = target - Date.now();
    if (left <= 0) { stopSync(); send(target); }
    else say('wait', 'Sende ' + text(target) + ' Uhr in ' + Math.ceil(left / 1000) + ' s …');
  }, 50);
};
document.getElementById('cancel').onclick = () => { stopSync(); say('wait', 'Abgebrochen.'); };
document.getElementById('send').onclick = () => {
  const v = document.getElementById('manual').value;
  if (!v) { say('err', 'Bitte Datum und Uhrzeit eintragen.'); return; }
  send(new Date(v));
};
function tick() {
  const d = new Date();
  document.getElementById('now').textContent = two(d.getHours()) + ':' + two(d.getMinutes()) + ':' + two(d.getSeconds());
}
const d0 = new Date();
document.getElementById('manual').value = d0.getFullYear() + '-' + two(d0.getMonth() + 1) + '-' + two(d0.getDate()) + 'T' + two(d0.getHours()) + ':' + two(d0.getMinutes());
tick();
setInterval(tick, 200);
</script>
</body>
</html>
)HTML";

/**************************************************************  API  ***********************************************************/
// Text fuer JSON aufbereiten: nur druckbare ASCII-Zeichen, Anfuehrungszeichen und Backslash maskiert
static String jsonText(const char *s) {
  String out;
  for (; *s; s++) {
    char c = *s;
    if (c < 0x20 || c > 0x7E) c = '?';
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

static void handleBoard()   { web->send(200, "text/html", String(PAGE_BOARD)); }
static void handleMonitor() { web->send(200, "text/html", String(PAGE_MONITOR)); }
static void handleClockPage() { web->send(200, "text/html", String(PAGE_CLOCK)); }

static void handleState() {
  RS485_Lock();
  uint32_t now = millis();
  HdState st = HD_GetState();
  bool online = HD_Online(now);
  bool game = HD_GameMode(now);
  RS485_Unlock();

  String json = "{";
  json += "\"online\":" + String(online ? "true" : "false");
  json += ",\"game\":" + String(game ? "true" : "false");
  json += ",\"seen\":" + String(st.seen ? "true" : "false");
  json += ",\"age\":" + String(st.seen ? (uint32_t)(now - st.lastValidMs) : 0);
  json += ",\"time\":\"" + jsonText(st.time) + "\"";
  json += ",\"score\":\"" + jsonText(st.score) + "\"";
  json += ",\"period\":\"" + jsonText(st.period) + "\"";
  json += ",\"status\":\"" + jsonText(st.status) + "\"";
  json += ",\"pen\":\"" + jsonText(st.pen) + "\"";
  json += ",\"horn\":" + String((st.status[2] & 0x20) ? "true" : "false");
  json += ",\"frames\":" + String(st.frames);
  json += ",\"chkErr\":" + String(st.chkErrors);
  json += ",\"incomplete\":" + String(st.incomplete);
  json += ",\"rx\":" + String(RS485_RxBytes());
  json += ",\"baud\":" + String(RS485_GetBaud());
  json += "}";
  web->send(200, "application/json", json);
}

#define HD_LOG_CHUNK 40
static HdFrame chunk[HD_LOG_CHUNK];           // statisch: der Webserver arbeitet Anfragen nacheinander ab

static void handleLog() {
  uint32_t since = 0;
  if (web->hasArg("since")) since = (uint32_t)strtoul(web->arg("since").c_str(), NULL, 10);

  RS485_Lock();
  uint32_t head = HD_LogHead();
  if (since > head) since = head;
  if (head - since > HD_LOG_SIZE) since = head - HD_LOG_SIZE;
  int n = 0;
  uint32_t next = since;
  while (next < head && n < HD_LOG_CHUNK) {
    if (HD_LogGet(next, &chunk[n])) n++;
    next++;
  }
  RS485_Unlock();

  String json;
  json.reserve(256 + n * 220);
  json = "{\"head\":" + String(head) + ",\"next\":" + String(next) + ",\"frames\":[";
  char hex[4], desc[96];
  for (int i = 0; i < n; i++) {
    const HdFrame &f = chunk[i];
    if (i) json += ',';
    json += "{\"s\":" + String(f.seq) + ",\"t\":" + String(f.ms) + ",\"f\":" + String(f.flags) + ",\"h\":\"";
    for (int k = 0; k < f.len; k++) {
      snprintf(hex, sizeof(hex), k ? " %02X" : "%02X", f.data[k]);
      json += hex;
    }
    HD_Describe(f, desc, sizeof(desc));
    json += "\",\"d\":\"" + jsonText(desc) + "\"}";
  }
  json += "]}";
  web->send(200, "application/json", json);
}

// Uhr der Anzeige stellen: /api/clock?d=TTMMJJHHMM
static void handleClock() {
  String d = web->arg("d");
  const char *p = d.c_str();
  bool digits = (strlen(p) == 10);
  for (int i = 0; digits && i < 10; i++) digits = (p[i] >= '0' && p[i] <= '9');
  if (!digits) {
    web->send(400, "application/json", String("{\"ok\":false,\"error\":\"Parameter d fehlt oder ist nicht TTMMJJHHMM.\"}"));
    return;
  }
  int v[5];
  for (int i = 0; i < 5; i++) v[i] = (p[2 * i] - '0') * 10 + (p[2 * i + 1] - '0');

  uint8_t frame[HD_FRAME_MAX];
  size_t len = 0;
  int res = RS485_SendClock(v[0], v[1], v[2], v[3], v[4], frame, &len);
  if (res == HD_SEND_OK) {
    String json = "{\"ok\":true,\"frame\":\"";
    char hex[4];
    for (size_t k = 0; k < len; k++) {
      snprintf(hex, sizeof(hex), k ? " %02X" : "%02X", frame[k]);
      json += hex;
    }
    json += "\"}";
    web->send(200, "application/json", json);
    return;
  }
  const char *err = "Der RS485-Bus wurde nicht ruhig, es wurde nichts gesendet.";
  if (res == HD_SEND_INVALID) err = "Datum oder Uhrzeit sind ungültig.";
  if (res == HD_SEND_GAME) err = "Das Bediengerät sendet im Spielmodus. Die Uhr lässt sich erst nach Spielende stellen.";
  web->send(res == HD_SEND_INVALID ? 400 : 409, "application/json", String("{\"ok\":false,\"error\":\"") + err + "\"}");
}

void HD_Web_Register(WebServer &srv) {
  web = &srv;
  srv.on("/", handleBoard);
  srv.on("/monitor", handleMonitor);
  srv.on("/api/state", handleState);
  srv.on("/api/log", handleLog);
  srv.on("/uhr", handleClockPage);
  srv.on("/api/clock", handleClock);
}
