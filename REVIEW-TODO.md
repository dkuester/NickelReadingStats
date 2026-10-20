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
- [!] **KC3** Symbole, Return-Konventionen und Offset `0x94` gegen eine
  **Kobo**-5.x-`libnickel` prüfen (`check-symbols.py`).
  *Blockiert: Kobo-5.x-Firmware liegt lokal nicht vor. Stand 2026-10-19:
  Kobos Update-API (`UpgradeCheck`, Affiliates kobo/beta/fnac/rakutenbooks)
  und pgaskins Firmware-Liste bieten für den Clara BW N365 (ID …391) nur bis
  4.46.23836 (Aug. 2026, Qt5) an, für keine Geräte-ID eine 5.x. Einziger Weg:
  `libnickel.so.1.0.0` von einem Gerät ziehen, auf dem bereits 5.x läuft.*
- [!] **KC4** Smoke-Test auf einem Kobo mit 5.x. *Blockiert: kein Gerät mit 5.x.*
- [ ] **KC5** README/Beschreibung: Kobo 5.18+ erst nach KC3/KC4 als
  unterstützt nennen.

## Neue Befunde während der Umsetzung

- [ ] **N1** `hasValidReadingStats` prüft im Original auf Tolino zusätzlich
  `wordCountsReady()` und `0 <= mergedTOCAndSpineIndex() < wordCounts.size()`
  (Feld `+0xac`). Der Hook überspringt diese Bereichsprüfung. Seit H3 und dem
  `mergedTOC`-Fallback könnte das Original für plain epubs selbst `true`
  liefern; auf dem Gerät prüfen, ob der Override noch nötig ist bzw. die
  Bereichsprüfung nachbilden (Absturzrisiko bei Index ≥ Größe).
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
- [ ] **N6** `PagesTurnedThisSession` ist in allen ausgewerteten Sitzungen 0
  (auch vor N5). Prüfen, ob im Test umgeblättert wurde; falls ja, erreicht der
  Seitenwechsel-Pfad (`fireEvent` → `fireEventSlot` → `PageTurnEventHandler`)
  die Erfassung nicht, und „Seiten pro Minute“ bleibt leer.

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
- 2026-10-19: Gerätetest ok (User). Merge von PR #1, Branch gelöscht. KoboReader.sqlite ausgewertet: H2b teilweise, N4 neu, N1 bewertet.
- 2026-10-20: N4 durchgeführt: Analytics entstehen nur mit Mod (H2b bestätigt). N5 als Gegenmaßnahme aufgenommen. Mod ist derzeit deinstalliert.
- 2026-10-20: N5 umgesetzt (Branch fix/drop-forced-analytics), Gerätetest steht aus.
- 2026-10-20: N5 auf dem Gerät bestätigt (keine Analytics, Statistik läuft). N6 aufgenommen.
