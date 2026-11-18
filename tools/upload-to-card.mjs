// Replace all pictures on a card over WiFi, using the same API as the phone page's "Send" button.
// Usage: node tools/upload-to-card.mjs <card-ip> <folder-or-gif>...
import fs from 'node:fs';
import path from 'node:path';

const [ip, ...inputs] = process.argv.slice(2);
if (!ip || !inputs.length) { console.error('usage: node upload-to-card.mjs <card-ip> <folder-or-gif>...'); process.exit(1); }
const base = `http://${ip}`;
const files = inputs.flatMap(p => fs.statSync(p).isDirectory()
  ? fs.readdirSync(p).filter(f => f.endsWith('.gif')).sort().map(f => path.join(p, f)) : [p]);

const info = await (await fetch(`${base}/api/info`)).json();
console.log(`Card fw ${info.fw}, ${info.files} pictures, heap ${info.heap}`);
for (let tries = 0; ; tries++) {
  const r = await fetch(`${base}/api/clear`, { method: 'POST' });
  if (r.ok) break;
  if (r.status !== 503 || tries > 40) throw new Error(`clear failed: ${r.status} ${await r.text()}`);
  await new Promise(res => setTimeout(res, 300));
}
let n = 0, bytes = 0;
const t0 = Date.now();
for (const f of files) {
  const fd = new FormData();
  const name = String(++n).padStart(2, '0') + '-' + path.basename(f).slice(0, 20);
  fd.append('file', new Blob([fs.readFileSync(f)], { type: 'image/gif' }), name.endsWith('.gif') ? name : name + '.gif');
  const r = await fetch(`${base}/api/upload`, { method: 'POST', body: fd });
  const size = fs.statSync(f).size; bytes += size;
  console.log(`${String(n).padStart(3)}/${files.length} ${r.ok ? 'ok ' : 'ERR'} ${(size / 1024).toFixed(0).padStart(4)} KB ${path.basename(f)}${r.ok ? '' : ' -> ' + await r.text()}`);
  if (!r.ok) break;
}
await fetch(`${base}/api/play`, { method: 'POST' });
const after = await (await fetch(`${base}/api/info`)).json();
console.log(`Done: ${after.files} pictures, ${(bytes / 1048576).toFixed(2)} MB in ${((Date.now() - t0) / 1000).toFixed(0)} s, card heap ${after.heap}`);
