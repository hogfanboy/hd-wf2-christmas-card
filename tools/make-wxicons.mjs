// Draws our weather icons (shaded sun with glow, moon, layered clouds, lightning bolt, small forecast icons) and
// writes them as sprites for the card: firmware/pocket_jumbotron/wx_icons.h (RGB565 + 8-bit alpha per pixel).
// Rain, snow, twinkles, drifting and the lightning flash are animated by the firmware on top of these.
//   node tools/make-wxicons.mjs [preview.png]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const SS = 4;                                    // supersampling per pixel

// A sprite canvas: draw at SS x resolution as RGBA floats, then box-filter down.
function sprite(w, h) {
  const W = w * SS, H = h * SS, c = new Float32Array(W * H * 4);
  const S = {
    w, h, W, H, c,
    // Paint with fn(x, y) -> [r, g, b, a] | null over every subsample (x, y in sprite pixels), "over" blending.
    paint(fn) {
      for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
        const v = fn((i + 0.5) / SS, (j + 0.5) / SS); if (!v || v[3] <= 0) continue;
        const o = (j * W + i) * 4, a = Math.min(1, v[3]), ia = 1 - a;
        c[o] = v[0] * a + c[o] * ia; c[o + 1] = v[1] * a + c[o + 1] * ia; c[o + 2] = v[2] * a + c[o + 2] * ia; c[o + 3] = a + c[o + 3] * ia;
      }
    },
    out() {                                       // premultiplied average -> straight color + alpha, 0..255
      const px = [];
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        let r = 0, g = 0, b = 0, a = 0;
        for (let j = 0; j < SS; j++) for (let i = 0; i < SS; i++) { const o = ((y * SS + j) * W + x * SS + i) * 4; r += c[o]; g += c[o + 1]; b += c[o + 2]; a += c[o + 3]; }
        const n = SS * SS; px.push(a ? [r / a, g / a, b / a, a / n] : [0, 0, 0, 0]);
      }
      return px;
    },
  };
  return S;
}
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * Math.max(0, Math.min(1, t)));
const hex = (n) => [(n >> 16) & 255, (n >> 8) & 255, n & 255];
const clamp01 = (t) => Math.max(0, Math.min(1, t));

// Sun: glowing disc, gradient from a pale center to deep gold, 8 tapered rays (long/short), rotated by `rot`.
function sun(S, cx, cy, r, rot) {
  const core = hex(0xFFF6B0), edge = hex(0xFFB01E), ray = hex(0xFFC22E), glow = hex(0xFF9A20);
  S.paint((x, y) => { const d = Math.hypot(x - cx, y - cy); return d < r * 1.9 ? [...glow, 0.35 * (1 - clamp01((d - r) / (r * 0.9))) ** 2] : null; });
  S.paint((x, y) => {
    const dx = x - cx, dy = y - cy, d = Math.hypot(dx, dy);
    const k = 8, seg = (2 * Math.PI) / k;
    const a = Math.atan2(dy, dx) - rot + 20 * Math.PI;
    const idx = Math.round(a / seg), off = Math.abs(a - idx * seg) * d;   // distance from the nearest ray's center line
    const inner = r + 2.2, outer = r + (idx % 2 ? 5.5 : 8.5);
    if (d < inner || d > outer) return null;
    const halfW = 1.6 * (1 - (d - inner) / (outer - inner)) + 0.35;
    return off < halfW ? [...ray, 1] : null;
  });
  S.paint((x, y) => {
    const d = Math.hypot(x - cx, y - cy); if (d > r) return null;
    const t = Math.hypot(x - (cx - r * 0.35), y - (cy - r * 0.35)) / (r * 1.3);
    return [...mix(core, edge, t), 1];
  });
}
// Moon: a shaded crescent (cut out with alpha, so effects show through) with two craters (no halo: it read as a gray disc on the panel).
function moon(S, cx, cy, r) {
  const lit = hex(0xF4EED4), dark = hex(0xC8C0A0);
  const cut = (x, y) => Math.hypot(x - (cx + r * 0.62), y - (cy - r * 0.45)) < r * 0.9;
  S.paint((x, y) => {
    const d = Math.hypot(x - cx, y - cy); if (d > r || cut(x, y)) return null;
    let c = mix(lit, dark, clamp01((Math.hypot(x - (cx - r * 0.4), y - (cy - r * 0.3)) / r) - 0.2));
    if (Math.hypot(x - (cx - r * 0.45), y - (cy + r * 0.25)) < r * 0.2) c = mix(c, dark, 0.8);
    if (Math.hypot(x - (cx - r * 0.1), y - (cy + r * 0.62)) < r * 0.14) c = mix(c, dark, 0.8);
    return [...c, 1];
  });
}
// Cloud: union of round puffs on a flat base, lit from the top left, darker underneath, with a bright rim.
function cloud(S, cx, cy, sc, top, bottom) {
  const puffs = [[-8, 3, 6.5], [0, -3, 9.5], [10, 0, 8], [16, 5, 4.5]].map(([x, y, r]) => [cx + x * sc, cy + y * sc, r * sc]);
  const bx0 = cx - 9 * sc, bx1 = cx + 16 * sc, by = cy + 5 * sc, br = 4.5 * sc;   // flat bottom: a capsule
  const inside = (x, y) => {
    let m = -1e9;
    for (const [px, py, r] of puffs) m = Math.max(m, r - Math.hypot(x - px, y - py));
    const qx = Math.max(bx0, Math.min(bx1, x));
    m = Math.max(m, br - Math.hypot(x - qx, y - by));
    return m;                                     // >0 inside, roughly the distance to the edge
  };
  const T = hex(top), Bt = hex(bottom), rim = mix(hex(top), [255, 255, 255], 0.6);
  const y0 = cy - 12.5 * sc, y1 = cy + 9.5 * sc, under = mix(hex(bottom), [0, 0, 0], 0.25);
  S.paint((x, y) => {
    const m = inside(x, y); if (m <= 0) return null;
    let c = mix(T, Bt, (y - y0) / (y1 - y0));
    const lx = inside(x + 0.9, y + 0.9);          // lit from the top left: edges facing it get the rim color
    if (lx < m && m < 1.3) c = mix(c, rim, 0.55 * (1.3 - m));
    else if (lx > m && m < 1.2 && y > cy) c = mix(c, under, 0.8);   // shadowed lower edge
    return [...c, 1];
  });
}
function bolt(S, ox, oy) {
  const P = [[7, 0], [2, 9], [6, 9], [1, 18], [11, 6], [7, 6], [11, 0]].map(([x, y]) => [ox + x, oy + y]);
  const inPoly = (x, y) => { let c = false; for (let i = 0, j = P.length - 1; i < P.length; j = i++) { const [xi, yi] = P[i], [xj, yj] = P[j]; if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) c = !c; } return c; };
  S.paint((x, y) => { let d = 9; for (let j = -2; j <= 2; j++) for (let i = -2; i <= 2; i++) if (inPoly(x + i, y + j)) d = Math.min(d, Math.hypot(i, j)); return d < 9 ? [255, 230, 120, 0.4 * (1 - d / 3)] : null; });
  S.paint((x, y) => (inPoly(x, y) ? [255, 248, 170, 1] : null));
}

const SPR = [];                                    // [name, w, h, ox, oy, px] (ox/oy: where the sprite's 0,0 sits from the anchor)
function add(name, w, h, draw) { const S = sprite(w, h); draw(S); SPR.push([name, w, h, -Math.floor(w / 2), -Math.floor(h / 2), S.out()]); }
const SUN_FRAMES = 6;
for (let f = 0; f < SUN_FRAMES; f++) add(`SUN${f}`, 40, 40, (S) => sun(S, 20, 20, 9, (f / SUN_FRAMES) * (Math.PI / 4)));
for (let f = 0; f < SUN_FRAMES; f++) add(`SUNS${f}`, 30, 30, (S) => sun(S, 15, 15, 6.5, (f / SUN_FRAMES) * (Math.PI / 4)));
add('MOON', 36, 36, (S) => moon(S, 18, 18, 10.5));
add('MOONS', 26, 26, (S) => moon(S, 13, 13, 7.5));
add('CLOUD_L', 38, 26, (S) => cloud(S, 16, 14, 1, 0xF8FAFF, 0xA8B2C6));
add('CLOUD_M', 38, 26, (S) => cloud(S, 16, 14, 1, 0xC6CCD8, 0x7C8498));
add('CLOUD_D', 38, 26, (S) => cloud(S, 16, 14, 1, 0x8C92A4, 0x4A5064));
add('CLOUD_B', 30, 21, (S) => cloud(S, 13, 11, 0.78, 0xA4AAB8, 0x6E7486));
add('BOLT', 16, 22, (S) => bolt(S, 2, 2));
// Forecast icons (column 25 px wide): sun, partly, cloudy, fog, rain, snow, storm (particles added on the card).
const MINI = [
  (S) => sun(S, 12, 9, 4.2, 0.2),
  (S) => { sun(S, 9, 6, 3.4, 0.2); cloud(S, 11, 10, 0.5, 0xF8FAFF, 0xA8B2C6); },
  (S) => { cloud(S, 8, 6, 0.42, 0xA4AAB8, 0x6E7486); cloud(S, 11, 10, 0.5, 0xF8FAFF, 0xA8B2C6); },
  (S) => { cloud(S, 10, 6, 0.5, 0xC6CCD8, 0x7C8498); },
  (S) => { cloud(S, 10, 6, 0.5, 0xC6CCD8, 0x7C8498); },
  (S) => { cloud(S, 10, 6, 0.5, 0xF8FAFF, 0xA8B2C6); },
  (S) => { cloud(S, 10, 6, 0.5, 0x8C92A4, 0x4A5064); },
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
