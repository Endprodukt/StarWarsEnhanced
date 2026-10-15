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

`swenhanced` kennt zwei Arten:

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
  ein MP4 (braucht PyAV). Ergebnis: `video/swenhanced.mp4` (nicht im Repo;
  es liegt immer nur das neueste Video dort, alte werden gelöscht). Behoben
  am 2026-10-13: Das Werkzeug ließ das Soundboard nicht zwischen zwei
  Anforderungen eines Frames laufen, das Spiel setzte es deshalb 129-mal
  zurück (Ton und Musik abgehackt). Das Spiel selbst (`src/main.c`) war
  nicht betroffen.
* **Liste aller Sounds** für den späteren Ersatz: `docs/SOUNDS.md`.
* **Strich-Sortierung**: 3-mal weniger Leerweg auf der Highscore-Seite.
* **Grafische Erweiterungen über der Oberfläche** (`src/render/enhance.c`,
  2026-10-13, Wunsch des Besitzers; vier Einstellungen, jede abschaltbar, im
  Testmenü und in der Einstellungsdatei, Standard an):
  * `enh_surface`: der Boden als Plattenmuster des Todessterns, Linien in
    Perspektive bis zum Horizont, manche Platten unterteilt oder mit Luke.
  * `enh_stars`: Sterne am Himmel; auch über dem Graben, im Streifen
    zwischen den Oberkanten der Wände (ein Stern zeigt sich, wenn die
    Sichtlinie den Graben durch die offene Oberseite verlässt).
  * `enh_towers`: Türme und Bunker mit Stufen,
    Nähten, Geschützturm und Rohren (Vorlage: Foto eines Turmmodells). Ihre
    Maße sind auf die Einheit die des Automaten (Vorgabe des Besitzers: man
    muss weiter zwischen ihnen durchpassen): Turm 960, 720, 600, 480 an den
    Höhen seiner ROM-Punkte, Hut 480 und 720 hoch, Bunker 960 auf 720 und
    720 hoch; von vorn gesehen genau so breit. Jeder Turm hat in jeder
    Entfernung alle Details (Vorgabe des Besitzers: nichts baut sich in
    Stücken auf, kein zeitliches Einblenden); nur die Helligkeit ändert
    sich. Damit kein Turm auf halber Strecke auftaucht, werden jenseits der
    Reichweite des Spiels (3C00h) alle Gebäude des Durchgangs gezeichnet,
    auch die, die das Spiel noch nicht eingerichtet hat; innerhalb gilt die
    Regel des Spiels. Die fernsten sind dunkel und
    werden linear mit der Nähe heller (Vorgabe des Besitzers, damit die
    hinteren die vorderen nicht überdecken). Der Geschützturm dreht sich
    suchend hin und her und richtet sich auf den Spieler, sobald der in
    Reichweite kommt (nur Bild: geschossen wird nach der Regel des
    Automaten). Die Türme des Spiels werden dafür aus dem Bild genommen. Am
    Spiel selbst ändert sich nichts; der Laser trifft nach der Regel des
    Automaten (ein Umweg über `enh_towers` für die echte Perspektive ist
    wieder weg, seit die Türme die Größe des Automaten haben: mit ihm waren
    die Hüte nicht mehr zu treffen).
  * `enh_dogfights`: klein in der Ferne jagen TIE Fighter einen X-Wing
    über den Himmel, alle 13 Sekunden, mal zwei Schiffe, mal drei. Der TIE
    Fighter ist das Modell des ROMs, Linie für Linie. Einen X-Wing hat das
    ROM nicht (das eigene Schiff sieht man nie von außen): er ist aus elf
    Linien selbst gezeichnet.
  Alles wird aus dem Zustand des Spiels gezeichnet (Ort und Lage des
  Spielers, Karte der Gebäude und ihr Zustand) mit der Perspektive des
  Spiels; die Rechnung stimmt mit der Math Box auf den Punkt überein
  (gemessen). Mit Occlusion verdecken die neuen Türme, was dahinter liegt.
  Geprüft an Standbildern; mit allen vier aus stimmen die Aufnahmen weiter.
* **Eigenes Testmenü** (`src/platform/menu.c`, 2026-10-13): Der
  Selbsttest-Knopf (`p1_button6`), am Gerät die Tab-Taste, öffnet es, das
  Spiel steht solange. Seit 2026-10-13 in Seiten (Graphics, Sound, Controls
  and Screen, Game Options; dazu Arcade Operator Screens und Save and Exit
  auf der Hauptseite), weil eine Seite mit 23 Zeilen zu klein geschrieben
  war. Zeilen:
  Enhanced Settings (Occlusion, Surface Plating, Surface Stars, Detailed
  Towers, Distant Dogfights, Trench Clipping, Yellow Logo, Replaced Sounds,
  Yoke Auto Center, Smooth Motion, Frame Pace), Game Options (die acht
  Optionen des Automaten),
  Sound Test (alle 59 Anforderungen aus `docs/SOUNDS.md` mit Nummer und
  Name), Screen Calibration, Arcade Operator Screens (die Statistik- und
  Optionsseiten des Automatenprogramms; der Knopf führt von dort zurück),
  Save and Exit. Joch wählt und ändert, Feuer löst aus, der Selbsttest-Knopf
  speichert in `save/swenhanced.ini` und schließt. Kompiliert für PC und Pi,
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
* **Fadenkreuz zentriert** ab dem ersten Bild (`yoke_autocenter`). Seit
  2026-10-13 gehört dazu ein Auto-Centering (`src/platform/controls.c`):
  Die Ruhelage des Jochs gilt als Mitte. Sie wird beim Start aus der
  ersten Messung gelernt und nachgeführt, wenn das Joch im Attract-Modus
  rund zwei Sekunden still liegt; die Endanschläge bleiben, wo sie sind.
  Die gelernte Ruhelage wird sofort in der Einstellungsdatei abgelegt
  (`yoke_center_x`, `yoke_center_y`) und gilt ab dem nächsten Start vom
  ersten Bild an. Im Testmenü schaltbar (YOKE AUTO CENTER). Am Automaten ungeprüft: Ob
  sein Joch still genug liegt (Schwelle 1,5 % des Wegs), ist offen.
* **Sound-Austausch** (`src/sound/samples.c`, 2026-10-13, Enhanced,
  `sound_replace = 1`): Liegt in `sounds/` (oder `save/sounds/`) eine
  WAV-Datei mit dem Namen einer Anforderung (`AUDXL.wav`, `PMTH5.wav`,
  `SPKRED.wav` ...), wird sie statt des Chip-Sounds gespielt; alle anderen
  klingen wie bisher. Regeln und Format in `docs/SOUNDS.md`. Geprüft:
  `tests/test_samples.c` und ein Spiel mit zwei Testtönen (der Ton für
  `AUDXL` bei jedem Schuss, der für `PMTH5` beim Spielstart, gemessen im
  Spektrum). Es gibt noch **keine echten Ersatz-Sounds**. Als Ausgangspunkt
  schreibt `tools/swsound` jeden Sound der Platine als WAV (Musik bis der
  Musiktreiber fertig ist, Sprache bis der Sprachchip fertig ist, Effekte
  bis zur Stille), jeden in den Ordner seiner Art: `sounds/voice/` (22
  Sätze), `sounds/music/` (11 Stücke), `sounds/effects/` (18), `sounds/test/`
  (4, der Selbsttest der Platine). Alle 55 liegen am PC (nicht im Repo,
  nicht am Automaten); `SPKI`, `SPKSKP`, `AUDPBZ` und `AUDTCZ` machen keinen
  Ton und bekommen keine Datei. Der Lader sucht eine Datei in `sounds/`
  selbst und im Ordner ihrer Art. Folge am PC: Weil die Originale jetzt in
  `sounds/` liegen, spielt die PC-Fassung sie als "Ersatz" ab (klingt
  gleich; `sound_replace = 0` schaltet es ab). Offen: `AUDRS` (R2, kurzes
  Ja) beginnt mit gut zwei Sekunden Stille, die in der Datei mit drin sind;
  ob das die Platine so macht oder am Export liegt, ist nicht untersucht.
* **Schriftzug gelb** (`logo_yellow = 1`, Standard, Enhanced): das STAR-WARS-
  Logo im Attract-Modus in Gelb statt im Blau des Automaten (`enhance.c`
  färbt die Linien um). Im Testmenü schaltbar (YELLOW LOGO).
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

## Durchsicht der Darstellung (2026-10-13)

Auf Wunsch des Besitzers die ganze Bildkette durchgesehen (Occlusion,
Objekte, Erweiterungen, Zeichnen der Vektoren). Werkzeug: `swvideo --check`
läuft ein Spiel durch und prüft jedes Bild (Zahlen, die keine sind;
Linien ganz außerhalb des Schirms; Linien von Dingen ohne Entfernung;
Rechenzeit pro Bild je Phase). Befund über 9000 Bilder (Attract, Weltraum,
Oberfläche, Graben, Welle 5):

* Keine ungültige Zahl. Rechenzeit am PC: Weltraum 0,27 ms, Oberfläche
  0,55 ms, Graben 0,29 ms, Schriftzug-Seite 0,06 ms pro Bild (die ganze
  Kette: Zwischenbild, Erweiterungen, Occlusion). Am Pi 5 ist das grob das
  Dreifache; die Platine braucht je Bild deutlich länger (2 bis 3,5 ms laut
  Log). Keine Optimierung nötig.
* Linien ganz außerhalb des Schirms gibt es viele (Kanonen des X-Wing,
  Feuerbälle, die zurückweichende Titelschrift): Das SDK schneidet sie weg,
  bevor sie zur Platine gehen (`vp_dvg.cpp`, Cohen-Sutherland), sie kosten
  dort nichts.
* **Fehler gefunden und behoben**: Feuerbälle, die auf den Schilden glühen
  (GLOWVW), hatten keine Entfernung (1/X = 0) und galten der Occlusion als
  hinter allem; ein TIE oder Turm dahinter schnitt sie weg. Jetzt stehen
  sie vor allem (Entfernung 65535, und die Occlusion lässt solche Linien
  ganz). Alle anderen Dinge, die verdeckt werden können, haben ihre
  Entfernung.
* **Entfernt**: `sort_strokes` (Code und Einstellung), weil die Sortierung
  das Bild auf der Röhre nachweislich wackeln lässt; `smooth` 1 und 2 aus
  dem Menü (ein Frame zu spät bzw. Überschießen), die Datei nimmt die Werte
  noch an.
* Zeichnen der Vektoren: Striche werden in der Reihenfolge des Spiels
  gezeichnet, zusammenhängende Linien gleicher Farbe als ein Zug. Eine
  Sortierung, die von Bild zu Bild gleich bleibt, ist nicht gebaut: Jede
  Umordnung ändert, woher der Strahl kommt.
* Nicht gefunden, aber möglich: Die Flächen der Schiffe sind Ringe von
  Linien, die bis 20 % aus der Ebene liegen dürfen; bei stark gekrümmten
  Ringen (Rumpf des TIE) kann die angenäherte Ebene eigene Linien knapp
  dahinter abschneiden. Auf Standbildern nicht aufgefallen.
* **Neu im Testmenü**: die Optionen des Spiels (Münzwert, Münzmechaniken,
  Bonus-Münzen, Start-Schilde, Schwierigkeit, Bonus-Schilde, Musik im
  Attract) direkt aus dem NOVRAM, mit den Prüfsummen und dem Backup-Bereich
  wie die Optionsseite des Automaten (`sw_option_get/set`,
  `tests/test_options.c`). Die DIP-Schalter sind dem Programm nur Vorgabe
  beim Zurücksetzen; die Resets und der NOVRAM-Test bleiben auf den
  Operator-Seiten.

## Schriftzug wackelte am oberen Rand des T (2026-10-13)

Der Schriftzug im Attract-Modus fliegt davon (er wird jedes Frame kleiner
und rückt nach oben). Die Occlusion schnitt seine Seitenkanten dort weg, wo
sie hinter der Vorderseite liegen, auf einem Raster von 1 Zelle pro Einheit,
das für jedes Bild neu über den Schriftzug gelegt wurde: Je nachdem, wie
der schrumpfende Schriftzug im Raster lag, fielen kurze Kantenstücke (am
T oben) mal weg, mal nicht. Jetzt wird die Form des Schriftzugs einmal in
seinen eigenen Koordinaten gebaut (800x480 Zellen über seiner Box, Schlüssel:
die Helligkeiten seiner Linien) und jedes Bild nur noch hineingerechnet;
beim Schrumpfen ändert sich damit nichts am Schnitt. Nebenbei kostet die
Seite am PC 0,02 statt 0,06 ms pro Bild (die 22 ms am Pi sind damit auch
weg). Gemessen: Länge der Schriftzug-Linien je Bild fällt mit und ohne
Occlusion gleichmäßig (Differenz nimmt stetig ab, kein Springen).

Das half nicht: Der Besitzer sah weiter nur den obersten Strich des T
wackeln. Zweiter Befund: Dieser Strich ist die erste Linie des Schriftzugs
in der Displayliste, und davor kommen die Sterne. Der Strahl springt also
dunkel von einem Stern, der jedes Bild woanders steht, zum Anfang dieses
Strichs; auf der DVG-Platine zittert der Anfang der ersten Linie nach so
einem Sprung mit (gemessen: die Linie davor war von Bild zu Bild eine
andere, die Linie selbst in der Rechnung glatt). Jetzt zeichnet
`sw_picture_lines` die Sterne jedes Bildes zuletzt (Punkte stört der
Sprung nicht); die Reihenfolge aller anderen Linien bleibt die des Spiels,
nichts wird umsortiert. Damit kommt der Strahl zum T immer vom selben Ort.
Am Gerät ungeprüft.

## Größe der Türme, Helligkeit (2026-10-13)

* **Türme wirkten gedrungen, das Original weitläufiger** (Besitzer). Grund:
  Der Automat zeichnet ein Gebäude nicht in reiner Perspektive. GDVIEW legt
  über die Perspektive noch einen linearen Maßstab mit der Entfernung
  (`M.GDSC`: Linearfaktor XP<<2, also Größe mal 1 - XP/4000h), um den Fuß
  des Gebäudes herum. Ein fernes Gebäude ist damit viel kleiner als die
  Perspektive es machte, und die Szene wirkt weit. Die erweiterten Türme
  bekommen jetzt genau diesen Faktor (`sw_enhance.shrink`, um den Fuß).
  Standbilder Original gegen erweitert: gleiche Größe und gleicher
  Abstand. Folge: Die Türme wachsen beim Näherkommen wieder wie am Automaten
  (das ist derselbe Mechanismus); die Helligkeit nimmt weiter mit der Nähe
  zu.
* **Gesamthelligkeit unter MAME am Gerät** (Besitzer). Die Zeichenroutinen
  des Spiels benutzen als normale Helligkeit das COLOR-Byte 80h; nach dem
  ZZ-Netzwerk und der Kurve `floor + (1 - floor) * (i/255)^gamma` landete
  eine normale Linie bei 56 %. `brightness` darf jetzt bis 2,0 gehen, ab
  Werk 1,7: eine normale Linie bei 95 %, die hellen (FFh, Treffer, Blitze)
  am Anschlag. Wie AdvanceMAME die Intensität auf die DVG abbildet, ist
  nicht nachgesehen; der Wert ist eine Annahme und am Gerät nachzustellen
  (Testmenü, Screen Calibration, vierter Modus, Joch).

## Türme von weiter her, eingeblendet (2026-10-13)

Mit der Größe des Automaten wollte der Besitzer noch einmal weiter nach
hinten sehen und die Türme sauber einblenden. Jetzt: Die Türme werden ab
6000h voraus gezeichnet (der Automat ab 3C00h), dunkel und klein. Die Größe
folgt der Formel des Automaten (1 - XP/4000h), nach unten auf 8 % begrenzt
(die Formel endet bei 4000h bei 0); die Helligkeit steigt linear von 0 bei
6000h auf voll bei 2500. Damit kommt nichts schlagartig: ein Turm erscheint
als dunkler kleiner Strich am Horizont und wächst und hellt sich auf.
Innerhalb der Reichweite des Automaten gilt dessen Regel (nur eingerichtete
Gebäude); jenseits werden alle Gebäude des Durchgangs gezeichnet, und die
des nächsten Durchgangs nur, wenn sie bei seinem Beginn noch jenseits der
Reichweite liegen (sonst kämen sie auf halber Strecke hoch). Die Karte
wiederholt sich alle 8000h, jedes Gebäude wird auf dieser und der nächsten
Runde betrachtet. Standbilder: am Start der Oberfläche nichts neben dem
Spieler, am Horizont die kleinen dunklen Türme.

## Filmmusik im Spiel, Ton in Stereo (2026-10-15, 0.3.18)

* Die fünf Stücke des Besitzers in `sounds/music` (PMTH5, PMTHB, PMDAR, PM4TH,
  PMCNT) ersetzen die der Platine, so wie er sie abgelegt hat (nur
  umgewandelt: 16 kHz, ADPCM, −30 dB). Privat: `package/sounds/music` ist in
  der `.gitignore`, die Stücke kommen über `deploy.sh` und die vpk auf den
  Automaten, nicht ins Repo; fehlen sie, lässt `build.py` sie aus.
* **Stereo** ("bitte nicht mono"): Die Tonausgabe ist jetzt stereo
  (Ringpuffer der Platine in Frames links/rechts, SDL mit 2 Kanälen). Eine
  Stereo-Aufnahme behält ihre Seiten, Mono-Aufnahmen und der Ton der Platine
  liegen in der Mitte. Die Musik wird in Stereo gebaut, Effekte und Sprache
  bleiben mono. `sw_sndboard_read` liefert für Werkzeuge und Tests weiter den
  Mono-Mix.
* **Fehler gefunden:** libsndfile schreibt bei Stereo-IMA-ADPCM die halbe
  Frame-Zahl ins fact-Feld; der Lader nahm sie und spielte nur die erste
  Hälfte. Er nimmt "fact" jetzt nur, wo es zu den Blöcken passt. Alle
  Dateien (auch stereo) sind bitgenau wie libsndfile.

## Nase im Dogfight, Ansicht beim Spielstart, Filmmusik (2026-10-15, 0.3.17)

* **Nase im Dogfight durchsichtig:** Der Dogfight schnitt alle Linien seiner
  Liste am Bildrand ab (wegen des Todessterns), auch Nase und Kanonen.
  render/solid schließt deren Umriss mit dem Bildrand aber nur, wo Linien
  über ihn hinausgehen; so blieb die Nase unten offen und deckte nicht ab.
  Nase und Kanonen werden jetzt nicht mehr abgeschnitten (wie im Spiel).
  Geprüft mit einem TIE hinter der Nase: vorher 6 bis 8 seiner Linien in
  ihrer Mitte sichtbar, jetzt keine.
* **Ansicht beim Spielstart:** Jedes Spiel und jeder Dogfight beginnt in der
  Arcade-Ansicht, gleich was zuletzt mit Start gewählt war; Start schaltet
  wie bisher weiter.
* **Filmmusik (privat, nicht im Repo, nicht im Paket):** Aus Soundtrack-
  Ausschnitten des Besitzers (Desktop) geschnitten, nach dem Arcade-Stück
  gefunden (Melodieband-Chroma, Tempo und Tonart gesucht, Einsätze
  verglichen), mit hartem Schnitt wie im Original und Nachhall danach:
  PMTH5 (Film 7,00–16,19 s, 1,18× schneller, 7,79 s; das Spiel lässt bis
  PMTHB 8 s), PMTHB (16,19–26,08 s, 1,06×, 9,33 s; bis PMDES 10 s), PMCNT
  (Cantina 15,00–43,13 s, Originaltempo, 28,13 s, seine Achtel liegen genau
  auf denen des Arcade-Stücks). Die Werkzeuge liegen noch im Scratchpad;
  wenn die Stücke bleiben, kommen sie nach `tools/` und die Stücke als
  lokaler Musikordner neben die Sounds.

## Sounds aus den Filmen und der SFX-Sammlung (2026-10-15)

Auftrag des Besitzers: die neuen Filmstimmen (`sounds/voice`, 21 statt 22),
passende Originalsamples aus der SW Ultimate SFX Collection für alles, ein
eigenes Triebwerk (innen und außen verschieden), Multisamples, Lautstärken,
platzsparendes Format, höchstens 16 kHz.

* **Fehlende Stimme:** SPKR2N (R2 schimpft, Abluftschacht verfehlt). Es ist
  kein Satz aus dem Film, sondern R2 selbst; genommen ist "R2 Freakout 1A"
  der Sammlung. Das einzelne "Always" (SPKALW) ist da: Es ist genau das Ende
  von SPKFOA (Kreuzkorrelation 0,92 bei 1,56 s), es muss nicht geschnitten
  werden.
* **Multisamples:** `NAME.wav`, `NAME_2.wav` ... `NAME_12.wav`, zufällig,
  nie zweimal dieselbe hintereinander; gleiche Effekte schneiden sich nicht
  mehr ab (12 Stimmen statt 8). TIE-Vorbeiflug 8 + 5 Varianten, TIE-Kanone 4,
  X-Wing-Laser 6, Explosionen 8, Türme 5, Torpedos 4, R2 je 2 bis 5.
* **Triebwerk** (ENGINE SOUND, `enh_engine`, an): XWENG von innen (Arcade-
  und Cockpitansicht), XWEXT von außen, Schleifen aus den X-Wing-Aufnahmen der
  Sammlung; im Dogfight lauter und schneller mit der Geschwindigkeit; beim
  Wechsel der Ansicht überblendet; aus im Menü, beim Verlust des Schiffs,
  außerhalb des Flugs.
* **Format:** 16 kHz, Mono (die Platine gibt Mono aus), IMA ADPCM (4 Bit);
  105 Dateien, 1,4 MB (als 16-Bit-PCM 5,6 MB, die Quellen 1,4 GB). Der Lader
  liest IMA ADPCM jetzt selbst, bitgenau wie libsndfile (alle 105 geprüft).
* **Lautstärken:** an die Platine angelehnt (Sprache der Platine −29 dB,
  Effekte −37, Musik −32), die neuen Stimmen streuten von −19 bis −31 dB und
  stehen jetzt alle bei −27. Ein Dogfight-Film (45 s) übersteuert nicht
  (Spitze 0,33).
* Auswahl und Pegel stehen in `tools/swsamples/samples.txt`,
  `tools/swsamples/build.py` baut `package/sounds`. Die Dateien liegen im
  Paket und neben dem Programm (CMake kopiert sie), `deploy.sh` und `pack.sh`
  nehmen sie mit.
* **Nachgebessert (0.3.15), nach dem ersten Hören am Automaten:**
  Ducking: unter Sprache alles andere −3 dB (auch Musik und Effekte der
  Platine), unter einer großen Explosion oder einem Crash der Rest −1,5 dB
  (`samples.c`, `sndboard.c`, getestet). Die roten Kanonen (AUDTL) klangen
  wie Vorbeiflüge: "DS Turret" schwillt nach dem Einsatz auf das 2,4- bis
  2,9-Fache an. Jetzt Turbolaser 9, 3R, 1, Heavy Cannon 10 und 9, Heavy
  Blaster 2A, alle mit hartem Einsatz und unter 0,5 s abklingend. Sprache:
  Alle 22 hörbaren Sätze des Originals sind belegt. Bei Vaders vier Sätzen
  fehlte sein Atmen vor und nach dem Satz, das das Original hat; es ist
  jetzt aus der Sammlung davor- und dahintergesetzt (Inhale/Exhale, 9 dB
  leiser), die Sätze sind damit 3 bis 5 s lang wie im Original.
* **Falcon (0.3.16):** Der Überflug hat einen eigenen Vorbeiflug (FALC
  Flyby 10, 2, 3, 4B, 13; so geschnitten, dass jeder 0,85 s nach Beginn am
  lautesten ist), und "Yahoo, you're all clear, kid" kommt genau, wenn er über
  dem Spieler ist. Sein Flug wird 1,3 s vorher geplant (aus der wirklichen
  Annäherung an den Abluftschacht; die Geschwindigkeit des Spiels springt von
  Bild zu Bild), Bild und Ton halten sich daran: im Film über dem Spieler bei
  132,40 s, der Vorbeiflug am lautesten bei 132,38 s. Das eigene SPKYAU des
  Spiels (im Graben zu seiner Zeit) wird dann nicht gespielt. Die Explosion
  des Todessterns ist 4,5 dB leiser (−28,5 dB).
* **Nicht angehört:** Die Auswahl ist nach Namen und Messwerten (Länge,
  Einzelschuss, Gleichmäßigkeit, Helligkeit) getroffen. Ob jede Datei passt,
  muss am Automaten gehört werden. Die Musik bleibt die der Platine.

## Dogfight: Radar, Anleitung, eigene Bestenliste, Ende; Attract still (2026-10-14)

Rückmeldung des Besitzers vom Automaten (0.3.10) und was daraus wurde (0.3.11):

* **X-Wings waren unsichtbar:** Das ROM-Modell TDSXW liefert hier keine
  Kanten (2 Punkte, 0 Kanten; das Spiel zeichnet sein X-Wing nur aus dem
  Cockpit). Deshalb schossen die Jagden im Hintergrund "auf niemanden", und
  die Flügelmänner fehlten. Der Dogfight hat jetzt ein eigenes X-Wing
  (`draw_xwing`, nach dem der Außenansicht, mit Flächen für die Occlusion,
  Größe 420).
* **Radar:** eine Scheibe mittig oben unter SHIELD. Vorn liegt in der Mitte,
  hinten am Rand, oben oben. Gegner rot (Vader gelb, nahe hell),
  Flügelmänner türkis; ein innerer Ring für "quer".
* **Anleitung mit dem Joch** (wie Time Pilot 3D, `F:\Time Pilot 3D\src\attract.c`
  draw_howto): Nach der Wahl des Dogfights kommt HOW TO FLY. Das Joch (Kasten mit
  Atari-Logo zum Spieler, Kegel, Griffe, Abzüge vorn, Daumentasten hinten)
  dreht, nickt, zeigt jede Taste blinkend mit ihrer Aufgabe (rechter
  Abzug Laser, linker Torpedos, rechter Daumen schneller, linker langsamer),
  dann das Rollen und START. Daneben das X-Wing, das tut, was das Joch tut.
  32 s, oder ein Zug am Abzug (nach 1,5 s) beginnt. `swvideo --howto`.
* **Eigene Bestenliste:** zehn Plätze (Initialen, Punkte, Welle) in
  `save/dogfight.hi`, anfangs LUK 30000 bis GLD 3000. Nach GAME OVER (4 s)
  gibt ein neuer bester Pilot seine Initialen ein (Joch zur Seite: Buchstabe,
  Abzug: weiter, 30 s), dann zeigt DOGFIGHT BEST PILOTS die Liste 7 s.
* **Übergang in den Attract:** Danach geht es durch das Spielende des
  Originals (PHSEGM: "Remember...", "The Force will be with you", Buchhaltung,
  Bens Thema, BNR) statt direkt zur Titelseite. Die eigene Ansage des
  Dogfights beim Game Over fällt dafür weg.
* **Nachbesserung (0.3.12):** In der Anleitung schießt das X-Wing beim
  rechten Abzug Laser (türkise Bolzen, paarweise im Wechsel, sie treffen sich
  vorn) und beim linken zwei Torpedos (das Funkelbild der Schüsse). Beim
  Schnellerwerden klappen die Flügel zusammen, und die Triebwerke bekommen
  blaues Feuer mit einem wachsenden Leuchtring. Beim Langsamerwerden öffnen
  sich die Flügel wieder. Es wird genau von hinten gesehen (die Nase entlang
  der Blicklinie).
* **Nicht mehr ins Gesicht (0.3.12):** Die Flügelmänner fliegen weit außen
  und hinten (2600 seitlich, 1800 bis 2600 hinter dem Spieler) und weichen
  unter 2200 aus. Gegner drehen schon unter 2200 ab (unter 3400, wenn sie
  vorbei sind) und werden unter 1600 vom Spieler weggelenkt.
* **Flügel und Explosion (0.3.13):** Die S-Foils der Außenansicht klappten
  im Dogfight nie, weil ihre Animation auf der Uhr des Spiels lief und die
  im Dogfight steht. Der Renderer bekommt dort jetzt die Uhr des Dogfights.
  Flügel und Triebwerksfeuer folgen der wirklichen Geschwindigkeit
  (`sw_dogfight_thrust`), nicht den gehaltenen Tasten. Neu ist
  **SHIP EXPLOSION** (Grafikseite, `enh_blast`, an): Geht das X-Wing
  verloren, blitzt es weiß auf, Strahlen fliegen weiß, gelb und rot aus ihm
  heraus (2,6 s), und von außen fliegen seine Teile auseinander. Im
  Dogfight mit Crash, Explosion und R2s Abgesang, die Nase verschwindet. Auf
  den Flügen des Originals geht es los, sobald die Schilde weg sind
  (S.GAS 80; die Töne bleiben die des Spiels). **Im Original noch nicht im
  Spiel gesehen.** `swvideo --lose-at S` lässt das Schiff im Dogfight bei S
  Sekunden verloren gehen.
* **Attract still:** Ist MUSIC IN ATTRACT auf NO, ist auch der Demo-Flug stumm
  (Lautstärke der Soundplatine 0, solange er läuft). Vorher waren seine
  Schüsse, Musik und Ansagen zu hören.

## Dogfight: Originalschüsse, Protonentorpedos, große Schlacht (2026-10-14)

Rückmeldung des Besitzers: falsche Schüsse, kein Torpedo, Gegner greifen zu
wenig an, es fehlt das Gefühl einer großen Schlacht. Nur lokal gebaut und
gefilmt, **nicht auf dem Automaten**.

* **VG-RAM wie im Original:** Der Dogfight hat jetzt das, was der Interrupt
  bei jedem Neustart des Vektorgenerators unten in den VG-RAM schreibt
  (VRLZF-Farben rotiert, VRLZS, VRGNB/VRGNX jedes vierte Mal, VRGNT, VRFLS,
  VRMFL, Splash-Quadranten über `sw_splash_into`), mit 41 Hz. Dazu kommt
  das "Fake VG ROM" (DOWNVG, ab 2800h), in dem die Funkelbilder der Schüsse
  liegen. Die Liste beginnt bei VGVARB (70h) und endet unter 2800h.
* **Laser wie VWLAZ/LAZVW:** Je Schuss das andere Kanonenpaar, 8 Spielframes
  (20,5 Hz) lang, in halbierenden Schritten mit den wandernden Farben aus
  TLZFR. Bei Treffer steht der Strahl 4 Frames türkis und verblassend auf
  dem Ziel, dazu der Splash VJLZS.
* **Feuerbälle wie GN1DRW:** Maßstab aus der Entfernung, Funkelbild VJGNB.
  Abgeschossen bleiben sie stehen und lösen sich lila auf (VJGNX). Treffen
  sie die Schilde, glühen sie dort weiß (GLOWVW).
* **Protonentorpedos (linker Trigger):** laden in 7 s (Balken TORPEDO
  links oben, blinkt türkis, wenn bereit; R2 piepst). Es sind zwei Kugeln
  wie die Feuerbälle, jede verfolgt einen anderen Gegner (den nächsten am
  Fadenkreuz) und zerstört ihn mit einem Treffer. Fällt ihr Ziel, sucht
  sie ein neues.
* **Angriffslustiger:** Wellen 4 + 2 × Welle Gegner (höchstens 30),
  Interceptors ab Welle 2, bis zu 7 + Welle/2 gleichzeitig (höchstens 11),
  Interceptors auch paarweise. Feuer in Salven (TIE 2, Interceptor 3,
  Vader 4, 0,22 s Abstand), schon ab 10000 Entfernung, dazwischen
  1,6/1,3/1,0 s. Sie drehen erst unter 1100 ab und kommen nach 1–2 s wieder.
* **Schlacht:** drei Flügelmänner (X-Wings) fliegen mit, jagen die Gegner und
  feuern rote Bolzen (jeder zweite trifft, kein Punkt für den Spieler). Ein
  Viertel der Gegner jagt sie mit grünen Bolzen. Nach drei Treffern
  explodiert ein Flügelmann und kommt nach 14 s an der Wand wieder.
  Im Hintergrund laufen drei Verfolgungsjagden gleichzeitig, und alle 1–3 s
  explodiert in der Ferne ein Schiff. Explosionen in der Nähe blitzen mit
  dem Funkelbild auf.
* **Zurückgenommen nach dem ersten Spielen am Automaten** ("die schießen
  sich alle gegenseitig ab, völlig überladen mit den Schüssen", 0.3.10):
  Die Flügelmänner fliegen nur noch an den Flügeln des Spielers mit und
  kämpfen nicht, kein Gegner jagt sie. Gegner feuern einzeln (TIE 1,
  Interceptor 2, Vader 3 je Salve), alle 2,6/2,2/1,8 s, nur bis 8500 und
  genau vor der Nase. Höchstens 3 + Welle/3 Feuerbälle fliegen zugleich
  (höchstens 6), höchstens 5 + Welle/3 Gegner (höchstens 8). Explosionen in
  der Ferne nur alle 3–7 s.
* Offen: Sichtbarkeit der Laser am Automaten prüfen; Bildrate im Dogfight
  am Automaten messen.

## Dogfight: mehr Tempo, Formationen, Abprallen, neuer Interceptor (2026-10-14)

Rückmeldungen des Besitzers und was daraus wurde:

* **"Wirkt sehr langsam":** eigenes Schiff 700/1350/2300 statt 450/900/1500,
  Wenden 1,75/1,5/3,3 rad/s statt 1,3/1,1/2,6, Gegner gut 40 % schneller
  (TIE 1350, Interceptor 1650, Vader 1500) und wendiger, Feuerbälle 2300
  statt 1500, Laser alle 0,18 s und bis 11000; Box 9000.
* **"Gegner kleben aufeinander":** Sie halten 900 Abstand voneinander
  (Flügelleute ihrer Formation 280), unter 260 werden sie auseinander
  geschoben.
* **Formationen:** TIE Fighter kommen oft zu dritt in V (Flügelleute 420
  seitlich und 380 hinter dem Anführer), fliegen gemeinsam an, schießen
  auch in Formation und lösen sich auf, wenn der Anführer unter 2600 am
  Schiff ist oder fällt.
* **Kollision:** Schiff und Gegner prallen voneinander ab (900 Stoß, klingt
  ab), beide nehmen Schaden: ein Schild, ein Treffer am Gegner; ein Ruck
  in Gier und Nicken. Vorher explodierte der Gegner sofort.
* **Todesstern größer:** Maßstab der Stempel VGSCAL+200h statt +340h (groß, vom Besitzer so gewollt).
* **Interceptor neu** ("sieht gegen die anderen schlecht aus"; Vorlage ein
  Foto des Modells): jetzt aus den Teilen des ROM-TIE (Kugel aus zwei
  Achtecken, Pylonenringe, Naben) und eigenen Flügeln im selben Stil:
  lange schmale Dolche mit tiefer Kerbe bis zur Nabe, Ecken 62 nach innen
  geknickt, Speichen von der Nabe zu jeder Ecke, innerer Rahmen; Flächen
  für die Occlusion (Flügel in drei, Kugel in vier Stücken plus Seiten).
* **Weitläufiger** ("wirkt gedrungen, sehr nah"): die Modelle in voller
  ROM-Größe (ein Versuch mit 60 % war falsch verstanden: die Größe passte,
  sie hingen nur aufeinander). Gegner halten 1800 Abstand voneinander
  (Flügelleute 600), unter 520 werden sie auseinandergeschoben; Box
  11000, Gegner erscheinen ab 7000 Abstand, drehen unter 1800 ab, schießen
  bis 8500; Formationen 850 seitlich und 700 dahinter, lösen sich unter
  3500; Laser bis 14000.
* **Todesstern mit mehr Details** ("paar mehr Details"): sechs Breitengrade
  parallel zum Graben, vier gekrümmte Längengrade, 40 Oberflächenplatten (auf
  der Kugel gekrümmt, nicht auf der Schüssel), alles von derselben Sonne
  beleuchtet; rund 250 Linien mehr für den Strahl (Bildrate am Gerät prüfen).
  Die Kugel wird in quadratischen Einheiten gerechnet: die Einheiten der
  Linien sind es nicht (255 breit, 303 hoch auf einer 4:3-Röhre, Faktor
  1,58), und die Stempel des ROMs sitzen nicht mittig auf dem Punkt, an den
  man sie setzt; Mittelpunkt und Radius kommen deshalb aus einem Kreis durch
  die Punkte des Umrisses (kleinste Quadrate).
* **Todesstern in Filmfarben** (Vorschläge 1 bis 3 des 2026-10-14, vom Besitzer
  gewählt): nach dem Lauf der Liste umgefärbt: Umriss und Platten gedimmtes
  Weiß (Grau), Graben hell, Schüssel grau mit hellem Rand, ihre Speichen grün
  (Superlaser, glüht immer etwas); beleuchtet von einer im Raum festen Sonne
  (von links oben, wie man ihn zuerst sieht): helle und dunkle Seite mit
  weichem Übergang, aus der Lage jeder Linie auf der Kugel. Speichen erkannt
  an ihrer Länge (der Rand besteht aus kurzen Stücken). Nicht umgesetzt
  (Vorschläge 4, 5): Stadtlichter, gelegentliches Aufladen des Superlasers.
* **Todesstern wandert sauber aus dem Bild** ("bewegt sich zu schnell raus, wirkt klein"):
  er wurde ausgeblendet, sobald sein Mittelpunkt 60 über dem Rand war, obwohl
  da noch die halbe Scheibe (Radius rund 100) im Bild lag. Jetzt bleibt er,
  solange etwas von ihm zu sehen ist; alle Linien des Dogfights werden am
  Bildrand abgeschnitten (die Listen des Spiels reichen nie darüber, die
  Ausgabe schneidet nicht).
* **Todesstern verdeckt Sterne:** Nach dem Lauf der Liste wird seine Scheibe
  aus seinen eigenen Linien bestimmt, Hintergrundsterne darin fallen weg
  (die nahen Staubpunkte bleiben, sie liegen davor). Gilt auch ohne Occlusion.
* `swvideo --dogfight --wave N` beginnt bei Welle N; `--wave -1..-3`
  zeigt einen Gegner allein, langsam drehend (zum Ansehen der Modelle).

## Dogfight: Fadenkreuz geht mit dem Joch (2026-10-14)

Rückmeldung: Fadenkreuz fest in der Mitte macht die Bewegungen zu nervös.
Jetzt bewegt das Joch zuerst das Fadenkreuz (sofort und weich, bis 40 nach
der Seite und 32 nach oben/unten), und das Schiff dreht, je weiter es
draußen ist, mit einer Kennlinie n * (0,3 + 0,7 |n|): nah der Mitte sanft
(kleine Korrekturen nur mit dem Fadenkreuz), am Rand voll. Laser und
Treffer gehen dorthin, wo das Fadenkreuz steht. Beim Rollen (beide
Daumentasten) und beim Umdrehen am Rand kehrt es zur Mitte zurück. Am
Gerät nicht gefühlt; Reichweite und Kennlinie sind Setzungen
(`SIGHT_REACH_X`, `SIGHT_REACH_Y` in dogfight.c).

## Dogfight, Etappe 2: Gegner, Laser, Joch direkt (2026-10-14)

* **Joch direkt** (`src/platform/yoke.c`, aus Time Pilot 3D übernommen):
  Ist ein Joystick angeschlossen, dessen Name "yoke" enthält, liest der
  Dogfight dessen Achsen (ganzer Weg) und Tasten selbst. Belegung wie das
  Joch des Besitzers in Time Pilot 3D (yoke_layout 1 2 3 4): Taste 1 rechter
  Trigger = Laser, 2 linker Trigger = Torpedo (Etappe 3), 3 rechter Daumen =
  schneller, 4 linker Daumen = langsamer. Ohne Joch (PC) wie bisher über
  `p1_button1..4` und die Achsen der Steuerung. Joch zurückziehen = Nase hoch.
  Am Gerät nicht geprüft; noch nicht im Testmenü einstellbar.
* **Gegner** (die Modelle des ROMs, wie im Spiel gezeichnet und mit
  Modellnummer, Punkten und ihren Raumpunkten markiert, so dass
  render/solid sie massiv macht): TIE Fighter (`TDSTIE`, 1 Treffer, 1000),
  TIE Interceptor (eigenes Modell: Kugel, Pylonen, je Seite vier nach vorn
  geknickte Dolchflügel; eigene Flächen für die Occlusion; 2 Treffer, 1500),
  Vaders TIE (`TDSRTH`, 3 Treffer, 2000; jede fünfte Welle, mit seinem Thema
  und "I have you now"). Sie greifen an (mit Vorhalt auf das Schiff),
  drehen unter 1100 ab, kommen wieder, bleiben in der Box, feuern
  Feuerbälle, wenn das Schiff vor ihrer Nase ist; rammen kostet einen
  Schild. Explosion: die Trümmerteile des ROMs (`TDSTI1..3`), die taumelnd
  auseinanderfliegen.
* **Wellen:** Welle n bringt 2 + n Gegner (höchstens 14, höchstens 8
  zugleich), ab Welle 3 Interceptors; je Welle schneller, wendiger und
  schussfreudiger (bis zum 1,6- bzw. 2,5-fachen). Nach jeder Welle repariert
  R2 einen Schild ("R2 MENDS A SHIELD"). 6 Schilde; leer: "GAME OVER" mit
  Punktzahl, "The Force will be with you, always", nach 7 s zurück in den
  Attract.
* **Laser:** rechter Trigger, gehalten alle 0,22 s ein Schuss, abwechselnd
  linkes und rechtes Kanonenpaar, sichtbar 0,15 s von den Kanonen zum
  Fadenkreuz (blau, dann türkis). Getroffen wird, was unter dem Fadenkreuz
  am nächsten liegt (bis 9000): Gegner oder Feuerball (33 Punkte).
* **Feuerbälle:** eigene Funken (acht Strahlen weiß/rot, drehend und
  flackernd, nach Entfernung groß). Das Sprite des ROMs (VJGNB) holt sein
  Funkeln aus Mustern, die das Spiel jedes Bild in den VG-RAM schreibt; in
  der eigenen Liste ergab es weiße Klumpen und brach die Liste ab.
* **Anzeige:** SCORE oben links, Schilde in der Mitte (rot beim Treffer,
  türkis bei der Reparatur), WAVE oben rechts, Schub darunter; "WAVE n"
  bei jeder Welle, "DARTH VADER" in seiner. Ist kein Gegner im Bild, zeigt ein
  roter Pfeil um das Fadenkreuz zum nächsten.
* **Ton:** die Platine selbst, angefordert wie vom Spiel (Warteschlange,
  eine Anforderung je Bild): Laser, TIE-Kanone, Vorbeiflug, Explosion,
  Schildtreffer, Feuerball getroffen, R2, Sprache ("Red Five standing by",
  "I'm hit, but not bad", "R2, try and increase the power"), Hauptthema zu
  Beginn.
* **Hintergrund:** alle 19 s eine Jagd in der Ferne (26000 weg, 11 s quer
  über den Himmel): mal TIEs hinter einem X-Wing (`TDSXW`, grüne Schüsse),
  mal X-Wings hinter einem TIE (rote Schüsse). Nur zum Ansehen.
* `swvideo --dogfight` fliegt mit eigenem Piloten (dreht auf den nächsten
  Gegner, feuert, wenn er vor der Nase ist) und mit Ton.

Offen: Torpedos (Etappe 3), Anleitungsseite mit Joch (Etappe 4),
Highscores und Feinschliff (Etappe 5), Tastenbelegung im Testmenü; am Gerät
nichts gesehen.

## Dogfight, Etappe 1: Fliegen in der Box (2026-10-14)

Gebaut (`src/dogfight/dogfight.c`), Plan siehe nächster Abschnitt:

* **Einstieg:** Auf der Seite "Select a Death Star" steht unten rechts ein
  vierter Todesstern, "DOGFIGHT / ENDLESS" (am Platz der Tabelle TDTH des
  Originals, x 400, y -300); Fadenkreuz darauf (dieselbe Trefferprüfung wie
  PHESDS) und Feuer startet den Modus. Das Spiel von 1983 steht solange still
  (wie im Menü), die Soundplatine läuft weiter. F2 verlässt den Dogfight in
  dieser Etappe zurück in den Attract (später: Spielende durch Tod).
* **Bild:** eine eigene Anzeigeliste für den Vektorgenerator, die die
  Routinen des Vektor-ROMs aufruft: Schrift (Tabelle VGMSGA wie MESOUT),
  Nase und Kanonen (VJSTBM usw. wie VWPLAN), Fadenkreuz (VGSITE, fest in der
  Mitte des Blicks), Sterne (VGSTAR), der Todesstern (die Stempel von DTHVW,
  ohne die Stadtlichter, die in diesem Maßstab als Linie quer laufen).
  Ausgeführt von `avg` wie die Listen des Spiels, mit Tags; darum gelten
  Cockpit- und Außenansicht (render/enhance, Flag `dogfight`), Start schaltet.
* **Fliegen:** Box +-8000, Todesstern weit außerhalb bei (70000, 18000,
  9000). Joch giert (1,3 rad/s) und nickt (1,1 rad/s), weich nachgezogen;
  beide Daumentasten plus Joch zur Seite rollen (2,6 rad/s). Schub wie Time
  Pilot 3D: die gehaltene Taste zieht ihn ihre Richtung, doppelt so schnell
  jenseits der Mitte, losgelassen wandert er zurück; Tempo 450 bis 1500,
  Mitte 900, kein Strömungsabriss. Am Rand der Box dreht das Schiff selbst
  zur Mitte (1,6 rad/s, Joch ohne Wirkung), "WHERE ARE YOU GOING" blinkt,
  bis es innen ist und zur Mitte zeigt. Schubanzeige oben rechts, "DOGFIGHT"
  die ersten 4 s. Staubpunkte um das Schiff zeigen die Fahrt.
* **Cockpit im Dogfight** schwingt mit dem Kurven und Nicken wie im
  Original-Modus mit dem Fadenkreuz.
* `swvideo --dogfight` fliegt ihn selbst (ohne Ton).
* Tasten am Automaten: Laut Time Pilot 3D legt `advmame.rc` alle vier
  Joch-Tasten auf `p1_button1`; die Daumentasten (`p1_button2/3`) kommen
  dann nicht an. Time Pilot 3D liest das Joch deshalb selbst
  (`src/controls.c` dort); das muss für den Dogfight übernommen werden.
  Am Gerät nicht geprüft.

X-Wing (Außenansicht, im Spiel und im Dogfight), auf Wunsch des Besitzers:

* **Occlusion:** Die Teile des Modells verdecken einander jetzt richtig.
  Jede Fläche trägt ihre Tiefe (1/X als Ebene über den Bildschirm) und ihr
  Teil (`sw_line.own`, `hide_part`); `hide()` schneidet nur, was hinter der
  Fläche liegt, und kein Teil seine eigenen Linien. Das gilt auch für das
  Cockpit (seine Flächen liegen vor allem).
* **Triebwerke:** hinten zwei blaue Ringe (Glühen), im Dogfight beim
  Beschleunigen ein flackernder blauer Strahl je Triebwerk mit weißem Kern,
  Länge mit dem Schub.
* **S-Foils:** Im Dogfight fahren die Flügel mit dem Schub zusammen (flach
  bei vollem Schub, gut eine Sekunde), beim Langsamwerden wieder in X-Stellung;
  die Kanonenspitzen (Laserstart) wandern mit. Auf den Schienen offen.

## Geplant: Dogfight-Modus (besprochen 2026-10-14, Etappe 1 gebaut)

Idee des Besitzers: ein weiteres Level, endlos bis zum Tod. Frei im
Weltall fliegen statt auf Schienen, endlose Wellen von Gegnern, im
Hintergrund der Todesstern. Eine Box als Spielfeld: Wer an die Grenze
fliegt, wird automatisch umgedreht ("WHERE ARE YOU GOING?"). Steuerung wie
Time Pilot 3D (`F:\Time Pilot 3D`, STATUS.md dort): Joch nickt und kurvt,
Daumentasten schneller/langsamer mit Rückkehr des Schubs zur Mitte, beide
zusammen plus Joch zur Seite rollen um die Längsachse, Trigger feuern.

Entschieden: Im Cockpit und in der Original-Ansicht steht das Fadenkreuz
fest in der Bildmitte (man zielt mit der Nase, wie in Time Pilot 3D).
Der Besitzer fand die übrigen Vorschläge gut: Gegner aus dem ROM (TIE
Fighter, Vaders TIE als Boss-Welle mit seinen Sprüchen, Feuerbälle zum
Abschießen), Schilde wie im Original mit Reparatur durch R2 nach jeder
Welle, Radar und Zielpfeil, der Falcon als Helfer alle paar Wellen,
Todesstern-Countdown als Option, eigene Highscores mit Kombo, alle drei
Ansichten mit Start umschaltbar.

Weiter entschieden (2026-10-14): Einstieg als vierte Wahl neben den drei
Todessternen bei "Select a Death Star". Endlos bis zum Tod wie das
Original, die Schwierigkeit zieht mit den Wellen an. Nach der Wahl eine
Anleitungsseite wie die von Time Pilot 3D (`draw_howto` in dessen
`src/attract.c`): das Joch als Drahtmodell, diesmal mit Atari-Logo, und
daneben der X-Wing, der vormacht, was das Joch tut. Tasten: rechter
Trigger Laser; linker Trigger (vorne links) Protonentorpedos: zwei
Kugeln wie die Schüsse der Gegner, jede verfolgt einen eigenen Gegner
(zwei zugleich), danach lädt der Torpedo erst wieder auf (Ladeanzeige);
Daumentasten schneller/langsamer, beide plus Joch zur Seite rollen;
Start schaltet die Ansicht. Treffer, bis ein Gegner fällt (Laser): TIE
Fighter 1, ein neuer TIE Interceptor (Abfangjäger, eigenes Modell, nicht
im ROM) 2, Vaders TIE 3. Vorschläge noch ohne Antwort: Aufladezeit der
Torpedos etwa 8 s; ein Torpedo zerstört jeden Gegner mit einem Treffer.
Freigegeben am 2026-10-14 ("Bau mal ein"); Plan in Etappen (Fliegen
in der Box mit Todesstern; Wellen und Laser; Torpedos; Anleitungsseite;
Highscores und Feinschliff), nach jeder Bilder oder Video. Technisch: ein eigener Spielteil in C neben dem Programm von 1983
(das kennt nur Schienen), mit dessen Modellen, Sounds und Sprache und dem
Bildweg von render/ (Occlusion, Ansichten).

## Cockpit bewegt sich mit dem Schiff (2026-10-14)

Das Fadenkreuz lief über Streben und Armaturenbrett: Es reicht im Original
bis x +-112 (von +-127,5) und y -104 nach unten (gemessen, Joch in allen
Ecken), also bis in die Konsole; kein Cockpit mit sichtbaren Streben passt
darum herum. Wunsch des Besitzers: das Schiff bewegt sich mit. Jetzt folgt
das Cockpit dem Fadenkreuz zu 60 % seines Wegs und legt sich mit ihm bis
0,22 rad in die Kurve (um die Mitte der Brüstung), leicht geglättet; das
Fadenkreuz bleibt so im Fenster. Die Lage des Fadenkreuzes kommt aus seinen
Linien im Bild (vom Interrupt gezeichnet, 246-mal je Sekunde), nicht aus
dem Spielzustand (13 Hz): die Bewegung ist so glatt wie das Fadenkreuz.
Rahmen und Flächen reichen dafür weit über den Bildschirm hinaus. Spiel,
Fadenkreuz und Schüsse unverändert. Am Gerät nicht gesehen.

Bei der Wahl des Todessterns (SDS) zeichnet das Spiel Nase und Kanonen
auch; dort gilt jetzt immer die Arcade-Sicht, Cockpit und Außen erst im Flug.

## Keine Türme mehr am Ende der Oberfläche (2026-10-14)

Am Ende der Oberfläche sah man noch ferne Türme. Das Original räumt in der
fünften Runde ab (S1MVGD: GD.SEQ 5 setzt Q.KTW, und noch nicht gezeigte
Gebäude werden nicht mehr aufgestellt); nur die schon sichtbaren fliegen
noch vorbei. Die erweiterten Türme zeichneten jenseits der Reichweite
(3C00h) aber weiter alle Gebäude der Karte und die der nächsten Runde.
Jetzt: jenseits der Reichweite nichts mehr, sobald Q.KTW gesetzt ist, und
nie etwas aus Runde 5 und später. Am Gerät nicht gesehen.

## Demo-Flug im Attract, Servicemenü auf F2 (2026-10-14)

Der Attract-Modus des Originals hat keine Flugszenen (Highscores, Logo,
Fluganleitung, Punkteliste). Auf Wunsch des Besitzers gibt es jetzt einen
Demo-Flug (`enh_demo`, Standard an, Testmenü "Attract Demo";
`platform/demo.c`): Wenn der Attract von der Punkteliste zu den Highscores
geht und niemand bezahlt hat (im Free Play immer), wird die ganze Maschine
beiseitegelegt, ein Credit gegeben, im Takt Feuer gedrückt, bis das Spiel
läuft, der mittlere Todesstern gewählt, dann fliegt der Autopilot wie das
Filmskript (Joch pendelt, Feuer im Takt, Schilde laufen nie aus). Jeder
Abschnitt (Weltraum, Oberfläche, Graben) kommt aus der nächsten Ansicht,
der nächste Demo beginnt eine Ansicht weiter. Nach 112 s, wenn das Spiel
endet, oder sobald jemand eine Münze einwirft oder einen Knopf drückt,
wird die Maschine zurückgelegt: kein Credit, keine Punkte, keine Zählung
im NOVRAM (das Speichern des NOVRAM ist während des Demos aus), und die
Münze oder der Knopf zählt danach wie immer. Der Ton der Platine wird beim
Zurücklegen zurückgesetzt. `tests/test_demo.c`: Demo beginnt, kommt durch
Weltraum (1 s), Oberfläche (33 s) und Graben (72 s), jeder Teil aus einer
anderen Ansicht, und die Maschine ist danach wie vorher (RAM, NOVRAM).
Befund dabei: Ein Feuerdruck im einen Bild, das eine Attract-Seite
aufbaut, wird vom Original nicht geprüft (STRTCK läuft dort nicht), daher
drückt der Demo im Takt. Bekannt: Die Betriebszeit-Zähler der Maschine
laufen während des Demos nicht weiter (sie werden mit zurückgelegt).
`swvideo --demo` (mit langem `--attract`) filmt ihn.

Das Servicemenü liegt jetzt auf F2 (die Service-Taste von MAME) statt Tab,
ebenso das Zurück aus Kalibrierung und Operator-Seiten.

## Außenansicht, Umschalten mit Start (2026-10-14)

Die Cockpit-Option ist eine von drei Ansichten geworden: `enh_view` 0
(Arcade, Standard), 1 (Cockpit), 2 (Außen); Testmenü Graphics "View". Im
Flug (wenn das Spiel Nase und Kanonen zeichnet, `plane_seen`) schaltet die
Start-Taste weiter und feuert dort nicht (`start_fires` in
`platform/controls.c`); sonst, im Attract, startet sie wie bisher das Spiel.
Die Wahl wird sofort in die Einstellungsdatei geschrieben.

Außen (`outside` in `render/enhance.c`): ein eigenes X-Wing-Modell
(`xwing_build`, ~200 Linien, ~70 Flächen: Rumpf, spitz zulaufende Nase mit
rotem Band, Kanzel, R2-Kuppel, vier Flügel in X-Stellung mit roten
Streifen, vier sechseckige Triebwerke mit rotem Glühen hinten, Kanonen mit
Läufen an den Flügelspitzen) fliegt 1000 vor der Kamera, unten im Bild. Es
legt sich mit der Lage des Fadenkreuzes in die Kurve (bis 31 Grad) und
dreht leicht mit (bis 13 Grad Gier, 7 Grad Nicken); die Welt bleibt die
Sicht des Spiels (eine Verfolgerkamera knapp hinter dem Schiff). Die Laser
des Spiels werden so verschoben, dass sie an den vier Kanonenspitzen
beginnen und am selben Ziel enden (Fadenkreuz oder Treffer), ihre Farben
und Teilstücke bleiben. Was hinter dem Schiff liegt, wird an dessen
projizierten Flächen abgeschnitten (gemeinsam mit dem Cockpit: `hide`),
Rückseiten des Modells über die Normalen weggelassen. Die Zeichenhilfe des
Falcon ist dafür zu einem allgemeinen Modell geworden (`model`,
`model_draw`, `msolid`). Bekannt: Teile des Modells verdecken einander
nicht (nur jede konvexe Form ihre eigene Rückseite), Treffer bleiben die
des Spiels (wie in der Arcade-Sicht). Am Gerät nicht gesehen.

## Cockpit-Ansicht (2026-10-14)

Wunsch des Besitzers nach einer Vorlage aus dem Film: Ego-Perspektive aus
dem Cockpit statt Nase und Kanonen des X-Wings, heute `enh_view = 1` (Testmenü
"View", siehe oben), in `render/enhance.c` (`cockpit`): Wo das
Spiel Nase und Kanonen zeichnet (SW_TAG_PLANE), fallen deren Linien weg, und
im Bildraum steht ein Cockpit: zwei Kanzelstreben von den oberen Ecken
(unter Score und Wave, die frei bleiben) zur Brüstung, ein flaches
Armaturenbrett bis y = -96 von -153 (gut ein Fünftel des Schirms; der
Grabenboden bleibt sichtbar) mit Leuchtenreihe, zwei Zielschirmen, Knopf,
Hebel und Seitenkonsolen mit rotem Rahmen, ~110 Linien. Alles dahinter
(Sterne, Gegner, Türme, Graben, Laser, Falcon) wird an drei konvexen
Flächen abgeschnitten, unabhängig von der Occlusion-Einstellung; Texte,
Score und Fadenkreuz nicht. Schüsse unverändert: die Laser laufen vom Ort
der Kanonen zum Fadenkreuz und tauchen hinter Konsole und Streben auf.
Nicht am Gerät gesehen: Helligkeit des Rahmens, Bildzeit (+110 Linien je
Bild, beim Strahl ungefähr 10 % mehr), ob die Streben beim Zielen stören.

## Menüeintrag und Marquee (2026-10-13)

Das Spiel hieß bis hierher nach der Plattform des Automaten (VectorPie)
mit dem Kürzel VP; der Besitzer will es Star Wars Enhanced genannt haben.
Umbenannt ist alles, was man sieht oder tippt: der Menüeintrag (`name` in
`package/game.ini`), die Texte im Programm (Testmenü, Controls Test,
ROM-Fehlermeldung, Fenstertitel), das Programm selbst und die Paket-Id
(`swenhanced`; kurz, weil `pkill -x` nur 15 Zeichen eines Namens kennt),
die Einstellungsdatei (`swenhanced.ini`), die Umgebungsvariable für die
ROMs (`SWE_ROMS`), die Doku. Auf dem Automaten ist der Ordner mit seinem
`save/` (NOVRAM, Einstellungen) nach `/persistent/games/swenhanced/`
umgezogen. Das GitHub-Repo heißt seit 2026-10-14 `Endprodukt/StarWarsEnhanced`
(privat; die alte Adresse leitet weiter). Nicht umbenannt: der lokale Ordner.

Das Menü zeigte außerdem kein Marquee: in `game.ini` fehlte `marquee =
marquee.png` (Tempest 2000 daneben hat den Schlüssel). Version 0.2.0,
Marquee das Artwork des Besitzers ("STAR WARS ENHANCED"). Im Menü zu sehen,
sobald das Menü neu startet (nach dem Verlassen des Spiels). Nicht geprüft
am Gerät, auch nicht, ob der längere Titel des Testmenüs (jetzt Größe 90
statt 110) in die Breite passt.

Marquee erneuert (zweites Artwork des Besitzers, ohne die Streifen unten; auf die volle Breite 1920 gezogen, 17 % breiter als das Original), Version 0.2.1. **Noch nicht auf dem Automaten**, der war aus: beim nächsten Mal `sh tools/deploy.sh` und die neueste `dist/swenhanced-<version>.vpk` (jetzt 0.3.18, mit Dogfight Etappe 4) nach `/persistent/games/packages/` (dort liegt noch 0.2.0).

Paketdatei: `sh tools/pack.sh` (in WSL, nach dem Pi-Build) schreibt
`dist/swenhanced-<version>.vpk` mit dem `vp-pack` des SDK (dessen Skript und
das Manifest werden dabei von CR LF befreit, die ein Windows-Checkout
mitbringt). `swenhanced-0.2.0.vpk` liegt auf dem Automaten in
`/persistent/games/packages/`; dorthin sind auch `bzoneii-1.0.0.vpk`,
`gltron-0.1.1.vpk` und `neverball-0.1.0.vpk` verschoben, die eine Ebene zu
hoch lagen. Installiert wird über das Menü (INSTALL GAMES), nicht von
selbst; das Installieren aus der `.vpk` ist am Gerät nicht ausprobiert.
Offen: Version in `package/game.ini` vor jedem neuen Paket hochzählen.

## Falcon, Kamera, Sterne, Gitter (2026-10-13)

Vier Wünsche des Besitzers, alle umgesetzt (Commit nach a8f006e):

* **Gitter weiter, mehr Sterne, Bunker detaillierter:** Plattenmuster voll
  bis 6000, aus bei 11000 voraus (vorher 3500/7000); 64 statt 28 Sterne
  über der Oberfläche; die roten Bunker wieder mit Nähten an den Seiten
  und einer Luke auf dem Dach (nicht mehr die Plattenlinien von vorher).
* **Millennium Falcon im Graben** (`enh_falcon`, Menü "Falcon Fly-Over"):
  Eigenes Modell in `render/enhance.c` (`falcon_build`): flache Rumpfscheibe
  mit Plateau oben und unten, zwei Mandibeln, sechseckige Cockpitröhre
  rechts mit türkisem Fenster, Radarschüssel auf Stiel, blaues
  Triebwerksband hinten, ~150 Linien, ~50 Flächen für die Occlusion. Jede
  Linie trägt die Normalen ihrer Flächen und wird nur gezeichnet, wenn eine
  davon zum Betrachter zeigt (die abgewandte Rumpfseite scheint nicht
  durch). Er kommt von hinten oben, fliegt über den Spieler und voraus den
  Graben entlang (3,2 s), leicht pendelnd und in die Kurve gelegt. Start:
  wenn der Spieler bei seinem Tempo 3,4 s vor dem Schacht ist; der Schacht
  wird dafür aus der Grabentabelle vorausberechnet (`port_ahead`: die
  Keile ab dem nahen Keil, über die Pies hinweg), weil das Spiel selbst ihn
  erst 7000h voraus kennt. Einmal je Anlauf; läuft bei verfehltem Schacht
  wieder. Im Film: Überflug 133,0 bis 134,6 s im Bild (dann klein voraus
  weg), "EXHAUST PORT AHEAD" ab 133,8 s, Schacht bei 135,8 s.
* **Kamera-Schwenks** (`enh_camera`, Menü "Smooth Camera"): Das Spiel ist
  ein Rail Shooter, im Weltraum dreht sich das Schiff von selbst zum
  verfolgten Gegner (AIMA) oder zum Todesstern (AIMDTH): die Drehrate
  RH.RAT wird jedes Bild neu aus der Lage des Ziels gesetzt, bei neuem
  Ziel springt sie auf voll. Mit `camera_ease` (sw_machine, aus
  `enh_camera`) ändert sich die Rate pro Bild um höchstens 12 von 127
  (`set_rate` in `game/player.c`): Anfahren und Abbremsen. Das greift ins
  Spiel ein (nicht nur ins Bild), damit Fadenkreuz, Treffer und Bild
  zusammenbleiben; die Aufnahmen laufen ohne das Flag und stimmen weiter.
  `tests/test_camera.c`: mit Flag nie ein Sprung über 12, ohne Flag gibt es
  welche. Die Zwischenbilder bleiben dabei stetig: der Rest-Mechanismus des
  Originals (ganze Schritte an S1, Rest nur im Bild) summiert sich linear.
* **Sterne machten komische Bewegungen:** Ursache in den Zwischenbildern
  (`game/subframe.c`): Ein Stern, der das Blickfeld verlässt, wird vom
  Original an zufälliger Stelle voraus neu gesetzt. Lag der neue Ort
  weniger als 1000h vom alten, lief die Interpolation ihn dorthin, ein
  Strich quer über den Schirm. Jetzt wird ein im Bild ersetzter Stern
  (`star_gen` unterschiedlich) nicht interpoliert. Das Original selbst
  springt an dieser Stelle (ist dort nicht zu sehen, weil der alte Stern
  außerhalb lag).

Am Gerät nicht gesehen: Falcon-Größe und -Helligkeit, das Gefühl der
Kamera (12 je Bild ist eine Setzung), Bildzeit mit 64 Sternen und mehr
Gitter.

## Türme genau wie das Original, ohne "Gemüse" (2026-10-13)

Der Besitzer will die Türme genau so groß und so weit auseinander wie im
Original, aber nicht aus dem Boden wachsend, und keine Option dafür. Eine
Einstellung mit drei Stärken des Maßstabs (Commit 096c3c5) ist deshalb
wieder zurückgenommen (80b88fd).

Befund: Größe und Abstand des Originals sind sein linearer Maßstab um den
Fuß (1 - XP/4000h, `M.GDSC`), und genau der ist das Wachsen: Der Fuß steht
am wahren Ort, die Spitze steigt schneller als die Perspektive. Beides
zugleich geht geometrisch nicht; eine Verzerrung der ganzen Szene in der
Tiefe, die Boden und Türme konsistent machte, ließe den Boden zu einem Punkt
statt zu einer Horizontlinie zusammenlaufen und schöbe die seitlichen Türme
zur Mitte (verworfen). Im Original fällt das Wachsen nicht auf, weil kein
Boden als Bezug da ist. Also: Türme exakt wie das Original (Maßstab, Fuß,
Trefferprüfung 100h), und das Plattenmuster liegt nur noch nah um den
Spieler (voll bis 3500, aus bei 7000 voraus; vorher bis 26000). Ferne Türme
stehen damit auf dunklem Boden wie am Automaten und wachsen ohne Bezug; wenn
sie das Plattenmuster erreichen, haben sie 70 bis 80 % ihrer Größe. Am
Gerät nicht gesehen.

## Free Play (2026-10-13)

Der Besitzer wünschte sich ein Spiel ohne Münzen. Das hat das Original
schon: die Option COIN VALUE auf FREE PLAY (im Testmenü unter Game Options
oder auf der Optionsseite des Automaten). Dann hält der Attract-Modus immer
einen Credit, "INSERT COINS" erscheint nicht mehr, es steht "PRESS FIRE TO
START", und Feuer startet das Spiel. Die Zeile "1 CREDIT" darunter, die das
Original im Free Play zeigt, ist auf Wunsch des Besitzers weg (vwcoin in
`src/game/mainline.c` bricht bei COIN VALUE 0 nach der ersten Zeile ab; die
Aufnahmen laufen mit 1 COIN 1 PLAY und sind unberührt). `tests/test_options.c`
prüft: Credit da, "PRESS FIRE TO START" gezeichnet, keine Credit-Zeile,
Feuer startet.

## Marquee (2026-10-13)

Der Marquee-Bildschirm des Automaten (HDMI, 1920x360, `advmame.rc`:
`vector_aux_marquee_display_mode stretch`) zeigte nur den Cursor der
Konsole: Das Programm fragt das SDK nach `marquee.png` neben der
ausführbaren Datei, und das Paket hatte keines. Jetzt liegt
`package/marquee.png` bei (1920x360, gelber Schriftzug über Sternen, mit
Pillow aus `impact.ttf` gezeichnet; kein Atari-Artwork), `deploy.sh` kopiert
es mit, der Build legt es neben das Programm. Ob es am Gerät erscheint, ist
beim Schreiben noch nicht gesehen.

## Szenenwechsel (2026-10-13)

Der Besitzer fand die Übergänge zwischen Szenen unsauber. Geprüft mit
`swvideo --check` (Linienzahl jedes Bildes, `SWDBG=1` schreibt sie) und
Bildfolgen um jeden Phasenwechsel (Attract-Seiten, Weltraum, Anflug,
Oberfläche, Graben):

* Kein Bild fällt aus (keine Einbrüche der Linienzahl, keine schwarzen
  Bilder).
* **Fehler gefunden und behoben**: Zwischen dem Einrichten einer Phase und
  ihrer ersten Ansicht steht die Phasennummer schon auf der neuen Phase,
  aber die Displayliste und die Blickvektoren des Betrachters sind noch die
  der alten. Die Erweiterungen zeichneten für dieses eine Bild die
  Oberfläche mit dem Blick des Anflugs (riesige Platten quer über den
  Schirm). Jetzt merkt sich die Maschine, welche Phase die Liste gezeichnet
  hat (`sw_machine.view_phase`, gesetzt in IVGEN1), die Erweiterungen
  richten sich danach und nehmen den Standort des Betrachters aus U1 (so wie
  die Ansicht ihn benutzt hat); im Rückfallpfad (Linien zum nächsten Bild
  bewegt) bekommen sie den Zustand des voraus gerechneten nächsten Frames.
* Ein Frame, in dem der Spieler springt (Lage oder Ort um mehr als 2000h),
  gilt dem Zwischenbild als Schnitt: Es wird nicht aus dem Zustand davor
  gerechnet.
* Am Ende der Oberfläche (das Spiel räumt die Gebäude weg, der Spieler
  taucht zum Graben) blendet das Plattenmuster in 0,8 s aus, statt beim
  Abtauchen über den Schirm zu fegen.
* Was bleibt und vom Original kommt: Beim Wechsel einer Phase zeigt das
  Spiel die neue Szene von einem Frame auf den anderen (kein Überblenden);
  im Attract-Modus wechseln die Seiten hart. Das ist so gebaut.

## Am Automaten gesehen (Besitzer, 2026-10-13)

Erster Lauf des C-Spiels mit den Erweiterungen am Gerät. Befund und was
daraus wurde:

* **Sortierte Striche wackeln** auf der Röhre (die Reihenfolge, und damit
  woher der Strahl kommt, wechselt von Bild zu Bild). `sort_strokes` ist
  darauf entfernt. Preis, am Gerät gemessen: Die Highscore-Seite läuft
  unsortiert mit 37 statt 50 Bildern pro Sekunde (Leerweg 172000 statt
  60000 Einheiten).
* **Oberfläche war zu voll**: Sterne dort auf 28 funkelnde reduziert
  (zuerst ganz entfernt, dann auf Wunsch ein paar zurück), Türme nur noch
  dort, wo das Spiel sie zeigt (100h bis 3C00h voraus, 45 Grad zu jeder
  Seite, eingerichtet, Bunker nicht abgeschossen; zuerst war der Kegel
  weiter, und am Start standen Türme neben dem Spieler), Bunker ohne Nähte.
  Am Ende der Oberfläche fliegt man damit wieder ins Dunkle wie im Original.
  Zwischendurch blieben abgeschossene Bunker stehen (die Prüfung auf
  "beschädigt" war beim Umbau verloren gegangen): behoben.
* **Graben, Hindernisse verändern ihre Form**: Der Automat kann eine Linie
  nicht am Bildrand abschneiden; bei nahen Objekten schiebt er die Punkte
  knapp hinter den Rand (BJBCLP), das verbiegt sie. Neu `enh_clip = 1`
  (TRENCH CLIPPING): Die Zeichenroutinen notieren die Punkte der
  Wandobjekte im Raum, `enhance.c` zeichnet ihre Linien daraus neu und
  schneidet richtig; die Flächen für die Occlusion werden ebenso im Raum
  beschnitten. Gemessen: Wo das Spiel nichts verschoben hat, liegen die neu
  gezeichneten Linien im Mittel 0,13 Einheiten neben denen des Spiels. Am
  Gerät noch nicht gesehen.
* **Testmenü**: kein Selbsttest-Knopf am Gerät belegt (alle Tasten des
  Jochs sind Feuer); die Tab-Taste öffnet und schließt es jetzt.
* Leistung im Spiel laut Besitzer in Ordnung. Die Zahl `frame` im Log ist
  die Zeit, die die Platine für ein Bild braucht, nicht Rechenzeit des Pi.

## Bekannte Grenzen

* **Erweiterungen über der Oberfläche**: nicht am Automaten gesehen; sie
  bringen deutlich mehr Linien ins Bild (Boden rund 80, ein naher Turm rund
  60), ob die Platine das flüssig schafft, ist offen. Die Türme schießen
  und werden getroffen nach der Regel des Automaten. Verfolgungsjagden gibt es nur über der Oberfläche,
  nicht im Weltraum oder im Graben. Der Tod über dem Boden und der Übergang
  zum Graben sind nur flüchtig angesehen.

* **Testmenü**: ungesehen (siehe oben). YOKE AUTO CENTER schaltet nur mit
  Feuer um, nicht mit dem Joch; ausgeschaltet lernt das Automatenprogramm
  den Bereich des Jochs erst nach dem nächsten Start wieder selbst. Der
  Controls-Test (`--controls`) ist noch kein Menüpunkt.

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
