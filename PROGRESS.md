# PROGRESS

## Working state (fw 0.9.8 on card 1 via WiFi, test show playing; card 2 still 0.6.2, offline 2026-11-20)
- **0.9.8 (card 1 runs it, serial clean; panel look NOT yet judged by the user):**
  - **Jumbo font (our own):** `web/pjfont.js` = stroke glyphs (rounded-square scoreboard style), rendered with 4-bit
    soft edges by `tools/make-pjfont.mjs` into `pj_font.h` (52 KB) and by the page (same code, same pixels).
    Font numbers 8 thin (cap 9), 9 small (11), 10 medium (16), 11 big (24), 12 huge (34). Items using 8-12 also get
    thin Jumbo labels (`pjLabels`, `label()/labelC()` in canvas.ino). fitBig: 12 -> 11 -> 10. Degree sign = char 127.
    Page: Size menu "Jumbo ..." + "Classic ..."; new items default to Jumbo (news 8, text/stocks 11, clock/weather 12).
  - **Weather icons:** `tools/make-wxicons.mjs` draws shaded sprites (sun 6 frames, moon, 4 cloud tones, bolt, 7
    forecast minis) -> `wx_icons.h` (RGB565+A8, 70 KB) + `web/wxicons.js` (13 KB PNG for the page). Rain, snow,
    stars, drift and lightning are animated on top in weather.ino (`wxSprite`, `wxFall`).
  - **Self-update:** daily at 3:xx (minute from MAC) the card reads `firmware/info.json`, downloads a newer app over
    HTTPS into the other slot, checks SHA-256, restarts (rollback protects). NVS `aupd`, /api/info `autoUpdate`,
    `/api/settings?au=0|1`, `POST /api/update-site` = install now. Page: "Software updates" box with the switch.
    **Verified on card 1:** a build labelled 0.9.0 + update-site -> fetched 0.9.8 from Pages, 16 s, confirmed, show resumed.
  - **Improv WiFi:** `improvPoll()` in net.ino (Improv Serial over USB). install.html: after flashing, a "Connect it
    to your home WiFi" step (scan list, password) -> card joins, saves, restarts; done steps show its address.
    **Verified on card 1 (PowerShell over COM10):** state/URL, device info, scan list. Not yet: the WiFi-set step and the page.
  - Version 0.9.8; app 1.53 MB of 1.75 MB.
- **0.9.7 (card 1, serial):** HTTPS news/markets feeds from the website (200 in ~800 ms, heap ~78 KB steady),
  automatic location (Naperville) + time zone, news/stocks items. User: news text was too big and pure white too harsh.
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, web app, packs, feeds.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (also on the PC's USB, COM10). Card 2 (RUL6024): 192.168.50.199.
- App on the website, schedule `@...`, 5-day forecast, QR screen, effects, night mode, packs: see PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups, no https->card.
- Fast-scan screens must use `CF()` colors only (library LUT is 8-bit; at runtime 2-bit only its low 2 bits count).
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP.
- Pictures live in the private repo; the website is public, so they're unlisted, not secret.
- No 10/12-bit color (HUB75 lib 3.1.0 #errors above 8 bits). Build with `upload.maximum_size=1835008`.
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.
- build-web inserts vendor scripts with a function replace (a `$'` in pjfont.js broke a plain string replace).

## Next
00. **User judges card 1's test show** (`%TEMP%\pj-test2.txt`): 7 text colors (0 pure white ... 6 lavender, each a
   label then the clock), news Jumbo thin vs small, stocks, alphabet scroll, weather + forecast with new icons.
   Then set the page's default text color to their pick and promote 0.9.8 to PROJECT-NOTES.md.
1. Test install.html end to end on a card over USB (card 2 or a tester card), including the WiFi step.
2. Beta testers: flash card 2 and the testers' cards. Card fetching new packs by itself (IDEAS.md).
