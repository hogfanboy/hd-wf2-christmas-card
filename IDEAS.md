# IDEAS.md (parking lot)

Raw, untested ideas parked so they don't derail the current task. Not settled truth.
At the end of a session: promote what was built and verified to PROJECT-NOTES.md, move a new direction to PROGRESS.md, delete the rest.

## Fix first (found in the 2026-11-20 review)
- **Website promises things that don't exist**: "photos & videos", "checks for new animations once a day", a "Straight from your phone / no WiFi" upload path (dropped: uploads are home-WiFi only), hotspot named `PocketJumbotron` (it's `PocketJumbo-XXXX` + password on the panel), "press the button" (it's hold 2 s). Rewrite `docs/index.html` for the real flow.

## Big features (ambitious, roughly in order)
1. **Content packs from GitHub Pages**: source pictures in a repo folder (e.g. `content/<pack>/`), CI converts them with `make-gifs.mjs` into `docs/packs/<pack>/` + `docs/packs/manifest.json`. Anything there is **public**, so only your own or royalty-free art (no commercial characters). Step 1 (button + build-packs) done in 0.6.3. Step 2: card fetches it daily by itself (HTTPS on the card; tight on RAM, so pause the player while downloading).
2. **Per-family messages**: done in 0.6.3 via `cards` in pack.json. Next: card shows a "new pack for you" hint on the panel.
3. **Playlist + text on the card**: a `playlist.json` with order and seconds per item, plus text items drawn by the card (scrolling message, font, color). Text costs almost no storage. Then a **show builder** on the card's page (LedArt-like): cake GIF 10 s → scrolling "Happy Birthday!" → fireworks, with a live preview.
4. **Live widgets** once the card has the time (NTP over WiFi): "12 days until Christmas" countdown, clock, birthday countdown.
5. **Night schedule**: auto-dim or turn off at set hours (saves the panel, nicer in bedrooms).
6. **Wireless firmware updates from GitHub**: card checks `info.json` and offers "Update available" on its page (today you upload a .bin by hand). Rollback already exists.
7. **Short videos**: convert on the phone/PC into a JPEG-frame file (full color, unlike GIF's 256 colors), decode with JPEGDEC. Trim clips to fit ~4.3 MB.
8. **Built-in effects library**: our own fireworks, snow, confetti, hearts as code (like `docs/panel.js` scenes), free to publish and zero storage if drawn by the card.

## Smaller improvements
- **First-install**: installer backs up Huidu first (esptool-js read-flash → download). Panel-chip check no longer needed (one firmware for both).
- Storage meter on the card page before Send ("2.1 of 4.3 MB").
- Optional PIN for the card page (anyone on the home WiFi can wipe it today).
- **Phone reachability on home WiFi**: Pixel 10a couldn't reach the card while the PC could (guest network / AP isolation?). Test another phone; explain in the help.
- **VBUS check**: does the board back-feed the PC port? Mostly moot with the new unplug-the-panel install steps.
- Dark grays weak at the very dark end at 60 Hz. Color depth A/B (8 vs 10/12-bit). Text-heavy GIF cleanup. People with no home WiFi.
- Tidy: move the one-off probe sketches into `firmware/probes/`.

## Owners with no computer
Phone OTG install dropped (no iPhone path). Options: borrow any Chrome/Edge computer or a Chromebook; mail-in, or mail them a pre-flashed spare WF2 to swap (1 ribbon + 2 power screws). Long shot: Huidu's own WiFi update (LedArt) with a small stage-1 firmware (Huidu app slots are 1 MB, ours is 1.08 MB). Test only on the spare WF2.

## Separate track: Waveshare ESP32-S3-RGB-Matrix
3 boards ordered (N32R16, microSD, ES8311 audio, USB-C; no speaker included, so a 4–8 Ω speaker is needed). Idea: MJPEG video + audio from SD. Confirm it drives a 128x64 panel (2x 64x64 chained). Don't mix assumptions with the WF2 (it has no PSRAM). Distribute only your own family video, not commercial specials.

## Parking Lot
- 
