/* Antipode — tiny WebAudio synth. No assets. */
(function (AP) {
  'use strict';

  let ctx = null, master = null, muted = false;
  const PENT = [0, 2, 4, 7, 9];

  function unlock() {
    if (!ctx) {
      const C = window.AudioContext || window.webkitAudioContext;
      if (!C) return null;
      ctx = new C();
      master = ctx.createGain();
      master.gain.value = 0.22;
      master.connect(ctx.destination);
    }
    if (ctx.state === 'suspended') ctx.resume();
    return ctx;
  }

  function tone(f0, f1, dur, type, vol, delay) {
    if (!ctx || muted) return;
    const t = ctx.currentTime + (delay || 0);
    const o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type || 'sine';
    o.frequency.setValueAtTime(f0, t);
    if (f1 && f1 !== f0) o.frequency.exponentialRampToValueAtTime(f1, t + dur);
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(vol, t + 0.01);
    g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g); g.connect(master);
    o.start(t); o.stop(t + dur + 0.03);
  }

  AP.audio = {
    unlock: unlock,
    toggleMute: function () { muted = !muted; },
    start: function () { tone(330, 660, 0.18, 'triangle', 0.5); },
    pickup: function (n) {
      const semi = PENT[n % 5] + 12 * Math.floor(n / 5 % 2);
      const f = 523.25 * Math.pow(2, semi / 12);
      tone(f, f, 0.16, 'sine', 0.6);
      tone(f * 2, f * 2, 0.1, 'triangle', 0.2, 0.04);
    },
    // A tritone — the "antipode" of the octave — falling away.
    die: function () {
      tone(440, 60, 0.6, 'sawtooth', 0.4);
      tone(622.25, 80, 0.6, 'square', 0.22);
    }
  };
})(window.AP = window.AP || {});
