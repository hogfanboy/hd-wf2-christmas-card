# HD-WF2 LED Christmas Card – Working Notes (for Friday session)

## Hardware (what's deployed)
- ~15 cards sent out last Christmas: Huidu **HD-WF2 V7.2** (board dated wk 49 2024) + **P2.5 320x160 HUB75E panel = 128x64 px, 1/32 scan ("2020-32S")**.
- Each recipient also got a **5V 10A supply** (idea: maybe add a 2nd panel in future years).
- Power: barrel jack in 3D-printed red bracket -> WF2 screw terminals -> panel power lead. Ribbon cable to panel HUB75.
- WF2 spec (V7.1 PDF): ESP32-S3, **8 MB flash** (Huidu firmware leaves ~4.5 MB usable), **no PSRAM**, 2x HUB75E, USB-A port (Huidu uses it as USB host for U-disk), S1 button. Huidu firmware = only **8 grayscale levels (3-bit/channel, 512 colors)** -> poor color.
- Original content: ~10 simple GIFs loaded with HD2020 software.

## Goal
Replace Huidu firmware with our own (Arduino IDE) for much better color, and give the 15 recipients an easy, safe update path.

## Key facts / decisions so far
- Library: **ESP32-HUB75-MatrixPanel-DMA** (mrcodetastic) + **AnimatedGIF** (bitbank). WF2 pinout: library issue #433. WLED has a prebuilt `ESP32-S3_HD-WF2.bin`; ESPHome has `huidu-hd-wf2` preset (useful for pin reference/quick test).
- Target color depth: **6-8 bits/channel** (vs Huidu 3). 128x64 fits in internal SRAM.
- Flashing: **USB-A to USB-A cable**, hold the button while plugging in (ROM boot mode – can't be bricked). Unplug the card's own 5V supply while connected to the PC.
- Arduino IDE gotcha: set **Flash Mode = DIO** (known WF2 issue; filesystem goes read-only otherwise).
- GIF storage is limited by flash, not color depth. Resize GIFs to exactly 128x64 (ezgif.com).
- Set a **max brightness (~50-60%)** in firmware – protects the barrel jack (~5A typical rating), esp. if 2 panels later.

## Update-path plan for the 15 recipients
1. **One-time cable flash** via a **GitHub Pages web flasher (ESP Web Tools)** – Chrome/Edge, Connect -> Install. Include photo/video of the hold-button step.
2. After that, **WiFi updates from GitHub**: first boot = WiFiManager hotspot to enter home WiFi; each boot checks a manifest on GitHub for new GIFs and new firmware.
3. **Dual OTA partitions with rollback** so a bad update can't brick a card. Rough 8 MB layout: 2 app slots + ~4 MB LittleFS for GIFs.
4. Tell recipients: HD2020 / LedArt apps stop working after the update.
5. Order ~15 USB-A to USB-A cables.
- Later/stretch: USB-stick (U-disk) playback = USB host mode – hard in Arduino (ESP-IDF USB Host MSC).

## Decisions (2026-11-17)
- **Name: Pocket Jumbotron.** Not limited to one family's holiday card. Hotspot SSID like `PocketJumbotron-4F2A`.
- **Toolchain: PlatformIO** (pioarduino / Arduino core 3.x), not the Arduino IDE. Claude does the coding and testing. GitHub Actions builds firmware releases.
- **Website: GitHub Pages from `/docs`**, aimed at non-technical recipients first. The maker/technical info goes on a separate page. It will later host the ESP Web Tools flasher.
- **No-WiFi path: on-demand card hotspot.** Press S1 and the card starts a password-protected AP with a local web page: upload photos/GIFs/video, playlist, brightness, firmware update file, optional join-home-WiFi. It never needs the internet.
- **No USB uploads** for now (recipients aren't technical). USB drive mode is shelved.
- **Media format:** GIF as-is. Photos/video converted *in the browser on the phone* (crop to 128x64, preview, encode as a sequence of JPEG frames) for full color. Card decodes with JPEGDEC.
- **Storage rule: a new upload/content pack replaces everything** (wipe, then write), so flash can't fill up.
- **Fresh content through the season:** WiFi-connected cards check once a day and download that day's pack from GitHub (a manifest listing packs by date). Offline cards keep whatever was last uploaded.
- ⚠️ **Check VBUS on Friday:** USB-A to A with the card's own 5V supply also connected may back-feed the PC port. Keep "unplug card power while on USB" until verified.

## Findings from first connection (2026-11-17)
- **Backup done**: `firmware-backup/wf2_backup_20261117_163902.bin` (8 MB, sha256 `b7893e09…f45973ec`). It's gitignored: keep copies off this PC.
- Chip: ESP32-S3 (QFN56) rev v0.2, MAC e8:06:90:fe:ee:d0, flash 8 MB (mfr 0x1c), **DIO** mode in bootloader, no flash encryption.
- **How connecting actually works:** S1 is *not* the boot (GPIO0) button. It's read by Huidu firmware (fast LED blink). At power-up the chip's built-in USB-Serial/JTAG enumerates (PC dings), then Huidu firmware switches the USB pins to host mode and the PC loses it. Holding S1 just delays that. Fix: catch the port as soon as it appears and run esptool with `--before usb-reset`, which resets the chip straight into ROM download mode. `tools/backup-watch.ps1` does this. The web flasher (ESP Web Tools) will need the same trick, or a 1-click retry, for the first install. Once our firmware is on, it keeps USB in device mode, so this only matters once.
- Huidu partition table: nvs, otadata, phy_init, **ota_0 1 MB** (WFX_PRO, IDF v4.4, 2023), **ota_1 1 MB** (WF2_2, Jun 2024, active), **app_data 5 MB** (custom type 0x40, media; ~450 KB used).

## First flash: color test works (2026-11-17)
- `firmware/wf2_colortest` flashed with `tools/flash-watch.ps1`. **Pinout confirmed**: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34. The default shift-register driver works on this panel. Orientation, color bars, gradients and rainbow all look right, with 270 KB heap free after the DMA buffers.
- Toolchain switched to **arduino-cli** (no Python needed). ESP32 core 3.3.13, HUB75 DMA lib 3.1.0.
- **Order matters for the first flash:** the watcher script (or the web flasher's Connect) must already be waiting *before* plugging in. The port only exists while Huidu is still starting, or for as long as S1 is held. For recipients: "hold S1 the whole time until the install starts".
- After our firmware is on, the card stays on USB normally, so reflashing needs no S1 trick.

## Friday agenda
1. USB-A to USB-A cable arrives.
2. **Back up Huidu firmware**: `esptool --chip esp32s3 read_flash 0 ALL wf2_backup.bin` (confirm size with `esptool flash_id`).
3. Confirm pinout / board version vs. library issue #433 (optional quick test with WLED WF2 bin).
4. Write basic Arduino GIF player: GIFs from LittleFS, higher color depth, brightness cap, gamma.
5. Then: partition scheme + WiFiManager + GitHub OTA.

## Parallel track (later): Waveshare ESP32-S3-RGB-Matrix
- 3 boards ordered from AliExpress ($75), **no speaker included** – need a small 4-8 ohm speaker.
- N32R16 (32 MB flash, 16 MB PSRAM), microSD, ES8311 audio codec + speaker header, USB-C, 10A rated.
- Open question: confirm it drives the 128x64 panel (Waveshare lists 64x64 max per panel; 128x64 = two 64x64 chained, should be fine). Quick test: WLED Waveshare build.
- Ideas for it: MJPEG video + audio from SD (AVI, convert with ffmpeg), download family Christmas video from GitHub Pages/Releases. Possible own custom board later (ESP32-S3 N16R8, 74HCT245 x2, microSD, MAX98357A, USB-C).
- Rights note: own family video is fine to distribute; don't mail out copies of commercial specials.
