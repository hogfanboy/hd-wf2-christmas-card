// Effects: a JS copy of the card's effects library (effects.ino), for the preview.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
// ---------- Effects: a JS copy of the card's effects library (show.ino), for the preview ----------
const FX = { none: 'None', snow: 'Snow', sparkle: 'Sparkle', hearts: 'Hearts', confetti: 'Confetti', fireworks: 'Fireworks',
  stars: 'Starfield', fire: 'Fire', matrix: 'Code rain', sleigh: "Santa's sleigh", balloons: 'Balloons', fish: 'Aquarium', rain: 'Rain',
  lights: 'Christmas lights', turkey: 'Turkey & leaves', bunny: 'Easter bunny', bats: 'Bats & pumpkins', shamrocks: 'Shamrocks',
  flag: 'US flag', baby: 'Baby & stork',
  auto: 'Match the weather' };
// "Match the weather" (snow or rain when it does) on a weather item or a clock (the clock uses the weather the screen
// fetched last, or the weather where the screen is). On a countdown it means "match the holiday" (turkey for Thanksgiving...).
const fxOptions = (kind) => Object.entries(FX).filter(([k]) => k !== 'auto' || kind === 'weather' || kind === 'clock' || kind === 'countdown')
  .map(([k, v]) => `<option value="${k}">${k === 'auto' && kind === 'countdown' ? 'Match the holiday' : v}</option>`).join('');
const rand = (n) => Math.floor(Math.random() * n);
const sin8 = (a) => 128 + Math.trunc(127 * Math.sin((a & 255) * 6.2832 / 256));
const tr = Math.trunc;
function hsvRGB(h) {
  h = ((h % 1536) + 1536) % 1536; const x = h & 255;
  return [[255, x, 0], [255 - x, 255, 0], [0, 255, x], [0, 255 - x, 255], [x, 0, 255], [255, 0, 255 - x]][h >> 8];
}
const GLYPHS = '0123456789ABCDEFXZ$#@%&*+=<>';
function starReset(f) { f.x = rand(1024) - 512; f.y = rand(512) - 256; f.bri = 200 + rand(56); f.spd = 1 + rand(3); }
function fxNew(effect, behind) {
  const s = { effect, frame: 0, sparks: [], rockets: [{}, {}, {}], glyphs: [], sleighWait: 0,
    sleighX: (effect === 'sleigh' && !behind ? -94 : effect === 'turkey' ? -36 : effect === 'bunny' ? -32 : effect === 'baby' ? -36 : -50) * 16, bundle: 0,
    flakes: Array.from({ length: 48 }, () => ({ x: rand(W * 16), y: rand(H * 16), spd: 4 + rand(10), bri: 90 + rand(166) })),
    heat: effect === 'fire' ? new Uint8Array(W * H) : null,
    cols: Array.from({ length: 21 }, () => ({ head: rand(40), len: 3 + rand(5), spd: 1 + rand(3), g: Array.from({ length: 8 }, () => GLYPHS[rand(GLYPHS.length)]) })) };
  if (effect === 'stars') s.flakes.forEach(f => { starReset(f); f.bri = 30 + rand(225); });
  if (effect === 'hearts') s.flakes.forEach(f => f.y = rand(H * 24));
  return s;
}
const COS = [64,63,59,53,45,36,24,12,0,-12,-24,-36,-45,-53,-59,-63,-64,-63,-59,-53,-45,-36,-24,-12,0,12,24,36,45,53,59,63];
const BULB = [[255, 16, 32], [255, 176, 32], [32, 224, 64], [32, 80, 255]];   // Christmas lights: red, gold, green, blue
// One step of the effect. d = RGBA pixels to draw into, or null to only move things along.
function fxStep(s, d, behind) {
  const frame = s.frame++;
  const px = (x, y, c) => { x = tr(x); y = tr(y); if (!d || x < 0 || y < 0 || x >= W || y >= H) return; const i = (y * W + x) * 4; d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; };
  const add = (x, y, c, v) => { x = tr(x); y = tr(y); if (!d || x < 0 || y < 0 || x >= W || y >= H) return; const i = (y * W + x) * 4; d[i] += c[0] * v / 255; d[i + 1] += c[1] * v / 255; d[i + 2] += c[2] * v / 255; };
  const WHITE = [255, 255, 255];
  const spr = (n, x, y, flip, col) => fxSpr(d, n, x, y, flip, col);
  s.glyphs = [];
  switch (s.effect) {
    case 'snow':
      for (const f of s.flakes) {
        f.y += f.spd; f.x = (f.x + (((frame + f.spd) % 16 < 8) ? 1 : -1) + W * 16) % (W * 16);
        if (f.y >= H * 16) { f.y = 0; f.x = rand(W * 16); }
        const x = (tr(f.x / 16) + W) % W, y = tr(f.y / 16);
        px(x, y, [f.bri, f.bri, f.bri]);
        if (!behind && f.spd > 10) { add(x + 1, y, WHITE, f.bri / 2); add(x, y + 1, WHITE, f.bri / 2); }
      }
      break;
    case 'sparkle':
      s.flakes.forEach((f, i) => {
        const ph = tr(frame * f.spd / 4 + i * 37) % 64, v = ph < 32 ? ph * 8 : (63 - ph) * 8;
        if (ph === 63) { f.x = rand(W * 16); f.y = rand(H * 16); }
        add(f.x / 16, f.y / 16, hsvRGB(i * 200), v);
      });
      break;
    case 'hearts':
      for (let i = 0; i < (behind ? 10 : 18); i++) {
        const f = s.flakes[i];
        f.y -= tr(f.spd / 2) + 1;
        if (f.y < -9 * 16) { f.y = H * 16 + rand(64); f.x = rand(W * 16); }
        const x = tr(f.x / 16) + tr((sin8(frame * 3 + i * 40) - 128) / 40), y = tr(f.y / 16);
        spr(f.spd > 8 ? 'HEART' : 'HEART_S', x, y, false, [255, 30 + (i * 37) % 90, 90 + (i * 53) % 120]);
      }
      break;
    case 'confetti':
      s.flakes.forEach((f, i) => {
        f.y += tr(f.spd / 2) + 2;
        if (f.y >= H * 16) { f.y = -16; f.x = rand(W * 16); }
        const x = tr(f.x / 16) + tr((sin8(frame * 4 + i * 23) - 128) / 50), y = tr(f.y / 16), c = hsvRGB(i * 97), o = (tr(frame / 4) + i) % 3;
        px(x, y, c); if (o === 0) px(x + 1, y, c); else if (o === 1) px(x, y + 1, c);
      });
      break;
    case 'fireworks': {
      let any = s.rockets.some(r => r.on);
      for (const r of s.rockets) if (!r.on && (frame === 0 || (!any && rand(12) === 0) || rand(70) === 0)) {
        Object.assign(r, { x: (14 + rand(W - 28)) * 16, y: H * 16, vy: -(34 + rand(13)), hue: rand(1536), on: true }); any = true; break;
      }
      for (const r of s.rockets) {
        if (!r.on) continue;
        r.y += r.vy; r.vy += 1;
        const x = tr(r.x / 16), y = tr(r.y / 16);
        add(x, y, [255, 240, 192], 255); add(x, y + 1, [255, 144, 48], 150); add(x, y + 2, [255, 96, 16], 70);
        if (r.vy >= -3 || r.y < 5 * 16) {
          r.on = false;
          const speed = 14 + rand(10);
          for (let n = 0; n < 28; n++) {
            const a = tr(n * 32 / 28), sp = speed + rand(5), life = 28 + rand(18);
            s.sparks.push({ x: r.x, y: r.y, vx: tr(COS[a] * sp / 64), vy: tr(COS[(a + 24) & 31] * sp / 64), life, max: life, hue: (r.hue + rand(120)) % 1536 });
          }
        }
      }
      for (const p of s.sparks) {
        p.x += p.vx; p.y += p.vy;
        if (frame & 1) p.vy += 1;
        if ((frame & 3) === 0) { p.vx = tr(p.vx * 7 / 8); p.vy = tr(p.vy * 7 / 8); }
        p.life--;
        const v = 255 * p.life / p.max;
        if (p.life < 8 && rand(3) === 0) continue;
        const c = p.life > p.max - 4 ? WHITE : hsvRGB(p.hue);
        add(p.x / 16, p.y / 16, c, v); add((p.x - p.vx * 2) / 16, (p.y - p.vy * 2) / 16, c, v / 3);
      }
      s.sparks = s.sparks.filter(p => p.life > 0).slice(-96);
      break;
    }
    case 'stars':
      for (const f of s.flakes) {
        const z0 = f.bri;
        f.bri = z0 > f.spd * 2 ? z0 - f.spd * 2 : 0;
        if (f.bri < 8) { starReset(f); continue; }
        const x = W / 2 + tr(f.x * 16 / f.bri), y = H / 2 + tr(f.y * 16 / f.bri), x0 = W / 2 + tr(f.x * 16 / z0), y0 = H / 2 + tr(f.y * 16 / z0);
        if (x < 0 || y < 0 || x >= W || y >= H) { starReset(f); continue; }
        const v = 255 - f.bri;
        add(x, y, [220, 230, 255], v); if (x0 !== x || y0 !== y) add(x0, y0, [220, 230, 255], v / 3);
      }
      break;
    case 'fire': {
      const h = s.heat;
      for (let x = 0; x < W; x++) h[(H - 1) * W + x] = rand(3) ? 36 : 12 + rand(20);
      for (let y = 0; y < H - 1; y++) for (let x = 0; x < W; x++) {
        const sx = Math.max(0, Math.min(W - 1, x + rand(3) - 1)), dd = rand(4), decay = behind ? (dd < 2 ? dd : 2) : (dd ? 1 : 0);
        h[y * W + x] = Math.max(0, h[(y + 1) * W + sx] - decay);
      }
      if (d) for (let i = 0; i < W * H; i++) {
        if (h[i] < 2) continue;
        const t = h[i] * 255 / 36;
        d[i * 4] = t * 3; d[i * 4 + 1] = (t - 85) * 3; d[i * 4 + 2] = (t - 190) * 4;
      }
      break;
    }
    case 'sleigh': {                       // same as fxSleigh in effects.ino
      if (!behind) {
        nightSky(px, add, s, frame, 24);
        spr('MOON', 99, 2); spr('TOWN', 0, H - 30);
        FXS.win.forEach(([wx, wy, ww, wh], k) => {
          const dark = (tr(frame / 150) + k * 3) % 9 === 0;
          for (let y = 0; y < wh; y++) for (let x = 0; x < ww; x++) px(wx + x, H - 30 + wy + y, hx(dark ? 0x241810 : (y === 0 || x === ww - 1) ? 0xD88A28 : 0xFFC860));
        });
      }
      const team = behind ? 50 : 94, k = behind ? 0.42 : 0.8;
      if (s.sleighWait) s.sleighWait--;
      else if ((s.sleighX += behind ? 9 : 16) > (W + 4) * 16) { s.sleighX = -team * 16; s.sleighWait = 20 + rand(60); }
      const x = tr(s.sleighX / 16), y = (behind ? 1 : 5) + tr((sin8(frame * 2) - 128) / (behind ? 40 : 26));
      if (!s.sleighWait) for (let n = 0; n < 3; n++) { const life = 14 + rand(18); s.sparks.push({ x: (x + tr(4 * k)) * 16 + rand(tr(80 * k)), y: (y + tr(14 * k) + rand(tr(10 * k) + 1)) * 16, vx: -(2 + rand(8)), vy: rand(5) - 2, life, max: life }); }
      for (const p of s.sparks) { p.x += p.vx; p.y += p.vy; p.life--; add(p.x / 16, p.y / 16, p.life > p.max - 3 ? WHITE : [255, 208, 96], 255 * p.life / p.max); }
      s.sparks = s.sparks.filter(p => p.life > 0).slice(-96);
      if (!s.sleighWait) {
        const step = tr(frame / 4) & 1, ns = step ? 0 : 1, d1 = x + tr(50 * k), d2 = x + tr(83 * k), dy = y - tr(3 * k), hx0 = tr(20.5 * k), hy = tr(13 * k), sm = behind ? '_S' : '';
        fxLine(px, x + tr(30.5 * k), y + tr(15 * k), d1 + hx0, dy + step + hy, hx(0xC89030));
        fxLine(px, d1 + hx0, dy + step + hy, d2 + hx0, dy + ns + hy, hx(0xC89030));
        spr((step ? 'DEER0' : 'DEER1') + sm, d1, dy + step);
        spr((step ? 'DEER1R' : 'DEER0R') + sm, d2, dy + ns);
        spr('SLEIGH' + sm, x, y);
      }
      if (!behind) for (let i = 24; i < 44; i++) {
        const f = s.flakes[i]; f.y += tr(f.spd / 3) + 2;
        if (f.y >= H * 16) { f.y = 0; f.x = rand(W) * 16; }
        add(tr(f.x / 16) + (tr((frame + i * 7) / 24) & 1), f.y / 16, WHITE, tr(f.bri * 3 / 4));
      }
      break;
    }
    case 'turkey': case 'bunny': case 'bats': case 'shamrocks': case 'flag': case 'baby': fxHoliday(s, frame, px, add, spr, behind); break;
    case 'balloons':
      for (let i = 0; i < (behind ? 6 : 10); i++) {
        const f = s.flakes[i];
        f.y -= tr(f.spd / 3) + 2;
        if (f.y < -21 * 16) { f.y = H * 16 + rand(320); f.x = rand(W - 10) * 16; f.bri = rand(7); }
        const x = tr(f.x / 16) + tr((sin8(frame * 2 + i * 50) - 128) / 48), y = tr(f.y / 16);
        for (let r = 0; r < 7; r++) px(x + 5 + (((tr(frame / 6) + r + i) & 2) ? 1 : 0), y + 14 + r, [140, 140, 140]);
        spr('BALLOON', x, y, false, PARTY[f.bri % 7]);
      }
      break;
    case 'fish': {
      for (let i = 16; i < (behind ? 26 : 36); i++) {
        const f = s.flakes[i];
        f.y -= tr(f.spd / 3) + 3;
        if (f.y < 0) { f.y = H * 16 + rand(400); f.x = rand(W) * 16; }
        const x = tr(f.x / 16) + ((tr(frame / 8) + i) & 1), y = tr(f.y / 16), b = [128, 176, 255];
        if (f.spd > 10) { add(x, y, b, 150); add(x + 1, y, b, 150); add(x, y + 1, b, 90); add(x + 1, y + 1, b, 90); }
        else add(x, y, [160, 200, 255], 170);
      }
      for (let i = 0; i < (behind ? 4 : 7); i++) {
        const f = s.flakes[i], left = !!(f.bri & 128);
        f.x += left ? -(tr(f.spd / 3) + 2) : tr(f.spd / 3) + 2;
        if (f.x < -16 * 16 || f.x > (W + 4) * 16) {
          f.bri = (rand(2) ? 128 : 0) | rand(7); f.spd = 4 + rand(10);
          f.x = (f.bri & 128) ? (W + 2) * 16 : -16 * 16; f.y = (2 + rand(H - 12)) * 16;
        }
        spr('FISH', tr(f.x / 16), tr(f.y / 16) + tr((sin8(frame * 3 + i * 70) - 128) / 64), left, PARTY[(f.bri & 7) % 7]);
      }
      break;
    }
    case 'rain':
      for (let i = 0; i < (behind ? 28 : 48); i++) {
        const f = s.flakes[i], c = [140, 170, 255];
        f.y += f.spd * 2 + 14; f.x -= 3;
        if (f.y >= H * 16) {
          if (!behind) { add(tr(f.x / 16) - 1, H - 1, c, 140); add(tr(f.x / 16) + 1, H - 1, c, 140); }
          f.y = -rand(64); f.x = rand(W + 16) * 16;
        }
        const x = tr(f.x / 16), y = tr(f.y / 16), v = behind ? tr(f.bri * 3 / 4) : f.bri;
        add(x, y, c, v); add(x, y - 1, c, v / 2); add(x, y - 2, c, v / 4);
      }
      break;
    case 'lights': {                       // same as fxLights in effects.ino
      if (!behind) for (let i = 0; i < 24; i++) {
        const f = s.flakes[i];
        f.y += tr(f.spd / 2) + 1;
        if (f.y >= (H - 4) * 16) { f.y = 4 * 16; f.x = (4 + rand(W - 8)) * 16; }
        if (f.y < 4 * 16) f.y = 4 * 16;
        const x = tr(f.x / 16); if (x < 4 || x >= W - 4) { f.x = (4 + rand(W - 8)) * 16; continue; }
        add(x, f.y / 16, WHITE, tr(f.bri * 2 / 3));
      }
      const wire = [12, 56, 18];
      for (let x = 0; x < W; x++) { px(x, 0, wire); px(x, H - 1, wire); }
      for (let y = 0; y < H; y++) { px(0, y, wire); px(W - 1, y, wire); }
      const mode = behind ? 0 : tr(frame / 200) % 3;
      for (let i = 0; i < 44; i++) {
        const v = mode === 0 ? 70 + ((sin8(frame * 3 + i * 53) * 185) >> 8)
          : mode === 1 ? ((i + tr(frame / 5)) % 4 === 0 ? 255 : 45)
          : ((i & 1) === (tr(frame / 20) & 1) ? 255 : 45);
        const c = BULB[i % 4].map(ch => ch - tr(ch * (255 - v) / 255));
        const [x, y] = i < 16 ? [3 + i * 8, 1] : i < 22 ? [W - 3, 9 + (i - 16) * 8] : i < 38 ? [3 + (37 - i) * 8, H - 3] : [1, 9 + (43 - i) * 8];
        px(x, y, c); px(x + 1, y, c); px(x, y + 1, c); px(x + 1, y + 1, c);
      }
      break;
    }
    case 'matrix':
      s.cols.forEach((c, ci) => {
        if (frame % c.spd === 0) c.head++;
        const head = tr(c.head / 4);
        if (head - c.len > 8) { c.head = 0; c.len = 3 + rand(6); c.spd = 1 + rand(3); }
        if (rand(6) === 0) c.g[rand(8)] = GLYPHS[rand(GLYPHS.length)];
        for (let r = 0; r < 8; r++) {
          const age = head - r;
          if (age < 0 || age > c.len) continue;
          const v = 255 - tr(age * 200 / (c.len + 1));
          s.glyphs.push([c.g[r], ci * 6 + 1, r * 8, age === 0 ? 'rgb(200,255,200)' : `rgb(${tr(v / 6)},${v},${tr(v / 5)})`]);
        }
      });
      break;
  }
}
// Sprite effects: shaded sprites from tools/make-fxsprites.mjs (web/fxsprites.js), the same pixels as the card.
const PARTY = [[255, 32, 48], [255, 192, 32], [48, 112, 255], [48, 208, 96], [255, 80, 176], [160, 80, 255], [255, 128, 32]];
const mixRGB = (a, b, n) => a.map((v, i) => tr(v + (b[i] - v) * n));
function fxSpr(d, name, x, y, flip = false, col = [0, 0, 0]) {     // same as fxSpr in effects.ino
  const r = FXS.spr[name], p = FXS.px; if (!d || !r || !p) return;
  const [sx, sy, w, h, tint] = r; x = tr(x); y = tr(y);
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
    const o = ((sy + j) * FXS.sw + sx + i) * 4; if (!p[o + 3]) continue;
    const X = x + (flip ? w - 1 - i : i), Y = y + j; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
    const q = (Y * W + X) * 4;
    if (tint) for (let c = 0; c < 3; c++) { const v = tr(col[c] * p[o] / 255); d[q + c] = v + tr((255 - v) * p[o + 1] / 255); }
    else if (p[o] === 255 && p[o + 1] === 0 && p[o + 2] === 255) { d[q] = col[0]; d[q + 1] = col[1]; d[q + 2] = col[2]; }
    else { d[q] = p[o]; d[q + 1] = p[o + 1]; d[q + 2] = p[o + 2]; }
  }
}
function fxLine(px, x0, y0, x1, y1, c) {     // not "line": 3-weather.js has one (the app is one script)
  const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1; let e = dx + dy;
  for (;;) { px(x0, y0, c); if (x0 === x1 && y0 === y1) break; const e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
function nightSky(px, add, s, frame, n) {
  for (let y = 0; y < H; y++) { const c = mixRGB([0, 2, 12], [14, 20, 52], Math.min(y, 44) / 44); for (let x = 0; x < W; x++) px(x, y, c); }
  for (let i = 0; i < n; i++) { const f = s.flakes[i]; add(f.x / 16, f.y / 32, [220, 230, 255], (sin8(frame * (tr(f.spd / 4) + 1) + i * 41) * f.bri) / 512); }
}
// Holiday effects: same as effects.ino.
const hx = (h) => [(h >> 16) & 255, (h >> 8) & 255, h & 255];
const BABYC = [0xFF8CC0, 0x7CC0FF, 0xFFD870, 0x8CE8A8];   // baby: pink, blue, yellow, mint
const LEAF = [0xE04010, 0xFF9020, 0xF0C030, 0x9A4A18], PASTEL = [0xFF90C0, 0xB090FF, 0x80E8A0, 0xFFE070, 0x80C0FF], GREENS = [0x20C040, 0x40E060, 0x109030];
function fxHoliday(s, frame, px, add, spr, behind) {
  const WHITE = [255, 255, 255];
  const faintStars = (n) => { for (let i = 0; i < n; i++) { const f = s.flakes[i]; add(f.x / 16, f.y / 32, WHITE, (sin8(frame * 2 + i * 41) * f.bri) / 1024); } };
  switch (s.effect) {
    case 'turkey': {
      for (let i = 0; i < (behind ? 14 : 30); i++) {
        const f = s.flakes[i]; f.y += tr(f.spd / 4) + 1;
        if (f.y >= H * 16) { f.y = -32; f.x = rand(W) * 16; }
        const x = tr(f.x / 16) + tr((sin8(frame * 2 + i * 37) - 128) / 36), y = tr(f.y / 16), c = hx(LEAF[i & 3]);
        px(x, y, c); if ((tr(frame / 8) + i) & 1) px(x + 1, y, c); else px(x, y + 1, c); if (f.spd > 10) px(x + 1, y + 1, c);
      }
      if (behind) return;
      for (let x = 0; x < W; x++) { px(x, H - 1, hx(0x4A2810)); px(x, H - 2, hx(LEAF[(x * 7 + tr(x / 3)) & 3]).map(v => v - tr(v / 2))); }
      if (s.sleighWait) { s.sleighWait--; return; }
      if ((s.sleighX += 9) > (W + 2) * 16) { s.sleighX = -36 * 16; s.sleighWait = 40 + rand(60); return; }
      const step = tr(frame / 6) & 1;
      spr(step ? 'TURKEY1' : 'TURKEY0', tr(s.sleighX / 16), H - 2 - 34 + 1 - step);
      return;
    }
    case 'bunny': {
      if (behind) {
        for (let i = 0; i < 7; i++) {
          const f = s.flakes[i]; f.y -= tr(f.spd / 4) + 1;
          if (f.y < -11 * 16) { f.y = H * 16 + rand(320); f.x = rand(W - 8) * 16; }
          spr('EGG', tr(f.x / 16) + tr((sin8(frame * 2 + i * 50) - 128) / 60), tr(f.y / 16), false, hx(PASTEL[i % 5]));
        }
        return;
      }
      faintStars(16);
      for (let x = 0; x < W; x++) {
        for (let y = H - 3; y < H; y++) px(x, y, hx(y === H - 3 ? 0x28922F : 0x1E7A28));
        const k = (x * 37) % 11;
        if (k < 4) px(x, H - 4, hx(0x30B040));
        if (k === 0 && (x & 1)) px(x, H - 5, hx(0x30B040));
        if (x % 19 === 7) px(x, H - 5, hx(x % 38 === 7 ? 0xFF8CB4 : 0xFFE040));
      }
      [8, 32, 60, 86, 112].forEach((ex, e) => spr('EGG', ex, H - 11, false, hx(PASTEL[e])));
      if (s.sleighWait) { s.sleighWait--; return; }
      const ph = frame % 30; let hop = 0;
      if (ph < 20) { hop = tr((sin8(tr(ph * 128 / 20)) - 128) * 14 / 127); s.sleighX += 28; }
      if (s.sleighX > (W + 2) * 16) { s.sleighX = -32 * 16; s.sleighWait = 30 + rand(50); return; }
      spr(hop > 3 ? 'BUNNY1' : 'BUNNY0', tr(s.sleighX / 16), H - 3 - 26 - hop);
      return;
    }
    case 'bats': {
      if (!behind) {
        faintStars(16);
        spr('MOON_O', 98, 3);
        const glow = mixRGB(hx(0xFFE040), hx(0xFF8000), (tr(sin8(frame * 5) / 2) + rand(40)) / 255);
        spr('PUMPKIN', 8, H - 14, false, glow); spr('PUMPKIN', 102, H - 14, true, mixRGB(glow, hx(0xFF8000), 1 / 3));
      }
      for (let i = 0; i < (behind ? 4 : 6); i++) {
        const f = s.flakes[40 + i], left = f.bri & 128;
        f.x += left ? -(tr(f.spd / 3) + 4) : tr(f.spd / 3) + 4;
        if (f.x < -22 * 16 || f.x > (W + 3) * 16) { f.bri = rand(2) ? 128 : 0; f.spd = 4 + rand(10); f.x = (f.bri & 128) ? (W + 2) * 16 : -21 * 16; f.y = (2 + rand(behind ? 20 : 34)) * 16; }
        spr((tr(frame / 5) + i) & 1 ? 'BAT0' : 'BAT1', tr(f.x / 16), tr(f.y / 16) + tr((sin8(frame * 4 + i * 60) - 128) / 32));
      }
      return;
    }
    case 'shamrocks':
      for (let i = 0; i < (behind ? 10 : 18); i++) {
        const f = s.flakes[i]; f.y += tr(f.spd / 4) + 1;
        if (f.y >= H * 16) { f.y = -14 * 16; f.x = rand(W) * 16; }
        spr(f.spd > 8 ? 'CLOVER' : 'CLOVER_S', tr(f.x / 16) + tr((sin8(frame * 3 + i * 40) - 128) / 40), tr(f.y / 16), false, hx(GREENS[i % 3]));
      }
      if (!behind) for (let i = 30; i < 42; i++) {
        const f = s.flakes[i], ph = (frame * 2 + i * 23) % 80;
        if (ph === 0) { f.x = rand(W) * 16; f.y = rand(H) * 16; }
        const v = ph < 20 ? ph * 12 : ph < 40 ? (40 - ph) * 12 : 0, x = tr(f.x / 16), y = tr(f.y / 16), g = hx(0xFFC830);
        add(x, y, g, v);
        if (v > 150) { const v3 = tr(v / 3); add(x + 1, y, g, v3); add(x - 1, y, g, v3); add(x, y + 1, g, v3); add(x, y - 1, g, v3); }
      }
      return;
    case 'baby': {                       // same as fxBaby: footprints, rattles, hearts; the stork full screen or on the due day
      for (let i = 0; i < (behind ? 8 : 14); i++) {
        const f = s.flakes[i]; f.y -= tr(f.spd / 4) + 1;
        if (f.y < -14 * 16) { f.y = H * 16 + rand(320); f.x = rand(W - 12) * 16; }
        const k = i % 3;
        spr(k === 0 ? 'FEET' : k === 1 ? 'RATTLE' : 'HEART_S', tr(f.x / 16) + tr((sin8(frame * 2 + i * 45) - 128) / 50), tr(f.y / 16), false, hx(BABYC[(tr(i / 3) + k) & 3]));
      }
      if (behind && !s.party) return;
      if (s.sleighWait) { s.sleighWait--; return; }
      if ((s.sleighX += behind ? 10 : 12) > (W + 2) * 16) { s.sleighX = -36 * 16; s.sleighWait = 30 + rand(50); s.bundle++; return; }
      const up = tr(frame / 6) & 1;
      spr('STORK' + (up ? 0 : 1) + (behind ? '_S' : ''), tr(s.sleighX / 16), (behind ? 1 : 12) + tr((sin8(frame * 4) - 128) / 40), false, hx(BABYC[s.bundle & 3]));
      return;
    }
    case 'flag': {
      const top = 6, x0 = 3;
      if (!behind) {
        for (let y = top - 2; y < H; y++) { px(0, y, [144, 144, 144]); px(1, y, [106, 106, 106]); }
        for (let y = top - 4; y < top - 2; y++) { px(0, y, hx(0xFFC830)); px(1, y, hx(0xFFC830)); }
      }
      for (let x = x0; x < W; x++) {
        const a = x * 4 - frame * 5, fx = x - x0;
        const dy = tr((sin8(a) - 128) * 3 * fx / (127 * (W - x0)));
        let k = 200 + tr((sin8(a + 64) - 128) * 55 / 127);
        if (behind) k = tr(k / 4);
        for (let y = 0; y < 52; y++) {
          let c = (tr(y / 4) & 1) ? 0xE8E8E8 : 0xD01020;
          if (fx < 52 && y < 28) {
            c = 0x1838A0;
            const r = y - 2, odd = tr(r / 3) & 1, sx = fx - (odd ? 8 : 4);
            if (r >= 0 && r <= 24 && r % 3 === 0 && sx >= 0 && sx % 8 === 0 && sx <= 40 - odd * 8) c = 0xFFFFFF;
          }
          px(x, top + y + dy, hx(c).map(v => v + tr(-v * (255 - k) / 255)));
        }
      }
      return;
    }
  }
}
function paintBg(it, t) {
  tctx.fillStyle = it.bg; tctx.fillRect(0, 0, W, H);
  let effect = it.effect;
  if (effect === 'auto' && it.kind === 'countdown') { const h = countdownDays(it.date).hol; effect = isBaby(it.date) ? 'baby' : h >= 0 ? HOLIDAYS[h][7] : 'sparkle'; }   // match the holiday
  else if (it.kind === 'dates' && datesShown(it, t)?.n === 0) effect = 'fireworks';   // the big day
  else if (effect === 'auto') {                    // match the weather (a clock: the show's first weather item)
    const w = it.kind === 'weather' ? it : items.find(i => i.kind === 'weather' && i.wx), k = w ? wxKind((w.wx || SAMPLE_WX).code) : '';
    effect = k === 'snow' ? 'snow' : k === 'rain' || k === 'storm' ? 'rain' : 'none';
  }
  if (!effect || effect === 'none') return;
  const per = it.kind === 'text' ? 25 : it.kind === 'fx' ? 30 : 40, behind = it.kind !== 'fx';
  if (!it.fx || it.fx.t0 !== it.t0 || it.fx.effect !== effect) { it.fx = fxNew(effect, behind); it.fx.t0 = it.t0; }
  it.fx.dark = /^#0{6}$/i.test(it.bg);
  it.fx.party = it.kind === 'countdown' && isBaby(it.date) && countdownDays(it.date).late <= 0;   // due day: the stork flies behind the words
  const n = Math.max(1, Math.min(4, Math.floor(t / per) + 1 - it.fx.frame));
  for (let i = 1; i < n; i++) fxStep(it.fx, null, behind);
  const img = tctx.getImageData(0, 0, W, H);
  fxStep(it.fx, img.data, behind);
  tctx.putImageData(img, 0, 0);
  if (it.fx.glyphs.length) {
    const keep = tctx.font, kb = tctx.textBaseline;
    tctx.font = '8px monospace'; tctx.textBaseline = 'top';
    for (const [ch, x, y, col] of it.fx.glyphs) { tctx.fillStyle = col; tctx.fillText(ch, x, y); }
    tctx.font = keep; tctx.textBaseline = kb;
  }
}

