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
static const uint8_t BUTTON_PIN = 17;          // S1 on the WF2 board, active LOW (found by pin scan)

// ---- Tuning ----
static const char *FW_VERSION = "0.4.0";
static const uint8_t MAX_BRIGHTNESS = 140;    // ~55% of full: protects barrel jack / supply. 100% in the app = this.
uint8_t brightnessPct = 100;                   // user setting, saved in NVS
// The HUB75 library already applies CIE1931 brightness correction (lumConvTab), so no extra gamma here.
static const float GAMMA = 1.0f;
static const uint32_t MIN_SHOW_MS = 8000;     // short GIFs loop until this long
static const uint32_t MAX_SHOW_MS = 20000;    // long GIFs get cut off here (at a loop end)

enum ButtonAction { BTN_NONE, BTN_SHORT, BTN_LONG };
volatile ButtonAction buttonAction = BTN_NONE;
bool portalActive = false;                     // WiFi setup hotspot running
bool holdPlayback = false;                     // phone is replacing pictures: stop playing
bool playing = false;                          // a GIF file is open
uint32_t addrUntil = 0;                        // showing the web address on the panel until then
size_t playIdx = 0;
MatrixPanel_I2S_DMA *panel = nullptr;
bool doubleBuffered = false;
// Player memory is allocated on the heap so setup mode can hand it to WiFi (which needs ~70 KB more).
AnimatedGIF *gifp = nullptr;
#define gif (*gifp)
uint8_t gammaLut[256];
typedef uint8_t PixRow[W][3];
static const size_t CANVAS_BYTES = sizeof(PixRow) * H;
PixRow *canvas = nullptr;                      // composited frame (RGB888)
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

// GIF "disposal" says what to do with a frame's area *after* it has been shown, before the next frame.
// 2 = clear it to background, 3 = restore what was there before it. We remember the previous frame's
// rule and apply it when the next frame starts (including when the animation loops back to frame 1).
struct PendingDisposal { uint8_t method; int x, y, w, h; } pending = {0, 0, 0, 0, 0};
static PixRow *saved = nullptr;                // canvas snapshot for disposal 3 (allocated only if a GIF uses it)

static void fillRect(int x0, int y0, int w, int h, const PixRow *src) {
  for (int y = std::max(0, y0); y < std::min(H, y0 + h); y++)
    for (int x = std::max(0, x0); x < std::min(W, x0 + w); x++)
      for (int c = 0; c < 3; c++) canvas[y][x][c] = src ? src[y][x][c] : 0;
}

static void beginFrame(GIFDRAW *d) {
  if (pending.method == 2) fillRect(pending.x, pending.y, pending.w, pending.h, nullptr);
  else if (pending.method == 3 && saved) fillRect(pending.x, pending.y, pending.w, pending.h, saved);
  pending = {d->ucDisposalMethod, d->iX + offX, d->iY + offY, d->iWidth, d->iHeight};
  if (pending.method == 3) { if (!saved) saved = (PixRow *)malloc(CANVAS_BYTES); if (saved) memcpy(saved, canvas, CANVAS_BYTES); }
}

// Called once per line of each frame: composite into the canvas (skips transparent pixels).
void gifDraw(GIFDRAW *d) {
  if (d->y == 0) beginFrame(d);
  int y = d->iY + d->y + offY;
  if (y < 0 || y >= H) return;
  const uint8_t *pal = d->pPalette24;
  const uint8_t *src = d->pPixels;
  int x0 = d->iX + offX;
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

bool playerAlloc() {
  if (!gifp) { gifp = new AnimatedGIF(); gifp->begin(GIF_PALETTE_RGB888); }
  if (!canvas) canvas = (PixRow *)calloc(1, CANVAS_BYTES);
  return gifp && canvas;
}
void playerFree() {
  delete gifp; gifp = nullptr;
  free(canvas); canvas = nullptr;
  free(saved); saved = nullptr;
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
  settingsLoad();
  applyBrightness();
  panel->clearScreen();
  Serial.printf("Panel ok, %d-bit color, double buffer: %s, free heap %u\n", PIXEL_COLOR_DEPTH_BITS,
                doubleBuffered ? "yes" : "no", (unsigned)ESP.getFreeHeap());
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) {   // formats an empty/blank storage area on first boot
    Serial.println("LittleFS mount failed");
    showMessage("NO STORAGE", "flash media");
    for (;;) delay(1000);
  }
  Serial.printf("LittleFS %u / %u KB used\n", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  playerAlloc();
  loadPlaylist();
  Serial.printf("%u GIFs in playlist\n", (unsigned)playlist.size());
  if (playlist.empty()) showMessage("POCKET", "JUMBOTRON");
  markAppValid();                 // we started fine: cancel automatic rollback of a fresh update
  netBegin();
}

void playOne(const String &name) {
  if (!gif.open(name.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
    Serial.printf("  can't open %s (err %d)\n", name.c_str(), gif.getLastError());
    return;
  }
  playing = true;
  offX = (W - gif.getCanvasWidth()) / 2;
  offY = (H - gif.getCanvasHeight()) / 2;
  memset(canvas, 0, CANVAS_BYTES);
  pending.method = 0;
  uint32_t start = millis(), frames = 0, loops = 0;
  for (;;) {
    uint32_t t0 = millis();
    int delayMs = 0;
    int more = gif.playFrame(false, &delayMs);
    pushCanvas();
    frames++;
    if (delayMs < 20) delayMs = 100;            // GIFs with 0 delay: browsers use ~100 ms
    int32_t wait = delayMs - (int32_t)(millis() - t0);
    if (wait > 0 && waitMs(wait)) break;        // button / phone / address screen interrupts
    if (buttonAction || holdPlayback || addrUntil) break;
    if (more <= 0) {                             // end of one loop
      loops++;
      uint32_t elapsed = millis() - start;
      if (elapsed >= MIN_SHOW_MS || more < 0) break;
      gif.reset();
      memset(canvas, 0, CANVAS_BYTES);       // each loop starts from a blank screen, like browsers do
      pending.method = 0;
    } else if (millis() - start > MAX_SHOW_MS && loops > 0) break;
    if (millis() - start > MAX_SHOW_MS * 2) break;  // very long single loop: hard stop
  }
  uint32_t ms = millis() - start;
  Serial.printf("  %-36s %3dx%-3d %4u frames %2u loops %5.1f fps\n", name.c_str(), gif.getCanvasWidth(), gif.getCanvasHeight(),
                (unsigned)frames, (unsigned)loops, frames * 1000.0f / (ms ? ms : 1));
  gif.close();
  playing = false;
  if (saved) { free(saved); saved = nullptr; }
}

// ---- S1 button: short press = show the web address, hold 2 s = WiFi setup hotspot ----
void pollButton() {
  static bool down = false, longFired = false;
  static uint32_t downAt = 0;
  bool pressed = digitalRead(BUTTON_PIN) == LOW;
  uint32_t now = millis();
  if (pressed && !down) { down = true; longFired = false; downAt = now; }
  else if (pressed && down && !longFired && now - downAt >= 2000) { longFired = true; buttonAction = BTN_LONG; }
  else if (!pressed && down) { down = false; if (!longFired && now - downAt >= 40) buttonAction = BTN_SHORT; }
}
// Waits up to ms while serving the network and watching the button.
// Returns true if something should interrupt the current picture.
bool waitMs(uint32_t ms) {
  uint32_t end = millis() + ms;
  while ((int32_t)(end - millis()) > 0) {
    pollButton();
    netService();
    if (buttonAction != BTN_NONE || holdPlayback || addrUntil) return true;
    delay(3);
  }
  return false;
}

void loop() {
  pollButton();
  netService();
  if (buttonAction == BTN_LONG) { buttonAction = BTN_NONE; portalActive ? portalStop() : portalStart(); }
  if (portalActive) { buttonAction = BTN_NONE; delay(2); return; }
  if (buttonAction == BTN_SHORT) { buttonAction = BTN_NONE; showAddress(10000); }
  if (addrUntil) {                                   // address screen is up
    if ((int32_t)(millis() - addrUntil) < 0) { delay(5); return; }
    addrUntil = 0;
  }
  if (holdPlayback || playlist.empty()) { delay(5); return; }
  if (playIdx >= playlist.size()) playIdx = 0;
  playOne(playlist[playIdx++]);
}
