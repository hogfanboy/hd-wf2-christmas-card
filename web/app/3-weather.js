// Weather + forecast and stocks previews, and the animation loop for thumbnails and the big preview.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
// ---------- Weather (same look as the card; the preview fetches the real weather too) ----------
const wxKind = (c) => c <= 1 ? 'sun' : c === 2 ? 'partly' : c === 3 ? 'cloud' : c === 45 || c === 48 ? 'fog'
  : (c >= 71 && c <= 77) || c === 85 || c === 86 ? 'snow' : c >= 95 ? 'storm' : 'rain';
const wxWord = (c) => c === 0 ? 'CLEAR' : c === 1 ? 'MOSTLY CLEAR' : c === 2 ? 'PARTLY CLOUDY' : c === 3 ? 'CLOUDY' : c === 45 || c === 48 ? 'FOG'
  : c >= 51 && c <= 57 ? 'DRIZZLE' : c >= 61 && c <= 67 ? 'RAIN' : c >= 71 && c <= 77 ? 'SNOW' : c >= 80 && c <= 82 ? 'SHOWERS'
  : c === 85 || c === 86 ? 'SNOW SHOWERS' : c >= 95 ? 'THUNDERSTORM' : '';
const disc = (x, y, r) => { tctx.beginPath(); tctx.arc(x + 0.5, y + 0.5, r + 0.5, 0, 6.2832); tctx.fill(); };
function wxSpr(name, x, y) {            // x, y = sprite center, like the card's wxSprite
  const r = WXI.spr[name]; if (!r || !WXI.img.complete) return;
  tctx.drawImage(WXI.img, r[0], r[1], r[2], r[3], x + r[4], y + r[5], r[2], r[3]);
}
function wxFall(snow, x0, w, y0, h, n, f) {
  for (let i = 0; i < n; i++) {
    const x = x0 + (i * 37) % w, y = tr((f * (snow ? 1 : 5) / (snow ? 3 : 2)) + i * 11) % h;
    if (snow) { const sx = x + tr((sin8(f * 3 + i * 50) - 128) / 64); tctx.fillStyle = '#fff'; tctx.fillRect(sx - 1, y0 + y, 3, 1); tctx.fillRect(sx, y0 + y - 1, 1, 3); }
    else { const sx = x - tr(y / 4); tctx.fillStyle = '#46aaff'; tctx.fillRect(sx, y0 + y - 1, 1, 2); }
  }
}
function wxStars(cx, cy, f) {
  [[-17, -12], [13, -16], [18, 4], [-19, 9], [4, 17]].forEach(([dx, dy], i) => { const v = sin8(f * 3 + i * 51); tctx.fillStyle = `rgb(${v},${v},${v})`; tctx.fillRect(cx + dx, cy + dy, 1, 1); });
}
function wxIcon(kind, day, it, f) {
  const cx = 23, cy = 21, drift = tr((sin8(f * 2) - 128) / 64), sunF = tr(f / 5) % 6, flash = kind === 'storm' && tr(f / 4) % 12 < 2;
  if (kind === 'sun') { if (day) wxSpr('SUN' + sunF, cx, cy); else { wxStars(cx, cy, f); wxSpr('MOON', cx, cy); } }
  else if (kind === 'partly') { if (day) wxSpr('SUNS' + sunF, cx - 7, cy - 7); else { wxStars(cx, cy, f); wxSpr('MOONS', cx - 7, cy - 7); } wxSpr('CLOUD_L', cx + 3 + drift, cy + 6); }
  else if (kind === 'cloud') { wxSpr('CLOUD_B', cx - 4 - drift, cy - 6); wxSpr('CLOUD_L', cx + 3 + drift, cy + 5); }
  else if (kind === 'fog') { wxSpr('CLOUD_M', cx + 1, cy - 5); for (let i = 0; i < 3; i++) { tctx.fillStyle = i === 1 ? '#a0a8b8' : '#8c94a4'; tctx.fillRect(cx - 15 + (tr(f / 6) + i * 3) % 6, cy + 10 + i * 4, 26, 1); } }
  else if (kind === 'snow') { wxFall(true, cx - 13, 28, cy + 3, 20, 9, f); wxSpr('CLOUD_L', cx + 1, cy - 6); }
  else { wxFall(false, cx - 12, 26, cy + 4, 20, 8, f); if (flash) wxSpr('BOLT', cx + 1, cy + 12); wxSpr(flash ? 'CLOUD_L' : kind === 'storm' ? 'CLOUD_D' : 'CLOUD_M', cx + 1, cy - 6); }
}
const SAMPLE_WX = { temp: 72, hi: 75, lo: 58, code: 2, day: true, fc: [[2, 75, 58], [61, 68, 55], [3, 64, 50], [0, 70, 49], [71, 38, 29]] };
// 5-day forecast screen (copy of playForecast/wxMini in show.ino).
const MINI_IX = { sun: 0, partly: 1, cloud: 2, fog: 3, rain: 4, snow: 5, storm: 6 };
function wxMini(kind, cx, cy, f) {
  if (kind === 'rain' || kind === 'storm' || kind === 'snow') wxFall(kind === 'snow', cx - 6, 13, cy + 2, 8, 3, f);
  if (kind === 'storm' && tr(f / 4) % 12 < 2) { tctx.strokeStyle = '#fff07a'; tctx.lineWidth = 1; tctx.beginPath(); tctx.moveTo(cx + 1.5, cy + 2.5); tctx.lineTo(cx - 1.5, cy + 6.5); tctx.lineTo(cx + 1.5, cy + 6.5); tctx.lineTo(cx - 1.5, cy + 10.5); tctx.stroke(); }
  wxSpr('MINI' + MINI_IX[kind], ['sun', 'partly', 'cloud'].includes(kind) ? cx : cx + 2, cy + 1);
}
function drawForecast(it, w, t) {
  const f = Math.floor(t / 40), dow = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'], today = new Date().getDay();
  smallF(); tctx.textBaseline = 'top';
  w.fc.slice(0, 5).forEach(([code, hi, lo], i) => {
    const cx = 2 + i * 25 + 12, d = dow[(today + i) % 7], h = String(Math.round(hi)), l = String(Math.round(lo));
    tctx.fillStyle = TH().acc; T(d, cx - M(d) / 2, 1);
    wxMini(wxKind(code), cx, 20, f + i * 7);
    tctx.fillStyle = it.rainbow ? hue(t / 8 + i * 300) : it.fg;
    if (curLabels) { setF(9); T(h, cx - Math.trunc(M(h) / 2), 33); smallF(); } else T(h, cx - M(h) / 2, 36);
    tctx.fillStyle = WX_LO; T(l, cx - M(l) / 2, 49);
  });
}
// ---------- Stocks: the screen reads the same small files from the website (made hourly, tools/build-feeds.mjs) ----------
const FEEDS = SITE + 'feeds/';
const feedCache = {};
const feedText = (name) => feedCache[name] ??= fetch(FEEDS + name, { cache: 'no-cache' }).then(r => r.ok ? r.text() : '').catch(() => '');
const tsv = (txt) => txt.split('\n').filter(l => l && l[0] !== '#').map(l => l.split('\t'));
const SAMPLE_MKT = [['^GSPC', 'S&P 500', '6910', '-265', '-3.70', '13,15,13,11,10,10,9,7,8,6,6,5,3,2,0']];
const MKT_LABEL = { '^GSPC': 'S&P 500', '^DJI': 'DOW', '^IXIC': 'NASDAQ' };
let MARKETS = null;
const loadMarkets = () => MARKETS ??= fetch(FEEDS + 'index.json', { cache: 'no-cache' }).then(r => r.json()).then(j => j.markets)
  .catch(() => Object.entries(MKT_LABEL).map(([sym, label]) => ({ sym, label })))
  .then(l => { l.forEach(m => MKT_LABEL[m.sym] = m.label); return l; });
async function stocksFetch(it) { const l = tsv(await feedText('markets.txt')); it.quotes = l.length ? l : null; }
const commas = (n) => { const [a, b] = String(n).replace('-', '').split('.'); return a.replace(/\B(?=(\d{3})+(?!\d))/g, ',') + (b ? '.' + b : ''); };
function drawStocks(it, t) {        // same as the card's playStocks: one symbol at a time
  const all = it.quotes || SAMPLE_MKT;
  const pick = it.syms.split(',').filter(Boolean).map(s => all.find(q => q[0] === s)).filter(Boolean);
  const list = pick.length ? pick : SAMPLE_MKT;
  const per = it.secs ? Math.max(3000, it.secs * 1000 / list.length) : 5000;
  const q = list[Math.floor(t / per) % list.length];
  const up = q[3][0] !== '-', col = up ? '#30dc6e' : '#ff4650';
  const price = commas(q[2]), pct = (up ? '+' : '-') + q[4].replace('-', '') + '%', chg = commas(q[3]);
  smallF(); tctx.textBaseline = 'top';
  tctx.fillStyle = TH().acc; T(q[1], 2, 1);
  tctx.fillStyle = col; T(pct, W - 1 - M(pct), 1);
  tctx.textBaseline = 'alphabetic';
  fitBig(Math.min(it.font, it.font >= 8 ? 11 : 3), price, W - 2);
  drawStr(it, price, (W - M(price)) / 2, Math.round(9 + (30 + capOf()) / 2), t);
  smallF(); tctx.textBaseline = 'top';
  const cx = (W - M(chg) - 9) / 2;
  tctx.fillStyle = col; tctx.beginPath();
  if (up) { tctx.moveTo(cx, 47); tctx.lineTo(cx + 6, 47); tctx.lineTo(cx + 3, 41); } else { tctx.moveTo(cx, 41); tctx.lineTo(cx + 6, 41); tctx.lineTo(cx + 3, 47); }
  tctx.fill(); T(chg, cx + 9, curLabels ? 40 : 41);
  const v = (q[5] || '').split(',').map(Number);
  tctx.strokeStyle = col; tctx.lineWidth = 1; tctx.beginPath();
  v.forEach((y, i) => { const x = 2 + i * (W - 4) / Math.max(1, v.length - 1) + 0.5, yy = 63 - Math.trunc(y * 14 / 15) + 0.5; i ? tctx.lineTo(x, yy) : tctx.moveTo(x, yy); });
  tctx.stroke();
}
async function wxFetch(it) {
  if (!it.lat || it.lat === 'auto') { it.wx = null; return; }
  try {
    const r = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${it.lat}&longitude=${it.lon}&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5${it.unit === 'F' ? '&temperature_unit=fahrenheit' : ''}`);
    const j = await r.json();
    it.wx = { temp: j.current.temperature_2m, code: j.current.weather_code, day: !!j.current.is_day, hi: j.daily.temperature_2m_max[0], lo: j.daily.temperature_2m_min[0],
      fc: j.daily.weather_code.map((c, i) => [c, j.daily.temperature_2m_max[i], j.daily.temperature_2m_min[i]]) };
  } catch { it.wx = null; }
}
function drawStr(it, s, x, y, t) {
  if (!it.rainbow) { tctx.fillStyle = it.fg; T(s, x, y); return; }
  const gap = curPJ ? PJFont.font(curPJ).gap : 0;
  for (let i = 0; i < s.length; i++) { tctx.fillStyle = hue(t / 4 + i * 120); T(s[i], x, y); x += M(s[i]) + gap; }
}
function daysUntil(mmdd) {
  const now = new Date(), [m, d] = mmdd.split('-').map(Number);
  let tgt = new Date(now.getFullYear(), m - 1, d, 12);
  const today = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 12);
  if (tgt < today) tgt = new Date(now.getFullYear() + 1, m - 1, d, 12);
  return Math.round((tgt - today) / 86400000);
}
const wxPlace = (it) => it.place || (it.lat === 'auto' ? card.place || '' : '');
function renderItem(it, t) {      // t = ms since the item started
  curLabels = it.font >= 8;
  if (it.useTheme && !it.rainbow) it.fg = TH().main;
  paintBg(it, t);
  tctx.textBaseline = 'alphabetic';
  if (it.kind === 'fx') return tctx.getImageData(0, 0, W, H);
  if (it.kind === 'stocks') { drawStocks(it, t); return tctx.getImageData(0, 0, W, H); }
  if (it.kind === 'weather') {
    const w = it.wx || SAMPLE_WX, f = Math.floor(t / 40), per = (it.secs || 10) * 1000;
    if (it.fc && w.fc && Math.floor(t / per) % 2 === 1) { drawForecast(it, w, t); return tctx.getImageData(0, 0, W, H); }
    wxIcon(wxKind(w.code), w.day, it, f);
    const temp = String(Math.round(w.temp)) + (curLabels ? '\x7f' : ''), deg = curLabels ? 0 : 8;   // Jumbo has a degree sign
    fitBig(it.font, temp, curLabels ? 78 : 70);
    const tw = M(temp), x = 48 + Math.floor((80 - tw - deg) / 2), base = Math.round(2 + (38 + capOf()) / 2);
    drawStr(it, temp, x, base, t);
    if (!curLabels) { tctx.strokeStyle = it.rainbow ? hue(t / 4) : it.fg; tctx.lineWidth = 1.5; tctx.beginPath(); tctx.arc(x + tw + 4.5, base - capOf() + 3.5, 2, 0, 6.2832); tctx.stroke(); }
    smallF(); tctx.textBaseline = 'top';
    const word = wxWord(w.code), l2 = `H${Math.round(w.hi)} L${Math.round(w.lo)} ${wxPlace(it).toUpperCase()}`.slice(0, 21);
    tctx.fillStyle = TH().acc; T(word, 48 + (80 - M(word)) / 2, 44);
    // same as the card: "H" warm and "L" cool with 1 px after each, numbers in the main color, the town dim
    const lP = l2.indexOf(' L') + 1, tP = l2.indexOf(' ', lP) < 0 ? l2.length : l2.indexOf(' ', lP);
    const x2 = Math.trunc(W / 2 - Math.trunc((M(l2) + 2) / 2)), at = (k) => (k ? M(l2.slice(0, k) + '0') - M('0') : 0) + (k > 0) + (k > lP);
    [[0, 1, WX_HI], [1, lP, TH().main], [lP, lP + 1, WX_LO], [lP + 1, tP, TH().main], [tP, l2.length, TH().dim]].forEach(([a, b, c]) => {
      if (b > a) { tctx.fillStyle = c; T(l2.slice(a, b), x2 + at(a), 55); }
    });
    return tctx.getImageData(0, 0, W, H);
  }
  if (it.kind === 'text') {
    setF(it.font);
    const tw = M(it.text);
    const x = tw > W - 4 ? W - (Math.floor(t / 25) % (W + tw)) : (W - tw) / 2;
    drawStr(it, it.text, x, Math.round((H + capOf()) / 2), t);
  } else {
    let bigS, l1 = '', l2 = '';
    if (it.kind === 'clock') {
      const d = new Date(), h = it.h24 ? d.getHours() : (d.getHours() % 12 || 12);
      bigS = (it.h24 ? String(h).padStart(2, '0') : h) + ':' + String(d.getMinutes()).padStart(2, '0');
      l2 = d.toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' }).replace(',', '').toUpperCase() ;
    } else {
      const n = daysUntil(it.date);
      bigS = n ? String(n) : 'TODAY!'; l1 = n ? (n === 1 ? 'DAY UNTIL' : 'DAYS UNTIL') : it.text.toUpperCase(); l2 = n ? it.text.toUpperCase() : '';
    }
    const pmW = it.kind === 'clock' && !it.h24 ? (curLabels ? PJFont.width(8, 'PM') + 2 : 13) : 0;      // 12-hour clock: small AM/PM beside the time
    fitBig(it.font, bigS, W - 2, pmW);
    const bw = M(bigS), bx = (W - bw - pmW) / 2, by = Math.round((40 + capOf()) / 2), top = by - Math.round(capOf());
    drawStr(it, bigS, bx, by, t);
    if (pmW) { smallF(); tctx.textBaseline = 'top'; tctx.fillStyle = it.rainbow ? '#fff' : it.fg; T(new Date().getHours() < 12 ? 'AM' : 'PM', bx + bw + 2, top); }
    smallF(); tctx.textBaseline = 'top';
    if (it.kind === 'clock') { tctx.fillStyle = TH().acc; T(l2, (W - M(l2)) / 2, 50); }
    else { tctx.fillStyle = TH().main; T(l1, (W - M(l1)) / 2, 42); tctx.fillStyle = TH().acc; T(l2, (W - M(l2)) / 2, 53); }
  }
  return tctx.getImageData(0, 0, W, H);
}

// Animate every thumbnail and the selected item on the big preview.
function animate(now) {
  for (const it of items) {
    if (it.kind !== 'pic') {
      if (now >= (it.next || 0)) {
        it.t0 ??= now;
        const img = renderItem(it, now - it.t0);
        it.thumb.getContext('2d').putImageData(img, 0, 0);
        if (it === selected) drawLED(img);
        it.next = now + 40;
      }
      continue;
    }
    if (!it.frames.length) continue;
    if (now >= (it.next || 0)) {
      it.fi = ((it.fi ?? -1) + 1) % it.frames.length;
      it.next = now + it.frames[it.fi].delay;
      it.thumb.getContext('2d').putImageData(it.frames[it.fi].img, 0, 0);
      if (it === selected) drawLED(it.frames[it.fi].img);
    }
  }
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);

