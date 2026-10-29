// Network bring-up for ENet on the Wii. Linked in with -Wl,--wrap=enet_initialize / enet_deinitialize /
// enet_host_create / enet_host_connect (see CMakeLists.txt).
// Kept free of libogc and upstream headers: wiisocket.h must not be mixed with libogc's <network.h>.
//
// Upstream calls net_init() -> enet_initialize() once at startup, and creates its sockets later, with
// enet_host_create(), each time the player enters the netplay menu or hosts a game. On the Wii the network
// has to be brought up first (IOS network stack + DHCP), which can take several seconds, or never succeed.
//
//  * At boot: enet_initialize() only STARTS the bring-up (wiisocket_async_init) and returns at once, so
//    startup never waits for the network. (ENet's own enet_initialize() does nothing on this platform; all
//    the real work happens in enet_host_create().)
//  * Entering netplay / hosting: enet_host_create() makes sure the network is up first. Normally it has been
//    for a long time by then. If it isn't, because the first attempt failed or the link has dropped since,
//    the bring-up is started again and we wait for it (at most kWaitMs). If it still isn't up we return NULL,
//    which upstream already treats as "could not open the connection port".
//  * enet_host_connect() refuses a NULL host. Upstream doesn't check that its client host was created, and
//    ENet would crash on it.
//
// Everything is logged with smw_log(), so the reason shows up in the crash log if the game later stops.
#include <cstddef>
#include <cstdint>

#include <SDL3/SDL_timer.h>
#include <wiisocket.h>

#include "wii_log.h"

extern "C" {

int  __real_enet_initialize(void);
void __real_enet_deinitialize(void);
void* __real_enet_host_create(const void* address, size_t peerCount, size_t channelLimit,
                              uint32_t incomingBandwidth, uint32_t outgoingBandwidth);
void* __real_enet_host_connect(void* host, const void* address, size_t channelCount, uint32_t data);

// From libwiisocket. Declared here because <unistd.h> would drag in headers that clash with wiisocket.h.
long gethostid(void);

}

namespace {

constexpr unsigned kPollMs = 50;
constexpr unsigned kWaitMs = 15000;   // longer than wiisocket's own worst case (about 13 s): a wait always ends with an answer

// Written by the completion callback, which runs in IOS callback context: it only stores values.
volatile bool     g_done = false;
volatile int      g_done_res = 0;
volatile uint32_t g_done_at = 0;
uint32_t          g_started_at = 0;
bool              g_reported = true;   // has the outcome of the latest attempt been logged yet?

void on_init_done(int res, void*)
{
    g_done_res = res;
    g_done_at = (uint32_t)SDL_GetTicks();
    g_done = true;
}

const char* describe(int res)
{
    switch (res) {
    case -2: return "libwiisocket could not start";
    case -3: return "no network connection (no connection configured in Wii Settings, no Wi-Fi or cable, or no DHCP answer)";
    case -4: return "could not mount the socket device";
    default: return "unknown error";
    }
}

// wiisocket_async_init(): 0 = started, 1 = already up, -1 = already in progress, < -1 = could not start.
bool start_bringup(const char* why)
{
    g_done = false;
    g_reported = false;
    g_started_at = (uint32_t)SDL_GetTicks();

    const int r = wiisocket_async_init(on_init_done, nullptr);
    switch (r) {
    case 0:
        smw_log("[net] network bring-up started (%s)", why);
        return true;
    case 1:
        g_reported = true;
        smw_log("[net] network already up (%s)", why);
        return true;
    case -1:
        smw_log("[net] network bring-up already in progress (%s)", why);
        return true;
    default:
        g_reported = true;
        smw_log("[net] network bring-up could not start (%s): wiisocket error %d, %s", why, r, describe(r));
        return false;
    }
}

// The callback can't log, so the outcome is logged here, the next time we look.
void report_if_finished()
{
    if (g_reported || !g_done) return;
    g_reported = true;
    const unsigned ms = (unsigned)(g_done_at - g_started_at);
    if (g_done_res >= 0)
        smw_log("[net] network is up (took %u ms)", ms);
    else
        smw_log("[net] network bring-up FAILED after %u ms: wiisocket error %d, %s", ms, g_done_res, describe(g_done_res));
}

bool ensure_up(const char* why)
{
    int st = wiisocket_get_status();   // 1 = up, 0 = not initialised (never started, or failed), -1 = in progress

    if (st == 1) {
        // The link can drop after a successful bring-up (cable pulled, access point gone). Ask IOS for our
        // current IP: instant when we have one, about half a second when we don't. Addresses such as
        // 192.168.x.x are negative as a signed long, so only 0 and -1 (error) mean "none".
        const long ip = gethostid();
        if (ip != 0 && ip != -1) {
            report_if_finished();
            return true;
        }
        smw_log("[net] network was up but has no IP address now: restarting it (%s)", why);
        wiisocket_deinit();
        st = 0;
    }

    if (st == 0 && !start_bringup(why))
        return false;

    for (unsigned waited = 0; waited < kWaitMs && wiisocket_get_status() == -1; waited += kPollMs)
        SDL_Delay(kPollMs);

    report_if_finished();
    if (wiisocket_get_status() == 1)
        return true;

    smw_log("[net] network is not available (%s)", why);
    return false;
}

} // namespace

extern "C" {

// Boot: start the bring-up in the background and carry on. Upstream's net_init() then also loads the
// server list, which it skips if this fails.
int __wrap_enet_initialize(void)
{
    start_bringup("boot");
    return __real_enet_initialize();
}

void __wrap_enet_deinitialize(void)
{
    __real_enet_deinitialize();
    wiisocket_deinit();
    smw_log("[net] network shut down");
}

// Entering the netplay menu (address == NULL: client) or hosting a game (address given).
void* __wrap_enet_host_create(const void* address, size_t peerCount, size_t channelLimit,
                              uint32_t incomingBandwidth, uint32_t outgoingBandwidth)
{
    if (!ensure_up(address ? "hosting a netplay game" : "entering netplay"))
        return nullptr;
    return __real_enet_host_create(address, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth);
}

void* __wrap_enet_host_connect(void* host, const void* address, size_t channelCount, uint32_t data)
{
    if (!host) {
        smw_log("[net] connect refused: there is no network host (the network is down)");
        return nullptr;
    }
    return __real_enet_host_connect(host, address, channelCount, data);
}

}
