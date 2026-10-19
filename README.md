# Star Wars VP

Star Wars (Atari, 1983) as a native port with remaster options for
[VectorPie](https://github.com/Goldfinch2/VectorPie): Raspberry Pi 5,
USB-DVG, vector monitor (Wells-Gardner 6100).

* Game logic true to the original: at the original pace (20.51 Hz game, 41 Hz VG,
  246 Hz IRQ), Math Box in fixed point, checked against MAME captures
* Smooth picture: interpolation between the ticks, 50–60 Hz on the
  monitor
* Remaster: hidden lines, new models, sound either original
  (POKEY/TMS5220) or new

Status: **step 3 of 8** (skeleton, Math Box, interrupt and controls). The
current work is on the branch `native-port`. Plan and background:
[docs/PLAN.md](docs/PLAN.md).

## ROMs

The game's data (vector ROM, objects, sound program, speech) is read at run
time from your own MAME ROM set **`starwars1`**. None of it goes into the
repository or the package. Needed are `starwars1.zip` and `starwars.zip` (the
parent set). Searched in:

1. `--roms PATH`
2. `dir_rom` from the cabinet's `advmame.rc`
3. `$SWVP_ROMS`, `./roms`, `~/.advance/rom`, `~/mame/roms`, `/home/pi/.advance/rom`

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

## Programs

| Program | Purpose |
|---|---|
| `starwarsvp` | the game; for now the ROM and Math Box check and a test picture for the controls: crosshair on the yoke (with the original's lag and the yoke calibration), fire turns it red, coins count credits |
| `starwarsvp --calibrate` | calibration picture: screen edge, clip limits, circle, all colors at 7 brightnesses. The joystick changes, button 1 switches the mode (size, position, gamma/floor, brightness), button 2 saves to `save/starwarsvp.ini` |
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

## Captures from MAME

```sh
SWCAP_OUT=attract.swcap SWCAP_TICKS=2000 \
  mame starwars1 -autoboot_script tools/mame/swcapture.lua
build-linux/avgreplay attract.swcap --lerp --windowed
```

For each game tick VG RAM, RAM and Math Box RAM are saved, format in
`src/capture/capture.h`. The captures serve for calibration and as the
reference for the native core.

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
