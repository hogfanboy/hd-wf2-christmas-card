# PROGRESS

## Working state (fw 0.9.6, 2026-11-20; card 1 on 0.9.5 (user: all good), card 2 still 0.6.2)
- **0.9.6 (fake card only):** card serves the tab icon at `/favicon.ico` + starter page links it (Chrome showed the
  globe on the card's page: 404). Family code + encrypted vault removed (page, firmware `fc`, build-vault, aes-js,
  js-sha256; page 192 -> 109 KB). Pictures now live only in the private repo; CI runs
  `build-packs.mjs private-content` (plain packs on Pages). Page always loads the screen's show on open/refresh;
  green line under "1. Build your show" says so, ↻ Reload beside it; Send refuses while loading and warns if the
  load failed; items added before the screen answers are kept after its items.
- **0.9.5 (user checked on card 1):** slanted Jumbo font dropped (user: looked bad);
  `font_jumbo.h` deleted, show font 5-7 loads as 2-4 (card + page). Page: opens by loading the screen's show
  automatically (Send replaces all, so a sister adding 1 of 20 birthdays no longer wipes the other 19); only one
  item open at a time, the rest are compact tap-to-open rows (name · date + schedule), ✓ Done closes, ✕ top right
  removes; fixed the ⏰ When panel always showing (`.edit{display:grid}` beat `[hidden]`); 5-day forecast box on
  by default for new weather items; PJ tab icon (`docs/favicon.svg`, inline data: URI on card pages).
- **0.9.4:** 12-hour clock: small AM/PM beside the time, top level with the digits. Clock/countdown/weather pick
  the biggest font that fits (`fitBig`). "Something wrong?" box shows /api/info (family code removed) + app build.
  `tools/make-font.mjs` (TTF -> GFX bitmap header) kept for a future upright scoreboard font.
- **0.9.3:** firmware split into one file per job (map at the top of pocket_jumbotron.ino), no behavior change.
- **0.9.2:** QR screen at full panel brightness. + Reminder button (countdown, lead 14), fireworks on the day.
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, web app, packs.
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
- Build with `--build-property upload.maximum_size=1835008` (app is 1.37 MB).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show (form type = empty show), then POST /api/play.

## Next (1.0 plan)
0. Beta testers 2026-11-21 evening: user puts 0.9.6 on card 1, checks the tab icon on the card's page, checks the clock (12h + 24h), the page with a
   long reminder list, overnight soak; then flash card 2 and the testers' cards (USB installer or WiFi update).
1. User checks the QR screen is now white and bright (0.9.2+), and a reminder made with + Reminder.
2. Optional: colorful pixel-art weather icons (IDEAS.md); user is fine with the current ones. Five cloudy days
   = five identical icons is correct (one icon per WMO code group).
3. Improv WiFi in the installer, update card 2 (and the other 13 cards) to 1.0. News ticker + finance: IDEAS.md.
