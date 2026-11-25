# PROGRESS

## Working state (2026-11-24: 1.1.0 released; card 2 = 1.1.0, auto-update on; card 1 = dev card)
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
- Card 1 (FM6124C): 192.168.50.16 / eed0 (USB COM10). Card 2 (RUL6024): 192.168.50.199 / 5df8. Rest: PROJECT-NOTES.md.

- **Beta tester 3 (blue top half, sheared bands; panel P2.5-320X160-2020-32S RUL6024+7258D, same as card 2):** branch
  `claude/project-thread-oceq9v` adds app **Panel type** (Standard / No chip setup / FM6126A / Plain, NVS `ptype`, card
  restarts) and test screen 5 "Wiring test" in Color tune. NOT compiled or tried on a card yet. Suspect a HUB75 cable/pin
  fault first. Default behavior unchanged.

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
0000. **1.1.0 released 11-24 (main = radar branch)**: radar ranges, birthdays & dates, night Off/10/25%, new cards 50%,
   Color tune (defaults = card 2's tuning; details PROJECT-NOTES). Installer step 3 now says "No compatible devices
   found" is normal. 11-24: tester 2 saw "Installing, Done" loop = step 6's example drawing (it repeats); drawing no
   longer shows "Done", drawings captioned "Example drawing"; Connect/Cancel order = Windows; a failed install shows a
   red box + "Copy the technical details"; window drawings use the real title and say "Not a button" (she may have
   clicked the drawn Connect). Starter pictures now keep the pack builder's Fill/Fit (build-install.mjs passed
   none) and GIF/Full color (make-gifs --format=full writes .pjm). New clock + weather (app and first show): theme
   color, Jumbo huge, Match the weather (clock: last weather fetched, else where the screen is: weather.ino
   wxMatchFx; firmware change, not on a card yet). Weather adds alerts + 24 h + 10 days. Item cards (app and pack
   builder): labeled settings, then a bottom bar (Plays/When; Move + Done or Delete).
   Next: tester 2 sends that copy; judge the new card layout on the phone; parked: share pictures.
000. **Soak passed (card 2, 32.3 h, 11-23)**: 0 crashes, flat memory. Card 2's auto-update stays OFF on the beta.
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
