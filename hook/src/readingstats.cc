/*
 * NickelReadingStats — restore Kobo "Reading Stats" on Tolino and enable them
 * for plain epubs (firmware 5.18+, Qt 6.5.2; verified against Tolino 5.18)
 *
 * What it does and why:
 *
 * Kobo and Tolino share one libnickel.so.1.0.0; Device::isTolino() switches
 * between them at runtime. The reading stats are held back by several
 * independent gates:
 *
 *   1. Device::supportsReadingLife() returns `!isTolino()`: the whole
 *      Reading Life / stats UI is off on a Tolino.
 *   2. Device::supportsActivityInMore() checks isTolino() itself, so the
 *      "Activity" entry in the More menu stays hidden even with (1) fixed.
 *   3. EventEngine::startReadingSession()/endReadingSession() (session time)
 *      and fireEvent()/fireEventSlot() (page turns) return early on a Tolino,
 *      so the Activity metrics stay at 0.
 *   4. ReadingView::supportsReadingStats() accepts kepubs only, on Kobo too;
 *      hasValidReadingStats() adds a merged TOC/spine check on Tolino.
 *   5. ReadingView::updateWordCounts() counts words for kepubs only, and
 *      mergedTOCAndSpineIndex() is never computed for plain epubs, so the
 *      chapter bars and time estimates stay empty.
 *
 * The hooks below:
 *   - (1) force supportsReadingLife() and bookSpecificStatsEnabled() to true
 *   - (2) re-run supportsActivityInMore() without its isTolino() early-out
 *     (device-class and parental-control guards kept, fail closed)
 *   - (3) run the EventEngine calls with isTolino() reporting false on the
 *     calling thread only, for the duration of the call (ForceKoboScope)
 *   - (4) mirror the original supportsReadingStats() for plain epubs: same
 *     accessibility and fixed-layout guards, kepub check swapped for epub
 *   - (5) count plain-epub words ourselves (wordcount.cc, background thread,
 *     cached) and fall back to the raw spine chapterIndex()
 *
 * Every hook calls the original first and keeps a positive answer, so kepubs
 * and Kobo behaviour are unchanged; on a Kobo the Tolino hooks pass through.
 * Fixed-layout content (fixed-layout epubs, PDFs, comics) stays excluded.
 */

#include <NickelHook.h>

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>

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
// EventEngine signatures (ARM EABI): the Volume/PluginState references are
// pointers, and a QByteArray/Volume/QMap passed BY VALUE (non-trivial types)
// is passed as a hidden pointer to a caller-owned copy, so each argument is
// one void*. Return types are not in the mangled names; from 5.18
// disassembly: startReadingSession/fireEvent/fireEventSlot leave r0 unset
// (void), endReadingSession returns QDateTime::secsTo() truncated to int
// (session seconds). tools/check-symbols.py verifies void vs. value.
static void (*EventEngine__startReadingSession)(void *, void *, void *, void *);
static int  (*EventEngine__endReadingSession)(void *, void *, void *, void *);
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
    .desc = "Restore Kobo reading stats on Tolino and enable them for plain epubs (firmware 5.18+)",
    .uninstall_flag = "/mnt/onboard/readingstats_uninstall",
};

// Every hook and dlsym is optional: each feature degrades on its own (every
// call through a resolved pointer is NULL-checked, guards fail closed), so a
// symbol missing on some 5.18+ build disables that feature only instead of
// the whole mod. NickelHook logs every symbol it could not resolve.
static struct nh_hook NickelReadingStatsHook[] = {
    {
        .sym     = "_ZNK6Device19supportsReadingLifeEv",
        .sym_new = "_nh_Device__supportsReadingLife",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__supportsReadingLife),
        .desc    = "Re-enable the reading stats feature on Tolino devices",
        .optional = true,
    },
    {
        .sym     = "_ZNK6Device8isTolinoEv",
        .sym_new = "_nh_Device__isTolino",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__isTolino),
        .desc    = "Report 'not Tolino' only while a reading session is tracked",
        .optional = true,
    },
    {
        .sym     = "_ZNK6Device22supportsActivityInMoreEv",
        .sym_new = "_nh_Device__supportsActivityInMore",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(Device__supportsActivityInMore),
        .desc    = "Show the Activity entry in the More menu on Tolino devices",
        .optional = true,
    },
    {
        .sym     = "_ZNK11ReadingView20supportsReadingStatsEv",
        .sym_new = "_nh_ReadingView__supportsReadingStats",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__supportsReadingStats),
        .desc    = "Allow reading stats for sideloaded (non-kepub) epubs",
        .optional = true,
    },
    {
        .sym     = "_ZN11ReadingView20hasValidReadingStatsEv",
        .sym_new = "_nh_ReadingView__hasValidReadingStats",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__hasValidReadingStats),
        .desc    = "Drop the Tolino-only TOC/spine validation for reading stats",
        .optional = true,
    },
    {
        .sym     = "_ZN11ReadingView16updateWordCountsEv",
        .sym_new = "_nh_ReadingView__updateWordCounts",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__updateWordCounts),
        .desc    = "Compute word counts for plain epubs (chapter bars / time estimates)",
        .optional = true,
    },
    {
        .sym     = "_ZN11ReadingView22mergedTOCAndSpineIndexEv",
        .sym_new = "_nh_ReadingView__mergedTOCAndSpineIndex",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(ReadingView__mergedTOCAndSpineIndex),
        .desc    = "Fall back to the spine index for plain epubs so read chapters are highlighted",
        .optional = true,
    },
    {
        .sym     = "_ZN15FeatureSettings24bookSpecificStatsEnabledEv",
        .sym_new = "_nh_FeatureSettings__bookSpecificStatsEnabled",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(FeatureSettings__bookSpecificStatsEnabled),
        .desc    = "Force per-book reading stats on",
        .optional = true,
    },
    {
        .sym     = "_ZN11EventEngine19startReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__startReadingSession",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__startReadingSession),
        .desc    = "Run the Kobo session-start path on Tolino devices",
        .optional = true,
    },
    {
        .sym     = "_ZN11EventEngine17endReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__endReadingSession",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__endReadingSession),
        .desc    = "Run the Kobo session-end path on Tolino devices",
        .optional = true,
    },
    {
        .sym     = "_ZN11EventEngine9fireEventEPKcRK6VolumeRK4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__fireEvent",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__fireEvent),
        .desc    = "Run the Kobo page-turn event entry on Tolino devices",
        .optional = true,
    },
    {
        .sym     = "_ZN11EventEngine13fireEventSlotE10QByteArray6Volume4QMapI7QString8QVariantE",
        .sym_new = "_nh_EventEngine__fireEventSlot",
        .lib     = "libnickel.so.1.0.0",
        .out     = nh_symoutptr(EventEngine__fireEventSlot),
        .desc    = "Run the Kobo event dispatch (page turns) on Tolino devices",
        .optional = true,
    },
    {0},
};

static struct nh_dlsym NickelReadingStatsDlsym[] = {
    {
        .name = "_ZNK11ReadingView13isFixedLayoutEv",
        .out  = nh_symoutptr(ReadingView__isFixedLayout),
        .desc = "Keep fixed-layout content excluded from stats",
        .optional = true,
    },
    {
        .name = "_ZNK6Device14getDeviceClassEv",
        .out  = nh_symoutptr(Device__getDeviceClass),
        .desc = "Preserve the device-class check for the Activity entry",
        .optional = true,
    },
    {
        .name = "_ZNK6Device24isParentalControlEnabledEv",
        .out  = nh_symoutptr(Device__isParentalControlEnabled),
        .desc = "Preserve the parental-control check for the Activity entry",
        .optional = true,
    },
    {
        .name = "_ZNK7Content11getMimeTypeEv",
        .out  = nh_symoutptr(Content__getMimeType),
        .desc = "Detect plain epubs for the word-count retrofit",
        .optional = true,
    },
    {
        .name = "_ZNK7Content5getIdERK7QString",
        .out  = nh_symoutptr(Content__getId),
        .desc = "Resolve the epub file path for the word-count retrofit",
        .optional = true,
    },
    {
        .name = "_ZN11ReadingView13gotWordCountsERK5QListIiE",
        .out  = nh_symoutptr(ReadingView__gotWordCounts),
        .desc = "Deliver the computed word counts to the reading view",
        .optional = true,
    },
    {
        .name = "_ZNK7Content16getAccessibilityEv",
        .out  = nh_symoutptr(Content__getAccessibility),
        .desc = "Apply the original accessibility check to plain epubs",
        .optional = true,
    },
    {
        .name = "_ZN11ReadingView12chapterIndexEv",
        .out  = nh_symoutptr(ReadingView__chapterIndex),
        .desc = "Raw spine chapter index (fallback for the merged TOC/spine index on epubs)",
        .optional = true,
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
    // NickelHook sets .out before installing the hook, so this cannot be NULL
    // here; the check is purely defensive and keeps the Tolino answer.
    if (!Device__isTolino) {
        return true;
    }
    return Device__isTolino(device);
}

// startReadingSession/endReadingSession call isTolino() themselves and skip
// the whole Kobo session path on a Tolino. We run them with isTolino() forced
// to false so that path executes and records the Activity data.
extern "C" __attribute__((visibility("default"))) void
_nh_EventEngine__startReadingSession(void *engine, void *volume, void *state, void *map) {
    if (!EventEngine__startReadingSession) {
        return;
    }
    ForceKoboScope scope;
    EventEngine__startReadingSession(engine, volume, state, map);
}

// Returns the session length in seconds; it must be passed through as int
// (declaring it bool would let the compiler hand back only 0/1 or a byte).
extern "C" __attribute__((visibility("default"))) int
_nh_EventEngine__endReadingSession(void *engine, void *volume, void *state, void *map) {
    if (!EventEngine__endReadingSession) {
        return 0;
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
    // Defensive only (see _nh_Device__isTolino); -1 is the original's own
    // "not computed" value, which its callers already handle.
    if (!ReadingView__mergedTOCAndSpineIndex) {
        return -1;
    }
    if (ReadingView__chapterIndex && is_plain_epub(view)) {
        return ReadingView__chapterIndex(view);
    }
    return ReadingView__mergedTOCAndSpineIndex(view);
}

// --- Background word counting -------------------------------------------
//
// Counting a whole epub can take seconds on the device, so it runs on a
// worker thread and the result is delivered back on the GUI thread, like
// Nickel's own KepubBookWordCounter does for kepubs. Everything below except
// the worker lambda runs on the GUI thread only.

// A view waiting for the counts of a book.
struct WordCountWaiter {
    // ReadingView's primary base is QWidget (offset 0 in the 5.18 typeinfo),
    // so the view pointer is its QObject; QPointer notices if it is destroyed.
    QPointer<QObject> guard;
    ReadingView *view;
    QString id;  // content ID at request time, to detect a reused view
};

// Counts of recently opened epubs by path, invalidated when the file's size
// or mtime changes. Failed counts (empty) are cached too, so a broken epub is
// not re-parsed on every open.
struct CachedWordCounts {
    qint64 size;
    QDateTime mtime;
    QList<int> counts;
};
static constexpr int kWordCountCacheSize = 16;
static QHash<QString, CachedWordCounts> g_word_count_cache;
static QHash<QString, QList<WordCountWaiter>> g_word_count_waiters;  // in flight

// One worker thread: books are counted one at a time (bounded memory), and
// Nickel's global thread pool is left alone. Never deleted, so a count still
// running at shutdown cannot block or crash Nickel's exit.
static QThreadPool *word_count_pool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        return p;
    }();
    return pool;
}

// Lives on the GUI thread and is never deleted, so a worker can always post
// its result here.
static QObject *word_count_receiver() {
    static QObject *receiver = new QObject;
    return receiver;
}

static void deliver_word_counts(const WordCountWaiter &w, const QList<int> &counts) {
    // The view may have been closed, or reused for another book, meanwhile.
    if (counts.isEmpty() || !w.guard || !is_plain_epub(w.view) ||
        Content__getId(reading_view_content(w.view), QString()) != w.id) {
        return;
    }
    ReadingView__gotWordCounts(w.view, counts);
}

static void word_counts_done(const QString &path, qint64 size, const QDateTime &mtime,
                             const QList<int> &counts) {
    if (g_word_count_cache.size() >= kWordCountCacheSize) {
        g_word_count_cache.clear();
    }
    g_word_count_cache.insert(path, CachedWordCounts{size, mtime, counts});
    const QList<WordCountWaiter> waiters = g_word_count_waiters.take(path);
    for (const WordCountWaiter &w : waiters) {
        deliver_word_counts(w, counts);
    }
}

// ReadingView::updateWordCounts() is the kepub-only gate that computes the
// per-chapter word counts behind the chapter bars and time estimates. It
// early-returns for anything that is not a kepub. We run the original (kepub
// and instapaper are handled there) and then, for a sideloaded plain epub,
// count the words ourselves (cached, in the background) and deliver them
// through gotWordCounts().
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
    const QString path = id.mid(7);  // strip "file://"

    const WordCountWaiter waiter{QPointer<QObject>(reinterpret_cast<QObject *>(view)), view, id};

    const QFileInfo info(path);
    const qint64 size = info.size();
    const QDateTime mtime = info.lastModified();
    const auto cached = g_word_count_cache.constFind(path);
    if (cached != g_word_count_cache.constEnd() && cached->size == size && cached->mtime == mtime) {
        deliver_word_counts(waiter, cached->counts);
        return;
    }

    // Already being counted (e.g. reopened quickly): just wait for it.
    const auto pending = g_word_count_waiters.find(path);
    if (pending != g_word_count_waiters.end()) {
        pending->append(waiter);
        return;
    }
    g_word_count_waiters.insert(path, QList<WordCountWaiter>{waiter});

    // The worker only touches its own copies of value types.
    QObject *receiver = word_count_receiver();
    word_count_pool()->start([receiver, path, size, mtime]() {
        const QList<int> counts = count_epub_words(path);
        QMetaObject::invokeMethod(
            receiver, [path, size, mtime, counts]() { word_counts_done(path, size, mtime, counts); },
            Qt::QueuedConnection);
    });
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
