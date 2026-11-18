# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Read [PROJECT-NOTES.md](PROJECT-NOTES.md) first.** It has the current state, decisions, gotchas and backlog. Keep it current.

## Layout
- `firmware/pocket_jumbotron/`: the firmware (Arduino sketch). `pocket_jumbotron.ino` = panel, GIF player, button, main loop. `net.ino` = home WiFi + web server/API, WiFi-setup hotspot, settings (NVS), OTA. `rollback.cpp` = OTA rollback hook. `partitions.csv` = flash layout. `web_index.h` is **generated**; don't edit it.
- `web/index.html` (main page: pictures, brightness, updates) and `web/wifi.html` (hotspot WiFi picker) are served by the card. `tools/build-web.mjs` inlines the GIF libs, gzips them and writes `web_index.h`. Rerun it after editing `web/`, then recompile.
- `docs/`: the GitHub Pages website (recipients) with the panel simulator (`panel.js`).
- `tools/`: Node + PowerShell helpers (below). `firmware/wf2_colortest/` is the original pin/color test.

## Build
```bash
A="/c/Users/hogfa/AppData/Local/Programs/arduino-cli/arduino-cli.exe"   # not on PATH
node tools/build-web.mjs                                                  # after changing web/
cd firmware && "$A" compile -b "esp32:esp32:esp32s3:FlashMode=dio,FlashSize=8M,CDCOnBoot=cdc,USBMode=hwcdc,PSRAM=disabled" --output-dir build/pocket_jumbotron pocket_jumbotron
```
The sketch's own `partitions.csv` is used automatically (ignore the "Maximum is 1310720" in the size report; the real slot is 1.75 MB).

## Flash / talk to the card (run in a terminal tab the user can see)
- Firmware only: `tools/flash-watch.ps1 -LogSec 300 -Images "0x10000=firmware\build\pocket_jumbotron\pocket_jumbotron.ino.bin"`
- Everything + a GIF storage image: `tools/flash-player.ps1` (expects `firmware/build/littlefs.bin`, built with mklittlefs from `firmware/build/fs/gifs/`, `-b 4096 -p 256 -s 0x460000`).
- Serial log only: `tools/serial-log.ps1 -Seconds 600`.
- Over WiFi (no USB): `node tools/upload-to-card.mjs pocketjumbo-eed0.local media/webp` replaces all pictures. The card's API: `GET /api/info`, `POST /api/clear` (503 = busy, retry), `POST /api/upload` (multipart), `POST /api/play`, `POST /api/brightness?v=`, `POST /api/update` (firmware .bin), `POST /api/forget-wifi`.
- esptool v5 (hyphenated commands): `%LOCALAPPDATA%\Programs\esptool\esptool-windows-amd64\esptool.exe`. The GitHub CLI is at `%LOCALAPPDATA%\Programs\gh\bin\gh.exe`.
- A card still on **Huidu** firmware: start the watcher script first, then the user plugs in holding S1.

## Media
`node tools/make-gifs.mjs <outDir> <files-or-folders> [--fit=auto|cover|contain] [--max-frames=150]` converts GIF/WEBP/PNG/JPG to the card's 128x64 GIF format (border trim, edge-color bars, true black, changed-pixel frames). Output goes in gitignored `media/`. Check crops visually by rendering a contact sheet (sharp) and viewing it.

## Testing without the card
`.claude/launch.json` → `fake-card` (`tools/fake-card.mjs`, port 8090) serves the built pages with a fake API (`/wifi` = WiFi page; `/test/<path>` serves files from `media/` for feeding the file input). `site` serves `docs/` on 8080.
