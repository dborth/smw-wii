// Early-boot diagnostics for the Wii port. Linked in with
//   -Wl,--wrap=SDL_RunApp -Wl,--wrap=exit -Wl,--wrap=abort      (see CMake snippet)
//
// What this does:
//   1. Keeps every boot message in an in-memory ring buffer (and SYS_Report, for Dolphin / USB Gecko).
//   2. Wraps SDL_RunApp: logs before/after it, and hands it a traced main() so we know exactly which
//      stage was reached (static init -> RunApp wrapper -> main entered -> main returned rc=N).
//   3. Once main() starts (SD is mounted by then) it creates <root>boot.txt (truncated each run), dumps
//      the ring into it, and appends every later boot_log() line.
//   4. Wraps exit()/abort() and installs a std::terminate handler. On an abnormal end (abort, terminate,
//      exit(nonzero), exit before main started, uncaught exception) it draws the last log lines on
//      screen and waits for a button press, so you can read (or photograph) why it quit. A normal
//      exit(0) after main() ran still returns to the HBC immediately.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <malloc.h>
#include <sys/stat.h>

#include <fat.h>
#include <ogcsys.h>
#include <ogc/consol.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <wiiuse/wpad.h>

#ifndef SMW_WII_ROOT
#define SMW_WII_ROOT "sd:/smw/"   // must end in '/'
#endif

namespace {

constexpr const char* kBootFile = SMW_WII_ROOT "boot.txt";

typedef int (*main_fn)(int, char**);

char g_ring[8192];
size_t g_ring_len = 0;
bool g_file_ready = false;     // SD mounted and boot.txt created
bool g_main_entered = false;
bool g_in_fatal = false;
main_fn g_real_main = nullptr;

void append_file(const char* s, const char* mode)
{
    if (FILE* f = std::fopen(kBootFile, mode)) {
        std::fputs(s, f);
        std::fclose(f);    // fclose flushes libfat's cache, so the last line survives a crash
    }
}

void boot_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void boot_log(const char* fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    char line[340];
    const unsigned ms = (unsigned)ticks_to_millisecs(gettime());
    const int n = std::snprintf(line, sizeof line, "[boot %u.%03u] %s\n", ms / 1000, ms % 1000, msg);

    // in-memory ring: when full, drop the oldest half
    const size_t len = n > 0 ? (size_t)n : 0;
    if (g_ring_len + len >= sizeof g_ring) {
        const size_t keep = sizeof g_ring / 2;
        std::memmove(g_ring, g_ring + g_ring_len - keep, keep);
        g_ring_len = keep;
    }
    std::memcpy(g_ring + g_ring_len, line, len);
    g_ring_len += len;

    if (g_file_ready) append_file(line, "a");
}

// SD is mounted by SDL_RunApp before it calls main. Create boot.txt then and flush what we have so far.
void open_boot_file()
{
    if (g_file_ready) return;
    mkdir("sd:/smw", 0777);   // harmless if it exists (matches SMW_WII_ROOT's default)
    FILE* f = std::fopen(kBootFile, "w");   // truncate: boot.txt describes only the latest run
    if (!f) {
        return;
    }
    std::fwrite(g_ring, 1, g_ring_len, f);
    std::fclose(f);
    g_file_ready = true;
}

// Prints the last `n` lines of the ring.
void print_tail(int n)
{
    size_t start = g_ring_len;
    int lines = 0;
    while (start > 0) {
        --start;
        if (g_ring[start] == '\n' && start + 1 < g_ring_len && ++lines > n) {
            ++start;
            break;
        }
    }
    std::fwrite(g_ring + start, 1, g_ring_len - start, stdout);
}

// Black-box crash screen: own framebuffer + libogc console, then wait for a button.
void fatal_screen(const char* why)
{
    if (g_in_fatal) return;
    g_in_fatal = true;
    boot_log("FATAL: %s", why);

    VIDEO_Init();
    GXRModeObj* rmode = VIDEO_GetPreferredMode(nullptr);
    void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    CON_Init(xfb, 16, 16, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    std::printf("\n Super Mario War (Wii) stopped: %s\n\n", why);
    print_tail(18);
    std::printf("\n Press any button to return to the loader (auto-exit after 2 min).\n");
    std::fflush(stdout);

    WPAD_Init();    // normally already done by SDL_RunApp; harmless if repeated
    PAD_Init();
    for (int frame = 0; frame < 60 * 120; ++frame) {
        VIDEO_WaitVSync();
        WPAD_ScanPads();
        PAD_ScanPads();
        if (WPAD_ButtonsDown(0) || WPAD_ButtonsDown(1) || PAD_ButtonsDown(0))
            break;
    }
}

[[noreturn]] void boot_terminate()
{
    char what[200] = "std::terminate() (no active exception)";
    if (std::exception_ptr ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::snprintf(what, sizeof what, "uncaught exception: %.160s", e.what());
        } catch (...) {
            std::snprintf(what, sizeof what, "uncaught non-std exception");
        }
    }
    boot_log("terminate: %s", what);
    fatal_screen(what);
    std::abort();
}

// Earliest point we control: after libogc's own init (constructor(0..100)), before any normal static
// initializer (including upstream's globals). If this line never shows on the crash screen, the program
// died before static initialization even started.
void __attribute__((constructor(101))) early_marker()
{
    std::set_terminate(boot_terminate);
    boot_log("static init reached (constructor 101)");
}

int traced_main(int argc, char** argv)
{
    g_main_entered = true;
    open_boot_file();
    boot_log("main() entered, argc=%d argv0=%s", argc, argc > 0 && argv && argv[0] ? argv[0] : "?");

    int rc = -1;
    try {
        rc = g_real_main(argc, argv);
    } catch (const std::exception& e) {
        char what[200];
        std::snprintf(what, sizeof what, "exception escaped main(): %.160s", e.what());
        boot_log("%s", what);
        fatal_screen(what);
        return -1;
    } catch (...) {
        boot_log("non-std exception escaped main()");
        fatal_screen("non-std exception escaped main()");
        return -1;
    }
    boot_log("main() returned %d", rc);
    if (rc != 0) fatal_screen("main() returned a non-zero exit code");
    return rc;
}

} // namespace

// Lets other files (e.g. the SDL_ShowSimpleMessageBox wrapper in wii_hooks.cpp) put an error on screen.
extern "C" void smw_fatal(const char* why) { fatal_screen(why ? why : "fatal error"); }
extern "C" void smw_boot_log(const char* s) { boot_log("%s", s); }

extern "C" int __real_SDL_RunApp(int argc, char* argv[], main_fn mainFunction, void* reserved);
extern "C" int __wrap_SDL_RunApp(int argc, char* argv[], main_fn mainFunction, void* reserved)
{
    boot_log("SDL_RunApp wrapper entered (static init completed)");
    boot_log("IOS %u, preferred IOS %d (SDL_RunApp reloads IOS when they differ)",
             (unsigned)IOS_GetVersion(), (int)IOS_GetPreferredVersion());
    boot_log("MEM1 arena %u KB, MEM2 arena %u KB", (unsigned)(SYS_GetArena1Size() / 1024),
             (unsigned)(SYS_GetArena2Size() / 1024));

    g_real_main = mainFunction;
    const int rc = __real_SDL_RunApp(argc, argv, traced_main, reserved);
    boot_log("SDL_RunApp returned %d", rc);
    return rc;
}

extern "C" [[noreturn]] void __real_exit(int status);
extern "C" [[noreturn]] void __wrap_exit(int status)
{
    boot_log("exit(%d) called from lr=%p (main entered: %s)", status, __builtin_return_address(0),
             g_main_entered ? "yes" : "NO");
    if (status != 0 || !g_main_entered) {
        char why[80];
        std::snprintf(why, sizeof why, "exit(%d) %s", status, g_main_entered ? "" : "before main() started");
        fatal_screen(why);
    }
    __real_exit(status);
}

extern "C" [[noreturn]] void __real_abort(void);
extern "C" [[noreturn]] void __wrap_abort(void)
{
    boot_log("abort() called from lr=%p", __builtin_return_address(0));
    fatal_screen("abort()");
    __real_abort();
}
