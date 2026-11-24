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
//   radar.ino     radar weather screen (RainViewer tiles + the page's map)
//   screens.ino   text, clock and countdown items
//   tune.ino      Color tune test screens (color balance per brightness)
//   show.ino      the show list (/show.txt format), schedule, time and night mode
//   weather.ino   weather (Open-Meteo) and its extra pages (hours, days, details, air, sun/moon), US weather alerts
//   web.ino       web API, the starter page that loads the app, built-in app at /local
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <JPEGDEC.h>
#include <PNGdec.h>                           // radar tiles (radar.ino)
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include <algorithm>
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "intro_gif.h"
#include <time.h>
#include <sys/time.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "pj_font.h"
#include "wx_icons.h"
#include "fx_sprites.h"

// ---- Hardware (confirmed on WF2 V7.2, see PROJECT-NOTES.md) ----
static const HUB75_I2S_CFG::i2s_pins PINS = {
  /*R1*/ 2, /*G1*/ 6, /*B1*/ 10, /*R2*/ 3, /*G2*/ 7, /*B2*/ 11,
  /*A*/ 39, /*B*/ 38, /*C*/ 37, /*D*/ 36, /*E*/ 21, /*LAT*/ 33, /*OE*/ 35, /*CLK*/ 34};
static const int W = 128, H = 64;
static const uint8_t BUTTON_PIN = 17;          // S1 on the WF2 board, active LOW (found by pin scan)

// ---- Tuning ----
static const char *FW_VERSION = "1.1.0-beta2";
// Double buffering costs ~100 KB of RAM (no PSRAM); single buffering leaves room for WiFi.
static const bool USE_DOUBLE_BUFFER = false;
static const uint8_t MAX_BRIGHTNESS = 140;    // ~55% of full: protects barrel jack / supply. 100% in the app = this.
static const uint8_t fastBits = 2;             // color bits in fast scan for the QR screen (~650 Hz; 2-6 tried, 2 reads best)
uint8_t brightnessPct = 100;                   // user setting, saved in NVS
uint8_t themeIx = 2;                           // color theme (canvas.ino THEMES), saved in NVS
String wxHome;                                 // last weather town picked on the page ("lat,lon,place" or "auto"), NVS
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
uint8_t gammaLutR[256], gammaLutG[256], gammaLutB[256];   // per color, from the color balance: applyBrightness
// Color balance per brightness, tuned by eye on the Color tune screens: red, green, blue percent at 6 panel levels
// out of 255, blended in between. NVS "cal". Levels: night 5% and 10% of 50%, night 25%, normal 25%, 50%, 100%.
static const uint8_t CAL_N = 6;
static const float CAL_LEVEL[CAL_N] = {3.5f, 7.0f, 17.5f, 35.0f, 70.0f, 140.0f};
static const uint8_t CAL_DEFAULT[CAL_N][3] = {{64, 100, 100}, {64, 100, 100}, {75, 100, 100}, {100, 100, 100}, {100, 100, 100}, {100, 100, 100}};
uint8_t calGain[CAL_N][3];
uint32_t tuneUntil = 0;                        // a Color tune screen is up until then: tune.ino
int8_t tuneLevel = -1;                         // the level being tuned, shown on the panel whatever the time of day
uint8_t hwMin = 12;                            // lowest panel level used; dimmer than that is done in the colors: NVS hwmin
typedef uint8_t PixRow[W][3];
static const size_t CANVAS_BYTES = sizeof(PixRow) * H;
PixRow *canvas = nullptr;                      // composited frame (RGB888)
int offX = 0, offY = 0;                        // centers GIFs smaller than the panel
// The show (see show.ino). Declared here because Arduino joins the .ino files in name order.
struct ShowItem {
  char type = 'p';                // p picture, t text, c clock, d countdown, f effect, w weather, s stocks, b dates
  uint16_t sec = 0;
  String file, text, extra;       // extra: clock 12/24, countdown MM-DD / holiday key / next, weather "F,lat,lon"
  uint32_t fg = 0xFFFFFF, bg = 0;
  bool rainbow = false;
  uint8_t font = 2, effect = 0;   // font: 1 tiny, 2-4 classic S/M/B, 8-12 PJ (canvas.ino). effect: FX_NAMES index
  // When it plays (show.ino itemActive): yearly date range MMDD (0 = any), weekdays bit 0 = Sunday,
  // hours [hFrom, hTo) (-1 = any), countdown only within `lead` days (-1 = always). wxPages: weather extra screens (WXP_*).
  uint16_t dFrom = 0, dTo = 0;
  uint8_t days = 0x7F;
  int8_t hFrom = -1, hTo = -1;
  int16_t lead = -1;
  uint16_t wxPages = 0;
  uint8_t rzoom = 6;              // weather radar range: LED zoom 7 close, 6 normal, 5 wide, 4 regional (show.txt rzoom=)
  uint8_t alerts = 0;             // weather: 1 = show weather alerts (US, weather.gov), 2 = demo alerts (testing)
  bool themed = false;            // color "theme": fg follows the card's theme
};
// Weather screens after "now" (show.txt options, checkboxes on the page), in the order they play.
enum { WXP_RAINSOON = 1, WXP_HOURLY = 2, WXP_CHART = 4, WXP_DAYS5 = 8, WXP_DAYS10 = 16, WXP_DETAILS = 32, WXP_AIR = 64, WXP_SUN = 128, WXP_RADAR = 256, WXP_RADAR_ALWAYS = 512 };   // radar=always: testing
typedef void (*WxDraw)(const ShowItem &, uint32_t frame, uint32_t ms);   // draws one weather screen (weather.ino)
struct DateHit { int days; int years; char kind; String name; };   // a birthday/date coming up (screens.ino)
struct WxAlert { char title[32]; uint8_t level; time_t until; };   // a weather alert (weather.ino): level 1-3
struct Theme { uint32_t main, acc, dim; };   // color theme: main text, accent, dim (canvas.ino THEMES)
std::vector<ShowItem> playlist;
std::vector<uint16_t> playOrder;               // play order: playlist indexes, shuffled each round in shuffle mode
bool shuffleShow = false;                      // show.txt "order shuffle" line (show.ino)
struct Flake { int16_t x, y; uint8_t spd, bri; };   // effect particles (show.ino)
// Effect names in show lines (show.ino); index = ShowItem.effect.
const char *const FX_NAMES[] = {"none", "snow", "sparkle", "hearts", "confetti", "fireworks", "stars", "fire", "matrix",
                               "sleigh", "balloons", "fish", "rain", "auto", "lights", "turkey", "bunny", "bats", "shamrocks", "flag"};
                               // auto: weather items snow or rain to match; countdowns the holiday's own effect
enum { FX_NONE, FX_SNOW, FX_SPARKLE, FX_HEARTS, FX_CONFETTI, FX_FIREWORKS, FX_STARS, FX_FIRE, FX_MATRIX,
       FX_SLEIGH, FX_BALLOONS, FX_FISH, FX_RAIN, FX_AUTO, FX_LIGHTS, FX_TURKEY, FX_BUNNY, FX_BATS, FX_SHAMROCKS, FX_FLAG, FX_COUNT };
// ---------- Settings: time zone + night mode (NVS, see net.ino settingsLoad) ----------
String tzString = "UTC0";
String geoTz;                                  // the time zone that goes with the card's location (online.ino)
bool tzAuto = true;                           // no time zone from the page yet: guessed from the location (online.ino)
int8_t nightFrom = -1, nightTo = -1;          // hours 0-23; -1 = night mode off
uint8_t nightLevel = 0;                        // % of normal brightness at night (0 = dark)
bool nightNow = false;

// Declared here for setup(): the auto-made declarations skip functions with default arguments.
bool startPanel(bool dbl, bool fast = false);   // panel.ino
void alertsBetweenItems();                      // weather.ino: warnings come back every few items
// Player state (player.ino), here because the panel restart (panel.ino) resets it too.
static PixRow *shown = nullptr;                // what the panel currently shows (single-buffer mode)
static bool shownValid = false;
static PixRow *saved = nullptr;                // canvas snapshot for disposal 3 (allocated only if a GIF uses it)

// ---------- Health: quiet restarts and the numbers for /api/info ----------
// Days of downloads chop free memory into smaller pieces. A quiet restart puts it back: every night at 4 am (after
// 20 h up), or at once if the biggest free piece gets small (normal while playing: ~32 KB). RTC_NOINIT memory
// survives a software restart, so the card then skips the intro and the address screen and keeps the clock (night
// mode stays right). It also counts quiet restarts and crashes since power-on, for the soak test.
RTC_NOINIT_ATTR static uint32_t rtcMagic, rtcQuiet, rtcWhy, rtcTime, rtcQuietN, rtcCrashN;
static const uint32_t RTC_MAGIC = 0x50A11E57, QUIET_MAGIC = 0x51E7B007;
static const uint32_t LOWMEM_BLOCK = 12000;    // restart below this biggest free block (between items)
enum { BOOT_POWER, BOOT_SOFT, BOOT_CRASH, BOOT_NIGHTLY, BOOT_LOWMEM };
static const char *const BOOT_NAME[] = {"power", "restart", "crash", "nightly", "lowmem"};
static uint8_t bootKind = BOOT_POWER;
static const char *bootDetail = "";            // esp_reset_reason in words
bool quietBoot = false;                        // this start was a quiet restart: no intro, no address screen
// Card report (for helping a tester): starts and crashes over the card's whole life, kept in NVS so they survive
// unplugging. lifeLast = reason and software of the last crash ("" = never).
static uint32_t lifeBoots = 0, lifeCrashes = 0;
static char lifeLast[48] = "";

static uint32_t upSec() { return (uint32_t)(esp_timer_get_time() / 1000000); }
static void quietArm(uint8_t why) {             // the next software restart is a quiet one
  rtcQuiet = QUIET_MAGIC; rtcWhy = why; rtcTime = timeValid() ? (uint32_t)time(nullptr) : 0; rtcQuietN++;
}
static void quietRestart(uint8_t why) {
  quietArm(why);
  Serial.printf("Quiet restart (%s): up %u s, free %u, largest block %u, lowest free %u\n", BOOT_NAME[why], (unsigned)upSec(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
  delay(100);
  ESP.restart();
}
static void healthBoot() {                     // setup(), after settingsLoad (needs tzString)
  esp_reset_reason_t r = esp_reset_reason();
  bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
  bootDetail = r == ESP_RST_POWERON ? "power on" : r == ESP_RST_SW ? "software" : r == ESP_RST_PANIC ? "panic" :
               r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ? "watchdog" : r == ESP_RST_BROWNOUT ? "brownout" :
               r == ESP_RST_EXT ? "reset pin" : r == ESP_RST_USB ? "usb" : "other";
  if (r == ESP_RST_POWERON || rtcMagic != RTC_MAGIC) { rtcMagic = RTC_MAGIC; rtcQuietN = 0; rtcCrashN = 0; rtcQuiet = 0; }
  if (crash) { rtcCrashN++; bootKind = BOOT_CRASH; }
  else if (r == ESP_RST_SW && rtcQuiet == QUIET_MAGIC && (rtcWhy == BOOT_NIGHTLY || rtcWhy == BOOT_LOWMEM)) {
    bootKind = rtcWhy; quietBoot = true;
    if (rtcTime) {                               // keep the clock until NTP answers again
      timeval tv = {(time_t)rtcTime + 2, 0}; settimeofday(&tv, nullptr);
      setenv("TZ", tzString.c_str(), 1); tzset();
    }
  } else if (r != ESP_RST_POWERON) bootKind = BOOT_SOFT;
  rtcQuiet = 0;
  Preferences life;                            // its own namespace: one small write per start
  if (life.begin("pjlife", false)) {
    lifeBoots = life.getUInt("boots", 0) + 1; life.putUInt("boots", lifeBoots);
    lifeCrashes = life.getUInt("crashes", 0);
    if (crash) {
      lifeCrashes++; life.putUInt("crashes", lifeCrashes);
      snprintf(lifeLast, sizeof(lifeLast), "%s, %s", bootDetail, FW_VERSION); life.putString("last", lifeLast);
    } else life.getString("last", lifeLast, sizeof(lifeLast));
    life.end();
  }
  Serial.printf("Start: %s (%s), quiet restarts %u, crashes %u since power-on\n", BOOT_NAME[bootKind], bootDetail,
                (unsigned)rtcQuietN, (unsigned)rtcCrashN);
}
void healthService() {                         // loop(), between items
  uint32_t up = upSec();
  if (up > 600 && ESP.getMaxAllocHeap() < LOWMEM_BLOCK) quietRestart(BOOT_LOWMEM);
  if (up < 20UL * 3600) return;
  if (!timeValid()) { if (up > 72UL * 3600) quietRestart(BOOT_NIGHTLY); return; }   // no clock: every 3 days
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  if (lt.tm_hour == 4) quietRestart(BOOT_NIGHTLY);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nPocket Jumbotron - GIF player");
  for (int i = 0; i < 256; i++) gammaLutR[i] = gammaLutG[i] = gammaLutB[i] = gammaLut[i] = (uint8_t)(powf(i / 255.0f, GAMMA) * 255.0f + 0.5f);

  if (!startPanel(USE_DOUBLE_BUFFER) && !startPanel(false)) { Serial.println("Panel init FAILED"); for (;;) delay(1000); }
  settingsLoad();
  panelRegInit();                  // every panel (FM6124C ignores these; RUL6024 needs them)
  applyBrightness();
  healthBoot();
  if (quietBoot) forceNightCheck();           // the clock came along: dark at once if it is night
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
  if (!quietBoot) playIntro();
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
    if (buttonAction != BTN_NONE || holdPlayback || addrUntil || tuneUntil) return true;
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
    if (addrUntil) addrUntil = 1;               // already up: let it expire now
    else showAddress(10000);
  }
  if (addrUntil) {                                   // address screen is up
    if ((int32_t)(millis() - addrUntil) < 0) { addressTick(); delay(5); return; }
    addrUntil = 0;
    panelSetFast(false);                             // back to full color after the QR screen
    applyBrightness();                               // the QR screen sets its own brightness
  }
  if (tuneUntil) { tuneTick(); delay(5); return; }   // Color tune screen (tune.ino)
  updateService();                                   // 3 am: newer firmware on the website? (online.ino)
  healthService();                                   // 4 am quiet restart, or at once if memory gets tight
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
  char type = it.type;                               // (the show may be replaced while it plays)
  playItem(it);
  if (type != 'w') alertsBetweenItems();
}
