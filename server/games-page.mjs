// The page of GET /games for a browser: the rooms of GET /games.json as cards, refreshed every
// 10, 30 or 60 s (60 by default; the choice is kept in the browser). Everything inline: the
// server sends it as it is, nothing comes from the built game.

export const GAMES_PAGE = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>GTA2 games</title>
<style>
:root{--bg:#0e0e12;--card:#16161a;--line:#2a2a31;--text:#eee;--dim:#999;--accent:#d8452e;--play:#3fb26b;--wait:#d8a52e}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px 48px}
header{display:flex;flex-wrap:wrap;align-items:center;gap:12px 20px;margin-bottom:20px}
h1{margin:0;font-size:1.5rem;letter-spacing:.02em}
h1 span{color:var(--accent)}
.bar{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin-left:auto;color:var(--dim);font-size:.9rem}
select,a.button{font:inherit;color:var(--text);background:var(--card);border:1px solid var(--line);border-radius:6px;padding:.35rem .6rem}
a.button{text-decoration:none;background:var(--accent);border-color:var(--accent);font-weight:600}
.summary{color:var(--dim);margin:0 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(290px,1fr));gap:14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px}
.top{display:flex;align-items:baseline;gap:10px;margin-bottom:6px}
.room{font-size:1.15rem;font-weight:700;overflow-wrap:anywhere}
.state{margin-left:auto;font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:.06em;padding:.15rem .5rem;border-radius:99px;border:1px solid}
.state.playing{color:var(--play);border-color:var(--play)}
.state.starting{color:var(--wait);border-color:var(--wait)}
.state.lobby{color:var(--dim);border-color:var(--line)}
.meta{color:var(--dim);font-size:.9rem;margin-bottom:10px}
.meta b{color:var(--text);font-weight:600}
ul{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:4px}
li{display:flex;gap:8px;align-items:baseline;overflow-wrap:anywhere}
li .crew{color:var(--dim);font-size:.85rem}
li .host{margin-left:auto;color:var(--accent);font-size:.75rem;font-weight:700;text-transform:uppercase}
.empty{color:var(--dim);text-align:center;padding:48px 16px;border:1px dashed var(--line);border-radius:10px}
.error{color:#ff8a7a}
</style></head>
<body><main>
<header>
  <h1>GTA2 <span>games</span></h1>
  <div class="bar">
    <span id="updated">Loading...</span>
    <label>Refresh <select id="every" aria-label="Refresh every">
      <option value="10">10 s</option><option value="30">30 s</option><option value="60">60 s</option>
    </select></label>
    <a class="button" href="/">Play</a>
  </div>
</header>
<p class="summary" id="summary"></p>
<div id="rooms"></div>
</main>
<script>
const MODES = { roam: 'Free roam', wars: 'Gang Wars', ffa: 'Free-for-all' };
const STATES = { playing: 'Playing', starting: 'Starting', lobby: 'In the lobby' };
const cap = (s) => (s ? s[0].toUpperCase() + s.slice(1) : '');
const money = (n) => '$' + Number(n).toLocaleString('en-US');
const clock = (s) => { const m = Math.floor(s / 60); return m >= 60 ? Math.floor(m / 60) + ' h ' + String(m % 60).padStart(2, '0') + ' min' : m + ' min ' + String(s % 60).padStart(2, '0') + ' s'; };
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

function rules(r) {
  const s = r.settings || {};
  const out = [];
  if (s.mode === 'roam') out.push('friendly fire ' + (s.friendlyFire ? 'on' : 'off'));
  if (s.mode === 'ffa' && s.killLimit) out.push(s.killLimit + ' kills');
  if (s.mode === 'wars' && s.pointsTarget) out.push(money(s.pointsTarget) + ' to win');
  if (s.mode !== 'roam' && s.minutes) out.push(s.minutes + ' min');
  return out.join(', ');
}

function card(r) {
  const c = el('article', 'card');
  const top = el('div', 'top');
  top.append(el('span', 'room', r.room), el('span', 'state ' + r.state, STATES[r.state] || r.state));
  c.append(top);
  const meta = el('div', 'meta');
  const mode = MODES[(r.settings || {}).mode];
  if (mode) { meta.append(el('b', null, mode)); meta.append(' in ' + cap(r.settings.district || '')); }
  const extra = rules(r);
  if (extra) meta.append(' (' + extra + ')');
  if (r.state === 'playing' && r.playingSeconds != null) meta.append(el('br'), 'Playing for ' + clock(r.playingSeconds) + (r.tick ? ', tick ' + r.tick : ''));
  if (!meta.childNodes.length) meta.append('Waiting for players');
  c.append(meta);
  const list = el('ul');
  for (const p of r.players) {
    const li = el('li');
    li.append(el('span', null, p.name || '(name not known yet)'));
    if (p.crew) li.append(el('span', 'crew', '[' + p.crew + ']'));
    if (r.host && p.name === r.host) li.append(el('span', 'host', 'host'));
    list.append(li);
  }
  c.append(list);
  return c;
}

let timer = null;
async function load() {
  const updated = document.getElementById('updated');
  try {
    const res = await fetch('/games.json', { cache: 'no-store' });
    if (res.redirected || res.status !== 200) throw new Error(res.status === 200 ? 'logged out' : 'HTTP ' + res.status);
    const data = await res.json();
    const rooms = document.getElementById('rooms');
    rooms.replaceChildren();
    const playing = data.rooms.filter((r) => r.state === 'playing');
    const players = data.rooms.reduce((n, r) => n + r.players.length, 0);
    document.getElementById('summary').textContent = data.rooms.length
      ? data.rooms.length + (data.rooms.length > 1 ? ' rooms, ' : ' room, ') + playing.length + ' playing, ' + players + (players > 1 ? ' players' : ' player')
      : '';
    if (!data.rooms.length) rooms.append(el('div', 'empty', 'No one is playing right now.'));
    else { const grid = el('div', 'grid'); data.rooms.forEach((r) => grid.append(card(r))); rooms.append(grid); }
    updated.className = '';
    updated.textContent = 'Updated ' + new Date().toLocaleTimeString();
  } catch (err) {
    updated.className = 'error';
    updated.textContent = err.message === 'logged out' ? 'Logged out: open the invite link again' : 'Cannot reach the server (' + err.message + ')';
  }
}

const every = document.getElementById('every');
let saved = null;
try { saved = localStorage.getItem('gta2-games-every'); } catch {}
every.value = ['10', '30', '60'].includes(saved) ? saved : '60';
function schedule() {
  clearInterval(timer);
  timer = setInterval(load, Number(every.value) * 1000);
}
every.addEventListener('change', () => {
  try { localStorage.setItem('gta2-games-every', every.value); } catch {}
  schedule();
  load();
});
load();
schedule();
</script></body></html>`;
