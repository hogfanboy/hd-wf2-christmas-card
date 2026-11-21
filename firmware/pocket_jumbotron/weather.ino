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
// Icons: shaded sprites drawn by tools/make-wxicons.mjs (wx_icons.h), animated here with drifting clouds, a turning
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
// Rain streaks (slanted, bright head, fading tail) or snowflakes falling through rows [y0, y0+h) across x0..x0+w.
static void wxFall(bool snow, int x0, int w, int y0, int h, int n, uint32_t frame) {
  for (int i = 0; i < n; i++) {
    int x = x0 + (i * 37) % w, sp = snow ? 1 : 5;
    int y = (int)((frame * sp / (snow ? 3 : 2) + i * 11) % h);
    if (snow) {
      int sx = x + (sin8(frame * 3 + i * 50) - 128) / 64;
      px(sx, y0 + y, 0xFFFFFF);
      pxAdd(sx - 1, y0 + y, 0xDCE6FF, 110); pxAdd(sx + 1, y0 + y, 0xDCE6FF, 110);
      pxAdd(sx, y0 + y - 1, 0xDCE6FF, 110); pxAdd(sx, y0 + y + 1, 0xDCE6FF, 110);
    } else {
      int sx = x - y / 4;
      px(sx, y0 + y, 0x8CD2FF);
      pxAdd(sx, y0 + y - 1, 0x50A0FF, 170); pxAdd(sx + 1, y0 + y - 3, 0x3C82E6, 90);
    }
  }
}
static void wxStars(int cx, int cy, uint32_t frame) {
  static const int8_t S[][2] = {{-17, -12}, {13, -16}, {18, 4}, {-19, 9}, {4, 17}};
  for (int i = 0; i < 5; i++) pxAdd(cx + S[i][0], cy + S[i][1], 0xFFFFFF, sin8(frame * 3 + i * 51));
}
static void wxIcon(int kind, bool day, const ShowItem &it, uint32_t frame) {
  const int cx = 23, cy = 21;
  int drift = (sin8(frame * 2) - 128) / 64;
  int sunF = (frame / 5) % 6;
  bool flash = kind == WX_STORM && (frame / 4) % 12 < 2;
  switch (kind) {
    case WX_SUN:
      if (day) wxSprite(WXS_SUN0 + sunF, cx, cy); else { wxStars(cx, cy, frame); wxSprite(WXS_MOON, cx, cy); }
      break;
    case WX_PARTLY:
      if (day) wxSprite(WXS_SUNS0 + sunF, cx - 7, cy - 7); else { wxStars(cx, cy, frame); wxSprite(WXS_MOONS, cx - 7, cy - 7); }
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
// 5-day forecast: 5 columns of 25 px: weekday, small icon, high (item color), low (gray).
static void wxMini(int kind, int cx, int cy, uint32_t frame) {
  if (kind == WX_RAIN || kind == WX_STORM || kind == WX_SNOW) wxFall(kind == WX_SNOW, cx - 6, 13, cy + 2, 8, 3, frame);
  if (kind == WX_STORM && (frame / 4) % 12 < 2) { cg.rgb(0xFFF07A); cg.drawLine(cx + 1, cy + 2, cx - 2, cy + 6, 0); cg.drawLine(cx - 2, cy + 6, cx + 1, cy + 6, 0); cg.drawLine(cx + 1, cy + 6, cx - 2, cy + 10, 0); }
  wxSprite(WXS_MINI0 + kind, kind == WX_SUN || kind == WX_PARTLY || kind == WX_CLOUD ? cx : cx + 2, cy + 1);
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
    // fcStyle (being tried out with tools/forecast-probe.ps1): 0 plain, 1 every other column shaded,
    // 2 every column a shaded tile with a 1 px gap, 3 four days (32 px columns), every other one shaded.
    int n = fcStyle == 3 ? std::min(wx.fcN, 4) : wx.fcN, cw = fcStyle == 3 ? 32 : 25, x0 = fcStyle == 3 ? 0 : 2;
    for (int i = 0; i < n; i++) {
      if ((fcStyle == 1 || fcStyle == 3) && i % 2) { cg.rgb(FC_SHADE); cg.fillRect(x0 + i * cw, 0, cw, H, 0); }
      if (fcStyle == 2) { cg.rgb(FC_SHADE); cg.fillRect(x0 + i * cw, 0, cw - 1, H, 0); }
    }
    for (int i = 0; i < n; i++) {
      int cx = x0 + i * cw + cw / 2;
      String d = DOW[(today + i) % 7];
      String hi = String((int)lroundf(wx.fcHi[i])), lo = String((int)lroundf(wx.fcLo[i]));
      // day name, icon, high (bold small Jumbo when the item uses Jumbo), low; the town is on the first screen
      labelC(d, cx, 1, th().acc);
      wxMini(wxKind(wx.fcCode[i]), cx, 20, frame + i * 7);
      uint32_t hc = it.rainbow ? hsv((millis() / 8 + i * 300) % 1536) : it.fg;
      if (pjLabels) { setFont(9); printAt(hi, cx - textWidth(hi) / 2, 33 + 11, hc); }
      else labelC(hi, cx, 36, hc);
      labelC(lo, cx, 49, WX_LO);                // lows in the same cool blue as the "L"
    }
    if (fcProbeUntil) label(String(fcStyle), W - 6, 57, 0xFF0000);   // probe tag
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
  if (it.effect == FX_AUTO) {                     // "match the weather": snow or rain when it does, else nothing
    int k = wxKind(wx.code);
    it.effect = k == WX_SNOW ? FX_SNOW : (k == WX_RAIN || k == WX_STORM) ? FX_RAIN : FX_NONE;
  }
  effectInit(it); FxGuard fxg;
  if (fcProbeUntil && (int32_t)(millis() - fcProbeUntil) >= 0) fcProbeUntil = 0;
  if (fcProbeUntil) { it.sec = (fcProbeUntil - millis()) / 1000 + 1; if (wx.fcN) playForecast(it, 0); return; }
  String temp = String((int)lroundf(wx.temp));
  String line2 = "H" + String((int)lroundf(wx.hi)) + " L" + String((int)lroundf(wx.lo)) + " " + upcase(it.text);
  if (line2.length() > 21) line2 = line2.substring(0, 21);
  int lPos = line2.indexOf(" L") + 1;              // the "L" (the "H" is at 0)
  int townPos = line2.indexOf(' ', lPos); if (townPos < 0) townPos = line2.length();
  String word = wxWord(wx.code);
  int kind = wxKind(wx.code);
  pjLabels = isPJ(it.font);
  if (pjLabels) temp += '';                    // the PJ font has a degree sign
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    wxIcon(kind, wx.day, it, frame);
    uint8_t f = fitBig(it.font, temp, pjLabels ? 78 : 70, 0);
    int deg = pjLabels ? 0 : 8;
    int tw = textWidth(temp), x = 48 + (80 - tw - deg) / 2, base = textBaseline(f, 2, 40);
    int y1 = digitTop(base);
    drawText(temp, x, base, it, millis());
    if (!pjLabels) { cg.rgb(it.rainbow ? 0xFFFFFF : it.fg); cg.drawCircle(x + tw + 4, y1 + 3, 2, 0); cg.drawCircle(x + tw + 4, y1 + 3, 1, 0); }
    labelC(word, 48 + 40, 44, th().acc);
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
