// Convert GIFs/images (any size) to 128x64 GIFs for the Pocket Jumbotron.
// Wide images fill the panel (center crop); tall/square ones are letterboxed on pure black.
// Every frame is written as a full, opaque frame with its own palette that always includes exact black
// (so letterbox bars stay black instead of the nearest dark gray/green).
// Usage: node tools/make-gifs.mjs <outDir> <file-or-folder>... [--fit=auto|cover|contain] [--max-frames=150]
import sharp from 'sharp';
import gifenc from 'gifenc';
const { GIFEncoder, quantize, applyPalette } = gifenc;
import fs from 'node:fs';
import path from 'node:path';

const W = 128, H = 64;
const args = process.argv.slice(2);
const opt = (k, d) => (args.find(a => a.startsWith(`--${k}=`)) || `--${k}=${d}`).split('=')[1];
const fitArg = opt('fit', 'auto'), maxFrames = +opt('max-frames', 150);
const [outDir, ...inputs] = args.filter(a => !a.startsWith('--'));
if (!outDir || !inputs.length) { console.error('usage: node make-gifs.mjs <outDir> <inputs...> [--fit=auto|cover|contain]'); process.exit(1); }
fs.mkdirSync(outDir, { recursive: true });

const files = inputs.flatMap(p => fs.statSync(p).isDirectory()
  ? fs.readdirSync(p).filter(f => /\.(gif|png|jpe?g|webp)$/i.test(f)).map(f => path.join(p, f)) : [p]);
const slug = s => path.basename(s, path.extname(s)).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 32);

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

// Box around everything that isn't the plain border color (taken from the top-left corner), across all
// frames, with a small margin. Returns the full image when there's no meaningful border.
function trimBox(frames, w, h) {
  const f0 = frames[0].rgba, br = f0[0], bgc = f0[1], bb = f0[2];
  let x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (const { rgba } of frames)
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 4;
        if (Math.abs(rgba[i] - br) + Math.abs(rgba[i + 1] - bgc) + Math.abs(rgba[i + 2] - bb) > 48) {
          if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
      }
  if (x1 < 0) return { x: 0, y: 0, w, h };
  const m = Math.round(Math.max(w, h) * 0.02);
  x0 = Math.max(0, x0 - m); y0 = Math.max(0, y0 - m); x1 = Math.min(w - 1, x1 + m); y1 = Math.min(h - 1, y1 + m);
  const bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  if (bw * bh > w * h * 0.9) return { x: 0, y: 0, w, h };     // border too thin to matter
  return { x: x0, y: y0, w: bw, h: bh };
}
// Average color of the box's left+right edges (bars at the sides) or top+bottom edges; black if busy.
function edgeColor(rgba, w, bx, sideBars) {
  const px = [];
  // sample a little inside the box, past any leftover sliver of an outer frame/margin
  const ix = Math.max(1, Math.round(bx.w * 0.05)), iy = Math.max(1, Math.round(bx.h * 0.05));
  if (sideBars) for (let y = bx.y; y < bx.y + bx.h; y++) px.push((y * w + bx.x + ix) * 4, (y * w + bx.x + bx.w - 1 - ix) * 4);
  else for (let x = bx.x; x < bx.x + bx.w; x++) px.push(((bx.y + iy) * w + x) * 4, ((bx.y + bx.h - 1 - iy) * w + x) * 4);
  // Median color, then "plain" if >= 80% of edge pixels are close to it (tolerates watermarks/specks).
  const med = [0, 1, 2].map(c => { const v = px.map(i => rgba[i + c]).sort((a, b) => a - b); return v[v.length >> 1]; });
  const close = px.filter(i => Math.abs(rgba[i] - med[0]) + Math.abs(rgba[i + 1] - med[1]) + Math.abs(rgba[i + 2] - med[2]) < 40).length;
  return close >= px.length * 0.8 ? { r: med[0], g: med[1], b: med[2] } : { r: 0, g: 0, b: 0 };
}

let total = 0;
const written = new Set();
for (const f of files) {
  try {
    const meta = await sharp(f, { animated: true }).metadata();
    const pages = meta.pages || 1, sw = meta.width, sh = meta.pageHeight || meta.height;
    // libvips gives fully composited frames at full size; transparent pixels become black.
    const { data } = await sharp(f, { animated: true }).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const step = Math.max(1, Math.ceil(pages / maxFrames));
    const srcFrames = [];
    for (let p = 0; p < pages; p += step) {
      const rgba = new Uint8Array(data.buffer, data.byteOffset + p * sw * sh * 4, sw * sh * 4).slice();
      for (let i = 0; i < rgba.length; i += 4) { const a = rgba[i + 3] / 255; rgba[i] *= a; rgba[i + 1] *= a; rgba[i + 2] *= a; rgba[i + 3] = 255; }
      let delay = 0;
      for (let k = p; k < Math.min(pages, p + step); k++) delay += Math.max(20, (meta.delay?.[k] ?? 100) || 100);
      srcFrames.push({ rgba, delay: pages === 1 ? 10000 : delay });
    }
    // 1) Trim plain borders: union box (over all frames) of pixels that differ from the corner color.
    const bx = trimBox(srcFrames, sw, sh);
    const ratio = bx.w / bx.h;
    const fit = fitArg !== 'auto' ? fitArg : (ratio >= 1.6 ? 'cover' : 'contain');
    const frames = [];
    for (const s of srcFrames) {
      // 2) Letterbox bars take the image's own edge color when that edge is plain, otherwise black.
      const bg = fit === 'contain' ? edgeColor(s.rgba, sw, bx, ratio < W / H) : { r: 0, g: 0, b: 0 };
      const out = await sharp(Buffer.from(s.rgba.buffer), { raw: { width: sw, height: sh, channels: 4 } })
        .extract({ left: bx.x, top: bx.y, width: bx.w, height: bx.h })
        .resize(W, H, { fit, background: { ...bg, alpha: 1 }, kernel: 'lanczos3' })
        .ensureAlpha().raw().toBuffer();
      frames.push({ rgba: new Uint8Array(out), delay: s.delay });
    }
    let out = path.join(outDir, slug(f) + '.gif');
    for (let k = 2; written.has(out); k++) out = path.join(outDir, slug(f) + '-' + k + '.gif');
    written.add(out);
    fs.writeFileSync(out, encodeFrames(frames));
    const kb = fs.statSync(out).size / 1024; total += kb;
    console.log(`${kb.toFixed(0).padStart(5)} KB  ${String(frames.length).padStart(4)} frames  ${fit.padEnd(7)} ${path.basename(out)}  <- ${path.basename(f)}`);
  } catch (e) { console.log(`  SKIP ${path.basename(f)}: ${e.message}`); }
}
console.log(`Total ${(total / 1024).toFixed(2)} MB in ${outDir}`);
