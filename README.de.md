# NickelReadingStats

> English: [README.md](README.md)

NickelHook-Mod für Tolino-Firmware 5.18+ (Qt 6.5.2), der die
Kobo-„Reading Stats" (Lesestatistik) wiederherstellt und für normale
(sideloaded) EPUBs freischaltet.

## Was das Mod tut

Auf einem Tolino ist die Lesestatistik durch zwei unabhängige Gatter in
`libnickel.so.1.0.0` deaktiviert:

1. `Device::supportsReadingLife()` liefert `!Device::isTolino()` zurück.
   Auf einem Tolino also immer `false` — die komplette Reading-Life-/
   Statistik-UI („Reading stats"-Seite, Statistik-Tab im Lesemenü) wird nie
   angeboten. Das ist der Unterschied „Kobo-Modus vs. Tolino-Modus".

2. `ReadingView::supportsReadingStats()` verlangt den MIME-Typ
   `application/x-kobo-epub+zip` (kepub). Sideloaded EPUBs
   (`application/epub+zip`) bekommen daher auch auf einem echten Kobo keine
   Statistik. `hasValidReadingStats()` prüft zusätzlich den Tolino-Flag und
   einen merged-TOC/Spine-Index, den normale EPUBs nicht besitzen.

Das Mod hookt:

| Hook | Symbol | Wirkung |
|---|---|---|
| `Device::supportsReadingLife()` | `_ZNK6Device19supportsReadingLifeEv` | Feature auf Tolino wieder aktivieren |
| `Device::supportsActivityInMore()` | `_ZNK6Device22supportsActivityInMoreEv` | „Aktivität"-Eintrag im Mehr-Menü auf Tolino anzeigen |
| `Device::isTolino()` | `_ZNK6Device8isTolinoEv` | meldet nur während der Session-Erfassung „kein Tolino" (siehe unten) |
| `EventEngine::startReadingSession()` | `_ZN11EventEngine19startReadingSessionE…` | Kobo-Session-Start auf Tolino ausführen |
| `EventEngine::endReadingSession()` | `_ZN11EventEngine17endReadingSessionE…` | Kobo-Session-Ende auf Tolino ausführen |
| `EventEngine::fireEvent()` | `_ZN11EventEngine9fireEventEPKcRK6Vol…` | Seitenumbruch-Einstieg auf Tolino ausführen |
| `EventEngine::fireEventSlot()` | `_ZN11EventEngine13fireEventSlotE10QByt…` | Seitenumbrüche/Events auf Tolino erfassen (Seiten pro Minute) |
| `ReadingView::supportsReadingStats()` | `_ZNK11ReadingView20supportsReadingStatsEv` | Statistik für normale EPUBs zulassen |
| `ReadingView::hasValidReadingStats()` | `_ZN11ReadingView20hasValidReadingStatsEv` | Tolino-only TOC/Spine-Prüfung entschärfen |
| `ReadingView::updateWordCounts()` | `_ZN11ReadingView16updateWordCountsEv` | Wortzählung für normale EPUBs nachrüsten (siehe unten) |
| `ReadingView::mergedTOCAndSpineIndex()` | `_ZN11ReadingView22mergedTOCAndSpineIndexEv` | Rohen Spine-Index verwenden, damit gelesene Kapitel hervorgehoben werden |
| `FeatureSettings::bookSpecificStatsEnabled()` | `_ZN15FeatureSettings24bookSpecificStatsEnabledEv` | Per-Book-Statistik forcieren |

Die drei Kennzahlen „Stunden gelesen", „Minuten pro Sitzung" und „Seiten pro
Minute" speist die Lesesitzungs-Erfassung (`startReadingSession`/
`endReadingSession`). Die prüfen intern `isTolino()` und überspringen auf dem
Tolino die komplette Kobo-Erfassung. Der Hook führt sie mit einem
`g_force_kobo_session`-Flag aus, das `isTolino()` nur während dieser zwei
Aufrufe `false` liefern lässt — der Rest des Geräts (Tolino-Shop/-Cloud/-DRM)
behält sein normales Verhalten.

Seitenumbrüche werden separat erfasst: Sie laufen über
`EventEngine::fireEventSlot()`, das ebenfalls `isTolino()` prüft und auf dem
Tolino abbricht. Der Hook führt es mit demselben Flag aus, damit Seitenwechsel
gezählt werden.

Wichtig: Der „Aktivität"-Eintrag hängt an `supportsActivityInMore()`, das auf
Tolino wegen `isTolino()` sofort `false` liefert — unabhängig von
`supportsReadingLife()`. Deshalb blieb der Eintrag zunächst versteckt. Der
Hook führt die ursprüngliche Prüfung (`!isTolino() && deviceClass == 2 &&
!ParentalControl`) erneut aus, lässt aber nur die `isTolino()`-Sperre weg;
Geräteklasse und Jugendschutz bleiben erhalten.

Fixed-Layout-Inhalte (Fixed-Layout-EPUBs, PDF, Comics) bleiben bewusst
ausgeschlossen — dafür wird `ReadingView::isFixedLayout()` via `dlsym`
aufgelöst. Die Hooks rufen zuerst die Originalfunktion auf und greifen nur
ein, wenn Nickel selbst `false` liefert; dadurch bleibt bestehendes
Verhalten (z. B. kepub auf Kobo) unverändert.

## Wortzählung für normale EPUBs

`ReadingView::updateWordCounts()` ist das kepub-only-Gatter, das die
Wortzählung pro Kapitel erzeugt — die Grundlage für das Kapitel-Balkendiagramm
und die „verbleibende Zeit"-Schätzung. Es kehrt für alles, was kein kepub ist,
sofort um. Der Hook ruft zuerst die Originalfunktion auf (kepub und Instapaper
laufen unverändert) und zählt danach für ein sideloaded EPUB die Wörter selbst:

1. MIME-Typ prüfen (`application/epub+zip`) und Fixed-Layout ausschließen.
2. Den Dateipfad des EPUBs aus der Content-ID (`file://…`) ableiten.
3. Das EPUB-Zip mit `QZipReader` öffnen, die OPF lokalisieren
   (`META-INF/container.xml`, Fallback `*.opf`) und die Spine-Reihenfolge
   parsen (Manifest + Spine).
4. Pro Spine-Datei das XHTML lesen, Tags/Skripte/Styles entfernen und die
   Wörter zählen.
5. Die `QList<int>` über `ReadingView::gotWordCounts()` an Nickel liefern.

Damit füllt sich auch bei normalen EPUBs das Kapitel-Balkendiagramm und die
zeitbasierten Schätzungen werden plausibel. Randnotiz: Bücher, die aus einer
einzigen großen HTML-Datei bestehen, ergeben genau einen Balken — das ist
korrekt, keine leere Anzeige.

## Grenzen („robust" heißt hier: sauber degradieren, nicht abstürzen)

Fixed-Layout-Inhalte (Fixed-Layout-EPUBs, PDF, Comics) bleiben bewusst
ausgeschlossen — dafür wird `ReadingView::isFixedLayout()` via `dlsym`
aufgelöst. Die Wortzählung wird nur angestoßen, wenn alle dlsym-Ziele
vorhanden sind; fehlt etwas, wird sie still übersprungen statt zu crashen.
Die Hooks rufen zuerst die Originalfunktion auf und greifen nur ein, wenn
Nickel selbst nichts liefert; dadurch bleibt bestehendes Verhalten
(z. B. kepub auf Kobo) unverändert.

## Bauen

Voraussetzung: Docker-Image `strayrose/nickeltc:latest`
(`arm-kobo-linux-gnueabihf`-Toolchain, Qt 6.5.2).

    cd hook
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make kobo

## Testen

Drei Ebenen, von günstig zu gründlich:

1. **Host-Unit-Tests** (`hook/tests/`) — zwei Binaries, beide gegen das
   Host-Qt:

   - `test_wordcount` prüft die pure Logik (Tokenisierung, Script-/Style-
     Entfernung, Entity-Decoding, Manifest-href-Auflösung, OPF-Spine-Parsing)
     über `wordcount_logic.h`. Nur QtCore, keine privaten Header.
   - `test_epub_words` führt das echte `count_epub_words()` — inklusive
     QZipReader — gegen ein Fixture-EPUB (`fixtures/sample.epub`) aus und
     prüft die exakten Wortzahlen pro Kapitel. Braucht QtGuis privaten
     `qzipreader_p.h` und baut nur, wo diese privaten Header installiert sind.

   Beide laufen mit:

       make -C hook/tests check

2. **Lokaler Symbol- & ABI-Check** (`tools/check-symbols.py`) — verifiziert,
   dass jedes Hook- (`.sym`) und dlsym-Symbol (`.name`) in der Ziel-Firmware
   existiert und jede dlsym-Rückgabe-Konvention (sret/Wert vs. Referenz/
   Pointer) zur deklarierten Funktion passt. Genau diese Konvention war der
   Absturz-Grund beim Buchöffnen: `Content::getMimeType()` gibt `QByteArray&`
   zurück, nicht `QByteArray` per Wert. Außerdem prüft er den fest
   eingetragenen Offset `ReadingView` → `Content`
   (`kReadingViewContentOffset`, 0x94 auf 5.18) gegen die Instruktion in
   `ReadingView::updateWordCounts()`, die ihn berechnet. Braucht die
   proprietären Firmware-Dateien:

       python3 tools/check-symbols.py \
           --source hook/src/readingstats.cc \
           --defsyms <defsyms.txt> \
           --firmware <libnickel.so.1.0.0>

   `defsyms.txt` und `libnickel.so.1.0.0` stammen aus der Tolino-Firmware
   (proprietär) und liegen bewusst nicht im Repo — den lokalen Pfad angeben.

3. **CI** (`.forgejo/workflows/ci.yml`) — installiert bei jedem Push Qt5 und
   führt die Host-Unit-Tests aus, plus den firmware-unabhängigen
   Manifest-Check (`tools/check-manifest.py`), der die `.sym`/`.name`-Symbole
   im Quelltext mit `tools/symbols.txt` (firmware-verifiziert) abgleicht. Der
   ARM/Qt6-Cross-Compile-Build bleibt lokal (das Toolchain-Image ist ~3,4 GB,
   zu schwer für Codebergs Runner).

## Installieren

`hook/Kobo.tgz` nach `.kobo/` auf dem Gerät legen und Nickel neu starten.
Deinstallieren: leere Datei `readingstats_uninstall` im Geräteroot ablegen
und Nickel neu starten.

## Quellen & Danksagung

Der Mod steht auf den Schultern folgender Projekte:

- **NickelHook** (MIT, Copyright (c) 2020–2025 Patrick Gaskin) — das
  Hook-Framework, auf dem der Mod aufbaut. Verwendet als Git-Submodul in der
  `qt6-support`-Fork von **Nico Verbruggen** (`nicoverbruggen/NickelHook`), die
  Qt-6-Unterstützung ergänzt.
- **Qt 6.5.2** (The Qt Project / The Qt Company) — `QZipReader` (private API,
  LGPL-3.0/GPL-2.0/GPL-3.0) zum Lesen der EPUB-Zip-Archive sowie
  `QXmlStreamReader` und `QRegularExpression` fürs Parsen und Zählen.
- **strayrose/nickeltc** — Docker-Cross-Compile-Toolchain
  (`arm-kobo-linux-gnueabihf`, GCC 11.5, Qt 6.5.2), mit der das Hook gebaut wird.
- **Kobo/Tolino-Firmware** (`libnickel.so.1.0.0`, Tolino 5.18) — das Ziel-Binary.
  Die internen Klassen (`Device`, `ReadingView`, `Content`, `EventEngine`) wurden
  per Symbolanalyse und Capstone-Disassembly reverse-engineered.

## KI-Unterstützung

Dieses Mod wurde mit Hilfe einer KI reverse-engineered und gebaut: die Analyse
der Firmware-Gatter, das Disassemblieren der Zielklassen und die
Implementierung der Hooks entstanden im Dialog mit einem KI-Assistenten
(Hermes Agent, Nous Research). Fachliche Kontrolle, On-Device-Tests und
Freigabe lagen bei einem Menschen.
