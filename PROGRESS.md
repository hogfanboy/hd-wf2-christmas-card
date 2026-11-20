# PROGRESS

## Working state (fw 0.7.3, 2026-11-20; card 1 on 0.7.3 via WiFi, card 2 still 0.6.2)
- One firmware for both panel chips. CI (push to main + hourly) builds firmware, installer, public packs, encrypted packs.
- Card 1 (FM6124C): 192.168.50.16 / pocketjumbo-eed0. Card 2 (RUL6024): 192.168.50.199 / pocketjumbo-5df8.
- Show (`show.ino`, `/show.txt`): pictures, messages, clock, countdown, seconds per item. Pictures are `.gif` or
  full-color `.pjm` (JPEG frames). NTP time + phone's time zone; night mode. All ran on card 1 (not yet judged by eye).
- Card page: show builder, load show back from the card, public packs, 🔒 family packs, one-tap update from the
  website. Pack button verified by the user on the Pixel.
- Private pictures: repo hogfanboy/pocket-jumbotron-content (local clone ../pocket-jumbotron-content). Family codes
  live only in its `codes.json`, never in this public repo. CI reads it with a read-only deploy key (secret
  `CONTENT_DEPLOY_KEY`) → `tools/build-vault.mjs` → `docs/vault/`. Live and checked.

## Verified pins (HD-WF2)
- HUB75: R1 2, G1 6, B1 10, R2 3, G2 7, B2 11, A 39, B 38, C 37, D 36, E 21, LAT 33, OE 35, CLK 34.
- S1 button: GPIO17.

## Panel setup (same on every card)
- ESP32-HUB75-MatrixPanel-DMA 3.1.0, `cfg.driver = DP3246`, default 60 Hz; after `begin()` `GPIO.func_out_sel_cfg[CLK].inv_sel = 0`.
- `panelRegInit()`: LAT 12 = REG1, LAT 13 = REG2 (FM6126A values), LAT 11 = all ones, LAT 14 = 0x0100. FM6124C ignores them.

## Key decisions
- One firmware and one panel setup for every card (no per-chip detection).
- Card stores GIF or JPEG frames, never WEBP (no small decoder, RAM). WEBP is an input format only.
- Private content = encrypted packs on public Pages, unlocked by a family code (AES-CTR + HMAC, PBKDF2; plain JS
  because the card page is http and has no WebCrypto). Never write a family code into this repo.

## Active issue: QR code
- 0.7.3 shows 6 QR styles (S1 presses while the address is up; number bottom right). Google Lens reads all 6,
  some faster. The Pixel 10a's own Camera app offers no link for any of them (it did for other QR codes before).
  Likely: the camera app won't offer plain `http://` links to a local IP. Next try: QR → an https page on our
  website (e.g. `.../go#192.168.50.16`) that forwards to the card. Then keep the fastest style only.

## Next
- User judges card 1 by eye: message/clock/countdown look, full-color picture, night mode, family code on the page.
- Fix the QR for the Pixel camera app (above). Then: rewrite the website text (IDEAS "Fix first"), news + weather
  items, Improv WiFi in the installer. Update card 2 when it's back in use.
