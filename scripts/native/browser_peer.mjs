// A browser player for native multiplayer tests (A3): headless Chromium (Playwright) joins a
// co-op room on the game server (server/server.mjs) as a guest, plays with random key
// presses, and prints the page's co-op log, so that a native gta2-online host and a browser
// can be checked against each other (the same sync checksums every 90 ticks).
//
//   node scripts/native/browser_peer.mjs <server url> <invite code> <ROOM> <seconds> [name]
//
// Run it in the Playwright image (mcr.microsoft.com/playwright), which has Chromium:
//   docker run --rm -v "$PWD:/work" -w /work mcr.microsoft.com/playwright:v1.49.1-noble \
//     sh -c 'npm i -s --no-save playwright@1.49.1 && node scripts/native/browser_peer.mjs ...'
import { chromium } from 'playwright';

const [server, invite, room, seconds = '120', name = 'Web'] = process.argv.slice(2);
if (!server || !invite || !room) {
  console.error('usage: browser_peer.mjs <server url> <invite code> <ROOM> <seconds> [name]');
  process.exit(2);
}

const browser = await chromium.launch({
  args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage({ viewport: { width: 960, height: 720 } });
const t0 = Date.now();
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(1)} s`;
page.on('console', (m) => {
  const text = m.text();
  if (/co-op|drift|error|Error|desync|waiting/i.test(text)) console.log(`[page ${stamp()}] ${text}`);
});
page.on('pageerror', (e) => console.log(`[page error ${stamp()}] ${e.message}`));

// the invite link logs in (session cookie) and opens the lobby on this room
const syncDump = process.env.SYNCDUMP ? `&syncdump=${process.env.SYNCDUMP}` : '';
await page.goto(`${server.replace(/\/$/, '')}/?invite=${encodeURIComponent(invite)}&coop${syncDump}#${room}`);
await page.waitForSelector('#coop-join', { state: 'visible', timeout: 30000 });
await page.fill('#coop-name', name);
await page.fill('#coop-code', room);
await page.click('#coop-join');
console.log(`[${stamp()}] joined room ${room} as ${name}`);

// wait for the match to start (the host starts it) and the game to run
await page.waitForFunction(() => window.gta2Lobby && window.gta2Lobby.started, null, { timeout: 300000, polling: 500 });
console.log(`[${stamp()}] the match started: slot ${await page.evaluate(() => window.gta2Lobby.mySlot)}`);
await page.waitForFunction(() => window.gta2Lobby.ready, null, { timeout: 300000, polling: 500 });
console.log(`[${stamp()}] the game is running`);
await page.click('canvas').catch(() => {});

// DUMP=n: once n ticks have run, the first n ticks' sync records (as GTA2_NET_DUMP_HISTORY natively)
const dumpTicks = Number(process.env.DUMP || 0);
let dumped = false;
const dumpHistory = async () => {
  const rows = await page.evaluate((n) => {
    const M = window.gta2 && window.gta2.Module;
    const p = M.ccall('gta2_dev_sync_history', 'number', [], []);
    const v = M.HEAP32.subarray(p >> 2, (p >> 2) + 1024 * 9);
    if (v[(n - 1) * 9] !== n - 1) return null; // not that far yet
    const out = [];
    for (let t = 0; t < n; t++) out.push(Array.from(v.subarray(t * 9, t * 9 + 7)).join(' '));
    return out;
  }, dumpTicks);
  if (!rows) return;
  dumped = true;
  for (const r of rows) console.log(`history ${r}`);
  if (process.env.SYNCDUMP) {
    const peds = await page.evaluate(() => {
      const M = window.gta2.Module;
      const p = M.ccall('gta2_dev_sync_peds', 'number', [], []) >> 2;
      const n = M.HEAP32[p];
      const out = [];
      for (let i = 0; i < n; i++) out.push(Array.from(M.HEAP32.subarray(p + 1 + i * 5, p + 6 + i * 5)));
      return out;
    });
    peds.forEach((o, i) => console.log(`ped ${i} id ${o[0]} at (${o[1]}, ${o[2]}) sprite ${o[3]} type ${o[4]}`));
  }
};

// random presses of the original's default keys, as the native soak does (no test aids);
// PRESSES=0: none
const keys = ['ArrowUp', 'ArrowUp', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'ArrowDown', 'Control', 'Enter', 'Space'];
let seed = 7;
const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
const end = Date.now() + Number(seconds) * 1000;
let presses = 0;
let lastReport = 0;
const pressing = process.env.PRESSES !== '0';
while (Date.now() < end) {
  if (dumpTicks && !dumped) await dumpHistory();
  if (!pressing) { await page.waitForTimeout(1000); continue; }
  const key = keys[rnd(keys.length)];
  await page.keyboard.down(key);
  await page.waitForTimeout(200 + rnd(1500));
  await page.keyboard.up(key);
  presses++;
  if (Date.now() - lastReport > 30000) {
    lastReport = Date.now();
    const s = await page.evaluate(() => {
      const l = window.gta2Lobby;
      return { sent: l.stats.sent, received: l.stats.received, early: l.stats.early };
    });
    console.log(`[${stamp()}] ${presses} presses, game messages ${s.sent} sent, ${s.received} received`);
  }
}
const banner = await page.evaluate(() => document.getElementById('coop-banner')?.textContent || '');
console.log(`[${stamp()}] done, ${presses} presses; banner: "${banner}"`);
await page.screenshot({ path: process.env.SHOT || '/tmp/browser_peer.png' }).catch(() => {});
await browser.close();
