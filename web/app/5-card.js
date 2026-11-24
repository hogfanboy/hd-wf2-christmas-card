// Talking to the screen (load/send the show), picture packs, software updates, brightness/time/night settings.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
// ---------- Talking to the screen ----------
async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error((await r.text()) || r.status);
  return r.headers.get('content-type')?.includes('json') ? r.json() : r.text();
}
// "1.0.0-beta2" < "1.0.0" (a release beats its betas); same rule as the card's verNum (online.ino).
const verKey = (s) => { const m = /^(\d+)\.(\d+)\.(\d+)(-[a-z]*(\d*))?/i.exec(String(s)) || [];
  return [+m[1] || 0, +m[2] || 0, +m[3] || 0, m[4] ? +(m[5] || 0) : 99]; };
const newer = (a, b) => { const x = verKey(a), y = verKey(b); for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return x[i] > y[i]; return false; };
function setConn(ok, name) {
  $('conn').className = 'pill ' + (ok ? 'on' : 'off');
  $('connText').textContent = ok ? name : 'Not connected';
  $('connHelp').hidden = ok;
}
async function loadInfo() {
  try {
    card = await api('/api/info');
    $('diag').textContent = cardReport(card);
    setConn(true, card.name || 'your screen');
    const n = card.items ?? card.files;
    $('about').innerHTML = [['Name', card.name], ['Playing', n + ' item' + (n === 1 ? '' : 's')], ['Location', card.place || 'not found yet'],
      ['Software', card.fw], ['App', location.pathname === '/local' ? 'built-in' : APP_BUILD], ['Space used', card.total ? kb(card.used) + ' of ' + kb(card.total) : '']]
      .filter(r => r[1]).map(([k, v]) => `<dt>${k}</dt><dd>${esc(v)}</dd>`).join('');
    $('bright').value = card.brightness;
    $('brightLabel').textContent = card.brightness + '%';
    if (card.theme !== undefined) themeIx = card.theme;
    drawThemes();
    showTime();
    checkUpdate();
    // Every time the page opens, start from what the screen is playing, so adding one reminder can't wipe the rest.
    if (showState === 'start') loadShow(true);
  } catch { setConn(false); }
  updateSpace();
}
loadInfo();

// Card report: the numbers someone helping a tester needs, readable, then the raw /api/info for anything else.
// Older firmware leaves the newer fields out; those lines are just skipped.
function cardReport(c) {
  const dur = (s) => s >= 86400 ? Math.floor(s / 86400) + ' d ' + Math.floor(s % 86400 / 3600) + ' h'
    : s >= 3600 ? Math.floor(s / 3600) + ' h ' + Math.floor(s % 3600 / 60) + ' min' : Math.floor(s / 60) + ' min';
  const sig = (r) => r + ' dBm (' + (r >= -60 ? 'strong' : r >= -70 ? 'good' : r >= -80 ? 'weak' : 'very weak') + ')';
  const rows = [
    ['Card', [c.name, c.id && 'id ' + c.id].filter(Boolean).join(', ')],
    ['Software', c.fw + ' · app ' + (location.pathname === '/local' ? 'built-in' : APP_BUILD)],
    ['Chip', c.chip],
    ['App size', c.app && kb(c.app) + ' of ' + kb(c.appMax)],
    ['Up', c.up !== undefined && dur(c.up) + ', started by ' + c.boot + (c.bootWhy ? ' (' + c.bootWhy + ')' : '')],
    ['Since power-on', c.crashes !== undefined && c.crashes + ' crashes, ' + c.quietRestarts + ' quiet restarts'],
    ['Whole life', c.boots && c.boots + ' starts, ' + c.lifeCrashes + ' crashes' + (c.lastCrash ? ' (last: ' + c.lastCrash + ')' : '')],
    ['Memory', c.block && 'free ' + kb(c.heap) + ', biggest piece ' + kb(c.block) + ', lowest ' + kb(c.minHeap)],
    ['WiFi', c.rssi ? sig(c.rssi) : ''],
    ['Show', (c.items ?? c.files) + ' items, ' + kb(c.used) + ' of ' + kb(c.total) + ' used'],
    ['Place', [c.place, c.tz].filter(Boolean).join(' · ')],
    ['Clock', c.time ? new Date(c.time * 1000).toISOString().replace('T', ' ').slice(0, 16) + ' UTC' + (c.night ? ', night mode on' : '') : 'not set'],
    ['Phone', navigator.userAgent],
    ['Report made', new Date().toString()],
  ].filter((r) => r[1]);
  return 'Pocket Jumbotron card report\n' + rows.map(([k, v]) => k + ': ' + v).join('\n') + '\n\nraw: ' + JSON.stringify(c);
}
$('diagCopy').onclick = async () => {
  try { await navigator.clipboard.writeText($('diag').textContent); $('diagCopy').textContent = 'Copied'; }
  catch { getSelection().selectAllChildren($('diag')); $('diagCopy').textContent = 'Selected: copy it'; }
  setTimeout(() => { $('diagCopy').textContent = 'Copy report'; }, 2500);
};
if (navigator.share) {
  $('diagShare').hidden = false;
  $('diagShare').onclick = () => navigator.share({ title: 'Pocket Jumbotron card report', text: $('diag').textContent }).catch(() => {});
}

// Load the current show back from the screen, so it can be changed instead of rebuilt.
// showState: start, loading, ok (loaded or the screen is empty), failed. Send warns unless it's ok.
let showState = 'start';
// Play order: in order (top to bottom) or shuffle (the screen picks a new random order every round, fw 1.0+).
let shuffle = false;
function setShuffle(v) {
  shuffle = v;
  $('ordIn').classList.toggle('on', !v); $('ordShuf').classList.toggle('on', v);
  $('ordHelp').textContent = v ? 'Items play in a random order, a new one every time through (never the same one twice in a row).'
    : 'Items play top to bottom, then start over.';
}
$('ordIn').onclick = () => setShuffle(false);
$('ordShuf').onclick = () => setShuffle(true);
$('loadShow').onclick = () => {
  if (items.length && !confirm('Throw away the changes on this page and load what the screen is playing?')) return;
  loadShow(false);
};
async function loadShow(auto) {
  const st = $('showStatus'); st.className = 'status'; st.style.margin = '0'; st.textContent = 'Reading what\'s on the screen…';
  showState = 'loading';
  const mine = auto ? items.slice() : [];          // added before the screen answered: keep them, after its items
  try {
    const all = (await api('/api/show')).split('\n').filter(Boolean);
    const ord = all.find(l => l.startsWith('order\t'));
    setShuffle(!!ord && ord.split('\t')[1] === 'shuffle');
    const lines = all.filter(l => l !== ord);
    if (!auto) items.slice().forEach(remove);
    if (!lines.length) { showState = 'ok'; st.textContent = 'The screen is empty. Add something below, then Send.'; return; }
    loadingShow = true;
    for (const ln of lines) {
      const f = ln.split('\t'), secs = +f[1] || 0;
      const op = f.length > 3 && f[f.length - 1].startsWith('@') ? parseOpts(f.pop()) : parseOpts('@');
      const before = items.length;
      if (f[0] === 'pic') {
        const r = await fetch('/api/file?name=' + encodeURIComponent(f[2]));
        if (r.ok) await addFile(new File([await r.blob()], f[2]), secs, true);
      } else {
        const o = { lead: op.lead, alerts: op.alerts, rzoom: op.rzoom || '6', radarAlways: !!op.radarAlways, ...Object.fromEntries(WX_KEYS.map((k) => [k, !!op[k]])), secs, fg: f[2] === 'rainbow' ? '#ffffff' : f[2] === 'theme' ? TH().main : '#' + f[2], useTheme: f[2] === 'theme', rainbow: f[2] === 'rainbow', bg: '#' + f[3], font: +f[4] >= 8 && +f[4] <= 12 ? +f[4] : +f[4] >= 5 && +f[4] <= 7 ? +f[4] - 3 : Math.max(1, Math.min(4, +f[4] || 3)), effect: FX[f[5]] ? f[5] : 'none' };
        if (f[0] === 'text') addDrawn('text', { ...o, text: f[6] });
        if (f[0] === 'clock') addDrawn('clock', { ...o, h24: f[6] === '24' });
        if (f[0] === 'countdown') addDrawn('countdown', { ...o, date: f[6], text: f[7] || '' });
        if (f[0] === 'fx' && o.effect !== 'none') addDrawn('fx', o);
        if (f[0] === 'weather') { const w = addDrawn('weather', { ...o, unit: f[6] || 'F', lat: f[7] || '', lon: f[8] || '', place: f[9] || '' }); if (!card.wxHome && w.lat) wxRemember(w); }
        if (f[0] === 'stocks') addDrawn('stocks', { ...o, syms: f[6] || '' });
        if (f[0] === 'dates' && !items.some(i => i.kind === 'dates')) {
          const d = addDates({ ...o, lead: o.lead || '7' });
          try { const r = await fetch('/api/dates'); if (r.ok) d.dates = datesFromText(await r.text()); } catch {}
          d.drawDates(); d.relabel();
          if (d.dates.length) { try { localStorage.setItem(datesKey(), JSON.stringify(d.dates)); } catch {} } else d.offerRestore();
        }
      }
      const added = items[before];
      if (added) { Object.assign(added.when, op.when); added.whenRefresh(); }
    }
    loadingShow = false;
    mine.forEach(it => { items.splice(items.indexOf(it), 1); items.push(it); $('items').appendChild(it.el); });
    showState = 'ok';
    st.className = 'status ok'; st.textContent = `✓ Your screen's ${lines.length} item${lines.length > 1 ? 's are' : ' is'} below. Add or change things, then Send.`;
    items.forEach(i => { const s = i.el.querySelector('.secs'); s.value = i.secs || ''; });
  } catch (err) { showState = 'failed'; st.className = 'status err'; st.textContent = 'Couldn\'t read what\'s on the screen (' + err.message + '). Tap Reload.'; }
  loadingShow = false;
}

const hex6 = (c) => c.replace('#', '').toUpperCase();
const clean = (s) => String(s).replace(/[\t\r\n]/g, ' ').trim();
function showLine(it, fileName) { return showLineBase(it, fileName) + '\t' + optsField(it); }
function showLineBase(it, fileName) {
  const sec = it.secs || 0;
  if (it.kind === 'pic') return ['pic', sec, fileName].join('\t');
  const fgOut = it.rainbow ? 'rainbow' : it.useTheme ? (card.theme !== undefined ? 'theme' : hex6(TH().main)) : hex6(it.fg);
  const common = [sec, fgOut, hex6(it.bg), it.font, it.effect];
  if (it.kind === 'text') return ['text', ...common, clean(it.text) || ' '].join('\t');
  if (it.kind === 'clock') return ['clock', ...common, it.h24 ? '24' : '12'].join('\t');
  if (it.kind === 'fx') return ['fx', ...common, ''].join('\t');
  if (it.kind === 'weather') return ['weather', ...common, it.unit, it.lat, it.lon, clean(it.place)].join('\t');
  if (it.kind === 'stocks') return ['stocks', ...common, it.syms].join('\t');
  if (it.kind === 'dates') return ['dates', ...common].join('\t');
  return ['countdown', ...common, it.date, clean(it.text)].join('\t');
}

$('send').onclick = async () => {
  const list = ready();
  if (showState === 'loading') { alert('Still reading what\'s on the screen. Try again in a moment.'); return; }
  if (showState === 'failed' && !confirm('This page couldn\'t read what the screen is playing, so Send will erase it. Send anyway?')) return;
  const noTown = items.filter(i => i.kind === 'weather' && !i.lat).length;
  if (noTown && !confirm(`${noTown === 1 ? 'A weather item has' : noTown + ' weather items have'} no town and won't be sent. Send the rest?`)) return;
  if (!confirm(`Replace everything on the screen with these ${list.length} item${list.length > 1 ? 's' : ''}?`)) return;
  const st = $('sendStatus'); st.className = 'status';
  $('send').disabled = true;
  try {
    st.textContent = 'Clearing old pictures…';
    for (let tries = 0; ; tries++) {          // the screen finishes its current frame first (503 = busy)
      const r = await fetch('/api/clear', { method: 'POST' });
      if (r.ok) break;
      if (r.status !== 503 || tries > 30) throw new Error(await r.text());
      await new Promise(res => setTimeout(res, 300));
    }
    const lines = shuffle ? ['order\tshuffle'] : [];
    let n = 0;
    const pics = list.filter(i => i.kind === 'pic');
    for (const it of list) {
      if (it.kind !== 'pic') { lines.push(showLine(it)); continue; }
      n++;
      st.textContent = `Sending picture ${n} of ${pics.length}…`;
      const name = String(n).padStart(2, '0') + '.' + it.ext;
      const fd = new FormData();
      fd.append('file', it.blob, name);
      await api('/api/upload', { method: 'POST', body: fd });
      lines.push(showLine(it, name));
    }
    // radar maps (one per town with the radar screen ticked, up to 3), for screens with radar software
    const radarTowns = [...new Map(list.filter(i => i.kind === 'weather' && i.radar && i.lat && i.lat !== 'auto' && !isNaN(+i.lat))
      .map(i => [radarKey(i.lat, i.lon, rdZoom(i)), i])).values()].slice(0, 3);
    if (radarTowns.length && !newer('1.1.0-beta1', card.fw)) {
      st.textContent = 'Making the radar map…';
      for (const [k, it] of radarTowns.entries()) {
        const m = await radarMap(it.lat, it.lon, rdZoom(it));
        if (!m) continue;                           // no map (OpenStreetMap unreachable): the radar shows on black
        const fd = new FormData();
        fd.append('file', new Blob([m]), `radar${k ? k + 1 : ''}.map`);
        await api('/api/upload', { method: 'POST', body: fd });
      }
    }
    const dl = list.find(i => i.kind === 'dates');   // the birthday list: its own file on the card, kept when the show changes
    if (dl && !newer('1.1.0-beta2', card.fw)) {
      st.textContent = 'Saving the birthday list…';
      await api('/api/dates', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: datesText(dl.dates) });
    }
    await api('/api/show', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: lines.join('\n') + '\n' });
    await api('/api/play', { method: 'POST' });
    st.className = 'status ok'; st.textContent = `Done! ${list.length} item${list.length > 1 ? 's are' : ' is'} now playing.`;
  } catch (err) {
    st.className = 'status err'; st.textContent = 'Something went wrong: ' + err.message + '. Try again.';
  }
  $('send').disabled = false;
  loadInfo();
};

// ---------- Picture packs from the Pocket Jumbotron website ----------
function packBox(p, getFile) {
  const el = document.createElement('div'); el.className = 'box'; el.style.marginTop = '0';
  el.innerHTML = `<b></b><div class="muted"></div><button class="secondary" style="margin-top:8px">Add these ${p.files.length}</button>`;
  el.querySelector('b').textContent = (p.cards ? '🎁 ' : '') + p.title;
  el.querySelector('.muted').textContent = p.about || '';
  const b = el.querySelector('button');
  b.onclick = async () => {
    b.disabled = true;
    for (let i = 0; i < p.files.length; i++) {
      b.textContent = `Adding ${i + 1} of ${p.files.length}…`;
      const s = (p.pics || {})[p.files[i]] || {};
      try { await addFile(await getFile(p.files[i]), 0, false, { fit: s.fit, format: s.format || p.format }); } catch (e) { console.warn(e); }
    }
    b.textContent = 'Added. Press Send to put them on the screen.';
  };
  $('packs').appendChild(el);
}
const forThisCard = (p) => !p.cards || p.cards.some(c => c.toLowerCase() === (card.name || '').toLowerCase());
$('packBtn').onclick = async () => {
  const st = $('packStatus'), box = $('packs');
  st.className = 'status'; st.textContent = 'Checking for new pictures…';
  box.innerHTML = ''; box.hidden = false;
  let shown = 0, problem = '';
  try {
    const r = await fetch(SITE + 'packs/manifest.json', { cache: 'no-store' });
    if (!r.ok) throw new Error(r.status);
    for (const p of (await r.json()).packs.filter(forThisCard)) {
      shown++;
      packBox(p, async (name) => {
        const f = await fetch(SITE + 'packs/' + p.id + '/' + encodeURIComponent(name));
        return new File([await f.blob()], name, { type: f.headers.get('content-type') || '' });
      });
    }
  } catch (err) { problem = "Couldn't reach the Pocket Jumbotron website. Is your WiFi connected to the internet?"; }
  st.className = problem ? 'status err' : 'status';
  st.textContent = problem || (shown ? '' : 'Nothing new right now.');
};

// ---------- Software updates from the website ----------
let latest = null;
async function checkUpdate() {
  try {
    latest = await (await fetch(SITE + 'firmware/info.json', { cache: 'no-store' })).json();
    if (card.fw && newer(latest.version, card.fw)) { $('updVer').textContent = 'version ' + latest.version; $('updBox').hidden = false; }
  } catch { /* no internet: no update check */ }
}
$('updBtn').onclick = async () => {
  const st = $('updStatus'); st.className = 'status';
  $('updBtn').disabled = true;
  try {
    st.textContent = 'Downloading…';
    const bin = await (await fetch(SITE + 'firmware/' + latest.app.file, { cache: 'no-store' })).blob();
    if (bin.size !== latest.app.size) throw new Error('download incomplete');
    st.textContent = 'Installing… keep this page open.';
    const fd = new FormData(); fd.append('firmware', bin, latest.app.file);
    await api('/api/update', { method: 'POST', body: fd });
    st.className = 'status ok'; st.textContent = 'Installed. The screen is restarting; this page reloads in 20 seconds.';
    setTimeout(() => location.reload(), 20000);
  } catch (err) { st.className = 'status err'; st.textContent = 'Update failed: ' + err.message; $('updBtn').disabled = false; }
};

// ---------- Brightness, time zone, night mode ----------
let brightTimer;
$('bright').oninput = (e) => {
  $('brightLabel').textContent = e.target.value + '%';
  clearTimeout(brightTimer);
  brightTimer = setTimeout(() => api('/api/brightness?v=' + e.target.value, { method: 'POST' }).catch(() => {}), 250);
};
for (const id of ['nf', 'nt']) $(id).innerHTML = '<option value="-1">—</option>' +
  Array.from({ length: 24 }, (_, h) => `<option value="${h}">${new Date(2000, 0, 1, h).toLocaleTimeString([], { hour: 'numeric' })}</option>`).join('');
$('au').onchange = async () => {
  const st = $('auStatus'); st.className = 'status';
  try { await api('/api/settings?au=' + ($('au').checked ? 1 : 0), { method: 'POST' }); st.className = 'status ok'; st.textContent = 'Saved.'; }
  catch (err) { st.className = 'status err'; st.textContent = 'Couldn\'t save: ' + err.message; }
};
function showTime() {
  if (card.autoUpdate !== undefined) { $('auBox').hidden = false; $('au').checked = card.autoUpdate; }
  $('nf').value = card.nightFrom ?? -1; $('nt').value = card.nightTo ?? -1; $('nl').value = card.nightLevel ?? 0;
  if (card.time === undefined) { $('timeInfo').textContent = 'Needs newer screen software.'; return; }
  $('timeInfo').textContent = card.time ? 'The screen\'s clock is set' + (card.night ? ' (night mode is on now).' : '.') : 'The screen hasn\'t got the time yet (needs internet).';
}
$('saveTime').onclick = async () => {
  const st = $('timeStatus'); st.className = 'status';
  try {
    await api(`/api/settings?${card.tzAuto === false ? 'tz=auto&' : ''}nf=${$('nf').value}&nt=${$('nt').value}&nl=${$('nl').value}`, { method: 'POST' });
    st.className = 'status ok'; st.textContent = 'Saved.';
    setTimeout(loadInfo, 3000);
  } catch (err) { st.className = 'status err'; st.textContent = 'Couldn\'t save: ' + err.message; }
};

$('forget').onclick = async () => {
  if (!confirm('Make the screen forget this WiFi?')) return;
  try { await api('/api/forget-wifi', { method: 'POST' }); } catch {}
  document.querySelector('main').innerHTML = '<h1>Pocket <span>Jumbotron</span></h1><div class="box">Done. The screen is restarting. Hold its button for 2 seconds to connect it to a WiFi network.</div>';
};

$('fw').onchange = async (e) => {
  const f = e.target.files[0]; if (!f) return;
  const st = $('fwStatus'); st.className = 'status';
  if (!confirm(`Install update "${f.name}"? The screen restarts when it's done.`)) { e.target.value = ''; return; }
  st.textContent = 'Installing… keep this page open.';
  try {
    const fd = new FormData(); fd.append('firmware', f, f.name);
    await api('/api/update', { method: 'POST', body: fd });
    st.className = 'status ok'; st.textContent = 'Update installed. The screen is restarting.';
  } catch (err) { st.className = 'status err'; st.textContent = 'Update failed: ' + err.message; }
  e.target.value = '';
};
