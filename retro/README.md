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
`?mode=afterimage`, `?mode=prism-run`, `?mode=tidepool` or `?mode=moth` starts on one.

- **Afterimage**: you glide leaving a trail that ripens into your complement, then kills.
- **Prism Run**: you fly down a tunnel of rainbow cells and roll it left and right. Your own colour drifts round the wheel;
  its opposite kills and glows hot wherever it is. Stay on your own colour (it is always a way through, two cells wide). It starts at
  1.5 rings a second and reaches 6 by the 150th.
- **Tidepool**: you skim a dark pool leaving a wake; sources at its edges answer with waves in your opposite colour. Where the two
  stand up together the water kills. Your wake grows with your speed, so slow water is safe water. Collect pearls; the pool pulses
  faster and gathers more sources as you do.
- **Moth**: you are a point of light in a dark that answers in your opposite. Blobs of anti-light chase you; touch one and you
  are gone. They are metaballs: near each other their fields join in a bridge, and touching they merge into one bigger, slower
  blob. Lure them together and slip round what is left. A merge is a point; the dark sends more the longer you last.

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

### How Tidepool works here

Two small integer wave simulations run on a 160x100 grid (the old ripple algorithm: half the sum of the four neighbours minus the
last, a little damping), one for your wake and one for the pool's. They pass through each other as real waves do. A cell is one
palette index made of both heights, `16 + level(yours) * 15 + level(the pool's)`: a **two-dimensional palette**, so blending two
things is a lookup and nothing is mixed per pixel. The palette gives each combination its colour: your hue where your crests are,
its opposite where the pool's are, the hues between where they overlap, white-hot where they stand up together. Yours is quantised
three times finer than the pool's, because a wake is low. The lethal test reads the true heights: the two together at 1,800 kill.
A wake is raised to your speed (not added to), so it never piles up on a slow boat; at full speed it adds about 700, which is why
moving through a rising crest is deadly and standing in it is not.

### How Moth works here

Nothing is drawn per blob. Each is *splatted* into a field through a precomputed radial kernel, `(1 - (d/R)^2)^2` with `R` 2.2 times
the radius and scaled so the surface (field = 1) sits at the radius, by one table lookup a pixel; two blobs' bridge is just the sum
of their two skirts. Your own light is a second small field. A pixel is one palette index made of both, `16 + level(your light) * 20
+ level(the dark)`, 240 entries, so the palette draws the soft aura below the surface, the hot rim at it, the solid body inside, your
hue where your light falls and the hues between where the two overlap. Touching blobs merge by area, keeping their momentum; a blob
past a size no longer merges but pushes apart, so the dark has a limit. The lethal test is the field's value under the point of light.

## Controls

Keyboard (WASD / arrows), mouse (Afterimage heads for the cursor; Prism Run rolls toward its side), touch (the same, by finger),
gamepad (left stick / d-pad). Any key, click, tap in the middle, or button starts and retries. `F` fullscreen, `M` mute.

## What has been tested

- **Native harness:** builds without warnings. Afterimage's bot gives the same runs as before the other scenes existed (same
  seed: best 7, 5 deaths in 60 s). Prism Run's bot, which follows its own colour, scored 385 rings with 4 deaths in 300 s at the
  new, slower start (1.5 rings a second, measured: 6 rings at 5 s, 15 at 10 s, 68 at 30 s, the old top speed by ring 150). Tidepool's
  bot, which heads for pearls and slows in rising water, collects up to 8 a life with about one death in 20 s; with no input you
  are safe (it is a pool, not a clock: there is nothing to do but collect). About 0.05 ms (Afterimage), 0.2 ms (Prism Run) and 0.25 ms
  (Tidepool) a frame natively; Moth about 0.13 ms.
- **Sanitizers:** every scene, 90 s of its bot, and scripted switching through all four with taps and the wrap, under AddressSanitizer
  and UBSan: clean.
- **Moth, native:** the bot (which keeps clear of the dark, more of the big blobs, and the walls) scores up to 8 merges and lasts
  about 16 s a life, 18 deaths in 300 s; with no input you die in about 7 s. About 0.13 ms a frame.
- **Chromium, headless:** the web build runs; keyboard, mouse (hover, click, drag) and a simulated gamepad drive it. Every way of
  switching scenes works, through all four and round the ends; `?mode=` picks one. No errors; about 0.04 ms to step and 0.32 ms
  to draw Moth.
- **Found by testing, and fixed:** Prism Run's first rings could be unflyable (a one-cell way that shifted sideways between rings;
  lethal hues next to the opposite that flipped as your colour drifted). Tidepool's own wake killed the boat on its own when it
  moved slowly (the wake was added every step and piled up), and its first waves were too faint to ever be lethal.
- **Not tested:** an Xbox, a real controller, real touch, or whether the sound is audible. Feel and balance are first guesses;
  Tidepool's strengths (pulse, wake, the lethal height) and Moth's (the blobs' speed, the spawn rate, the merge size) were set by
  measurement and a simple bot, not by a person's hand; a person who lures the blobs together should do better than the bot.
