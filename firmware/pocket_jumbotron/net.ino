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
  tzString = prefs.getString("tz", "");
  tzAuto = tzString.isEmpty();
  if (tzAuto) tzString = "UTC0";
  if (prefs.isKey("fc")) prefs.remove("fc");   // old family code (0.9.5 and older), no longer used
  nightFrom = prefs.getChar("nfrom", -1);
  nightTo = prefs.getChar("nto", -1);
  nightLevel = prefs.getUChar("nlevel", 0);
  autoUpdate = prefs.getBool("aupd", true);
  themeIx = prefs.getUChar("theme", 2);
  wxHome = prefs.getString("wxhome", "");
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

// Address screen: a white QR code of HTTP://<ip> on the left, "SCAN ME" and the address on one line at the right.
// The panel is in fast scan while it's up (fewer color bits, much higher refresh), which is what lets phone cameras
// read it (0.8.4). Everything here is drawn with drawPixelRGB888 in 0/255 colors: the library's color table maps
// 255 to 255, so these come out right at any bit depth (565 colors lose their low bits and went wrong at 2-bit).
// The URL is upper case so the QR encoder uses alphanumeric mode: version 1 (21 modules) with a full 4-module
// quiet zone. The screen switches every 2.5 s between a big code (2 LEDs per module, 58 px), which phones read
// from about 4 to 7 ft, and a small one (1 LED per module, 29 px) for about 1.5 to 4 ft (QR probe, 2026-11-21).
// Brightness made no difference to the distance, so it's not at full: half for the big code, a quarter for the small.
static void pfill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) panel->drawPixelRGB888(i, j, r, g, b);
}
static int qrSize = 0;                         // the big code's size: the text column goes to the right of it
static bool qrSmall = false, addrHasQr = false;
static uint32_t qrFlipAt = 0;
static void qrDraw(const uint8_t *q) {        // q is an esp_qrcode_handle_t
  int n = esp_qrcode_get_size(q), qz = 4, big = (n + 2 * qz) * 2 <= 64 ? 2 : 1, px = qrSmall ? 1 : big;
  int bigSz = (n + 2 * qz) * big, sz = (n + 2 * qz) * px, x0 = 2 + (bigSz - sz) / 2, y0 = (64 - sz) / 2;
  qrSize = bigSz;
  pfill(x0, y0, sz, sz, 255, 255, 255);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) pfill(x0 + (qz + x) * px, y0 + (qz + y) * px, px, px, 0, 0, 0);
}
static bool drawQr(String ip) {
  String url = "HTTP://" + ip;
  esp_qrcode_config_t cfg = {qrDraw, 5, ESP_QRCODE_ECC_LOW};
  return esp_qrcode_generate(&cfg, url.c_str()) == ESP_OK;
}
// 4x7 digits (and '.') so a whole IP address fits on one line in 60 px.
static int digits47(const String &s, int x, int y, uint8_t r, uint8_t g, uint8_t b, bool draw) {
  static const uint8_t D[10][7] = {{6,9,9,9,9,9,6},{2,6,2,2,2,2,7},{6,9,1,2,4,8,15},{14,1,1,6,1,1,14},{2,6,10,10,15,2,2},
                                   {15,8,14,1,1,9,6},{6,8,8,14,9,9,6},{15,1,2,2,4,4,4},{6,9,9,6,9,9,6},{6,9,9,7,1,1,6}};
  for (unsigned k = 0; k < s.length(); k++) {
    char c = s[k];
    if (c == '.') { if (draw) panel->drawPixelRGB888(x, y + 6, r, g, b); x += 2; continue; }
    if (c < '0' || c > '9') continue;
    if (draw) for (int j = 0; j < 7; j++) for (int i = 0; i < 4; i++) if (D[c - '0'][j] >> (3 - i) & 1) panel->drawPixelRGB888(x + i, y + j, r, g, b);
    x += 5;
  }
  return x;
}
// Our Jumbo font straight to the panel, 1-bit (soft edges would need the in-between colors fast scan lacks).
static int pjPanel(const String &s, uint8_t font, int x, int base, uint8_t r, uint8_t g, uint8_t b, bool draw) {
  setFont(font);
  for (unsigned k = 0; k < s.length(); k++) {
    const PJGlyph &gl = pjGlyph(s[k]);
    const uint8_t *bits = pjf->bits + gl.off;
    if (draw) for (int i = 0, n = gl.w * gl.h; i < n; i++)
      if (((bits[i >> 1] >> (i & 1 ? 0 : 4)) & 15) >= 7) panel->drawPixelRGB888(x + gl.dx + i % gl.w, base + gl.dy + i / gl.w, r, g, b);
    x += gl.adv;
  }
  return x - pjf->gap;
}

// Shown at startup once WiFi connects, and on a short S1 press.
void showAddress(uint32_t ms) {
  qrSmall = false;
  drawAddress();
  qrFlipAt = millis();
  if (addrHasQr) Serial.printf("Address screen: %d-bit fast scan, big/small QR\n", fastBits);
  addrUntil = millis() + ms;
  if (!addrUntil) addrUntil = 1;
}
// While the address screen is up (loop): switch between the big and the small QR code.
void addressTick() {
  if (!addrHasQr || millis() - qrFlipAt < 2500) return;
  qrSmall = !qrSmall;
  qrFlipAt = millis();
  drawAddress();
}
static void drawAddress() {
  addrHasQr = false;
  panel->clearScreen();
  panel->setTextSize(1);
  if (staSsid.isEmpty()) {
    text(0, "POCKET JUMBOTRON", C(255, 190, 60));
    text(14, "No home WiFi yet.", C(150, 150, 170));
    text(26, "Hold the button", C(90, 200, 255));
    text(35, "2 sec to set up.", C(90, 200, 255));
  } else if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    panelSetFast(true);                       // fast scan for phone cameras (back to full color after)
    panel->clearScreen();
    panel->setBrightness8(qrSmall ? 64 : 128);
    addrHasQr = drawQr(ip);
    if (!addrHasQr) qrSize = 0;
    int cx = qrSize ? 2 + qrSize + (W - 2 - qrSize) / 2 : W / 2;      // center of the right-hand column
    int colW = W - (qrSize ? qrSize + 4 : 0);
    uint8_t tf = pjPanel("SCAN ME", 9, 0, 0, 0, 0, 0, false) <= colW - 2 ? 9 : 8;
    pjPanel("SCAN ME", tf, cx - pjPanel("SCAN ME", tf, 0, 0, 0, 0, 0, false) / 2, 17, 255, 255, 0, true);
    String l2 = qrSize ? "or type" : "On your phone open";
    pjPanel(l2, 8, cx - pjPanel(l2, 8, 0, 0, 0, 0, 0, false) / 2, 36, 0, 255, 255, true);
    int iw = digits47(ip, 0, 0, 0, 0, 0, false) - 1;
    digits47(ip, cx - iw / 2, 44, 255, 255, 255, true);
  } else {
    text(0, "POCKET JUMBOTRON", C(255, 190, 60));
    text(12, "Can't join WiFi:", C(255, 90, 110));
    text(21, staSsid.c_str(), C(255, 255, 255));
    text(34, "Hold the button", C(90, 200, 255));
    text(43, "2 sec to fix it.", C(90, 200, 255));
  }
  if (doubleBuffered) panel->flipDMABuffer();
}

// ---------- WiFi setup hotspot page + API ----------
static void handleWifiPage() { if (!touch()) return; sendPage(WEB_WIFI_GZ, WEB_WIFI_GZ_LEN); }
static void handleScan() { if (!touch()) return; sendJson(scanJson); }
static void handleSaveWifi() {
  if (!touch()) return;
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
  improvPoll();                                    // WiFi details from the install page over USB
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
    if (!quietBoot) showAddress(10000);   // not after a quiet restart (4 am)
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

// ---------- Improv WiFi over USB (the install page sends the home WiFi right after installing) ----------
// Improv Serial (improv-wifi.com): packets "IMPROV" ver type len data checksum on the USB serial port. Log lines
// on the same port are ignored by the page. On good WiFi details the card saves them, answers with its address
// and restarts into the home network (same as the hotspot setup page).
static uint8_t imBuf[200];
static uint8_t imLen = 0;
static void imSend(uint8_t type, const uint8_t *d, uint8_t n) {
  uint8_t h[9] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, n};
  uint8_t sum = 0;
  for (uint8_t b : h) sum += b;
  for (int i = 0; i < n; i++) sum += d[i];
  Serial.write(h, 9); Serial.write(d, n); Serial.write(sum); Serial.write('\n');
  Serial.flush();
}
static void imState(uint8_t s) { imSend(0x01, &s, 1); }
static void imError(uint8_t e) { imSend(0x02, &e, 1); }
static void imResult(uint8_t cmd, std::initializer_list<String> strs) {
  uint8_t d[200]; int n = 2;
  for (auto &s : strs) { int l = std::min<int>(s.length(), 60); if (n + 1 + l > (int)sizeof(d)) break; d[n++] = l; memcpy(d + n, s.c_str(), l); n += l; }
  d[0] = cmd; d[1] = n - 2;
  imSend(0x04, d, n);
}
static String myUrl() { return "http://" + WiFi.localIP().toString(); }
static void imCommand(const uint8_t *d, uint8_t n) {
  uint8_t cmd = d[0];
  if (cmd == 0x02) {                                         // current state
    if (WiFi.status() == WL_CONNECTED) { imState(0x04); imResult(0x02, {myUrl()}); } else imState(0x02);
  } else if (cmd == 0x03) {                                  // device info
    imResult(0x03, {"Pocket Jumbotron", FW_VERSION, "ESP32-S3", hostName});
  } else if (cmd == 0x04) {                                  // WiFi networks
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);    // a new card has no WiFi running yet
    int k = WiFi.scanNetworks(false, false);
    if (k < 0) {                                             // can't scan while retrying a saved network (moved house)
      WiFi.setAutoReconnect(false); WiFi.disconnect(); delay(300);
      k = WiFi.scanNetworks(false, false);
      WiFi.setAutoReconnect(true);
      if (!staSsid.isEmpty()) WiFi.begin(staSsid.c_str(), staPass.c_str());
    }
    std::vector<String> seen;
    for (int i = 0; i < k && seen.size() < 20; i++) {
      String s = WiFi.SSID(i);
      if (s.isEmpty() || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
      seen.push_back(s);
      imResult(0x04, {s, String(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
    }
    WiFi.scanDelete();
    imResult(0x04, {});
  } else if (cmd == 0x01 && n >= 3) {                        // WiFi settings: ssid, password
    uint8_t sl = d[2];
    if (!sl || 4 + sl > n || 4 + sl + d[3 + sl] > n) { imError(0x01); return; }
    String ssid((const char *)d + 3, sl);
    uint8_t pl = d[3 + sl];
    String pass((const char *)d + 4 + sl, pl);
    Serial.printf("Improv: joining %s ...\n", ssid.c_str());
    imState(0x03);
    playerFree();
    panel->fillScreen(0); panel->setFont(nullptr); panel->setTextSize(1);
    text(20, "Joining WiFi", C(90, 200, 255)); text(32, ssid.c_str(), C(255, 255, 255));
    if (!portalActive) WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostName.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(100);
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Improv: couldn't join");
      imError(0x03); imState(0x02);
      text(44, "Wrong password?", C(255, 90, 110));
      delay(1500);
      WiFi.disconnect();                                     // back to how it was; the page lets them try again
      if (!staSsid.isEmpty()) WiFi.begin(staSsid.c_str(), staPass.c_str());
      panel->clearScreen();
      if (!portalActive && !playerAlloc()) ESP.restart();
      shownValid = false;
      return;
    }
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    Serial.printf("Improv: joined, %s\n", myUrl().c_str());
    imState(0x04);
    imResult(0x01, {myUrl()});
    text(44, myUrl().substring(7).c_str(), C(60, 220, 120));
    delay(1500);
    ESP.restart();
  } else imError(0x02);
}
void improvPoll() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (imLen < 6) {
      if (b == (uint8_t)"IMPROV"[imLen]) imBuf[imLen++] = b; else imLen = b == 'I' ? (imBuf[0] = b, 1) : 0;
      continue;
    }
    imBuf[imLen++] = b;
    if (imLen > 9 && imLen == 10 + imBuf[8]) {
      uint8_t sum = 0;
      for (int i = 0; i < imLen - 1; i++) sum += imBuf[i];
      uint8_t type = imBuf[7], n = imBuf[8];
      imLen = 0;
      if (sum != imBuf[9 + n]) { imError(0x01); continue; }
      if (type == 0x03 && n >= 2) imCommand(imBuf + 9, n);
    } else if (imLen >= sizeof(imBuf)) imLen = 0;
  }
}
