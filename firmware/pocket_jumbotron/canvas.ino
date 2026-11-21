// Drawing helpers: text and pixels into the RGB canvas (pushed by player.ino), and plain panel text.

// ---------- Drawing text into the canvas (so updates go through pushCanvas: no flicker) ----------
class CanvasGFX : public Adafruit_GFX {
 public:
  uint8_t r = 255, g = 255, b = 255;
  CanvasGFX() : Adafruit_GFX(W, H) {}
  void drawPixel(int16_t x, int16_t y, uint16_t) override {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    canvas[y][x][0] = r; canvas[y][x][1] = g; canvas[y][x][2] = b;
  }
  void rgb(uint32_t c) { r = c >> 16; g = c >> 8; b = c; }
  const GFXfont *font() const { return gfxFont; }
  uint8_t size() const { return textsize_x; }
};
static CanvasGFX cg;

static uint32_t hsv(uint16_t h) {                // h 0..1535, full saturation and value
  uint8_t x = h & 255;
  switch (h >> 8) {
    case 0: return 0xFF0000 | (x << 8);
    case 1: return ((255 - x) << 16) | 0xFF00;
    case 2: return 0xFF00 | x;
    case 3: return ((255 - x) << 8) | 0xFF;
    case 4: return (x << 16) | 0xFF;
    default: return 0xFF0000 | (255 - x);
  }
}
// Fonts: 1 tiny (5x7 doubled), 2-4 classic (FreeSans Bold) small/medium/big, 8-12 our own PJ font (pj_font.h,
// drawn with soft edges): 8 thin small, 9 small, 10 medium, 11 big, 12 extra big.
static const PJFontDef *pjf = nullptr;           // the current font when it's a PJ size, else null (Adafruit GFX)
static uint8_t curFont = 2;
static inline bool isPJ(uint8_t f) { return f >= 8 && f <= 12; }
static void setFont(uint8_t f) {
  static const GFXfont *const F[] = {&FreeSansBold12pt7b, &FreeSansBold18pt7b, &FreeSansBold24pt7b};
  curFont = f;
  pjf = isPJ(f) ? &PJ_FONTS[f - 8] : nullptr;
  if (pjf) return;
  cg.setTextWrap(false);
  if (f < 2 || f > 4) { cg.setFont(nullptr); cg.setTextSize(2); }
  else { cg.setTextSize(1); cg.setFont(F[f - 2]); }
}
static inline const PJGlyph &pjGlyph(char c) { uint8_t u = c; return pjf->g[(u >= 32 && u <= 127 ? u : '?') - 32]; }
// Blends one PJ glyph into the canvas (alpha 0..15 per pixel); returns the advance.
static int pjChar(char c, int x, int base, uint32_t col) {
  const PJGlyph &g = pjGlyph(c);
  const uint8_t *bits = pjf->bits + g.off;
  uint8_t r = col >> 16, gg = col >> 8, b = col;
  for (int i = 0, n = g.w * g.h; i < n; i++) {
    uint8_t a = (bits[i >> 1] >> (i & 1 ? 0 : 4)) & 15;
    if (!a) continue;
    int px = x + g.dx + i % g.w, py = base + g.dy + i / g.w;
    if ((unsigned)px >= (unsigned)W || (unsigned)py >= (unsigned)H) continue;
    uint8_t *p = canvas[py][px];
    if (a == 15) { p[0] = r; p[1] = gg; p[2] = b; continue; }
    p[0] = (p[0] * (15 - a) + r * a) / 15; p[1] = (p[1] * (15 - a) + gg * a) / 15; p[2] = (p[2] * (15 - a) + b * a) / 15;
  }
  return g.adv;
}
// Big numbers (clock, countdown, weather): the largest font, up to the item's size (at least medium), whose
// width for s plus extra fits in maxW. Sets it and returns it.
static int textWidth(const String &s);
static uint8_t fitBig(uint8_t f, const String &s, int maxW, int extra) {
  static const uint8_t K[] = {4, 3, 2}, P[] = {12, 11, 10};
  bool pj = isPJ(f);
  const uint8_t *l = (pj ? P : K) + (f == (pj ? 12 : 4) ? 0 : 1);
  uint8_t n = f == (pj ? 12 : 4) ? 3 : 2;
  for (uint8_t i = 0; i < n; i++) { setFont(l[i]); if (textWidth(s) + extra <= maxW || i == n - 1) return l[i]; }
  return l[n - 1];
}
// Width of a string and the y that centers it vertically in a band [top, top+h).
static int textWidth(const String &s) {
  if (pjf) { int w = 0; for (unsigned i = 0; i < s.length(); i++) w += pjGlyph(s[i]).adv; return s.length() ? w - pjf->gap : 0; }
  int16_t x1, y1; uint16_t w, h; cg.getTextBounds(s.c_str(), 0, 0, &x1, &y1, &w, &h); return w + x1;
}
static int textBaseline(uint8_t font, int top, int h) {
  if (pjf) return top + (h - pjf->cap) / 2 + pjf->cap;
  int16_t x1, y1; uint16_t w, hh;
  cg.getTextBounds("0AH", 0, 30, &x1, &y1, &w, &hh);     // cap height of this font
  return top + (h - hh) / 2 + (30 - y1);
}
// Top row of the digits for text drawn on this baseline (the AM/PM and degree sign line up with it).
static int digitTop(int base) {
  if (pjf) return base - pjf->cap;
  int16_t x1, y1; uint16_t bw, bh; cg.getTextBounds("0", 0, base, &x1, &y1, &bw, &bh); return y1;
}
// Draws s at x (left) on baseline y in color c (current font); returns the x after it.
static int printAt(const String &s, int x, int y, uint32_t c) {
  if (pjf) { for (unsigned i = 0; i < s.length(); i++) x += pjChar(s[i], x, y, c); return x; }
  cg.rgb(c); cg.setCursor(x, y); cg.print(s); return cg.getCursorX();
}
// Draws s at x (left) on baseline y; rainbow shifts hue per letter over time.
static void drawText(const String &s, int x, int y, const ShowItem &it, uint32_t now) {
  if (!it.rainbow) { printAt(s, x, y, it.fg); return; }
  if (pjf) { for (unsigned i = 0; i < s.length(); i++) x += pjChar(s[i], x, y, hsv((now / 4 + i * 120) % 1536)); return; }
  cg.setCursor(x, y);
  for (unsigned i = 0; i < s.length(); i++) { cg.rgb(hsv((now / 4 + i * 120) % 1536)); cg.write(s[i]); }
}
// Small labels (dates, captions, titles): the 5x7 pixel font, or our thin PJ font when the item uses PJ fonts.
// y is the top of the letters; both are 7-9 px tall. Leaves the label font set.
static bool pjLabels = false;
static void labelFont() { if (pjLabels) setFont(8); else { setFont(1); cg.setTextSize(1); } }
static int labelW(const String &s) { labelFont(); return textWidth(s); }
static void label(const String &s, int x, int top, uint32_t c) {
  labelFont();
  printAt(s, x, pjLabels ? top + pjf->cap : top, c);
}
static void labelC(const String &s, int cx, int top, uint32_t c) { label(s, cx - labelW(s) / 2, top, c); }

static uint32_t rs = 0x9E3779B9;                 // fast xorshift: fire touches every pixel each frame
static inline uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static inline uint32_t rnd(uint32_t n) { return rnd() % n; }
static inline void px(int x, int y, uint32_t c) {
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  canvas[y][x][0] = c >> 16; canvas[y][x][1] = c >> 8; canvas[y][x][2] = c;
}
static inline void pxAdd(int x, int y, uint32_t c, uint8_t v) {      // additive glow, v = 0..255
  if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
  uint8_t *p = canvas[y][x];
  p[0] = std::min(255, p[0] + (int)((c >> 16) & 255) * v / 255);
  p[1] = std::min(255, p[1] + (int)((c >> 8) & 255) * v / 255);
  p[2] = std::min(255, p[2] + (int)(c & 255) * v / 255);
}
static uint8_t sin8(uint8_t a) {                 // 0..255 sine, 0 -> 128
  static uint8_t t[256]; static bool made = false;
  if (!made) { for (int i = 0; i < 256; i++) t[i] = 128 + (int)(127 * sinf(i * 6.2832f / 256)); made = true; }
  return t[a];
}

static String upcase(String s) { s.toUpperCase(); return s; }

// ---------- Straight-to-panel text (setup and address screens) ----------
static void text(int y, const char *s, uint16_t color) { panel->setTextColor(color); panel->setCursor(1, y); panel->print(s); }
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return panel->color565(r, g, b); }
// Pure color for fast scan (2-bit): 224 is a value whose 565-truncated table entry has both low bits set on
// every channel (255 becomes 248 for red/blue = only 1/3 lit). See the QR notes below.
static uint16_t CF(bool r, bool g, bool b) { return C(r ? 224 : 0, g ? 224 : 0, b ? 224 : 0); }
