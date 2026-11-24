# PROGRESS

## Working state (2026-11-22: 1.0.0 released; card 1 = dev card, beta20 until its 3 am update; card 2 = 1.1.0-beta2 (radar range + birthdays), au off)
- **beta20 (better effect art + card report), on card 1:** sleigh scene, turkey, bunny + eggs, bats + pumpkins +
  moon, shamrocks, balloons, fish, hearts use shaded sprites from `tools/make-fxsprites.mjs` (1 byte/pixel + palette
  per sprite: 21 KB flash, no RAM; web/fxsprites.js for the page). App 1.64 of 1.75 MB (195 KB free). Art not
  judged by eye yet. Card report: app → "Something wrong? Card report" (Copy / Share); /api/info adds id, rssi,
  chip, app, appMax, boots, lifeCrashes, lastCrash (lifetime, NVS "pjlife"). Verified on card 1.
- **beta19 (holidays):** countdown date can be MM-DD, a US holiday key or `next` (screens.ino HOLIDAYS = page
  3-weather.js list); effect `auto` on a countdown = the holiday's effect; turkey, bunny, bats, shamrocks, flag.
- **beta18 (memory safety):** quiet restart every night at 4 am (after 20 h up; every 3 days with no clock) and at
  once if the biggest free block drops under 12 KB (normal ~32 KB). Quiet = no intro, no address screen, clock kept
  through RTC memory so night mode stays dark. `/api/info` adds up, minHeap, block, boot (power/restart/crash/
  nightly/lowmem), bootWhy, quietRestarts, crashes (since power-on). `POST /api/restart?quiet=1` tests it (verified
  on card 1: boot nightly, count 1). Soak: `tools/soak-log.ps1` (CSV in `soak/`, git-ignored; `-Summary`). Card 1 ran 8.9 h clean (block 31.7 KB,
  0 crashes), then the soak moved to card 2 (family room, 20 items, flash full) on 2026-11-22 10:45, 32 h, tab
  "soak card 2". Loaded card: block 27.6 KB between items, ~17 KB while a .pjm plays; player freed 59 KB.
- **Pack builder hosted** at /builder/; new cards' first show = `Holiday Starter/` pack. Details in PROJECT-NOTES.
- Card 1 (FM6124C): 192.168.50.16 / eed0 (USB COM10). Card 2 (RUL6024): 192.168.50.199 / 5df8. Rest: PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups, no https->card.
- LEDs want crisp, high-contrast pixels with black around them: no soft edges, glows or shaded rims (fonts, icons).
- Pictures: GIF unless GIF visibly loses detail; full color only as 4:4:4 JPEG (never the browser's JPEG).
- Fast-scan screens: draw with drawPixelRGB888 and 0/255 colors.
- New cards' first show = loose pictures + "install" packs (pack builder), not hard-coded in the build.
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP (videos too).
- Structs used in function signatures go in the main .ino; no default arguments in other .ino files.
- No sports scores (2026-11-22): people who care have the game on TV.
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.

## Next (the user can answer with one of these commands)
0000. **1.1.0-beta2 on card 2 (branch `radar`, NOT merged).** 11-24: Regional judged great by eye. Now set to Close +
   "even when dry (test)" (writes `radar=always`; re-sends keep it). Try each range from **http://192.168.50.199/local**
   (the website app doesn't have this code until the merge). Remove the test box before release.
   - 11-24 also on card 2: radar 750 ms/frame (was 450, "too fast"); night brightness Off/5/10/25% (50 gone, old 50
     = 25; finer math so 5% of 50% isn't rounded away); a new card starts at 50% brightness. Not judged by eye yet.
   - **Radar:** range picker next to the radar checkbox (`rzoom=`): Close 10-mi ring (zoom-7 tile 1:1), Normal 25,
     Wide 50, Regional 100 (no roads). Home at column 85 (weather comes from the west), labels on the right, no town
     name. US state lines (solid) + county dots (Close/Normal) from Census us-atlas. Old radar.map no longer matches.
   - **Birthdays & dates** (replaces + Reminder): one item, list in `/dates.txt` (GET/POST /api/dates; Send/Clear
     leave it), shows only within N days (default 7), one screen per date, fireworks + "HAPPY BIRTHDAY" on the day,
     "TURNS 41" with a year. Paste a list, Save/Load a file, the phone keeps a copy and offers it back.
   - App 1.71 of 1.75 MB. Release (user, 11-23): only after a real rain loop by eye, merged as 1.1.0.
000. **Soak passed (card 2, 32.3 h, 11-23)**: 0 crashes, 0 lowmem restarts, block flat 26-32 KB. First nightly restart
   due 11-25 04:00 (20 h rule). Card 2's auto-update stays OFF while it runs the beta.
00. **Judge beta17 by eye** - message scrolling and the moons (sun & moon screen).
00a. **"Weather screens look good" / "change X"** - card 1: now, 24 hours sliding, 10 days sliding, details, air,
   sun & moon. Speed, hold times, column width and temperature colors are easy to change.
00. **"QR works close and far" / "change X"** - press S1 on either card; try the phone at 1.5 ft, 4 ft and 7 ft.
0. **"New effects look good" / "change X"** - run `tools/effects-probe.ps1` on card 1 (beta20): sleigh scene, turkey,
   bunny, bats, shamrocks, flag, 3 holiday countdowns, text over the flag, balloons, fish, hearts, text over the
   small sleigh. (Procedural effects like snow, fire, fireworks are already full color, not redone.)
1. **"Fonts/icons look good" / "change X"** - weather + forecast and a Jumbo text item ("use the straight rays").
2. **"Re-add the Christmas pack"** - on the phone page, ★ Get the latest pictures, add Christmas 2026, Send; they
   should now come in as GIF and look like the originals. Try Shuffle at the same time.
3. **Online builder** works (Christmas 2026 published from it). Next there: Holiday starter animations → new cards.
4. **Installer is solid** - spare 2026-11-22: 10/10 verified writes, 0 retries; mid-write unplug recovered on re-plug
   by itself (details PROJECT-NOTES). WiFi only worked on card 1 (install steps now hide). 1.0.0 released 2026-11-22.
