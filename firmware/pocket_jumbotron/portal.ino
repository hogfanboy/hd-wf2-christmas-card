// Phone setup mode: the card makes its own WiFi network and serves the setup page (web/index.html).
// Started/stopped by holding S1 for 2 s. Never touches the user's home network or the internet.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "web_index.h"

static const uint32_t PORTAL_IDLE_MS = 10UL * 60 * 1000;   // leave setup mode after 10 min without activity

static WebServer *web = nullptr;
static DNSServer dns;
static Preferences prefs;
static String apSsid, apPass;
static uint32_t portalLastActivity = 0;
static bool portalStopRequested = false, restartRequested = false;
static File upFile;
static String upError;
static int upCount = 0;

// ---------- Settings (NVS) ----------
void settingsLoad() {
  prefs.begin("pj", false);
  brightnessPct = prefs.getUChar("bright", 100);
  apPass = prefs.getString("appass", "");
  if (apPass.length() != 8) {                      // first boot: make this card's own hotspot password
    char buf[9];
    for (int i = 0; i < 8; i++) buf[i] = '0' + (esp_random() % 10);
    buf[8] = 0;
    apPass = buf;
    prefs.putString("appass", apPass);
  }
  uint64_t mac = ESP.getEfuseMac();                // bytes in order, lowest byte first
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "PocketJumbo-%02X%02X", (unsigned)((mac >> 32) & 0xff), (unsigned)((mac >> 40) & 0xff));
  apSsid = ssid;
}
void applyBrightness() {
  panel->setBrightness8((uint16_t)MAX_BRIGHTNESS * brightnessPct / 100);
}

// With app rollback enabled in the bootloader, a freshly installed update must confirm itself or the
// next reboot goes back to the previous version. We confirm once the panel and storage started fine.
void markAppValid() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("New firmware confirmed (rollback cancelled)");
  }
}

// ---------- Panel screen while in setup mode ----------
void showPortalScreen(const char *status) {
  panel->clearScreen();
  panel->setTextSize(1);
  panel->setTextColor(panel->color565(255, 190, 60));
  panel->setCursor(1, 0);  panel->print("PHONE SETUP");
  panel->setTextColor(panel->color565(150, 150, 170));
  panel->setCursor(1, 11); panel->print("Join WiFi:");
  panel->setTextColor(panel->color565(90, 200, 255));
  panel->setCursor(1, 20); panel->print(apSsid);
  panel->setTextColor(panel->color565(150, 150, 170));
  panel->setCursor(1, 31); panel->print("Password:");
  panel->setTextColor(panel->color565(90, 200, 255));
  panel->setCursor(1, 40); panel->print(apPass);
  panel->setTextColor(panel->color565(78, 224, 138));
  panel->setCursor(1, 54); panel->print(status);
  if (doubleBuffered) panel->flipDMABuffer();
}

// ---------- HTTP handlers ----------
static void touch() { portalLastActivity = millis(); }
static void sendJson(const String &s) { web->send(200, "application/json", s); }

static void handleIndex() {
  touch();
  web->sendHeader("Content-Encoding", "gzip");
  web->sendHeader("Cache-Control", "no-store");
  web->send_P(200, "text/html", (const char *)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static void handleInfo() {
  touch();
  int files = 0;
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory()) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) files++;
  char buf[200];
  snprintf(buf, sizeof(buf), "{\"fw\":\"%s\",\"total\":%u,\"used\":%u,\"files\":%d,\"brightness\":%u}", FW_VERSION,
           (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(), files, brightnessPct);
  sendJson(buf);
}

// "Replace everything": wipe all pictures before a new set is uploaded.
static void handleClear() {
  touch();
  File dir = LittleFS.open("/gifs");
  std::vector<String> names;
  if (dir && dir.isDirectory()) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) names.push_back(String("/gifs/") + f.name());
  dir.close();
  for (auto &n : names) LittleFS.remove(n);
  LittleFS.mkdir("/gifs");
  playlist.clear();
  upCount = 0;
  Serial.printf("Cleared %u pictures, %u KB used\n", (unsigned)names.size(), (unsigned)(LittleFS.usedBytes() / 1024));
  showPortalScreen("Receiving...");
  sendJson("{\"ok\":true}");
}

static void handleUploadData() {
  touch();
  HTTPUpload &up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    String name = up.filename;
    name.replace("/", ""); name.replace("\\", "");
    if (!name.endsWith(".gif")) name += ".gif";
    upFile = LittleFS.open("/gifs/" + name, "w");
    if (!upFile) upError = "can't create file";
    char st[22]; snprintf(st, sizeof(st), "Receiving #%d", ++upCount);
    showPortalScreen(st);
  } else if (up.status == UPLOAD_FILE_WRITE && upFile) {
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < up.currentSize + 8192) { upError = "screen is full"; upFile.close(); }
    else if (upFile.write(up.buf, up.currentSize) != up.currentSize) { upError = "write failed"; upFile.close(); }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    if (up.status == UPLOAD_FILE_ABORTED) upError = "upload interrupted";
    Serial.printf("Upload %s: %u bytes %s\n", up.filename.c_str(), (unsigned)up.totalSize, upError.c_str());
  }
}
static void handleUploadDone() {
  if (upError.length()) web->send(500, "text/plain", upError);
  else sendJson("{\"ok\":true}");
}

static void handlePlay() {
  touch();
  loadPlaylist();
  char st[22]; snprintf(st, sizeof(st), "Got %u pictures!", (unsigned)playlist.size());
  showPortalScreen(st);
  sendJson("{\"ok\":true}");
}

static void handleBrightness() {
  touch();
  int v = web->arg("v").toInt();
  brightnessPct = constrain(v, 10, 100);
  prefs.putUChar("bright", brightnessPct);
  applyBrightness();
  sendJson("{\"ok\":true}");
}

static void handleDone() {
  sendJson("{\"ok\":true}");
  portalStopRequested = true;
}

// Firmware update: written to the other app slot; the bootloader switches to it on restart.
static void handleUpdateData() {
  touch();
  HTTPUpload &up = web->upload();
  if (up.status == UPLOAD_FILE_START) {
    upError = "";
    Serial.printf("Firmware update: %s\n", up.filename.c_str());
    showPortalScreen("Updating...");
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
  if (upError.length()) { web->send(500, "text/plain", upError); showPortalScreen("Update failed"); return; }
  sendJson("{\"ok\":true}");
  showPortalScreen("Restarting...");
  restartRequested = true;
}

// Phones probe a few URLs to detect a "sign-in" WiFi; sending everything to our page pops it up.
static void handleNotFound() {
  web->sendHeader("Location", "http://192.168.4.1/", true);
  web->send(302, "text/plain", "");
}

// ---------- Start / stop / loop ----------
void portalStart() {
  Serial.printf("Setup mode: WiFi %s / %s\n", apSsid.c_str(), apPass.c_str());
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  web = new WebServer(80);
  web->on("/", HTTP_GET, handleIndex);
  web->on("/api/info", HTTP_GET, handleInfo);
  web->on("/api/clear", HTTP_POST, handleClear);
  web->on("/api/upload", HTTP_POST, handleUploadDone, handleUploadData);
  web->on("/api/play", HTTP_POST, handlePlay);
  web->on("/api/brightness", HTTP_POST, handleBrightness);
  web->on("/api/done", HTTP_POST, handleDone);
  web->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateData);
  web->onNotFound(handleNotFound);
  web->begin();
  portalActive = true;
  portalStopRequested = restartRequested = false;
  touch();
  showPortalScreen("S1 hold = exit");
  Serial.printf("Setup mode on, free heap %u\n", (unsigned)ESP.getFreeHeap());
}

void portalStop() {
  if (web) { web->stop(); delete web; web = nullptr; }
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  portalActive = false;
  loadPlaylist();
  panel->clearScreen();
  if (doubleBuffered) panel->flipDMABuffer();
  Serial.printf("Setup mode off, %u pictures\n", (unsigned)playlist.size());
}

void portalLoop() {
  dns.processNextRequest();
  web->handleClient();
  if (restartRequested) { delay(500); ESP.restart(); }
  if (portalStopRequested || millis() - portalLastActivity > PORTAL_IDLE_MS) portalStop();
}
