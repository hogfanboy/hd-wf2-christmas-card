# Radar map on the card: verdict (2026-11-21)

**Rule (user, 2026-11-22): the radar screen only plays when rain/snow is on the way (same test as rainSoon).**
Probe files: `radar_probe.ino` (v2, what card 2 ran), `makebase.py` (basemap maker). Sections below are in order: v1 verdict, card 1 measurements, then v2.

**Feasible: measured on card 1 (v1) and card 2 (v2), see below.** Probe: `firmware/probes/radar_probe/radar_probe.ino`,
branch `claude/inspiring-perlman-7d2eaa`, commit e34355f (not pushed, not flashed).

## Source: RainViewer free API (checked live today)
- Index `api.rainviewer.com/public/weather-maps.json` (~1.5 KB): only `radar.past`, 13 frames 10 min apart (2 h).
  Nowcast and satellite are empty (both dropped 2026-01-01).
- Free tier since 2026-01-01: **max zoom 7** (z8 returns a "Zoom Level Not Supported" image), **only color scheme 2
  (Universal Blue)** (scheme 4 returns the same bytes), **100 requests/IP/min**, "personal and educational use",
  attribution "RainViewer" + link is required, no key, no SLA. Modifying the images is allowed.
- **A tile centered on lat/lon:** `{host}{path}/256/7/{lat}/{lon}/2/{smooth}_{snow}.png` returns a 256x256 tile
  centered on home, so one tile is enough and edges don't matter.
- **Plain http:// works** (200, no redirect) on tilecache and api, so no TLS is needed. If it ever goes, HTTPS is
  Let's Encrypt YE1 -> Root YE -> **ISRG Root X2**, which SITE_CA already has (X1 + X2).
- Tiles: 8-bit RGBA PNG, non-interlaced, 6-26 KB at 256 px (26 KB = Seattle with widespread rain), and 19-25 KB at
  512 px. The zlib window is always 32 KB.
- Other free sources checked: Iowa Mesonet (NEXRAD, US only) is signed by InCommon/Sectigo, which isn't in SITE_CA.
  NOAA opengeo (weather.gov) is Let's Encrypt X1 but US only and WMS (heavier). RainViewer is the one to use.

## Decoding: PNGdec 1.1.6 (bitbank2)
- It decodes line by line to a callback, so there's no full-image buffer. But the PNG object is one block of about
  **45 KB** (32 KB zlib window + ~7 KB inflate state + 1 KB palette + 2.5 KB line buffers + 2 KB file buffer). The
  window can't shrink because the tiles use 32 KB.
- It seeks back a little while parsing chunks, so the probe downloads the whole tile into RAM (malloc of
  Content-Length, capped at 60 KB) and uses openRAM.
- Peak during a fetch is about 45 KB + tile (up to ~26 KB) + WiFi. That doesn't fit beside the player (~75 KB free)
  but fits after `playerFree()` (canvas 24.6 + shown 24.6 + GIF decoder ~22 KB come back). Risk: 45 KB has to be
  **one contiguous block**. The probe logs `heap_caps_get_largest_free_block` before the allocation.
- Picture: crop the middle 256x128 of the z7 tile and shrink it 2:1 by keeping the strongest pixel (so storms don't
  vanish). That gives 128x64 ≈ 275 x 140 km. Alpha < 96 = black. White plus at home with a black ring. A PC preview
  of Seattle rain looked clear (brown = light rain, blues = rain, cyan = snow with snow=1). There's no
  coastline/road layer, so the home cross is the only landmark.

## Probe
- Reads WiFi + `wxhome` from the main firmware's NVS ("pj", read only). It falls back to Orlando.
- SIM_PLAYER=1 holds player-sized blocks + ballast to get ~75 KB free, then frees the player blocks only for the
  fetch, the same way the firmware would. It logs heap (free / largest block / min ever) at each step, and timing
  (index, download, decode+draw). It refetches every 10 min or on S1.
- Needs `arduino-cli lib install "PNGdec@1.1.6"` (installed locally; CI's lib list would need it if this goes
  into the firmware). 1.08 MB app.
- Not measured yet: card timings. From the PC, each HTTP request takes ~0.3 s. Decode should be ~100 ms, but that's
  unverified.

## If it works on the card: integration sketch
`@radar` weather screen. Every 10 min: playerFree -> new PNG -> fetch -> decode into an 8 KB palette-index buffer
(or a LittleFS file) -> delete PNG -> playerAlloc (restart if that fails, like alertsFetch). Show it with the frame
age ("12 min ago") and "RainViewer" credit. PNGdec goes into the CI lib list.

## Measured on card 1 (eed0), 2026-11-21. Probe sent over WiFi; it rolls back to beta12 on restart (verified once)
Real beta12 while playing: free ~80 KB, **largest block 31.7-33.8 KB**.
Probe with SIM_PLAYER (75 KB free):

| run | tile | after playerFree: largest | PNG fits | lowest free | index | download | decode+draw | total |
|---|---|---|---|---|---|---|---|---|
| Naperville 1 (dry) | 383 B | 71.7 KB | yes | 69.6 KB | 569 | 390 | 47 | 1010 ms |
| Naperville 2-3 | 383 B | **46.1 KB** | yes (barely) | 67.7 KB | ~450 | ~400 | 46 | ~905 ms |
| Seattle 1 (heavy rain, 4892/8192 LEDs) | 26.5 KB | 71.7 KB | yes | 58.8 KB | 535 | 1214 | 63 | 1816 ms |
| Seattle 2 | 26.5 KB | **46.1 KB** | yes (barely) | 58.3 KB | 464 | 1212 | 63 | 1743 ms |

Player memory always came back. Free heap slips ~0.5 KB per fetch (WiFi/HTTP, not PNG).
**Risk:** from the 2nd fetch on, the largest block left after playerFree is 46.1 KB vs the PNG's ~45.5 KB. That
works but has no margin. For the real firmware, either (a) allocate the PNG object first thing after playerFree
and give up the radar for that round if it fails (show the last good one), or (b) shrink PNGdec: compile with
`PNG_MAX_BUFFERED_PIXELS=(256*4+1)*2` (-512 B), or split the PNG struct so the 32 KB window is its own block
(needs a library patch; CI uses the stock library). (a) is enough to start with.
Card 1 is still running the probe (Seattle forced) until restarted: hold S1 3 s, unplug, or the 'x' serial key.
/api/update does not work while the probe runs.

## v2 (2026-11-22, commit on claude/inspiring-perlman-7d2eaa): map, home, scale, time, loop. On CARD 2 now
- Layout: radar over a dim basemap (shore line + dim water, interstates gray, state lines purple). White home cross
  with a black ring (it blinks while the newest frame holds), town name above it, dotted 25-mile ring + "25 MI",
  radar time top-right (yellow = newest, gray = older loop frames). The loop is 7 frames (1 h), 450 ms each, then
  a 3 s hold. 3x5 pixel font for the labels.
- Basemap: free dark tiles over HTTP don't exist any more (CARTO needs a key; Esri/OSM are HTTPS with
  DigiCert/GlobalSign). So it is an 8 KB class map uploaded once per town (POST /base). makebase.py builds it
  from OSM tiles by color class. **In the real firmware the page would build it in the browser** (OSM sends
  CORS *) when the town is saved, and upload it with the show.
- Memory fixes worth keeping: (1) stream the PNG from the socket through an 8 KB window (PNGdec only seeks back
  inside its last 2 KB read), so there's no 26 KB tile buffer; (2) 15 fixed levels (RainViewer Universal Blue
  table: 10 rain, 5 snow) at 4 bits = 4 KB per frame; (3) decode scratch + window = the 16 KB canvas.
- Card 2 numbers, 7 heavy-rain frames (Seattle): 0.4 s request + 0.8 s stream/decode per 26 KB tile, 9 s for the
  whole loop, lowest free heap 28.8 KB (121 KB before; the probe has no player). Dry tiles: 0.4 s each.
- Web on the probe: GET / | /log | /restart (back to beta10) | /home?lat=&lon=&name=&tz= ; POST /base.
- Real-firmware cost estimate: frames 28 KB held while playing is too much beside the player (~75 KB free).
  Keep frames in LittleFS (4 KB files, 1 new per 10 min) or only the newest 2-3 in RAM.
