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
const files = () => fs.readdirSync(store).filter(f => f.endsWith('.gif'));

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  const log = (...a) => console.log(new Date().toISOString().slice(11, 19), req.method, url.pathname, ...a);
  if (url.pathname === '/') { res.writeHead(200, { 'Content-Type': 'text/html' }); return res.end(fs.readFileSync(path.join(root, 'firmware/build/web/index.html'))); }
  if (url.pathname === '/wifi') { res.writeHead(200, { 'Content-Type': 'text/html' }); return res.end(fs.readFileSync(path.join(root, 'firmware/build/web/wifi.html'))); }
  if (url.pathname === '/api/scan') return json(res, [{ ssid: 'HomeNet', rssi: -48, open: false }, { ssid: 'Neighbor 5G', rssi: -77, open: false }, { ssid: 'CoffeeShop', rssi: -85, open: true }]);
  if (url.pathname === '/api/wifi' || url.pathname === '/api/forget-wifi') { let b = ''; req.on('data', c => b += c); req.on('end', () => { log(b); json(res, { ok: true }); }); return; }
  if (url.pathname === '/api/info') {
    const used = files().reduce((s, f) => s + fs.statSync(path.join(store, f)).size, 0);
    return json(res, { fw: 'fake', total: TOTAL, used, files: files().length, brightness });
  }
  if (url.pathname === '/api/clear' && (globalThis.busy = !globalThis.busy)) { log('503 busy'); res.writeHead(503); return res.end('busy'); }
  if (url.pathname === '/api/clear') { files().forEach(f => fs.unlinkSync(path.join(store, f))); log(); return json(res, { ok: true }); }
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
  // Test helper: /test/<file> serves sample media from media/ so the page can be fed files in tests.
  if (url.pathname.startsWith('/test/')) {
    const f = path.join(root, 'media', path.normalize(decodeURIComponent(url.pathname.slice(6))));
    if (f.startsWith(path.join(root, 'media')) && fs.existsSync(f)) { res.writeHead(200); return res.end(fs.readFileSync(f)); }
  }
  res.writeHead(404); res.end('not found');
}).listen(8090, () => console.log('fake card on http://localhost:8090'));
