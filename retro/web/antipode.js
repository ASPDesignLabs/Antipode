/* Antipode (retro) - the browser shell. The game is C compiled to WebAssembly (platform/web_main.c); this file only
 * feeds it input, takes its RGBA frame onto a plain 2D canvas (putImageData, integer pixelated scale: no WebGL, no
 * shaders), and pumps its sound into an AudioWorklet. WASM_B64 is put in front of this file by web/inline.py.
 * Add ?perf to the address to see step / draw times on screen. */
(async function () {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const canvas = $("c"), failEl = $("fail"), perfEl = $("perf");
  const fail = (m) => { failEl.textContent = m + "\n\n" + navigator.userAgent; failEl.hidden = false; };
  const PERF = /[?&]perf\b/.test(location.search);

  let X;
  try {
    const bytes = Uint8Array.from(atob(WASM_B64), (c) => c.charCodeAt(0));
    X = (await WebAssembly.instantiate(bytes, {})).instance.exports;
  } catch (e) { fail("This browser could not start the game (WebAssembly): " + e); return; }
  const ctx = canvas.getContext("2d", { alpha: false });
  if (!ctx) { fail("This browser has no 2D canvas."); return; }

  const mem = () => X.memory.buffer;
  const cstr = (p) => { const m = new Uint8Array(mem()); let s = ""; while (m[p]) s += String.fromCharCode(m[p++]); return s; };
  const KEYS = cstr(X.web_key_names()).split(" ");
  const K = {}; KEYS.forEach((n, i) => (K[n] = i));
  if (KEYS.length !== X.web_key_count()) { fail("This build is out of step: the page's keys do not match the game's."); return; }

  // ---- what is saved: the best score of each scene and the pair of colours last chosen, 16 bytes kept as hex in localStorage ----
  const SAVE = "antipode.retro.save";
  try {
    let hex = localStorage.getItem(SAVE);
    if (!hex) {                                                // an older save held only the first scene's best, as a number
      const v = parseInt(localStorage.getItem("antipode.retro.best") || "0", 10) | 0;
      if (v > 0) hex = (v & 255).toString(16).padStart(2, "0") + ((v >> 8) & 255).toString(16).padStart(2, "0");
    }
    if (hex) {
      const n = Math.min(16, hex.length >> 1), b = new Uint8Array(mem(), X.web_slot_buf(), 16);
      for (let i = 0; i < n; i++) b[i] = parseInt(hex.substr(i * 2, 2), 16) || 0;
      X.web_slot_load(n);
    }
  } catch (_) {}
  function flushSlot() {
    const n = X.web_slot_take();
    if (!n) return;
    let hex = "";
    for (const v of new Uint8Array(mem(), X.web_slot_buf(), n)) hex += v.toString(16).padStart(2, "0");
    try { localStorage.setItem(SAVE, hex); } catch (_) {}
  }

  X.web_config(1, 70, 100);
  // The season is on by default (?theme=classic is the plain game); ?pair=gold plays gold and blue, ?pair=red red and green;
  // with neither, the pair last chosen with T (or Y, or a tap on the corner) is kept.
  const QS = new URLSearchParams(location.search);
  const pairQ = (QS.get("pair") || "").toLowerCase();
  X.web_theme(QS.get("theme") === "classic" ? 0 : 1, pairQ === "gold" ? 1 : pairQ === "red" ? 0 : -1);
  const want = (QS.get("mode") || "").toLowerCase().replace(/[^a-z]/g, "");
  const norm = (n) => n.toLowerCase().replace(/[^a-z]/g, "");
  const names = cstr(X.web_scene_names()).split("\n"), titles = cstr(X.web_scene_titles()).split("\n");
  let first = names.findIndex((n) => norm(n) === want);
  if (first < 0) first = titles.findIndex((n) => norm(n) === want);          // ?mode=sparkler, advent, sleighbells, lantern
  X.web_select(first < 0 ? 0 : first);                       // ?mode=prismrun, ?mode=afterimage
  X.web_start();
  let W = X.web_width(), H = X.web_height();
  let img = ctx.createImageData(W, H);
  canvas.width = W; canvas.height = H;

  // ---- the picture, scaled by a whole number of device pixels ----
  function fit() {
    const dpr = window.devicePixelRatio || 1;
    const s = Math.max(1, Math.floor(Math.min((innerWidth * dpr) / W, (innerHeight * dpr) / H)));
    canvas.style.width = (W * s) / dpr + "px"; canvas.style.height = (H * s) / dpr + "px";
  }
  addEventListener("resize", fit); fit();

  // ---- sound: C renders, an AudioWorklet plays ----
  let actx = null, node = null, owed = 0, muted = false;
  const WORKLET = `class P extends AudioWorkletProcessor{constructor(){super();this.q=[];this.o=0;this.n=0;
    this.port.onmessage=(e)=>{this.q.push(e.data);this.n+=e.data.length/2;while(this.n>8192){const d=this.q.shift();this.n-=d.length/2-this.o;this.o=0;}};}
    process(i,o){const l=o[0][0],r=o[0][1];for(let k=0;k<l.length;k++){const d=this.q[0];if(!d){l[k]=0;if(r)r[k]=0;continue;}
    l[k]=d[this.o*2];if(r)r[k]=d[this.o*2+1];this.n--;if(++this.o*2>=d.length){this.q.shift();this.o=0;}}return true;}}
    registerProcessor("ap",P);`;
  async function unlockAudio() {
    if (actx) { if (actx.state === "suspended") actx.resume(); return; }
    try {
      const AC = window.AudioContext || window.webkitAudioContext;
      actx = new AC({ sampleRate: X.web_rate() });
      await actx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: "text/javascript" })));
      node = new AudioWorkletNode(actx, "ap", { outputChannelCount: [2] });
      node.connect(actx.destination);
      if (actx.state === "suspended") actx.resume();
    } catch (e) { actx = null; node = null; }               // no sound is not fatal
  }
  function pump(dt) {
    if (!node || muted) return;
    owed += dt * X.web_rate();
    let n = Math.floor(owed);
    if (n < 128) return;
    owed -= n; n = Math.min(n, 4096);
    const p = X.web_audio(n);
    node.port.postMessage(new Float32Array(mem(), p, n * 2).slice());
  }

  // ---- keyboard ----
  const CODE = { ArrowUp: "UP", ArrowDown: "DOWN", ArrowLeft: "LEFT", ArrowRight: "RIGHT", KeyW: "W", KeyA: "A", KeyS: "S", KeyD: "D",
                 Space: "SPACE", Enter: "ENTER", Escape: "ESC", KeyM: "M", KeyQ: "Q", KeyE: "E", KeyT: "T" };
  addEventListener("keydown", (e) => {
    if (e.repeat) return;
    if (e.code === "Space" || e.code.startsWith("Arrow")) e.preventDefault();
    unlockAudio();
    if (e.code === "KeyF") { try { document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen(); } catch (_) {} return; }
    if (e.code === "KeyM") { muted = !muted; return; }
    X.web_pointer(0, 0, 0);                                    // the keyboard takes over from the mouse
    X.web_key(K[CODE[e.code]] ?? K.ANY, 1);
    if (!CODE[e.code]) X.web_key(K.ANY, 0);                    // "any other key": a tap, not a hold
  });
  addEventListener("keyup", (e) => { if (CODE[e.code]) X.web_key(K[CODE[e.code]], 0); });
  const releaseAll = () => X.web_release();
  addEventListener("blur", releaseAll);
  document.addEventListener("visibilitychange", () => { if (document.hidden) releaseAll(); });

  // ---- mouse and touch: one pointer, in the picture's pixels ----
  function place(e) {
    const r = canvas.getBoundingClientRect();
    X.web_pointer(((e.clientX - r.left) * W) / r.width, ((e.clientY - r.top) * H) / r.height, 1);
  }
  canvas.addEventListener("pointerdown", (e) => { unlockAudio(); place(e); X.web_pointer_down(); try { canvas.setPointerCapture(e.pointerId); } catch (_) {} });
  const release = (e) => {
    const r = canvas.getBoundingClientRect();
    X.web_pointer_up(((e.clientX - r.left) * W) / r.width, ((e.clientY - r.top) * H) / r.height);
    if (e.pointerType !== "mouse") X.web_pointer(0, 0, 0);   // a finger that lifted is gone; the mouse is still there
  };
  canvas.addEventListener("pointermove", (e) => { if (e.pointerType === "mouse" || e.buttons) place(e); });
  canvas.addEventListener("pointerup", release);
  canvas.addEventListener("pointercancel", (e) => { if (e.pointerType !== "mouse") X.web_pointer(0, 0, 0); });
  canvas.addEventListener("pointerleave", (e) => { if (e.pointerType === "mouse") X.web_pointer(0, 0, 0); });
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  // ---- gamepad: the standard layout -> XInput's order, sticks radial dead zone, y up ----
  const PADBIT = [0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15, 6, 7];   // PAD_* index -> standard button index
  let padWas = false;
  function pollPad() {
    const pads = navigator.getGamepads ? Array.from(navigator.getGamepads()).filter((g) => g && g.connected) : [];
    if (!pads.length) { if (padWas) { X.web_pad(0, 0, 0, 0, 0, 0); padWas = false; } return; }
    const g = pads[0];
    let mask = 0;
    for (let i = 0; i < PADBIT.length; i++) { const b = g.buttons[PADBIT[i]]; if (b && (b.pressed || b.value > 0.4)) mask |= 1 << i; }
    const dz = (x, y) => { const m = Math.hypot(x, y); if (m < 0.2) return [0, 0]; const k = (Math.min(m, 1) - 0.2) / 0.8 / m; return [x * k, y * k]; };
    const [lx, ly] = dz(g.axes[0] || 0, g.axes[1] || 0), [rx, ry] = dz(g.axes[2] || 0, g.axes[3] || 0);
    if (mask && !padWas) unlockAudio();
    X.web_pad(1, mask, lx, -ly, rx, -ry);
    padWas = true;
  }

  // ---- the loop ----
  const perf = { n: 0, step: 0, draw: 0, worst: 0, t: 0 };
  let last = 0;
  function frame(ts) {
    requestAnimationFrame(frame);
    const dt = last ? Math.min(0.1, (ts - last) / 1000) : 0;
    last = ts;
    pollPad();
    const t0 = performance.now();
    X.web_step(dt);
    const t1 = performance.now();
    const p = X.web_frame();
    img.data.set(new Uint8Array(mem(), p, W * H * 4));
    ctx.putImageData(img, 0, 0);
    const t2 = performance.now();
    pump(dt);
    flushSlot();
    if (PERF) {
      perf.n++; perf.step += t1 - t0; perf.draw += t2 - t1; perf.worst = Math.max(perf.worst, t2 - t0); perf.t += dt;
      if (perf.t >= 1) {
        perfEl.hidden = false;
        perfEl.textContent = "fps " + perf.n + "\nstep " + (perf.step / perf.n).toFixed(2) + " ms\ndraw " + (perf.draw / perf.n).toFixed(2) + " ms\nworst " + perf.worst.toFixed(1) + " ms";
        perf.n = perf.step = perf.draw = perf.worst = perf.t = 0;
      }
    }
  }
  window.antipodeDebug = { X, K };                           // for automated tests
  requestAnimationFrame(frame);
})();
