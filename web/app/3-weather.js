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
// Moon phase from the date, like the card's moonPhase(): 0 new .. 4 full .. 7 waning crescent; mirrored south of the equator.
const MOON_NAME = ['NEW MOON', 'WAXING CRESCENT', 'FIRST QUARTER', 'WAXING GIBBOUS', 'FULL MOON', 'WANING GIBBOUS', 'LAST QUARTER', 'WANING CRESCENT'];
function moonPhase() { const SYN = 29.530588853, age = (((Date.now() / 1000 - 947182440) / 86400) % SYN + SYN) % SYN; return Math.floor(age / SYN * 8 + 0.5) % 8; }
function wxIcon(kind, day, moon, f) {
  const cx = 23, cy = 21, drift = tr((sin8(f * 2) - 128) / 64), sunF = tr(f / 5) % 6, flash = kind === 'storm' && tr(f / 4) % 12 < 2;
  if (kind === 'sun') { if (day) wxSpr('SUN' + sunF, cx, cy); else { wxStars(cx, cy, f); wxSpr('MOON' + moon, cx, cy); } }
  else if (kind === 'partly') { if (day) wxSpr('SUNS' + sunF, cx - 7, cy - 7); else { wxStars(cx, cy, f); wxSpr('MOONS' + moon, cx - 7, cy - 7); } wxSpr('CLOUD_L', cx + 3 + drift, cy + 6); }
  else if (kind === 'cloud') { wxSpr('CLOUD_B', cx - 4 - drift, cy - 6); wxSpr('CLOUD_L', cx + 3 + drift, cy + 5); }
  else if (kind === 'fog') { wxSpr('CLOUD_M', cx + 1, cy - 5); for (let i = 0; i < 3; i++) { tctx.fillStyle = i === 1 ? '#a0a8b8' : '#8c94a4'; tctx.fillRect(cx - 15 + (tr(f / 6) + i * 3) % 6, cy + 10 + i * 4, 26, 1); } }
  else if (kind === 'snow') { wxFall(true, cx - 13, 28, cy + 3, 20, 9, f); wxSpr('CLOUD_L', cx + 1, cy - 6); }
  else { wxFall(false, cx - 12, 26, cy + 4, 20, 8, f); if (flash) wxSpr('BOLT', cx + 1, cy + 12); wxSpr(flash ? 'CLOUD_L' : kind === 'storm' ? 'CLOUD_D' : 'CLOUD_M', cx + 1, cy - 6); }
}
const SAMPLE_WX = (() => {
  const off = -new Date().getTimezoneOffset() * 60, now = Math.floor(((Date.now() / 1000 + off) / 60) % 1440), h0 = Math.trunc(now / 60);
  const days = [[2, 75, 58, 10], [61, 68, 55, 80], [3, 64, 50, 20], [0, 70, 49, 0], [71, 38, 29, 60], [0, 45, 30, 0], [2, 52, 36, 10], [3, 58, 44, 20],
    [61, 61, 50, 70], [80, 63, 52, 50], [2, 66, 51, 10], [0, 71, 54, 0], [0, 74, 57, 0], [2, 72, 58, 20], [95, 69, 57, 60], [3, 65, 53, 30]];
  const hr = Array.from({ length: 25 }, (_, i) => { const h = (h0 + i) % 24, day = h >= 7 && h < 18;
    return [Math.round(66 + 8 * Math.sin((h - 9) / 24 * 6.2832)), i >= 2 && i <= 6 ? 61 : day ? 2 : 0, i >= 1 && i <= 7 ? [20, 60, 80, 70, 50, 30, 15][i - 1] : 5, day]; });
  return { temp: 72, feels: 70, hi: 75, lo: 58, code: 2, day: true, F: true, off, hum: 64, wind: 9, gust: 18, windDir: 225, uv: 4, dew: 58, aqi: 42,
    sunrise: 409, sunset: 1013, fc: days, hr, hr0: h0, mn0: Math.trunc(now / 15) * 15, mn: [0, 0, 0, 0.1, 0.4, 1.2, 2.5, 0.8, 0.2].map(r => [r, 0]) };
})();
const WX_DROP = '#46aaff', WX_PCT = '#c8e6ff';
// The PJ label font leaves 2 px between letters; the chance is drawn with 1 px so the drop and "40%" fit 25 px (like the card).
const tightW = (s) => curLabels ? [...s].reduce((w, ch) => w + LW(ch), 0) - 1 : LW(s);
function tightL(s, x, top, c) { if (!curLabels) { L(s, x, top, c); return; } for (const ch of s) { L(ch, x, top, c); x += LW(ch); } }
function wxRainPct(pct, cx, top) {          // a drop and the chance, like the card's wxRainPct
  const p = Math.round(pct), s = tightW(p + '%') > 25 ? String(p) : p + '%', w = tightW(s), drop = w + 6 <= 25 ? 6 : 0, x = cx - Math.trunc((w + drop) / 2);
  if (drop) { tctx.fillStyle = WX_DROP; [[2, 0, 1], [1, 1, 3], [0, 2, 5], [0, 3, 5], [1, 4, 3]].forEach(([dx, dy, n]) => tctx.fillRect(x + dx, top + (curLabels ? 3 : 1) + dy, n, 1)); }
  tightL(s, x + drop, top, WX_PCT);
}
// 5-day forecast screen (copy of playForecast/wxMini in show.ino).
const MINI_IX = { sun: 0, partly: 1, cloud: 2, fog: 3, rain: 4, snow: 5, storm: 6 };
function wxMini(kind, cx, cy, f) {
  if (kind === 'rain' || kind === 'storm' || kind === 'snow') wxFall(kind === 'snow', cx - 6, 13, cy + 2, 8, 3, f);
  if (kind === 'storm' && tr(f / 4) % 12 < 2) { tctx.strokeStyle = '#fff07a'; tctx.lineWidth = 1; tctx.beginPath(); tctx.moveTo(cx + 1.5, cy + 2.5); tctx.lineTo(cx - 1.5, cy + 6.5); tctx.lineTo(cx + 1.5, cy + 6.5); tctx.lineTo(cx - 1.5, cy + 10.5); tctx.stroke(); }
  wxSpr('MINI' + MINI_IX[kind], ['sun', 'partly', 'cloud'].includes(kind) ? cx : cx + 2, cy + 1);
}
// Hourly and daily strips (copies of the card's stripMs/stripOff): hold 3 s, slide left at 25 px/s, hold 2 s.
const stripMs = (w) => { const m = Math.max(0, w - W); return m ? 3000 + m * 40 + 2000 : 0; };
const stripOff = (ms, w) => ms < 3000 ? 0 : Math.min(Math.max(0, w - W), Math.trunc((ms - 3000) / 40));
const dayN = (it, w) => Math.min(it.days10 ? 10 : 5, w.fc.length);
function drawForecast(it, w, t, ms = 0) {   // copy of the card's drawDays5: 25-px day columns, 10 days slide
  const f = Math.floor(t / 40), dow = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'], today = new Date().getDay(), n = dayN(it, w), off = stripOff(ms, 3 + n * 25);
  smallF(); tctx.textBaseline = 'top';
  w.fc.slice(0, n).forEach(([code, hi, lo, rain], i) => {
    const cx = 2 + i * 25 + 12 - off;
    if (hi == null || lo == null || cx - 12 >= W || cx + 13 <= 0) return;
    const d = dow[(today + i) % 7], h = String(Math.round(hi)), l = String(Math.round(lo));
    smallF(); tctx.textBaseline = 'top';
    tctx.fillStyle = TH().acc; T(d, cx - M(d) / 2, 1);
    wxMini(wxKind(code), cx, 18, f + i * 7);
    tctx.fillStyle = it.rainbow ? hue(t / 8 + i * 300) : it.fg;
    if (curLabels) { setF(9); T(h, cx - Math.trunc(M(h) / 2), 29); smallF(); } else T(h, cx - M(h) / 2, 31);
    tctx.fillStyle = WX_LO; T(l, cx - M(l) / 2, 42);
    if (rain != null && rain >= 10) wxRainPct(rain, cx, 54);
  });
}
// ---------- More weather screens (copies of the card's drawRainSoon, drawHourly, ... in weather.ino) ----------
// [show.txt option, item key, checkbox text, toggle], in the order the card plays them after "now". A toggle
// [item key, off text, on text, show.txt option when on] is a choice next to the checkbox: hourly = 12 hours
// ("hourly") or 24 hours ("chart"), one column per hour, sliding; daily = 5 days ("forecast") or 10 days ("days10", sliding).
const WX_PAGES = [
  ['rainsoon', 'rainsoon', 'Rain on the way: only when rain or snow is coming in the next 2 hours'],
  ['hourly', 'hourly', 'Hourly forecast', ['hours24', '12 hours', '24 hours', 'chart']],
  ['forecast', 'fc', 'Daily forecast', ['days10', '5 days', '10 days', 'days10']],
  ['details', 'details', 'Wind, humidity, UV, dew point'],
  ['air', 'air', 'Air quality'],
  ['sun', 'sun', 'Sunrise, sunset and the moon'],
];
const hexC = (n) => '#' + n.toString(16).padStart(6, '0');
const P = (x, y, c) => { tctx.fillStyle = c; tctx.fillRect(x, y, 1, 1); };
const LW = (s) => { smallF(); return M(s); };
const L = (s, x, y, c) => { smallF(); tctx.textBaseline = 'top'; tctx.fillStyle = c; T(s, x, y); };
const LC = (s, cx, y, c) => L(s, cx - Math.trunc(LW(s) / 2), y, c);
function line(x0, y0, x1, y1, c) {               // Bresenham, like the card's drawLine
  const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  let e = dx + dy;
  for (;;) { P(x0, y0, c); if (x0 === x1 && y0 === y1) break; const e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
const hhmm = (mins, F) => { const h = Math.trunc(mins / 60) % 24, m = mins % 60, mm = (m < 10 ? ':0' : ':') + m; return F ? `${h % 12 || 12}${mm} ${h < 12 ? 'AM' : 'PM'}` : h + mm; };
const wxHourName = (h, F) => { h %= 24; return F ? `${h % 12 || 12}${h < 12 ? 'AM' : 'PM'}` : h + 'H'; };
function mixC(a, b, f) { let r = 0; for (let s = 0; s < 24; s += 8) r |= Math.trunc(((a >> s) & 255) * (1 - f) + ((b >> s) & 255) * f + 0.5) << s; return r; }
function tempColor(t, F) {
  const c = F ? (t - 32) / 1.8 : t, TT = [-10, 0, 10, 18, 25, 32, 40], K = [0xC8A0FF, 0x5096FF, 0x3CDCC8, 0x8CF050, 0xFFD23C, 0xFF7832, 0xFF2828];
  if (c <= TT[0]) return hexC(K[0]);
  for (let i = 1; i < 7; i++) if (c < TT[i]) return hexC(mixC(K[i - 1], K[i], (c - TT[i - 1]) / (TT[i] - TT[i - 1])));
  return hexC(K[6]);
}
const uvWord = (u) => u < 3 ? 'LOW' : u < 6 ? 'MODERATE' : u < 8 ? 'HIGH' : u < 11 ? 'VERY HIGH' : 'EXTREME';
const uvColor = (u) => u < 3 ? '#00e400' : u < 6 ? '#ffe600' : u < 8 ? '#ff7e00' : u < 11 ? '#ff1e1e' : '#b040ff';
const AQI_COLOR = ['#00e400', '#ffe600', '#ff7e00', '#ff1e1e', '#b040ff', '#c00040'];
const AQI_WORD = ['GOOD', 'MODERATE', 'BAD FOR SENSITIVE', 'UNHEALTHY', 'VERY UNHEALTHY', 'HAZARDOUS'];
const aqiCat = (a) => a <= 50 ? 0 : a <= 100 ? 1 : a <= 150 ? 2 : a <= 200 ? 3 : a <= 300 ? 4 : 5;
const townMin = (w) => Math.floor(((Date.now() / 1000 + w.off) / 60) % 1440 + 1440) % 1440;   // the time at the town
function rainSoon(w) {                            // { m: minutes until it starts (0 = now, -1 = none), snow, ends }
  const r = { m: -1, snow: false, ends: -1 };
  if (!w.mn?.length || w.mn0 == null) return r;
  const now = townMin(w);
  for (let i = 0; i < w.mn.length; i++) {
    let end = w.mn0 + 15 * i - now;
    if (end < -720) end += 1440; else if (end > 720) end -= 1440;
    if (end <= 0) continue;
    const wet = w.mn[i][0] >= 0.1;
    if (r.m < 0) { if (wet) { r.m = Math.max(0, end - 15); r.snow = w.mn[i][1] > 0; } }
    else if (!wet) { r.ends = Math.max(0, end - 15); break; }
  }
  return r;
}
function rainSoonText({ m, snow, ends }) {
  const w = snow ? 'SNOW' : 'RAIN', r5 = (v) => Math.max(5, Math.trunc((v + 2) / 5) * 5);
  return m > 0 ? `${w} IN ${r5(m)} MIN` : ends > 0 ? `${w} STOPS IN ${r5(ends)} MIN` : snow ? 'SNOWING NOW' : 'RAINING NOW';
}
function drawRainSoon(it, w, ms) {
  const rs = rainSoon(w); LC(rainSoonText(rs), W / 2, 1, rs.snow ? '#ffffff' : WX_PCT);
  const base = 51, grow = Math.min(1, ms / 600);
  w.mn.forEach(([mm, sn], i) => {
    const rate = mm * 4;
    let h = mm >= 0.05 ? Math.max(3, Math.min(36, Math.round(18 * (Math.log10(rate) + 1)))) : 0;
    h = Math.trunc(h * grow);
    tctx.fillStyle = sn > 0 ? '#dcf0ff' : rate < 2.5 ? '#32d250' : rate < 7.6 ? '#ffd200' : rate < 25 ? '#ff8c00' : '#ff1e1e';
    tctx.fillRect(1 + i * 14, base - h, 12, h);
  });
  tctx.fillStyle = '#505a6e'; tctx.fillRect(1, base, W - 2, 1);
  L('NOW', 1, 55, TH().acc); LC('1H', 70, 55, TH().dim); L('2H', W - 1 - LW('2H'), 55, TH().dim);
}
const hourN = (it, w) => Math.min(it.hours24 ? 25 : 13, w.hr.length);
function drawHourStrip(it, w, ms, f, t) {   // copy of the card's drawHourStrip: one 32-px column per hour, sliding
  const n = hourN(it, w), off = stripOff(ms, n * 32), dow = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'], today = new Date().getDay();
  for (let c = 0; c < n; c++) {
    const x0 = c * 32 - off, cx = x0 + 16, hr = w.hr0 + c, [temp, code, pop, day] = w.hr[c], k = wxKind(code);
    if (x0 >= W || x0 + 32 <= 0) continue;
    const mid = c && hr % 24 === 0;
    if (mid) for (let y = 0; y < H; y += 2) P(x0, y, '#404040');
    LC(c === 0 ? 'NOW' : mid ? dow[(today + Math.trunc(hr / 24)) % 7] : wxHourName(hr, w.F), cx, 3, mid ? '#ffffff' : TH().acc);
    if (!day && (k === 'sun' || k === 'partly')) wxSpr(k === 'sun' ? 'MINI7' : 'MINI8', cx, 22); else wxMini(k, cx, 21, f + c * 7);
    const s = String(Math.round(temp)); tctx.fillStyle = it.rainbow ? hue(t / 8 + c * 300) : tempColor(temp, w.F);
    if (curLabels) { setF(9); tctx.textBaseline = 'top'; T(s, cx - Math.trunc(M(s) / 2), 35); } else LC(s, cx, 35, tctx.fillStyle);
    if (pop >= 10) { smallF(); tctx.textBaseline = 'top'; wxRainPct(pop, cx, 49); }
  }
}
function drawDetails(it, w) {
  const DIR = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'], unit = w.F ? ' MPH' : ' KM/H', num = (v) => v == null ? '-' : String(Math.round(v));
  const rows = [['WIND', (w.windDir != null ? DIR[Math.trunc((w.windDir + 22) / 45) % 8] + ' ' : '') + num(w.wind) + unit], ['GUSTS', num(w.gust) + unit],
    ['HUMIDITY', num(w.hum) + '%'], ['UV INDEX', num(w.uv) + (w.uv == null ? '' : ' ' + uvWord(w.uv))], ['DEW POINT', num(w.dew) + (curLabels ? '\x7f' : '')]];
  rows.forEach(([k, v], r) => { L(k, 2, 2 + r * 12, TH().acc); L(v, W - 2 - LW(v), 2 + r * 12, r === 3 && w.uv != null ? uvColor(w.uv) : TH().main); });
}
function drawAir(it, w) {
  const a = w.aqi, c = aqiCat(a), s = String(a);
  LC('AIR QUALITY', W / 2, 1, TH().acc);
  fitBig(Math.min(it.font, it.font >= 8 ? 11 : 3), s, 90); tctx.textBaseline = 'alphabetic'; tctx.fillStyle = AQI_COLOR[c]; T(s, Math.trunc((W - M(s)) / 2), Math.round(11 + (28 + capOf()) / 2));
  LC(AQI_WORD[c], W / 2, 42, AQI_COLOR[c]);
  const B = [0, 50, 100, 150, 200, 300, 500];
  AQI_COLOR.forEach((col, k) => { tctx.fillStyle = col; tctx.fillRect(4 + k * 20, 58, 19, 3); });
  const p = 4 + c * 20 + Math.max(0, Math.min(18, Math.trunc((a - B[c]) * 19 / (B[c + 1] - B[c]))));
  tctx.fillStyle = '#fff'; for (let r = 0; r < 3; r++) tctx.fillRect(p - 2 + r, 53 + r, 5 - 2 * r, 1);
}
function sunGlyph(x, y, rise) {
  tctx.fillStyle = rise ? '#ffc83c' : '#ff8c32';
  for (let r = 0; r < 3; r++) { const h = rise ? r : 2 - r; tctx.fillRect(x + 5 - h, y + r, 2 * h + 1, 1); }
  for (let j = 0; j < 4; j++) { const dy = 4 - j; for (let i = -4; i <= 4; i++) if (i * i + dy * dy <= 20) tctx.fillRect(x + 5 + i, y + 4 + j, 1, 1); }
  tctx.fillRect(x, y + 8, 11, 1);
}
function drawSun(it, w, ms, f) {
  const phase = moonPhase();
  wxStars(23, 24, f); wxSpr('MOON' + (+it.lat < 0 ? (8 - phase) % 8 : phase), 23, 24);
  if (w.sunrise != null) { sunGlyph(52, 4, true); L(hhmm(w.sunrise, w.F), 67, 5, TH().main); }
  if (w.sunset != null) { sunGlyph(52, 20, false); L(hhmm(w.sunset, w.F), 67, 21, TH().main); }
  if (w.sunrise != null && w.sunset > w.sunrise) { const d = w.sunset - w.sunrise; L(`DAY ${Math.trunc(d / 60)}H ${d % 60}M`, 52, 37, TH().dim); }
  LC(MOON_NAME[phase], W / 2, 55, TH().acc);
}
// The line under the temperature takes turns, like the card: the sky, then only what matters today (rain or snow
// on the way, else today's chance when 10% or more; strong gusts; high UV in the daytime; unhealthy air), then feels like.
function wxSay(w, pj) {
  const say = [[wxWord(w.code), TH().acc]], rs = rainSoon(w);
  if (rs.m >= 0) say.push([rainSoonText(rs), rs.snow ? '#ffffff' : WX_PCT]);
  else if (w.fc?.[0]?.[3] >= 10) say.push([(wxKind(w.fc[0][0]) === 'snow' ? 'SNOW ' : 'RAIN ') + Math.round(w.fc[0][3]) + '%', TH().acc]);
  if (w.gust >= (w.F ? 30 : 50)) say.push(['GUSTS ' + Math.round(w.gust) + (w.F ? ' MPH' : ' KM/H'), TH().acc]);
  if (w.day && w.uv >= 6) say.push([`UV ${Math.round(w.uv)} ${uvWord(w.uv)}`, uvColor(w.uv)]);
  if (w.aqi > 100) say.push([`AIR ${w.aqi} ${aqiCat(w.aqi) === 2 ? 'POOR' : AQI_WORD[aqiCat(w.aqi)]}`, AQI_COLOR[aqiCat(w.aqi)]]);
  say.push(['FEELS ' + Math.round(w.feels ?? w.temp) + (pj ? '\x7f' : ''), TH().acc]);
  return say;
}
// Which screens have something to show (the card skips the others), and how to draw each.
const WX_HAS = { rainsoon: (w) => rainSoon(w).m >= 0, hourly: (w) => w.hr?.length, fc: (w) => w.fc?.length,
  details: () => true, air: (w) => w.aqi >= 0, sun: () => true };
const WX_DRAW = { rainsoon: drawRainSoon, hourly: drawHourStrip, fc: (it, w, ms, f, t) => drawForecast(it, w, t, ms),
  details: drawDetails, air: drawAir, sun: drawSun };
const WX_MINMS = { hourly: (it, w) => stripMs(hourN(it, w) * 32), fc: (it, w) => stripMs(3 + dayN(it, w) * 25) };   // strips last at least this long
const wxScreens = (it, w) => WX_PAGES.filter(p => it[p[1]] && WX_HAS[p[1]](w)).map(p => p[1]);
// Every item key the checkboxes and toggles use (for loading a show from the card).
const WX_KEYS = WX_PAGES.flatMap(p => p[3] ? [p[1], p[3][0]] : [p[1]]);
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
  const F = it.unit === 'F' || (it.unit === 'A' && usesF());
  try {
    const r = await fetch(`https://api.open-meteo.com/v1/forecast?latitude=${it.lat}&longitude=${it.lon}&current=temperature_2m,apparent_temperature,weather_code,is_day,relative_humidity_2m,wind_speed_10m,wind_gusts_10m,wind_direction_10m,uv_index,dew_point_2m&minutely_15=precipitation,snowfall&forecast_minutely_15=9&hourly=temperature_2m,weather_code,precipitation_probability,is_day&forecast_hours=25&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset&timezone=auto&forecast_days=14${F ? '&temperature_unit=fahrenheit&wind_speed_unit=mph' : ''}`);
    const j = await r.json(), c = j.current, d = j.daily, h = j.hourly, m = j.minutely_15, rain = d.precipitation_probability_max || [];
    const hm = (iso) => iso ? +iso.slice(11, 13) * 60 + +iso.slice(14, 16) : null;
    it.wx = { temp: c.temperature_2m, feels: c.apparent_temperature ?? c.temperature_2m, code: c.weather_code, day: !!c.is_day, F, off: j.utc_offset_seconds || 0,
      hum: c.relative_humidity_2m, wind: c.wind_speed_10m, gust: c.wind_gusts_10m, windDir: c.wind_direction_10m, uv: c.uv_index, dew: c.dew_point_2m,
      hi: d.temperature_2m_max[0], lo: d.temperature_2m_min[0], sunrise: hm(d.sunrise?.[0]), sunset: hm(d.sunset?.[0]),
      fc: d.weather_code.map((code, i) => [code, d.temperature_2m_max[i], d.temperature_2m_min[i], rain[i]]),
      hr: h ? h.time.map((_, i) => [h.temperature_2m[i], h.weather_code[i], h.precipitation_probability[i] ?? 0, !!h.is_day[i]]) : [], hr0: h ? +h.time[0].slice(11, 13) : 0,
      mn: m ? m.time.map((_, i) => [m.precipitation[i] ?? 0, m.snowfall?.[i] ?? 0]) : [], mn0: m ? hm(m.time[0]) : null, aqi: -1 };
    fetch(`https://air-quality-api.open-meteo.com/v1/air-quality?latitude=${it.lat}&longitude=${it.lon}&current=us_aqi`).then(r => r.json())
      .then(a => { if (it.wx && a.current?.us_aqi != null) it.wx.aqi = Math.round(a.current.us_aqi); }).catch(() => {});
  } catch { it.wx = null; }
  alertsFetch(it);
}
// ---------- Weather alerts (US, weather.gov), like the card's alertsFetch / playAlert ----------
const ALERT_COLOR = ['', '#ffd200', '#ff8c00', '#ff1e1e'], ALERT_WORD = ['', 'ADVISORY', 'WATCH', 'WARNING'];
const alertLevel = (ev) => { const l = ev.split(' ').pop(); return l === 'Warning' || l === 'Emergency' ? 3 : l === 'Watch' ? 2 : l === 'Advisory' || l === 'Alert' ? 1 : 0; };
function alertList(features) {
  const out = [];
  for (const { properties: p } of features) {
    const level = alertLevel(p.event || ''), until = Date.parse(p.ends || p.expires || '') || 0;
    if (!level || p.messageType === 'Cancel' || (until && until < Date.now())) continue;
    const title = (p.event.includes(' ') ? p.event.slice(0, p.event.lastIndexOf(' ')) : p.event).toUpperCase().slice(0, 31);
    const same = out.find(a => a.level === level && a.title === title);
    if (same) same.until = Math.max(same.until, until); else out.push({ title, level, until });
  }
  return out.sort((a, b) => b.level - a.level).slice(0, 3);
}
async function alertsFetch(it) {
  if (!it.alerts || !it.lat || it.lat === 'auto') { it.alertList = []; return; }
  try {
    const r = await fetch(`https://api.weather.gov/alerts/active?status=actual&point=${(+it.lat).toFixed(4)},${(+it.lon).toFixed(4)}`, { headers: { Accept: 'application/geo+json' } });
    it.alertList = r.ok ? alertList((await r.json()).features || []) : [];
  } catch { it.alertList = []; }
}
const SAMPLE_ALERTS = () => [{ title: 'WINTER STORM', level: 3, until: Date.now() + 5 * 3600e3 }, { title: 'WIND CHILL', level: 2, until: Date.now() + 30 * 3600e3 }];
function alertUntil(u) {
  if (!u) return 'IN EFFECT';
  const d = new Date(u), h = d.getHours() % 12 || 12, m = d.getMinutes();
  let s = `UNTIL ${h}${m ? (m < 10 ? ':0' : ':') + m : ''} ${d.getHours() < 12 ? 'AM' : 'PM'}`;
  if (d.toDateString() !== new Date().toDateString()) s += ' ' + ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'][d.getDay()];
  return s;
}
function alertTriangle(cx, top, c, mark) {
  tctx.fillStyle = c; for (let r = 0; r < 10; r++) { const half = Math.trunc(r * 5 / 9); tctx.fillRect(cx - half, top + r, half * 2 + 1, 1); }
  tctx.fillStyle = mark; tctx.fillRect(cx, top + 3, 1, 4); tctx.fillRect(cx, top + 8, 1, 1);
}
function drawAlert(a, t) {
  const col = ALERT_COLOR[a.level], lit = a.level < 3 || Math.floor(t / 400) % 2 === 0, bar = lit ? col : '#000', ink = lit ? '#000' : col;
  tctx.fillStyle = '#000'; tctx.fillRect(0, 0, W, H);
  tctx.fillStyle = bar; tctx.fillRect(0, 0, W, 13);
  if (!lit) { tctx.fillStyle = col; tctx.fillRect(0, 0, W, 1); tctx.fillRect(0, 12, W, 1); }
  tctx.textBaseline = 'alphabetic';
  setF(9); tctx.fillStyle = ink; T(ALERT_WORD[a.level], Math.trunc((W - M(ALERT_WORD[a.level])) / 2), 1 + capOf());
  alertTriangle(7, 2, ink, bar); alertTriangle(W - 8, 2, ink, bar);
  let lines = [], font = 8;
  for (const f of [11, 10, 9, 8]) {
    setF(f); const l = []; let cur = '', fits = true;
    for (const w of a.title.split(' ').filter(Boolean)) { const tr = cur ? cur + ' ' + w : w; if (M(tr) <= 124) cur = tr; else { if (cur) l.push(cur); cur = w; if (M(w) > 124) fits = false; } }
    if (cur) l.push(cur);
    lines = l; font = f;
    if (fits && l.length <= (f === 11 ? 1 : f === 8 ? 3 : 2) && l.length * capOf() + (l.length - 1) * 4 <= 38) break;
  }
  setF(font); const lh = capOf(); let y = 15 + Math.trunc((38 - (lines.length * lh + (lines.length - 1) * 4)) / 2);
  tctx.fillStyle = '#fff'; for (const l of lines) { T(l, Math.trunc((W - M(l)) / 2), y + lh); y += lh + 4; }
  setF(8); const u = alertUntil(a.until); tctx.fillStyle = col; T(u, Math.trunc((W - M(u)) / 2), 54 + capOf());
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
    // same order as the card: alerts (4.5 s each), the weather, then the forecast
    const al = it.alerts ? (it.alertSample > performance.now() ? SAMPLE_ALERTS() : it.alertList || []) : [];
    const say = wxSay(w, curLabels), nowLen = Math.max(per, say.length * 2500);   // "now" stays long enough for every line
    const screens = ['now', ...wxScreens(it, w)];
    const len = screens.map((s, i) => i ? Math.max(per, WX_MINMS[s]?.(it, w) || 0) : nowLen);
    const at0 = (i) => al.length * 4500 + len.slice(0, i).reduce((a, b) => a + b, 0);   // when screen i starts
    const jump = it.focusPage && screens.includes(it.focusPage) ? at0(screens.indexOf(it.focusPage)) : 0;
    const cyc = at0(screens.length), tc = ((t + jump) % cyc + cyc) % cyc;   // t can be a little below 0 right after a change
    if (tc < al.length * 4500) { drawAlert(al[Math.floor(tc / 4500)], tc); return tctx.getImageData(0, 0, W, H); }
    let si = 0; while (si + 1 < screens.length && tc >= at0(si + 1)) si++;
    const tw0 = tc - at0(si);
    if (si) { WX_DRAW[screens[si]](it, w, tw0, f, t); return tctx.getImageData(0, 0, W, H); }
    const kind = wxKind(w.code), phase = moonPhase();
    wxIcon(kind, w.day, +it.lat < 0 ? (8 - phase) % 8 : phase, f);
    if (al.some(a => a.level === 3) && Math.floor(t / 400) % 2 === 0) alertTriangle(5, 0, ALERT_COLOR[3], '#000');
    const temp = String(Math.round(w.temp)) + (curLabels ? '\x7f' : ''), deg = curLabels ? 0 : 8;   // Jumbo has a degree sign
    fitBig(it.font, temp, curLabels ? 78 : 70);
    const tw = M(temp), x = 48 + Math.floor((80 - tw - deg) / 2), base = Math.round(2 + (38 + capOf()) / 2);
    drawStr(it, temp, x, base, t);
    if (!curLabels) { tctx.strokeStyle = it.rainbow ? hue(t / 4) : it.fg; tctx.lineWidth = 1.5; tctx.beginPath(); tctx.arc(x + tw + 4.5, base - capOf() + 3.5, 2, 0, 6.2832); tctx.stroke(); }
    smallF(); tctx.textBaseline = 'top';
    const [word, wordC] = say[Math.min(say.length - 1, Math.floor(tw0 / (nowLen / say.length)))], l2 =`H${Math.round(w.hi)} L${Math.round(w.lo)} ${wxPlace(it).toUpperCase()}`.slice(0, 21);
    tctx.fillStyle = wordC; T(word, Math.max(0, Math.min(Math.trunc(88 - M(word) / 2), W - 1 - M(word))), 44);
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

