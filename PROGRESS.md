# PROGRESS

## Working state (2026-11-22: card 1 = dev card on USB at the desk, beta20; card 2 = soak, beta18)
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
- **Pack builder hosted** at /builder/ (GitHub token, one-commit Publish); new cards' first show = `Holiday Starter/`
  pack (`install`, per-picture dates). Details in PROJECT-NOTES.
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
0000. **"Build the radar screen"** - radar probe v2 approved by eye on card 2 (Seattle rain loop over a dim map,
   home cross + town, 25 MI ring, radar time top-right). Probe + notes: branch `claude/inspiring-perlman-7d2eaa`,
   `firmware/probes/radar_probe/` (NOTES.md has URLs, memory numbers, makebase.py). Make it a weather screen (`radar`
   checkbox) that only plays when rain/snow is on the way (same test as rainSoon). The page builds the town's map
   once (OSM tiles in the browser) and uploads it (~8 KB) when the town is saved; frames in flash, not RAM.
   Data: RainViewer over plain HTTP (zoom 7). Needs PNGdec 1.1.6 in CI libs; its 45 KB block fits only after
   playerFree(), so allocate it first and skip the round if it fails. Details in NOTES.md (merge only the notes).
000. **"Soak results"** - after 11-23 08:00 run `tools/soak-log.ps1 -Cards 192.168.50.199 -Summary`: want crashes 0,
   no lowmem restarts, one nightly restart. If card 2's block keeps sinking, find what holds it (20 items). Don't
   flash/restart card 2 till then; card 1 is free for firmware work. Radar waits on this. Card 2's auto-update
   is OFF for the soak (`POST /api/settings?au=0`); turn it back on after (`?au=1`).
00. **Judge beta17 by eye** - message scrolling and the moons (sun & moon screen).
00a. **"Weather screens look good" / "change X"** - card 1: now, 24 hours sliding, 10 days sliding, details, air,
   sun & moon. Speed, hold times, column width and temperature colors are easy to change. Card 2 already has beta18.
00. **"QR works close and far" / "change X"** - press S1 on either card; try the phone at 1.5 ft, 4 ft and 7 ft.
0. **"New effects look good" / "change X"** - run `tools/effects-probe.ps1` on card 1 (beta20): sleigh scene, turkey,
   bunny, bats, shamrocks, flag, 3 holiday countdowns, text over the flag, balloons, fish, hearts, text over the
   small sleigh. Not redone yet (procedural, already full color): snow, sparkle, confetti, fireworks, stars, fire,
   code rain, rain, lights, flag.
1. **"Fonts/icons look good" / "change X"** - weather + forecast and a Jumbo text item ("use the straight rays").
2. **"Re-add the Christmas pack"** - on the phone page, ★ Get the latest pictures, add Christmas 2026, Send; they
   should now come in as GIF and look like the originals. Try Shuffle at the same time.
3. **Online builder** works (Christmas 2026 published from it). Next there: Holiday starter animations → new cards.
4. **"Try WiFi only"** - card 1 on USB (close any serial-log tab), Chrome: install.html → Set up WiFi only →
   pick the same WiFi + password. Then a full install on card 2 (also tests the new first show). Then 1.0.0.
