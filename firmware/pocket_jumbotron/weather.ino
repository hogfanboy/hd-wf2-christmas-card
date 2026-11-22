// ---------- Weather (Open-Meteo: free, no key, plain http so no TLS memory) ----------
struct Weather {
  bool ok = false; uint32_t at = 0; String key; float temp = 0, feels = 0, hi = 0, lo = 0; int code = 0; bool day = true;
  float hum = NAN, wind = NAN, gust = NAN, dew = NAN, uv = NAN; int windDir = -1;   // now (wind in mph or km/h)
  int fcN = 0, fcCode[16]; float fcHi[16], fcLo[16], fcRain[16];   // daily forecast, [0] = today at the town; rain = chance %
  int hrN = 0, hr0 = 0; float hrT[25]; uint8_t hrCode[25], hrPop[25]; bool hrDay[25];   // hourly from this hour (hr0 0-23)
  int mnN = 0, mn0 = 0; float mnRain[9], mnSnow[9];   // 15-minute steps, each the mm / cm in the 15 minutes before it;
                                                       // mn0 = time of the first step, minutes after midnight
  int sunrise = -1, sunset = -1;                       // today, minutes after midnight
  uint32_t failAt = 0;                                 // last failed fetch (back off 5 min)
};
static Weather wx;
static float jsonNum(const String &s, const char *key, int from) {
  if (from < 0) return NAN;
  int i = s.indexOf(key, from);
  return i < 0 ? NAN : s.substring(i + strlen(key), i + strlen(key) + 16).toFloat();
}
// Reads up to n numbers of a JSON array that starts right after key (key ends with '[').
static int jsonArr(const String &s, const char *key, int from, float *out, int n) {
  if (from < 0) return 0;
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
// The time of the first "YYYY-MM-DDTHH:MM" in the array after key (key ends with '["'), minutes after midnight; -1 = none.
static int jsonHM(const String &s, const char *key, int from) {
  if (from < 0) return -1;
  int i = s.indexOf(key, from);
  if (i < 0) return -1;
  int t = i + strlen(key) + 10;
  if (t + 6 > (int)s.length() || s[t] != 'T') return -1;
  return s.substring(t + 1, t + 3).toInt() * 60 + s.substring(t + 4, t + 6).toInt();
}
// it.extra = "F,lat,lon". Refreshes at most every 15 minutes; keeps the last reading if a fetch fails. One request
// gets everything the extra screens need (~5 KB): now, the next 2 hours by 15 minutes, 24 hours, 16 days.
static bool weatherFetch(const ShowItem &it) {
  bool cached = wx.ok && wx.key == it.extra;
  if (cached && millis() - wx.at < 15UL * 60 * 1000) return true;
  if (WiFi.status() != WL_CONNECTED) return cached;
  if (wx.failAt && millis() - wx.failAt < 5UL * 60 * 1000) return cached;   // service down: don't stall the show
  int c1 = it.extra.indexOf(','), c2 = it.extra.indexOf(',', c1 + 1);
  bool F = it.extra.startsWith("F");
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + it.extra.substring(c1 + 1, c2) + "&longitude=" + it.extra.substring(c2 + 1) +
               "&current=temperature_2m,apparent_temperature,weather_code,is_day,relative_humidity_2m,wind_speed_10m,wind_gusts_10m,"
               "wind_direction_10m,uv_index,dew_point_2m&minutely_15=precipitation,snowfall&forecast_minutely_15=9"
               "&hourly=temperature_2m,weather_code,precipitation_probability,is_day&forecast_hours=25"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
               "&timezone=auto&forecast_days=16" + String(F ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  uint32_t t0 = millis();
  if (!http.begin(url)) return cached;
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  int cur = body.indexOf("\"current\":{"), day = body.indexOf("\"daily\":{");
  int hr = body.indexOf("\"hourly\":{"), mn = body.indexOf("\"minutely_15\":{");
  Serial.printf("Weather: HTTP %d, %u bytes, %u ms, free heap %u\n", code, (unsigned)body.length(), (unsigned)(millis() - t0), (unsigned)ESP.getFreeHeap());
  if (cur < 0 || day < 0) { wx.failAt = millis() | 1; return cached; }
  float t = jsonNum(body, "\"temperature_2m\":", cur);
  if (isnan(t)) return cached;
  wx.temp = t;
  wx.feels = jsonNum(body, "\"apparent_temperature\":", cur);
  if (isnan(wx.feels)) wx.feels = t;
  wx.code = (int)jsonNum(body, "\"weather_code\":", cur);
  wx.day = jsonNum(body, "\"is_day\":", cur) != 0;
  wx.hum = jsonNum(body, "\"relative_humidity_2m\":", cur);
  wx.wind = jsonNum(body, "\"wind_speed_10m\":", cur);
  wx.gust = jsonNum(body, "\"wind_gusts_10m\":", cur);
  float wd = jsonNum(body, "\"wind_direction_10m\":", cur);
  wx.windDir = isnan(wd) ? -1 : (int)wd;
  wx.uv = jsonNum(body, "\"uv_index\":", cur);
  wx.dew = jsonNum(body, "\"dew_point_2m\":", cur);
  wx.hi = jsonNum(body, "\"temperature_2m_max\":[", day);
  wx.lo = jsonNum(body, "\"temperature_2m_min\":[", day);
  float tmp[25];
  int n = jsonArr(body, "\"weather_code\":[", day, tmp, 16);
  for (int i = 0; i < n; i++) wx.fcCode[i] = (int)tmp[i];
  n = std::min(n, jsonArr(body, "\"temperature_2m_max\":[", day, wx.fcHi, 16));
  n = std::min(n, jsonArr(body, "\"temperature_2m_min\":[", day, wx.fcLo, 16));
  for (int i = 0; i < 16; i++) wx.fcRain[i] = NAN;
  jsonArr(body, "\"precipitation_probability_max\":[", day, wx.fcRain, 16);
  wx.fcN = n;
  wx.sunrise = jsonHM(body, "\"sunrise\":[\"", day);
  wx.sunset = jsonHM(body, "\"sunset\":[\"", day);
  // hourly (it comes after minutely_15 and before daily, so its keys are the first ones after "hourly")
  int h0 = jsonHM(body, "\"time\":[\"", hr);
  n = jsonArr(body, "\"temperature_2m\":[", hr, wx.hrT, 25);
  int k = jsonArr(body, "\"weather_code\":[", hr, tmp, 25); n = std::min(n, k);
  for (int i = 0; i < k; i++) wx.hrCode[i] = (uint8_t)tmp[i];
  k = jsonArr(body, "\"precipitation_probability\":[", hr, tmp, 25); n = std::min(n, k);
  for (int i = 0; i < k; i++) wx.hrPop[i] = isnan(tmp[i]) ? 0 : (uint8_t)tmp[i];
  k = jsonArr(body, "\"is_day\":[", hr, tmp, 25); n = std::min(n, k);
  for (int i = 0; i < k; i++) wx.hrDay[i] = tmp[i] != 0;
  wx.hr0 = h0 / 60; wx.hrN = h0 < 0 ? 0 : n;
  // the next 2 hours, 15 minutes at a time
  wx.mn0 = jsonHM(body, "\"time\":[\"", mn);
  n = jsonArr(body, "\"precipitation\":[", mn, wx.mnRain, 9);
  for (int i = 0; i < 9; i++) wx.mnSnow[i] = 0;
  jsonArr(body, "\"snowfall\":[", mn, wx.mnSnow, 9);
  wx.mnN = wx.mn0 < 0 ? 0 : n;
  Serial.printf("Weather: %.0f now, %d days, %d hours, %d quarter hours, sun %d-%d\n", wx.temp, wx.fcN, wx.hrN, wx.mnN, wx.sunrise, wx.sunset);
  wx.key = it.extra; wx.at = millis(); wx.ok = true; wx.failAt = 0;
  return true;
}
// Air quality (Open-Meteo's air quality API, also free over plain http): the US AQI, 0-500. Every 30 minutes.
static struct { String key; uint32_t at = 0, failAt = 0; int aqi = -1; } aq;
static void airFetch(const String &extra) {
  int c1 = extra.indexOf(','), c2 = extra.indexOf(',', c1 + 1);
  String key = extra.substring(c1 + 1);
  if (aq.key != key) { aq = {}; aq.key = key; }
  if (aq.at && millis() - aq.at < 30UL * 60 * 1000) return;
  if (aq.failAt && millis() - aq.failAt < 10UL * 60 * 1000) return;
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  if (!http.begin("http://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + extra.substring(c1 + 1, c2) + "&longitude=" +
                  extra.substring(c2 + 1) + "&current=us_aqi")) return;
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  float v = jsonNum(body, "\"us_aqi\":", body.indexOf("\"current\":{"));
  Serial.printf("Air: HTTP %d, AQI %.0f\n", code, v);
  if (isnan(v)) { aq.failAt = millis() | 1; return; }
  aq.aqi = (int)v; aq.at = millis(); aq.failAt = 0;
}
static int aqiNow() { return aq.at && millis() - aq.at < 2UL * 3600 * 1000 ? aq.aqi : -1; }   // -1 = not known
static uint8_t aqiCat(int a) { return a <= 50 ? 0 : a <= 100 ? 1 : a <= 150 ? 2 : a <= 200 ? 3 : a <= 300 ? 4 : 5; }
static const uint32_t AQI_COLOR[] = {0x00E400, 0xFFE600, 0xFF7E00, 0xFF1E1E, 0xB040FF, 0xC00040};
static const char *const AQI_WORD[] = {"GOOD", "MODERATE", "BAD FOR SENSITIVE", "UNHEALTHY", "VERY UNHEALTHY", "HAZARDOUS"};
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
// Small icons for the column screens (5 days, next hours), with rain/snow falling and the storm flashing.
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
// The PJ label font leaves 2 px between letters; the chance is drawn with 1 px so the drop and "40%" fit 25 px.
static int tightW(const String &s) { if (!pjLabels) return labelW(s); int w = 0; for (unsigned i = 0; i < s.length(); i++) w += labelW(String(s[i])); return w - 1; }
static void tightLabel(const String &s, int x, int top, uint32_t c) {
  if (!pjLabels) { label(s, x, top, c); return; }
  for (unsigned i = 0; i < s.length(); i++) { String ch(s[i]); label(ch, x, top, c); x += labelW(ch); }
}
static void wxRainPct(float pct, int cx, int top) {
  int p = (int)lroundf(pct);
  String s = String(p) + "%";                     // the drop and "40%"; "100%" without the drop
  if (tightW(s) > 25) s = String(p);
  int w = tightW(s), drop = w + 6 <= 25 ? 6 : 0, x = cx - (w + drop) / 2;
  if (drop) wxDrop(x, top + (pjLabels ? 3 : 1));
  tightLabel(s, x + drop, top, WX_PCT);
}
// ---------- The extra screens (each plays for the item's seconds; the background effect carries on) ----------
static bool wxF() { return wx.key.startsWith("F"); }            // °F: mph and 12-hour times; °C: km/h and 24-hour
static int nowMin() { time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); return lt.tm_hour * 60 + lt.tm_min; }
static int wxToday() { if (!timeValid()) return 0; time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt); return lt.tm_wday; }
static String hhmm(int mins) {                                   // "6:52 AM" or "16:33"
  int h = (mins / 60) % 24, m = mins % 60;
  String mm = String(m < 10 ? ":0" : ":") + String(m);
  if (!wxF()) return String(h) + mm;
  return String(h % 12 ? h % 12 : 12) + mm + (h < 12 ? " AM" : " PM");
}
static String hourName(int h) {                                  // "3PM" or "15H"
  h %= 24;
  if (!wxF()) return String(h) + "H";
  return String(h % 12 ? h % 12 : 12) + (h < 12 ? "AM" : "PM");
}
static uint32_t mix(uint32_t a, uint32_t b, float f) {
  uint32_t r = 0;
  for (int s = 0; s < 24; s += 8) r |= (uint32_t)(((a >> s) & 255) * (1 - f) + ((b >> s) & 255) * f + 0.5f) << s;
  return r;
}
// A color for a temperature, the same in °F and °C: lavender very cold, blue near freezing, teal and green mild,
// yellow warm, orange and red hot.
static uint32_t tempColor(float t) {
  float c = wxF() ? (t - 32) / 1.8f : t;
  static const float T[] = {-10, 0, 10, 18, 25, 32, 40};
  static const uint32_t K[] = {0xC8A0FF, 0x5096FF, 0x3CDCC8, 0x8CF050, 0xFFD23C, 0xFF7832, 0xFF2828};
  if (c <= T[0]) return K[0];
  for (int i = 1; i < 7; i++) if (c < T[i]) return mix(K[i - 1], K[i], (c - T[i - 1]) / (T[i] - T[i - 1]));
  return K[6];
}
static const char *uvWord(float u) { return u < 3 ? "LOW" : u < 6 ? "MODERATE" : u < 8 ? "HIGH" : u < 11 ? "VERY HIGH" : "EXTREME"; }
static uint32_t uvColor(float u) { return u < 3 ? 0x00E400 : u < 6 ? 0xFFE600 : u < 8 ? 0xFF7E00 : u < 11 ? 0xFF1E1E : 0xB040FF; }
// Rain or snow in the next 2 hours (15-minute steps): minutes until it starts (0 = now), -1 = none coming.
// *snow = it is snow; *ends = minutes until it stops (-1 = not within the 2 hours).
static int rainSoon(bool *snow, int *ends) {
  *snow = false; *ends = -1;
  if (!wx.mnN || !timeValid() || millis() - wx.at > 45UL * 60 * 1000) return -1;
  int now = nowMin(), start = -1;
  for (int i = 0; i < wx.mnN; i++) {
    int end = wx.mn0 + 15 * i - now;             // each step is the 15 minutes before its time
    if (end < -720) end += 1440; else if (end > 720) end -= 1440;
    if (end <= 0) continue;                       // already over
    bool wet = wx.mnRain[i] >= 0.1f;
    if (start < 0) { if (wet) { start = std::max(0, end - 15); *snow = wx.mnSnow[i] > 0; } }
    else if (!wet) { *ends = std::max(0, end - 15); break; }
  }
  return start;
}
static String rainSoonText(int m, bool snow, int ends) {
  String w = snow ? "SNOW" : "RAIN";
  auto r5 = [](int v) { return String(std::max(5, (v + 2) / 5 * 5)); };
  if (m > 0) return w + " IN " + r5(m) + " MIN";
  if (ends > 0) return w + " STOPS IN " + r5(ends) + " MIN";
  return snow ? "SNOWING NOW" : "RAINING NOW";
}
// Rain or snow coming: a radar-colored bar for every 15 minutes of the next 2 hours (height = how hard).
static void drawRainSoon(const ShowItem &it, uint32_t frame, uint32_t ms) {
  bool snow; int ends, m = rainSoon(&snow, &ends);
  labelC(rainSoonText(m, snow, ends), W / 2, 1, snow ? 0xFFFFFF : WX_PCT);
  const int base = 51;
  float grow = std::min(1.f, ms / 600.f);
  for (int i = 0; i < wx.mnN; i++) {
    float rate = wx.mnRain[i] * 4;               // mm per hour
    int h = wx.mnRain[i] >= 0.05f ? constrain((int)lroundf(18 * (log10f(rate) + 1)), 3, 36) : 0;
    h = (int)(h * grow);
    uint32_t c = wx.mnSnow[i] > 0 ? 0xDCF0FF : rate < 2.5f ? 0x32D250 : rate < 7.6f ? 0xFFD200 : rate < 25 ? 0xFF8C00 : 0xFF1E1E;
    for (int y = base - h; y < base; y++) for (int dx = 0; dx < 12; dx++) px(1 + i * 14 + dx, y, c);
  }
  for (int x = 1; x < W - 1; x++) px(x, base, 0x505A6E);
  label("NOW", 1, 55, th().acc);
  labelC("1H", 70, 55, th().dim);
  label("2H", W - 1 - labelW("2H"), 55, th().dim);
}
// Next 12 hours: now and every 3 hours, columns like the 5-day screen (moon icons at night).
static void wxMiniDN(int kind, bool day, int cx, int cy, uint32_t frame) {
  if (!day && (kind == WX_SUN || kind == WX_PARTLY)) wxSprite(kind == WX_SUN ? WXS_MINI7 : WXS_MINI8, cx, cy + 1);
  else wxMini(kind, cx, cy, frame);
}
static void hourCols(const ShowItem &it, uint32_t frame, int step) {
  for (int c = 0; c < 5 && c * step < wx.hrN; c++) {
    int i = c * step, cx = 2 + c * 25 + 12;
    String hn = c ? hourName(wx.hr0 + i) : String("NOW");
    if (labelW(hn) > 23) hn.remove(hn.length() - 1);   // "11AM" is too wide for the column: "11A"
    labelC(hn, cx, 3, th().acc);
    wxMiniDN(wxKind(wx.hrCode[i]), wx.hrDay[i], cx, 21, frame + c * 7);
    String t = String((int)lroundf(wx.hrT[i]));
    uint32_t hc = it.rainbow ? hsv((millis() / 8 + c * 300) % 1536) : it.fg;
    if (pjLabels) { setFont(9); printAt(t, cx - textWidth(t) / 2, 35 + pjf->cap, hc); }
    else labelC(t, cx, 35, hc);
    if (wx.hrPop[i] >= 10) wxRainPct(wx.hrPop[i], cx, 49);
  }
}
static void drawHourly(const ShowItem &it, uint32_t frame, uint32_t) { hourCols(it, frame, 3); }
// Next 24 hours: now and every 6 hours, the same columns (was a line chart: too busy on the LEDs).
static void drawChart(const ShowItem &it, uint32_t frame, uint32_t) { hourCols(it, frame, 6); }
// 5 days: 5 columns of 25 px: weekday, small icon, high (item color), low (blue), chance of rain/snow (a drop and
// the %, only on days with 10% or more).
static void drawDays5(const ShowItem &it, uint32_t frame, uint32_t) {
  static const char *const DOW[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  int today = wxToday();
  for (int i = 0; i < std::min(wx.fcN, 5); i++) {
    int cx = 2 + i * 25 + 12;
    String hi = String((int)lroundf(wx.fcHi[i])), lo = String((int)lroundf(wx.fcLo[i]));
    labelC(DOW[(today + i) % 7], cx, 1, th().acc);
    wxMini(wxKind(wx.fcCode[i]), cx, 18, frame + i * 7);
    uint32_t hc = it.rainbow ? hsv((millis() / 8 + i * 300) % 1536) : it.fg;
    if (pjLabels) { setFont(9); printAt(hi, cx - textWidth(hi) / 2, 29 + 11, hc); }
    else labelC(hi, cx, 31, hc);
    labelC(lo, cx, 42, WX_LO);                   // lows in the same cool blue as the "L"
    if (!isnan(wx.fcRain[i]) && wx.fcRain[i] >= 10) wxRainPct(wx.fcRain[i], cx, 54);
  }
}
// 16 days: two pages of 8 columns of 16 px (days 1-8, then 9-16 for the second half of the time): 2-letter
// weekday (today in the accent color), high (item color), low (blue), and on days with a 30% chance or more a drop
// (or a snowflake) over the chance. Numbers only: icons don't fit 16 px and bars read as noise.
static void drawDays16(const ShowItem &it, uint32_t frame, uint32_t ms) {
  static const char *const D2[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  int n = std::min(wx.fcN, 16), total = (it.sec ? it.sec : 10) * 1000;
  int first = n > 8 && (int)ms >= total / 2 ? 8 : 0, today = wxToday();
  for (int c = 0; c < 8 && first + c < n; c++) {
    int i = first + c, cx = c * 16 + 8;
    uint32_t hc = it.rainbow ? hsv((millis() / 8 + c * 200) % 1536) : it.fg;
    auto tc = [](const String &t, int cx, int top, uint32_t col) { tightLabel(t, cx - tightW(t) / 2, top, col); };   // 1 px between letters
    tc(D2[(today + i) % 7], cx, 2, i == 0 ? th().acc : th().dim);
    tc(String((int)lroundf(wx.fcHi[i])), cx, 16, hc);
    tc(String((int)lroundf(wx.fcLo[i])), cx, 29, WX_LO);
    if (isnan(wx.fcRain[i]) || wx.fcRain[i] < 30) continue;
    if (wxKind(wx.fcCode[i]) == WX_SNOW) { for (int d = -2; d <= 2; d++) { px(cx - 1 + d, 45, 0xFFFFFF); px(cx - 1, 45 + d, 0xFFFFFF); } }
    else wxDrop(cx - 3, 43);
    tc(String((int)lroundf(wx.fcRain[i])), cx, 52, WX_PCT);
  }
}
// Details: wind (direction it comes from), gusts, humidity, UV index (colored by level), dew point.
static void drawDetails(const ShowItem &it, uint32_t frame, uint32_t) {
  static const char *const DIR[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  String unit = wxF() ? " MPH" : " KM/H";
  auto num = [](float v) { return isnan(v) ? String("-") : String((int)lroundf(v)); };
  String rows[5][2] = {
    {"WIND", (wx.windDir >= 0 ? String(DIR[((wx.windDir + 22) / 45) % 8]) + " " : String("")) + num(wx.wind) + unit},
    {"GUSTS", num(wx.gust) + unit},
    {"HUMIDITY", num(wx.hum) + "%"},
    {"UV INDEX", num(wx.uv) + (isnan(wx.uv) ? "" : String(" ") + uvWord(wx.uv))},
    {"DEW POINT", num(wx.dew) + (pjLabels ? "\x7f" : "")},
  };
  for (int r = 0; r < 5; r++) {
    int y = 2 + r * 12;
    label(rows[r][0], 2, y, th().acc);
    label(rows[r][1], W - 2 - labelW(rows[r][1]), y, r == 3 && !isnan(wx.uv) ? uvColor(wx.uv) : th().main);
  }
}
// Air quality: the US AQI in big numbers in its color, the level, and the color scale with a marker.
static void drawAir(const ShowItem &it, uint32_t frame, uint32_t) {
  int a = aqiNow();
  if (a < 0) return;
  uint8_t c = aqiCat(a);
  labelC("AIR QUALITY", W / 2, 1, th().acc);
  String s(a);
  uint8_t f = fitBig(std::min<uint8_t>(it.font, isPJ(it.font) ? 11 : 3), s, 90, 0);   // not the huge size: it would reach the title
  printAt(s, (W - textWidth(s)) / 2, textBaseline(f, 11, 28), AQI_COLOR[c]);
  labelC(AQI_WORD[c], W / 2, 42, AQI_COLOR[c]);
  static const int B[] = {0, 50, 100, 150, 200, 300, 500};
  for (int k = 0; k < 6; k++) for (int x = 4 + k * 20; x < 4 + k * 20 + 19; x++) for (int y = 58; y < 61; y++) px(x, y, AQI_COLOR[k]);
  int p = 4 + c * 20 + constrain((a - B[c]) * 19 / (B[c + 1] - B[c]), 0, 18);
  for (int r = 0; r < 3; r++) for (int x = p - 2 + r; x <= p + 2 - r; x++) px(x, 53 + r, 0xFFFFFF);
}
// Sun and moon: the moon tonight (big, its phase name below), sunrise and sunset, and how long the day is.
static void sunGlyph(int x, int y, bool rise) {  // 11 x 9: an arrow (up for sunrise) over half a sun on the horizon
  uint32_t c = rise ? 0xFFC83C : 0xFF8C32;
  for (int r = 0; r < 3; r++) { int half = rise ? r : 2 - r; for (int d = -half; d <= half; d++) px(x + 5 + d, y + r, c); }
  for (int j = 0; j < 4; j++) { int dy = 4 - j; for (int i = -4; i <= 4; i++) if (i * i + dy * dy <= 20) px(x + 5 + i, y + 4 + j, c); }
  for (int i = -5; i <= 5; i++) px(x + 5 + i, y + 8, c);
}
static float wxLatNow = 0;                        // latitude of the weather item playing (moon icon side)
static void drawSun(const ShowItem &it, uint32_t frame, uint32_t) {
  int phase = moonPhase();
  wxStars(23, 24, frame);
  wxSprite(WXS_MOON0 + moonIcon(phase, wxLatNow), 23, 24);
  if (wx.sunrise >= 0) { sunGlyph(52, 4, true); label(hhmm(wx.sunrise), 67, 5, th().main); }
  if (wx.sunset >= 0) { sunGlyph(52, 20, false); label(hhmm(wx.sunset), 67, 21, th().main); }
  if (wx.sunrise >= 0 && wx.sunset > wx.sunrise) {
    int d = wx.sunset - wx.sunrise;
    label("DAY " + String(d / 60) + "H " + String(d % 60) + "M", 52, 37, th().dim);
  }
  labelC(MOON_NAME[phase], W / 2, 55, th().acc);
}
static bool wxScreen(const ShowItem &it, uint32_t &frame, WxDraw draw) {   // true = interrupted (button, new show)
  uint32_t total = (it.sec ? it.sec : 10) * 1000UL, start = millis();
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    draw(it, frame, millis() - start);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return true;
  }
  return false;
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
  airFetch(it.extra);
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
  int kind = wxKind(wx.code);
  wxLatNow = wxLat(it);
  pjLabels = isPJ(it.font);
  if (pjLabels) temp += '\x7f';                    // the PJ font has a degree sign
  // The line under the temperature takes turns: the sky ("PARTLY CLOUDY"), then only what matters today: rain/snow
  // on the way in the next 2 hours (or today's chance when 10% or more), strong gusts, a high UV index in the daytime,
  // unhealthy air; then feels like. Each in its own color where the color means something. The screen stays long
  // enough to show every line for 2.5 s.
  struct Say { String s; uint32_t c; };
  std::vector<Say> say;
  say.push_back({wxWord(wx.code), th().acc});
  bool snow; int ends, soon = rainSoon(&snow, &ends);
  if (soon >= 0) say.push_back({rainSoonText(soon, snow, ends), snow ? 0xFFFFFF : WX_PCT});
  else if (wx.fcN && !isnan(wx.fcRain[0]) && wx.fcRain[0] >= 10)
    say.push_back({String(wxKind(wx.fcCode[0]) == WX_SNOW ? "SNOW " : "RAIN ") + String((int)lroundf(wx.fcRain[0])) + "%", th().acc});
  if (!isnan(wx.gust) && wx.gust >= (wxF() ? 30 : 50)) say.push_back({"GUSTS " + String((int)lroundf(wx.gust)) + (wxF() ? " MPH" : " KM/H"), th().acc});
  if (wx.day && !isnan(wx.uv) && wx.uv >= 6) say.push_back({"UV " + String((int)lroundf(wx.uv)) + " " + uvWord(wx.uv), uvColor(wx.uv)});
  int aqi = aqiNow();
  if (aqi > 100) say.push_back({"AIR " + String(aqi) + " " + (aqiCat(aqi) == 2 ? "POOR" : AQI_WORD[aqiCat(aqi)]), AQI_COLOR[aqiCat(aqi)]});
  say.push_back({"FEELS " + String((int)lroundf(wx.feels)) + (pjLabels ? "\x7f" : ""), th().acc});
  int moon = moonIcon(moonPhase(), wxLatNow);
  uint32_t total = std::max<uint32_t>((it.sec ? it.sec : 10) * 1000UL, say.size() * 2500), start = millis(), frame = 0;
  uint32_t per = total / say.size();
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
    {   // centered under the temperature; a long one slides left so it fits (the icon ends above it)
      const Say &w = say[std::min<size_t>(say.size() - 1, (millis() - start) / per)];
      int ww = labelW(w.s), wx0 = std::max(0, std::min(48 + 40 - ww / 2, W - 1 - ww));
      label(w.s, wx0, 44, w.c);
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
  // the extra screens the item asks for, in this order; each only when there is something to show
  uint8_t pg = it.wxPages;
  if ((pg & WXP_RAINSOON) && rainSoon(&snow, &ends) >= 0 && wxScreen(it, frame, drawRainSoon)) return;
  if ((pg & WXP_HOURLY) && wx.hrN && wxScreen(it, frame, drawHourly)) return;
  if ((pg & WXP_CHART) && wx.hrN >= 2 && wxScreen(it, frame, drawChart)) return;
  if ((pg & WXP_DAYS5) && wx.fcN && wxScreen(it, frame, drawDays5)) return;
  if ((pg & WXP_DAYS16) && wx.fcN >= 2 && wxScreen(it, frame, drawDays16)) return;
  if ((pg & WXP_DETAILS) && wxScreen(it, frame, drawDetails)) return;
  if ((pg & WXP_AIR) && aqiNow() >= 0 && wxScreen(it, frame, drawAir)) return;
  if ((pg & WXP_SUN) && wxScreen(it, frame, drawSun)) return;
}
