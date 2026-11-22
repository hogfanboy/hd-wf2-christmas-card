// ---------- Text / clock / countdown items ----------

static void playText(const ShowItem &it) {
  pjLabels = isPJ(it.font);
  setFont(it.font);
  effectInit(it); FxGuard fxg;
  int tw = textWidth(it.text);
  int base = textBaseline(it.font, 0, H);
  bool scroll = tw > W - 4;
  // Scrolling moves exactly 1 px per 40-ms frame on a fixed clock (25 px/s, same as the weather strips): every step
  // the same size, so it looks smooth. One pass = W + tw frames.
  uint32_t total = it.sec ? it.sec * 1000UL : scroll ? (uint32_t)(W + tw) * 40 : 6000;
  uint32_t start = millis(), frame = 0;
  shownValid = false;
  for (uint32_t k = 0; k * 40 < total; k++) {
    paintBackground(it, frame);
    int x = scroll ? W - (int)(k % (uint32_t)(W + tw)) : (W - tw) / 2;
    drawText(it.text, x, base, it, start + k * 40);
    pushCanvas();
    frame++;
    int32_t wait = (int32_t)(start + (k + 1) * 40 - millis());
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

static void playClock(const ShowItem &it) {
  if (!timeValid()) return;
  effectInit(it); FxGuard fxg;
  uint32_t total = (it.sec ? it.sec : 8) * 1000UL, start = millis(), frame = 0;
  bool h12 = it.extra != "24";
  pjLabels = isPJ(it.font);
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char hm[8], date[24];
    int hour = h12 ? (lt.tm_hour % 12 ? lt.tm_hour % 12 : 12) : lt.tm_hour;
    snprintf(hm, sizeof(hm), h12 ? "%d:%02d" : "%02d:%02d", hour, lt.tm_min);
    strftime(date, sizeof(date), "%a %b %e", &lt);
    paintBackground(it, frame);
    // 12-hour: a small AM/PM beside the time, its top level with the digits (11 px wide + 2 px gap).
    const int PM_W = h12 ? labelW("PM") + 2 : 0;
    fitBig(it.font, hm, W - 2, PM_W);
    int tw = textWidth(hm), x = (W - tw - PM_W) / 2, base = textBaseline(curFont, 2, 40);
    int top = digitTop(base);
    drawText(hm, x, base, it, millis());
    if (h12) label(lt.tm_hour < 12 ? "AM" : "PM", x + tw + 2, top, it.rainbow ? 0xFFFFFF : it.fg);
    labelC(upcase(date), W / 2, 50, th().acc);     // accent, not dim: the brown-ish dims read muddy on LEDs
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

static int daysUntil(const String &mmdd) {
  int m = mmdd.substring(0, 2).toInt(), d = mmdd.substring(3, 5).toInt();
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  struct tm a = lt; a.tm_hour = 12; a.tm_min = a.tm_sec = 0; a.tm_isdst = -1;
  struct tm b = a; b.tm_mon = m - 1; b.tm_mday = d;
  time_t ta = mktime(&a), tb = mktime(&b);
  if (tb < ta - 3600) { b.tm_year++; tb = mktime(&b); }
  return (int)((tb - ta + 43200) / 86400);
}

static void playCountdown(const ShowItem &item) {
  if (!timeValid() || item.extra.length() < 5) return;
  int days = daysUntil(item.extra);
  ShowItem it = item;
  if (days == 0 && it.lead >= 0) it.effect = FX_FIREWORKS;       // a reminder's big day
  effectInit(it); FxGuard fxg;
  String big = days == 0 ? "TODAY!" : String(days);
  String l1 = days == 0 ? "" : days == 1 ? "DAY UNTIL" : "DAYS UNTIL";
  String l2 = upcase(it.text);
  if (days == 0) { l1 = l2; l2 = ""; }
  uint32_t total = (it.sec ? it.sec : 8) * 1000UL, start = millis(), frame = 0;
  pjLabels = isPJ(it.font);
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    fitBig(it.font, big, W - 2, 0);
    drawText(big, (W - textWidth(big)) / 2, textBaseline(curFont, 0, 40), it, millis());
    if (l1.length()) labelC(l1, W / 2, 42, th().main);
    if (l2.length()) labelC(l2, W / 2, 53, th().acc);
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
