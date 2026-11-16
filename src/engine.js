/* Antipode — engine.
 * WebGL2, one fullscreen triangle, no buffers, no assets.
 * Owns: GL setup, shader/target helpers, resize + adaptive resolution,
 * the main loop, the menu/play/dead state machine, and the (tiny) DOM HUD.
 *
 * A mode is registered in AP.modes as { id, name, hint, create(engine) } and
 * create() returns { resize(w,h), reset(), step(dt,input,playing)->dead, draw(t,deadAge), score, hue }.
 */
(function (AP) {
  'use strict';

  const VERT = '#version 300 es\n' +
    'out vec2 vUv;\n' +
    'void main(){\n' +
    '  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n' +
    '  vUv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n' +
    '}\n';
  const FRAG_HEAD = '#version 300 es\nprecision highp float;\nprecision highp int;\n';

  function Engine(canvas) {
    const gl = canvas.getContext('webgl2', {
      antialias: false, alpha: false, depth: false, stencil: false,
      powerPreference: 'high-performance'
    });
    if (!gl) throw new Error('WebGL2 is not available on this device.');
    const self = this;
    this.gl = gl;
    this.canvas = canvas;
    this.W = 1; this.H = 1; this.aspect = 1;
    this.scale = 1;              // adaptive resolution multiplier
    this.maxPixels = 1.6e6;      // cap on backing-store pixels (keeps 4K consoles light)
    this.time = 0;
    this.input = new AP.Input(canvas);
    this.audio = AP.audio;
    this.input.onGesture = function () { AP.audio.unlock(); };

    // ---- GL helpers -------------------------------------------------------
    function compile(type, src) {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        const log = gl.getShaderInfoLog(s);
        gl.deleteShader(s);
        throw new Error('Shader compile failed:\n' + log);
      }
      return s;
    }
    const vs = compile(gl.VERTEX_SHADER, VERT);

    // Compile a fragment shader (version/precision/hue helpers are prepended).
    this.program = function (fragBody) {
      const fs = compile(gl.FRAGMENT_SHADER, FRAG_HEAD + AP.GLSL_HUE + fragBody);
      const p = gl.createProgram();
      gl.attachShader(p, vs); gl.attachShader(p, fs);
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error('Program link failed:\n' + gl.getProgramInfoLog(p));
      return { p: p, locs: {} };
    };

    function setUniforms(prog, u) {
      gl.useProgram(prog.p);
      let unit = 0;
      for (const k in u) {
        let l = prog.locs[k];
        if (l === undefined) l = prog.locs[k] = gl.getUniformLocation(prog.p, k);
        if (l === null) continue;
        const v = u[k];
        if (typeof v === 'number') gl.uniform1f(l, v);
        else if (v instanceof WebGLTexture) { gl.activeTexture(gl.TEXTURE0 + unit); gl.bindTexture(gl.TEXTURE_2D, v); gl.uniform1i(l, unit++); }
        else if (v.length === 2) gl.uniform2f(l, v[0], v[1]);
        else if (v.length === 3) gl.uniform3f(l, v[0], v[1], v[2]);
        else if (v.length === 4) gl.uniform4f(l, v[0], v[1], v[2], v[3]);
      }
    }

    // Draw a fullscreen pass into `target` (or the screen when null).
    this.draw = function (prog, target, u) {
      setUniforms(prog, u || {});
      gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.fbo : null);
      gl.viewport(0, 0, target ? target.w : canvas.width, target ? target.h : canvas.height);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    };

    // RGBA8 render target, NEAREST filtering (safe on every GPU, packable data).
    this.target = function (w, h) {
      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      const t = { tex: tex, fbo: fbo, w: w, h: h };
      this.clear(t);
      return t;
    };
    this.clear = function (t) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
    };
    this.destroyTarget = function (t) { gl.deleteFramebuffer(t.fbo); gl.deleteTexture(t.tex); };
    // Read back a small block (x,y from bottom-left). Keep it tiny: it stalls the GPU.
    this.read = function (t, x, y, w, h, out) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, out);
      return out;
    };

    // ---- sizing -----------------------------------------------------------
    let mode = null;
    this.resize = function (force) {
      const cw = canvas.clientWidth || window.innerWidth, ch = canvas.clientHeight || window.innerHeight;
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const k = Math.min(dpr, Math.sqrt(self.maxPixels * self.scale / (cw * ch)));
      const w = Math.max(2, Math.round(cw * k)), h = Math.max(2, Math.round(ch * k));
      if (!force && w === self.W && h === self.H) return;
      canvas.width = w; canvas.height = h;
      self.W = w; self.H = h; self.aspect = w / h;
      self.input.setView(cw / ch);
      if (mode) mode.resize(w, h);
    };
    window.addEventListener('resize', function () { self.resize(false); });

    // Drop resolution if the device can't hold ~40fps (never raises again; avoids flip-flopping).
    let accT = 0, accN = 0;
    function adapt(dt) {
      if (dt <= 0) return;
      accT += dt; accN++;
      if (accN < 120) return;
      const avg = accT / accN; accT = 0; accN = 0;
      if (avg > 0.024 && self.scale > 0.5) { self.scale = Math.max(0.5, self.scale * 0.85); self.resize(true); }
    }

    // ---- HUD (plain DOM, kept tiny) --------------------------------------
    const hud = {
      root: document.documentElement,
      mode: document.getElementById('mode'),
      msg: document.getElementById('msg'),
      score: document.getElementById('score'),
      last: {}
    };
    function setText(el, key, text) {
      if (hud.last[key] === text) return;
      hud.last[key] = text; el.textContent = text;
    }

    // ---- state machine ----------------------------------------------------
    let state = 'menu', deadT = 0, modeIdx = 0, best = {};
    try { best = JSON.parse(localStorage.getItem('antipode.best') || '{}'); } catch (_) {}
    function saveBest() { try { localStorage.setItem('antipode.best', JSON.stringify(best)); } catch (_) {} }

    function selectMode(i) {
      if (mode && mode.dispose) mode.dispose();
      modeIdx = (i + AP.modes.length) % AP.modes.length;
      mode = AP.modes[modeIdx].create(self);
      mode.resize(self.W, self.H);
      mode.reset();
      state = 'menu';
    }
    this.selectModeById = function (id) {
      const i = AP.modes.findIndex(function (m) { return m.id === id; });
      if (i >= 0) selectMode(i);
    };
    this.getState = function () { return state; };
    this.getMode = function () { return mode; };

    function begin() { mode.reset(); state = 'play'; AP.audio.start(); }
    function die() {
      state = 'dead'; deadT = 0;
      const id = AP.modes[modeIdx].id;
      if (mode.score > (best[id] || 0)) { best[id] = mode.score; saveBest(); }
      AP.audio.die();
    }

    function drawHud() {
      const def = AP.modes[modeIdx], id = def.id;
      hud.root.style.setProperty('--hd', String(Math.round(mode.hue * 360)));
      setText(hud.mode, 'mode', def.name);
      setText(hud.score, 'score', state === 'menu' ? '' : String(mode.score));
      let msg = '';
      if (state === 'menu') msg = def.hint + '\n\nPress any key · click · tap · A';
      else if (state === 'dead' && deadT > 0.6) msg = 'Best ' + (best[id] || 0) + '\n\nPress to retry';
      setText(hud.msg, 'msg', msg);
      hud.root.dataset.state = state;
    }

    // ---- main loop --------------------------------------------------------
    let last = 0;
    function frame(ts) {
      requestAnimationFrame(frame);
      const dt = last ? Math.min(0.05, (ts - last) / 1000) : 0;
      last = ts;
      self.time += dt;
      adapt(dt);
      const input = self.input;
      input.poll();
      const act = input.takeAction(), cyc = input.takeCycle();
      if (state === 'menu') {
        if (cyc && AP.modes.length > 1) selectMode(modeIdx + cyc);
        else if (act) begin();
      } else if (state === 'dead') {
        deadT += dt;
        if (act && deadT > 0.6) begin();
      }
      const dead = mode.step(dt, input, state === 'play');
      if (state === 'play' && dead) die();
      mode.draw(self.time, state === 'dead' ? deadT : -1);
      drawHud();
    }

    this.start = function () {
      self.resize(true);
      const want = new URLSearchParams(location.search).get('mode');
      const i = Math.max(0, AP.modes.findIndex(function (m) { return m.id === want; }));
      selectMode(i);
      requestAnimationFrame(frame);
    };
  }

  AP.Engine = Engine;
})(window.AP = window.AP || {});
