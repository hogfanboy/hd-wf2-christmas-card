// Pocket Jumbotron - 128x64 LED panel simulator.
// Draws scenes into a 128x64 pixel buffer, then scales it up with an LED-dot mask and glow.
(function () {
  const W = 128, H = 64;
  const FONT = { // 5x7, column bytes, bit0 = top row
    A:[0x7E,0x11,0x11,0x11,0x7E],B:[0x7F,0x49,0x49,0x49,0x36],C:[0x3E,0x41,0x41,0x41,0x22],
    D:[0x7F,0x41,0x41,0x22,0x1C],E:[0x7F,0x49,0x49,0x49,0x41],F:[0x7F,0x09,0x09,0x09,0x01],
    G:[0x3E,0x41,0x49,0x49,0x7A],H:[0x7F,0x08,0x08,0x08,0x7F],I:[0x00,0x41,0x7F,0x41,0x00],
    J:[0x20,0x40,0x41,0x3F,0x01],K:[0x7F,0x08,0x14,0x22,0x41],L:[0x7F,0x40,0x40,0x40,0x40],
    M:[0x7F,0x02,0x0C,0x02,0x7F],N:[0x7F,0x04,0x08,0x10,0x7F],O:[0x3E,0x41,0x41,0x41,0x3E],
    P:[0x7F,0x09,0x09,0x09,0x06],Q:[0x3E,0x41,0x51,0x21,0x5E],R:[0x7F,0x09,0x19,0x29,0x46],
    S:[0x46,0x49,0x49,0x49,0x31],T:[0x01,0x01,0x7F,0x01,0x01],U:[0x3F,0x40,0x40,0x40,0x3F],
    V:[0x1F,0x20,0x40,0x20,0x1F],W:[0x3F,0x40,0x38,0x40,0x3F],X:[0x63,0x14,0x08,0x14,0x63],
    Y:[0x07,0x08,0x70,0x08,0x07],Z:[0x61,0x51,0x49,0x45,0x43],'!':[0x00,0x00,0x5F,0x00,0x00],
    ' ':[0,0,0,0,0]
  };

  // Pixel drawing into an RGBA buffer (128x64). Shared by the website and tools/make-starter.mjs.
  function makeDrawer(buf) {
    function set(x, y, r, g, b) {
      x |= 0; y |= 0;
      if (x < 0 || y < 0 || x >= W || y >= H) return;
      const i = (y * W + x) * 4;
      buf[i] = r; buf[i + 1] = g; buf[i + 2] = b; buf[i + 3] = 255;
    }
    function clear() { for (let i = 0; i < buf.length; i += 4) { buf[i] = buf[i+1] = buf[i+2] = 0; buf[i+3] = 255; } }
    function text(str, x0, y0, color, size) {
      size = size || 1;
      let x = x0;
      for (const ch of str) {
        const cols = FONT[ch] || FONT[' '];
        for (let c = 0; c < 5; c++) for (let row = 0; row < 7; row++)
          if (cols[c] >> row & 1)
            for (let dy = 0; dy < size; dy++) for (let dx = 0; dx < size; dx++)
              set(x + c * size + dx, y0 + row * size + dy, ...(typeof color === 'function' ? color(x + c * size, y0 + row * size) : color));
        x += 6 * size;
      }
      return x - x0;
    }
    return { set, clear, text };
  }

  function makePanel(canvas) {
    const src = document.createElement('canvas');
    src.width = W; src.height = H;
    const sctx = src.getContext('2d');
    const img = sctx.createImageData(W, H);
    const buf = img.data;
    const ctx = canvas.getContext('2d');
    let mask = null, scale = 1;

    function resize() {
      const cssW = canvas.clientWidth || 640;
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      scale = Math.max(2, Math.floor((cssW * dpr) / W));
      canvas.width = W * scale; canvas.height = H * scale;
      // Dot mask: dark board with round transparent holes, one per LED.
      mask = document.createElement('canvas');
      mask.width = canvas.width; mask.height = canvas.height;
      const m = mask.getContext('2d');
      m.fillStyle = '#07070b'; m.fillRect(0, 0, mask.width, mask.height);
      m.globalCompositeOperation = 'destination-out';
      const r = scale * 0.40;
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        m.beginPath(); m.arc(x * scale + scale / 2, y * scale + scale / 2, r, 0, 6.2832); m.fill();
      }
    }

    const { set, clear, text } = makeDrawer(buf);
    function present() {
      sctx.putImageData(img, 0, 0);
      ctx.imageSmoothingEnabled = false;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.drawImage(src, 0, 0, canvas.width, canvas.height);
      ctx.drawImage(mask, 0, 0);
    }
    resize();
    window.addEventListener('resize', resize);
    return { set, clear, text, present, src };
  }

  function hsv(h, s, v) {
    const i = Math.floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    const [r, g, b] = [[v,t,p],[q,v,p],[p,v,t],[p,q,v],[t,p,v],[v,p,q]][((i % 6) + 6) % 6];
    return [r * 255, g * 255, b * 255];
  }

  // ---- Scenes ----
  const flakes = Array.from({ length: 70 }, () => ({ x: Math.random() * W, y: Math.random() * H, v: 0.15 + Math.random() * 0.45 }));
  const scenes = {
    snow(p, t, opts) {
      for (let y = 0; y < H; y++) { const k = y / H; for (let x = 0; x < W; x++) p.set(x, y, 4, 8 + 20 * k, 30 + 40 * k); }
      // ground + tree
      for (let x = 0; x < W; x++) for (let y = 56 + Math.round(Math.sin(x / 9) * 1.5); y < H; y++) p.set(x, y, 200, 210, 230);
      for (let row = 0; row < 26; row++) { const half = Math.floor(row / 2) + 1; for (let dx = -half; dx <= half; dx++) p.set(108 + dx, 28 + row, 10, 120 + (row * 3), 40); }
      for (let y = 54; y < 58; y++) for (let x = 106; x < 111; x++) p.set(x, y, 110, 60, 20);
      const tw = (Math.sin(t / 150) + 1) / 2; p.set(108, 26, 255, 220 * tw + 35, 40); p.set(107, 27, 255, 200, 40); p.set(109, 27, 255, 200, 40);
      [[104,36],[112,40],[102,46],[110,50],[106,42]].forEach(([x, y], i) => { const on = Math.sin(t / 200 + i * 1.7) > 0; p.set(x, y, on ? 255 : 90, on ? 60 : 20, on ? 60 : 20); });
      for (const f of flakes) { f.y += f.v; f.x += Math.sin((t / 700) + f.y / 7) * 0.15; if (f.y > H) { f.y = -1; f.x = Math.random() * W; } p.set(f.x, f.y, 235, 240, 255); }
      const msg = (opts && opts.msg) || 'MERRY CHRISTMAS!', w = msg.length * 12;
      const x = W - ((t / 28) % (w + W));
      p.text(msg, x, 6, (cx) => hsv(((cx + t / 12) / 160) % 1, 0.65, 1), 2);
    },
    color(p, t) {
      for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const v = Math.sin(x / 11 + t / 900) + Math.sin(y / 7 - t / 1300) + Math.sin((x + y) / 17 + t / 700);
        p.set(x, y, ...hsv(((v + 3) / 6 + t / 9000) % 1, 0.9, 0.95));
      }
    },
    name(p, t) {
      p.clear();
      for (let i = 0; i < 40; i++) { const sx = (i * 53) % W, sy = (i * 29) % H, b = (Math.sin(t / 300 + i) + 1) / 2; if (b > 0.6) p.set(sx, sy, 255 * b, 230 * b, 160 * b); }
      const gold = (cx, cy) => { const g = 0.75 + 0.25 * Math.sin((cx - t / 8) / 6); return [255 * g, 190 * g, 60 * g]; };
      p.text('POCKET', 64 - 36, 12, gold, 2);
      p.text('JUMBOTRON', 64 - 27, 36, [90, 200, 255], 1);
      for (let x = 20; x < 108; x++) { const on = ((x + Math.floor(t / 60)) % 6) < 3; p.set(x, 48, on ? 255 : 40, on ? 60 : 10, on ? 90 : 20); }
    }
  };

  window.PocketJumbotron = { W, H, scenes, makeDrawer, mount(canvas, opts) {
    const p = makePanel(canvas);
    let current = (opts && opts.scene) || 'snow', last = performance.now(), auto = true, sceneStart = last;
    const order = Object.keys(scenes);
    const reduce = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
    function frame(now) {
      if (auto && now - sceneStart > 9000) { current = order[(order.indexOf(current) + 1) % order.length]; sceneStart = now; opts.onScene && opts.onScene(current); }
      scenes[current](p, now); p.present();
      if (!reduce) requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
    return { show(name) { current = name; auto = false; if (reduce) { scenes[name](p, performance.now()); p.present(); } } };
  } };
})();
