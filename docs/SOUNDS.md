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
|    | (AUDSXG) | not a request: the recordings AUDSX takes over the ground and in the trench (effects/AUDSXG.wav ...), plain blasts without a TIE fighter's engine; `sw_samples_ground` | tower or bunker shot down |
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
the program, or `save/sounds` on the cabinet, which is looked at first),
in the folder of its kind: `sounds/effects/AUDXL.wav` replaces the X-wing's
lasers, `sounds/music/PMTH5.wav` the main theme, `sounds/voice/SPKRED.wav`
"Red Five standing by". The kinds are `voice` (SPK...), `music` (PM...),
`effects` (AUD...) and `test` (AUDTST, PMTST, AUDNAY, AUDYEA); a file
directly in `sounds` is taken too, and goes first. A request without a file
sounds as before, so sounds can be replaced one at a time.

* Format: WAV, PCM 8 or 16 bit, or IMA ADPCM (4 bit), mono or stereo, any
  sample rate (converted to 44.1 kHz when loaded). The program's sound is
  stereo: a stereo recording keeps its two sides (the music), a mono one
  plays in the middle, as the board's own sound does (it is mono). Full scale in the
  file is full scale at the output; the board's own sounds stay well below
  that, so keep replacements at a moderate level.
* Several recordings for one request: `NAME.wav`, `NAME_2.wav`, up to
  `NAME_12.wav`. Each request plays one of them, at random, never the same
  one twice running (multisamples: a pass-by or a shot does not sound the
  same every time).
* Effects: several play at once, the same one too (a new shot does not cut
  the last off). `AUDPBZ` stops `AUDPB`, `AUDTCZ` stops `AUDTC`.
* Ducking, so that it is not all too much and speech is always understood:
  while a sentence is spoken (a recording or the board's speech chip)
  everything else is 3 dB lower, the board's own music and effects too;
  while a big explosion or a crash sounds (AUDSX, AUDDF, AUDCR) the other
  effects, the engine and the music are 1.5 dB lower. In within 20 ms, out
  in 300 ms. Only with the recordings on (`sound_replace`).
* Music: one piece at a time. A new piece, replaced or not, ends the one
  playing; a replaced piece also ends the board's own music.
* Speech: sentences play one after the other; `SPKSKP` drops those waiting.
* Request 00 (the board's reset) silences everything.
* The setting `sound_replace = 0` (test menu: REPLACED SOUNDS) switches all
  of them off. The test menu's SOUND TEST plays any request, replaced or
  not. `swvideo --sounds DIR` films with a folder of replacements.

The code is `src/sound/samples.c`; the board's program asks it in
`request()` (`src/sound/sndprog.c`).

## The recordings that come with the game

`package/sounds` (shipped with the program and in the package) holds 105
recordings for 41 requests and the X-wing's engine: the film lines of
`sounds/voice`, and sounds of the SW Ultimate SFX Collection (in the
project's folder, not in the repository). `tools/swsamples/samples.txt` says
which recording takes the place of which sound, how loud it is and how long
at most; `tools/swsamples/build.py` makes the folder from it:

    python tools/swsamples/build.py

Each recording is brought to mono (the board's sound is mono), cut (silence
before and after, its longest, faded out), to 16 kHz (nothing above 8 kHz)
and to its loudness, and written as IMA ADPCM: 8 KB a second, 1.4 MB in
all. Loudness: the RMS of the loud part, about as loud as the board's own
sound for the same thing (its speech -29 dB, its effects -37 dB, its music
-32 dB); speech -27 dB, lasers -32 dB, explosions -27 dB, R2 -31 dB, the
Death Star -28.5 dB.

Music: the owner's own cuts from the films' soundtrack (private: in
`sounds/music`, made into `package/sounds/music`, both kept out of git;
the build tool leaves them out where they are not there), in stereo, -30
dB (a little under speech): PMTH5, PMTHB, PMDAR, PM4TH, PMCNT. The other
pieces are the board's own. Darth Vader's four
lines (SPKHAV, SPKIMO, SPKSTA, SPKSTR) have his breath before and after, as
the arcade game's have (the films' lines have none; the breath is the
collection's, 9 dB below his voice). The towers' guns (AUDTL) are turbolasers
and heavy cannons: short, hard shots.

The Millennium Falcon's fly-over in the trench (enhanced, FALCON FLY-OVER)
has a fly-by of its own (`effects/FALCON.wav` ... `FALCON_5.wav`, cut so
that each is loudest 0.85 s into it), and "Yahoo, you're all clear, kid"
(SPKYAU) comes just as it passes over the player. Its run is planned 1.3 s
before it passes, from how fast the exhaust port really comes nearer; the
picture and the two sounds (`sw_samples_cue`) keep to that plan. While the
Falcon flies, the game's own request for SPKYAU (it would come at its own
time in the trench) is taken and not played.

The engine (enhanced, the arcade game has none; setting ENGINE SOUND,
`enh_engine`): `effects/XWENG.wav` from inside (the arcade's view and the
cockpit), `effects/XWEXT.wav` from outside, loops of 6 s cut from the
collection's own X-wing engines (XW ENG 19B, XW EXT 9G) and joined end to
start. Heard in flight as the view is, a change of view crossfades; in the
dogfight it runs up (louder, faster) as the ship speeds up. It is quieter
inside than outside, and under everything else.

To start from the originals: `swsound` (`tools/swsound`) runs the sound
board on its own and writes any sound as a WAV file, a piece of music
until the board's music driver is done, speech until the speech chip is,
an effect until it has been quiet for half a second:

    swsound --roms starwars.zip --out sounds PMTH5 AUDXL
    swsound --roms starwars.zip --out sounds        (every sound)

Each goes into the folder of its kind (`sounds/music/PMTH5.wav`,
`sounds/effects/AUDXL.wav`). `SPKI`, `SPKSKP`, `AUDPBZ` and `AUDTCZ` make
no sound and get no file. The files are made from the ROMs and are not in
the repository (the WAV files under `sounds` are ignored).

## To keep in mind for replacements

* Music and speech are long and run across phases; a new piece of music
  replaces the one playing, speech is queued (SPKSKP empties the queue).
* Several effects sound at the same time (their own POKEY channels); some
  have their own end request (AUDPB/AUDPBZ, AUDTC/AUDTCZ).
* While music or speech is playing the program switches a bit "FM" on the
  RIOT; what it changes in the sound has not been looked into yet.
* The column "When" is derived from the places in the source, not each one
  listened to in the game.
