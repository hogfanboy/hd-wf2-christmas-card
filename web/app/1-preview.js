// Shared state, the LED-look preview, text/clock/countdown drawing (a close copy of the card), color themes.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
'use strict';
const SITE = location.hostname === 'localhost' ? '/' : 'https://hogfanboy.github.io/hd-wf2-christmas-card/';
const $ = (id) => document.getElementById(id);
let card = { total: 0, used: 0, brightness: 100, fw: '?' };
// Show items. Pictures: { kind:'pic', name, src, fit, frames:[{img,delay}], blob, ext }.
// Others: { kind:'text'|'clock'|'countdown', text, fg, bg, rainbow, font, effect, date, h24 }. All: secs (0 = auto).
const items = [];
let selected = null;

// ---------- LED-look preview ----------
const big = $('big'), bctx = big.getContext('2d');
const S = big.width / W;
const mask = (() => {
  const m = document.createElement('canvas'); m.width = big.width; m.height = big.height;
  const c = m.getContext('2d'); c.fillStyle = '#07070b'; c.fillRect(0, 0, m.width, m.height);
  c.globalCompositeOperation = 'destination-out';
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) { c.beginPath(); c.arc(x * S + S / 2, y * S + S / 2, S * 0.42, 0, 6.2832); c.fill(); }
  return m;
})();
const small = document.createElement('canvas'); small.width = W; small.height = H;
const sctx = small.getContext('2d');
function drawLED(img) {
  sctx.putImageData(img, 0, 0);
  bctx.imageSmoothingEnabled = false;
  bctx.drawImage(small, 0, 0, big.width, big.height);
  bctx.drawImage(mask, 0, 0);
}
function blank() { bctx.fillStyle = '#07070b'; bctx.fillRect(0, 0, big.width, big.height); }
blank();

// ---------- Text / clock / countdown rendering (a close copy of what the screen draws) ----------
const tcan = document.createElement('canvas'); tcan.width = W; tcan.height = H;
const tctx = tcan.getContext('2d', { willReadFrequently: true });
// Screen fonts: 1 pixel, 2-4 small/medium/big. CAP = capital-letter height in pixels.
const CAP = { 1: 14, 2: 17, 3: 25, 4: 34 };
const fontCss = (f) => f == 1 ? `bold ${Math.round(CAP[1] / 0.72)}px monospace`
  : `bold ${Math.round(CAP[f] / 0.72)}px Arial, Helvetica, sans-serif`;
// 8-12: our own "Jumbo" font (web/pjfont.js): the exact pixels the card draws. curPJ = the PJ size in use (0 = canvas font).
let curPJ = 0, curLabels = false;          // curLabels: this item uses Jumbo, so its small labels use thin Jumbo too
const setF = (f) => { curPJ = f >= 8 ? f : 0; if (!curPJ) tctx.font = fontCss(f); tctx.letterSpacing = '0px'; };
const smallF = () => { curPJ = curLabels ? 8 : 0; tctx.font = '8px monospace'; tctx.letterSpacing = '0px'; };
const M = (s) => curPJ ? PJFont.width(curPJ, s) : tctx.measureText(s).width;
function T(s, x, y) {                      // fillText in the current font (PJ: baseline, or top when textBaseline is 'top')
  if (!curPJ) return tctx.fillText(s, x, y);
  const base = tctx.textBaseline === 'top' ? y + PJFont.SIZES[curPJ][0] : y, ga = tctx.globalAlpha;
  PJFont.draw(curPJ, s, Math.round(x), Math.round(base), (px, py, a) => { tctx.globalAlpha = ga * a; tctx.fillRect(px, py, 1, 1); });
  tctx.globalAlpha = ga;
}
// Same as the card's fitBig: the largest font (up to the item's size, at least medium) that fits.
function fitBig(f, s, maxW, extra = 0) {
  const l = f >= 8 ? (f == 12 ? [12, 11, 10] : [11, 10]) : f == 4 ? [4, 3, 2] : [3, 2];
  for (const x of l) { setF(x); if (M(s) + extra <= maxW) return x; }
  return l[l.length - 1];
}
const capOf = () => curPJ ? PJFont.SIZES[curPJ][0] : +tctx.font.match(/(\d+)px/)[1] * 0.72;
// Color themes: the same table as the card (canvas.ino THEMES). Items with "Theme" color follow it.
const THEMES = [
  { name: 'Pure white', main: '#ffffff', acc: '#ffbe3c', dim: '#b4b4c8' },
  { name: 'Warm white', main: '#f0e4cc', acc: '#ffbe3c', dim: '#b4aa96' },
  { name: 'Soft gold', main: '#ffd27a', acc: '#f0e4cc', dim: '#b49c78' },
  { name: 'Ice blue', main: '#a8d8ff', acc: '#ffd27a', dim: '#8ca0b8' },
  { name: 'Mint', main: '#b4f0c8', acc: '#ffd27a', dim: '#8cb49c' },
  { name: 'Peach', main: '#ffc09a', acc: '#f0e4cc', dim: '#b49682' },
  { name: 'Lavender', main: '#c8c0ff', acc: '#ffd27a', dim: '#a09cc0' },
];
let themeIx = 2;
const TH = () => THEMES[themeIx] || THEMES[2];
const WX_HI = '#ff8250', WX_LO = '#78b4ff';   // same as the card: today's high / low (and forecast lows)
function drawThemes() {
  $('themeRow').innerHTML = THEMES.map((t, i) => `<button class="sw${i === themeIx ? ' on' : ''}" data-i="${i}" title="${t.name}" style="background:${t.main}"></button>`).join('');
  $('themeName').innerHTML = `<span class="muted">Selected:</span> <b style="color:${TH().main}">${TH().name}</b>` + (card.theme === undefined && card.fw !== '?' ? ' (this screen needs new software for themes; the color is sent instead)' : '');
  $('themeRow').querySelectorAll('.sw').forEach(b => b.onclick = async () => {
    themeIx = +b.dataset.i; drawThemes();
    if (card.theme !== undefined) { try { await api('/api/settings?theme=' + themeIx, { method: 'POST' }); card.theme = themeIx; } catch {} }
  });
}
const hue = (h) => `hsl(${(h % 1536) / 1536 * 360},100%,50%)`;
