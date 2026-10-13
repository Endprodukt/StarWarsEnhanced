# Star Wars Enhanced – Architektur und Plan

Ziel: Gameplay möglichst identisch zum Automaten von 1983, deutlich flüssigere
Darstellung auf einem Raspberry Pi 5 mit USB-DVG und Wells-Gardner 6100,
dazu Remaster-Optionen für Grafik und Sound. Grundlage ist der originale
Quellcode samt Kommentaren
([historicalsource/star-wars](https://github.com/historicalsource/star-wars)).

## Timing des Originals (WSINT.MAC, WSMAIN.MAC)

| Takt | Rate | Inhalt |
|---|---|---|
| IRQ | 12,096 MHz / 4096 / 12 = **246,09 Hz** | Münzen, Entprellen der Schalter, Potis (A/D), Spielzeit |
| VG | jede 6. IRQ = **41,02 Hz** | Puffertausch, Fadenkreuz (`RHPOS`), Laser-, Blitz- und Schuss-Animationen |
| Spiel | jede 12. IRQ = **20,51 Hz** | die gesamte Simulation (`GMTIMR = 11`, `WAITFRAME` → `TPHASE`) |

* Bewegungen laufen **pro Tick**, ohne Zeitfaktor. Phasenzeiten zählen Ticks
  (`CMPD #2*20.` = 2 s).
* Doppelpuffer über `VGSYNC`: Jedes 20-Hz-Bild wird etwa zweimal gezeichnet.
  Das ist die Ursache für das Ruckeln.
* Die Rotation ist schon im Original getrennt: Der Simulationszustand ändert
  sich in Stufen von 4,99° bzw. 0,895°, der Rest (`RH$RSD`) wird nur auf eine
  Kopie für die Anzeige angewandt (`RESIDU`).
* Die Weltkoordinaten laufen in 16 Bit über (Bodenphase: `IFVS → INC GD.SEQ`),
  deshalb wird modulo 2¹⁶ interpoliert.

## Aufbau

```
sw_core   C, Festkomma, bitgenau      Takte · Math Box (Q2.14) · Phasen
  │                                    aus WSMAIN, WSGRND, WSBASE …
  ├─ Weltzustand pro Tick  ──►  Renderer „Original“: Linien wie beim AVG,
  │                              interpoliert
  │                         └─►  Renderer „Remaster“: vp3d, verdeckte Linien,
  │                              Nebel, neue Modelle
  ├─ Sound-Ereignisse (AUDxx / SPKxx)
  │                         ──►  „Original“: Sound-Treiber aus SND*.MAC
  │                              + POKEY ×4 + TMS5220, emuliert
  │                         └─►  „Remaster“: Samples / OGG
  └─ AVG-Displayliste (HUD, Text) ──► avg.c ──► dvgmap.c ──► vp_dvg ──► USB-DVG
```

### Warum eine eigene Schleife statt `vp_run`

Das SDK bleibt unverändert. `vp_run` gibt `draw()` keinen Interpolationsfaktor
und taktet Updates nur in ganzen Hz. `src/platform/app.c` übernimmt daher die
Konventionen von `vp_run` (Wahl der Ausgabe, `--dvg/--window/--windowed`,
`ui_cancel`, Ctrl+C, Marquee, `VP_STATS=1`) und ergänzt den 246-Hz-Takt sowie
eine frei wählbare Bildrate. Alle benutzten SDK-Funktionen sind öffentlich.

### Sound ohne Änderung am SDK

`vp_audio` spielt nur Samples und Dateien, einen Callback für selbst erzeugten
Ton gibt es nicht. Der Original-Sound (emulierte POKEYs und TMS5220 in
Echtzeit) läuft deshalb über ein eigenes SDL-Audiogerät mit eigenem Mixer.
SDL2 wird über das SDK ohnehin gelinkt. Remaster-Samples laufen durch
denselben Mixer, damit es nur ein Audiogerät gibt.

### Referenz für „identisch“

`tools/mame/swcapture.lua` zeichnet in MAME (`starwars1`) pro Spieltick
VG-RAM, RAM und Math-Box-RAM auf. Der native Kern muss Tick für Tick denselben
Zustand liefern. Für Determinismus werden später auch die gelesenen
Eingaben und Zufallszahlen (`PRNG`, 0x4703) mitgeschnitten und eingespeist.

## Zwei Wege, ein Spiel (seit 2026-10-13)

Das Spiel von Hand zu portieren heißt rund 20.000 Zeilen 6809-Assembler.
Damit es vorher schon vollständig und unverändert läuft, gibt es zwei Wege
nebeneinander:

1. **Das Originalprogramm auf einem 6809-Kern** (`src/cpu/m6809.c`,
   `src/core/emu.c`), der Standard von `swenhanced`. Es läuft gegen die
   native Maschine (`sw_machine`: RAM, Math Box, Dividierer, NOVRAM,
   Schalter, Zeit des Vektorgenerators). Das Tempo ergibt sich aus den
   gezählten Zyklen; Math Box und Vektorgenerator brauchen ihre Zeit wie in
   MAME. Die Soundplatine läuft genauso mit (`src/sound`).
2. **Die von Hand portierten Routinen** (`src/game`, `swenhanced --native`).
   Sie rechnen Frame für Frame dasselbe und werden mit `swcompare` gegen
   dieselben MAME-Aufnahmen geprüft. Sie sind der Weg zum Remaster: Erst
   wenn eine Phase nativ vorliegt, kann ihr Bild aus dem Spielzustand statt
   aus der Displayliste gezeichnet werden.

Die Darstellung ist für beide dieselbe: Displayliste, AVG-Interpreter,
Zwischenbilder, Platine.

### Zwischenbilder

`src/render/smooth.c`. Jede Linie einer Displayliste bekommt eine Kennung
(`sw_avg_run_tagged`): welche Routine das Wort geschrieben hat (beim
Originalprogramm die Rücksprungadresse auf dem Stack, bei den portierten
Routinen ein Tag aus `core/tags.h`), die wievielte Anweisung dieses Aufrufs
es ist, welche Vektor-ROM-Routine die Linie zeichnet und die wievielte
Linie darin. Linien mit gleicher Kennung in zwei aufeinanderfolgenden
Frames sind dieselbe Linie; bei mehreren (alle Sterne, derselbe Buchstabe,
dieselbe Kante zweier Schiffe) gilt die nächstliegende. Dazwischen wird
linear bewegt. Das Fadenkreuz gehört nicht zum Frame: Der Interrupt bewegt
es, gezeigt wird es dort, wo das Joch es jetzt hat.

## Fahrplan

- [x] **1 Grundgerüst**: CMake gegen das unveränderte SDK (Presets linux/pc/pi),
      ROM-Lader (CRC, zip oder Ordner), Scheduler mit den drei Takten,
      AVG-Interpreter (Befehlssatz aus WSVGMC.MAC), Abbildung auf den DVG mit
      Konfiguration für den WG6100, Kalibrierbild, MAME-Aufnahme und
      `avgreplay`, Symboltabelle aus WSROOT.MAP, Tests
- [x] **2 Math Box**: `tools/mbasm.py` assembliert SWMP.MAC nach den Makros
      von TCODE2.MAC. Das Ergebnis trifft die CRCs aller vier PROMs
      (136021-110…113), und alle 13 Einsprünge liegen dort, wo das Spiel sie
      aufruft. Daraus wird `src/mbox/mbox_programs.c` erzeugt: eine
      C-Funktion pro Programm, Mikrobefehl für Mikrobefehl, mit den
      Originalkommentaren. Dazu ein Mikrocode-Interpreter als Referenz,
      Tests gegen ihn und gegen die Formeln aus SWMP.DOC, der Divider, und
      beim Start ein Selbsttest gegen die PROMs des eigenen ROM-Satzes
- [x] **3 Interrupt und Steuerung**: WSINT nativ (`src/game/irq.c`):
      Spieltakt über `GMTIMR`/`GMSYNC`, Entprellen, Feuer-Flanken
      (`VG.LON`), Potifilter (`DOPOTS`), Spielzeit-Timer in BCD,
      VG-Neustart mit Pufferwechsel, Joch-Kalibrierung (`RHLIM`),
      Fadenkreuz (`RHPOS`, `GNSITE`), Laser-, Schuss- und Blitz-Tabellen,
      Laser-Splash (TCSPLS). Münzen über Ataris MOOLAH (COIN69) 1:1.
      Speicherbild mit den Originaladressen (`src/core/machine.c`),
      Steuerung über `vp_input` (`src/platform/controls.c`), Live-Testbild
      für Joch, Knöpfe und Münzen
- [ ] **4 Phasen**, jede gegen die MAME-Aufnahme geprüft (`tools/swcompare`):
  - [x] Hauptschleife (WAITFRAME, IFRAME, TPHASE), Attract: Banner mit
        Lauftext, Fluganweisungen, Punktetabelle, Highscores; dazu
        Spielstart bis zur Wahl des Todessterns und Initialen-Eingabe.
        Meldungen (TCMES), Highscores (TCHSCR), Sterne (WSSTAR), EEROM für
        Reset und Attract (TCEROM), Fadenkreuz (WSSITE). 3006 von 3006
        Frames der Attract-Aufnahme stimmen bitgenau (RAM, Math-Box-RAM,
        Displayliste, Zahl der gelesenen Zufallszahlen)
  - [ ] Weltraumwellen (SP1/SP2). Erledigt: Welle aufbauen (`NWNSHP`),
        Choreografie-Interpreter, Objekte (`INOBJ`, `ADBOBJ`, `OBJPNT`,
        `OBJDRW`). Offen: `VIEW`, Spielerbewegung, `CPU`, `AIM`, WSLAZR,
        WSGUNS, WSXPLD, WSPANL, WSGAS, WSGLOW (Reihenfolge in STATUS.md)
  - [ ] Boden und Türme (WSGRND)
  - [ ] Graben (WSBASE)
  - [ ] Todesstern-Explosion (DX1–DX3), Spielende, nächste Welle
  - [ ] Statistik- und Optionsseite (EEACC, EEOPT), Selbsttest (TCTEST)
  Unabhängig vom Stand der Portierung läuft das ganze Spiel über den
  6809-Kern (siehe oben), geprüft mit 2623 von 2623 Frames einer gespielten
  Aufnahme.
- [x] **5a Zwischenbilder aus der Displayliste** (`src/render/smooth.c`),
      Strich-Sortierung, Fadenkreuz-Zentrierung
- [x] **6a Ton über die Soundplatine**: zweiter 6809 mit dem Sound-Programm,
      RIOT, POKEY, TMS5220 (`src/sound`). Am Automaten noch nicht gehört.
- [ ] **5 Renderer „Original“** mit Interpolation (Slerp der Matrizen,
      Positionen modulo 2¹⁶, keine Interpolation bei Spawn/Despawn)
- [ ] **6 Sound „Original“**: Sound-Treiber aus SNDAUD/SNDPM/SNDSPK nativ,
      POKEY- und TMS5220-Emulation, Sprachdaten aus den Sound-ROMs
- [ ] **7 Remaster**: vp3d-Szenen mit verdeckten Linien, Nebel, eigene Modelle,
      Sample-Sound; umschaltbar
- [ ] **8 Paket**: `.vpk`, Highscores über `vp_hiscore`, Marquee

## Tempo des Originals

Das Original läuft nicht mit festen 20,5 Hz. Ein Frame beginnt erst, wenn
der 12. Interrupt da ist **und** der Interrupt den VG-Puffer des vorigen
Bildes übergeben hat. Die Hauptschleife des 6809 braucht im Attract-Modus
25 bis 50 ms pro Frame, der Vektorgenerator bis 40 ms pro Bild. Auf der
Anleitungsseite ergibt das 18,6 statt 12 Interrupts pro Frame, also 13 statt
20,5 Bilder pro Sekunde. Davon hängt die Geschwindigkeit von allem ab.

Nativ ist beides sofort fertig, deshalb wird die Zeit nachgebildet:

* **Vektorgenerator**: `sw_avg_cycles` (src/avg/avg.c) rechnet die Zeichenzeit
  einer Displayliste nach MAMEs AVG-Zustandsmaschine (Zustände pro Befehl,
  Vektortimer mit Normalisierung). Gegen das Zustands-PROM geprüft: exakt.
  Der native Interrupt startet das nächste Bild erst, wenn diese Zeit um ist.
* **6809**: `src/game/pace.c`. Die Routinen zählen ihre Arbeit (Zeichen,
  Ziffern, Meldungen, Sterne), jede Art hat einen Preis. Die Preise sind an
  MAME angepasst: `swcapture.lua` misst die Rechenzeit jedes Frames (TIME),
  `swcompare --csv` schreibt sie neben die nativen Zähler. Fehler im
  Attract-Modus: 1 ms (rms) bei 25 bis 50 ms.

Ergebnis (Interrupts pro Frame, Original/nativ): Banner 12,2/13,5,
Anleitung 18,6/18,3, Punktetabelle 12,1/12,0, Highscores 18,0/17,9. Jede
neu portierte Phase braucht eigene Zähler und eine neue Anpassung.

## Offene Punkte

Für das Originalprogramm auf dem 6809-Kern (der Standard):

* Zwischenbilder und Ton sind am Automaten noch nicht gesehen und gehört
  (docs/STATUS.md).
* POKEY ohne Hochpassfilter, TMS5220 vereinfacht (Interpolation und
  Anregung nach Datenblatt, nicht taktgenau wie MAME).
* Menüeintrag und Paket fehlen.

Für die von Hand portierten Routinen (`--native`, `src/game`):

* Es fehlen alle Spielphasen ab der Weltraumwelle. Phasen, die fehlen,
  führen zurück zum Banner.
* Das Tempo kommt dort aus einem Modell (`src/game/pace.c`), das den Banner
  nur auf 10 % trifft (13,5 statt 12,2 Interrupts pro Frame). Der 6809-Kern
  braucht kein Modell.
* Buchhaltung beim Münzeinwurf und Spielende (`EEACCU`, `EEACCT`),
  Statistik- und Optionsseite fehlen.
* Beim Vergleich der portierten Routinen werden die Werte, die die
  Hauptschleife mitten im Frame vom Interrupt liest, nur für das Fadenkreuz
  eingespeist. Der 6809-Weg bekommt sie vollständig aus der Aufnahme (IRDS).
* Beim Vergleich fehlt der Dividend des Dividierers in der Aufnahme;
  `swcompare` setzt ihn auf den Wert aus `IMATH`. WSGUNS und WSLAZR
  schreiben andere Werte: dann mit aufnehmen.

Allgemein:

* Bildlage: Das Spiel zeichnet mit `VGSCAL` in Viertelgröße, der Bildrand
  (VGEDG*: ±510 × −612…+600 in Vektorworten) liegt also bei ±127,5 ×
  −153…+150 AVG-Einheiten. So steht es jetzt in `swenhanced.ini`; am
  Automaten füllt das Bild die Röhre. Feinabgleich mit `--calibrate` steht
  aus.
* Die Skalierung der AVG-Vektoren folgt MAME (lineare Skala `^0xFF`/256,
  binäre Skala 1/2ⁿ). Ob das exakt stimmt, zeigt der Vergleich der
  Aufnahmen mit MAMEs eigener Darstellung.
* Strahlbudget: Der USB-DVG schafft laut SDK etwa 3 Mio. Punkte pro Sekunde.
  Wie viele Hz im Graben möglich sind, zeigt `VP_STATS=1`.
