# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Pocket Jumbotron: replacement firmware for Huidu HD-WF2 LED cards (ESP32-S3, 8 MB flash, no PSRAM, 128x64 HUB75, 1/32 scan).
This folder is the clone of github.com/hogfanboy/hd-wf2-christmas-card (public). Pushing to `main` makes GitHub Actions
build the firmware and publish `docs/` (website + online installer) to GitHub Pages.

**Read [PROGRESS.md](PROGRESS.md) first.** [PROJECT-NOTES.md](PROJECT-NOTES.md) is the spec and settled truth (features, hardware, panel setup, decisions, gotchas); read only the part you need.

## Docs (four files, one job each)
- `CLAUDE.md`: permanent rules and commands only. No brainstorming.
- `PROGRESS.md`: current target, what just broke, next step. If an idea changes direction, replace the old target.
- `PROJECT-NOTES.md`: the spec (acts as the FSD) and settled truth. Add only things that are built and verified; no history or failed attempts.
- `IDEAS.md`: backlog and parking lot for untested ideas. Don't read it unless asked or when picking the next task. When the user says "park this", append one line under "Backlog / Parking Lot" and go back to the current task without discussing it. At session end, if asked, promote verified items to PROJECT-NOTES.md and remove them.

## Rules
- Don't read `firmware/build/`, `tools/node_modules/`, `~/.arduino15`, Arduino `libraries/`, `firmware-backup/` or `media/` unless asked.
- Never commit `media/` (user's pictures) or `firmware-backup/` (Huidu dumps). Commit as hogfanboy (noreply), never the Gmail.
- Before a USB flash, confirm which card is plugged in (`esptool read-mac`); use `flash-watch.ps1 -WaitGone` when switching cards.
- The user judges the panel by eye: give them test-card probes, not photos.
- After any driver, feature or bug fix, update PROGRESS.md (under 60 lines): working state and verified pins, active bug or next task, key decisions (no failed attempts). Then say: "Task complete and PROGRESS.md updated. Ready for /clear."

## Layout
- `firmware/pocket_jumbotron/`: `pocket_jumbotron.ino` (panel setup + register writes, GIF player, button, shared globals), `show.ino` (show list, text/clock/countdown, full-color .pjm player, night mode), `net.ino` (WiFi, web API, hotspot, NVS, OTA, QR), `rollback.cpp`, `partitions.csv`. `web_index.h` is generated.
- `web/` pages served by the card (`index.html`, `wifi.html`, `vault.js`); `node tools/build-web.mjs` regenerates `web_index.h` (run after editing `web/`, then recompile).
- `content/` public packs (`tools/build-packs.mjs`). Private pictures + family codes: repo hogfanboy/pocket-jumbotron-content, cloned at `../pocket-jumbotron-content`, encrypted by `tools/build-vault.mjs` in CI. Never put a family code in this repo.
- `docs/` website + `install.html` (browser USB installer); `docs/firmware/` is built by `node tools/build-install.mjs` (CI does this).
- Other `firmware/*` folders are one-off panel probes/tests.

## Build (Arduino CLI, same toolchain as CI: esp32 core 3.3.13, HUB75 DMA 3.1.0)
```bash
A="/c/Users/hogfa/AppData/Local/Programs/arduino-cli/arduino-cli.exe"   # not on PATH
cd firmware && "$A" compile -b "esp32:esp32:esp32s3:FlashMode=dio,FlashSize=8M,CDCOnBoot=cdc,USBMode=hwcdc,PSRAM=disabled" --build-property upload.maximum_size=1835008 --output-dir build/pocket_jumbotron pocket_jumbotron
```
`upload.maximum_size` = our 1.75 MB app slot; without it arduino-cli refuses apps over 1.25 MB and leaves the old .bin in place.

## Flash / talk to a card (run in a terminal tab the user can see, via `powershell -ExecutionPolicy Bypass -File ...`)
- Firmware: `tools/flash-watch.ps1 -LogSec 300 -Images "0x10000=firmware\build\pocket_jumbotron\pocket_jumbotron.ino.bin"`. A card still on Huidu: start the script, then the user plugs in holding S1.
- Serial log: `tools/serial-log.ps1 -Seconds 600`.
- Over WiFi: firmware `curl -F "firmware=@firmware/build/pocket_jumbotron/pocket_jumbotron.ino.bin" http://<ip>/api/update`. API: `GET /api/info|show|file?name=`, `POST /api/clear|upload|show|play|brightness?v=|settings|update|forget-wifi`.
- esptool v5: `%LOCALAPPDATA%\Programs\esptool\esptool-windows-amd64\esptool.exe`. gh: `%LOCALAPPDATA%\Programs\gh\bin\gh.exe`.

## Media and testing without a card
- `node tools/make-gifs.mjs <outDir> <inputs> [--fit=auto|cover|contain]` converts to the card's 128x64 format (output in `media/`).
- `.claude/launch.json`: `fake-card` (port 8090, fake API) and `site` (`docs/` on 8080).
