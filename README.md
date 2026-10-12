# Star Wars VP

Star Wars (Atari, 1983) als nativer Port mit Remaster-Optionen für
[VectorPie](https://github.com/Goldfinch2/VectorPie): Raspberry Pi 5,
USB-DVG, Vektormonitor (Wells-Gardner 6100).

* Spiellogik originalgetreu: im Originaltakt (20,51 Hz Spiel, 41 Hz VG,
  246 Hz IRQ), Math Box in Festkomma, geprüft gegen MAME-Aufnahmen
* Darstellung flüssig: Interpolation zwischen den Ticks, 50–60 Hz auf dem
  Monitor
* Remaster: verdeckte Linien, neue Modelle, Sound wahlweise original
  (POKEY/TMS5220) oder neu

Stand: **Schritt 2 von 8** (Grundgerüst, Math Box). Plan und Hintergründe:
[docs/PLAN.md](docs/PLAN.md).

## ROMs

Die Daten des Spiels (Vektor-ROM, Objekte, Sound-Programm, Sprache) werden
zur Laufzeit aus dem eigenen MAME-ROM-Satz **`starwars1`** gelesen. Ins
Repository oder Paket kommt nichts davon. Gebraucht werden `starwars1.zip`
und `starwars.zip` (der Parent-Satz). Gesucht wird in:

1. `--roms PFAD`
2. `dir_rom` aus der `advmame.rc` des Automaten
3. `$SWVP_ROMS`, `./roms`, `~/.advance/rom`, `~/mame/roms`, `/home/pi/.advance/rom`

Die Dateien werden per CRC erkannt, Namen und Aufteilung auf die zips spielen
keine Rolle.

## Bauen

```sh
git clone --recursive <repo>            # oder: git submodule update --init
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
cmake --preset pi    && cmake --build --preset pi       # Automat (WSL2 Debian trixie, siehe SDK)
cmake --preset pc    && cmake --build --preset pc       # Windows .exe
```

Das SDK liegt unverändert als Submodul unter `external/VectorPie`.

## Programme

| Programm | Zweck |
|---|---|
| `starwarsvp` | das Spiel; zurzeit ROM-Prüfung, Takte, Kalibrierung |
| `starwarsvp --calibrate` | Kalibrierbild: Bildschirmrand, Clip-Grenzen, Kreis, alle Farben in 7 Helligkeiten. Joystick ändert, Knopf 1 wechselt den Modus (Größe, Lage, Gamma/Floor, Helligkeit), Knopf 2 speichert nach `save/starwarsvp.ini` |
| `avgreplay AUFNAHME.swcap` | spielt in MAME aufgenommene Displaylisten im Originaltakt auf dem Monitor ab; `--lerp` zeigt die Interpolation, `--loop` wiederholt |

Allgemeine Optionen: `--dvg [PORT]`, `--window`, `--windowed`, `--vertical`,
`--hz N` (Bildrate), `--config INI`. `VP_STATS=1` gibt die Kosten pro Bild aus
(Vektoren, Leerwege, Weglängen).

## Aufnahmen aus MAME

```sh
SWCAP_OUT=attract.swcap SWCAP_TICKS=2000 \
  mame starwars1 -autoboot_script tools/mame/swcapture.lua
build-linux/avgreplay attract.swcap --lerp --windowed
```

Pro Spieltick werden VG-RAM, RAM und Math-Box-RAM gespeichert, Format in
`src/capture/capture.h`. Die Aufnahmen dienen zum Kalibrieren und als
Referenz für den nativen Kern.

## Math Box

`tools/mbasm.py` assembliert den originalen Mikrocode (SWMP.MAC aus
historicalsource/star-wars) und prüft ihn gegen die CRCs der PROMs. Daraus
erzeugt es `src/mbox/mbox_programs.c`:

```sh
python3 -I tools/mbasm.py SWMP.MAC --check
python3 -I tools/mbasm.py SWMP.MAC --c src/mbox/mbox_programs.c --prom-header tests/mbox_prom.h
```

## Symbole

`src/core/sw_symbols.h` enthält alle Adressen der originalen Load-Map
(`WSROOT.MAP`) unter ihren Originalnamen und wird mit `tools/mapsyms.py`
erzeugt.

## Rechtliches

Star Wars ist ein Warenzeichen von Lucasfilm, das Spiel stammt von Atari.
Dieses Projekt enthält keinen Code und keine Daten aus den ROMs. Es braucht
einen eigenen ROM-Satz und ist für den privaten Einsatz am eigenen Automaten
gedacht.
