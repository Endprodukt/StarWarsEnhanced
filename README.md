# Star Wars Enhanced

Star Wars (Atari, 1983) for
[VectorPie](https://github.com/Goldfinch2/VectorPie): Raspberry Pi 5,
USB-DVG, vector monitor (Wells-Gardner 6100). The game unchanged, the
picture smooth.

* **The game is the original, in C**: The 1983 program is ported to C by hand,
  line by line, and checked frame by frame against MAME captures
  (27,313 frames, all equal), and so is the sound board's program
  (effects, music, speech). In normal use no 6809 runs alongside any more.
  Math Box, divider, NOVRAM, POKEYs and speech chip are modelled as
  components.
* **Fixed pace**: In every phase the game computes at a fixed rate, the
  cabinet's average (trench 13, banner 20.5 frames per second): as fast as
  the original, without its wobble. Adjustable (`steady_frames`).
* **Test menu**: The self-test button opens a menu of the port's own with the
  Enhanced Settings (each can be switched off), a sound test, picture
  calibration and the way to the cabinet's operator pages.
* **Death Star surface** (Enhanced, four switches): the ground as a plating
  pattern, stars in the sky, towers in true perspective with more detail and
  shown from further away, and in the distance TIE fighters chasing an
  X-wing.
* **Occlusion** (Enhanced, off with `occlusion = 0`): ships, towers, the logo
  and your own guns are solid; what lies behind them is not drawn.
* **Smooth picture**: Between two game frames (the game computes 12 to 20 per
  second) every line is moved, the picture comes at the board's rate. The
  crosshair follows the yoke directly and is centered from the first
  picture.
* **Sound**: the sound program in C, four POKEYs, TMS5220 speech.
* **Way to a remaster**: From the game state of the C routines (`src/game`)
  the picture can later be drawn anew (hidden lines, new models).

Current status, what is still unchecked on the cabinet, and next steps:
[docs/STATUS.md](docs/STATUS.md). Plan and background:
[docs/PLAN.md](docs/PLAN.md). All the game's sounds:
[docs/SOUNDS.md](docs/SOUNDS.md).

## ROMs

The game's data (vector ROM, objects, sound program, speech) is read at run
time from your own MAME ROM set **`starwars1`**. None of it goes into the
repository or the package. Needed are `starwars1.zip` and `starwars.zip` (the
parent set). Older sets work too (`starwar1.zip`, Math Box PROMs with another
CRC, without the AVG PROM), as they lie on a VectorPie cabinet. Searched in:

1. `--roms PATH`
2. `dir_rom` from the cabinet's `advmame.rc`
3. `$SWE_ROMS`, `./roms`, `~/.advance/rom`, `~/mame/roms`, `/home/pi/.advance/rom`

Files are recognized by CRC; names and how they are split across the zips do
not matter.

## Building

```sh
git clone --recursive <repo>            # or: git submodule update --init
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
cmake --preset pi    && cmake --build --preset pi       # cabinet (WSL2 Debian trixie, see SDK)
cmake --preset pc    && cmake --build --preset pc       # Windows .exe
```

The SDK lies unchanged as a submodule under `external/VectorPie`.

For the cabinet without WSL, from Windows (Git Bash, clang and lld on the
PATH, as sysroot a copy of the cabinet's libraries, by default the
Tempest 2000 port's):

```sh
export PATH="/c/Users/ocare/.local/share/retcomm/toolchains/cmake-clang-v1/latest/bin:$PATH"
cmake --preset pi-clang && cmake --build --preset pi-clang
sh tools/deploy.sh        # copy to the cabinet and restart there
```

`tools/deploy.sh` puts everything into `/persistent/games/swenhanced`, ends a
running game and starts the new one through `run.sh` (stops the menu and
brings it back afterwards). Output and picture costs are in `run.log` there.

This is also the package the cabinet's menu knows
(`external/VectorPie/sdk/docs/07-packages.md`): one folder per game under
`/persistent/games/<id>/` with `game.ini` (manifest), the program and
`marquee.png`. From `package/game.ini` the menu takes the entry's title
(`name = Star Wars Enhanced`), the group (`manufacturer = Atari`), the
description and the marquee (`marquee = marquee.png`, 1920x360; `vp_run` shows
the same picture on the marquee screen during the game). The menu reads the
manifests at start-up: after a deploy the new title shows once the game is
left and the menu comes back.

As a package file for another cabinet or a USB stick (in WSL, after the Pi
build):

```sh
sh tools/pack.sh          # -> dist/swenhanced-<version>.vpk
```

On the cabinet a `.vpk` goes into `/persistent/games/packages/` (by scp or
network share) or onto a USB stick in its folder `games/`. It is not
installed by itself but in the menu: Settings -> GAMES -> INSTALL GAMES;
there it shows as NEW, UPDATE, SAME or OLDER against what is installed. The
version is in `package/game.ini`: count it up before a new package, or it
counts as SAME. The package holds the program, `game.ini`, `swenhanced.ini`
and `marquee.png`, not the ROMs (the game looks for them in
`/home/pi/.advance/rom`) and not `save/`.

## Checking against MAME

```sh
# capture (MAME 0.289):
SWCAP_OUT=attract.swcap SWCAP_TICKS=3000 mame starwars1 -autoboot_script tools/mame/swcapture.lua -video none -sound none -nothrottle
# with a played script: two coins, start, then yoke and fire
SWCAP_PLAY="120 coin 1;160 coin 1;240 fire 3;560 weave 7" SWCAP_OUT=play.swcap SWCAP_TICKS=2600 mame starwars1 -autoboot_script tools/mame/swcapture.lua -video none -sound none -nothrottle
# build, tests, comparison of all captures in a folder (in WSL):
sh tools/checkall.sh FOLDER /mnt/d/Mame/roms --emu 1    # the original program on the 6809 core
sh tools/checkall.sh FOLDER /mnt/d/Mame/roms            # the routines ported by hand
```

For each frame `swcompare` loads the original's memory, runs the native
frame and compares RAM, Math Box RAM and display list with the original's
next frame. At the end it shows the pace per phase (interrupts per frame,
original and native). `--show N --max N --from F --to F --phase HH` narrow
the output, `--csv FILE` writes computing time and work counters per frame
for the pace model (docs/PLAN.md).

## Programs

| Program | Purpose |
|---|---|
| `swenhanced` | the game: game and sound program in C, no 6809 |
| `swenhanced --original` | the whole original programs (game and sound) on 6809 cores, for comparison |
| `swenhanced --nosound` | without the sound board |
| `swenhanced --controls` | test picture for the controls: crosshair on the yoke (with the original's lag and the yoke calibration), fire turns it red, coins count credits |
| `swcompare CAPTURE.swcap` | checks the native core frame by frame against a MAME capture |
| `swenhanced --calibrate` | calibration picture: screen edge, clip limits, circle, all colors at 7 brightnesses. The joystick changes, button 1 switches the mode (size, position, gamma/floor, brightness), button 2 saves to `save/swenhanced.ini` |
| `swvideo --wav SOUND.wav > PICTURES.rgb` | a film of the port without a cabinet: attract, then a played game; pictures as raw RGB24 on standard output, sound as WAV. `python tools/swvideo/mux.py FILM.mp4 SOUND.wav 960 720 60 < PICTURES.rgb` makes an MP4 of it (needs PyAV) |
| `avgreplay CAPTURE.swcap` | plays display lists captured in MAME on the monitor at the original pace; `--lerp` shows the interpolation, `--loop` repeats |

General options: `--dvg [PORT]`, `--window`, `--windowed`, `--vertical`,
`--hz N` (picture rate), `--config INI`. `VP_STATS=1` prints the costs per
picture (vectors, blank moves, path lengths).

## Controls

Bound through the cabinet's `advmame.rc`, as with MAME's Star Wars:

| Cabinet | VectorPie action |
|---|---|
| Yoke left/right, up/down | `p1_stickx`, `p1_sticky` |
| Left fire (also start) | `p1_button1`, `start1` |
| Right fire | `p1_button4`, `start2` |
| Left / right thumb | `p1_button2` / `p1_button3` |
| Coin L / R | `coin1` / `coin2` |
| Aux coin (operator: next page in the self-test) | `p1_button5` |
| Self-test button: opens and closes the test menu (see below) | `p1_button6` |

Every digital input (yoke as directions, both fires, both thumbs, start 1/2,
coins, aux, test menu) can be bound freely on the menu page CONTROLS (up to
two keys, joystick buttons, axes or hats per input; fire waits for the input,
left/right restores the cabinet's binding). A yoke bound to digital inputs
eases in (0.3 s) and out; without a binding of its own the yoke stays purely
analog (the cabinet's `p1_left` names are the same axis). The binding is
stored as `bind_*` in the INI (`bind_fire_l = key:44, joy0:button0`).

### Test menu

The F2 key (MAME's service key) or the self-test button pauses the game and
opens the menu; the same key leads out again and back from the calibration
and the operator pages. Yoke up/down (also arrow keys and the first joystick)
picks a line, left/right or fire (Return) changes it, fire opens a page;
BACK, left thumb or Backspace leads to the main page, F2 saves and returns to
the game.

| Page | Lines |
|---|---|
| Game Options | Coin Value, Left Mech, Right Mech, Bonus Adder; Starting Shields, Difficulty, Bonus Shields, Music in Attract: the game's options, the same as on the cabinet's options page; they live in its NOVRAM (`save/nvram.bin`) and apply at once. Coin Value = FREE PLAY: no coins, fire starts the game, and the attract mode only shows "PRESS FIRE TO START" without a credit line |
| Graphics | Smooth Motion, Smooth Camera, Frame Pace, Occlusion, Yellow Logo, View, Attract Demo, Ship Explosion; Surface Plating, Detailed Towers, Distant Dogfights; Stars, Trench Clipping, Falcon Fly-Over (like `smooth`, `camera_ease`, `steady_frames`, `occlusion`, `logo_yellow`, `enh_*` below) |
| Sound | Replaced Sounds, Engine Sound; Music / Effects / Speech / Engine Volume (0 to 200 %, step 10, `vol_*`); Sound Test (yoke left/right picks one of the 59 requests from [docs/SOUNDS.md](docs/SOUNDS.md), fire plays it) |
| Controls | Yoke Auto Center; one line per digital input (binding, see above; a held binding lights up green); All Inputs as the Cabinet's |
| Screen | Size Across, Size Up (in % of the picture area), Position Across, Position Up (in board units), Brightness (%), Gamma, Black Level (%): with numbers, left/right changes; behind it the white edge through the four blue points the game draws in every picture (they must be on the tube), and the center cross. Test Pattern (the test picture of `--calibrate`, which now also shows the value; fire switches size, position, gamma, brightness, the yoke adjusts) |
| Arcade Operator Screens | statistics, options, resets and NOVRAM test of the cabinet's program (the aux coin turns the pages); F2 leads back to the game |
| Save and Exit | like F2 |

## Marquee

The SDK shows `package/marquee.png` (1920x360) on the cabinet's HDMI screen
while the game runs. For a picture of your own, replace the same file (PNG;
size after `advmame.rc`, `vector_aux_marquee_display_mode`).

## Settings

`swenhanced.ini` (package defaults next to the program, your own values in
`save/swenhanced.ini`):

| Key | Meaning |
|---|---|
| `steady_frames` | `1` (default): the frames of a phase come at a fixed interval, the cabinet's average in that phase; the same pace as the original, without wobble. `2`: 20.5 per second everywhere (in the game about 50 % faster than the cabinet). `0`: every frame as long as the cabinet's 6809 computed on it |
| `smooth` | Movement between the game frames: `3` (default, ON in the menu) recomputed from the game state, without delay and with correct perspective; on the attract pages the lines move toward the picture of the next frame, computed ahead. `0` (OFF): every game picture as in the original. (`1` and `2`, lines moved between or ahead of the last frames, are still read but no longer offered: one shows the world a frame late, the other overshoots.) |
| `yoke_autocenter` | `1` (default): crosshair centered from the first picture. The yoke goes through exactly, and its rest position counts as the middle: it is learned at start-up and followed while the yoke lies still in attract mode. `0`: like the 1983 cabinet, which learns the yoke's range anew after every power-on |
| `vol_music`, `vol_effects`, `vol_speech`, `vol_engine` | volumes of the four groups of replaced sounds in percent (default `100`; 0 to 200). Speech also applies to the speech chip; effects also to the POKEYs |
| `bind_left`, `bind_right`, `bind_up`, `bind_down`, `bind_fire_l`, `bind_fire_r`, `bind_thumb_l`, `bind_thumb_r`, `bind_start`, `bind_start2`, `bind_coin1`, `bind_coin2`, `bind_coin_aux`, `bind_test` | your own binding of a digital input, up to two separated by a comma: `key:N` (SDL scancode), `joyN:buttonN`, `joyN:axisN+`/`-`, `joyN:hatN:up` (`down`, `left`, `right`). Without the key, the cabinet's `advmame.rc` applies |
| `sound_replace` | Enhanced. `1` (default): sounds for which a WAV file with their name lies in `sounds/` (in `voice/`, `music/`, `effects/` or `test/`) come from the file ([docs/SOUNDS.md](docs/SOUNDS.md)). `0`: all sounds from the chips as on the cabinet |
| `yoke_center_x`, `yoke_center_y`, `yoke_center_known` | the yoke's learned rest position; the program writes these itself |
| `logo_yellow` | Enhanced. `1` (default): the STAR WARS logo in attract mode is yellow. `0`: blue as on the cabinet |
| `occlusion` | Enhanced. `1` (default): things are solid, what lies behind a ship, a tower, the logo or your own guns is not drawn. `0`: see-through wireframes as on the cabinet |
| `enh_surface` | Enhanced, above the surface. `1` (default): the ground around the player as the Death Star's plating pattern (full up to 6000, faded out up to 11000 ahead) |
| `enh_stars` | Enhanced. `1` (default): twinkling stars above the surface (64) and many above the trench, in the strip between the top edges of the walls |
| `enh_towers` | Enhanced. `1` (default): towers and bunkers more detailed, in size and distance as on the cabinet (its linear scale with distance included), but visible from 6000h instead of 3C00h ahead, small and dark there, brighter as they come nearer; hits as on the cabinet. `0`: the cabinet's towers, which grow out of the ground as they come nearer |
| `enh_dogfights` | Enhanced. `1` (default): in the distance TIE fighters chase an X-wing across the sky |
| `camera_ease` | Enhanced. `0` (default): the ship turns toward the enemy and the Death Star as on the cabinet, at once at full rate. `1`: turns ease in and out (at most 12 of 127 per picture); this changes the game, enemies then stay to the side longer, the turn toward the Death Star comes later. The old key `enh_camera` is ignored |
| `enh_clip` | Enhanced, in the trench. `1` (default): near obstacles keep their shape, their lines are cut off at the picture edge. `0`: as on the cabinet, which pushes the points of near objects to the edge |
| `enh_falcon` | Enhanced, in the trench. `1` (default): a good three seconds before the exhaust port the Millennium Falcon comes over the player from behind and flies off ahead down the trench (its own model, solid). `0`: nothing |
| `enh_demo` | Enhanced. `1` (default): in attract mode a demo flight comes before the high scores (space, surface, trench of the middle Death Star, a good 110 s), each section from another view, the next demo starts with the next one. It leaves nothing behind (no credit, no points, no count in the NOVRAM); a coin or a button ends it at once and then counts as always. `0`: the cabinet's attract mode, text pages only |
| Dogfight | Enhanced, under construction (stage 4), **currently not selectable** (`SW_DOGFIGHT_OFFERED 0` in `src/main.c`; the fourth Death Star is not shown, the code stays). When offered: on "Select a Death Star" aim at the fourth Death Star DOGFIGHT / ENDLESS at the bottom right and fire: fly freely in a box, endless waves of TIE fighters, Interceptors and Vader, 6 shields, R2 repairs one after each wave. The yoke yaws and pitches, right trigger lasers, left trigger two homing proton torpedoes (reload 7 s), three wingmen fly along, thumb buttons faster/slower, both plus yoke to the side roll, start switches the view; at the edge the ship turns around by itself. A yoke whose name contains "yoke" is read directly. Radar at the top center. First the HOW TO FLY instructions (the trigger starts), at the end a high score table of its own (save/dogfight.hi). F2 leads back. |
| `enh_view` | Enhanced: where you see from in flight. `0` (default): as on the cabinet, the X-wing's nose and guns. `1`: from the cockpit: canopy frame with two struts to the top corners and a flat dashboard below, what lies behind them is not drawn; the lasers come out from behind the console and struts. `2`: from outside, behind the X-wing: a model of its own (fuselage, nose, canopy, R2, four wings in X position with red stripes, four engines with a red glow, guns at the wing tips) that banks into turns with the yoke and turns slightly along; the lasers start at its gun tips and go where the game sends them. In flight the start button switches on (arcade, cockpit, outside) and does not fire there; in attract mode it starts the game as before. The choice is saved |
| `enh_camera` | Enhanced. `1` (default): the camera's turns (in space the ship turns by itself toward the enemy it chases or toward the Death Star) pick up speed and slow down, instead of turning at full rate from the first picture as on the cabinet. In the game itself, not only in the picture: the turn rate changes by at most 12 of 127 per picture |
| `avg_*`, `margin_*`, `flip_*` | position and size of the picture, set with `--calibrate` |
| `gamma`, `floor`, `brightness`, `color_*`, `defocus_mode` | brightness and colors. `brightness` up to 2.0 (default 1.7: a normal line of the game, intensity 80h, arrives at 95 %) |

The game's options (coins, shields, difficulty, music in attract mode) are
set in the test menu under GAME OPTIONS, or as on the cabinet on its options
page (Arcade Operator Screens, then the aux coin), where the resets and the
NOVRAM test are too. They are stored with the bookkeeping and the best three
scores in `save/nvram.bin`. The board's DIP switches (`opt0`, `opt1`) only
serve the cabinet's program as defaults when the options in the NOVRAM are
reset.

## Captures from MAME

```sh
SWCAP_OUT=attract.swcap SWCAP_TICKS=2000 \
  mame starwars1 -autoboot_script tools/mame/swcapture.lua
build-linux/avgreplay attract.swcap --lerp --windowed
```

For each game tick VG RAM, RAM and Math Box RAM are saved, and at the start
of each frame the same memory, the NOVRAM, the random numbers read and the
6809's computing time. Format in `src/capture/capture.h`. The captures serve
for calibration and as the reference for the native core.

## Math Box

`tools/mbasm.py` assembles the original microcode (SWMP.MAC from
historicalsource/star-wars) and checks it against the PROMs' CRCs. From it
it generates `src/mbox/mbox_programs.c`:

```sh
python3 -I tools/mbasm.py SWMP.MAC --check
python3 -I tools/mbasm.py SWMP.MAC --c src/mbox/mbox_programs.c --prom-header tests/mbox_prom.h
```

## Symbols

`src/core/sw_symbols.h` holds all addresses of the original load map
(`WSROOT.MAP`) under their original names and is generated with
`tools/mapsyms.py`.

## Legal

Star Wars is a trademark of Lucasfilm, the game is by Atari. Program ROMs,
vector ROM and sound ROMs are not included, they come from your own ROM set.
The Math Box microcode, however, is included, as generated C code and as a
test file (`tests/mbox_prom.h`). It is generated from the published source
(SWMP.MAC) and matches the PROMs in content. The project is meant for private
use on your own cabinet.

The vector generator's timing follows MAME's `avgdvg.cpp`, the TMS5220's
tables come from MAME's `tms5110r.hxx` (both BSD-3-Clause, credits in the
source).
