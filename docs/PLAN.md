# Star Wars Enhanced – Architecture and plan

Goal: gameplay as identical as possible to the 1983 cabinet, a much smoother
picture on a Raspberry Pi 5 with USB-DVG and Wells-Gardner 6100, plus remaster
options for graphics and sound. The basis is the original source code with
its comments
([historicalsource/star-wars](https://github.com/historicalsource/star-wars)).

## Timing of the original (WSINT.MAC, WSMAIN.MAC)

| Clock | Rate | Purpose |
|---|---|---|
| IRQ | 12.096 MHz / 4096 / 12 = **246.09 Hz** | Coins, switch debouncing, pots (A/D), game time |
| VG | every 6th IRQ = **41.02 Hz** | Buffer swap, crosshair (`RHPOS`), laser, flash and shot animations |
| Game | every 12th IRQ = **20.51 Hz** | The entire simulation (`GMTIMR = 11`, `WAITFRAME` → `TPHASE`) |

* Movements run **per tick**, with no time factor. Phase times count ticks
  (`CMPD #2*20.` = 2 s).
* Double buffering via `VGSYNC`: Each 20 Hz frame is drawn about twice.
  This is the cause of the judder.
* The rotation is already separate in the original: The simulation state
  changes in steps of 4.99° or 0.895°, the remainder (`RH$RSD`) is applied
  only to a copy for the display (`RESIDU`).
* The world coordinates overflow at 16 bits (ground phase: `IFVS → INC GD.SEQ`),
  so interpolation is done modulo 2¹⁶.

## Structure

```
sw_core   C, fixed point, bit-exact   clocks · Math Box (Q2.14) · phases
  │                                    from WSMAIN, WSGRND, WSBASE …
  ├─ world state per tick  ──►  renderer "Original": lines like the AVG,
  │                              interpolated
  │                         └─►  renderer "Remaster": vp3d, hidden lines,
  │                              fog, new models
  ├─ sound events (AUDxx / SPKxx)
  │                         ──►  "Original": sound driver from SND*.MAC
  │                              + POKEY ×4 + TMS5220, emulated
  │                         └─►  "Remaster": samples / OGG
  └─ AVG display list (HUD, text) ──► avg.c ──► dvgmap.c ──► vp_dvg ──► USB-DVG
```

### Why a loop of its own instead of `vp_run`

The SDK stays unchanged. `vp_run` gives `draw()` no interpolation factor
and clocks updates only in whole Hz. `src/platform/app.c` therefore takes over
the conventions of `vp_run` (choice of output, `--dvg/--window/--windowed`,
`ui_cancel`, Ctrl+C, marquee, `VP_STATS=1`) and adds the 246 Hz clock and a
freely selectable frame rate. All SDK functions used are public.

### Sound without changes to the SDK

`vp_audio` only plays samples and files; there is no callback for self-generated
sound. The original sound (emulated POKEYs and TMS5220 in real time) therefore
runs through an SDL audio device of its own with its own mixer. SDL2 is linked
through the SDK anyway. Remaster samples go through the same mixer, so there
is only one audio device.

### Reference for "identical"

`tools/mame/swcapture.lua` records VG RAM, RAM and Math Box RAM in MAME
(`starwars1`) for every game tick. The native core has to deliver the same
state, tick by tick. For determinism, the inputs read and the random numbers
(`PRNG`, 0x4703) will also be recorded and fed in later.

## Two ways, one game (since 2026-10-13)

Porting the game by hand means about 20,000 lines of 6809 assembler.
So that it already runs completely and unchanged before that, there are two
ways side by side:

1. **The original program on a 6809 core** (`src/cpu/m6809.c`,
   `src/core/emu.c`), the default of `swenhanced`. It runs against the
   native machine (`sw_machine`: RAM, Math Box, divider, NOVRAM,
   switches, vector generator timing). The pace comes from the counted
   cycles; Math Box and vector generator take their time as in MAME. The
   sound board runs along the same way (`src/sound`).
2. **The routines ported by hand** (`src/game`, `swenhanced --native`).
   They compute the same thing frame by frame and are checked with
   `swcompare` against the same MAME captures. They are the way to the
   remaster: Only once a phase exists natively can its picture be drawn from
   the game state instead of from the display list.

The display is the same for both: display list, AVG interpreter,
in-between frames, board.

### In-between frames

`src/render/smooth.c`. Every line of a display list gets an identifier
(`sw_avg_run_tagged`): which routine wrote the word (for the original
program the return address on the stack, for the ported routines a tag from
`core/tags.h`), which instruction of that call it is, which vector ROM
routine draws the line and which line within it. Lines with the same
identifier in two consecutive frames are the same line; where there are
several (all stars, the same letter, the same edge of two ships), the nearest
one counts. In between, the line is moved linearly. The crosshair does not
belong to the frame: The interrupt moves it, and it is shown where the yoke
has it now.

## Roadmap

- [x] **1 Framework**: CMake against the unchanged SDK (presets linux/pc/pi),
      ROM loader (CRC, zip or folder), scheduler with the three clocks,
      AVG interpreter (instruction set from WSVGMC.MAC), mapping to the DVG
      with configuration for the WG6100, calibration image, MAME capture and
      `avgreplay`, symbol table from WSROOT.MAP, tests
- [x] **2 Math Box**: `tools/mbasm.py` assembles SWMP.MAC according to the
      macros of TCODE2.MAC. The result matches the CRCs of all four PROMs
      (136021-110…113), and all 13 entry points lie where the game calls
      them. From this `src/mbox/mbox_programs.c` is generated: one
      C function per program, microinstruction by microinstruction, with the
      original comments. Also a microcode interpreter as a reference,
      tests against it and against the formulas from SWMP.DOC, the divider,
      and at startup a self-test against the PROMs of the ROM set in use
- [x] **3 Interrupt and controls**: WSINT natively (`src/game/irq.c`):
      game clock via `GMTIMR`/`GMSYNC`, debouncing, fire edges
      (`VG.LON`), pot filter (`DOPOTS`), game time timer in BCD,
      VG restart with buffer swap, yoke calibration (`RHLIM`),
      crosshair (`RHPOS`, `GNSITE`), laser, shot and flash tables,
      laser splash (TCSPLS). Coins via Atari's MOOLAH (COIN69) 1:1.
      Memory image with the original addresses (`src/core/machine.c`),
      controls via `vp_input` (`src/platform/controls.c`), live test image
      for yoke, buttons and coins
- [ ] **4 Phases**, each checked against the MAME capture (`tools/swcompare`):
  - [x] Main loop (WAITFRAME, IFRAME, TPHASE), attract: banner with
        scrolling text, flight instructions, score table, high scores; also
        game start up to the choice of the Death Star and entry of initials.
        Messages (TCMES), high scores (TCHSCR), stars (WSSTAR), EEROM for
        reset and attract (TCEROM), crosshair (WSSITE). 3006 of 3006
        frames of the attract capture match bit for bit (RAM, Math Box RAM,
        display list, number of random numbers read)
  - [ ] Space waves (SP1/SP2). Done: building the wave (`NWNSHP`),
        choreography interpreter, objects (`INOBJ`, `ADBOBJ`, `OBJPNT`,
        `OBJDRW`). Open: `VIEW`, player movement, `CPU`, `AIM`, WSLAZR,
        WSGUNS, WSXPLD, WSPANL, WSGAS, WSGLOW (order in STATUS.md)
  - [ ] Surface and towers (WSGRND)
  - [ ] Trench (WSBASE)
  - [ ] Death Star explosion (DX1–DX3), game over, next wave
  - [ ] Statistics and options page (EEACC, EEOPT), self-test (TCTEST)
  Regardless of the state of the port, the whole game runs through the
  6809 core (see above), checked with 2623 of 2623 frames of a played
  capture.
- [x] **5a In-between frames from the display list** (`src/render/smooth.c`),
      line sorting, crosshair centering
- [x] **6a Sound via the sound board**: second 6809 with the sound program,
      RIOT, POKEY, TMS5220 (`src/sound`). Not yet heard on the cabinet.
- [ ] **5 Renderer "Original"** with interpolation (matrix slerp,
      positions modulo 2¹⁶, no interpolation on spawn/despawn)
- [ ] **6 Sound "Original"**: sound driver from SNDAUD/SNDPM/SNDSPK natively,
      POKEY and TMS5220 emulation, speech data from the sound ROMs
- [ ] **7 Remaster**: vp3d scenes with hidden lines, fog, own models,
      sample sound; switchable
- [ ] **8 Package**: `.vpk`, high scores via `vp_hiscore`, marquee

## Pace of the original

The original does not run at a fixed 20.5 Hz. A frame starts only when
the 12th interrupt has arrived **and** the interrupt has handed over the VG
buffer of the previous picture. In attract mode the 6809's main loop needs
25 to 50 ms per frame, and the vector generator up to 40 ms per picture. On
the instructions page this gives 18.6 instead of 12 interrupts per frame,
so 13 instead of 20.5 pictures per second. The speed of everything depends
on this.

Natively, both are finished instantly, so the time is reproduced:

* **Vector generator**: `sw_avg_cycles` (src/avg/avg.c) calculates the draw
  time of a display list from MAME's AVG state machine (states per
  instruction, vector timer with normalization). Checked against the state
  PROM: exact. The native interrupt starts the next picture only when this
  time is up.
* **6809**: `src/game/pace.c`. The routines count their work (characters,
  digits, messages, stars), and each kind has a price. The prices are fitted
  to MAME: `swcapture.lua` measures the compute time of each frame (TIME),
  `swcompare --csv` writes it next to the native counters. Error in attract
  mode: 1 ms (rms) at 25 to 50 ms.

Result (interrupts per frame, original/native): banner 12.2/13.5,
instructions 18.6/18.3, score table 12.1/12.0, high scores 18.0/17.9. Each
newly ported phase needs its own counters and a new fit.

## Open points

For the original program on the 6809 core (the default):

* In-between frames and sound have not yet been seen and heard on the cabinet
  (docs/STATUS.md).
* POKEY without high-pass filter, TMS5220 simplified (interpolation and
  excitation per the data sheet, not clock-exact like MAME).
* Menu entry and package are missing.

For the routines ported by hand (`--native`, `src/game`):

* All game phases from the space wave on are missing. Phases that are
  missing lead back to the banner.
* The pace there comes from a model (`src/game/pace.c`) that hits the banner
  only to within 10% (13.5 instead of 12.2 interrupts per frame). The 6809
  core needs no model.
* Bookkeeping for coin insertion and game over (`EEACCU`, `EEACCT`),
  statistics and options page are missing.
* When comparing the ported routines, the values that the main loop reads
  from the interrupt in the middle of the frame are fed in only for the
  crosshair. The 6809 way gets them completely from the capture (IRDS).
* When comparing, the divider's dividend is missing from the capture;
  `swcompare` sets it to the value from `IMATH`. WSGUNS and WSLAZR
  write other values: then record them as well.

General:

* Picture position: The game draws with `VGSCAL` at quarter size, so the
  picture edge (VGEDG*: ±510 × −612…+600 in vector words) lies at ±127.5 ×
  −153…+150 AVG units. This is how it is set in `swenhanced.ini` now; on the
  cabinet the picture fills the tube. Fine adjustment with `--calibrate` is
  still to do.
* The scaling of the AVG vectors follows MAME (linear scale `^0xFF`/256,
  binary scale 1/2ⁿ). Whether this is exactly right will show in the
  comparison of the captures with MAME's own display.
* Beam budget: According to the SDK, the USB-DVG manages about 3 million
  points per second. How many Hz are possible in the trench is shown by
  `VP_STATS=1`.
