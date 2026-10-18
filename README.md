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
   `hasValidReadingStats()` additionally re-checks the Tolino flag and a
   merged TOC/spine index that regular EPUBs do not have.

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
| `ReadingView::supportsReadingStats()` | `_ZNK11ReadingView20supportsReadingStatsEv` | Allow stats for regular EPUBs |
| `ReadingView::hasValidReadingStats()` | `_ZN11ReadingView20hasValidReadingStatsEv` | Drop the Tolino-only TOC/spine validation |
| `ReadingView::updateWordCounts()` | `_ZN11ReadingView16updateWordCountsEv` | Retrofit word counting for regular EPUBs (see below) |
| `ReadingView::mergedTOCAndSpineIndex()` | `_ZN11ReadingView22mergedTOCAndSpineIndexEv` | Use the raw spine index so read chapters are highlighted |
| `FeatureSettings::bookSpecificStatsEnabled()` | `_ZN15FeatureSettings24bookSpecificStatsEnabledEv` | Force per-book stats on |

The three metrics "hours read", "minutes per session" and "pages per minute"
are fed by the reading-session tracking (`startReadingSession`/
`endReadingSession`). Those check `isTolino()` internally and skip the entire
Kobo recording on a Tolino. The hook runs them with a `g_force_kobo_session`
flag that makes `isTolino()` return `false` only during those calls — the rest
of the device (Tolino store/cloud/DRM) keeps its normal behaviour.

Page turns are recorded separately: they are dispatched through
`EventEngine::fireEventSlot()`, which also checks `isTolino()` and bails out on
a Tolino. The hook runs it with the same flag so page turns are counted.

Important: the "Activity" entry depends on `supportsActivityInMore()`, which
returns `false` on a Tolino because of `isTolino()` — independently of
`supportsReadingLife()`. That is why the entry stayed hidden at first. The hook
re-runs the original check (`!isTolino() && deviceClass == 2 &&
!ParentalControl`) but drops only the `isTolino()` gate; the device-class and
parental-control guards remain.

Fixed-layout content (fixed-layout EPUBs, PDF, comics) is deliberately
excluded — `ReadingView::isFixedLayout()` is resolved via `dlsym` for that. The
hooks call the original function first and only intervene when Nickel itself
returns `false`, so existing behaviour (e.g. kepub on Kobo) is unchanged.

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

This fills the chapter bars and makes the time-based estimates plausible for
regular EPUBs too. Note: a book made of a single large HTML file yields exactly
one bar — that is correct, not an empty view.

## Limits ("robust" here means: degrade gracefully, don't crash)

Fixed-layout content (fixed-layout EPUBs, PDF, comics) is deliberately
excluded — `ReadingView::isFixedLayout()` is resolved via `dlsym` for that.
Word counting only runs when all dlsym targets are present; if one is missing
it is silently skipped rather than crashing. The hooks call the original
function first and only intervene when Nickel provides nothing, so existing
behaviour (e.g. kepub on Kobo) is unchanged.

## Building

Prerequisite: the Docker image `strayrose/nickeltc:latest`
(`arm-kobo-linux-gnueabihf` toolchain, Qt 6.5.2).

    cd hook
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make
    docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" strayrose/nickeltc make kobo

## Testing

Three levels, from cheap to thorough:

1. **Host unit tests** (`hook/tests/`) — exercise the word-counting logic
   (tokenising, script/style stripping, entity decoding, manifest-href
   resolution, OPF spine parsing) against the host Qt. No QZipReader and no
   cross toolchain, so it runs on any machine with a Qt dev install:

       make -C hook/tests check

2. **Local symbol & ABI check** (`tools/check-symbols.py`) — verifies every
   hook (`.sym`) and dlsym (`.name`) symbol exists in the target firmware, and
   that each dlsym return convention (sret/value vs. reference/pointer) matches
   the function declared in the hook. This exact convention was the reason for
   the book-open crash: `Content::getMimeType()` returns `QByteArray&`, not
   `QByteArray` by value. Needs the proprietary firmware files:

       python3 tools/check-symbols.py \
           --source hook/src/readingstats.cc \
           --defsyms <defsyms.txt> \
           --firmware <libnickel.so.1.0.0>

   `defsyms.txt` and `libnickel.so.1.0.0` come from the Tolino firmware
   (proprietary) and are deliberately not in the repo — pass the local path.

3. **CI** (`.forgejo/workflows/ci.yml`) — on every push it installs Qt5 and
   runs the host unit tests, plus the firmware-independent manifest check
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
