// Builds the picture packs the card's web page offers under "Get the latest Pocket Jumbotron pictures".
//   content/<pack-id>/            pictures (gif, png, jpg, webp; animated is fine) + optional pack.json
//   content/<pack-id>/pack.json   { "title": "...", "about": "...", "cards": ["PocketJumbo-EED0"], "order": 10 }
//                                 "cards" = only those screens see it (per-family messages). Lower "order" first.
// Output: docs/packs/<pack-id>/<files> + docs/packs/manifest.json (published by GitHub Actions, not committed).
// Also adds the generated "holiday-starter" pack (our own animations from docs/panel.js).
// Everything here is PUBLIC on the website: only your own or royalty-free pictures.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const src = path.join(root, 'content'), out = path.join(root, 'docs/packs');
const PIC = /\.(gif|png|jpe?g|webp)$/i;
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });

const packs = [];
execFileSync(process.execPath, [path.join(root, 'tools/make-starter.mjs'), path.join(out, 'holiday-starter')], { stdio: 'inherit' });
packs.push({ id: 'holiday-starter', title: 'Holiday starter animations', about: 'Snow, greetings and color tests.', order: 90,
  files: fs.readdirSync(path.join(out, 'holiday-starter')).filter(f => PIC.test(f)).sort() });

// Pictures dropped straight into content/ (e.g. uploaded on github.com) become the "new-pictures" pack.
const loose = fs.existsSync(src) ? fs.readdirSync(src).filter(f => PIC.test(f) && fs.statSync(path.join(src, f)).isFile()).sort() : [];
if (loose.length) {
  fs.mkdirSync(path.join(out, 'new-pictures'));
  for (const f of loose) fs.copyFileSync(path.join(src, f), path.join(out, 'new-pictures', f));
  packs.push({ id: 'new-pictures', title: 'New pictures', about: 'The latest additions.', order: 10, files: loose });
}

if (fs.existsSync(src)) for (const id of fs.readdirSync(src).sort()) {
  const dir = path.join(src, id);
  if (!fs.statSync(dir).isDirectory() || id.startsWith('_')) continue;
  if (!/^[a-z0-9-]+$/.test(id)) throw new Error(`pack folder "${id}": use lowercase letters, digits and dashes`);
  const meta = fs.existsSync(path.join(dir, 'pack.json')) ? JSON.parse(fs.readFileSync(path.join(dir, 'pack.json'), 'utf8')) : {};
  const files = fs.readdirSync(dir).filter(f => PIC.test(f)).sort();
  if (!files.length) continue;
  fs.mkdirSync(path.join(out, id));
  for (const f of files) fs.copyFileSync(path.join(dir, f), path.join(out, id, f));
  packs.push({ id, title: meta.title || id, about: meta.about || '', ...(meta.cards ? { cards: meta.cards } : {}), order: meta.order ?? 50, files });
}

packs.sort((a, b) => a.order - b.order || a.id.localeCompare(b.id));
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({ built: new Date().toISOString(), packs: packs.map(({ order, ...p }) => p) }, null, 2));
for (const p of packs) console.log(`pack ${p.id}: ${p.files.length} files${p.cards ? ' (only ' + p.cards.join(', ') + ')' : ''}`);
