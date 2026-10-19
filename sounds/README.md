# Replacement sounds

The recordings the game comes with are in `package/sounds`, made by
`tools/swsamples/build.py` from `tools/swsamples/samples.txt`: from the film
lines here in `voice/` and from the SW Ultimate SFX Collection (docs/SOUNDS.md,
"The recordings that come with the game"). This folder holds the sources:
`voice/` the film lines (`voice/original/` the board's own speech), `music/`
the owner's own cuts from the films' soundtrack (private, in the private
repository; `music/original/` the board's own pieces), and the board's own
sounds `swsound` writes, to compare with.

A WAV file named after a sound of the game is played in its place:
`effects/AUDXL.wav` for the X-wing's lasers, `music/PMTH5.wav` for the main
theme, `voice/SPKRED.wav` for "Red Five standing by". The 59 names, the
format of the files and how they play are in
[docs/SOUNDS.md](../docs/SOUNDS.md).

A file is looked for in this folder itself and in the folder of its kind:

| Folder | Sounds |
|---|---|
| `voice/` | speech, `SPK...` (22 files) |
| `music/` | music, `PM...` (11 files) |
| `effects/` | effects, `AUD...` (18 files) |
| `test/` | the sound board's own test: `AUDTST`, `PMTST`, `AUDNAY`, `AUDYEA` |

`swsound --roms starwars.zip --out sounds` fills the folders with the
board's own sounds, as files to start from. They are made from the ROMs and
are in the (private) repository. Four requests make no sound and get no file:
`SPKI`, `SPKSKP`, `AUDPBZ`, `AUDTCZ`.
