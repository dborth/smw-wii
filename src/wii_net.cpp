// Network bring-up for ENet. Linked in via -Wl,--wrap=enet_initialize / enet_deinitialize (CMakeLists.txt).
// Kept free of libogc headers: wiisocket.h must not be mixed with libogc's <network.h>.
#include <wiisocket.h>

extern "C" {

int __real_enet_initialize(void);
void __real_enet_deinitialize(void);

// wiisocket_init(): 0 = ok, 1 = already up, < 0 = failure (no network / IOS error).
// Failing here makes upstream's NetworkLayerENet report that netplay is unavailable.
int __wrap_enet_initialize(void)
{
    if (wiisocket_init() < 0)
        return -1;
    return __real_enet_initialize();
}

void __wrap_enet_deinitialize(void)
{
    __real_enet_deinitialize();
    wiisocket_deinit();
}

}
