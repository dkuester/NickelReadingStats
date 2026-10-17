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
| `ReadingView::supportsReadingStats()` | `_ZNK11ReadingView20supportsReadingStatsEv` | Statistik für normale EPUBs zulassen |
| `ReadingView::hasValidReadingStats()` | `_ZN11ReadingView20hasValidReadingStatsEv` | Tolino-only TOC/Spine-Prüfung entschärfen |
| `FeatureSettings::bookSpecificStatsEnabled()` | `_ZN15FeatureSettings24bookSpecificStatsEnabledEv` | Per-Book-Statistik forcieren |

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

## Grenzen („robust" heißt hier: sauber degradieren, nicht abstürzen)

Die Kapitelgrößen (Balkendiagramm) und die „verbleibende Zeit"-Schätzungen
brauchen Wortzählungen pro Kapitel, die `KepubBookWordCounter` nur für
kepubs erzeugt. Normale EPUBs werden vom Adobe-Renderer (RMSDK) dargestellt
und haben diese Daten nicht. Für EPUBs zeigt die Statistik daher:

- funktioniert: Fortschritt in %, gelesene Zeit, umgeblätterte Seiten
- leer/0: Kapitel-Balkendiagramm und zeitbasierte Schätzungen

Das entspricht exakt dem Verhalten eines echten Kobo mit sideloaded EPUBs.
Die Wortzählung für EPUBs nachzurüsten wäre eine eigene Feature-Entwicklung
(EPUB-HTML parsen), nicht Teil dieses Mods.

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
