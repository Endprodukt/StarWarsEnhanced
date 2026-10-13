# Sounds

Alle Töne, die das Spiel beim Soundboard anfordern kann: 59 Einträge, je ein
Byte (`SNDPBX.MAC` des Originals). Die Liste ist die Grundlage dafür, Sounds
später durch neue zu ersetzen.

* **Nr.** ist das Byte, das das Spiel schickt (`sw_doxmt` in
  `src/game/gmath.c`); das Soundprogramm verteilt es in `request()` in
  `src/sound/sndprog.c`. Dort ist die Stelle, an der ein Ersatz einhaken würde.
* **Art**: Sprache kommt aus dem Sprachchip (TMS5220), Musik und Effekte aus
  den vier POKEYs. Alles wird heute aus den Daten der ROMs erzeugt, es gibt
  keine Aufnahmen.
* **Wann** ist die Stelle im Programm des Automaten, an der der Ton
  angefordert wird.

## Sprache (24)

| Nr. | Name | Text | Wann |
|---|---|---|---|
| 03 | SPKALW | "Always" | Attract, nach "The Force will be with you" |
| 04 | SPKELE | Vorbeiflug-Geräusch (TIE Fighter, "Elefant") | Weltraum, ein TIE fliegt vorbei |
| 05 | SPKFOA | "The Force will be with you, always" | Spielende |
| 06 | SPKFOR | "The Force will be with you" | Attract |
| 07 | SPKGRE | "Great shot kid, that was one in a million" | Graben, Schacht getroffen |
| 08 | SPKHAV | Atmen / "I have you now" / Atmen | Weltraum, Darth Vader |
| 09 | SPKHIT | "I'm hit, but not bad. R2, see what you can do with it" | Treffer am eigenen Schiff |
| 0A | SPKI | Sprachchip neu starten (kein Ton) | nicht vom Spiel benutzt |
| 0B | SPKIMO | Atmen / "I'm on the leader" / Atmen | Weltraum, Darth Vader |
| 0C | SPKLET | "Let go, Luke" | Graben |
| 0D | SPKLOS | "I've lost R2" | Schilde: R2 getroffen |
| 0E | SPKR2N | R2 schimpft | Graben, Schacht verfehlt |
| 0F | SPKR2T | "R2, try and increase the power" | Schilde fast leer |
| 10 | SPKRED | "Red Five standing by" | Spielstart |
| 11 | SPKREM | "Remember" | Spielende, vor SPKFOA |
| 12 | SPKSHK | "I can't shake him" | Weltraum, TIE schießt |
| 13 | SPKSIZ | "Look at the size of that thing" | Anflug auf den Todesstern |
| 14 | SPKSKP | wartende Sprache verwerfen (kein Ton) | bei jedem Tod |
| 15 | SPKSTA | Atmen / "Stay in attack formation" / Atmen | Weltraum, Darth Vader |
| 16 | SPKSTR | Atmen / "The Force is strong with this one" / Atmen | Graben |
| 17 | SPKTHI | "This is Red Five, I'm going in" | Ende Weltraum, Anflug |
| 18 | SPKTRU | "Luke, trust me" | Graben |
| 19 | SPKUSE | "Use the Force, Luke" | Einflug in den Graben |
| 1A | SPKYAU | "Yahoo! You're all clear, kid" | Graben |

## Musik (11)

| Nr. | Name | Stück | Wann |
|---|---|---|---|
| 1B | PMBEN | Bens Thema | Spielende ohne Highscore |
| 1C | PMCNT | Cantina | Highscore-Eingabe |
| 1D | PMDAR | Darth Vaders Thema | Weltraum |
| 1E | PMDES | Abstieg zum Todesstern | Ende Weltraum |
| 1F | PMEND | Schlussmusik | nach der Explosion des Todessterns |
| 20 | PM4TH | Kampfmusik in Quarten | Boden mit Türmen |
| 21 | PMREB | Thema der Rebellen | Ende Boden |
| 22 | PMRRP | Thema der Rebellen mit schnellen Wiederholungen | Graben |
| 23 | PMSF2 | Effekt 2: Schuss in den Schacht | Protonentorpedo abgefeuert |
| 24 | PMTH5 | Hauptthema | Spielbeginn (Weltraum) |
| 25 | PMTHB | Thema, Teil B | Weltraum, nach dem Hauptthema |

Im Attract spielt das Programm die Stücke über die Tabelle `TMUSIC`
(BEN, CNT, END, RRP, THB, TH5, REB, 4TH, DAR).

## Effekte (22)

| Nr. | Name | Geräusch | Wann |
|---|---|---|---|
| 26 | AUDCR | Aufprall | gegen Turm, Bunker, Kraftfeld oder Grabenwand |
| 27 | AUDDF | Explosion des Todessterns | Todesstern explodiert (drei Phasen) |
| 28 | AUDDR | R2 tot, "Schwanengesang" | Schilde: R2 getroffen |
| 2A | AUDPB | Vorbeiflug, Annäherung | Weltraum, TIE fliegt vorbei |
| 2B | AUDPBZ | Vorbeiflug beenden (kein Ton) | TIE abgeschossen |
| 2C | AUDPF | Turm-"Pffft" | Boden, an Turm oder Bunker |
| 2D | AUDPH | Schacht (begleitet PMSF2) | Protonentorpedo; Ende der Initialen |
| 2E | AUDPS | Vorbeiflug kurz (Doppler) | Weltraum, TIE ist vorbei |
| 2F | AUDRC | R2 "gekocht" | Schilde bei 1 |
| 30 | AUDRD | R2 fertig | Schilde fast leer |
| 31 | AUDRS | R2, kurzes Ja | Treffer am eigenen Schiff |
| 32 | AUDRY | R2, Ja | Ende Weltraum, Anflug |
| 33 | AUDSH | Schild | eigener Treffer durch ein Geschütz |
| 34 | AUDSS | Feuerball abgeschossen | Schuss des Gegners getroffen; Initialen |
| 35 | AUDSX | kleine Explosion | TIE, Bunker oder Turm abgeschossen |
| 36 | AUDTC | Kanone des TIE Fighters | TIE schießt |
| 37 | AUDTCZ | Kanone des TIE Fighters, Ende | Feuerball abgeschossen |
| 38 | AUDTH | Schub | Anflug auf den Todesstern |
| 39 | AUDTL | Laser der Türme | Turm oder Grabengeschütz schießt |
| 3A | AUDXL | Laser des X-Wing | eigener Schuss; Initialen |

## Nur im Test des Soundboards (4)

| Nr. | Name | Ton |
|---|---|---|
| 01 | AUDTST | Testtöne der Effekt-POKEYs |
| 02 | PMTST | Testtöne der Musik-POKEYs |
| 29 | AUDNAY | Test nicht bestanden |
| 3B | AUDYEA | Test bestanden |

00 ist kein Ton: Es setzt das Soundboard zurück.

## Für den Ersatz zu beachten

* Musik und Sprache sind lang und laufen über Phasen hinweg; ein neues
  Musikstück löst das laufende ab, Sprache wird in eine Warteschlange gestellt
  (SPKSKP leert sie).
* Mehrere Effekte klingen gleichzeitig (eigene POKEY-Kanäle); manche haben
  ein eigenes Ende-Kommando (AUDPB/AUDPBZ, AUDTC/AUDTCZ).
* Solange Musik oder Sprache läuft, schaltet das Programm ein Bit "FM" am
  RIOT um; was es am Klang ändert, ist noch nicht untersucht.
* Die Spalte "Wann" ist aus den Stellen im Quelltext abgeleitet, nicht jede
  einzeln im Spiel nachgehört.
