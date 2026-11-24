// Color tune: test screens on the panel (the card's tune.ino) to set the color balance at each brightness level.
// Part of the card app: tools/build-web.mjs joins web/app/*.js in name order into one <script> in web/index.html.
const TUNE_LEVELS = ['Night 10%', 'Night 25%', 'Normal 25%', 'Normal 50%', 'Normal 100%'];
const TUNE_HELP = ['night 10% with brightness 50%', 'night 25% with brightness 50%, or brightness 12%',
  'brightness 25%', 'brightness 50%', 'brightness 100%'];
let tune = null, tuneLv = 0, tuneTimer;
function tuneShow(t) {
  tune = t;
  $('tuneLv').innerHTML = TUNE_LEVELS.map((n, i) => `<button class="secondary tuneLvB${t.on && t.level === i ? ' on' : ''}" data-i="${i}">${n}</button>`).join('');
  $('tuneLv').querySelectorAll('button').forEach(b => b.onclick = () => { tuneLv = +b.dataset.i; tuneSend('level=' + tuneLv + '&pattern=' + $('tunePat').value + '&color=' + $('tuneCol').value); });
  const g = t.gain[t.on && t.level >= 0 ? t.level : tuneLv];
  for (const [i, c] of ['R', 'G', 'B'].entries()) { $('tune' + c).value = g[i]; $('tune' + c + 'v').textContent = g[i]; }
  if (t.on && t.pattern) $('tunePat').value = t.pattern;
  if (t.on) $('tuneCol').value = [...$('tuneCol').options].some(o => o.value === t.color) ? t.color : 'ffffff';
  $('tuneTable').innerHTML = (t.on && t.level >= 0 ? `Tuning <b>${TUNE_LEVELS[t.level]}</b> (${TUNE_HELP[t.level]}). ` : 'Pick a level to start. ') +
    'Red / green / blue now: ' + t.gain.map((g, i) => `${TUNE_LEVELS[i]} <b>${g.join('/')}</b>`).join(', ') + '.' +
    (t.saved ? '' : ' <b>Not saved yet.</b>');
}
async function tuneSend(q) {
  const st = $('tuneStatus'); st.className = 'status';
  try { tuneShow(await api('/api/cal?' + q, { method: 'POST' })); st.textContent = ''; }
  catch (err) { st.className = 'status err'; st.textContent = 'Couldn\'t reach the screen: ' + err.message; }
}
async function tuneLoad() {
  $('tuneBox').hidden = card.hwMin === undefined;
  if ($('tuneBox').hidden) return;
  try { tuneShow(await api('/api/cal')); } catch {}
}
$('tuneRow').innerHTML = 'ABCDE'.split('').map((r, i) => `<option value="${i}"${i === 2 ? ' selected' : ''}>${r}</option>`).join('');
$('tuneCN').innerHTML = Array.from({ length: 7 }, (_, i) => `<option value="${i}"${i === 3 ? ' selected' : ''}>${i + 1}</option>`).join('');
const tuneOn = () => tune?.on && tune.level >= 0;
$('tunePat').onchange = $('tuneCol').onchange = () => tuneSend('level=' + (tuneOn() ? tune.level : tuneLv) + '&pattern=' + $('tunePat').value + '&color=' + $('tuneCol').value);
$('tunePick').onclick = () => {
  if (!tuneOn()) return tuneSend('level=' + tuneLv + '&pattern=1&color=' + $('tuneCol').value);
  tuneSend('pick=' + $('tuneRow').value + $('tuneCN').value);
  $('tuneRow').value = 2; $('tuneCN').value = 3;              // the picked patch is the new middle
};
for (const c of ['R', 'G', 'B']) $('tune' + c).oninput = (e) => {
  $('tune' + c + 'v').textContent = e.target.value;
  clearTimeout(tuneTimer);
  tuneTimer = setTimeout(() => tuneSend((tuneOn() ? '' : 'level=' + tuneLv + '&pattern=' + $('tunePat').value + '&') + c.toLowerCase() + '=' + e.target.value), 200);
};
$('tuneSave').onclick = () => tuneSend('save=1');
$('tuneReset').onclick = () => tuneSend((tuneOn() ? '' : 'level=' + tuneLv + '&pattern=' + $('tunePat').value + '&') + 'reset=1');
$('tuneStop').onclick = () => tuneSend('pattern=0');
$('tuneBox').ontoggle = () => { if (!$('tuneBox').open && tune?.on) tuneSend('pattern=0'); };   // folding it away ends the test
