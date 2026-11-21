// Builds the small text feeds the card fetches over HTTPS from the website (docs/feeds/, published by CI hourly):
//   markets.txt       one symbol per line: "SYM<TAB>label<TAB>price<TAB>change<TAB>pct<TAB>spark"
//                     spark = today's prices as comma-separated 0-15 (0 = day low, 15 = day high), up to 32 points
//   index.json        what's there (the page's Stocks picker reads it)
// A source that fails keeps the last published copy (fetched back from the website), so one bad hour
// doesn't blank the screens. Never fails the CI run.
// Usage: node tools/build-feeds.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(root, 'docs/feeds');
const SITE = 'https://hogfanboy.github.io/hd-wf2-christmas-card/feeds/';
const UA = { 'User-Agent': 'Mozilla/5.0 (PocketJumbotron feed builder)' };

// Yahoo symbol, label on the panel (short: 128 px wide).
export const MARKETS = [
  ['^GSPC', 'S&P 500'], ['^DJI', 'DOW'], ['^IXIC', 'NASDAQ'], ['^RUT', 'RUSSELL 2000'],
  ['AAPL', 'APPLE'], ['MSFT', 'MICROSOFT'], ['NVDA', 'NVIDIA'], ['GOOGL', 'GOOGLE'], ['AMZN', 'AMAZON'],
  ['META', 'META'], ['TSLA', 'TESLA'], ['BRK-B', 'BERKSHIRE'], ['WMT', 'WALMART'], ['DIS', 'DISNEY'],
  ['F', 'FORD'], ['GM', 'GM'], ['BA', 'BOEING'], ['COST', 'COSTCO'],
  ['BTC-USD', 'BITCOIN'], ['GC=F', 'GOLD'], ['CL=F', 'OIL'],
];

async function get(url, ms = 15000) {
  const r = await fetch(url, { headers: UA, signal: AbortSignal.timeout(ms) });
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.text();
}
async function keepOld(file, why) {
  console.log(`  ${file}: ${why}; keeping the published copy`);
  try { fs.writeFileSync(path.join(out, file), await get(SITE + file)); return true; } catch { console.log(`  ${file}: no published copy either`); return false; }
}

async function quote(sym, label) {
  const j = JSON.parse(await get(`https://query1.finance.yahoo.com/v8/finance/chart/${encodeURIComponent(sym)}?range=1d&interval=15m`));
  const r = j.chart?.result?.[0], m = r?.meta;
  if (!m || typeof m.regularMarketPrice !== 'number') throw new Error('no price');
  const prev = m.chartPreviousClose ?? m.previousClose, price = m.regularMarketPrice;
  const pts = (r.indicators?.quote?.[0]?.close || []).filter(v => typeof v === 'number');
  const step = Math.max(1, Math.ceil(pts.length / 32)), sm = pts.filter((_, i) => i % step === 0);
  if (pts.length && sm[sm.length - 1] !== pts[pts.length - 1]) sm.push(pts[pts.length - 1]);
  const lo = Math.min(...sm, prev), hi = Math.max(...sm, prev);
  const spark = sm.map(v => hi > lo ? Math.round((v - lo) / (hi - lo) * 15) : 8).join(',');
  const dec = price >= 1000 ? 0 : 2;
  return [sym, label, price.toFixed(dec), (price - prev).toFixed(dec), ((price - prev) / prev * 100).toFixed(2), spark].join('\t');
}

fs.mkdirSync(out, { recursive: true });
const index = { built: new Date().toISOString(), markets: [] };
const lines = [];
for (const [sym, label] of MARKETS) {
  try { lines.push(await quote(sym, label)); } catch (e) { console.log(`  ${sym}: ${e.message}`); }
}
if (lines.length >= MARKETS.length / 2) { fs.writeFileSync(path.join(out, 'markets.txt'), lines.join('\n') + '\n'); console.log(`  markets.txt: ${lines.length} symbols`); }
else await keepOld('markets.txt', `only ${lines.length} quotes`);
const have = fs.existsSync(path.join(out, 'markets.txt')) ? fs.readFileSync(path.join(out, 'markets.txt'), 'utf8') : '';
index.markets = MARKETS.filter(([s]) => have.split('\n').some(l => l.startsWith(s + '\t'))).map(([sym, label]) => ({ sym, label }));
fs.writeFileSync(path.join(out, 'index.json'), JSON.stringify(index, null, 1));
console.log(`feeds: ${index.markets.length} market symbols -> docs/feeds/`);
