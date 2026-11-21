// Web API for the app, the starter page that loads the app from the website, and the built-in app (/local).

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
  char buf[480];
  snprintf(buf, sizeof(buf), "{\"api\":1,\"fw\":\"%s\",\"name\":\"%s\",\"total\":%u,\"used\":%u,\"files\":%d,\"items\":%u,\"brightness\":%u,\"heap\":%u,"
           "\"tz\":\"%s\",\"nightFrom\":%d,\"nightTo\":%d,\"nightLevel\":%u,\"night\":%s,\"time\":%lu}",
           FW_VERSION, hostName.c_str(), (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes(), countPictures(),
           (unsigned)playlist.size(), brightnessPct, (unsigned)ESP.getFreeHeap(), tzString.c_str(), nightFrom, nightTo, nightLevel,
           nightNow ? "true" : "false", timeValid() ? (unsigned long)time(nullptr) : 0UL);
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

// The card's front page is a tiny starter: it loads the app (the full page) from the website, so the app
// updates without new firmware, and keeps a copy on the phone for when the website can't be reached. The app
// then runs on the card's own address, so its /api calls go straight to the card (an https page can't call
// http devices; an http page may load https code). No app at all: the built-in copy at /local.
static const char SHELL_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Pocket Jumbotron</title><link rel=icon href=/favicon.ico></head>
<body style="background:#0b0d12;color:#9aa;font:16px system-ui,sans-serif;padding:20px"><p>Loading...</p><script>
(async()=>{
 const APP='https://hogfanboy.github.io/hd-wf2-christmas-card/app/index.html';
 let h=null,ls=null;try{ls=localStorage}catch(e){}
 try{const c=new AbortController();setTimeout(()=>c.abort(),5000);
  const r=await fetch(APP,{cache:'no-cache',signal:c.signal});
  if(r.ok){h=await r.text();try{ls&&ls.setItem('pjApp',h)}catch(e){}}}catch(e){}
 if(!h)try{h=ls&&ls.getItem('pjApp')}catch(e){}
 if(!h)return location.replace('/local');
 document.open();document.write(h);document.close();
})();
</script></body></html>)HTML";
// Browser tab icon (same as docs/favicon.svg). Chrome asks every site for /favicon.ico; a 404 = the gray globe.
static const char FAVICON_SVG[] PROGMEM = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><rect x='1' y='2' width='30' height='28' rx='5' fill='#0d0d14' stroke='#c8202f' stroke-width='2.5'/><path fill='#ffbe3c' d='M4 7.25h3.5v3.5h-3.5zM7.5 7.25h3.5v3.5h-3.5zM11 7.25h3.5v3.5h-3.5zM4 10.75h3.5v3.5h-3.5zM11 10.75h3.5v3.5h-3.5zM4 14.25h3.5v3.5h-3.5zM7.5 14.25h3.5v3.5h-3.5zM11 14.25h3.5v3.5h-3.5zM4 17.75h3.5v3.5h-3.5zM4 21.25h3.5v3.5h-3.5z'/><path fill='#4cc3ff' d='M17.5 7.25h3.5v3.5h-3.5zM21 7.25h3.5v3.5h-3.5zM24.5 7.25h3.5v3.5h-3.5zM24.5 10.75h3.5v3.5h-3.5zM24.5 14.25h3.5v3.5h-3.5zM17.5 17.75h3.5v3.5h-3.5zM24.5 17.75h3.5v3.5h-3.5zM21 21.25h3.5v3.5h-3.5z'/></svg>";
static void handleFavicon() { web->sendHeader("Cache-Control", "max-age=604800"); web->send_P(200, "image/svg+xml", FAVICON_SVG); }
static void handleShell() { touch(); web->sendHeader("Cache-Control", "no-store"); web->send_P(200, "text/html", SHELL_HTML); }

static void startHomeWeb() {
  web = new WebServer(80);
  web->on("/", HTTP_GET, handleShell);
  web->on("/local", HTTP_GET, handleIndex);
  web->on("/favicon.ico", HTTP_GET, handleFavicon);       // built-in copy of the app (no internet)
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
