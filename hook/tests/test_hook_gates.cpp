// Host test for the gate hooks in readingstats.cc that need no epub: the
// analytics drop, the scoped isTolino() override and the Activity entry.
// readingstats.cc is included directly so the libnickel function pointers
// can be replaced with fakes.
#include "../src/readingstats.cc"
#include "nh_log_stub.h"

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
static bool g_tolino_seen_in_callee = false;

static bool fake_isTolino(Device *) { return g_is_tolino; }

static int g_ga_api = 0, g_ga_send = 0, g_ga_load = 0;
static void fake_ga_apiRequest(void *, const void *, bool) { ++g_ga_api; }
static void fake_ga_sendRequest(void *, const void *) { ++g_ga_send; }
static void fake_ga_loadQueue(void *, void *) { ++g_ga_load; }

static void ga_all()
{
    _nh_GoogleAnalyticsRequester__apiRequest(nullptr, nullptr, true);
    _nh_GoogleAnalyticsRequester__sendRequest(nullptr, nullptr);
    _nh_GoogleAnalyticsRequester__loadQueue(nullptr, nullptr);
}

// Stands in for code the gate calls further down, e.g. EventEngine::
// initialize() or Affiliate::getDeviceAffiliate(): it must keep getting the
// real answer even inside the window.
// Must be a real call, not a tail call: a tail call would leave the return
// address in the gate (check-symbols.py guards the firmware against that).
__attribute__((noinline)) static bool callee_asks_isTolino()
{
    volatile bool answer = _nh_Device__isTolino(nullptr);
    return answer;
}

static void fake_fireAnalyticsEvent(void *, Device *, void *, void *, bool flag)
{
    ++g_analytics_calls;
    g_last_flag = flag;
}

// Stands in for fireEventSlot() (a gate): records what isTolino() reports to
// the gate itself and to a callee, and fires an analytics event like the real
// slot does.
__attribute__((noinline)) static void fake_fireEventSlot(void *engine, void *, void *, void *)
{
    g_tolino_seen_in_slot = _nh_Device__isTolino(nullptr);
    g_tolino_seen_in_callee = callee_asks_isTolino();
    _nh_EventEngine__fireAnalyticsEvent(engine, nullptr, nullptr, nullptr, true);
}

// Host stand-in for the range readingstats_init() takes from the dynamic
// symbol: the start of the fake gate plus a span that covers its first call.
static constexpr uintptr_t kFakeGateSpan = 128;

static bool in_fake_gate(const void *fn)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(fn);
    return g_tolino_gates[0].begin <= a && a < g_tolino_gates[0].end;
}

int main()
{
    Device__isTolino = fake_isTolino;
    EventEngine__fireAnalyticsEvent = fake_fireAnalyticsEvent;
    EventEngine__fireEventSlot = fake_fireEventSlot;

    GoogleAnalyticsRequester__apiRequest = fake_ga_apiRequest;
    GoogleAnalyticsRequester__sendRequest = fake_ga_sendRequest;
    GoogleAnalyticsRequester__loadQueue = fake_ga_loadQueue;

    // --- Google Analytics requester --------------------------------------------
    ga_all();  // nobody has asked isTolino() yet
    check("GA: dropped while the device type is unknown", g_ga_api + g_ga_send + g_ga_load == 0);
    g_is_tolino = true;
    _nh_Device__isTolino(nullptr);  // Nickel asks once, outside any window
    ga_all();
    check("GA: dropped on a Tolino (new hit, upload, restored queue)",
          g_ga_api + g_ga_send + g_ga_load == 0);
    g_is_tolino = false;
    _nh_Device__isTolino(nullptr);
    ga_all();
    check("GA: passed through on a Kobo", g_ga_api == 1 && g_ga_send == 1 && g_ga_load == 1);
    g_is_tolino = true;
    _nh_Device__isTolino(nullptr);

    // --- gate range logic ------------------------------------------------------
    g_tolino_gates[0] = CodeRange{0x1000, 0x1100};
    check("range: start is inside", called_from_tolino_gate(0x1000));
    check("range: Thumb bit ignored", called_from_tolino_gate(0x10ff));
    check("range: end is outside", !called_from_tolino_gate(0x1100));
    check("range: below is outside", !called_from_tolino_gate(0x0fff));
    g_tolino_gates[0] = CodeRange{0, 0};
    check("range: empty range matches nothing", !called_from_tolino_gate(0x1000));

    const uintptr_t gate = reinterpret_cast<uintptr_t>(&fake_fireEventSlot);
    g_tolino_gates[0] = CodeRange{gate, gate + kFakeGateSpan};
    if (in_fake_gate(reinterpret_cast<const void *>(&callee_asks_isTolino)) ||
        in_fake_gate(reinterpret_cast<const void *>(&main))) {
        std::printf("FAIL test layout: another function lies inside the fake gate span\n");
        return 1;
    }

    // --- Tolino --------------------------------------------------------------
    g_is_tolino = true;
    _nh_EventEngine__fireAnalyticsEvent(nullptr, nullptr, nullptr, nullptr, true);
    check("Tolino: analytics outside the scope dropped", g_analytics_calls == 0);

    _nh_EventEngine__fireEventSlot(nullptr, nullptr, nullptr, nullptr);
    check("Tolino: gate sees isTolino() == false inside the scope", !g_tolino_seen_in_slot);
    check("Tolino: callee of the gate still sees isTolino() == true", g_tolino_seen_in_callee);
    check("Tolino: analytics inside the scope dropped", g_analytics_calls == 0);
    check("Tolino: isTolino() is true again after the scope", _nh_Device__isTolino(nullptr));

    fake_fireEventSlot(nullptr, nullptr, nullptr, nullptr);  // gate, but no scope
    check("Tolino: gate outside the scope sees isTolino() == true", g_tolino_seen_in_slot);

    const CodeRange saved = g_tolino_gates[0];
    g_tolino_gates[0] = CodeRange{0, 0};  // gate could not be resolved
    _nh_EventEngine__fireEventSlot(nullptr, nullptr, nullptr, nullptr);
    check("Tolino: unresolved gate keeps the Tolino path", g_tolino_seen_in_slot);
    g_tolino_gates[0] = saved;

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
