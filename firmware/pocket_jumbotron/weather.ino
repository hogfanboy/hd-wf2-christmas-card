// ---------- Weather (Open-Meteo: free, no key, plain http so no TLS memory) ----------
struct Weather {
  bool ok = false; uint32_t at = 0; String key; float temp = 0, feels = 0, hi = 0, lo = 0; int code = 0; bool day = true;
  int fcN = 0, fcCode[5]; float fcHi[5], fcLo[5], fcRain[5];   // daily forecast, [0] = today at the town; rain = chance %
  uint32_t failAt = 0;                                       // last failed fetch (back off 5 min)
};
static Weather wx;
static float jsonNum(const String &s, const char *key, int from) {
  int i = s.indexOf(key, from);
  return i < 0 ? NAN : s.substring(i + strlen(key), i + strlen(key) + 16).toFloat();
}
// Reads up to n numbers of a JSON array that starts right after key (key ends with '[').
static int jsonArr(const String &s, const char *key, int from, float *out, int n) {
  int i = s.indexOf(key, from);
  if (i < 0) return 0;
  const char *p = s.c_str() + i + strlen(key);
  int k = 0;
  while (k < n && *p && *p != ']') {
    if (!strncmp(p, "null", 4)) { out[k++] = NAN; p += 4; while (*p == ',' || *p == ' ') p++; continue; }
    char *end; float v = strtof(p, &end);
    if (end == p) break;
    out[k++] = v; p = end;
    while (*p == ',' || *p == ' ') p++;
  }
  return k;
}
// it.extra = "F,lat,lon". Refreshes at most every 15 minutes; keeps the last reading if a fetch fails.
static bool weatherFetch(const ShowItem &it) {
  bool cached = wx.ok && wx.key == it.extra;
  if (cached && millis() - wx.at < 15UL * 60 * 1000) return true;
  if (WiFi.status() != WL_CONNECTED) return cached;
  if (wx.failAt && millis() - wx.failAt < 5UL * 60 * 1000) return cached;   // service down: don't stall the show
  int c1 = it.extra.indexOf(','), c2 = it.extra.indexOf(',', c1 + 1);
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + it.extra.substring(c1 + 1, c2) + "&longitude=" + it.extra.substring(c2 + 1) +
               "&current=temperature_2m,apparent_temperature,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,"
               "precipitation_probability_max&timezone=auto&forecast_days=5" +
               (it.extra.startsWith("F") ? "&temperature_unit=fahrenheit" : "");
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  uint32_t t0 = millis();
  if (!http.begin(url)) return cached;
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  int cur = body.indexOf("\"current\":{"), day = body.indexOf("\"daily\":{");
  Serial.printf("Weather: HTTP %d, %u bytes, %u ms, free heap %u\n", code, (unsigned)body.length(), (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
  if (cur < 0 || day < 0) { wx.failAt = millis() | 1; return cached; }
  float t = jsonNum(body, "\"temperature_2m\":", cur);
  if (isnan(t)) return cached;
  wx.temp = t;
  wx.feels = jsonNum(body, "\"apparent_temperature\":", cur);
  if (isnan(wx.feels)) wx.feels = t;
  wx.code = (int)jsonNum(body, "\"weather_code\":", cur);
  wx.day = jsonNum(body, "\"is_day\":", cur) != 0;
  wx.hi = jsonNum(body, "\"temperature_2m_max\":[", day);
  wx.lo = jsonNum(body, "\"temperature_2m_min\":[", day);
  float codes[5];
  int n = jsonArr(body, "\"weather_code\":[", day, codes, 5);
  n = std::min(n, jsonArr(body, "\"temperature_2m_max\":[", day, wx.fcHi, 5));
  n = std::min(n, jsonArr(body, "\"temperature_2m_min\":[", day, wx.fcLo, 5));
  for (int i = 0; i < 5; i++) wx.fcRain[i] = NAN;
  jsonArr(body, "\"precipitation_probability_max\":[", day, wx.fcRain, 5);
  for (int i = 0; i < n; i++) wx.fcCode[i] = (int)codes[i];
  wx.fcN = n;
  Serial.printf("Weather: %.0f now, forecast %d days (today %d, hi %.0f lo %.0f)\n", wx.temp, n, n ? wx.fcCode[0] : -1, n ? wx.fcHi[0] : 0.f, n ? wx.fcLo[0] : 0.f);
  wx.key = it.extra; wx.at = millis(); wx.ok = true; wx.failAt = 0;
  return true;
}
enum { WX_SUN, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };
static int wxKind(int c) {                       // WMO weather codes
  if (c <= 1) return WX_SUN;
  if (c == 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUD;
  if (c == 45 || c == 48) return WX_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_RAIN;
}
static const char *wxWord(int c) {
  if (c == 0) return "CLEAR"; if (c == 1) return "MOSTLY CLEAR"; if (c == 2) return "PARTLY CLOUDY"; if (c == 3) return "CLOUDY";
  if (c == 45 || c == 48) return "FOG"; if (c >= 51 && c <= 57) return "DRIZZLE"; if (c >= 61 && c <= 67) return "RAIN";
  if (c >= 71 && c <= 77) return "SNOW"; if (c >= 80 && c <= 82) return "SHOWERS"; if (c == 85 || c == 86) return "SNOW SHOWERS";
  if (c >= 95) return "THUNDERSTORM"; return "";
}
// Moon phase from the date (no internet needed): 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full,
// 5 waning gibbous, 6 last quarter, 7 waning crescent. The icons are drawn as seen from the north (waxing = lit on
// the right); south of the equator the lit side flips, which is the mirror phase.
static const char *const MOON_NAME[] = {"NEW MOON", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS", "FULL MOON",
                                        "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"};
static int moonPhase() {
  if (!timeValid()) return 1;
  const double SYN = 29.530588853;                 // days from new moon to new moon
  double age = fmod((time(nullptr) - 947182440.0) / 86400.0, SYN);   // 947182440 = new moon, 2000-01-06 18:14 UTC
  if (age < 0) age += SYN;
  return (int)(age / SYN * 8 + 0.5) % 8;
}
static int moonIcon(int phase, float lat) { return lat < 0 ? (8 - phase) % 8 : phase; }
static float wxLat(const ShowItem &it) { int c = it.extra.indexOf(','); return c < 0 ? 0 : it.extra.substring(c + 1).toFloat(); }
// Icons: shaded sprites with hard edges drawn by tools/make-wxicons.mjs (wx_icons.h), animated here with drifting clouds, a turning
// sun, rain streaks, swaying snowflakes, twinkling stars and lightning flashes.
static void wxSprite(int id, int x, int y) {      // x, y = the sprite's center
  const WxSprite &sp = WX_SPR[id];
  const uint8_t *p = sp.px;
  int x0 = x + sp.ox, y0 = y + sp.oy;
  for (int j = 0; j < sp.h; j++) for (int i = 0; i < sp.w; i++, p += 3) {
    uint8_t a = p[0];
    int X = x0 + i, Y = y0 + j;
    if (!a || (unsigned)X >= (unsigned)W || (unsigned)Y >= (unsigned)H) continue;
    uint16_t c = p[1] << 8 | p[2];
    uint8_t r = (c >> 11) << 3 | (c >> 13), g = ((c >> 5) & 63) << 2 | ((c >> 9) & 3), b = (c & 31) << 3 | ((c >> 2) & 7);
    uint8_t *q = canvas[Y][X];
    if (a == 255) { q[0] = r; q[1] = g; q[2] = b; continue; }
    q[0] = (q[0] * (255 - a) + r * a) / 255; q[1] = (q[1] * (255 - a) + g * a) / 255; q[2] = (q[2] * (255 - a) + b * a) / 255;
  }
}
// Rain drops (2 px, slanted fall) or snowflakes (solid plus) falling through rows [y0, y0+h) across x0..x0+w.
static void wxFall(bool snow, int x0, int w, int y0, int h, int n, uint32_t frame) {
  for (int i = 0; i < n; i++) {
    int x = x0 + (i * 37) % w, sp = snow ? 1 : 5;
    int y = (int)((frame * sp / (snow ? 3 : 2) + i * 11) % h);
    if (snow) {
      int sx = x + (sin8(frame * 3 + i * 50) - 128) / 64;
      px(sx, y0 + y, 0xFFFFFF); px(sx - 1, y0 + y, 0xFFFFFF); px(sx + 1, y0 + y, 0xFFFFFF);   // solid white plus
      px(sx, y0 + y - 1, 0xFFFFFF); px(sx, y0 + y + 1, 0xFFFFFF);
    } else {
      int sx = x - y / 4;
      px(sx, y0 + y, 0x46AAFF); px(sx, y0 + y - 1, 0x46AAFF);                         // solid 2 px drop
    }
  }
}
static void wxStars(int cx, int cy, uint32_t frame) {
  static const int8_t S[][2] = {{-17, -12}, {13, -16}, {18, 4}, {-19, 9}, {4, 17}};
  for (int i = 0; i < 5; i++) pxAdd(cx + S[i][0], cy + S[i][1], 0xFFFFFF, sin8(frame * 3 + i * 51));
}
static void wxIcon(int kind, bool day, int moon, uint32_t frame) {
  const int cx = 23, cy = 21;
  int drift = (sin8(frame * 2) - 128) / 64;
  int sunF = (frame / 5) % 6;
  bool flash = kind == WX_STORM && (frame / 4) % 12 < 2;
  switch (kind) {
    case WX_SUN:
      if (day) wxSprite(WXS_SUN0 + sunF, cx, cy); else { wxStars(cx, cy, frame); wxSprite(WXS_MOON0 + moon, cx, cy); }
      break;
    case WX_PARTLY:
      if (day) wxSprite(WXS_SUNS0 + sunF, cx - 7, cy - 7); else { wxStars(cx, cy, frame); wxSprite(WXS_MOONS0 + moon, cx - 7, cy - 7); }
      wxSprite(WXS_CLOUD_L, cx + 3 + drift, cy + 6);
      break;
    case WX_CLOUD: wxSprite(WXS_CLOUD_B, cx - 4 - drift, cy - 6); wxSprite(WXS_CLOUD_L, cx + 3 + drift, cy + 5); break;
    case WX_FOG:
      wxSprite(WXS_CLOUD_M, cx + 1, cy - 5);
      for (int i = 0; i < 3; i++) { cg.rgb(i == 1 ? 0xA0A8B8 : 0x8C94A4); cg.drawFastHLine(cx - 15 + ((frame / 6 + i * 3) % 6), cy + 10 + i * 4, 26, 0); }
      break;
    case WX_RAIN: case WX_STORM:
      wxFall(false, cx - 12, 26, cy + 4, 20, 8, frame);
      if (flash) wxSprite(WXS_BOLT, cx + 1, cy + 12);
      wxSprite(flash ? WXS_CLOUD_L : kind == WX_STORM ? WXS_CLOUD_D : WXS_CLOUD_M, cx + 1, cy - 6);
      break;
    case WX_SNOW:
      wxFall(true, cx - 13, 28, cy + 3, 20, 9, frame);
      wxSprite(WXS_CLOUD_L, cx + 1, cy - 6);
      break;
  }
}
// 5-day forecast: 5 columns of 25 px: weekday, small icon, high (item color), low (blue), chance of rain/snow (a drop
// and the %, only on days with 10% or more).
static void wxMini(int kind, int cx, int cy, uint32_t frame) {
  if (kind == WX_RAIN || kind == WX_STORM || kind == WX_SNOW) wxFall(kind == WX_SNOW, cx - 6, 13, cy + 2, 8, 3, frame);
  if (kind == WX_STORM && (frame / 4) % 12 < 2) { cg.rgb(0xFFF07A); cg.drawLine(cx + 1, cy + 2, cx - 2, cy + 6, 0); cg.drawLine(cx - 2, cy + 6, cx + 1, cy + 6, 0); cg.drawLine(cx + 1, cy + 6, cx - 2, cy + 10, 0); }
  wxSprite(WXS_MINI0 + kind, kind == WX_SUN || kind == WX_PARTLY || kind == WX_CLOUD ? cx : cx + 2, cy + 1);
}
static const uint32_t WX_DROP = 0x46AAFF, WX_PCT = 0xC8E6FF;
static void wxDrop(int x, int y) {                 // 5x5 rain drop, top-left at x, y
  static const uint8_t D[5] = {0x04, 0x0E, 0x1F, 0x1F, 0x0E};
  for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) if (D[j] >> (4 - i) & 1) px(x + i, y + j, WX_DROP);
}
static void wxRainPct(float pct, int cx, int top) {
  int p = (int)lroundf(pct);
  String s = String(p) + "%";                     // the drop and "40%"; in a wide font the drop and "40"
  if (labelW(s) + 6 > 25) s = String(p);
  int w = labelW(s), drop = w + 6 <= 25 ? 6 : 0, x = cx - (w + drop) / 2;
  if (drop) wxDrop(x, top + (pjLabels ? 3 : 1));
  label(s, x + drop, top, WX_PCT);
}
static void playForecast(const ShowItem &it, uint32_t frame) {
  static const char *const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int today = 0;
  if (timeValid()) { time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); today = lt.tm_wday; }
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis();
  pjLabels = isPJ(it.font);
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    int n = wx.fcN, cw = 25, x0 = 2;
    for (int i = 0; i < n; i++) {
      int cx = x0 + i * cw + cw / 2;
      String d = DOW[(today + i) % 7];
      String hi = String((int)lroundf(wx.fcHi[i])), lo = String((int)lroundf(wx.fcLo[i]));
      // day name, icon, high (bold small Jumbo when the item uses Jumbo), low; the town is on the first screen
      labelC(d, cx, 1, th().acc);
      wxMini(wxKind(wx.fcCode[i]), cx, 18, frame + i * 7);
      uint32_t hc = it.rainbow ? hsv((millis() / 8 + i * 300) % 1536) : it.fg;
      if (pjLabels) { setFont(9); printAt(hi, cx - textWidth(hi) / 2, 29 + 11, hc); }
      else labelC(hi, cx, 31, hc);
      labelC(lo, cx, 42, WX_LO);                // lows in the same cool blue as the "L"
      if (!isnan(wx.fcRain[i]) && wx.fcRain[i] >= 10) wxRainPct(wx.fcRain[i], cx, 54);
    }
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
// ---------- Weather alerts (US only: the National Weather Service, api.weather.gov, free, no key) ----------
// HTTPS (same Let's Encrypt root as the website, so SITE_CA works), and the answer can be 50+ KB of text, so it is
// read as a stream and only a few fields are kept per alert: event ("Winter Storm Warning"), ends/expires, and the
// message type (cancellations are skipped). Warnings/emergencies = level 3 (red), watches 2 (orange), advisories
// and alerts 1 (yellow); statements and outlooks are left out. Checked at most every 10 minutes per town.
static struct {
  String key; uint32_t at = 0, failAt = 0; bool covered = true; uint8_t n = 0; WxAlert a[3];
} wa;
static const uint32_t ALERT_COLOR[] = {0, 0xFFD200, 0xFF8C00, 0xFF1E1E};
static const char *const ALERT_WORD[] = {"", "ADVISORY", "WATCH", "WARNING"};
static uint8_t alertLevel(const char *ev) {
  const char *sp = strrchr(ev, ' ');
  const char *last = sp ? sp + 1 : ev;
  if (!strcmp(last, "Warning") || !strcmp(last, "Emergency")) return 3;
  if (!strcmp(last, "Watch")) return 2;
  if (!strcmp(last, "Advisory") || !strcmp(last, "Alert")) return 1;
  return 0;
}
static time_t isoTime(const char *s) {            // "2026-11-22T07:00:00-09:00" -> UTC seconds (0 if not a time)
  int Y, M, D, h, m, sec, oh = 0, om = 0; char sg = 'Z';
  if (sscanf(s, "%d-%d-%dT%d:%d:%d%c%d:%d", &Y, &M, &D, &h, &m, &sec, &sg, &oh, &om) < 6) return 0;
  Y -= M <= 2;                                     // days from 1970-01-01 (civil calendar)
  int era = (Y >= 0 ? Y : Y - 399) / 400, yoe = Y - era * 400, doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  long days = era * 146097L + (yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
  long off = (oh * 3600L + om * 60L) * (sg == '-' ? -1 : sg == '+' ? 1 : 0);
  return (time_t)(days * 86400L + h * 3600L + m * 60L + sec - off);
}
static void alertAdd(WxAlert *list, uint8_t &n, const char *ev, time_t until) {
  uint8_t lvl = alertLevel(ev);
  if (!lvl || (until && until < time(nullptr))) return;
  char title[32];
  const char *sp = strrchr(ev, ' ');
  size_t len = std::min((size_t)(sp && sp != ev ? sp - ev : strlen(ev)), sizeof(title) - 1);
  for (size_t i = 0; i < len; i++) title[i] = toupper(ev[i]);
  title[len] = 0;
  for (uint8_t i = 0; i < n; i++)                  // the same alert for several zones or as updates: keep one
    if (list[i].level == lvl && !strcmp(list[i].title, title)) { if (until > list[i].until) list[i].until = until; return; }
  uint8_t at = n;
  if (n == 3) { at = 0; for (uint8_t i = 1; i < 3; i++) if (list[i].level < list[at].level) at = i; if (list[at].level >= lvl) return; }
  else n++;
  strcpy(list[at].title, title); list[at].level = lvl; list[at].until = until;
  std::sort(list, list + n, [](const WxAlert &a, const WxAlert &b) { return a.level > b.level; });
}
static void alertsFetch(const String &extra, uint8_t mode) {
  if (mode == 2) {                                 // alerts=demo: a made-up warning and watch, to see the screens
    if (wa.key == "demo") return;
    wa = {}; wa.key = "demo"; wa.at = millis();
    alertAdd(wa.a, wa.n, "Winter Storm Warning", time(nullptr) + 5 * 3600);
    alertAdd(wa.a, wa.n, "Wind Chill Watch", time(nullptr) + 30 * 3600);
    return;
  }
  int c1 = extra.indexOf(','), c2 = extra.indexOf(',', c1 + 1);
  String key = String(extra.substring(c1 + 1, c2).toFloat(), 4) + "," + String(extra.substring(c2 + 1).toFloat(), 4);
  if (wa.key == key) {
    if (wa.at && millis() - wa.at < (wa.covered ? 10UL : 360UL) * 60 * 1000) return;
    if (wa.failAt && millis() - wa.failAt < 5UL * 60 * 1000) return;
  } else { wa = {}; wa.key = key; }
  if (WiFi.status() != WL_CONNECTED || !timeValid()) return;   // checking the certificate needs the date
  WxAlert got[3]; uint8_t n = 0;
  int code = -1; uint32_t bytes = 0, t0 = millis();
  playerFree();                                    // TLS needs the player's memory for a moment (see online.ino)
  {
    WiFiClientSecure tls;
    tls.setCACert(SITE_CA);
    tls.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(6000);
    http.setUserAgent("PocketJumbotron/1.0 (github.com/hogfanboy/hd-wf2-christmas-card)");   // weather.gov asks for one
    if (http.begin(tls, "https://api.weather.gov/alerts/active?status=actual&point=" + key)) {
      http.addHeader("Accept", "application/geo+json");
      code = http.GET();
      if (code == 200) {
        static const char *const K[] = {"\"expires\":", "\"ends\":", "\"messageType\":", "\"event\":"};
        uint8_t ks[4] = {0, 0, 0, 0};
        int kk = -1, vs = 0, vl = 0;              // reading the value of K[kk]: vs 0 = before it, 1 = in "...", 2 = after '\'
        char val[48];
        time_t expires = 0, ends = 0; bool cancel = false;
        uint8_t buf[512];
        WiFiClient *st = http.getStreamPtr();
        uint32_t last = millis();
        while (bytes < 150000 && millis() - t0 < 12000 && millis() - last < 4000) {
          size_t av = st->available();
          if (!av) { if (!st->connected()) break; delay(2); continue; }
          int got_n = st->readBytes(buf, std::min(av, sizeof(buf)));
          if (got_n <= 0) continue;
          bytes += got_n; last = millis();
          for (int b = 0; b < got_n; b++) {
            char c = buf[b];
            if (kk >= 0) {
              if (vs == 0) { if (c == ' ') continue; if (c == '"') { vs = 1; vl = 0; continue; } vl = 0; }   // null or a number
              else if (vs == 2) { vs = 1; if (vl < 47) val[vl++] = c; continue; }
              else if (c == '\\') { vs = 2; continue; }
              else if (c != '"') { if (vl < 47) val[vl++] = c; continue; }
              val[vl] = 0;
              if (kk == 0) expires = isoTime(val);
              else if (kk == 1) ends = isoTime(val);
              else if (kk == 2) cancel = !strcmp(val, "Cancel");
              else { if (!cancel) alertAdd(got, n, val, ends ? ends : expires); expires = ends = 0; cancel = false; }
              kk = -1;
              continue;
            }
            for (int k = 0; k < 4; k++) {
              if (c == K[k][ks[k]]) { if (!K[k][++ks[k]]) { kk = k; vs = 0; memset(ks, 0, sizeof(ks)); break; } }
              else ks[k] = c == '"';
            }
          }
        }
      }
      http.end();
    }
  }
  if (!playerAlloc()) { Serial.println("Player memory lost after HTTPS: restarting"); delay(200); ESP.restart(); }
  Serial.printf("Alerts %s: HTTP %d, %u bytes, %u ms, %u alerts\n", key.c_str(), code, (unsigned)bytes, (unsigned)(millis() - t0), n);
  for (uint8_t i = 0; i < n; i++) Serial.printf("  %s %s until %ld\n", got[i].title, ALERT_WORD[got[i].level], (long)got[i].until);
  if (code == 200) { memcpy(wa.a, got, sizeof(got)); wa.n = n; wa.at = millis(); wa.failAt = 0; wa.covered = true; }
  else if (code == 400 || code == 404) { wa.n = 0; wa.at = millis(); wa.covered = false; }   // not a US place
  else wa.failAt = millis() | 1;
}
// Alerts that are still on: not past their end, and checked in the last 40 minutes (else they may be over).
static bool alertOn(const WxAlert &a) { return !a.until || a.until > time(nullptr); }
static uint8_t alertsTop() {
  if (!wa.n || !wa.at || millis() - wa.at > 40UL * 60 * 1000) return 0;
  uint8_t top = 0;
  for (uint8_t i = 0; i < wa.n; i++) if (alertOn(wa.a[i])) top = std::max(top, wa.a[i].level);
  return top;
}
static String alertUntil(time_t u) {
  if (!u) return "IN EFFECT";
  static const char *const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  time_t now = time(nullptr); struct tm lt, nt; localtime_r(&u, &lt); localtime_r(&now, &nt);
  int h = lt.tm_hour % 12 ? lt.tm_hour % 12 : 12;
  String s = "UNTIL " + String(h) + (lt.tm_min ? (lt.tm_min < 10 ? ":0" : ":") + String(lt.tm_min) : "") + (lt.tm_hour < 12 ? " AM" : " PM");
  if (lt.tm_yday != nt.tm_yday || lt.tm_year != nt.tm_year) s += String(" ") + DOW[lt.tm_wday];
  return s;
}
static void alertTriangle(int cx, int top, uint32_t c, uint32_t mark) {   // 11 x 10 warning sign with a "!"
  for (int r = 0; r < 10; r++) { int half = r * 5 / 9; for (int x = cx - half; x <= cx + half; x++) px(x, top + r, c); }
  for (int r = 3; r <= 6; r++) px(cx, top + r, mark);
  px(cx, top + 8, mark);
}
// One alert, full screen: a colored bar with the level ("WARNING", flashing for warnings), what it is in big
// letters (wrapped, largest font that fits) and until when. Black around everything, no effects: it should stand out.
static bool playAlert(const WxAlert &a, uint32_t ms) {
  uint32_t col = ALERT_COLOR[a.level], start = millis();
  String until = alertUntil(a.until);
  // pick the biggest Jumbo font where the title fits in at most 2 lines (3 in the thin one) between the bar and the bottom
  std::vector<String> words, lines;
  { String t = a.title; int s = 0; while (s < (int)t.length()) { int e = t.indexOf(' ', s); if (e < 0) e = t.length(); if (e > s) words.push_back(t.substring(s, e)); s = e + 1; } }
  uint8_t font = 8;
  for (uint8_t f : {11, 10, 9, 8}) {
    setFont(f);
    std::vector<String> l; String cur;
    bool fits = true;
    for (auto &w : words) {
      String tryS = cur.length() ? cur + " " + w : w;
      if (textWidth(tryS) <= 124) cur = tryS;
      else { if (cur.length()) l.push_back(cur); cur = w; if (textWidth(w) > 124) fits = false; }
    }
    if (cur.length()) l.push_back(cur);
    int maxL = f == 11 ? 1 : f == 8 ? 3 : 2;
    if (fits && (int)l.size() <= maxL && (int)l.size() * pjf->cap + ((int)l.size() - 1) * 4 <= 38) { lines = l; font = f; break; }
    lines = l;
  }
  shownValid = false;
  while (millis() - start < ms) {
    uint32_t t0 = millis();
    memset(canvas, 0, CANVAS_BYTES);
    bool lit = a.level < 3 || (millis() / 400) % 2 == 0;   // warnings flash
    uint32_t barC = lit ? col : 0, ink = lit ? 0 : col;
    for (int y = 0; y < 13; y++) for (int x = 0; x < W; x++) px(x, y, barC);
    if (!lit) for (int x = 0; x < W; x++) { px(x, 0, col); px(x, 12, col); }
    setFont(9);
    String word = ALERT_WORD[a.level];
    printAt(word, (W - textWidth(word)) / 2, 1 + pjf->cap, ink);
    alertTriangle(7, 2, ink, barC); alertTriangle(W - 8, 2, ink, barC);
    setFont(font);
    int lh = pjf->cap, total = lines.size() * lh + (lines.size() - 1) * 4, y = 15 + (38 - total) / 2;
    for (auto &l : lines) { printAt(l, (W - textWidth(l)) / 2, y + lh, 0xFFFFFF); y += lh + 4; }
    setFont(8);
    printAt(until, (W - textWidth(until)) / 2, 54 + pjf->cap, col);
    pushCanvas();
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return true;
  }
  return false;
}
static bool playAlerts(uint8_t minLevel, uint32_t ms) {   // every alert on at this level or higher; true = interrupted
  for (uint8_t i = 0; i < wa.n; i++)
    if (wa.a[i].level >= minLevel && alertOn(wa.a[i]) && playAlert(wa.a[i], ms)) return true;
  return false;
}
// Called after every other item: while a warning is on, it comes back after every 2 items (the weather item
// shows all alerts itself). Only for shows that have a weather item with alerts turned on.
void alertsBetweenItems() {
  static uint8_t gap = 0;
  bool want = false;
  for (auto &it : playlist) if (it.type == 'w' && it.alerts) { want = true; break; }
  if (!want || alertsTop() < 3) { gap = 0; return; }
  if (++gap < 2) return;
  gap = 0;
  playAlerts(3, 5000);
}

static void playWeather(const ShowItem &item) {
  ShowItem it = item;
  if (it.extra.indexOf("auto") > 0) {            // "weather here": the card's own location (online.ino)
    if (!geoOk) return;
    it.extra = String(it.extra[0]) + "," + String(geoLat, 3) + "," + String(geoLon, 3);
    if (it.text.isEmpty()) it.text = geoCity;
  }
  if (it.extra[0] == 'A') it.extra[0] = geoUsesF() ? 'F' : 'C';
  if (it.alerts) {                                // alerts first, so a warning is seen even if the forecast fails
    alertsFetch(it.extra, it.alerts);
    if (alertsTop() && playAlerts(1, 4500)) return;
  }
  if (!weatherFetch(it)) return;
  if (it.effect == FX_AUTO) {                     // "match the weather": snow or rain when it does, else nothing
    int k = wxKind(wx.code);
    it.effect = k == WX_SNOW ? FX_SNOW : (k == WX_RAIN || k == WX_STORM) ? FX_RAIN : FX_NONE;
  }
  effectInit(it); FxGuard fxg;
  String temp = String((int)lroundf(wx.temp));
  String line2 = "H" + String((int)lroundf(wx.hi)) + " L" + String((int)lroundf(wx.lo)) + " " + upcase(it.text);
  if (line2.length() > 21) line2 = line2.substring(0, 21);
  int lPos = line2.indexOf(" L") + 1;              // the "L" (the "H" is at 0)
  int townPos = line2.indexOf(' ', lPos); if (townPos < 0) townPos = line2.length();
  int kind = wxKind(wx.code), phase = moonPhase();
  pjLabels = isPJ(it.font);
  if (pjLabels) temp += '\x7f';                    // the PJ font has a degree sign
  // The line under the temperature takes turns: the sky ("PARTLY CLOUDY"), feels like, today's chance of rain or
  // snow, and at night under a clear or partly cloudy sky the moon's phase.
  std::vector<String> say;
  say.push_back(wxWord(wx.code));
  say.push_back("FEELS " + String((int)lroundf(wx.feels)) + (pjLabels ? "\x7f" : ""));
  if (wx.fcN && !isnan(wx.fcRain[0])) say.push_back(String(wxKind(wx.fcCode[0]) == WX_SNOW ? "SNOW " : "RAIN ") + String((int)lroundf(wx.fcRain[0])) + "%");
  if (!wx.day && (kind == WX_SUN || kind == WX_PARTLY)) say.push_back(MOON_NAME[phase]);
  int moon = moonIcon(phase, wxLat(it));
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis(), frame = 0;
  uint32_t per = std::max<uint32_t>(2500, total / say.size());
  bool warn = alertsTop() >= 3;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    wxIcon(kind, wx.day, moon, frame);
    if (warn && (millis() / 400) % 2 == 0) alertTriangle(5, 0, ALERT_COLOR[3], 0);   // a warning is on: flashing sign
    uint8_t f = fitBig(it.font, temp, pjLabels ? 78 : 70, 0);
    int deg = pjLabels ? 0 : 8;
    int tw = textWidth(temp), x = 48 + (80 - tw - deg) / 2, base = textBaseline(f, 2, 40);
    int y1 = digitTop(base);
    drawText(temp, x, base, it, millis());
    if (!pjLabels) { cg.rgb(it.rainbow ? 0xFFFFFF : it.fg); cg.drawCircle(x + tw + 4, y1 + 3, 2, 0); cg.drawCircle(x + tw + 4, y1 + 3, 1, 0); }
    {   // centered under the temperature; a long one (moon phase) slides left so it fits (the icon ends above it)
      const String &w = say[((millis() - start) / per) % say.size()];
      int ww = labelW(w), wx0 = std::max(0, std::min(48 + 40 - ww / 2, W - 1 - ww));
      label(w, wx0, 44, th().acc);
    }
    {   // "H" warm and "L" cool with 1 px after each, the numbers in the theme's main color, the town dim
      labelFont();
      int x = W / 2 - (textWidth(line2) + 2) / 2, y = pjLabels ? 55 + pjf->cap : 55;
      for (unsigned k = 0; k < line2.length(); k++) {
        uint32_t c = k == 0 ? WX_HI : (int)k == lPos ? WX_LO : (int)k < townPos ? th().main : th().dim;
        x = printAt(String(line2[k]), x, y, c);
        if (k == 0 || (int)k == lPos) x++;
      }
    }
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
  if (it.forecast && wx.fcN) playForecast(it, frame);   // same background effect carries on
}
