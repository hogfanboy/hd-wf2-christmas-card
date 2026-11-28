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
  : it.kind === 'dates' ? `🎂 Birthdays & dates (${it.dates.length})`
  : it.kind === 'stocks' ? 'Stocks: ' + (it.syms.split(',').filter(Boolean).map(s => MKT_LABEL[s] || s).join(', ') || 'pick some') : (it.lead !== '' && it.lead != null ? '🔔 ' : 'Countdown to ') + (isBaby(it.date) ? '👶 ' : '') + (it.date === 'next' ? 'the next holiday' : it.text || HOLIDAYS.find(h => h[0] === it.date)?.[1] || (isBaby(it.date) ? 'Baby' : '…')) + (/^\d\d-\d\d$/.test(it.date) ? ' · ' + md(it.date.replace('-', '')) : isBaby(it.date) ? ' · due ' + md(it.date.slice(10).replace('-', '')) : '');
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
  // Open card, top to bottom: picture + name, the item's own settings (added by the caller before .foot), then the
  // bottom bar: how long and when it plays (the When panel opens right under its button), then Move and Done.
  el.innerHTML = `<canvas width="128" height="64"></canvas><div><div class="name"></div><div class="meta"></div></div>
    <button class="chip del" title="Remove from the show" aria-label="Remove from the show">✕</button>
    <div class="foot">
      <div class="row plays"><span class="lbl">Plays</span><input class="s secs" type="number" min="0" max="600" placeholder="auto"><span class="lbl">seconds</span>
        <button class="chip whenBtn">⏰ When…</button></div>
      <div class="row ends"><button class="chip mv" data-mv="-1" title="Move up in the show" aria-label="Move up">▲</button><button class="chip mv" data-mv="1" title="Move down in the show" aria-label="Move down">▼</button>
        <span class="lbl">Move</span><span style="flex:1"></span><button class="chip editBtn">✓ Done</button></div>
    </div>`;
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
  el.querySelector('.plays').after(wp);
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
  if ((it.kind === 'countdown' || it.kind === 'dates') && it.lead !== '' && it.lead != null) o.push('lead=' + it.lead);
  if (it.kind === 'weather') for (const [opt, k, , tg] of WX_PAGES) if (it[k]) o.push(tg && it[tg[0]] ? tg[3] : k === 'radar' && it.radarAlways ? 'radar=always' : opt);   // radar=always: testing
  if (it.kind === 'weather' && it.radar && String(it.rzoom) !== '6') o.push('rzoom=' + it.rzoom);
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
    for (const [opt, ik, , tg] of WX_PAGES) {
      if (k === opt) o[ik] = true;
      if (tg && (k === tg[3] || (k === 'days16' && ik === 'fc'))) { o[ik] = true; o[tg[0]] = true; }   // days16: the old 14 days
    }
    if (k === 'alerts') o.alerts = true;
    if (k === 'rzoom') o.rzoom = v;
    if (k === 'radar' && v === 'always') o.radarAlways = true;
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
  const row = document.createElement('div'); row.className = 'edit';
  row.innerHTML = `<div class="lrow"><span class="lbl">Shape</span><span class="seg"><button class="chip" data-fit="fill">Fill</button><button class="chip" data-fit="fit">Fit</button></span></div>
    <div class="lrow"><span class="lbl">Colors</span><span class="seg"><button class="chip" data-fmt="gif">GIF</button><button class="chip" data-fmt="full">Full color</button></span></div>`;
  el.insertBefore(row, el.querySelector('.foot'));
  row.querySelectorAll('[data-fit]').forEach(b => b.onclick = async () => { it.fit = b.dataset.fit; marks(it); await build(it); select(it); });
  row.querySelectorAll('[data-fmt]').forEach(b => b.onclick = async () => { it.format = b.dataset.fmt; marks(it); await build(it); select(it); });
  try {
    it.src = await decode(file, it);
    if (it.vDur) {                                 // a video: pick which part to show
      it.file = file;
      const vr = document.createElement('div'); vr.className = 'edit';
      vr.innerHTML = `<label>Start at <input class="secs" type="number" min="0" step="0.5" data-v="vStart" value="0"> s</label>
        <label>Length <input class="secs" type="number" min="0.5" max="${VIDEO_MAX_S}" step="0.5" data-v="vLen" value="${+Math.min(10, it.vDur).toFixed(1)}"> s</label>
        <span class="muted" style="font-size:12px">of ${it.vDur.toFixed(1)} s (up to ${VIDEO_MAX_S} s; longer clips get fewer frames a second)</span>`;
      el.insertBefore(vr, el.querySelector('.foot'));
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
const META = { text: 'Message', clock: 'Shows the time (needs internet)', countdown: 'Counts the days (needs internet). With "days before" it only shows near the date, with fireworks on the day',
  dates: 'Birthdays and other dates, every year. Plays only when one is coming up (needs internet)',
  fx: 'Drawn by the screen itself: takes no space', weather: '⚠ Pick a town below. Weather without a town is not sent to the screen.',
  stocks: 'Market prices and today\'s change (needs internet, updated every hour, may be delayed)' };
const usesF = () => /^en-(US|LR|MM)$/i.test(navigator.language) || /-(US)$/i.test(navigator.language);
const SIZE_NAME = { 8: 'Jumbo thin', 9: 'Jumbo small', 10: 'Jumbo medium', 11: 'Jumbo big', 12: 'Jumbo huge',
  1: 'Pixel', 2: 'Classic small', 3: 'Classic medium', 4: 'Classic big' };
function addDrawn(kind, o = {}) {
  const it = Object.assign({ kind, secs: 0, useTheme: !('fg' in o), fg: TH().main, bg: COLORS.bg, rainbow: false,
    font: kind === 'text' || kind === 'stocks' ? 11 : 12, syms: '^GSPC,^DJI,^IXIC', effect: kind === 'countdown' ? 'auto' : kind === 'fx' ? 'fireworks' : kind === 'clock' || kind === 'weather' ? 'auto' : 'none',
    text: kind === 'text' ? 'Happy Birthday!' : kind === 'countdown' ? 'Christmas' : '', date: kind === 'countdown' ? 'next' : '12-25',
    unit: usesF() ? 'F' : 'C', lat: '', lon: '', place: '', lead: '', fc: false, rainsoon: false, alerts: false, rzoom: '6',
    h24: !new Intl.DateTimeFormat(undefined, { hour: 'numeric' }).resolvedOptions().hour12 }, o);
  const el = baseEl(it);
  it.metaEl.textContent = META[kind]; it.metaEl.classList.add('desc');
  const wxMeta = () => it.lat === 'auto' ? `Weather where the screen is${card.place ? ': ' + card.place : ''}, guessed from its internet connection. Not right? Type your town.` : `Weather for ${it.place} (needs internet)`;
  const wxMark = () => { const need = !it.lat; el.classList.toggle('need', need); it.metaEl.textContent = need ? META.weather : wxMeta(); };
  const ed = document.createElement('div'); ed.className = 'edit';
  const colors = `<div class="lrow"><span class="lbl">Color</span><label class="nw"><input type="checkbox" data-k="useTheme"> Theme</label>
      <input type="color" data-k="fg" title="Your own color"><label class="nw"><input type="checkbox" data-k="rainbow"> Rainbow</label></div>
    <label>Background <input type="color" data-k="bg"></label>`;
  const opt = (l) => l.map(v => `<option value="${v}">${SIZE_NAME[v]}</option>`).join('');
  const sizes = opt(kind === 'weather' ? [11, 12, 3, 4] : kind === 'stocks' ? [10, 11, 2, 3]
    : kind === 'text' ? [9, 10, 11, 12, 1, 2, 3, 4] : [10, 11, 12, 2, 3, 4]);
  const dateKinds = Object.entries(DATE_KINDS).map(([k, v]) => `<option value="${k}">${v}</option>`).join('');
  ed.innerHTML = kind === 'fx'
    ? `<label>Effect <select data-k="effect">${fxOptions().replace('<option value="none">None</option>', '')}</select></label>
       <label>Background <input type="color" data-k="bg"></label>`
    : `
    ${kind === 'text' || kind === 'countdown' ? `<input type="text" maxlength="${kind === 'text' ? 120 : 40}" data-k="text" placeholder="${kind === 'text' ? 'Your message' : 'What for, e.g. Christmas'}">` : ''}
    ${kind === 'countdown' ? `<label>Holiday <select class="hol"><option value="">My own date</option><option value="next">The next holiday (whichever comes next)</option><option value="baby">👶 Baby due date</option>
        ${HOLIDAYS.map(h => `<option value="${h[0]}">${h[1]}</option>`).join('')}</select></label>
      <label class="owndate"><span class="dlbl">Date</span> <input type="date" data-k="date"></label>
      <div class="muted babynote" style="font-size:13px">Counts down the days with a stork, baby footprints and rattles. On the due day the stork flies in, and after it the screen says "any day now" for 3 weeks. Type a name above if you like, e.g. Baby Emma.</div>
      <label>Start showing <input class="s" type="number" min="0" max="365" data-k="lead" placeholder="any" style="width:70px"> days before (blank = all year)</label>` : ''}
    ${kind === 'dates' ? `<div class="dlist"></div>
      <div class="flex"><input type="text" class="dname" maxlength="30" placeholder="Name, e.g. Kevin" style="flex:1;min-width:0"></div>
      <div class="flex"><select class="dmon"></select><select class="dday"></select><input class="dyear s" type="number" min="1880" max="2100" placeholder="Year (optional)" style="width:130px"><select class="dkind">${dateKinds}</select><button class="chip dadd">+ Add</button></div>
      <details><summary>Paste a whole list</summary><textarea class="dpaste" rows="5" style="width:100%;box-sizing:border-box" placeholder="One per line, for example:&#10;Kevin 3/14/1985&#10;Mom &amp; Dad anniversary 6/20/1980&#10;Grandma March 2"></textarea><button class="chip dpasteGo">Add these</button></details>
      <div class="flex"><button class="chip dsave">💾 Save to a file</button><label class="chip">📂 Load from a file<input type="file" class="dloadFile" accept=".txt,.csv,text/plain,text/csv" hidden></label></div>
      <div class="muted dnote" style="font-size:13px">With a year, the screen counts it: "Kevin turns 41". Tap a name to change it.</div>
      <label>Start showing <input class="s" type="number" min="0" max="60" data-k="lead" placeholder="7" style="width:70px"> days before (each date gets its own screen; fireworks on the day)</label>` : ''}
    ${kind === 'clock' ? `<label><input type="checkbox" data-k="h24"> 24-hour clock</label>` : ''}
    ${kind === 'stocks' ? '<div class="row syms">Loading…</div>' : ''}
    ${kind === 'weather' ? `<div class="flex"><input type="text" class="wq" placeholder="Your town" style="flex:1;min-width:0"><button class="chip wfind">Find</button></div>
      <button class="chip wauto">📍 Where the screen is</button>
      <div class="row wres"></div>
      <label>Units <select data-k="unit"><option value="F">°F</option><option value="C">°C</option><option value="A">Auto</option></select></label>
      <div class="wpages"><div><b>Screens</b> (tick what you want; each plays for the item's seconds, longer when it slides across)</div>
        <label><input type="checkbox" data-k="alerts"> Weather alerts (US only): watches and warnings before the weather; a warning also comes back every few items</label>
        ${WX_PAGES.map(([, k, t, tg]) => `<label><input type="checkbox" data-k="${k}"> ${t}${tg ? ` <select class="wtg" data-k="${tg[0]}"><option value="">${tg[1]}</option><option value="1">${tg[2]}</option></select>` : ''}</label>`).join('')}</div>
      <button class="chip walert" type="button">⚠ See a sample alert</button>` : ''}
    ${colors}
    <label>Size <select data-k="font">${sizes}</select></label>
    <label>Effect <select data-k="effect">${fxOptions(kind)}</select></label>`;
  el.insertBefore(ed, el.querySelector('.foot'));
  ed.querySelectorAll('[data-k]').forEach(inp => {
    const k = inp.dataset.k;
    if (inp.type === 'checkbox') inp.checked = !!it[k];
    else if (inp.classList.contains('wtg')) inp.value = it[k] ? '1' : '';
    else if (k === 'date') inp.value = /^\d\d-\d\d$/.test(it.date) ? new Date().getFullYear() + '-' + it.date : isBaby(it.date) ? it.date.slice(5) : '';
    else inp.value = it[k];
    inp.oninput = () => {
      it[k] = inp.type === 'checkbox' ? inp.checked : inp.classList.contains('wtg') ? inp.value === '1' : k === 'font' ? +inp.value : k === 'date' ? (isBaby(it.date) ? (inp.value ? 'baby:' + inp.value : it.date) : inp.value.slice(5)) : inp.value;
      if (k === 'fg') { it.useTheme = false; const c = ed.querySelector('[data-k=useTheme]'); if (c) c.checked = false; }
      if (k === 'useTheme' && it.useTheme) { it.fg = TH().main; const c = ed.querySelector('[data-k=fg]'); if (c) c.value = it.fg; }
      if (k === 'lead') { it.whenRefresh(); it.drawDates?.(); }
      el.querySelector('.name').textContent = label(it);
      if (k === 'unit') wxFetch(it);
      if (k === 'alerts') alertsFetch(it);
      const pg = WX_PAGES.find(p => p[1] === k || p[3]?.[0] === k || (k === 'rzoom' && p[1] === 'radar'));   // the preview jumps to a screen just ticked or toggled
      if (pg) {
        if ((pg[3]?.[0] === k || k === 'rzoom') && !it[pg[1]]) { it[pg[1]] = true; const c = ed.querySelector(`[data-k=${pg[1]}]`); if (c) c.checked = true; }   // picking 24 hours ticks Hourly
        it.focusPage = it[pg[1]] ? pg[1] : null;
      }
      it.t0 = performance.now(); updateSpace();
    };
  });
  const hs = ed.querySelector('.hol');
  if (hs) {                                        // holiday picker: moving dates (Thanksgiving, Easter...) are worked out by the screen
    const show = () => {
      const baby = isBaby(it.date), own = baby || (it.date !== 'next' && !HOLIDAYS.some(h => h[0] === it.date));
      hs.value = baby ? 'baby' : own ? '' : it.date;
      ed.querySelector('.owndate').style.display = own ? '' : 'none';
      ed.querySelector('.dlbl').textContent = baby ? 'Due date' : 'Date';
      ed.querySelector('.babynote').style.display = baby ? '' : 'none';
      ed.querySelector('[data-k=text]').style.display = it.date === 'next' ? 'none' : '';
    };
    hs.onchange = () => {
      const k = hs.value, h = HOLIDAYS.find(x => x[0] === k);
      if (/^\d\d-\d\d$/.test(it.date)) it.ownDate = it.date;
      if (isBaby(it.date)) it.babyDate = it.date;
      if (k === 'baby') {                              // a baby's due date: once, with its year (default: about 6 months away)
        const d = new Date(Date.now() + 182 * 864e5), p2 = (v) => String(v).padStart(2, '0');
        it.date = it.babyDate || `baby:${d.getFullYear()}-${p2(d.getMonth() + 1)}-${p2(d.getDate())}`;
        if (!it.text || it.text === 'Christmas') it.text = 'Baby';
        it.effect = 'baby';
      } else {
        it.date = k || it.ownDate || '12-25';
        if (k) { it.text = ''; it.effect = 'auto'; }      // blank label: the screen shows the holiday's own name
        else if (it.effect === 'baby') it.effect = 'snow';
      }
      ed.querySelector('[data-k=text]').value = it.text;
      ed.querySelector('[data-k=effect]').value = it.effect;
      ed.querySelector('[data-k=date]').value = /^\d\d-\d\d$/.test(it.date) ? new Date().getFullYear() + '-' + it.date : isBaby(it.date) ? it.date.slice(5) : '';
      show(); el.querySelector('.name').textContent = label(it); it.t0 = performance.now(); updateSpace();
    };
    show();
  }
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
  if (!loadingShow) requestAnimationFrame(() =>     // bring the new item's editor into view (a tall one: its top)
    el.scrollIntoView({ behavior: 'smooth', block: el.offsetHeight > innerHeight * 0.9 ? 'start' : 'center' }));
  return it;
}
$('addText').onclick = () => addDrawn('text');
$('addClock').onclick = () => addDrawn('clock');
$('addCount').onclick = () => addDrawn('countdown');
$('addFx').onclick = () => addDrawn('fx');
// ---------- Birthdays & dates: one item holds the whole list (the card's /dates.txt, apart from the show) ----------
// The list is safe three ways: on the card in its own file (Send and Clear leave it alone), on this phone (saved on
// every change, offered back if the card comes up empty), and as a file the user saves ("Save to a file").
const DATE_KINDS = { b: 'Birthday', a: 'Anniversary', o: 'Other' };
const MONTHS = ['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct', 'nov', 'dec'];
const datesKey = () => 'pjDates:' + (card.id || 'card');
// One typed or pasted line -> { name, date: 'MM-DD', year, kind } or null. Reads "Kevin 3/14/1985", "Mom & Dad
// anniversary 6-20-1980", "Grandma March 2", "1985-03-14 Kevin" and spreadsheet rows / our own file ("Kevin, 03/14/1985, birthday").
function parseDateLine(line) {
  let s = String(line).replace(/[\t,;"]+/g, ' ').trim();
  if (!s || s.startsWith('#')) return null;
  const kind = /\banniv(ersary)?\b/i.test(s) ? 'a' : /\b(other|event)\b/i.test(s) ? 'o' : 'b';
  s = s.replace(/\b(anniv(ersary)?|birthday|b-?day|other|event)\b/ig, ' ');
  let r, m, d, y = '';
  if ((r = s.match(/\b(\d{4})-(\d{1,2})-(\d{1,2})\b/))) { y = r[1]; m = +r[2]; d = +r[3]; }
  else if ((r = s.match(/\b(\d{1,2})[/\-.](\d{1,2})(?:[/\-.](\d{2,4}))?\b/))) { m = +r[1]; d = +r[2]; y = r[3] || ''; }
  else if ((r = s.match(/\b(jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\.?\s+(\d{1,2})(?:st|nd|rd|th)?\b(?:\s+(\d{4})\b)?/i))) { m = MONTHS.indexOf(r[1].toLowerCase()) + 1; d = +r[2]; y = r[3] || ''; }
  if (!r || !(m >= 1 && m <= 12 && d >= 1 && d <= 31)) return null;
  if (y.length === 2) y = String(+y > new Date().getFullYear() % 100 ? 1900 + +y : 2000 + +y);
  if (y && (+y < 1880 || +y > new Date().getFullYear())) y = '';
  const name = s.replace(r[0], ' ').replace(/\s+/g, ' ').trim().slice(0, 30);
  return name ? { name, date: String(m).padStart(2, '0') + '-' + String(d).padStart(2, '0'), year: y, kind } : null;
}
// Days until a date, and how many years it will be then (0 = no year given).
function dateNext(e) {
  const n = countdownDays(e.date).n, when = new Date(Date.now() + n * 864e5);
  return { n, years: e.year ? when.getFullYear() - +e.year : 0 };
}
const datesSorted = (list) => list.map(e => ({ ...e, ...dateNext(e) })).sort((a, b) => a.n - b.n || a.name.localeCompare(b.name));
const datesText = (list) => list.map(e => [e.date, e.year || '', e.kind, clean(e.name)].join('\t')).join('\n') + (list.length ? '\n' : '');
function datesFromText(txt) {               // the card's /dates.txt
  return txt.split('\n').map(l => l.replace('\r', '').split('\t')).filter(f => f.length >= 4 && /^\d\d-\d\d$/.test(f[0]))
    .map(f => ({ date: f[0], year: f[1], kind: DATE_KINDS[f[2]] ? f[2] : 'b', name: f.slice(3).join(' ') }));
}
const datesFile = (list) => '# Pocket Jumbotron birthdays & dates. One per line: name, date (month/day/year born), kind\n'
  + datesSorted(list).sort((a, b) => a.date.localeCompare(b.date)).map(e => `${e.name}, ${+e.date.slice(0, 2)}/${+e.date.slice(3)}${e.year ? '/' + e.year : ''}, ${DATE_KINDS[e.kind].toLowerCase()}`).join('\n') + '\n';
function datesLines(e, font) {             // what the card shows for one date: [big, line 1, line 2]
  const nm = e.name.toUpperCase(), y = e.years;
  const fit = (...t) => { smallF(); return t.find(s => s && M(s) <= W - 2) || t[t.length - 1]; };
  if (!e.n) {                               // the day: the name big; a name too long for that goes under "TODAY!"
    const hi = e.kind === 'b' ? 'HAPPY BIRTHDAY' : e.kind === 'a' ? 'HAPPY ANNIVERSARY' : 'TODAY', yr = y ? (e.kind === 'b' ? 'TURNS ' + y : e.kind === 'a' ? y + ' YEARS' : '') : '';
    fitBig(font, nm, W - 2, 0);
    if (M(nm) <= W - 2) return [nm, hi, yr];
    return ['TODAY!', e.kind === 'o' ? '' : hi, fit(yr ? nm + ' ' + yr : '', nm)];
  }
  return [String(e.n), e.n === 1 ? 'DAY UNTIL' : 'DAYS UNTIL', e.kind === 'b' ? fit(y ? `${nm} TURNS ${y}` : '', nm + "'S BIRTHDAY", nm + "'S BDAY", nm)
    : e.kind === 'a' ? fit(nm + "'S ANNIVERSARY", y ? `${nm} ${y} YRS` : '', nm + "'S ANNIV", nm) : nm];
}
function addDates(o = {}) {
  const it = addDrawn('dates', Object.assign({ effect: 'sparkle', lead: '7', dates: [] }, o));
  const ed = it.el.querySelector('.edit:not(.when)'), list = ed.querySelector('.dlist'), note = ed.querySelector('.dnote');
  const f = (c) => ed.querySelector(c);
  f('.dmon').innerHTML = MONTHS.map((m, i) => `<option value="${i + 1}">${m[0].toUpperCase() + m.slice(1)}</option>`).join('');
  f('.dday').innerHTML = Array.from({ length: 31 }, (_, i) => `<option>${i + 1}</option>`).join('');
  const changed = () => {
    try { localStorage.setItem(datesKey(), JSON.stringify(it.dates)); } catch {}
    it.relabel(); it.t0 = performance.now(); draw(); updateSpace();
  };
  const draw = () => {
    const lead = it.lead === '' ? 7 : +it.lead;
    list.innerHTML = it.dates.length ? '' : '<div class="muted">No dates yet. Add one below, or paste a whole list.</div>';
    for (const e of datesSorted(it.dates)) {
      const row = document.createElement('div'); row.className = 'drow';
      const when = new Date(2000, +e.date.slice(0, 2) - 1, +e.date.slice(3)).toLocaleDateString([], { month: 'short', day: 'numeric' });
      row.innerHTML = `<span><b></b> · ${when}${e.years ? ' · ' + (e.kind === 'b' ? 'turns ' + e.years : e.years + ' years') : ''}${e.kind === 'a' ? ' · anniversary' : ''} · <span class="${e.n <= lead ? 'soon' : 'muted'}">${e.n ? 'in ' + e.n + ' day' + (e.n > 1 ? 's' : '') : 'today!'}</span></span><button class="chip" aria-label="Remove">✕</button>`;
      row.querySelector('b').textContent = e.name;
      row.querySelector('b').onclick = () => {   // tap a name to change it: back into the boxes
        f('.dname').value = e.name; f('.dmon').value = +e.date.slice(0, 2); f('.dday').value = +e.date.slice(3); f('.dyear').value = e.year; f('.dkind').value = e.kind;
        it.dates.splice(it.dates.findIndex(x => x.name === e.name && x.date === e.date), 1); changed(); f('.dname').focus();
      };
      row.querySelector('button').onclick = () => { it.dates.splice(it.dates.findIndex(x => x.name === e.name && x.date === e.date), 1); changed(); };
      list.appendChild(row);
    }
  };
  const addOne = (e) => {                   // same name and date again = no duplicate
    if (!it.dates.some(x => x.name.toLowerCase() === e.name.toLowerCase() && x.date === e.date)) { it.dates.push(e); return true; }
    return false;
  };
  f('.dadd').onclick = () => {
    const name = f('.dname').value.trim().slice(0, 30), y = f('.dyear').value.trim();
    if (!name) { f('.dname').focus(); return; }
    addOne({ name, date: String(f('.dmon').value).padStart(2, '0') + '-' + String(f('.dday').value).padStart(2, '0'), year: /^\d{4}$/.test(y) ? y : '', kind: f('.dkind').value });
    f('.dname').value = ''; f('.dyear').value = ''; changed(); f('.dname').focus();
  };
  const addText = (txt, where) => {
    const lines = txt.split(/\r?\n/).filter(l => l.trim() && !l.trim().startsWith('#')), bad = [];
    let n = 0;
    for (const l of lines) { const e = parseDateLine(l); if (!e) bad.push(l.trim()); else if (addOne(e)) n++; }
    changed();
    note.textContent = `Added ${n} date${n === 1 ? '' : 's'}${where}.` + (bad.length ? ` Couldn't read ${bad.length}: ${bad.slice(0, 3).join(' | ')}${bad.length > 3 ? ' …' : ''} (try "Name 3/14/1985").` : '');
  };
  f('.dpasteGo').onclick = () => { addText(f('.dpaste').value, ''); f('.dpaste').value = ''; };
  f('.dsave').onclick = () => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([datesFile(it.dates)], { type: 'text/plain' }));
    a.download = 'pocket-jumbotron-dates.txt'; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    note.textContent = 'Saved. Keep the file somewhere safe; "Load from a file" puts the list back on any screen.';
  };
  f('.dloadFile').onchange = async (ev) => { const file = ev.target.files[0]; if (file) addText(await file.text(), ' from ' + file.name); ev.target.value = ''; };
  it.offerRestore = () => {                 // the card has no list but this phone remembers one
    let saved = [];
    try { saved = JSON.parse(localStorage.getItem(datesKey()) || '[]'); } catch {}
    if (it.dates.length || !saved.length) return;
    note.innerHTML = `This phone remembers <b>${saved.length}</b> date${saved.length > 1 ? 's' : ''} from before. <button class="chip drestore">Put them back</button>`;
    note.querySelector('.drestore').onclick = () => { saved.forEach(addOne); changed(); note.textContent = `Put back ${saved.length} dates. Tap Send to save them on the screen.`; };
  };
  it.drawDates = draw;
  it.relabel = () => { it.el.querySelector('.name').textContent = label(it); if (selected === it) $('bigLabel').textContent = 'Preview: ' + label(it); };
  draw();
  return it;
}
// A new list starts from what the screen already has (a list stays on the card even after its item is removed).
$('addDates').onclick = async () => {
  if (items.some(i => i.kind === 'dates')) { openItem(items.find(i => i.kind === 'dates')); return; }   // one list is enough
  const it = addDates();
  try { const r = await fetch('/api/dates'); if (r.ok) { it.dates = datesFromText(await r.text()); it.drawDates(); updateSpace(); } } catch {}
  it.relabel();
  it.offerRestore();
};
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
$('addWx').onclick = () => addDrawn('weather', { alerts: true, hourly: true, hours24: true, fc: true, days10: true, effect: 'auto', ...wxDefault() });   // new: alerts, 24 hours, 10 days, match the weather (theme color + Jumbo huge like every new item)
$('addStocks').onclick = () => addDrawn('stocks');

const ready = () => items.filter(i => i.kind === 'pic' ? i.blob && !i.failed : i.kind === 'weather' ? i.lat : i.kind === 'stocks' ? i.syms : i.kind === 'dates' ? i.dates.length : true);
// Which date the dates item is showing at t (ms) in the preview: those within the lead days, else the soonest one.
function datesShown(it, t) {
  const lead = it.lead === '' ? 7 : +it.lead, all = datesSorted(it.dates || []), soon = all.filter(e => e.n <= lead).slice(0, 6), show = soon.length ? soon : all.slice(0, 1);
  return show.length ? show[Math.floor(t / ((it.secs || 6) * 1000)) % show.length] : null;
}
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

