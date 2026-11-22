// ---------- Effects library (drawn by the card: no storage, any length) ----------
// Used full screen (the "fx" item) or behind a message / clock / countdown / weather. Positions are in
// 1/16 pixel so slow things move smoothly. Fire borrows an 8 KB heat map only while it plays.

static Flake flakes[48];
struct Spark { int16_t x, y, vx, vy; uint8_t life, max; uint16_t hue; };
static Spark sparks[96];
struct Rocket { int16_t x, y, vy; uint16_t hue; bool on; };
static Rocket rockets[3];
static uint8_t *heat = nullptr;                  // fire heat map W*H, values 0..36
static uint8_t mHead[22], mLen[22], mSpd[22];    // matrix rain: head row in 1/4 rows, trail length, speed
static char mGlyph[22][8];
static int16_t sleighX; static uint8_t sleighWait;  // sleigh position in 1/16 px, frames to wait off screen
static const char MGLYPHS[] = "0123456789ABCDEFXZ$#@%&*+=<>";

static void starReset(Flake &f) { f.x = (int16_t)rnd(1024) - 512; f.y = (int16_t)rnd(512) - 256; f.bri = 200 + rnd(56); f.spd = 1 + rnd(3); }
static void effectInit(const ShowItem &it) {
  rs ^= millis() | 1;
  for (auto &f : flakes) { f.x = random(W * 16); f.y = random(H * 16); f.spd = 4 + random(10); f.bri = 90 + random(166); }
  if (it.effect == FX_STARS) for (auto &f : flakes) { starReset(f); f.bri = 30 + rnd(225); }
  if (it.effect == FX_HEARTS) for (auto &f : flakes) f.y = random(H * 24);
  for (auto &s : sparks) s.life = 0;
  for (auto &r : rockets) r.on = false;
  for (int c = 0; c < 22; c++) { mHead[c] = rnd(40); mLen[c] = 3 + rnd(5); mSpd[c] = 1 + rnd(3); for (int r = 0; r < 8; r++) mGlyph[c][r] = MGLYPHS[rnd(sizeof(MGLYPHS) - 1)]; }
  if (it.effect == FX_FIRE && !heat) heat = (uint8_t *)calloc(W, H);
  sleighX = it.effect == FX_SLEIGH && it.type == 'f' ? -84 * 16 : it.effect == FX_TURKEY || it.effect == FX_BUNNY ? -32 * 16 : -46 * 16;
  sleighWait = 0;
}
static void effectEnd() { free(heat); heat = nullptr; }
struct FxGuard { ~FxGuard() { effectEnd(); } };    // frees the fire heat map however the item ends

static void fxFireworks(uint32_t frame) {
  static const int8_t COS[32] = {64,63,59,53,45,36,24,12,0,-12,-24,-36,-45,-53,-59,-63,-64,-63,-59,-53,-45,-36,-24,-12,0,12,24,36,45,53,59,63};
  bool any = false;
  for (auto &r : rockets) any |= r.on;
  for (auto &r : rockets) {
    if (!r.on && (frame == 0 || (!any && rnd(12) == 0) || rnd(70) == 0)) {          // launch
      r = {(int16_t)((14 + rnd(W - 28)) * 16), (int16_t)(H * 16), (int16_t)-(34 + rnd(13)), (uint16_t)rnd(1536), true};
      any = true; break;
    }
  }
  for (auto &r : rockets) {
    if (!r.on) continue;
    r.y += r.vy; r.vy += 1;
    int x = r.x / 16, y = r.y / 16;
    pxAdd(x, y, 0xFFF0C0, 255); pxAdd(x, y + 1, 0xFF9030, 150); pxAdd(x, y + 2, 0xFF6010, 70);
    if (r.vy >= -3 || r.y < 5 * 16) {                                            // burst
      r.on = false;
      int n = 0, speed = 14 + rnd(10);
      for (auto &s : sparks) {
        if (s.life || n >= 28) continue;
        int a = n * 32 / 28, sp = speed + rnd(5);
        s = {r.x, r.y, (int16_t)(COS[a] * sp / 64), (int16_t)(COS[(a + 24) & 31] * sp / 64), (uint8_t)(28 + rnd(18)), 0, (uint16_t)((r.hue + rnd(120)) % 1536)};
        s.max = s.life; n++;
      }
    }
  }
  for (auto &s : sparks) {
    if (!s.life) continue;
    s.x += s.vx; s.y += s.vy;
    if (frame & 1) s.vy += 1;
    if ((frame & 3) == 0) { s.vx = s.vx * 7 / 8; s.vy = s.vy * 7 / 8; }
    s.life--;
    uint8_t v = 255 * s.life / s.max;
    if (s.life < 8 && rnd(3) == 0) continue;                                       // crackle at the end
    uint32_t c = s.life > s.max - 4 ? 0xFFFFFF : hsv(s.hue);
    pxAdd(s.x / 16, s.y / 16, c, v);
    pxAdd((s.x - s.vx * 2) / 16, (s.y - s.vy * 2) / 16, c, v / 3);
  }
}

static void fxFire(bool behind) {
  if (!heat) return;
  for (int x = 0; x < W; x++) heat[(H - 1) * W + x] = rnd(3) ? 36 : 12 + rnd(20);
  for (int y = 0; y < H - 1; y++)
    for (int x = 0; x < W; x++) {
      uint32_t r = rnd();
      int sx = x + (int)(r % 3) - 1; if (sx < 0) sx = 0; if (sx >= W) sx = W - 1;
      int d = (r >> 4) & 3, decay = behind ? (d < 2 ? d : 2) : (d ? 1 : 0);
      int v = heat[(y + 1) * W + sx] - decay;
      heat[y * W + x] = v < 0 ? 0 : v;
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int v = heat[y * W + x];
      if (v < 2) continue;
      int t = v * 255 / 36;                                                      // black-red-orange-yellow-white
      canvas[y][x][0] = std::min(255, t * 3);
      canvas[y][x][1] = std::max(0, std::min(255, (t - 85) * 3));
      canvas[y][x][2] = std::max(0, std::min(255, (t - 190) * 4));
    }
}

static void fxMatrix(uint32_t frame) {
  const GFXfont *keep = cg.font(); uint8_t ks = cg.size();
  cg.setFont(nullptr); cg.setTextSize(1);
  for (int c = 0; c < 21; c++) {
    if (frame % mSpd[c] == 0) mHead[c]++;
    int head = mHead[c] / 4;
    if (head - mLen[c] > 8) { mHead[c] = 0; mLen[c] = 3 + rnd(6); mSpd[c] = 1 + rnd(3); }
    if (rnd(6) == 0) mGlyph[c][rnd(8)] = MGLYPHS[rnd(sizeof(MGLYPHS) - 1)];
    for (int r = 0; r < 8; r++) {
      int age = head - r;
      if (age < 0 || age > mLen[c]) continue;
      uint8_t v = 255 - age * 200 / (mLen[c] + 1);
      if (age == 0) cg.rgb(0xC8FFC8); else cg.rgb((uint32_t)(v / 6) << 16 | (uint32_t)v << 8 | (v / 5));
      cg.drawChar(c * 6 + 1, r * 8, mGlyph[c][r], 0, 0, 1);
    }
  }
  cg.setFont(keep); cg.setTextSize(ks);
}


// ---------- Sprite effects (1.0): sleigh, balloons, fish, rain ----------
// Sprites are rows of letters, one per pixel; the page's preview (web/index.html SPR) has the same rows.
// X main color, D darker main, T main a bit dimmer, L main toward white, H/W/E white, R red, S skin, G gold,
// B brown, b dark brown, N nose (red on the lead reindeer, brown on the others), O orange, Y yellow, P pink,
// K black, g green.
static const char *const SLEIGH[] = {
  "...WRR.........", "....RRR........", "....WWWW.......", "....SSSS.......", "R...WWWW......R", "RR..RWWR.....RR",
  "RRRRRRRRRRRRRR.", "RGGGGGGGGGGGGR.", ".RRRRRRRRRRRR..", "..G.......G...G", "GGGGGGGGGGGGGG."};
static const char *const DEER_A[] = {
  ".......b.b.", "........bb.", ".......BBBN", ".BBBBBBBB..", "BBBBBBBB...", ".b.b..b.b..", "b...b.b...b"};
static const char *const DEER_B[] = {
  ".......b.b.", "........bb.", ".......BBBN", ".BBBBBBBB..", "BBBBBBBB...", "..bb..bb...", "..bb..bb..."};
static const char *const BALLOON[] = {"..XXX..", ".XXXXX.", "XHXXXXX", "XXXXXXX", "XXXXXXD", ".XXXXD.", "..XXD..", "...X..."};
static const char *const FISH[] = {"...XXX..", "T.XXLXX.", "TTXXLXEX", "T.XXLXX.", "...XXX.."};
static const uint32_t PARTY[7] = {0xFF2030, 0xFFC020, 0x3070FF, 0x30D060, 0xFF50B0, 0xA050FF, 0xFF8020};
static uint32_t mixC(uint32_t a, uint32_t b, int num, int den) {     // a toward b by num/den
  uint32_t o = 0;
  for (int s = 0; s <= 16; s += 8) { int ca = (a >> s) & 255, cb = (b >> s) & 255; o |= (uint32_t)(ca + (cb - ca) * num / den) << s; }
  return o;
}
static void sprite(const char *const *rows, int n, int x, int y, uint32_t main, bool flip, bool nose, int sc) {   // sc: 1 or 2 px per letter
  for (int r = 0; r < n; r++) {
    int w = strlen(rows[r]);
    for (int i = 0; i < w; i++) {
      uint32_t c;
      switch (rows[r][flip ? w - 1 - i : i]) {
        case 'X': c = main; break;
        case 'D': c = mixC(main, 0, 2, 5); break;
        case 'T': c = mixC(main, 0, 1, 4); break;
        case 'L': c = mixC(main, 0xFFFFFF, 1, 2); break;
        case 'H': case 'W': case 'E': c = 0xFFFFFF; break;
        case 'R': c = 0xE01020; break;
        case 'S': c = 0xFFB48C; break;
        case 'G': c = 0xFFC830; break;
        case 'B': c = 0xA0602C; break;
        case 'b': c = 0x6A3A18; break;
        case 'N': c = nose ? 0xFF1010 : 0xA0602C; break;
        case 'O': c = 0xFF7A18; break;
        case 'Y': c = 0xFFE040; break;
        case 'P': c = 0xFF8CB4; break;
        case 'K': c = 0; break;
        case 'g': c = 0x30C040; break;
        default: continue;
      }
      for (int a = 0; a < sc; a++) for (int b = 0; b < sc; b++) px(x + i * sc + a, y + r * sc + b, c);
    }
  }
}

// Santa's sleigh and two reindeer crossing with a glitter trail. Full screen it is a night scene: twinkling stars,
// a full moon, snowy rooftops with lit windows, falling snow, and the sleigh at double size crossing the moon.
static void disc(int cx, int cy, int r2, uint32_t c) {           // filled circle, r2 = radius squared (r <= 8)
  for (int dy = -8; dy <= 8; dy++) for (int dx = -8; dx <= 8; dx++) if (dx * dx + dy * dy <= r2) px(cx + dx, cy + dy, c);
}
static const int8_t HOUSES[][4] = {{-3, 26, 11, 0}, {25, 20, 16, 1}, {47, 28, 10, 0}, {77, 22, 14, 1}, {101, 30, 12, 0}};   // x, width, wall height, chimney
static void fxTown(uint32_t frame) {
  disc(110, 11, 56, 0xFFF0C0);                                   // moon, two soft craters
  px(107, 9, 0xE0D0A0); px(108, 9, 0xE0D0A0); px(112, 14, 0xE0D0A0); px(113, 13, 0xE0D0A0);
  for (int h = 0; h < 5; h++) {
    int x0 = HOUSES[h][0], w = HOUSES[h][1], wall = H - HOUSES[h][2], half = w / 2;
    if (HOUSES[h][3]) for (int y = wall - half + 2; y < wall; y++) for (int x = x0 + w - 7; x < x0 + w - 4; x++) px(x, y, 0x5A2A2A);   // chimney
    for (int x = x0; x < x0 + w; x++) {
      int d = x - x0 < half ? x - x0 : x0 + w - 1 - x, top = wall - d - 1;   // 45-degree roof
      px(x, top, 0xE8F0FF);                                       // snow on the roof
      for (int y = top + 1; y < wall; y++) px(x, y, 0x5A2030);
      for (int y = wall; y < H; y++) px(x, y, 0x3C2A5A);
    }
    for (int k = 0; k < 2; k++) {                                // two windows, now and then one goes dark
      int wx = k ? x0 + w - 8 : x0 + 5, wy = wall + 3;
      if ((frame / 150 + h * 3 + k) % 7 == 0) continue;
      for (int y = wy; y < wy + 3; y++) for (int x = wx; x < wx + 3; x++) px(x, y, 0xFFB030);
    }
  }
}
static void fxSleigh(uint32_t frame, bool behind) {
  int sc = behind ? 1 : 2;
  for (int i = 0; !behind && i < 24; i++) {                      // stars in the top half of the sky
    Flake &f = flakes[i];
    pxAdd(f.x / 16, f.y / 32, 0xDCE6FF, (sin8(frame * (f.spd / 4 + 1) + i * 41) * f.bri) >> 9);
  }
  if (!behind) fxTown(frame);
  if (sleighWait) sleighWait--;
  else if ((sleighX += behind ? 9 : 16) > (W + 4) * 16) { sleighX = (behind ? -46 : -84) * 16; sleighWait = 20 + rnd(60); }
  int x = sleighX / 16, y = (behind ? 2 : 9) + ((int)sin8(frame * 2) - 128) / (behind ? 40 : 26);
  for (int k = 0; !sleighWait && k < 3; k++)                     // glitter from the back of the sleigh
    for (auto &s : sparks)
      if (!s.life) {
        s = {(int16_t)((x + sc) * 16 + rnd(32 * sc)), (int16_t)((y + 6 * sc + rnd(5 * sc)) * 16), (int16_t)-(2 + rnd(8)), (int16_t)((int)rnd(5) - 2), (uint8_t)(14 + rnd(18)), 0, 0};
        s.max = s.life; break;
      }
  for (auto &s : sparks) {
    if (!s.life) continue;
    s.x += s.vx; s.y += s.vy; s.life--;
    pxAdd(s.x / 16, s.y / 16, s.life > s.max - 3 ? 0xFFFFFF : 0xFFD060, 255 * s.life / s.max);
  }
  if (!sleighWait) {
    for (int i = x + 8 * sc; i < x + 37 * sc; i++)               // reins
      for (int t = 0; t < sc; t++) px(i, y + (4 + (i > x + 22 * sc)) * sc + t, 0xB08020);
    bool step = (frame / 4) & 1;
    sprite(step ? DEER_A : DEER_B, 7, x + 17 * sc, y + step * sc, 0, false, false, sc);
    sprite(step ? DEER_B : DEER_A, 7, x + 30 * sc, y + ((int)!step - 1) * sc, 0, false, true, sc);
    sprite(SLEIGH, 11, x, y, 0, false, false, sc);
  }
  for (int i = 24; !behind && i < 44; i++) {                     // gentle snow in front
    Flake &f = flakes[i];
    f.y += f.spd / 3 + 2;
    if (f.y >= H * 16) { f.y = 0; f.x = rnd(W) * 16; }
    pxAdd(f.x / 16 + (((frame + i * 7) / 24) & 1), f.y / 16, 0xFFFFFF, f.bri * 3 / 4);
  }
}

static void fxBalloons(uint32_t frame, bool behind) {
  for (int i = 0; i < (behind ? 6 : 10); i++) {
    Flake &f = flakes[i];
    f.y -= f.spd / 3 + 2;
    if (f.y < -15 * 16) { f.y = H * 16 + rnd(320); f.x = rnd(W - 8) * 16; f.bri = rnd(7); }
    int x = f.x / 16 + ((int)sin8(frame * 2 + i * 50) - 128) / 48, y = f.y / 16;
    for (int r = 0; r < 6; r++) px(x + 3 + (((frame / 6 + r + i) & 2) ? 1 : 0), y + 8 + r, 0x8C8C8C);   // string
    sprite(BALLOON, 8, x, y, PARTY[f.bri % 7], false, false, 1);
  }
}

// Fish swimming both ways with bubbles, on black (a background for text).
static void fxFish(uint32_t frame, bool behind) {
  for (int i = 16; i < (behind ? 26 : 36); i++) {                // bubbles
    Flake &f = flakes[i];
    f.y -= f.spd / 3 + 3;
    if (f.y < 0) { f.y = H * 16 + rnd(400); f.x = rnd(W) * 16; }
    int x = f.x / 16 + (((frame / 8 + i) & 1) ? 1 : 0), y = f.y / 16;
    if (f.spd > 10) { pxAdd(x, y, 0x80B0FF, 150); pxAdd(x + 1, y, 0x80B0FF, 150); pxAdd(x, y + 1, 0x80B0FF, 90); pxAdd(x + 1, y + 1, 0x80B0FF, 90); }
    else pxAdd(x, y, 0xA0C8FF, 170);
  }
  for (int i = 0; i < (behind ? 4 : 7); i++) {                   // fish (bri: bit 7 = swims left, low bits = color)
    Flake &f = flakes[i];
    bool left = f.bri & 128;
    f.x += left ? -(f.spd / 3 + 2) : f.spd / 3 + 2;
    if (f.x < -12 * 16 || f.x > (W + 4) * 16) {
      f.bri = (rnd(2) ? 128 : 0) | rnd(7); f.spd = 4 + rnd(10);
      f.x = (f.bri & 128) ? (W + 2) * 16 : -10 * 16; f.y = (3 + rnd(H - 10)) * 16;
    }
    int y = f.y / 16 + ((int)sin8(frame * 3 + i * 70) - 128) / 64;
    sprite(FISH, 5, f.x / 16, y, PARTY[(f.bri & 7) % 7], left, false, 1);
  }
}

static void fxRain(uint32_t frame, bool behind) {
  for (int i = 0; i < (behind ? 28 : 48); i++) {
    Flake &f = flakes[i];
    f.y += f.spd * 2 + 14; f.x -= 3;
    if (f.y >= H * 16) {
      if (!behind) { pxAdd(f.x / 16 - 1, H - 1, 0x8CAAFF, 140); pxAdd(f.x / 16 + 1, H - 1, 0x8CAAFF, 140); }   // splash
      f.y = -(int)rnd(64); f.x = rnd(W + 16) * 16;
    }
    int x = f.x / 16, y = f.y / 16;
    uint8_t v = behind ? f.bri * 3 / 4 : f.bri;
    pxAdd(x, y, 0x8CAAFF, v); pxAdd(x, y - 1, 0x8CAAFF, v / 2); pxAdd(x, y - 2, 0x8CAAFF, v / 4);
  }
}

// A string of Christmas lights around the edge: a dim green wire on the outer pixels, 44 bulbs (2x2) in red, gold,
// green, blue. Full screen it changes pattern every ~6 s (twinkle, chase, alternate blink) over gentle snow; behind
// a message it only twinkles. Bulb i goes clockwise from the top left (page preview: the same order and colors).
static const uint32_t BULB[4] = {0xFF1020, 0xFFB020, 0x20E040, 0x2050FF};
static void bulbPos(int i, int &x, int &y) {
  if (i < 16) { x = 3 + i * 8; y = 1; }                          // top, left to right
  else if (i < 22) { x = W - 3; y = 9 + (i - 16) * 8; }          // right, down
  else if (i < 38) { x = 3 + (37 - i) * 8; y = H - 3; }          // bottom, right to left
  else { x = 1; y = 9 + (43 - i) * 8; }                          // left, up
}
static void fxLights(uint32_t frame, bool behind) {
  if (!behind)
    for (int i = 0; i < 24; i++) {                               // snow inside the frame
      Flake &f = flakes[i];
      f.y += f.spd / 2 + 1;
      if (f.y >= (H - 4) * 16) { f.y = 4 * 16; f.x = (4 + rnd(W - 8)) * 16; }
      if (f.y < 4 * 16) f.y = 4 * 16;
      int x = f.x / 16; if (x < 4 || x >= W - 4) { f.x = (4 + rnd(W - 8)) * 16; continue; }
      pxAdd(x, f.y / 16, 0xFFFFFF, f.bri * 2 / 3);
    }
  for (int x = 0; x < W; x++) { px(x, 0, 0x0C3812); px(x, H - 1, 0x0C3812); }
  for (int y = 0; y < H; y++) { px(0, y, 0x0C3812); px(W - 1, y, 0x0C3812); }
  int mode = behind ? 0 : (frame / 200) % 3;
  for (int i = 0; i < 44; i++) {
    int v = mode == 0 ? 70 + (sin8(frame * 3 + i * 53) * 185 >> 8)
          : mode == 1 ? (((i + frame / 5) % 4) == 0 ? 255 : 45)
          : ((int)(i & 1) == (int)((frame / 20) & 1) ? 255 : 45);
    uint32_t c = mixC(BULB[i % 4], 0, 255 - v, 255);
    int x, y; bulbPos(i, x, y);
    px(x, y, c); px(x + 1, y, c); px(x, y + 1, c); px(x + 1, y + 1, c);
  }
}

// ---------- Holiday effects (1.0.0-beta19): turkey, bunny, bats, shamrocks, flag ----------
// Same rows and colors in the page preview (web/app/2-effects.js SPR).
static const char *const TURKEY[] = {
  "....RRRRRRR....", "..RRROOOOORRR..", ".RROOOYYYOOORR.", ".ROOYYYBYYYOOR.", "RROYYYBBBYYYORR", "ROOYYBWBWBYYOOR",
  "ROYYYBBOBBYYYOR", "ROYYBBBRBBBYYOR", "ROYBBBBRBBBBYOR", ".OYBBbBBBbBBYO.", "..YBBBbBbBBBY..", "...BBBBBBBBB...",
  "....BBBBBBB....", ".....O...O.....", "....OO....O....", ".....O...O.....", ".....O...OO...."};   // feet: rows 13-14 or 15-16
static const char *const BUNNY[] = {
  ".....WW.WW...", ".....WP.WP...", ".....WP.WP...", ".....WW.WW...", "......WWWW...", ".....WWWKWW..", "....WWWWWWP..",
  ".WWWWWWWWW...", "WWWWWWWWWW...", "WWWWWWWWWW...", ".WWWWWWWW....", "..WW...WW...."};
static const char *const EGG[] = {".XX.", "XXXX", "LLLL", "XXXX", ".XX."};
static const char *const BAT_A[] = {"X.........X", "XX.......XX", "XXX.X.X.XXX", ".XXXYXYXXX.", "....XXX...."};
static const char *const BAT_B[] = {"....X.X....", "...XYXYX...", ".XXXXXXXXX.", "XXX.XXX.XXX", "X.........X"};
static const char *const PUMPKIN[] = {"....g....", "..OOgOO..", ".OOOOOOO.", "OOXOOOXOO", "OOOOXOOOO", "OXOXOXOXO", "OOXXXXXOO", ".OOOOOOO."};
static const char *const CLOVER[] = {"..XXX..", "..XXX..", "XX.X.XX", "XXXXXXX", "XX.X.XX", "...D...", "..D...."};
static const char *const CLOVER_S[] = {".X.", "XXX", ".D."};
static const uint32_t LEAF[4] = {0xE04010, 0xFF9020, 0xF0C030, 0x9A4A18};
static const uint32_t PASTEL[5] = {0xFF90C0, 0xB090FF, 0x80E8A0, 0xFFE070, 0x80C0FF};
static const uint32_t GREENS[3] = {0x20C040, 0x40E060, 0x109030};

// Autumn leaves tumbling down; full screen a turkey struts across the leaf-covered ground.
static void fxTurkey(uint32_t frame, bool behind) {
  for (int i = 0; i < (behind ? 14 : 30); i++) {
    Flake &f = flakes[i];
    f.y += f.spd / 4 + 1;
    if (f.y >= H * 16) { f.y = -32; f.x = rnd(W) * 16; }
    int x = f.x / 16 + ((int)sin8(frame * 2 + i * 37) - 128) / 36, y = f.y / 16;
    uint32_t c = LEAF[i & 3];
    px(x, y, c);
    if ((frame / 8 + i) & 1) px(x + 1, y, c); else px(x, y + 1, c);
    if (f.spd > 10) px(x + 1, y + 1, c);
  }
  if (behind) return;
  for (int x = 0; x < W; x++) { px(x, H - 1, 0x4A2810); px(x, H - 2, mixC(LEAF[(x * 7 + x / 3) & 3], 0, 1, 2)); }   // leafy ground
  if (sleighWait) { sleighWait--; return; }
  if ((sleighX += 9) > (W + 2) * 16) { sleighX = -32 * 16; sleighWait = 40 + rnd(60); return; }
  bool step = (frame / 6) & 1;
  int x = sleighX / 16, y = H - 2 - 30 - step;                 // double size
  sprite(TURKEY, 13, x, y, 0, false, false, 2);
  sprite(TURKEY + (step ? 15 : 13), 2, x, y + 26, 0, false, false, 2);
}

// Easter: a bunny hops across the grass past painted eggs; behind a message, eggs float up.
static void fxBunny(uint32_t frame, bool behind) {
  if (behind) {
    for (int i = 0; i < 7; i++) {
      Flake &f = flakes[i];
      f.y -= f.spd / 4 + 1;
      if (f.y < -6 * 16) { f.y = H * 16 + rnd(320); f.x = rnd(W - 4) * 16; }
      sprite(EGG, 5, f.x / 16 + ((int)sin8(frame * 2 + i * 50) - 128) / 60, f.y / 16, PASTEL[i % 5], false, false, 1);
    }
    return;
  }
  for (int i = 0; i < 16; i++) { Flake &f = flakes[i]; pxAdd(f.x / 16, f.y / 32, 0xFFFFFF, (sin8(frame * 2 + i * 41) * f.bri) >> 10); }   // faint stars
  for (int x = 0; x < W; x++) {                                  // grass with blades and a few flowers
    for (int y = H - 3; y < H; y++) px(x, y, 0x1E7A28);
    int k = (x * 37) % 11;
    if (k < 4) px(x, H - 4, 0x30B040);
    if (k == 0 && (x & 1)) px(x, H - 5, 0x30B040);
    if (x % 19 == 7) px(x, H - 5, x % 38 == 7 ? 0xFF8CB4 : 0xFFE040);
  }
  static const uint8_t EGG_X[5] = {9, 33, 61, 87, 113};
  for (int e = 0; e < 5; e++) sprite(EGG, 5, EGG_X[e], H - 7, PASTEL[e], false, false, 1);
  if (sleighWait) { sleighWait--; return; }
  int ph = frame % 30, hop = 0;
  if (ph < 20) { hop = ((int)sin8(ph * 128 / 20) - 128) * 14 / 127; sleighX += 28; }   // in the air: moving
  if (sleighX > (W + 2) * 16) { sleighX = -28 * 16; sleighWait = 30 + rnd(50); return; }
  sprite(BUNNY, 12, sleighX / 16, H - 3 - 24 - hop, 0, false, false, 2);   // double size
}

// Halloween: bats flapping across; full screen with an orange moon and two glowing jack-o-lanterns.
static void fxBats(uint32_t frame, bool behind) {
  if (!behind) {
    for (int i = 0; i < 16; i++) { Flake &f = flakes[i]; pxAdd(f.x / 16, f.y / 32, 0xFFFFFF, (sin8(frame * 2 + i * 41) * f.bri) >> 10); }
    disc(108, 13, 72, 0xFF9A28);
    px(104, 10, 0xE07A18); px(105, 10, 0xE07A18); px(111, 17, 0xE07A18); px(110, 8, 0xE07A18);
    uint32_t glow = mixC(0xFFE040, 0xFF8000, sin8(frame * 5) / 2 + rnd(40), 255);
    sprite(PUMPKIN, 8, 10, H - 8, glow, false, false, 1);
    sprite(PUMPKIN, 8, 96, H - 8, mixC(glow, 0xFF8000, 1, 3), false, false, 1);
  }
  for (int i = 0; i < (behind ? 4 : 6); i++) {
    Flake &f = flakes[40 + i];                                   // bri bit 7: flies left
    bool left = f.bri & 128;
    f.x += left ? -(f.spd / 3 + 4) : f.spd / 3 + 4;
    if (f.x < -14 * 16 || f.x > (W + 3) * 16) {
      f.bri = rnd(2) ? 128 : 0; f.spd = 4 + rnd(10);
      f.x = (f.bri & 128) ? (W + 2) * 16 : -12 * 16; f.y = (2 + rnd(behind ? 20 : 34)) * 16;
    }
    int y = f.y / 16 + ((int)sin8(frame * 4 + i * 60) - 128) / 32;
    sprite(((frame / 5 + i) & 1) ? BAT_A : BAT_B, 5, f.x / 16, y, 0x9050D0, false, false, 1);
  }
}

// St Patrick's: shamrocks drifting down, big and small, with gold twinkles (full screen).
static void fxShamrocks(uint32_t frame, bool behind) {
  for (int i = 0; i < (behind ? 10 : 18); i++) {
    Flake &f = flakes[i];
    f.y += f.spd / 4 + 1;
    if (f.y >= H * 16) { f.y = -8 * 16; f.x = rnd(W) * 16; }
    int x = f.x / 16 + ((int)sin8(frame * 3 + i * 40) - 128) / 40, y = f.y / 16;
    if (f.spd > 8) sprite(CLOVER, 7, x, y, GREENS[i % 3], false, false, 1);
    else sprite(CLOVER_S, 3, x, y, GREENS[i % 3], false, false, 1);
  }
  for (int i = 30; !behind && i < 42; i++) {
    Flake &f = flakes[i];
    int ph = (frame * 2 + i * 23) % 80;
    if (ph == 0) { f.x = rnd(W) * 16; f.y = rnd(H) * 16; }
    int v = ph < 20 ? ph * 12 : ph < 40 ? (40 - ph) * 12 : 0, x = f.x / 16, y = f.y / 16;
    pxAdd(x, y, 0xFFC830, v);
    if (v > 150) { pxAdd(x + 1, y, 0xFFC830, v / 3); pxAdd(x - 1, y, 0xFFC830, v / 3); pxAdd(x, y + 1, 0xFFC830, v / 3); pxAdd(x, y - 1, 0xFFC830, v / 3); }
  }
}

// The US flag waving on a pole (behind a message: dimmed to a quarter so the words read).
static void fxFlag(uint32_t frame, bool behind) {
  const int top = 6, x0 = 3;
  if (!behind) {
    for (int y = top - 2; y < H; y++) { px(0, y, 0x909090); px(1, y, 0x6A6A6A); }
    for (int y = top - 4; y < top - 2; y++) { px(0, y, 0xFFC830); px(1, y, 0xFFC830); }
  }
  for (int x = x0; x < W; x++) {
    int a = x * 4 - (int)frame * 5, fx = x - x0;
    int dy = ((int)sin8(a) - 128) * 3 * fx / (127 * (W - x0));
    int k = 200 + ((int)sin8(a + 64) - 128) * 55 / 127;          // crests lighter, troughs darker
    if (behind) k /= 4;
    for (int y = 0; y < 52; y++) {
      uint32_t c = (y / 4) & 1 ? 0xE8E8E8 : 0xD01020;            // 13 stripes of 4 px
      if (fx < 52 && y < 28) {                                   // blue field: 9 rows of 6 and 5 stars
        c = 0x1838A0;
        int r = y - 2, odd = (r / 3) & 1, sx = fx - (odd ? 8 : 4);
        if (r >= 0 && r <= 24 && r % 3 == 0 && sx >= 0 && sx % 8 == 0 && sx <= 40 - odd * 8) c = 0xFFFFFF;
      }
      px(x, top + y + dy, mixC(c, 0, 255 - k, 255));
    }
  }
}

static void paintBackground(const ShowItem &it, uint32_t frame) {
  uint8_t br = it.bg >> 16, bgg = it.bg >> 8, bb = it.bg;
  bool behind = it.type != 'f';                  // something is drawn on top: keep busy effects calmer
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { canvas[y][x][0] = br; canvas[y][x][1] = bgg; canvas[y][x][2] = bb; }
  switch (it.effect) {
    case FX_SNOW:
      for (auto &f : flakes) {
        f.y += f.spd; f.x = (f.x + (((frame + f.spd) % 16 < 8) ? 1 : -1) + W * 16) % (W * 16);
        if (f.y >= H * 16) { f.y = 0; f.x = random(W * 16); }
        int x = (f.x / 16 + W) % W, y = f.y / 16;
        canvas[y][x][0] = canvas[y][x][1] = canvas[y][x][2] = f.bri;
        if (!behind && f.spd > 10) { pxAdd(x + 1, y, 0xFFFFFF, f.bri / 2); pxAdd(x, y + 1, 0xFFFFFF, f.bri / 2); }   // big near flakes
      }
      break;
    case FX_SPARKLE:
      for (int i = 0; i < 48; i++) {
        Flake &f = flakes[i];
        int phase = (frame * f.spd / 4 + i * 37) % 64;                 // twinkle up and down
        uint8_t v = phase < 32 ? phase * 8 : (63 - phase) * 8;
        if (phase == 63) { f.x = random(W * 16); f.y = random(H * 16); }
        pxAdd(f.x / 16, f.y / 16, hsv((i * 200) % 1536), v);
      }
      break;
    case FX_HEARTS: {                            // hearts floating up, big and small
      static const uint8_t BIG[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08}, SMALL[3] = {0x5, 0x7, 0x2};
      for (int i = 0; i < (behind ? 10 : 18); i++) {
        Flake &f = flakes[i];
        f.y -= f.spd / 2 + 1;
        if (f.y < -8 * 16) { f.y = H * 16 + random(64); f.x = random(W * 16); }
        int x = f.x / 16 + (sin8(frame * 3 + i * 40) - 128) / 40, y = f.y / 16;
        uint32_t c = (255UL << 16) | ((uint32_t)(30 + i * 37 % 90) << 8) | (90 + i * 53 % 120);
        bool big = f.spd > 8;
        for (int r = 0; r < (big ? 6 : 3); r++)
          for (int b = 0; b < (big ? 7 : 3); b++)
            if (((big ? BIG[r] : SMALL[r]) >> (big ? 6 - b : 2 - b)) & 1) px(x + b, y + r, c);
      }
      break;
    }
    case FX_CONFETTI:
      for (int i = 0; i < 48; i++) {
        Flake &f = flakes[i];
        f.y += f.spd / 2 + 2;
        if (f.y >= H * 16) { f.y = -16; f.x = random(W * 16); }
        int x = f.x / 16 + (sin8(frame * 4 + i * 23) - 128) / 50, y = f.y / 16;
        uint32_t c = hsv(i * 97 % 1536);
        int o = (frame / 4 + i) % 3;
        px(x, y, c);
        if (o == 0) px(x + 1, y, c); else if (o == 1) px(x, y + 1, c);
      }
      break;
    case FX_FIREWORKS: fxFireworks(frame); break;
    case FX_STARS:                               // flying through space
      for (auto &f : flakes) {
        int z0 = f.bri;
        f.bri = z0 > f.spd * 2 ? z0 - f.spd * 2 : 0;
        if (f.bri < 8) { starReset(f); continue; }
        int x = W / 2 + f.x * 16 / f.bri, y = H / 2 + f.y * 16 / f.bri;
        int x0 = W / 2 + f.x * 16 / z0, y0 = H / 2 + f.y * 16 / z0;
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) { starReset(f); continue; }
        uint8_t v = 255 - f.bri;
        pxAdd(x, y, 0xDCE6FF, v);
        if (x0 != x || y0 != y) pxAdd(x0, y0, 0xDCE6FF, v / 3);
      }
      break;
    case FX_FIRE: fxFire(behind); break;
    case FX_MATRIX: fxMatrix(frame); break;
    case FX_SLEIGH: fxSleigh(frame, behind); break;
    case FX_BALLOONS: fxBalloons(frame, behind); break;
    case FX_FISH: fxFish(frame, behind); break;
    case FX_RAIN: fxRain(frame, behind); break;
    case FX_LIGHTS: fxLights(frame, behind); break;
    case FX_TURKEY: fxTurkey(frame, behind); break;
    case FX_BUNNY: fxBunny(frame, behind); break;
    case FX_BATS: fxBats(frame, behind); break;
    case FX_SHAMROCKS: fxShamrocks(frame, behind); break;
    case FX_FLAG: fxFlag(frame, behind); break;
  }
}

// ---------- Effect on its own ----------
static void playFx(const ShowItem &it) {
  effectInit(it); FxGuard fxg;
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame++);
    pushCanvas();
    int32_t wait = 30 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
