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

## The season

It is Christmas by default (`?theme=classic` is the plain game). A theme layer in the core does it, and no scene's rules know about it:

- **A painter's wheel.** The engine's "opposite" is hue + 180° on a ring. In season every hue goes through one table (`theme_wheel` in
  `gfx_hsv`) that lays the ring out as red, orange, yellow, green, blue, violet, so the opposite of red is green and of gold is
  blue. You play **red/green** (the default) or **gold/blue**: `T`, the pad's Y, or a tap on the corner of a title screen
  switches, and the choice is kept (`?pair=red` or `?pair=gold` forces one). Each run you are one of the pair's two colours.
- **Sparkler** (Afterimage): the trail is a wire of fairy lights, a bulb every few pixels, that ripens red-orange-yellow-green; the
  border is a candy cane; sparks are baubles; the head is a sparkler; death throws shards.
- **Advent Run** (Prism Run): gold ribbon on the ring edges, gingham paper, a star for a ship; a ring is a day (`DAY 12`), with a
  banner at the twenty-fourth (Christmas Eve) and the twenty-fifth.
- **Sleigh Bells** (Tidepool): the pool's sources are bells that swing before they ring; you are a sleigh; pearls are snowflakes.
- **Lantern** (Moth): you are a lantern; the dark is frost, paler; a blob that has grown gets a snowman's face (coal eyes, a carrot
  nose toward you, a smile, buttons, and a hat on the big ones).
- **Danger is also a pattern.** Red and green are the hardest pair for many colour-blind players, and the rule of the game is a colour.
  Whatever kills is therefore striped like a candy cane as well as hot (ripe trail, deadly cells, water standing up, the frost's body),
  and the two colours also differ in brightness.
- **Snow** falls on the title and game-over screens and through a scene change, never in play (a flake must not hide what kills you).
- **Carols**, our own arrangements of public-domain tunes: bells over a triangle bass, sleigh-bell shakes, a ping-pong delay, a key
  change each time round (Silent Night a minor third up, Jingle Bells and Deck the Halls climbing). Silent Night waits on a title or
  game-over screen; Sparkler plays Jingle Bells in the minor (the third, sixth and seventh lowered), quickening with your score;
  Advent Run plays Deck the Halls at the pace of the flight; Sleigh Bells plays Joy to the World's falling scale with an answer
  of ours; Lantern plays We Wish You a Merry Christmas in the minor and each merge lifts it a semitone. Sound effects are bells,
  a shake, and a cracked bauble.

## The games

Names below are the plain ones; in season they are Sparkler, Advent Run, Sleigh Bells and Lantern (`?mode=` takes either).

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

- **Plain mode is exact.** With the season off (`HOST_THEME=classic`) every bot gives the same numbers as before the season existed
  (Afterimage best 7, 5 deaths in 60 s; Prism Run 316 and 1 in 120 s, 385 and 4 in 300 s; Tidepool 8 and 6; Moth 8 and 5). The
  season uses the same random stream (it always draws the colour, then maps it), so a seed means the same game in either.
- **In season, both pairs:** each bot plays all four scenes for 150 s (Prism Run 362 and 206 rings; the others as before); 0.2 to 0.3 ms
  a frame natively. Sanitizers (AddressSanitizer and UBSan), every scene in both modes, the audio path, switching and the three ways
  of toggling the pair: clean.
- **The carols render as written:** `make test-audio` renders each tune alone and checks every note against its intended pitch,
  across loops (so the key changes and the minor mode too): 542 notes, none more than 40 cents off. No clipping, no silence; a soft
  limiter rounds the loud moments.
- **Chromium, headless:** season on by default; both pairs and `?pair=`; the choice kept across a reload; `?theme=classic`; the season's
  `?mode=` names; a tap on the corner, a side tap and a key all do what they should; no errors.
- **Found by testing, and fixed:**
  - The page's list of key names was missing Q and E (an earlier patch silently did nothing), so any key outside the list was sent as
    the wrong key and, on a title screen, switched scene instead of starting. The page now refuses to run if its list and the game's
    differ in length, and the build asserts it.
  - The page saved only the first scene's best score; the other scenes' bests (and now the pair) were lost on reload. It saves all 16
    bytes and migrates an old save.
  - Afterimage's game-over screen was not a place to switch scenes or hear the title carol; it is now, like the others.
- **Not tested, and what needs a person:**
  - **The carols by ear.** The tunes are written from memory as our own arrangements; only Deck the Halls was checked against a
    transcription I could find (its first eight bars, the Welsh Nos Galan tune). Silent Night's rhythm in particular is simplified. The
    test proves the synth plays the score, not that the score is right: please listen.
  - A colour-blind player or a simulator on the red/green pair; an Xbox; a real controller or touch; sound over a speaker.
  - Balance in season: the scenes' rules are unchanged, but the sprites and stripes make a few things (a thin fairy-light wire, a
    bauble) read differently from the plain game's.
