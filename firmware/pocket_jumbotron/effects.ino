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
