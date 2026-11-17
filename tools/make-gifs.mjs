// Convert GIFs (any size) to 128x64 GIFs for the Pocket Jumbotron.
// Wide images fill the panel (center crop); tall/square ones are letterboxed on black.
// Usage: node tools/make-gifs.mjs <outDir> <file-or-folder>... [--fit=auto|cover|contain]
import sharp from 'sharp';
import fs from 'node:fs';
import path from 'node:path';

const W = 128, H = 64;
const args = process.argv.slice(2);
const fitArg = (args.find(a => a.startsWith('--fit=')) || '--fit=auto').slice(6);
const [outDir, ...inputs] = args.filter(a => !a.startsWith('--'));
if (!outDir || !inputs.length) { console.error('usage: node make-gifs.mjs <outDir> <inputs...> [--fit=auto|cover|contain]'); process.exit(1); }
fs.mkdirSync(outDir, { recursive: true });

const files = inputs.flatMap(p => fs.statSync(p).isDirectory()
  ? fs.readdirSync(p).filter(f => /\.gif$/i.test(f)).map(f => path.join(p, f)) : [p]);

const slug = s => path.basename(s, path.extname(s)).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 24);
let total = 0;
for (const f of files) {
  try {
    const meta = await sharp(f, { animated: true }).metadata();
    const fh = meta.pageHeight || meta.height, ratio = meta.width / fh;
    const fit = fitArg !== 'auto' ? fitArg : (ratio >= 1.6 ? 'cover' : 'contain');
    const out = path.join(outDir, slug(f) + '.gif');
    await sharp(f, { animated: true })
      .resize(W, H, { fit, background: { r: 0, g: 0, b: 0, alpha: 1 }, kernel: 'lanczos3' })
      .gif({ effort: 10, colours: 256, dither: 0.6, loop: 0 })
      .toFile(out);
    const kb = fs.statSync(out).size / 1024; total += kb;
    console.log(`${kb.toFixed(0).padStart(5)} KB  ${String(meta.pages || 1).padStart(4)} frames  ${fit.padEnd(7)} ${path.basename(out)}  <- ${path.basename(f)}`);
  } catch (e) { console.log(`  SKIP ${path.basename(f)}: ${e.message}`); }
}
console.log(`Total ${(total / 1024).toFixed(2)} MB in ${outDir}`);
