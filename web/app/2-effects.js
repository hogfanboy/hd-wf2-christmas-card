// Effects: a JS copy of the card's effects library (effects.ino), for the preview.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
// ---------- Effects: a JS copy of the card's effects library (show.ino), for the preview ----------
const FX = { none: 'None', snow: 'Snow', sparkle: 'Sparkle', hearts: 'Hearts', confetti: 'Confetti', fireworks: 'Fireworks',
  stars: 'Starfield', fire: 'Fire', matrix: 'Code rain', sleigh: "Santa's sleigh", balloons: 'Balloons', fish: 'Aquarium', rain: 'Rain',
  lights: 'Christmas lights',
  auto: 'Match the weather' };
// "Match the weather" (snow or rain when it does) only makes sense on a weather item.
const fxOptions = (kind) => Object.entries(FX).filter(([k]) => k !== 'auto' || kind === 'weather').map(([k, v]) => `<option value="${k}">${v}</option>`).join('');
const rand = (n) => Math.floor(Math.random() * n);
const sin8 = (a) => 128 + Math.trunc(127 * Math.sin((a & 255) * 6.2832 / 256));
const tr = Math.trunc;
function hsvRGB(h) {
  h = ((h % 1536) + 1536) % 1536; const x = h & 255;
  return [[255, x, 0], [255 - x, 255, 0], [0, 255, x], [0, 255 - x, 255], [x, 0, 255], [255, 0, 255 - x]][h >> 8];
}
const GLYPHS = '0123456789ABCDEFXZ$#@%&*+=<>';
function starReset(f) { f.x = rand(1024) - 512; f.y = rand(512) - 256; f.bri = 200 + rand(56); f.spd = 1 + rand(3); }
function fxNew(effect) {
  const s = { effect, frame: 0, sparks: [], rockets: [{}, {}, {}], glyphs: [], sleighX: -46 * 16, sleighWait: 0,
    flakes: Array.from({ length: 48 }, () => ({ x: rand(W * 16), y: rand(H * 16), spd: 4 + rand(10), bri: 90 + rand(166) })),
    heat: effect === 'fire' ? new Uint8Array(W * H) : null,
    cols: Array.from({ length: 21 }, () => ({ head: rand(40), len: 3 + rand(5), spd: 1 + rand(3), g: Array.from({ length: 8 }, () => GLYPHS[rand(GLYPHS.length)]) })) };
  if (effect === 'stars') s.flakes.forEach(f => { starReset(f); f.bri = 30 + rand(225); });
  if (effect === 'hearts') s.flakes.forEach(f => f.y = rand(H * 24));
  return s;
}
const COS = [64,63,59,53,45,36,24,12,0,-12,-24,-36,-45,-53,-59,-63,-64,-63,-59,-53,-45,-36,-24,-12,0,12,24,36,45,53,59,63];
const BULB = [[255, 16, 32], [255, 176, 32], [32, 224, 64], [32, 80, 255]];   // Christmas lights: red, gold, green, blue
const HEART_BIG = [0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08], HEART_SMALL = [0x5, 0x7, 0x2];
// One step of the effect. d = RGBA pixels to draw into, or null to only move things along.
function fxStep(s, d, behind) {
  const frame = s.frame++;
  const px = (x, y, c) => { x = tr(x); y = tr(y); if (!d || x < 0 || y < 0 || x >= W || y >= H) return; const i = (y * W + x) * 4; d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; };
  const add = (x, y, c, v) => { x = tr(x); y = tr(y); if (!d || x < 0 || y < 0 || x >= W || y >= H) return; const i = (y * W + x) * 4; d[i] += c[0] * v / 255; d[i + 1] += c[1] * v / 255; d[i + 2] += c[2] * v / 255; };
  const WHITE = [255, 255, 255];
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
        if (f.y < -8 * 16) { f.y = H * 16 + rand(64); f.x = rand(W * 16); }
        const x = tr(f.x / 16) + tr((sin8(frame * 3 + i * 40) - 128) / 40), y = tr(f.y / 16);
        const c = [255, 30 + (i * 37) % 90, 90 + (i * 53) % 120], big = f.spd > 8, rows = big ? HEART_BIG : HEART_SMALL, w = big ? 7 : 3;
        rows.forEach((bits, r) => { for (let b = 0; b < w; b++) if ((bits >> (w - 1 - b)) & 1) px(x + b, y + r, c); });
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
    case 'sleigh': {
      if (!behind) s.flakes.slice(0, 24).forEach((f, i) => add(f.x / 16, f.y / 16, [220, 230, 255], (sin8(frame * (tr(f.spd / 4) + 1) + i * 41) * f.bri) / 512));
      if (s.sleighWait) s.sleighWait--;
      else if ((s.sleighX += behind ? 9 : 11) > (W + 4) * 16) { s.sleighX = -46 * 16; s.sleighWait = 20 + rand(60); }
      const x = tr(s.sleighX / 16), y = (behind ? 2 : 24) + tr((sin8(frame * 2) - 128) / 40);
      if (!s.sleighWait) for (let k = 0; k < 3; k++) { const life = 14 + rand(18); s.sparks.push({ x: (x + 1) * 16 + rand(32), y: (y + 6 + rand(5)) * 16, vx: -(2 + rand(8)), vy: rand(5) - 2, life, max: life }); }
      for (const p of s.sparks) { p.x += p.vx; p.y += p.vy; p.life--; add(p.x / 16, p.y / 16, p.life > p.max - 3 ? WHITE : [255, 208, 96], 255 * p.life / p.max); }
      s.sparks = s.sparks.filter(p => p.life > 0).slice(-96);
      if (s.sleighWait) break;
      for (let i = x + 8; i < x + 37; i++) px(i, y + 4 + (i > x + 22 ? 1 : 0), [176, 128, 32]);
      const step = tr(frame / 4) & 1;
      sprite(px, step ? SPR.deerA : SPR.deerB, x + 17, y + step, null);
      sprite(px, step ? SPR.deerB : SPR.deerA, x + 30, y - 1 + (step ? 0 : 1), null, false, true);
      sprite(px, SPR.sleigh, x, y, null);
      break;
    }
    case 'balloons':
      for (let i = 0; i < (behind ? 6 : 10); i++) {
        const f = s.flakes[i];
        f.y -= tr(f.spd / 3) + 2;
        if (f.y < -15 * 16) { f.y = H * 16 + rand(320); f.x = rand(W - 8) * 16; f.bri = rand(7); }
        const x = tr(f.x / 16) + tr((sin8(frame * 2 + i * 50) - 128) / 48), y = tr(f.y / 16);
        for (let r = 0; r < 6; r++) px(x + 3 + (((tr(frame / 6) + r + i) & 2) ? 1 : 0), y + 8 + r, [140, 140, 140]);
        sprite(px, SPR.balloon, x, y, PARTY[f.bri % 7]);
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
        if (f.x < -12 * 16 || f.x > (W + 4) * 16) {
          f.bri = (rand(2) ? 128 : 0) | rand(7); f.spd = 4 + rand(10);
          f.x = (f.bri & 128) ? (W + 2) * 16 : -10 * 16; f.y = (3 + rand(H - 10)) * 16;
        }
        sprite(px, SPR.fish, tr(f.x / 16), tr(f.y / 16) + tr((sin8(frame * 3 + i * 70) - 128) / 64), PARTY[(f.bri & 7) % 7], left);
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
// Sprite effects (1.0): same rows and colors as the card (effects.ino).
const SPR = {
  sleigh: ['...WRR.........', '....RRR........', '....WWWW.......', '....SSSS.......', 'R...WWWW......R', 'RR..RWWR.....RR',
    'RRRRRRRRRRRRRR.', 'RGGGGGGGGGGGGR.', '.RRRRRRRRRRRR..', '..G.......G...G', 'GGGGGGGGGGGGGG.'],
  deerA: ['.......b.b.', '........bb.', '.......BBBN', '.BBBBBBBB..', 'BBBBBBBB...', '.b.b..b.b..', 'b...b.b...b'],
  deerB: ['.......b.b.', '........bb.', '.......BBBN', '.BBBBBBBB..', 'BBBBBBBB...', '..bb..bb...', '..bb..bb...'],
  balloon: ['..XXX..', '.XXXXX.', 'XHXXXXX', 'XXXXXXX', 'XXXXXXD', '.XXXXD.', '..XXD..', '...X...'],
  fish: ['...XXX..', 'T.XXLXX.', 'TTXXLXEX', 'T.XXLXX.', '...XXX..'],
};
const PARTY = [[255, 32, 48], [255, 192, 32], [48, 112, 255], [48, 208, 96], [255, 80, 176], [160, 80, 255], [255, 128, 32]];
const mixRGB = (a, b, n) => a.map((v, i) => tr(v + (b[i] - v) * n));
function sprite(px, rows, x, y, main, flip = false, nose = false) {
  const FIX = { H: [255, 255, 255], W: [255, 255, 255], E: [255, 255, 255], R: [224, 16, 32], S: [255, 180, 140], G: [255, 200, 48],
    B: [160, 96, 44], b: [106, 58, 24], N: nose ? [255, 16, 16] : [160, 96, 44] };
  rows.forEach((row, r) => { for (let i = 0; i < row.length; i++) {
    const ch = row[flip ? row.length - 1 - i : i];
    const c = ch === 'X' ? main : ch === 'D' ? mixRGB(main, [0, 0, 0], 2 / 5) : ch === 'T' ? mixRGB(main, [0, 0, 0], 1 / 4) : ch === 'L' ? mixRGB(main, [255, 255, 255], 1 / 2) : FIX[ch];
    if (c) px(x + i, y + r, c);
  } });
}
function paintBg(it, t) {
  tctx.fillStyle = it.bg; tctx.fillRect(0, 0, W, H);
  let effect = it.effect;
  if (effect === 'auto') { const k = it.kind === 'weather' ? wxKind((it.wx || SAMPLE_WX).code) : ''; effect = k === 'snow' ? 'snow' : k === 'rain' || k === 'storm' ? 'rain' : 'none'; }
  if (!effect || effect === 'none') return;
  if (!it.fx || it.fx.t0 !== it.t0 || it.fx.effect !== effect) { it.fx = fxNew(effect); it.fx.t0 = it.t0; }
  it.fx.dark = /^#0{6}$/i.test(it.bg);
  const per = it.kind === 'text' ? 25 : it.kind === 'fx' ? 30 : 40, behind = it.kind !== 'fx';
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

