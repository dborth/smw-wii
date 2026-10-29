// Hard-wires Super Mario War's file locations on the Wii, without modifying upstream.
// Hooked in with -Wl,--wrap=SDL_GetBasePath / -Wl,--wrap=SDL_GetPrefPath (see CMakeLists.txt).
// SMW_WII_ROOT comes from CMakeLists.txt.
//
//     game data -> <SMW_WII_ROOT>data/      e.g. sd:/smw/data/   (contents of supermariowar/data)
//     settings  -> <SMW_WII_ROOT>           e.g. sd:/smw/
#include <SDL3/SDL.h>

namespace {
const char* const kRoot = SMW_WII_ROOT;   // must end in '/'
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
