# Star Wars VP

Star Wars (Atari, 1983) für
[VectorPie](https://github.com/Goldfinch2/VectorPie): Raspberry Pi 5,
USB-DVG, Vektormonitor (Wells-Gardner 6100). Das Spiel unverändert, die
Darstellung flüssig.

* **Das Spiel ist das Original, in C**: Das Programm von 1983 ist von Hand
  Zeile für Zeile nach C portiert und Frame für Frame gegen MAME-Aufnahmen
  geprüft (27.313 Frames, alle gleich), ebenso das Programm der
  Soundplatine (Effekte, Musik, Sprache). Im normalen Betrieb läuft kein
  6809 mehr mit. Math Box, Dividierer, NOVRAM, POKEYs und Sprachchip sind
  als Bausteine nachgebildet.
* **Fester Takt**: Das Spiel rechnet in jeder Phase mit einer festen Rate,
  der des Automaten im Schnitt (Graben 13, Banner 20,5 Frames pro Sekunde):
  so schnell wie das Original, ohne dessen Schwanken. Einstellbar
  (`steady_frames`).
* **Testmenü**: Der Selbsttest-Knopf öffnet ein eigenes Menü mit den
  Enhanced Settings (alle abschaltbar), Soundtest, Bildkalibrierung und dem
  Weg zu den Operator-Seiten des Automaten.
* **Oberfläche des Todessterns** (Enhanced, vier Schalter): der Boden als
  Plattenmuster, Sterne am Himmel, Türme in echter Perspektive mit mehr
  Details und aus größerer Entfernung eingeblendet, in der Ferne
  Verfolgungsjagden von TIE Fightern und X-Wing.
* **Occlusion** (Enhanced, abschaltbar mit `occlusion = 0`): Schiffe, Türme,
  der Schriftzug und die eigenen Kanonen sind massiv; was dahinter liegt,
  wird nicht gezeichnet.
* **Darstellung flüssig**: Zwischen zwei Spielframes (das Spiel rechnet 12
  bis 20 pro Sekunde) wird jede Linie bewegt, das Bild kommt mit der Rate
  der Platine. Das Fadenkreuz folgt dem Joch ohne Umweg und ist ab dem
  ersten Bild zentriert.
* **Ton**: das Sound-Programm in C, vier POKEYs, TMS5220-Sprache.
* **Weg zum Remaster**: Aus dem Spielzustand der C-Routinen (`src/game`)
  lässt sich später das Bild neu zeichnen (verdeckte Linien, neue Modelle).

Aktueller Stand, was am Automaten noch ungeprüft ist, und nächste Schritte:
[docs/STATUS.md](docs/STATUS.md). Plan und Hintergründe:
[docs/PLAN.md](docs/PLAN.md). Alle Sounds des Spiels:
[docs/SOUNDS.md](docs/SOUNDS.md).

## ROMs

Die Daten des Spiels (Vektor-ROM, Objekte, Sound-Programm, Sprache) werden
zur Laufzeit aus dem eigenen MAME-ROM-Satz **`starwars1`** gelesen. Ins
Repository oder Paket kommt nichts davon. Gebraucht werden `starwars1.zip`
und `starwars.zip` (der Parent-Satz). Ältere Sätze gehen auch (`starwar1.zip`,
Math-Box-PROMs mit anderem CRC, ohne AVG-PROM), so wie sie auf einem
VectorPie-Automaten liegen. Gesucht wird in:

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

Für den Automaten ohne WSL, von Windows aus (Git Bash, clang und lld im
PATH, als Sysroot eine Kopie der Bibliotheken des Automaten, voreingestellt
die des Tempest-2000-Ports):

```sh
export PATH="/c/Users/ocare/.local/share/retcomm/toolchains/cmake-clang-v1/latest/bin:$PATH"
cmake --preset pi-clang && cmake --build --preset pi-clang
sh tools/deploy.sh        # auf den Automaten kopieren und dort neu starten
```

`tools/deploy.sh` legt alles nach `/persistent/games/starwarsvp`, beendet ein
laufendes Spiel und startet das neue über `run.sh` (hält das Menü an und holt
es danach zurück). Ausgabe und Bildkosten stehen dort in `run.log`. Einen
Menüeintrag gibt es noch nicht.

## Prüfen gegen MAME

```sh
# Aufnahme (MAME 0.289):
SWCAP_OUT=attract.swcap SWCAP_TICKS=3000 mame starwars1 -autoboot_script tools/mame/swcapture.lua -video none -sound none -nothrottle
# mit gespieltem Skript: zwei Münzen, Start, dann Joch und Feuer
SWCAP_PLAY="120 coin 1;160 coin 1;240 fire 3;560 weave 7" SWCAP_OUT=play.swcap SWCAP_TICKS=2600 mame starwars1 -autoboot_script tools/mame/swcapture.lua -video none -sound none -nothrottle
# Bauen, Tests, Vergleich aller Aufnahmen eines Ordners (in WSL):
sh tools/checkall.sh ORDNER /mnt/d/Mame/roms --emu 1    # das Originalprogramm auf dem 6809-Kern
sh tools/checkall.sh ORDNER /mnt/d/Mame/roms            # die von Hand portierten Routinen
```

`swcompare` lädt für jeden Frame den Speicher des Originals, lässt den
nativen Frame laufen und vergleicht RAM, Math-Box-RAM und Displayliste mit
dem nächsten Frame des Originals. Am Ende steht das Tempo pro Phase
(Interrupts pro Frame, Original und nativ). `--show N --max N --from F --to F
--phase HH` grenzen die Ausgabe ein, `--csv DATEI` schreibt Rechenzeit und
Arbeitszähler pro Frame für das Tempomodell (docs/PLAN.md).

## Programme

| Programm | Zweck |
|---|---|
| `starwarsvp` | das Spiel: Spiel- und Sound-Programm in C, kein 6809 |
| `starwarsvp --original` | die ganzen Originalprogramme (Spiel und Ton) auf 6809-Kernen, zum Vergleich |
| `starwarsvp --nosound` | ohne Soundplatine |
| `starwarsvp --controls` | Testbild für die Steuerung: Fadenkreuz am Joch (mit dem originalen Nachziehen und der Joch-Kalibrierung), Feuer färbt es rot, Münzen zählen Credits |
| `swcompare AUFNAHME.swcap` | prüft den nativen Kern Frame für Frame gegen eine MAME-Aufnahme |
| `starwarsvp --calibrate` | Kalibrierbild: Bildschirmrand, Clip-Grenzen, Kreis, alle Farben in 7 Helligkeiten. Joystick ändert, Knopf 1 wechselt den Modus (Größe, Lage, Gamma/Floor, Helligkeit), Knopf 2 speichert nach `save/starwarsvp.ini` |
| `swvideo --wav TON.wav > BILDER.rgb` | Film vom Port ohne Automat: Attract, dann ein gespieltes Spiel; Bilder als rohes RGB24 auf die Standardausgabe, Ton als WAV. `python tools/swvideo/mux.py FILM.mp4 TON.wav 960 720 60 < BILDER.rgb` macht daraus ein MP4 (braucht PyAV) |
| `avgreplay AUFNAHME.swcap` | spielt in MAME aufgenommene Displaylisten im Originaltakt auf dem Monitor ab; `--lerp` zeigt die Interpolation, `--loop` wiederholt |

Allgemeine Optionen: `--dvg [PORT]`, `--window`, `--windowed`, `--vertical`,
`--hz N` (Bildrate), `--config INI`. `VP_STATS=1` gibt die Kosten pro Bild aus
(Vektoren, Leerwege, Weglängen).

## Steuerung

Belegung über die `advmame.rc` des Automaten, wie bei MAMEs Star Wars:

| Automat | VectorPie-Aktion |
|---|---|
| Joch links/rechts, hoch/runter | `p1_stickx`, `p1_sticky` |
| Feuer links (auch Start) | `p1_button1`, `start1` |
| Feuer rechts | `p1_button4`, `start2` |
| Daumen links / rechts | `p1_button2` / `p1_button3` |
| Münze L / R | `coin1` / `coin2` |
| Aux-Münze (Betreiber: nächste Seite im Selbsttest) | `p1_button5` |
| Selbsttest-Knopf: öffnet und schließt das Testmenü (siehe unten) | `p1_button6` |

### Testmenü

Die Tab-Taste (wie in MAME) oder der Selbsttest-Knopf hält das Spiel an
und öffnet das Menü; dieselbe Taste führt auch wieder hinaus und aus der
Kalibrierung und den Operator-Seiten zurück. Joch auf/ab
wählt eine Zeile, links/rechts oder Feuer ändert sie, Feuer öffnet eine
Seite; BACK führt zur Hauptseite, Tab speichert und kehrt ins Spiel zurück.

| Seite | Zeilen |
|---|---|
| Graphics | Smooth Motion, Frame Pace, Occlusion, Yellow Logo; Surface Plating, Detailed Towers, Distant Dogfights; Stars, Trench Clipping (wie `smooth`, `steady_frames`, `occlusion`, `logo_yellow`, `enh_*` unten) |
| Sound | Replaced Sounds; Sound Test (Joch links/rechts wählt eine der 59 Anforderungen aus [docs/SOUNDS.md](docs/SOUNDS.md), Feuer spielt sie) |
| Controls and Screen | Yoke Auto Center; Screen Calibration (das Testbild von `--calibrate`; Feuer wechselt Größe, Lage, Gamma, Helligkeit, das Joch stellt ein) |
| Game Options | Coin Value, Left Mech, Right Mech, Bonus Adder; Starting Shields, Difficulty, Bonus Shields, Music in Attract: die Optionen des Spiels, dieselben wie auf der Optionsseite des Automaten; sie liegen in dessen NOVRAM (`save/nvram.bin`) und gelten sofort |
| Arcade Operator Screens | Statistik, Optionen, Resets und NOVRAM-Test des Automatenprogramms (Aux-Münze blättert); Tab führt zurück ins Spiel |
| Save and Exit | wie Tab |

## Einstellungen

`starwarsvp.ini` (Paketvorgabe neben dem Programm, eigene Werte in
`save/starwarsvp.ini`):

| Schlüssel | Bedeutung |
|---|---|
| `steady_frames` | `1` (Standard): die Frames einer Phase kommen in festem Abstand, dem Durchschnitt des Automaten in dieser Phase; gleiches Tempo wie das Original, ohne Schwanken. `2`: überall 20,5 pro Sekunde (im Spiel rund 50 % schneller als der Automat). `0`: jedes Frame so lang, wie der 6809 des Automaten daran rechnete |
| `smooth` | Bewegung zwischen den Spielframes: `3` (Standard, im Menü ON) aus dem Spielzustand neu gerechnet, ohne Verzögerung und mit richtiger Perspektive; auf den Attract-Seiten wandern die Linien zum voraus gerechneten Bild des nächsten Frames. `0` (OFF): jedes Spielbild wie im Original. (`1` und `2`, Linien zwischen bzw. vor den letzten Frames verschoben, werden noch gelesen, aber nicht mehr angeboten: das eine zeigt die Welt einen Frame zu spät, das andere schießt über.) |
| `yoke_autocenter` | `1` (Standard): Fadenkreuz ab dem ersten Bild zentriert. Das Joch geht exakt durch, und seine Ruhelage gilt als Mitte: Sie wird beim Start gelernt und nachgeführt, wenn das Joch im Attract-Modus still liegt. `0`: wie der Automat von 1983, der den Bereich des Jochs nach jedem Einschalten neu lernt |
| `sound_replace` | Enhanced. `1` (Standard): Sounds, für die in `sounds/` eine WAV-Datei mit ihrem Namen liegt, kommen aus der Datei ([docs/SOUNDS.md](docs/SOUNDS.md)). `0`: alle Sounds aus den Chips wie am Automaten |
| `yoke_center_x`, `yoke_center_y`, `yoke_center_known` | die gelernte Ruhelage des Jochs; schreibt das Programm selbst |
| `logo_yellow` | Enhanced. `1` (Standard): der STAR-WARS-Schriftzug im Attract-Modus ist gelb. `0`: blau wie am Automaten |
| `occlusion` | Enhanced. `1` (Standard): Dinge sind massiv, was hinter einem Schiff, einem Turm, dem Schriftzug oder den eigenen Kanonen liegt, wird nicht gezeichnet. `0`: durchsichtige Drahtmodelle wie am Automaten |
| `enh_surface` | Enhanced, über der Oberfläche. `1` (Standard): der Boden wird als Plattenmuster des Todessterns gezeichnet |
| `enh_stars` | Enhanced. `1` (Standard): ein paar funkelnde Sterne über der Oberfläche und viele über dem Graben, im Streifen zwischen den Oberkanten der Wände |
| `enh_towers` | Enhanced. `1` (Standard): Türme und Bunker in echter Perspektive und detaillierter, so weit sichtbar wie am Automaten, fern dunkel und zur Nähe hin heller; der Laser trifft sie so, wie sie gezeichnet sind. `0`: die Türme des Automaten, die beim Näherkommen aus dem Boden wachsen |
| `enh_dogfights` | Enhanced. `1` (Standard): in der Ferne jagen TIE Fighter einen X-Wing über den Himmel |
| `enh_clip` | Enhanced, im Graben. `1` (Standard): Nahe Hindernisse behalten ihre Form, ihre Linien werden am Bildrand abgeschnitten. `0`: wie am Automaten, der die Punkte naher Objekte an den Rand schiebt |
| `avg_*`, `margin_*`, `flip_*` | Lage und Größe des Bildes, mit `--calibrate` einzustellen |
| `gamma`, `floor`, `brightness`, `color_*`, `defocus_mode` | Helligkeit und Farben |

Optionen des Spiels (Münzen, Schilde, Schwierigkeit, Musik im Attract-Modus)
stellt man im Testmenü unter GAME OPTIONS ein, oder wie am Automaten auf
dessen Optionsseite (Arcade Operator Screens, dann Aux-Münze), wo auch die
Resets und der NOVRAM-Test sind. Sie liegen mit Buchhaltung und den besten
drei Scores in `save/nvram.bin`. Die DIP-Schalter der Platine (`opt0`,
`opt1`) dienen dem Automatenprogramm nur als Vorgabe, wenn die Optionen im
NOVRAM zurückgesetzt werden.

## Aufnahmen aus MAME

```sh
SWCAP_OUT=attract.swcap SWCAP_TICKS=2000 \
  mame starwars1 -autoboot_script tools/mame/swcapture.lua
build-linux/avgreplay attract.swcap --lerp --windowed
```

Pro Spieltick werden VG-RAM, RAM und Math-Box-RAM gespeichert, dazu am
Anfang jedes Frames derselbe Speicher, das NOVRAM, die gelesenen
Zufallszahlen und die Rechenzeit des 6809. Format in `src/capture/capture.h`. Die Aufnahmen dienen zum Kalibrieren und als
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
Programm-ROMs, Vektor-ROM und Sound-ROMs sind nicht enthalten, sie kommen
aus dem eigenen ROM-Satz. Der Math-Box-Mikrocode ist dagegen enthalten, als
generierter C-Code und als Testdatei (`tests/mbox_prom.h`). Er ist aus dem
veröffentlichten Quelltext (SWMP.MAC) erzeugt und entspricht inhaltlich den
PROMs. Das Projekt ist für den privaten Einsatz am eigenen Automaten
gedacht.

Die Zeitrechnung des Vektorgenerators folgt MAMEs `avgdvg.cpp`, die
Tabellen des TMS5220 stammen aus MAMEs `tms5110r.hxx` (beide BSD-3-Clause,
Nachweise im Quelltext).
