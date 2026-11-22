# PROGRESS

## Working state (fw 1.0.0-beta14 on card 1 via WiFi, card 2 radar probe v2 / beta10 on restart, 2026-11-22)
- **beta14:** one "Hourly forecast" checkbox with a 12 hours / 24 hours choice (`hourly` / `chart`) and one "Daily
  forecast" checkbox with 5 days / 10 days (`forecast` / `days10`). 10 days = two 5-day screens, each for the item's
  seconds, with a page bar on the top row (left half lit = days 1-5, right half = days 6-10). 14 days removed; old
  `days16` plays as 10 days. Card 1's show: alerts rainsoon hourly days10 details air sun.
- **beta11-13:** weather "Screens" checklist; line under the temperature: sky, what matters, FEELS (PROJECT-NOTES).
- **beta10:** alerts (weather.gov). **beta7:** Christmas lights. **beta9:** QR big/small alternating. **beta8:** card remembers the weather town (NVS `wxhome`), page redesign.
- **Fonts/icons:** crisp Jumbo font and shaded weather icons with hard edges (details in PROJECT-NOTES).
- **Shuffle:** page Play order In order / Shuffle (`order<TAB>shuffle` in show.txt), never the same item twice.
- **Border trim (beta4 page):** every picture's plain-color border (all 4 corners the same color) is cut off before
  scaling, so small art in a big square fills the screen (01-35-46 "Merry Christmas": 464x227 art in 512x512, now
  full width). Near-black -> black now for GIF too, not only full color. The "bad full color" picture was this.
- **Pack builder hosted:** https://hogfanboy.github.io/hd-wf2-christmas-card/builder/ (docs/builder/, built by
  build-web.mjs from tools/pack-builder.html). Sign in with a fine-grained GitHub token (Contents + Actions write on
  both repos), kept in the browser. Changes wait in the page; Publish = one commit via the GitHub API (noreply
  author) + workflow dispatch. Publish sends the complete file list (fixes 422 GitRPC::BadObjectState, which came
  from deleting paths already gone because the repo changed after the page loaded). Fake-API tested; real use next.
- **New cards' first show is editable in the pack builder (2026-11-21):** the holiday greetings are now a real
  pack, `Holiday Starter/` in the content repo (was generated in CI, so the builder never saw it). Pack setting
  "On new cards" (pack.json `install`), per picture On/Off + dates ("11-15 to 01-06"), and a "What a new card
  gets" list with size vs 4.1 MB. build-install.mjs reads it (`--first-show-only` to check). Tested on a copy of
  the content repo: builder writes the right pack.json, the build makes the same first show as before.
- **Pack convert settings:** GIF / Auto / Full color per pack and per picture in the builder (pack.json `format`, `pictures`).
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (PC USB COM10). Card 2 (RUL6024): 192.168.50.199 /
  pocketjumbo-5df8. Both reachable, heap ~70 KB playing.
- Everything else (news removed, app, schedule, stocks, night mode, packs, effects): PROJECT-NOTES.md.

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
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.

## Next (the user can answer with one of these commands)
0000. **"Build the radar screen"** - radar probe v2 approved by eye on card 2 (Seattle rain loop over a dim map,
   home cross + town, 25 MI ring, radar time top-right). Probe + notes: branch `claude/inspiring-perlman-7d2eaa`,
   `firmware/probes/radar_probe/` (NOTES.md has URLs, memory numbers, makebase.py). Make it a weather screen (`radar`
   checkbox) that only plays when rain/snow is on the way (same test as rainSoon). The page builds the town's map
   once (OSM tiles in the browser) and uploads it (~8 KB) when the town is saved; frames in flash, not RAM.
   Data: RainViewer over plain HTTP (zoom 7). Needs PNGdec 1.1.6 in CI libs; its 45 KB block fits only after
   playerFree(), so allocate it first and skip the round if it fails. Details in NOTES.md (merge only the notes).
000. **"Weather screens look good" / "change X"** - card 1: now, 12 hours, 10 days (watch the top-row page bar:
   left half = days 1-5, right half = 6-10), details, air, sun & moon. Then flash card 2 (restart it first: it runs the radar probe until then).
00. **"QR works close and far" / "change X"** - press S1 on either card; try the phone at 1.5 ft, 4 ft and 7 ft.
0. **"Lights look good" / "change X"** - run `tools/effects-probe.ps1` (bulb size 2x2, spacing 8, pattern speed).
1. **"Fonts/icons look good" / "change X"** - weather + forecast and a Jumbo text item ("use the straight rays").
2. **"Re-add the Christmas pack"** - on the phone page, ★ Get the latest pictures, add Christmas 2026, Send; they
   should now come in as GIF and look like the originals. Try Shuffle at the same time.
3. **Online builder** works (Christmas 2026 published from it). Next there: Holiday starter animations → new cards.
4. **"Try WiFi only"** - card 1 on USB (close any serial-log tab), Chrome: install.html → Set up WiFi only →
   pick the same WiFi + password. Then a full install on card 2 (also tests the new first show). Then 1.0.0.
