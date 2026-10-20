// Host test for the gate hooks in readingstats.cc that need no epub: the
// analytics drop, the scoped isTolino() override and the Activity entry.
// readingstats.cc is included directly so the libnickel function pointers
// can be replaced with fakes.
#include "../src/readingstats.cc"

#include <cstdio>

static int failures = 0;

static void check(const char *what, bool ok)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

// --- fakes ------------------------------------------------------------------

static bool g_is_tolino = true;
static int g_analytics_calls = 0;
static bool g_last_flag = false;
static bool g_tolino_seen_in_slot = true;

static bool fake_isTolino(Device *) { return g_is_tolino; }

static void fake_fireAnalyticsEvent(void *, Device *, void *, void *, bool flag)
{
    ++g_analytics_calls;
    g_last_flag = flag;
}

// Stands in for fireEventSlot(): records what isTolino() reports inside, and
// fires an analytics event like the real slot does.
static void fake_fireEventSlot(void *engine, void *, void *, void *)
{
    g_tolino_seen_in_slot = _nh_Device__isTolino(nullptr);
    _nh_EventEngine__fireAnalyticsEvent(engine, nullptr, nullptr, nullptr, true);
}

int main()
{
    Device__isTolino = fake_isTolino;
    EventEngine__fireAnalyticsEvent = fake_fireAnalyticsEvent;
    EventEngine__fireEventSlot = fake_fireEventSlot;

    // --- Tolino --------------------------------------------------------------
    g_is_tolino = true;
    _nh_EventEngine__fireAnalyticsEvent(nullptr, nullptr, nullptr, nullptr, true);
    check("Tolino: analytics outside the scope dropped", g_analytics_calls == 0);

    _nh_EventEngine__fireEventSlot(nullptr, nullptr, nullptr, nullptr);
    check("Tolino: slot runs with isTolino() == false", !g_tolino_seen_in_slot);
    check("Tolino: analytics inside the scope dropped", g_analytics_calls == 0);
    check("Tolino: isTolino() is true again after the scope", _nh_Device__isTolino(nullptr));

    // --- Kobo ----------------------------------------------------------------
    g_is_tolino = false;
    _nh_EventEngine__fireAnalyticsEvent(nullptr, nullptr, nullptr, nullptr, true);
    check("Kobo: analytics passed through", g_analytics_calls == 1);
    check("Kobo: stack bool forwarded", g_last_flag);
    _nh_EventEngine__fireAnalyticsEvent(nullptr, nullptr, nullptr, nullptr, false);
    check("Kobo: stack bool false forwarded", g_analytics_calls == 2 && !g_last_flag);
    _nh_EventEngine__fireEventSlot(nullptr, nullptr, nullptr, nullptr);
    check("Kobo: analytics from the slot passed through", g_analytics_calls == 3);

    // --- Activity entry fails closed when a guard is unresolved ---------------
    Device__supportsActivityInMore = nullptr;
    Device__getDeviceClass = nullptr;
    Device__isParentalControlEnabled = nullptr;
    check("Activity: hidden when guards are unresolved",
          !_nh_Device__supportsActivityInMore(nullptr));

    if (failures) {
        std::printf("%d gate check(s) failed\n", failures);
        return 1;
    }
    std::printf("all gate checks passed\n");
    return 0;
}
