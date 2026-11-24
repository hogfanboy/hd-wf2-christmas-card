// ---------- Radar (a weather screen: the last hour of rain/snow radar over a dim map, as a loop) ----------
// Only plays when rain or snow is on the way (weather.ino: same test as the "rain on the way" screen).
// Radar: RainViewer free API over plain http (no TLS memory): api.rainviewer.com/public/weather-maps.json lists the
// last 2 hours (13 frames, 10 min apart); one 256x256 tile per frame (color scheme 2). Range (item rzoom = the LED's
// map zoom L): 7 close (10-mile ring), 6 normal (25), 5 wide (50), 4 regional (100); the tile is zoom L+1 shrunk
// 2:1, except close (zoom 7 = the free tier's maximum, used 1:1). Home sits right of the middle (RD_HX) because
// weather mostly comes from the west: the tile is centered RD_HX - W/2 LEDs west of home. Personal use, credit "RainViewer" (on the page), 100 requests/min per IP.
// Memory: PNGdec is one ~45 KB block (32 KB zlib window), which only fits after playerFree(); so frames are fetched
// in one go just before the screen plays, then the player gets its memory back. The tile streams from the socket
// through an 8 KB window (PNGdec only seeks back inside its last read). The middle 256x128 is shrunk 2:1 keeping
// the strongest pixel (1:1 for close), each LED becomes one of 15 RainViewer levels (4 bits), so a frame is a 4 KB file in /rdr
// (named by its time). Only frames not seen yet are fetched: normally 1 per 10 minutes, ~1 s.
// Map: the page builds it from OpenStreetMap when the show is sent (web/app/3-weather.js radarMap) and uploads it
// as /gifs/radar.map (radar2.map, radar3.map for more towns): "PJRM", float lat, float lon, uint8 L, uint8 RD_HX,
// 2 spare bytes, then one class per LED row by row: 0 nothing, 1 water, 2 shore, 3 interstate, 4 state line, 5 county. Measured on the probe
// (firmware/probes/radar_probe/NOTES.md on branch claude/inspiring-perlman-7d2eaa): 0.4 s per request, 0.8 s to
// stream and decode a 26 KB heavy-rain tile.
static const uint8_t RD_NF = 7;                 // frames in the loop (the last hour)
static const uint16_t RD_FRAME_MS = 450, RD_HOLD_MS = 3000;   // loop speed, how long the newest frame stays
static const uint8_t RD_MIN_ALPHA = 96;         // fainter radar stays map
static const uint8_t RD_HX = 85;                // home's column (home row = H / 2)
static uint8_t rdRingMiles(uint8_t z) { return z >= 7 ? 10 : z == 6 ? 25 : z == 5 ? 50 : 100; }
static const uint32_t RD_C_WATER = 0x000A24, RD_C_SHORE = 0x003C82, RD_C_ROAD = 0x2C2C2C, RD_C_ROAD_DIM = 0x181818,
                      RD_C_STATE = 0x460046, RD_C_COUNTY = 0x1E1E1E,
                      RD_C_RING = 0x3C3C3C, RD_C_TIME_NEW = 0xFFC83C, RD_C_TIME_OLD = 0x787878;
// RainViewer "Universal Blue" levels (rainviewer.com/files/rainviewer_api_colors_table.csv): the tile color each
// level is matched by, and the saturated LED color it is shown in. 0 = none, 1-10 rain, 11-15 snow.
static const uint32_t RD_MATCH[16] = {0, 0xDED097, 0x6CD1EB, 0x00A3E0, 0x005A8C, 0xFFE000, 0xFF9500, 0xEB2800, 0x8F0000,
                                      0xFF78FF, 0xFFFFFF, 0xC7FFFF, 0x98D8FF, 0x5895FF, 0x2262FF, 0x0000FF};
static const uint32_t RD_LED[16] = {0, 0x786030, 0x5AD2FF, 0x008CFF, 0x003CC8, 0xFFDC00, 0xFF8200, 0xFF1E00, 0x960000,
                                    0xFF50FF, 0xFFFFFF, 0xBEFFFF, 0x8CBEFF, 0x5A78FF, 0x2846FF, 0x0000FF};
static uint8_t rdF = 2;                         // tile pixels per LED (2, or 1 for close)
static struct { String key; uint32_t at = 0, failAt = 0; uint8_t n = 0; uint32_t t[RD_NF]; } rd;

static uint8_t rdLevel(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t best = 1; uint32_t bd = UINT32_MAX;
  for (uint8_t i = 1; i < 16; i++) {
    int dr = (int)(RD_MATCH[i] >> 16) - r, dg = (int)((RD_MATCH[i] >> 8) & 255) - g, db = (int)(RD_MATCH[i] & 255) - b;
    uint32_t d = dr * dr + dg * dg + db * db;
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}
// ---- Decoding one tile straight from the network ----
static PNG *rdPng = nullptr;
static uint8_t *rdDec = nullptr;                // one level per LED while decoding (8 KB)
static uint8_t rdBest[W][4];                    // strongest pixel of each 2x2 block, per column (RGBA)
static int rdDrawLine(PNGDRAW *d) {
  const int f = rdF, y = d->y - (rdPng->getHeight() - f * H) / 2;
  if (y < 0 || y >= f * H) return 1;
  const int x0 = (d->iWidth - f * W) / 2;
  if (y % f == 0) memset(rdBest, 0, sizeof(rdBest));
  for (int X = 0; X < W; X++)
    for (int k = 0; k < f; k++) {
      int x = x0 + X * f + k;
      if (x < 0 || x >= d->iWidth) continue;
      uint8_t r, g, b, a;
      if (d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA && d->iBpp == 8) { const uint8_t *p = d->pPixels + x * 4; r = p[0]; g = p[1]; b = p[2]; a = p[3]; }
      else if (d->iPixelType == PNG_PIXEL_INDEXED) {
        int i = d->iBpp == 8 ? d->pPixels[x] : d->iBpp == 4 ? (d->pPixels[x >> 1] >> ((~x & 1) * 4)) & 15 : 0;
        r = d->pPalette[i * 3]; g = d->pPalette[i * 3 + 1]; b = d->pPalette[i * 3 + 2]; a = d->iHasAlpha ? d->pPalette[768 + i] : 255;
      } else return 0;
      if (a > rdBest[X][3]) { rdBest[X][0] = r; rdBest[X][1] = g; rdBest[X][2] = b; rdBest[X][3] = a; }
    }
  if (y % f == f - 1) {
    uint8_t *row = rdDec + (y / f) * W;
    for (int X = 0; X < W; X++) row[X] = rdBest[X][3] >= RD_MIN_ALPHA ? rdLevel(rdBest[X][0], rdBest[X][1], rdBest[X][2]) : 0;
  }
  return 1;
}
// PNGdec reads up to 2 KB at a time and only seeks back inside the block it just read, so a window that starts
// at the last read position is enough (no buffer for the whole file).
static struct { WiFiClient *c; int32_t size, start, len; uint8_t *buf; } rdNet;
static const int32_t RD_WIN = 8192;
static void *rdOpen(const char *, int32_t *size) { *size = rdNet.size; return &rdNet; }
static void rdClose(void *) {}
static int32_t rdSeek(PNGFILE *f, int32_t pos) { f->iPos = pos; return pos; }
static int32_t rdRead(PNGFILE *f, uint8_t *dst, int32_t len) {
  int32_t pos = f->iPos;
  if (pos < rdNet.start || pos >= rdNet.size) return 0;
  if (len > rdNet.size - pos) len = rdNet.size - pos;
  if (pos + len > rdNet.start + RD_WIN) {                     // make room: the window now starts at this read
    int32_t keep = rdNet.start + rdNet.len - pos;
    if (keep > 0) memmove(rdNet.buf, rdNet.buf + (pos - rdNet.start), keep);
    else {                                                    // skipping ahead: drop the bytes in between
      for (int32_t skip = -keep; skip > 0;) {
        uint8_t tmp[256]; int n = rdNet.c->readBytes(tmp, std::min<int32_t>(skip, sizeof(tmp)));
        if (n <= 0) return 0;
        skip -= n;
      }
      keep = 0;
    }
    rdNet.start = pos; rdNet.len = keep;
  }
  uint32_t t = millis();
  while (rdNet.start + rdNet.len < pos + len && millis() - t < 8000) {
    int n = rdNet.c->readBytes(rdNet.buf + rdNet.len, std::min<int32_t>(RD_WIN - rdNet.len, pos + len - rdNet.start - rdNet.len));
    if (n > 0) { rdNet.len += n; t = millis(); } else delay(2);
  }
  int32_t have = std::min<int32_t>(len, rdNet.start + rdNet.len - pos);
  if (have <= 0) return 0;
  memcpy(dst, rdNet.buf + (pos - rdNet.start), have);
  f->iPos = pos + have;
  return have;
}
static String rdFile(uint32_t t) { return "/rdr/" + String(t) + ".bin"; }
// One tile -> /rdr/<time>.bin (two LEDs per byte, high nibble first). False if it failed.
static bool rdFetchTile(const String &path, uint32_t t, float lat, float lon, uint8_t z) {
  char url[160];
  snprintf(url, sizeof(url), "http://tilecache.rainviewer.com%s/256/%u/%.4f/%.4f/2/0_1.png", path.c_str(), (unsigned)z, lat, lon);
  uint32_t t0 = millis();
  HTTPClient http; http.setConnectTimeout(4000); http.setTimeout(5000);
  bool ok = false;
  int code = -1, len = 0;
  if (http.begin(url)) {
    code = http.GET(); len = http.getSize();
    if (code == 200 && len > 0 && len <= 60000) {
      rdNet = {http.getStreamPtr(), len, 0, 0, rdNet.buf};
      memset(rdDec, 0, W * H);
      if (rdPng->open("tile", rdOpen, rdClose, rdRead, rdSeek, rdDrawLine) == PNG_SUCCESS) { ok = rdPng->decode(nullptr, 0) == PNG_SUCCESS; rdPng->close(); }
    }
    http.end();
  }
  if (ok) {
    for (int i = 0; i < W * H; i += 2) rdDec[i >> 1] = (rdDec[i] << 4) | rdDec[i + 1];   // pack in place (writes trail reads)
    File f = LittleFS.open(rdFile(t), "w");
    ok = f && f.write(rdDec, W * H / 2) == W * H / 2;
    if (f) f.close();
    if (!ok) LittleFS.remove(rdFile(t));
  }
  Serial.printf("  radar tile %u: HTTP %d, %d B, %u ms %s\n", (unsigned)t, code, len, (unsigned)(millis() - t0), ok ? "" : "FAILED");
  return ok;
}
// Brings the frames up to date (at most every 5 minutes). Needs the player's memory for a moment.
// The view's middle for home at (RD_HX, H / 2) on LED zoom z (same as the page's radarView).
static float rdCenterLon(float lon, uint8_t z) { return lon - (RD_HX - W / 2) * 360.0f / (256.0f * (1 << z)); }
static String rdKey(float lat, float lon, uint8_t z) { return String(lat, 4) + "," + String(lon, 4) + "," + z; }
static void radarRefresh(float lat, float lon, uint8_t z) {
  String key = rdKey(lat, lon, z);
  if (rd.key != key) {                           // another town or range: the old frames are no use
    File dir = LittleFS.open("/rdr");
    std::vector<String> old;
    if (dir && dir.isDirectory()) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) old.push_back(String("/rdr/") + f.name());
    dir.close();
    for (auto &n : old) LittleFS.remove(n);
    rd = {}; rd.key = key;
  }
  if (rd.at && millis() - rd.at < 5UL * 60 * 1000) return;
  if (rd.failAt && millis() - rd.failAt < 5UL * 60 * 1000) return;
  if (WiFi.status() != WL_CONNECTED) return;
  uint32_t t0 = millis();
  // the frame list (~1.5 KB)
  HTTPClient http; http.setConnectTimeout(4000); http.setTimeout(5000);
  String body;
  int code = -1;
  if (http.begin("http://api.rainviewer.com/public/weather-maps.json")) { code = http.GET(); if (code == 200) body = http.getString(); http.end(); }
  int past = body.indexOf("\"past\""), end = body.indexOf(']', past);
  if (code != 200 || past < 0 || end < 0) { Serial.printf("Radar list: HTTP %d\n", code); rd.failAt = millis() | 1; return; }
  uint32_t times[16]; String paths[16]; int n = 0;
  for (int p = past; n < 16;) {
    int t = body.indexOf("\"time\":", p); if (t < 0 || t > end) break;
    int q = body.indexOf("\"path\":\"", t); if (q < 0 || q > end) break;
    times[n] = body.substring(t + 7).toInt(); paths[n] = body.substring(q + 8, body.indexOf('"', q + 8)); n++; p = q + 8;
  }
  body = String();
  int first = std::max(0, n - RD_NF);
  // drop frames that are too old, then fetch the missing ones
  LittleFS.mkdir("/rdr");
  {
    File dir = LittleFS.open("/rdr");
    std::vector<String> old;
    if (dir && dir.isDirectory())
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        uint32_t ft = String(f.name()).toInt(); bool keep = false;
        for (int i = first; i < n; i++) keep |= times[i] == ft;
        if (!keep) old.push_back(String("/rdr/") + f.name());
      }
    dir.close();
    for (auto &o : old) LittleFS.remove(o);
  }
  int missing = 0;
  for (int i = first; i < n; i++) missing += !LittleFS.exists(rdFile(times[i]));
  int fetched = 0;
  if (missing) {
    size_t room = LittleFS.totalBytes() - LittleFS.usedBytes();
    playerFree();
    rdPng = new (std::nothrow) PNG();               // the big one first, while the biggest free block is there
    rdNet.buf = rdPng ? (uint8_t *)malloc(RD_WIN) : nullptr;
    rdDec = rdNet.buf ? (uint8_t *)malloc(W * H) : nullptr;
    Serial.printf("Radar: %d frames to fetch, PNG %s, biggest free block %u\n", missing, rdDec ? "ok" : "NO MEMORY",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    rdF = z >= 7 ? 1 : 2;
    for (int i = first; i < n && rdDec; i++) {
      if (LittleFS.exists(rdFile(times[i]))) continue;
      if (room < 4096 + 32768) { Serial.println("  radar: storage full"); break; }
      if (!rdFetchTile(paths[i], times[i], lat, rdCenterLon(lon, z), std::min<uint8_t>(7, z + 1))) break;   // the service or WiFi is having trouble: try later
      room -= 4096; fetched++;
    }
    delete rdPng; rdPng = nullptr;
    free(rdNet.buf); rdNet.buf = nullptr;
    free(rdDec); rdDec = nullptr;
    if (!playerAlloc()) { Serial.println("Player memory lost after radar: restarting"); delay(200); ESP.restart(); }
  }
  rd.n = 0;
  for (int i = first; i < n; i++) if (LittleFS.exists(rdFile(times[i]))) rd.t[rd.n++] = times[i];
  if (fetched < missing) rd.failAt = millis() | 1; else { rd.at = millis(); rd.failAt = 0; }
  Serial.printf("Radar: %d listed, %u kept, %d of %d fetched, %u ms, free heap %u\n", n, rd.n, fetched, missing,
                (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
}
// ---- Drawing ----
static uint16_t rdGlyph(char c) {                // 3x5 font for the small labels (crisp, 1 px gap)
  static const char *chars = "0123456789:- .ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  static const uint16_t bits[] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7292, 0x7BEF, 0x7BCF, 0x0410, 0x01C0, 0x0000, 0x0002,
    0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x2B6A,
    0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7};
  const char *p = strchr(chars, toupper(c));
  return p && c ? bits[p - chars] : 0;
}
static int rdTextW(const String &s) { return s.length() * 4 - 1; }
static void rdText(const String &s, int x, int y, uint32_t c) {   // on a black box, so it reads over the radar
  for (int yy = y - 1; yy < y + 6; yy++) for (int xx = x - 1; xx <= x + rdTextW(s); xx++) px(xx, yy, 0);
  for (size_t i = 0; i < s.length(); i++) {
    uint16_t g = rdGlyph(s[i]);
    for (int k = 0; k < 15; k++) if (g & (0x4000 >> k)) px(x + i * 4 + k % 3, y + k / 3, c);
  }
}
static float rdMilesPerLed(float lat, uint8_t z) { return 156543.03f / (1 << z) * cosf(lat * DEG_TO_RAD) / 1609.34f; }
// The page's map for this town (header must match), or nullptr. 8 KB, freed by the caller.
static uint8_t *rdLoadMap(float lat, float lon, uint8_t z) {
  for (const char *name : {"/gifs/radar.map", "/gifs/radar2.map", "/gifs/radar3.map"}) {
    File f = LittleFS.open(name, "r");
    if (!f) continue;
    char head[16];
    float mlat, mlon;
    bool match = f.size() == 16 + W * H && f.read((uint8_t *)head, 16) == 16 && !memcmp(head, "PJRM", 4);
    if (match) { memcpy(&mlat, head + 4, 4); memcpy(&mlon, head + 8, 4); match = fabsf(mlat - lat) < 0.01f && fabsf(mlon - lon) < 0.01f && (uint8_t)head[12] == z && (uint8_t)head[13] == RD_HX; }
    uint8_t *m = match ? (uint8_t *)malloc(W * H) : nullptr;
    if (m && f.read(m, W * H) != W * H) { free(m); m = nullptr; }
    f.close();
    if (m) return m;
  }
  return nullptr;
}
static String rdClock(uint32_t t, bool h12) {
  time_t tt = t; struct tm lt; localtime_r(&tt, &lt);
  char s[8];
  if (h12) snprintf(s, sizeof(s), "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
  else snprintf(s, sizeof(s), "%d:%02d", lt.tm_hour, lt.tm_min);
  return s;
}
// One picture: map, dotted ring, radar (px = 4 KB frame or nullptr), home cross (blinks off when homeOn is false),
// the radar time top right (yellow = newest frame, gray = older) or what is going on instead, the ring's miles
// bottom right. Labels stay on the right, over weather that has passed; no town name (the cross is home).
static void rdCompose(const uint8_t *map, const uint8_t *ring, const uint8_t *frame, uint8_t z,
                      const String &corner, uint32_t cornerC, bool homeOn) {
  const uint32_t road = z <= 5 ? RD_C_ROAD_DIM : RD_C_ROAD;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint32_t c = 0;
      uint8_t k = map ? map[y * W + x] : 0;
      if (k == 1) c = RD_C_WATER; else if (k == 2) c = RD_C_SHORE; else if (k == 3) c = road; else if (k == 4) c = RD_C_STATE;
      else if (k == 5) c = RD_C_COUNTY;
      else if (ring[(y * W + x) >> 3] & (0x80 >> (x & 7))) c = RD_C_RING;
      if (frame) { int i = y * W + x; uint8_t lv = (frame[i >> 1] >> ((~i & 1) * 4)) & 15; if (lv) c = RD_LED[lv]; }
      px(x, y, c);
    }
  const int cx = RD_HX, cy = H / 2;
  for (int d = -3; d <= 3; d++) { px(cx + d, cy - 1, 0); px(cx + d, cy + 1, 0); px(cx - 1, cy + d, 0); px(cx + 1, cy + d, 0); }   // black ring
  if (homeOn) for (int d = -2; d <= 2; d++) { px(cx + d, cy, 0xFFFFFF); px(cx, cy + d, 0xFFFFFF); }
  else px(cx, cy, 0);
  String sc = String(rdRingMiles(z)) + " MI";
  rdText(sc, W - rdTextW(sc) - 1, H - 6, RD_C_TIME_OLD);
  if (corner.length()) rdText(corner, W - rdTextW(corner) - 1, 1, cornerC);
}
// The dotted ring around home as a bitmask (1 KB, freed by the caller).
static uint8_t *rdRing(float lat, uint8_t z) {
  uint8_t *ring = (uint8_t *)calloc(1, W * H / 8);
  if (!ring) return nullptr;
  float r = rdRingMiles(z) / rdMilesPerLed(lat, z);
  for (int t = 0; t < 1440; t++) {
    float a = t * TWO_PI / 1440; int x = lroundf(RD_HX + r * cosf(a)), y = lroundf(H / 2 + r * sinf(a));
    if (x >= 0 && x < W && y >= 0 && y < H && ((x + y) & 1) == 0) ring[(y * W + x) >> 3] |= 0x80 >> (x & 7);
  }
  return ring;
}
// The radar screen for a weather item (lat/lon = the item's town). True = interrupted (button, new show).
static bool playRadar(const ShowItem &it, float lat, float lon) {
  const uint8_t z = it.rzoom;
  uint8_t *ring = rdRing(lat, z);
  if (!ring) return false;
  uint8_t *map = rdLoadMap(lat, lon, z);
  bool h12 = it.extra.startsWith("F");
  bool due = !(rd.key == rdKey(lat, lon, z) && rd.at && millis() - rd.at < 5UL * 60 * 1000);
  if (due) {                                      // the map with "RADAR" while the frames come in (1-9 s)
    shownValid = false;
    rdCompose(map, ring, nullptr, z, "RADAR", RD_C_TIME_OLD, true);
    pushCanvas();
    free(map); map = nullptr;                      // give the fetch every byte; read it again after
    free(ring); ring = nullptr;
    radarRefresh(lat, lon, z);
    ring = rdRing(lat, z);
    if (!ring) return false;
    map = rdLoadMap(lat, lon, z);
  }
  uint8_t *frame = rd.n ? (uint8_t *)malloc(W * H / 2) : nullptr;
  bool stop = false;
  if (frame) {
    // two loops (oldest -> newest, then the newest held with the home blinking), or the item's seconds if longer
    uint32_t loopMs = (rd.n - 1) * RD_FRAME_MS + RD_HOLD_MS;
    uint32_t total = std::max<uint32_t>((it.sec ? it.sec : 10) * 1000UL, 2 * loopMs), start = millis();
    int shownFi = -1, loaded = -1; bool shownHome = true;
    shownValid = false;
    while (!stop && millis() - start < total) {
      uint32_t inLoop = (millis() - start) % loopMs;
      int fi = std::min<int>(rd.n - 1, inLoop / RD_FRAME_MS);
      bool last = fi == rd.n - 1, home = !last || ((inLoop - fi * RD_FRAME_MS) / 400) % 2 == 0;
      if (fi != shownFi || home != shownHome) {
        if (fi != loaded) {
          File f = LittleFS.open(rdFile(rd.t[fi]), "r");
          if (!f || f.read(frame, W * H / 2) != W * H / 2) memset(frame, 0, W * H / 2);
          if (f) f.close();
          loaded = fi;
        }
        rdCompose(map, ring, frame, z, rdClock(rd.t[fi], h12), last ? RD_C_TIME_NEW : RD_C_TIME_OLD, home);
        pushCanvas();
        shownFi = fi; shownHome = home;
      }
      stop = waitMs(20);
    }
  }
  free(frame); free(map); free(ring);
  return stop;
}
