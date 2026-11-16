/* Antipode — unified input.
 * Keyboard, mouse, touch and gamepad all collapse into one thing:
 *   steerFor(x, y) -> [dirX, dirY, magnitude]   (world units, y up)
 * plus two edge-triggered buttons: takeAction() and takeCycle().
 */
(function (AP) {
  'use strict';

  function Input(canvas) {
    const keys = new Set();
    const ptr = { x: 0, y: 0, active: false };
    const axis = { x: 0, y: 0 };
    let aspect = 1;
    let action = false;
    let cycle = 0;
    let padPrev = { a: false, lb: false, rb: false };
    const self = this;
    this.onGesture = null;

    function gesture() { if (self.onGesture) self.onGesture(); }

    function setPointer(e) {
      const r = canvas.getBoundingClientRect();
      ptr.x = ((e.clientX - r.left) / r.width * 2 - 1) * aspect;
      ptr.y = -((e.clientY - r.top) / r.height * 2 - 1);
    }

    window.addEventListener('keydown', function (e) {
      if (e.repeat) return;
      const k = e.key.toLowerCase();
      if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' '].indexOf(k) >= 0) e.preventDefault();
      keys.add(k);
      ptr.active = false;                       // keyboard takes over from mouse
      gesture();
      if (k === 'q') cycle = -1;
      else if (k === 'e') cycle = 1;
      else if (k === 'f') toggleFullscreen();
      else if (k === 'm') AP.audio.toggleMute();
      else action = true;                       // any other key starts / retries
    });
    window.addEventListener('keyup', function (e) { keys.delete(e.key.toLowerCase()); });
    window.addEventListener('blur', function () { keys.clear(); });

    canvas.addEventListener('pointerdown', function (e) {
      setPointer(e); ptr.active = true; action = true; gesture();
      try { canvas.setPointerCapture(e.pointerId); } catch (_) {}
    });
    canvas.addEventListener('pointermove', function (e) {
      setPointer(e);
      if (e.pointerType === 'mouse') ptr.active = true;
    });
    function up(e) { if (e.pointerType !== 'mouse') ptr.active = false; }
    canvas.addEventListener('pointerup', up);
    canvas.addEventListener('pointercancel', up);
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });

    function toggleFullscreen() {
      try {
        if (document.fullscreenElement) document.exitFullscreen();
        else document.documentElement.requestFullscreen();
      } catch (_) {}
    }

    this.setView = function (a) { aspect = a; };

    // Call once per frame.
    this.poll = function () {
      let x = 0, y = 0;
      if (keys.has('a') || keys.has('arrowleft')) x -= 1;
      if (keys.has('d') || keys.has('arrowright')) x += 1;
      if (keys.has('w') || keys.has('arrowup')) y += 1;
      if (keys.has('s') || keys.has('arrowdown')) y -= 1;

      const pads = navigator.getGamepads ? navigator.getGamepads() : [];
      for (let i = 0; i < pads.length; i++) {
        const p = pads[i];
        if (!p || !p.connected) continue;
        let px = p.axes[0] || 0, py = -(p.axes[1] || 0);
        const m = Math.hypot(px, py);
        if (m < 0.2) { px = 0; py = 0; } else { const s = (m - 0.2) / 0.8 / m; px *= s; py *= s; }
        if (p.buttons[14] && p.buttons[14].pressed) px -= 1;
        if (p.buttons[15] && p.buttons[15].pressed) px += 1;
        if (p.buttons[12] && p.buttons[12].pressed) py += 1;
        if (p.buttons[13] && p.buttons[13].pressed) py -= 1;
        if (px || py) { x = px; y = py; ptr.active = false; }
        const a = !!((p.buttons[0] && p.buttons[0].pressed) || (p.buttons[9] && p.buttons[9].pressed));
        const lb = !!(p.buttons[4] && p.buttons[4].pressed);
        const rb = !!(p.buttons[5] && p.buttons[5].pressed);
        if (a && !padPrev.a) { action = true; gesture(); }
        if (lb && !padPrev.lb) cycle = -1;
        if (rb && !padPrev.rb) cycle = 1;
        padPrev = { a: a, lb: lb, rb: rb };
        break;                                  // first connected pad only
      }
      const m = Math.hypot(x, y);
      if (m > 1) { x /= m; y /= m; }
      axis.x = x; axis.y = y;
    };

    // Steering intent relative to a position in world space.
    this.steerFor = function (px, py) {
      const am = Math.hypot(axis.x, axis.y);
      if (am > 0.01) return [axis.x / am, axis.y / am, Math.min(1, am)];
      if (ptr.active) {
        const dx = ptr.x - px, dy = ptr.y - py;
        const d = Math.hypot(dx, dy);
        if (d > 0.03) return [dx / d, dy / d, Math.min(1, d / 0.15)];
      }
      return [0, 0, 0];
    };

    this.takeAction = function () { const a = action; action = false; return a; };
    this.takeCycle = function () { const c = cycle; cycle = 0; return c; };
  }

  AP.Input = Input;
})(window.AP = window.AP || {});
