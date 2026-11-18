// 128x64 GIF encoder for the card (used by make-gifs.mjs and make-starter.mjs).
// frames: [{ rgba: Uint8Array(128*64*4), delay: ms }]
import gifenc from 'gifenc';
const { GIFEncoder, quantize, applyPalette } = gifenc;
const W = 128, H = 64;

// Each frame: 254 quantized colors + exact black + one "transparent" slot. After the first frame, pixels that
// look the same as what is already on screen are written as transparent (kept from the previous frame,
// disposal 1), which shrinks files a lot. "shown" tracks what the screen displays to avoid drift.
const SAME = 10;                       // max summed RGB difference treated as "unchanged"
export function encodeFrames(frames) {
  const g = GIFEncoder();
  const shown = new Uint8Array(W * H * 3);
  frames.forEach((f, n) => {
    const pal = quantize(f.rgba, 254);
    pal.push([0, 0, 0]);
    const BLACK = pal.length - 1, TRANSP = pal.length;
    pal.push([0, 0, 0]);
    const idx = applyPalette(f.rgba, pal.slice(0, TRANSP));
    for (let i = 0, j = 0; i < idx.length; i++, j += 3) {
      const r = f.rgba[i * 4], gg = f.rgba[i * 4 + 1], b = f.rgba[i * 4 + 2];
      if (n > 0 && Math.abs(r - shown[j]) + Math.abs(gg - shown[j + 1]) + Math.abs(b - shown[j + 2]) <= SAME) { idx[i] = TRANSP; continue; }
      if (r + gg + b === 0) idx[i] = BLACK;
      const c = pal[idx[i]]; shown[j] = c[0]; shown[j + 1] = c[1]; shown[j + 2] = c[2];
    }
    g.writeFrame(idx, W, H, { palette: pal, delay: f.delay, transparent: n > 0, transparentIndex: TRANSP, dispose: 1 });
  });
  g.finish();
  return g.bytes();
}
