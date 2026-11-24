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

// ---------- US holidays: a countdown's date is MM-DD, a holiday key (thanksgiving, easter...) or "next" ----------
// wd -1: fixed month/day. wd 7: Easter. Else the n-th weekday (0 = Sunday) of the month, n -1 = the last one.
// Names have no apostrophes (label font). The page has the same list (web/app/3-weather.js HOLIDAYS).
struct Holiday { const char *key, *name; uint8_t m, d; int8_t wd, n; uint8_t fx; };
static const Holiday HOLIDAYS[] = {
  {"newyear", "NEW YEAR", 1, 1, -1, 0, FX_FIREWORKS},       {"mlk", "MLK DAY", 1, 0, 1, 3, FX_STARS},
  {"valentine", "VALENTINES DAY", 2, 14, -1, 0, FX_HEARTS}, {"presidents", "PRESIDENTS DAY", 2, 0, 1, 3, FX_FLAG},
  {"stpatrick", "ST PATRICKS DAY", 3, 17, -1, 0, FX_SHAMROCKS}, {"easter", "EASTER", 0, 0, 7, 0, FX_BUNNY},
  {"mothers", "MOTHERS DAY", 5, 0, 0, 2, FX_HEARTS},        {"memorial", "MEMORIAL DAY", 5, 0, 1, -1, FX_FLAG},
  {"fathers", "FATHERS DAY", 6, 0, 0, 3, FX_BALLOONS},      {"july4", "JULY 4TH", 7, 4, -1, 0, FX_FIREWORKS},
  {"labor", "LABOR DAY", 9, 0, 1, 1, FX_FLAG},              {"halloween", "HALLOWEEN", 10, 31, -1, 0, FX_BATS},
  {"veterans", "VETERANS DAY", 11, 11, -1, 0, FX_FLAG},     {"thanksgiving", "THANKSGIVING", 11, 0, 4, 4, FX_TURKEY},
  {"christmas", "CHRISTMAS", 12, 25, -1, 0, FX_SLEIGH}};
static const int N_HOLIDAYS = sizeof(HOLIDAYS) / sizeof(HOLIDAYS[0]);
static int dowOf(int y, int m, int d) {                         // 0 = Sunday (Sakamoto)
  static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static void holidayDay(int i, int y, int &m, int &d) {           // HOLIDAYS[i] in year y
  const Holiday &h = HOLIDAYS[i];
  m = h.m; d = h.d;
  if (h.wd == 7) {                                               // Easter (anonymous Gregorian algorithm)
    int a = y % 19, b = y / 100, c = y % 100, e = (19 * a + b - b / 4 - (b - (b + 8) / 25 + 1) / 3 + 15) % 30;
    int l = (32 + 2 * (b % 4) + 2 * (c / 4) - e - c % 4) % 7, k = (a + 11 * e + 22 * l) / 451;
    m = (e + l - 7 * k + 114) / 31; d = (e + l - 7 * k + 114) % 31 + 1;
  } else if (h.wd >= 0 && h.n > 0) d = 1 + (h.wd - dowOf(y, m, 1) + 7) % 7 + (h.n - 1) * 7;
  else if (h.wd >= 0) { static const uint8_t DIM[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; int dl = DIM[m - 1]; d = dl - (dowOf(y, m, dl) - h.wd + 7) % 7; }
}
static int daysTo(int y, int m, int d) {                         // whole days from today to y-m-d (negative = past)
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  struct tm a = lt; a.tm_hour = 12; a.tm_min = a.tm_sec = 0; a.tm_isdst = -1;
  struct tm b = a; b.tm_year = y - 1900; b.tm_mon = m - 1; b.tm_mday = d;
  time_t ta = mktime(&a), tb = mktime(&b);
  return (int)floor((double)(tb - ta) / 86400 + 0.5);
}
// Days until a countdown's date (0 = today, -1 = not a date) and which holiday it is (-1 = none).
static int countdownDays(const String &when, int *hol) {
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  int y = lt.tm_year + 1900, best = -1, bi = -1;
  for (int i = 0; i < N_HOLIDAYS; i++) {
    if (when != "next" && when != HOLIDAYS[i].key) continue;
    for (int yy = y; yy <= y + 1; yy++) {
      int m, d; holidayDay(i, yy, m, d);
      int n = daysTo(yy, m, d);
      if (n < 0) continue;
      if (best < 0 || n < best) { best = n; bi = i; }
      break;
    }
  }
  if (bi < 0 && when.length() >= 5 && isdigit((unsigned char)when[0])) {   // MM-DD, every year
    int m = when.substring(0, 2).toInt(), d = when.substring(3, 5).toInt(), yy = y;
    best = daysTo(y, m, d);
    if (best < 0) best = daysTo(++yy, m, d);
    for (int i = 0; i < N_HOLIDAYS && bi < 0; i++) { int hm, hd; holidayDay(i, yy, hm, hd); if (hm == m && hd == d) bi = i; }
  }
  if (hol) *hol = bi;
  return best;
}

static void playCountdown(const ShowItem &item) {
  if (!timeValid()) return;
  int hol, days = countdownDays(item.extra, &hol);
  if (days < 0) return;
  ShowItem it = item;
  if (it.effect == FX_AUTO) it.effect = hol >= 0 ? HOLIDAYS[hol].fx : FX_SPARKLE;   // "match the holiday"
  if (days == 0 && it.lead >= 0) it.effect = FX_FIREWORKS;       // a reminder's big day
  String name = item.extra == "next" && hol >= 0 ? String(HOLIDAYS[hol].name) : it.text.length() ? it.text : hol >= 0 ? String(HOLIDAYS[hol].name) : String("");
  effectInit(it); FxGuard fxg;
  String big = days == 0 ? "TODAY!" : String(days);
  String l1 = days == 0 ? "" : days == 1 ? "DAY UNTIL" : "DAYS UNTIL";
  String l2 = upcase(name);
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

// ---------- Birthdays & dates (show item "dates") ----------
// The list lives in /dates.txt, apart from the show, so Send, Clear and firmware updates never touch it (the page
// reads and writes it with GET/POST /api/dates and keeps a copy on the phone). One date per line, TABs:
//   MM-DD <year born / married, or empty> <kind: b birthday, a anniversary, o other> <name>
// The item plays only when a date is within `lead` days (default 7), one screen per date, soonest first.
static std::vector<DateHit> datesComing(int lead, size_t maxN) {
  std::vector<DateHit> hits;
  File f = LittleFS.open("/dates.txt", "r");
  if (!f) return hits;
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  const int y = lt.tm_year + 1900;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.replace("\r", "");
    int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1), t3 = line.indexOf('\t', t2 + 1);
    if (t1 != 5 || t2 < 0 || t3 < 0) continue;
    int m = line.substring(0, 2).toInt(), d = line.substring(3, 5).toInt();
    if (m < 1 || m > 12 || d < 1 || d > 31) continue;
    int yy = y, n = daysTo(y, m, d);
    if (n < 0) n = daysTo(++yy, m, d);
    if (n < 0 || n > lead) continue;
    int born = line.substring(t1 + 1, t2).toInt();
    hits.push_back({n, born > 1800 && born <= yy ? yy - born : 0, line[t2 + 1], line.substring(t3 + 1)});
  }
  f.close();
  std::stable_sort(hits.begin(), hits.end(), [](const DateHit &a, const DateHit &b) { return a.days < b.days; });
  if (hits.size() > maxN) hits.resize(maxN);
  return hits;
}
static int datesLead(const ShowItem &it) { return it.lead >= 0 ? it.lead : 7; }
// The first text that fits the width.
static String fitLabel(std::initializer_list<String> tries) {
  String last;
  for (const String &s : tries) { last = s; if (s.length() && labelW(s) <= W - 2) return s; }
  return last;
}
static void playDates(const ShowItem &item) {
  if (!timeValid()) return;
  auto hits = datesComing(datesLead(item), 6);
  pjLabels = isPJ(item.font);
  for (auto &h : hits) {
    ShowItem it = item;
    if (h.days == 0) it.effect = FX_FIREWORKS;                  // the big day
    String nm = upcase(h.name), big, l1, l2, n = String(h.years);
    if (h.days == 0) {
      big = nm;                                                 // the name big; too long for that: under "TODAY!"
      l1 = h.kind == 'b' ? "HAPPY BIRTHDAY" : h.kind == 'a' ? "HAPPY ANNIVERSARY" : "TODAY";
      l2 = h.years ? (h.kind == 'b' ? "TURNS " + n : h.kind == 'a' ? n + " YEARS" : "") : "";
      fitBig(it.font, big, W - 2, 0);
      if (textWidth(big) > W - 2) { big = "TODAY!"; if (h.kind == 'o') l1 = ""; l2 = fitLabel({l2.length() ? nm + " " + l2 : "", nm}); }
    } else {
      big = String(h.days);
      l1 = h.days == 1 ? "DAY UNTIL" : "DAYS UNTIL";
      if (h.kind == 'b') l2 = fitLabel({h.years ? nm + " TURNS " + n : "", nm + "'S BIRTHDAY", nm + "'S BDAY", nm});
      else if (h.kind == 'a') l2 = fitLabel({nm + "'S ANNIVERSARY", h.years ? nm + " " + n + " YRS" : "", nm + "'S ANNIV", nm});
      else l2 = nm;
    }
    effectInit(it); FxGuard fxg;
    uint32_t total = (it.sec ? it.sec : 6) * 1000UL, start = millis(), frame = 0;
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
}
