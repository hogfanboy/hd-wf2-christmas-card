# PROGRESS

## Working state (fw 1.0.0-beta4 on both cards via WiFi, 2026-11-21; NOT judged by eye yet)
- **Crisp fonts:** Jumbo font pixels are fully on or off (no soft edge shading). `SOFT` in `web/pjfont.js` can bring
  soft edges back per size (e.g. the huge font). Page preview uses the same pixels.
- **Weather icons (beta4):** back to the shaded beta2 drawings (gradient sun with turning tapered rays, shaded moon
  with craters, lit clouds) but with hard outer edges: each pixel fully on or off, no bolt glow. The beta3 flat
  icons were too plain. Rain drops/snowflakes stay solid. Preview: `node tools/make-wxicons.mjs <out.png>`.
  Alternative offered: shaded disc + beta3's straight breathing rays (not built in).
- **Shuffle:** page "Play order: In order / 🔀 Shuffle" writes `order<TAB>shuffle` into show.txt; card plays a new
  random order each round, never the same item twice in a row. Tested on the fake card (Send + reload keeps it).
- **News removed** (page button, card player, CI feed). Old shows with a news line just skip it. Stocks stay.
- **Full-color fix:** the Christmas2026 pictures were picked as "full color" (1000+ colors from smoothing) and sent
  as browser JPEGs that halve color resolution (reds/greens smeared). Now: auto picks GIF when a 254-color palette
  matches (all 17 Christmas pictures -> GIF, error ~1), full color uses a 4:4:4 JPEG (jpeg-js, q92) with near-black
  forced to black, and the card floors near-black too. Page GIFs store only changed pixels (smaller). Preview shows
  the encoded result. Conversion code moved to `web/convert.js`.
- **Border trim (beta4 page):** every picture's plain-color border (all 4 corners the same color) is cut off before
  scaling, so small art in a big square fills the screen (01-35-46 "Merry Christmas": 464x227 art in 512x512, now
  full width). Near-black -> black now for GIF too, not only full color. The "bad full color" picture was this.
- **Pack builder hosted:** https://hogfanboy.github.io/hd-wf2-christmas-card/builder/ (docs/builder/, built by
  build-web.mjs from tools/pack-builder.html). Sign in with a fine-grained GitHub token (Contents + Actions write on
  both repos), kept in the browser. Changes wait in the page; Publish = one commit via the GitHub API (noreply
  author) + workflow dispatch. Tested against a fake GitHub API only; first real use still to do.
- **Pack builder (local):** `tools/pack-builder.mjs` (launch config `pack-builder`, http://localhost:8095). Edits
  ../pocket-jumbotron-content: new pack, details as real JSON, drop pictures, drag to reorder, delete, previews as a
  phone converts them, Publish = commit + push + `gh workflow run` (site rebuilds in ~3 min). Tested on a scratch
  copy only (not yet published for real).
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0 (PC USB COM10). Card 2 (RUL6024): 192.168.50.199 /
  pocketjumbo-5df8. Both reachable, heap ~70 KB playing.
- Everything else (app on the website, schedule, stocks, night mode, packs, effects): PROJECT-NOTES.md.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. S1: GPIO17.

## Key decisions
- Card = display engine; the website = the app (loaded by the card's starter page). No bridge/popups, no https->card.
- LEDs want crisp, high-contrast pixels with black around them: no soft edges, glows or shaded rims (fonts, icons).
- Pictures: GIF unless GIF visibly loses detail; full color only as 4:4:4 JPEG (never the browser's JPEG).
- Fast-scan screens: draw with drawPixelRGB888 and 0/255 colors.
- One firmware and one panel setup for every card. Card stores GIF/JPEG frames, never WEBP (videos too).
- Pictures live in the private repo; the website is public, so they're unlisted, not secret.
- Build with `upload.maximum_size=1835008` (app 1.53 MB of 1.75). Structs used in function signatures go in the
  main .ino; no default arguments in other .ino files (arduino prototypes).
- Test uploads with curl: `-H "Content-Type: text/plain"` for /api/show, then POST /api/play.

## Next (the user can answer with one of these commands)
1. **"Fonts/icons look good" / "change X"** - look at a weather item (with forecast) and a Jumbo text item.
   "Use the straight rays" switches the sun to the alternative.
2. **"Re-add the Christmas pack"** - on the phone page, ★ Get the latest pictures, add Christmas 2026, Send; they
   should now come in as GIF and look like the originals. Try Shuffle at the same time.
3. **"Try the online pack builder"** - make the token, sign in at /builder/, edit a pack, Publish.
4. Still open from beta1/2: judge sleigh/balloons/aquarium/rain (`tools/effects-probe.ps1`), phone video,
   installer test on card 2. Then 1.0.0 release.
