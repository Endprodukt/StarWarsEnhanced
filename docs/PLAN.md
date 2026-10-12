# Star Wars VP – Architektur und Plan

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
- [ ] **3 Takte und Steuerung**: WSINT nativ (Entprellen, `DOPOTS`, `RHLIM`,
      `RHPOS`, `GNSITE`), Joch über `vp_input_axis`, Münzen und Credits
      über `vp_credits`, LEDs über `vp_lamps`
- [ ] **4 Phasen**, jede gegen die MAME-Aufnahme geprüft: Attract/Banner/
      Highscores → Weltraumwellen (SP1/SP2, `STWSP1`, `AIM`, `CPU`) →
      Boden und Türme (WSGRND) → Graben (WSBASE) → Explosionen (WSXPLD)
- [ ] **5 Renderer „Original“** mit Interpolation (Slerp der Matrizen,
      Positionen modulo 2¹⁶, keine Interpolation bei Spawn/Despawn)
- [ ] **6 Sound „Original“**: Sound-Treiber aus SNDAUD/SNDPM/SNDSPK nativ,
      POKEY- und TMS5220-Emulation, Sprachdaten aus den Sound-ROMs
- [ ] **7 Remaster**: vp3d-Szenen mit verdeckten Linien, Nebel, eigene Modelle,
      Sample-Sound; umschaltbar
- [ ] **8 Paket**: `.vpk`, Highscores über `vp_hiscore`, Marquee

## Offene Punkte

* Die AVG-Einheiten des Bildschirms (VGEDG*: ±510 × −612…+600) sind aus
  WSROOT.MAP übernommen. Lage und Seitenverhältnis auf dem WG6100 müssen mit
  `--calibrate` und `avgreplay` eingestellt werden.
* Die Skalierung der AVG-Vektoren folgt MAME (lineare Skala `^0xFF`/256,
  binäre Skala 1/2ⁿ). Ob das exakt stimmt, zeigt der Vergleich der
  Aufnahmen mit MAMEs eigener Darstellung.
* Strahlbudget: Der USB-DVG schafft laut SDK etwa 3 Mio. Punkte pro Sekunde.
  Wie viele Hz im Graben möglich sind, zeigt `VP_STATS=1`.
