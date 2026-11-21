# PROGRESS

## Working state (fw 1.0.0-beta1 built + pushed; card 1 on it via WiFi, card 2 gets it by self-update at 3:xx)
- **1.0.0-beta1 (card 1, heap ~79 KB, one full loop of every new effect with no resets; NOT judged by eye yet):**
  - **New effects:** Santa's sleigh (2 reindeer, red-nosed lead, glitter trail; full screen at mid height under a
    twinkling sky, behind text along the top), Balloons, Aquarium (fish both ways + bubbles; full screen adds
    seaweed + blue water on a black background), Rain, and "Match the weather" (weather items only: snow/rain when
    the weather is, else none). Sprites are letter rows in `effects.ino`, same rows in the page's `SPR`.
    Probe: `tools/effects-probe.ps1` (plays them all ~77 s, then puts the card's own show back).
  - **Videos on the page:** "+ Pictures or videos" takes MP4/MOV/WEBM; Start at / Length per clip (up to 30 s,
    12 fps, ≤150 frames), then GIF or full color like pictures. Tested in the browser (fake card, recorded WEBM:
    3 s -> 35 frames, 47 KB, <1 s; trim + Send OK). Not tried with a phone video yet.
  - **Theme picker:** chosen swatch is bigger with a white ring, glow and check mark; the others are dimmed;
    "Selected: <name>" in the theme's color under the row.
  - **Versions:** `1.0.0-betaN` < `1.0.0` on both card (`verNum`) and page (`verKey`), so betas update to the
    release. Old 0.9.x cards see 1.0.0-beta1 as newer (checked the math).
- **Decided (user, 2026-11-21):** forecast stays the plain layout for now (no shaded/gray options; user likes high
  contrast and will think about it; `fcStyle`/`/api/fcprobe` still in the code). Themes liked. No going back to
  Huidu. No PIN (15-20 family members; anyone on the WiFi can change the show but not hurt the card).
- 0.9.9 items (themes, QR redesign, readability colors, tz=auto) are on both cards; details in git history.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (PC USB COM10). Card 2 (RUL6024): 192.168.50.199 /
  pocketjumbo-5df8 (didn't answer at 01:00 on 2026-11-21; maybe unplugged).
- Everything else (app on the website, schedule, news/stocks, night mode, packs): PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups, no https->card.
- Fast-scan screens: draw with drawPixelRGB888 and 0/255 colors. Soft-edged text can't show in fast scan.
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP (videos too).
- Pictures live in the private repo; the website is public, so they're unlisted, not secret.
- Build with `upload.maximum_size=1835008` (app 1.54 MB of 1.75). Structs used in function signatures go in the
  main .ino; no default arguments in other .ino files (arduino prototypes).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.

## Next (the user can answer with one of these commands)
1. **"Effects look good" / "change X"** - run `tools/effects-probe.ps1`, judge sleigh/balloons/aquarium/rain by eye
   (size, speed, colors). Also look at the theme picker on the phone page.
2. **"Try a phone video"** - add a short phone clip on the page, Send, judge it on the panel.
3. **"Check card 2"** - confirm it self-updated to 1.0.0-beta1 overnight (`/api/info`), or update it over WiFi.
4. **"Test the installer on card 2"** - USB, install.html end to end incl. the Improv WiFi step.
5. Then 1.0.0 release: promote what's judged OK, ideas: Christmas lights border effect, bigger sleigh scene.
