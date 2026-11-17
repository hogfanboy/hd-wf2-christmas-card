// Pocket Jumbotron firmware - GIF player.
// Plays every GIF in /gifs on LittleFS, in name order, with gamma correction and a brightness cap.
// Board: Huidu HD-WF2 (ESP32-S3, 8 MB flash, no PSRAM), one 128x64 1/32-scan HUB75E panel.
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <algorithm>

// ---- Hardware (confirmed on WF2 V7.2, see WF2-friday-plan.md) ----
static const HUB75_I2S_CFG::i2s_pins PINS = {
  /*R1*/ 2, /*G1*/ 6, /*B1*/ 10, /*R2*/ 3, /*G2*/ 7, /*B2*/ 11,
  /*A*/ 39, /*B*/ 38, /*C*/ 37, /*D*/ 36, /*E*/ 21, /*LAT*/ 33, /*OE*/ 35, /*CLK*/ 34};
static const int W = 128, H = 64;

// ---- Tuning ----
static const uint8_t BRIGHTNESS = 140;        // ~55%: protects barrel jack / supply
static const float GAMMA = 2.2f;
static const uint32_t MIN_SHOW_MS = 8000;     // short GIFs loop until this long
static const uint32_t MAX_SHOW_MS = 20000;    // long GIFs get cut off here (at a loop end)

MatrixPanel_I2S_DMA *panel = nullptr;
bool doubleBuffered = false;
AnimatedGIF gif;
uint8_t gammaLut[256];
uint8_t canvas[H][W][3];                       // composited frame (RGB888, before gamma)
int offX = 0, offY = 0;                        // centers GIFs smaller than the panel
std::vector<String> playlist;

// ---- AnimatedGIF file callbacks (stream from LittleFS) ----
static File gifFile;
void *gifOpen(const char *name, int32_t *size) {
  gifFile = LittleFS.open(name, "r");
  if (!gifFile) return nullptr;
  *size = gifFile.size();
  return &gifFile;
}
void gifClose(void *h) { if (h) static_cast<File *>(h)->close(); }
int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  File *file = static_cast<File *>(f->fHandle);
  int32_t n = std::min(len, f->iSize - f->iPos);
  if (n <= 0) return 0;
  n = file->read(buf, n);
  f->iPos = file->position();
  return n;
}
int32_t gifSeek(GIFFILE *f, int32_t pos) {
  File *file = static_cast<File *>(f->fHandle);
  file->seek(pos);
  f->iPos = file->position();
  return f->iPos;
}

// Called once per line of each frame: composite into the canvas (skips transparent pixels).
void gifDraw(GIFDRAW *d) {
  int y = d->iY + d->y + offY;
  if (y < 0 || y >= H) return;
  const uint8_t *pal = d->pPalette24;
  const uint8_t *src = d->pPixels;
  int x0 = d->iX + offX;
  // Disposal 2 ("restore to background"): clear the frame area before drawing.
  if (d->ucDisposalMethod == 2) {
    for (int i = 0; i < d->iWidth; i++) { int x = x0 + i; if (x >= 0 && x < W) canvas[y][x][0] = canvas[y][x][1] = canvas[y][x][2] = 0; }
  }
  for (int i = 0; i < d->iWidth; i++) {
    uint8_t idx = src[i];
    if (d->ucHasTransparency && idx == d->ucTransparent) continue;
    int x = x0 + i;
    if (x < 0 || x >= W) continue;
    const uint8_t *c = pal + idx * 3;
    canvas[y][x][0] = c[0]; canvas[y][x][1] = c[1]; canvas[y][x][2] = c[2];
  }
}

void pushCanvas() {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      panel->drawPixelRGB888(x, y, gammaLut[canvas[y][x][0]], gammaLut[canvas[y][x][1]], gammaLut[canvas[y][x][2]]);
  if (doubleBuffered) panel->flipDMABuffer();
}

void loadPlaylist() {
  playlist.clear();
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();
      if (n.endsWith(".gif") || n.endsWith(".GIF")) playlist.push_back(String("/gifs/") + n);
    }
  }
  std::sort(playlist.begin(), playlist.end());
}

void showMessage(const char *line1, const char *line2) {
  panel->clearScreen();
  panel->setTextColor(panel->color565(255, 190, 60));
  panel->setCursor(2, 20); panel->print(line1);
  panel->setTextColor(panel->color565(90, 200, 255));
  panel->setCursor(2, 34); panel->print(line2);
  if (doubleBuffered) panel->flipDMABuffer();
}

bool startPanel(bool dbl) {
  HUB75_I2S_CFG cfg(W, H, 1, PINS);
  cfg.clkphase = false;
  cfg.double_buff = dbl;
  panel = new MatrixPanel_I2S_DMA(cfg);
  if (panel->begin()) { doubleBuffered = dbl; return true; }
  delete panel; panel = nullptr;
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nPocket Jumbotron - GIF player");
  for (int i = 0; i < 256; i++) gammaLut[i] = (uint8_t)(powf(i / 255.0f, GAMMA) * 255.0f + 0.5f);

  if (!startPanel(true) && !startPanel(false)) { Serial.println("Panel init FAILED"); for (;;) delay(1000); }
  panel->setBrightness8(BRIGHTNESS);
  panel->clearScreen();
  Serial.printf("Panel ok, double buffer: %s, free heap %u\n", doubleBuffered ? "yes" : "no", (unsigned)ESP.getFreeHeap());

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed");
    showMessage("NO STORAGE", "flash media");
    for (;;) delay(1000);
  }
  Serial.printf("LittleFS %u / %u KB used\n", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  gif.begin(GIF_PALETTE_RGB888);
  loadPlaylist();
  Serial.printf("%u GIFs in playlist\n", (unsigned)playlist.size());
  if (playlist.empty()) showMessage("POCKET", "JUMBOTRON");
}

void playOne(const String &name) {
  if (!gif.open(name.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
    Serial.printf("  can't open %s (err %d)\n", name.c_str(), gif.getLastError());
    return;
  }
  offX = (W - gif.getCanvasWidth()) / 2;
  offY = (H - gif.getCanvasHeight()) / 2;
  memset(canvas, 0, sizeof(canvas));
  uint32_t start = millis(), frames = 0, loops = 0;
  for (;;) {
    uint32_t t0 = millis();
    int delayMs = 0;
    int more = gif.playFrame(false, &delayMs);
    pushCanvas();
    frames++;
    if (delayMs < 20) delayMs = 100;            // GIFs with 0 delay: browsers use ~100 ms
    int32_t wait = delayMs - (int32_t)(millis() - t0);
    if (wait > 0) delay(wait);
    if (more <= 0) {                             // end of one loop
      loops++;
      uint32_t elapsed = millis() - start;
      if (elapsed >= MIN_SHOW_MS || more < 0) break;
      gif.reset();
    } else if (millis() - start > MAX_SHOW_MS && loops > 0) break;
    if (millis() - start > MAX_SHOW_MS * 2) break;  // very long single loop: hard stop
  }
  uint32_t ms = millis() - start;
  Serial.printf("  %-36s %3dx%-3d %4u frames %2u loops %5.1f fps\n", name.c_str(), gif.getCanvasWidth(), gif.getCanvasHeight(),
                (unsigned)frames, (unsigned)loops, frames * 1000.0f / (ms ? ms : 1));
  gif.close();
}

void loop() {
  if (playlist.empty()) { delay(1000); return; }
  for (const String &name : playlist) playOne(name);
}
