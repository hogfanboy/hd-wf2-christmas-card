// Pocket Jumbotron firmware. Board: Huidu HD-WF2 (ESP32-S3, 8 MB flash, no PSRAM), one 128x64 1/32-scan
// HUB75E panel. Arduino joins the .ino files into one program: this file first, then the others in name order,
// so shared types, globals and includes live here. One file per job:
//   pocket_jumbotron.ino  config, shared state, setup(), loop(), S1 button
//   canvas.ino    drawing helpers (canvas text, fonts, pixels, panel text colors)
//   effects.ino   effects library (snow, fireworks, fire...) and effect-only items
//   net.ino       WiFi, settings (NVS), hotspot setup mode, address/QR screen
//   online.ino    HTTPS from the website, stocks, automatic location + time zone
//   panel.ino     panel driver setup: chip registers, fast scan (QR), restart
//   player.ino    GIF and full-color .pjm players, canvas push
//   screens.ino   text, clock and countdown items
//   show.ino      the show list (/show.txt format), schedule, time and night mode
//   weather.ino   weather and 5-day forecast (Open-Meteo)
//   web.ino       web API, the starter page that loads the app, built-in app at /local
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <algorithm>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "intro_gif.h"
#include <time.h>
#include <HTTPClient.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "pj_font.h"
#include "wx_icons.h"

// ---- Hardware (confirmed on WF2 V7.2, see PROJECT-NOTES.md) ----
static const HUB75_I2S_CFG::i2s_pins PINS = {
  /*R1*/ 2, /*G1*/ 6, /*B1*/ 10, /*R2*/ 3, /*G2*/ 7, /*B2*/ 11,
  /*A*/ 39, /*B*/ 38, /*C*/ 37, /*D*/ 36, /*E*/ 21, /*LAT*/ 33, /*OE*/ 35, /*CLK*/ 34};
static const int W = 128, H = 64;
static const uint8_t BUTTON_PIN = 17;          // S1 on the WF2 board, active LOW (found by pin scan)

// ---- Tuning ----
static const char *FW_VERSION = "1.0.0-beta7";
// Double buffering costs ~100 KB of RAM (no PSRAM); single buffering leaves room for WiFi.
static const bool USE_DOUBLE_BUFFER = false;
static const uint8_t MAX_BRIGHTNESS = 140;    // ~55% of full: protects barrel jack / supply. 100% in the app = this.
static const uint8_t fastBits = 2;             // color bits in fast scan for the QR screen (~650 Hz; 2-6 tried, 2 reads best)
uint8_t brightnessPct = 100;                   // user setting, saved in NVS
uint8_t themeIx = 2;                           // color theme (canvas.ino THEMES), saved in NVS
bool autoUpdate = true;                        // daily update check from the website (online.ino), saved in NVS
extern bool updateNow;
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
// The show (see show.ino). Declared here because Arduino joins the .ino files in name order.
struct ShowItem {
  char type = 'p';                // p picture, t text, c clock, d countdown, f effect, w weather
  uint16_t sec = 0;
  String file, text, extra;       // extra: clock 12/24, countdown MM-DD, weather "F,lat,lon"
  uint32_t fg = 0xFFFFFF, bg = 0;
  bool rainbow = false;
  uint8_t font = 2, effect = 0;   // font: 1 tiny, 2-4 classic S/M/B, 8-12 PJ (canvas.ino). effect: FX_NAMES index
  // When it plays (show.ino itemActive): yearly date range MMDD (0 = any), weekdays bit 0 = Sunday,
  // hours [hFrom, hTo) (-1 = any), countdown only within `lead` days (-1 = always). forecast: weather + 5 days.
  uint16_t dFrom = 0, dTo = 0;
  uint8_t days = 0x7F;
  int8_t hFrom = -1, hTo = -1;
  int16_t lead = -1;
  bool forecast = false;
  bool themed = false;            // color "theme": fg follows the card's theme
};
struct Theme { uint32_t main, acc, dim; };   // color theme: main text, accent, dim (canvas.ino THEMES)
std::vector<ShowItem> playlist;
std::vector<uint16_t> playOrder;               // play order: playlist indexes, shuffled each round in shuffle mode
bool shuffleShow = false;                      // show.txt "order shuffle" line (show.ino)
struct Flake { int16_t x, y; uint8_t spd, bri; };   // effect particles (show.ino)
// Effect names in show lines (show.ino); index = ShowItem.effect.
const char *const FX_NAMES[] = {"none", "snow", "sparkle", "hearts", "confetti", "fireworks", "stars", "fire", "matrix",
                               "sleigh", "balloons", "fish", "rain", "auto", "lights"};   // auto: weather items, snow or rain to match
enum { FX_NONE, FX_SNOW, FX_SPARKLE, FX_HEARTS, FX_CONFETTI, FX_FIREWORKS, FX_STARS, FX_FIRE, FX_MATRIX,
       FX_SLEIGH, FX_BALLOONS, FX_FISH, FX_RAIN, FX_AUTO, FX_LIGHTS, FX_COUNT };
// ---------- Settings: time zone + night mode (NVS, see net.ino settingsLoad) ----------
String tzString = "UTC0";
String geoTz;                                  // the time zone that goes with the card's location (online.ino)
bool tzAuto = true;                           // no time zone from the page yet: guessed from the location (online.ino)
int8_t nightFrom = -1, nightTo = -1;          // hours 0-23; -1 = night mode off
uint8_t nightLevel = 0;                        // % of normal brightness at night (0 = dark)
bool nightNow = false;

// Declared here for setup(): the auto-made declarations skip functions with default arguments.
bool startPanel(bool dbl, bool fast = false);   // panel.ino
// Player state (player.ino), here because the panel restart (panel.ino) resets it too.
static PixRow *shown = nullptr;                // what the panel currently shows (single-buffer mode)
static bool shownValid = false;
static PixRow *saved = nullptr;                // canvas snapshot for disposal 3 (allocated only if a GIF uses it)

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nPocket Jumbotron - GIF player");
  for (int i = 0; i < 256; i++) gammaLut[i] = (uint8_t)(powf(i / 255.0f, GAMMA) * 255.0f + 0.5f);

  if (!startPanel(USE_DOUBLE_BUFFER) && !startPanel(false)) { Serial.println("Panel init FAILED"); for (;;) delay(1000); }
  settingsLoad();
  panelRegInit();                  // every panel (FM6124C ignores these; RUL6024 needs them)
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
  markAppValid();                 // we started fine: cancel automatic rollback of a fresh update
  playIntro();
  if (playlist.empty()) showMessage("POCKET", "JUMBOTRON");
  netBegin();
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
// Returns true if something should interrupt the current picture. Always serves at least once, so a GIF whose
// frames take longer to draw than their delay can't starve the web page and the button.
bool waitMs(uint32_t ms) {
  uint32_t end = millis() + ms;
  do {
    pollButton();
    netService();
    checkNight();
    if (buttonAction != BTN_NONE || holdPlayback || addrUntil) return true;
    if ((int32_t)(end - millis()) > 0) delay(3);
  } while ((int32_t)(end - millis()) > 0);
  return false;
}

void loop() {
  pollButton();
  netService();
  if (buttonAction == BTN_LONG) { buttonAction = BTN_NONE; portalActive ? portalStop() : portalStart(); }
  if (portalActive) { buttonAction = BTN_NONE; delay(2); return; }
  if (buttonAction == BTN_SHORT) {                   // press: show the address; press again: back to the show
    buttonAction = BTN_NONE;
    if (addrUntil) addrUntil = 1;                    // already up: let it expire now
    else showAddress(10000);
  }
  if (addrUntil) {                                   // address screen is up
    if ((int32_t)(millis() - addrUntil) < 0) { delay(5); return; }
    addrUntil = 0;
    panelSetFast(false);                             // back to full color after the QR screen
    applyBrightness();                               // some QR styles use full panel brightness
  }
  updateService();                                   // 3 am: newer firmware on the website? (online.ino)
  if (holdPlayback || playlist.empty()) { delay(5); return; }
  if (playOrder.size() != playlist.size()) shuffleOrder();
  if (playIdx >= playlist.size()) { playIdx = 0; shuffleOrder(); }   // next round (a new random order in shuffle mode)
  static size_t skipped = 0;                         // items in a row that are off schedule right now
  const ShowItem &it = playlist[playOrder[playIdx++]];
  if (!itemActive(it)) {
    if (++skipped >= playlist.size()) { skipped = 0; panel->clearScreen(); shownValid = false; waitMs(1000); }
    return;
  }
  skipped = 0;
  geoService();                                      // once per start: where am I (online.ino)
  playItem(it);
}
