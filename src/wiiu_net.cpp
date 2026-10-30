// Network readiness for ENet on the Wii U. Linked in with -Wl,--wrap=enet_initialize / enet_host_create /
// enet_host_connect (see cmake/wiiu.cmake).
//
// So ENet's own sockets work as soon as the console has a connection.
//
//  * enet_initialize() (boot): only logs the connection state. It never waits, so startup is unaffected.
//  * enet_host_create() (entering netplay / hosting): waits up to kWaitMs for the connection and for an IP
//    address. If there is none it returns NULL, which upstream already treats as "could not open the
//    connection port".
//  * enet_host_connect() refuses a NULL host. Upstream doesn't check that its client host was created, and
//    ENet would crash on it.
//
// Everything is logged with smw_log(), so the reason shows up in the crash log if the game later stops.
// Free of SDL and upstream headers.
#include <cstddef>
#include <cstdint>

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <nn/ac/ac_c.h>
#include <nn/result.h>

#include "wiiu_log.h"

extern "C" {

int   __real_enet_initialize(void);
void* __real_enet_host_create(const void* address, size_t peerCount, size_t channelLimit,
                              uint32_t incomingBandwidth, uint32_t outgoingBandwidth);
void* __real_enet_host_connect(void* host, const void* address, size_t channelCount, uint32_t data);

}

namespace {

constexpr unsigned kPollMs = 100;
constexpr unsigned kWaitMs = 10000;

// true when the console is connected and has an IP address (returned in host byte order, 0 if none).
bool connected(uint32_t* ip = nullptr)
{
    BOOL up = FALSE;
    uint32_t addr = 0;
    if (NNResult_IsSuccess(ACIsApplicationConnected(&up)) && up
        && NNResult_IsSuccess(ACGetAssignedAddress(&addr)) && addr != 0) {
        if (ip) *ip = addr;
        return true;
    }
    return false;
}

bool ensure_up(const char* why)
{
    uint32_t ip = 0;
    if (connected(&ip)) {
        smw_log("[net] network is up (%s): %u.%u.%u.%u", why,
                (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        return true;
    }

    smw_log("[net] waiting for the network (%s)", why);
    for (unsigned waited = 0; waited < kWaitMs; waited += kPollMs) {
        OSSleepTicks(OSMillisecondsToTicks(kPollMs));
        if (connected(&ip)) {
            smw_log("[net] network is up after %u ms (%s): %u.%u.%u.%u", waited + kPollMs, why,
                    (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
            return true;
        }
    }

    smw_log("[net] network is not available (%s): no connection configured in System Settings, no Wi-Fi or cable, or no DHCP answer", why);
    return false;
}

} // namespace

extern "C" {

int __wrap_enet_initialize(void)
{
    uint32_t ip = 0;
    if (connected(&ip))
        smw_log("[net] boot: network already up");
    else
        smw_log("[net] boot: network not up yet (wut is connecting in the background)");
    return __real_enet_initialize();
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
