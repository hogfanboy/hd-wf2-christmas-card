# PROGRESS

## Working state (fw 0.9.7 built, not pushed; card 1 on 0.9.5 (user: all good), card 2 still 0.6.2)
- **0.9.7 (compiles; page checked on the fake card; NOT run on a card yet, not pushed):** HTTPS from the card
  (`online.ino`: `siteGet`, trusts ISRG Root X1/X2 in `site_ca.h`, frees the player buffers during the TLS fetch).
  CI `tools/build-feeds.mjs` writes `docs/feeds/` hourly: `news-<topic>.txt` (NPR/BBC/ESPN/Ars RSS) and
  `markets.txt` (Yahoo chart API, 21 symbols, day sparkline). New show items `news <topic>` (headlines scroll
  under a title bar) and `stocks <syms>` (price, change, today's line); page + News/Stocks buttons and previews.
  Location once per start from ip-api.com (http): time zone when the page never set one (`tzAuto`), and
  `weather ... A auto auto` = weather where the screen is (page: 📍 button, Units Auto). /api/info has place, tzAuto.
  Install image now has a first show: clock, weather here + forecast, private-repo pictures (loose ones, or packs
  with `"install": true`), holiday greetings 11/15-1/6, title GIF.
- **0.9.6 (fake card only):** card serves the tab icon at `/favicon.ico` + starter page links it (Chrome showed the
  globe on the card's page: 404). Family code + encrypted vault removed (page, firmware `fc`, build-vault, aes-js,
  js-sha256; page 192 -> 109 KB). Pictures now live only in the private repo; CI runs
  `build-packs.mjs private-content` (plain packs on Pages). Page always loads the screen's show on open/refresh;
  green line under "1. Build your show" says so, ↻ Reload beside it; Send refuses while loading and warns if the
  load failed; items added before the screen answers are kept after its items.
- **0.9.5 (user checked on card 1):** Jumbo font dropped (font 5-7 loads as 2-4); page auto-loads the screen's
  show, compact tap-to-open rows, ✕ removes; forecast on by default. **0.9.4:** AM/PM beside the clock, `fitBig`.
- **0.9.2:** QR at full brightness; + Reminder (countdown, lead 14). **0.9.3:** one file per job.
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, web app, packs, feeds.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (also on the PC's USB, COM10). Card 2 (RUL6024): 192.168.50.199.
- **App on the website (0.9.0, verified):** the card's `/` is a ~1 KB starter (`SHELL_HTML`, web.ino) that fetches
  `docs/app/index.html` from Pages, saves it in the phone's localStorage, and document.write()s it, so it runs on
  the card's origin (same-origin /api). No website and no saved copy: `/local` = built-in copy (web_index.h).
  The page footer line says `app <date> <sha>` or `app built-in`. `/api/info` has `"api":1`.
- **Schedule (0.9.1, verified by serial):** optional last field `@...` on any show line: `dates=MMDD-MMDD`,
  `days=0123456`, `hours=H-H`, `lead=N` (countdown reminder), `forecast` (weather). `itemActive()` skips items off
  schedule; all off = blank screen, checked every second. Page: ⏰ When panel on every item, "Start showing N days
  before" on countdowns, "Then a 5-day forecast" on weather. The page always writes the `@` field.
- **5-day forecast (0.9.1):** same Open-Meteo call (`forecast_days=5`), 5 columns: weekday, small icon, high, low.
  Parsed OK on card 1 (5 days, 329 ms, heap ~81 KB). Weather backs off 5 min after a failed fetch (Open-Meteo
  was down for a while on 2026-11-20 and each try stalled the show 4 s).
- **QR screen (0.9.0/0.9.2):** white `http://<ip>` code, panel in fast scan (2-bit, 651 Hz) while it's up. All 4 test styles
  scanned with the Pixel camera on 0.8.4. S1 press = show address, press again = back to the show.
- Effects, weather, clock, countdown, full-color `.pjm`, night mode, packs: see PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups (tested,
  failed in Chrome), no direct https→card calls (blocked).
- Fast-scan screens must use `CF()` colors only (library LUT is 8-bit; at runtime 2-bit only its low 2 bits count).
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP.
- Pictures live in the private repo (off public GitHub); the website is public, so they're unlisted, not secret.
- No 10/12-bit color: HUB75 lib 3.1.0 #errors above 8 bits at compile time; 12-bit would also need ~+50% DMA RAM.
- Build with `--build-property upload.maximum_size=1835008` (app is 1.36 MB).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show (form type = empty show), then POST /api/play.

## Next (1.0 plan)
00. **0.9.7 on card 1 (WiFi update), serial log:** `HTTPS ...: 200` line with heap numbers, `Location:` + `Time
   zone (automatic)` lines, news/stocks items readable on the panel (scroll speed ~55 px/s, stock fonts). Feeds
   only exist on the website after the next push, so push (CI) first, then test. Then retest a fresh install image.
0. Beta testers 2026-11-21 evening: user puts 0.9.6 on card 1, checks the tab icon on the card's page, checks the clock (12h + 24h), the page with a
   long reminder list, QR screen, a + Reminder item, overnight soak; then flash card 2 and the testers' cards (USB installer or WiFi update).
2. Optional: colorful pixel-art weather icons (IDEAS.md); user is fine with the current ones.
3. Improv WiFi in the installer, update card 2 (and the other 13 cards) to 1.0. Card fetching new packs by itself (now possible with HTTPS): IDEAS.md.
