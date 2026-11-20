// Builds the ENCRYPTED picture packs ("the vault") that screens unlock with a family code.
// Usage: node tools/build-vault.mjs <private-content-folder>   (output: docs/vault/, published by CI)
//
// The private folder looks like content/ (loose pictures = "New pictures" pack, folders = packs with an
// optional pack.json) plus codes.json:
//   [ { "code": "SNOW-4821", "packs": "*" },                      every pack
//     { "code": "SMITH-1225", "packs": ["smith-family", "*public-ish ids*"] } ]
// Each code gets its own encrypted list of packs, named by an id derived from the code, so the website
// shows nothing readable: only <id>.bin lists and f/<random>.bin pictures.
//
// Crypto (matched by the card page in web/index.html, which can't use WebCrypto because it is http):
//   master = PBKDF2-HMAC-SHA256(code normalized to A-Z0-9, "pocket-jumbotron-vault-v1", 20000, 32 bytes)
//   encKey = HMAC(master, "enc"), macKey = HMAC(master, "mac"), id = hex(HMAC(master, "id"))[0..16]
//   envelope = iv(16) | AES-256-CTR(encKey, iv, data) | HMAC-SHA256(macKey, iv | ciphertext)
// Every picture has its own random master; the list carries it (base64) inside the code's envelope.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const src = path.resolve(process.argv[2] || path.join(root, 'private-content'));
const out = path.join(root, 'docs/vault');
const PIC = /\.(gif|png|jpe?g|webp)$/i;
const SALT = 'pocket-jumbotron-vault-v1', ITER = 20000;

export const normCode = (c) => String(c).toUpperCase().replace(/[^A-Z0-9]/g, '');
const hmac = (k, d) => crypto.createHmac('sha256', k).update(d).digest();
export function keysFromMaster(master) {
  return { enc: hmac(master, 'enc'), mac: hmac(master, 'mac'), id: hmac(master, 'id').toString('hex').slice(0, 16) };
}
export const codeMaster = (code) => crypto.pbkdf2Sync(normCode(code), SALT, ITER, 32, 'sha256');
export function seal(data, k) {
  const iv = crypto.randomBytes(16);
  const c = crypto.createCipheriv('aes-256-ctr', k.enc, iv);
  const ct = Buffer.concat([c.update(data), c.final()]);
  return Buffer.concat([iv, ct, hmac(k.mac, Buffer.concat([iv, ct]))]);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (!fs.existsSync(src)) { console.log(`No private content at ${src}: vault skipped.`); process.exit(0); }
  const codes = JSON.parse(fs.readFileSync(path.join(src, 'codes.json'), 'utf8'));
  fs.rmSync(out, { recursive: true, force: true });
  fs.mkdirSync(path.join(out, 'f'), { recursive: true });

  // Collect packs exactly like tools/build-packs.mjs does for public content.
  const packs = [];
  const addPack = (id, dir, files, meta, order) => {
    const entries = files.map(f => {
      const master = crypto.randomBytes(32), name = crypto.randomBytes(12).toString('hex') + '.bin';
      fs.writeFileSync(path.join(out, 'f', name), seal(fs.readFileSync(path.join(dir, f)), keysFromMaster(master)));
      return { name: f, url: 'f/' + name, key: master.toString('base64') };
    });
    packs.push({ id, title: meta.title || id, about: meta.about || '', ...(meta.cards ? { cards: meta.cards } : {}), order: meta.order ?? order, files: entries });
  };
  const loose = fs.readdirSync(src).filter(f => PIC.test(f) && fs.statSync(path.join(src, f)).isFile()).sort();
  if (loose.length) addPack('new-pictures', src, loose, { title: 'New pictures', about: 'The latest additions.' }, 10);
  for (const id of fs.readdirSync(src).sort()) {
    const dir = path.join(src, id);
    if (!fs.statSync(dir).isDirectory() || id.startsWith('_') || id.startsWith('.')) continue;
    const meta = fs.existsSync(path.join(dir, 'pack.json')) ? JSON.parse(fs.readFileSync(path.join(dir, 'pack.json'), 'utf8')) : {};
    const files = fs.readdirSync(dir).filter(f => PIC.test(f)).sort();
    if (files.length) addPack(id, dir, files, meta, 50);
  }
  packs.sort((a, b) => a.order - b.order || a.id.localeCompare(b.id));

  for (const c of codes) {
    const k = keysFromMaster(codeMaster(c.code));
    const mine = packs.filter(p => c.packs === '*' || (Array.isArray(c.packs) && c.packs.includes(p.id))).map(({ order, ...p }) => p);
    fs.writeFileSync(path.join(out, k.id + '.bin'), seal(Buffer.from(JSON.stringify({ built: new Date().toISOString(), packs: mine })), k));
    console.log(`vault code ****${normCode(c.code).slice(-2)}: ${mine.length} packs, ${mine.reduce((s, p) => s + p.files.length, 0)} pictures`);
  }
}
