# PROGRESS

## Working state (fw 0.7.3, 2026-11-20; card 1 updated over WiFi, card 2 still 0.6.2)
- One firmware for every card, both panel chips (FM6124C and RUL6024). CI builds the installer + packs on push to main.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0. Card 2 (RUL6024): 192.168.50.199 / pocketjumbo-5df8.
- **Show** (`show.ino`, `/show.txt`, tab-separated lines): pictures, messages (4 fonts, rainbow, snow/sparkle),
  clock, countdown; seconds per item (0 = auto). Without show.txt every picture plays (old behavior).
- **Pictures**: `.gif` (≤256 colors) or `.pjm` full color (JPEG frames, JPEGDEC 1.8.5). Page picks per picture.
- **Time**: NTP after WiFi; POSIX TZ worked out by the phone page (`/api/settings`). Night mode hours + level.
- **Card page**: build/reorder the show, load the current show back (`/api/show`, `/api/file`), packs from the
  website (`content/` → `docs/packs/`, `cards` in pack.json = one family only), one-tap update from `info.json`.
- **Family packs (encrypted)**: private repo hogfanboy/pocket-jumbotron-content (`codes.json` + pictures) → CI
  (deploy key secret `CONTENT_DEPLOY_KEY`, hourly cron) `tools/build-vault.mjs` → `docs/vault/`. Card page unlocks with a
  family code saved on the card (`/api/settings?fc=`); crypto in `web/vault.js` (aes-js + js-sha256, page is http).
- **Address screen**: QR code of `http://<ip>` + the IP (S1 short press, and 10 s after WiFi connects).
- Android 16+/Chrome: phone needs Chrome's "Nearby devices" permission to open the card page.
- Uploads over home WiFi only. API: `GET /api/info|show|file?name=`, `POST /api/clear|upload|show|play|brightness|settings|update|forget-wifi`.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34.
- S1 button: GPIO17.

## Panel setup (same on every card)
- ESP32-HUB75-MatrixPanel-DMA 3.1.0, `cfg.driver = DP3246` (3-clock latch), default refresh (60 Hz; 30/45 make whites jitter).
- After `begin()`: `GPIO.func_out_sel_cfg[CLK].inv_sel = 0` (undo the library's clock inversion).
- `panelRegInit()`: LAT 12 = REG1, LAT 13 = REG2 (FM6126A values), LAT 11 = all ones, LAT 14 = 0x0100. FM6124C ignores them.
- Registers persist until panel power-off, so test variants per power-up.

## Key decisions
- One firmware and one panel setup for every card (no per-chip detection).
- Card stores GIF or JPEG frames, not WEBP (no small decoder, not enough RAM). WEBP is an input format only.
- Free heap with WiFi + show playing: ~72-82 KB.

## Next
- User checks on card 1 by eye: QR scan, message/clock/countdown look, full-color test picture, night mode.
- QR not scanning on the phone: 0.7.3 has 6 QR styles (S1 presses while the address shows); user reports which number works. Next: news + weather items, Improv WiFi in the installer.
