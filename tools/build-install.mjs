// Builds everything a fresh card needs and puts it where the website's install page can fetch it:
//   docs/firmware/pocket-jumbotron-install.bin  one image for address 0x0 (bootloader, partitions, blank NVS,
//                                               OTA data, app, LittleFS with the starter pack)
//   docs/firmware/pocket-jumbotron-app.bin      app only (for wireless updates)
//   docs/firmware/info.json                     version, sizes, sha256
// Runs on Windows (this PC) and Linux (GitHub Actions). Needs arduino-cli + the ESP32 core installed.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

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

// 1. Web pages -> web_index.h, then compile.
run(process.execPath, [path.join(root, 'tools/build-web.mjs')]);
run(arduinoCli, ['compile', '-b', FQBN, '--build-property', 'upload.maximum_size=1835008', '--output-dir', appOut, sketch]);   // our app slot is 1.75 MB (partitions.csv)
const version = /FW_VERSION = "([^"]+)"/.exec(fs.readFileSync(path.join(sketch, 'pocket_jumbotron.ino'), 'utf8'))[1];

// 2. Starter pack -> LittleFS image (must match partitions.csv: spiffs @0x390000, 0x460000).
const fsDir = path.join(build, 'install-fs');
fs.rmSync(fsDir, { recursive: true, force: true });
run(process.execPath, [path.join(root, 'tools/make-starter.mjs'), path.join(fsDir, 'gifs')]);
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
