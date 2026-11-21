// Networking.
//  * Home WiFi (normal): the card joins the user's WiFi and serves the full page (web/index.html) at its
//    IP address while GIFs keep playing. Short-press S1 shows the address on the panel.
//  * WiFi setup hotspot (hold S1 2 s): the card makes its own network with a sign-in pop-up page
//    (web/wifi.html) where the user picks their home WiFi. Saving restarts the card onto that WiFi.
// Why not upload pictures over the hotspot: Android only lets its limited sign-in window use a WiFi
// without internet (no photo picker there), and Chrome can't reach the card at all. See plan notes.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <qrcode.h>
#include "web_index.h"

static const uint32_t PORTAL_IDLE_MS = 10UL * 60 * 1000;   // hotspot gives up after 10 min idle
static const uint32_t HOLD_IDLE_MS = 2UL * 60 * 1000;      // resume playing if a phone stops mid-upload

static WebServer *web = nullptr;
static DNSServer dns;
static Preferences prefs;
static String apSsid, apPass, hostName, staSsid, staPass;
static String scanJson = "[]";
static uint32_t lastActivity = 0, staBeganAt = 0;
static bool webStarted = false, restartRequested = false, staFailShown = false;
static File upFile;
static String upError;
static int upCount = 0;
static size_t upFree = 0;   // free space measured once per file (usedBytes() walks the whole FS: slow)

// ---------- Settings (NVS) ----------
void settingsLoad() {
  prefs.begin("pj", false);
  brightnessPct = prefs.getUChar("bright", 100);
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");
  apPass = prefs.getString("appass", "");
  tzString = prefs.getString("tz", "UTC0");
  if (prefs.isKey("fc")) prefs.remove("fc");   // old family code (0.9.5 and older), no longer used
  nightFrom = prefs.getChar("nfrom", -1);
  nightTo = prefs.getChar("nto", -1);
  nightLevel = prefs.getUChar("nlevel", 0);
  if (apPass.length() != 8) {                      // first boot: make this card's own hotspot password
    char buf[9];
    for (int i = 0; i < 8; i++) buf[i] = '0' + (esp_random() % 10);
    buf[8] = 0;
    apPass = buf;
    prefs.putString("appass", apPass);
  }
  uint64_t mac = ESP.getEfuseMac();                // bytes in order, lowest byte first
  char id[8];
  snprintf(id, sizeof(id), "%02X%02X", (unsigned)((mac >> 32) & 0xff), (unsigned)((mac >> 40) & 0xff));
  apSsid = String("PocketJumbo-") + id;
  hostName = String("pocketjumbo-") + id;
  hostName.toLowerCase();
}
void applyBrightness() {
  uint16_t pct = nightNow ? brightnessPct * nightLevel / 100 : brightnessPct;
  panel->setBrightness8((uint16_t)MAX_BRIGHTNESS * pct / 100);
}

// A freshly installed update must confirm itself, or the next reboot goes back to the previous version
// (see rollback.cpp). We confirm once the panel and storage started fine.
void markAppValid() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("New firmware confirmed (rollback cancelled)");
  }
}

// ---------- Panel screens ----------
static void showHotspotScreen(const char *status) {
  panel->clearScreen();
  panel->setTextSize(1);
  text(0, "WIFI SETUP", C(255, 190, 60));
  text(10, apSsid.c_str(), C(90, 200, 255));
  text(19, ("Pass " + apPass).c_str(), C(90, 200, 255));
  text(31, "Join it on your", C(150, 150, 170));
  text(40, "phone. A page opens", C(150, 150, 170));
  text(54, status, C(78, 224, 138));
  if (doubleBuffered) panel->flipDMABuffer();
}

// QR code of http://<ip> at the left of the panel: white modules, 4-module quiet zone, 2 LEDs per module,
// drawn while the panel is in fast scan (2-bit, ~650 Hz). Fast scan is what made phone cameras read it
// (0.8.4: all styles scanned with the Pixel camera). Uses the ESP32 core's QR encoder.
// Fast scan colors: the library's color table is built for 8-bit, and at 2-bit it reads only the table's two
// lowest bits, so most values come out wrong (white looked green). Use CF() pure colors only.
static void qrDraw(const uint8_t *q) {        // q is an esp_qrcode_handle_t
  int n = esp_qrcode_get_size(q), qz = 4, px = (n + 2 * qz) * 2 <= 64 ? 2 : 1;
  int sz = (n + 2 * qz) * px, x0 = 1, y0 = (64 - sz) / 2;
  panel->fillRect(x0, y0, sz, sz, CF(1, 1, 1));
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) panel->fillRect(x0 + (qz + x) * px, y0 + (qz + y) * px, px, px, 0);
}
static bool drawQr(String ip) {
  String url = "http://" + ip;
  esp_qrcode_config_t cfg = {qrDraw, 5, ESP_QRCODE_ECC_LOW};
  return esp_qrcode_generate(&cfg, url.c_str()) == ESP_OK;
}

// Shown at startup once WiFi connects, and on a short S1 press.
void showAddress(uint32_t ms) {
  panel->clearScreen();
  panel->setTextSize(1);
  text(0, "POCKET JUMBOTRON", C(255, 190, 60));
  if (staSsid.isEmpty()) {
    text(14, "No home WiFi yet.", C(150, 150, 170));
    text(26, "Hold the button", C(90, 200, 255));
    text(35, "2 sec to set up.", C(90, 200, 255));
  } else if (WiFi.status() == WL_CONNECTED) {
    // A QR code of the page address: point the phone camera at it, no typing.
    String ip = WiFi.localIP().toString();
    panelSetFast(true);                       // fast scan for phone cameras (back to full color after)
    panel->clearScreen();
    if (!drawQr(ip)) {
      text(12, "On your phone, open:", C(150, 150, 170));
      text(30, ip.c_str(), C(255, 255, 255));
    } else {
      // Full panel brightness here: at the normal cap the light pulses are so short at 651 Hz that green
      // drops off first (white looked purple and dull). The QR screen lights ~40% of the LEDs, so 255 draws
      // less than a full white screen at the cap. The loop restores the normal brightness after.
      panel->setBrightness8(255);
      // Pure colors only (fast scan, see above).
      panel->setTextColor(CF(1, 1, 1));
      panel->setCursor(64, 2);  panel->print("Scan with");
      panel->setCursor(64, 11); panel->print("your phone");
      panel->setTextColor(CF(0, 1, 1));
      panel->setCursor(64, 28); panel->print("or type:");
      panel->setTextColor(CF(1, 1, 1));
      int dot = ip.lastIndexOf('.', ip.lastIndexOf('.') - 1);   // split 192.168. / 50.16
      panel->setCursor(64, 38); panel->print(ip.substring(0, dot + 1));
      panel->setCursor(64, 47); panel->print(ip.substring(dot + 1));
    }
  } else {
    text(12, "Can't join WiFi:", C(255, 90, 110));
    text(21, staSsid.c_str(), C(255, 255, 255));
    text(34, "Hold the button", C(90, 200, 255));
    text(43, "2 sec to fix it.", C(90, 200, 255));
  }
  if (doubleBuffered) panel->flipDMABuffer();
  addrUntil = millis() + ms;
  if (!addrUntil) addrUntil = 1;
}

// ---------- WiFi setup hotspot page + API ----------
static void handleWifiPage() { touch(); sendPage(WEB_WIFI_GZ, WEB_WIFI_GZ_LEN); }
static void handleScan() { touch(); sendJson(scanJson); }
static void handleSaveWifi() {
  touch();
  String ssid = web->arg("ssid"), pass = web->arg("pass");
  if (ssid.isEmpty()) { web->send(400, "text/plain", "pick a network"); return; }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  Serial.printf("Saved home WiFi: %s\n", ssid.c_str());
  sendJson("{\"ok\":true}");
  showHotspotScreen("Saved! Restarting");
  restartRequested = true;
}
// Captive portal: every unknown URL (including the phone's internet checks) goes to the setup page,
// so the phone pops it up by itself right after joining.
static void handleCaptive() {
  lastActivity = millis();
  web->sendHeader("Location", "http://192.168.4.1/", true);
  web->send(302, "text/plain", "");
}

static void buildScanJson() {
  int n = WiFi.scanNetworks(false, false);
  String js = "[";
  std::vector<String> seen;
  for (int i = 0; i < n && seen.size() < 25; i++) {      // results come sorted strongest first
    String s = WiFi.SSID(i);
    if (s.isEmpty() || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
    seen.push_back(s);
    s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
    if (js.length() > 1) js += ",";
    js += "{\"ssid\":\"" + s + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" +
          (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  js += "]";
  WiFi.scanDelete();
  scanJson = js;
  Serial.printf("Scan: %d networks\n", (int)seen.size());
}

void portalStart() {
  playerFree();                                  // give the GIF player memory to WiFi
  showHotspotScreen("Looking for WiFi...");
  if (web) { web->stop(); delete web; web = nullptr; webStarted = false; }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  buildScanJson();
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  web = new WebServer(80);
  web->on("/", HTTP_GET, handleWifiPage);
  web->on("/api/scan", HTTP_GET, handleScan);
  web->on("/api/wifi", HTTP_POST, handleSaveWifi);
  web->on("/api/forget-wifi", HTTP_POST, handleForgetWifi);
  web->onNotFound(handleCaptive);
  web->begin();
  portalActive = true;
  restartRequested = false;
  lastActivity = millis();
  showHotspotScreen("Hold button = cancel");
  Serial.printf("WiFi setup hotspot %s / %s, free heap %u\n", apSsid.c_str(), apPass.c_str(), (unsigned)ESP.getFreeHeap());
}

// Leaving the hotspot restarts the card: turning WiFi modes on/off repeatedly in one session is
// unreliable on this core, and a restart gives the player all its memory back cleanly.
void portalStop() {
  Serial.println("WiFi setup off: restarting");
  showHotspotScreen("Restarting...");
  if (web) web->stop();
  delay(300);
  ESP.restart();
}

// ---------- Start / service ----------
void netBegin() {
  if (staSsid.isEmpty()) {
    Serial.println("No home WiFi saved (hold S1 2 s to set up)");
    showAddress(15000);                          // tells a new owner to hold the button
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staBeganAt = millis();
  Serial.printf("Joining home WiFi %s ...\n", staSsid.c_str());
}

void netService() {
  if (portalActive) {
    dns.processNextRequest();
    web->handleClient();
    if (restartRequested) { delay(800); ESP.restart(); }
    if (millis() - lastActivity > PORTAL_IDLE_MS) portalStop();
    return;
  }
  if (staSsid.isEmpty()) return;
  if (!webStarted && WiFi.status() == WL_CONNECTED) {
    startHomeWeb();
    Serial.printf("Home WiFi ok: http://%s  (http://%s.local)  RSSI %d, free heap %u\n", WiFi.localIP().toString().c_str(),
                  hostName.c_str(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
    showAddress(10000);
  }
  if (!webStarted && !staFailShown && millis() - staBeganAt > 30000) {
    staFailShown = true;
    Serial.println("Home WiFi: can't connect (still retrying)");
    showAddress(10000);
  }
  if (webStarted) {
    web->handleClient();
    if (restartRequested) { delay(800); ESP.restart(); }
    if (holdPlayback && millis() - lastActivity > HOLD_IDLE_MS) {   // phone went away mid-upload
      holdPlayback = false;
      loadPlaylist();
      playIdx = 0;
    }
  }
}
