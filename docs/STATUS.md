# Stand und Weitermachen

Stand: 2026-10-13. Architektur und Hintergründe: [PLAN.md](PLAN.md).

## Ziel (Wunsch des Besitzers, 2026-10-13)

So weit weg von Emulation wie möglich, und das Bild so flüssig wie möglich.
Der 6809-Kern ist deshalb nur Lückenfüller und wird mit jeder portierten
Routine kleiner; am Ende soll er nicht mehr gebraucht werden.

## Was läuft

Das ganze Spiel, unverändert, in drei Arten (`starwarsvp`):

* **Standard**: Interrupt und Hauptschleife sind die C-Routinen aus
  `src/game`. Was noch nicht portiert ist, führt ein 6809-Kern aus dem
  Original-ROM aus: ganze Phasen (`original_frame`) oder einzelne
  Unterroutinen einer sonst portierten Phase (`ORIG(adresse)`), auf demselben
  Speicher. Portiert sind Interrupt, Münzen, Attract, Spielstart,
  Wellenaufbau, Choreografie, die ganze Weltraumwelle, der Anflug auf den
  Todesstern (S0G, S1G), der ganze Graben (Aufbau, Ansicht, Wandteile,
  Kanonen, Laser, Protonentorpedo, Wiederholung nach verfehltem Schacht),
  die Tode im Weltraum und im Graben und das Spielende.
  Die erste Welle vom Start bis Game Over ist damit reines C; aus dem
  Original kommt dort nur noch die Buchhaltung am Spielende (EEACCT).
  Noch ganz über den Kern laufen: die Explosion des Todessterns (DX1 bis
  DX3), der Übergang zur nächsten Welle (NXT), der Boden mit den Türmen
  (GD, G0B, G1B, G0D), Selbsttest und Optionen (STS, OPT).
* `--original`: das ganze Originalprogramm auf dem 6809-Kern.
* `--native`: nur C, ohne Kern (eine fehlende Phase führt zum Banner).

Nativ sind in jedem Fall Math Box, Dividierer, NOVRAM, Schalter und Joch,
die Zeit des Vektorgenerators, die Bildausgabe und die Zwischenbilder. Der
Ton kommt noch von der nachgebildeten Soundplatine (zweiter 6809-Kern mit
dem Sound-Programm): auch das ist noch Emulation.

* **Geprüft gegen MAME**, Frame für Frame (RAM, Math-Box-RAM, Displayliste),
  fünf Aufnahmen mit 13.570 Frames: Attract und vier gespielte (Tod im
  Graben, Tod im Weltraum, verfehlter Schacht mit Wiederholung, zerstörter
  Todesstern mit zweiter Welle, Darth Vader und Boden). Das ganze Original
  auf dem Kern stimmt in allen Frames; von den C-Routinen alle 12.695
  Frames der portierten Phasen.
* **Tempo** aus den gezählten CPU-Zyklen, wie das Original: Interrupts pro
  Frame Original/hier 12,2/12,4 (Banner), 18,6/18,6 (Anleitung), 12,1/12,2
  (Punktetabelle), 18,0/17,9 (Highscores).
* **Flüssige Darstellung** (`src/render/smooth.c`): Zwischen zwei
  Spielframes wird jede Linie bewegt; das Bild kommt mit der Rate der
  Platine statt 12 bis 20 Mal pro Sekunde. Im freien Lauf werden 92 % der
  Linien im nächsten Frame wiedergefunden (Attract: 99 %).
* **Strich-Sortierung**: 3-mal weniger Leerweg auf der Highscore-Seite
  (59.000 statt 172.000 Einheiten).
* **Fadenkreuz zentriert** ab dem ersten Bild (`yoke_autocenter`).
* **Ton**: Die Soundplatine läuft mit (zweiter 6809 mit dem Programm aus
  den Sound-ROMs, RIOT, vier POKEYs, TMS5220-Sprache).
* **NOVRAM** (Optionen, Buchhaltung, beste drei Scores) in `save/nvram.bin`.
* Die von Hand portierten Routinen (`src/game`: Attract, Spielstart,
  Wellenaufbau, Choreografie, Objekte) rechnen dasselbe und bleiben
  geprüft; `starwarsvp --native` lässt sie statt des Originalprogramms
  laufen (ohne Ton, Spielphasen fehlen dort).

## Noch nicht am Automaten gesehen oder gehört

Der Automat war bei dieser Arbeit aus. Aufgespielt ist der Stand vom
Attract-Modus davor. Ungeprüft am Gerät, also nur gerechnet und gemessen:

* die Zwischenbilder (Aussehen, Artefakte bei auftauchenden Objekten),
* der Ton überhaupt: Die Platine erzeugt plausible Pegel zur richtigen Zeit
  (Sprache beim Münzeinwurf und Start, Musik ab der Welle), aber niemand
  hat ihn gehört. POKEY ohne Hochpassfilter, TMS5220 vereinfacht.
* Münzen, Start, Joch und Feuer im Spiel, `--calibrate`,
* die Bildrate der Platine im Spiel.

## Bekannte Grenzen

* `smooth = 1` zeigt die Welt einen Spielframe (50 bis 90 ms) später; das
  Fadenkreuz nicht. `smooth = 2` hat keine Verzögerung, schießt aber bei
  Richtungswechseln über. Am Automaten ausprobieren.
* Linien ohne Partner im vorigen Frame springen (wie im Original).
* Kein Menüeintrag am Automaten, kein `.vpk`.
* Selbsttest und Optionsseite brauchen den Testschalter: `p1_button6`
  schaltet ihn um, `p1_button5` ist die Aux-Münze. Beide sind am Automaten
  nicht belegt, solange `advmame.rc` sie nicht zuweist.
* `--native`: nur Attract und Spielstart, Tempomodell statt Zyklen.

## Als Nächstes

0. Weiter portieren, jede Routine einzeln gegen die Aufnahmen geprüft
   (`sh tools/checkall.sh ORDNER ROMDIR`). Offen: EEACCT (TCEROM.MAC), dann
   DX1 bis DX3 (BGXPLN, VWXPLN in WSXPLD.MAC) und NXT, dann der Boden:
   GD, G0B, G1B, G0D (WSGRND.MAC, FRBxGN/FRWxGN in `guns.c`, BGBKXP/BGTWXP
   in `xpld.c`, GDHTGB), zuletzt STS und OPT. Die Aufnahme `play4` kommt
   durch alle diese Phasen außer STS/OPT.
   Schon portiert, aber von keiner Aufnahme durchlaufen: die Bewegung der
   Schüsse von Bunkern und Türmen (MOVBx, MOVWx), die Trümmer von
   Bunkern und Türmen (MVBKn, VWBKn, VWTWn), BJGCLP.
   Was das Spiel von selbst nicht erreicht, holt `poke ADRESSE WERT` im
   Aufnahmeskript (`4860 06`: Schilde auffüllen, `4844 01`: Torpedo
   auslösen, `4860 00`: keine Schilde mehr); der Vergleich macht den
   Eingriff mit. Ein gespieltes Skript sieht so aus:
   `SWCAP_PLAY="120 coin 1;160 coin 1;240 fire 3;560 aim 5;..."`.
   Lokale Adressen findet `python3 -I tools/dis6809.py ROMZIP ADRESSE`
   (Phasentabelle bei 6044).
   Danach den Ton: SNDAUD, SNDPM, SNDSPK.
   Parallel der Renderer aus dem Spielzustand: Schiffe und Sterne aus ihren
   Matrizen und Positionen im Math-Box-RAM 50-mal pro Sekunde neu rechnen,
   statt fertige Linien zu verschieben.
1. Am Automaten: `sh tools/deploy.sh`, dann Bild, Ton und Steuerung prüfen;
   `smooth` 1 gegen 2 vergleichen.
2. Menüeintrag und `.vpk` (Schritt 8).
3. Ton nach Gehör: POKEY-Hochpass, TMS5220 genauer (Vergleich mit einer
   MAME-Aufnahme, `-wavwrite`).
4. Remaster (Schritt 7): Bild aus dem Spielzustand statt aus der
   Displayliste. Dafür die Routinen in `src/game` weiter portieren; jede
   ist sofort gegen das Originalprogramm prüfbar.

## Arbeitsweise

* Originalquelle: `F:\_ref\star-wars` (historicalsource/star-wars), MAME-Quelle:
  `F:\_ref\mame-src` und `F:\mame\src`. MAME 0.289 mit ROM-Satz: `D:\Mame`.
* Prüfen (WSL): `sh tools/checkall.sh AUFNAHMEN-ORDNER /mnt/d/Mame/roms --emu 1`
  für das Originalprogramm auf dem 6809-Kern, ohne `--emu 1` für die
  portierten Routinen.
* Aufnahmen: `tools/mame/swcapture.lua`, mit `SWCAP_PLAY` als gespieltem
  Skript (Münzpulse 16 bis 200 ms: `coin 1`).
* Zahlen in der Quelle sind hexadezimal, dezimale haben einen Punkt (`24.`).
  Symbolnamen sind auf 6 Zeichen gekürzt. Lokale Tabellen stehen nicht in
  WSROOT.MAP: `src/game/m6809.h` (`SW_L_xxx`). Die "J"-Routinen liegen in
  der zweiten ROM-Bank (`O8`/`O16`).
* Quelltext nie über Shell-Heredocs ändern (Backslashes gehen verloren).
* Pi-Build: `cmake --build --preset pi-clang`, dann `sh tools/deploy.sh`.
