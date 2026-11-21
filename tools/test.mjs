// Quick checks that run in CI before anything is published (and locally: node tools/test.mjs after build-web.mjs).
// They catch the mistakes that would otherwise reach every card at 3 am: a page that doesn't parse, the page and
// the firmware disagreeing on effect names or version order, a malformed version string.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8');
let failed = 0;
const check = (name, ok, detail = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${ok || !detail ? '' : ': ' + detail}`); if (!ok) failed++; };

// 1. Every inline script in the pages people open parses (a syntax error = a blank page on every phone).
const pages = ['docs/app/index.html', 'docs/builder/index.html', 'docs/install.html', 'docs/index.html', 'docs/go.html', 'web/wifi.html'];
for (const p of pages) {
  if (!fs.existsSync(path.join(root, p))) { check(p, false, 'missing (run node tools/build-web.mjs first)'); continue; }
  const html = read(p);
  const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/g)];
  let err = '';
  for (const [, attrs, code] of scripts) {
    if (!/type=["']?module/.test(attrs)) { try { new vm.Script(code); } catch (e) { err = e.message; break; } continue; }
    const tmp = path.join(os.tmpdir(), `pj-check-${process.pid}.mjs`);   // modules: let node parse it as one
    fs.writeFileSync(tmp, code);
    try { execFileSync(process.execPath, ['--check', tmp], { stdio: 'pipe' }); }
    catch (e) { err = String(e.stderr).split('\n').find(l => /Error/.test(l)) || 'module does not parse'; }
    finally { fs.rmSync(tmp, { force: true }); }
    if (err) break;
  }
  check(`${p} scripts parse (${scripts.length})`, !err, err);
}

// 2. Effect names: the page offers exactly the effects the firmware knows (an unknown name plays as none).
const ino = fs.readdirSync(path.join(root, 'firmware/pocket_jumbotron')).filter(f => f.endsWith('.ino'))
  .map(f => read('firmware/pocket_jumbotron/' + f)).join('\n');
const fw = [...(/FX_NAMES\[\]\s*=\s*\{([^}]*)\}/.exec(ino)?.[1] ?? '').matchAll(/"(\w+)"/g)].map(m => m[1]);
const appJs = fs.readdirSync(path.join(root, 'web/app')).sort().map(f => read('web/app/' + f)).join('\n');
const pageFx = [...(/const FX = \{([\s\S]*?)\};/.exec(appJs)?.[1] ?? '').matchAll(/(\w+):\s*['"]/g)].map(m => m[1]);
const missing = pageFx.filter(n => !fw.includes(n)), unused = fw.filter(n => !pageFx.includes(n));
check('effect names match (firmware FX_NAMES vs page FX)', fw.length > 0 && !missing.length && !unused.length,
  `page only: ${missing.join(',') || '-'}; firmware only: ${unused.join(',') || '-'}`);

// 3. Version: well formed, and the page's version order agrees with the card's (online.ino verNum).
const version = /FW_VERSION = "([^"]+)"/.exec(ino)?.[1] ?? '';
check(`firmware version "${version}" is x.y.z or x.y.z-betaN`, /^\d+\.\d+\.\d+(-beta\d+)?$/.test(version));
const verKeySrc = /const verKey = [\s\S]*?\};/.exec(appJs)?.[0], newerSrc = /const newer = [^\n]*/.exec(appJs)?.[0];
const verNum = (v) => {                                   // JS copy of the firmware's verNum
  const [a = 0, b = 0, c = 0] = v.split('-')[0].split('.').map(n => parseInt(n) || 0);
  const dash = v.indexOf('-'); let pre = 99;
  if (dash >= 0) { const m = /\d+/.exec(v.slice(dash)); pre = m ? +m[0] : 0; }
  return ((a * 10000 + b * 100 + c) * 100) + pre;
};
let orderErr = '';
try {
  const newer = new Function(`${verKeySrc}\n${newerSrc}\nreturn newer;`)();
  const cases = [['1.0.0', '1.0.0-beta6'], ['1.0.0-beta6', '1.0.0-beta5'], ['1.0.0-beta10', '1.0.0-beta9'], ['1.0.1', '1.0.0'], ['1.1.0', '1.0.9'], ['2.0.0', '1.9.9']];
  for (const [hi, lo] of cases) {
    if (!newer(hi, lo) || newer(lo, hi)) orderErr += ` page: ${hi} vs ${lo};`;
    if (!(verNum(hi) > verNum(lo))) orderErr += ` card: ${hi} vs ${lo};`;
  }
  if (newer(version, version)) orderErr += ' a version is newer than itself;';
} catch (e) { orderErr = e.message; }
check('version order (release > its betas, beta10 > beta9)', !orderErr, orderErr);

// 4. The firmware build will pick up the app: web_index.h exists and isn't empty.
const h = path.join(root, 'firmware/pocket_jumbotron/web_index.h');
check('web_index.h present', fs.existsSync(h) && fs.statSync(h).size > 10000);

console.log(failed ? `\n${failed} check(s) failed` : '\nAll checks passed');
process.exit(failed ? 1 : 0);
