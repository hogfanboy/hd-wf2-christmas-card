# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

No code yet. The only source of truth is [WF2-friday-plan.md](WF2-friday-plan.md) (working notes: hardware, decisions, update-path plan, agenda). Read it before starting work and keep it current when decisions change.

## What this project is

Custom Arduino firmware to replace the stock Huidu firmware on ~15 already-deployed LED Christmas cards, plus a safe self-service update path for the people who have them. You can't reach the deployed cards to fix them, so any firmware or update mechanism must not be able to brick a card.

## Hardware constraints that shape the code

- Board: Huidu **HD-WF2 V7.2** (ESP32-S3, 8 MB flash, **no PSRAM**). Panel: one HUB75E **128x64, 1/32 scan**. Frame buffers must fit in internal SRAM.
- Pinout reference: ESP32-HUB75-MatrixPanel-DMA issue #433, WLED `ESP32-S3_HD-WF2.bin`, ESPHome `huidu-hd-wf2` preset. Check the pinout against the V7.2 board before relying on it.
- Arduino IDE: **Flash Mode must be DIO**. Otherwise the filesystem ends up read-only (known WF2 issue).
- Cap brightness at ~50–60% in firmware. It protects the barrel jack (~5A), especially if a second panel is added later.
- Libraries: **ESP32-HUB75-MatrixPanel-DMA** (mrcodetastic) for the display, **AnimatedGIF** (bitbank) for playback. Target 6–8 bits per color channel, with gamma correction.
- GIFs live in LittleFS and must be exactly 128x64. Flash space limits storage, not color depth.

## Planned architecture

1. **GIF player**: reads GIFs from LittleFS, renders through the DMA HUB75 driver, applies the brightness cap and gamma.
2. **Partition scheme** (8 MB): two OTA app slots with rollback + ~4 MB LittleFS.
3. **First-time provisioning**: WiFiManager captive hotspot.
4. **Updates**: on each boot, fetch a manifest from GitHub and download any new GIFs or firmware. Use the OTA rollback so a bad image reverts on its own.
5. **One-time cable install for recipients**: ESP Web Tools page on GitHub Pages (Chrome/Edge). Flash over USB-A to USB-A while holding S1 to enter ROM boot mode, with the card's own 5V supply unplugged.

## Useful commands

```bash
esptool --chip esp32s3 flash_id
esptool --chip esp32s3 read_flash 0 ALL wf2_backup.bin
```

Before experimenting on a card, back up its stock Huidu firmware with the commands above. Keep the backup.

## Separate track

A Waveshare ESP32-S3-RGB-Matrix board (32 MB flash, 16 MB PSRAM, microSD, ES8311 audio) is planned for later video+audio experiments. It is separate from the WF2 firmware, so don't mix assumptions between the two boards (the WF2 has no PSRAM).
