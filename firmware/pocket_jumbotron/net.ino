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
static String apSsid, apPass, hostName, staSsid, staPass, familyCode;
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
  familyCode = prefs.getString("fc", "");
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
static void text(int y, const char *s, uint16_t color) { panel->setTextColor(color); panel->setCursor(1, y); panel->print(s); }
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return panel->color565(r, g, b); }
// Pure color for fast scan (2-bit): 224 is a value whose 565-truncated table entry has both low bits set on
// every channel (255 becomes 248 for red/blue = only 1/3 lit). See the QR notes below.
static uint16_t CF(bool r, bool g, bool b) { return C(r ? 224 : 0, g ? 224 : 0, b ? 224 : 0); }

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

// QR code at the left of the panel, drawn while the panel is in fast scan (2-bit). S1 presses while the
// address is up step through the styles; the number shows bottom right. Uses the ESP32 core's QR encoder.
// Fast scan colors: the library's color table is built for 8-bit, and at 2-bit it reads only the table's two
// lowest bits, so most values come out wrong (white looked green). Use CF() pure colors only.
// 0.8.3 test: only the website-link styles scanned with the Pixel camera (they happened to show green).
struct QrStyle { bool web, r, g, b; };
static const QrStyle QR_STYLES[] = {
  {true,  0, 1, 0},       // 1: https link to go.html (forwards to http://<ip>), green: scanned in the 0.8.3 test
  {true,  1, 1, 1},       // 2: same link, white
  {false, 0, 1, 0},       // 3: plain http://<ip> (smaller code, 2 LEDs per module), green
  {false, 1, 1, 1},       // 4: plain http://<ip>, white
};
static const int QR_COUNT = sizeof(QR_STYLES) / sizeof(QR_STYLES[0]);
int qrVariant = 1;
static const QrStyle *qrs = &QR_STYLES[0];
static void qrDraw(const uint8_t *q) {        // q is an esp_qrcode_handle_t
  int n = esp_qrcode_get_size(q), qz = 4, px = (n + 2 * qz) * 2 <= 64 ? 2 : 1;
  int sz = (n + 2 * qz) * px, x0 = 1, y0 = (64 - sz) / 2;
  uint16_t light = CF(qrs->r, qrs->g, qrs->b), dark = 0;
  panel->fillRect(x0, y0, sz, sz, light);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(q, x, y)) panel->fillRect(x0 + (qz + x) * px, y0 + (qz + y) * px, px, px, dark);
}
void qrNext() { qrVariant = qrVariant % QR_COUNT + 1; }
static bool drawQr(String ip) {
  qrs = &QR_STYLES[(qrVariant - 1) % QR_COUNT];
  String url = qrs->web ? "https://hogfanboy.github.io/hd-wf2-christmas-card/go#" + ip : "http://" + ip;
  esp_qrcode_config_t cfg = {qrDraw, 5, ESP_QRCODE_ECC_LOW};   // max version 5: the website link needs 4
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
      // Pure colors only (fast scan, see above).
      panel->setTextColor(CF(1, 1, 0));
      panel->setCursor(64, 2);  panel->print("Scan with");
      panel->setCursor(64, 11); panel->print("your phone");
      panel->setTextColor(CF(0, 1, 1));
      panel->setCursor(64, 28); panel->print("or type:");
      panel->setTextColor(CF(1, 1, 1));
      int dot = ip.lastIndexOf('.', ip.lastIndexOf('.') - 1);   // split 192.168. / 50.16
      panel->setCursor(64, 38); panel->print(ip.substring(0, dot + 1));
      panel->setCursor(64, 47); panel->print(ip.substring(dot + 1));
      panel->setTextColor(CF(0, 0, 1));
      panel->setCursor(64, 56); panel->print(panel->calculated_refresh_rate); panel->print("Hz");
      panel->setCursor(110, 56); panel->print(qrVariant); panel->print("/"); panel->print(QR_COUNT);
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
  // Test: lets the website read this directly where the browser allows it (Chrome "local network access").
  web->sendHeader("Access-Control-Allow-Origin", "https://hogfanboy.github.io");
  web->sendHeader("Access-Control-Allow-Private-Network", "true");
  char buf[480];
  snprintf(buf, sizeof(buf), "{\"fw\":\"%s\",\"name\":\"%s\",\"total\":%u,\"used\":%u,\"files\":%d,\"items\":%u,\"brightness\":%u,\"heap\":%u,"
           "\"tz\":\"%s\",\"nightFrom\":%d,\"nightTo\":%d,\"nightLevel\":%u,\"night\":%s,\"time\":%lu,\"fc\":\"%s\"}",
           FW_VERSION, hostName.c_str(), (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(), countPictures(),
           (unsigned)playlist.size(), brightnessPct, (unsigned)ESP.getFreeHeap(), tzString.c_str(), nightFrom, nightTo, nightLevel,
           nightNow ? "true" : "false", timeValid() ? (unsigned long)time(nullptr) : 0UL, familyCode.c_str());
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
  LittleFS.remove("/show.txt");
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
    String low = name; low.toLowerCase();
    if (!low.endsWith(".gif") && !low.endsWith(".pjm")) name += ".gif";
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

// The show list (see show.ino), sent as plain text after the pictures.
static void handleShowGet() {
  touch();
  File f = LittleFS.open("/show.txt", "r");
  if (!f) {                                        // older content: every picture, automatic timing
    String s;
    for (auto &it : playlist) if (it.type == 'p') s += "pic\t0\t" + it.file.substring(6) + "\n";
    web->send(200, "text/plain", s);
    return;
  }
  web->streamFile(f, "text/plain");
  f.close();
}
// One stored picture, so the page can load the current show back for editing.
static void handleFileGet() {
  touch();
  String n = web->arg("name");
  if (n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 || n.isEmpty()) { web->send(400, "text/plain", "bad name"); return; }
  File f = LittleFS.open("/gifs/" + n, "r");
  if (!f) { web->send(404, "text/plain", "not found"); return; }
  web->streamFile(f, "application/octet-stream");
  f.close();
}
static void handleShowPost() {
  touch();
  String body = web->arg("plain");
  if (body.length() > 16000) { web->send(400, "text/plain", "show too long"); return; }
  File f = LittleFS.open("/show.txt", "w");
  if (!f || f.print(body) != body.length()) { web->send(500, "text/plain", "write failed"); return; }
  f.close();
  sendJson("{\"ok\":true}");
}

// Time zone (POSIX TZ string from the page) and night mode hours.
static void handleSettings() {
  touch();
  if (web->hasArg("tz") && web->arg("tz").length() < 60) { tzString = web->arg("tz"); prefs.putString("tz", tzString); timeBegin(); }
  if (web->hasArg("fc")) {                         // family code for encrypted packs: A-Z 0-9 only
    String c = web->arg("fc"), k;
    for (char ch : c) if (isalnum((unsigned char)ch) && k.length() < 32) k += (char)toupper(ch);
    familyCode = k; prefs.putString("fc", familyCode);
  }
  if (web->hasArg("nf")) { nightFrom = constrain((int)web->arg("nf").toInt(), -1, 23); prefs.putChar("nfrom", nightFrom); }
  if (web->hasArg("nt")) { nightTo = constrain((int)web->arg("nt").toInt(), -1, 23); prefs.putChar("nto", nightTo); }
  if (web->hasArg("nl")) { nightLevel = constrain((int)web->arg("nl").toInt(), 0, 100); prefs.putUChar("nlevel", nightLevel); }
  forceNightCheck();
  applyBrightness();                               // night level may have changed while it's night
  sendJson("{\"ok\":true}");
}

static void handleBrightness() {
  touch();
  brightnessPct = constrain((int)web->arg("v").toInt(), 10, 100);
  prefs.putUChar("bright", brightnessPct);
  applyBrightness();
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

// Bridge (test): the website (https) can't call the card (http) directly, so it opens this page in a tab
// and posts requests to it; the page runs them against the card's own API and posts the answers back.
// Only the website's origin (and the local preview) is listened to.
static const char BRIDGE_HTML[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Pocket Jumbotron bridge</title>
<body style="font:15px system-ui,sans-serif;background:#111;color:#ccc;padding:16px">
<p id=s>Connecting to the website... keep this tab open.</p><script>
const OK=['https://hogfanboy.github.io','http://localhost:8080'],out=window.opener,s=document.getElementById('s');
let peer=null,n=0;
function say(m){if(out&&peer)out.postMessage(m,peer);}
addEventListener('message',async e=>{
 if(!OK.includes(e.origin))return;
 if(!peer){peer=e.origin;}
 const d=e.data||{};
 if(d.hello){say({ready:true,host:location.host});s.textContent='Connected to '+peer+'. Keep this tab open.';return;}
 if(d.probe){say({id:d.id,bytes:d.probe.byteLength});return;}
 if(typeof d.path!=='string'||!d.path.startsWith('/api/'))return say({id:d.id,error:'bad path'});
 try{let b=d.body;
  if(d.fields){b=new FormData();for(const f of d.fields)f.name?b.append(f.key,new Blob([f.value]),f.name):b.append(f.key,f.value);}
  const r=await fetch(d.path,{method:d.method||'GET',body:b});
  say({id:d.id,status:r.status,text:await r.text()});s.textContent='Connected to '+peer+'. Requests: '+(++n);
 }catch(err){say({id:d.id,error:String(err)});}
});
if(out)for(const o of OK)try{out.postMessage({ready:false,host:location.host},o)}catch(e){}
</script>)HTML";
static void handleBridge() { touch(); web->sendHeader("Cache-Control", "no-store"); web->send_P(200, "text/html", BRIDGE_HTML); }

static void startHomeWeb() {
  web = new WebServer(80);
  web->on("/", HTTP_GET, handleIndex);
  web->on("/bridge", HTTP_GET, handleBridge);
  web->on("/api/info", HTTP_GET, handleInfo);
  web->on("/api/clear", HTTP_POST, handleClear);
  web->on("/api/upload", HTTP_POST, handleUploadDone, handleUploadData);
  web->on("/api/play", HTTP_POST, handlePlay);
  web->on("/api/brightness", HTTP_POST, handleBrightness);
  web->on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateData);
  web->on("/api/forget-wifi", HTTP_POST, handleForgetWifi);
  web->on("/api/show", HTTP_GET, handleShowGet);
  web->on("/api/show", HTTP_POST, handleShowPost);
  web->on("/api/settings", HTTP_POST, handleSettings);
  web->on("/api/file", HTTP_GET, handleFileGet);
  web->onNotFound([]() { web->send(404, "text/plain", "not found"); });
  web->begin();
  MDNS.begin(hostName.c_str());
  MDNS.addService("http", "tcp", 80);
  webStarted = true;
  timeBegin();
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
