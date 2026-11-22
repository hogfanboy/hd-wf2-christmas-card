// Draws the effect sprites (Santa's sleigh and reindeer, the snowy town, moons, turkey, bunny, eggs, bats, pumpkin,
// shamrocks, balloons, fish, hearts) and writes them for the card: firmware/pocket_jumbotron/fx_sprites.h, and for
// the page preview: web/fxsprites.js (the same pixels). Shapes are drawn at 4x4 subsamples with light from the top
// left, so the inside is shaded in full 24-bit color, but the outer edge is hard: a pixel is fully on or off (LEDs
// look best sharp with black around everything, see PROJECT-NOTES).
// Two kinds of sprite:
//   color  - fixed colors. Pure magenta (FF00FF) = "glow" pixel: the card paints it in the color it is given
//            (the pumpkin's face flickers that way).
//   tint   - drawn in any color the effect picks (balloons, fish, eggs, hearts, shamrocks): R = shade 0..255 of the
//            color, G = how far toward white (highlights, stripes).
//   node tools/make-fxsprites.mjs [preview.png]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const SS = 4, ON = 0.5;
const clamp01 = (t) => Math.max(0, Math.min(1, t));
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * clamp01(t));
const hex = (n) => [(n >> 16) & 255, (n >> 8) & 255, n & 255];
const WHITE = [255, 255, 255], BLACK = [0, 0, 0], GLOW = [255, 0, 255];

// A sprite drawn in design units (dw x dh) at k sprite pixels per unit.
function sprite(dw, dh, k = 1) {
  const w = Math.ceil(dw * k - 0.01), h = Math.ceil(dh * k - 0.01), W = w * SS, H = h * SS, c = new Float32Array(W * H * 4);
  const keys = [];
  return {
    w, h,
    // fn(x, y) in design units -> [r, g, b] or null; layers paint over each other.
    paint(fn) {
      for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
        const v = fn((i + 0.5) / SS / k, (j + 0.5) / SS / k); if (!v) continue;
        const o = (j * W + i) * 4; c[o] = v[0]; c[o + 1] = v[1]; c[o + 2] = v[2]; c[o + 3] = 1;
      }
    },
    erase(fn) { for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) if (fn((i + 0.5) / SS / k, (j + 0.5) / SS / k)) c[(j * W + i) * 4 + 3] = 0; },
    key(fn) { keys.push(fn); },                   // pixels whose center is inside fn become glow pixels
    out() {
      const px = [];
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        let r = 0, g = 0, b = 0, a = 0;
        for (let j = 0; j < SS; j++) for (let i = 0; i < SS; i++) {
          const o = ((y * SS + j) * W + x * SS + i) * 4;
          if (c[o + 3]) { r += c[o]; g += c[o + 1]; b += c[o + 2]; a++; }
        }
        const cx = (x + 0.5) / k, cy = (y + 0.5) / k;
        if (keys.some((f) => f(cx, cy))) { px.push([...GLOW, 1]); continue; }
        px.push(a / (SS * SS) >= ON ? [r / a, g / a, b / a, 1] : [0, 0, 0, 0]);
      }
      return px;
    },
  };
}

// ---- shape helpers (design units) ----
function ellD(x, y, cx, cy, rx, ry, rot = 0) {   // < 1 inside; also the local u, v
  const c = Math.cos(rot), s = Math.sin(rot), dx = x - cx, dy = y - cy;
  const u = (dx * c + dy * s) / rx, v = (-dx * s + dy * c) / ry;
  return Math.hypot(u, v);
}
function segD(x, y, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay, t = clamp01(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy || 1));
  return Math.hypot(x - ax - t * dx, y - ay - t * dy);
}
const cap = (x, y, a, b, r) => segD(x, y, a[0], a[1], b[0], b[1]) <= r;
function inPoly(x, y, p) {
  let ins = false;
  for (let i = 0, j = p.length - 1; i < p.length; j = i++) {
    const [xi, yi] = p[i], [xj, yj] = p[j];
    if ((yi > y) !== (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) ins = !ins;
  }
  return ins;
}
const L = (() => { const v = [-0.45, -0.62, 0.65], n = Math.hypot(...v); return v.map((a) => a / n); })();
function lit(x, y, cx, cy, rx, ry, rot = 0) {    // light on an ellipsoid at (x, y): 0..1, null outside
  const c = Math.cos(rot), s = Math.sin(rot), dx = x - cx, dy = y - cy;
  const u = (dx * c + dy * s) / rx, v = (-dx * s + dy * c) / ry, d2 = u * u + v * v;
  if (d2 > 1) return null;
  const z = Math.sqrt(1 - d2), nx = u * c - v * s, ny = u * s + v * c;   // normal back in screen axes
  return clamp01(nx * L[0] + ny * L[1] + z * L[2]);
}
// Base color lit by l: darkest side keeps 45% (so shapes never vanish on LEDs), highlights go toward white.
function shade(base, l, hi = 0.35) {
  const dark = base.map((v) => v * 0.45);
  return l < 0.7 ? mix(dark, base, l / 0.7) : mix(base, WHITE, ((l - 0.7) / 0.3) * hi);
}
const T = (sh, w = 0) => [255 * clamp01(sh), 255 * clamp01(w), 0];   // tint pixel

const SPR = [];                                    // [name, w, h, tint, px]
function add(name, dw, dh, k, tint, draw) {
  const S = sprite(dw, dh, k);
  draw(S);
  SPR.push([name, S.w, S.h, tint ? 1 : 0, S.out()]);
}

// ---- Santa's sleigh (faces right) ----
function sleigh(S) {
  const sack = hex(0x9A6A3A), red = hex(0xD8202A), skin = hex(0xFFC4A0), gold = hex(0xFFCC4A), fur = hex(0xF4F4F4);
  S.paint((x, y) => { const l = lit(x, y, 9, 11, 6, 6.5); return l == null ? null : shade(sack, l); });
  S.paint((x, y) => cap(x, y, [8.5, 4.6], [10, 5.4], 1) ? gold : null);
  S.paint((x, y) => { const l = lit(x, y, 21, 13, 6.5, 6); return l == null ? null : shade(red, l); });      // coat
  S.paint((x, y) => cap(x, y, [23, 12], [30, 15], 1.8) ? shade(red, 0.45 + 0.4 * clamp01((15.5 - y) / 4)) : null);   // arm
  S.paint((x, y) => ellD(x, y, 30.5, 15, 1.5, 1.4) < 1 ? hex(0x208838) : null);                               // mitten
  S.paint((x, y) => { const l = lit(x, y, 23.5, 8.4, 3.3, 2.9); return l == null ? null : shade(fur, 0.5 + l / 2, 0); });   // beard
  S.paint((x, y) => { const l = lit(x, y, 23.2, 5.6, 2.5, 2.4); return l == null || y > 6.9 ? null : shade(skin, 0.6 + l / 2.5, 0); });
  S.paint((x, y) => ellD(x, y, 24.4, 6.9, 1.6, 0.7) < 1 ? fur : null);                                       // moustache
  S.paint((x, y) => ellD(x, y, 25.6, 5.8, 0.65, 0.6) < 1 ? hex(0xFF8A80) : null);                            // nose
  S.paint((x, y) => ellD(x, y, 24.4, 5.0, 0.45, 0.55) < 1 ? hex(0x201010) : null);                           // eye
  S.paint((x, y) => inPoly(x, y, [[19.6, 4.3], [26.6, 4.3], [24, 1.2], [20.5, 0.4], [17, 2.4]]) ? shade(red, clamp01(1 - y / 5)) : null);   // hat
  S.paint((x, y) => cap(x, y, [19.9, 4.5], [26.4, 4.5], 1.05) ? shade(fur, 0.8, 0) : null);                  // brim
  S.paint((x, y) => ellD(x, y, 16.8, 2.9, 1.35, 1.35) < 1 ? fur : null);                                     // pompom
  const body = [[1.5, 10], [7, 9.4], [10, 15], [30, 15], [36, 12], [41, 9.6], [44.5, 11], [42.5, 15.5], [38, 21], [8, 22], [3.5, 19.5], [1.5, 14]];
  S.paint((x, y) => {
    if (!inPoly(x, y, body)) return null;
    const t = clamp01((y - 9.5) / 12.5);
    let c = mix(hex(0xF8505A), hex(0x8A0E1C), t);
    if (Math.abs(y - 18.4) < 0.75 && x > 5 && x < 39.5) c = gold;                                            // trim
    const sw = Math.hypot(x - 11.5, y - 15.6);
    if (sw > 1.6 && sw < 2.5 && y < 17.5) c = gold;                                                          // scroll
    return c;
  });
  for (const [a, b] of [[[1.5, 10], [7, 9.4]], [[10, 15], [30, 15]], [[30, 15], [36, 12]], [[36, 12], [41, 9.6]], [[41, 9.6], [44.5, 11]]])
    S.paint((x, y) => cap(x, y, a, b, 0.8) ? mix(gold, WHITE, 0.25) : null);                                   // gold rim
  const runner = [[[3, 26.4], [40, 26.4]], [[40, 26.4], [44, 25]], [[44, 25], [46.2, 22.2]], [[46.2, 22.2], [45, 20.4]], [[3, 26.4], [1.4, 25]],
    [[13, 22], [13, 26.4]], [[33, 21], [33, 26.4]]];
  for (const [a, b] of runner) S.paint((x, y) => cap(x, y, a, b, 0.85) ? mix(gold, hex(0xA87818), clamp01((y - 20) / 7)) : null);
}
// ---- Reindeer (faces right): frame 0 stretched, 1 gathered; red = lead deer's nose ----
function deer(S, frame, red) {
  const fur = hex(0xA8682E), leg = hex(0x8A5024), hoof = hex(0x3A2010), antler = hex(0x7A5028);
  const legs = frame ? [[[20, 16.5], [19, 23]], [[18, 17], [15.5, 22.5]], [[7, 17], [10, 23]], [[5, 16.5], [7.5, 22]]]
                     : [[[21, 16], [26, 21.5]], [[19, 17], [22.5, 23]], [[6, 16], [1.2, 20.5]], [[8, 17], [4, 23]]];
  legs.forEach(([a, b], i) => S.paint((x, y) => {
    if (segD(x, y, a[0], a[1], b[0], b[1]) > 1.1) return null;
    const t = Math.hypot(x - a[0], y - a[1]) / Math.hypot(b[0] - a[0], b[1] - a[1]);
    return t > 0.82 ? hoof : shade(leg, i & 1 ? 0.45 : 0.7);
  }));
  S.paint((x, y) => ellD(x, y, 3, 11.5, 2, 1.3) < 1 ? hex(0xF0E6D0) : null);                                 // tail
  S.paint((x, y) => { const l = lit(x, y, 13, 14, 10, 5); if (l == null) return null; const c = shade(fur, l); return y > 16.5 ? mix(c, hex(0xE0C090), 0.35) : c; });
  for (const [a, b] of [[[25, 3.6], [23.2, 0.6]], [[24.2, 1.9], [22, 1.6]], [[26, 3.6], [27.4, 0.5]], [[26.7, 1.8], [28.8, 1.4]]])
    S.paint((x, y) => cap(x, y, a, b, 0.6) ? antler : null);
  S.paint((x, y) => cap(x, y, [20, 12], [24, 7], 2.6) ? shade(fur, 0.55 + 0.35 * clamp01((x - 19) / 6)) : null);   // neck
  S.paint((x, y) => ellD(x, y, 23.6, 4.6, 1.7, 0.8, -0.5) < 1 ? shade(fur, 0.5) : null);                       // ear
  S.paint((x, y) => { const l = lit(x, y, 26, 6, 3.4, 2.4, 0.25); return l == null ? null : shade(fur, l); });
  S.paint((x, y) => cap(x, y, [27, 6.8], [30, 7.6], 1.45) ? shade(hex(0xC89060), 0.75) : null);                // snout
  S.paint((x, y) => ellD(x, y, 30.6, 7.3, 1.25, 1.2) < 1 ? (red ? mix(hex(0xFF2018), WHITE, x < 30.4 && y < 7 ? 0.4 : 0) : hex(0x3A2010)) : null);
  S.paint((x, y) => ellD(x, y, 26.6, 5.2, 0.55, 0.6) < 1 ? hex(0x100808) : null);                             // eye
  S.paint((x, y) => cap(x, y, [19.6, 9.6], [20.8, 18], 0.95) ? hex(0xD02030) : null);                          // harness
  S.paint((x, y) => ellD(x, y, 21.2, 17.6, 1.05, 1.05) < 1 ? mix(hex(0xFFCC4A), WHITE, x < 21 && y < 17.4 ? 0.5 : 0) : null);   // bell
}
add('SLEIGH', 47, 27.5, 0.8, false, sleigh);
add('DEER0', 32, 24.2, 0.8, false, (S) => deer(S, 0, false));
add('DEER1', 32, 24.2, 0.8, false, (S) => deer(S, 1, false));
add('DEER0R', 32, 24.2, 0.8, false, (S) => deer(S, 0, true));
add('DEER1R', 32, 24.2, 0.8, false, (S) => deer(S, 1, true));
add('SLEIGH_S', 47, 27.5, 0.42, false, sleigh);
add('DEER0_S', 32, 24.2, 0.42, false, (S) => deer(S, 0, false));
add('DEER1_S', 32, 24.2, 0.42, false, (S) => deer(S, 1, false));
add('DEER0R_S', 32, 24.2, 0.42, false, (S) => deer(S, 0, true));
add('DEER1R_S', 32, 24.2, 0.42, false, (S) => deer(S, 1, true));

// ---- Moons: pale winter moon and orange Halloween moon, shaded with craters ----
function moon(S, base, crater) {
  const CR = [[6.8, 7.6, 2.2, 1.8], [12.6, 12.8, 2.6, 2.1], [12.8, 5.8, 1.4, 1.2], [6.2, 13.4, 1.3, 1.1], [9.6, 10, 0.9, 0.8]];
  S.paint((x, y) => {
    const l = lit(x, y, 10, 10, 9.3, 9.3); if (l == null) return null;
    let c = mix(base.map((v) => v * 0.72), base, 0.35 + l * 0.65);
    for (const [cx, cy, rx, ry] of CR) { const d = ellD(x, y, cx, cy, rx, ry); if (d < 1) c = mix(crater, c, d * d * 0.6); }
    return c;
  });
}
add('MOON', 20, 20, 1, false, (S) => moon(S, hex(0xFFF4D0), hex(0xC8B890)));
add('MOON_O', 20, 20, 1, false, (S) => moon(S, hex(0xFFA238), hex(0xC86418)));

// ---- The snowy town for the full-screen sleigh (128 x 30, bottom of the screen) ----
const WINDOWS = [];                                // [x, y, w, h] in town pixels: the card lights them
function town(S) {
  const houses = [   // x, width, wall top, roof rise, wall color, roof color, chimney x or -1
    [-2, 27, 16, 9, 0x6A3048, 0x3A3A58, 17], [24, 21, 12, 8, 0x3E5A86, 0x5A2A30, -1], [44, 28, 18, 10, 0x7A4A2A, 0x34405A, 4],
    [70, 22, 13, 9, 0x4E6A3A, 0x5A2A30, 14], [91, 16, 19, 6, 0x6A3048, 0x3A3A58, -1], [106, 24, 14, 9, 0x3E5A86, 0x4A2A2A, 3]];
  const groundY = 26;
  // pine trees behind the houses
  for (const [tx, th] of [[22, 15], [67, 13], [104, 12]]) S.paint((x, y) => {
    const top = groundY - th, dy = y - top; if (dy < 0 || y > groundY) return null;
    const half = 0.5 + dy * 0.36 - ((dy % 4) < 1.2 ? 0.8 : 0);
    if (Math.abs(x - tx) > half) return null;
    const c = shade(hex(0x1E6A34), 0.45 + 0.4 * clamp01((tx - x + half) / (2 * half)));
    return (dy % 4) < 1.4 && Math.abs(x - tx) > half - 1.4 ? mix(c, hex(0xE8F0FF), 0.8) : c;
  });
  for (const [x0, w, wt, rise, wc, rc, ch] of houses) {
    const cx = x0 + w / 2, peak = wt - rise;
    if (ch >= 0) S.paint((x, y) => x >= x0 + ch && x < x0 + ch + 3 && y >= peak + 1 && y < wt ? (y < peak + 2.2 ? hex(0xEEF4FF) : shade(hex(0x7A3A30), 0.4 + (x - x0 - ch) / 4)) : null);
    S.paint((x, y) => {                                                    // walls with siding lines
      if (x < x0 || x >= x0 + w || y < wt || y > groundY) return null;
      let c = mix(hex(wc), hex(wc).map((v) => v * 0.6), (y - wt) / (groundY - wt));
      if (x > cx) c = c.map((v) => v * 0.82);                              // the side away from the moon
      if ((Math.floor(y) - wt) % 3 === 2) c = c.map((v) => v * 0.85);
      return c;
    });
    S.paint((x, y) => {                                                    // roof with a thick snow cap
      const top = peak + Math.abs(x - cx) * (rise / (w / 2 + 1));
      if (x < x0 - 1 || x >= x0 + w + 1 || y < top || y > wt + 0.5) return null;
      if (y < top + 1.6) return mix(hex(0xF4F8FF), hex(0xB8C8F0), x > cx ? 0.5 : 0);
      if (y < top + 2.4) return hex(0x9AAAD8);
      return shade(hex(rc), x > cx ? 0.45 : 0.75);
    });
    const door = x0 + Math.round(w * 0.6);
    if (x0 + w > 0 && x0 < 128) S.paint((x, y) => x >= door && x < door + 3 && y >= groundY - 5 && y <= groundY ? (x < door + 1 ? hex(0x4A2814) : hex(0x6A3A1C)) : null);
    for (const wx of [x0 + 3, x0 + w - 7]) if (wx >= 0 && wx + 4 <= 128 && (wx + 4 <= door || wx >= door + 3)) WINDOWS.push([wx, wt + 3, 4, 4]);
  }
  for (const [wx, wy, ww, wh] of WINDOWS) S.paint((x, y) => x >= wx - 0.5 && x < wx + ww + 0.5 && y >= wy - 0.5 && y < wy + wh + 0.5 ? hex(0x2A1A12) : null);   // frames
  S.paint((x, y) => {                                                      // snowy ground with drifts
    const top = groundY - 0.8 + Math.sin(x * 0.21) * 0.9 + Math.sin(x * 0.07 + 1) * 0.7;
    if (y < top) return null;
    return mix(hex(0xF0F6FF), hex(0x8898C8), (y - top) / 5 + (Math.sin(x * 0.9) > 0.85 ? 0.25 : 0));
  });
}
add('TOWN', 128, 30, 1, false, town);

// ---- Turkey strutting (faces right), legs frame 0 / 1 ----
function turkey(S, frame) {
  const brown = hex(0x965A2E), leg = hex(0xE89030);
  const legs = frame ? [[[16, 27], [18, 32.2]], [[21, 27], [20.5, 32.2]]] : [[[16, 27], [14, 32.2]], [[21, 27], [23, 32.2]]];
  legs.forEach(([a, b], i) => {
    S.paint((x, y) => cap(x, y, a, b, 0.9) ? shade(leg, i ? 0.75 : 0.55) : null);
    S.paint((x, y) => cap(x, y, b, [b[0] + 2, b[1] + 0.4], 0.65) || cap(x, y, b, [b[0] - 1.3, b[1] + 0.6], 0.6) ? shade(leg, 0.6) : null);
  });
  S.paint((x, y) => {                                                      // tail fan: bands and feather splits
    const dx = x - 12, dy = y - 17, r = Math.hypot(dx, dy); if (y > 23 || r > 15) return null;
    const a = Math.atan2(dy, dx), f = ((a / 0.27) % 1 + 1) % 1, R = 14.6 - 1.3 * (1 - Math.sin(Math.PI * f));
    if (r > R) return null;
    const c = r > R - 1.2 ? hex(0xF6E4B4) : r > R - 3 ? hex(0x5A3018) : r > R - 5.6 ? hex(0xC85A1E) : r > R - 7.6 ? hex(0xF0A834) : hex(0x7A4420);
    return f < 0.13 ? c.map((v) => v * 0.65) : c;
  });
  S.paint((x, y) => { const l = lit(x, y, 18, 21, 9, 7.5); if (l == null) return null; let c = shade(brown, l, 0.5); if (((x * 0.9 + y * 1.3) % 2.3 + 2.3) % 2.3 < 0.5) c = c.map((v) => v * 0.8); return c; });
  S.paint((x, y) => { const l = lit(x, y, 15.5, 21, 6, 4, 0.2); if (l == null) return null; const c = shade(hex(0x744420), l, 0.3); return ((y - 17) % 2.6 + 2.6) % 2.6 < 0.7 ? mix(c, hex(0xE0C090), 0.55) : c; });   // wing
  S.paint((x, y) => cap(x, y, [24, 17], [27.4, 8], 2.3) ? (y < 12.5 ? shade(hex(0xC0C8E4), 0.7) : shade(brown, 0.75)) : null);   // neck
  S.paint((x, y) => { const l = lit(x, y, 28, 7, 2.6, 2.6); return l == null ? null : shade(hex(0xB8C4E4), l); });
  S.paint((x, y) => inPoly(x, y, [[29.8, 6], [33.4, 7.2], [29.8, 8.2]]) ? hex(0xFFB028) : null);              // beak
  S.paint((x, y) => cap(x, y, [28.6, 9], [28.8, 13], 1.2) || cap(x, y, [29.8, 5.4], [31.2, 8.6], 0.65) ? shade(hex(0xE81E28), y < 9 ? 0.9 : 0.7) : null);   // wattle, snood
  S.paint((x, y) => ellD(x, y, 28.9, 6.2, 0.6, 0.65) < 1 ? hex(0x100808) : null);
}
add('TURKEY0', 34, 34, 1, false, (S) => turkey(S, 0));
add('TURKEY1', 34, 34, 1, false, (S) => turkey(S, 1));

// ---- Easter bunny (faces right): 0 sitting, 1 leaping ----
function bunny(S, leap) {
  const fur = hex(0xF4F4FA), shadow = hex(0x9AA4C4), pink = hex(0xFFA0BC);
  const furC = (l) => l < 0.6 ? mix(shadow, fur, l / 0.6) : fur;
  const blob = (cx, cy, rx, ry, rot = 0, k = 1) => S.paint((x, y) => { const l = lit(x, y, cx, cy, rx, ry, rot); return l == null ? null : furC(l * k); });
  const ear = (cx, cy, rot, k) => {
    blob(cx, cy, 1.8, 5.2, rot, k);
    S.paint((x, y) => ellD(x, y, cx + 0.15, cy + 0.3, 0.8, 4, rot) < 1 ? shade(pink, 0.6 + k * 0.3) : null);
  };
  if (!leap) {
    ear(19.2, 4.8, -0.35, 0.7);
    S.paint((x, y) => ellD(x, y, 9, 24.3, 4.6, 1.4) < 1 ? furC(0.55) : null);                                 // hind foot
    blob(12, 18, 9.5, 6.5); blob(9, 19.5, 6, 5.4, 0, 1.1); blob(19, 17, 4.5, 5);
    S.paint((x, y) => ellD(x, y, 21, 23.3, 2.3, 1.2) < 1 ? furC(0.75) : null);                                 // paw
    blob(22, 11, 4.6, 4);
    ear(21.8, 4.2, -0.12, 1);
    S.paint((x, y) => ellD(x, y, 2.8, 15.5, 2.4, 2.4) < 1 ? WHITE : null);                                     // tail
    S.paint((x, y) => ellD(x, y, 23.9, 10, 0.9, 1) < 1 ? (Math.hypot(x - 24.2, y - 9.6) < 0.35 ? WHITE : hex(0x181018)) : null);
    S.paint((x, y) => ellD(x, y, 26.4, 11.8, 0.6, 0.55) < 1 ? hex(0xFF6A90) : null);
  } else {
    ear(20.3, 4.6, -1.0, 0.7);
    S.paint((x, y) => cap(x, y, [7, 16.5], [1.2, 20.6], 1.7) ? furC(0.5) : null);                              // hind legs
    S.paint((x, y) => cap(x, y, [21, 15.5], [28, 19], 1.4) ? furC(0.7) : null);                                // front legs
    blob(14, 14, 11, 5, -0.12); blob(8.5, 14, 5.4, 4.6, 0, 1.1);
    blob(25, 9, 4.4, 3.8);
    ear(22.4, 3.8, -0.75, 1);
    S.paint((x, y) => ellD(x, y, 2.8, 11.2, 2.3, 2.3) < 1 ? WHITE : null);
    S.paint((x, y) => ellD(x, y, 26.8, 8.2, 0.9, 1) < 1 ? (Math.hypot(x - 27.1, y - 7.8) < 0.35 ? WHITE : hex(0x181018)) : null);
    S.paint((x, y) => ellD(x, y, 29.2, 9.6, 0.6, 0.55) < 1 ? hex(0xFF6A90) : null);
  }
}
add('BUNNY0', 30, 26, 1, false, (S) => bunny(S, false));
add('BUNNY1', 30, 26, 1, false, (S) => bunny(S, true));

// ---- Painted egg (tint) ----
function egg(S) {
  S.paint((x, y) => {
    const ry = 4.6, rx = 3.6 * (y < 5 ? 0.86 + 0.14 * (y / 5) : 1), l = lit(x, y, 4, 5.2, rx, ry); if (l == null) return null;
    let w = l > 0.85 ? (l - 0.85) * 2.5 : 0;
    if (Math.abs(y - 5.4 - 0.7 * Math.sin(x * 2.2)) < 0.65) w = 0.75;                                          // zigzag band
    if (Math.abs(y - 2.8) < 0.5 && Math.floor(x) % 2 === 0) w = 0.55;                                           // dots
    return T(0.5 + l * 0.5, w);
  });
}
add('EGG', 8, 10, 1, true, egg);

// ---- Bat: 0 wings up, 1 wings down ----
function bat(S, down) {
  const wing = hex(0x9858D8), body = hex(0x6A3AA8);
  const L1 = down ? [[9.4, 5], [6, 5.6], [1, 7.4], [0.4, 9.6], [2.8, 8.6], [4.6, 9.8], [6.2, 8.2], [9.4, 7.6]]
                  : [[9.4, 4.6], [5.4, 0.4], [3, 2], [0.4, 2.4], [2.4, 4.6], [1.4, 6.4], [4.6, 5.6], [6.6, 6.8], [9.4, 6.2]];
  const R1 = L1.map(([x, y]) => [20 - x, y]);
  S.paint((x, y) => inPoly(x, y, L1) || inPoly(x, y, R1) ? shade(wing, 0.5 + 0.5 * clamp01(1 - (y - 1) / 9)) : null);
  S.paint((x, y) => { const l = lit(x, y, 10, 6.2, 2.2, 2.9); return l == null ? null : shade(body, l); });
  S.paint((x, y) => Math.hypot(x - 10, y - 3.6) < 1.7 || inPoly(x, y, [[8.6, 3], [8.6, 0.9], [9.9, 2.4]]) || inPoly(x, y, [[11.4, 3], [11.4, 0.9], [10.1, 2.4]]) ? shade(body, 0.8) : null);
  S.paint((x, y) => Math.hypot(x - 9.3, y - 3.7) < 0.5 || Math.hypot(x - 10.7, y - 3.7) < 0.5 ? hex(0xFFD830) : null);
}
add('BAT0', 20, 10, 1, false, (S) => bat(S, false));
add('BAT1', 20, 10, 1, false, (S) => bat(S, true));

// ---- Jack-o-lantern: the face is glow pixels (the card flickers them) ----
function pumpkin(S) {
  const org = hex(0xE86010);
  S.paint((x, y) => cap(x, y, [8, 4], [9.2, 0.9], 1.0) ? shade(hex(0x6A8A28), x < 8.6 ? 0.85 : 0.5) : null);
  for (const [cx, rx, k] of [[4.8, 4, 0.8], [11.2, 4, 0.8], [8, 4.4, 1]]) S.paint((x, y) => { const l = lit(x, y, cx, 8.8, rx, 4.8); return l == null ? null : shade(org, l * k + (1 - k) * 0.2, 0.25); });
  S.key((x, y) => inPoly(x, y, [[4, 8], [6.8, 8], [5.6, 5.6]]) || inPoly(x, y, [[9.2, 8], [12, 8], [10.4, 5.6]]) || inPoly(x, y, [[7.3, 9.9], [8.7, 9.9], [8, 8.6]])
    || inPoly(x, y, [[3.8, 10.4], [5.6, 11.2], [6.8, 10.5], [8, 11.3], [9.2, 10.5], [10.4, 11.2], [12.2, 10.4], [11, 12.7], [5, 12.7]]));
}
add('PUMPKIN', 16, 14, 1, false, pumpkin);

// ---- Shamrock (tint), big and small ----
function clover(S) {
  const c = [6, 5.6];
  for (const a of [-Math.PI / 2, Math.PI * 5 / 6, Math.PI / 6]) {
    const d = [Math.cos(a), Math.sin(a)], p = [-d[1], d[0]];
    S.paint((x, y) => {
      const lobes = [[c[0] + d[0] * 3.2 + p[0] * 1.15, c[1] + d[1] * 3.2 + p[1] * 1.15, 1.55], [c[0] + d[0] * 3.2 - p[0] * 1.15, c[1] + d[1] * 3.2 - p[1] * 1.15, 1.55], [c[0] + d[0] * 1.9, c[1] + d[1] * 1.9, 1.0]];
      if (!lobes.some(([lx, ly, r]) => Math.hypot(x - lx, y - ly) < r)) return null;
      const lx = c[0] + d[0] * 2.8, ly = c[1] + d[1] * 2.8, l = 1 - Math.hypot(x - lx - 0.6, y - ly - 0.8) / 4.2;
      const along = (x - c[0]) * d[0] + (y - c[1]) * d[1], across = Math.abs((x - c[0]) * p[0] + (y - c[1]) * p[1]);
      return T(0.55 + 0.45 * clamp01(l), across < 0.35 && along > 0.5 && along < 4 ? 0.3 : 0);
    });
  }
  S.paint((x, y) => cap(x, y, [6.2, 7], [7.8, 12.2], 0.7) ? T(0.6) : null);
}
add('CLOVER', 12, 13, 1, true, clover);
add('CLOVER_S', 12, 13, 0.5, true, clover);

// ---- Party balloon (tint) ----
add('BALLOON', 11, 13.5, 1, true, (S) => {
  S.paint((x, y) => { const l = lit(x, y, 5.5, 6, 5, 6); if (l == null) return null; return T(0.5 + l * 0.5, ellD(x, y, 3.5, 3.4, 1.1, 1.8, 0.4) < 1 ? 0.85 : 0); });
  S.paint((x, y) => inPoly(x, y, [[4.5, 12.9], [6.5, 12.9], [5.5, 11.5]]) ? T(0.7) : null);
});

// ---- Fish (tint, faces right) ----
add('FISH', 15, 9, 1, true, (S) => {
  S.paint((x, y) => inPoly(x, y, [[4, 4.5], [0.3, 0.8], [1.6, 4.5], [0.3, 8.2]]) ? T(0.7, ((y * 1.6) % 1.6) < 0.5 ? 0.15 : 0) : null);   // tail
  S.paint((x, y) => inPoly(x, y, [[6, 1.6], [8.6, 0.2], [10.6, 1.6]]) ? T(0.75) : null);                       // fin
  S.paint((x, y) => { const l = lit(x, y, 8.5, 4.6, 5.6, 3.6); if (l == null) return null; return T(0.5 + l * 0.5, (y > 5.8 ? 0.35 : 0) + (Math.abs(x - 7.4) < 0.5 ? 0.45 : 0)); });
  S.paint((x, y) => Math.hypot(x - 11.6, y - 3.7) < 1.15 ? (Math.hypot(x - 11.9, y - 3.7) < 0.55 ? T(0, 0) : T(1, 1)) : null);
});

// ---- Heart (tint), big and small ----
function heart(S) {
  S.paint((x, y) => {
    if (Math.hypot(x - 2.5, y - 2.5) > 2.35 && Math.hypot(x - 6.5, y - 2.5) > 2.35 && !inPoly(x, y, [[0.3, 3.2], [8.7, 3.2], [4.5, 7.9]])) return null;
    const l = clamp01(1 - Math.hypot(x - 3.6, y - 2.8) / 5.2);
    return T(0.55 + 0.45 * l, Math.hypot(x - 2.6, y - 2.2) < 0.9 ? 0.75 : 0);
  });
}
add('HEART', 9, 8, 1, true, heart);
add('HEART_S', 9, 8, 0.6, true, heart);

// ---- palettes: each sprite keeps at most 255 colors (index 0 = clear). A sprite with more (town, turkey) is
// rounded to a coarser color grid until it fits; the glow color FF00FF stays exact. The page uses the same pixels.
for (const s of SPR) {
  const px = s[4], key = (c) => c[0] + ',' + c[1] + ',' + c[2];
  for (const p of px) if (p[3]) for (let k = 0; k < 3; k++) p[k] = Math.round(p[k]);
  const isGlow = (p) => !s[3] && p[0] === 255 && p[1] === 0 && p[2] === 255;
  for (let step = 1; ; step++) {
    const q = (v) => Math.min(255, Math.round(v / step) * step);
    const set = new Set(px.filter((p) => p[3]).map((p) => isGlow(p) ? '255,0,255' : key([q(p[0]), q(p[1]), q(p[2])])));
    if (set.size <= 255) { if (step > 1) for (const p of px) if (p[3] && !isGlow(p)) for (let k = 0; k < 3; k++) p[k] = q(p[k]); break; }
  }
}

// ---- card header: 1 byte per pixel (0 = clear, n = palette entry n-1); palette = R G B (tint: shade, white, 0) ----
let h = `// Generated by tools/make-fxsprites.mjs - do not edit. Effect sprites: 1 byte per pixel, 0 = clear, n = color n-1
// of the sprite's palette (R, G, B; tint sprites: shade, toward-white, 0). A color sprite's FF00FF entries are
// "glow": painted in the color the effect passes.
#pragma once
#include <stdint.h>
struct FxSprite { uint8_t w, h, tint; const uint8_t *px; const uint8_t *pal; };
`;
let bytes = 0;
for (const [name, w, hh, tint, px] of SPR) {
  const pal = [], idx = new Map();
  const ix = px.map(([r, g, b, a]) => {
    if (!a) return 0;
    const k = r + ',' + g + ',' + b;
    if (!idx.has(k)) { idx.set(k, pal.length + 1); pal.push([r, g, b]); }
    return idx.get(k);
  });
  let s = '';
  ix.forEach((v, i) => { s += (i % 32 === 0 ? '\n  ' : '') + v + ','; });
  bytes += ix.length + pal.length * 3;
  h += `static const uint8_t FX_${name}_PX[] = {${s}\n};\n`;
  h += `static const uint8_t FX_${name}_PAL[] = {${pal.map((c) => c.join(',')).join(', ')}};\n`;
}
h += `static const FxSprite FX_SPR[] = {\n${SPR.map(([n, w, hh, t]) => `  {${w}, ${hh}, ${t}, FX_${n}_PX, FX_${n}_PAL},   // ${n}`).join('\n')}\n};\n`;
h += `enum { ${SPR.map(([n], i) => `FXS_${n} = ${i}`).join(', ')} };\n`;
h += `static const uint8_t FX_WINDOWS[][4] = {${WINDOWS.map((w) => `{${w.join(', ')}}`).join(', ')}};   // town windows: x, y, w, h\n`;
fs.writeFileSync(path.join(root, 'firmware/pocket_jumbotron/fx_sprites.h'), h);
console.log(`fx_sprites.h: ${SPR.length} sprites, ${(bytes / 1024).toFixed(1)} KB`);

// ---- page preview: one PNG sheet as a data URL (web/fxsprites.js, inlined by build-web) ----
const sharp = (await import('sharp')).default;
{
  const SW = 256, spr = {};
  let x = 0, y = 0, rowH = 0;
  for (const [name, w, hh, tint] of SPR) { if (x + w > SW) { x = 0; y += rowH; rowH = 0; } spr[name] = [x, y, w, hh, tint]; x += w; rowH = Math.max(rowH, hh); }
  const SH = y + rowH, raw = Buffer.alloc(SW * SH * 4);
  for (const [name, w, , , px] of SPR) {
    const [x0, y0] = spr[name];
    px.forEach(([r, g, b, a], i) => { if (!a) return; const o = ((y0 + Math.floor(i / w)) * SW + x0 + (i % w)) * 4; raw[o] = r; raw[o + 1] = g; raw[o + 2] = b; raw[o + 3] = 255; });
  }
  const png = await sharp(raw, { raw: { width: SW, height: SH, channels: 4 } }).png({ compressionLevel: 9 }).toBuffer();
  fs.writeFileSync(path.join(root, 'web/fxsprites.js'), `// Generated by tools/make-fxsprites.mjs - do not edit. Effect sprites for the page preview (same as the card).
// spr: name -> [x, y, w, h, tint] in the sheet; px is filled with the sheet's RGBA once the image loads.
window.FXS = { spr: ${JSON.stringify(spr)}, win: ${JSON.stringify(WINDOWS)}, img: new Image(), px: null, sw: ${SW} };
FXS.img.onload = () => { const c = document.createElement('canvas'); c.width = FXS.img.width; c.height = FXS.img.height; const x = c.getContext('2d'); x.drawImage(FXS.img, 0, 0); FXS.px = x.getImageData(0, 0, c.width, c.height).data; };
FXS.img.src = 'data:image/png;base64,${png.toString('base64')}';
`);
  console.log(`web/fxsprites.js: ${(png.length / 1024).toFixed(1)} KB PNG`);
}

// ---- preview: each sprite over a dark background, scaled 5x (tint sprites in a sample color) ----
const out = process.argv[2];
if (out) {
  const SC = 5, PW = 300;
  let x = 2, y = 2, rowH = 0; const place = [];
  for (const s of SPR) { if (x + s[1] + 2 > PW) { x = 2; y += rowH + 3; rowH = 0; } place.push([x, y]); x += s[1] + 3; rowH = Math.max(rowH, s[2]); }
  const PH = y + rowH + 2, buf = new Array(PW * PH).fill(0).map(() => [6, 6, 16]);
  const sample = [255, 60, 90];
  SPR.forEach(([, w, , tint, px], k) => {
    const [x0, y0] = place[k];
    px.forEach(([r, g, b, a], i) => {
      if (!a) return;
      let c = [r, g, b];
      if (tint) c = mix(sample.map((v) => v * r / 255), WHITE, g / 255);
      else if (r === 255 && g === 0 && b === 255) c = [255, 200, 60];
      buf[(y0 + Math.floor(i / w)) * PW + x0 + (i % w)] = c;
    });
  });
  const img = Buffer.alloc(PW * SC * PH * SC * 3);
  for (let yy = 0; yy < PH; yy++) for (let xx = 0; xx < PW; xx++) for (let j = 0; j < SC - 1; j++) for (let i = 0; i < SC - 1; i++) {
    const o = ((yy * SC + j) * PW * SC + xx * SC + i) * 3, c = buf[yy * PW + xx];
    img[o] = c[0]; img[o + 1] = c[1]; img[o + 2] = c[2];
  }
  await sharp(img, { raw: { width: PW * SC, height: PH * SC, channels: 3 } }).png().toFile(out);
  console.log('preview:', out);
}
