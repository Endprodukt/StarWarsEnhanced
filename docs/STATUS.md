# Stand und Weitermachen

Stand: 2026-10-13. Architektur und Hintergründe: [PLAN.md](PLAN.md).

## Ziel (Wunsch des Besitzers, 2026-10-13)

So weit weg von Emulation wie möglich, und das Bild so flüssig wie möglich.
Der 6809-Kern ist deshalb nur Lückenfüller und wird mit jeder portierten
Routine kleiner; am Ende soll er nicht mehr gebraucht werden.

## Was läuft

Das ganze Spiel, unverändert, als C-Programm (`src/game`, Zeile für Zeile
nach der Quelle von 1983): Interrupt, Münzen, Attract, Spielstart,
Weltraumwelle, Anflug, Boden mit Türmen und Bunkern, Graben, Protonentorpedo,
Explosion des Todessterns, Wellenwechsel, alle Tode, Spielende mit
Buchhaltung und Highscore-Eingabe. In `src/game` gibt es keinen Aufruf ins
Originalprogramm mehr (`grep -rn "ORIG(" src/game` ist leer).

Ebenso das Programm der Soundplatine (`src/sound/sndprog.c`: Hauptschleife,
Effekte SNDAUD, Musik SNDPM, Sprache SNDSPK).

`starwarsvp` kennt drei Arten:

* **Standard**: alles in C, kein 6809. Selbsttest und Optionsseite (STS,
  OPT; nur mit Testschalter erreichbar) sind nicht portiert und führen
  zurück zum Banner.
* `--hybrid`: dasselbe, für STS und OPT springt ein 6809-Kern ein.
* `--original`: Spiel- und Sound-Programm ganz auf 6809-Kernen (zum Vergleich).

Als Bausteine nachgebildet (keine Programme, sondern Hardware): Math Box,
Dividierer, NOVRAM, die vier POKEYs, der Sprachchip TMS5220, der RIOT der
Soundplatine, die Zeit des Vektorgenerators.

* **Feste Bildrate** (`steady_frames = 1`, Standard, Wunsch des Besitzers):
  ein Spielframe jeden 12. Interrupt, 20,5 pro Sekunde, in jeder Phase
  (gemessen: 12,0 Interrupts je Frame überall). So ist das Programm
  geschrieben ("20 PER SECOND", alle Zeiten rechnen mit 20 Frames je
  Sekunde); der Automat schaffte das in Graben und Boden nicht (13 bis 15).
  Folge: Graben und Boden laufen schneller als am Automaten. Mit
  `steady_frames = 0` wartet das Spiel wie der 6809 (siehe Tempo).
* **Ton in C, geprüft gegen das Original** (`tests/test_sound.c`): Zwei
  Soundplatinen bekommen dieselben 59 Anforderungen (alle, die es gibt);
  die eine führt das Original-Programm auf dem 6809-Kern aus, die andere
  das C-Programm. Alle Schreibzugriffe sind gleich und im selben Durchlauf
  der Hauptschleife: 3.656 auf die Effekt-Kanäle, 55.296 auf die
  Musik-Kanäle, 18 auf die Steuerregister; die 6.685 Sprachbytes sind
  gleich und höchstens einen Durchlauf (4 ms) versetzt.
* **Geprüft gegen MAME**, Frame für Frame (RAM, Math-Box-RAM,
  Displayliste), sechs Aufnahmen mit 21.609 Frames: Attract und fünf
  gespielte (Tod im Graben, Tod im Weltraum, verfehlter Schacht mit
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
  Gilt für Weltraum, Anflug, Boden, Graben und die Tode; die Attract-Seiten
  und einzelne Übergangsframes nutzen weiter das Verschieben der Linien
  (`src/render/smooth.c`, `smooth = 1` oder `2`), bei dem 92 % der Linien im
  nächsten Frame wiedergefunden werden (Attract: 99 %).
* **Strich-Sortierung**: 3-mal weniger Leerweg auf der Highscore-Seite.
* **Fadenkreuz zentriert** ab dem ersten Bild (`yoke_autocenter`).
* **NOVRAM** (Optionen, Buchhaltung, beste drei Scores) in `save/nvram.bin`.

## Noch nicht am Automaten gesehen oder gehört

Der Automat war bei dieser Arbeit aus. Aufgespielt ist der Stand vom
Attract-Modus davor. Ungeprüft am Gerät, also nur gerechnet und gemessen:

* das ganze Spiel in C am Gerät (Münzen, Start, Joch, Feuer), die feste
  Bildrate (ob das Tempo in Graben und Boden so gewollt ist),
* die neu gerechneten Zwischenbilder (`smooth = 3`): Aussehen, Rechenzeit
  auf dem Pi, Fadenkreuz. Steht in der Einstellungsdatei am Automaten noch
  `smooth = 1`, bleibt es dabei, bis man es dort ändert.
* der Ton überhaupt: Die Platine erzeugt plausible Pegel zur richtigen Zeit
  (Sprache beim Münzeinwurf und Start, Musik ab der Welle), aber niemand
  hat ihn gehört. POKEY ohne Hochpassfilter, TMS5220 vereinfacht.
* `--calibrate`, die Bildrate der Platine im Spiel.

## Bekannte Grenzen

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
* Selbsttest und Optionsseite brauchen den Testschalter: `p1_button6`
  schaltet ihn um, `p1_button5` ist die Aux-Münze. Beide sind am Automaten
  nicht belegt, solange `advmame.rc` sie nicht zuweist. Diese beiden Phasen
  gibt es nur mit `--hybrid` (6809-Kern).

## Als Nächstes

1. Am Automaten: `sh tools/deploy.sh`, dann Bild, Ton und Steuerung prüfen;
   `smooth` 1 gegen 2 vergleichen.
2. Die Zwischenbilder aus dem Spielzustand auch für die Attract-Seiten
   (Banner, Anleitung, Highscores), die Explosion des Todessterns (DX2, DX3)
   und den Wellenwechsel (NXT); das Fadenkreuz mit jedem Bild nachführen.
3. Den Ton nach Gehör prüfen: POKEY-Hochpass, TMS5220 genauer (Vergleich
   mit einer MAME-Aufnahme, `-wavwrite`). Das Programm ist portiert; was
   noch von MAME abweichen kann, sind die Bausteine.
4. STS und OPT portieren (TCTEST.MAC); dann kann `--hybrid` entfallen und
   der 6809-Kern dient nur noch den Vergleichstests.
5. Menüeintrag und `.vpk`.
6. Mehr Aufnahmen: ab Welle 5 (schwerer Todesstern), Tod über dem Boden.

## Arbeitsweise

* Originalquelle: `F:\_ref\star-wars` (historicalsource/star-wars), MAME-Quelle:
  `F:\_ref\mame-src` und `F:\mame\src`. MAME 0.289 mit ROM-Satz: `D:\Mame`.
* Prüfen (WSL): `sh tools/checkall.sh AUFNAHMEN-ORDNER /mnt/d/Mame/roms`
  für die C-Routinen, mit `--emu 1` für das Originalprogramm auf dem Kern.
* Aufnahmen: `tools/mame/swcapture.lua`, mit `SWCAP_PLAY` als gespieltem
  Skript, z. B. `"120 coin 1;160 coin 1;240 fire 3;560 aim 5"` (Münzpulse
  16 bis 200 ms: `coin 1`). Was das Spiel von selbst nicht erreicht, holt
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
