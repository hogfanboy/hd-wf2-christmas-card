// Picture conversion shared by the card app (web/index.html) and the pack builder (tools/pack-builder.mjs), so
// the builder previews exactly what a phone will send: any picture or video in, 128x64 frames out, then GIF or
// full color (.pjm). Needs omggif (GifReader), gifenc and the jpeg-js encoder loaded first.
const W = 128, H = 64, MAX_FRAMES = 150, STILL_MS = 10000, JPEG_Q = 92;   // jpeg-js quality 1-100

// ---------- Decoding pictures ----------
// Returns full-size frames as canvases + delays (ms). Long animations keep every Nth frame, and each kept
// frame lasts as long as the frames it stands for, so the timing stays right.
function sniffType(buf, fallback) {
  const s = (a, b) => String.fromCharCode(...buf.slice(a, b));
  if (s(0, 3) === 'GIF') return 'image/gif';
  if (s(0, 4) === 'RIFF' && s(8, 12) === 'WEBP') return 'image/webp';
  if (buf[0] === 0x89 && s(1, 4) === 'PNG') return 'image/png';
  if (s(0, 4) === 'PJM1') return 'pjm';
  return fallback || 'image/jpeg';
}
function keepFrames(n, delayOf, grab) {
  const step = Math.max(1, Math.ceil(n / MAX_FRAMES));
  const out = [];
  for (let i = 0; i < n; i += step) {
    let d = 0; for (let k = i; k < Math.min(n, i + step); k++) d += delayOf(k);
    out.push(grab(i, d));
  }
  return out;
}
const toCanvas = (src, w, h) => { const c = document.createElement('canvas'); c.width = w; c.height = h; c.getContext('2d').drawImage(src, 0, 0); return c; };

async function decode(file, it) {
  if (/^video\//.test(file.type) || /\.(mp4|mov|m4v|webm)$/i.test(file.name)) return decodeVideo(file, it);
  const buf = new Uint8Array(await file.arrayBuffer());
  const type = sniffType(buf, file.type);
  if (type === 'image/gif') return decodeGif(buf);
  if (type === 'pjm') return decodePjm(buf);
  // Animated WEBP/PNG: ImageDecoder gives fully composited frames (Chrome, Edge, Android). Elsewhere: a still.
  if ('ImageDecoder' in window && (type === 'image/webp' || type === 'image/png')) {
    try {
      const dec = new ImageDecoder({ data: buf, type });
      await dec.tracks.ready;
      const n = dec.tracks.selectedTrack.frameCount;
      if (n > 1) {
        const step = Math.max(1, Math.ceil(n / MAX_FRAMES));
        const ms = [], kept = [];
        for (let i = 0; i < n; i++) {
          const { image } = await dec.decode({ frameIndex: i });
          ms.push(Math.max(20, Math.round((image.duration || 100000) / 1000)));
          if (i % step === 0) kept[i] = toCanvas(image, image.displayWidth, image.displayHeight);
          image.close();
        }
        dec.close();
        return keepFrames(n, k => ms[k], (i, d) => ({ canvas: kept[i], delay: d }));
      }
      dec.close();
    } catch (e) { console.warn('ImageDecoder failed, using a still', e); }
  }
  // ImageDecoder only exists on https pages; the card page is http, so animated WEBP is split here instead.
  if (type === 'image/webp') {
    try { const fr = await decodeWebpAnim(buf); if (fr) return fr; }
    catch (e) { console.warn('WEBP split failed, using a still', e); }
  }
  const bmp = await createImageBitmap(file);
  return [{ canvas: toCanvas(bmp, bmp.width, bmp.height), delay: STILL_MS }];
}
// Video (MP4/MOV/WEBM, whatever this browser can play): frames grabbed by seeking a hidden <video>, from
// it.vStart for it.vLen seconds, at up to VIDEO_FPS (fewer for long clips, so it stays within MAX_FRAMES).
// Frames are kept small (the screen is 128x64). No sound: the screen has no speaker.
const VIDEO_FPS = 12, VIDEO_MAX_S = 30;
async function decodeVideo(file, it = {}) {
  const v = document.createElement('video');
  v.muted = true; v.playsInline = true; v.preload = 'auto';
  const url = URL.createObjectURL(file);
  const once = (ev) => new Promise((ok, bad) => {
    const t = setTimeout(() => bad(new Error('video ' + ev + ' timed out')), 15000);
    v.addEventListener(ev, () => { clearTimeout(t); ok(); }, { once: true });
    v.addEventListener('error', () => { clearTimeout(t); bad(new Error('this browser cannot play this video')); }, { once: true });
  });
  try {
    const meta = once('loadeddata'); v.src = url; await meta;
    if (!isFinite(v.duration)) { const sk = once('seeked'); v.currentTime = 1e7; await sk; }   // recorded WEBMs: length unknown until the end
    const dur = isFinite(v.duration) && v.duration > 0 ? v.duration : 10;
    it.vDur = dur;
    const start = Math.max(0, Math.min(it.vStart || 0, Math.max(0, dur - 0.2)));
    const len = Math.max(0.2, Math.min(it.vLen || 10, VIDEO_MAX_S, dur - start));
    const fps = Math.min(VIDEO_FPS, MAX_FRAMES / len), n = Math.max(1, Math.floor(len * fps)), delay = Math.round(1000 / fps);
    const k = Math.min(1, 320 / Math.max(v.videoWidth, v.videoHeight));
    const w = Math.max(1, Math.round(v.videoWidth * k)), h = Math.max(1, Math.round(v.videoHeight * k));
    const out = [];
    for (let i = 0; i < n; i++) {
      const sk = once('seeked'); v.currentTime = start + i / fps; await sk;
      const c = document.createElement('canvas'); c.width = w; c.height = h;
      c.getContext('2d').drawImage(v, 0, 0, w, h);
      out.push({ canvas: c, delay });
      if (it.metaEl && i % 5 === 0) it.metaEl.textContent = `Reading the video… ${Math.round(i * 100 / n)}%`;
    }
    return out;
  } finally { URL.revokeObjectURL(url); v.removeAttribute('src'); v.load(); }
}
// Animated WEBP without ImageDecoder: each ANMF frame is wrapped as its own still WEBP, decoded by the
// browser, then composited (blend + dispose) onto a canvas the size of the animation.
async function decodeWebpAnim(buf) {
  const u24 = o => buf[o] | buf[o + 1] << 8 | buf[o + 2] << 16;
  const u32 = o => (u24(o) | buf[o + 3] << 24) >>> 0;
  const tag = o => String.fromCharCode(buf[o], buf[o + 1], buf[o + 2], buf[o + 3]);
  let W = 0, H = 0; const raw = [];
  for (let p = 12; p + 8 <= buf.length;) {
    const id = tag(p), len = u32(p + 4), d = p + 8;
    if (id === 'VP8X') { W = u24(d + 4) + 1; H = u24(d + 7) + 1; }
    if (id === 'ANMF') raw.push({ x: u24(d) * 2, y: u24(d + 3) * 2, w: u24(d + 6) + 1, h: u24(d + 9) + 1,
      ms: u24(d + 12), noBlend: (buf[d + 15] & 2) !== 0, dispose: (buf[d + 15] & 1) !== 0, body: buf.subarray(d + 16, d + len) });
    p = d + len + (len & 1);
  }
  if (raw.length < 2 || !W) return null;
  const chunk = (id, data) => {
    const out = new Uint8Array(8 + data.length + (data.length & 1));
    for (let i = 0; i < 4; i++) out[i] = id.charCodeAt(i);
    new DataView(out.buffer).setUint32(4, data.length, true); out.set(data, 8); return out;
  };
  const still = f => {                 // standalone WEBP: VP8X header + the frame's ALPH/VP8/VP8L chunks
    const hasAlpha = String.fromCharCode(...f.body.subarray(0, 4)) === 'ALPH';
    const x = new Uint8Array(10); x[0] = hasAlpha ? 0x10 : 0;
    x[4] = (f.w - 1) & 255; x[5] = (f.w - 1) >> 8 & 255; x[6] = (f.w - 1) >> 16;
    x[7] = (f.h - 1) & 255; x[8] = (f.h - 1) >> 8 & 255; x[9] = (f.h - 1) >> 16;
    const vp8x = chunk('VP8X', x), size = 4 + vp8x.length + f.body.length;
    const out = new Uint8Array(8 + size);
    out.set([82, 73, 70, 70]); new DataView(out.buffer).setUint32(4, size, true);
    out.set([87, 69, 66, 80], 8); out.set(vp8x, 12); out.set(f.body, 12 + vp8x.length);
    return new Blob([out], { type: 'image/webp' });
  };
  const n = raw.length, step = Math.max(1, Math.ceil(n / MAX_FRAMES));
  const cv = document.createElement('canvas'); cv.width = W; cv.height = H;
  const ctx = cv.getContext('2d'); const kept = []; let prev = null;
  for (let i = 0; i < n; i++) {
    const f = raw[i];
    if (prev && prev.dispose) ctx.clearRect(prev.x, prev.y, prev.w, prev.h);
    const bmp = await createImageBitmap(still(f));
    if (f.noBlend) ctx.clearRect(f.x, f.y, f.w, f.h);
    ctx.drawImage(bmp, f.x, f.y); bmp.close();
    if (i % step === 0) kept[i] = toCanvas(cv, W, H);
    prev = f;
  }
  return keepFrames(n, k => Math.max(20, raw[k].ms || 100), (i, d) => ({ canvas: kept[i], delay: d }));
}
function decodeGif(buf) {
  const r = new GifReader(buf);
  const n = r.numFrames(), step = Math.max(1, Math.ceil(n / MAX_FRAMES));
  const rgba = new Uint8ClampedArray(r.width * r.height * 4);
  const full = [];
  let prevInfo = null, saved = null;
  for (let i = 0; i < n; i++) {
    const info = r.frameInfo(i);
    if (prevInfo && prevInfo.disposal === 2) clearRect(rgba, r.width, prevInfo);
    if (prevInfo && prevInfo.disposal === 3 && saved) rgba.set(saved);
    if (info.disposal === 3) saved = rgba.slice();
    r.decodeAndBlitFrameRGBA(i, rgba);
    prevInfo = info;
    full.push({ delay: Math.max(20, (info.delay || 10) * 10), px: i % step === 0 ? rgba.slice() : null });
  }
  if (n === 1) full[0].delay = STILL_MS;
  return keepFrames(n, k => full[k].delay, (i, d) => {
    const c = document.createElement('canvas'); c.width = r.width; c.height = r.height;
    c.getContext('2d').putImageData(new ImageData(full[i].px, r.width, r.height), 0, 0);
    return { canvas: c, delay: d };
  });
}
async function decodePjm(buf) {           // our own full-color format, read back from the screen
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength), out = [];
  for (let p = 4; p + 8 <= buf.length;) {
    const len = dv.getUint32(p, true), delay = dv.getUint16(p + 4, true);
    const bmp = await createImageBitmap(new Blob([buf.slice(p + 8, p + 8 + len)], { type: 'image/jpeg' }));
    out.push({ canvas: toCanvas(bmp, bmp.width, bmp.height), delay });
    p += 8 + len;
  }
  return out;
}
function clearRect(rgba, w, f) {
  for (let y = f.y; y < f.y + f.height; y++) rgba.fill(0, (y * w + f.x) * 4, (y * w + f.x + f.width) * 4);
}

// Scale one source frame into a 128x64 ImageData (fill = crop to cover, fit = letterbox on black).
const work = document.createElement('canvas'); work.width = W; work.height = H;
const wctx = work.getContext('2d', { willReadFrequently: true });
function scale(src, fit) {
  const sw = src.width, sh = src.height;
  const k = fit === 'fill' ? Math.max(W / sw, H / sh) : Math.min(W / sw, H / sh);
  const dw = sw * k, dh = sh * k;
  wctx.fillStyle = '#000'; wctx.fillRect(0, 0, W, H);
  wctx.imageSmoothingEnabled = true; wctx.imageSmoothingQuality = 'high';
  wctx.drawImage(src, (W - dw) / 2, (H - dh) / 2, dw, dh);
  return wctx.getImageData(0, 0, W, H);
}

// ---------- Encoding for the screen ----------
// GIF: up to 255 colors per frame, exact, small for cartoons, text and most animated stickers. Full color
// (.pjm): JPEG per frame, for photos, gradients and video. "auto" tries GIF first and keeps it when its palette
// copies the picture closely (cartoons often have 1000+ colors only from smoothing, yet look perfect as GIF);
// otherwise full color. The preview then shows the encoded result, i.e. exactly what the screen will show.
const GIF_OK = 4.5;                        // average error (0-255 per channel) up to which GIF counts as a match
const BLACK_FLOOR = 16;                    // darker than this on every channel = black (LEDs show the faintest glow)
function gifQuant(rgba) {                  // 254 colors + exact black (letterbox bars and black stay pure black)
  const { quantize, applyPalette } = gifenc;
  const pal = quantize(rgba, 254); pal.push([0, 0, 0]);
  const idx = applyPalette(rgba, pal), BLACK = pal.length - 1;
  for (let i = 0; i < idx.length; i++) if (rgba[i * 4] + rgba[i * 4 + 1] + rgba[i * 4 + 2] === 0) idx[i] = BLACK;
  return { pal, idx };
}
function gifError(img) {                   // root-mean-square difference after palette reduction
  const d = img.data, { pal, idx } = gifQuant(d);
  let e = 0;
  for (let i = 0; i < idx.length; i++) { const c = pal[idx[i]]; e += (c[0] - d[i * 4]) ** 2 + (c[1] - d[i * 4 + 1]) ** 2 + (c[2] - d[i * 4 + 2]) ** 2; }
  return Math.sqrt(e / (idx.length * 3));
}
function gifFits(frames) {                 // checks up to 6 frames spread over the animation
  const n = frames.length, k = Math.min(6, n);
  let worst = 0;
  for (let j = 0; j < k; j++) worst = Math.max(worst, gifError(frames[Math.floor(j * n / k)].img));
  return worst <= GIF_OK;
}
// GIF like tools/gif-encode.mjs: after the first frame, pixels that look the same as what's already on the
// screen are stored as transparent (kept from the previous frame), which shrinks animations a lot.
// Returns the blob and the frames as the screen will show them.
function encodeGif(frames) {
  const { GIFEncoder } = gifenc, SAME = 10;
  const g = GIFEncoder(), shown = new Uint8ClampedArray(W * H * 4).fill(255), out = [];
  frames.forEach((f, n) => {
    const d = f.img.data, { pal, idx } = gifQuant(d), TRANSP = pal.length;
    pal.push([0, 0, 0]);
    for (let i = 0; i < idx.length; i++) {
      const o = i * 4;
      if (n > 0 && Math.abs(d[o] - shown[o]) + Math.abs(d[o + 1] - shown[o + 1]) + Math.abs(d[o + 2] - shown[o + 2]) <= SAME) { idx[i] = TRANSP; continue; }
      const c = pal[idx[i]]; shown[o] = c[0]; shown[o + 1] = c[1]; shown[o + 2] = c[2];
    }
    g.writeFrame(idx, W, H, { palette: pal, delay: f.delay, transparent: n > 0, transparentIndex: TRANSP, dispose: 1 });
    out.push({ img: new ImageData(shown.slice(), W, H), delay: f.delay });
  });
  g.finish();
  return { blob: new Blob([g.bytes()], { type: 'image/gif' }), shown: out };
}
// Full color: near-black made pure black first, then a 4:4:4 JPEG (jpeg-js) at high quality. The browser's own
// JPEG keeps color at half resolution, which smears reds and greens badly at 128x64.
async function encodePjm(frames) {
  const parts = [new TextEncoder().encode('PJM1')], out = [];
  for (const f of frames) {
    const d = new Uint8ClampedArray(f.img.data);
    for (let i = 0; i < d.length; i += 4) if (d[i] < BLACK_FLOOR && d[i + 1] < BLACK_FLOOR && d[i + 2] < BLACK_FLOOR) d[i] = d[i + 1] = d[i + 2] = 0;
    const jpg = window['jpeg-js'].encode({ data: d, width: W, height: H }, JPEG_Q).data;
    const hdr = new DataView(new ArrayBuffer(8));
    hdr.setUint32(0, jpg.length, true); hdr.setUint16(4, Math.min(65535, Math.round(f.delay)), true);
    parts.push(new Uint8Array(hdr.buffer), jpg);
    // What the screen shows: the decoded JPEG with the screen's black floor (player.ino jpegDraw).
    const bmp = await createImageBitmap(new Blob([jpg], { type: 'image/jpeg' }));
    wctx.drawImage(bmp, 0, 0); bmp.close();
    const img = wctx.getImageData(0, 0, W, H), p = img.data;
    for (let i = 0; i < p.length; i += 4) if (p[i] < 16 && p[i + 1] < 16 && p[i + 2] < 16) p[i] = p[i + 1] = p[i + 2] = 0;
    out.push({ img, delay: f.delay });
  }
  return { blob: new Blob(parts, { type: 'application/octet-stream' }), shown: out };
}
