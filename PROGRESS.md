# PROGRESS

## Working state (fw 1.0.0-beta6 on card 1, beta5 on card 2, 2026-11-21)
- **beta6 (review fixes):** other websites can no longer POST to the card (Origin/Host check, 403; tested with curl
  and the real page in a browser on card 1). Failed uploads delete their half file. The web page and button stay
  responsive during GIFs whose frames draw slower than their delay. Tab icon on install.html + pack builder.
- **beta5:** aquarium = fish + bubbles on black (no water/seaweed). Improv WiFi over USB fixed (the page dropped
  all but the first network of the scan burst) + "Set up WiFi only" on install.html for switched cards. Card side
  tested over COM10; the page with a real card (Chrome) not yet. Effects judged good by the user.
- **Crisp fonts:** Jumbo font pixels are fully on or off (no soft edge shading). `SOFT` in `web/pjfont.js` can bring
  soft edges back per size (e.g. the huge font). Page preview uses the same pixels.
- **Weather icons (beta4):** back to the shaded beta2 drawings (gradient sun with turning tapered rays, shaded moon
  with craters, lit clouds) but with hard outer edges: each pixel fully on or off, no bolt glow. The beta3 flat
  icons were too plain. Rain drops/snowflakes stay solid. Preview: `node tools/make-wxicons.mjs <out.png>`.
  Alternative offered: shaded disc + beta3's straight breathing rays (not built in).
- **Shuffle:** page "Play order: In order / 🔀 Shuffle" writes `order<TAB>shuffle` into show.txt; card plays a new
  random order each round, never the same item twice in a row. Tested on the fake card (Send + reload keeps it).
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
- **Pack convert settings:** builder details "Phones convert the pictures as" GIF / Auto / Full color (new packs
  default GIF) and Fill/Fit + GIF/Full color buttons per picture (bold = set on that picture). Saved in pack.json
  (`format`, `pictures`), passed on by build-packs as manifest `format` / `pics`, used by the phone page.
- **Pack builder (local):** `tools/pack-builder.mjs` (launch `pack-builder`, :8095) edits ../pocket-jumbotron-content;
  Publish = commit + push + `gh workflow run`. Test on a copy: launch `pack-builder-test` (firmware/build/content-copy).
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
1. **"Fonts/icons look good" / "change X"** - look at a weather item (with forecast) and a Jumbo text item.
   "Use the straight rays" switches the sun to the alternative.
2. **"Re-add the Christmas pack"** - on the phone page, ★ Get the latest pictures, add Christmas 2026, Send; they
   should now come in as GIF and look like the originals. Try Shuffle at the same time.
3. **Online builder** works (Christmas 2026 published from it). Next there: Holiday starter animations → new cards.
4. **"Try WiFi only"** - card 1 on USB (close any serial-log tab), Chrome: install.html → Set up WiFi only →
   pick the same WiFi + password. Then a full install on card 2 (also tests the new first show). Then 1.0.0.
