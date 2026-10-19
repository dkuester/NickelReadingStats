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
- [ ] **H2b** Auf dem Gerät prüfen, ob der Kobo-Event-Pfad Analytics/Netzwerk an
  Kobo auslöst (Netzwerk-Mitschnitt oder `AnalyticsEvents`-Tabelle). *(braucht Gerät)*
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
- [ ] **M6** Wortzählung blockiert den UI-Thread: Cache (Pfad + mtime +
  Größe) und/oder Hintergrund-Thread.
- [ ] **M7** Entpacken begrenzen: Einträge über einem Limit (z. B. 16 MB)
  überspringen bzw. mit `0` zählen.
- [ ] **M8** Hook-Signaturen dokumentieren und prüfen (Rückgabetyp von
  `start/endReadingSession`, Wertparameter von `fireEventSlot`).

## Genauigkeit der Wortzählung

- [ ] **W1** Nur `<body>` zählen (nicht `<title>`/`<head>`).
- [ ] **W2** Self-closing `<script …/>` darf keinen Text verschlucken;
  `<!-- … -->` vor dem Tag-Strip entfernen.
- [ ] **W3** Reine Satzzeichen-Tokens nicht als Wort zählen (`* * *`, `—`).
- [ ] **W4** Weitere Entities: `&#xa0;`, `&thinsp;`, `&ensp;`, `&emsp;`.
- [ ] **W5** Kleinkram: Encoding-Erkennung, `linear="no"`, `join_path`
  normalisiert bei leerem `opf_dir` nicht.

## Konsistenz und Doku

- [ ] **K1** Kopfkommentar in `readingstats.cc` („two gates“ vs. 3, Hook-Liste
  unvollständig).
- [ ] **K2** Versionsangaben vereinheitlichen („5.18+/6.x“ vs. „5.18“).
- [x] **K3** README: doppelter Fixed-Layout-Absatz.
- [x] **K4** `chapterIndex` fehlt in `EXPECTED` von `check-symbols.py`;
  Symbole ohne Erwartung sollen als Fehler zählen. *Gegen Tolino-5.18-
  `libnickel` (BuildID bcfe1a2b…) geprüft: 19/19 Symbole, 7/7 Konventionen OK.*
- [ ] **K5** Tote Fallbacks (`!Device__isTolino` → `true`, `mergedTOC` → `0`)
  aufräumen oder kommentieren.
- [ ] **K6** `extract()` aus beiden Python-Tools in ein gemeinsames Modul.

## Repo, CI, Tools

- [x] **R1** `tools/__pycache__/*.pyc` aus Git entfernen und ignorieren.
- [ ] **R2** CI-Job mit Qt6 zusätzlich zu Qt5.
- [ ] **R3** `check-symbols.py`: VMA ≠ Datei-Offset und Thumb-Bit prüfen
  *(für Tolino 5.18 unkritisch: `llvm-nm` liefert gerade Adressen, alle
  Klassifikationen stimmen; für andere Binaries absichern)*;
  `defsyms.txt` mit nur einer Spalte (reine Namensliste) unterstützen.
- [~] **R4** Fixture erweitern: `mimetype`, `<title>`, Unterordner,
  prozent-kodierter Href, fehlende Spine-Datei. *Erledigt: `mimetype`,
  prozent-kodierter Href, fehlende Spine-Datei, Generator
  `hook/tests/fixtures/make_sample_epub.py`. Offen: `<title>`, Unterordner
  (zusammen mit W1).*

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
  *Blockiert: Kobo-5.x-Firmware liegt lokal nicht vor; Download nur mit Freigabe.*
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
- [ ] **N2** Virtuelle Methode an vtable+0x10c in `supportsReadingStats`
  identifizieren (vtable von `ReadingView` auflösen) und ggf. nachbilden.

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
