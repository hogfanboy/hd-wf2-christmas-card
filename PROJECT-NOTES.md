# Pocket Jumbotron: project notes

The current state, the decisions and the hard-won gotchas. Keep this current. Last updated 2026-11-17.

## What it is
Custom firmware + website for ~15 LED "Christmas cards" already mailed out last year. Each one is a Huidu **HD-WF2 V7.2** controller (ESP32-S3, 8 MB flash, **no PSRAM**) + one **P2.5 HUB75E 128x64, 1/32-scan** panel + a 5V 10A supply (barrel jack → WF2 screw terminals). Stock Huidu firmware only does 3-bit color. Our firmware gives 8-bit color, the user's own pictures, and wireless updates. The name is **Pocket Jumbotron** (not tied to one family's card).

- Repo: https://github.com/hogfanboy/hd-wf2-christmas-card (public). Website: GitHub Pages from `/docs` → https://hogfanboy.github.io/hd-wf2-christmas-card/
- **Never commit** the user's media (`media/`, many are commercial characters) or Huidu firmware dumps (`firmware-backup/`). Both are gitignored.

## Current state (fw 0.4.0, one test card)
- **Player**: plays every GIF in LittleFS `/gifs` in name order. Short GIFs loop for ≥8 s; long ones stop at ~20 s at the end of a loop. Double-buffered, 8-bit color, brightness capped at 55% of full (`MAX_BRIGHTNESS` 140); the user's % setting is saved in NVS.
- **Home WiFi** (`net.ino`): the card joins the saved WiFi and serves the full page (`web/index.html`) at its IP **and** at `http://pocketjumbo-XXXX.local` / `http://pocketjumbo-XXXX` (mDNS + DHCP hostname; both confirmed from Windows Chrome) while GIFs keep playing. Page features: add photos/GIFs (converted to 128x64 on the phone/PC), Send (= replace everything), brightness, firmware update upload, forget WiFi.
- **WiFi setup hotspot**: hold S1 for 2 s → AP `PocketJumbo-XXXX` + a per-card 8-digit password shown on the panel → the captive pop-up page (`web/wifi.html`) lists networks → save → restart onto home WiFi. Leaving the hotspot always restarts the card.
- **S1 short press** shows the web address on the panel for 10 s (also shown for 10 s after WiFi connects).
- **OTA rollback**: `rollback.cpp` makes new firmware unconfirmed until `markAppValid()` runs after panel + storage start OK.
- **Test card content**: the 22 WEBPs from `Desktop/Gif Files/WEBP files` (the user wants only these now), converted with `tools/make-gifs.mjs` into `media/webp/` and uploaded over WiFi.
- Website: recipient-friendly landing page with a panel simulator. It still says "coming soon" and hasn't been updated for the home-WiFi flow yet.

## Hardware facts (confirmed on the test card)
- HUB75 pins: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. The default shift driver works.
- **S1 = GPIO 17, active LOW.** S1 is *not* the boot/GPIO0 button.
- Flash is DIO, 8 MB, unencrypted. Test card MAC e8:06:90:fe:ee:d0 → `PocketJumbo-EED0`, at 192.168.50.16 on the user's WiFi.
- Huidu backup: `firmware-backup/wf2_backup_20261117_163902.bin` (sha256 `b7893e09…f45973ec`). Huidu layout was 2x 1 MB app + 5 MB media.
- Our layout (`partitions.csv`): nvs, otadata, app0 1.75 MB, app1 1.75 MB, LittleFS 4.375 MB @0x390000, coredump. The app is ~1.08 MB.

## Decisions
- Toolchain: **arduino-cli** (no Python on this PC), ESP32 core 3.3.13, HUB75-DMA 3.1.0, AnimatedGIF 2.2.0. Node tools in `tools/` (sharp, gifenc, omggif).
- **Pictures over home WiFi only**, not over the card's hotspot (see gotchas). The hotspot is just for entering WiFi details.
- **Send replaces everything** on the card (wipe, then write), so storage can't fill up.
- Stored format is 128x64 GIF. Each frame has 254 quantized colors + exact black + 1 transparent slot, and only pixels that changed are stored. The converter trims plain borders and fills letterbox bars with the image's own edge color when the edge is plain, otherwise black.
- Planned: WiFi cards fetch a fresh content pack from GitHub daily during the season.

## Gotchas (learned the hard way)
- **First flash of a Huidu card**: the USB port exists only briefly at power-up (Huidu then switches USB to host mode), or for as long as S1 is held. A watcher must be running *before* plugging in. `esptool --before usb-reset` then forces ROM download mode (`tools/backup-watch.ps1`, `tools/flash-watch.ps1`). With our firmware on, USB stays up, so no trick is needed.
- Flashing over PC USB can drop mid-write (the PC port also powers the lit panel). A retry works.
- **Android vs. the card's hotspot**: a WiFi network without internet is "limited"/"sign-in". Only the sign-in pop-up may use it (and it has no file picker), and Chrome is routed over mobile data and times out, even with mobile data off. That's why uploads go over home WiFi.
- The user's Pixel 10a couldn't reach the card on home WiFi either, while the PC could. Not solved yet (guest network / AP isolation / Chrome setting?). The user will test with another phone.
- **Memory is the main limit**: no PSRAM. Double-buffered DMA takes ~100 KB. With home WiFi + playing, free heap is only ~20–25 KB, so uploads crawl at ~10 KB/s. The player buffers are heap-allocated so the hotspot can free them (`playerFree()`). Turning WiFi off/on again in one session fails on this core → restart instead.
- The HUB75 library already applies CIE1931 correction, so don't add gamma (the earlier gamma 2.2 made everything too dark).
- GIF playback: apply the *previous* frame's disposal before the next frame, and clear the canvas when a loop restarts. Both fixed visible glitches.
- arduino-cli shares the user's Arduino IDE sketchbook/core (`~/Documents/Arduino/libraries`). Installs affect their other projects.
- Arduino `.ino` files are concatenated. Globals used across files must be declared in the main `.ino` before use. A C-linkage override (`verifyRollbackLater`) must live in a `.cpp` file.

## Next steps (backlog, roughly in priority order)
1. **Free memory → faster uploads**: try single buffering (or 7-bit color) and measure the heap with WiFi on. The user judges any flicker.
2. **First-install path for recipients**: GitHub Actions builds firmware + a default content image; ESP Web Tools flasher page on GitHub Pages; handle the "catch the port at power-up / hold S1" step; photo instructions.
3. **Daily fresh content from GitHub**: manifest of dated packs, downloaded once a day, replaces the content (respect the ~4.3 MB limit).
4. Update the website for the home-WiFi flow (setup steps, the S1 address screen, `pocketjumbo-xxxx.local`).
5. Phone reachability on home WiFi: test another phone; explain guest network / AP isolation in the help.
6. Color depth A/B (8 vs 10/12-bit); text-heavy GIF cleanup; video (JPEG-frame format); people with no home WiFi.
7. ⚠️ VBUS check: with a USB-A to A cable *and* the card's own supply connected, does it back-feed the PC port? Until checked: unplug the card's supply while on USB.

## Separate track (later): Waveshare ESP32-S3-RGB-Matrix
3 boards ordered (N32R16, microSD, ES8311 audio, USB-C; no speaker included, so a 4–8 Ω speaker is needed). Idea: MJPEG video + audio from SD. Confirm it drives a 128x64 panel (2x 64x64 chained). Don't mix assumptions with the WF2 (it has no PSRAM). Distribute only your own family video, not commercial specials.
