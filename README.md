<div align="center">

# NickelReadingStats

**Kobo's reading statistics on your Tolino, for sideloaded EPUBs too.**

[![Release](https://img.shields.io/github/v/release/dkuester/NickelReadingStats?label=release)](https://github.com/dkuester/NickelReadingStats/releases/latest)
[![CI](https://github.com/dkuester/NickelReadingStats/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dkuester/NickelReadingStats/actions/workflows/ci.yml)
[![Firmware](https://img.shields.io/badge/firmware-5.18%2B%20(Qt%206)-informational)](#compatibility)
[![License](https://img.shields.io/github/license/dkuester/NickelReadingStats)](LICENSE)

[Installation](#installation) · [Features](#features) · [Privacy](#privacy) · [How it works](docs/ARCHITECTURE.md) · [Development](docs/DEVELOPMENT.md) · [Releases](https://github.com/dkuester/NickelReadingStats/releases) · [Deutsch](README.de.md)

</div>

Tolino e-readers run the same software as Kobo devices, but Kobo's "Reading
Stats" are switched off on them, and even on a Kobo they only work for
kepub files. NickelReadingStats is a [NickelHook](https://github.com/pgaskin/NickelHook)
mod that turns them back on, adds them to regular EPUBs, and keeps your Tolino
a Tolino: store, cloud, DRM and privacy behave as before.

> [!IMPORTANT]
> **Using v0.1.0 or v0.2.0? Please update to [v0.2.1](https://github.com/dkuester/NickelReadingStats/releases/tag/v0.2.1).**
> Those versions let Kobo's analytics run on the Tolino and could upload
> reading events to Google. v0.2.1 stops this and deletes entries that have
> not been sent yet. See [Privacy](#privacy).

---

## Features

| Feature | Description |
|---|---|
| **Book statistics** | Per-book page in the reading menu with chapter bars. Chapters already read are highlighted, and "time left" estimates are shown. |
| **Activity** | "Activity" entry in the More menu: hours read, minutes per session, pages per minute. |
| **Regular EPUBs** | Works for sideloaded EPUBs, not just kepubs. Word counts are computed in the background and cached per book. |
| **Tolino stays Tolino** | Kobo behaviour is enabled only for the few functions that record reading time and page turns. The Thalia (or other bookseller) account, sync and downloads are unaffected. |
| **Privacy** | Kobo's analytics and Google Analytics are silenced on the Tolino. |
| **Fails safe** | Every hook is optional and degrades gracefully: if something does not match the firmware, a statistic is missing, not your device's behaviour. |

## Compatibility

| Device / firmware | Status |
|---|---|
| Tolino, firmware 5.18+ (Qt 6) | ✅ Tested (5.18.264769) |
| Kobo, firmware 5.x (Qt 6) | ⚠️ Expected to work, not verified yet |
| Kobo / Tolino, firmware 4.x (Qt 5) | ❌ Not supported |

| Content | Statistics |
|---|---|
| Sideloaded EPUB (`.epub`) | ✅ Added by this mod |
| kepub (`.kepub.epub`, store books) | ✅ Unchanged, as provided by Nickel |
| Fixed-layout EPUB, image-only books, PDF, comics | ❌ Excluded, like Nickel does for kepubs |

## Installation

> [!TIP]
> No NickelMenu or other mod is required. NickelReadingStats brings its own
> hook framework.

1. Download `Kobo.tgz` from the [latest release](https://github.com/dkuester/NickelReadingStats/releases/latest).
2. Connect the device via USB and copy `Kobo.tgz` into the hidden `.kobo/`
   folder.
3. Eject the device. It installs the update on the next start; if it does
   not restart by itself, restart it.

Open a sideloaded EPUB and give the word count a moment. Then the book
statistics appear in the reading menu, and "Activity" shows up in the More
menu. Hours read and pages per minute only fill up after you have actually
read for a while.

**Update:** copy the newer `Kobo.tgz` the same way; there is no need to
uninstall first.

**Uninstall:** create an empty file named `readingstats_uninstall` (no file
extension) in the device's root folder and restart. The mod removes itself
and the device is back to stock behaviour.

## Privacy

Kobo's reading-life code, which the statistics need, also contains Kobo's
analytics. A stock Tolino never runs it. NickelReadingStats lets the
statistics part run and keeps the analytics part silent on a Tolino:

| | Stock Tolino | With NickelReadingStats 0.2.1 |
|---|---|---|
| Reading statistics stored on the device | — | ✅ |
| Analytics events (`KoboReader.sqlite`) | none | none (dropped) |
| Google Analytics queue (`.kobo/Kobo/Analytics.conf`) | none | none (silenced, old entries deleted) |

The statistics stay on the device. On a Kobo, analytics behave exactly as
without the mod. Details: [Privacy internals](docs/ARCHITECTURE.md#privacy-internals).

## Known limitations

- The "time left" estimate can read too low until Nickel has learned your
  reading speed from a few longer sessions.
- A book made of a single large HTML file shows a single chapter bar.
- The book statistics appear a moment after opening a book, once the
  background word count is done.
- Kobo devices on firmware 5.x have not been verified yet. If you have one,
  feedback is very welcome.

## How it works

On a Tolino, the statistics are hidden behind several checks of
`Device::isTolino()` and a kepub-only rule. The mod hooks these functions in
`libnickel.so.1.0.0`, answers "not a Tolino" only to the five Kobo
reading-life functions that need it, counts words for regular EPUBs itself,
and silences Kobo's analytics. The full description, with every hook and
the call graph, is in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Development

```sh
git clone --recurse-submodules https://github.com/dkuester/NickelReadingStats.git
cd NickelReadingStats/hook
docker run --rm --volume="$PWD:$PWD" --workdir="$PWD" ghcr.io/dkuester/nickelstorygraph-toolchain make kobo
make -C tests check            # host unit tests
```

Every `v*` tag builds `Kobo.tgz` in CI and publishes it as a release.
Building, the test levels, the firmware symbol/ABI checks and CI are described
in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Support

| | |
|---|---|
| Bug reports and questions | [Issues](https://github.com/dkuester/NickelReadingStats/issues). Please include device model, firmware version (`.kobo/version`) and mod version. |
| Downloads and changelog | [Releases](https://github.com/dkuester/NickelReadingStats/releases) |
| Earlier history | Issues and pull requests up to v0.2.1 are in the [archived Codeberg repository](https://codeberg.org/hortizept/nickel-reading-stats). |
| Contributing | Pull requests against `main`; CI must pass. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). |

> [!WARNING]
> This mod changes the behaviour of your e-reader's software. It is not
> affiliated with Kobo, Rakuten or the Tolino alliance. Use it at your own
> risk.

## Acknowledgements

This mod stands on the shoulders of:

- **[NickelHook](https://github.com/pgaskin/NickelHook)** (MIT, © 2020–2025
  Patrick Gaskin): the hook framework, used in the `qt6-support` fork by
  **Nico Verbruggen** ([nicoverbruggen/NickelHook](https://github.com/nicoverbruggen/NickelHook)),
  which adds Qt 6 support.
- **Qt 6.5.2** (The Qt Project / The Qt Company): `QZipReader` (private API,
  LGPL-3.0/GPL-2.0/GPL-3.0) for reading EPUBs, plus `QXmlStreamReader` and
  `QRegularExpression`.
- **Toolchain image** [`ghcr.io/dkuester/nickelstorygraph-toolchain`](https://github.com/dkuester/NickelStorygraph/pkgs/container/nickelstorygraph-toolchain)
  from NickelStorygraph: the Docker cross-compile toolchain
  (`arm-kobo-linux-gnueabihf`, GCC 11.5, Qt 6.5.2), built on
  [pgaskin/NickelTC](https://github.com/pgaskin/NickelTC).
- **Kobo/Tolino firmware** (`libnickel.so.1.0.0`, Tolino 5.18): the target
  binary. The internal classes (`Device`, `ReadingView`, `Content`,
  `EventEngine`) were reverse-engineered via symbol analysis and Capstone
  disassembly.

## AI assistance

This mod was reverse-engineered and built with the help of AI assistants. The
analysis of the firmware gates, the disassembly and the first implementation
were done with Hermes Agent (Nous Research). The code review, the hardening
and the privacy fixes in v0.2.x were done with Claude Code (Anthropic).
Technical review, on-device testing and sign-off were done by a human.

## License

[MIT](LICENSE)
