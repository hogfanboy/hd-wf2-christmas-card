// Radar probe v2: a real precipitation radar map as a weather screen, with a map underneath and a loop.
//
// What it shows (128x64, ~145 x 72 miles around home, 1.1 mi per LED at 42 N):
//  - RainViewer radar, the last hour as a loop (7 frames, 10 min apart), newest frame held with a blinking home
//  - a dim basemap under it: lake/sea shore lines + dim water, interstates, state lines (uploaded, see below)
//  - white home cross + town name, dotted 25-mile ring with its label, radar clock time in the top-right corner
//    (yellow = newest frame, gray = older frames of the loop)
//
// Radar: api.rainviewer.com/public/weather-maps.json + one 256x256 tile per frame, centered on home
// (.../256/7/{lat}/{lon}/2/0_1.png, zoom 7 = free tier max), plain HTTP (no TLS). Decoded with PNGdec line by line,
// the middle 256x128 shrunk 2:1 keeping the strongest pixel. The tile streams from the socket through an 8 KB
// window (no buffer for the whole file). Each LED becomes one of 15 levels (10 rain, 5 snow), 4 bits, so a frame
// is 4 KB and the loop 28 KB. Only frames not seen yet are fetched (normally 1 per 10 min).
// RainViewer free API: personal use, credit "RainViewer" required (on the page), 100 requests/min per IP.
//
// Basemap: the card can't fetch map tiles itself (OSM is HTTPS with a GlobalSign chain, and its tile policy is
// not for devices; CARTO now needs a key). A map only changes with the town, so it is made elsewhere and
// uploaded: POST /base (multipart, field "base") = 8192 bytes, one class per LED, row by row:
// 0 nothing, 1 water, 2 shore, 3 interstate, 4 state line. makebase.py (next to this file) makes it on the PC from
// OSM tiles; in the real firmware the page would do the same in the browser (OSM tiles allow CORS).
//
// HTTP (port 80): GET / status, GET /log recent serial log, POST /base, GET /restart (back to Pocket Jumbotron),
// GET /home?lat=&lon=&name=&tz= (another place, RAM only; upload its basemap after).
// Rollback: the probe never confirms itself, so any restart (GET /restart, hold S1 3 s, unplug) boots the
// firmware it was installed from again. Card 2 (5df8) runs it; put it on with curl .../api/update as usual.
// WiFi, home ("pj" wxhome "lat,lon,name"), time zone and brightness come from the main firmware's NVS (read only).
// Memory: PNGdec is one 45 KB object (32 KB zlib window). Measured with v1 inside the real firmware's budget:
// it fits after playerFree(), but the largest block left can be only 46 KB. Here (no player): free heap 121 KB
// before a refresh, lowest 28.8 KB while fetching 7 heavy-rain frames.
// Build: main firmware's FQBN + upload.maximum_size; needs PNGdec (arduino-cli lib install "PNGdec@1.1.6").

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PNGdec.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
#include "esp_ota_ops.h"

struct RGB { uint8_t r, g, b; };            // (structs above the first function: Arduino puts its prototypes there)
struct Frame { uint32_t time; char path[28]; uint8_t *px; uint16_t len; };   // see "Radar frames"

extern "C" bool verifyRollbackLater() { return true; }   // stay unconfirmed: any restart = back to Pocket Jumbotron

static const HUB75_I2S_CFG::i2s_pins PINS = {
  /*R1*/ 2, /*G1*/ 6, /*B1*/ 10, /*R2*/ 3, /*G2*/ 7, /*B2*/ 11,
  /*A*/ 39, /*B*/ 38, /*C*/ 37, /*D*/ 36, /*E*/ 21, /*LAT*/ 33, /*OE*/ 35, /*CLK*/ 34};
static const int W = 128, H = 64;
static const uint8_t BUTTON_PIN = 17;
static const uint8_t MAX_BRIGHTNESS = 140;

// ---- Look (the user judges these by eye) ----
static const uint8_t NF = 7;                 // frames in the loop (10 min apart: the last hour)
static const uint16_t FRAME_MS = 450, HOLD_MS = 3000;   // loop speed, how long the newest frame stays
static const uint8_t MIN_ALPHA = 96;         // fainter radar stays black/map
static const uint8_t RING_MILES = 25;

static const RGB C_WATER = {0, 10, 36}, C_SHORE = {0, 60, 130}, C_ROAD = {44, 44, 44}, C_STATE = {70, 0, 70},
                 C_RING = {60, 60, 60}, C_HOME = {255, 255, 255}, C_NAME = {255, 255, 255},
                 C_TIME_NEW = {255, 200, 60}, C_TIME_OLD = {120, 120, 120}, C_SCALE = {120, 120, 120};
static const float DEFAULT_LAT = 41.786f, DEFAULT_LON = -88.147f;
static const uint32_t TILE_MAX = 60000;

MatrixPanel_I2S_DMA *panel = nullptr;
WebServer web(80);
static float homeLat = DEFAULT_LAT, homeLon = DEFAULT_LON;
static String townName = "HOME";
static uint8_t baseMap[H][W];                // classes, 0 = nothing (until /base is uploaded)
static bool baseLoaded = false;

// ---- Log: Serial + a ring buffer for GET /log (card 2 isn't on USB) ----
static char logBuf[4096]; static size_t logLen = 0;
static void logf(const char *fmt, ...) {
  char line[200]; va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
  if (n < 0) return; if (n > (int)sizeof(line) - 1) n = sizeof(line) - 1;
  Serial.print(line);
  if (logLen + n >= sizeof(logBuf)) { size_t drop = logLen + n - sizeof(logBuf) + 1024; memmove(logBuf, logBuf + drop, logLen - drop); logLen -= drop; }
  memcpy(logBuf + logLen, line, n); logLen += n;
}
static void heapLog(const char *what) {
  logf("  [heap] %-22s free %6u  largest %6u  min %6u\n", what, (unsigned)ESP.getFreeHeap(),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)ESP.getMinFreeHeap());
}

// ---- Panel (same setup as firmware/pocket_jumbotron/panel.ino) ----
static void panelRegWrite(const bool reg[16], int latClocks) {
  const uint8_t steal[] = {PINS.r1, PINS.g1, PINS.b1, PINS.r2, PINS.g2, PINS.b2, PINS.clk, PINS.lat, PINS.oe};
  const uint8_t data[] = {PINS.r1, PINS.g1, PINS.b1, PINS.r2, PINS.g2, PINS.b2};
  uint32_t saved[sizeof(steal)];
  gpio_set_level((gpio_num_t)PINS.oe, 1); gpio_set_level((gpio_num_t)PINS.lat, 0); gpio_set_level((gpio_num_t)PINS.clk, 0);
  for (int i = 0; i < (int)sizeof(steal); i++) { saved[i] = GPIO.func_out_sel_cfg[steal[i]].val; GPIO.func_out_sel_cfg[steal[i]].val = 0x100; }
  for (int l = 0; l < W; l++) {
    for (uint8_t p : data) gpio_set_level((gpio_num_t)p, reg[l % 16]);
    if (l == W - latClocks) gpio_set_level((gpio_num_t)PINS.lat, 1);
    gpio_set_level((gpio_num_t)PINS.clk, 1); gpio_set_level((gpio_num_t)PINS.clk, 0);
  }
  gpio_set_level((gpio_num_t)PINS.lat, 0);
  for (uint8_t p : data) gpio_set_level((gpio_num_t)p, 0);
  gpio_set_level((gpio_num_t)PINS.clk, 1); gpio_set_level((gpio_num_t)PINS.clk, 0);
  for (int i = 0; i < (int)sizeof(steal); i++) GPIO.func_out_sel_cfg[steal[i]].val = saved[i];
}
static void panelStart(uint8_t pct) {
  HUB75_I2S_CFG cfg(W, H, 1, PINS);
  cfg.driver = HUB75_I2S_CFG::DP3246;
  panel = new MatrixPanel_I2S_DMA(cfg);        // single buffer: double costs ~100 KB, the frames need it
  if (!panel->begin()) { Serial.println("panel begin failed"); return; }
  GPIO.func_out_sel_cfg[PINS.clk].inv_sel = 0;
  static const bool REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
  static const bool REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};
  static const bool GAIN[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  static const bool R14[16]  = {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};
  panelRegWrite(REG1, 12); panelRegWrite(REG2, 13); panelRegWrite(GAIN, 11); panelRegWrite(R14, 14);
  panel->setBrightness8((uint16_t)MAX_BRIGHTNESS * pct / 100);
  panel->clearScreen();
}
// Single buffer: each picture is composed in a canvas and every LED written once (no layer flashing).
static uint16_t cv[H][W];
static void put(int x, int y, RGB c) {
  if (x >= 0 && x < W && y >= 0 && y < H) cv[y][x] = ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
}
static void blit() {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint16_t v = cv[y][x]; uint8_t r = v >> 11, g = (v >> 5) & 63, b = v & 31;
      panel->drawPixelRGB888(x, y, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
    }
}

// ---- 3x5 font for the small labels (crisp, 1 px gap) ----
static uint16_t glyph(char c) {
  static const char *chars = "0123456789:- .ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const uint16_t bits[] = {
    0x7B6F,0x2C97,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7292,0x7BEF,0x7BCF,0x0410,0x01C0,0x0000,0x0002,
    0x2BED,0x6BAE,0x3923,0x6B6E,0x79A7,0x79A4,0x396B,0x5BED,0x7497,0x126A,0x5BAD,0x4927,0x5FED,0x6B6D,0x2B6A,
    0x6BA4,0x2B73,0x6BAD,0x388E,0x7492,0x5B6F,0x5B6A,0x5BFD,0x5AAD,0x5A92,0x72A7};
  const char *p = strchr(chars, toupper(c));
  return p ? bits[p - chars] : 0;
}
static int textW(const String &s) { return s.length() * 4 - 1; }
static void text(const String &s, int x, int y, RGB c, bool box = true) {
  if (box) for (int yy = y - 1; yy < y + 6; yy++) for (int xx = x - 1; xx <= x + textW(s); xx++) put(xx, yy, {0, 0, 0});
  for (size_t i = 0; i < s.length(); i++) {
    uint16_t g = glyph(s[i]);
    for (int k = 0; k < 15; k++) if (g & (0x4000 >> k)) put(x + i * 4 + k % 3, y + k / 3, c);
  }
}

// ---- Radar frames ----
// A frame = one of 15 radar levels per LED, 4 bits each (4 KB; 7 frames = 28 KB). Each tile color goes to the
// nearest RainViewer "Universal Blue" level (rainviewer.com/files/rainviewer_api_colors_table.csv), shown in a
// saturated LED color. Decoded into the canvas memory (idle while fetching), then packed.
// (struct Frame is at the top)
static Frame frames[NF];                     // oldest first
static uint8_t nFrames = 0;
struct Level { RGB match, led; };
static const Level LV[16] = {
  {{0, 0, 0}, {0, 0, 0}},                                   // 0 = no radar
  {{222, 208, 151}, {120, 96, 48}},                         // drizzle (tan, < 15 dBZ)
  {{108, 209, 235}, {90, 210, 255}},                        // light rain
  {{0, 163, 224}, {0, 140, 255}},                           // rain
  {{0, 90, 140}, {0, 60, 200}},                             // moderate rain (30 dBZ)
  {{255, 224, 0}, {255, 220, 0}},                           // heavy (yellow, 35)
  {{255, 149, 0}, {255, 130, 0}},                           // orange (40)
  {{235, 40, 0}, {255, 30, 0}},                             // red (45)
  {{143, 0, 0}, {150, 0, 0}},                               // dark red (50)
  {{255, 120, 255}, {255, 80, 255}},                        // pink: hail/extreme (55)
  {{255, 255, 255}, {255, 255, 255}},                       // white (65+)
  {{199, 255, 255}, {190, 255, 255}},                       // snow light
  {{152, 216, 255}, {140, 190, 255}},                       // snow
  {{88, 149, 255}, {90, 120, 255}},                         // snow moderate
  {{34, 98, 255}, {40, 70, 255}},                           // snow heavy
  {{0, 0, 255}, {0, 0, 255}}};                              // snow very heavy
static uint8_t levelOf(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t best = 1; uint32_t bd = UINT32_MAX;
  for (uint8_t i = 1; i < 16; i++) {
    int dr = LV[i].match.r - r, dg = LV[i].match.g - g, db = LV[i].match.b - b; uint32_t d = dr * dr + dg * dg + db * db;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}
static PNG *png = nullptr;
static uint8_t *decodeTo = nullptr;
static uint8_t best[W][4];
static int drawLine(PNGDRAW *d) {
  const int y0 = (png->getHeight() - 2 * H) / 2, y = d->y - y0;
  if (y < 0 || y >= 2 * H) return 1;
  const int x0 = (d->iWidth - 2 * W) / 2;
  if ((y & 1) == 0) memset(best, 0, sizeof(best));
  for (int X = 0; X < W; X++)
    for (int k = 0; k < 2; k++) {
      int x = x0 + X * 2 + k;
      if (x < 0 || x >= d->iWidth) continue;
      uint8_t r, g, b, a;
      if (d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA && d->iBpp == 8) { const uint8_t *p = d->pPixels + x * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
      else if (d->iPixelType == PNG_PIXEL_INDEXED) {
        int i = d->iBpp == 8 ? d->pPixels[x] : d->iBpp == 4 ? (d->pPixels[x >> 1] >> ((~x & 1) * 4)) & 15 : 0;
        r = d->pPalette[i * 3]; g = d->pPalette[i * 3 + 1]; b = d->pPalette[i * 3 + 2]; a = d->iHasAlpha ? d->pPalette[768 + i] : 255;
      } else return 0;
      if (a > best[X][3]) { best[X][0] = r; best[X][1] = g; best[X][2] = b; best[X][3] = a; }
    }
  if (y & 1) {
    uint8_t *row = decodeTo + (y >> 1) * W;
    for (int X = 0; X < W; X++) row[X] = best[X][3] >= MIN_ALPHA ? levelOf(best[X][0], best[X][1], best[X][2]) : 0;
  }
  return 1;
}
// The tile is decoded straight from the network, without a buffer for the whole file: PNGdec reads up to 2 KB at
// a time and only ever seeks back inside the block it just read, so a sliding window that starts at the last read
// position is enough. Window = the second 8 KB of the canvas (idle while fetching).
static struct { WiFiClient *c; int32_t size, start, len; uint8_t *buf; } net;
static const int32_t WIN = 8192;
static void *netOpen(const char *, int32_t *size) { *size = net.size; return &net; }
static void netClose(void *) {}
static int32_t netSeek(PNGFILE *f, int32_t pos) { f->iPos = pos; return pos; }
static int32_t netRead(PNGFILE *f, uint8_t *dst, int32_t len) {
  int32_t pos = f->iPos;
  if (pos < net.start || pos >= net.size) return 0;          // (never happens: PNGdec doesn't seek back further)
  if (len > net.size - pos) len = net.size - pos;
  if (pos + len > net.start + WIN) {                          // make room: the window now starts at this read
    int32_t keep = net.start + net.len - pos;
    if (keep > 0) memmove(net.buf, net.buf + (pos - net.start), keep);
    else {                                                    // skipping ahead: drop the bytes in between
      for (int32_t skip = -keep; skip > 0;) { uint8_t tmp[256]; int n = net.c->readBytes(tmp, min<int32_t>(skip, sizeof(tmp))); if (n <= 0) return 0; skip -= n; }
      keep = 0;
    }
    net.start = pos; net.len = keep;
  }
  uint32_t t = millis();
  while (net.start + net.len < pos + len && millis() - t < 8000) {
    int n = net.c->readBytes(net.buf + net.len, min<int32_t>(WIN - net.len, pos + len - net.start - net.len));
    if (n > 0) { net.len += n; t = millis(); } else delay(2);
  }
  int32_t have = min<int32_t>(len, net.start + net.len - pos);
  if (have <= 0) return 0;
  memcpy(dst, net.buf + (pos - net.start), have);
  f->iPos = pos + have;
  return have;
}
static bool fetchTile(const char *path, Frame &f) {
  uint8_t *dst = (uint8_t *)cv;              // first 8 KB of the canvas: one level per LED
  char url[160];
  snprintf(url, sizeof(url), "http://tilecache.rainviewer.com%s/256/7/%.4f/%.4f/2/0_1.png", path, homeLat, homeLon);
  uint32_t t0 = millis();
  HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(6000);
  http.begin(url);
  int code = http.GET(), len = http.getSize();
  uint32_t t1 = millis();
  bool ok = false;
  if (code == 200 && len > 0 && len <= (int)TILE_MAX) {
    net = {http.getStreamPtr(), len, 0, 0, dst + W * H};
    decodeTo = dst;
    if (png->open("tile", netOpen, netClose, netRead, netSeek, drawLine) == PNG_SUCCESS) { ok = png->decode(nullptr, 0) == PNG_SUCCESS; png->close(); }
  }
  http.end();
  int lit = 0;
  if (ok && (f.px = (uint8_t *)malloc(W * H / 2))) {           // pack two LEDs per byte, high nibble first
    for (int i = 0; i < W * H; i += 2) { f.px[i >> 1] = (dst[i] << 4) | dst[i + 1]; lit += (dst[i] != 0) + (dst[i + 1] != 0); }
    f.len = W * H / 2;
  } else ok = false;
  logf("  tile %s: HTTP %d, %d B, request %u ms, stream+decode %u ms, %d LEDs lit %s\n", path, code, len, (unsigned)(t1 - t0),
       (unsigned)(millis() - t1), lit, ok ? "" : "FAILED");
  return ok;
}
// Reads the frame list and fetches the frames not seen yet. Returns true if anything changed.
static bool radarRefresh() {
  uint32_t t0 = millis();
  HTTPClient http; http.setConnectTimeout(5000); http.setTimeout(6000);
  http.begin("http://api.rainviewer.com/public/weather-maps.json");
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  int past = body.indexOf("\"past\""), end = body.indexOf(']', past);
  if (code != 200 || past < 0 || end < 0) { logf("maps.json: HTTP %d\n", code); return false; }
  uint32_t times[16]; String paths[16]; int n = 0;
  for (int p = past; n < 16;) {
    int t = body.indexOf("\"time\":", p); if (t < 0 || t > end) break;
    int q = body.indexOf("\"path\":\"", t); if (q < 0 || q > end) break;
    times[n] = body.substring(t + 7).toInt(); paths[n] = body.substring(q + 8, body.indexOf('"', q + 8)); n++; p = q + 8;
  }
  int first = n > NF ? n - NF : 0;
  bool changed = false;
  Frame next[NF]; uint8_t nn = 0;
  for (int i = first; i < n; i++) {                       // keep the frames we have, in order
    Frame f = {times[i], "", nullptr, 0}; strlcpy(f.path, paths[i].c_str(), sizeof(f.path));
    for (uint8_t j = 0; j < nFrames; j++) if (frames[j].px && !strcmp(frames[j].path, f.path)) { f.px = frames[j].px; f.len = frames[j].len; frames[j].px = nullptr; }
    next[nn++] = f;
  }
  for (uint8_t j = 0; j < nFrames; j++) { free(frames[j].px); frames[j].px = nullptr; }   // dropped (too old)
  heapLog("before PNG");
  png = new (std::nothrow) PNG();
  if (!png) logf("  !! no memory for PNGdec\n");
  int fetched = 0;
  for (uint8_t i = 0; i < nn && png; i++) {
    if (next[i].px) continue;
    if (fetchTile(next[i].path, next[i])) { fetched++; changed = true; }
  }
  uint32_t packed = 0; for (uint8_t i = 0; i < nn; i++) if (next[i].px) packed += next[i].len;
  delete png; png = nullptr;
  nFrames = 0;
  for (uint8_t i = 0; i < nn; i++) if (next[i].px) frames[nFrames++] = next[i];
  heapLog("after refresh");
  logf("radar: %d frames listed, %u kept (%u B), %d fetched, %u ms\n", n, nFrames, (unsigned)packed, fetched, (unsigned)(millis() - t0));
  return changed;
}

// ---- Drawing ----
static float milesPerLed() { return 2 * 156543.03f / 128 * cosf(homeLat * DEG_TO_RAD) / 1609.34f; }
static uint8_t ringMask[H][W / 8];           // bit set = dotted ring pixel
static void makeRing() {
  memset(ringMask, 0, sizeof(ringMask));
  float r = RING_MILES / milesPerLed();
  for (int t = 0; t < 1440; t++) {
    float a = t * TWO_PI / 1440; int x = lroundf(W / 2 + r * cosf(a)), y = lroundf(H / 2 + r * sinf(a));
    if (x >= 0 && x < W && y >= 0 && y < H && ((x + y) & 1) == 0) ringMask[y][x >> 3] |= 0x80 >> (x & 7);
  }
}
static String clockText(uint32_t t) {
  time_t tt = t; struct tm lt; localtime_r(&tt, &lt);
  char s[8]; snprintf(s, sizeof(s), "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
  return s;
}
static void drawFrame(int fi, bool homeOn) {
  const uint8_t *px = fi >= 0 && fi < nFrames ? frames[fi].px : nullptr;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      RGB c = {0, 0, 0};
      uint8_t k = baseMap[y][x];
      if (k == 1) c = C_WATER; else if (k == 2) c = C_SHORE; else if (k == 3) c = C_ROAD; else if (k == 4) c = C_STATE;
      else if (ringMask[y][x >> 3] & (0x80 >> (x & 7))) c = C_RING;
      if (px) { int i = y * W + x; uint8_t lv = (px[i >> 1] >> ((~i & 1) * 4)) & 15; if (lv) c = LV[lv].led; }
      put(x, y, c);
    }
  const int cx = W / 2, cy = H / 2;
  const RGB K = {0, 0, 0};
  for (int d = -3; d <= 3; d++) { put(cx + d, cy - 1, K); put(cx + d, cy + 1, K); put(cx - 1, cy + d, K); put(cx + 1, cy + d, K); }   // black ring
  if (homeOn) for (int d = -2; d <= 2; d++) { put(cx + d, cy, C_HOME); put(cx, cy + d, C_HOME); }
  else put(cx, cy, K);
  String nm = townName; nm.toUpperCase(); if (nm.length() > 14) nm = nm.substring(0, 14);
  text(nm, constrain(cx - textW(nm) / 2, 1, W - 1 - textW(nm)), cy - 10, C_NAME);
  int r = lroundf(RING_MILES / milesPerLed());
  String sc = String(RING_MILES) + " MI";
  if (cx + r + 2 + textW(sc) < W) text(sc, cx + r + 2, cy - 2, C_SCALE); else text(sc, 1, H - 6, C_SCALE);
  if (px) {
    String t = clockText(frames[fi].time);
    text(t, W - textW(t) - 1, 1, fi == nFrames - 1 ? C_TIME_NEW : C_TIME_OLD);
  } else text("NO RADAR", W - textW("NO RADAR") - 1, 1, C_TIME_OLD);
  blit();
}
static volatile bool refreshNow = false;
static void setHome(float lat, float lon, const String &name) {
  homeLat = lat; homeLon = lon; townName = name;
  for (uint8_t j = 0; j < nFrames; j++) { free(frames[j].px); frames[j].px = nullptr; }
  nFrames = 0; memset(baseMap, 0, sizeof(baseMap)); baseLoaded = false;
  makeRing();
  logf("home now %.4f,%.4f %s (%.2f mi per LED); upload its basemap\n", homeLat, homeLon, townName.c_str(), milesPerLed());
  refreshNow = true;
}

// ---- Web ----
static void webSetup() {
  web.on("/", []() {
    String s = "radar probe v2\nhome " + String(homeLat, 4) + "," + String(homeLon, 4) + " " + townName + "\nframes " + String(nFrames) +
               "\nbasemap " + (baseLoaded ? "yes" : "no") + "\nfree heap " + String(ESP.getFreeHeap()) + "\n";
    web.send(200, "text/plain", s);
  });
  web.on("/log", []() { web.setContentLength(logLen); web.send(200, "text/plain", ""); web.sendContent(logBuf, logLen); });
  web.on("/home", []() {                     // test somewhere else (RAM only): /home?lat=47.6&lon=-122.3&name=Seattle
    if (web.hasArg("tz")) { setenv("TZ", web.arg("tz").c_str(), 1); tzset(); }   // e.g. CST6CDT,M3.2.0,M11.1.0
    setHome(web.arg("lat").toFloat(), web.arg("lon").toFloat(), web.arg("name"));
    web.send(200, "text/plain", "ok\n");
  });
  web.on("/restart", []() { web.send(200, "text/plain", "restarting -> Pocket Jumbotron\n"); delay(300); ESP.restart(); });
  static size_t got = 0;
  web.on("/base", HTTP_POST, []() {
    baseLoaded = got == sizeof(baseMap);
    web.send(baseLoaded ? 200 : 400, "text/plain", baseLoaded ? "ok\n" : "need 8192 bytes\n");
    logf("basemap upload: %u bytes %s\n", (unsigned)got, baseLoaded ? "ok" : "BAD");
  }, []() {
    HTTPUpload &u = web.upload();
    if (u.status == UPLOAD_FILE_START) got = 0;
    else if (u.status == UPLOAD_FILE_WRITE) { size_t n = min(u.currentSize, sizeof(baseMap) - min(got, sizeof(baseMap))); memcpy((uint8_t *)baseMap + got, u.buf, n); got += u.currentSize; }
  });
  web.begin();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  logf("\nRadar probe v2\n");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK)
    logf("OTA state %d (%s)\n", (int)st, st == ESP_OTA_IMG_PENDING_VERIFY ? "pending: restart rolls back" : "NOT pending");
  Preferences prefs;
  prefs.begin("pj", true);
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", ""), home = prefs.getString("wxhome", ""), tz = prefs.getString("tz", "");
  uint8_t bright = prefs.getUChar("bright", 100);
  prefs.end();
  panelStart(bright);
  int c1 = home.indexOf(','), c2 = home.indexOf(',', c1 + 1);
  if (c1 > 0 && home.substring(0, c1).toFloat() != 0) {
    homeLat = home.substring(0, c1).toFloat(); homeLon = home.substring(c1 + 1).toFloat();
    if (c2 > 0) townName = home.substring(c2 + 1);
  }
  logf("home %.4f,%.4f %s, tz \"%s\", %.2f mi per LED\n", homeLat, homeLon, townName.c_str(), tz.c_str(), milesPerLed());
  makeRing();
  drawFrame(-1, true);
  if (!ssid.length()) { logf("no WiFi saved\n"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) { logf("WiFi failed\n"); return; }
  logf("WiFi ip %s\n", WiFi.localIP().toString().c_str());
  configTzTime(tz.length() ? tz.c_str() : "UTC0", "pool.ntp.org");
  webSetup();
  heapLog("ready");
  radarRefresh();
}

void loop() {
  web.handleClient();
  static uint32_t lastRefresh = millis(), shownAt = 0, blinkAt = 0, downAt = 0;
  static int fi = 0; static bool homeOn = true, was = true;
  bool up = digitalRead(BUTTON_PIN) == HIGH;
  if (!up && was) downAt = millis();
  if (!up && downAt && millis() - downAt > 3000) { text("RESTART", 50, 29, C_TIME_NEW); blit(); delay(300); ESP.restart(); }
  if (up) downAt = 0;
  was = up;
  if ((refreshNow || millis() - lastRefresh > 5UL * 60 * 1000) && WiFi.status() == WL_CONNECTED) {
    refreshNow = false; lastRefresh = millis(); radarRefresh(); fi = 0; shownAt = 0;
  }
  uint32_t now = millis();
  if (nFrames == 0) { if (now - shownAt > 1000) { drawFrame(-1, true); shownAt = now; } delay(5); return; }
  if (fi >= nFrames) fi = nFrames - 1;
  bool last = fi == nFrames - 1;
  if (!shownAt || now - shownAt >= (last ? HOLD_MS : FRAME_MS)) {   // loop oldest -> newest, hold the newest
    fi = !shownAt ? 0 : last ? 0 : fi + 1;
    shownAt = blinkAt = now; homeOn = true;
    drawFrame(fi, true);
  } else if (last && now - blinkAt >= 400) {                        // home blinks while the newest frame holds
    homeOn = !homeOn; blinkAt = now;
    drawFrame(fi, homeOn);
  }
  delay(5);
}
