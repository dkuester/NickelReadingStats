# Review-Fortschritt NickelReadingStats

Arbeitsliste aus dem Code-Review vom 2026-10-19. Branch: `review/code-review-fixes`.
Ein Commit pro erledigtem Punkt. Nach einer Unterbrechung (z. B. Sitzungslimit):
diese Datei und `git status` / `git log` lesen, dann beim ersten offenen Punkt
weitermachen.

Status: `[ ]` offen · `[~]` in Arbeit · `[x]` erledigt · `[!]` blockiert (Grund steht dabei)

## Ziele

- **Z1:** Alle Review-Punkte unten abarbeiten.
- **Z2:** Kompatibilität mit „normalen“ Kobos auf Firmware 5.18+ (Qt 6.5.2).
  Siehe Abschnitt „Kobo-Kompatibilität“.

## Hohe Priorität

- [x] **H1** Hartkodierter Offset `ReadingView + 0x94` (2×): benannte Konstante
  plus Helper. `check-symbols.py` soll den Offset per Disassembly von
  `updateWordCounts` mitprüfen (braucht die Firmware lokal).
- [x] **H2** `g_force_kobo_session`: `thread_local` und RAII-Guard statt
  4× kopiertem prev/set/restore. README-Aussage „nur während dieser Calls“
  präzisieren (synchrone Slots in `fireEventSlot` laufen mit).
- [x] **H2b** Auf dem Gerät prüfen, ob der Kobo-Event-Pfad Analytics/Netzwerk an
  Kobo auslöst. *Auswertung `KoboReader.sqlite` (Tolino 5.18.264769, 2026-10-19):
  Die Tabelle `AnalyticsEvents` enthält `OpenContent`/`leave_content` auch für
  sideloaded EPUBs, mit Titel, Autor, ISBN, Fortschritt, Lesesekunden und
  Seitenwechseln. Die Warteschlange enthält nur Einträge von heute, wird also
  regelmäßig übertragen. Die Reading-Life-Events tragen einen `GA4`-Block
  (`book_id` = Dateipfad); `Analytics.conf` (Tolino-Backup) zeigt auf einen
  Google-Endpunkt. Ob diese Einträge ohne Mod ebenfalls entstehen, ist offen:
  kein Vergleich ohne Mod vorhanden → N4.*
- [x] **H3** Wortzählung: fehlende Spine-Datei → `0` anhängen statt überspringen
  (Index-Verschiebung). Manifest-Hrefs prozent-dekodieren.

## Mittlere Priorität

- [x] **M4** `supportsActivityInMore`: fehlendes dlsym → fail-closed (Eintrag
  verbergen) statt fail-open (Jugendschutz).
- [x] **M5** `reading_stats_supported`: fail-closed, wenn `isFixedLayout` fehlt;
  zusätzlich Whitelist der MIME-Typen (epub/kepub). *Umgesetzt als Nachbildung
  des Originals (per Disassembly ermittelt): plain epub statt kepub,
  `getAccessibility() != 6`, `!isFixedLayout()`. Die virtuelle Prüfung an
  vtable+0x10c ist nicht nachgebildet (unbekannt) → N2.*
- [x] **M6** Wortzählung blockiert den UI-Thread: Cache (Pfad + mtime +
  Größe) und/oder Hintergrund-Thread. *Umgesetzt: eigener Pool mit 1 Thread,
  Auslieferung per Queued-Call auf den GUI-Thread mit `QPointer`- und
  `getId`-Prüfung, Cache (16 Bücher), Warteliste für parallele Öffnungen.
  Host-Test `test_hook_async`.*
- [x] **M7** Entpacken begrenzen: Einträge über einem Limit (z. B. 16 MB)
  überspringen bzw. mit `0` zählen. *Umgesetzt mit 8 MB pro Eintrag
  (`kMaxEpubEntryBytes`), geprüft gegen die unkomprimierte Größe im
  ZIP-Header vor dem Entpacken.*
- [x] **M8** Hook-Signaturen dokumentieren und prüfen (Rückgabetyp von
  `start/endReadingSession`, Wertparameter von `fireEventSlot`). *Befund per
  Disassembly: `startReadingSession` ist `void`, `endReadingSession` gibt
  `int` (Sekunden aus `QDateTime::secsTo`) zurück – war als `bool`
  deklariert. Korrigiert; `check-symbols.py` prüft jetzt void/value der
  EventEngine-Hooks.*

## Genauigkeit der Wortzählung

- [x] **W1** Nur `<body>` zählen (nicht `<title>`/`<head>`).
- [x] **W2** Self-closing `<script …/>` darf keinen Text verschlucken;
  `<!-- … -->` vor dem Tag-Strip entfernen.
- [x] **W3** Reine Satzzeichen-Tokens nicht als Wort zählen (`* * *`, `—`).
- [x] **W4** Weitere Entities: `&#xa0;`, `&thinsp;`, `&ensp;`, `&emsp;`.
- [x] **W5** Kleinkram: Encoding-Erkennung, `linear="no"`, `join_path`
  normalisiert bei leerem `opf_dir` nicht. *UTF-16 per BOM (`decode_xhtml`),
  `linear="no"` bleibt bewusst drin (Spine-Indizes), `join_path` normalisiert
  immer.*

## Konsistenz und Doku

- [x] **K1** Kopfkommentar in `readingstats.cc` („two gates“ vs. 3, Hook-Liste
  unvollständig).
- [x] **K2** Versionsangaben vereinheitlichen („5.18+/6.x“ vs. „5.18“).
- [x] **K3** README: doppelter Fixed-Layout-Absatz.
- [x] **K4** `chapterIndex` fehlt in `EXPECTED` von `check-symbols.py`;
  Symbole ohne Erwartung sollen als Fehler zählen. *Gegen Tolino-5.18-
  `libnickel` (BuildID bcfe1a2b…) geprüft: 19/19 Symbole, 7/7 Konventionen OK.*
- [x] **K5** Tote Fallbacks (`!Device__isTolino` → `true`, `mergedTOC` → `0`)
  aufräumen oder kommentieren.
- [x] **K6** `extract()` aus beiden Python-Tools in ein gemeinsames Modul.

## Repo, CI, Tools

- [x] **R1** `tools/__pycache__/*.pyc` aus Git entfernen und ignorieren.
- [x] **R2** CI-Job mit Qt6 zusätzlich zu Qt5. *Lokal in `debian:bookworm` geprüft: Qt 5.15.8 und Qt 6.4.2 grün.*
- [x] **R3** `check-symbols.py`: VMA ≠ Datei-Offset und Thumb-Bit prüfen;
  `defsyms.txt` mit nur einer Spalte (reine Namensliste) unterstützen.
  *Umgesetzt über PT_LOAD-Segmente. Im 5.18-Binary weicht das Datensegment
  um 0x10000 ab (relevant für Typinfo-Prüfungen wie N3).*
- [x] **R4** Fixture erweitern: `mimetype`, `<title>`, Unterordner,
  prozent-kodierter Href, fehlende Spine-Datei. *Generator:
  `hook/tests/fixtures/make_sample_epub.py`.*

## Kobo-Kompatibilität (Z2)

Stand der Analyse:

- Kobo-Firmware 5.x existiert (europäische Libra Colour, Clara Colour,
  Clara BW werden damit ausgeliefert) und nutzt laut NickelHook-Qt6-Fork
  dieselbe Qt-6.5.2-Basis wie Tolino 5.18.
- Die Tolino-5.18-`libnickel` enthält Kobo- und Tolino-Logik in einem Binary;
  `isTolino()` ist eine Laufzeitabfrage. Alle 19 Symbole des Mods sind in der
  lokalen 5.18-Symbolliste vorhanden.
- Auf einem Kobo ist `isTolino()` ohnehin `false`. Die Tolino-Hooks
  (`supportsReadingLife`, `supportsActivityInMore`, `isTolino`, `EventEngine`)
  laufen dort praktisch als Durchreichung. Die EPUB-Erweiterungen
  (`supportsReadingStats`, Wortzählung, `mergedTOC`) wirken auch auf Kobo.
- Rahmen: ausschließlich Firmware 5.18+ (Kobo und Tolino). Ältere
  Firmware (4.x, Qt 5.2.1) ist ausdrücklich kein Ziel.

Aufgaben:

- [x] **KC1** Code-Pfade auf Kobo-Tauglichkeit prüfen; nichts darf von
  `isTolino() == true` abhängen. *Ergebnis: Tolino-Hooks (`supportsReadingLife`,
  `isTolino`, `EventEngine`, `supportsActivityInMore`) liefern auf Kobo
  dasselbe wie das Original; EPUB-Erweiterungen wirken auf beiden;
  `bookSpecificStatsEnabled` wird auf beiden erzwungen (gewollt).*
- [x] **KC2** Nicht-essenzielle Hooks als `optional` markieren, damit ein auf
  Kobo fehlendes Symbol nicht den ganzen Mod abschaltet.
- [x] **KC3** Symbole, Return-Konventionen und Offset `0x94` gegen eine
  **Kobo**-5.x-`libnickel` prüfen (`check-symbols.py`). *Erledigt 2026-10-21
  gegen zwei vom User bereitgestellte Kobo-Bibliotheken: 5.18.270971
  (BuildID `5bfd31d7…`, intern `5.00.270974`) und 6.0.276679 (BuildID
  `1740d695…`, intern `5.00.276691`). Beide ARM32, Qt 6.5
  (`qt_version_tag@Qt_6.5`, kein `@Qt_5`), dieselben `NEEDED` wie die
  Tolino-Referenz `bcfe1a2b…`. Ob es Kobo- und nicht Tolino-Builds sind, lässt
  sich aus der Bibliothek allein nicht belegen (ein Binary für beide); die
  Herkunft stammt vom User. `check-symbols.py` für beide grün (15 Hooks,
  9 dlsyms, 0 Probleme): alle 24 Symbole vorhanden, alle Return-Konventionen
  und void/value wie deklariert, `ReadingView`-Basis `QWidget` bei 0, virtueller
  Aufruf = `hasOnlyImages()`, die fünf Sperrfunktionen rufen `isTolino()` je
  einmal per `bl`/`blx` und keinen Tail-Caller (einziger weiterhin
  `tolinoDemoModeEnabled()`), Offset `0x94`. Unterschiede zur Tolino-Referenz
  nur in Adressen. In 6.0 ist `fireEvent()` kleiner (0x474 statt 0x504):
  gleiche Aufrufkette, nur ruft der Compiler die `QByteArray`-Destruktoren
  jetzt über `QArrayDataPointer<char>::~QArrayDataPointer` statt sie
  einzubetten. Direkte Callees aller Sperrfunktionen und der übrigen
  gehookten Funktionen sind in allen drei Builds gleich; von den
  Sperrfunktionen aus erreichbare `isTolino()`-Aufrufer weiterhin nur
  `Affiliate::getDeviceAffiliate()` und `DictionaryFileManager::getAll()`.
  Keine Codeänderung nötig.*
- [!] **KC4** Smoke-Test auf einem Kobo mit 5.x. *Blockiert: kein Gerät mit 5.x.*
- [~] **KC5** README/Beschreibung: Kobo 5.18+ erst nach KC3/KC4 als
  unterstützt nennen. *Nach KC3 (2026-10-21): Kompatibilitätstabelle und
  Hinweise führen Kobo 5.18/6.0 als „symbolgeprüft, auf Gerät ungetestet“.
  „Unterstützt“ erst nach KC4.*

## Neue Befunde während der Umsetzung

- [x] **N1** `hasValidReadingStats` prüft im Original auf Tolino zusätzlich
  `wordCountsReady()` und `0 <= mergedTOCAndSpineIndex() < wordCounts.size()`.
  *Disassembly 5.18: `wordCountsReady()` = `wordCounts.size() != 0` (Feld
  `+0xac`, von `gotWordCounts()` gesetzt); `supportsReadingStats()` und
  `mergedTOCAndSpineIndex()` ruft das Original über die PLT, also über unsere
  Hooks. Seit H3/M6 besteht das Original für plain epubs von selbst, sobald
  die Wortzahlen da sind. Der Override griff nur noch, wenn die Prüfung zu
  Recht scheitert (leere oder verschobene Liste). → Override entfernt.
  Gerätetest 2026-10-20: Buchstatistik erscheint wie zuvor.*
- [x] **N2** Virtuelle Methode an vtable+0x10c in `supportsReadingStats`
  identifizieren (vtable von `ReadingView` auflösen) und ggf. nachbilden.
  *Ist `ReadingView::hasOnlyImages()`. Keine Unterklasse überschreibt sie →
  direkter Aufruf per dlsym; Original damit vollständig nachgebildet.
  `check-symbols.py` (VIRTUAL_CALLS) prüft die Auflösung.*
- [x] **N3** `ReadingView` → `QObject*`-Cast (M6) setzt voraus, dass `QWidget`
  die primäre Basis bei Offset 0 ist. *`check-symbols.py` (PRIMARY_BASES)
  liest die Typinfo über die ELF-Relocations: 5.18 OK (13 Basen, base[0] =
  `QWidget`, Offset 0, public).*

- [x] **N4** Vergleichstest ohne Mod (2026-10-20, Tolino 5.18.264769, WLAN
  aus). *Ergebnis: Mit Mod entstehen `AppStart`, `OpenContent`,
  `leave_content` (Titel, Autor, ISBN, Fortschritt, Lesesekunden,
  Seitenwechsel) u. a. sowie Reading-Life-Events mit `GA4`-Block; die
  Warteschlange wurde über Nacht übertragen. Ohne Mod (Buch gelesen, 62 → 66 %)
  entsteht weder ein Analytics- noch ein Reading-Life-Eintrag. **Der Mod
  verursacht die Analytics-Übertragung.** `EventEngine::fireAnalyticsEvent()`
  prüft selbst nicht `isTolino()`, nur `PrivacyPermissionsManager::
  analyticsEnabled()` und `ReadingLifeSettings::getAnalyticsTrackingEnabled()`;
  gesperrt wird auf dem Tolino bei den Aufrufern, die der Mod freischaltet.*
- [x] **N5** `EventEngine::fireAnalyticsEvent()` hooken. *Umgesetzt auf
  `fix/drop-forced-analytics`: Auf einem echten Tolino (Original-`isTolino()`)
  wird jedes Analytics-Ereignis verworfen, auf Kobo nichts. Breiter als
  ursprünglich geplant, weil auch `updateSessionStatsSlot()` außerhalb des
  Fensters Analytics feuert; ohne Mod reiht der Tolino ohnehin keine ein.
  Member-Funktion mit `bool` auf dem Stack, `void`, Aufrufe nur über PLT
  (GOT-Hook greift). Host-Test `test_hook_gates`. Gerätetest 2026-10-20
  (Tolino 5.18.264769, WLAN aus): `AnalyticsEvents` leer; Lesesitzung
  07:16–07:21Z aufgezeichnet (Sitzungen 14 → 18, Lesesekunden 3017 → 5492),
  Fortschritt 66 → 67 %.*
- [x] **N6** `PagesTurnedThisSession` ist in allen ausgewerteten Sitzungen 0.
  *Analyse: Pfad `onPageTurned` → `fireEvent` (Hook) → Timer → Slot-Objekt
  → `fireEventSlot` (über PLT, Hook) → `fireSocialEvent` →
  `PageTurnEventHandler::save` → `saveTransient`; alle `isTolino()`-Sperren
  liegen in gehookten Aufrufen. Die Null war eine Fehldeutung:
  `PagesTurnedThisSession` steht im Event, das beim Sitzungsstart geschrieben
  wird. `Event 46` zählt die Seitenwechsel (10 → 87 zwischen 19. und 20.10.)
  und wird erst beim nächsten Sitzungsstart geschrieben (Zeitstempel =
  Sitzungsstart). Gerätetest 2026-10-20: ein Kapitel (~24 Seiten) gelesen,
  Buch erneut geöffnet → `Event 46` 87 → 112 (+25), „Seiten pro Minute“ zeigt
  1,1. Kein Fehler im Mod.*

- [x] **N7** Testerbericht (2026-10-20, Tolino Vision Color, Version v0.1.0):
  Nach Installation wurde der Tolino „zur Mischung aus Kobo und Tolino“:
  Kobo-Menüs sichtbar, Thalia-Konto weg (nur Kobo-Konto), Sync defekt;
  ohne Mod wieder normal, reproduzierbar. *Ursache sehr wahrscheinlich H2
  (globaler `isTolino()`-Override in v0.1.0, u. a. beim `AppStart` während der
  Konto-/Sync-Initialisierung). Gegenprobe auf eigenem Tolino mit aktuellem
  Stand: Sync und Downloads ok, keine Kobo-Menüs; mit v0.1.0 am 19.10.
  `FailedSync`. Behoben in v0.2.0 (Release 2026-10-20). Restrisiko: Im selben
  Thread sieht weiterhin alles, was die Sperrfunktionen synchron aufrufen,
  „kein Tolino“. → Branch `fix/istolino-caller-scope`:*
  1. Per Firmware-Analyse alle Aufrufer von `isTolino()` in der Kette
     `start/endReadingSession`, `fireEvent`, `fireEventSlot`,
     `fireSocialEvent` und deren Callees ermitteln (Liste der Sperrfunktionen).
  2. Prüfen, dass keine davon `isTolino()` per Tail-Call erreicht.
  3. Hook: `false` nur, wenn das Fenster aktiv ist **und** der direkte
     Aufrufer (Rücksprungadresse, Adressbereiche beim Start per `dladdr`/
     Symbolen bestimmt) eine Sperrfunktion ist; sonst echter Wert.
  4. `check-symbols.py`: Liste der Sperrfunktionen gegen die Firmware prüfen
     (rufen `isTolino()` über PLT, kein Tail-Call).
  5. Host-Test, ARM-Build, Gerätetest: alle drei Activity-Kennzahlen,
     Buchstatistik, Sync, Konto, Menüs.
  Testerrückmeldung zu v0.2.0 steht aus.
  *Stand 2026-10-20 (Branch): Schritte 1–4 umgesetzt. Analyse 5.18: von den
  Sperrfunktionen aus (direkte/PLT-Aufrufe, 6 Ebenen) fragen außerdem
  `EventEngine::initialize()` (richtet Google Analytics und `SessionStats`
  ein), `Affiliate::getDeviceAffiliate()` (Thalia/Kobo, nur gelesen, nicht
  gespeichert) und `DictionaryFileManager::getAll()` `isTolino()` ab – alle
  analytics-bezogen, alle sehen jetzt den echten Wert. Einziger Tail-Caller
  von `isTolino()`: `ApplicationSettings::tolinoDemoModeEnabled()`, von keiner
  Sperrfunktion aufgerufen. Bereiche zur Laufzeit aus `.dynsym`
  (`dladdr1`, st_size); `check-symbols.py` prüft mit denselben Grenzen.
  Gerätetest 2026-10-21 (Tolino 5.18.264769): zuerst fehlte `initialize()`
  als Sperrfunktion (Statistik eingefroren, per Diagnose-Log gefunden); mit
  fünf Sperrfunktionen zählen Sitzungen und Seitenwechsel wieder, Thalia-
  Konto, Sync und Downloads ok, keine Kobo-Menüs.*

- [x] **N8** Google Analytics über `initialize()` (2026-10-21). *Gerätetest des
  engeren Overrides: Statistik ok (Seitenwechsel 112 → 122, Sitzungen +2),
  aber `.kobo/Kobo/Analytics.conf` enthielt 52 GA-Hits an
  `ssl.google-analytics.com` (u. a. OpenContent/LeaveContent, 5× `book_id`,
  Akku, Schriftart). Ursache: `initialize()` muss den Kobo-Zweig nehmen
  (sonst keine Statistik), und der erzeugt `GoogleAnalyticsHandler`; dessen
  Hits laufen an `fireAnalyticsEvent()` vorbei. **Betrifft auch v0.2.0 und
  v0.1.0**; bei N4/N5 wurde nur die DB-Tabelle geprüft. Gegenmaßnahme auf
  `fix/istolino-caller-scope`: `GoogleAnalyticsRequester::apiRequest`,
  `sendRequest`, `loadQueue` auf dem Tolino stilllegen (echter `isTolino()`-
  Wert, unbekannt = stilllegen). Gerätetest 2026-10-21: Warteschlange von 52
  auf 0 geleert (WLAN aus, also nicht gesendet), bleibt auch mit WLAN und
  Sync leer; Statistik zählt weiter (Seitenwechsel 122 → 139). Hotfix
  v0.2.1 veröffentlicht (2026-10-21, Tag auf e0685d7).*

Geräte-Auswertung N1 (2026-10-19): Aus der Datenbank nicht entscheidbar, weil
die Kapitel-Wortzahlen nur im Speicher liegen (`ReadingView+0xa4`). Seit H3 hat
die Liste genau so viele Einträge wie die Spine, und `chapterIndex()` liegt
darin; die Bereichsprüfung des Originals wäre damit erfüllt. Der Override
bleibt vorerst bestehen; Risiko gering. Die Sitzungsaufzeichnung funktioniert
für sideloaded EPUBs (14 Sitzungen, 3017 s, Start/Ende paarweise).

## Log

- 2026-10-19: Datei angelegt, Branch `review/code-review-fixes` erstellt.
- 2026-10-19: Rahmen auf Firmware 5.18+ festgelegt (Vorgabe des Users).
- 2026-10-19: R1, K4 erledigt; check-symbols.py lokal grün gegen 5.18.
- 2026-10-19: M4 erledigt (Activity-Eintrag fail-closed).
- 2026-10-19: H3 erledigt (ein Eintrag pro Spine-Item, Prozent-Dekodierung).
- 2026-10-19: H1 erledigt (Konstante + Helper, Offset-Check in check-symbols.py, gegen 5.18 grün).
- 2026-10-19: H2 erledigt (thread_local, ForceKoboScope, README präzisiert).
- 2026-10-19: M5 erledigt; neue Befunde N1, N2 aufgenommen.
- 2026-10-19: KC1, KC2 (alle Hooks/dlsyms optional), K3 (README-Limits neu) erledigt.
- 2026-10-19: W1–W4 und R4 erledigt (count_words neu, 44 Host-Checks).
- 2026-10-19: M7 erledigt (BoundedZip, 8 MB/Eintrag, Tests mit Limit).
- 2026-10-19: M6 erledigt (Hintergrund-Thread, Cache, Host-Test); N3 aufgenommen.
- 2026-10-19: Suche nach Clara-BW-5.x-Firmware: öffentlich nicht verfügbar (KC3 bleibt blockiert).
- 2026-10-19: K1, K2, K5 erledigt (Kopfkommentar, „5.18+“ einheitlich, Fallbacks dokumentiert).
- 2026-10-19: M8 erledigt (Rückgabetypen korrigiert, HOOK_RETURNS-Check).
- 2026-10-19: K6, R3 erledigt (tools/nhsyms.py, ELF-Segment-Mapping, Thumb-Bit, Namenslisten).
- 2026-10-19: W5 erledigt (55 Host-Checks).
- 2026-10-19: R2 erledigt (Job wordcount-qt6; beide Jobs installieren make/g++/pkg-config explizit).
- 2026-10-19: Branch nach Codeberg gepusht (Sicherung, kein PR).
- 2026-10-19: N2, N3 erledigt (hasOnlyImages nachgebildet; Typinfo-/vtable-Checks).
- 2026-10-19: Gerätetest ok (User). Merge von [PR #1](https://codeberg.org/hortizept/nickel-reading-stats/pulls/1), Branch gelöscht. KoboReader.sqlite ausgewertet: H2b teilweise, N4 neu, N1 bewertet.
- 2026-10-20: N4 durchgeführt: Analytics entstehen nur mit Mod (H2b bestätigt). N5 als Gegenmaßnahme aufgenommen. Mod ist derzeit deinstalliert.
- 2026-10-20: N5 umgesetzt (Branch fix/drop-forced-analytics), Gerätetest steht aus.
- 2026-10-20: N5 auf dem Gerät bestätigt (keine Analytics, Statistik läuft). N6 aufgenommen.
- 2026-10-20: N1 erledigt (Override entfernt), N6 analysiert (vermutlich kein Fehler, Gerätetest offen). Branch fix/page-turns-valid-stats.
- 2026-10-20: Gerätetest N1/N6 bestanden (Statistikseite ok, Seitenwechsel +25, 1,1 Seiten/min, Analytics leer).
- 2026-10-20: Release v0.2.0 (Tag auf cdcaa94, Asset SHA-256 f36cb4fc…). N7 (Testerbericht) aufgenommen; Branch fix/istolino-caller-scope für den engeren Override.
- 2026-10-20: N7 Schritte 1–4 auf fix/istolino-caller-scope umgesetzt; Gerätetest offen.
- 2026-10-21: Gerätetest N7 ok (initialize als Sperrfunktion). N8: GA-Hits entdeckt, Requester-Hooks umgesetzt; Diagnose-Commit zurückgenommen.
- 2026-10-21: Gerätetest N8 ok (GA-Warteschlange leer, auch mit WLAN). PR für fix/istolino-caller-scope.
- 2026-10-21: [PR #4](https://codeberg.org/hortizept/nickel-reading-stats/pulls/4) gemergt (e0685d7). Release v0.2.1 veröffentlicht (Asset SHA-256 c896532b…, Download geprüft). v0.2.0-Beschreibung mit Warnhinweis „bitte auf v0.2.1 aktualisieren (Datenschutz)“ versehen. Release v0.1.0 samt Asset gelöscht, Git-Tag v0.1.0 bleibt.
- 2026-10-21: Umzug von Codeberg nach GitHub (https://github.com/dkuester/NickelReadingStats): Historie, Tags und Releases v0.2.0/v0.2.1 übertragen; CI auf GitHub Actions mit ARM-Cross-Build im Qt-6-Toolchain-Image, automatische Releases per Tag (`.github/release-notes/<tag>.md`). PR-Nummern bis #6 beziehen sich auf das archivierte Codeberg-Repo (https://codeberg.org/hortizept/nickel-reading-stats).
- 2026-10-21: KC3 erledigt (Kobo 5.18.270971 und 6.0.276679 grün, keine Codeänderung); README als „symbolgeprüft, auf Gerät ungetestet“ (KC5 teilweise).
