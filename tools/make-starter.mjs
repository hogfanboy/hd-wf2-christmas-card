// Renders the website simulator's scenes (docs/panel.js) into GIFs for a fresh card's starter pack.
// No third-party artwork, so it's safe to publish. Usage: node tools/make-starter.mjs <outDir>
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { encodeFrames } from './gif-encode.mjs';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = process.argv[2] || path.join(root, 'firmware/build/starter');
fs.mkdirSync(outDir, { recursive: true });

const sandbox = { window: {}, Math, console };
vm.runInNewContext(fs.readFileSync(path.join(root, 'docs/panel.js'), 'utf8'), sandbox);
const PJ = sandbox.window.PocketJumbotron;
const W = PJ.W, H = PJ.H;

// name, scene, options, frame step (ms), frame count
const items = [
  ['01-merry-christmas', 'snow', { msg: 'MERRY CHRISTMAS!' }, 70, 128],
  ['02-pocket-jumbotron', 'name', {}, 80, 30],
  ['03-happy-holidays', 'snow', { msg: 'HAPPY HOLIDAYS!' }, 70, 122],
  ['04-full-color', 'color', {}, 120, 40],
];
let total = 0;
for (const [name, scene, opts, step, count] of items) {
  const frames = [];
  for (let i = 0; i < count; i++) {
    const rgba = new Uint8Array(W * H * 4);
    const p = PJ.makeDrawer(rgba);
    p.clear();
    PJ.scenes[scene](p, 1000 + i * step, opts);
    frames.push({ rgba, delay: step });
  }
  const file = path.join(outDir, name + '.gif');
  fs.writeFileSync(file, encodeFrames(frames));
  const kb = fs.statSync(file).size / 1024; total += kb;
  console.log(`${kb.toFixed(0).padStart(5)} KB  ${String(count).padStart(4)} frames  ${name}.gif`);
}
console.log(`Starter pack ${(total / 1024).toFixed(2)} MB in ${outDir}`);
