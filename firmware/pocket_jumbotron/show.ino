// The show: an ordered list of items, each shown for a number of seconds.
//   /show.txt, one item per line, fields separated by TABs (written by the web page):
//     pic       <sec> <file in /gifs>
//     text      <sec> <fg> <bg> <font 1-4> <effect> <message>
//     clock     <sec> <fg> <bg> <font> <effect> <12|24>
//     countdown <sec> <fg> <bg> <font> <effect> <MM-DD> <label>
//     fx        <sec> <fg> <bg> <font> <effect>                      (an effect on its own, full screen)
//     weather   <sec> <fg> <bg> <font> <effect> <F|C> <lat> <lon> <place>
//   sec 0 = automatic (pictures: 8-20 s by their own length; scrolling text: one pass; others: 8-10 s).
//   fg/bg are RRGGBB hex, fg may be "rainbow". effect: one of FX_NAMES (none, snow, sparkle, hearts, confetti,
//   fireworks, stars, fire, matrix). Unknown names (e.g. the removed "plasma") play as none.
//   Any line may end with an options field starting with '@' (space-separated, all optional; fw 0.9.0+):
//     dates=MMDD-MMDD  only between these days, every year (may wrap past New Year, e.g. 1215-0105)
//     days=0123456     only on these weekdays (0 = Sunday)
//     hours=H-H        only from the first hour up to (not including) the second; may wrap past midnight
//     lead=N           countdown: only in the last N days before the date (a reminder)
//     forecast         weather: then a 5-day forecast screen
//   Items with a time rule are skipped until the clock is set. Older firmware ignores the field except as
//   the last text of a line.
//   Without /show.txt every file in /gifs plays in name order (the old behavior).
// Picture files: .gif (AnimatedGIF) or .pjm = full-color JPEG frames:
//   "PJM1", then per frame: u32 jpeg length, u16 delay ms, u16 0, the JPEG (128x64 baseline).
// Time (for clock/countdown/night mode) comes from NTP once home WiFi is up; the time zone is a POSIX TZ
// string saved by the web page.
#include <JPEGDEC.h>
#include <time.h>
#include <HTTPClient.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

bool timeValid() { return time(nullptr) > 1700000000; }
void timeBegin() { configTzTime(tzString.c_str(), "pool.ntp.org", "time.google.com"); }

static uint32_t nightLast = 0;
void forceNightCheck() { nightLast = 0; checkNight(); }
void checkNight() {
  uint32_t &last = nightLast;
  if (last && millis() - last < 20000) return;
  last = millis();
  bool n = false;
  if (nightFrom >= 0 && nightTo >= 0 && nightFrom != nightTo && timeValid()) {
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    int h = lt.tm_hour;
    n = nightFrom < nightTo ? (h >= nightFrom && h < nightTo) : (h >= nightFrom || h < nightTo);
  }
  if (n != nightNow) { nightNow = n; applyBrightness(); Serial.printf("Night mode %s\n", n ? "on" : "off"); }
}

// ---------- Show list ----------
const char *const FX_NAMES[] = {"none", "snow", "sparkle", "hearts", "confetti", "fireworks", "stars", "fire", "matrix"};
enum { FX_NONE, FX_SNOW, FX_SPARKLE, FX_HEARTS, FX_CONFETTI, FX_FIREWORKS, FX_STARS, FX_FIRE, FX_MATRIX, FX_COUNT };
static uint8_t fxByName(const String &s) { for (int i = 0; i < FX_COUNT; i++) if (s == FX_NAMES[i]) return i; return FX_NONE; }

static bool isPicture(const String &n) { String l = n; l.toLowerCase(); return l.endsWith(".gif") || l.endsWith(".pjm"); }
static uint32_t hex(const String &s) { return strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF; }

static void parseOptions(ShowItem &it, const String &opts) {
  int s = 0;
  while (s < (int)opts.length()) {
    int e = opts.indexOf(' ', s); if (e < 0) e = opts.length();
    String t = opts.substring(s, e); s = e + 1;
    int eq = t.indexOf('='), dash;
    String k = eq < 0 ? t : t.substring(0, eq), v = eq < 0 ? "" : t.substring(eq + 1);
    dash = v.indexOf('-');
    if (k == "dates" && dash > 0) { it.dFrom = v.substring(0, dash).toInt(); it.dTo = v.substring(dash + 1).toInt(); }
    else if (k == "days") { it.days = 0; for (char c : v) if (c >= '0' && c <= '6') it.days |= 1 << (c - '0'); if (!it.days) it.days = 0x7F; }
    else if (k == "hours" && dash > 0) { it.hFrom = constrain((int)v.substring(0, dash).toInt(), 0, 23); it.hTo = constrain((int)v.substring(dash + 1).toInt(), 0, 24); }
    else if (k == "lead") it.lead = v.toInt();
    else if (k == "forecast") it.forecast = true;
  }
}
static int daysUntil(const String &mmdd);
// Is this item on the schedule right now? (see the options above)
bool itemActive(const ShowItem &it) {
  bool timed = it.dFrom || it.days != 0x7F || it.hFrom >= 0 || it.lead >= 0;
  if (!timed) return true;
  if (!timeValid()) return false;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  if (it.dFrom && it.dTo) {
    int md = (lt.tm_mon + 1) * 100 + lt.tm_mday;
    bool in = it.dFrom <= it.dTo ? (md >= it.dFrom && md <= it.dTo) : (md >= it.dFrom || md <= it.dTo);
    if (!in) return false;
  }
  if (!(it.days & (1 << lt.tm_wday))) return false;
  if (it.hFrom >= 0 && it.hTo >= 0 && it.hFrom != it.hTo) {
    int h = lt.tm_hour;
    bool in = it.hFrom < it.hTo ? (h >= it.hFrom && h < it.hTo) : (h >= it.hFrom || h < it.hTo);
    if (!in) return false;
  }
  if (it.lead >= 0 && it.type == 'd' && it.extra.length() >= 5 && daysUntil(it.extra) > it.lead) return false;
  return true;
}

void loadPlaylist() {
  playlist.clear();
  File sf = LittleFS.open("/show.txt", "r");
  if (sf) {
    while (sf.available()) {
      String line = sf.readStringUntil('\n');
      line.replace("\r", "");
      std::vector<String> f;
      int s = 0;
      for (;;) { int t = line.indexOf('\t', s); if (t < 0) { f.push_back(line.substring(s)); break; } f.push_back(line.substring(s, t)); s = t + 1; }
      ShowItem it;
      if (f.size() > 3 && f.back().startsWith("@")) { parseOptions(it, f.back().substring(1)); f.pop_back(); }
      if (f.size() < 3) continue;
      it.sec = f[1].toInt();
      if (f[0] == "pic") { it.type = 'p'; it.file = "/gifs/" + f[2]; if (!LittleFS.exists(it.file)) continue; }
      else {
        if (f.size() < 6) continue;
        it.type = f[0] == "text" ? 't' : f[0] == "clock" ? 'c' : f[0] == "countdown" ? 'd' : f[0] == "fx" ? 'f' : f[0] == "weather" ? 'w' : 0;
        if (!it.type) continue;
        it.rainbow = f[2] == "rainbow";
        it.fg = it.rainbow ? 0xFFFFFF : hex(f[2]);
        it.bg = hex(f[3]);
        it.font = constrain((int)f[4].toInt(), 1, 4);
        it.effect = fxByName(f[5]);
        f.resize(std::max<size_t>(f.size(), 10));
        if (it.type == 't') it.text = f[6];
        if (it.type == 'c') it.extra = f[6];
        if (it.type == 'd') { it.extra = f[6]; it.text = f[7]; }
        if (it.type == 'f' && it.effect == FX_NONE) continue;          // removed effect
        if (it.type == 'w') { it.extra = f[6] + "," + f[7] + "," + f[8]; it.text = f[9]; if (f[7].isEmpty()) continue; }
      }
      playlist.push_back(it);
    }
    sf.close();
    if (!playlist.empty()) return;
  }
  std::vector<String> names;
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory())
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) if (isPicture(f.name())) names.push_back(String("/gifs/") + f.name());
  std::sort(names.begin(), names.end());
  for (auto &n : names) { ShowItem it; it.file = n; playlist.push_back(it); }
}

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
static void setFont(uint8_t f) {
  cg.setTextWrap(false);
  if (f == 1) { cg.setFont(nullptr); cg.setTextSize(2); }
  else { cg.setTextSize(1); cg.setFont(f == 2 ? &FreeSansBold12pt7b : f == 3 ? &FreeSansBold18pt7b : &FreeSansBold24pt7b); }
}
// Width of a string and the y that centers it vertically in a band [top, top+h).
static int textWidth(const String &s) { int16_t x1, y1; uint16_t w, h; cg.getTextBounds(s.c_str(), 0, 0, &x1, &y1, &w, &h); return w + x1; }
static int textBaseline(uint8_t font, int top, int h) {
  int16_t x1, y1; uint16_t w, hh;
  cg.getTextBounds("0AH", 0, 30, &x1, &y1, &w, &hh);     // cap height of this font
  return top + (h - hh) / 2 + (30 - y1);
}
// Draws s at x (left) on baseline y; rainbow shifts hue per letter over time.
static void drawText(const String &s, int x, int y, const ShowItem &it, uint32_t now) {
  if (!it.rainbow) { cg.rgb(it.fg); cg.setCursor(x, y); cg.print(s); return; }
  cg.setCursor(x, y);
  for (unsigned i = 0; i < s.length(); i++) { cg.rgb(hsv((now / 4 + i * 120) % 1536)); cg.write(s[i]); }
}

// ---------- Effects library (drawn by the card: no storage, any length) ----------
// Used full screen (the "fx" item) or behind a message / clock / countdown / weather. Positions are in
// 1/16 pixel so slow things move smoothly. Fire borrows an 8 KB heat map only while it plays.

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

// ---------- Text / clock / countdown items ----------
static String upcase(String s) { s.toUpperCase(); return s; }

static void playText(const ShowItem &it) {
  setFont(it.font);
  effectInit(it); FxGuard fxg;
  int tw = textWidth(it.text);
  int base = textBaseline(it.font, 0, H);
  bool scroll = tw > W - 4;
  uint32_t total = it.sec ? it.sec * 1000UL : scroll ? (uint32_t)(W + tw) * 25 : 6000;
  uint32_t start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    int x = scroll ? W - (int)((millis() - start) / 25) % (W + tw) : (W - tw) / 2;
    drawText(it.text, x, base, it, millis());
    pushCanvas();
    frame++;
    int32_t wait = 25 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

static void playClock(const ShowItem &it) {
  if (!timeValid()) return;
  effectInit(it); FxGuard fxg;
  uint32_t total = (it.sec ? it.sec : 8) * 1000UL, start = millis(), frame = 0;
  bool h12 = it.extra != "24";
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char hm[8], date[24];
    int hour = h12 ? (lt.tm_hour % 12 ? lt.tm_hour % 12 : 12) : lt.tm_hour;
    snprintf(hm, sizeof(hm), h12 ? "%d:%02d" : "%02d:%02d", hour, lt.tm_min);
    strftime(date, sizeof(date), "%a %b %e", &lt);
    paintBackground(it, frame);
    setFont(it.font < 3 ? 3 : it.font);
    if (textWidth(hm) > W - 2) setFont(3);
    drawText(hm, (W - textWidth(hm)) / 2, textBaseline(3, 2, 40), it, millis());
    setFont(1); cg.setTextSize(1);
    String d = upcase(date) + (h12 ? (lt.tm_hour < 12 ? " AM" : " PM") : "");
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(d)) / 2, 50); cg.print(d);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

static int daysUntil(const String &mmdd) {
  int m = mmdd.substring(0, 2).toInt(), d = mmdd.substring(3, 5).toInt();
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  struct tm a = lt; a.tm_hour = 12; a.tm_min = a.tm_sec = 0; a.tm_isdst = -1;
  struct tm b = a; b.tm_mon = m - 1; b.tm_mday = d;
  time_t ta = mktime(&a), tb = mktime(&b);
  if (tb < ta - 3600) { b.tm_year++; tb = mktime(&b); }
  return (int)((tb - ta + 43200) / 86400);
}

static void playCountdown(const ShowItem &item) {
  if (!timeValid() || item.extra.length() < 5) return;
  int days = daysUntil(item.extra);
  ShowItem it = item;
  if (days == 0 && it.lead >= 0) it.effect = FX_FIREWORKS;       // a reminder's big day
  effectInit(it); FxGuard fxg;
  String big = days == 0 ? "TODAY!" : String(days);
  String l1 = days == 0 ? "" : days == 1 ? "DAY UNTIL" : "DAYS UNTIL";
  String l2 = upcase(it.text);
  if (days == 0) { l1 = l2; l2 = ""; }
  uint32_t total = (it.sec ? it.sec : 8) * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    setFont(it.font < 3 ? 3 : it.font);
    if (textWidth(big) > W - 2) setFont(3);
    if (textWidth(big) > W - 2) setFont(2);
    drawText(big, (W - textWidth(big)) / 2, textBaseline(3, 0, 40), it, millis());
    setFont(1); cg.setTextSize(1);
    cg.rgb(0xFFFFFF);
    if (l1.length()) { cg.setCursor((W - textWidth(l1)) / 2, 42); cg.print(l1); }
    if (l2.length()) { cg.rgb(0xFFBE3C); cg.setCursor((W - textWidth(l2)) / 2, 53); cg.print(l2); }
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

// ---------- Full-color JPEG-frame pictures (.pjm) ----------
static int jpegDraw(JPEGDRAW *d) {
  const uint32_t *px = (const uint32_t *)d->pPixels;
  for (int y = 0; y < d->iHeight; y++) {
    int yy = d->y + y;
    if (yy >= H) break;
    for (int x = 0; x < d->iWidthUsed; x++) {
      int xx = d->x + x;
      if (xx >= W) continue;
      uint32_t c = px[y * d->iWidth + x];            // 0xAABBGGRR
      canvas[yy][xx][0] = c; canvas[yy][xx][1] = c >> 8; canvas[yy][xx][2] = c >> 16;
    }
  }
  return 1;
}

void playPjm(const ShowItem &it) {
  File f = LittleFS.open(it.file, "r");
  char magic[4];
  if (!f || f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "PJM1", 4)) { Serial.printf("  bad pjm %s\n", it.file.c_str()); return; }
  // The buffer only needs to hold this file's biggest frame (usually 5-12 KB): asking for less than a fixed
  // 24 KB keeps full-color pictures working when the heap is fragmented (after weather fetches, uploads...).
  size_t CAP = 0;
  for (uint8_t hdr[8]; f.read(hdr, 8) == 8;) {
    uint32_t len = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    if (len <= 32768 && len > CAP) CAP = len;
    f.seek(f.position() + len);
  }
  f.seek(4);
  JPEGDEC *jpeg = new (std::nothrow) JPEGDEC();
  uint8_t *buf = CAP ? (uint8_t *)malloc(CAP) : nullptr;
  if (!jpeg || !buf) {
    Serial.printf("  no memory for jpeg (frame %u, free %u, largest block %u)\n", (unsigned)CAP, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    delete jpeg; free(buf); f.close(); return;
  }
  playing = true;
  memset(canvas, 0, CANVAS_BYTES);
  shownValid = false;
  uint32_t start = millis(), frames = 0, loops = 0;
  uint32_t limit = it.sec ? it.sec * 1000UL : 0;
  for (;;) {
    uint32_t t0 = millis();
    uint8_t hdr[8];
    if (f.read(hdr, 8) != 8) {                                   // end of one loop
      loops++;
      uint32_t el = millis() - start;
      if (!frames) break;
      if (limit ? el >= limit : el >= MIN_SHOW_MS) break;
      f.seek(4);
      continue;
    }
    uint32_t len = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    int delayMs = hdr[4] | hdr[5] << 8;
    if (len > CAP) { f.seek(f.position() + len); continue; }
    if (f.read(buf, len) != (int)len) break;
    if (jpeg->openRAM(buf, len, jpegDraw)) {
      jpeg->setPixelType(RGB8888);
      jpeg->decode(0, 0, 0);
      jpeg->close();
    }
    pushCanvas();
    frames++;
    if (delayMs < 20) delayMs = 100;
    uint32_t el = millis() - start;
    if (limit && el >= limit) break;
    if (!limit && el > MAX_SHOW_MS && loops > 0) break;
    if (!limit && el > MAX_SHOW_MS * 2) break;
    int32_t wait = delayMs - (int32_t)(millis() - t0);
    if (wait > 0 && waitMs(wait)) break;
    if (buttonAction || holdPlayback || addrUntil) break;
  }
  uint32_t ms = millis() - start;
  Serial.printf("  %-36s jpeg %4u frames %2u loops %5.1f fps\n", it.file.c_str(), (unsigned)frames, (unsigned)loops, frames * 1000.0f / (ms ? ms : 1));
  free(buf);
  delete jpeg;
  f.close();
  playing = false;
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

// ---------- Weather (Open-Meteo: free, no key, plain http so no TLS memory) ----------
struct Weather {
  bool ok = false; uint32_t at = 0; String key; float temp = 0, hi = 0, lo = 0; int code = 0; bool day = true;
  int fcN = 0, fcCode[5]; float fcHi[5], fcLo[5];          // daily forecast, [0] = today at the town
  uint32_t failAt = 0;                                       // last failed fetch (back off 5 min)
};
static Weather wx;
static float jsonNum(const String &s, const char *key, int from) {
  int i = s.indexOf(key, from);
  return i < 0 ? NAN : s.substring(i + strlen(key), i + strlen(key) + 16).toFloat();
}
// Reads up to n numbers of a JSON array that starts right after key (key ends with '[').
static int jsonArr(const String &s, const char *key, int from, float *out, int n) {
  int i = s.indexOf(key, from);
  if (i < 0) return 0;
  const char *p = s.c_str() + i + strlen(key);
  int k = 0;
  while (k < n && *p && *p != ']') {
    char *end; float v = strtof(p, &end);
    if (end == p) break;
    out[k++] = v; p = end;
    while (*p == ',' || *p == ' ') p++;
  }
  return k;
}
// it.extra = "F,lat,lon". Refreshes at most every 15 minutes; keeps the last reading if a fetch fails.
static bool weatherFetch(const ShowItem &it) {
  bool cached = wx.ok && wx.key == it.extra;
  if (cached && millis() - wx.at < 15UL * 60 * 1000) return true;
  if (WiFi.status() != WL_CONNECTED) return cached;
  if (wx.failAt && millis() - wx.failAt < 5UL * 60 * 1000) return cached;   // service down: don't stall the show
  int c1 = it.extra.indexOf(','), c2 = it.extra.indexOf(',', c1 + 1);
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + it.extra.substring(c1 + 1, c2) + "&longitude=" + it.extra.substring(c2 + 1) +
               "&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5" +
               (it.extra.startsWith("F") ? "&temperature_unit=fahrenheit" : "");
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  uint32_t t0 = millis();
  if (!http.begin(url)) return cached;
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  int cur = body.indexOf("\"current\":{"), day = body.indexOf("\"daily\":{");
  Serial.printf("Weather: HTTP %d, %u bytes, %u ms, free heap %u\n", code, (unsigned)body.length(), (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
  if (cur < 0 || day < 0) { wx.failAt = millis() | 1; return cached; }
  float t = jsonNum(body, "\"temperature_2m\":", cur);
  if (isnan(t)) return cached;
  wx.temp = t;
  wx.code = (int)jsonNum(body, "\"weather_code\":", cur);
  wx.day = jsonNum(body, "\"is_day\":", cur) != 0;
  wx.hi = jsonNum(body, "\"temperature_2m_max\":[", day);
  wx.lo = jsonNum(body, "\"temperature_2m_min\":[", day);
  float codes[5];
  int n = jsonArr(body, "\"weather_code\":[", day, codes, 5);
  n = std::min(n, jsonArr(body, "\"temperature_2m_max\":[", day, wx.fcHi, 5));
  n = std::min(n, jsonArr(body, "\"temperature_2m_min\":[", day, wx.fcLo, 5));
  for (int i = 0; i < n; i++) wx.fcCode[i] = (int)codes[i];
  wx.fcN = n;
  Serial.printf("Weather: %.0f now, forecast %d days (today %d, hi %.0f lo %.0f)\n", wx.temp, n, n ? wx.fcCode[0] : -1, n ? wx.fcHi[0] : 0.f, n ? wx.fcLo[0] : 0.f);
  wx.key = it.extra; wx.at = millis(); wx.ok = true; wx.failAt = 0;
  return true;
}
enum { WX_SUN, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };
static int wxKind(int c) {                       // WMO weather codes
  if (c <= 1) return WX_SUN;
  if (c == 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUD;
  if (c == 45 || c == 48) return WX_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_RAIN;
}
static const char *wxWord(int c) {
  if (c == 0) return "CLEAR"; if (c == 1) return "MOSTLY CLEAR"; if (c == 2) return "PARTLY CLOUDY"; if (c == 3) return "CLOUDY";
  if (c == 45 || c == 48) return "FOG"; if (c >= 51 && c <= 57) return "DRIZZLE"; if (c >= 61 && c <= 67) return "RAIN";
  if (c >= 71 && c <= 77) return "SNOW"; if (c >= 80 && c <= 82) return "SHOWERS"; if (c == 85 || c == 86) return "SNOW SHOWERS";
  if (c >= 95) return "THUNDERSTORM"; return "";
}
static void wxCloud(int x, int y, uint32_t c) {
  cg.rgb(c);
  cg.fillCircle(x - 7, y + 2, 6, 0); cg.fillCircle(x + 1, y - 3, 8, 0); cg.fillCircle(x + 9, y + 2, 6, 0); cg.fillRect(x - 7, y + 2, 16, 7, 0);
}
static void wxSun(int x, int y, int r, uint32_t frame) {
  cg.rgb(0xFFC828);
  cg.fillCircle(x, y, r, 0);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7854f + frame * 0.02f;
    cg.drawLine(x + cosf(a) * (r + 3), y + sinf(a) * (r + 3), x + cosf(a) * (r + 6), y + sinf(a) * (r + 6), 0);
  }
}
static void wxMoon(int x, int y, const ShowItem &it, uint32_t frame) {
  cg.rgb(0xE6E6C8); cg.fillCircle(x, y, 10, 0);
  cg.rgb(it.bg); cg.fillCircle(x + 6, y - 4, 9, 0);
  for (int i = 0; i < 4; i++) pxAdd(x - 14 + i * 9, y - 12 + (i * 7) % 20, 0xFFFFFF, sin8(frame * 4 + i * 64));
}
static void wxIcon(int kind, bool day, const ShowItem &it, uint32_t frame) {
  const int cx = 23, cy = 22;
  int drift = (sin8(frame * 2) - 128) / 64;
  switch (kind) {
    case WX_SUN: if (day) wxSun(cx, cy, 9, frame); else wxMoon(cx, cy, it, frame); break;
    case WX_PARTLY:
      if (day) wxSun(cx - 6, cy - 6, 7, frame); else wxMoon(cx - 5, cy - 6, it, frame);
      wxCloud(cx + 3 + drift, cy + 5, 0xDCDCE6); break;
    case WX_CLOUD: wxCloud(cx - 4 - drift, cy - 4, 0x8C8C9B); wxCloud(cx + 3 + drift, cy + 4, 0xDCDCE6); break;
    case WX_FOG:
      wxCloud(cx, cy - 4, 0x8C8C9B);
      for (int i = 0; i < 3; i++) { cg.rgb(0xB4B4BE); cg.drawFastHLine(cx - 14 + ((frame / 6 + i * 3) % 6), cy + 9 + i * 4, 24, 0); }
      break;
    case WX_RAIN: case WX_SNOW: case WX_STORM: {
      for (int i = 0; i < 6; i++) {
        int x = cx - 11 + i * 4 + (i & 1), y = cy + 6 + (int)((frame * (kind == WX_SNOW ? 1 : 3) / 2 + i * 7) % 16);
        if (kind == WX_SNOW) { px(x, y, 0xFFFFFF); pxAdd(x - 1, y, 0xFFFFFF, 90); pxAdd(x + 1, y, 0xFFFFFF, 90); }
        else { px(x, y, 0x50A0FF); px(x, y + 1, 0x50A0FF); pxAdd(x, y + 2, 0x50A0FF, 100); }
      }
      if (kind == WX_STORM && (frame / 4) % 10 < 2) {
        cg.rgb(0xFFF050);
        cg.drawLine(cx + 2, cy + 4, cx - 3, cy + 13, 0); cg.drawLine(cx - 3, cy + 13, cx + 2, cy + 13, 0); cg.drawLine(cx + 2, cy + 13, cx - 3, cy + 22, 0);
      }
      wxCloud(cx, cy - 4, kind == WX_STORM ? 0x6E6E7D : 0xAAAAB9);
      break;
    }
  }
}
// 5-day forecast: 5 columns of 25 px: weekday, small icon, high (item color), low (gray).
static void wxMini(int kind, int cx, int cy, uint32_t frame) {
  auto cloud = [&](int x, int y, uint32_t c) {
    cg.rgb(c); cg.fillCircle(x - 4, y + 1, 3, 0); cg.fillCircle(x + 1, y - 2, 4, 0); cg.fillCircle(x + 5, y + 1, 3, 0); cg.fillRect(x - 4, y + 1, 9, 4, 0);
  };
  auto sun = [&](int x, int y, int r) {
    cg.rgb(0xFFC828); cg.fillCircle(x, y, r, 0);
    for (int i = 0; i < 8; i++) { float a = i * 0.7854f + frame * 0.02f; px(x + lroundf(cosf(a) * (r + 2)), y + lroundf(sinf(a) * (r + 2)), 0xFFC828); }
  };
  switch (kind) {
    case WX_SUN: sun(cx, cy, 4); break;
    case WX_PARTLY: sun(cx - 3, cy - 3, 3); cloud(cx + 2, cy + 2, 0xDCDCE6); break;
    case WX_CLOUD: cloud(cx - 2, cy - 2, 0x8C8C9B); cloud(cx + 2, cy + 2, 0xDCDCE6); break;
    case WX_FOG: cloud(cx, cy - 2, 0x8C8C9B); cg.rgb(0xB4B4BE); cg.drawFastHLine(cx - 6, cy + 5, 12, 0); cg.drawFastHLine(cx - 4, cy + 7, 12, 0); break;
    default: {
      for (int i = 0; i < 3; i++) {
        int x = cx - 4 + i * 4, y = cy + 4 + (int)((frame / (kind == WX_SNOW ? 4 : 2) + i * 3) % 6);
        px(x, y, kind == WX_SNOW ? 0xFFFFFF : 0x50A0FF);
      }
      if (kind == WX_STORM && (frame / 4) % 10 < 2) { cg.rgb(0xFFF050); cg.drawLine(cx + 1, cy + 2, cx - 2, cy + 7, 0); cg.drawLine(cx - 2, cy + 7, cx + 1, cy + 7, 0); cg.drawLine(cx + 1, cy + 7, cx - 2, cy + 11, 0); }
      cloud(cx, cy - 2, kind == WX_STORM ? 0x6E6E7D : 0xAAAAB9);
    }
  }
}
static void playForecast(const ShowItem &it, uint32_t frame) {
  static const char *const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int today = 0;
  if (timeValid()) { time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); today = lt.tm_wday; }
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis();
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    setFont(1); cg.setTextSize(1);
    for (int i = 0; i < wx.fcN; i++) {
      int cx = 2 + i * 25 + 12;
      String d = DOW[(today + i) % 7];
      String hi = String((int)lroundf(wx.fcHi[i])), lo = String((int)lroundf(wx.fcLo[i]));
      cg.rgb(0xFFBE3C); cg.setCursor(cx - textWidth(d) / 2, 1); cg.print(d);
      wxMini(wxKind(wx.fcCode[i]), cx, 19, frame + i * 7);
      cg.rgb(it.rainbow ? hsv((millis() / 8 + i * 300) % 1536) : it.fg); cg.setCursor(cx - textWidth(hi) / 2, 35); cg.print(hi);
      cg.rgb(0x8C8CA0); cg.setCursor(cx - textWidth(lo) / 2, 45); cg.print(lo);
    }
    String place = upcase(it.text);
    if (place.length() > 21) place = place.substring(0, 21);
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(place)) / 2, 56); cg.print(place);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
static void playWeather(const ShowItem &it) {
  if (!weatherFetch(it)) return;
  effectInit(it); FxGuard fxg;
  String temp = String((int)lroundf(wx.temp));
  String line2 = "H" + String((int)lroundf(wx.hi)) + " L" + String((int)lroundf(wx.lo)) + " " + upcase(it.text);
  if (line2.length() > 21) line2 = line2.substring(0, 21);
  String word = wxWord(wx.code);
  int kind = wxKind(wx.code);
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    wxIcon(kind, wx.day, it, frame);
    uint8_t f = it.font < 3 ? 3 : it.font;
    setFont(f);
    if (textWidth(temp) > 70) { f = 3; setFont(3); }
    int tw = textWidth(temp), x = 48 + (80 - tw - 8) / 2, base = textBaseline(f, 2, 40);
    int16_t x1, y1; uint16_t bw, bh;
    cg.getTextBounds("0", 0, base, &x1, &y1, &bw, &bh);
    drawText(temp, x, base, it, millis());
    cg.drawCircle(x + tw + 4, y1 + 3, 2, 0); cg.drawCircle(x + tw + 4, y1 + 3, 1, 0);   // degree sign
    setFont(1); cg.setTextSize(1);
    cg.rgb(0xFFBE3C); cg.setCursor(48 + (80 - textWidth(word)) / 2, 44); cg.print(word);
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(line2)) / 2, 55); cg.print(line2);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
  if (it.forecast && wx.fcN) playForecast(it, frame);   // same background effect carries on
}

void playItem(const ShowItem &it) {
  if (it.type != 'p') Serial.printf("  %c %-10s free %u, largest block %u\n", it.type, FX_NAMES[it.effect], (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  switch (it.type) {
    case 'f': playFx(it); break;
    case 'w': playWeather(it); break;
    case 't': playText(it); break;
    case 'c': playClock(it); break;
    case 'd': playCountdown(it); break;
    default: {
      String l = it.file; l.toLowerCase();
      if (l.endsWith(".pjm")) playPjm(it); else playOne(it.file, it.sec);
    }
  }
}
