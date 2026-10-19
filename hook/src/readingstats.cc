/*
 * NickelReadingStats — restore Kobo "Reading Stats" on Tolino firmware (5.18+/6.x)
 *
 * What it does and why:
 *
 * On Tolino devices the Kobo "Reading Stats" feature is disabled by two
 * independent gates in libnickel.so.1.0.0:
 *
 *   1. Device::supportsReadingLife()  returns `!Device::isTolino()`.
 *      On a Tolino this is false, so the whole Reading Life / reading-stats
 *      UI (the per-book "Reading stats" page and the reading-menu stats tab)
 *      is never offered. This is the "Kobo mode vs. Tolino mode" difference.
 *
 *   2. ReadingView::supportsReadingStats() requires the book's MIME type to
 *      be `application/x-kobo-epub+zip` (kepub). Sideloaded plain epubs
 *      (`application/epub+zip`) therefore never show stats either, even on a
 *      real Kobo. hasValidReadingStats() additionally re-checks the Tolino
 *      flag and a merged TOC/spine index that plain epubs do not have.
 *
 *   3. EventEngine::startReadingSession()/endReadingSession() return early on
 *      Tolino because they check isTolino() directly. That is the Kobo
 *      reading-session tracking which feeds the Activity metrics
 *      ("hours read", "minutes per session", "pages per minute"). On a Tolino
 *      those stay at 0 because no session is ever recorded.
 *
 * The hooks below:
 *   - force supportsReadingLife() to true  -> restore the feature on Tolino
 *   - re-enable supportsActivityInMore() on Tolino -> the "Activity" entry
 *     in the More menu (it is gated by isTolino() directly, independently
 *     of supportsReadingLife(), which is why it stayed hidden)
 *   - relax supportsReadingStats()/hasValidReadingStats() to accept any
 *     reflowable (non-fixed-layout) book -> plain epubs work too
 *   - force FeatureSettings::bookSpecificStatsEnabled() to true
 *
 * Fixed-layout content (fixed-layout epubs, PDFs, comics) is still excluded,
 * because the stats estimator has nothing meaningful to show there and this
 * is what keeps the feature "robust" rather than showing garbage.
 */

#include <NickelHook.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include "wordcount.h"

// Opaque forward decls (we only ever pass pointers through).
typedef void ReadingView;
typedef void Device;
typedef void FeatureSettings;
typedef void Content;

// Original symbols, filled by NickelHook (.out) before our hooks run.
static bool (*ReadingView__supportsReadingStats)(ReadingView *);
static bool (*ReadingView__hasValidReadingStats)(ReadingView *);
static void (*ReadingView__updateWordCounts)(ReadingView *);
static int  (*ReadingView__mergedTOCAndSpineIndex)(ReadingView *);
static bool (*Device__supportsReadingLife)(Device *);
static bool (*Device__supportsActivityInMore)(Device *);
static bool (*Device__isTolino)(Device *);
static bool (*FeatureSettings__bookSpecificStatsEnabled)(FeatureSettings *);
static bool (*EventEngine__startReadingSession)(void *, void *, void *, void *);
static bool (*EventEngine__endReadingSession)(void *, void *, void *, void *);
static void (*EventEngine__fireEvent)(void *, void *, void *, void *);
static void (*EventEngine__fireEventSlot)(void *, void *, void *, void *);

// Resolved via dlsym.
static bool (*ReadingView__isFixedLayout)(ReadingView *);
static int  (*Device__getDeviceClass)(Device *);
static bool (*Device__isParentalControlEnabled)(Device *);
static QByteArray &(*Content__getMimeType)(const Content *);
static QString    (*Content__getId)(const Content *, const QString &);
static void       (*ReadingView__gotWordCounts)(ReadingView *, const QList<int> &);
static int        (*ReadingView__chapterIndex)(ReadingView *);
static int        (*Content__getAccessibility)(const Content *);

// Offset of the Volume/Content embedded in ReadingView. Firmware-specific:
// ReadingView::updateWordCounts() computes it itself (`add.w rX, r0, #0x94`
// on 5.18), and tools/check-symbols.py verifies this value against the
// target firmware. A wrong offset hands getMimeType() a bogus `this`.
static constexpr unsigned kReadingViewContentOffset = 0x94;

// While set, isTolino() reports false so the reading-life tracking runs its
// Kobo path on a Tolino. Set only by ForceKoboScope around the EventEngine
// calls below. thread_local: other threads (sync, store, DRM) asking
// isTolino() at the same moment keep getting the real answer. Note the
// window still covers everything those calls run synchronously on this
// thread, e.g. directly connected slots of signals they emit.
static thread_local bool g_force_kobo_session = false;

// Sets g_force_kobo_session for one scope and restores the previous value
// on exit, so nested calls (fireEventSlot inside a session call) are safe.
struct ForceKoboScope {
    bool prev;
    ForceKoboScope() : prev(g_force_kobo_session) { g_force_kobo_session = true; }
    ~ForceKoboScope() { g_force_kobo_session = prev; }
    ForceKoboScope(const ForceKoboScope &) = delete;
    ForceKoboScope &operator=(const ForceKoboScope &) = delete;
};

static struct nh_info NickelReadingStats = {
    .name = "NickelReadingStats",
    .desc = "Restore Kobo reading stats on Tolino 5.18+/6.x and enable them for plain epubs",
    .uninstall_flag = "/mnt/onboard/readingstats_uninstall",
};

static struct nh_hook NickelReadingStatsHook[] = {
    {
        .sym     = "_ZNK6Device19supportsReadingLifeEv",
        .sym_new = "_nh_Device__supportsReadingLife",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__supportsReadingLife),
        .desc    = "Re-enable the reading stats feature on Tolino devices",
    },
    {
        .sym     = "_ZNK6Device8isTolinoEv",
        .sym_new = "_nh_Device__isTolino",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__isTolino),
        .desc    = "Report 'not Tolino' only while a reading session is tracked",
    },
    {
        .sym     = "_ZNK6Device22supportsActivityInMoreEv",
        .sym_new = "_nh_Device__supportsActivityInMore",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__supportsActivityInMore),
        .desc    = "Show the Activity entry in the More menu on Tolino devices",
    },
    {
        .sym     = "_ZNK11ReadingView20supportsReadingStatsEv",
        .sym_new = "_nh_ReadingView__supportsReadingStats",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__supportsReadingStats),
        .desc    = "Allow reading stats for sideloaded (non-kepub) epubs",
    },
    {
        .sym     = "_ZN11ReadingView20hasValidReadingStatsEv",
        .sym_new = "_nh_ReadingView__hasValidReadingStats",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__hasValidReadingStats),
        .desc    = "Drop the Tolino-only TOC/spine validation for reading stats",
    },
    {
        .sym     = "_ZN11ReadingView16updateWordCountsEv",
        .sym_new = "_nh_ReadingView__updateWordCounts",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__updateWordCounts),
        .desc    = "Compute word counts for plain epubs (chapter bars / time estimates)",
    },
    {
        .sym     = "_ZN11ReadingView22mergedTOCAndSpineIndexEv",
        .sym_new = "_nh_ReadingView__mergedTOCAndSpineIndex",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__mergedTOCAndSpineIndex),
        .desc    = "Fall back to the spine index for plain epubs so read chapters are highlighted",
    },
    {
        .sym     = "_ZN15FeatureSettings24bookSpecificStatsEnabledEv",
        .sym_new = "_nh_FeatureSettings__bookSpecificStatsEnabled",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(FeatureSettings__bookSpecificStatsEnabled),
        .desc    = "Force per-book reading stats on",
    },
    {
        .sym     = "_ZN11EventEngine19startReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__startReadingSession",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__startReadingSession),
        .desc    = "Run the Kobo session-start path on Tolino devices",
    },
    {
        .sym     = "_ZN11EventEngine17endReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__endReadingSession",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__endReadingSession),
        .desc    = "Run the Kobo session-end path on Tolino devices",
    },
    {
        .sym     = "_ZN11EventEngine9fireEventEPKcRK6VolumeRK4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__fireEvent",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__fireEvent),
        .desc    = "Run the Kobo page-turn event entry on Tolino devices",
    },
    {
        .sym     = "_ZN11EventEngine13fireEventSlotE10QByteArray6Volume4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__fireEventSlot",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__fireEventSlot),
        .desc    = "Run the Kobo event dispatch (page turns) on Tolino devices",
    },
    {0},
};

static struct nh_dlsym NickelReadingStatsDlsym[] = {
    {
        .name = "_ZNK11ReadingView13isFixedLayoutEv",
        .out  = nh_symoutptr(ReadingView__isFixedLayout),
        .desc = "Keep fixed-layout content excluded from stats",
    },
    {
        .name = "_ZNK6Device14getDeviceClassEv",
        .out  = nh_symoutptr(Device__getDeviceClass),
        .desc = "Preserve the device-class check for the Activity entry",
    },
    {
        .name = "_ZNK6Device24isParentalControlEnabledEv",
        .out  = nh_symoutptr(Device__isParentalControlEnabled),
        .desc = "Preserve the parental-control check for the Activity entry",
    },
    {
        .name = "_ZNK7Content11getMimeTypeEv",
        .out  = nh_symoutptr(Content__getMimeType),
        .desc = "Detect plain epubs for the word-count retrofit",
    },
    {
        .name = "_ZNK7Content5getIdERK7QString",
        .out  = nh_symoutptr(Content__getId),
        .desc = "Resolve the epub file path for the word-count retrofit",
    },
    {
        .name = "_ZN11ReadingView13gotWordCountsERK5QListIiE",
        .out  = nh_symoutptr(ReadingView__gotWordCounts),
        .desc = "Deliver the computed word counts to the reading view",
    },
    {
        .name     = "_ZNK7Content16getAccessibilityEv",
        .out      = nh_symoutptr(Content__getAccessibility),
        .desc     = "Apply the original accessibility check to plain epubs",
        .optional = true,  // missing -> plain epubs get no stats (fail closed)
    },
    {
        .name = "_ZN11ReadingView12chapterIndexEv",
        .out  = nh_symoutptr(ReadingView__chapterIndex),
        .desc = "Raw spine chapter index (fallback for the merged TOC/spine index on epubs)",
    },
    {0},
};

static const Content *reading_view_content(ReadingView *view) {
    return reinterpret_cast<const Content *>(
        reinterpret_cast<const char *>(view) + kReadingViewContentOffset);
}

// True for a sideloaded plain epub (not kepub). False if the MIME type
// cannot be read.
static bool is_plain_epub(ReadingView *view) {
    return Content__getMimeType &&
           Content__getMimeType(reading_view_content(view)) == QByteArray("application/epub+zip");
}

// The original ReadingView::supportsReadingStats() (5.18) is
//     mime == kepub && content.getAccessibility() != 6
//         && !<virtual at vtable+0x10c>() && !isFixedLayout()
// This mirrors it for plain epubs: the kepub check becomes a plain-epub
// check, the other guards stay. A kepub the original rejected stays
// rejected. Anything that cannot be evaluated counts as unsupported (fail
// closed). The virtual check is not replicated (unidentified).
static constexpr int kAccessibilityNoStats = 6;  // state the original rejects

static bool reading_stats_supported(ReadingView *view) {
    if (!is_plain_epub(view)) {
        return false;
    }
    if (!ReadingView__isFixedLayout || ReadingView__isFixedLayout(view)) {
        return false;
    }
    if (!Content__getAccessibility ||
        Content__getAccessibility(reading_view_content(view)) == kAccessibilityNoStats) {
        return false;
    }
    return true;
}

extern "C" __attribute__((visibility("default"))) bool
_nh_Device__supportsReadingLife(Device *device) {
    (void)device;
    return true;
}

// Only deviate from the real isTolino() while a reading session is being
// tracked. Everywhere else the original value is returned, so the Tolino
// store/cloud/DRM behaviour is untouched.
extern "C" __attribute__((visibility("default"))) bool
_nh_Device__isTolino(Device *device) {
    if (g_force_kobo_session) {
        return false;
    }
    if (!Device__isTolino) {
        return true;  // fallback: preserve original Tolino behaviour
    }
    return Device__isTolino(device);
}

// startReadingSession/endReadingSession call isTolino() themselves and skip
// the whole Kobo session path on a Tolino. We run them with isTolino() forced
// to false so that path executes and records the Activity data.
extern "C" __attribute__((visibility("default"))) bool
_nh_EventEngine__startReadingSession(void *engine, void *volume, void *state, void *map) {
    if (!EventEngine__startReadingSession) {
        return false;
    }
    ForceKoboScope scope;
    return EventEngine__startReadingSession(engine, volume, state, map);
}

extern "C" __attribute__((visibility("default"))) bool
_nh_EventEngine__endReadingSession(void *engine, void *volume, void *state, void *map) {
    if (!EventEngine__endReadingSession) {
        return false;
    }
    ForceKoboScope scope;
    return EventEngine__endReadingSession(engine, volume, state, map);
}

// fireEvent(char const*, Volume const&, QMap const&) is the page-turn ENTRY:
// ReadingView::onPageTurned() calls it, and it checks isTolino() before
// scheduling fireEventSlot() via a single-shot timer. On a Tolino it returns
// early, so the event never reaches the slot — this gate must be un-gated
// first. Run it with isTolino() forced to false.
extern "C" __attribute__((visibility("default"))) void
_nh_EventEngine__fireEvent(void *engine, void *name, void *volume, void *map) {
    if (!EventEngine__fireEvent) {
        return;
    }
    ForceKoboScope scope;
    EventEngine__fireEvent(engine, name, volume, map);
}

// fireEventSlot() is the Qt slot that dispatches every reading-life event
// (page turns, session start/end, book finished) to the social-event handlers
// that update the Activity metrics. It also checks isTolino() and bails out on
// a Tolino, which is why "pages per minute" stays at 0 even though the session
// start/end above record the time. Run it with isTolino() forced to false so
// page turns are counted. (The analytics half of the slot is still gated by
// the user's analytics/privacy permission.)
extern "C" __attribute__((visibility("default"))) void
_nh_EventEngine__fireEventSlot(void *engine, void *name, void *volume, void *map) {
    if (!EventEngine__fireEventSlot) {
        return;
    }
    ForceKoboScope scope;
    EventEngine__fireEventSlot(engine, name, volume, map);
}

// Device::supportsActivityInMore() gates the "Activity" entry in the More
// menu. On Tolino it returns false immediately because of isTolino(); the
// original logic is:
//     if (isTolino()) return false;
//     if (getDeviceClass() != 2) return false;
//     return !isParentalControlEnabled();
// We re-run the same checks but drop the isTolino() early-out, so the
// device-class and parental-control guards are preserved. If either guard
// cannot be evaluated, keep the entry hidden (fail closed): showing it would
// bypass parental control.
extern "C" __attribute__((visibility("default"))) bool
_nh_Device__supportsActivityInMore(Device *device) {
    if (Device__supportsActivityInMore && Device__supportsActivityInMore(device)) {
        return true;
    }
    if (!Device__getDeviceClass || !Device__isParentalControlEnabled) {
        return false;
    }
    if (Device__getDeviceClass(device) != 2) {
        return false;
    }
    return !Device__isParentalControlEnabled(device);
}

extern "C" __attribute__((visibility("default"))) bool
_nh_ReadingView__supportsReadingStats(ReadingView *view) {
    // Preserve the original result; if Nickel already says yes, keep it.
    if (ReadingView__supportsReadingStats && ReadingView__supportsReadingStats(view)) {
        return true;
    }
    return reading_stats_supported(view);
}

extern "C" __attribute__((visibility("default"))) bool
_nh_ReadingView__hasValidReadingStats(ReadingView *view) {
    if (ReadingView__hasValidReadingStats && ReadingView__hasValidReadingStats(view)) {
        return true;
    }
    return reading_stats_supported(view);
}

// mergedTOCAndSpineIndex() is what ReadingView::readingStats() passes into the
// ReadingStats constructor as the "current chapter" for the chapter-bar
// coloring (setChapterSizes: chapters before the index are drawn dark/read,
// chapters after are light/unread). On a plain epub the merged TOC/spine index
// is never computed — its cached value stays -1 — so the caller clamps it to 0
// and every chapter after the first is drawn as "unread". Fall back to the raw
// spine chapterIndex() for plain epubs so the already-read chapters are
// highlighted correctly.
extern "C" __attribute__((visibility("default"))) int
_nh_ReadingView__mergedTOCAndSpineIndex(ReadingView *view) {
    if (!ReadingView__mergedTOCAndSpineIndex) {
        return 0;
    }
    if (ReadingView__chapterIndex && is_plain_epub(view)) {
        return ReadingView__chapterIndex(view);
    }
    return ReadingView__mergedTOCAndSpineIndex(view);
}

// ReadingView::updateWordCounts() is the kepub-only gate that computes the
// per-chapter word counts behind the chapter bars and time estimates. It
// early-returns for anything that is not a kepub. We run the original (kepub
// and instapaper are handled there) and then, for a sideloaded plain epub,
// count the words ourselves and deliver them through gotWordCounts().
extern "C" __attribute__((visibility("default"))) void
_nh_ReadingView__updateWordCounts(ReadingView *view) {
    if (ReadingView__updateWordCounts) {
        ReadingView__updateWordCounts(view);
    }

    if (!Content__getMimeType || !Content__getId || !ReadingView__gotWordCounts) {
        return;
    }
    if (ReadingView__isFixedLayout && ReadingView__isFixedLayout(view)) {
        return;  // fixed-layout content has no meaningful chapter bars
    }

    if (!is_plain_epub(view)) {
        return;  // kepub and other types are handled by nickel itself
    }

    const QString id = Content__getId(reading_view_content(view), QString());
    if (!id.startsWith(QLatin1String("file://"))) {
        return;  // not a sideloaded file (store content uses the kepub path)
    }

    const QList<int> counts = count_epub_words(id.mid(7));  // strip "file://"
    if (!counts.isEmpty()) {
        ReadingView__gotWordCounts(view, counts);
    }
}

extern "C" __attribute__((visibility("default"))) bool
_nh_FeatureSettings__bookSpecificStatsEnabled(FeatureSettings *settings) {
    (void)settings;
    return true;
}

NickelHook(
    .init  = nullptr,
    .info  = &NickelReadingStats,
    .hook  = NickelReadingStatsHook,
    .dlsym = NickelReadingStatsDlsym,
)
