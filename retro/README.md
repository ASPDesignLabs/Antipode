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

## The games

Left and right (arrow keys, a tap or click at either side, a swipe, the stick or d-pad, Q and E, the bumpers) switch between
them on a title or game-over screen; the screen fades out to black and the next one fades in. (Through black because there
is one palette per frame: two scenes cannot be mixed pixel by pixel.) Mid-run, left and right steer as usual. A small
`< 1/2 >` hint dissolves in when you arrive or move the mouse; a pointer inside a side strip makes that arrow bigger.
`?mode=afterimage` or `?mode=prism-run` starts on one.

- **Afterimage**: you glide leaving a trail that ripens into your complement, then kills.
- **Prism Run**: you fly down a tunnel of rainbow cells and roll it left and right. Your own colour drifts round the wheel;
  its opposite kills and glows hot wherever it is. Stay on your own colour (it is always a way through, two cells wide).

### How Afterimage works here

Every pixel is a palette index: `index = 16 + slot * 4 + level`. `slot` is the tick a pixel was stamped on (a ring of 60
birth times, 11.25 a second), `level` a brightness step (halo, body, core: a glow is index arithmetic). Each frame the
palette is rewritten so a slot's colour is a function of its age, `hue = yours + 0.5 * age / 36`, so the trail recolours itself
toward your complement for 240 palette entries of work and no per-pixel update. Past 32 ticks a stamp is lethal; the danger
is a 256-entry table asked of the pixels under the head (CPU collision, no read-back). Once a tick one sweep returns the
expired slot to the background. Indices 0..15: an 8-step dithered background ramp, the border (which pulses in the
palette), the spark, the head, the death ring and the text. On death the whole palette's hue turns half a circle: the world
goes to its antipode.

### How Prism Run works here

The tunnel is a table: for each of the 64,000 pixels the angle round it and the depth down it are worked out once (a perspective
divide and an atan2). A frame is one pass of lookups, `cell = pattern[ring(depth + z)][segment(angle + roll)]`, and
`index = 16 + hue * 10 + fog`: 24 hues by 10 steps of fog. The rest is palette: the hue wheel, the fog, the glow on whichever hues
are your complement right now (it moves as you drift), and the half-turn flip when you die. The lethal test asks what is drawn
under the ship. Rings are made as you reach them; each has a way through two cells wide that shifts a cell at most from the last,
so two rings always share a safe cell. Lethal cells are your exact opposite, never a hue next to it, because your colour drifts
while you cross a ring and a borderline cell would be safe one moment and deadly the next.

## Controls

Keyboard (WASD / arrows), mouse (Afterimage heads for the cursor; Prism Run rolls toward its side), touch (the same, by finger),
gamepad (left stick / d-pad). Any key, click, tap in the middle, or button starts and retries. `F` fullscreen, `M` mute.

## What has been tested

- **Native harness:** builds without warnings. Afterimage's bot collects sparks and dies on ripe trail (the refactor to several
  scenes gives identical runs for the same seed). Prism Run's bot, which follows its own colour, survives 300 s with 5 deaths and a
  best of 385 rings; with no input you die within about six rings. 0.06 ms (Afterimage) and 0.2 ms (Prism Run) a frame natively.
- **Chromium, headless:** the web build runs; keyboard, mouse (hover, click, drag) and a simulated gamepad drive it, and every way
  of switching scenes works, with no errors.
- **Found by testing, and fixed:** the first Prism Run rings could be unflyable (a one-cell way that moved sideways between rings, and
  lethal hues next to the opposite that flipped as your colour drifted). Both are in the section above.
- **Not tested:** an Xbox, a real controller, real touch, or whether the sound is audible. Feel and balance are first guesses;
  Prism Run speeds up to six rings a second and the late game is hard.
