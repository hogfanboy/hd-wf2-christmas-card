# PROGRESS

## Working state (fw 0.8.3, 2026-11-20; card 1 on 0.8.3 via WiFi, card 2 still 0.6.2)
- 0.8.2: animated WEBP animates from the card page (`ImageDecoder` is https-only, so `decodeWebpAnim` in
  `web/index.html` splits frames itself). Tested in the preview; user to try from the PC.
- 0.8.3 fast-scan QR screen: `panelSetFast()` restarts the panel at 2-bit color while the address screen is up
  (serial: 651 Hz vs 84 Hz full color, ~20 ms per switch, heap back to ~82 KB). `panelStop()` frees the GDMA
  channel the library leaks on delete. Hz shows at the bottom of the QR screen.
- 0.8.3 bridge test: card serves `/bridge` (runs requests from https://hogfanboy.github.io against its own /api);
  `/api/info` sends CORS + private-network headers; `docs/bridge-test.html?ip=<ip>` runs both tests.
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, public packs, encrypted packs.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (also on the PC's USB, COM10). Card 2 (RUL6024): 192.168.50.199.
- Show (`show.ino`, `/show.txt`): pictures, messages, clock, countdown, **effects** (`fx`), **weather**, seconds per item.
  Pictures are `.gif` or full-color `.pjm`. NTP time + phone's time zone; night mode.
- Effects library (drawn by the card, no storage): snow, sparkle, hearts, confetti, fireworks, starfield, fire,
  code rain. User loves them all (judged by eye on card 1); plasma removed at their request (0.8.1). Full screen as an item, or behind any message/clock/countdown/weather. The card page has a JS
  copy of each for the preview (keep `show.ino` and the page's `fxStep` in step).
- 0.8.0 weather: Open-Meteo over plain http (no key, no TLS RAM), refreshed at most every 15 min; town picked on the
  page (Open-Meteo geocoding), stored in the show line. Animated icon, temp, condition, high/low, town.
- Ran 3+ loops on card 1 (serial log): no resets, heap ~81 KB, largest block ~31 KB. Fixed: `.pjm` buffer now sized
  to the file's biggest frame (fixed 24 KB failed with "no memory for jpeg" after weather fetches).
- Startup screen (0.8.1): the "POCKET JUMBOTRON" arcade marquee (`firmware/intro.gif` → `tools/make-intro.mjs` →
  `intro_gif.h`), 2 loops (~5 s), S1 skips it.
- Card page: show builder (+ Effect, + Weather), load show back, packs, 🔒 family packs, one-tap update.
- Website: homepage text rewritten for the real flow; `go.html` = QR target that forwards to `http://<ip>` and
  remembers the screen (home-screen icon via `go.webmanifest`); `go.html?test` shows QR codes for a camera test.
- Private pictures: repo hogfanboy/pocket-jumbotron-content → `tools/build-vault.mjs` → `docs/vault/` (see notes).

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34.
- S1 button: GPIO17.

## Panel setup (same on every card)
- ESP32-HUB75-MatrixPanel-DMA 3.1.0, `cfg.driver = DP3246`, default 60 Hz; after `begin()` `GPIO.func_out_sel_cfg[CLK].inv_sel = 0`.
- `panelRegInit()`: LAT 12 = REG1, LAT 13 = REG2 (FM6126A values), LAT 11 = all ones, LAT 14 = 0x0100. FM6124C ignores them.

## Key decisions
- One firmware and one panel setup for every card (no per-chip detection).
- Card stores GIF or JPEG frames, never WEBP (no small decoder, RAM). WEBP is an input format only.
- Private content = encrypted packs on public Pages, unlocked by a family code. Never write a family code into this repo.
- Build with `--build-property upload.maximum_size=1835008` (app is 1.34 MB now; the default 1.25 MB check fails
  the build and silently keeps the old .bin).

## Active: two tests that decide the new structure
- QR: user scans the fast-scan QR screen with the Pixel camera (link type was already fine). Press S1 several
  times: steps styles, and the serial log's "free heap" shows whether panel restarts leak.
- Website → card: user opens bridge-test.html in PC Chrome and on the phone. Test 1 = direct (Chrome local
  network permission), test 2 = bridge tab. The in-app browser blocks test 1 and can't open the tab (no verdict).

## Next (proposed structure, test results first)
- Card = display engine: modules (panel incl. fast scan, layers, screens, scheduler, storage, net/API v1),
  `show.json`, rescue page + bridge in firmware. Website (Pages, https) = the app: studio, reminders, packs.
- Then: reminders ("start N days before", yearly), 5-day forecast (same Open-Meteo call), news, Improv, card 2.
- Still unjudged on the panel: full color, night mode, weather look.
