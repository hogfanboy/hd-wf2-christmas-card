// Builds the picture packs the card's web page offers under "Get the latest Pocket Jumbotron pictures".
// Usage: node tools/build-packs.mjs [more source folders...]
//   Sources: content/ in this repo (normally empty) plus each folder given, e.g. CI's checkout of the private
//   repo hogfanboy/pocket-jumbotron-content, so the pictures aren't on public GitHub.
//   <source>/<pack folder>/        pictures (gif, png, jpg, webp; animated is fine) + optional pack.json
//   <source>/<pack folder>/pack.json  { "title": "...", "about": "...", "cards": ["PocketJumbo-EED0"], "order": 10 }
//                                  "cards" = only those screens see it (per-family messages). Lower "order" first.
//                                  "format": "gif" | "full" (else auto) and "pictures": { "<file>": { "fit", "format" } }
//                                  tell phones how to convert (set in the pack builder); passed on as format / pics.
//   <source>/<picture>             loose pictures = the "New pictures" pack
// Forgiving: any folder name works (Christmas2026 -> id christmas2026), file names are made web-safe
// (200 (1).webp -> 200-1.webp), pictures play in natural name order (no numbering needed), pack.json may be
// JSON or "title: ..." lines, and a broken pack is skipped with a warning instead of failing the build.
// Output: docs/packs/<pack-id>/<files> + docs/packs/manifest.json (published by GitHub Actions, not committed).
// The website is public: anyone with a picture's address can download it. Private here = not on GitHub.
// Also adds the generated "holiday-starter" pack (our own animations from docs/panel.js).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PIC, packId, safeName, byName, readPackMeta } from './pack-meta.mjs';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(root, 'docs/packs');
const sources = [path.join(root, 'content'), ...process.argv.slice(2).map(p => path.resolve(p))].filter(p => fs.existsSync(p));
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });

const packs = [];
execFileSync(process.execPath, [path.join(root, 'tools/make-starter.mjs'), path.join(out, 'holiday-starter')], { stdio: 'inherit' });
packs.push({ id: 'holiday-starter', title: 'Holiday starter animations', about: 'Snow, greetings and color tests.', order: 90,
  files: fs.readdirSync(path.join(out, 'holiday-starter')).filter(f => PIC.test(f)).sort() });

const isFile = (p) => fs.statSync(p).isFile();
// Copies pictures into docs/packs/<id>/ under web-safe, unique names; returns the names in play order.
// rename (optional) collects source file name -> published name.
function copyPics(paths, id, rename = {}) {
  fs.mkdirSync(path.join(out, id), { recursive: true });
  const names = [];
  for (const f of [...paths].sort((a, b) => byName(path.basename(a), path.basename(b)))) {
    const first = safeName(path.basename(f));
    let n = first, i = 2;
    while (names.includes(n)) n = first.replace(/(\.\w+)$/, `-${i++}$1`);
    fs.copyFileSync(f, path.join(out, id, n));
    rename[path.basename(f)] = n;
    names.push(n);
  }
  return names;
}

// Pictures dropped straight into a source folder become the "new-pictures" pack.
const loose = [];
for (const src of sources) for (const f of fs.readdirSync(src)) if (PIC.test(f) && isFile(path.join(src, f))) loose.push(path.join(src, f));
if (loose.length) packs.push({ id: 'new-pictures', title: 'New pictures', about: 'The latest additions.', order: 10,
  files: copyPics(loose, 'new-pictures') });

for (const src of sources) for (const folder of fs.readdirSync(src).sort(byName)) {
  const dir = path.join(src, folder);
  if (!fs.statSync(dir).isDirectory() || folder.startsWith('_') || folder.startsWith('.')) continue;
  try {
    const id = packId(folder);
    if (!id) throw new Error('folder name has no letters or digits');
    if (packs.some(p => p.id === id)) throw new Error(`a pack called "${id}" already exists`);
    const meta = readPackMeta(dir);
    const pics = fs.readdirSync(dir).filter(f => PIC.test(f) && isFile(path.join(dir, f))).map(f => path.join(dir, f));
    if (!pics.length) { console.warn(`warning: pack folder "${folder}" has no pictures; skipped`); continue; }
    const rename = {}, files = copyPics(pics, id, rename), conv = {};
    for (const [src, s] of Object.entries(meta.pictures || {})) {
      if (!rename[src] || !s || typeof s !== 'object') continue;
      const c = {}; if (s.fit === 'fill' || s.fit === 'fit') c.fit = s.fit; if (s.format === 'gif' || s.format === 'full') c.format = s.format;
      if (Object.keys(c).length) conv[rename[src]] = c;
    }
    packs.push({ id, title: meta.title || folder, about: meta.about || '', ...(meta.cards ? { cards: [].concat(meta.cards) } : {}),
      ...(meta.format === 'gif' || meta.format === 'full' ? { format: meta.format } : {}), ...(Object.keys(conv).length ? { pics: conv } : {}),
      order: Number(meta.order) || 50, files });
  } catch (e) {
    console.warn(`warning: pack folder "${folder}" skipped: ${e.message}`);
  }
}

packs.sort((a, b) => a.order - b.order || a.id.localeCompare(b.id));
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({ built: new Date().toISOString(), packs: packs.map(({ order, ...p }) => p) }, null, 2));
for (const p of packs) console.log(`pack ${p.id}: ${p.files.length} files${p.cards ? ' (only ' + p.cards.join(', ') + ')' : ''}`);
