# Replacement sounds

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
are not in the repository. Four requests make no sound and get no file:
`SPKI`, `SPKSKP`, `AUDPBZ`, `AUDTCZ`.
