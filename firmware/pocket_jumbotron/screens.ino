// ---------- Text / clock / countdown items ----------

static void playText(const ShowItem &it) {
  setFont(it.font);
  effectInit(it); FxGuard fxg;
  int tw = textWidth(it.text);
  int base = textBaseline(it.font, 0, H);
  bool scroll = tw > W - 4;
  uint32_t total = it.sec ? it.sec * 1000UL : scroll ? (uint32_t)(W + tw) * 25 : 6000;
  uint32_t start = millis(), frame = 0;
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    int x = scroll ? W - (int)((millis() - start) / 25) % (W + tw) : (W - tw) / 2;
    drawText(it.text, x, base, it, millis());
    pushCanvas();
    frame++;
    int32_t wait = 25 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}

static void playClock(const ShowItem &it) {
  if (!timeValid()) return;
  effectInit(it); FxGuard fxg;
  uint32_t total = (it.sec ? it.sec : 8) * 1000UL, start = millis(), frame = 0;
  bool h12 = it.extra != "24";
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char hm[8], date[24];
    int hour = h12 ? (lt.tm_hour % 12 ? lt.tm_hour % 12 : 12) : lt.tm_hour;
    snprintf(hm, sizeof(hm), h12 ? "%d:%02d" : "%02d:%02d", hour, lt.tm_min);
    strftime(date, sizeof(date), "%a %b %e", &lt);
    paintBackground(it, frame);
    setFont(it.font < 3 ? 3 : it.font);
    if (textWidth(hm) > W - 2) setFont(3);
    drawText(hm, (W - textWidth(hm)) / 2, textBaseline(3, 2, 40), it, millis());
    setFont(1); cg.setTextSize(1);
    String d = upcase(date) + (h12 ? (lt.tm_hour < 12 ? " AM" : " PM") : "");
    cg.rgb(0xB4B4C8); cg.setCursor((W - textWidth(d)) / 2, 50); cg.print(d);
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
  shownValid = false;
  while (millis() - start < total) {
    uint32_t t0 = millis();
    paintBackground(it, frame);
    setFont(it.font < 3 ? 3 : it.font);
    if (textWidth(big) > W - 2) setFont(3);
    if (textWidth(big) > W - 2) setFont(2);
    drawText(big, (W - textWidth(big)) / 2, textBaseline(3, 0, 40), it, millis());
    setFont(1); cg.setTextSize(1);
    cg.rgb(0xFFFFFF);
    if (l1.length()) { cg.setCursor((W - textWidth(l1)) / 2, 42); cg.print(l1); }
    if (l2.length()) { cg.rgb(0xFFBE3C); cg.setCursor((W - textWidth(l2)) / 2, 53); cg.print(l2); }
    pushCanvas();
    frame++;
    int32_t wait = 40 - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 1)) return;
  }
}
