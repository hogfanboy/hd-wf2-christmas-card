// Builds everything a fresh card needs and puts it where the website's install page can fetch it:
//   docs/firmware/pocket-jumbotron-install.bin  one image for address 0x0 (bootloader, partitions, blank NVS,
//                                               OTA data, app, LittleFS with the first show, see step 2)
//   docs/firmware/pocket-jumbotron-app.bin      app only (for wireless updates)
//   docs/firmware/info.json                     version, sizes, sha256
// Usage: node tools/build-install.mjs [content folders...]   (CI passes its checkout of the private picture repo)
//   --first-show-only: just build + list the first show (firmware/build/install-fs), no compile or image.
// Runs on Windows (this PC) and Linux (GitHub Actions). Needs arduino-cli + the ESP32 core installed.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PIC, byName, readPackMeta } from './pack-meta.mjs';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const win = process.platform === 'win32';
const exe = (n) => (win ? n + '.exe' : n);
const run = (cmd, args, opts = {}) => { console.log('>', path.basename(cmd), args.join(' ').slice(0, 160)); return execFileSync(cmd, args, { stdio: 'inherit', ...opts }); };

const CORE = '3.3.13';
const FQBN = 'esp32:esp32:esp32s3:FlashMode=dio,FlashSize=8M,CDCOnBoot=cdc,USBMode=hwcdc,PSRAM=disabled';
const arduinoCli = process.env.ARDUINO_CLI || (win ? path.join(os.homedir(), 'AppData/Local/Programs/arduino-cli/arduino-cli.exe') : 'arduino-cli');
const a15 = process.env.ARDUINO15 || (win ? path.join(os.homedir(), 'AppData/Local/Arduino15') : path.join(os.homedir(), '.arduino15'));
const findTool = (name, bin) => {
  const dir = path.join(a15, 'packages/esp32/tools', name);
  const hits = [];
  const walk = (d, depth) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isFile() && e.name === exe(bin)) hits.push(p); else if (e.isDirectory() && depth < 3) walk(p, depth + 1); } };
  walk(dir, 0);
  if (!hits.length) throw new Error(`can't find ${bin} under ${dir}`);
  return hits.sort().pop();
};

const sketch = path.join(root, 'firmware/pocket_jumbotron');
const build = path.join(root, 'firmware/build');
const appOut = path.join(build, 'pocket_jumbotron');
const out = path.join(root, 'docs/firmware');
fs.mkdirSync(out, { recursive: true });

const firstOnly = process.argv.includes('--first-show-only');
process.argv = process.argv.filter(a => a !== '--first-show-only');
// 1. Web pages -> web_index.h, then compile.
if (!firstOnly) {
run(process.execPath, [path.join(root, 'tools/build-web.mjs')]);
run(arduinoCli, ['compile', '-b', FQBN, '--build-property', 'upload.maximum_size=1835008', '--output-dir', appOut, sketch]);   // our app slot is 1.75 MB (partitions.csv)
}
const version = /FW_VERSION = "([^"]+)"/.exec(fs.readFileSync(path.join(sketch, 'pocket_jumbotron.ino'), 'utf8'))[1];

// 2. The first show -> LittleFS image (must match partitions.csv: spiffs @0x390000, 0x460000):
//    clock, weather where the screen is (found by its internet address) + 5-day forecast, then the pictures from
//    the content folders: loose pictures, then every pack whose pack.json has "install": true (pack list order,
//    then play order). Per picture, pack.json "pictures": { "<file>": { "install": false } } leaves it off new
//    cards and { "dates": "1115-0106" } shows it only between those days. All set in the pack builder; the
//    "Holiday Starter" pack holds our own greetings. No install pack at all (a local build without the content
//    repo): the generated greetings from make-starter.mjs.
const fsDir = path.join(build, 'install-fs'), gifs = path.join(fsDir, 'gifs'), stage = path.join(build, 'install-stage');
fs.rmSync(fsDir, { recursive: true, force: true }); fs.rmSync(stage, { recursive: true, force: true });
fs.mkdirSync(gifs, { recursive: true }); fs.mkdirSync(stage, { recursive: true });
const picked = [];                                   // { src, dates }
const isPic = (p) => PIC.test(p) && fs.statSync(p).isFile();
for (const src of process.argv.slice(2).map(p => path.resolve(p)).filter(p => fs.existsSync(p))) {
  for (const f of fs.readdirSync(src).sort(byName)) if (isPic(path.join(src, f))) picked.push({ src: path.join(src, f) });
  const packs = fs.readdirSync(src).filter(d => !d.startsWith('_') && !d.startsWith('.') && fs.statSync(path.join(src, d)).isDirectory())
    .map(d => ({ dir: path.join(src, d), name: d, meta: readPackMeta(path.join(src, d)) })).filter(p => p.meta.install)
    .sort((a, b) => (Number(a.meta.order) || 50) - (Number(b.meta.order) || 50) || byName(a.name, b.name));
  for (const p of packs) for (const f of fs.readdirSync(p.dir).sort(byName)) {
    const s = (p.meta.pictures || {})[f] || {};
    if (!isPic(path.join(p.dir, f)) || s.install === false) continue;
    picked.push({ src: path.join(p.dir, f), dates: /^\d{4}-\d{4}$/.test(s.dates || '') ? s.dates : '' });
  }
}
if (!picked.length) {                                 // no content repo: our generated greetings
  run(process.execPath, [path.join(root, 'tools/make-starter.mjs'), stage]);
  picked.push({ src: path.join(stage, '01-merry-christmas.gif'), dates: '1115-0106' }, { src: path.join(stage, '03-happy-holidays.gif'), dates: '1115-0106' },
    { src: path.join(stage, '02-pocket-jumbotron.gif') });
}
// Card-ready GIFs (128x64) go on as they are; anything else is converted (any picture -> 128x64 GIF).
// Each gets a numbered name so the same file name in two packs can't clash.
const isCardGif = (f) => { const b = fs.readFileSync(f); return b.slice(0, 3).toString() === 'GIF' && b.readUInt16LE(6) === 128 && b.readUInt16LE(8) === 64; };
const showPics = [];
picked.forEach((p, i) => {
  const base = String(i + 1).padStart(2, '0') + '-' + path.basename(p.src, path.extname(p.src)).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 24);
  if (isCardGif(p.src)) { fs.copyFileSync(p.src, path.join(gifs, base + '.gif')); showPics.push({ file: base + '.gif', dates: p.dates }); return; }
  const tmp = path.join(stage, 'one'); fs.rmSync(tmp, { recursive: true, force: true }); fs.mkdirSync(tmp);
  run(process.execPath, [path.join(root, 'tools/make-gifs.mjs'), tmp, p.src]);
  const made = fs.readdirSync(tmp).filter(f => f.endsWith('.gif'));
  if (!made.length) { console.warn('warning: could not convert ' + p.src + '; left off the first show'); return; }
  fs.copyFileSync(path.join(tmp, made[0]), path.join(gifs, base + '.gif')); showPics.push({ file: base + '.gif', dates: p.dates });
});
const L = (...f) => f.join('\t');
const show = [
  L('clock', 0, '5AC8FF', '000000', 4, 'none', 12),
  L('weather', 0, 'FFFFFF', '000000', 4, 'none', 'A', 'auto', 'auto', '', '@forecast'),
  ...showPics.map(p => p.dates ? L('pic', 0, p.file, '@dates=' + p.dates) : L('pic', 0, p.file)),
];
fs.writeFileSync(path.join(fsDir, 'show.txt'), show.join('\n') + '\n');
const fsBytes = fs.readdirSync(gifs).reduce((s, f) => s + fs.statSync(path.join(gifs, f)).size, 0);
console.log(`First show: ${show.length} items, ${showPics.length} pictures, ${(fsBytes / 1048576).toFixed(2)} MB`);
for (const l of show) console.log('  ' + l.replace(/\t/g, ' | '));
if (firstOnly) process.exit(0);
if (fsBytes > 0x460000 - 0x40000) throw new Error('first show is too big for the picture storage (4.1 MB)');
const littlefs = path.join(build, 'install-littlefs.bin');
run(findTool('mklittlefs', 'mklittlefs'), ['-c', fsDir, '-b', '4096', '-p', '256', '-s', String(0x460000), littlefs], { stdio: ['ignore', 'ignore', 'inherit'] });

// 3. Blank NVS (clears whatever the Huidu firmware left there) + merge into one image.
const nvsBlank = path.join(build, 'nvs-blank.bin');
fs.writeFileSync(nvsBlank, Buffer.alloc(0x5000, 0xff));
const bootApp0 = path.join(a15, `packages/esp32/hardware/esp32/${CORE}/tools/partitions/boot_app0.bin`);
const image = path.join(out, 'pocket-jumbotron-install.bin');
run(findTool('esptool_py', 'esptool'), ['--chip', 'esp32s3', 'merge-bin', '-o', image, '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '8MB',
  '0x0', path.join(appOut, 'pocket_jumbotron.ino.bootloader.bin'),
  '0x8000', path.join(appOut, 'pocket_jumbotron.ino.partitions.bin'),
  '0x9000', nvsBlank,
  '0xe000', bootApp0,
  '0x10000', path.join(appOut, 'pocket_jumbotron.ino.bin'),
  '0x390000', littlefs]);
fs.copyFileSync(path.join(appOut, 'pocket_jumbotron.ino.bin'), path.join(out, 'pocket-jumbotron-app.bin'));

const sha = (f) => crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const info = {
  version, built: new Date().toISOString(),
  install: { file: 'pocket-jumbotron-install.bin', size: fs.statSync(image).size, sha256: sha(image) },
  app: { file: 'pocket-jumbotron-app.bin', size: fs.statSync(path.join(out, 'pocket-jumbotron-app.bin')).size, sha256: sha(path.join(out, 'pocket-jumbotron-app.bin')) },
};
fs.writeFileSync(path.join(out, 'info.json'), JSON.stringify(info, null, 2));
console.log(`\nInstall image v${version}: ${(info.install.size / 1048576).toFixed(2)} MB -> docs/firmware/`);
