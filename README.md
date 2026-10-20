# NickelReadingStats

> Deutsch: [README.de.md](README.de.md)

A NickelHook mod for Tolino firmware 5.18+ (Qt 6.5.2) that restores the Kobo
"Reading Stats" and enables them for regular (sideloaded) EPUBs.

## What it does

On a Tolino, reading stats are disabled by two independent gates in
`libnickel.so.1.0.0`:

1. `Device::supportsReadingLife()` returns `!Device::isTolino()`.
   On a Tolino that is always `false`, so the entire Reading Life / stats UI
   (the "Reading stats" page and the stats tab in the reading menu) is never
   offered. This is the "Kobo mode vs. Tolino mode" difference.

2. `ReadingView::supportsReadingStats()` requires the book's MIME type to be
   `application/x-kobo-epub+zip` (kepub). Sideloaded EPUBs
   (`application/epub+zip`) therefore get no stats even on a real Kobo.
   `hasValidReadingStats()` additionally requires, on a Tolino, per-chapter
   word counts and a merged TOC/spine index within them, which regular EPUBs
   do not have.

The mod hooks:

| Hook | Symbol | Effect |
|---|---|---|
| `Device::supportsReadingLife()` | `_ZNK6Device19supportsReadingLifeEv` | Re-enable the feature on Tolino |
| `Device::supportsActivityInMore()` | `_ZNK6Device22supportsActivityInMoreEv` | Show the "Activity" entry in the More menu on Tolino |
| `Device::isTolino()` | `_ZNK6Device8isTolinoEv` | Report "not Tolino" only while reading events are tracked (see below) |
| `EventEngine::startReadingSession()` | `_ZN11EventEngine19startReadingSessionE…` | Run the Kobo session-start path on Tolino |
| `EventEngine::endReadingSession()` | `_ZN11EventEngine17endReadingSessionE…` | Run the Kobo session-end path on Tolino |
| `EventEngine::fireEvent()` | `_ZN11EventEngine9fireEventEPKcRK6Vol…` | Run the page-turn event entry on Tolino |
| `EventEngine::fireEventSlot()` | `_ZN11EventEngine13fireEventSlotE10QByt…` | Record page turns/events on Tolino (pages per minute) |
| `EventEngine::fireAnalyticsEvent()` | `_ZN11EventEngine18fireAnalyticsEvent…` | Drop analytics events on Tolino (see "Privacy") |
| `ReadingView::supportsReadingStats()` | `_ZNK11ReadingView20supportsReadingStatsEv` | Allow stats for regular EPUBs |
| `ReadingView::updateWordCounts()` | `_ZN11ReadingView16updateWordCountsEv` | Retrofit word counting for regular EPUBs (see below) |
| `ReadingView::mergedTOCAndSpineIndex()` | `_ZN11ReadingView22mergedTOCAndSpineIndexEv` | Use the raw spine index so read chapters are highlighted |
| `FeatureSettings::bookSpecificStatsEnabled()` | `_ZN15FeatureSettings24bookSpecificStatsEnabledEv` | Force per-book stats on |

The three metrics "hours read", "minutes per session" and "pages per minute"
are fed by the reading-session tracking (`startReadingSession`/
`endReadingSession`). Those check `isTolino()` internally and skip the entire
Kobo recording on a Tolino. While the hook runs them, `isTolino()` returns
`false` under three conditions at once: on the calling thread only
(thread-local flag), only for the duration of those calls, and only when it
is called **directly from one of the gate functions** (`startReadingSession`,
`endReadingSession`, `fireEvent`, `fireEventSlot`), recognised by the return
address. Everything else keeps the real value: other threads, the Tolino
store/cloud/DRM, and also everything the gates call further down. On 5.18
that includes `EventEngine::initialize()`, which would otherwise set up Kobo's
Google Analytics session, and the affiliate lookup (Thalia vs. Kobo).
`tools/check-symbols.py` verifies that each gate calls `isTolino()` itself and
that none calls a function reaching it by a tail call.

Page turns are recorded separately: `ReadingView::onPageTurned()` calls
`EventEngine::fireEvent()`, which schedules `EventEngine::fireEventSlot()`.
Both check `isTolino()` and bail out on a Tolino, so the hook runs both with
the same flag so page turns are counted.

Important: the "Activity" entry depends on `supportsActivityInMore()`, which
returns `false` on a Tolino because of `isTolino()` — independently of
`supportsReadingLife()`. That is why the entry stayed hidden at first. The hook
re-runs the original check (`!isTolino() && deviceClass == 2 &&
!ParentalControl`) but drops only the `isTolino()` gate; the device-class and
parental-control guards remain.

Which books get stats: the hooks call the original function first and keep a
`true`. Otherwise they apply the original `supportsReadingStats()` rule with
only its kepub requirement replaced by "plain EPUB" (`application/epub+zip`):
the content's accessibility state must not be 6, the book must not consist of
images only (`hasOnlyImages()`), and it must not be fixed-layout. Fixed-layout content (fixed-layout EPUBs, PDF, comics) and other
formats stay excluded, and a kepub that Nickel rejects stays rejected.

## Privacy

Opening the Kobo reading-life paths on a Tolino would also open Kobo's
analytics: measured on Tolino 5.18, the mod made Nickel queue events such as
`OpenContent`/`leave_content` with title, author, ISBN and reading time for
sideloaded books, which are uploaded on the next sync (if analytics are
allowed in the privacy settings). Without the mod a Tolino queued none. The
hook on `EventEngine::fireAnalyticsEvent()` therefore drops every analytics
event on a Tolino, matching the stock behaviour; the reading-life data that
feeds the stats is stored separately on the device and is unaffected. On a
Kobo, analytics are left alone.

## Word counting for regular EPUBs

`ReadingView::updateWordCounts()` is the kepub-only gate that produces the
per-chapter word counts behind the chapter bars and the "time left" estimate.
It returns immediately for anything that is not a kepub. The hook calls the
original first (kepub and Instapaper run unchanged), then counts the words for
a sideloaded EPUB itself:

1. Check the MIME type (`application/epub+zip`) and exclude fixed-layout.
2. Derive the EPUB file path from the content ID (`file://…`).
3. Open the EPUB zip with `QZipReader`, locate the OPF
   (`META-INF/container.xml`, fallback `*.opf`) and parse the spine order
   (manifest + spine).
4. For each spine file, read the XHTML, strip tags/scripts/styles and count
   the words.
5. Deliver the `QList<int>` to Nickel via `ReadingView::gotWordCounts()`.

Steps 3–4 run on a single background thread, like Nickel's own kepub word
counter, so opening a book never blocks the UI. The result is delivered on the
GUI thread, only if the view still exists and still shows the same book.
Results are cached per file (invalidated when size or mtime change), and each
zip entry is capped at 8 MB uncompressed.

This fills the chapter bars and makes the time-based estimates plausible for
regular EPUBs too. Note: a book made of a single large HTML file yields exactly
one bar — that is correct, not an empty view.

Whether a book's stats page is offered is left to Nickel's own
`hasValidReadingStats()`: it needs the word counts and a current chapter
within them. For a plain EPUB that holds as soon as the background count has
delivered (usually a moment after opening the book); a book whose count fails
gets no per-book stats rather than an empty or misaligned view.

## Limits ("robust" here means: degrade gracefully, don't crash)

Every hook and `dlsym` target is marked optional: if a 5.18+ build lacks a
symbol, only the feature that needs it is lost instead of the whole mod
(NickelHook logs each unresolved symbol). Checks that cannot be evaluated fail
closed — no stats, no Activity entry — rather than open. Existing behaviour
(e.g. kepub on Kobo) is unchanged because the original function always runs
first.

## Building

Prerequisite: the Docker image `strayrose/nickeltc:latest`
(`arm-kobo-linux-gnueabihf` toolchain, Qt 6.5.2).

    cd hook
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make kobo

## Testing

Three levels, from cheap to thorough:

1. **Host unit tests** (`hook/tests/`) — four binaries, all against the host
   Qt:

   - `test_wordcount` exercises the pure logic (tokenising, script/style
     stripping, entity decoding, manifest-href resolution, OPF spine parsing)
     via `wordcount_logic.h`. QtCore only, no private headers.
   - `test_epub_words` runs the real `count_epub_words()` — QZipReader
     included — against a fixture EPUB (`fixtures/sample.epub`) and asserts the
     exact per-chapter counts. It needs QtGui's private `qzipreader_p.h`, so it
     only builds where those private headers are installed.
   - `test_hook_gates` checks the analytics drop (Tolino vs. Kobo), the scoped
     `isTolino()` override and the fail-closed Activity entry.
   - `test_hook_async` includes `readingstats.cc` with fake libnickel
     function pointers and checks the background word counting: delivery,
     cache, and that a view closed or reused for another book meanwhile gets
     nothing. Same private-header requirement.

   Run all with:

       make -C hook/tests check

2. **Local symbol & ABI check** (`tools/check-symbols.py`) — verifies every
   hook (`.sym`) and dlsym (`.name`) symbol exists in the target firmware, and
   that each dlsym return convention (sret/value vs. reference/pointer) matches
   the function declared in the hook. This exact convention was the reason for
   the book-open crash: `Content::getMimeType()` returns `QByteArray&`, not
   `QByteArray` by value. It also checks the hard-coded `ReadingView` →
   `Content` offset (`kReadingViewContentOffset`, 0x94 on 5.18) against the
   instruction in `ReadingView::updateWordCounts()` that computes it, that
   `ReadingView`'s first base is still `QWidget` at offset 0 (the hook uses
   the view as a `QObject`), and that the virtual call in the original
   `supportsReadingStats()` still resolves to `hasOnlyImages()` — via the
   typeinfo/vtable relocations. Needs the
   proprietary firmware files:

       python3 tools/check-symbols.py \
           --source hook/src/readingstats.cc \
           --defsyms <defsyms.txt> \
           --firmware <libnickel.so.1.0.0>

   Generate `defsyms.txt` with `llvm-nm -D --defined-only libnickel.so.1.0.0`
   (addresses are mapped through the ELF load segments; a bare name list
   also works but only checks existence, reported as `PARTIAL`).
   `defsyms.txt` and `libnickel.so.1.0.0` come from the Tolino firmware
   (proprietary) and are deliberately not in the repo — pass the local path.

3. **CI** (`.forgejo/workflows/ci.yml`) — on every push it runs the host unit
   tests twice, against Qt5 and against Qt6, plus the firmware-independent manifest check
   (`tools/check-manifest.py`), which verifies the `.sym`/`.name` symbols in
   the source match `tools/symbols.txt` (firmware-verified) exactly. The
   ARM/Qt6 cross-compile build stays local (the toolchain image is ~3.4 GB, too
   heavy for Codeberg's runners).

## Installing

Put `hook/Kobo.tgz` into `.kobo/` on the device and restart Nickel.
To uninstall: create an empty file `readingstats_uninstall` in the device root
and restart Nickel.

## Sources & acknowledgements

This mod stands on the shoulders of the following projects:

- **NickelHook** (MIT, Copyright (c) 2020–2025 Patrick Gaskin) — the hook
  framework this mod is built on. Used as a git submodule in the
  `qt6-support` fork by **Nico Verbruggen** (`nicoverbruggen/NickelHook`),
  which adds Qt 6 support.
- **Qt 6.5.2** (The Qt Project / The Qt Company) — `QZipReader` (private API,
  LGPL-3.0/GPL-2.0/GPL-3.0) for reading the EPUB zip archives, plus
  `QXmlStreamReader` and `QRegularExpression` for parsing and counting.
- **strayrose/nickeltc** — Docker cross-compile toolchain
  (`arm-kobo-linux-gnueabihf`, GCC 11.5, Qt 6.5.2) used to build the hook.
- **Kobo/Tolino firmware** (`libnickel.so.1.0.0`, Tolino 5.18) — the target
  binary. The internal classes (`Device`, `ReadingView`, `Content`,
  `EventEngine`) were reverse-engineered via symbol analysis and Capstone
  disassembly.

## AI assistance

This mod was reverse-engineered and built with the help of an AI: the analysis
of the firmware gates, the disassembly of the target classes and the
implementation of the hooks were done in dialogue with an AI assistant (Hermes
Agent, Nous Research). Technical review, on-device testing and sign-off were
done by a human.
