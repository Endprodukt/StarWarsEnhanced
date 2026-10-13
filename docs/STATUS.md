# Stand und Weitermachen

Stand: 2026-10-13. Architektur und Hintergründe: [PLAN.md](PLAN.md).

## Ziel (Wunsch des Besitzers, 2026-10-13)

Hundertprozentig C, auch der Ton; keine Emulation. Eine feste Bildrate ohne
die Einbrüche des Automaten ("steady frames per second"), und echte, nicht
vorgetäuschte Flüssigkeit. Der 6809-Kern dient nur noch dem Vergleich
(`--original`, die Tests).

## Was läuft

Das ganze Spiel, unverändert, als C-Programm (`src/game`, Zeile für Zeile
nach der Quelle von 1983): Interrupt, Münzen, Attract, Spielstart,
Weltraumwelle, Anflug, Boden mit Türmen und Bunkern, Graben, Protonentorpedo,
Explosion des Todessterns, Wellenwechsel, alle Tode, Spielende mit
Buchhaltung und Highscore-Eingabe, dazu die Betreiberseiten (Statistik,
Optionen mit Ändern, Zurücksetzen und NOVRAM-Test). In `src/game` gibt es keinen Aufruf ins
Originalprogramm mehr (`grep -rn "ORIG(" src/game` ist leer).

Ebenso das Programm der Soundplatine (`src/sound/sndprog.c`: Hauptschleife,
Effekte SNDAUD, Musik SNDPM, Sprache SNDSPK).

`starwarsvp` kennt zwei Arten:

* **Standard**: alles in C, kein 6809.
* `--original`: Spiel- und Sound-Programm ganz auf 6809-Kernen (zum Vergleich).

Nicht portiert und nicht vorgesehen: der Selbsttest der Platine (TCTEST.MAC:
RAM-, ROM- und Math-Box-Prüfung beim Einschalten mit Testschalter). Diese
Hardware gibt es hier nicht; "AUX" auf der Optionsseite setzt das Spiel nur
zurück.

Als Bausteine nachgebildet (keine Programme, sondern Hardware): Math Box,
Dividierer, NOVRAM, die vier POKEYs, der Sprachchip TMS5220, der RIOT der
Soundplatine, die Zeit des Vektorgenerators.

* **Fester Takt im Tempo des Automaten** (`steady_frames = 1`, Standard,
  Entscheidung des Besitzers vom 2026-10-13): Die Frames einer Phase kommen
  in festem Abstand, und zwar dem, den der Automat dort im Schnitt brauchte
  (`sw_pace_steady_interval` in `src/game/pace.c`, aus acht MAME-Aufnahmen):
  Banner, Punkteseite, Auswahl, Anflug 12 Interrupts (20,5 Frames je
  Sekunde), Anleitung 18,4, Highscores 17,7, Weltraum 16,7, Boden 15,1,
  Graben 18,7 (13,2 je Sekunde), Tod im Weltraum 24. Das Spiel ist damit so
  schnell wie am Automaten, aber ohne dass sein Tempo mit dem Bildinhalt
  schwankt. Gemessen im freien Lauf: genau diese Werte in jeder Phase.
  `steady_frames = 2`: 12 Interrupts überall (so ist das Programm
  geschrieben, "20 PER SECOND"; im Spiel rund 45 bis 55 % schneller als der
  Automat). `steady_frames = 0`: jedes Frame so lang, wie der 6809 daran
  rechnete (siehe Tempo). Der Tod über dem Boden steht in keiner Aufnahme;
  sein Takt ist der des Todes im Graben.
* **Ton in C, geprüft gegen das Original** (`tests/test_sound.c`): Zwei
  Soundplatinen bekommen dieselben 59 Anforderungen (alle, die es gibt);
  die eine führt das Original-Programm auf dem 6809-Kern aus, die andere
  das C-Programm. Alle Schreibzugriffe sind gleich und im selben Durchlauf
  der Hauptschleife: 3.656 auf die Effekt-Kanäle, 55.296 auf die
  Musik-Kanäle, 18 auf die Steuerregister; die 6.685 Sprachbytes sind
  gleich und höchstens einen Durchlauf (4 ms) versetzt.
* **Geprüft gegen MAME**, Frame für Frame (RAM, Math-Box-RAM,
  Displayliste), acht Aufnahmen mit 27.313 Frames: Attract, zwei der
  Betreiberseiten (Statistik mit Spielgeschichte, Optionen mit 30
  Änderungen und Zurücksetzen) und fünf gespielte (Tod im Graben, Tod im Weltraum, verfehlter Schacht mit
  Wiederholung, zerstörter Todesstern, vier Wellen am Stück mit Darth
  Vader, Bunkern und Türmen). Alle Frames stimmen, mit den C-Routinen wie
  mit dem ganzen Original auf dem Kern.
* **Tempo ohne feste Bildrate** (`steady_frames = 0`): Im Original bestimmt die Rechenzeit des 6809 die Bildrate (im
  Graben etwa 13 Bilder pro Sekunde). Die C-Routinen sind sofort fertig und
  warten deshalb so lange, wie der 6809 gebraucht hätte (`src/game/pace.c`):
  aus gezählter Arbeit (gelesene Bytes, Math-Box-Läufe, Zeichen) und einem
  Grundwert je Phase, angepasst an MAMEs gemessene Zeit je Frame. Abweichung
  in den Spielphasen 0,9 ms (rms) bei 25 bis 80 ms je Frame, im Mittel je
  Phase null. Im freien Lauf: Graben 18,6 Interrupts je Frame gegen 18,2 im
  Original, Weltraum 16,1 gegen 17,4 (anderer Spielverlauf), Attract
  12,2/12,4 (Banner), 18,6/18,6 (Anleitung), 18,0/17,9 (Highscores).
* **Flüssige Darstellung aus dem Spielzustand** (`src/game/subframe.c`,
  Einstellung `smooth = 3`, Standard): Das Spiel bewegt alles einmal pro
  Frame (12 bis 20 Mal pro Sekunde). Für jedes Bild der Platine dazwischen
  werden Spieler, Schiffe, Schüsse, Trümmer und Sterne auf ihrem Weg vom
  Zustand vor dem Frame zum Zustand danach eingesetzt, und die Zeichenroutinen
  des Spiels rechnen das Bild dieses Augenblicks neu, auf einer Kopie der
  Maschine: mit richtiger Perspektive, ohne Verzögerung, und was ins Bild
  kommt, kommt ins Bild. Das Spiel selbst merkt davon nichts. Geprüft
  (`tests/test_subframe.c`, 2640 Spielframes eines gespielten Laufs): Zum
  Zeitpunkt des Frames ist das berechnete Bild Wort für Wort dessen
  Displayliste, dazwischen liegt es dazwischen (31 % des Wegs nach einem
  Drittel der Zeit), und am Übergang zum nächsten Frame bleibt kein Sprung.
  Gilt für Weltraum, Anflug, Boden, Graben und die Tode.
* **Attract-Seiten, Explosion, Wellenwechsel**: Dort zählen und zeichnen die
  Routinen in einem Zug (Banner-Zoom und Laufschrift hängen an ganzzahligen
  Zählern), ein Zwischenzustand lässt sich nicht einsetzen. Stattdessen
  wird das Bild des nächsten Frames einen Frame im Voraus gerechnet (das
  Spiel auf einer Kopie einen Frame weiter laufen lassen), und die Linien
  wandern vom Bild dieses Frames zu jenem (`src/render/picture.c`,
  `sw_subframe_next`). Auch das ohne Verzögerung. Geprüft: 96 % der Wörter
  der voraus gerechneten Displayliste sind die, die das Spiel dann zeichnet;
  der Rest sind Joch und Feuer, die erst im nächsten Frame gelesen werden.
* **Film vom Port** (`tools/swvideo`): lässt das Spiel ohne Automat laufen
  (Attract, zwei Münzen, Start, Joch pendelt und feuert), zeichnet jedes
  Bild wie ein Vektormonitor und schreibt den Ton mit; `mux.py` macht daraus
  ein MP4 (braucht PyAV). Ergebnis: `video/starwarsvp.mp4` (nicht im Repo;
  es liegt immer nur das neueste Video dort, alte werden gelöscht). Behoben
  am 2026-10-13: Das Werkzeug ließ das Soundboard nicht zwischen zwei
  Anforderungen eines Frames laufen, das Spiel setzte es deshalb 129-mal
  zurück (Ton und Musik abgehackt). Das Spiel selbst (`src/main.c`) war
  nicht betroffen.
* **Liste aller Sounds** für den späteren Ersatz: `docs/SOUNDS.md`.
* **Strich-Sortierung**: 3-mal weniger Leerweg auf der Highscore-Seite.
* **Eigenes Testmenü** (`src/platform/menu.c`, 2026-10-13): Der
  Selbsttest-Knopf (`p1_button6`) öffnet es, das Spiel steht solange. Zeilen:
  Enhanced Settings (Occlusion, Smooth Motion, Frame Pace, Stroke Sorting),
  Sound Test (alle 59 Anforderungen aus `docs/SOUNDS.md` mit Nummer und
  Name), Screen Calibration, Arcade Operator Screens (die Statistik- und
  Optionsseiten des Automatenprogramms; der Knopf führt von dort zurück),
  Save and Exit. Joch wählt und ändert, Feuer löst aus, der Selbsttest-Knopf
  speichert in `save/starwarsvp.ini` und schließt. Kompiliert für PC und Pi,
  aber **noch nie gesehen oder bedient** (kein Automat, kein Bildschirmfoto).
* **Occlusion** (`occlusion = 1`, Standard; erste "Enhanced"-Funktion,
  abschaltbar, Wunsch des Besitzers vom 2026-10-13): Dinge sind nicht mehr
  durchsichtig. `src/render/solid.c` schneidet aus dem fertigen Bild die
  Linien weg, die hinter etwas Näherem liegen; am Spiel ändert sich nichts
  (alle Aufnahmen stimmen weiter). Was verdeckt:
  * die Flächen der 3-D-Objekte (TIE Fighter, Darth Vaders Schiff, Türme,
    Bunker, Wandgeschütze, Wandplatten, Kraftfelder, Schacht). Das ROM kennt
    nur Punkte und Linien; die Flächen werden beim Start als Ringe von
    Linien gefunden, die (fast) in einer Ebene liegen.
  * was als Umriss gezeichnet ist: der STAR-WARS-Schriftzug (seine Vorderseite
    verdeckt auch die eigenen hinteren Kanten), Nase und Kanonen des X-Wing,
    der Todesstern. Die Form wird auf einem Raster bestimmt, weil die
    Umrisse des X-Wing über den Bildrand laufen und keine Ringe sind.
  Die Entfernung notieren die Zeichenroutinen neben jedem Wort der
  Displayliste (`sw_machine.vgnote`: 1/X des Punktes, Objekt, Punktnummer).
  Nie verdeckt: Texte, Punktestand, Fadenkreuz, Laserstrahlen.
  Geprüft: `tests/test_solid.c` und Standbilder mit/ohne (`swvideo --wire`).
* **Fadenkreuz zentriert** ab dem ersten Bild (`yoke_autocenter`).
* **NOVRAM** (Optionen, Buchhaltung, beste drei Scores) in `save/nvram.bin`.

## Noch nicht am Automaten gesehen oder gehört

Der Automat war bei dieser Arbeit aus. Aufgespielt ist der Stand vom
Attract-Modus davor. Ungeprüft am Gerät, also nur gerechnet und gemessen:

* das ganze Spiel in C am Gerät (Münzen, Start, Joch, Feuer), der feste
  Takt (ob sich das Tempo wie am Automaten anfühlt),
* die neu gerechneten Zwischenbilder (`smooth = 3`): Aussehen, Rechenzeit
  auf dem Pi, Fadenkreuz. Steht in der Einstellungsdatei am Automaten noch
  `smooth = 1`, bleibt es dabei, bis man es dort ändert.
* der Ton überhaupt: Die Platine erzeugt plausible Pegel zur richtigen Zeit
  (Sprache beim Münzeinwurf und Start, Musik ab der Welle), aber niemand
  hat ihn gehört. POKEY ohne Hochpassfilter, TMS5220 vereinfacht.
* `--calibrate`, die Bildrate der Platine im Spiel.

## Bekannte Grenzen

* **Testmenü**: ungesehen (siehe oben). `yoke_autocenter` steht nicht im
  Menü (wirkt erst nach Neustart). Der Controls-Test (`--controls`) ist
  noch kein Menüpunkt.

* **Occlusion**: noch nicht am Automaten gesehen, und die Rechenzeit auf dem
  Pi ist nicht gemessen (jede Linie gegen jede Fläche; am PC unauffällig).
  Die Wände und der Boden des Grabens und der Boden der Oberfläche
  verdecken nichts (sie haben keine Flächen, nur Kanten). Explosionen und
  Feuerbälle verdecken nichts. Texte scheinen durch den Schriftzug. Der
  Todesstern in der Auswahl und im Anflug verdeckt nur Sterne. Im Modus
  `--original` (Programm auf dem 6809) gibt es keine Occlusion.

* `smooth = 3`: Unstetiges (Laser, Blinken, Zähler, neu erscheinende
  Dinge) wechselt wie im Original im Takt der Spielframes; Drehungen werden
  geradlinig überblendet (bei 10 Grad pro Frame 0,4 % zu klein in der
  Mitte). Das Fadenkreuz folgt dem Joch im Takt der Neustarts des
  Vektorgenerators (wie im Original), nicht mit jedem Bild.
* `smooth = 1` zeigt die Welt einen Spielframe (50 bis 90 ms) später; das
  Fadenkreuz nicht. `smooth = 2` hat keine Verzögerung, schießt aber bei
  Richtungswechseln über.
* Linien ohne Partner im vorigen Frame springen dort (wie im Original).
* Das Tempo-Modell ist an fünf Aufnahmen bis Welle 4 angepasst. Spätere
  Wellen (mehr Türme, Zufallsgräben) liegen etwas außerhalb davon; der
  Fehler dort ist nicht gemessen.
* Von keiner Aufnahme durchlaufen und deshalb nur gelesen, nicht geprüft:
  der Tod über dem Boden (G0D), die Meldung "AVOID CATWALKS", die
  Histogramm-Halbierung in EEACCT und CHKEE bei kaputter Prüfsumme.
* Kein Menüeintrag am Automaten, kein `.vpk`.
* Statistik- und Optionsseite brauchen den Testschalter: `p1_button6`
  schaltet ihn um, `p1_button5` ist die Aux-Münze. Beide sind am Automaten
  nicht belegt, solange `advmame.rc` sie nicht zuweist.
* Der NOVRAM-Test der Optionsseite ist portiert, aber in keiner Aufnahme
  sicher ausgelöst worden. Er hält das Bild im Original 66 Frames an; hier
  wird die Arbeit sofort getan und danach 66 Frames gewartet.

## Als Nächstes

1. Am Automaten: `sh tools/deploy.sh`, dann Bild, Ton und Steuerung prüfen;
   `smooth` 1 gegen 2 vergleichen.
2. Das Fadenkreuz mit jedem Bild nachführen (jetzt: im Takt der Neustarts
   des Vektorgenerators).
3. Den Ton nach Gehör prüfen: POKEY-Hochpass, TMS5220 genauer (Vergleich
   mit einer MAME-Aufnahme, `-wavwrite`). Das Programm ist portiert; was
   noch von MAME abweichen kann, sind die Bausteine.
4. Menüeintrag und `.vpk`.
5. Mehr Aufnahmen: ab Welle 5 (schwerer Todesstern), Tod über dem Boden.

## Arbeitsweise

* Originalquelle: `F:\_ref\star-wars` (historicalsource/star-wars), MAME-Quelle:
  `F:\_ref\mame-src` und `F:\mame\src`. MAME 0.289 mit ROM-Satz: `D:\Mame`.
* Prüfen (WSL): `sh tools/checkall.sh AUFNAHMEN-ORDNER /mnt/d/Mame/roms`
  für die C-Routinen, mit `--emu 1` für das Originalprogramm auf dem Kern.
* Aufnahmen: `tools/mame/swcapture.lua`, mit `SWCAP_PLAY` als gespieltem
  Skript, z. B. `"120 coin 1;160 coin 1;240 fire 3;560 aim 5"` (Münzpulse
  16 bis 200 ms: `coin 1`); `test N` und `aux N` sind Testschalter und
  Aux-Münze (dafür einen eigenen `-cfg_directory` nehmen: MAME merkt sich
  den Testschalter, und mit ihm startet die nächste Aufnahme im
  Selbsttest). Was das Spiel von selbst nicht erreicht, holt
  `poke ADRESSE WERT` (`4860 06`: Schilde auffüllen, `4844 01`: Torpedo
  auslösen, `4860 00`: keine Schilde mehr); der Vergleich macht den
  Eingriff mit. Eine Aufnahme braucht 44 KB je Frame.
* Tempo neu anpassen: `swcompare AUFNAHME --csv DATEI` schreibt je Frame
  MAMEs Rechenzeit und die gezählte Arbeit; `python3 tools/fitpace.py DATEIEN`
  (braucht numpy) rechnet daraus per kleinster Quadrate die Kosten für
  `src/game/pace.c`.
* Zahlen in der Quelle sind hexadezimal, dezimale haben einen Punkt (`24.`).
  Symbolnamen sind auf 6 Zeichen gekürzt. Lokale Tabellen stehen nicht in
  WSROOT.MAP: Adressen findet `python3 -I tools/dis6809.py ROMZIP ADRESSE`
  (Phasentabelle bei 6044). Die "J"-Routinen liegen in der zweiten ROM-Bank
  (`O8`/`O16`).
* Quelltext nie über Shell-Heredocs ändern (Backslashes und Apostrophe
  gehen dort kaputt).
* Pi-Build: `cmake --build --preset pi-clang`, dann `sh tools/deploy.sh`.
