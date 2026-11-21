// Shared by build-packs.mjs and build-install.mjs: forgiving pack folder reading.
// pack.json may be real JSON or simple "key: value" lines (title: Christmas 2026). Bad input warns, never fails CI.
import fs from 'node:fs';
import path from 'node:path';

export const PIC = /\.(gif|png|jpe?g|webp)$/i;

// "Christmas2026" -> "christmas2026", "Smith Family" -> "smith-family"
export const packId = (name) => name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');

// "200 (1).webp" -> "200-1.webp": safe in web addresses and on the card
export const safeName = (name) => {
  const ext = path.extname(name).toLowerCase(), base = path.basename(name, path.extname(name));
  return (base.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '') || 'picture') + ext;
};

// Natural name order: "200 (2)" before "200 (10)". No numbering needed.
const collator = new Intl.Collator('en', { numeric: true, sensitivity: 'base' });
export const byName = (a, b) => collator.compare(a, b);

export function readPackMeta(dir) {
  const file = path.join(dir, 'pack.json');
  if (!fs.existsSync(file)) return {};
  const text = fs.readFileSync(file, 'utf8').replace(/^﻿/, '').trim();
  if (!text) return {};
  try { return JSON.parse(text); } catch { /* fall through to key: value lines */ }
  const meta = {};
  for (const line of text.split(/\r?\n/)) {
    const m = line.match(/^\s*"?(\w+)"?\s*[:=]\s*(.*?)\s*,?\s*$/);
    if (!m) continue;
    let v = m[2].replace(/^"(.*)"$/, '$1');
    const key = m[1].toLowerCase();
    if (key === 'cards') v = v.replace(/[[\]"]/g, '').split(/[,\s]+/).filter(Boolean);
    else if (key === 'order') v = Number(v) || 50;
    else if (key === 'install') v = /^(true|yes|1)$/i.test(v);
    meta[key] = v;
  }
  if (!Object.keys(meta).length) console.warn(`warning: ${file} could not be read; using defaults`);
  return meta;
}
