// Radar probe: can the card show a real precipitation radar map?
// Fetches RainViewer's latest radar frame as ONE 256x256 PNG tile centered on home (zoom 7, the free tier's max),
// decodes it line by line with PNGdec, keeps the middle 256x128 and shrinks it 2:1 to 128x64 (~275 x 140 km),
// draws it on black with a white home cross, and logs heap and timing over serial. Repeats every 10 minutes
// (S1 = fetch now). RainViewer's free API asks for credit "RainViewer" and personal use only.
//
// Plain HTTP: tilecache.rainviewer.com serves the same tiles on http://, so no TLS is needed. (Its HTTPS chain
// ends at ISRG Root X2, which SITE_CA already has, if http ever goes away.)
//
// Memory: PNGdec keeps everything in one ~45 KB object (32 KB zlib window, tiles always use a 32 KB window), and
// the tile is downloaded whole into RAM (PNGdec seeks back a little while parsing chunks). In the real firmware
// that only fits after playerFree(), like the HTTPS fetches. SIM_PLAYER mimics that here: it holds blocks the size
// of the player's canvas/shown/GIF decoder plus ballast so free heap is ~75 KB (card while playing), and frees
// the player blocks just for the fetch. The number to watch is "largest block" before the PNG allocation.
//
// WiFi and home come from the main firmware's NVS ("pj": ssid, pass, wxhome "lat,lon,..."), read only, so this
// probe needs no settings. Flashing it replaces Pocket Jumbotron on that card until the real firmware goes back.
// Build: same FQBN as the main firmware; needs the PNGdec library (arduino-cli lib install "PNGdec@1.1.6").

#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PNGdec.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

static const HUB75_I2S_CFG::i2s_pins PINS = {
  /*R1*/ 2, /*G1*/ 6, /*B1*/ 10, /*R2*/ 3, /*G2*/ 7, /*B2*/ 11,
  /*A*/ 39, /*B*/ 38, /*C*/ 37, /*D*/ 36, /*E*/ 21, /*LAT*/ 33, /*OE*/ 35, /*CLK*/ 34};
static const int W = 128, H = 64;
static const uint8_t BUTTON_PIN = 17;

// ---- Probe settings ----
#define SIM_PLAYER 1                       // 1 = mimic the main firmware's memory while playing (see top)
static const uint32_t TARGET_FREE = 75000; // free heap to leave with SIM_PLAYER (card shows ~70-75 KB playing)
static const uint8_t ZOOM = 7;             // RainViewer free tier: 7 is the max (8+ returns "Zoom Level Not Supported")
static const uint8_t STEP = 2;             // source pixels per LED: 2 = 256x128 of the tile -> 128x64
static const uint8_t MIN_ALPHA = 96;       // fainter radar (light haze at the edges of rain) stays black
static const uint8_t BRIGHT = 140;         // same cap as the main firmware
static const float DEFAULT_LAT = 28.54f, DEFAULT_LON = -81.38f;   // used if the card has no weather town saved
static const uint32_t TILE_MAX = 60000;    // refuse bigger tiles (biggest seen: 26 KB, widespread rain)

MatrixPanel_I2S_DMA *panel = nullptr;
static float homeLat = DEFAULT_LAT, homeLon = DEFAULT_LON;

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
static void panelStart() {
  HUB75_I2S_CFG cfg(W, H, 1, PINS);
  cfg.driver = HUB75_I2S_CFG::DP3246;
  panel = new MatrixPanel_I2S_DMA(cfg);
  if (!panel->begin()) { Serial.println("panel begin failed"); return; }
  GPIO.func_out_sel_cfg[PINS.clk].inv_sel = 0;
  static const bool REG1[16] = {0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0};
  static const bool REG2[16] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};
  static const bool GAIN[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  static const bool R14[16]  = {0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0};
  panelRegWrite(REG1, 12); panelRegWrite(REG2, 13); panelRegWrite(GAIN, 11); panelRegWrite(R14, 14);
  panel->setBrightness8(BRIGHT);
  panel->clearScreen();
}
static void say(const char *a, const char *b = "") {
  if (!panel) return;
  panel->clearScreen();
  panel->setTextSize(1); panel->setTextWrap(false);
  panel->setTextColor(panel->color565(255, 190, 60)); panel->setCursor(2, 20); panel->print(a);
  panel->setTextColor(panel->color565(90, 200, 255)); panel->setCursor(2, 34); panel->print(b);
}

// ---- Heap ----
static void heapLog(const char *what) {
  Serial.printf("  [heap] %-26s free %6u  largest block %6u  min ever %6u\n", what, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)ESP.getMinFreeHeap());
}
#if SIM_PLAYER
static void *simPlayer[3];
static const size_t SIM_SIZES[3] = {24576, 24576, 22000};   // canvas, shown (single-buffered), AnimatedGIF object
static void simPlayerAlloc() { for (int i = 0; i < 3; i++) if (!simPlayer[i]) simPlayer[i] = malloc(SIM_SIZES[i]); }
static void simPlayerFree() { for (int i = 0; i < 3; i++) { free(simPlayer[i]); simPlayer[i] = nullptr; } }
static void simBallast() {                                    // small blocks, like the rest of the firmware's heap use
  int n = 0;
  while (ESP.getFreeHeap() > TARGET_FREE + 4096 && malloc(4096)) n++;
  while (ESP.getFreeHeap() > TARGET_FREE + 256 && malloc(256)) n++;
  Serial.printf("  ballast: %d blocks held\n", n);
}
#endif

// ---- Radar ----
// Slippy-map math: the tile is centered on home (RainViewer's lat/lon tile URL), so home = tile center.
static PNG *png = nullptr;
static uint8_t best[W][4];                 // strongest radar pixel per LED column over the current STEP lines
static uint16_t litCount;
static int drawLine(PNGDRAW *d) {
  const int srcH = H * STEP, y0 = (png->getHeight() - srcH) / 2;
  int y = d->y - y0;
  if (y < 0 || y >= srcH) return 1;
  const int srcW = W * STEP, x0 = (d->iWidth - srcW) / 2;
  if (y % STEP == 0) memset(best, 0, sizeof(best));
  for (int X = 0; X < W; X++)
    for (int k = 0; k < STEP; k++) {
      int x = x0 + X * STEP + k;
      if (x < 0 || x >= d->iWidth) continue;
      uint8_t r, g, b, a;
      if (d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA && d->iBpp == 8) {
        const uint8_t *p = d->pPixels + x * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3];
      } else if (d->iPixelType == PNG_PIXEL_INDEXED) {
        int i = d->iBpp == 8 ? d->pPixels[x] : d->iBpp == 4 ? (d->pPixels[x >> 1] >> ((~x & 1) * 4)) & 15 : 0;
        r = d->pPalette[i * 3]; g = d->pPalette[i * 3 + 1]; b = d->pPalette[i * 3 + 2];
        a = d->iHasAlpha ? d->pPalette[768 + i] : 255;
      } else return 0;                      // other formats: not what RainViewer sends
      if (a > best[X][3]) { best[X][0] = r; best[X][1] = g; best[X][2] = b; best[X][3] = a; }   // storms never vanish
    }
  if (y % STEP == STEP - 1) {
    int Y = y / STEP;
    for (int X = 0; X < W; X++) {
      bool on = best[X][3] >= MIN_ALPHA;
      if (on) litCount++;
      panel->drawPixelRGB888(X, Y, on ? best[X][0] : 0, on ? best[X][1] : 0, on ? best[X][2] : 0);
    }
  }
  return 1;
}
static void drawHome() {                    // white plus, 5 px across, black ring so it shows over rain
  const int cx = W / 2, cy = H / 2;
  for (int d = -3; d <= 3; d++) { panel->drawPixelRGB888(cx + d, cy - 1, 0, 0, 0); panel->drawPixelRGB888(cx + d, cy + 1, 0, 0, 0);
                                  panel->drawPixelRGB888(cx - 1, cy + d, 0, 0, 0); panel->drawPixelRGB888(cx + 1, cy + d, 0, 0, 0); }
  for (int d = -2; d <= 2; d++) { panel->drawPixelRGB888(cx + d, cy, 255, 255, 255); panel->drawPixelRGB888(cx, cy + d, 255, 255, 255); }
}

// Latest radar frame path from weather-maps.json (~1.5 KB): the last "/v2/radar/..." in radar.past.
static bool latestPath(String &path, uint32_t &frameTime) {
  HTTPClient http;
  http.setConnectTimeout(5000); http.setTimeout(6000);
  http.begin("http://api.rainviewer.com/public/weather-maps.json");
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  int past = body.indexOf("\"past\""), now = body.indexOf("\"nowcast\"", past);
  int p = body.lastIndexOf("\"path\":\"", now > 0 ? now : body.length());
  int t = body.lastIndexOf("\"time\":", p);
  if (code != 200 || past < 0 || p < past) { Serial.printf("  maps.json failed: HTTP %d, %u bytes\n", code, body.length()); return false; }
  path = body.substring(p + 8, body.indexOf('"', p + 8));
  frameTime = body.substring(t + 7).toInt();
  return true;
}

static void radarFetch() {
  Serial.println("\n== radar fetch");
  uint32_t t0 = millis();
  String path; uint32_t frameTime = 0;
  if (!latestPath(path, frameTime)) { say("RADAR", "no index"); return; }
  uint32_t tIndex = millis() - t0;
  char url[160];
  snprintf(url, sizeof(url), "http://tilecache.rainviewer.com%s/256/%u/%.4f/%.4f/2/0_1.png", path.c_str(), ZOOM, homeLat, homeLon);
  Serial.printf("  frame %s (%lu s old)\n  %s\n", path.c_str(), (unsigned long)(time(nullptr) > 1700000000 ? time(nullptr) - frameTime : 0), url);

#if SIM_PLAYER
  heapLog("playing (sim)");
  simPlayerFree();
#endif
  heapLog("player freed");
  uint32_t t1 = millis();
  png = new (std::nothrow) PNG();          // the big one first, while the heap is least broken up
  heapLog("PNG object allocated");
  uint8_t *tile = nullptr; int len = 0, code = -1;
  if (png) {
    HTTPClient http;
    http.setConnectTimeout(5000); http.setTimeout(6000);
    http.begin(url);
    code = http.GET();
    len = http.getSize();
    if (code == 200 && len > 0 && len <= (int)TILE_MAX && (tile = (uint8_t *)malloc(len))) {
      WiFiClient *s = http.getStreamPtr();
      int got = 0; uint32_t tr = millis();
      while (got < len && millis() - tr < 8000) {
        int n = s->readBytes(tile + got, len - got);
        if (n > 0) { got += n; tr = millis(); } else delay(5);
      }
      if (got != len) { Serial.printf("  short read %d/%d\n", got, len); free(tile); tile = nullptr; }
    }
    http.end();
  }
  uint32_t tDown = millis() - t1;
  heapLog("tile downloaded");
  int rc = -1, err = 0; uint32_t tDec = 0;
  litCount = 0;
  if (tile) {
    uint32_t t2 = millis();
    rc = png->openRAM(tile, len, drawLine);
    if (rc == PNG_SUCCESS) {
      Serial.printf("  PNG %dx%d bpp %d type %d alpha %d\n", png->getWidth(), png->getHeight(), png->getBpp(), png->getPixelType(), png->hasAlpha());
      panel->clearScreen();
      rc = png->decode(nullptr, 0);
      png->close();
      drawHome();
    }
    err = png->getLastError();
    tDec = millis() - t2;
  }
  free(tile);
  delete png; png = nullptr;
#if SIM_PLAYER
  simPlayerAlloc();
  if (!simPlayer[0] || !simPlayer[1] || !simPlayer[2]) Serial.println("  !! player memory could NOT be taken back (firmware would restart)");
#endif
  heapLog("after (player back)");
  Serial.printf("  HTTP %d, %d bytes, decode rc %d (err %d), %u of %u LEDs lit\n", code, len, rc, err, litCount, W * H);
  Serial.printf("  timing: index %u ms, download %u ms, decode+draw %u ms, total %u ms\n",
                (unsigned)tIndex, (unsigned)tDown, (unsigned)tDec, (unsigned)(millis() - t0));
  if (rc != PNG_SUCCESS) say("RADAR", code == 200 ? "decode failed" : "fetch failed");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\nRadar probe (RainViewer, PNGdec)");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  panelStart();
  say("RADAR PROBE", "WiFi...");
  Preferences prefs;
  prefs.begin("pj", true);
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", ""), home = prefs.getString("wxhome", "");
  prefs.end();
  int c = home.indexOf(',');
  if (c > 0 && home.substring(0, c).toFloat() != 0) { homeLat = home.substring(0, c).toFloat(); homeLon = home.substring(c + 1).toFloat(); }
  Serial.printf("home %.4f,%.4f (wxhome \"%s\")\n", homeLat, homeLon, home.c_str());
  if (!ssid.length()) { Serial.println("No WiFi saved in NVS (set it up with the main firmware first)"); say("RADAR PROBE", "no WiFi saved"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) { say("RADAR PROBE", "WiFi failed"); return; }
  Serial.printf("WiFi %s, ip %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
  configTime(0, 0, "pool.ntp.org");
  heapLog("WiFi up");
#if SIM_PLAYER
  simPlayerAlloc();
  simBallast();
#endif
  radarFetch();
}

void loop() {
  static uint32_t last = millis();
  static bool was = true;
  bool now = digitalRead(BUTTON_PIN) == HIGH;
  if ((!now && was) || millis() - last > 10UL * 60 * 1000) { last = millis(); if (WiFi.status() == WL_CONNECTED) radarFetch(); }
  was = now;
  delay(20);
}
