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
  sleighX = it.effect == FX_SLEIGH && it.type == 'f' ? -94 * 16 : it.effect == FX_TURKEY ? -36 * 16 : it.effect == FX_BUNNY ? -32 * 16 : -50 * 16;
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


// ---------- Sprite effects: sleigh, balloons, fish, rain, holidays ----------
// The pictures are shaded sprites with hard edges drawn by tools/make-fxsprites.mjs (fx_sprites.h, 24-bit color);
// the page preview (web/fxsprites.js + web/app/2-effects.js) uses the same pixels.
static const uint32_t PARTY[7] = {0xFF2030, 0xFFC020, 0x3070FF, 0x30D060, 0xFF50B0, 0xA050FF, 0xFF8020};
static uint32_t mixC(uint32_t a, uint32_t b, int num, int den) {     // a toward b by num/den
  uint32_t o = 0;
  for (int s = 0; s <= 16; s += 8) { int ca = (a >> s) & 255, cb = (b >> s) & 255; o |= (uint32_t)(ca + (cb - ca) * num / den) << s; }
  return o;
}
// Draws sprite id with its top left at x, y. flip = mirror left-right. col = the color of a tint sprite, or of a
// color sprite's glow pixels.
static void fxSpr(int id, int x, int y, bool flip, uint32_t col) {
  const FxSprite &s = FX_SPR[id];
  const uint8_t *ix = s.px;
  int cr = col >> 16 & 255, cgg = col >> 8 & 255, cb = col & 255;
  for (int j = 0; j < s.h; j++) for (int i = 0; i < s.w; i++, ix++) {
    if (!*ix) continue;
    const uint8_t *p = s.pal + (*ix - 1) * 3 - 1;   // p[1..3] = the palette color, as in the old 4-byte layout
    int X = x + (flip ? s.w - 1 - i : i), Y = y + j;
    if ((unsigned)X >= (unsigned)W || (unsigned)Y >= (unsigned)H) continue;
    uint8_t *q = canvas[Y][X];
    if (s.tint) {
      int sh = p[1], wt = p[2], r = cr * sh / 255, g = cgg * sh / 255, b = cb * sh / 255;
      q[0] = r + (255 - r) * wt / 255; q[1] = g + (255 - g) * wt / 255; q[2] = b + (255 - b) * wt / 255;
    } else if (p[1] == 255 && p[2] == 0 && p[3] == 255) { q[0] = cr; q[1] = cgg; q[2] = cb; }
    else { q[0] = p[1]; q[1] = p[2]; q[2] = p[3]; }
  }
}
static void fxLine(int x0, int y0, int x1, int y1, uint32_t c) {     // reins
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
// Night sky: deep blue fading lighter toward the horizon, with twinkling stars (flakes 0..n-1).
static void fxNightSky(uint32_t frame, int n) {
  for (int y = 0; y < H; y++) {
    uint32_t c = mixC(0x00020C, 0x0E1434, y < 44 ? y : 44, 44);
    for (int x = 0; x < W; x++) px(x, y, c);
  }
  for (int i = 0; i < n; i++) {
    Flake &f = flakes[i];
    pxAdd(f.x / 16, f.y / 32, 0xDCE6FF, (sin8(frame * (f.spd / 4 + 1) + i * 41) * f.bri) >> 9);
  }
}

// Santa's sleigh and two reindeer crossing with a glitter trail. Full screen it is a night scene: stars, the moon,
// a snowy town with lit windows (now and then one goes dark), falling snow, and a big sleigh crossing the moon.
static void fxSleigh(uint32_t frame, bool behind) {
  if (!behind) {
    fxNightSky(frame, 24);
    fxSpr(FXS_MOON, 99, 2, false, 0);
    fxSpr(FXS_TOWN, 0, H - 30, false, 0);
    int nw = sizeof(FX_WINDOWS) / sizeof(FX_WINDOWS[0]);
    for (int k = 0; k < nw; k++) {
      const uint8_t *w = FX_WINDOWS[k];
      bool dark = (frame / 150 + k * 3) % 9 == 0;
      for (int y = 0; y < w[3]; y++) for (int x = 0; x < w[2]; x++)
        px(w[0] + x, H - 30 + w[1] + y, dark ? 0x241810 : (y == 0 || x == w[2] - 1) ? 0xD88A28 : 0xFFC860);
    }
  }
  // sizes in design units (make-fxsprites.mjs): big sprites are 0.8 px per unit, small ones 0.42
  int team = behind ? 50 : 94;                                    // whole team's width + gap, in px
  if (sleighWait) sleighWait--;
  else if ((sleighX += behind ? 9 : 16) > (W + 4) * 16) { sleighX = -team * 16; sleighWait = 20 + rnd(60); }
  float k = behind ? 0.42f : 0.8f;
  int x = sleighX / 16, y = (behind ? 1 : 5) + ((int)sin8(frame * 2) - 128) / (behind ? 40 : 26);
  for (int n = 0; !sleighWait && n < 3; n++)                      // glitter from the back of the sleigh
    for (auto &s : sparks)
      if (!s.life) {
        s = {(int16_t)((x + (int)(4 * k)) * 16 + rnd((int)(80 * k))), (int16_t)((y + (int)(14 * k) + rnd((int)(10 * k) + 1)) * 16), (int16_t)-(2 + rnd(8)), (int16_t)((int)rnd(5) - 2), (uint8_t)(14 + rnd(18)), 0, 0};
        s.max = s.life; break;
      }
  for (auto &s : sparks) {
    if (!s.life) continue;
    s.x += s.vx; s.y += s.vy; s.life--;
    pxAdd(s.x / 16, s.y / 16, s.life > s.max - 3 ? 0xFFFFFF : 0xFFD060, 255 * s.life / s.max);
  }
  if (!sleighWait) {
    bool step = (frame / 4) & 1;
    int d1 = x + (int)(50 * k), d2 = x + (int)(83 * k), dy = y - (int)(3 * k);
    int hx = (int)(20.5f * k), hy = (int)(13 * k);                // harness on a deer sprite
    uint32_t rein = 0xC89030;
    fxLine(x + (int)(30.5f * k), y + (int)(15 * k), d1 + hx, dy + step + hy, rein);
    fxLine(d1 + hx, dy + step + hy, d2 + hx, dy + !step + hy, rein);
    fxSpr(behind ? (step ? FXS_DEER0_S : FXS_DEER1_S) : (step ? FXS_DEER0 : FXS_DEER1), d1, dy + step, false, 0);
    fxSpr(behind ? (step ? FXS_DEER1R_S : FXS_DEER0R_S) : (step ? FXS_DEER1R : FXS_DEER0R), d2, dy + !step, false, 0);
    fxSpr(behind ? FXS_SLEIGH_S : FXS_SLEIGH, x, y, false, 0);
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
    if (f.y < -21 * 16) { f.y = H * 16 + rnd(320); f.x = rnd(W - 10) * 16; f.bri = rnd(7); }
    int x = f.x / 16 + ((int)sin8(frame * 2 + i * 50) - 128) / 48, y = f.y / 16;
    for (int r = 0; r < 7; r++) px(x + 5 + (((frame / 6 + r + i) & 2) ? 1 : 0), y + 14 + r, 0x8C8C8C);   // string
    fxSpr(FXS_BALLOON, x, y, false, PARTY[f.bri % 7]);
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
    if (f.x < -16 * 16 || f.x > (W + 4) * 16) {
      f.bri = (rnd(2) ? 128 : 0) | rnd(7); f.spd = 4 + rnd(10);
      f.x = (f.bri & 128) ? (W + 2) * 16 : -16 * 16; f.y = (2 + rnd(H - 12)) * 16;
    }
    int y = f.y / 16 + ((int)sin8(frame * 3 + i * 70) - 128) / 64;
    fxSpr(FXS_FISH, f.x / 16, y, left, PARTY[(f.bri & 7) % 7]);
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

// ---------- Holiday effects: turkey, bunny, bats, shamrocks, flag ----------
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
  if ((sleighX += 9) > (W + 2) * 16) { sleighX = -36 * 16; sleighWait = 40 + rnd(60); return; }
  bool step = (frame / 6) & 1;
  fxSpr(step ? FXS_TURKEY1 : FXS_TURKEY0, sleighX / 16, H - 2 - 34 + 1 - step, false, 0);
}

// Easter: a bunny hops across the grass past painted eggs; behind a message, eggs float up.
static void fxBunny(uint32_t frame, bool behind) {
  if (behind) {
    for (int i = 0; i < 7; i++) {
      Flake &f = flakes[i];
      f.y -= f.spd / 4 + 1;
      if (f.y < -11 * 16) { f.y = H * 16 + rnd(320); f.x = rnd(W - 8) * 16; }
      fxSpr(FXS_EGG, f.x / 16 + ((int)sin8(frame * 2 + i * 50) - 128) / 60, f.y / 16, false, PASTEL[i % 5]);
    }
    return;
  }
  for (int i = 0; i < 16; i++) { Flake &f = flakes[i]; pxAdd(f.x / 16, f.y / 32, 0xFFFFFF, (sin8(frame * 2 + i * 41) * f.bri) >> 10); }   // faint stars
  for (int x = 0; x < W; x++) {                                  // grass with blades and a few flowers
    for (int y = H - 3; y < H; y++) px(x, y, y == H - 3 ? 0x28922F : 0x1E7A28);
    int k = (x * 37) % 11;
    if (k < 4) px(x, H - 4, 0x30B040);
    if (k == 0 && (x & 1)) px(x, H - 5, 0x30B040);
    if (x % 19 == 7) px(x, H - 5, x % 38 == 7 ? 0xFF8CB4 : 0xFFE040);
  }
  static const uint8_t EGG_X[5] = {8, 32, 60, 86, 112};
  for (int e = 0; e < 5; e++) fxSpr(FXS_EGG, EGG_X[e], H - 11, false, PASTEL[e]);
  if (sleighWait) { sleighWait--; return; }
  int ph = frame % 30, hop = 0;
  if (ph < 20) { hop = ((int)sin8(ph * 128 / 20) - 128) * 14 / 127; sleighX += 28; }   // in the air: moving
  if (sleighX > (W + 2) * 16) { sleighX = -32 * 16; sleighWait = 30 + rnd(50); return; }
  fxSpr(hop > 3 ? FXS_BUNNY1 : FXS_BUNNY0, sleighX / 16, H - 3 - 26 - hop, false, 0);
}

// Halloween: bats flapping across; full screen with an orange moon and two flickering jack-o-lanterns.
static void fxBats(uint32_t frame, bool behind) {
  if (!behind) {
    for (int i = 0; i < 16; i++) { Flake &f = flakes[i]; pxAdd(f.x / 16, f.y / 32, 0xFFFFFF, (sin8(frame * 2 + i * 41) * f.bri) >> 10); }
    fxSpr(FXS_MOON_O, 98, 3, false, 0);
    uint32_t glow = mixC(0xFFE040, 0xFF8000, sin8(frame * 5) / 2 + rnd(40), 255);
    fxSpr(FXS_PUMPKIN, 8, H - 14, false, glow);
    fxSpr(FXS_PUMPKIN, 102, H - 14, true, mixC(glow, 0xFF8000, 1, 3));
  }
  for (int i = 0; i < (behind ? 4 : 6); i++) {
    Flake &f = flakes[40 + i];                                   // bri bit 7: flies left
    bool left = f.bri & 128;
    f.x += left ? -(f.spd / 3 + 4) : f.spd / 3 + 4;
    if (f.x < -22 * 16 || f.x > (W + 3) * 16) {
      f.bri = rnd(2) ? 128 : 0; f.spd = 4 + rnd(10);
      f.x = (f.bri & 128) ? (W + 2) * 16 : -21 * 16; f.y = (2 + rnd(behind ? 20 : 34)) * 16;
    }
    int y = f.y / 16 + ((int)sin8(frame * 4 + i * 60) - 128) / 32;
    fxSpr(((frame / 5 + i) & 1) ? FXS_BAT0 : FXS_BAT1, f.x / 16, y, false, 0);
  }
}

// St Patrick's: shamrocks drifting down, big and small, with gold twinkles (full screen).
static void fxShamrocks(uint32_t frame, bool behind) {
  for (int i = 0; i < (behind ? 10 : 18); i++) {
    Flake &f = flakes[i];
    f.y += f.spd / 4 + 1;
    if (f.y >= H * 16) { f.y = -14 * 16; f.x = rnd(W) * 16; }
    int x = f.x / 16 + ((int)sin8(frame * 3 + i * 40) - 128) / 40, y = f.y / 16;
    fxSpr(f.spd > 8 ? FXS_CLOVER : FXS_CLOVER_S, x, y, false, GREENS[i % 3]);
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
    case FX_HEARTS:                              // hearts floating up, big and small
      for (int i = 0; i < (behind ? 10 : 18); i++) {
        Flake &f = flakes[i];
        f.y -= f.spd / 2 + 1;
        if (f.y < -9 * 16) { f.y = H * 16 + random(64); f.x = random(W * 16); }
        int x = f.x / 16 + (sin8(frame * 3 + i * 40) - 128) / 40, y = f.y / 16;
        uint32_t c = (255UL << 16) | ((uint32_t)(30 + i * 37 % 90) << 8) | (90 + i * 53 % 120);
        fxSpr(f.spd > 8 ? FXS_HEART : FXS_HEART_S, x, y, false, c);
      }
      break;
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
