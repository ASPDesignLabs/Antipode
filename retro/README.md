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

It is Christmas by default (`?theme=classic` is the plain game, exactly as it was). A theme layer in the core does it, and no scene's
rules need to know about colour:

- **A painter's wheel.** The engine's "opposite" is hue + 180° on a ring. In season every hue goes through one table (`theme_wheel` in
  `gfx_hsv`) that lays the ring out as red, orange, yellow, green, blue, violet, so the opposite of red is green and of gold is blue. You
  play **red/green** (the default) or **gold/blue**: `T`, the pad's Y, or a tap on the bottom-right corner of a title screen switches, and
  the choice is kept (`?pair=red` or `?pair=gold` forces one).
- **Lettering, not a terminal.** In season all-capital text is set in friendly mixed case, small text is slanted and shadowed, and titles are
  smoothed (Scale2x), rounded, filled in candy stripes, outlined and shadowed. A scene says which palette indices to use once a frame
  (`gfx_set_style`); the plain game keeps the VGA font.
- **Warm, light backgrounds** (a winter-dusk blue, or deep green for gold and blue; snow and mist, not black), a **garland of twinkling
  bulbs and sparkles** on the title screens, and **snow** falling on title and game-over screens (never in play, except in Caroling Run,
  where it falls through the empty spaces only, so it can never hide a solid cell).
- **A funny game over:** a comic trombone (wah, wah, wah, waaah, sliding down) into a crash of cymbals and kick drum, and presents and
  ornaments tumbling down and bouncing. The title carol returns once the last sound has died away.
- **Bright, major-key carols everywhere**, our own arrangements of public-domain tunes: bells over a triangle bass, sleigh-bell shakes, a
  ping-pong delay, a key change each time round. Silent Night waits on a title or game-over screen. Sparkler plays Jingle Bells,
  quickening with your score; Caroling Run plays Deck the Halls at the pace of the flight; Sleigh Bells plays Joy to the World's falling
  scale with an answer of ours; Lantern plays We Wish You a Merry Christmas and each merge lifts it a semitone; as the night is climbed it gains a shaker, a shimmer, drums and brass, a higher key and a quicker pace, and its last step is Jingle Bells.
- **Danger is also a shape or a pattern, never only a colour** (red and green are the hardest pair for many colour-blind players): ripe
  trail, deadly water and the naughty list's blobs are striped like candy canes; in Caroling Run what kills is an empty space.

## The games

Names below are the plain ones; in season they are Sparkler, Caroling Run, Sleigh Bells and Lantern (`?mode=` takes either).

Left and right (arrow keys, a tap or click at either side, a swipe, the stick or d-pad, Q and E, the bumpers) switch between
them on a title or game-over screen; the screen fades out to black and the next one fades in. (Through black because there
is one palette per frame: two scenes cannot be mixed pixel by pixel.) Mid-run, left and right steer as usual. A small
`< 1/2 >` hint dissolves in when you arrive or move the mouse; a pointer inside a side strip makes that arrow bigger.
`?mode=afterimage`, `?mode=prism-run`, `?mode=tidepool` or `?mode=moth` starts on one.

- **Afterimage** (in season **Sparkler**): you glide leaving a trail that ripens into your complement, then kills. The trail is a wire of
  fairy lights that goes red-orange-yellow-green; the border is a candy cane; sparks are baubles; your head is a sparkler.
- **Prism Run** (in season **Caroling Run**): you fly down a tunnel and roll it left and right. *Plain:* rainbow cells, and your own colour
  drifts round the wheel; its opposite kills and glows hot. It starts at 1.5 rings a second and reaches 6 by the 150th. *In season:* the
  walls are red candy canes, holly with berries and white snow, edged in garland; **what kills is an empty space**, a hollow panel with
  a snowy sky showing through, rimmed in the colour you may not touch. You are red for a verse (24 rings) and green for the next, and
  the other colour is always hollow; solid cells are always safe. The snow beyond falls faster as you do (about 90 pixels a second at
  the start, 300 at full speed). "Spread Christmas cheer while avoiding obstacles."
- **Tidepool** (in season **Sleigh Bells**): you skim a pond leaving a wake; sources at its edges answer with waves in your opposite colour.
  Where the two stand up together the water kills. Your wake grows with your speed, so slow water is safe water. The sources are bells
  that swing before they ring, you are a sleigh, and pearls are snowflakes.
- **Moth** (in season **Lantern**): you are a point of light (a lantern) and blobs chase you; touch one and you are gone. They are
  metaballs: near each other their fields join in a bridge, and touching they merge into one bigger, slower blob. In season it is more of a
  game: a blob that grows into a **snowman** (face, carrot nose and, when big, a hat) is **finished** at full size: +5, a fanfare, and it
  waddles off leaving a **present**. A present gives a six-second effect: **hot cocoa** (you are quicker, your light brighter), a **candy
  cane** (the naughty list freezes) or a **magnet** (the blobs draw together, so they merge). Merges in quick succession build a **combo**
  (1, 2, 3 up to 5 a merge).

  **The night is climbed in steps, by score**: Little Nibbles, Stocking Stuffers, Naughty List, Christmas Eve and Midnight (Nice List at 0,
  15, 40, 80 and 150; Naughty List at 0, 9, 24, 48 and 90). Each asks more: they come sooner and quicker, small quick **icicles** from the
  second step, heavy slow lumps of **coal** from the third (coal cannot become a snowman; it melts into the snow it touches for +2).
  Christmas Eve brings the **Grand Snowman** (size 30, never merges, sheds snowballs): each finished snowman pelts it, the third melts it
  (+25 and three presents), and only then can Midnight come. Every step up leaves a present near you, cocoa lasts longer and lights further
  the higher you are, and a **last bell** (two on the Nice List, one on the Naughty) saves you from being caught: the list freezes and is
  pushed back. The title asks which list you play (up/down, stick or a tap on the bar; it is kept): **Nice List**, the gentler way, or
  **Naughty List**, which asks sooner and harder, sends presents less often and scores half as much again. The music grows with the steps.

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

(This describes the plain game; Caroling Run's cells are a byte each, a colour and a hollow flag, and the lethal test asks whether the cell is hollow.)

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
  (Afterimage best 7, 5 deaths in 60 s; Prism Run 316 and 1 in 120 s, 385 and 4 in 300 s; Tidepool 8 and 6; Moth 8 and 5). The season draws
  the same random numbers (it always draws the colour, then maps it), so a seed is the same game in either.
- **In season:** each bot plays all four scenes for 150 s (Caroling Run 202 rings, 2 deaths; Lantern is harder now, as meant); 0.1 to 0.3 ms
  a frame natively. Sanitizers (AddressSanitizer and UBSan), every scene in both modes, the audio path, the unit tests: clean.
- **`make test-game`** tests the rules one at a time. Lantern: a snowman is finished at full size (+5, a present, gone); a present gives its
  effect; a candy cane freezes the blobs; a magnet draws them together (the gap shrinks to under 60% of what it is without); cocoa makes you
  at least 25% quicker; merges in a row make a combo (1 then 2); the steps come at their scores on each list and Midnight waits for the boss; the boss comes at Christmas Eve, does not merge, is pelted by finished snowmen and melts at the third (+25, three presents); coal appears only from the third step, melts into snow (the stated size and +2) and never becomes a snowman; coal is slower; last bells are two and one and save you once each; the Naughty List scores x1.5 and sends them sooner and faster; cocoa lasts longer higher up; the carol gains a layer at each step; the title's choice by key and by tap, and that it is kept; none of it
  applies in the plain game. Caroling Run: over 1,500 generated rings every way through is two solid cells and neighbouring rings share a
  solid cell, the verses alternate your colour, a hollow cell is always the other colour, the first rings are safe, only a hollow cell
  kills, and the snow falls 93 px a second at the start and 295 at full speed.
- **`make test-audio`** (needs numpy): every note of every carol, across loops (so key changes too), is within 40 cents of its score (542
  notes); the full mix of each carol has no DC offset, a sensible level that is not clipped, bass present and no flood of voices; and the
  crash is checked: the trombone's three notes and slide are where they should be, the kick lands three times with real low-frequency weight,
  the cymbals are bright and die away, nothing clips.
- **Chromium, headless:** season on by default; both pairs and `?pair=`; the choice kept across a reload; `?theme=classic`; the season's
  `?mode=` names; a tap on the bottom corner, a side tap and a key all do what they should; no errors.
- **Found by testing, and fixed** (some of these were in what was already released):
  - **The carols were broken.** A start-up value in the sequencer made it launch a bass note on every audio sample, flooding all the
    voices and stealing the melody's; the output was a constant offset with the music nearly inaudible. The earlier tests looked at the
    melody alone and at "not silent", which the offset fooled. The full-mix test above now fails on that bug (offset 0.108, level 0.004, all
    28 voices busy) and passes on the fix.
  - The page's list of key names was missing Q and E, so a key outside the list could switch scene instead of starting the game; the page now
    refuses to run if its list and the game's differ.
  - The page saved only the first scene's best score; it now saves all of them and the pair.
  - Afterimage's game-over screen was not a place to switch scenes or hear the title carol.
- **Not tested, and what needs a person:**
  - **The carols and the crash by ear.** The tunes are written from memory as our own arrangements; only Deck the Halls was checked against a
    transcription I could find. Silent Night's rhythm is simplified. The tests prove the synth plays the score, not that the score is right.
  - A colour-blind player or a simulator; an Xbox; a real controller or touch; sound over a speaker.
  - Balance in season, above all Lantern's steps, boss, coal and last bells (the strengths and timings were set by unit tests and a bot that
    ignores presents; nobody has beaten the boss, by bot or by hand).
