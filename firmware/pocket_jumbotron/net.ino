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
  edgeShift = prefs.getBool("edgefix", true);
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");
  apPass = prefs.getString("appass", "");
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
  panel->setBrightness8((uint16_t)MAX_BRIGHTNESS * brightnessPct / 100);
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
static void text(int y, const char *s, uint16_t color) { panel->setTextColor(color); panel->setCursor(1, y); panel->print(s); }
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return panel->color565(r, g, b); }

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
    text(12, "On your phone (same", C(150, 150, 170));
    text(21, "WiFi), open browser:", C(150, 150, 170));
    String ip = WiFi.localIP().toString();
    text(34, ("http://" + ip).c_str(), C(255, 255, 255));
    text(50, staSsid.c_str(), C(78, 224, 138));
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

// ---------- Common HTTP helpers ----------
static void touch() {
  lastActivity = millis();
  Serial.printf("HTTP %s %s\n", web->method() == HTTP_POST ? "POST" : "GET", web->uri().c_str());
}
static void sendJson(const String &s) { web->send(200, "application/json", s); }
static void sendPage(const uint8_t *gz, size_t len) {
  web->sendHeader("Content-Encoding", "gzip");
  web->sendHeader("Cache-Control", "no-store");
  web->send_P(200, "text/html", (const char *)gz, len);
}

// ---------- Home-WiFi page + API ----------
static void handleIndex() { touch(); sendPage(WEB_INDEX_GZ, WEB_INDEX_GZ_LEN); }

static int countPictures() {
  int n = 0;
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory()) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) n++;
  return n;
}
static void handleInfo() {
  touch();
  char buf[220];
  snprintf(buf, sizeof(buf), "{\"fw\":\"%s\",\"total\":%u,\"used\":%u,\"files\":%d,\"brightness\":%u,\"edgefix\":%s,\"heap\":%u}", FW_VERSION,
           (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(), countPictures(), brightnessPct, edgeShift ? "true" : "false",
           (unsigned)ESP.getFreeHeap());
  sendJson(buf);
}

// "Replace everything": stop playing, then wipe all pictures. While a GIF is still open the phone is
// told to retry in a moment (the player closes it within one frame).
static void handleClear() {
  touch();
  holdPlayback = true;
  if (playing) { web->send(503, "text/plain", "busy"); return; }
  std::vector<String> names;
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory()) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) names.push_back(String("/gifs/") + f.name());
  dir.close();
  for (auto &n : names) LittleFS.remove(n);
  LittleFS.mkdir("/gifs");
  playlist.clear();
  upCount = 0;
  Serial.printf("Cleared %u pictures, %u KB used\n", (unsigned)names.size(), (unsigned)(LittleFS.usedBytes() / 1024));
  panel->clearScreen();
  text(28, "Receiving pictures", C(78, 224, 138));
  if (doubleBuffered) panel->flipDMABuffer();
  sendJson("{\"ok\":true}");
}

static void handleUploadData() {
  lastActivity = millis();
  HTTPUpload &up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    if (!holdPlayback || playing) { upError = "not ready"; return; }
    String name = up.filename;
    name.replace("/", ""); name.replace("\\", "");
    if (!name.endsWith(".gif")) name += ".gif";
    upFree = LittleFS.totalBytes() - LittleFS.usedBytes();
    upFile = LittleFS.open("/gifs/" + name, "w");
    if (!upFile) upError = "can't create file";
    char st[24]; snprintf(st, sizeof(st), "Receiving #%d", ++upCount);
    panel->clearScreen();
    text(28, st, C(78, 224, 138));
    if (doubleBuffered) panel->flipDMABuffer();
  } else if (up.status == UPLOAD_FILE_WRITE && upFile) {
    if (up.totalSize + up.currentSize + 16384 > upFree) { upError = "screen is full"; upFile.close(); }
    else if (upFile.write(up.buf, up.currentSize) != up.currentSize) { upError = "write failed"; upFile.close(); }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    if (up.status == UPLOAD_FILE_ABORTED) upError = "upload interrupted";
    Serial.printf("Upload %s: %u bytes %s\n", up.filename.c_str(), (unsigned)up.totalSize, upError.c_str());
  }
}
static void handleUploadDone() {
  touch();
  if (upError.length()) web->send(500, "text/plain", upError);
  else sendJson("{\"ok\":true}");
}

static void handlePlay() {
  touch();
  loadPlaylist();
  playIdx = 0;
  holdPlayback = false;
  panel->clearScreen();
  if (doubleBuffered) panel->flipDMABuffer();
  sendJson("{\"ok\":true}");
}

static void handleBrightness() {
  touch();
  brightnessPct = constrain((int)web->arg("v").toInt(), 10, 100);
  prefs.putUChar("bright", brightnessPct);
  applyBrightness();
  sendJson("{\"ok\":true}");
}

static void handleEdgeFix() {
  touch();
  edgeShift = web->arg("v") == "1";
  prefs.putBool("edgefix", edgeShift);
  shownValid = false;                 // redraw every pixel in the new position
  panel->clearScreen();
  sendJson("{\"ok\":true}");
}

// Firmware update: written to the other app slot; the bootloader switches to it on restart.
static void handleUpdateData() {
  lastActivity = millis();
  HTTPUpload &up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    holdPlayback = true;
    Serial.printf("Firmware update: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) upError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE && !upError.length()) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) upError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (!upError.length() && !Update.end(true)) upError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    upError = "upload interrupted";
  }
}
static void handleUpdateDone() {
  touch();
  if (upError.length()) { holdPlayback = false; web->send(500, "text/plain", upError); return; }
  sendJson("{\"ok\":true}");
  restartRequested = true;
}

static void handleForgetWifi() {
  touch();
  prefs.remove("ssid"); prefs.remove("pass");
  sendJson("{\"ok\":true}");
  restartRequested = true;
}

static void startHomeWeb() {
  web = new WebServer(80);
  web->on("/", HTTP_GET, handleIndex);
  web->on("/api/info", HTTP_GET, handleInfo);
  web->on("/api/clear", HTTP_POST, handleClear);
  web->on("/api/upload", HTTP_POST, handleUploadDone, handleUploadData);
  web->on("/api/play", HTTP_POST, handlePlay);
  web->on("/api/brightness", HTTP_POST, handleBrightness);
  web->on("/api/edgefix", HTTP_POST, handleEdgeFix);
  web->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateData);
  web->on("/api/forget-wifi", HTTP_POST, handleForgetWifi);
  web->onNotFound([]() { web->send(404, "text/plain", "not found"); });
  web->begin();
  MDNS.begin(hostName.c_str());
  MDNS.addService("http", "tcp", 80);
  webStarted = true;
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
