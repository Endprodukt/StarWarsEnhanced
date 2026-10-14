# Star Wars Enhanced

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
3. `$SWE_ROMS`, `./roms`, `~/.advance/rom`, `~/mame/roms`, `/home/pi/.advance/rom`

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

`tools/deploy.sh` legt alles nach `/persistent/games/swenhanced`, beendet ein
laufendes Spiel und startet das neue über `run.sh` (hält das Menü an und holt
es danach zurück). Ausgabe und Bildkosten stehen dort in `run.log`.

Das ist zugleich das Paket, das das Menü des Automaten kennt
(`external/VectorPie/sdk/docs/07-packages.md`): ein Ordner je Spiel unter
`/persistent/games/<id>/` mit `game.ini` (Manifest), dem Programm und
`marquee.png`. Aus `package/game.ini` nimmt das Menü den Titel des Eintrags
(`name = Star Wars Enhanced`), die Gruppe (`manufacturer = Atari`), die
Beschreibung und das Marquee (`marquee = marquee.png`, 1920x360; dasselbe
Bild zeigt `vp_run` während des Spiels auf dem Marquee-Bildschirm). Das Menü
liest die Manifeste beim Start: nach einem Deploy sieht man den neuen Titel,
sobald das Spiel verlassen wird und das Menü zurückkommt.

Als Paketdatei für einen anderen Automaten oder einen USB-Stick (in WSL,
nach dem Pi-Build):

```sh
sh tools/pack.sh          # -> dist/swenhanced-<version>.vpk
```

Eine `.vpk` gehört auf dem Automaten nach `/persistent/games/packages/` (per
scp oder Netzwerkfreigabe) oder auf einen USB-Stick in dessen Ordner
`games/`. Installiert wird sie nicht von selbst, sondern im Menü: Settings ->
GAMES -> INSTALL GAMES; dort steht sie mit NEW, UPDATE, SAME oder OLDER
gegenüber dem, was installiert ist. Die Version steht in `package/game.ini`:
vor einem neuen Paket hochzählen, sonst gilt es als SAME. Das Paket enthält
Programm, `game.ini`, `swenhanced.ini` und `marquee.png`, nicht die ROMs (die
sucht das Spiel in `/home/pi/.advance/rom`) und nicht `save/`.

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
| `swenhanced` | das Spiel: Spiel- und Sound-Programm in C, kein 6809 |
| `swenhanced --original` | die ganzen Originalprogramme (Spiel und Ton) auf 6809-Kernen, zum Vergleich |
| `swenhanced --nosound` | ohne Soundplatine |
| `swenhanced --controls` | Testbild für die Steuerung: Fadenkreuz am Joch (mit dem originalen Nachziehen und der Joch-Kalibrierung), Feuer färbt es rot, Münzen zählen Credits |
| `swcompare AUFNAHME.swcap` | prüft den nativen Kern Frame für Frame gegen eine MAME-Aufnahme |
| `swenhanced --calibrate` | Kalibrierbild: Bildschirmrand, Clip-Grenzen, Kreis, alle Farben in 7 Helligkeiten. Joystick ändert, Knopf 1 wechselt den Modus (Größe, Lage, Gamma/Floor, Helligkeit), Knopf 2 speichert nach `save/swenhanced.ini` |
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

Die Taste F2 (die Service-Taste von MAME) oder der Selbsttest-Knopf hält das Spiel an
und öffnet das Menü; dieselbe Taste führt auch wieder hinaus und aus der
Kalibrierung und den Operator-Seiten zurück. Joch auf/ab
wählt eine Zeile, links/rechts oder Feuer ändert sie, Feuer öffnet eine
Seite; BACK führt zur Hauptseite, F2 speichert und kehrt ins Spiel zurück.

| Seite | Zeilen |
|---|---|
| Graphics | Smooth Motion, Smooth Camera, Frame Pace, Occlusion, Yellow Logo, View, Attract Demo; Surface Plating, Detailed Towers, Distant Dogfights; Stars, Trench Clipping, Falcon Fly-Over (wie `smooth`, `enh_camera`, `steady_frames`, `occlusion`, `logo_yellow`, `enh_*` unten) |
| Sound | Replaced Sounds; Sound Test (Joch links/rechts wählt eine der 59 Anforderungen aus [docs/SOUNDS.md](docs/SOUNDS.md), Feuer spielt sie) |
| Controls and Screen | Yoke Auto Center; Screen Calibration (das Testbild von `--calibrate`; Feuer wechselt Größe, Lage, Gamma, Helligkeit, das Joch stellt ein) |
| Game Options | Coin Value, Left Mech, Right Mech, Bonus Adder; Starting Shields, Difficulty, Bonus Shields, Music in Attract: die Optionen des Spiels, dieselben wie auf der Optionsseite des Automaten; sie liegen in dessen NOVRAM (`save/nvram.bin`) und gelten sofort. Coin Value = FREE PLAY: kein Münzeinwurf, Feuer startet das Spiel, und im Attract steht nur "PRESS FIRE TO START" ohne Credit-Zeile |
| Arcade Operator Screens | Statistik, Optionen, Resets und NOVRAM-Test des Automatenprogramms (Aux-Münze blättert); F2 führt zurück ins Spiel |
| Save and Exit | wie F2 |

## Marquee

`package/marquee.png` (1920x360) zeigt das SDK auf dem HDMI-Schirm des
Automaten, solange das Spiel läuft. Ein eigenes Bild: dieselbe Datei
ersetzen (PNG; Größe nach `advmame.rc`, `vector_aux_marquee_display_mode`).

## Einstellungen

`swenhanced.ini` (Paketvorgabe neben dem Programm, eigene Werte in
`save/swenhanced.ini`):

| Schlüssel | Bedeutung |
|---|---|
| `steady_frames` | `1` (Standard): die Frames einer Phase kommen in festem Abstand, dem Durchschnitt des Automaten in dieser Phase; gleiches Tempo wie das Original, ohne Schwanken. `2`: überall 20,5 pro Sekunde (im Spiel rund 50 % schneller als der Automat). `0`: jedes Frame so lang, wie der 6809 des Automaten daran rechnete |
| `smooth` | Bewegung zwischen den Spielframes: `3` (Standard, im Menü ON) aus dem Spielzustand neu gerechnet, ohne Verzögerung und mit richtiger Perspektive; auf den Attract-Seiten wandern die Linien zum voraus gerechneten Bild des nächsten Frames. `0` (OFF): jedes Spielbild wie im Original. (`1` und `2`, Linien zwischen bzw. vor den letzten Frames verschoben, werden noch gelesen, aber nicht mehr angeboten: das eine zeigt die Welt einen Frame zu spät, das andere schießt über.) |
| `yoke_autocenter` | `1` (Standard): Fadenkreuz ab dem ersten Bild zentriert. Das Joch geht exakt durch, und seine Ruhelage gilt als Mitte: Sie wird beim Start gelernt und nachgeführt, wenn das Joch im Attract-Modus still liegt. `0`: wie der Automat von 1983, der den Bereich des Jochs nach jedem Einschalten neu lernt |
| `sound_replace` | Enhanced. `1` (Standard): Sounds, für die in `sounds/` (in `voice/`, `music/`, `effects/` oder `test/`) eine WAV-Datei mit ihrem Namen liegt, kommen aus der Datei ([docs/SOUNDS.md](docs/SOUNDS.md)). `0`: alle Sounds aus den Chips wie am Automaten |
| `yoke_center_x`, `yoke_center_y`, `yoke_center_known` | die gelernte Ruhelage des Jochs; schreibt das Programm selbst |
| `logo_yellow` | Enhanced. `1` (Standard): der STAR-WARS-Schriftzug im Attract-Modus ist gelb. `0`: blau wie am Automaten |
| `occlusion` | Enhanced. `1` (Standard): Dinge sind massiv, was hinter einem Schiff, einem Turm, dem Schriftzug oder den eigenen Kanonen liegt, wird nicht gezeichnet. `0`: durchsichtige Drahtmodelle wie am Automaten |
| `enh_surface` | Enhanced, über der Oberfläche. `1` (Standard): der Boden um den Spieler als Plattenmuster des Todessterns (voll bis 6000, ausgeblendet bis 11000 voraus) |
| `enh_stars` | Enhanced. `1` (Standard): funkelnde Sterne über der Oberfläche (64) und viele über dem Graben, im Streifen zwischen den Oberkanten der Wände |
| `enh_towers` | Enhanced. `1` (Standard): Türme und Bunker detaillierter, in Größe und Abstand wie am Automaten (dessen Linearmaßstab mit der Entfernung eingeschlossen), aber schon von 6000h statt 3C00h voraus sichtbar, dort klein und dunkel, zur Nähe hin heller; Treffer wie am Automaten. `0`: die Türme des Automaten, die beim Näherkommen aus dem Boden wachsen |
| `enh_dogfights` | Enhanced. `1` (Standard): in der Ferne jagen TIE Fighter einen X-Wing über den Himmel |
| `enh_clip` | Enhanced, im Graben. `1` (Standard): Nahe Hindernisse behalten ihre Form, ihre Linien werden am Bildrand abgeschnitten. `0`: wie am Automaten, der die Punkte naher Objekte an den Rand schiebt |
| `enh_falcon` | Enhanced, im Graben. `1` (Standard): gut drei Sekunden vor dem Abluftschacht kommt der Millennium Falcon von hinten über den Spieler und fliegt den Graben voraus davon (eigenes Modell, massiv). `0`: nichts |
| `enh_demo` | Enhanced. `1` (Standard): Im Attract-Modus kommt vor den Highscores ein Demo-Flug (Weltraum, Oberfläche, Graben des mittleren Todessterns, gut 110 s), jeder Abschnitt aus einer anderen Ansicht, der nächste Demo beginnt mit der nächsten. Er hinterlässt nichts (kein Credit, keine Punkte, keine Zählung im NOVRAM); eine Münze oder ein Knopf beendet ihn sofort und zählt dann wie immer. `0`: der Attract-Modus des Automaten, nur Textseiten |
| Dogfight | Enhanced, im Bau (Etappe 3). Auf "Select a Death Star" unten rechts der vierte Todesstern DOGFIGHT / ENDLESS anvisieren und feuern: frei fliegen in einer Box, endlose Wellen aus TIE Fightern, Interceptors und Vader, 6 Schilde, R2 repariert nach jeder Welle einen. Joch giert und nickt, rechter Trigger Laser, linker Trigger zwei zielsuchende Protonentorpedos (laden 7 s), drei Flügelmänner fliegen mit, Daumentasten schneller/langsamer, beide plus Joch zur Seite rollen, Start schaltet die Ansicht; am Rand dreht das Schiff selbst um. Ein Joch, dessen Name "yoke" enthält, wird direkt gelesen. F2 führt zurück. Anleitung und Highscores folgen |
| `enh_view` | Enhanced: von wo man im Flug sieht. `0` (Standard): wie am Automaten, Nase und Kanonen des X-Wings. `1`: aus dem Cockpit: Kanzelrahmen mit zwei Streben zu den oberen Ecken und ein flaches Armaturenbrett unten, dahinter Liegendes wird nicht gezeichnet; die Laser kommen hinter Konsole und Streben hervor. `2`: von außen, hinter dem X-Wing: ein eigenes Modell (Rumpf, Nase, Kanzel, R2, vier Flügel in X-Stellung mit roten Streifen, vier Triebwerke mit rotem Glühen, Kanonen an den Flügelspitzen), das sich mit dem Joch in die Kurve legt und leicht mitdreht; die Laser starten an seinen Kanonenspitzen und laufen dorthin, wohin das Spiel sie schickt. Im Flug schaltet die Start-Taste weiter (Arcade, Cockpit, Außen) und feuert dort nicht; im Attract startet sie wie bisher das Spiel. Die Wahl wird gespeichert |
| `enh_camera` | Enhanced. `1` (Standard): die Schwenks der Kamera (das Schiff dreht sich im Weltraum von selbst zum verfolgten Gegner oder zum Todesstern) nehmen Fahrt auf und bremsen ab, statt wie am Automaten vom ersten Bild an mit voller Rate zu drehen. Im Spiel selbst, nicht nur im Bild: die Drehrate ändert sich pro Bild um höchstens 12 von 127 |
| `avg_*`, `margin_*`, `flip_*` | Lage und Größe des Bildes, mit `--calibrate` einzustellen |
| `gamma`, `floor`, `brightness`, `color_*`, `defocus_mode` | Helligkeit und Farben. `brightness` bis 2,0 (Standard 1,7: eine normale Linie des Spiels, Intensität 80h, kommt bei 95 % an) |

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
