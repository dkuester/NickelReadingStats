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

// Opaque forward decls (we only ever pass pointers through).
typedef void ReadingView;
typedef void Device;
typedef void FeatureSettings;

// Original symbols, filled by NickelHook (.out) before our hooks run.
static bool (*ReadingView__supportsReadingStats)(ReadingView *);
static bool (*ReadingView__hasValidReadingStats)(ReadingView *);
static bool (*Device__supportsReadingLife)(Device *);
static bool (*Device__supportsActivityInMore)(Device *);
static bool (*Device__isTolino)(Device *);
static bool (*FeatureSettings__bookSpecificStatsEnabled)(FeatureSettings *);
static bool (*EventEngine__startReadingSession)(void *, void *, void *, void *);
static bool (*EventEngine__endReadingSession)(void *, void *, void *, void *);

// Resolved via dlsym.
static bool (*ReadingView__isFixedLayout)(ReadingView *);
static int  (*Device__getDeviceClass)(Device *);
static bool (*Device__isParentalControlEnabled)(Device *);

// While set, isTolino() reports false so the reading-session tracking runs its
// Kobo path on a Tolino. Scoped to the start/endReadingSession calls only.
static bool g_force_kobo_session = false;

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
    {0},
};

// A book is eligible for reading stats if it is reflowable.
// Fixed-layout content is deliberately excluded.
static bool reading_stats_supported(ReadingView *view) {
    if (ReadingView__isFixedLayout && ReadingView__isFixedLayout(view)) {
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
    bool prev = g_force_kobo_session;
    g_force_kobo_session = true;
    bool ret = EventEngine__startReadingSession(engine, volume, state, map);
    g_force_kobo_session = prev;
    return ret;
}

extern "C" __attribute__((visibility("default"))) bool
_nh_EventEngine__endReadingSession(void *engine, void *volume, void *state, void *map) {
    if (!EventEngine__endReadingSession) {
        return false;
    }
    bool prev = g_force_kobo_session;
    g_force_kobo_session = true;
    bool ret = EventEngine__endReadingSession(engine, volume, state, map);
    g_force_kobo_session = prev;
    return ret;
}

// Device::supportsActivityInMore() gates the "Activity" entry in the More
// menu. On Tolino it returns false immediately because of isTolino(); the
// original logic is:
//     if (isTolino()) return false;
//     if (getDeviceClass() != 2) return false;
//     return !isParentalControlEnabled();
// We re-run the same checks but drop the isTolino() early-out, so the
// device-class and parental-control guards are preserved.
extern "C" __attribute__((visibility("default"))) bool
_nh_Device__supportsActivityInMore(Device *device) {
    if (Device__supportsActivityInMore && Device__supportsActivityInMore(device)) {
        return true;
    }
    if (Device__getDeviceClass && Device__getDeviceClass(device) != 2) {
        return false;
    }
    if (Device__isParentalControlEnabled && Device__isParentalControlEnabled(device)) {
        return false;
    }
    return true;
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
