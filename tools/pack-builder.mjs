// Pack builder (just for the owner, runs on this PC): a page at http://localhost:8095 to make and edit picture
// packs in the private content repo (../pocket-jumbotron-content) without hand-made folders or pack.json typos.
// Drag pictures in, set title / about / which screens / list order, put them in order, see each one exactly as
// a phone will convert it (same code: web/convert.js), then Publish: commits + pushes the content repo with your
// own git login and asks GitHub to rebuild the website now (gh), instead of waiting for the hourly run.
//   node tools/pack-builder.mjs [content repo folder]
// Only answers on this computer (127.0.0.1). Nothing here is part of the website.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { execFile } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PIC, safeName, byName, readPackMeta } from './pack-meta.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const repo = path.resolve(process.argv[2] || path.join(root, '../pocket-jumbotron-content'));
const PORT = 8095, MAX_UPLOAD = 40 << 20;
const GH = path.join(process.env.LOCALAPPDATA || '', 'Programs/gh/bin/gh.exe');
if (!fs.existsSync(repo)) { console.error('content repo not found: ' + repo); process.exit(1); }

// The same libraries + conversion code the card app inlines (tools/build-web.mjs).
function vendor() {
  const nm = path.join(here, 'node_modules');
  const omggif = fs.readFileSync(path.join(nm, 'omggif/omggif.js'), 'utf8');
  let gifenc = fs.readFileSync(path.join(nm, 'gifenc/dist/gifenc.esm.js'), 'utf8').replace(/\/\/# sourceMappingURL.*$/m, '');
  gifenc = gifenc.replace(/export\{([^}]*)\};?/, (_, list) => `window.gifenc={${list.split(',').map(s => s.trim().split(/\s+as\s+/)).map(([l, n]) => `${n || l}:${l}`).join(',')}};`);
  const jpegjs = fs.readFileSync(path.join(nm, 'jpeg-js/lib/encoder.js'), 'utf8');
  const convert = fs.readFileSync(path.join(root, 'web/convert.js'), 'utf8');
  return `<script>${omggif}</script>\n<script>(function(){${gifenc}})();</script>\n<script>(function(){var module;${jpegjs}})();</script>\n<script>${convert}</script>`;
}

// ---- the content repo ----
const LOOSE = '';                                   // loose pictures in the repo root = the "New pictures" pack
const packDir = (id) => {
  if (id === LOOSE) return repo;
  if (!/^[^/\\:*?"<>|]+$/.test(id) || id.startsWith('.') || id.includes('..')) throw new Error('bad pack name');
  return path.join(repo, id);
};
const pics = (dir) => fs.readdirSync(dir).filter(f => PIC.test(f) && fs.statSync(path.join(dir, f)).isFile()).sort(byName);
function listPacks() {
  const out = [];
  const loose = pics(repo);
  if (loose.length) out.push({ id: LOOSE, folder: '(loose pictures)', meta: { title: 'New pictures' }, loose: true, files: loose.map(f => ({ name: f, size: fs.statSync(path.join(repo, f)).size })) });
  for (const d of fs.readdirSync(repo).sort(byName)) {
    const dir = path.join(repo, d);
    if (d.startsWith('.') || !fs.statSync(dir).isDirectory()) continue;
    out.push({ id: d, folder: d, skipped: d.startsWith('_'), meta: readPackMeta(dir), files: pics(dir).map(f => ({ name: f, size: fs.statSync(path.join(dir, f)).size })) });
  }
  return out;
}
// Play order = natural name order, so ordering renames files to 01-name, 02-name... (two steps: no clashes).
function setOrder(id, names) {
  const dir = packDir(id), have = pics(dir);
  if (names.length !== have.length || names.some(n => !have.includes(n))) throw new Error('the list changed; reload');
  const w = String(names.length).length < 2 ? 2 : String(names.length).length;
  const final = names.map((n, i) => String(i + 1).padStart(w, '0') + '-' + n.replace(/^\d{2,3}-/, ''));   // our own old prefix
  names.forEach((n, i) => fs.renameSync(path.join(dir, n), path.join(dir, `.tmp-${i}-${n}`)));
  names.forEach((n, i) => fs.renameSync(path.join(dir, `.tmp-${i}-${n}`), path.join(dir, final[i])));
  return final;
}
const run = (cmd, args) => new Promise((ok) => execFile(cmd, args, { cwd: repo, windowsHide: true }, (e, so, se) => ok({ ok: !e, out: (so + se).trim() })));
async function publish(msg) {
  const log = [];
  const step = async (cmd, args) => { const r = await run(cmd, args); log.push('$ ' + path.basename(cmd) + ' ' + args.join(' ') + (r.out ? '\n' + r.out : '')); return r; };
  await step('git', ['add', '-A']);
  const st = await run('git', ['status', '--porcelain']);
  if (st.out) { const c = await step('git', ['commit', '-m', msg || 'Pack builder update']); if (!c.ok) return { ok: false, log }; }
  const p = await step('git', ['push']);
  if (!p.ok) return { ok: false, log };
  let site = false;
  if (fs.existsSync(GH)) site = (await step(GH, ['workflow', 'run', 'build-and-publish.yml', '-R', 'hogfanboy/hd-wf2-christmas-card'])).ok;
  return { ok: true, site, log };
}

// ---- server ----
const json = (res, o, code = 200) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(o)); };
const body = (req) => new Promise((ok, bad) => {
  const parts = []; let n = 0;
  req.on('data', c => { n += c.length; if (n > MAX_UPLOAD) { bad(new Error('file too big')); req.destroy(); } else parts.push(c); });
  req.on('end', () => ok(Buffer.concat(parts))); req.on('error', bad);
});
http.createServer(async (req, res) => {
  try {
    // Only this computer's own pages (stops other web pages from poking at the repo through the browser).
    if (!/^(localhost|127\.0\.0\.1):\d+$/.test(req.headers.host || '') || (req.headers.origin && !/^http:\/\/(localhost|127\.0\.0\.1):\d+$/.test(req.headers.origin))) return json(res, { error: 'local only' }, 403);
    const u = new URL(req.url, 'http://x'), q = (k) => u.searchParams.get(k) ?? '';
    if (u.pathname === '/') {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      return res.end(fs.readFileSync(path.join(here, 'pack-builder.html'), 'utf8').replace('<!--VENDOR-->', () => vendor()));
    }
    if (u.pathname === '/api/packs') return json(res, { repo, packs: listPacks(), status: (await run('git', ['status', '--porcelain'])).out });
    if (u.pathname === '/api/file') {
      const f = path.join(packDir(q('pack')), path.basename(q('name')));
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Cache-Control': 'no-store' }); return res.end(fs.readFileSync(f));
    }
    if (req.method !== 'POST') return json(res, { error: 'not found' }, 404);
    if (u.pathname === '/api/new') {
      const name = q('name').trim();
      if (!name || /[/\\:*?"<>|]/.test(name) || name.startsWith('.') || name.startsWith('_')) throw new Error('pick a folder name without / \\ : * ? " < > | (and not starting with . or _)');
      const dir = packDir(name); if (fs.existsSync(dir)) throw new Error('that folder already exists');
      fs.mkdirSync(dir); fs.writeFileSync(path.join(dir, 'pack.json'), JSON.stringify({ title: q('title') || name }, null, 2) + '\n');
      return json(res, { ok: true, id: name });
    }
    if (u.pathname === '/api/meta') {
      const m = JSON.parse((await body(req)).toString('utf8')), o = {};
      if (m.title) o.title = String(m.title).trim(); if (m.about) o.about = String(m.about).trim();
      const cards = String(m.cards || '').split(/[,\s]+/).filter(Boolean); if (cards.length) o.cards = cards;
      if (m.order !== '' && m.order != null && !isNaN(+m.order)) o.order = +m.order;
      fs.writeFileSync(path.join(packDir(q('pack')), 'pack.json'), JSON.stringify(o, null, 2) + '\n');
      return json(res, { ok: true });
    }
    if (u.pathname === '/api/upload') {
      const dir = packDir(q('pack')), data = await body(req);
      let name = safeName(q('name')); if (!PIC.test(name)) throw new Error('packs take gif, png, jpg or webp');
      const base = name; for (let i = 2; fs.existsSync(path.join(dir, name)); i++) name = base.replace(/(\.\w+)$/, `-${i}$1`);
      fs.writeFileSync(path.join(dir, name), data);
      return json(res, { ok: true, name });
    }
    if (u.pathname === '/api/order') return json(res, { ok: true, names: setOrder(q('pack'), JSON.parse((await body(req)).toString('utf8'))) });
    if (u.pathname === '/api/delete') { fs.unlinkSync(path.join(packDir(q('pack')), path.basename(q('name')))); return json(res, { ok: true }); }
    if (u.pathname === '/api/publish') return json(res, await publish(q('msg')));
    json(res, { error: 'not found' }, 404);
  } catch (e) { json(res, { error: e.message }, 400); }
}).listen(PORT, '127.0.0.1', () => console.log(`Pack builder: http://localhost:${PORT}  (content repo ${repo})`));
