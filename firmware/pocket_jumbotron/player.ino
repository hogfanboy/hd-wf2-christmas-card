// GIF and full-color (.pjm) players, the RGB canvas and pushing it to the panel.

// ---- AnimatedGIF file callbacks (stream from LittleFS) ----
static File gifFile;
void *gifOpen(const char *name, int32_t *size) {
  gifFile = LittleFS.open(name, "r");
  if (!gifFile) return nullptr;
  *size = gifFile.size();
  return &gifFile;
}
void gifClose(void *h) { if (h) static_cast<File *>(h)->close(); }
int32_t gifRead(GIFFILE *f, uint8_t *buf, int32_t len) {
  File *file = static_cast<File *>(f->fHandle);
  int32_t n = std::min(len, f->iSize - f->iPos);
  if (n <= 0) return 0;
  n = file->read(buf, n);
  f->iPos = file->position();
  return n;
}
int32_t gifSeek(GIFFILE *f, int32_t pos) {
  File *file = static_cast<File *>(f->fHandle);
  file->seek(pos);
  f->iPos = file->position();
  return f->iPos;
}

// GIF "disposal" says what to do with a frame's area *after* it has been shown, before the next frame.
// 2 = clear it to background, 3 = restore what was there before it. We remember the previous frame's
// rule and apply it when the next frame starts (including when the animation loops back to frame 1).
struct PendingDisposal { uint8_t method; int x, y, w, h; } pending = {0, 0, 0, 0, 0};

static void fillRect(int x0, int y0, int w, int h, const PixRow *src) {
  for (int y = std::max(0, y0); y < std::min(H, y0 + h); y++)
    for (int x = std::max(0, x0); x < std::min(W, x0 + w); x++)
      for (int c = 0; c < 3; c++) canvas[y][x][c] = src ? src[y][x][c] : 0;
}

static void beginFrame(GIFDRAW *d) {
  if (pending.method == 2) fillRect(pending.x, pending.y, pending.w, pending.h, nullptr);
  else if (pending.method == 3 && saved) fillRect(pending.x, pending.y, pending.w, pending.h, saved);
  pending = {d->ucDisposalMethod, d->iX + offX, d->iY + offY, d->iWidth, d->iHeight};
  if (pending.method == 3) { if (!saved) saved = (PixRow *)malloc(CANVAS_BYTES); if (saved) memcpy(saved, canvas, CANVAS_BYTES); }
}

// Called once per line of each frame: composite into the canvas (skips transparent pixels).
void gifDraw(GIFDRAW *d) {
  if (d->y == 0) beginFrame(d);
  int y = d->iY + d->y + offY;
  if (y < 0 || y >= H) return;
  const uint8_t *pal = d->pPalette24;
  const uint8_t *src = d->pPixels;
  int x0 = d->iX + offX;
  for (int i = 0; i < d->iWidth; i++) {
    uint8_t idx = src[i];
    if (d->ucHasTransparency && idx == d->ucTransparent) continue;
    int x = x0 + i;
    if (x < 0 || x >= W) continue;
    const uint8_t *c = pal + idx * 3;
    canvas[y][x][0] = c[0]; canvas[y][x][1] = c[1]; canvas[y][x][2] = c[2];
  }
}

// Single-buffered: only pixels that changed since the last push are redrawn, which keeps each update
// short (less chance of a visible half-drawn frame). "shown" mirrors what the panel displays.
void pushCanvas() {
  if (doubleBuffered || !shown || !shownValid) {
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
        panel->drawPixelRGB888(x, y, gammaLutR[canvas[y][x][0]], gammaLut[canvas[y][x][1]], gammaLut[canvas[y][x][2]]);
    if (doubleBuffered) panel->flipDMABuffer();
    if (shown) { memcpy(shown, canvas, CANVAS_BYTES); shownValid = true; }
    return;
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t *c = canvas[y][x], *o = shown[y][x];
      if (c[0] == o[0] && c[1] == o[1] && c[2] == o[2]) continue;
      panel->drawPixelRGB888(x, y, gammaLutR[c[0]], gammaLut[c[1]], gammaLut[c[2]]);
      o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
    }
}

void showMessage(const char *line1, const char *line2) {
  panel->clearScreen();
  panel->setTextColor(panel->color565(255, 190, 60));
  panel->setCursor(2, 20); panel->print(line1);
  panel->setTextColor(panel->color565(90, 200, 255));
  panel->setCursor(2, 34); panel->print(line2);
  if (doubleBuffered) panel->flipDMABuffer();
}

bool playerAlloc() {
  if (!gifp) { gifp = new AnimatedGIF(); gifp->begin(GIF_PALETTE_RGB888); }
  if (!canvas) canvas = (PixRow *)calloc(1, CANVAS_BYTES);
  if (!doubleBuffered && !shown) shown = (PixRow *)calloc(1, CANVAS_BYTES);
  shownValid = false;
  return gifp && canvas;
}
void playerFree() {
  delete gifp; gifp = nullptr;
  free(canvas); canvas = nullptr;
  free(saved); saved = nullptr;
  free(shown); shown = nullptr;
}
// Plays one GIF. sec 0: loops short ones to 8 s, cuts long ones at ~20 s. sec > 0: exactly that long.
void playOne(const String &name, uint16_t sec) {
  if (!gif.open(name.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
    Serial.printf("  can't open %s (err %d)\n", name.c_str(), gif.getLastError());
    return;
  }
  playing = true;
  offX = (W - gif.getCanvasWidth()) / 2;
  offY = (H - gif.getCanvasHeight()) / 2;
  memset(canvas, 0, CANVAS_BYTES);
  pending.method = 0;
  shownValid = false;                            // other screens may have been drawn since
  uint32_t start = millis(), frames = 0, loops = 0;
  for (;;) {
    uint32_t t0 = millis();
    int delayMs = 0;
    int more = gif.playFrame(false, &delayMs);
    pushCanvas();
    frames++;
    if (delayMs < 20) delayMs = 100;            // GIFs with 0 delay: browsers use ~100 ms
    int32_t wait = delayMs - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 0)) break;        // button / phone / address screen interrupts
    if (buttonAction || holdPlayback || addrUntil) break;
    if (sec && millis() - start >= sec * 1000UL) break;
    if (more <= 0) {                             // end of one loop
      loops++;
      uint32_t elapsed = millis() - start;
      if (more < 0 || (!sec && elapsed >= MIN_SHOW_MS)) break;
      gif.reset();
      memset(canvas, 0, CANVAS_BYTES);       // each loop starts from a blank screen, like browsers do
      pending.method = 0;
    } else if (!sec && millis() - start > MAX_SHOW_MS && loops > 0) break;
    if (!sec && millis() - start > MAX_SHOW_MS * 2) break;  // very long single loop: hard stop
  }
  uint32_t ms = millis() - start;
  Serial.printf("  %-36s %3dx%-3d %4u frames %2u loops %5.1f fps\n", name.c_str(), gif.getCanvasWidth(), gif.getCanvasHeight(),
                (unsigned)frames, (unsigned)loops, frames * 1000.0f / (ms ? ms : 1));
  gif.close();
  playing = false;
  if (saved) { free(saved); saved = nullptr; }
}

// Startup screen: the "POCKET JUMBOTRON" arcade marquee, built into the firmware (intro_gif.h), two loops
// (~5 s). A button press skips it.
void playIntro() {
  if (!gif.open((uint8_t *)INTRO_GIF, INTRO_GIF_LEN, gifDraw)) return;
  offX = (W - gif.getCanvasWidth()) / 2;
  offY = (H - gif.getCanvasHeight()) / 2;
  memset(canvas, 0, CANVAS_BYTES);
  pending.method = 0;
  shownValid = false;
  for (int loops = 0; loops < 2;) {
    uint32_t t0 = millis();
    int delayMs = 0;
    if (gif.playFrame(false, &delayMs) <= 0) { loops++; gif.reset(); memset(canvas, 0, CANVAS_BYTES); pending.method = 0; }
    pushCanvas();
    if (delayMs < 20) delayMs = 100;
    while ((int32_t)(millis() - t0) < delayMs) { if (digitalRead(BUTTON_PIN) == LOW) loops = 2; delay(2); }
  }
  gif.close();
  while (digitalRead(BUTTON_PIN) == LOW) delay(5);   // don't count the skip press as a button action
}

// ---------- Full-color JPEG-frame pictures (.pjm) ----------
static int jpegDraw(JPEGDRAW *d) {
  const uint32_t *px = (const uint32_t *)d->pPixels;
  for (int y = 0; y < d->iHeight; y++) {
    int yy = d->y + y;
    if (yy >= H) break;
    for (int x = 0; x < d->iWidthUsed; x++) {
      int xx = d->x + x;
      if (xx >= W) continue;
      uint32_t c = px[y * d->iWidth + x];            // 0xAABBGGRR
      uint8_t r = c, g = c >> 8, b = c >> 16;
      // Black floor: JPEG leaves faint noise in black areas that dimly lights LEDs in odd colors; make it black.
      if (r < 16 && g < 16 && b < 16) r = g = b = 0;
      canvas[yy][xx][0] = r; canvas[yy][xx][1] = g; canvas[yy][xx][2] = b;
    }
  }
  return 1;
}

void playPjm(const ShowItem &it) {
  File f = LittleFS.open(it.file, "r");
  char magic[4];
  if (!f || f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "PJM1", 4)) { Serial.printf("  bad pjm %s\n", it.file.c_str()); return; }
  // The buffer only needs to hold this file's biggest frame (usually 5-12 KB): asking for less than a fixed
  // 24 KB keeps full-color pictures working when the heap is fragmented (after weather fetches, uploads...).
  size_t CAP = 0;
  for (uint8_t hdr[8]; f.read(hdr, 8) == 8;) {
    uint32_t len = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    if (len <= 32768 && len > CAP) CAP = len;
    f.seek(f.position() + len);
  }
  f.seek(4);
  JPEGDEC *jpeg = new (std::nothrow) JPEGDEC();
  uint8_t *buf = CAP ? (uint8_t *)malloc(CAP) : nullptr;
  if (!jpeg || !buf) {
    Serial.printf("  no memory for jpeg (frame %u, free %u, largest block %u)\n", (unsigned)CAP, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    delete jpeg; free(buf); f.close(); return;
  }
  playing = true;
  memset(canvas, 0, CANVAS_BYTES);
  shownValid = false;
  uint32_t start = millis(), frames = 0, loops = 0;
  uint32_t limit = it.sec ? it.sec * 1000UL : 0;
  for (;;) {
    uint32_t t0 = millis();
    uint8_t hdr[8];
    if (f.read(hdr, 8) != 8) {                                   // end of one loop
      loops++;
      uint32_t el = millis() - start;
      if (!frames) break;
      if (limit ? el >= limit : el >= MIN_SHOW_MS) break;
      f.seek(4);
      continue;
    }
    uint32_t len = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    int delayMs = hdr[4] | hdr[5] << 8;
    if (len > CAP) { f.seek(f.position() + len); continue; }
    if (f.read(buf, len) != (int)len) break;
    if (jpeg->openRAM(buf, len, jpegDraw)) {
      jpeg->setPixelType(RGB8888);
      jpeg->decode(0, 0, 0);
      jpeg->close();
    }
    pushCanvas();
    frames++;
    if (delayMs < 20) delayMs = 100;
    uint32_t el = millis() - start;
    if (limit && el >= limit) break;
    if (!limit && el > MAX_SHOW_MS && loops > 0) break;
    if (!limit && el > MAX_SHOW_MS * 2) break;
    int32_t wait = delayMs - (int32_t)(millis() - t0);
    if (waitMs(wait > 0 ? wait : 0)) break;
    if (buttonAction || holdPlayback || addrUntil) break;
  }
  uint32_t ms = millis() - start;
  Serial.printf("  %-36s jpeg %4u frames %2u loops %5.1f fps\n", it.file.c_str(), (unsigned)frames, (unsigned)loops, frames * 1000.0f / (ms ? ms : 1));
  free(buf);
  delete jpeg;
  f.close();
  playing = false;
}
