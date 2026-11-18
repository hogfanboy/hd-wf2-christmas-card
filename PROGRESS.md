# PROGRESS

## Working state (fw 0.6.2, 2026-11-18)
- One firmware for every card, both panel chips (FM6124C and RUL6024). Online installer is built by CI on push to main.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0.local. Card 2 (RUL6024): 192.168.50.199 / pocketjumbo-5df8.local.
- Uploads go over home WiFi (`node tools/upload-to-card.mjs <host> <folder>`); sending new content replaces everything. WEBP only.
- S1 hotspot is only for entering WiFi credentials.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34.
- S1 button: GPIO17.

## Panel setup (same on every card)
- ESP32-HUB75-MatrixPanel-DMA 3.1.0, `cfg.driver = DP3246` (3-clock latch), default refresh (60 Hz; 30/45 make whites jitter).
- After `begin()`: `GPIO.func_out_sel_cfg[CLK].inv_sel = 0` (undo the library's clock inversion; fixes column shift and half misalignment).
- `panelRegInit()` before `begin()` bit-bangs registers (LAT high for the last N of 128 clocks):
  LAT 12 = REG1, LAT 13 = REG2 (FM6126A values, smoother grays), LAT 11 = all ones (RUL brightness gain),
  LAT 14 = 0x0100 (removes RUL noise in lower middle-right, keeps a good gradient). The FM6124C ignores all of them.
- Registers persist until panel power-off, so test variants per power-up with an NVS counter.

## Key decisions
- Claude codes and tests; the user handles hardware and judges by eye, using test-card probes rather than photos.
- Before flashing over USB, check the MAC (`esptool read-mac`) and use `flash-watch.ps1 -WaitGone` so the right card gets flashed.
- Never commit `media/` or `firmware-backup/` (gitignored). Commit as hogfanboy noreply identity.

## Next
- User checks card 1 on 0.6.2 by eye: edges, half alignment, lower-left noise during the WiFi text.
- Confirm the CI installer build for 0.6.2 succeeded.
- Backlog: daily content packs, short videos, website update for the home-WiFi flow, installer backs up Huidu first,
  dark-gray weakness at 60 Hz, Pixel 10a reachability, VBUS check.
