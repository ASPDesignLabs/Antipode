# Antipode (retro)

The same game on a **CPU-only, 256-colour, 320x200** engine: no GPU, no WebGL, no WebGPU, no shaders. It exists because
the Xbox's Edge refuses every GPU API (WebGL2, WebGL1 and WebGPU all failed there), while a plain 2D canvas works.

The core is vendored and slimmed from [HAUNTED.EXE](https://github.com/ASPDesignLabs/HAUNTED.EXE) (`src/core`): an 8-bit
indexed `Frame`, a 6-bit VGA DAC palette, `app_present` turning it into RGBA through a 256-entry table. What was
screensaver-only is gone (scene rotation, hold-H, touch stage, rumble, the ledger); a single scene needs an absolute pointer,
an "any button" edge and one saved number, which are added. The fonts are HAUNTED.EXE's too
(`third_party/font`, CC BY-SA 4.0).

## Build and run

```bash
make host     # build/host: PNG frames, timing, a scripted bot (needs a C compiler and zlib)
make smoke    # a quick headless run of it
make web      # build/web/index.html: the whole game as ONE file, ~43 KB
```

`make web` needs only `clang` with the wasm32 target and `wasm-ld` (no Emscripten, no libc, no libm: `core.h` carries its
own `m_sin`/`m_atan2`, so every build computes the same numbers). The page embeds the module and runs from `file://`.
The built page is committed as `play/index.html` so GitHub Pages can serve it; rebuild it with `make web && cp build/web/index.html play/` after changing anything. Add `?perf` to the address for step and draw times on screen.

```bash
HOST_INPUT="0.5:+SPACE;1:+RIGHT;1.6:-RIGHT" ./build/host shot 2,4 build/smoke/play 3
./build/host bot 60          # a bot steers at the spark; prints deaths and its best
./build/host bench 20        # ms per frame
```

## How Afterimage works here

Every pixel is a palette index: `index = 16 + slot * 4 + level`. `slot` is the tick a pixel was stamped on (a ring of 60
birth times, 11.25 a second), `level` a brightness step (halo, body, core: a glow is index arithmetic). Each frame the
palette is rewritten so a slot's colour is a function of its age, `hue = yours + 0.5 * age / 36`, so the trail recolours itself
toward your complement for 240 palette entries of work and no per-pixel update. Past 32 ticks a stamp is lethal; the danger
is a 256-entry table asked of the pixels under the head (CPU collision, no read-back). Once a tick one sweep returns the
expired slot to the background. Indices 0..15: an 8-step dithered background ramp, the border (which pulses in the
palette), the spark, the head, the death ring and the text. On death the whole palette's hue turns half a circle: the world
goes to its antipode.

## Controls

Keyboard (WASD / arrows), mouse (head for the cursor), touch (head for the finger), gamepad (left stick / d-pad). Any key,
click, tap or button starts and retries. `F` fullscreen, `M` mute.

## What has been tested

- **Native harness:** builds without warnings; a scripted bot collects sparks and dies on ripe trail; about 0.06 ms a frame.
- **Chromium, headless:** the web build runs; keyboard, mouse and a simulated gamepad drive it; about 0.15 ms a frame.
- **Not tested:** an Xbox, a real controller, touch, or whether the sound is audible (the audio path ran without errors
  but nobody has listened). Game feel and balance are first guesses.
