# NickelReadingStats

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
| `ReadingView::supportsReadingStats()` | `_ZNK11ReadingView20supportsReadingStatsEv` | Statistik für normale EPUBs zulassen |
| `ReadingView::hasValidReadingStats()` | `_ZN11ReadingView20hasValidReadingStatsEv` | Tolino-only TOC/Spine-Prüfung entschärfen |
| `ReadingView::updateWordCounts()` | `_ZN11ReadingView16updateWordCountsEv` | Wortzählung für normale EPUBs nachrüsten (siehe unten) |
| `FeatureSettings::bookSpecificStatsEnabled()` | `_ZN15FeatureSettings24bookSpecificStatsEnabledEv` | Per-Book-Statistik forcieren |

Die drei Kennzahlen „Stunden gelesen", „Minuten pro Sitzung" und „Seiten pro
Minute" speist die Lesesitzungs-Erfassung (`startReadingSession`/
`endReadingSession`). Die prüfen intern `isTolino()` und überspringen auf dem
Tolino die komplette Kobo-Erfassung. Der Hook führt sie mit einem
`g_force_kobo_session`-Flag aus, das `isTolino()` nur während dieser zwei
Aufrufe `false` liefern lässt — der Rest des Geräts (Tolino-Shop/-Cloud/-DRM)
behält sein normales Verhalten.

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

## Installieren

`hook/Kobo.tgz` nach `.kobo/` auf dem Gerät legen und Nickel neu starten.
Deinstallieren: leere Datei `readingstats_uninstall` im Geräteroot ablegen
und Nickel neu starten.
