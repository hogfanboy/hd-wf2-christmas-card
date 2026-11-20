// The show: an ordered list of items, each shown for a number of seconds.
//   /show.txt, one item per line, fields separated by TABs (written by the web page):
//     pic       <sec> <file in /gifs>
//     text      <sec> <fg> <bg> <font 1-4> <effect> <message>
//     clock     <sec> <fg> <bg> <font> <effect> <12|24>
//     countdown <sec> <fg> <bg> <font> <effect> <MM-DD> <label>
//   sec 0 = automatic (pictures: 8-20 s by their own length; scrolling text: one pass; others: 8 s).
//   fg/bg are RRGGBB hex, fg may be "rainbow". effect: none | snow | sparkle.
//   Without /show.txt every file in /gifs plays in name order (the old behavior).
// Picture files: .gif (AnimatedGIF) or .pjm = full-color JPEG frames:
//   "PJM1", then per frame: u32 jpeg length, u16 delay ms, u16 0, the JPEG (128x64 baseline).
// Time (for clock/countdown/night mode) comes from NTP once home WiFi is up; the time zone is a POSIX TZ
// string saved by the web page.
#include <JPEGDEC.h>
#include <time.h>
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

static bool isPicture(const String &n) { String l = n; l.toLowerCase(); return l.endsWith(".gif") || l.endsWith(".pjm"); }
static uint32_t hex(const String &s) { return strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF; }

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
      if (f.size() < 3) continue;
      ShowItem it;
      it.sec = f[1].toInt();
      if (f[0] == "pic") { it.type = 'p'; it.file = "/gifs/" + f[2]; if (!LittleFS.exists(it.file)) continue; }
      else {
        if (f.size() < 7) continue;
        it.type = f[0] == "text" ? 't' : f[0] == "clock" ? 'c' : f[0] == "countdown" ? 'd' : 0;
        if (!it.type) continue;
        it.rainbow = f[2] == "rainbow";
        it.fg = it.rainbow ? 0xFFFFFF : hex(f[2]);
        it.bg = hex(f[3]);
        it.font = constrain((int)f[4].toInt(), 1, 4);
        it.effect = f[5] == "snow" ? 1 : f[5] == "sparkle" ? 2 : 0;
        if (it.type == 't') it.text = f[6];
        if (it.type == 'c') it.extra = f[6];
        if (it.type == 'd') { it.extra = f[6]; it.text = f.size() > 7 ? f[7] : ""; }
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

// ---------- Background effects ----------
struct Flake { int16_t x, y; uint8_t spd, bri; };
static Flake flakes[48];
static void effectInit() { for (auto &f : flakes) { f.x = random(W * 16); f.y = random(H * 16); f.spd = 4 + random(10); f.bri = 90 + random(166); } }
static void paintBackground(const ShowItem &it, uint32_t frame) {
  uint8_t br = it.bg >> 16, bgg = it.bg >> 8, bb = it.bg;
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { canvas[y][x][0] = br; canvas[y][x][1] = bgg; canvas[y][x][2] = bb; }
  if (it.effect == 1) {
    for (auto &f : flakes) {
      f.y += f.spd; f.x = (f.x + (((frame + f.spd) % 16 < 8) ? 1 : -1) + W * 16) % (W * 16);
      if (f.y >= H * 16) { f.y = 0; f.x = random(W * 16); }
      int x = (f.x / 16 + W) % W, y = f.y / 16;
      canvas[y][x][0] = canvas[y][x][1] = canvas[y][x][2] = f.bri;
    }
  } else if (it.effect == 2) {
    for (int i = 0; i < 48; i++) {
      Flake &f = flakes[i];
      int phase = (frame * f.spd / 4 + i * 37) % 64;                 // twinkle up and down
      uint8_t v = phase < 32 ? phase * 8 : (63 - phase) * 8;
      if (phase == 63) { f.x = random(W * 16); f.y = random(H * 16); }
      uint32_t c = hsv((i * 200) % 1536);
      int x = f.x / 16, y = f.y / 16;
      canvas[y][x][0] = ((c >> 16) & 255) * v / 255; canvas[y][x][1] = ((c >> 8) & 255) * v / 255; canvas[y][x][2] = (c & 255) * v / 255;
    }
  }
}

// ---------- Text / clock / countdown items ----------
static String upcase(String s) { s.toUpperCase(); return s; }

static void playText(const ShowItem &it) {
  setFont(it.font);
  effectInit();
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
  effectInit();
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

static void playCountdown(const ShowItem &it) {
  if (!timeValid() || it.extra.length() < 5) return;
  effectInit();
  int days = daysUntil(it.extra);
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
  JPEGDEC *jpeg = new JPEGDEC();
  const size_t CAP = 24576;
  uint8_t *buf = (uint8_t *)malloc(CAP);
  if (!jpeg || !buf) { delete jpeg; free(buf); f.close(); Serial.println("  no memory for jpeg"); return; }
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

void playItem(const ShowItem &it) {
  switch (it.type) {
    case 't': playText(it); break;
    case 'c': playClock(it); break;
    case 'd': playCountdown(it); break;
    default: {
      String l = it.file; l.toLowerCase();
      if (l.endsWith(".pjm")) playPjm(it); else playOne(it.file, it.sec);
    }
  }
}
