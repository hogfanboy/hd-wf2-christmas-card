// Color tune: full-screen test screens for setting the color balance (calGain) by eye at each brightness level.
// The page (web/app/6-tune.js) picks a level and a screen through POST /api/cal; the panel shows that level
// whatever the time of day. The show comes back after 10 minutes without a call, or on "stop".
//   1 Grid:   7 columns x 5 rows of one color. Columns change red, rows change blue; the middle (C4) is the
//             current setting. "Use it" on the best-looking patch moves the setting there.
//   2 Colors: the colors the screens use, each as a solid block and as small text, numbered 1-8.
//   3 Ramps:  16 steps from dark to full of white, red, green, blue, gold and light blue (rows 1-6).
//   4 Solid:  the whole screen in one color (row bands show up best here).
//   5 Wiring: for cable / HUB75 pin faults. Top half red | green | blue | white, bottom half the other way round,
//             a black line every 8 rows and every 16 columns, a white frame. A dead color, a swapped half or a
//             repeated / shifted row group points at one HUB75 line.
static const float TUNE_RED[7] = {0.70f, 0.80f, 0.90f, 1.00f, 1.12f, 1.25f, 1.40f};   // grid columns 1-7
static const float TUNE_BLUE[5] = {1.25f, 1.12f, 1.00f, 0.90f, 0.80f};               // grid rows A-E
static const uint32_t TUNE_LABEL = 0x8C8C8C;
uint8_t tunePat = 1;
uint32_t tuneCol = 0xFFFFFF;
bool tuneDirty = true;

static void tuneRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  for (int j = y; j < y + h && j < H; j++)
    for (int i = x; i < x + w && i < W; i++) { canvas[j][i][0] = r; canvas[j][i][1] = g; canvas[j][i][2] = b; }
}
static inline uint8_t tuneMul(uint8_t v, float k) { float f = v * k + 0.5f; return f > 255 ? 255 : (uint8_t)f; }

void tuneDraw() {
  if (!canvas) return;
  memset(canvas, 0, CANVAS_BYTES);
  uint8_t r = tuneCol >> 16, g = tuneCol >> 8, b = tuneCol;
  if (tunePat == 1) {
    // brought to 180 at most so the stronger patches still fit under 255
    uint8_t mx = std::max(r, std::max(g, b));
    float k = mx ? 180.0f / mx : 0;
    for (int c = 0; c < 7; c++) labelC(String(c + 1), 8 + c * 17 + 7, 0, TUNE_LABEL);
    for (int row = 0; row < 5; row++) {
      label(String((char)('A' + row)), 0, 9 + row * 11, TUNE_LABEL);
      for (int c = 0; c < 7; c++)
        tuneRect(8 + c * 17, 9 + row * 11, 15, 9, tuneMul(r, k * TUNE_RED[c]), tuneMul(g, k), tuneMul(b, k * TUNE_BLUE[row]));
    }
  } else if (tunePat == 2) {
    const Theme &t = th();
    const uint32_t cols[8] = {t.main, t.acc, t.dim, WX_HI, WX_LO, 0xC8E6FF, 0x46AAFF, 0xFFFFFF};   // rain % and drop: weather.ino WX_PCT, WX_DROP
    const char *names[8] = {"MAIN 72", "ACCENT 72", "DIM 72", "HIGH 72", "LOW 45", "RAIN 40%", "DROP", "WHITE 72"};
    for (int i = 0; i < 8; i++) {
      uint32_t c = cols[i];
      label(String(i + 1), 0, i * 8, TUNE_LABEL);
      tuneRect(8, i * 8, 40, 7, c >> 16, c >> 8, c);
      label(names[i], 52, i * 8, c);
    }
  } else if (tunePat == 3) {
    const uint32_t cols[6] = {0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFD27A, 0xC8E6FF};
    for (int row = 0; row < 6; row++)
      for (int s = 0; s < 16; s++) {
        float k = (s + 1) / 16.0f;
        uint32_t c = cols[row];
        tuneRect(s * 8, 2 + row * 10, 7, 9, tuneMul(c >> 16, k), tuneMul((c >> 8) & 255, k), tuneMul(c & 255, k));
      }
  } else if (tunePat == 5) {
    const uint8_t C[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    for (int q = 0; q < 4; q++) {
      tuneRect(q * 32, 0, 32, 32, C[q][0], C[q][1], C[q][2]);
      tuneRect(q * 32, 32, 32, 32, C[3 - q][0], C[3 - q][1], C[3 - q][2]);
    }
    for (int y = 7; y < H; y += 8) tuneRect(0, y, W, 1, 0, 0, 0);
    for (int x = 15; x < W; x += 16) tuneRect(x, 0, 1, H, 0, 0, 0);
    tuneRect(0, 0, W, 1, 255, 255, 255); tuneRect(0, H - 1, W, 1, 255, 255, 255);
    tuneRect(0, 0, 1, H, 255, 255, 255); tuneRect(W - 1, 0, 1, H, 255, 255, 255);
  } else {
    tuneRect(0, 0, W, H, r, g, b);
  }
  pushCanvas();
  tuneDirty = false;
}
void tuneStop() {
  tuneUntil = 0; tuneLevel = -1;
  applyBrightness();
  memset(canvas, 0, CANVAS_BYTES); pushCanvas(); shownValid = false;
}
void tuneTick() {
  if ((int32_t)(millis() - tuneUntil) >= 0) { tuneStop(); return; }
  if (tuneDirty) tuneDraw();
}
