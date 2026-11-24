// A pretend card for testing the phone page in a browser: serves firmware/build/web/index.html
// and fakes the card's /api endpoints. Uploaded GIFs are saved to firmware/build/fake-card/.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const store = path.join(root, 'firmware/build/fake-card');
fs.mkdirSync(store, { recursive: true });
let brightness = 100;
const TOTAL = 0x460000;

const json = (res, o) => { res.writeHead(200, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(o)); };
const files = () => fs.readdirSync(store).filter(f => /.(gif|pjm)$/.test(f));
let settings = { tz: 'UTC0', nightFrom: -1, nightTo: -1, nightLevel: 0, theme: 2 };
const showFile = path.join(store, 'show.txt');
const datesFile = path.join(store, 'dates.txt');

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  const log = (...a) => console.log(new Date().toISOString().slice(11, 19), req.method, url.pathname, ...a);
  if (url.pathname === '/') { res.writeHead(200, { 'Content-Type': 'text/html' }); return res.end(fs.readFileSync(path.join(root, 'firmware/build/web/index.html'))); }
  if (url.pathname === '/wifi') { res.writeHead(200, { 'Content-Type': 'text/html' }); return res.end(fs.readFileSync(path.join(root, 'firmware/build/web/wifi.html'))); }
  if (url.pathname === '/api/scan') return json(res, [{ ssid: 'HomeNet', rssi: -48, open: false }, { ssid: 'Neighbor 5G', rssi: -77, open: false }, { ssid: 'CoffeeShop', rssi: -85, open: true }]);
  if (url.pathname === '/api/wifi' || url.pathname === '/api/forget-wifi') { let b = ''; req.on('data', c => b += c); req.on('end', () => { log(b); json(res, { ok: true }); }); return; }
  if (url.pathname === '/api/info') {
    const used = files().reduce((s, f) => s + fs.statSync(path.join(store, f)).size, 0);
    return json(res, { fw: '1.1.0-beta2', hwMin: 12, name: 'pocketjumbo-fake', place: 'Springfield', tzAuto: true, total: TOTAL, used, files: files().length, items: fs.existsSync(showFile) ? fs.readFileSync(showFile, 'utf8').split(String.fromCharCode(10)).filter(Boolean).length : files().length, brightness, ...settings, night: false, time: Math.floor(Date.now() / 1000), id: 'fa4e', rssi: -58, chip: 'ESP32-S3 rev 2', app: 1640000, appMax: 1835008, up: Math.floor(process.uptime()), boot: 'power', bootWhy: 'power on', crashes: 0, quietRestarts: 0, boots: 7, lifeCrashes: 1, lastCrash: 'watchdog, 1.0.0-beta18', heap: 120000, block: 31700, minHeap: 18000 });
  }
  if (url.pathname === '/api/clear' && (globalThis.busy = !globalThis.busy)) { log('503 busy'); res.writeHead(503); return res.end('busy'); }
  if (url.pathname === '/api/clear') { files().forEach(f => fs.unlinkSync(path.join(store, f))); fs.rmSync(showFile, { force: true }); log(); return json(res, { ok: true }); }
  if (url.pathname === '/api/dates' && req.method === 'GET') { res.writeHead(200, { 'Content-Type': 'text/plain' }); return res.end(fs.existsSync(datesFile) ? fs.readFileSync(datesFile) : ''); }
  if (url.pathname === '/api/dates') { let b = ''; req.on('data', c => b += c); req.on('end', () => { fs.writeFileSync(datesFile, b); log('dates', b.split(String.fromCharCode(10)).length - 1, 'lines'); json(res, { ok: true }); }); return; }
  if (url.pathname === '/api/show' && req.method === 'GET') { res.writeHead(200, { 'Content-Type': 'text/plain' }); return res.end(fs.existsSync(showFile) ? fs.readFileSync(showFile) : files().sort().map(f => 'pic	0	' + f + String.fromCharCode(10)).join('')); }
  if (url.pathname === '/api/show') { let b = ''; req.on('data', c => b += c); req.on('end', () => { fs.writeFileSync(showFile, b); log(b.split(String.fromCharCode(10)).length - 1, 'lines'); json(res, { ok: true }); }); return; }
  if (url.pathname === '/api/file') { const f = path.join(store, path.basename(url.searchParams.get('name') || '')); if (fs.existsSync(f)) { res.writeHead(200); return res.end(fs.readFileSync(f)); } res.writeHead(404); return res.end('not found'); }
  if (url.pathname === '/api/settings') { const q = url.searchParams; for (const [k, n] of [['tz', 'tz'], ['nf', 'nightFrom'], ['nt', 'nightTo'], ['nl', 'nightLevel'], ['fc', 'fc'], ['theme', 'theme'], ['wx', 'wxHome']]) if (q.has(k)) settings[n] = k === 'tz' || k === 'fc' || k === 'wx' ? q.get(k) : +q.get(k); log(JSON.stringify(settings)); return json(res, { ok: true }); }
  // Firmware info + files for the update check (serves docs/firmware/ when built locally).
  if (url.pathname.startsWith('/firmware/')) { const f = path.join(root, 'docs', path.normalize(url.pathname)); if (f.startsWith(path.join(root, 'docs', 'firmware')) && fs.existsSync(f)) { res.writeHead(200); return res.end(fs.readFileSync(f)); } }
  if (url.pathname === '/api/cal') {            // Color tune (the card's tune.ino), kept in memory
    const q = url.searchParams, c = globalThis.cal ??= { levels: [3.5, 7, 17.5, 35, 70, 140], gain: [[64, 100, 100], [64, 100, 100], [75, 100, 100], [100, 100, 100], [100, 100, 100], [100, 100, 100]], hwMin: 12, level: -1, pattern: 0, color: 'ffffff', on: false, now: 70, saved: true };
    if (req.method === 'POST') {
      if (q.has('level')) c.level = +q.get('level'); if (q.has('color')) c.color = q.get('color');
      const g = c.gain[c.level];
      if (g) { ['r', 'g', 'b'].forEach((k, i) => { if (q.has(k)) { g[i] = +q.get(k); c.saved = false; } });
        if (q.has('pick')) { const p = +q.get('pick'); g[0] = Math.round(g[0] * [0.7, 0.8, 0.9, 1, 1.12, 1.25, 1.4][p % 10]); g[2] = Math.round(g[2] * [1.25, 1.12, 1, 0.9, 0.8][Math.trunc(p / 10)]); c.saved = false; } }
      if (q.has('save')) c.saved = true;
      if (q.get('pattern') === '0') { c.on = false; c.level = -1; } else if (q.has('pattern')) { c.pattern = +q.get('pattern'); c.on = true; }
      log(url.search);
    }
    return json(res, { ...c, pattern: c.on ? c.pattern : 0 });
  }
  if (url.pathname === '/api/brightness') { brightness = +url.searchParams.get('v'); log(brightness); return json(res, { ok: true }); }
  if (url.pathname === '/api/play' || url.pathname === '/api/done') { log(); return json(res, { ok: true }); }
  if (url.pathname === '/api/upload' || url.pathname === '/api/update') {
    const chunks = [];
    req.on('data', c => chunks.push(c));
    req.on('end', () => {
      const body = Buffer.concat(chunks);
      const name = (/filename="([^"]+)"/.exec(body.toString('latin1')) || [])[1] || 'x.bin';
      const start = body.indexOf('\r\n\r\n') + 4;
      const boundary = body.slice(0, body.indexOf('\r\n'));
      const end = body.indexOf(boundary, start) - 2;
      if (url.pathname === '/api/upload') fs.writeFileSync(path.join(store, path.basename(name)), body.slice(start, end));
      log(name, end - start, 'bytes');
      json(res, { ok: true });
    });
    return;
  }
  // Picture packs and news/market feeds: serves docs/packs/ and docs/feeds/ (run build-packs / build-feeds first).
  if (url.pathname.startsWith('/packs/') || url.pathname.startsWith('/feeds/')) {
    const f = path.join(root, 'docs', path.normalize(decodeURIComponent(url.pathname)));
    if ((f.startsWith(path.join(root, 'docs', 'packs')) || f.startsWith(path.join(root, 'docs', 'feeds'))) &&fs.existsSync(f) && fs.statSync(f).isFile()) {
      res.writeHead(200, { 'Content-Type': f.endsWith('.json') ? 'application/json' : f.endsWith('.gif') ? 'image/gif' : f.endsWith('.webp') ? 'image/webp' : 'application/octet-stream' }); return res.end(fs.readFileSync(f)); }
  }
  // Test helper: /test/<file> serves sample media from media/ so the page can be fed files in tests.
  if (url.pathname.startsWith('/test/')) {
    const f = path.join(root, 'media', path.normalize(decodeURIComponent(url.pathname.slice(6))));
    if (f.startsWith(path.join(root, 'media')) && fs.existsSync(f)) { res.writeHead(200); return res.end(fs.readFileSync(f)); }
  }
  res.writeHead(404); res.end('not found');
}).listen(8090, () => console.log('fake card on http://localhost:8090'));
