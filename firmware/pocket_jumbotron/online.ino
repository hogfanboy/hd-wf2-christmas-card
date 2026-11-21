// Things the card looks up on the internet (weather is in weather.ino):
//  * Where it is, once per start (http://ip-api.com, free, no key): a time zone when the page never set one, and
//    the town for "weather here" items (lat "auto" in the show line). A fresh install works with no setup.
//  * News headlines and market quotes over HTTPS from the website (docs/feeds/, rebuilt hourly by CI with
//    tools/build-feeds.mjs, so the card only reads a small text file and never talks to news or quote sites).
//    TLS needs ~40 KB more RAM than is free while a picture plays, so the player's buffers are freed for the
//    fetch and taken back right after. A fetch takes 1-3 s and happens at most every 20 minutes per feed.
#include <WiFiClientSecure.h>
#include "site_ca.h"

static const char *SITE = "https://hogfanboy.github.io/hd-wf2-christmas-card/";

// ---------- HTTPS GET from the website ----------
bool siteGet(const String &path, String &body, size_t maxLen) {
  body = "";
  if (WiFi.status() != WL_CONNECTED || !timeValid()) return false;   // checking the certificate needs the date
  playerFree();
  uint32_t t0 = millis();
  unsigned before = ESP.getFreeHeap(), block = ESP.getMaxAllocHeap();
  int code = -1;
  {
    WiFiClientSecure tls;
    tls.setCACert(SITE_CA);
    tls.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(6000);
    if (http.begin(tls, String(SITE) + path)) {
      code = http.GET();
      int len = http.getSize();
      if (code == 200 && len <= (int)maxLen) body = http.getString();
      if (body.length() > maxLen) body = "";
      http.end();
    }
  }
  Serial.printf("HTTPS %s: %d, %u bytes, %u ms, heap before %u (block %u)\n", path.c_str(), code, (unsigned)body.length(),
                (unsigned)(millis() - t0), before, block);
  if (!playerAlloc()) { Serial.println("Player memory lost after HTTPS: restarting"); delay(200); ESP.restart(); }
  return code == 200 && body.length();
}

// Small cache of website feeds (each ~0.5-2 KB). Keeps the last good copy if a refresh fails.
struct Feed { String name, body; uint32_t at = 0, failAt = 0; };
static Feed feeds[4];
static const String *feedGet(const String &name) {
  Feed *f = nullptr, *oldest = &feeds[0];
  for (auto &x : feeds) { if (x.name == name) { f = &x; break; } if (x.at < oldest->at) oldest = &x; }
  bool have = f && f->body.length();
  if (have && millis() - f->at < 20UL * 60 * 1000) return &f->body;
  if (f && f->failAt && millis() - f->failAt < 5UL * 60 * 1000) return have ? &f->body : nullptr;   // site down: don't stall
  String body;
  bool ok = siteGet("feeds/" + name, body, 8000);
  if (!f) { f = oldest; f->name = name; f->body = ""; f->at = 0; }
  if (!ok) { f->failAt = millis() | 1; return f->body.length() ? &f->body : nullptr; }
  f->body = body; f->at = millis(); f->failAt = 0;
  return &f->body;
}
static void splitLines(const String &s, std::vector<String> &out) {
  int a = 0;
  while (a < (int)s.length()) {
    int e = s.indexOf('\n', a); if (e < 0) e = s.length();
    String l = s.substring(a, e); l.trim();
    if (l.length() && l[0] != '#') out.push_back(l);
    a = e + 1;
  }
}
static String field(const String &line, int n) {           // n-th TAB field
  int a = 0;
  for (int i = 0; i < n; i++) { a = line.indexOf('\t', a); if (a < 0) return ""; a++; }
  int e = line.indexOf('\t', a);
  return line.substring(a, e < 0 ? line.length() : e);
}

// ---------- Where am I (time zone + weather town) ----------
String geoCity, geoCountry;
float geoLat = 0, geoLon = 0;
bool geoOk = false;
static uint32_t geoTry = 0;
static String jsonText(const String &s, const char *key) {   // "key":"value" -> value
  int i = s.indexOf(key);
  if (i < 0) return "";
  i += strlen(key);
  return s.substring(i, s.indexOf('"', i));
}
// IANA zone name -> POSIX TZ rule (the C library only understands POSIX). Others: the current UTC offset, fixed.
static String posixFor(const String &z, long offset) {
  static const char *const T[][2] = {
    {"America/Phoenix", "MST7"}, {"America/Boise", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Indiana/Knox", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Indiana/Tell_City", "CST6CDT,M3.2.0,M11.1.0"}, {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Indiana", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Kentucky", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"}, {"America/Menominee", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/North_Dakota", "CST6CDT,M3.2.0,M11.1.0"}, {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"}, {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"}, {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"America/Juneau", "AKST9AKDT,M3.2.0,M11.1.0"}, {"America/Sitka", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Nome", "AKST9AKDT,M3.2.0,M11.1.0"}, {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"}, {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "HST10"}, {"America/Puerto_Rico", "AST4"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"}, {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"}, {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kiev", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow", "MSK-3"}, {"Europe/Istanbul", "<+03>-3"}, {"Europe/", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Australia/Brisbane", "AEST-10"}, {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"}, {"Australia/Darwin", "ACST-9:30"},
    {"Australia/Perth", "AWST-8"}, {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  };
  for (auto &t : T) if (z.startsWith(t[0])) return t[1];
  long m = labs(offset) / 60;
  char b[24];
  snprintf(b, sizeof(b), m % 60 ? "<LOC>%c%ld:%02ld" : "<LOC>%c%ld", offset > 0 ? '-' : '+', m / 60, m % 60);   // POSIX sign is reversed
  return b;
}
// Called between show items. One small http request per start (retried every 10 min until it works).
void geoService() {
  if (geoOk || WiFi.status() != WL_CONNECTED) return;
  if (geoTry && millis() - geoTry < 10UL * 60 * 1000) return;
  geoTry = millis() | 1;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  if (!http.begin("http://ip-api.com/json/?fields=status,countryCode,city,lat,lon,timezone,offset")) return;
  int code = http.GET();
  String b = code == 200 ? http.getString() : "";
  http.end();
  if (b.indexOf("\"success\"") < 0) { Serial.printf("Location: HTTP %d, no answer\n", code); return; }
  int la = b.indexOf("\"lat\":"), lo = b.indexOf("\"lon\":"), of = b.indexOf("\"offset\":");
  if (la < 0 || lo < 0) return;
  geoLat = b.substring(la + 6).toFloat(); geoLon = b.substring(lo + 6).toFloat();
  geoCity = jsonText(b, "\"city\":\""); geoCountry = jsonText(b, "\"countryCode\":\"");
  geoOk = true;
  String zone = jsonText(b, "\"timezone\":\"");
  Serial.printf("Location: %s, %s (%.2f, %.2f) %s\n", geoCity.c_str(), geoCountry.c_str(), geoLat, geoLon, zone.c_str());
  if (tzAuto) {                                  // the page never set a time zone: use this one (not saved)
    tzString = posixFor(zone, of < 0 ? 0 : b.substring(of + 9).toInt());
    setenv("TZ", tzString.c_str(), 1); tzset();
    Serial.printf("Time zone (automatic): %s\n", tzString.c_str());
  }
}
bool geoUsesF() { return !geoOk || geoCountry == "US" || geoCountry == "LR" || geoCountry == "MM" || geoCountry == "PR"; }

// ---------- News ticker: headlines scroll under a title bar ----------
static const char *newsTitle(const String &topic) {
  if (topic == "world") return "WORLD NEWS"; if (topic == "business") return "BUSINESS"; if (topic == "sports") return "SPORTS";
  if (topic == "tech") return "TECH NEWS"; if (topic == "science") return "SCIENCE"; return "NEWS";
}
static void newsBar(const char *title, const String &src) {
  cg.rgb(0x1E1E32); cg.fillRect(0, 0, W, 10, 0);
  label(title, 2, 1, 0xFFBE3C);
  String right = src;
  if (timeValid()) {
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char hm[8]; snprintf(hm, sizeof(hm), "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    right = src + " " + hm;
  }
  label(right, W - 1 - labelW(right), 1, 0x8C8CA0);
}
// it.extra = topic (top, world, business, sports, tech, science). sec 0 = three headlines, else that long.
static void playNews(const ShowItem &it) {
  const String *b = feedGet("news-" + (it.extra.length() ? it.extra : String("top")) + ".txt");
  if (!b) return;
  std::vector<String> lines;
  splitLines(*b, lines);
  if (lines.empty()) return;
  static uint16_t next = 0;                      // each pass carries on with the next headlines
  const char *title = newsTitle(it.extra);
  pjLabels = isPJ(it.font);
  effectInit(it); FxGuard fxg;
  uint32_t total = it.sec * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  for (int done = 0; it.sec || done < 3; done++) {
    const String &ln = lines[next++ % lines.size()];
    String src = field(ln, 0), head = field(ln, 1);
    setFont(it.font);
    int tw = textWidth(head), base = textBaseline(it.font, 10, H - 10);
    uint32_t hs = millis();
    for (;;) {
      uint32_t t0 = millis();
      int x = W - (int)((t0 - hs) / 18);       // ~55 px a second
      if (x + tw < 0) break;
      paintBackground(it, frame);
      setFont(it.font);
      drawText(head, x, base, it, t0);
      newsBar(title, src);
      pushCanvas();
      frame++;
      if (it.sec && millis() - start >= total) return;
      int32_t wait = 25 - (int32_t)(millis() - t0);
      if (waitMs(wait > 0 ? wait : 1)) return;
    }
  }
}

// ---------- Stocks: one symbol at a time: name, price, day change, today's line ----------
static String withCommas(const String &n) {
  int dot = n.indexOf('.'); if (dot < 0) dot = n.length();
  int start = n[0] == '-' ? 1 : 0;
  String s = n.substring(dot);
  for (int i = dot - 1, k = 0; i >= start; i--, k++) { if (k && k % 3 == 0) s = "," + s; s = n[i] + s; }
  return (start ? "-" : "") + s;
}
// it.extra = symbols separated by commas (Yahoo style: ^GSPC, AAPL, BTC-USD). sec 0 = 5 s each.
static void playStocks(const ShowItem &it) {
  const String *b = feedGet("markets.txt");
  if (!b) return;
  std::vector<String> lines, pick;
  splitLines(*b, lines);
  for (int a = 0; a < (int)it.extra.length();) {
    int e = it.extra.indexOf(',', a); if (e < 0) e = it.extra.length();
    String sym = it.extra.substring(a, e); sym.trim(); a = e + 1;
    for (auto &l : lines) if (l.startsWith(sym + "\t")) { pick.push_back(l); break; }
  }
  if (pick.empty()) return;
  pjLabels = isPJ(it.font);
  effectInit(it); FxGuard fxg;
  uint32_t per = it.sec ? std::max<uint32_t>(3000, it.sec * 1000UL / pick.size()) : 5000, frame = 0;
  shownValid = false;
  for (auto &l : pick) {
    String name = field(l, 1), price = withCommas(field(l, 2)), chg = field(l, 3), pct = field(l, 4), sp = field(l, 5);
    bool up = chg[0] != '-';
    if (!up) { chg.remove(0, 1); pct.remove(0, 1); }
    uint32_t col = up ? 0x30DC6E : 0xFF4650;
    String pctS = (up ? "+" : "-") + pct + "%", chgS = withCommas(chg);
    uint8_t v[40]; int n = 0;
    for (int a = 0; a < (int)sp.length() && n < 40;) { int e = sp.indexOf(',', a); if (e < 0) e = sp.length(); v[n++] = constrain((int)sp.substring(a, e).toInt(), 0, 15); a = e + 1; }
    uint32_t start = millis();
    while (millis() - start < per) {
      uint32_t t0 = millis();
      paintBackground(it, frame);
      ::label(name, 2, 1, 0xFFBE3C);
      ::label(pctS, W - 1 - labelW(pctS), 1, col);
      uint8_t f = fitBig(std::min<uint8_t>(it.font, isPJ(it.font) ? 11 : 3), price, W - 2, 0);   // big would overflow the 64 rows
      drawText(price, (W - textWidth(price)) / 2, textBaseline(f, 9, 30), it, t0);
      int cw = labelW(chgS), cx = (W - cw - 9) / 2;
      cg.rgb(col);
      if (up) cg.fillTriangle(cx, 47, cx + 6, 47, cx + 3, 41, 0); else cg.fillTriangle(cx, 41, cx + 6, 41, cx + 3, 47, 0);
      ::label(chgS, cx + 9, pjLabels ? 40 : 41, col);
      for (int i = 1; i < n; i++) {             // today's prices, rows 49-63
        int x0 = 2 + (i - 1) * (W - 4) / (n - 1), x1 = 2 + i * (W - 4) / (n - 1);
        cg.drawLine(x0, 63 - v[i - 1] * 14 / 15, x1, 63 - v[i] * 14 / 15, 0);
      }
      pushCanvas();
      frame++;
      int32_t wait = 40 - (int32_t)(millis() - t0);
      if (waitMs(wait > 0 ? wait : 1)) return;
    }
  }
}

// ---------- Automatic updates ----------
// Once a day at 3:xx in the morning (minute picked from the card's MAC so cards don't all ask at once) the card
// reads firmware/info.json from the website. A newer version is downloaded over HTTPS straight into the other app
// slot, checked against the SHA-256 in info.json, then the card restarts into it. A new version that fails to
// start goes back to the old one by itself (rollback.cpp). Off with the page's "Automatic updates" switch.
#include "mbedtls/sha256.h"
static String jsonStr(const String &s, const char *key) {      // "key" : "value" (spaces allowed) -> value
  int k = s.indexOf(String("\"") + key + "\""); if (k < 0) return "";
  int c = s.indexOf(':', k), q = c < 0 ? -1 : s.indexOf('"', c + 1), e = q < 0 ? -1 : s.indexOf('"', q + 1);
  return e < 0 ? "" : s.substring(q + 1, e);
}
static int verNum(const String &v) { int a = 0, b = 0, c = 0; sscanf(v.c_str(), "%d.%d.%d", &a, &b, &c); return a * 10000 + b * 100 + c; }
static void updateScreen(const String &ver, int pct) {
  panel->fillScreen(0);
  panel->setFont(nullptr); panel->setTextSize(1);
  text(14, "UPDATING TO", C(255, 190, 60));
  text(26, ver.c_str(), C(230, 230, 230));
  panel->drawRect(1, 42, W - 2, 8, C(90, 90, 110));
  if (pct > 0) panel->fillRect(3, 44, (W - 6) * pct / 100, 4, C(60, 200, 110));
  text(54, "DON'T UNPLUG", C(140, 140, 160));
}
static bool otaFromSite(const String &ver, const String &file, int size, const String &sha) {
  Serial.printf("Update: %s -> %s (%d bytes)\n", FW_VERSION, ver.c_str(), size);
  playerFree();
  updateScreen(ver, 0);
  bool ok = false;
  String err;
  uint32_t t0 = millis();
  {
    WiFiClientSecure tls;
    tls.setCACert(SITE_CA);
    tls.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    if (http.begin(tls, String(SITE) + "firmware/" + file)) {
      int code = http.GET(), len = http.getSize();
      if (code != 200 || len != size) err = "HTTP " + String(code) + ", size " + String(len);
      else if (!Update.begin(len, U_FLASH)) err = Update.errorString();
      else {
        WiFiClient *st = http.getStreamPtr();
        mbedtls_sha256_context c; mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0);
        uint8_t buf[1024];
        int got = 0, shown = -1;
        uint32_t last = millis();
        while (got < len && millis() - last < 15000) {
          size_t av = st->available();
          if (!av) { if (!st->connected()) break; delay(2); continue; }
          int n = st->readBytes(buf, std::min(av, sizeof(buf)));
          if (n <= 0) continue;
          mbedtls_sha256_update(&c, buf, n);
          if (Update.write(buf, n) != (size_t)n) { err = Update.errorString(); break; }
          got += n; last = millis();
          int pct = (int)(got * 100LL / len);
          if (pct != shown) { shown = pct; updateScreen(ver, pct); }
        }
        uint8_t d[32]; mbedtls_sha256_finish(&c, d); mbedtls_sha256_free(&c);
        char hex[65]; for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", d[i]);
        if (!err.length() && got != len) err = "download stopped at " + String(got);
        else if (!err.length() && sha != hex) err = "checksum does not match";
        if (!err.length() && Update.end(true)) ok = true;
        else { if (!err.length()) err = Update.errorString(); Update.abort(); }
      }
      http.end();
    } else err = "no connection";
  }
  Serial.printf("Update: %s in %u s %s\n", ok ? "installed" : "FAILED", (unsigned)((millis() - t0) / 1000), err.c_str());
  if (ok) { delay(300); ESP.restart(); }
  panel->clearScreen();
  if (!playerAlloc()) { delay(200); ESP.restart(); }
  shownValid = false;
  return false;
}
// Reads info.json; installs a newer app if there is one. Returns the website's version ("" if unreachable).
String updateCheck(bool install) {
  String body;
  if (!siteGet("firmware/info.json", body, 2000)) return "";
  String ver = jsonStr(body, "version");
  int app = body.indexOf("\"app\"");
  if (!ver.length() || app < 0) return "";
  String rest = body.substring(app);
  String file = jsonStr(rest, "file"), sha = jsonStr(rest, "sha256");
  int sz = rest.indexOf("\"size\""), colon = sz < 0 ? -1 : rest.indexOf(':', sz);
  int size = colon >= 0 ? rest.substring(colon + 1).toInt() : 0;
  Serial.printf("Update check: website has %s, this card %s\n", ver.c_str(), FW_VERSION);
  if (install && verNum(ver) > verNum(FW_VERSION) && file.length() && sha.length() == 64 && size > 100000) otaFromSite(ver, file, size, sha);
  return ver;
}
bool updateNow = false;                          // the page's "Update now" (web.ino): install from the website next turn
void updateService() {
  static int doneDay = -1;
  if (updateNow) { updateNow = false; updateCheck(true); return; }
  if (!autoUpdate || WiFi.status() != WL_CONNECTED || !timeValid()) return;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  uint8_t mac[6]; WiFi.macAddress(mac);
  if (lt.tm_hour != 3 || lt.tm_min < mac[5] % 50 || lt.tm_yday == doneDay) return;
  doneDay = lt.tm_yday;
  updateCheck(true);
}
