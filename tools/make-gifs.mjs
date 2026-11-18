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
const slug = s => path.basename(s, path.extname(s)).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 24);

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

let total = 0;
const written = new Set();
for (const f of files) {
  try {
    const meta = await sharp(f, { animated: true }).metadata();
    const pages = meta.pages || 1, fh = meta.pageHeight || meta.height, ratio = meta.width / fh;
    const fit = fitArg !== 'auto' ? fitArg : (ratio >= 1.6 ? 'cover' : 'contain');
    // libvips gives fully composited frames, resized page by page.
    const { data } = await sharp(f, { animated: true })
      .resize(W, H, { fit, background: { r: 0, g: 0, b: 0, alpha: 1 }, kernel: 'lanczos3' })
      .ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const step = Math.max(1, Math.ceil(pages / maxFrames));
    const frames = [];
    for (let p = 0; p < pages; p += step) {
      const rgba = new Uint8Array(data.buffer, data.byteOffset + p * W * H * 4, W * H * 4).slice();
      for (let i = 0; i < rgba.length; i += 4) {               // transparent -> black, then make opaque
        const a = rgba[i + 3] / 255;
        rgba[i] *= a; rgba[i + 1] *= a; rgba[i + 2] *= a; rgba[i + 3] = 255;
      }
      let delay = 0;
      for (let k = p; k < Math.min(pages, p + step); k++) delay += Math.max(20, (meta.delay?.[k] ?? 100) || 100);
      frames.push({ rgba, delay: pages === 1 ? 10000 : delay });
    }
    let out = path.join(outDir, slug(f) + '.gif');
    if (written.has(out)) out = path.join(outDir, slug(f) + '-' + path.extname(f).slice(1).toLowerCase() + '.gif');
    written.add(out);
    fs.writeFileSync(out, encodeFrames(frames));
    const kb = fs.statSync(out).size / 1024; total += kb;
    console.log(`${kb.toFixed(0).padStart(5)} KB  ${String(frames.length).padStart(4)} frames  ${fit.padEnd(7)} ${path.basename(out)}  <- ${path.basename(f)}`);
  } catch (e) { console.log(`  SKIP ${path.basename(f)}: ${e.message}`); }
}
console.log(`Total ${(total / 1024).toFixed(2)} MB in ${outDir}`);
