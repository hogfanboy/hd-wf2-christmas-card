# Pocket Jumbotron: project notes (spec and settled truth)

What the product does, verified hardware facts, decisions and gotchas. Add only things that are built and verified.
Current work is in PROGRESS.md; untested ideas and the backlog are in IDEAS.md. Last updated 2026-11-20.

## What it is
Custom firmware + website for ~15 LED "Christmas cards" already mailed out last year. Each one is a Huidu **HD-WF2 V7.2** controller (ESP32-S3, 8 MB flash, **no PSRAM**) + one **P2.5 HUB75E 128x64, 1/32-scan** panel + a 5V 10A supply (barrel jack → WF2 screw terminals). Stock Huidu firmware only does 3-bit color. Our firmware gives 8-bit color, the user's own pictures, and wireless updates. The name is **Pocket Jumbotron** (not tied to one family's card).

- Repo: https://github.com/hogfanboy/hd-wf2-christmas-card (public). Website: GitHub Pages from `/docs` → https://hogfanboy.github.io/hd-wf2-christmas-card/
- **Never commit** the user's media (`media/`, many are commercial characters) or Huidu firmware dumps (`firmware-backup/`). Both are gitignored.

## Features (verified on the cards)
- **Show** (fw 0.7.0+, `show.ino`): `/show.txt` lists items (tab-separated): `pic`, `text` (4 fonts, color or rainbow, snow/sparkle background), `clock`, `countdown` (MM-DD + label), each with seconds (0 = auto). Without show.txt every picture in `/gifs` plays in name order. Auto timing: short animations loop for ≥8 s; long ones stop at ~20 s at the end of a loop.
- **Pictures**: `.gif` (AnimatedGIF, ≤256 colors/frame) or `.pjm` full color = `PJM1` + per frame [u32 len, u16 delay ms, u16 0, baseline JPEG], decoded with JPEGDEC 1.8.5 (RGB8888). The card page converts any input (JPG/PNG/GIF/WEBP, animated WEBP via `ImageDecoder`) and picks GIF or full color per picture (>400 colors → full color).
- **Time**: NTP after WiFi joins; POSIX TZ string worked out by the phone page from its own DST changes. Night mode: hours + brightness level (NVS).
- **Player**: **Single-buffered** (redraws only changed pixels), 8-bit color, brightness capped at 55% of full (`MAX_BRIGHTNESS` 140); the user's % setting is saved in NVS.
- **Home WiFi** (`net.ino`): the card joins the saved WiFi and serves the full page (`web/index.html`) at its IP **and** at `http://pocketjumbo-XXXX.local` / `http://pocketjumbo-XXXX` (mDNS + DHCP hostname) while GIFs keep playing. Page: build the show, load the current show back (`GET /api/show`, `/api/file`), Send (= replace everything), packs, brightness, clock/night settings, one-tap update from `info.json` on the website, firmware file upload, forget WiFi.
- **Packs**: `content/` (public) → `tools/build-packs.mjs` → `docs/packs/`; private repo hogfanboy/pocket-jumbotron-content → `tools/build-vault.mjs` → `docs/vault/` (encrypted, unlocked by a family code saved on the card). `cards` in pack.json limits a pack to named screens. CI runs on push and hourly.
- **WiFi setup hotspot**: hold S1 for 2 s → AP `PocketJumbo-XXXX` + a per-card 8-digit password shown on the panel → the captive pop-up page (`web/wifi.html`) lists networks → save → restart onto home WiFi. Leaving the hotspot always restarts the card.
- **S1 short press** shows the address screen for 10 s (also 10 s after WiFi connects): a QR code of `http://<ip>` + the IP. Google Lens reads it; see PROGRESS.md for the Pixel camera app issue.
- **OTA update + rollback**: `rollback.cpp` makes new firmware unconfirmed until `markAppValid()` runs after panel + storage start OK. 1 MB OTA takes ~12 s.
- **Online installer**: CI builds the firmware and `docs/install.html` (browser USB flasher) on every push to main.

## Hardware facts (confirmed)
- HUB75 pins: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34.
- **S1 = GPIO 17, active LOW.** S1 is *not* the boot/GPIO0 button.
- Flash is DIO, 8 MB, unencrypted.
- Card 1: MAC e8:06:90:fe:ee:d0 → `PocketJumbo-EED0`, FM6124C panel. Card 2: MAC e8:06:90:fd:5d:f8 → `PocketJumbo-5DF8`, RUL6024 panel. Spare WF2 (no panel): MAC e8:06:90:fd:ad:04, still on Huidu.
- **Most of the user's 15 cards have RUL6024 panels.** Panel chips differ between cards; the firmware handles both the same way.
- Huidu backups: card 1 `firmware-backup/wf2_backup_20261117_163902.bin` (sha256 `b7893e09…f45973ec`), spare `wf2_backup_20261118_002618.bin` (same Huidu apps, WF2_2 Jun 2024). Card 2's Huidu firmware was never backed up; card 1's backup on card 2 boots but stays blank (wrong screen settings, which LedArt can't change).
- Huidu layout was 2x 1 MB app + 5 MB media. Ours (`partitions.csv`): nvs, otadata, app0 1.75 MB, app1 1.75 MB, LittleFS 4.375 MB @0x390000, coredump. The app is ~1.2 MB (fw 0.7.x).

## Panel driver setup (fw 0.6.2, same on every card)
- The RUL6024 is a plain shift-register driver (not S-PWM) that needs **LAT high for 3 clocks** to latch (Huidu calls it "ICN2038S"; HUB75 DMA library discussion #525). The library's `cfg.driver = HUB75_I2S_CFG::DP3246` does that. The FM6124C works with the same setting.
- After `begin()`: set CLK back to the original edge with `GPIO.func_out_sel_cfg[clk].inv_sel = 0`. DP3246 init forces `clkphase = true`, which shifted the RUL6024 image 1 column left and misaligned the FM6124C halves.
- `panelRegInit()` writes config registers by bit-banging (pins borrowed from the LCD peripheral via `GPIO.func_out_sel_cfg`, then restored), LAT high for the last N of 128 clocks:
  - LAT 12 = REG1, LAT 13 = REG2 with the library's FM6126A values (smoother RUL6024 grays).
  - LAT 11 = all ones (RUL6024 brightness; it was much dimmer before).
  - LAT 14 = 0x0100 (removes RUL6024 noise in the lower middle-right with a good gradient; 0x0F00/0xFFFF also remove it but roughen the gradient; 0x0200/0x0400/0x0800 make it worse).
  - The FM6124C ignores all of them.
- Registers survive an ESP restart, so compare register variants only after a panel power-cycle (one variant per power-up).
- Refresh: keep the default `min_refresh_rate` 60. 30 gives smoother gradients but whites jitter; 45 still jitters. Dark grays are a bit weak at the very dark end.
- Probe sketches for this work: `firmware/clk_edge_test`, `fm_reg_variants`, `rul_bright_probe`, `rul_cmd_sweep`, `rul_reg_variants`, `rul_reg14_bits`.

## Decisions
- Toolchain: **arduino-cli** (no Python on this PC), ESP32 core 3.3.13, HUB75-DMA 3.1.0, AnimatedGIF 2.2.0. Node tools in `tools/` (sharp, gifenc, omggif).
- **One firmware and one panel setup for every card**: no per-chip detection.
- **The card stores GIF or JPEG frames, never WEBP**: no small WEBP decoder for the ESP32 and not enough RAM. WEBP is an input format, converted on the phone/PC.
- **Private content is encrypted, not hidden**: Pages is always public, so private packs are AES-256-CTR + HMAC-SHA256 with keys from PBKDF2 (20000 rounds) of the family code; the card page does it in plain JS (aes-js, js-sha256) because it is http and browsers disable WebCrypto there. Family codes live only in the private repo.
- **Pictures over home WiFi only**, not over the card's hotspot (see gotchas). The hotspot is just for entering WiFi details.
- **Send replaces everything** on the card (wipe, then write), so storage can't fill up.
- Stored format is 128x64 GIF. Each frame has 254 quantized colors + exact black + 1 transparent slot, and only pixels that changed are stored. The converter trims plain borders and fills letterbox bars with the image's own edge color when the edge is plain, otherwise black.

## Gotchas (learned the hard way)
- **First flash of a Huidu card**: the USB port exists only briefly at power-up (Huidu then switches USB to host mode), or for as long as S1 is held. A watcher must be running *before* plugging in. `esptool --before usb-reset` then forces ROM download mode (`tools/backup-watch.ps1`, `tools/flash-watch.ps1`). With our firmware on, USB stays up, so no trick is needed.
- Flashing over PC USB can drop mid-write (the PC port also powers the lit panel). A retry works.
- ⚠️ VBUS back-feed not checked yet: until it is, unplug the card's own supply while it's on a PC's USB port.
- **Android vs. the card's hotspot**: a WiFi network without internet is "limited"/"sign-in". Only the sign-in pop-up may use it (and it has no file picker), and Chrome is routed over mobile data and times out, even with mobile data off. That's why uploads go over home WiFi.
- **Memory**: no PSRAM. Double-buffered DMA costs ~100 KB; with it, home WiFi + playing left only ~20-25 KB free. Now single-buffered (`USE_DOUBLE_BUFFER`): ~79 KB free with WiFi + playing. The player buffers are heap-allocated so the hotspot can free them (`playerFree()`). Turning WiFi off/on again in one session fails on this core, so restart instead.
- **Upload speed**: don't call `LittleFS.usedBytes()` (walks the whole FS) per chunk; measure once per file. 2.36 MB / 22 files takes 41 s.
- **Android 16+ Chrome needs the "Nearby devices" (local network) permission** to open the card page; without it Chrome says "site can't be reached" and offers a pop-up that opens Chrome's App info. Verified on the Pixel 10a.
- The card page is plain http: no WebCrypto, no geolocation, no camera APIs there. Fetching https files from GitHub Pages works (CORS allowed).
- The Arduino prototype generator puts function prototypes before other files' includes: don't use library types (e.g. `esp_qrcode_handle_t`) in function signatures of non-main .ino files.
- The HUB75 library already applies CIE1931 correction, so don't add gamma (gamma 2.2 made everything too dark).
- GIF playback: apply the *previous* frame's disposal before the next frame, and clear the canvas when a loop restarts.
- Cameras show partial-frame bands at low refresh (rolling shutter, invisible to the eye).
- arduino-cli shares the user's Arduino IDE sketchbook/core (`~/Documents/Arduino/libraries`). Installs affect their other projects.
- Arduino `.ino` files are concatenated. Globals used across files must be declared in the main `.ino` before use. A C-linkage override (`verifyRollbackLater`) must live in a `.cpp` file.
