// Makes the website's app icon (docs/icon-192.png, docs/icon-512.png): a little LED screen saying "PJ".
// Run once after changing it: node tools/make-icon.mjs
import sharp from 'sharp';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const docs = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'docs');
// 5x7 pixel letters
const P = ['11110', '10001', '10001', '11110', '10000', '10000', '10000'];
const J = ['00111', '00010', '00010', '00010', '10010', '10010', '01100'];
const cols = 13, rows = 9, cell = 26, x0 = (512 - cols * cell) / 2, y0 = 150;
let dots = '';
for (let r = 0; r < rows; r++)
  for (let c = 0; c < cols; c++) {
    const lr = r - 1, onP = c >= 1 && c < 6 && lr >= 0 && lr < 7 && P[lr][c - 1] === '1', onJ = c >= 7 && c < 12 && lr >= 0 && lr < 7 && J[lr][c - 7] === '1';
    const fill = onP ? '#ffbe3c' : onJ ? '#5ac8ff' : '#1c1c28';
    dots += `<circle cx="${x0 + c * cell + cell / 2}" cy="${y0 + r * cell + cell / 2}" r="${cell * 0.4}" fill="${fill}"/>`;
  }
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512">
  <rect width="512" height="512" fill="#0b0b12"/>
  <rect x="56" y="124" width="400" height="286" rx="28" fill="#b31d2c"/>
  <rect x="74" y="140" width="364" height="254" rx="12" fill="#07070b"/>
  ${dots}
</svg>`;
for (const size of [192, 512]) await sharp(Buffer.from(svg)).resize(size, size).png().toFile(path.join(docs, `icon-${size}.png`));
console.log('icons written');
