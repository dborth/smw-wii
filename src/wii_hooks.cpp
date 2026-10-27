// Hard-wires Super Mario War's file locations on Wii, without modifying upstream, and carries the
// diagnostics for bring-up. Linked in via -Wl,--wrap=SDL_GetBasePath / SDL_GetPrefPath (CMakeLists.txt).
//
//     game data -> <SMW_WII_ROOT>data/      e.g. sd:/smw/data/   (contents of supermariowar/data)
//     settings  -> <SMW_WII_ROOT>           e.g. sd:/smw/
//
// Logging goes to SYS_Report (Dolphin's OSREPORT; no stdio involved, so it works even before/after
// stdout is set up) and, if SMW_WII_LOG is defined, is appended to <root>log.txt (opened and closed per
// line so the last lines survive a crash). stdout/stderr are never reopened.
#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <malloc.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fat.h>
#include <ogc/system.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/keyboard.h>
#include <wiiuse/wpad.h>
#include <machine/malloc.h>   // mem1_*/mem2_* (libogc2 split heap)

#include "wii_mem2_alloc.h"

#ifndef SMW_WII_ROOT
#define SMW_WII_ROOT "sd:/smw/"   // must end in '/'
#endif

namespace {

const char* const kRoot    = SMW_WII_ROOT;
const char* const kLogPath = SMW_WII_ROOT "log.txt";

void smw_log(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    SYS_Report("[smw] %s\n", buf);
#ifdef SMW_WII_LOG
    if (FILE* f = std::fopen(kLogPath, "a")) {
        std::fputs(buf, f);
        std::fputc('\n', f);
        std::fclose(f);
    }
#endif
}

bool dir_exists(const char* path)
{
    DIR* d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

[[noreturn]] void on_terminate()
{
    smw_log("std::terminate() called");
    if (std::exception_ptr ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            smw_log("  uncaught C++ exception: %s", e.what());
        } catch (...) {
            smw_log("  uncaught non-std exception");
        }
    } else {
        smw_log("  (no active exception)");
    }
    std::abort();
}

void on_exit_handler()
{
    // Runs on exit()/return from main. If this shows up right before "start of main" repeats,
    // the program is exiting (e.g. upstream returned an error) rather than crashing.
    smw_log("exit handler ran (program is exiting normally via exit()/return from main)");
}

struct Diagnostics {
    Diagnostics()
    {
        std::set_terminate(on_terminate);
        std::atexit(on_exit_handler);
    }
} g_diagnostics;

} // namespace

// For src/wii_input.cpp, which includes upstream's headers and so must stay clear of the libogc ones.
extern "C" void smw_log_str(const char* s) { smw_log("%s", s); }

// "MEM1 used/free | MEM2 used/free | arena2 left". Per libogc2's malloc_wii.c: malloc()/new = the fixed MEM1 pool,
// mem2_*() (and, via the routing below, SDL's allocations) = the growable MEM2 pool. MEM2's "free" only counts what the
// pool has already claimed from the arena, so the arena's remaining size is the real headroom.
static void format_heaps(char* out, size_t n)
{
    const struct mallinfo m1 = mem1_mallinfo();
    const struct mallinfo m2 = mem2_mallinfo();
    std::snprintf(out, n, "MEM1 %d used/%d free KB | MEM2 %d used/%d free, arena2 left %u KB",
                  (int)(m1.uordblks / 1024), (int)(m1.fordblks / 1024),
                  (int)(m2.uordblks / 1024), (int)(m2.fordblks / 1024),
                  (unsigned)(SYS_GetArena2Size() / 1024));
}

// Call from anywhere (also used by src/wii_input.cpp).
extern "C" void smw_log_mem(const char* tag)
{
    char heaps[200];
    format_heaps(heaps, sizeof heaps);
    smw_log("[mem:%s] %s", tag, heaps);
}

// Route SDL's allocator to MEM2 (see wii_mem2_alloc.h for why). Priority 101: after libogc2's own init_malloc
// (constructor(0), which creates the MEM1/MEM2 pools) and before every default-priority static initializer, so before
// the first SDL call.
static void __attribute__((constructor(101))) route_sdl_allocator_to_mem2()
{
    void* probe = mem2_malloc(64);
    if (!probe) {
        SYS_Report("[smw] MEM2 heap unavailable (MALLOC_MEM2 == 0?); SDL stays on the MEM1 heap\n");
        return;
    }
    mem2_free(probe);
    const bool ok = SDL_SetMemoryFunctions(smw_mem::malloc, smw_mem::calloc, smw_mem::realloc, smw_mem::free);
    SYS_Report("[smw] SDL allocator -> MEM2: %s\n", ok ? "ok" : "SDL_SetMemoryFunctions FAILED");
}

// Logs every image SMW loads (size, running total, both heaps) so we can see what the game actually costs.
// Linked in with -Wl,--wrap=IMG_Load; SDL_image's header isn't needed, only SDL3's SDL_Surface.
extern "C" SDL_Surface* __real_IMG_Load(const char* file);
extern "C" SDL_Surface* __wrap_IMG_Load(const char* file)
{
    static unsigned count = 0;
    static unsigned total_kb = 0;

    SDL_Surface* surf = __real_IMG_Load(file);

    const char* base = file ? std::strrchr(file, '/') : nullptr;
    base = base ? base + 1 : (file ? file : "?");
    char heaps[200];
    format_heaps(heaps, sizeof heaps);

    if (surf) {
        const unsigned kb = (unsigned)(((size_t)surf->pitch * (size_t)surf->h) / 1024);
        total_kb += kb;
        ++count;
        smw_log("[img #%u] %u KB %dx%d %s | total %u KB | %s", count, kb, surf->w, surf->h, base, total_kb, heaps);
    } else {
        smw_log("[img] FAILED %s: %s | %s", file ? file : "?", SDL_GetError(), heaps);
    }
    return surf;
}

// Called at static-init time (RootDataDirectory is initialized from it), before SDL_RunApp mounts the
// SD card, so this must only return a string.
extern "C" const char* __wrap_SDL_GetBasePath(void)
{
    return kRoot;   // static; the caller never frees SDL_GetBasePath() results
}

// Called once from main_game() -> ensureSettingsDir(), long after SDL_RunApp.
extern "C" char* __wrap_SDL_GetPrefPath(const char* /*org*/, const char* /*app*/)
{
    return SDL_strdup(kRoot);   // caller releases with SDL_free()
}
