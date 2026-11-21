// ---------- Weather (Open-Meteo: free, no key, plain http so no TLS memory) ----------
struct Weather {
  bool ok = false; uint32_t at = 0; String key; float temp = 0, hi = 0, lo = 0; int code = 0; bool day = true;
  int fcN = 0, fcCode[5]; float fcHi[5], fcLo[5];          // daily forecast, [0] = today at the town
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
               "&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5" +
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
  wx.code = (int)jsonNum(body, "\"weather_code\":", cur);
  wx.day = jsonNum(body, "\"is_day\":", cur) != 0;
  wx.hi = jsonNum(body, "\"temperature_2m_max\":[", day);
  wx.lo = jsonNum(body, "\"temperature_2m_min\":[", day);
  float codes[5];
  int n = jsonArr(body, "\"weather_code\":[", day, codes, 5);
  n = std::min(n, jsonArr(body, "\"temperature_2m_max\":[", day, wx.fcHi, 5));
  n = std::min(n, jsonArr(body, "\"temperature_2m_min\":[", day, wx.fcLo, 5));
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
static void wxCloud(int x, int y, uint32_t c) {
  cg.rgb(c);
  cg.fillCircle(x - 7, y + 2, 6, 0); cg.fillCircle(x + 1, y - 3, 8, 0); cg.fillCircle(x + 9, y + 2, 6, 0); cg.fillRect(x - 7, y + 2, 16, 7, 0);
}
static void wxSun(int x, int y, int r, uint32_t frame) {
  cg.rgb(0xFFC828);
  cg.fillCircle(x, y, r, 0);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7854f + frame * 0.02f;
    cg.drawLine(x + cosf(a) * (r + 3), y + sinf(a) * (r + 3), x + cosf(a) * (r + 6), y + sinf(a) * (r + 6), 0);
  }
}
static void wxMoon(int x, int y, const ShowItem &it, uint32_t frame) {
  cg.rgb(0xE6E6C8); cg.fillCircle(x, y, 10, 0);
  cg.rgb(it.bg); cg.fillCircle(x + 6, y - 4, 9, 0);
  for (int i = 0; i < 4; i++) pxAdd(x - 14 + i * 9, y - 12 + (i * 7) % 20, 0xFFFFFF, sin8(frame * 4 + i * 64));
}
static void wxIcon(int kind, bool day, const ShowItem &it, uint32_t frame) {
  const int cx = 23, cy = 22;
  int drift = (sin8(frame * 2) - 128) / 64;
  switch (kind) {
    case WX_SUN: if (day) wxSun(cx, cy, 9, frame); else wxMoon(cx, cy, it, frame); break;
    case WX_PARTLY:
      if (day) wxSun(cx - 6, cy - 6, 7, frame); else wxMoon(cx - 5, cy - 6, it, frame);
      wxCloud(cx + 3 + drift, cy + 5, 0xDCDCE6); break;
    case WX_CLOUD: wxCloud(cx - 4 - drift, cy - 4, 0x8C8C9B); wxCloud(cx + 3 + drift, cy + 4, 0xDCDCE6); break;
    case WX_FOG:
      wxCloud(cx, cy - 4, 0x8C8C9B);
      for (int i = 0; i < 3; i++) { cg.rgb(0xB4B4BE); cg.drawFastHLine(cx - 14 + ((frame / 6 + i * 3) % 6), cy + 9 + i * 4, 24, 0); }
      break;
    case WX_RAIN: case WX_SNOW: case WX_STORM: {
      for (int i = 0; i < 6; i++) {
        int x = cx - 11 + i * 4 + (i & 1), y = cy + 6 + (int)((frame * (kind == WX_SNOW ? 1 : 3) / 2 + i * 7) % 16);
        if (kind == WX_SNOW) { px(x, y, 0xFFFFFF); pxAdd(x - 1, y, 0xFFFFFF, 90); pxAdd(x + 1, y, 0xFFFFFF, 90); }
        else { px(x, y, 0x50A0FF); px(x, y + 1, 0x50A0FF); pxAdd(x, y + 2, 0x50A0FF, 100); }
      }
      if (kind == WX_STORM && (frame / 4) % 10 < 2) {
        cg.rgb(0xFFF050);
        cg.drawLine(cx + 2, cy + 4, cx - 3, cy + 13, 0); cg.drawLine(cx - 3, cy + 13, cx + 2, cy + 13, 0); cg.drawLine(cx + 2, cy + 13, cx - 3, cy + 22, 0);
      }
      wxCloud(cx, cy - 4, kind == WX_STORM ? 0x6E6E7D : 0xAAAAB9);
      break;
    }
  }
}
// 5-day forecast: 5 columns of 25 px: weekday, small icon, high (item color), low (gray).
static void wxMini(int kind, int cx, int cy, uint32_t frame) {
  auto cloud = [&](int x, int y, uint32_t c) {
    cg.rgb(c); cg.fillCircle(x - 4, y + 1, 3, 0); cg.fillCircle(x + 1, y - 2, 4, 0); cg.fillCircle(x + 5, y + 1, 3, 0); cg.fillRect(x - 4, y + 1, 9, 4, 0);
  };
  auto sun = [&](int x, int y, int r) {
    cg.rgb(0xFFC828); cg.fillCircle(x, y, r, 0);
    for (int i = 0; i < 8; i++) { float a = i * 0.7854f + frame * 0.02f; px(x + lroundf(cosf(a) * (r + 2)), y + lroundf(sinf(a) * (r + 2)), 0xFFC828); }
  };
  switch (kind) {
    case WX_SUN: sun(cx, cy, 4); break;
    case WX_PARTLY: sun(cx - 3, cy - 3, 3); cloud(cx + 2, cy + 2, 0xDCDCE6); break;
    case WX_CLOUD: cloud(cx - 2, cy - 2, 0x8C8C9B); cloud(cx + 2, cy + 2, 0xDCDCE6); break;
    case WX_FOG: cloud(cx, cy - 2, 0x8C8C9B); cg.rgb(0xB4B4BE); cg.drawFastHLine(cx - 6, cy + 5, 12, 0); cg.drawFastHLine(cx - 4, cy + 7, 12, 0); break;
    default: {
      for (int i = 0; i < 3; i++) {
        int x = cx - 4 + i * 4, y = cy + 4 + (int)((frame / (kind == WX_SNOW ? 4 : 2) + i * 3) % 6);
        px(x, y, kind == WX_SNOW ? 0xFFFFFF : 0x50A0FF);
      }
      if (kind == WX_STORM && (frame / 4) % 10 < 2) { cg.rgb(0xFFF050); cg.drawLine(cx + 1, cy + 2, cx - 2, cy + 7, 0); cg.drawLine(cx - 2, cy + 7, cx + 1, cy + 7, 0); cg.drawLine(cx + 1, cy + 7, cx - 2, cy + 11, 0); }
      cloud(cx, cy - 2, kind == WX_STORM ? 0x6E6E7D : 0xAAAAB9);
    }
  }
}
static void playForecast(const ShowItem &it, uint32_t frame) {
  static const char *const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int today = 0;
  if (timeValid()) { time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); today = lt.tm_wday; }
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis();
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    setFont(1); cg.setTextSize(1);
    for (int i = 0; i < wx.fcN; i++) {
      int cx = 2 + i * 25 + 12;
      String d = DOW[(today + i) % 7];
      String hi = String((int)lroundf(wx.fcHi[i])), lo = String((int)lroundf(wx.fcLo[i]));
      cg.rgb(0xFFBE3C); cg.setCursor(cx - textWidth(d) / 2, 1); cg.print(d);
      wxMini(wxKind(wx.fcCode[i]), cx, 19, frame + i * 7);
      cg.rgb(it.rainbow ? hsv((millis() / 8 + i * 300) % 1536) : it.fg); cg.setCursor(cx - textWidth(hi) / 2, 35); cg.print(hi);
      cg.rgb(0x8C8CA0); cg.setCursor(cx - textWidth(lo) / 2, 45); cg.print(lo);
    }
    String place = upcase(it.text);
    if (place.length() > 21) place = place.substring(0, 21);
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(place)) / 2, 56); cg.print(place);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
static void playWeather(const ShowItem &item) {
  ShowItem it = item;
  if (it.extra.indexOf("auto") > 0) {            // "weather here": the card's own location (online.ino)
    if (!geoOk) return;
    it.extra = String(it.extra[0]) + "," + String(geoLat, 3) + "," + String(geoLon, 3);
    if (it.text.isEmpty()) it.text = geoCity;
  }
  if (it.extra[0] == 'A') it.extra[0] = geoUsesF() ? 'F' : 'C';
  if (!weatherFetch(it)) return;
  effectInit(it); FxGuard fxg;
  String temp = String((int)lroundf(wx.temp));
  String line2 = "H" + String((int)lroundf(wx.hi)) + " L" + String((int)lroundf(wx.lo)) + " " + upcase(it.text);
  if (line2.length() > 21) line2 = line2.substring(0, 21);
  String word = wxWord(wx.code);
  int kind = wxKind(wx.code);
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    wxIcon(kind, wx.day, it, frame);
    uint8_t f = fitBig(it.font, temp, 70, 0);
    int tw = textWidth(temp), x = 48 + (80 - tw - 8) / 2, base = textBaseline(f, 2, 40);
    int16_t x1, y1; uint16_t bw, bh;
    cg.getTextBounds("0", 0, base, &x1, &y1, &bw, &bh);
    drawText(temp, x, base, it, millis());
    cg.drawCircle(x + tw + 4, y1 + 3, 2, 0); cg.drawCircle(x + tw + 4, y1 + 3, 1, 0);   // degree sign
    setFont(1); cg.setTextSize(1);
    cg.rgb(0xFFBE3C); cg.setCursor(48 + (80 - textWidth(word)) / 2, 44); cg.print(word);
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(line2)) / 2, 55); cg.print(line2);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
  if (it.forecast && wx.fcN) playForecast(it, frame);   // same background effect carries on
}
