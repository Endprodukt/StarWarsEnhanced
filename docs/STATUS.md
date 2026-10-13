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

`starwarsvp` kennt drei Arten:

* **Standard**: die C-Routinen. Der 6809-Kern steht nur noch für zwei
  Dinge bereit: die Phasen Selbsttest und Optionen (STS, OPT, nur mit
  Testschalter erreichbar) und die Soundplatine.
* `--original`: das ganze Originalprogramm auf dem 6809-Kern (zum Vergleich).
* `--native`: nur C, ohne Kern und ohne Ton.

Nativ sind außerdem Math Box, Dividierer, NOVRAM, Schalter und Joch, die
Zeit des Vektorgenerators, die Bildausgabe und die Zwischenbilder. **Noch
Emulation ist der Ton**: die nachgebildete Soundplatine (zweiter 6809-Kern
mit dem Programm aus den Sound-ROMs, RIOT, vier POKEYs, TMS5220).

* **Geprüft gegen MAME**, Frame für Frame (RAM, Math-Box-RAM,
  Displayliste), sechs Aufnahmen mit 21.609 Frames: Attract und fünf
  gespielte (Tod im Graben, Tod im Weltraum, verfehlter Schacht mit
  Wiederholung, zerstörter Todesstern, vier Wellen am Stück mit Darth
  Vader, Bunkern und Türmen). Alle Frames stimmen, mit den C-Routinen wie
  mit dem ganzen Original auf dem Kern.
* **Tempo**: Im Original bestimmt die Rechenzeit des 6809 die Bildrate (im
  Graben etwa 13 Bilder pro Sekunde). Die C-Routinen sind sofort fertig und
  warten deshalb so lange, wie der 6809 gebraucht hätte (`src/game/pace.c`):
  aus gezählter Arbeit (gelesene Bytes, Math-Box-Läufe, Zeichen) und einem
  Grundwert je Phase, angepasst an MAMEs gemessene Zeit je Frame. Abweichung
  in den Spielphasen 0,9 ms (rms) bei 25 bis 80 ms je Frame, im Mittel je
  Phase null. Im freien Lauf: Graben 18,6 Interrupts je Frame gegen 18,2 im
  Original, Weltraum 16,1 gegen 17,4 (anderer Spielverlauf), Attract
  12,2/12,4 (Banner), 18,6/18,6 (Anleitung), 18,0/17,9 (Highscores).
* **Flüssige Darstellung** (`src/render/smooth.c`): Zwischen zwei
  Spielframes wird jede Linie bewegt; das Bild kommt mit der Rate der
  Platine statt 12 bis 20 Mal pro Sekunde. Im freien Lauf werden 92 % der
  Linien im nächsten Frame wiedergefunden (Attract: 99 %).
* **Strich-Sortierung**: 3-mal weniger Leerweg auf der Highscore-Seite.
* **Fadenkreuz zentriert** ab dem ersten Bild (`yoke_autocenter`).
* **NOVRAM** (Optionen, Buchhaltung, beste drei Scores) in `save/nvram.bin`.

## Noch nicht am Automaten gesehen oder gehört

Der Automat war bei dieser Arbeit aus. Aufgespielt ist der Stand vom
Attract-Modus davor. Ungeprüft am Gerät, also nur gerechnet und gemessen:

* das ganze Spiel in C am Gerät (Münzen, Start, Joch, Feuer),
* die Zwischenbilder (Aussehen, Artefakte bei auftauchenden Objekten),
* der Ton überhaupt: Die Platine erzeugt plausible Pegel zur richtigen Zeit
  (Sprache beim Münzeinwurf und Start, Musik ab der Welle), aber niemand
  hat ihn gehört. POKEY ohne Hochpassfilter, TMS5220 vereinfacht.
* `--calibrate`, die Bildrate der Platine im Spiel.

## Bekannte Grenzen

* `smooth = 1` zeigt die Welt einen Spielframe (50 bis 90 ms) später; das
  Fadenkreuz nicht. `smooth = 2` hat keine Verzögerung, schießt aber bei
  Richtungswechseln über. Am Automaten ausprobieren.
* Linien ohne Partner im vorigen Frame springen (wie im Original).
* Die Zwischenbilder verschieben fertige Linien. Der eigentliche Plan
  (Bild aus dem Spielzustand neu rechnen) ist noch nicht begonnen.
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
  laufen über den 6809-Kern.

## Als Nächstes

1. Am Automaten: `sh tools/deploy.sh`, dann Bild, Ton und Steuerung prüfen;
   `smooth` 1 gegen 2 vergleichen.
2. Der Renderer aus dem Spielzustand: Schiffe, Sterne, Graben und Türme aus
   ihren Matrizen und Positionen mit der Rate der Platine neu rechnen,
   statt fertige Linien zu verschieben. Die Routinen dafür sind jetzt alle
   in C da (`shipview.c`, `star.c`, `base.c`, `panel.c`, `ground.c`,
   `obj.c`); die Marken der Linien (`core/tags.h`) sagen, was was ist.
3. Den Ton nativ: SNDAUD, SNDPM, SNDSPK (Sound-Programm) portieren, wie das
   Spielprogramm. Vorher nach Gehör prüfen: POKEY-Hochpass, TMS5220 genauer
   (Vergleich mit einer MAME-Aufnahme, `-wavwrite`).
4. STS und OPT portieren (TCTEST.MAC), dann braucht das Spielprogramm den
   6809-Kern gar nicht mehr.
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
