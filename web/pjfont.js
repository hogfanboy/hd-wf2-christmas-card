// Pocket Jumbotron font ("PJ"): our own rounded-square scoreboard letters, drawn as strokes (centerlines) on a
// grid where 0 = cap top and 100 = baseline (lowercase from 34, descenders to 126). The same code renders the
// bitmaps for the card (tools/make-pjfont.mjs -> pj_font.h) and the page preview, so both show the same pixels.
// Point = [x, y] or [x, y, cornerRadius]; corners round with R unless the point says 0. One point = a dot.
(function (G) {
  const R = 18;
  const box = (x0, y0, x1, y1, r = R) => ({ closed: true, pts: [[x0, y0, r], [x1, y0, r], [x1, y1, r], [x0, y1, r]] });
  const X = 34, B = 100, D = 126;                 // x-height top, baseline, descender
  // [width, ...strokes]
  const GL = {
    ' ': [30],
    '!': [0, [[0, 0], [0, 66]], [[0, 100]]],
    '"': [20, [[0, 0], [0, 24]], [[20, 0], [20, 24]]],
    '#': [52, [[16, 8], [16, 92]], [[36, 8], [36, 92]], [[0, 34], [52, 34]], [[0, 66], [52, 66]]],
    '$': [52, [[50, 12], [0, 12], [0, 50], [52, 50], [52, 88], [0, 88]], [[26, -6], [26, 106]]],
    '%': [56, box(0, 0, 16, 28, 6), box(40, 72, 56, 100, 6), [[54, 0], [2, 100]]],
    '&': [60, [[60, 100], [14, 44, 6], [14, 0], [44, 0], [44, 34], [0, 62], [0, 100], [44, 100], [60, 80, 0]]],
    "'": [0, [[0, 0], [0, 24]]],
    '(': [22, [[22, -8], [0, -8, 22], [0, 108, 22], [22, 108]]],
    ')': [22, [[0, -8], [22, -8, 22], [22, 108, 22], [0, 108]]],
    '*': [40, [[20, 0], [20, 40]], [[2, 10], [38, 30]], [[2, 30], [38, 10]]],
    '+': [48, [[0, 64], [48, 64]], [[24, 40], [24, 88]]],
    ',': [6, [[6, 96], [0, 116]]],
    '-': [34, [[0, 64], [34, 64]]],
    '.': [0, [[0, 100]]],
    '/': [44, [[0, 108], [44, -8]]],
    '0': [52, box(0, 0, 52, 100, 22)],
    '1': [52, [[12, 14, 0], [28, 0, 0], [28, 100]]],
    '2': [52, [[0, 0], [52, 0], [52, 50], [0, 50], [0, 100, 0], [52, 100]]],
    '3': [52, [[0, 0], [52, 0], [52, 100], [0, 100]], [[16, 50], [52, 50]]],
    '4': [52, [[0, 0], [0, 62, 0], [52, 62]], [[40, 34], [40, 100]]],
    '5': [52, [[52, 0], [0, 0, 0], [0, 48, 0], [52, 48], [52, 100], [0, 100]]],
    '6': [52, [[50, 0], [0, 0], [0, 100], [52, 100], [52, 48], [0, 48, 0]]],
    '7': [52, [[0, 0], [52, 0, 0], [18, 100]]],
    '8': [52, box(4, 0, 48, 48, 16), box(0, 48, 52, 100, 20)],
    '9': [52, [[2, 100], [52, 100], [52, 0], [0, 0], [0, 52], [52, 52, 0]]],
    ':': [0, [[0, 30]], [[0, 76]]],
    ';': [6, [[6, 46]], [[6, 96], [0, 116]]],
    '<': [44, [[44, 36], [0, 64, 0], [44, 92]]],
    '=': [44, [[0, 50], [44, 50]], [[0, 78], [44, 78]]],
    '>': [44, [[0, 36], [44, 64, 0], [0, 92]]],
    '?': [46, [[0, 0], [46, 0], [46, 44], [22, 44, 0], [22, 64]], [[22, 100]]],
    '@': [76, box(22, 30, 52, 70, 10), [[52, 30], [52, 70, 0], [76, 70], [76, 0], [0, 0], [0, 100], [70, 100]]],
    'A': [56, [[0, 100], [0, 0], [56, 0], [56, 100]], [[0, 54], [56, 54]]],
    'B': [56, [[0, 100], [0, 0, 0], [46, 0, 16], [46, 48, 16], [0, 48]], [[0, 48], [56, 48, 20], [56, 100, 20], [0, 100, 0]]],
    'C': [56, [[56, 0], [0, 0], [0, 100], [56, 100]]],
    'D': [56, { closed: true, pts: [[0, 0, 0], [56, 0, 28], [56, 100, 28], [0, 100, 0]] }],
    'E': [52, [[52, 0], [0, 0, 0], [0, 100, 0], [52, 100]], [[0, 50], [44, 50]]],
    'F': [50, [[50, 0], [0, 0, 0], [0, 100]], [[0, 50], [42, 50]]],
    'G': [56, [[56, 0], [0, 0], [0, 100], [56, 100], [56, 52, 0], [30, 52]]],
    'H': [56, [[0, 0], [0, 100]], [[56, 0], [56, 100]], [[0, 50], [56, 50]]],
    'I': [0, [[0, 0], [0, 100]]],
    'J': [48, [[48, 0], [48, 100], [0, 100], [0, 70]]],
    'K': [54, [[0, 0], [0, 100]], [[54, 0], [2, 56]], [[20, 40], [56, 100]]],
    'L': [50, [[0, 0], [0, 100, 0], [50, 100]]],
    'M': [72, [[0, 100], [0, 0, 0], [36, 60, 0], [72, 0, 0], [72, 100]]],
    'N': [56, [[0, 100], [0, 0, 0], [56, 100, 0], [56, 0]]],
    'O': [60, box(0, 0, 60, 100, 24)],
    'P': [54, [[0, 100], [0, 0, 0], [54, 0], [54, 52], [0, 52, 0]]],
    'Q': [60, box(0, 0, 60, 100, 24), [[36, 74], [62, 106]]],
    'R': [54, [[0, 100], [0, 0, 0], [54, 0], [54, 52], [0, 52, 0]], [[26, 52], [56, 100]]],
    'S': [54, [[54, 0], [0, 0], [0, 50], [54, 50], [54, 100], [0, 100]]],
    'T': [56, [[0, 0], [56, 0]], [[28, 0], [28, 100]]],
    'U': [56, [[0, 0], [0, 100, 22], [56, 100, 22], [56, 0]]],
    'V': [56, [[0, 0], [28, 100, 0], [56, 0]]],
    'W': [80, [[0, 0], [20, 100, 0], [40, 24, 0], [60, 100, 0], [80, 0]]],
    'X': [56, [[0, 0], [56, 100]], [[56, 0], [0, 100]]],
    'Y': [56, [[0, 0], [28, 50, 0], [56, 0]], [[28, 50], [28, 100]]],
    'Z': [54, [[0, 0], [54, 0, 0], [0, 100, 0], [54, 100]]],
    '[': [22, [[22, -8], [0, -8, 0], [0, 108, 0], [22, 108]]],
    '\\': [44, [[0, -8], [44, 108]]],
    ']': [22, [[0, -8], [22, -8, 0], [22, 108, 0], [0, 108]]],
    '^': [44, [[0, 30], [22, 0, 0], [44, 30]]],
    '_': [56, [[0, D], [56, D]]],
    '`': [14, [[0, 0], [14, 16]]],
    'a': [50, [[2, X], [50, X], [50, B]], box(0, 64, 50, B, 14)],
    'b': [50, [[0, 0], [0, B]], box(0, X, 50, B)],
    'c': [48, [[48, X], [0, X], [0, B], [48, B]]],
    'd': [50, [[50, 0], [50, B]], box(0, X, 50, B)],
    'e': [50, [[0, 66], [50, 66, 0], [50, X], [0, X], [0, B], [48, B]]],
    'f': [40, [[40, 2], [16, 2], [16, B]], [[0, X], [38, X]]],
    'g': [50, box(0, X, 50, B), [[50, X], [50, D], [2, D]]],
    'h': [50, [[0, 0], [0, B]], [[0, X], [50, X], [50, B]]],
    'i': [0, [[0, X + 6], [0, B]], [[0, -2]]],
    'j': [16, [[16, X + 6], [16, D], [-4, D]], [[16, -2]]],
    'k': [46, [[0, 0], [0, B]], [[46, X], [2, 70]], [[16, 60], [48, B]]],
    'l': [12, [[0, 0], [0, B, 12], [12, B]]],
    'm': [68, [[0, X], [0, B]], [[0, X], [34, X, 12], [34, B]], [[34, X], [68, X, 12], [68, B]]],
    'n': [50, [[0, X], [0, B]], [[0, X], [50, X], [50, B]]],
    'o': [50, box(0, X, 50, B)],
    'p': [50, [[0, X], [0, D]], box(0, X, 50, B)],
    'q': [50, [[50, X], [50, D]], box(0, X, 50, B)],
    'r': [36, [[0, B], [0, X], [36, X]]],
    's': [48, [[48, X], [0, X], [0, 66], [48, 66], [48, B], [0, B]]],
    't': [38, [[16, 4], [16, B, 14], [38, B]], [[0, X], [38, X]]],
    'u': [50, [[0, X], [0, B], [50, B]], [[50, X], [50, B]]],
    'v': [50, [[0, X], [25, B, 0], [50, X]]],
    'w': [70, [[0, X], [17, B, 0], [35, 48, 0], [53, B, 0], [70, X]]],
    'x': [48, [[0, X], [48, B]], [[48, X], [0, B]]],
    'y': [50, [[0, X], [0, B], [50, B]], [[50, X], [50, D], [2, D]]],
    'z': [48, [[0, X], [48, X, 0], [0, B, 0], [48, B]]],
    '{': [24, [[24, -8], [10, -8], [10, 40], [0, 50, 0], [10, 60], [10, 108], [24, 108]]],
    '|': [0, [[0, -8], [0, D]]],
    '}': [24, [[0, -8], [14, -8], [14, 40], [24, 50, 0], [14, 60], [14, 108], [0, 108]]],
    '~': [48, [[0, 64], [14, 52, 0], [34, 72, 0], [48, 60]]],
    '\x7f': [30, box(0, -7, 30, 23, 15)],          // degree sign (sent as char 127): a ring, drawn thinner (K)
  };
  const K = { '\x7f': 0.55 };                     // stroke weight factor for single glyphs

  // One stroke in pixels: snapped corners, rounded with quadratic curves, as a list of segments.
  function strokeSegs(st, s, w, ox) {
    const closed = !Array.isArray(st), src = closed ? st.pts : st;
    const snap = (v) => (Math.round(w) % 2 ? Math.floor(v) + 0.5 : Math.round(v));
    const P = src.map(([x, y, r]) => ({ x: snap(ox + x * s), y: snap((y - 100) * s - w / 2), r: (r ?? R) * s }));
    if (P.length === 1) return [[P[0].x, P[0].y, P[0].x, P[0].y]];
    const out = [];                                // polyline points
    const n = P.length;
    for (let i = 0; i < n; i++) {
      const p = P[i];
      const hasPrev = closed || i > 0, hasNext = closed || i < n - 1;
      if (!hasPrev || !hasNext || p.r <= 0.01) { out.push([p.x, p.y]); continue; }
      const a = P[(i - 1 + n) % n], b = P[(i + 1) % n];
      const la = Math.hypot(a.x - p.x, a.y - p.y), lb = Math.hypot(b.x - p.x, b.y - p.y);
      const t = Math.min(p.r, la / 2, lb / 2);
      if (t < 0.3) { out.push([p.x, p.y]); continue; }
      const t1 = [p.x + (a.x - p.x) / la * t, p.y + (a.y - p.y) / la * t];
      const t2 = [p.x + (b.x - p.x) / lb * t, p.y + (b.y - p.y) / lb * t];
      for (let k = 0; k <= 8; k++) {
        const u = k / 8, v = 1 - u;
        out.push([v * v * t1[0] + 2 * u * v * p.x + u * u * t2[0], v * v * t1[1] + 2 * u * v * p.y + u * u * t2[1]]);
      }
    }
    if (closed) out.push(out[0]);
    const segs = [];
    for (let i = 1; i < out.length; i++) segs.push([out[i - 1][0], out[i - 1][1], out[i][0], out[i][1]]);
    return segs;
  }

  // Edges are crisp (each pixel fully on or off): soft shading read as muddy on the LEDs. A cap height listed in
  // SOFT keeps the old 4-bit soft edges (e.g. SOFT = { 34: 1 } brings them back for the huge font).
  const SOFT = {}, CRISP = 0.42;                  // CRISP: share of a pixel a stroke must cover to light it
  // Renders one glyph:{ w, h, dx, dy, adv, a: Uint8Array alpha 0..15 } (dx/dy = bitmap offset from pen/baseline).
  function glyph(ch, cap, w) {
    const g = GL[ch] || GL['?'];
    const s = (cap - w) / 100;
    const segs = [];
    const wk = K[ch] ? Math.max(1, Math.round(w * K[ch])) : w;
    for (const st of g.slice(1)) segs.push(...strokeSegs(st, s, wk, w / 2));
    const gap = Math.max(1, Math.round(w * 0.6 + cap * 0.05));
    const adv = ch === ' ' ? Math.max(2, Math.round(cap * 0.32)) + gap : Math.ceil(g[0] * s + w) + gap;
    if (!segs.length) return { w: 0, h: 0, dx: 0, dy: 0, adv, a: new Uint8Array(0) };
    let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const [ax, ay, bx, by] of segs) { x0 = Math.min(x0, ax, bx); x1 = Math.max(x1, ax, bx); y0 = Math.min(y0, ay, by); y1 = Math.max(y1, ay, by); }
    const r = wk / 2;
    x0 = Math.floor(x0 - r); y0 = Math.floor(y0 - r); x1 = Math.ceil(x1 + r); y1 = Math.ceil(y1 + r);
    const bw = x1 - x0, bh = y1 - y0, a = new Uint8Array(bw * bh), SS = 6, r2 = r * r;
    for (let py = 0; py < bh; py++) for (let px = 0; px < bw; px++) {
      let hit = 0;
      for (let sy = 0; sy < SS; sy++) for (let sx = 0; sx < SS; sx++) {
        const qx = x0 + px + (sx + 0.5) / SS, qy = y0 + py + (sy + 0.5) / SS;
        for (const [ax, ay, bx, by] of segs) {
          const vx = bx - ax, vy = by - ay, l2 = vx * vx + vy * vy;
          let t = l2 ? ((qx - ax) * vx + (qy - ay) * vy) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
          const ex = ax + vx * t - qx, ey = ay + vy * t - qy;
          if (ex * ex + ey * ey <= r2) { hit++; break; }
        }
      }
      const cov = hit / (SS * SS);
      a[py * bw + px] = SOFT[cap] ? Math.round(cov * 15) : cov >= CRISP ? 15 : 0;
    }
    return { w: bw, h: bh, dx: x0, dy: y0, adv, a };
  }

  // The sizes the card has (font numbers in the show list): cap height and stroke weight in pixels.
  const SIZES = { 8: [9, 1], 9: [11, 2], 10: [16, 3], 11: [24, 4], 12: [34, 6] };   // thin small, S, M, B, XL
  const FIRST = 32, LAST = 127;
  const cache = {};
  function font(n) {                              // glyphs are rendered the first time they're used
    if (cache[n]) return cache[n];
    const [cap, w] = SIZES[n];
    return (cache[n] = { cap, w, g: [], gap: Math.max(1, Math.round(w * 0.6 + cap * 0.05)) });
  }
  function glyphOf(f, ch) {
    const c = ch.charCodeAt(0) >= FIRST && ch.charCodeAt(0) <= LAST ? ch.charCodeAt(0) : 63;
    return f.g[c - FIRST] ??= glyph(String.fromCharCode(c), f.cap, f.w);
  }
  function all(n) { const f = font(n); for (let c = FIRST; c <= LAST; c++) glyphOf(f, String.fromCharCode(c)); return f; }
  // Drawing helpers for the page preview: plot(x, y, alpha 0..1) for each inked pixel.
  function width(n, s) { const f = font(n); let x = 0; for (const ch of s) x += glyphOf(f, ch).adv; return s.length ? x - f.gap : 0; }
  function draw(n, s, x, base, plot) {
    const f = font(n);
    for (const ch of s) {
      const g = glyphOf(f, ch);
      for (let y = 0; y < g.h; y++) for (let xx = 0; xx < g.w; xx++) { const v = g.a[y * g.w + xx]; if (v) plot(x + g.dx + xx, base + g.dy + y, v / 15); }
      x += g.adv;
    }
    return x;
  }
  G.PJFont = { GL, SIZES, FIRST, LAST, glyph, font, all, width, draw };
})(typeof window !== 'undefined' ? window : globalThis);
