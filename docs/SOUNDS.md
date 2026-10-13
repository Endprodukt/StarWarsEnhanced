# Sounds

Every sound the game can request from the sound board: 59 entries, one byte
each (`SNDPBX.MAC` of the original source). This list is the base for
replacing sounds with new ones later.

* **No.** is the byte the game sends (`sw_doxmt` in `src/game/gmath.c`); the
  sound program dispatches it in `request()` in `src/sound/sndprog.c`. That
  is where a replacement would hook in.
* **Kind**: speech comes from the speech chip (TMS5220), music and effects
  from the four POKEYs. Everything is generated from the data in the ROMs
  today; there are no recordings.
* **When** is the place in the arcade program where the sound is requested.

## Speech (24)

| No. | Name | Text | When |
|---|---|---|---|
| 03 | SPKALW | "Always" | Attract, after "The Force will be with you" |
| 04 | SPKELE | Pass-by sound (TIE fighter, "elephant") | Space, a TIE flies past |
| 05 | SPKFOA | "The Force will be with you, always" | Game over |
| 06 | SPKFOR | "The Force will be with you" | Attract |
| 07 | SPKGRE | "Great shot kid, that was one in a million" | Trench, exhaust port hit |
| 08 | SPKHAV | Breath / "I have you now" / breath | Space, Darth Vader |
| 09 | SPKHIT | "I'm hit, but not bad. R2, see what you can do with it" | Own ship hit |
| 0A | SPKI | Restart the speech chip (no sound) | Not used by the game |
| 0B | SPKIMO | Breath / "I'm on the leader" / breath | Space, Darth Vader |
| 0C | SPKLET | "Let go, Luke" | Trench |
| 0D | SPKLOS | "I've lost R2" | Shields: R2 hit |
| 0E | SPKR2N | R2 swears | Trench, exhaust port missed |
| 0F | SPKR2T | "R2, try and increase the power" | Shields almost gone |
| 10 | SPKRED | "Red Five standing by" | Game start |
| 11 | SPKREM | "Remember" | Game over, before SPKFOA |
| 12 | SPKSHK | "I can't shake him" | Space, a TIE fires |
| 13 | SPKSIZ | "Look at the size of that thing" | Approach to the Death Star |
| 14 | SPKSKP | Drop any waiting speech (no sound) | On every death |
| 15 | SPKSTA | Breath / "Stay in attack formation" / breath | Space, Darth Vader |
| 16 | SPKSTR | Breath / "The Force is strong with this one" / breath | Trench |
| 17 | SPKTHI | "This is Red Five, I'm going in" | End of space, approach |
| 18 | SPKTRU | "Luke, trust me" | Trench |
| 19 | SPKUSE | "Use the Force, Luke" | Entering the trench |
| 1A | SPKYAU | "Yahoo! You're all clear, kid" | Trench |

## Music (11)

| No. | Name | Piece | When |
|---|---|---|---|
| 1B | PMBEN | Ben's theme | Game over without a high score |
| 1C | PMCNT | Cantina | High score entry |
| 1D | PMDAR | Darth Vader's theme | Space |
| 1E | PMDES | Descent to the Death Star | End of space |
| 1F | PMEND | End music | After the Death Star explodes |
| 20 | PM4TH | Battle music in fourths | Ground with towers |
| 21 | PMREB | Rebel theme | End of ground |
| 22 | PMRRP | Rebel theme with fast repeats | Trench |
| 23 | PMSF2 | Special effect 2: shot into the exhaust port | Proton torpedo fired |
| 24 | PMTH5 | Main theme | Start of game (space) |
| 25 | PMTHB | Theme, part B | Space, after the main theme |

In attract mode the program plays the pieces through the table `TMUSIC`
(BEN, CNT, END, RRP, THB, TH5, REB, 4TH, DAR).

## Effects (20)

| No. | Name | Sound | When |
|---|---|---|---|
| 26 | AUDCR | Crash | Into a tower, bunker, force field or trench wall |
| 27 | AUDDF | Death Star explosion | Death Star explodes (three phases) |
| 28 | AUDDR | R2 dead, "swan song" | Shields: R2 hit |
| 2A | AUDPB | Pass-by, approach | Space, a TIE flies past |
| 2B | AUDPBZ | Stop the pass-by sound (no sound) | TIE shot down |
| 2C | AUDPF | Tower "pffft" | Ground, at a tower or bunker |
| 2D | AUDPH | Exhaust port (assists PMSF2) | Proton torpedo; end of initials entry |
| 2E | AUDPS | Pass-by, short (Doppler) | Space, the TIE has passed |
| 2F | AUDRC | R2 "cooked" | Shields at 1 |
| 30 | AUDRD | R2 done | Shields almost gone |
| 31 | AUDRS | R2, short yes | Own ship hit |
| 32 | AUDRY | R2, yes | End of space, approach |
| 33 | AUDSH | Shield | Hit by a gun |
| 34 | AUDSS | Fireball shot down | Enemy shot hit; initials entry |
| 35 | AUDSX | Small explosion | TIE, bunker or tower shot down |
| 36 | AUDTC | TIE fighter cannon | A TIE fires |
| 37 | AUDTCZ | TIE fighter cannon, end | Fireball shot down |
| 38 | AUDTH | Thrust | Approach to the Death Star |
| 39 | AUDTL | Tower lasers | A tower or trench gun fires |
| 3A | AUDXL | X-wing lasers | Own shot; initials entry |

## Sound board test only (4)

| No. | Name | Sound |
|---|---|---|
| 01 | AUDTST | Test tones of the effect POKEYs |
| 02 | PMTST | Test tones of the music POKEYs |
| 29 | AUDNAY | Test failed |
| 3B | AUDYEA | Test passed |

00 is not a sound: it resets the sound board.

## Replacing a sound

Put a WAV file named after the request into the folder `sounds` (next to
the program, or `save/sounds` on the cabinet, which is looked at first):
`sounds/AUDXL.wav` replaces the X-wing's lasers, `sounds/PMTH5.wav` the main
theme, `sounds/SPKRED.wav` "Red Five standing by". A request without a file
sounds as before, so sounds can be replaced one at a time.

* Format: WAV, PCM, 8 or 16 bit, mono or stereo, any sample rate (converted
  to mono at 44.1 kHz when loaded). Full scale in the file is full scale at
  the output; the board's own sounds stay well below that, so keep
  replacements at a moderate level.
* Effects: several play at once; one requested again starts again.
  `AUDPBZ` stops `AUDPB`, `AUDTCZ` stops `AUDTC`.
* Music: one piece at a time. A new piece, replaced or not, ends the one
  playing; a replaced piece also ends the board's own music.
* Speech: sentences play one after the other; `SPKSKP` drops those waiting.
* Request 00 (the board's reset) silences everything.
* The setting `sound_replace = 0` (test menu: REPLACED SOUNDS) switches all
  of them off. The test menu's SOUND TEST plays any request, replaced or
  not. `swvideo --sounds DIR` films with a folder of replacements.

The code is `src/sound/samples.c`; the board's program asks it in
`request()` (`src/sound/sndprog.c`).

To start from the originals: `swsound` (`tools/swsound`) runs the sound
board on its own and writes any sound as a WAV file, a piece of music
until the board's music driver is done, speech until the speech chip is,
an effect until it has been quiet for half a second:

    swsound --roms starwars.zip --out sounds PMTH5 AUDXL
    swsound --roms starwars.zip --out sounds        (all eleven pieces of music)

The files are made from the ROMs and are not in the repository
(`sounds/*.wav` is ignored).

## To keep in mind for replacements

* Music and speech are long and run across phases; a new piece of music
  replaces the one playing, speech is queued (SPKSKP empties the queue).
* Several effects sound at the same time (their own POKEY channels); some
  have their own end request (AUDPB/AUDPBZ, AUDTC/AUDTCZ).
* While music or speech is playing the program switches a bit "FM" on the
  RIOT; what it changes in the sound has not been looked into yet.
* The column "When" is derived from the places in the source, not each one
  listened to in the game.
