/* Antipode — mode 1: Afterimage.
 * You glide through the dark leaving a trail in your hue. The trail slowly drifts
 * around the color wheel and ripens into your complement — then it is lethal.
 * Touch ripe trail, or the complement-colored border, and you're gone. Collect sparks.
 *
 * All state that matters lives in a GPU texture (RGBA8):
 *   r = hue at stamp, g/b = 16-bit birth time (mod 64s), a = present.
 * Age is derived from birth time, so nothing needs a per-frame decay write and
 * plain RGBA8 is exact on every GPU. Collision is a tiny readPixels around the head.
 */
(function (AP) {
  'use strict';

  const LIFE = 3.2;            // seconds for a stamp to reach the complement
  const LETHAL_FROM = 0.88;    // fraction of LIFE where it turns deadly
  const RADIUS = 0.035;        // head radius (world units; world height = 2)
  const BORDER = 0.035;        // lethal border thickness
  const TURN = 3.4;            // rad/s
  const WRAP64 = 64;

  const UPDATE = `
uniform sampler2D uPrev;
uniform vec2 uA, uB;
uniform float uR, uAspect, uNow, uHue, uT, uFadeEnd, uStamp;
void main(){
  vec4 c = texture(uPrev, vUv);
  if (c.a > 0.5) {
    float n = floor(c.g * 255.0 + 0.5) * 256.0 + floor(c.b * 255.0 + 0.5);
    float age = mod(uNow - n / 65535.0 * 64.0 + 64.0, 64.0);
    if (age > uFadeEnd * uT) c = vec4(0.0);
  }
  if (uStamp > 0.5) {
    vec2 w = (vUv * 2.0 - 1.0) * vec2(uAspect, 1.0);
    vec2 pa = w - uA, ba = uB - uA;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    if (length(pa - ba * h) < uR) {
      float n = floor(uNow / 64.0 * 65535.0);       // floor: birth is never "in the future"
      float hi = floor(n / 256.0);
      c = vec4(uHue, hi / 255.0, (n - hi * 256.0) / 255.0, 1.0);
    }
  }
  outColor = c;
}`;

  const DISPLAY = `
uniform sampler2D uTrail;
uniform vec2 uRes, uPlayer, uPick;
uniform float uAspect, uNow, uHue, uT, uFadeEnd, uLethal, uBorder, uR, uTime, uDeath;

// Trail sample -> rgb (emissive) + lethal flag
vec4 trail(vec2 uv, float shift){
  vec4 c = texture(uTrail, uv);
  if (c.a < 0.5) return vec4(0.0);
  float n = floor(c.g * 255.0 + 0.5) * 256.0 + floor(c.b * 255.0 + 0.5);
  float age = mod(uNow - n / 65535.0 * 64.0 + 64.0, 64.0);
  float f = age / uT;
  float h = c.r + 0.5 * clamp(f, 0.0, 1.0) + shift;
  float fade = 1.0 - smoothstep(uLethal + 0.12, uFadeEnd, f);
  float lethal = step(uLethal, f) * fade;
  vec3 col = hsv2rgb(vec3(h, 0.82, 1.0 - 0.15 * clamp(f, 0.0, 1.0))) * fade;
  return vec4(col, lethal);
}

void main(){
  vec2 w = (vUv * 2.0 - 1.0) * vec2(uAspect, 1.0);
  float dd = length(w - uPlayer);

  // On death a ring of "everything flips to its complement" expands from the player.
  float ringR = uDeath * 2.4;
  float flipped = (uDeath >= 0.0) ? step(dd, ringR) : 0.0;
  float shift = flipped * 0.5;

  // background: dim, hue-tinted, gently breathing
  float v = length(vUv - 0.5);
  vec3 col = hsv2rgb(vec3(uHue + shift, 0.55, 0.075 - 0.07 * v * v * 2.0 + 0.012 * sin(uTime * 0.6 + w.x * 3.0 + w.y * 2.0)));

  // trail + cheap 8-tap glow
  vec4 t = trail(vUv, shift);
  vec2 px = 1.0 / uRes;
  float gr = uRes.y * 0.007;
  vec3 glow = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    float a = float(i) * 1.5708 + 0.785398;
    vec2 d = vec2(cos(a), sin(a));
    glow += trail(vUv + d * px * gr, shift).rgb;
    glow += trail(vUv + d * px * gr * 2.4, shift).rgb * 0.6;
  }
  col += t.rgb + glow * 0.16;
  col += t.a * (0.22 + 0.12 * sin(uTime * 22.0 + dd * 40.0));   // ripe trail shimmers hot

  // lethal border in the complement
  float edge = min(uAspect - abs(w.x), 1.0 - abs(w.y));
  float core = 1.0 - smoothstep(uBorder - 0.008, uBorder, edge);
  float halo = exp(-max(edge - uBorder, 0.0) * 16.0) * 0.32;
  col += hsv2rgb(vec3(uHue + 0.5 + shift, 0.9, 1.0)) * (core + halo) * (0.82 + 0.18 * sin(uTime * 3.0));

  // pickup spark
  if (uPick.x < 50.0) {
    float pk = length(w - uPick);
    float ring = exp(-pow((pk - 0.038 - 0.006 * sin(uTime * 5.0)) * 110.0, 2.0));
    float dot_ = exp(-pk * pk * 9000.0);
    col += hsv2rgb(vec3(uHue + shift, 0.6, 1.0)) * (ring * 0.9 + dot_) + exp(-pk * 14.0) * 0.06;
  }

  // head
  float disc = 1.0 - smoothstep(uR * 0.8, uR, dd);
  col += hsv2rgb(vec3(uHue + shift, 0.75, 1.0)) * exp(-dd * dd / (uR * uR * 7.0)) * 0.7;
  col = mix(col, hsv2rgb(vec3(uHue + shift, 0.25, 1.0)), disc);

  // death ring + flash
  if (uDeath >= 0.0) {
    float e = exp(-pow((dd - ringR) * 38.0, 2.0)) * exp(-uDeath * 1.6);
    col += hsv2rgb(vec3(uHue + 0.5, 0.7, 1.0)) * e * 1.4;
    col += vec3(exp(-uDeath * 9.0) * 0.35);
  }
  outColor = vec4(min(col, 1.0), 1.0);
}`;

  function wrapAngle(a) {
    const t = Math.PI * 2;
    return ((a + Math.PI) % t + t) % t - Math.PI;
  }

  AP.modes.push({
    id: 'afterimage',
    name: 'Afterimage',
    hint: 'Your trail ripens into the one color you cannot touch.\nSteer: WASD / stick / mouse / touch',

    create: function (e) {
      const upd = e.program(UPDATE);
      const dsp = e.program(DISPLAY);
      let A = null, B = null;
      let W = 2, H = 2;
      const buf = new Uint8Array(4 * 25 * 25);

      const s = {
        hue: Math.random(), score: 0, clock: 0,
        x: 0, y: 0, px: 0, py: 0, ang: Math.PI / 2,
        pick: [99, 99]
      };

      function fadeEnd() { return 1.25 + Math.min(s.score, 20) * 0.03; }

      function spawnPick() {
        const ax = e.aspect - 0.18, ay = 0.82;
        for (let i = 0; i < 20; i++) {
          const x = (Math.random() * 2 - 1) * ax, y = (Math.random() * 2 - 1) * ay;
          if (Math.hypot(x - s.x, y - s.y) > 0.5) { s.pick = [x, y]; return; }
        }
        s.pick = [-s.x * 0.6, -s.y * 0.6];
      }

      // Is there ripe trail under the head? (reads a small pixel block)
      function trailLethal() {
        const rad = Math.max(1, Math.min(11, Math.floor(RADIUS * H * 0.5 * 0.7)));
        const cx = Math.round((s.x / e.aspect * 0.5 + 0.5) * W);
        const cy = Math.round((s.y * 0.5 + 0.5) * H);
        const x0 = Math.max(0, cx - rad), y0 = Math.max(0, cy - rad);
        const x1 = Math.min(W - 1, cx + rad), y1 = Math.min(H - 1, cy + rad);
        const bw = x1 - x0 + 1, bh = y1 - y0 + 1;
        if (bw < 1 || bh < 1) return false;
        e.read(A, x0, y0, bw, bh, buf);
        const now = s.clock % WRAP64, fe = fadeEnd();
        for (let i = 0; i < bw * bh; i++) {
          if (buf[i * 4 + 3] < 128) continue;
          const n = buf[i * 4 + 1] * 256 + buf[i * 4 + 2];
          const age = (((now - n / 65535 * 64) % WRAP64) + WRAP64) % WRAP64;
          const f = age / LIFE;
          if (f >= LETHAL_FROM && f < fe) return true;
        }
        return false;
      }

      function stampPass(stamp) {
        e.draw(upd, B, {
          uPrev: A.tex, uA: [s.px, s.py], uB: [s.x, s.y], uR: RADIUS, uAspect: e.aspect,
          uNow: s.clock % WRAP64, uHue: s.hue, uT: LIFE, uFadeEnd: fadeEnd(), uStamp: stamp ? 1 : 0
        });
        const t = A; A = B; B = t;
      }

      return {
        get score() { return s.score; },
        get hue() { return s.hue; },

        resize: function (w, h) {
          W = w; H = h;
          if (A) { e.destroyTarget(A); e.destroyTarget(B); }
          A = e.target(w, h); B = e.target(w, h);
        },

        reset: function () {
          s.hue = Math.random(); s.score = 0;
          s.x = s.px = 0; s.y = s.py = 0; s.ang = Math.PI / 2;
          e.clear(A); e.clear(B);
          spawnPick();
        },

        // Returns true when the player has died this frame.
        step: function (dt, input, playing) {
          s.clock += dt;
          if (!playing) { s.px = s.x; s.py = s.y; stampPass(false); return false; }

          const st = input.steerFor(s.x, s.y);
          if (st[2] > 0.05) {
            const d = wrapAngle(Math.atan2(st[1], st[0]) - s.ang);
            const m = TURN * dt * (0.35 + 0.65 * st[2]);
            s.ang += Math.max(-m, Math.min(m, d));
          }
          const speed = 0.55 + Math.min(s.score, 20) * 0.012;
          s.px = s.x; s.py = s.y;
          s.x += Math.cos(s.ang) * speed * dt;
          s.y += Math.sin(s.ang) * speed * dt;

          // lethal border (the complement)
          const edge = Math.min(e.aspect - Math.abs(s.x), 1 - Math.abs(s.y));
          if (edge < BORDER + RADIUS * 0.3) { stampPass(false); return true; }
          // lethal trail
          if (trailLethal()) { stampPass(false); return true; }

          // spark
          if (Math.hypot(s.x - s.pick[0], s.y - s.pick[1]) < RADIUS + 0.04) {
            s.score++;
            e.audio.pickup(s.score);
            spawnPick();
          }
          stampPass(true);
          return false;
        },

        draw: function (t, deadAge) {
          const idle = Math.sin(t * 2) * 0.004;
          e.draw(dsp, null, {
            uTrail: A.tex, uRes: [W, H], uPlayer: [s.x, s.y], uPick: s.pick,
            uAspect: e.aspect, uNow: s.clock % WRAP64, uHue: s.hue, uT: LIFE,
            uFadeEnd: fadeEnd(), uLethal: LETHAL_FROM, uBorder: BORDER,
            uR: RADIUS + (deadAge < 0 && e.getState() === 'menu' ? idle : 0),
            uTime: t, uDeath: deadAge
          });
        },

        dispose: function () { if (A) { e.destroyTarget(A); e.destroyTarget(B); } A = B = null; }
      };
    }
  });
})(window.AP = window.AP || {});
