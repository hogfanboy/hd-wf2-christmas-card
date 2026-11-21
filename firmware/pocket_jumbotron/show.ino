// The show: an ordered list of items, each shown for a number of seconds.
//   /show.txt, one item per line, fields separated by TABs (written by the web page):
//     pic       <sec> <file in /gifs>
//     text      <sec> <fg> <bg> <font 1-4> <effect> <message>
//     clock     <sec> <fg> <bg> <font> <effect> <12|24>
//     countdown <sec> <fg> <bg> <font> <effect> <MM-DD> <label>
//     fx        <sec> <fg> <bg> <font> <effect>                      (an effect on its own, full screen)
//     weather   <sec> <fg> <bg> <font> <effect> <F|C|A> <lat> <lon> <place>   lat/lon "auto" = where the card is
//               (found by its internet address, online.ino), A = F in the US, else C; empty place = that town
//     news      <sec> <fg> <bg> <font> <effect> <topic>     top, world, business, sports, tech, science (fw 0.9.7)
//     stocks    <sec> <fg> <bg> <font> <effect> <symbols>   e.g. ^GSPC,^DJI,AAPL (see tools/build-feeds.mjs)
//   sec 0 = automatic (pictures: 8-20 s by their own length; scrolling text: one pass; others: 8-10 s).
//   fg/bg are RRGGBB hex, fg may be "rainbow". effect: one of FX_NAMES (none, snow, sparkle, hearts, confetti,
//   fireworks, stars, fire, matrix; sleigh, balloons, fish, rain since 1.0). Unknown names (e.g. the removed "plasma") play as none.
//   Any line may end with an options field starting with '@' (space-separated, all optional; fw 0.9.0+):
//     dates=MMDD-MMDD  only between these days, every year (may wrap past New Year, e.g. 1215-0105)
//     days=0123456     only on these weekdays (0 = Sunday)
//     hours=H-H        only from the first hour up to (not including) the second; may wrap past midnight
//     lead=N           countdown: only in the last N days before the date (a reminder)
//     forecast         weather: then a 5-day forecast screen
//   Items with a time rule are skipped until the clock is set. Older firmware ignores the field except as
//   the last text of a line.
//   Without /show.txt every file in /gifs plays in name order (the old behavior).
// Picture files: .gif (AnimatedGIF) or .pjm = full-color JPEG frames:
//   "PJM1", then per frame: u32 jpeg length, u16 delay ms, u16 0, the JPEG (128x64 baseline).
// Time (for clock/countdown/night mode) comes from NTP once home WiFi is up; the time zone is a POSIX TZ
// string saved by the web page.

bool timeValid() { return time(nullptr) > 1700000000; }
void timeBegin() { configTzTime(tzString.c_str(), "pool.ntp.org", "time.google.com"); }

static uint32_t nightLast = 0;
void forceNightCheck() { nightLast = 0; checkNight(); }
void checkNight() {
  uint32_t &last = nightLast;
  if (last && millis() - last < 20000) return;
  last = millis();
  bool n = false;
  if (nightFrom >= 0 && nightTo >= 0 && nightFrom != nightTo && timeValid()) {
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    int h = lt.tm_hour;
    n = nightFrom < nightTo ? (h >= nightFrom && h < nightTo) : (h >= nightFrom || h < nightTo);
  }
  if (n != nightNow) { nightNow = n; applyBrightness(); Serial.printf("Night mode %s\n", n ? "on" : "off"); }
}

// ---------- Show list ----------
static uint8_t fxByName(const String &s) { for (int i = 0; i < FX_COUNT; i++) if (s == FX_NAMES[i]) return i; return FX_NONE; }

static bool isPicture(const String &n) { String l = n; l.toLowerCase(); return l.endsWith(".gif") || l.endsWith(".pjm"); }
static uint32_t hex(const String &s) { return strtoul(s.c_str(), nullptr, 16) & 0xFFFFFF; }

static void parseOptions(ShowItem &it, const String &opts) {
  int s = 0;
  while (s < (int)opts.length()) {
    int e = opts.indexOf(' ', s); if (e < 0) e = opts.length();
    String t = opts.substring(s, e); s = e + 1;
    int eq = t.indexOf('='), dash;
    String k = eq < 0 ? t : t.substring(0, eq), v = eq < 0 ? "" : t.substring(eq + 1);
    dash = v.indexOf('-');
    if (k == "dates" && dash > 0) { it.dFrom = v.substring(0, dash).toInt(); it.dTo = v.substring(dash + 1).toInt(); }
    else if (k == "days") { it.days = 0; for (char c : v) if (c >= '0' && c <= '6') it.days |= 1 << (c - '0'); if (!it.days) it.days = 0x7F; }
    else if (k == "hours" && dash > 0) { it.hFrom = constrain((int)v.substring(0, dash).toInt(), 0, 23); it.hTo = constrain((int)v.substring(dash + 1).toInt(), 0, 24); }
    else if (k == "lead") it.lead = v.toInt();
    else if (k == "forecast") it.forecast = true;
  }
}
static int daysUntil(const String &mmdd);
// Is this item on the schedule right now? (see the options above)
bool itemActive(const ShowItem &it) {
  bool timed = it.dFrom || it.days != 0x7F || it.hFrom >= 0 || it.lead >= 0;
  if (!timed) return true;
  if (!timeValid()) return false;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  if (it.dFrom && it.dTo) {
    int md = (lt.tm_mon + 1) * 100 + lt.tm_mday;
    bool in = it.dFrom <= it.dTo ? (md >= it.dFrom && md <= it.dTo) : (md >= it.dFrom || md <= it.dTo);
    if (!in) return false;
  }
  if (!(it.days & (1 << lt.tm_wday))) return false;
  if (it.hFrom >= 0 && it.hTo >= 0 && it.hFrom != it.hTo) {
    int h = lt.tm_hour;
    bool in = it.hFrom < it.hTo ? (h >= it.hFrom && h < it.hTo) : (h >= it.hFrom || h < it.hTo);
    if (!in) return false;
  }
  if (it.lead >= 0 && it.type == 'd' && it.extra.length() >= 5 && daysUntil(it.extra) > it.lead) return false;
  return true;
}

void loadPlaylist() {
  playlist.clear();
  File sf = LittleFS.open("/show.txt", "r");
  if (sf) {
    while (sf.available()) {
      String line = sf.readStringUntil('\n');
      line.replace("\r", "");
      std::vector<String> f;
      int s = 0;
      for (;;) { int t = line.indexOf('\t', s); if (t < 0) { f.push_back(line.substring(s)); break; } f.push_back(line.substring(s, t)); s = t + 1; }
      ShowItem it;
      if (f.size() > 3 && f.back().startsWith("@")) { parseOptions(it, f.back().substring(1)); f.pop_back(); }
      if (f.size() < 3) continue;
      it.sec = f[1].toInt();
      if (f[0] == "pic") { it.type = 'p'; it.file = "/gifs/" + f[2]; if (!LittleFS.exists(it.file)) continue; }
      else {
        if (f.size() < 6) continue;
        it.type = f[0] == "text" ? 't' : f[0] == "clock" ? 'c' : f[0] == "countdown" ? 'd' : f[0] == "fx" ? 'f' : f[0] == "weather" ? 'w' :
                  f[0] == "news" ? 'n' : f[0] == "stocks" ? 's' : 0;
        if (!it.type) continue;
        it.rainbow = f[2] == "rainbow";
        it.themed = f[2] == "theme";
        it.fg = it.rainbow || it.themed ? 0xFFFFFF : hex(f[2]);
        it.bg = hex(f[3]);
        int fn = f[4].toInt();
        if (fn >= 5 && fn <= 7) fn -= 3;                                // 0.9.4 jumbo S/M/B -> classic S/M/B
        it.font = isPJ(fn) ? fn : constrain(fn, 1, 4);
        it.effect = fxByName(f[5]);
        f.resize(std::max<size_t>(f.size(), 10));
        if (it.type == 't') it.text = f[6];
        if (it.type == 'c') it.extra = f[6];
        if (it.type == 'd') { it.extra = f[6]; it.text = f[7]; }
        if (it.type == 'f' && it.effect == FX_NONE) continue;          // removed effect
        if (it.type == 'n' || it.type == 's') { it.extra = f[6]; if (it.type == 's' && it.extra.isEmpty()) continue; }
        if (it.type == 'w') { it.extra = f[6] + "," + f[7] + "," + f[8]; it.text = f[9]; if (f[7].isEmpty()) continue; }
      }
      playlist.push_back(it);
    }
    sf.close();
    if (!playlist.empty()) return;
  }
  std::vector<String> names;
  File dir = LittleFS.open("/gifs");
  if (dir && dir.isDirectory())
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) if (isPicture(f.name())) names.push_back(String("/gifs/") + f.name());
  std::sort(names.begin(), names.end());
  for (auto &n : names) { ShowItem it; it.file = n; playlist.push_back(it); }
}

void playItem(const ShowItem &item) {
  ShowItem it = item;
  if (it.themed) it.fg = th().main;
  if (it.type != 'p') Serial.printf("  %c %-10s free %u, largest block %u\n", it.type, FX_NAMES[it.effect], (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  switch (it.type) {
    case 'f': playFx(it); break;
    case 'w': playWeather(it); break;
    case 'n': playNews(it); break;
    case 's': playStocks(it); break;
    case 't': playText(it); break;
    case 'c': playClock(it); break;
    case 'd': playCountdown(it); break;
    default: {
      String l = it.file; l.toLowerCase();
      if (l.endsWith(".pjm")) playPjm(it); else playOne(it.file, it.sec);
    }
  }
}
