// Opens the encrypted picture packs made by tools/build-vault.mjs (see the crypto notes there).
// Plain JS (aes-js + js-sha256) because the card's page is http, where browsers turn WebCrypto off.
(function (g) {
  'use strict';
  const SALT = 'pocket-jumbotron-vault-v1', ITER = 20000;
  const te = new TextEncoder();
  const bytes = (x) => (typeof x === 'string' ? te.encode(x) : x);
  const hmac = (k, d) => new Uint8Array(g.sha256.hmac.arrayBuffer(k, bytes(d)));
  const normCode = (c) => String(c).toUpperCase().replace(/[^A-Z0-9]/g, '');

  function pbkdf2(pass, salt) {                  // one 32-byte block is all we need
    const s = new Uint8Array(salt.length + 4); s.set(salt); s[s.length - 1] = 1;
    let u = hmac(pass, s); const t = u.slice();
    for (let i = 1; i < ITER; i++) { u = hmac(pass, u); for (let j = 0; j < 32; j++) t[j] ^= u[j]; }
    return t;
  }
  function keys(master) {
    const id = Array.from(hmac(master, 'id').slice(0, 8), b => b.toString(16).padStart(2, '0')).join('');
    return { enc: hmac(master, 'enc'), mac: hmac(master, 'mac'), id };
  }
  function open(env, k) {
    env = new Uint8Array(env);
    if (env.length < 48) throw new Error('damaged file');
    const body = env.subarray(0, env.length - 32), tag = env.subarray(env.length - 32);
    const want = hmac(k.mac, body);
    let diff = 0; for (let i = 0; i < 32; i++) diff |= want[i] ^ tag[i];
    if (diff) throw new Error('wrong code');
    const ctr = new g.aesjs.ModeOfOperation.ctr(k.enc, new g.aesjs.Counter(body.slice(0, 16)));
    return ctr.decrypt(body.subarray(16));
  }
  const b64 = (s) => Uint8Array.from(atob(s), c => c.charCodeAt(0));

  g.vault = {
    normCode,
    fromCode: (code) => keys(pbkdf2(te.encode(normCode(code)), te.encode(SALT))),
    fromKey: (b64key) => keys(b64(b64key)),
    open,
  };
})(typeof window !== 'undefined' ? window : globalThis);
