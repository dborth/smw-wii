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

extern "C" SDL_Surface* __real_IMG_Load(const char* file);
extern "C" SDL_Surface* __wrap_IMG_Load(const char* file)
{
    SDL_Surface* surf = __real_IMG_Load(file);
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
