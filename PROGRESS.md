# PROGRESS

## Working state (fw 0.8.2, 2026-11-20; card 1 on 0.8.2 via WiFi, card 2 still 0.6.2)
- 0.8.2: animated WEBP now animates from the card page. `ImageDecoder` only exists on https pages, so the page
  splits WEBP frames itself (`decodeWebpAnim` in `web/index.html`). Tested in the preview; user to try from the PC.
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

## Active issue: QR code
- Camera test on the PC monitor (go.html?test): the Pixel camera app offers both the http://<ip> and the https go
  link. So the link type is fine; the panel's QR rendering is what the camera app won't read.
- 0.8.0 styles 7-8 never drew (QR encoder capped at version 3); fixed in 0.8.1 (max version 5). Style 8 is now dim
  white at full PWM (camera glare idea). User test: styles 7 and 8 on the panel.
- Idea under discussion: card page app hosted on GitHub Pages (see Next).

## Next
- User checks the startup screen and QR styles 7-8 on card 1. Still unjudged: full color, night mode, weather look.
- Decide: card serves a tiny shell page that loads the app from GitHub Pages (same-origin API calls, app updates
  without firmware). A pure https GitHub page can't call the card (mixed content + no way to find it by ID).
- QR test above, then keep one style. Then: news headlines item, Improv WiFi in the installer, update card 2.
