// Building pictures, the show list, schedules (when an item plays), item editors.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
// Decoding and encoding pictures: web/convert.js (shared with the pack builder, tools/pack-builder.mjs).
async function build(it) {
  const frames = it.src.map(s => ({ img: scale(s.canvas, it.fit), delay: s.delay }));
  it.fi = -1; it.next = 0;
  const full = it.format === 'full' || (it.format === 'auto' && !gifFits(frames));
  it.ext = full ? 'pjm' : 'gif';
  const r = full ? await encodePjm(frames) : encodeGif(frames);
  it.blob = r.blob; it.frames = r.shown;
  it.metaEl.innerHTML = `${it.frames.length} frame${it.frames.length > 1 ? 's' : ''} · ${kb(it.blob.size)}<span class="tag">${full ? 'Full color' : 'GIF'}</span>`;
  updateSpace();
}

// ---------- Show list UI ----------
const kb = (b) => b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : Math.round(b / 1024) + ' KB';
const esc = (s) => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
function select(it) {
  selected = it;
  items.forEach(i => i.el.classList.toggle('sel', i === it));
  $('bigLabel').textContent = 'Preview: ' + label(it);
  if (it && it.kind !== 'pic') it.t0 = performance.now();
}
const label = (it) => it.kind === 'pic' ? it.name : it.kind === 'text' ? '“' + it.text + '”' : it.kind === 'clock' ? 'Clock'
  : it.kind === 'fx' ? (FX[it.effect] || 'Effect') : it.kind === 'weather' ? (!it.lat ? 'Weather: pick a town' : it.lat === 'auto' ? 'Weather here' + (card.place ? ' (' + card.place + ')' : '') : 'Weather' + (it.place ? ' in ' + it.place : ''))
  : it.kind === 'stocks' ? 'Stocks: ' + (it.syms.split(',').filter(Boolean).map(s => MKT_LABEL[s] || s).join(', ') || 'pick some') : (it.lead !== '' && it.lead != null ? '🔔 ' : 'Countdown to ') + (it.text || '…') + (/^\d\d-\d\d$/.test(it.date) ? ' · ' + md(it.date.replace('-', '')) : '');
function move(it, d) {
  const i = items.indexOf(it), j = i + d;
  if (j < 0 || j >= items.length) return;
  items.splice(i, 1); items.splice(j, 0, it);
  const box = $('items'); items.forEach(x => box.appendChild(x.el));
}
function remove(it) {
  items.splice(items.indexOf(it), 1); it.el.remove();
  if (selected === it) { selected = null; items[0] ? select(items[0]) : blank(); }
  updateSpace();
}
function baseEl(it) {
  const el = document.createElement('div'); el.className = 'item';
  el.innerHTML = `<canvas width="128" height="64"></canvas><div><div class="name"></div><div class="meta"></div>
    <div class="row"><button class="chip editBtn">✓ Done</button><span class="muted" style="font-size:12px">Seconds</span><input class="s secs" type="number" min="0" max="600" placeholder="auto">
    <button class="chip" data-mv="-1">▲</button><button class="chip" data-mv="1">▼</button><button class="chip whenBtn">⏰ When</button></div></div>
    <button class="chip del" title="Remove" aria-label="Remove">✕</button>`;
  it.el = el; it.thumb = el.querySelector('canvas'); it.metaEl = el.querySelector('.meta');
  el.querySelector('.name').textContent = label(it);
  const secs = el.querySelector('.secs');
  secs.value = it.secs || ''; secs.oninput = () => { it.secs = Math.max(0, Math.min(600, +secs.value || 0)); };
  it.thumb.onclick = () => select(it);
  el.querySelectorAll('[data-mv]').forEach(b => b.onclick = () => move(it, +b.dataset.mv));
  el.querySelector('.del').onclick = () => remove(it);
  el.querySelector('.editBtn').onclick = (e) => { e.stopPropagation(); setMini(it, true); };
  el.addEventListener('click', (e) => { if (el.classList.contains('mini') && !e.target.closest('.del')) openItem(it); });
  whenPanel(it, el);
  $('items').appendChild(el);
  items.push(it);
  loadingShow ? setMini(it, true) : openItem(it);
  return el;
}
// Only one item is open for editing at a time; the rest are compact rows, so a long list of reminders stays short.
let loadingShow = false;
function setMini(it, mini) {
  it.el.classList.toggle('mini', mini);
  if (mini) it.el.querySelector('.edit.when').hidden = true;
}
function openItem(it) { items.forEach(x => setMini(x, x !== it)); select(it); }

// ---------- When an item plays (the screen checks it: dates every year, weekdays, hours) ----------
const DAY_LETTERS = ['S', 'M', 'T', 'W', 'T', 'F', 'S'];
const hourName = (h) => new Date(2000, 0, 1, h).toLocaleTimeString([], { hour: 'numeric' });
const mmdd = (v) => v ? v.slice(5, 7) + v.slice(8, 10) : '';                 // "2026-12-01" -> "1201"
const dateVal = (m) => m ? new Date().getFullYear() + '-' + m.slice(0, 2) + '-' + m.slice(2, 4) : '';
const md = (m) => new Date(2000, +m.slice(0, 2) - 1, +m.slice(2, 4)).toLocaleDateString([], { month: 'short', day: 'numeric' });
function whenSummary(it) {
  const w = it.when, parts = [];
  if (w.from && w.to) parts.push(md(w.from) + '–' + md(w.to));
  if (w.days.length < 7) parts.push(w.days === '12345' ? 'weekdays' : w.days === '06' ? 'weekends' : w.days.split('').map(d => ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'][d]).join(' '));
  if (w.h1 !== '' && w.h2 !== '' && w.h1 !== w.h2) parts.push(hourName(+w.h1) + '–' + hourName(+w.h2));
  if (it.kind === 'countdown' && it.lead !== '' && it.lead != null) parts.push(`only the last ${it.lead} days`);
  return parts.join(' · ');
}
function whenPanel(it, el) {
  it.when = Object.assign({ from: '', to: '', days: '0123456', h1: '', h2: '' }, it.when);
  const hours = '<option value="">any time</option>' + Array.from({ length: 24 }, (_, h) => `<option value="${h}">${hourName(h)}</option>`).join('');
  const wp = document.createElement('div'); wp.className = 'edit when'; wp.hidden = true;
  wp.innerHTML = `<label>Only from <input type="date" data-w="from"> to <input type="date" data-w="to"></label>
    <label>On ${DAY_LETTERS.map((d, i) => `<button class="chip" data-d="${i}">${d}</button>`).join('')}</label>
    <label>From <select data-w="h1">${hours}</select> to <select data-w="h2">${hours}</select></label>
    <div class="muted" style="font-size:12px">Dates repeat every year. Leave blank to always show. Needs the screen's clock (internet).</div>`;
  el.appendChild(wp);
  const sum = document.createElement('div'); sum.className = 'meta whenSum'; sum.style.color = 'var(--cyan)';
  it.metaEl.after(sum);
  it.whenRefresh = () => {
    wp.querySelector('[data-w=from]').value = dateVal(it.when.from);
    wp.querySelector('[data-w=to]').value = dateVal(it.when.to);
    wp.querySelector('[data-w=h1]').value = it.when.h1;
    wp.querySelector('[data-w=h2]').value = it.when.h2;
    wp.querySelectorAll('[data-d]').forEach(b => b.classList.toggle('on', it.when.days.includes(b.dataset.d)));
    const t = whenSummary(it); sum.textContent = t ? '⏰ ' + t : ''; el.querySelector('.whenBtn').classList.toggle('on', !!t);
  };
  wp.querySelectorAll('[data-w]').forEach(inp => inp.oninput = () => {
    const k = inp.dataset.w; it.when[k] = k === 'from' || k === 'to' ? mmdd(inp.value) : inp.value; it.whenRefresh();
  });
  wp.querySelectorAll('[data-d]').forEach(b => b.onclick = () => {
    const set = new Set(it.when.days.split('')); set.has(b.dataset.d) ? set.delete(b.dataset.d) : set.add(b.dataset.d);
    it.when.days = [...set].sort().join('') || '0123456'; it.whenRefresh();
  });
  el.querySelector('.whenBtn').onclick = () => { wp.hidden = !wp.hidden; };
  it.whenRefresh();
}
// The options field at the end of a show line: "@dates=1201-1231 days=12345 hours=7-22 lead=14 forecast".
function optsField(it) {
  const w = it.when, o = [];
  if (w.from && w.to) o.push(`dates=${w.from}-${w.to}`);
  if (w.days.length < 7) o.push('days=' + w.days);
  if (w.h1 !== '' && w.h2 !== '' && w.h1 !== w.h2) o.push(`hours=${w.h1}-${w.h2}`);
  if (it.kind === 'countdown' && it.lead !== '' && it.lead != null) o.push('lead=' + it.lead);
  if (it.kind === 'weather') for (const [opt, k] of WX_PAGES) if (it[k]) o.push(opt);
  if (it.kind === 'weather' && it.alerts) o.push('alerts');
  return '@' + o.join(' ');
}
function parseOpts(field) {
  const o = { when: { from: '', to: '', days: '0123456', h1: '', h2: '' }, lead: '', alerts: false };
  for (const t of field.slice(1).split(' ').filter(Boolean)) {
    const [k, v = ''] = t.split('=');
    if (k === 'dates') [o.when.from, o.when.to] = v.split('-');
    if (k === 'days') o.when.days = v;
    if (k === 'hours') [o.when.h1, o.when.h2] = v.split('-');
    if (k === 'lead') o.lead = v;
    const pg = WX_PAGES.find(p => p[0] === k); if (pg) o[pg[1]] = true;
    if (k === 'alerts') o.alerts = true;
  }
  return o;
}

$('files').addEventListener('change', async (e) => {
  for (const file of e.target.files) await addFile(file);
  e.target.value = '';
});
// set (picture packs): { fit, format } chosen in the pack builder, so the phone converts it the same way.
async function addFile(file, secs = 0, fromCard = false, set = {}) {
  const it = { kind: 'pic', name: file.name, fit: 'auto', format: set.format || 'auto', frames: [], blob: null, secs };
  const el = baseEl(it);
  it.metaEl.textContent = 'Converting…';
  const row = document.createElement('div'); row.className = 'row';
  row.innerHTML = `<button class="chip" data-fit="fill">Fill</button><button class="chip" data-fit="fit">Fit</button>
    <button class="chip" data-fmt="gif">GIF</button><button class="chip" data-fmt="full">Full color</button>`;
  el.children[1].appendChild(row);
  row.querySelectorAll('[data-fit]').forEach(b => b.onclick = async () => { it.fit = b.dataset.fit; marks(it); await build(it); select(it); });
  row.querySelectorAll('[data-fmt]').forEach(b => b.onclick = async () => { it.format = b.dataset.fmt; marks(it); await build(it); select(it); });
  try {
    it.src = await decode(file, it);
    if (it.vDur) {                                 // a video: pick which part to show
      it.file = file;
      const vr = document.createElement('div'); vr.className = 'row';
      vr.innerHTML = `<label>Start at <input class="secs" type="number" min="0" step="0.5" data-v="vStart" value="0"> s</label>
        <label>Length <input class="secs" type="number" min="0.5" max="${VIDEO_MAX_S}" step="0.5" data-v="vLen" value="${+Math.min(10, it.vDur).toFixed(1)}"> s</label>
        <span class="muted" style="font-size:12px">of ${it.vDur.toFixed(1)} s (up to ${VIDEO_MAX_S} s; longer clips get fewer frames a second)</span>`;
      el.children[1].appendChild(vr);
      vr.querySelectorAll('[data-v]').forEach(inp => inp.onchange = async () => {
        it[inp.dataset.v] = Math.max(0, +inp.value || 0);
        it.metaEl.textContent = 'Reading the video…';
        try { it.src = await decode(it.file, it); await build(it); select(it); }
        catch (err) { console.warn(err); it.metaEl.innerHTML = '<span class="err">Couldn’t read this video</span>'; }
      });
    }
    const r = it.src[0].canvas.width / it.src[0].canvas.height;
    it.fit = set.fit || (fromCard || r >= 1.6 ? 'fill' : 'fit');
    await build(it);
    if (fromCard) { it.format = it.ext === 'pjm' ? 'full' : 'gif'; }
    marks(it);
    if (!selected) select(it);
  } catch (err) {
    console.warn(err);
    it.metaEl.innerHTML = '<span class="err">Couldn\'t read this ' + (/^video\//.test(file.type) ? 'video (try a shorter MP4)' : 'picture') + '</span>';
    it.failed = true; updateSpace();
  }
}
function marks(it) {
  it.el.querySelectorAll('[data-fit]').forEach(b => b.classList.toggle('on', b.dataset.fit === it.fit));
  it.el.querySelectorAll('[data-fmt]').forEach(b => b.classList.toggle('on', b.dataset.fmt === (it.format === 'auto' ? (it.ext === 'pjm' ? 'full' : 'gif') : it.format)));
}

// Message / clock / countdown items with their own little editor.
const COLORS = { fg: '#ffbe3c', bg: '#000000' };
const META = { text: 'Message', clock: 'Shows the time (needs internet)', countdown: 'Counts the days (needs internet). With "days before" it is a reminder: fireworks on the day',
  fx: 'Drawn by the screen itself: takes no space', weather: '⚠ Pick a town below. Weather without a town is not sent to the screen.',
  stocks: 'Market prices and today\'s change (needs internet, updated every hour, may be delayed)' };
const usesF = () => /^en-(US|LR|MM)$/i.test(navigator.language) || /-(US)$/i.test(navigator.language);
const SIZE_NAME = { 8: 'Jumbo thin', 9: 'Jumbo small', 10: 'Jumbo medium', 11: 'Jumbo big', 12: 'Jumbo huge',
  1: 'Pixel', 2: 'Classic small', 3: 'Classic medium', 4: 'Classic big' };
function addDrawn(kind, o = {}) {
  const it = Object.assign({ kind, secs: 0, useTheme: !('fg' in o), fg: TH().main, bg: COLORS.bg, rainbow: false,
    font: kind === 'text' || kind === 'stocks' ? 11 : 12, syms: '^GSPC,^DJI,^IXIC', effect: kind === 'countdown' ? 'snow' : kind === 'fx' ? 'fireworks' : 'none',
    text: kind === 'text' ? 'Happy Birthday!' : kind === 'countdown' ? 'Christmas' : '', date: '12-25',
    unit: usesF() ? 'F' : 'C', lat: '', lon: '', place: '', lead: '', fc: kind === 'weather', rainsoon: kind === 'weather', alerts: kind === 'weather',
    h24: !new Intl.DateTimeFormat(undefined, { hour: 'numeric' }).resolvedOptions().hour12 }, o);
  const el = baseEl(it);
  it.metaEl.textContent = META[kind]; it.metaEl.classList.add('desc');
  const wxMeta = () => it.lat === 'auto' ? `Weather where the screen is${card.place ? ': ' + card.place : ''}, guessed from its internet connection. Not right? Type your town.` : `Weather for ${it.place} (needs internet)`;
  const wxMark = () => { const need = !it.lat; el.classList.toggle('need', need); it.metaEl.textContent = need ? META.weather : wxMeta(); };
  const ed = document.createElement('div'); ed.className = 'edit';
  const colors = `<label>Color <input type="checkbox" data-k="useTheme"> Theme &nbsp; <input type="color" data-k="fg"> <input type="checkbox" data-k="rainbow"> Rainbow
      &nbsp; Background <input type="color" data-k="bg"></label>`;
  const opt = (l) => l.map(v => `<option value="${v}">${SIZE_NAME[v]}</option>`).join('');
  const sizes = opt(kind === 'weather' ? [11, 12, 3, 4] : kind === 'stocks' ? [10, 11, 2, 3]
    : kind === 'text' ? [9, 10, 11, 12, 1, 2, 3, 4] : [10, 11, 12, 2, 3, 4]);
  ed.innerHTML = kind === 'fx'
    ? `<label>Effect <select data-k="effect">${fxOptions().replace('<option value="none">None</option>', '')}</select>
       &nbsp; Background <input type="color" data-k="bg"></label>`
    : `
    ${kind === 'text' || kind === 'countdown' ? `<input type="text" maxlength="${kind === 'text' ? 120 : 21}" data-k="text" placeholder="${kind === 'text' ? 'Your message' : 'What for, e.g. Christmas'}">` : ''}
    ${kind === 'countdown' ? `<label>Date <input type="date" data-k="date"></label>
      <label>Start showing <input class="s" type="number" min="0" max="365" data-k="lead" placeholder="any" style="width:70px"> days before (blank = all year)</label>` : ''}
    ${kind === 'clock' ? `<label><input type="checkbox" data-k="h24"> 24-hour clock</label>` : ''}
    ${kind === 'stocks' ? '<div class="row syms">Loading…</div>' : ''}
    ${kind === 'weather' ? `<div class="flex"><input type="text" class="wq" placeholder="Your town" style="flex:1;min-width:0"><button class="chip wfind">Find</button></div>
      <button class="chip wauto">📍 Where the screen is</button>
      <div class="row wres"></div>
      <label>Units <select data-k="unit"><option value="F">°F</option><option value="C">°C</option><option value="A">Auto</option></select></label>
      <div class="wpages"><div><b>Screens</b> (tick what you want; each plays for the item's seconds)</div>
        <label><input type="checkbox" data-k="alerts"> Weather alerts (US only): watches and warnings before the weather; a warning also comes back every few items</label>
        ${WX_PAGES.map(([, k, t]) => `<label><input type="checkbox" data-k="${k}"> ${t}</label>`).join('')}</div>
      <button class="chip walert" type="button">⚠ See a sample alert</button>` : ''}
    ${colors}
    <label>Size <select data-k="font">${sizes}</select>
      &nbsp; Effect <select data-k="effect">${fxOptions(kind)}</select></label>`;
  el.appendChild(ed);
  ed.querySelectorAll('[data-k]').forEach(inp => {
    const k = inp.dataset.k;
    if (inp.type === 'checkbox') inp.checked = !!it[k];
    else if (k === 'date') inp.value = new Date().getFullYear() + '-' + it.date;
    else inp.value = it[k];
    inp.oninput = () => {
      it[k] = inp.type === 'checkbox' ? inp.checked : k === 'font' ? +inp.value : k === 'date' ? inp.value.slice(5) : inp.value;
      if (k === 'fg') { it.useTheme = false; const c = ed.querySelector('[data-k=useTheme]'); if (c) c.checked = false; }
      if (k === 'useTheme' && it.useTheme) { it.fg = TH().main; const c = ed.querySelector('[data-k=fg]'); if (c) c.value = it.fg; }
      if (k === 'lead') it.whenRefresh();
      el.querySelector('.name').textContent = label(it);
      if (k === 'unit') wxFetch(it);
      if (k === 'alerts') alertsFetch(it);
      if (WX_PAGES.some(p => p[1] === k)) it.focusPage = it[k] ? k : null;   // the preview jumps to a screen just ticked
      it.t0 = performance.now(); updateSpace();
    };
  });
  if (kind === 'weather') {
    ed.querySelector('.walert').onclick = () => {   // 9 s of made-up alerts on the preview (the checkbox turns on)
      if (!it.alerts) { it.alerts = true; ed.querySelector('[data-k=alerts]').checked = true; alertsFetch(it); }
      it.alertSample = performance.now() + 9000; it.t0 = performance.now(); select(it);
    };
    const q = ed.querySelector('.wq'), res = ed.querySelector('.wres');
    q.value = it.place;
    const find = async () => {
      res.textContent = 'Looking…';
      try {
        const j = await (await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=' + encodeURIComponent(q.value.trim()))).json();
        res.textContent = j.results?.length ? '' : 'No town by that name. Try the nearest city.';
        for (const p of j.results || []) {
          const b = document.createElement('button'); b.className = 'chip';
          b.textContent = [p.name, p.admin1, p.country_code].filter(Boolean).join(', ');
          b.onclick = async () => {
            Object.assign(it, { lat: p.latitude.toFixed(3), lon: p.longitude.toFixed(3), place: p.name });
            q.value = p.name; res.textContent = '';
            wxMark(); wxRemember(it);
            el.querySelector('.name').textContent = label(it);
            await wxFetch(it); it.t0 = performance.now(); updateSpace();
          };
          res.appendChild(b);
        }
      } catch { res.textContent = 'Couldn\'t look that up. Is this phone online?'; }
    };
    ed.querySelector('.wfind').onclick = find;
    ed.querySelector('.wauto').onclick = () => {
      Object.assign(it, { lat: 'auto', lon: 'auto', place: '' });
      q.value = ''; res.textContent = '';
      wxMark(); wxRemember(it);
      el.querySelector('.name').textContent = label(it);
      wxFetch(it); it.t0 = performance.now(); updateSpace();
    };
    q.onkeydown = (e) => { if (e.key === 'Enter') find(); };
    wxMark();
    if (!it.lat && !loadingShow) setTimeout(() => q.focus(), 0);
    wxFetch(it);
  }
  if (kind === 'stocks') {
    const box = ed.querySelector('.syms');
    loadMarkets().then(list => {
      box.textContent = '';
      for (const m of list) {
        const b = document.createElement('button'); b.className = 'chip'; b.textContent = m.label;
        const on = () => it.syms.split(',').includes(m.sym);
        b.classList.toggle('on', on());
        b.onclick = () => {
          const l = it.syms.split(',').filter(Boolean), i = l.indexOf(m.sym);
          if (i < 0) l.push(m.sym); else l.splice(i, 1);
          it.syms = l.join(','); b.classList.toggle('on', on());
          el.querySelector('.name').textContent = label(it); it.t0 = performance.now(); updateSpace();
        };
        box.appendChild(b);
      }
      el.querySelector('.name').textContent = label(it);
    });
    stocksFetch(it);
  }
  select(it);
  updateSpace();
  return it;
}
$('addText').onclick = () => addDrawn('text');
$('addClock').onclick = () => addDrawn('clock');
$('addCount').onclick = () => addDrawn('countdown');
// A reminder is a countdown that shows only in the last N days (every year), with fireworks on the day.
$('addRem').onclick = () => {
  const d = new Date(Date.now() + 30 * 864e5), mm = String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0');
  const it = addDrawn('countdown', { text: '', date: mm, lead: '5', effect: 'sparkle' });
  it.el.querySelector('[data-k=text]').placeholder = "Who or what, e.g. Kevin's anniversary";
  it.el.querySelector('[data-k=text]').focus();
};
$('addFx').onclick = () => addDrawn('fx');
// A new weather item starts with the last town picked (the screen remembers it), else another weather item's town,
// else where the screen is (found from its internet connection). Only with none of those does it ask for a town.
function wxDefault() {
  const h = card.wxHome || '', [lat, lon, ...p] = h.split(',');
  if (h === 'auto') return { lat: 'auto', lon: 'auto', place: '' };
  if (lat && lon && !isNaN(+lat) && !isNaN(+lon)) return { lat, lon, place: p.join(',') };
  const other = items.find(i => i.kind === 'weather' && i.lat);
  if (other) return { lat: other.lat, lon: other.lon, place: other.place };
  return card.place ? { lat: 'auto', lon: 'auto', place: '' } : {};
}
function wxRemember(it) {
  const v = it.lat === 'auto' ? 'auto' : [it.lat, it.lon, clean(it.place)].join(',');
  if (v === card.wxHome || card.fw === '?') return;
  card.wxHome = v;
  api('/api/settings?wx=' + encodeURIComponent(v), { method: 'POST' }).catch(() => {});
}
$('addWx').onclick = () => addDrawn('weather', wxDefault());
$('addStocks').onclick = () => addDrawn('stocks');

const ready = () => items.filter(i => i.kind === 'pic' ? i.blob && !i.failed : i.kind === 'weather' ? i.lat : i.kind === 'stocks' ? i.syms : true);
function updateSpace() {
  const pics = ready().filter(i => i.kind === 'pic');
  const need = pics.reduce((s, i) => s + i.blob.size + 4096, 0);
  const cap = Math.max(1, card.total - 64 * 1024);
  const pct = Math.min(100, need / cap * 100);
  $('bar').querySelector('i').style.width = pct + '%';
  $('bar').classList.toggle('over', need > cap);
  $('space').textContent = card.total ? `${kb(need)} of ${kb(cap)} space${need > cap ? ': too much, remove some' : ''}` : '';
  $('send').disabled = !ready().length || need > cap || items.some(i => i.kind === 'pic' && !i.blob && !i.failed);
}

