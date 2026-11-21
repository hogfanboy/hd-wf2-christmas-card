// Draws our weather icons and writes them as sprites for the card: firmware/pocket_jumbotron/wx_icons.h
// (RGB565 + alpha per pixel) and web/wxicons.js (the same pixels for the page preview).
// Crisp pixel art: every pixel is fully on or off (no soft or shaded edges, no glow) in a few flat, saturated
// colors, because LEDs look best sharp with black around everything. Clouds get a flat second tone underneath.
// Rain, snow, twinkles, drifting and the lightning flash are animated by the firmware on top of these.
//   node tools/make-wxicons.mjs [preview.png]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const SS = 5;                                    // subsamples per pixel side (decides coverage, not blending)
const ON = 0.45;                                 // a pixel lights when this much of it is covered

// A sprite canvas: shapes paint flat colors on subsamples; each pixel then takes its most common color
// (never a blend of two) and lights only if covered enough.
function sprite(w, h) {
  const W = w * SS, H = h * SS, c = new Int32Array(W * H).fill(-1);
  return {
    w, h,
    paint(fn) {                                   // fn(x, y) -> 0xRRGGBB | null, x/y in sprite pixels
      for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
        const v = fn((i + 0.5) / SS, (j + 0.5) / SS); if (v != null) c[j * W + i] = v;
      }
    },
    out() {
      const px = [];
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const n = new Map(); let on = 0;
        for (let j = 0; j < SS; j++) for (let i = 0; i < SS; i++) {
          const v = c[(y * SS + j) * W + x * SS + i]; if (v < 0) continue;
          on++; n.set(v, (n.get(v) || 0) + 1);
        }
        if (on < ON * SS * SS) { px.push([0, 0, 0, 0]); continue; }
        let best = 0, bc = 0; for (const [k, v] of n) if (v > bc) { bc = v; best = k; }
        px.push([(best >> 16) & 255, (best >> 8) & 255, best & 255, 1]);
      }
      return px;
    },
  };
}

// Sun: a round gold disc and 8 straight orange rays, laid out on whole pixels so they stay clean: 2 px wide on the
// axes, a 2 px staircase on the diagonals. f (0..5) makes the rays breathe in and out (long on the axes while the
// diagonals are short, then the other way), so it moves without turning (turned rays get ragged at this size).
// cx, cy sit on a pixel corner, so everything is symmetric.
function sun(S, cx, cy, r, f) {
  const DISC = 0xFFC800, RAY = 0xFF7A00, big = r > 5;
  const wave = Math.round(Math.sin((f / 6) * 2 * Math.PI));        // -1, 0, 1
  const gap = big ? 2 : 1, aLen = big ? 5 + wave : 2.5, dLen = big ? 4 - wave : 2;
  S.paint((x, y) => {
    const u = Math.floor(x) + 0.5 - cx, v = Math.floor(y) + 0.5 - cy;   // this pixel's center
    const d = Math.hypot(u, v);
    if (d <= r) return DISC;
    const au = Math.abs(u), av = Math.abs(v);
    if ((au < 1 && av > r + gap - 0.5 && av < r + gap + aLen) || (av < 1 && au > r + gap - 0.5 && au < r + gap + aLen)) return RAY;
    const along = (au + av) / Math.SQRT2;
    const k = au - av;
    if ((k === 0 || (big && k === 1)) && along > r + gap - 0.3 && along < r + gap + dLen + 0.6) return RAY;
    return null;
  });
}
// Moon: a pale yellow crescent (cut out, so effects show through).
function moon(S, cx, cy, r) {
  S.paint((x, y) => {
    if (Math.hypot(x - cx, y - cy) > r) return null;
    if (Math.hypot(x - (cx + r * 0.58), y - (cy - r * 0.42)) < r * 0.88) return null;
    return 0xFFE98A;
  });
}
// Cloud: round puffs on a flat base; the top color, then a flat second color on the lower part.
function cloud(S, cx, cy, sc, top, bottom) {
  const puffs = [[-8, 3, 6.5], [0, -3, 9.5], [10, 0, 8], [16, 5, 4.5]].map(([x, y, r]) => [cx + x * sc, cy + y * sc, r * sc]);
  const bx0 = cx - 9 * sc, bx1 = cx + 16 * sc, by = cy + 5 * sc, br = 4.5 * sc;
  const inside = (x, y) => {
    for (const [px, py, r] of puffs) if (Math.hypot(x - px, y - py) < r) return true;
    const qx = Math.max(bx0, Math.min(bx1, x));
    return Math.hypot(x - qx, y - by) < br;
  };
  const split = cy + 3.2 * sc;                    // where the lower color starts
  S.paint((x, y) => (inside(x, y) ? (y > split ? bottom : top) : null));
}
function bolt(S, ox, oy) {
  const P = [[7, 0], [2, 9], [6, 9], [1, 18], [11, 6], [7, 6], [11, 0]].map(([x, y]) => [ox + x, oy + y]);
  const inPoly = (x, y) => { let c = false; for (let i = 0, j = P.length - 1; i < P.length; j = i++) { const [xi, yi] = P[i], [xj, yj] = P[j]; if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c; } return c; };
  S.paint((x, y) => (inPoly(x, y) ? 0xFFE600 : null));
}

// Cloud colors: [top, underside]. Bright clouds are white over sky blue; rain clouds steel blue; storm slate.
const C_WHITE = [0xFFFFFF, 0x9CC8FF], C_RAIN = [0xC8D2E6, 0x6E8CC8], C_STORM = [0x8A92B0, 0x50587C], C_BACK = [0xD6DEEE, 0x8EA6D2];

const SPR = [];                                    // [name, w, h, ox, oy, px] (ox/oy: where the sprite's 0,0 sits from the anchor)
function add(name, w, h, draw) { const S = sprite(w, h); draw(S); SPR.push([name, w, h, -Math.floor(w / 2), -Math.floor(h / 2), S.out()]); }
const SUN_FRAMES = 6;
for (let f = 0; f < SUN_FRAMES; f++) add(`SUN${f}`, 40, 40, (S) => sun(S, 20, 20, 9, f));
for (let f = 0; f < SUN_FRAMES; f++) add(`SUNS${f}`, 30, 30, (S) => sun(S, 15, 15, 6.5, f));
add('MOON', 36, 36, (S) => moon(S, 18, 18, 10.5));
add('MOONS', 26, 26, (S) => moon(S, 13, 13, 7.5));
add('CLOUD_L', 38, 26, (S) => cloud(S, 16, 14, 1, ...C_WHITE));
add('CLOUD_M', 38, 26, (S) => cloud(S, 16, 14, 1, ...C_RAIN));
add('CLOUD_D', 38, 26, (S) => cloud(S, 16, 14, 1, ...C_STORM));
add('CLOUD_B', 30, 21, (S) => cloud(S, 13, 11, 0.78, ...C_BACK));
add('BOLT', 16, 22, (S) => bolt(S, 2, 2));
// Forecast icons (column 25 px wide): sun, partly, cloudy, fog, rain, snow, storm (particles added on the card).
const MINI = [
  (S) => sun(S, 12, 9, 4.2, 0),
  (S) => { sun(S, 9, 6, 3.4, 0); cloud(S, 11, 10, 0.5, ...C_WHITE); },
  (S) => { cloud(S, 8, 6, 0.42, ...C_BACK); cloud(S, 11, 10, 0.5, ...C_WHITE); },
  (S) => { cloud(S, 10, 6, 0.5, ...C_RAIN); },
  (S) => { cloud(S, 10, 6, 0.5, ...C_RAIN); },
  (S) => { cloud(S, 10, 6, 0.5, ...C_WHITE); },
  (S) => { cloud(S, 10, 6, 0.5, ...C_STORM); },
];
MINI.forEach((d, i) => add(`MINI${i}`, 24, 18, d));

// ---- header ----
let h = `// Generated by tools/make-wxicons.mjs - do not edit. Weather icon sprites: per pixel alpha, then RGB565 (hi, lo).
#pragma once
#include <stdint.h>
struct WxSprite { uint8_t w, h; int8_t ox, oy; const uint8_t *px; };
`;
let bytes = 0;
for (const [name, w, hh, , , px] of SPR) {
  let s = '';
  px.forEach(([r, g, b, a], i) => {
    const A = Math.round(a * 255), c = A ? ((Math.round(r) >> 3) << 11) | ((Math.round(g) >> 2) << 5) | (Math.round(b) >> 3) : 0;
    s += (i % 8 === 0 ? '\n  ' : '') + `${A},${c >> 8},${c & 255},`;
  });
  bytes += px.length * 3;
  h += `static const uint8_t WX_${name}_PX[] = {${s}\n};\n`;
}
h += `static const WxSprite WX_SPR[] = {\n${SPR.map(([n, w, hh, ox, oy]) => `  {${w}, ${hh}, ${ox}, ${oy}, WX_${n}_PX},   // ${n}`).join('\n')}\n};\n`;
h += `enum { ${SPR.map(([n], i) => `WXS_${n} = ${i}`).join(', ')} };\n`;
fs.writeFileSync(path.join(root, 'firmware/pocket_jumbotron/wx_icons.h'), h);
console.log(`wx_icons.h: ${SPR.length} sprites, ${(bytes / 1024).toFixed(1)} KB`);

// ---- the same sprites for the page preview: one PNG sheet as a data URL (web/wxicons.js, inlined by build-web) ----
{
  const sharp = (await import('sharp')).default;
  const SW = 256, spr = {};
  let x = 0, y = 0, rowH = 0;
  for (const [name, w, hh, ox, oy] of SPR) { if (x + w > SW) { x = 0; y += rowH; rowH = 0; } spr[name] = [x, y, w, hh, ox, oy]; x += w; rowH = Math.max(rowH, hh); }
  const SH = y + rowH, raw = Buffer.alloc(SW * SH * 4);
  for (const [name, w, hh, , , px] of SPR) {
    const [x0, y0] = spr[name];
    px.forEach(([r, g, b, al], i) => { const o = ((y0 + Math.floor(i / w)) * SW + x0 + (i % w)) * 4; raw[o] = r; raw[o + 1] = g; raw[o + 2] = b; raw[o + 3] = Math.round(al * 255); });
  }
  const png = await sharp(raw, { raw: { width: SW, height: SH, channels: 4 } }).png({ compressionLevel: 9 }).toBuffer();
  fs.writeFileSync(path.join(root, 'web/wxicons.js'), `// Generated by tools/make-wxicons.mjs - do not edit. Weather icon sprites for the page preview (same as the card).
window.WXI = { spr: ${JSON.stringify(spr)}, img: new Image() };
WXI.img.src = 'data:image/png;base64,${png.toString('base64')}';
`);
  console.log(`web/wxicons.js: ${(png.length / 1024).toFixed(1)} KB PNG`);
}

// ---- preview: each sprite over a dark panel background, scaled 6x ----
const out = process.argv[2];
if (out) {
  const sharp = (await import('sharp')).default;
  const SC = 6, cols = 6, cell = 42, rows = Math.ceil(SPR.length / cols);
  const PW = cols * cell, PH = rows * cell, img = Buffer.alloc(PW * SC * PH * SC * 3);
  const bg = [8, 8, 14];
  const buf = new Array(PW * PH).fill(0).map(() => [...bg]);
  SPR.forEach(([, w, hh, , , px], k) => {
    const x0 = (k % cols) * cell + Math.floor((cell - w) / 2), y0 = Math.floor(k / cols) * cell + Math.floor((cell - hh) / 2);
    px.forEach(([r, g, b, a], i) => { const x = x0 + (i % w), y = y0 + Math.floor(i / w), o = buf[y * PW + x]; buf[y * PW + x] = [o[0] + (r - o[0]) * a, o[1] + (g - o[1]) * a, o[2] + (b - o[2]) * a]; });
  });
  for (let y = 0; y < PH; y++) for (let x = 0; x < PW; x++) for (let j = 0; j < SC - 1; j++) for (let i = 0; i < SC - 1; i++) {
    const o = ((y * SC + j) * PW * SC + x * SC + i) * 3; const c = buf[y * PW + x];
    img[o] = c[0]; img[o + 1] = c[1]; img[o + 2] = c[2];
  }
  await sharp(img, { raw: { width: PW * SC, height: PH * SC, channels: 3 } }).png().toFile(out);
  console.log('preview:', out);
}
