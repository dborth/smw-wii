// Crash log for the Wii U port.
//
// While the game runs, nothing is logged to disk: smw_log() appends to a small in-memory ring buffer and sends
// the line to OSReport (visible in Cemu's log and through Aroma's/UDP log tools). If the game ends abnormally,
// the ring is written to <save dir>smw-log.txt (default fs:/vol/external01/smw/smw-log.txt). A normal quit
// writes nothing.
//
// "Abnormal" means:
//   * before the game starts: the bundled game data (/vol/content/data) is missing or incomplete
//   * main() returned non-zero (upstream catches every exception, shows an error box, returns 1)
//   * exit() with a non-zero status, or exit() before main() was reached
//   * abort(), or an uncaught C++ exception (std::terminate)
//
// Bug-report builds (cmake -DSMW_WIIU_LOG=ON) also append every log line to <save dir>log.txt as it happens.
//
// Not covered: CPU exceptions (bad pointer, stack overflow, ...); they never reach this code. There is no
// failure screen either (the Wii build has one): the display belongs to SDL, so the log file and OSReport are
// the only outputs.
//
// Hooked in with -Wl,--wrap=SDL_RunApp / exit / abort / SDL_ShowSimpleMessageBox (see cmake/wiiu.cmake).
// SMW_SAVE_DIR and SMW_CONTENT_DIR come from cmake/wiiu.cmake.
#include "wiiu_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <coreinit/debug.h>
#include <coreinit/time.h>

namespace {

constexpr const char* kSaveDir    = SMW_SAVE_DIR;      // must end in '/'
constexpr const char* kContentDir = SMW_CONTENT_DIR;   // must end in '/'
constexpr const char* kLogPath    = SMW_SAVE_DIR "smw-log.txt";
#ifdef SMW_WIIU_LOG
constexpr const char* kLiveLogPath = SMW_SAVE_DIR "log.txt";
#endif

// Folders that must exist inside <content dir>data/ for the game to start (a spot check, not a full manifest).
constexpr const char* kRequiredDataDirs[] = { "gfx/packs", "maps", "music", "sfx" };

typedef int (*main_fn)(int, char**);

char g_ring[8192];                 // the log: oldest lines are dropped when it fills up
size_t g_ring_len = 0;
char g_game_error[200];            // text of the last error box upstream tried to show
bool g_main_entered = false;
bool g_reported = false;           // only the first failure is reported; anything after it is fallout
main_fn g_real_main = nullptr;

} // namespace

extern "C" void smw_log(const char* fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    char line[340];
    const unsigned ms = (unsigned)OSTicksToMilliseconds(OSGetSystemTime());
    int n = std::snprintf(line, sizeof line, "[%4u.%03u] %s\n", ms / 1000, ms % 1000, msg);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = sizeof line - 1;
    const size_t len = (size_t)n;

    OSReport("SMW: %s", line);

    if (g_ring_len + len >= sizeof g_ring) {   // full: keep the newest half, starting at a line boundary
        const size_t keep = sizeof g_ring / 2;
        const char* tail = g_ring + g_ring_len - keep;
        if (const char* nl = (const char*)std::memchr(tail, '\n', keep))
            tail = nl + 1;
        const size_t kept = (size_t)(g_ring + g_ring_len - tail);
        std::memmove(g_ring, tail, kept);
        g_ring_len = kept;
    }
    std::memcpy(g_ring + g_ring_len, line, len);
    g_ring_len += len;

#ifdef SMW_WIIU_LOG
    if (g_main_entered) {   // the SD card is mounted by then
        if (FILE* f = std::fopen(kLiveLogPath, "a")) {
            std::fwrite(line, 1, len, f);
            std::fclose(f);   // closed per line so the last lines survive a crash
        }
    }
#endif
}

namespace {

bool dir_exists(const char* path)
{
    DIR* d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

// One log line listing what is in a folder: shows at a glance when files were bundled one level too deep or
// into the wrong place, or which mounts exist at all.
void log_dir_contents(const char* path)
{
    DIR* d = opendir(path);
    if (!d) {
        smw_log("%s cannot be opened", path);
        return;
    }

    char line[256];
    size_t n = (size_t)std::snprintf(line, sizeof line, "%s contains:", path);
    if (n >= sizeof line) n = sizeof line - 1;
    int total = 0;
    while (const dirent* e = readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
            continue;
        ++total;
        if (n + 40 < sizeof line) {   // stop adding names when the line is nearly full
            n += (size_t)std::snprintf(line + n, sizeof line - n, " %s", e->d_name);
            if (n >= sizeof line) n = sizeof line - 1;
        }
    }
    closedir(d);

    if (total == 0)
        smw_log("%s is empty", path);
    else
        smw_log("%s", line);
}

// Best effort. Returns true if the file was written.
bool write_log_file(const char* why)
{
    // <save dir> may not exist yet: create it so the log has somewhere to go.
    char dir[128];
    std::snprintf(dir, sizeof dir, "%s", kSaveDir);
    const size_t n = std::strlen(dir);
    if (n > 0 && dir[n - 1] == '/') dir[n - 1] = '\0';
    mkdir(dir, 0777);   // harmless if it exists

    FILE* f = std::fopen(kLogPath, "w");   // truncate: the file describes only the latest failure
    if (!f) return false;

    const std::time_t now = std::time(nullptr);
    char stamp[32] = "unknown";
    if (const std::tm* t = std::gmtime(&now))
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", t);
    const struct mallinfo mi = mallinfo();

    std::fprintf(f,
                 "Super Mario War Wii U - stopped abnormally\n"
                 "Reason: %s\n"
                 "Time:   %s (console clock)\n"
                 "System: malloc in use %u KB\n"
                 "\nLog, newest last (times are seconds since the console started):\n",
                 why, stamp, (unsigned)(mi.uordblks / 1024));
    std::fwrite(g_ring, 1, g_ring_len, f);
    std::fclose(f);
    return true;
}

void report_failure(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void report_failure(const char* fmt, ...)
{
    if (g_reported) return;
    g_reported = true;

    char why[200];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);

    smw_log("STOPPED: %s", why);
    if (write_log_file(why))
        smw_log("log written to %s", kLogPath);
    else
        smw_log("could not write %s (no SD card, or it is read-only)", kLogPath);
}

// Describes the exception being handled. Call from a catch block or the terminate handler.
// Upstream throws std::string and const char* as well as std::exception.
void describe_current_exception(char* out, size_t size)
{
    std::snprintf(out, size, "(no active exception)");
    if (std::exception_ptr ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            std::snprintf(out, size, "%.160s", e.what());
        } catch (const std::string& s) {
            std::snprintf(out, size, "%.160s", s.c_str());
        } catch (const char* s) {
            std::snprintf(out, size, "%.160s", s ? s : "(null)");
        } catch (...) {
            std::snprintf(out, size, "unknown exception type");
        }
    }
}

[[noreturn]] void on_terminate()
{
    char what[200];
    describe_current_exception(what, sizeof what);
    report_failure("std::terminate(): %s", what);
    std::abort();
}

// Runs before any normal static initializer (including upstream's globals), so an exception thrown during
// static init also ends up in on_terminate(). If "static init starting" is the last line in the log, the
// program died while running static initializers.
void __attribute__((constructor(101))) early_init()
{
    std::set_terminate(on_terminate);
    smw_log("static init starting");
}

// The game can't do anything without its data, and upstream's own failure for missing files is obscure.
// So check first, and say plainly what is wrong. Returns false (after reporting) if the game can't start.
bool install_ok()
{
    char path[160];
    std::snprintf(path, sizeof path, "%sdata", kContentDir);
    if (!dir_exists(path)) {
        log_dir_contents(kContentDir);
        log_dir_contents("fs:/vol/");   // which mounts exist at all
        smw_log("HOW TO FIX: the game data is bundled in smw.wuhb: start it from the Wii U menu (Aroma). "
                "A bare .rpx has no /vol/content.");
        report_failure("game data not found: %s", path);
        return false;
    }

    int missing = 0;
    for (const char* sub : kRequiredDataDirs) {
        std::snprintf(path, sizeof path, "%sdata/%s", kContentDir, sub);
        if (!dir_exists(path)) {
            smw_log("missing folder: %s", path);
            ++missing;
        }
    }
    if (missing > 0) {
        std::snprintf(path, sizeof path, "%sdata", kContentDir);
        log_dir_contents(path);
        report_failure("game data incomplete: %d expected folder(s) missing in %sdata", missing, kContentDir);
        return false;
    }

    // Not fatal: the game runs without it, but settings and controls can't be saved.
    if (!dir_exists("fs:/vol/external01/"))
        smw_log("warning: SD card cannot be read: settings will not be saved");
    return true;
}

int traced_main(int argc, char** argv)
{
    g_main_entered = true;
    smw_log("main() entered");
    if (!install_ok())
        return 1;

    int rc = 1;
    try {
        rc = g_real_main(argc, argv);
    } catch (...) {
        char what[200];
        describe_current_exception(what, sizeof what);
        report_failure("exception escaped main(): %s", what);
        return 1;
    }

    smw_log("main() returned %d", rc);
    if (rc != 0) {
        if (g_game_error[0])
            report_failure("the game stopped with an error: %s", g_game_error);
        else
            report_failure("main() returned %d", rc);
    }
    return rc;
}

} // namespace

extern "C" int __real_SDL_RunApp(int argc, char* argv[], main_fn mainFunction, void* reserved);
extern "C" int __wrap_SDL_RunApp(int argc, char* argv[], main_fn mainFunction, void* reserved)
{
    smw_log("static init done");
    g_real_main = mainFunction;
    const int rc = __real_SDL_RunApp(argc, argv, traced_main, reserved);
    smw_log("SDL_RunApp returned %d", rc);
    return rc;
}

// Upstream reports every error it catches with an SDL message box (text: boilerplate + "The error message:" +
// the actual error), then returns 1 from main(). There is no message-box backend on the Wii U port, so this
// takes its place: the error text is kept for the log and the failure report (see traced_main). The real
// function is never called. (SDL's headers aren't needed: only the pointers are used.)
extern "C" bool __wrap_SDL_ShowSimpleMessageBox(unsigned /*flags*/, const char* title, const char* message, void* /*window*/)
{
    constexpr const char* kMarker = "The error message:";
    const char* detail = message ? std::strstr(message, kMarker) : nullptr;
    if (detail) {
        detail += std::strlen(kMarker);
        detail += std::strspn(detail, " \t\r\n");
    }

    smw_log("error box shown: %s", title ? title : "");
    if (!detail || !*detail) {
        smw_log("  (no error text)");
    } else {
        std::snprintf(g_game_error, sizeof g_game_error, "%s", detail);
        for (char* c = g_game_error; *c; ++c)
            if (*c == '\n' || *c == '\r') *c = ' ';

        for (const char* p = detail; *p;) {   // the ring's lines are short: log long text in pieces
            smw_log("  %.200s", p);
            p += strnlen(p, 200);
        }
    }
    return true;
}

extern "C" [[noreturn]] void __real_exit(int status);
extern "C" [[noreturn]] void __wrap_exit(int status)
{
    smw_log("exit(%d) called from %p", status, __builtin_return_address(0));
    if (status != 0 || !g_main_entered)
        report_failure("exit(%d)%s", status, g_main_entered ? "" : " before the game started");
    __real_exit(status);
}

extern "C" [[noreturn]] void __real_abort(void);
extern "C" [[noreturn]] void __wrap_abort(void)
{
    smw_log("abort() called from %p", __builtin_return_address(0));
    report_failure("abort()");
    __real_abort();
}
