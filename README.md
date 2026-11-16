# Antipode

A tiny, entirely shader-driven game. You are a hue. **Don't touch your complement** (hue + 180°).

No engine, no assets, no build step: WebGL2 (automatic WebGL1 fallback; force it with `?gl=1`) + one fullscreen triangle + a few KB of JS.
Runs in any modern browser (target: Xbox/PS5 browsers) and, later, as a small Windows exe.

> **No GPU?** [`retro/`](retro/) is the same game on a CPU-only, 256-colour, 320x200 engine (one ~43 KB page, no WebGL). It is the version for the Xbox browser.

## Run

Open `index.html` in a browser (works from `file://`), or serve the folder:

```bash
python3 -m http.server 8080
```

## Controls

One steering vector, four devices:

| Device   | Steer                       | Start / retry        |
|----------|-----------------------------|----------------------|
| Keyboard | WASD / arrows               | any key              |
| Mouse    | move the cursor             | click                |
| Touch    | hold a finger where to go   | tap                  |
| Gamepad  | left stick / d-pad          | A / Start            |

`Q` / `E` (or LB / RB) switch modes from the menu, `F` fullscreen, `M` mute.
`?mode=<id>` in the URL selects a mode.

## Layout

```
index.html          canvas + tiny DOM HUD
src/hue.js          hue helpers + shared GLSL chunk (hsv2rgb, complementOf, hueDist, isComplement)
src/engine.js       GL setup, pass/target helpers, adaptive resolution, loop, menu/play/dead states
src/input.js        keyboard/mouse/touch/gamepad -> steerFor(x, y)
src/audio.js        WebAudio oscillator sfx
modes/afterimage.js mode 1
```

A mode registers `{ id, name, hint, create(engine) }` in `AP.modes`; `create` returns
`{ resize, reset, step(dt, input, playing) -> dead, draw(t, deadAge), score, hue }`.
Adding a mode = one JS file with its shaders inline + one `<script>` tag.

## Modes

- **Afterimage** — your trail ripens into your complement, then it's lethal. Game state lives in an
  RGBA8 texture (hue + packed birth time); collision is a tiny `readPixels` at the head.

Planned: Antipode (flowing hue field), Prism Run (raymarched tunnel), Tidepool (wave interference), Moth (metaballs).

## Roadmap

- Windows exe via a Tauri / WebView2 wrapper (`desktop/`)
- More modes, shader-rendered score digits, console-browser tuning (PS5 / Xbox)
