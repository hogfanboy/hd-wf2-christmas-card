# IDEAS.md (parking lot)

Raw, untested ideas parked so they don't derail the current task. Not settled truth.
At the end of a session: promote what was built and verified to PROJECT-NOTES.md, move a new direction to PROGRESS.md, delete the rest.

## Big features (ambitious, roughly in order)
1. **Content packs**: done (0.6.3; source moved to the private repo in 0.9.6, family codes dropped). Next: the card fetches new packs daily by itself (HTTPS on the card; tight on RAM, pause the player while downloading).
2. **Per-family messages**: done in 0.6.3 via `cards` in pack.json. Next: card shows a "new pack for you" hint on the panel.
3. **Show builder**: done in 0.7.0 (messages, clock, countdown, seconds per item). Effects + weather in 0.8.0. Next: two-line messages, drag to reorder.
4. **News headline scroll** (user wants it, not yet): weather done in 0.8.0 (Open-Meteo over http). Headlines from RSS digested by a GitHub Actions cron into a small text file on Pages (the card would need HTTPS: ~40 KB RAM for TLS, or serve it over http from elsewhere); scroll them as a ticker.
9. **Finance / market item**: follow the market on the screen: a few tickers (S&P 500, Dow, a stock or two picked on the page), price + day change in green/red, maybe a tiny day chart. Same data path as news (Actions cron writes a small JSON on Pages, or the card fetches a free quote API); quotes can be delayed 15 min, say so on the page.
5. **Night schedule**: done in 0.7.0.
6. **Updates from GitHub**: one-tap button on the card page done in 0.7.0. Next: card checks by itself.
7. **Short videos**: the .pjm full-color format exists now; add video input (decode frames from an MP4 in the browser with a <video> element) and trim to fit.
8. **Built-in effects library**: done in 0.8.0 (9 effects). Next: more (balloons, Santa sleigh, aquarium), effects reacting to the weather (snow when it snows).

## Easier setup (no typing 192.168...)
- QR code on the address screen: done (0.7.1, styles in 0.7.3). Pixel camera app ignores it; try a QR to an https forwarding page on the website.
- Home-screen icon: done on the website (`go.html` + manifest, remembers screens). The card page itself is http, so no install there.
- **Improv WiFi in the installer**: after flashing, the install page asks for the home WiFi over USB (Improv Serial, used by ESPHome/WLED). No hotspot step for PC installs.
- Bluetooth: Web Bluetooth works on Android Chrome only (not iPhone). BLE stack costs ~30-50 KB RAM (we have ~75 KB free with WiFi); flash for it is fine (content has its own 4.3 MB partition). Only worth it for setup, then BLE off.

## Private content
- The 3 GIFs once in public `content/` are still in public git history (would need a history rewrite + force push).

## Smaller improvements
- **First-install**: installer backs up Huidu first (esptool-js read-flash → download). Panel-chip check no longer needed (one firmware for both).
- Storage meter on the card page before Send ("2.1 of 4.3 MB").
- Optional PIN for the card page (anyone on the home WiFi can wipe it today).
- Explain the Android "Nearby devices" permission in the website help and on the setup screens.
- **VBUS check**: does the board back-feed the PC port? Mostly moot with the new unplug-the-panel install steps.
- Dark grays weak at the very dark end at 60 Hz. Color depth A/B (8 vs 10/12-bit). Text-heavy GIF cleanup. People with no home WiFi.
- Tidy: move the one-off probe sketches into `firmware/probes/`.

## Owners with no computer
Phone OTG install dropped (no iPhone path). Options: borrow any Chrome/Edge computer or a Chromebook; mail-in, or mail them a pre-flashed spare WF2 to swap (1 ribbon + 2 power screws). Long shot: Huidu's own WiFi update (LedArt) with a small stage-1 firmware (Huidu app slots are 1 MB, ours is 1.08 MB). Test only on the spare WF2.

## Separate track: Waveshare ESP32-S3-RGB-Matrix
3 boards ordered (N32R16, microSD, ES8311 audio, USB-C; no speaker included, so a 4–8 Ω speaker is needed). Idea: MJPEG video + audio from SD. Confirm it drives a 128x64 panel (2x 64x64 chained). Don't mix assumptions with the WF2 (it has no PSRAM). Distribute only your own family video, not commercial specials.

## Parking Lot
- Colorful weather icons: hand-drawn pixel-art sprites (24x24 big + 12x12 forecast, shaded sun with glow, layered
  clouds, colored drops/flakes, lightning), 2-4 animation frames each, stored as RGB565 arrays in flash (~1 KB per
  frame). Draw them in a tool that also renders the page preview from the same data; judge on the panel with an
  "icon test" item that shows them all. Our own art only (no copied icon sets). 
- Research: scan GitHub for projects built on the ESP32-HUB75-MatrixPanel-DMA library, and Huidu's HDPlayer feature list, for things worth adding.
- News: works as designed but adds little (headlines too vague). Explore: a slow bottom-row crawl with longer, more
  detailed lines (like CNN's), under another item; or drop news from this card (may not be a good fit).
