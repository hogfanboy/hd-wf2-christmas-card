// Turns a TTF into an Adafruit GFX font header (1-bit, printable ASCII) for the panel.
//   node tools/make-font.mjs <font.ttf> <name> <capHeightPx> <out.h>
// Each glyph is rendered by sharp (Pango) between two H's, so the H's flat bottom gives the baseline and the
// trailing H's position gives the advance width. Glyphs are thresholded at 50% (the panel text is 1-bit).
import sharp from 'sharp';
import fs from 'node:fs';

const [ttf, name, capArg, out] = process.argv.slice(2);
if (!out) { console.log('usage: node tools/make-font.mjs <font.ttf> <name> <capHeightPx> <out.h>'); process.exit(1); }
const N = 4, SP = ' '.repeat(N);
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

async function render(text, dpi) {
  const { data, info } = await sharp({ text: { text: esc(text), font: name, fontfile: ttf, dpi } })
    .raw().toBuffer({ resolveWithObject: true });
  const w = info.width, h = info.height, c = info.channels;
  const ink = (x, y) => { const i = (y * w + x) * c; return (c === 4 ? data[i + 3] : data[i]) >= 128; };
  const cols = [];
  for (let x = 0; x < w; x++) { let any = false; for (let y = 0; y < h; y++) if (ink(x, y)) { any = true; break; } cols.push(any); }
  const segs = [];                 // runs of inked columns
  for (let x = 0; x < w; x++) if (cols[x]) { if (segs.length && x - segs[segs.length - 1][1] <= 1) segs[segs.length - 1][1] = x; else segs.push([x, x]); }
  return { w, h, ink, segs };
}
const box = (r, x0, x1) => {
  let t = 1e9, b = -1, l = 1e9, rr = -1;
  for (let y = 0; y < r.h; y++) for (let x = x0; x <= x1; x++) if (r.ink(x, y)) { t = Math.min(t, y); b = Math.max(b, y); l = Math.min(l, x); rr = Math.max(rr, x); }
  return b < 0 ? null : { t, b, l, r: rr };
};

// Find the dpi that gives the wanted cap height.
const cap = +capArg;
let dpi = 72 * 3;
for (let i = 0; i < 8; i++) {
  const r = await render('H', dpi);
  if (r.h === cap) break;
  dpi = Math.round(dpi * cap / r.h);
}

const ref1 = await render('H' + SP + 'H', dpi), ref2 = await render('H' + SP + SP + 'H', dpi);
const xA = ref1.segs[ref1.segs.length - 1][0], xRef = ref2.segs[ref2.segs.length - 1][0];
const LB = 1;                      // left bearing given to H (only shifts every glyph alike)
const glyphs = [], bits = [];
let yMin = 0, yMax = 0;
for (let code = 32; code <= 126; code++) {
  const ch = String.fromCharCode(code);
  const r = await render('H' + SP + ch + SP + 'H', dpi);
  const h1 = r.segs[0], h2 = r.segs[r.segs.length - 1];
  const base = box(r, h1[0], h1[1]).b + 1;                     // first row below the H = baseline
  const adv = h2[0] - xRef;
  const g = r.segs.length > 2 ? box(r, r.segs[1][0], r.segs[r.segs.length - 2][1]) : null;
  const start = bits.length;
  let gw = 0, gh = 0, xo = 0, yo = 0;
  if (g && ch !== ' ') {
    gw = g.r - g.l + 1; gh = g.b - g.t + 1; xo = g.l - xA + LB; yo = g.t - base;
    for (let y = g.t; y <= g.b; y++) for (let x = g.l; x <= g.r; x++) bits.push(r.ink(x, y) ? 1 : 0);
    yMin = Math.min(yMin, yo); yMax = Math.max(yMax, yo + gh);
  }
  glyphs.push({ off: start, gw, gh, adv: Math.max(adv, 1), xo, yo });
}
// Pack: GFX glyphs start on a byte boundary.
const bytes = [];
for (const g of glyphs) {
  const bitsOf = bits.slice(g.off, g.off + g.gw * g.gh);
  g.off = bytes.length;
  for (let i = 0; i < bitsOf.length; i += 8) { let v = 0; for (let k = 0; k < 8; k++) v = (v << 1) | (bitsOf[i + k] || 0); bytes.push(v); }
}
const id = `${name.replace(/\W/g, '')}${cap}`;
let s = `// ${name}, cap height ${cap} px, made by tools/make-font.mjs. ${bytes.length} bytes of glyphs.\n#pragma once\n`;
s += `const uint8_t ${id}Bitmaps[] PROGMEM = {\n`;
for (let i = 0; i < bytes.length; i += 16) s += '  ' + bytes.slice(i, i + 16).map(b => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',\n';
s += `};\nconst GFXglyph ${id}Glyphs[] PROGMEM = {\n`;
glyphs.forEach((g, i) => { s += `  {${g.off}, ${g.gw}, ${g.gh}, ${g.adv}, ${g.xo}, ${g.yo}},   // '${String.fromCharCode(32 + i).replace('\\', 'backslash')}'\n`; });
s += `};\nconst GFXfont ${id} PROGMEM = {(uint8_t *)${id}Bitmaps, (GFXglyph *)${id}Glyphs, 0x20, 0x7E, ${yMax - yMin + 2}};\n`;
fs.writeFileSync(out, s);
console.log(`${out}: dpi ${dpi}, ${bytes.length} bytes, digits "0" adv ${glyphs[16].adv}, ":" adv ${glyphs[26].adv}`);
