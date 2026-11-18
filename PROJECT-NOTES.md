# Pocket Jumbotron: project notes

The current state, the decisions and the hard-won gotchas. Keep this current. Last updated 2026-11-17.

## What it is
Custom firmware + website for ~15 LED "Christmas cards" already mailed out last year. Each one is a Huidu **HD-WF2 V7.2** controller (ESP32-S3, 8 MB flash, **no PSRAM**) + one **P2.5 HUB75E 128x64, 1/32-scan** panel + a 5V 10A supply (barrel jack → WF2 screw terminals). Stock Huidu firmware only does 3-bit color. Our firmware gives 8-bit color, the user's own pictures, and wireless updates. The name is **Pocket Jumbotron** (not tied to one family's card).

- Repo: https://github.com/hogfanboy/hd-wf2-christmas-card (public). Website: GitHub Pages from `/docs` → https://hogfanboy.github.io/hd-wf2-christmas-card/
- **Never commit** the user's media (`media/`, many are commercial characters) or Huidu firmware dumps (`firmware-backup/`). Both are gitignored.

## Current state (fw 0.4.2, one test card)
- **Player**: plays every GIF in LittleFS `/gifs` in name order. Short GIFs loop for ≥8 s; long ones stop at ~20 s at the end of a loop. **Single-buffered** (redraws only changed pixels), 8-bit color, brightness capped at 55% of full (`MAX_BRIGHTNESS` 140); the user's % setting is saved in NVS.
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

## ✅ RUL6024 panels WORK with the library's DP3246 mode (2026-11-17)
- The RUL6024 is NOT S-PWM after all: it's a plain shift-register driver that needs **LAT high for 3 clocks** to latch (per GitHub discussion #525 on the HUB75 DMA library; Huidu drives these as "ICN2038S"). The library's `cfg.driver = HUB75_I2S_CFG::DP3246` mode does exactly that (LAT on the last 3 pixels of each row). Card 2 then shows the color test correctly. **Most of the user's 15 cards have RUL6024 panels.**
- **One firmware for both panel types (fw 0.6.0):** DP3246 mode also works on the FM6124C panel (card 1: bright, looks very good). The only difference: RUL6024 shows everything 1 column left (DP3246 forces `clkphase = true`), FM6124C doesn't. Firmware draws at x+1 when the **edge fix** setting is on (NVS `edgefix`, default ON since most cards are RUL6024; checkbox on the card's web page, `POST /api/edgefix?v=0|1`). Wrong setting = a stray line at the left (FM card with fix on) or the left column showing at the right edge (RUL card with fix off).
- Refresh rate: `min_refresh_rate` 30 gave the smoothest gradients but whites visibly jitter; 45 still jitters; **keep the default 60** (dark grays a bit weak/raised at the very dark end).
- Card 2 (RUL6024) is much dimmer than card 1 at the same setting, and has fixed-position noise in the lower middle-right of the bottom half (~cols 64-80, rows 56-63) on every screen. Not seen on card 1 → likely card 2's panel/connection. Unresolved.
- Camera shows partial-frame bands at low refresh (rolling shutter, invisible to the eye).

## Panel driver chips differ between cards (found 2026-11-17)
- Card 1 panel: **FM6124C** shift-register drivers → works with ESP32-HUB75-MatrixPanel-DMA.
- **Card 2 panel: RUL6024 = S-PWM driver** (on-chip frame memory + PWM). Listed as NOT supported by the library (README "Unsupported Panels": RUL6024, MBI6024, ICN2053...). Browser install on card 2 worked (log shows the player running), but **the panel stays dark**. Card 2's Huidu firmware was NOT backed up before the install.
- The ICN2053 fork (github.com/LAutour/ESP32-HUB75-MatrixPanel-DMA-ICN2053, 2023) uses the original ESP32's I2S parallel mode, so it **doesn't run on the ESP32-S3** (LCD_CAM) without a port. No public RUL6024 datasheet found.
- **S-PWM probe results** (`firmware/spwm_probe`, slow bit-bang, card 2 MAC e8:06:90:fd:5d:f8): the RUL6024 accepts the ICN2053-style commands (LAT width: 14 pre-activate, 12 enable, 4/6/8/10 write config, 3 vsync), and the whole panel lights. Chain order is chip 0 = left 16 columns ... chip 7 = right; R1/G1/B1 = top half, R2/G2/B2 = bottom half. **But per-pixel data doesn't stick**: only patterns uniform across a whole chip appear (probe 3: one latch per channel after the whole 128-bit chain; probe 2: latch after every 16-bit word gave partial blocks). So the RUL6024's data-latch / SRAM-write sequence differs from the ICN2053/MBI5153 one. Also: red drops out when mixed with green/blue (yellow looks green, white looks cyan), and it's dim (slow bit-banged GCLK).
- Fastest way forward: capture what Huidu firmware sends to a RUL6024 panel with a cheap USB logic analyzer (24 MHz, 8 ch); else keep guessing latch variants.
- Before mailing the installer: find out which of the 15 cards have which panel chip; the install page should warn/check and **back up Huidu first** (esptool-js read-flash → download).

## Gotchas (learned the hard way)
- **First flash of a Huidu card**: the USB port exists only briefly at power-up (Huidu then switches USB to host mode), or for as long as S1 is held. A watcher must be running *before* plugging in. `esptool --before usb-reset` then forces ROM download mode (`tools/backup-watch.ps1`, `tools/flash-watch.ps1`). With our firmware on, USB stays up, so no trick is needed.
- Flashing over PC USB can drop mid-write (the PC port also powers the lit panel). A retry works.
- **Android vs. the card's hotspot**: a WiFi network without internet is "limited"/"sign-in". Only the sign-in pop-up may use it (and it has no file picker), and Chrome is routed over mobile data and times out, even with mobile data off. That's why uploads go over home WiFi.
- The user's Pixel 10a couldn't reach the card on home WiFi either, while the PC could. Not solved yet (guest network / AP isolation / Chrome setting?). The user will test with another phone.
- **Memory**: no PSRAM. Double-buffered DMA costs ~100 KB; with it, home WiFi + playing left only ~20-25 KB free. Now single-buffered (`USE_DOUBLE_BUFFER`): ~79 KB free with WiFi + playing. The player buffers are heap-allocated so the hotspot can free them (`playerFree()`). Turning WiFi off/on again in one session fails on this core, so restart instead.
- **Upload speed** was limited by calling `LittleFS.usedBytes()` (walks the whole FS) per 1.4 KB chunk. Now it's measured once per file: 2.36 MB / 22 files in 41 s (was 151 s). OTA firmware upload: 1 MB in ~12 s.
- The HUB75 library already applies CIE1931 correction, so don't add gamma (the earlier gamma 2.2 made everything too dark).
- GIF playback: apply the *previous* frame's disposal before the next frame, and clear the canvas when a loop restarts. Both fixed visible glitches.
- arduino-cli shares the user's Arduino IDE sketchbook/core (`~/Documents/Arduino/libraries`). Installs affect their other projects.
- Arduino `.ino` files are concatenated. Globals used across files must be declared in the main `.ino` before use. A C-linkage override (`verifyRollbackLater`) must live in a `.cpp` file.

## Next steps (backlog, roughly in priority order)
1. Done: single buffer + upload fix (fw 0.4.2). Wireless firmware update + rollback confirmation verified on the card. User checked: no flicker, looks the same as double-buffered.
2. **First-install path for recipients**: GitHub Actions builds firmware + a default content image; ESP Web Tools flasher page on GitHub Pages; handle the "catch the port at power-up / hold S1" step; photo instructions.
3. **Daily fresh content from GitHub**: manifest of dated packs, downloaded once a day, replaces the content (respect the ~4.3 MB limit).
4. **Short videos**: the user wants them. Plan: convert on the phone/PC into a compact JPEG-frame file (full color, unlike GIF's 256 colors), decoded on the card with JPEGDEC. Trim clips to fit the ~4.3 MB limit.
5. Update the website for the home-WiFi flow (setup steps, the S1 address screen, `pocketjumbo-xxxx.local`).
6. Phone reachability on home WiFi: test another phone; explain guest network / AP isolation in the help.
7. Color depth A/B (8 vs 10/12-bit); text-heavy GIF cleanup; people with no home WiFi.
8. ⚠️ VBUS check: with a USB-A to A cable *and* the card's own supply connected, does it back-feed the PC port? Until checked: unplug the card's supply while on USB.

## Separate track (later): Waveshare ESP32-S3-RGB-Matrix
3 boards ordered (N32R16, microSD, ES8311 audio, USB-C; no speaker included, so a 4–8 Ω speaker is needed). Idea: MJPEG video + audio from SD. Confirm it drives a 128x64 panel (2x 64x64 chained). Don't mix assumptions with the WF2 (it has no PSRAM). Distribute only your own family video, not commercial specials.
