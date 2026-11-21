# PROGRESS

## Working state (fw 0.9.9 built + pushed; card 1 and card 2 on it via WiFi)
- **0.9.9 (on both cards; NOT yet judged by eye):**
  - **Themes:** 7 themes (canvas.ino `THEMES`, page `THEMES`): 0 pure white, 1 warm white, 2 soft gold (default),
    3 ice blue, 4 mint, 5 peach, 6 lavender; each = main text + accent (titles, weather word, day names) + dim
    (dates). Show line color field `theme` = follow the card's theme (`it.themed`, resolved in `playItem`). NVS
    `theme`, /api/info `theme`, `/api/settings?theme=N`. Page: swatch row under "1. Build your show" (saves to
    the card at once); per item "Theme" checkbox or a color (picking a color unticks Theme). New items use Theme.
    Old cards (no `theme` in info) get the theme's hex color instead.
  - **Forecast roomier:** day, icon (y 20), high in bold Jumbo small (9), low dim; town line dropped (it's on the
    first weather screen). **Degree sign:** a thin ring (stroke x0.55), top level with the digits.
  - **QR screen redesign:** `HTTP://<IP>` upper case -> alphanumeric QR version 1 -> 2 LEDs per module (58 px, was
    33 px); all drawn with drawPixelRGB888 0/255 colors (right at any bit depth); "SCAN ME" in Jumbo, address on
    one line in a 4x7 digit font. `fastBits` (default 2); `POST /api/qr?bits=N` shows it 60 s at N bits with a red
    "N BIT" tag (probe: `tools/qr-probe.ps1`). **Probe result (card 1, by eye):** 2, 3 and 5 bits scan and look
    good, but only from 3-4 ft; 4 bits didn't scan; none scan close up (the old 33 px code did). Maybe the full
    brightness (setBrightness8(255)) or the bigger code. Kept 2 bits. Not fixing unless the user asks.
  - **Readability colors (card 1, not judged yet):** weather line 2 = "H" warm `WX_HI` 0xFF8250 and "L" cool
    `WX_LO` 0x78B4FF (1 px after each), numbers theme main, town dim; forecast lows `WX_LO`; clock date = accent
    (dims looked muddy); news bar time = dim (was a darker dim). Countdown/stocks unchanged. Page preview matches.
  - **QR on card 2:** same as card 1 (scans from 3-4 ft only).
  - **Time zone:** page's Save button is now "Save night mode"; it sends `tz=auto` to cards with a saved phone zone,
    which drops it and uses the zone from the card's location (`geoTz`). Both cards switched (CST6CDT, verified).
  - **Forecast layouts (trying):** `fcStyle` 0 plain, 1 alternate columns shaded, 2 shaded tiles, 3 four days
    (32 px) alternate shaded; `FC_SHADE` 0x1C1C2C. `POST /api/fcprobe?s=N` jumps to the first weather item's
    forecast for 60 s with a red tag (`tools/forecast-probe.ps1`). Not saved; page preview not changed yet.
  - **Moon:** halo removed (read as a gray disc). New reminders default to "5 days before" (was 14).
- **0.9.8 (verified):** Jumbo font, sprite weather icons, self-update (tested: 0.9.0 -> 0.9.8 by itself in 16 s),
  Improv WiFi (card side tested over COM10: state, info, scan; install page's WiFi step not tried on a card yet).
  User liked the font; asked for soft gold, themes, better QR screen, nicer degree sign, roomier forecast.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (PC USB COM10). Card 2 (RUL6024): 192.168.50.199 / pocketjumbo-5df8.
- Everything else (app on the website, schedule, news/stocks, effects, night mode, packs): PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups, no https->card.
- Fast-scan screens: draw with drawPixelRGB888 and 0/255 colors (565 colors lose low bits; at N bits the library
  uses only the color table's N low bits). Soft-edged text can't show in fast scan (draw it 1-bit).
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP.
- Pictures live in the private repo; the website is public, so they're unlisted, not secret.
- Build with `upload.maximum_size=1835008` (app 1.53 MB). Structs used in function signatures go in the main .ino.
- build-web inserts vendor scripts with a function replace (a `$'` in pjfont.js broke a plain string replace).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.

## Next (the user can answer with one of these commands)
1. **"Forecast layout N"** - user runs `toolsorecast-probe.ps1` and picks one. Then make it the only layout
   (drop fcStyle/probe), mirror it in the page preview. QR close up (dimmer white) stays optional.
2. **"Themes look good" / "change theme X"** - user picks themes on the page (swatches) and judges card 1's show.
   Then promote themes + QR + forecast to PROJECT-NOTES.md.
3. **"Test the installer on card 2"** - card 2 on USB: run install.html (online installer) end to end, including
   the new WiFi step (Improv). Confirm with `esptool read-mac` first; use `flash-watch.ps1 -WaitGone` when switching.
4. **"Research features"** - scan GitHub projects using ESP32-HUB75-MatrixPanel-DMA and Huidu HDPlayer's feature
   list for ideas (IDEAS.md parking lot).
5. Later: card fetches new picture packs by itself (HTTPS works now), beta testers' cards.
