# PROGRESS

## Working state (fw 0.9.1, 2026-11-20; card 1 on 0.9.1 via WiFi, card 2 still 0.6.2)
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, web app, packs, vault.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (also on the PC's USB, COM10). Card 2 (RUL6024): 192.168.50.199.
- **App on the website (0.9.0, verified):** the card's `/` is a ~1 KB starter (`SHELL_HTML`, net.ino) that fetches
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
- **QR screen (0.9.0):** white `http://<ip>` code, panel in fast scan (2-bit, 651 Hz) while it's up. All 4 test styles
  scanned with the Pixel camera on 0.8.4. S1 press = show address, press again = back to the show.
- 0.8.2: animated WEBP from the card page (own frame splitter; `ImageDecoder` is https-only).
- Effects, weather, clock, countdown, full-color `.pjm`, night mode, packs, family vault: see PROJECT-NOTES.md.

## Card 1 test show (left on for the user to judge)
- `01.pjm`, weather Fayetteville AR + forecast (8 s each), Christmas countdown `lead=40` (shows), a red
  "HIDDEN TEST" countdown `lead=10` (must NOT show until Dec 15). Replace via the page when done.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups (tested,
  failed in Chrome), no direct https→card calls (blocked).
- Fast-scan screens must use `CF()` colors only (library LUT is 8-bit; at runtime 2-bit only its low 2 bits count).
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP.
- Private content = encrypted packs, unlocked by a family code. Never write a family code into this repo.
- Build with `--build-property upload.maximum_size=1835008` (app is 1.37 MB).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show (form type = empty show), then POST /api/play.

## Next (1.0 plan)
1. User judges on card 1: forecast screen look, reminder, QR screen. Still unjudged: full color, night mode.
2. Page: forecast preview (JS copy of `playForecast`), a "Reminders" quick-add (name + date + days before).
3. Firmware restructure into modules (panel / player / show+schedule / net+api) with headers, no behavior change.
4. Improv WiFi in the installer, news headlines item, update card 2 (and the other 13 cards) to 1.0.
