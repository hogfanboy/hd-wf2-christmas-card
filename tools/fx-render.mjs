// Renders the page's effect preview code (web/app/2-effects.js + web/fxsprites.js) in Node, one frame per effect,
// into a PNG contact sheet: a quick check of the effects without a card or browser.
//   node tools/fx-render.mjs out.png [frames=60] [effect,effect...]
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const [out = 'fx-render.png', frames = '60', only] = process.argv.slice(2);
const fxs = fs.readFileSync(path.join(root, 'web/fxsprites.js'), 'utf8');
const b64 = /base64,([A-Za-z0-9+/=]+)/.exec(fxs)[1];
const { data, info } = await sharp(Buffer.from(b64, 'base64')).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
const ctx = { W: 128, H: 64, Math, console, window: {}, Image: class { set src(v) {} }, document: {} };
vm.createContext(ctx);
vm.runInContext(fxs.replace(/FXS\.img\.(onload|src) = [^\n]*\n/g, ''), ctx);
ctx.window.FXS.px = data; ctx.window.FXS.sw = info.width; ctx.FXS = ctx.window.FXS;
vm.runInContext(fs.readFileSync(path.join(root, 'web/app/2-effects.js'), 'utf8') + '\nthis.fxNew = fxNew; this.fxStep = fxStep; this.FX = FX;', ctx);
const list = only ? only.split(',') : Object.keys(ctx.FX).filter((k) => k !== 'none' && k !== 'auto');
const SC = 4, cols = 2, rows = Math.ceil(list.length * 2 / cols), PW = 128 * SC * cols, PH = 64 * SC * rows;
const img = Buffer.alloc(PW * PH * 3, 30);
list.forEach((e, n) => [false, true].forEach((behind, b) => {
  const s = ctx.fxNew(e, behind), d = new Float64Array(128 * 64 * 4);
  for (let f = 0; f < +frames; f++) ctx.fxStep(s, null, behind);
  if (s.sleighWait) s.sleighWait = 0;
  if (['sleigh', 'turkey', 'bunny'].includes(e)) s.sleighX = (behind ? 30 : 15) * 16;
  ctx.fxStep(s, d, behind);
  const k = n * 2 + b, ox = (k % cols) * 128 * SC, oy = Math.floor(k / cols) * 64 * SC;
  for (let y = 0; y < 64; y++) for (let x = 0; x < 128; x++) for (let j = 0; j < SC - 1; j++) for (let i = 0; i < SC - 1; i++) {
    const o = ((oy + y * SC + j) * PW + ox + x * SC + i) * 3, q = (y * 128 + x) * 4;
    img[o] = Math.min(255, d[q]); img[o + 1] = Math.min(255, d[q + 1]); img[o + 2] = Math.min(255, d[q + 2]);
  }
}));
await sharp(img, { raw: { width: PW, height: PH, channels: 3 } }).png().toFile(out);
console.log(out, list.join(' '), '(left: full screen, right: behind text)');
