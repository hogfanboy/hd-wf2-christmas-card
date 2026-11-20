# IDEAS.md (parking lot)

Raw, untested ideas parked so they don't derail the current task. Not settled truth.
At the end of a session: promote what was built and verified to PROJECT-NOTES.md, move a new direction to PROGRESS.md, delete the rest.

## Fix first (found in the 2026-11-20 review)
- **Website promises things that don't exist**: "photos & videos", "checks for new animations once a day", a "Straight from your phone / no WiFi" upload path (dropped: uploads are home-WiFi only), hotspot named `PocketJumbotron` (it's `PocketJumbo-XXXX` + password on the panel), "press the button" (it's hold 2 s). Rewrite `docs/index.html` for the real flow.

## Big features (ambitious, roughly in order)
1. **Content packs**: public + encrypted family packs done (0.6.3-0.7.2). Next: the card fetches new packs daily by itself (HTTPS on the card; tight on RAM, pause the player while downloading).
2. **Per-family messages**: done in 0.6.3 via `cards` in pack.json. Next: card shows a "new pack for you" hint on the panel.
3. **Show builder**: done in 0.7.0 (messages, clock, countdown, seconds per item). Next: more effects (fireworks, confetti), two-line messages, drag to reorder.
4. **News + weather items** (year-round): weather from Open-Meteo (free, no key; city picked on the page), headlines from RSS digested by a GitHub Actions cron into a small text file on Pages. Card does a small HTTPS GET between items (~40 KB RAM for TLS).
5. **Night schedule**: done in 0.7.0.
6. **Updates from GitHub**: one-tap button on the card page done in 0.7.0. Next: card checks by itself.
7. **Short videos**: the .pjm full-color format exists now; add video input (decode frames from an MP4 in the browser with a <video> element) and trim to fit.
8. **Built-in effects library**: our own fireworks, snow, confetti, hearts as code (like `docs/panel.js` scenes), free to publish and zero storage if drawn by the card.

## Easier setup (no typing 192.168...)
- QR code on the address screen: done (0.7.1, styles in 0.7.3). Pixel camera app ignores it; try a QR to an https forwarding page on the website.
- Add-to-home-screen icon for the card page (manifest + icon), so it opens like an app.
- **Improv WiFi in the installer**: after flashing, the install page asks for the home WiFi over USB (Improv Serial, used by ESPHome/WLED). No hotspot step for PC installs.
- Bluetooth: Web Bluetooth works on Android Chrome only (not iPhone). BLE stack costs ~30-50 KB RAM (we have ~75 KB free with WiFi); flash for it is fine (content has its own 4.3 MB partition). Only worth it for setup, then BLE off.

## Private content
- Encrypted family packs done (0.7.2). Later: show "new family pictures" on the panel. The 3 GIFs once in public `content/` are still in public git history.

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
- 
