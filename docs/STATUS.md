# Status and next steps

As of: 2026-10-13. Architecture and background: [PLAN.md](PLAN.md).

## Demo in short pieces between the attract pages (2026-10-21)

The owner's request: no Death Star explosion in the attract mode, shorter
game scenes, spread across the attract mode instead of one long round; when
the pilot dies, go straight to the next page, no GAME OVER.

* At **every page change** of the attract mode (logo, instructions, scores,
  high scores; one pass through the pages takes ~97 s) a piece of
  `CLIP_SECONDS` = 14 s plays, then the page as usual. The game continues
  from piece to piece (the game's machine is kept between the pieces; NOVRAM,
  pace and yoke setting always come from the running program): space,
  surface, trench over about eight pieces.
  Each piece and each new section within it from the next view.
* **End of a game:** 1 s after the torpedoes fly to the shaft (or
  earlier, if the end of the trench approaches), before the explosion; the
  explosion phases, the next wave and GAME OVER never come into view.
  If the ship is destroyed, you see 2 s of it, then the next page.
  The next piece starts a new game.
* No more shield floor: the pilot can lose.
* `tests/test_demo.c`: 600 s of attract mode with 16 pieces (each at most 14 s,
  each from a different view, machine afterwards as before), the game runs
  through space, surface and trench and ends at the shaft, never an
  explosion or GAME OVER phase; a lost ship ends its
  piece after 2.1 s.

## Demo flight like a real game, no sound in the quiet attract mode (2026-10-21)

Feedback from the owner: In the attract mode, short sound blips sometimes came
despite MUSIC IN ATTRACT: NO; in the demo flight the player flew into
everything without it costing anything. Built, measured and filmed only locally, **not
on the cabinet**.

* **Blips:** They came from the end of a silent demo flight. The volume
  was set only once per frame after the game step, and resetting the board
  (request 00) lets the film music fade out for 0.7 s
  and the DC-offset filter slowly settle, both then
  at full volume again. Now `demo_sound` (main.c) sets
  the muting at every interrupt, and at the end of a silent demo
  `sw_sndboard_hush` makes the board silent at once (recordings and music
  without a fade-out, engine, speech chip, DC offset). On the
  attract pages neither engine nor Falcon starts, not even from the
  last frame of a demo. With MUSIC IN ATTRACT: YES a demo fades out as
  before.
* **One pilot instead of swinging** (`platform/demo.c`): He sees the last
  drawn frame (the vector list with its tags, as the renderer
  runs it) and holds the yoke so that the crosshair goes there
  (`pot_for`: RHLIM reversed, also without YOKE AUTO CENTER). First
  the nearest live fireball the crosshair can reach; otherwise in
  space a TIE, over the surface a tower cap or bunker. He fires
  when the crosshair is on it and the last beam is gone.
* **Dodging:** Over the surface he computes ahead for each crosshair position
  whether a tower comes into the collision field (GDVIEW: near and in view).
  This includes the bank: the ship rolls with the crosshair and strongly
  after a hit, and the Math Box thereby turns the tall tower center
  to the side (measured: 2·YP = cos r·dy + sin r·(TZ − 1E00), r = GD.ACT, 78 per
  5 degrees). In the trench the same for the force fields (PNLW/PNRW, height ±200,
  first half of the section). First a path with clearance, then the already
  chosen one, then a tight one; once per game frame.
* **Exhaust shaft:** At the end of the trench it takes priority over everything:
  the crosshair so that the laser hits the trench floor on the shaft
  (CLBLZ), then the torpedoes fly.
* **Shields:** Hits cost shields as in the game (at first the last one
  always remained, no longer since the short pieces, see above).
* **Choice of the Death Star** via the crosshair instead of a raw
  pot value: Without YOKE AUTO CENTER the demo previously took the easy
  Death Star without a surface.
* Measured over 20 demos (both yoke settings): before, 23 to 29
  hits and the shaft missed (nose at the wall, trench once more);
  now 1 to 5 hits (mostly fireballs over the surface, rarely a
  force field), the Death Star explodes every time (~102 s), then wave 4
  begins in space. `tests/test_demo.c` checks this (hits cost
  shields, at most 8, shaft hit), `tests/test_samples.c` the
  immediate silencing.

## Bindings like all vector games (2026-10-19)

- `src/platform/bind.c` new: each input AUTO (read directly: the AdvanceMAME defaults – keys, buttons/stick of the yoke (joystick with "yoke" in its name, otherwise the first), mouse buttons), NONE or up to 4 own (key, joystick button/axis/hat, mouse button). File: `auto`, `off` (NONE), otherwise `key:..`, ...; an old `none` now means AUTO (was: like the cabinet).
- Test menu CONTROLS: START 1/Enter, then a button; the first one since the cursor landed on the row replaces, each further one adds. While waiting: Enter (for game functions also START 1) back, START 2 NONE, Del AUTO; Left/Right NONE/AUTO; Del on the row AUTO. No more 5-second timeout. A key may have several actions. Never bindable: Esc, F2, Enter, keypad Enter, Del, Backspace; for game functions also the START/COIN keys.
- The menu works only through fixed inputs (`src/platform/vpbind.[ch]`, copy from the Tempest 2000 port): arrows, any stick/hat/the yoke, START 1/Enter/keypad Enter, Backspace back, F2/test button out. Thumb left/fire no longer act in the menu.
- The yoke axes are read directly (device with "yoke" in its name, otherwise joystick 1, dead zone as before 0.02); the picture calibration moves with the fixed menu inputs.
- Checked on the PC (Windows test build `build-pcw` with `-DSDL_MAIN_HANDLED`: replace, add, NONE/AUTO), Pi build installed on the cabinet (old file `swenhanced.before-vpbind`), not tried there.

## PMEND re-cut (2026-10-17, 0.3.30)

* `sounds/music/PMEND.wav` re-edited by the owner; `build.py` run
  again: 17.35 s with reverb, loud part −30 dB, peak 0.19, like the
  other pieces. No other change.

## Picture menu with numbers (2026-10-17, 0.3.28)

The owner's request: size adjustable and everything with numbers, otherwise you don't know
what you are setting. The SCREEN page of the test menu now has SIZE
ACROSS / SIZE UP (percent of the picture area, step 16 board units per
side = 0.8 %), POSITION ACROSS / UP (board units, step 16),
BRIGHTNESS (%, step 5), GAMMA (step 0.05) and BLACK LEVEL (floor, %);
left/right (or fire) changes, saved when leaving the menu.
Behind the page stand the picture border and the center cross, so that you can see size
and position. The test pattern (TEST PATTERN, formerly SCREEN CALIBRATION) shows
the mode's value in its row now. Not yet seen on the cabinet.

**White border at the four blue dots (0.3.29):** The game draws four blue dots in
every frame (at the start of its list, VJINIT, at ±119.5 /
±137.5 line units = ±480 / ±552 VG units). The white border of the
test pattern and behind the SCREEN page now lies exactly through them (before, at the
edge of the AVG window, further out), and the four dots are drawn in, in blue: If
the white border is entirely on the tube, so are the dots. The window border
stays faintly visible in the test pattern.

## Goal (the owner's wish, 2026-10-13)

One hundred percent C, the sound too; no emulation. A fixed frame rate without
the dips of the cabinet ("steady frames per second"), and real, not
faked smoothness. The 6809 core serves only for comparison
(`--original`, the tests).

## What runs

The whole game, unchanged, as a C program (`src/game`, line by line
from the 1983 source): interrupt, coins, attract mode, game start,
space wave, approach, ground with towers and bunkers, trench, proton torpedo,
explosion of the Death Star, wave change, all deaths, game end with
bookkeeping and high-score entry, plus the operator pages (statistics,
options with change, reset and NOVRAM test). In `src/game` there is no call into the
original program any more (`grep -rn "ORIG(" src/game` is empty).

Likewise the sound board's program (`src/sound/sndprog.c`: main loop,
effects SNDAUD, music SNDPM, speech SNDSPK).

`swenhanced` knows two modes:

* **Default**: everything in C, no 6809.
* `--original`: game and sound program entirely on 6809 cores (for comparison).

Not ported and not planned: the board's self-test (TCTEST.MAC:
RAM, ROM and Math Box check at power-on with the test switch). This
hardware does not exist here; "AUX" on the options page only resets the game.

Modelled as components (not programs but hardware): Math Box,
divider, NOVRAM, the four POKEYs, the TMS5220 speech chip, the sound board's
RIOT, the vector generator's timing.

* **Fixed clock at the cabinet's pace** (`steady_frames = 1`, default,
  owner's decision of 2026-10-13): The frames of a phase come at a fixed
  interval, namely the one the cabinet needed there on average
  (`sw_pace_steady_interval` in `src/game/pace.c`, from eight MAME recordings):
  banner, score page, selection, approach 12 interrupts (20.5 frames per
  second), instructions 18.4, high scores 17.7, space 16.7, ground 15.1,
  trench 18.7 (13.2 per second), death in space 24. The game is thus as
  fast as on the cabinet, but without its pace wobbling with the picture
  content. Measured in free running: exactly these values in every phase.
  `steady_frames = 2`: 12 interrupts everywhere (that is how the program is
  written, "20 PER SECOND"; in the game about 45 to 55 % faster than the
  cabinet). `steady_frames = 0`: each frame as long as the 6809 took to
  compute it (see pace). Death over the ground is in no recording;
  its clock is that of death in the trench.
* **Sound in C, checked against the original** (`tests/test_sound.c`): Two
  sound boards get the same 59 requests (all there are);
  one runs the original program on the 6809 core, the other
  the C program. All write accesses are equal and in the same pass
  of the main loop: 3,656 to the effect channels, 55,296 to the
  music channels, 18 to the control registers; the 6,685 speech bytes are
  equal and offset by at most one pass (4 ms).
* **Checked against MAME**, frame by frame (RAM, Math Box RAM,
  display list), eight recordings with 27,313 frames: attract mode, two of
  the operator pages (statistics with game history, options with 30
  changes and reset) and five played ones (death in the trench, death in space, missed shaft with
  repeat, destroyed Death Star, four waves in a row with Darth
  Vader, bunkers and towers). All frames are equal, with the C routines
  as with the whole original on the core.
* **Pace without fixed frame rate** (`steady_frames = 0`): In the original the 6809's computing time determines the frame rate (in the
  trench about 13 frames per second). The C routines finish at once and
  therefore wait as long as the 6809 would have needed (`src/game/pace.c`):
  from counted work (bytes read, Math Box runs, characters) and a
  base value per phase, fitted to MAME's measured time per frame. Deviation
  in the game phases 0.9 ms (rms) at 25 to 80 ms per frame, on average per
  phase zero. In free running: trench 18.6 interrupts per frame against 18.2 in the
  original, space 16.1 against 17.4 (different course of the game), attract mode
  12.2/12.4 (banner), 18.6/18.6 (instructions), 18.0/17.9 (high scores).
* **Smooth display from the game state** (`src/game/subframe.c`,
  setting `smooth = 3`, default): The game moves everything once per
  frame (12 to 20 times per second). For each of the board's frames in between,
  player, ships, shots, debris and stars are placed on their way from the
  state before the frame to the state after it, and the game's drawing routines
  recompute the picture of that moment, on a copy of the
  machine: with correct perspective, without delay, and what comes into view
  comes into view. The game itself notices none of this. Checked
  (`tests/test_subframe.c`, 2640 game frames of a played run): At the
  time of the frame the computed picture is, word for word, its
  display list, in between it lies in between (31 % of the way after one
  third of the time), and at the transition to the next frame no jump remains.
  Applies to space, approach, ground, trench and the deaths.
* **Attract pages, explosion, wave change**: There the
  routines count and draw in one go (banner zoom and scrolling text depend on integer
  counters), an intermediate state cannot be inserted. Instead
  the picture of the next frame is computed one frame ahead (the
  game run on a copy one frame further), and the lines
  move from this frame's picture to that one (`src/render/picture.c`,
  `sw_subframe_next`). Also this without delay. Checked: 96 % of the words
  of the display list computed ahead are the ones the game then draws;
  the rest are yoke and fire, which are read only in the next frame.
* **Film of the port** (`tools/swvideo`): runs the game without a cabinet
  (attract mode, two coins, start, yoke swings and fires), draws every
  frame like a vector monitor and writes the sound along; `mux.py` makes an
  MP4 of it (needs PyAV). Result: `video/swenhanced.mp4` (not in the repo;
  only the newest video is kept there, old ones are deleted). Fixed
  on 2026-10-13: The tool did not let the sound board run between two
  requests of one frame, so the game reset it 129 times (sound and music
  choppy). The game itself (`src/main.c`) was
  not affected.
* **List of all sounds** for the later replacement: `docs/SOUNDS.md`.
* **Stroke sorting**: 3 times less blank travel on the high-score page.
* **Graphic enhancements over the surface** (`src/render/enhance.c`,
  2026-10-13, the owner's wish; four settings, each can be switched off, in the
  test menu and in the settings file, default on):
  * `enh_surface`: the ground as the Death Star's plating pattern, lines in
    perspective to the horizon, some plates subdivided or with a hatch.
  * `enh_stars`: stars in the sky; also over the trench, in the strip
    between the upper edges of the walls (a star shows when the
    line of sight leaves the trench through the open top).
  * `enh_towers`: towers and bunkers with steps,
    seams, gun turret and barrels (template: photo of a tower model). Their
    dimensions are, to the unit, those of the cabinet (owner's requirement: one
    must still be able to fit between them): tower 960, 720, 600, 480 at the
    heights of its ROM points, cap 480 and 720 high, bunker 960 by 720 and
    720 high; seen from the front exactly as wide. Every tower has all details
    at every distance (owner's requirement: nothing builds up
    in pieces, no fading in over time); only the brightness
    changes. So that no tower pops up halfway, beyond the
    game's range (3C00h) all buildings of the pass are drawn,
    even those the game has not set up yet; within it the game's
    rule applies. The farthest are dark and
    get brighter linearly with proximity (owner's requirement, so that the
    rear ones do not cover the front ones). The gun turret turns
    back and forth searching and aims at the player as soon as he comes
    into range (picture only: shooting follows the cabinet's
    rule). The game's own towers are taken out of the picture for this. The game
    itself is unchanged; the laser hits by the cabinet's rule
    (a detour via `enh_towers` for the true perspective is
    gone again, since the towers have the cabinet's size: with it
    the caps could no longer be hit).
  * `enh_dogfights`: small in the distance, TIE fighters chase an X-wing
    across the sky, every 13 seconds, sometimes two ships, sometimes three. The TIE
    fighter is the ROM's model, line by line. The ROM has no X-wing
    (you never see your own ship from outside): it is drawn
    by hand from eleven lines.
  Everything is drawn from the game's state (position and attitude of the
  player, map of the buildings and their state) with the game's perspective;
  the computation agrees with the Math Box to the dot
  (measured). With occlusion the new towers hide what lies behind them.
  Checked on still pictures; with all four off the recordings still match.
* **Own test menu** (`src/platform/menu.c`, 2026-10-13): The
  self-test button (`p1_button6`), on the device the Tab key, opens it, the
  game stands still meanwhile. Since 2026-10-13 in pages (Graphics, Sound, Controls
  and Screen, Game Options; plus Arcade Operator Screens and Save and Exit
  on the main page), because a page with 23 rows was written too
  small. Rows:
  Enhanced Settings (Occlusion, Surface Plating, Surface Stars, Detailed
  Towers, Distant Dogfights, Trench Clipping, Yellow Logo, Replaced Sounds,
  Yoke Auto Center, Smooth Motion, Frame Pace), Game Options (the eight
  options of the cabinet),
  Sound Test (all 59 requests from `docs/SOUNDS.md` with number and
  name), Screen Calibration, Arcade Operator Screens (the statistics and
  options pages of the cabinet's program; the button leads back from there),
  Save and Exit. Yoke selects and changes, fire triggers, the self-test button
  saves to `save/swenhanced.ini` and closes. Compiled for PC and Pi,
  but **never seen or operated yet** (no cabinet, no screenshot).
* **Occlusion** (`occlusion = 1`, default; first "Enhanced" feature,
  can be switched off, the owner's wish of 2026-10-13): Things are no longer
  transparent. `src/render/solid.c` cuts from the finished picture the
  lines that lie behind something nearer; nothing changes in the game
  (all recordings still match). What hides:
  * the faces of the 3-D objects (TIE fighter, Darth Vader's ship, towers,
    bunkers, wall guns, wall plates, force fields, shaft). The ROM knows
    only points and lines; the faces are found at start as rings of
    lines that lie (almost) in one plane.
  * what is drawn as an outline: the STAR WARS lettering (its front
    also hides its own rear edges), nose and cannons of the X-wing,
    the Death Star. The shape is determined on a grid, because the
    X-wing's outlines run over the picture edge and are not rings.
  The distance is noted by the drawing routines next to each word of the
  display list (`sw_machine.vgnote`: 1/X of the point, object, point number).
  Never hidden: text, score, crosshair, laser beams.
  Checked: `tests/test_solid.c` and still pictures with/without (`swvideo --wire`).
* **Crosshair centered** from the first frame (`yoke_autocenter`). Since
  2026-10-13 this includes auto-centering (`src/platform/controls.c`):
  The yoke's rest position counts as the center. It is learned at start from the
  first measurement and tracked when the yoke lies still in the attract mode for
  about two seconds; the end stops stay where they are.
  The learned rest position is stored in the settings file at once
  (`yoke_center_x`, `yoke_center_y`) and applies from the next start from the
  first frame. Switchable in the test menu (YOKE AUTO CENTER). Unchecked on the cabinet: Whether
  its yoke lies still enough (threshold 1.5 % of the travel) is open.
* **Sound replacement** (`src/sound/samples.c`, 2026-10-13, Enhanced,
  `sound_replace = 1`): If `sounds/` (or `save/sounds/`) holds
  a WAV file named like a request (`AUDXL.wav`, `PMTH5.wav`,
  `SPKRED.wav` ...), it is played instead of the chip sound; all others
  sound as before. Rules and format in `docs/SOUNDS.md`. Checked:
  `tests/test_samples.c` and a game with two test tones (the tone for
  `AUDXL` at every shot, the one for `PMTH5` at game start, measured in the
  spectrum). There are still **no real replacement sounds**. As a starting point
  `tools/swsound` writes every sound of the board as WAV (music until the
  music driver is finished, speech until the speech chip is finished, effects
  until silence), each into the folder of its kind: `sounds/voice/` (22
  sentences), `sounds/music/` (11 pieces), `sounds/effects/` (18), `sounds/test/`
  (4, the board's self-test). All 55 are on the PC (music and sentences
  not in the repo, which is public; not on the cabinet); `SPKI`, `SPKSKP`, `AUDPBZ` and `AUDTCZ` make no
  sound and get no file. The loader looks for a file in `sounds/`
  itself and in the folder of its kind. Consequence on the PC: Because the originals now lie in
  `sounds/`, the PC version plays them as "replacement" (sounds
  the same; `sound_replace = 0` switches it off). Open: `AUDRS` (R2, short
  yes) begins with a good two seconds of silence that are in the file;
  whether the board does it that way or it is due to the export has not been investigated.
* **Logo yellow** (`logo_yellow = 1`, default, Enhanced): the STAR WARS
  logo in the attract mode in yellow instead of the cabinet's blue (`enhance.c`
  recolors the lines). Switchable in the test menu (YELLOW LOGO).
* **NOVRAM** (options, bookkeeping, best three scores) in `save/nvram.bin`.

## Not yet seen or heard on the cabinet

The cabinet was off during this work. What is installed is the state of the
attract mode before. Unchecked on the device, so only computed and measured:

* the whole game in C on the device (coins, start, yoke, fire), the fixed
  clock (whether the pace feels like the cabinet),
* the newly computed intermediate pictures (`smooth = 3`): look, computing time
  on the Pi, crosshair. If the settings file on the cabinet still says
  `smooth = 1`, it stays that way until it is changed there.
* the sound at all: The board produces plausible levels at the right time
  (speech at coin insertion and start, music from the wave), but nobody
  has heard it. POKEY without high-pass filter, TMS5220 simplified.
* `--calibrate`, the board's frame rate in the game.
## Review of the rendering (2026-10-13)

At the owner's request, the whole picture chain was reviewed (occlusion,
objects, enhancements, drawing of the vectors). Tool: `swvideo --check`
runs through a game and checks every frame (numbers that are not numbers;
lines entirely outside the screen; lines of things without a distance;
compute time per frame for each phase). Result over 9000 frames (attract
mode, space, surface, trench, wave 5):

* No invalid number. Compute time on the PC: space 0.27 ms, surface
  0.55 ms, trench 0.29 ms, logo page 0.06 ms per frame (the whole
  chain: intermediate frame, enhancements, occlusion). On the Pi 5 this is
  roughly three times as much; the board needs clearly longer per frame (2 to
  3.5 ms according to the log). No optimization needed.
* Lines entirely outside the screen are plentiful (the X-wing's guns,
  fireballs, the receding title lettering): the SDK clips them away
  before they go to the board (`vp_dvg.cpp`, Cohen-Sutherland), so they cost
  nothing there.
* **Bug found and fixed**: Fireballs that glow on the shields
  (GLOWVW) had no distance (1/X = 0) and counted as behind everything for the
  occlusion; a TIE or tower behind them cut them away. Now
  they are in front of everything (distance 65535, and the occlusion leaves such lines
  alone). All other things that can be hidden have their
  distance.
* **Removed**: `sort_strokes` (code and setting), because the sorting
  demonstrably makes the picture wobble on the tube; `smooth` 1 and 2 from
  the menu (one frame too late and overshoot, respectively); the file still accepts the values.
* Drawing of the vectors: strokes are drawn in the game's order,
  connected lines of the same color as one run. A sorting that stays the same
  from frame to frame has not been built: every reordering changes where
  the beam comes from.
* Not found, but possible: The faces of the ships are rings of
  lines that may lie up to 20% out of the plane; with strongly curved
  rings (the TIE's hull) the approximated plane can cut off its own lines just
  behind it. Not noticed in still frames.
* **New in the test menu**: the game's options (coin value, coin mechanisms,
  bonus coins, starting shields, difficulty, bonus shields, music in
  attract) directly from the NOVRAM, with the checksums and the backup area
  like the cabinet's options page (`sw_option_get/set`,
  `tests/test_options.c`). For the program, the DIP switches are only the default
  when resetting; the resets and the NOVRAM test stay on the
  operator pages.

## Logo wobbled at the top of the T (2026-10-13)

The logo in attract mode flies away (it gets smaller every frame
and moves up). The occlusion cut away its side edges where
they lie behind the front face, on a grid of 1 cell per unit
that was laid over the logo anew for every frame: Depending on how
the shrinking logo sat in the grid, short edge pieces (at the
top of the T) were dropped at times and not at others. Now the logo's shape is built once
in its own coordinates (800x480 cells over its box, key:
the brightnesses of its lines) and only computed into the picture each frame;
when shrinking, nothing about the cut changes any more. Incidentally, the
page costs 0.02 instead of 0.06 ms per frame on the PC (the 22 ms on the Pi are
gone too). Measured: the length of the logo's lines per frame falls evenly with and without
occlusion (the difference decreases steadily, no jumping).

That did not help: The owner still saw only the topmost stroke of the T
wobble. Second finding: This stroke is the first line of the logo
in the display list, and before it come the stars. So the beam jumps
dark from a star that stands somewhere else every frame to the start of this
stroke; on the DVG board the start of the first line trembles along after such
a jump (measured: the line before it was a different one from frame to frame, the line itself smooth in the
computation). Now
`sw_picture_lines` draws the stars of each frame last (points are not bothered by
the jump); the order of all other lines stays the game's,
nothing is reordered. With that the beam reaches the T always from the same place.
Unchecked on the cabinet.

## Size of the towers, brightness (2026-10-13)

* **Towers looked squat, the original more spacious** (owner). Reason:
  The arcade machine does not draw a building in pure perspective. GDVIEW lays
  over the perspective a linear scale with the distance
  (`M.GDSC`: linear factor XP<<2, i.e. size times 1 - XP/4000h), around the base
  of the building. A distant building is thus much smaller than
  the perspective made it, and the scene looks vast. The enhanced towers
  now get exactly this factor (`sw_enhance.shrink`, around the base).
  Still frames, original versus enhanced: same size and same
  distance. Consequence: The towers grow again as you approach, as on the cabinet
  (it is the same mechanism); the brightness still increases with
  nearness.
* **Overall brightness under MAME on the cabinet** (owner). The game's
  drawing routines use the COLOR byte 80h as normal brightness; after the
  ZZ network and the curve `floor + (1 - floor) * (i/255)^gamma` a
  normal line ended up at 56%. `brightness` may now go up to 2.0, from the
  factory 1.7: a normal line at 95%, the bright ones (FFh, hits, flashes)
  at the stop. How AdvanceMAME maps the intensity onto the DVG is
  not looked up; the value is an assumption and to be readjusted on the cabinet
  (test menu, Screen Calibration, fourth mode, yoke).

## Towers from further away, faded in (2026-10-13)

With the size of the cabinet, the owner wanted to see further back once more
and have the towers faded in cleanly. Now: The towers are drawn from
6000h ahead (the cabinet from 3C00h), dark and small. The size
follows the cabinet's formula (1 - XP/4000h), limited downward to 8%
(the formula reaches 0 at 4000h); the brightness rises linearly from 0 at
6000h to full at 2500. So nothing arrives suddenly: a tower appears
as a dark small stroke on the horizon and grows and brightens.
Within the cabinet's range its rule applies (only established
buildings); beyond it all buildings of the pass are drawn, and
those of the next pass only if they are still beyond the range at its start
(otherwise they would pop up halfway). The map
repeats every 8000h, each building is considered on this and the next
round. Still frames: at the start of the surface nothing next to the
player, on the horizon the small dark towers.

## Colors back to the original (2026-10-17, 0.3.27)

* The Death Star in film colors and the X-wing colors from 0.3.26 are out again,
  not even as an option (owner: does not look good, the game's
  original colors stay). The code for it is removed (in the dogfight
  its own Death Star stays as before); the keys
  `enh_deathstar`/`enh_xwing` from saved settings are
  ignored.

## Camera off, music fades out, Death Star and X-wing in film colors (2026-10-17, 0.3.26, colors taken back in 0.3.27)

* **Enemy behavior checked** (owner: "enemies suddenly come from
  behind", "swing to the Death Star not in time"). The game core is
  unchanged: a new MAME recording (4000 frames, a played script with
  space, approach, surface, trench) runs with `swcompare` in every
  phase with 0 differing frames. The only intervention in the game was the
  **Smooth Camera** (`camera_ease`, on by default so far): it limits the
  turn rate of the ship (AIMA toward the enemy, AIMDTH toward the Death Star) to 12 of
  127 per frame. As a result the ship turns later and more slowly toward the target: The
  enemy that the original already has in front of it still stands to the side or
  behind, and the swing to the Death Star comes too late (the TIE fighters
  are already gone). Now **default off**; the old key `enh_camera`
  is ignored (so that saved settings go back to the original),
  the new one is called `camera_ease`. The option stays in the menu.
* **Music fades out:** (1) `build.py` puts a reverb on the last 0.6 s of every
  piece (decaying, low-pass-filtered noise, RT60
  1.8 s, -4 dB) and appends the reverb tail (2.2 s), so that the cut at the
  end is not hard. (2) If a piece is replaced by the next or ended by the
  board's reset, it fades out exponentially over 0.7 s in the player
  (`tail` in `samples.c`) instead of breaking off. `test_samples` adjusted.
* **Death Star in film colors** (FILM DEATH STAR, `enh_deathstar`, on): the
  recoloring and the details of the dogfight (0.3.1x, `death_star_colors`)
  now in the game as well, for all Death Stars of the list (selection page, approach,
  explosion): circle from the outermost points (least squares), sun
  fixed in the picture at top left, outline and plates gray, trench bright, dish
  gray with green spokes; from radius 45 latitude and longitude lines, from 80
  also plates (up to around 250 additional lines, as in the dogfight).
* **X-wing colors** (X-WING COLORS, `enh_xwing`, MODEL): Nose and guns
  of the arcade view (SW_TAG_PLANE, drawn by the game in red and blue)
  get the colors of the model: the red outlines gray (hull, 85%),
  the blue details red (markings, 70%). ARCADE: like the cabinet.
* Checked with `swvideo` (still frames from the film): selection page and approach.

## Ground explosion did not work (2026-10-17, 0.3.25)

* 0.3.23 compared the phase with `& ~1`; the phase constants are however
  odd (SW_PHSGD 0x29, running 0x2A), so a ground phase was never
  recognized and the tower tops kept exploding with the TIE engine sound
  (owner). Now `| 1`.

## Start lamps off (2026-10-17, 0.3.24)

* Both START lamps were lit in the game (owner). The game does not use
  the start buttons; `sw_app_open` now turns them off on the cabinet.
  A quirk of the SDK (`vp_lamps.cpp`): it writes the LEDs only when its mask
  changes, and the mask starts at 0, so first set both, then
  none. On exit `vp_lamps_close` restores the state it found; the cabinet's
  menu then takes over.

## Yoke fully analog again, ground explosion without TIE engine sound (2026-10-17, 0.3.23)

* **Yoke jumped to the edge** (owner, 0.3.21/22): The four directions
  of the digital yoke fell back, without their own binding, to the cabinet's `p1_left` ...,
  and in the `advmame.rc` those are the same yoke axis above
  a threshold. From half deflection the digital value went to 1 and
  won. Now the digital yoke counts only with its own binding
  (`sw_bind_is_bound`, `controls.c`); the cabinet stays purely analog.
* **Towers and bunkers explode without TIE engine sound:** AUDSX (small
  explosion) on the arcade machine is a request for TIE, tower and bunker; the
  recordings were TIE explosions with engine sound. New `AUDSXG` (nine pure
  explosions: Shield Bunker, Debris, Flak, Bomb, Large), which `samples.c`
  uses for AUDSX when `main.c` reports to it via `sw_samples_ground` a
  ground phase (GD, G0B, G1B, G0D) or trench phase (BS, B0B, B0D).
  In space the TIE explosion stays.

## Dogfight not selectable (2026-10-17, 0.3.22)

* The dogfight is not yet mature (owner, 2026-10-17) and is
  not offered until further notice: `SW_DOGFIGHT_OFFERED 0` in `src/main.c`
  leaves the fourth Death Star off the selection page and does not start
  it. All the code (`src/dogfight`, views, high-score list, sounds)
  stays and is still being built; `1` offers it again.

## Controls page, free binding, volumes, speech level (2026-10-17, 0.3.21)

* **Menu reordered:** GAME OPTIONS at the very top, then GRAPHICS, SOUND,
  CONTROLS, SCREEN; below, ARCADE OPERATOR SCREENS, SAVE AND EXIT.
  Operation also with arrow keys, Return, Backspace and the first joystick
  (`sw_bind_menu_held`), not only with the cabinet's yoke.
* **Free binding** (`src/platform/bind.c`, after the pattern of Vectorton):
  14 digital inputs (yoke as four directions, fire L/R, thumb L/R,
  Start 1/2, coin 1/2, Aux, test menu), each with up to two SDL inputs (key,
  joystick button, axis, hat). Without its own binding the names of the
  `advmame.rc` apply as before. CONTROLS page: Fire on a line waits for
  the input (blinking PRESS AN INPUT; ESC and F2 are never bound),
  left/right clears it, ALL INPUTS AS THE CABINET'S clears everything. A
  held binding lights up green (probe). Saved as `bind_*`.
  `main.c` asks `sw_act_*` everywhere instead of `vp_input_*` with names; F2 stays
  fixed (`sw_bind_test_pressed`).
* **Digital yoke:** `controls.c` runs a digitally pressed yoke to full
  deflection in 15 frames (0.3 s) and back in 6; analog and digital
  apply side by side, the one deflected further wins.
* **Volume ratios** (SOUND page): MUSIC / EFFECTS / SPEECH / ENGINE
  VOLUME, 0 to 200 in steps of ten, `vol_*` in the INI (`sw_samples_volumes`).
  Speech also applies to the speech chip, effects also to the POKEYs
  (`sndboard.c`: board level times effects volume, speech separate).
* **Speech level:** The previous measure (RMS of the loud part) counted the
  reverb tail as loud when there was reverb; so Obi-Wan's lines came out quieter by the measure,
  were boosted and were audibly 2 to 3 dB louder than the rest.
  `build.py` now measures speech by its loudest 400 ms
  (`loudness_speech`); all SPK targets raised by 1 dB to -26 (SPKELE -28,
  SPKR2N -29) so that the average stays. All 22 speech recordings are
  now at -26.0 dB in this measure (before, a spread of -23 to -29).
* Linux build and 13 tests run; Pi build (`pi-clang`), `deploy.sh`,
  `dist/swenhanced-0.3.21.vpk`.

## Film music complete, levels matched (2026-10-17, 0.3.20)

* All ten music pieces of the game are now available as the owner's recordings in
  `sounds/music` (plus PMTH5 and PMTHB in new cuts): PMTH5, PMTHB,
  PMDAR, PM4TH, PMCNT as before, new PMBEN, PMDES, PMEND, PMREB, PMRRP.
  PMSF2 stays the collection's torpedo impact (not a piece of music).
* **Level:** the sources ranged from -12 dB (PM4TH, peak 0 dB) to -28 dB
  (PMCNT); `build.py` brings the loud part of each piece to -30 dB
  (peaks 0.15 to 0.20), like the first five. The five new ones are in
  `tools/swsamples/samples.txt`; `package/sounds/music` rebuilt (2.1 MB).
* **Ducking** was already there (0.3.15): under speech everything else -3 dB, the
  music too; under a big explosion or a crash the rest -1.5 dB.
  Unchanged.
* Deployed (`deploy.sh`) and packed as `dist/swenhanced-0.3.20.vpk`. The
  music files in `sounds/music` are taken out of the repository
  (`git rm --cached`; they were still tracked despite `.gitignore`), as has been
  intended since 0.3.18.

## High scores on the marquee (2026-10-16, 0.3.19)

The VectorPie menu shows a game's high-score list on the marquee from
`save/highscores.txt` (sdk/docs/06-hiscores.md, "NAME SCORE", best first),
if `hiscores = yes` is in the manifest. For us it said `no`, and the game
keeps its table in RAM and NOVRAM like the cabinet. Now the program writes
the game's table (TCHSCR: ten lines, initials 1..26 =
A..Z, points as BCD) every 5 s if it has changed, via a
`.new` file and rename; not during a demo flight. Checked: After
the start, the cabinet's table is there (OBI 1285353, WAN 1110936, HAN
1024650 ...). The dogfight's high-score list is its own and does not appear
on the marquee. As on the cabinet, only the best three stay across
power-off (NOVRAM); places 4 to 10 start again with the defaults
after every start.

## Film music in the game, sound in stereo (2026-10-15, 0.3.18)

* The owner's five pieces in `sounds/music` (PMTH5, PMTHB, PMDAR, PM4TH,
  PMCNT) replace those of the board, as he filed them (only
  converted: 16 kHz, ADPCM, -30 dB). Private: `package/sounds/music` is in
  the `.gitignore`, the pieces get onto the cabinet via `deploy.sh` and the vpk, not into the repo; if they are missing, `build.py` leaves them out.
* **Stereo** ("please not mono"): The sound output is now stereo
  (the board's ring buffer in left/right frames, SDL with 2 channels). A
  stereo recording keeps its sides, mono recordings and the board's sound
  sit in the middle. The music is built in stereo, effects and speech
  stay mono. `sw_sndboard_read` still delivers the mono mix for tools and tests.
* **Bug found:** For stereo IMA ADPCM, libsndfile writes half the
  frame count into the fact field; the loader took it and played only the first
  half. It now takes "fact" only where it fits the blocks. All
  files (stereo too) are bit-exact with libsndfile.

## Nose in the dogfight, view at game start, film music (2026-10-15, 0.3.17)

* **Nose in the dogfight transparent:** The dogfight clipped all lines of its list at the
  edge of the picture (because of the Death Star), including nose and guns.
  render/solid, however, closes their outline with the edge of the picture only where lines
  go beyond it; so the nose stayed open at the bottom and did not occlude.
  Nose and guns are no longer clipped (as in the game).
  Checked with a TIE behind the nose: before, 6 to 8 of its lines
  visible in their middle, now none.
* **View at game start:** Every game and every dogfight starts in the
  arcade view, no matter what was last chosen with Start; Start switches
  on as before.
* **Film music (private, in the private repo, not in the package):** Cut from
  soundtrack excerpts of the owner (desktop), found
  by the arcade piece (melody-band chroma, tempo and key searched, entries
  compared), with a hard cut as in the original and reverb after it:
  PMTH5 (film 7.00-16.19 s, 1.18x faster, 7.79 s; the game lets it run until
  PMTHB 8 s), PMTHB (16.19-26.08 s, 1.06x, 9.33 s; until PMDES 10 s), PMCNT
  (Cantina 15.00-43.13 s, original tempo, 28.13 s, its eighth notes lie exactly
  on those of the arcade piece). The tools are still in the scratchpad;
  if the pieces stay, they go to `tools/` and the pieces as a
  local music folder next to the sounds.

## Sounds from the films and the SFX collection (2026-10-15)

The owner's assignment: the new film voices (`sounds/voice`, 21 instead of 22),
matching original samples from the SW Ultimate SFX Collection for everything, an
engine of its own (inside and outside different), multisamples, volumes,
space-saving format, 16 kHz at most.

* **Missing voice:** SPKR2N (R2 scolds, exhaust port missed). It is
  not a line from the film but R2 himself; taken is "R2 Freakout 1A"
  from the collection. The single "Always" (SPKALW) is there: It is exactly the end
  of SPKFOA (cross-correlation 0.92 at 1.56 s), it does not need
  to be cut.
* **Multisamples:** `NAME.wav`, `NAME_2.wav` ... `NAME_12.wav`, random,
  never the same one twice in a row; identical effects no longer cut each other
  off (12 voices instead of 8). TIE flyby 8 + 5 variants, TIE cannon 4,
  X-wing laser 6, explosions 8, towers 5, torpedoes 4, R2 2 to 5 each.
* **Engine** (ENGINE SOUND, `enh_engine`, on): XWENG from inside (arcade
  and cockpit view), XWEXT from outside, loops from the collection's X-wing recordings;
  in the dogfight louder and faster with speed; crossfaded
  when the view changes; off in the menu, on loss of the ship,
  outside of flight.
* **Format:** 16 kHz, mono (the board outputs mono), IMA ADPCM (4-bit);
  105 files, 1.4 MB (as 16-bit PCM 5.6 MB, the sources 1.4 GB). The loader
  now reads IMA ADPCM itself, bit-exact with libsndfile (all 105 checked).
* **Volumes:** modeled on the board (board speech -29 dB,
  effects -37, music -32), the new voices ranged from -19 to -31 dB and
  are now all at -27. A dogfight film (45 s) does not clip
  (peak 0.33).
* Selection and levels are in `tools/swsamples/samples.txt`,
  `tools/swsamples/build.py` builds `package/sounds`. The files are in the
  package and next to the program (CMake copies them), `deploy.sh` and `pack.sh`
  take them along.
* **Improved (0.3.15), after the first listening on the cabinet:**
  Ducking: under speech everything else -3 dB (music and effects of the
  board too), under a big explosion or a crash the rest -1.5 dB
  (`samples.c`, `sndboard.c`, tested). The red cannons (AUDTL) sounded
  like flybys: "DS Turret" swells to 2.4 to
  2.9 times after the onset. Now Turbolaser 9, 3R, 1, Heavy Cannon 10 and 9, Heavy
  Blaster 2A, all with a hard onset and decaying in under 0.5 s. Speech:
  All 22 audible lines of the original are covered. For Vader's four lines
  his breathing before and after the line was missing, which the original has; it is
  now set before and after from the collection (Inhale/Exhale, 9 dB
  quieter), so the lines are 3 to 5 s long as in the original.
* **Falcon (0.3.16):** The flyover has its own flyby (FALC
  Flyby 10, 2, 3, 4B, 13; cut so that each is loudest 0.85 s after the start),
  and "Yahoo, you're all clear, kid" comes exactly when he is above
  the player. His flight is planned 1.3 s ahead (from the real
  approach to the exhaust port; the game's speed jumps from
  frame to frame), picture and sound stick to it: in the film above the player at
  132.40 s, the flyby loudest at 132.38 s. The game's own SPKYAU
  (in the trench at its time) is then not played. The explosion
  of the Death Star is 4.5 dB quieter (-28.5 dB).
* **Not listened to:** The selection was made by names and measured values (length,
  single shot, evenness, brightness). Whether each file fits
  has to be heard on the cabinet. The music stays that of the board.

## Dogfight: radar, instructions, own high-score list, ending; attract mode quiet (2026-10-14)

Feedback from the owner on the cabinet (0.3.10) and what came of it (0.3.11):

* **X-wings were invisible:** The ROM model TDSXW delivers no edges here
  (2 points, 0 edges; the game draws its X-wing only from the cockpit).
  So the chases in the background shot "at nobody", and
  the wingmen were missing. The dogfight now has its own X-wing
  (`draw_xwing`, modeled on the outside view's, with faces for the occlusion,
  size 420).
* **Radar:** a disc in the middle at the top under SHIELD. Forward is in the center,
  behind at the edge, up at the top. Enemies red (Vader yellow, near ones bright),
  wingmen turquoise; an inner ring for "sideways".
* **Instructions with the yoke** (like Time Pilot 3D, `F:\Time Pilot 3D\src\attract.c`
  draw_howto): After choosing the dogfight comes HOW TO FLY. The yoke (box with
  Atari logo toward the player, cone, grips, triggers in front, thumb buttons at the back)
  turns, pitches, shows each button blinking with its function (right
  trigger laser, left torpedoes, right thumb faster, left slower),
  then the rolling and START. Next to it the X-wing, which does what the yoke does.
  32 s, or a pull on the trigger (after 1.5 s) starts. `swvideo --howto`.
* **Own high-score list:** ten places (initials, points, wave) in
  `save/dogfight.hi`, initially LUK 30000 down to GLD 3000. After GAME OVER (4 s)
  a new best pilot enters their initials (yoke to the side: letter,
  trigger: next, 30 s), then DOGFIGHT BEST PILOTS shows the list for 7 s.
* **Transition into attract mode:** After that it goes through the original's game ending
  (PHSEGM: "Remember...", "The Force will be with you", bookkeeping,
  Ben's theme, BNR) instead of straight to the title page. The dogfight's own
  announcement at game over is dropped for this.
* **Improvement (0.3.12):** In the instructions the X-wing fires lasers on the
  right trigger (turquoise bolts, in pairs alternating, they meet
  in front) and two torpedoes on the left (the shots' sparkle
  picture). When speeding up the wings fold together, and the engines get
  blue fire with a growing glow ring. When slowing down the wings open
  again. It is seen exactly from behind (the nose along the line
  of sight).
* **No longer in the face (0.3.12):** The wingmen fly far out
  and back (2600 to the side, 1800 to 2600 behind the player) and dodge
  below 2200. Enemies already turn away below 2200 (below 3400 when they are
  past) and are steered away from the player below 1600.
* **Wings and explosion (0.3.13):** The S-foils of the outside view never folded
  in the dogfight, because their animation ran on the game's clock and that
  one stands still in the dogfight. The renderer now gets the dogfight's clock there.
  Wings and engine fire follow the real speed
  (`sw_dogfight_thrust`), not the held buttons. New is
  **SHIP EXPLOSION** (graphics page, `enh_blast`, on): If the X-wing
  is lost, it flashes white, rays fly out of it in white, yellow and red
  (2.6 s), and from outside its parts fly apart. In the
  dogfight with crash, explosion and R2's swan song, the nose disappears. On
  the original's flights it starts as soon as the shields are gone
  (S.GAS 80; the sounds stay the game's). **Not yet seen in the original
  game.** `swvideo --lose-at S` makes the ship get lost in the dogfight at S
  seconds.
* **Attract mode quiet:** If MUSIC IN ATTRACT is NO, the demo flight is mute too
  (volume of the sound board 0 while it runs). Before, its
  shots, music and announcements were audible.

## Dogfight: original shots, proton torpedoes, big battle (2026-10-14)

Feedback from the owner: wrong shots, no torpedo, enemies attack too
little, the feeling of a big battle is missing. Only built and
filmed locally, **not on the cabinet**.

* **VG RAM as in the original:** The dogfight now has what the interrupt
  writes at the bottom of the VG RAM at every restart of the vector generator
  (VRLZF colors rotated, VRLZS, VRGNB/VRGNX every fourth time, VRGNT, VRFLS,
  VRMFL, splash quadrants via `sw_splash_into`), at 41 Hz. Added
  to that is the "Fake VG ROM" (DOWNVG, from 2800h), in which the sparkle pictures of the
  shots lie. The list starts at VGVARB (70h) and ends below 2800h.
* **Lasers as VWLAZ/LAZVW:** Each shot the other pair of cannons, 8 game frames
  (20.5 Hz) long, in halving steps with the wandering colors from
  TLZFR. On a hit the beam stands on the target for 4 frames, turquoise and fading,
  plus the splash VJLZS.
* **Fireballs as GN1DRW:** Scale from the distance, sparkle picture VJGNB.
  Shot down they stay in place and dissolve in purple (VJGNX). If they hit
  the shields, they glow white there (GLOWVW).
* **Proton torpedoes (left trigger):** charge in 7 s (TORPEDO bar
  at top left, blinks turquoise when ready; R2 beeps). They are two balls
  like the fireballs, each pursues a different enemy (the nearest to the
  crosshair) and destroys it with one hit. If its target falls, it
  looks for a new one.
* **More aggressive:** Waves of 4 + 2 x wave enemies (at most 30),
  Interceptors from wave 2, up to 7 + wave/2 at the same time (at most 11),
  Interceptors also in pairs. Fire in bursts (TIE 2, Interceptor 3,
  Vader 4, 0.22 s apart), already from a distance of 10000, in between
  1.6/1.3/1.0 s. They turn away only below 1100 and come back after 1-2 s.
* **Battle:** three wingmen (X-wings) fly along, chase the enemies and
  fire red bolts (every second one hits, no point for the player). A
  quarter of the enemies chase them with green bolts. After three hits
  a wingman explodes and returns after 14 s at the wall.
  In the background three chases run at the same time, and every 1-3 s
  a ship explodes in the distance. Explosions nearby flash with
  the sparkle picture.
* **Taken back after the first play on the cabinet** ("they all shoot
  each other down, totally overloaded with the shots", 0.3.10):
  The wingmen now only fly along at the player's wings
  and do not fight, no enemy chases them. Enemies fire singly (TIE 1,
  Interceptor 2, Vader 3 per burst), every 2.6/2.2/1.8 s, only up to 8500 and
  exactly in front of the nose. At most 3 + wave/3 fireballs fly at once
  (at most 6), at most 5 + wave/3 enemies (at most 8). Explosions in
  the distance only every 3-7 s.
* Open: check the visibility of the lasers on the cabinet; measure the frame rate in the dogfight
  on the cabinet.
## Dogfight: more speed, formations, bouncing off, new Interceptor (2026-10-14)

Feedback from the owner and what came of it:

* **"Feels very slow":** own ship 700/1350/2300 instead of 450/900/1500,
  turns 1.75/1.5/3.3 rad/s instead of 1.3/1.1/2.6, enemies a good 40 % faster
  (TIE 1350, Interceptor 1650, Vader 1500) and more agile, fireballs 2300
  instead of 1500, laser every 0.18 s and up to 11000; box 9000.
* **"Enemies stick together":** They keep 900 apart from each other
  (wingmen of their formation 280); below 260 they are pushed apart.
* **Formations:** TIE fighters often come in threes in a V (wingmen 420
  to the side and 380 behind the leader), attack together, shoot
  while in formation, and break up when the leader is under 2600 from the
  ship or is destroyed.
* **Collision:** Ship and enemy bounce off each other (900 push, fades
  out), both take damage: one shield, one hit on the enemy; a jolt
  in yaw and pitch. Before, the enemy exploded at once.
* **Death Star bigger:** scale of the stamps VGSCAL+200h instead of +340h (large, as the owner wants it).
* **Interceptor new** ("looks bad next to the others"; template a
  photo of the model): now built from the parts of the ROM TIE (sphere from two
  octagons, pylon rings, hubs) and its own wings in the same style:
  long narrow daggers with a deep notch down to the hub, corners 62 bent
  inward, spokes from the hub to each corner, inner frame; faces
  for the occlusion (wings in three pieces, sphere in four pieces plus sides).
* **More spacious** ("feels cramped, very close"): the models at full
  ROM size (an attempt with 60 % was a misunderstanding: the size was right,
  they were just stuck together). Enemies keep 1800 apart from each other
  (wingmen 600); below 520 they are pushed apart; box
  11000, enemies appear from 7000 away, turn away under 1800, shoot
  up to 8500; formations 850 to the side and 700 behind, break up under
  3500; laser up to 14000.
* **Death Star with more detail** ("a few more details"): six latitudes
  parallel to the trench, four curved longitudes, 40 surface plates (curved
  on the sphere, not on the dish), all lit by the same sun;
  about 250 more lines for the beam (check the frame rate on the cabinet).
  The sphere is computed in square units: the units of the
  lines are not (255 wide, 303 high on a 4:3 tube, factor
  1.58), and the ROM's stamps do not sit centered on the point they are
  placed at; center and radius therefore come from a circle through
  the points of the outline (least squares).
* **Death Star in film colors** (suggestions 1 to 3 of 2026-10-14, chosen by
  the owner): recolored after the list has run: outline and plates dimmed
  white (gray), trench bright, dish gray with a bright rim, its spokes green
  (superlaser, always glows a little); lit by a sun fixed in space
  (from top left, as one first sees it): bright and dark side with
  a soft transition, from the position of each line on the sphere. Spokes recognized
  by their length (the rim consists of short pieces). Not implemented
  (suggestions 4, 5): city lights, occasional charging of the superlaser.
* **Death Star leaves the picture cleanly** ("moves out too fast, looks small"):
  it was faded out as soon as its center was 60 above the edge, although
  half the disc (radius about 100) was still in the picture. Now it stays
  as long as any of it can be seen; all lines of the dogfight are clipped
  at the edge of the picture (the game's lists never reach beyond it,
  the output does not clip).
* **Death Star hides stars:** After the list has run, its disc
  is determined from its own lines, and background stars inside it are dropped
  (the near dust points stay, they lie in front). Also applies without occlusion.
* `swvideo --dogfight --wave N` starts at wave N; `--wave -1..-3`
  shows a single enemy, turning slowly (for looking at the models).

## Dogfight: crosshair moves with the yoke (2026-10-14)

Feedback: a crosshair fixed in the center makes the movements too nervous.
Now the yoke first moves the crosshair (at once and smoothly, up to 40 to
the side and 32 up/down), and the ship turns, the farther out it is,
with a curve n * (0.3 + 0.7 |n|): near the center gently
(small corrections with the crosshair alone), at the edge fully. Laser and
hits go where the crosshair is. When rolling (both
thumb buttons) and when turning around at the edge, it returns to the center. Not
felt on the cabinet; reach and curve are settings
(`SIGHT_REACH_X`, `SIGHT_REACH_Y` in dogfight.c).

## Dogfight, stage 2: enemies, laser, direct yoke (2026-10-14)

* **Direct yoke** (`src/platform/yoke.c`, taken from Time Pilot 3D):
  If a joystick whose name contains "yoke" is connected, the dogfight reads
  its axes (full travel) and buttons itself. Layout like the owner's
  yoke in Time Pilot 3D (yoke_layout 1 2 3 4): button 1 right
  trigger = laser, 2 left trigger = torpedo (stage 3), 3 right thumb =
  faster, 4 left thumb = slower. Without the yoke (PC) as before via
  `p1_button1..4` and the control's axes. Pull the yoke back = nose up.
  Not checked on the cabinet; not yet adjustable in the test menu.
* **Enemies** (the ROM's models, drawn as in the game and marked
  with model number, points and their space points, so that
  render/solid makes them solid): TIE fighter (`TDSTIE`, 1 hit, 1000),
  TIE Interceptor (own model: sphere, pylons, four forward-bent
  dagger wings per side; own faces for the occlusion; 2 hits, 1500),
  Vader's TIE (`TDSRTH`, 3 hits, 2000; every fifth wave, with his theme
  and "I have you now"). They attack (leading the ship),
  turn away under 1100, come back, stay in the box, fire
  fireballs when the ship is in front of their nose; ramming costs a
  shield. Explosion: the ROM's debris parts (`TDSTI1..3`), which tumble
  apart.
* **Waves:** Wave n brings 2 + n enemies (at most 14, at most 8
  at once), Interceptors from wave 3; each wave faster, more agile and
  more trigger-happy (up to 1.6 and 2.5 times). After each wave R2
  repairs a shield ("R2 MENDS A SHIELD"). 6 shields; when empty: "GAME OVER" with
  score, "The Force will be with you, always", after 7 s back to the
  attract mode.
* **Laser:** right trigger, held it fires every 0.22 s, alternating
  left and right cannon pair, visible 0.15 s from the cannons to the
  crosshair (blue, then turquoise). What is nearest under the crosshair
  is hit (up to 9000): enemy or fireball (33 points).
* **Fireballs:** own sparks (eight rays white/red, rotating and
  flickering, large by distance). The ROM's sprite (VJGNB) gets its
  sparkle from patterns that the game writes into the VG RAM every frame; in
  the own list it gave white blobs and broke off the list.
* **Display:** SCORE top left, shields in the middle (red on a hit,
  turquoise on repair), WAVE top right, thrust below it; "WAVE n"
  at each wave, "DARTH VADER" in his. If no enemy is in the picture, a
  red arrow around the crosshair points to the nearest.
* **Sound:** the board itself, requested as the game does (queue,
  one request per frame): laser, TIE cannon, fly-by, explosion,
  shield hit, fireball hit, R2, speech ("Red Five standing by",
  "I'm hit, but not bad", "R2, try and increase the power"), main theme at
  the start.
* **Background:** every 19 s a chase in the distance (26000 away, 11 s across
  the sky): sometimes TIEs behind an X-wing (`TDSXW`, green shots),
  sometimes X-wings behind a TIE (red shots). Just to look at.
* `swvideo --dogfight` flies with its own pilot (turns toward the nearest
  enemy, fires when it is in front of the nose) and with sound.

Open: torpedoes (stage 3), instruction page with yoke (stage 4),
high scores and finishing touches (stage 5), key layout in the test menu; nothing
seen on the cabinet.

## Dogfight, stage 1: flying in the box (2026-10-14)

Built (`src/dogfight/dogfight.c`), plan see next section:

* **Entry:** On the "Select a Death Star" page, at the bottom right there is
  a fourth Death Star, "DOGFIGHT / ENDLESS" (in place of the table TDTH of the
  original, x 400, y -300); crosshair on it (same hit test as
  PHESDS) and fire start the mode. The 1983 game stands still meanwhile
  (as in the menu), the sound board keeps running. In this stage F2 leaves the
  dogfight back to the attract mode (later: game end by death).
* **Picture:** its own display list for the vector generator, which calls
  the vector ROM's routines: text (table VGMSGA as MESOUT),
  nose and cannons (VJSTBM etc. as VWPLAN), crosshair (VGSITE, fixed in the
  middle of the view), stars (VGSTAR), the Death Star (the stamps of DTHVW,
  without the city lights, which at this scale run across as a line).
  Executed by `avg` like the game's lists, with tags; so
  cockpit and outside views apply (render/enhance, flag `dogfight`), Start switches.
* **Flying:** box +-8000, Death Star far outside at (70000, 18000,
  9000). The yoke yaws (1.3 rad/s) and pitches (1.1 rad/s), pulled along softly;
  both thumb buttons plus yoke to the side roll (2.6 rad/s). Thrust like Time
  Pilot 3D: the held button pulls it in its direction, twice as fast
  beyond the middle, when released it drifts back; speed 450 to 1500,
  middle 900, no stall. At the edge of the box the ship itself turns
  to the center (1.6 rad/s, yoke without effect), "WHERE ARE YOU GOING" blinks
  until it is inside and points to the center. Thrust display top right, "DOGFIGHT"
  for the first 4 s. Dust points around the ship show the motion.
* **Cockpit in the dogfight** sways with turning and pitching as in the
  original mode with the crosshair.
* `swvideo --dogfight` flies it itself (without sound).
* Buttons on the cabinet: According to Time Pilot 3D, `advmame.rc` puts all four
  yoke buttons on `p1_button1`; the thumb buttons (`p1_button2/3`) then
  do not arrive. Time Pilot 3D therefore reads the yoke itself
  (`src/controls.c` there); this has to be taken over for the dogfight.
  Not checked on the cabinet.

X-wing (outside view, in the game and in the dogfight), at the owner's request:

* **Occlusion:** The parts of the model now hide each other correctly.
  Each face carries its depth (1/X as a plane over the screen) and its
  part (`sw_line.own`, `hide_part`); `hide()` cuts only what lies behind the
  face, and no part its own lines. This also applies to the
  cockpit (its faces lie in front of everything).
* **Engines:** at the back two blue rings (glow), in the dogfight when
  accelerating a flickering blue jet per engine with a white core,
  length with the thrust.
* **S-foils:** In the dogfight the wings close with the thrust (flat
  at full thrust, a good second), and back into X position when slowing down;
  the cannon tips (laser origin) move with them. On the rails open.

## Planned: dogfight mode (discussed 2026-10-14, stage 1 built)

Idea of the owner: another level, endless until death. Flying freely in
space instead of on rails, endless waves of enemies, the Death Star in the
background. A box as the playing field: whoever flies to the border
is turned around automatically ("WHERE ARE YOU GOING?"). Controls like
Time Pilot 3D (`F:\Time Pilot 3D`, its STATUS.md): yoke pitches and turns,
thumb buttons faster/slower with thrust returning to the middle, both
together plus yoke to the side roll around the long axis, triggers fire.

Decided: In the cockpit and in the original view the crosshair stands
fixed in the center of the picture (you aim with the nose, as in Time Pilot 3D).
The owner liked the other suggestions: enemies from the ROM (TIE
fighter, Vader's TIE as a boss wave with his lines, fireballs to
shoot down), shields as in the original with repair by R2 after each
wave, radar and target arrow, the Falcon as a helper every few waves,
Death Star countdown as an option, own high scores with combo, all three
views switchable with Start.

Further decided (2026-10-14): Entry as a fourth choice next to the three
Death Stars at "Select a Death Star". Endless until death like the
original, difficulty rises with the waves. After the choice an
instruction page like the one in Time Pilot 3D (`draw_howto` in its
`src/attract.c`): the yoke as a wireframe model, this time with the Atari logo, and
next to it the X-wing, which shows what the yoke does. Buttons: right
trigger laser; left trigger (front left) proton torpedoes: two
balls like the enemies' shots, each chases its own enemy
(two at once), after that the torpedo has to recharge (charge display). Thumb buttons
faster/slower, both plus yoke to the side roll;
Start switches the view. Hits until an enemy falls (laser): TIE
fighter 1, a new TIE Interceptor (own model, not
in the ROM) 2, Vader's TIE 3. Suggestions still unanswered: recharge time of the
torpedoes about 8 s; a torpedo destroys any enemy with one hit.
Approved on 2026-10-14 ("go ahead and build it"); plan in stages (flying
in the box with Death Star; waves and laser; torpedoes; instruction page;
high scores and finishing touches), after each stage pictures or a video. Technically: a separate game part in C next to the 1983 program
(which knows only rails), with its models, sounds and speech and the
picture path of render/ (occlusion, views).

## Cockpit moves with the ship (2026-10-14)

The crosshair ran over struts and dashboard: In the original it reaches
to x +-112 (of +-127.5) and y -104 downward (measured, yoke in all
corners), i.e. into the console; no cockpit with visible struts fits
around it. The owner's wish: the ship moves along. Now
the cockpit follows the crosshair for 60 % of its travel and leans with it up to
0.22 rad into the turn (around the middle of the sill), slightly smoothed; the
crosshair thus stays in the window. The position of the crosshair comes from its
lines in the picture (drawn by the interrupt, 246 times per second), not from
the game state (13 Hz): the movement is as smooth as the crosshair.
Frame and faces extend far beyond the screen for this. Game,
crosshair and shots unchanged. Not seen on the cabinet.

When choosing the Death Star (SDS) the game also draws nose and cannons;
there the arcade view now always applies, cockpit and outside only in flight.

## No more towers at the end of the surface (2026-10-14)

At the end of the surface distant towers could still be seen. The original clears up
in the fifth round (S1MVGD: GD.SEQ 5 sets Q.KTW, and buildings not yet shown
are no longer set up); only those already visible
still fly by. The extended towers, however, beyond the range
(3C00h) kept drawing all buildings of the map and those of the next round.
Now: beyond the range nothing more as soon as Q.KTW is set, and
never anything from round 5 and later. Not seen on the cabinet.

## Demo flight in the attract mode, service menu on F2 (2026-10-14)

The original's attract mode has no flight scenes (high scores, logo,
flight instructions, score list). At the owner's request there is now a
demo flight (`enh_demo`, on by default, test menu "Attract Demo";
`platform/demo.c`): When the attract mode goes from the score list to the
high scores and nobody has paid (in free play always), the whole machine
is set aside, a credit is given, fire is pressed in rhythm until the game
runs, the middle Death Star is chosen, then the autopilot flies like the
film script (yoke swings, fire in rhythm, shields never run out). Each
section (space, surface, trench) comes from the next view,
the next demo starts one view further. After 112 s, when the game
ends, or as soon as someone inserts a coin or presses a button,
the machine is put back: no credit, no points, no count
in the NOVRAM (saving the NOVRAM is off during the demo), and the
coin or button then counts as usual. The board's sound is reset when
putting back. `tests/test_demo.c`: the demo starts, gets through
space (1 s), surface (33 s) and trench (72 s), each part from a
different view, and the machine is afterwards as before (RAM, NOVRAM).
Finding along the way: A fire press in the one frame that builds an attract
page is not checked by the original (STRTCK does not run there), so
the demo presses in rhythm. Known: The machine's operating-time counters
do not keep running during the demo (they are put back with it).
`swvideo --demo` (with a long `--attract`) films it.

The service menu is now on F2 (MAME's service key) instead of Tab,
likewise the way back from calibration and operator pages.

## Outside view, switching with Start (2026-10-14)

The cockpit option has become one of three views: `enh_view` 0
(arcade, default), 1 (cockpit), 2 (outside); test menu Graphics "View". In
flight (when the game draws nose and cannons, `plane_seen`) the
Start button switches on and does not fire there (`start_fires` in
`platform/controls.c`); otherwise, in the attract mode, it starts the game as before.
The choice is written to the settings file at once.

Outside (`outside` in `render/enhance.c`): its own X-wing model
(`xwing_build`, ~200 lines, ~70 faces: fuselage, tapering nose with
red band, canopy, R2 dome, four wings in X position with red
stripes, four hexagonal engines with red glow at the back, cannons with
barrels at the wing tips) flies 1000 in front of the camera, at the bottom of the picture. It
leans into the turn with the position of the crosshair (up to 31 degrees) and
turns slightly along (up to 13 degrees yaw, 7 degrees pitch); the world remains the
game's view (a chase camera just behind the ship). The game's lasers
are shifted so that they begin at the four cannon tips and end at the same
target (crosshair or hit), their colors
and segments stay. What lies behind the ship is cut off at its
projected faces (together with the cockpit: `hide`),
back sides of the model are left out via the normals. The Falcon's
drawing helper has become a general model for this (`model`,
`model_draw`, `msolid`). Known: Parts of the model do not hide each
other (only each convex shape its own back side), hits remain
those of the game (as in the arcade view). Not seen on the cabinet.

## Cockpit view (2026-10-14)

The owner's wish, from a template in the film: first-person view from
the cockpit instead of the X-wing's nose and cannons, today `enh_view = 1` (test menu
"View", see above), in `render/enhance.c` (`cockpit`): Where the
game draws nose and cannons (SW_TAG_PLANE), their lines are dropped, and
in screen space a cockpit stands: two canopy struts from the upper corners
(below Score and Wave, which stay free) to the sill, a flat
dashboard down to y = -96 from -153 (a good fifth of the screen; the
trench floor stays visible) with a row of lights, two targeting screens, knob,
levers and side consoles with a red frame, ~110 lines. Everything behind it
(stars, enemies, towers, trench, lasers, Falcon) is clipped at three convex
faces, independent of the occlusion setting; texts,
score and crosshair not. Shots unchanged: the lasers run from the place
of the cannons to the crosshair and emerge from behind console and struts.
Not seen on the cabinet: brightness of the frame, frame time (+110 lines per
frame, with the beam about 10 % more), whether the struts get in the way when aiming.

## Menu entry and marquee (2026-10-13)

Until here the game was named after the cabinet's platform (VectorPie)
with the abbreviation VP; the owner wants it called Star Wars Enhanced.
Everything one sees or types is renamed: the menu entry (`name` in
`package/game.ini`), the texts in the program (test menu, Controls Test,
ROM error message, window title), the program itself and the package id
(`swenhanced`; short, because `pkill -x` only knows 15 characters of a name),
the settings file (`swenhanced.ini`), the environment variable for the
ROMs (`SWE_ROMS`), the docs. On the cabinet the folder with its
`save/` (NOVRAM, settings) has moved to `/persistent/games/swenhanced/`.
The GitHub repo has been called `Endprodukt/StarWarsEnhanced` since 2026-10-14
(private; the old address redirects). Not renamed: the local folder.

The menu also showed no marquee: `game.ini` lacked `marquee =
marquee.png` (Tempest 2000 next to it has the key). Version 0.2.0,
marquee the owner's artwork ("STAR WARS ENHANCED"). To be seen in the menu
as soon as the menu restarts (after leaving the game). Not checked
on the cabinet, nor whether the longer title of the test menu (now size 90
instead of 110) fits the width.

Marquee renewed (second artwork of the owner, without the stripes at the bottom; stretched to the full width 1920, 17 % wider than the original), version 0.2.1. **Not yet on the cabinet**, it was off: next time `sh tools/deploy.sh` and the newest `dist/swenhanced-<version>.vpk` (now 0.3.19, with dogfight stage 4) to `/persistent/games/packages/` (0.2.0 is still there).

Package file: `sh tools/pack.sh` (in WSL, after the Pi build) writes
`dist/swenhanced-<version>.vpk` with the SDK's `vp-pack` (its script and
the manifest are stripped of the CR LF that a Windows checkout
brings along). `swenhanced-0.2.0.vpk` is on the cabinet in
`/persistent/games/packages/`; `bzoneii-1.0.0.vpk`,
`gltron-0.1.1.vpk` and `neverball-0.1.0.vpk`, which lay one level too
high, were moved there as well. Installation is via the menu (INSTALL GAMES), not
automatic; installing from the `.vpk` has not been tried on the cabinet.
Open: increase the version in `package/game.ini` before every new package.

Two packages (2026-10-17): `sh tools/pack.sh` makes the one for passing on, without
the owner's film music (only the torpedo PMSF2 of the collection stays in
`sounds/music`; the music is the board's); `WITH_MUSIC=1 sh
tools/pack.sh` makes the private one with all ten pieces into `dist/private/`.
`deploy.sh` still brings everything, music included, to the cabinet.

## Falcon, camera, stars, grid (2026-10-13)

Four wishes of the owner, all implemented (commit after a8f006e):

* **Grid farther, more stars, bunkers more detailed:** plate pattern full
  up to 6000, off at 11000 ahead (before 3500/7000); 64 instead of 28 stars
  above the surface; the red bunkers again with seams at the sides
  and a hatch on the roof (no longer the plate lines from before).
* **Millennium Falcon in the trench** (`enh_falcon`, menu "Falcon Fly-Over"):
  Own model in `render/enhance.c` (`falcon_build`): flat hull disc
  with plateau on top and bottom, two mandibles, hexagonal cockpit tube
  on the right with a turquoise window, radar dish on a stalk, blue
  engine band at the back, ~150 lines, ~50 faces for the occlusion. Each
  line carries the normals of its faces and is drawn only if one
  of them points toward the viewer (the far side of the hull does not show
  through). It comes from behind above, flies over the player and ahead along
  the trench (3.2 s), swaying slightly and leaning into the turn. Start:
  when the player, at his speed, is 3.4 s before the shaft; the shaft
  is precomputed from the trench table for this (`port_ahead`: the
  wedges from the near wedge on, across the pies), because the game itself
  only knows it 7000h ahead. Once per approach; runs again if the shaft
  is missed. In the film: fly-over 133.0 to 134.6 s in the picture (then small ahead
  and away), "EXHAUST PORT AHEAD" from 133.8 s, shaft at 135.8 s.
* **Camera pans** (`enh_camera`, menu "Smooth Camera"): The game is
  a rail shooter, in space the ship turns by itself toward the
  pursued enemy (AIMA) or toward the Death Star (AIMDTH): the turn rate
  RH.RAT is set anew every frame from the position of the target, with a new
  target it jumps to full. With `camera_ease` (sw_machine, from
  `enh_camera`) the rate changes per frame by at most 12 of 127
  (`set_rate` in `game/player.c`): starting up and braking. This intervenes in the
  game (not only in the picture), so that crosshair, hits and picture
  stay together; the recordings run without the flag and still match.
  `tests/test_camera.c`: with the flag never a jump over 12, without the flag there are
  some. The in-between frames remain continuous: the original's remainder mechanism
  (whole steps at S1, remainder only in the picture) adds up linearly.
* **Stars made odd movements:** Cause in the in-between frames
  (`game/subframe.c`): A star that leaves the field of view is reset by the
  original at a random place ahead. If the new place was
  less than 1000h from the old one, the interpolation ran it there, a
  stroke across the screen. Now a star replaced in the picture
  (`star_gen` different) is not interpolated. The original itself
  jumps at this point (not visible there, because the old star
  lay outside).

Not seen on the cabinet: Falcon size and brightness, the feel of the
camera (12 per frame is a setting), frame time with 64 stars and more
grid.

## Towers exactly like the original, without "vegetables" (2026-10-13)

The owner wants the towers exactly as large and as far apart as in the
original, but not growing out of the ground, and no option for it. A
setting with three strengths of the scale (commit 096c3c5) has therefore been
taken back (80b88fd).

Finding: The original's size and spacing are its linear scale around the
foot (1 - XP/4000h, `M.GDSC`), and exactly that is the growing: The foot stands
at the true place, the tip rises faster than the perspective. Both
at once is geometrically impossible; a distortion of the whole scene in
depth that made ground and towers consistent would make the ground converge to a point
instead of a horizon line and push the towers at the sides
toward the center (rejected). In the original the growing is not noticed because no
ground is there as a reference. So: towers exactly like the original (scale, foot,
hit test 100h), and the plate pattern lies only near
the player (full up to 3500, off at 7000 ahead; before up to 26000). Distant towers
thus stand on dark ground as on the cabinet and grow without a reference; when
they reach the plate pattern they have 70 to 80 % of their size. Not seen
on the cabinet.

## Free play (2026-10-13)

The owner wanted a game without coins. The original already has it:
the option COIN VALUE set to FREE PLAY (in the test menu under Game Options
or on the cabinet's options page). Then the attract mode always holds
a credit, "INSERT COINS" no longer appears, it says "PRESS FIRE TO
START", and fire starts the game. The line "1 CREDIT" below it, which the
original shows in free play, is gone at the owner's request (vwcoin in
`src/game/mainline.c` breaks off after the first line at COIN VALUE 0; the
recordings run with 1 COIN 1 PLAY and are untouched). `tests/test_options.c`
checks: credit present, "PRESS FIRE TO START" drawn, no credit line,
fire starts.

## Marquee (2026-10-13)

The cabinet's marquee screen (HDMI, 1920x360, `advmame.rc`:
`vector_aux_marquee_display_mode stretch`) showed only the console's
cursor: The program asks the SDK for `marquee.png` next to the
executable, and the package had none. Now
`package/marquee.png` is included (1920x360, yellow lettering over stars, drawn
with Pillow from `impact.ttf`; no Atari artwork), `deploy.sh` copies
it along, the build puts it next to the program. Whether it appears on the cabinet is
not yet seen as of writing.

## Scene changes (2026-10-13)

The owner found the transitions between scenes untidy. Checked with
`swvideo --check` (line count of each frame, `SWDBG=1` writes them) and
frame sequences around every phase change (attract pages, space, approach,
surface, trench):

* No frame drops out (no dips in the line count, no black
  frames).
* **Bug found and fixed**: Between setting up a phase and
  its first view, the phase number already stands on the new phase,
  but the display list and the viewer's view vectors are still those
  of the old one. The extensions drew the surface for this one frame
  with the approach's view (huge plates across the
  screen). Now the machine remembers which phase the list drew
  (`sw_machine.view_phase`, set in IVGEN1), the extensions go by it
  and take the viewer's position from U1 (the way the view
  used it); in the fallback path (lines moved to the next frame) they get the state
  of the precomputed next frame.
* A frame in which the player jumps (position or place by more than 2000h)
  counts as a cut for the in-between frame: It is not computed from the state before.
* At the end of the surface (the game clears the buildings, the player
  dives to the trench) the plate pattern fades out in 0.8 s, instead of
  sweeping across the screen on the dive.
* What remains and comes from the original: On a phase change the
  game shows the new scene from one frame to the next (no cross-fade);
  in the attract mode the pages change abruptly. That is how it is built.

## Seen on the cabinet (owner, 2026-10-13)

First run of the C game with the extensions on the device. Finding and what
came of it:

* **Sorted strokes wobble** on the tube (the order, and with it
  where the beam comes from, changes from frame to frame). `sort_strokes` is
  removed because of that. Price, measured on the cabinet: The high-score page runs
  unsorted at 37 instead of 50 frames per second (idle travel 172000 instead of
  60000 units).
* **Surface was too crowded**: Stars there reduced to 28 twinkling ones
  (first removed completely, then a few back on request), towers only
  where the game shows them (100h to 3C00h ahead, 45 degrees to each
  side, set up, bunker not shot down; at first the cone was
  wider, and at the start towers stood next to the player), bunkers without seams.
  At the end of the surface one thus flies into the dark again as in the original.
  In between, shot-down bunkers stayed standing (the check for
  "damaged" was lost in the rebuild): fixed.
* **Trench, obstacles change their shape**: The cabinet cannot clip a line
  at the edge of the picture; for near objects it pushes the points
  just behind the edge (BJBCLP), which bends them. New `enh_clip = 1`
  (TRENCH CLIPPING): The drawing routines note the points of the
  wall objects in space, `enhance.c` redraws their lines from them
  and clips correctly; the faces for the occlusion are likewise
  clipped in space. Measured: Where the game has not shifted anything, the
  redrawn lines lie on average 0.13 units from those of the game. Not yet seen
  on the cabinet.
* **Test menu**: no self-test button on the cabinet is assigned (all buttons of the
  yoke are fire); the Tab key now opens and closes it.
* Performance in the game is fine according to the owner. The number `frame` in the log
  is the time the board needs for a frame, not the Pi's computing time.

## Known limits

* **Extensions over the surface**: not seen on the cabinet; they
  bring clearly more lines into the picture (ground about 80, a near tower about
  60), whether the board manages this smoothly is open. The towers shoot
  and are hit by the cabinet's rule. Chases exist only over the surface,
  not in space or in the trench. Death above the ground and the transition
  to the trench have only been looked at briefly.

* **Test menu**: unseen (see above). YOKE AUTO CENTER switches only with
  fire, not with the yoke; when off, the cabinet program learns
  the yoke's range by itself again only after the next start. The
  controls test (`--controls`) is not yet a menu item.

* **Occlusion**: not yet seen on the cabinet, and the computing time on the
  Pi is not measured (each line against each face; unremarkable on the PC).
  The walls and floor of the trench and the floor of the surface
  hide nothing (they have no faces, only edges). Explosions and
  fireballs hide nothing. Texts show through the lettering. The
  Death Star in the selection and in the approach hides only stars. In mode
  `--original` (program on the 6809) there is no occlusion.

* `smooth = 3`: Discontinuous things (laser, blinking, counters, newly appearing
  things) change in the rhythm of the game frames as in the original; rotations
  are cross-faded in a straight line (at 10 degrees per frame 0.4 % too small in the
  middle). The crosshair follows the yoke in the rhythm of the restarts of the
  vector generator (as in the original), not with every frame.
* `smooth = 1` shows the world one game frame (50 to 90 ms) later; the
  crosshair not. `smooth = 2` has no delay but overshoots on
  changes of direction.
* Lines without a partner in the previous frame jump there (as in the original).
* The pace model is fitted to five recordings up to wave 4. Later
  waves (more towers, random trenches) lie somewhat outside it; the
  error there is not measured.
* Not run through by any recording and therefore only read, not checked:
  death above the ground (G0D), the message "AVOID CATWALKS", the
  histogram halving in EEACCT and CHKEE with a broken checksum.
* No menu entry on the cabinet, no `.vpk`.
* Statistics and options page need the test switch: `p1_button6`
  toggles it, `p1_button5` is the aux coin. Both are not assigned on the cabinet
  as long as `advmame.rc` does not assign them.
* The options page's NOVRAM test is ported, but was not reliably triggered in any
  recording. In the original it holds the picture for 66 frames; here
  the work is done at once and then 66 frames are waited.
* **Demo pilot**: It flies only the middle Death Star (wave 3) and
  knows towers and force fields from the RAM, fireballs and targets from the
  picture. Fireball salvos over the surface sometimes hit it, rarely
  a force field. A piece can begin or end in the middle of a transition (approach, descent
  into the trench): it is a hard cut.
  Game and pilot together compute 112 s of demo in 0.3 s on the PC; on the Pi
  not measured.

## Next

1. On the cabinet: `sh tools/deploy.sh`, then check picture, sound and controls;
   compare `smooth` 1 against 2.
2. Track the crosshair with every frame (now: in the rhythm of the restarts
   of the vector generator).
3. Check the sound by ear: POKEY high-pass, TMS5220 more exactly (comparison
   with a MAME recording, `-wavwrite`). The program is ported; what
   may still differ from MAME are the components.
4. Menu entry and `.vpk`.
5. More recordings: from wave 5 (heavy Death Star), death above the ground.

## Way of working

* Original source: `F:\_ref\star-wars` (historicalsource/star-wars), MAME source:
  `F:\_ref\mame-src` and `F:\mame\src`. MAME 0.289 with ROM set: `D:\Mame`.
* Checking (WSL): `sh tools/checkall.sh RECORDINGS-FOLDER /mnt/d/Mame/roms`
  for the C routines, with `--emu 1` for the original program on the core.
* Recordings: `tools/mame/swcapture.lua`, with `SWCAP_PLAY` as the played
  script, e.g. `"120 coin 1;160 coin 1;240 fire 3;560 aim 5"` (coin pulses
  16 to 200 ms: `coin 1`); `test N` and `aux N` are the test switch and
  aux coin (for these use a separate `-cfg_directory`: MAME remembers
  the test switch, and with it the next recording starts in the
  self-test). What the game does not reach by itself is fetched by
  `poke ADDRESS VALUE` (`4860 06`: refill shields, `4844 01`: trigger
  torpedo, `4860 00`: no shields left); the comparison makes the
  intervention too. A recording needs 44 KB per frame.
* Refitting the pace: `swcompare RECORDING --csv FILE` writes per frame
  MAME's computing time and the counted work; `python3 tools/fitpace.py FILES`
  (needs numpy) computes from it by least squares the costs for
  `src/game/pace.c`.
* Numbers in the source are hexadecimal, decimal ones have a point (`24.`).
  Symbol names are shortened to 6 characters. Local tables are not in
  WSROOT.MAP: addresses are found by `python3 -I tools/dis6809.py ROMZIP ADDRESS`
  (phase table at 6044). The "J" routines are in the second ROM bank
  (`O8`/`O16`).
* Never change source text via shell heredocs (backslashes and apostrophes
  get broken there).
* Pi build: `cmake --build --preset pi-clang`, then `sh tools/deploy.sh`.
  **Under Windows (Git Bash) with the retcomm cmake in the PATH (README), not
  in WSL:** there the build aborts because of the Windows cache in `build-pi`,
  and `deploy.sh` then silently copies the old binary (the same md5 as last
  time is the warning sign; happened 2026-10-17). The Linux build
  and `ctest`, on the other hand, run in WSL (`cmake --build --preset linux`).
* `deploy.sh` first ends the old `run.sh`, then the game: otherwise
  the old `run.sh` restarts the menu (getty) when its game dies,
  while the new game is already running; both hold screen and board, the
  game stands still (log: `Could not queue pageflip: -13`, no statistics;
  2026-10-17). If the script reports `MENU-STILL-RUNNING`, on the Pi run
  `sudo systemctl stop getty@tty1` and restart `run.sh`.

Repository (2026-10-19): `sounds/` (originals of the board, film music,
film lines) and `package/sounds/music` (film music) went into the repo, on the
assumption that it was private. **It is public** (noticed 2026-10-26). Therefore
film music and film lines (`sounds/music`, `sounds/voice`,
`package/sounds/music`, `package/sounds/voice`) have, at the owner's request,
been removed from the whole history of `native-port` and the tags `v0.3.31`,
`v0.3.32` (rewritten, force-push) and are in
`.gitignore`; they exist only on the PC now. The sharing package (`tools/pack.sh`
without `WITH_MUSIC=1`) has no film lines any more since then either. The local
branch `esb-port` (not on GitHub) still has the old history with the
files: do not push it like that. The SW Ultimate SFX Collection (1.4 GB) and
videos stay out.

GitHub release (2026-10-19): `v0.3.31` with the same package as in the Google Drive folder `games`.
GitHub release (2026-10-26): `v0.3.32` (demo in short pieces, no sound blips in the quiet attract mode); not yet in the Drive folder. With every new package in the Drive, also create a release (`gh release create v<version> <package>.vpk`).
