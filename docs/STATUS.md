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

## Cockpit-Ansicht (2026-10-14)

Wunsch des Besitzers nach einer Vorlage aus dem Film: Ego-Perspektive aus
dem Cockpit statt Nase und Kanonen des X-Wings. `enh_cockpit` (Testmenü
"Cockpit View", Standard aus), in `render/enhance.c` (`cockpit`): Wo das
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
umgezogen. Nicht umbenannt: das GitHub-Repo `Endprodukt/StarWarsVP` und
der lokale Ordner.

Das Menü zeigte außerdem kein Marquee: in `game.ini` fehlte `marquee =
marquee.png` (Tempest 2000 daneben hat den Schlüssel). Version 0.2.0,
Marquee das Artwork des Besitzers ("STAR WARS ENHANCED"). Im Menü zu sehen,
sobald das Menü neu startet (nach dem Verlassen des Spiels). Nicht geprüft
am Gerät, auch nicht, ob der längere Titel des Testmenüs (jetzt Größe 90
statt 110) in die Breite passt.

Marquee erneuert (zweites Artwork des Besitzers, ohne die Streifen unten; auf die volle Breite 1920 gezogen, 17 % breiter als das Original), Version 0.2.1. **Noch nicht auf dem Automaten**, der war aus: beim nächsten Mal `sh tools/deploy.sh` und die neueste `dist/swenhanced-<version>.vpk` (jetzt 0.2.2, mit der Cockpit-Ansicht) nach `/persistent/games/packages/` (dort liegt noch 0.2.0).

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
