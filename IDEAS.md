# IDEAS.md (parking lot)

Raw, untested ideas parked so they don't derail the current task. Not settled truth.
At the end of a session: promote what was built and verified to PROJECT-NOTES.md, move a new direction to PROGRESS.md, delete the rest.

## Backlog (roughly in priority order)
- **First-install path for recipients**: installer page should check which panel chip a card has and **back up Huidu first** (esptool-js read-flash → download); handle the "catch the port at power-up / hold S1" step; photo instructions. Find out which of the 15 cards have which chip before mailing.
- **Daily fresh content from GitHub**: WiFi cards fetch a manifest of dated packs once a day during the season and replace the content (respect the ~4.3 MB limit).
- **Short videos**: convert on the phone/PC into a compact JPEG-frame file (full color, unlike GIF's 256 colors), decode on the card with JPEGDEC. Trim clips to fit ~4.3 MB.
- **Website update for the home-WiFi flow**: setup steps, the S1 address screen, `pocketjumbo-xxxx.local`. Landing page still says "coming soon".
- **Phone reachability on home WiFi**: the user's Pixel 10a couldn't reach the card while the PC could (guest network / AP isolation / Chrome setting?). Test another phone; explain in the help.
- **VBUS check**: with a USB-A to A cable *and* the card's own supply connected, does it back-feed the PC port?
- Dark grays weak at the very dark end at 60 Hz.
- Color depth A/B (8 vs 10/12-bit); text-heavy GIF cleanup; people with no home WiFi.

## Separate track: Waveshare ESP32-S3-RGB-Matrix
3 boards ordered (N32R16, microSD, ES8311 audio, USB-C; no speaker included, so a 4–8 Ω speaker is needed). Idea: MJPEG video + audio from SD. Confirm it drives a 128x64 panel (2x 64x64 chained). Don't mix assumptions with the WF2 (it has no PSRAM). Distribute only your own family video, not commercial specials.

## Parking Lot
- **Show builder on GitHub Pages** (like LedArt): build a playlist mixing pictures/GIFs and text programs, e.g. birthday-cake GIF 10 s → scrolling "Happy Birthday!" → fireworks animation. Needs per-item duration and a text renderer (on the card, or pre-rendered to GIF in the browser).
- **"Get latest content" button on the card's web page**: pull the content pack currently being sent out to cards (ties in with daily content packs).
- **Owners with no computer** (phone OTG install dropped: no iPhone path). Options: borrow any Chrome/Edge computer or a Chromebook (installer is a web page); mail-in, or mail them a pre-flashed spare WF2 to swap (1 ribbon + 2 power screws). Long shot: does Huidu's own WiFi update (LedArt) accept a non-Huidu image? Huidu app slots are 1 MB, ours is 1.08 MB, so it would need a small stage-1 firmware that repartitions and downloads the real one. Test only on the spare WF2.
- Installer steps for USB flashing: 1) unplug the card's power supply, 2) unplug the data ribbon and the power lead from the panel (quick connectors), 3) plug in USB and flash the bare WF2, 4) unplug USB, reconnect both panel cables, plug the power supply back in. The PC then never powers the panel (should fix flashes dropping mid-write and the back-feed worry).
