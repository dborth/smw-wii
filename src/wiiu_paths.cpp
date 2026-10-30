// Hard-wires Super Mario War's file locations on the Wii U, without modifying upstream.
// Hooked in with -Wl,--wrap=SDL_GetBasePath / -Wl,--wrap=SDL_GetPrefPath (see cmake/wiiu.cmake).
// SMW_ROOT comes from cmake/wiiu.cmake.
//
//     game data -> <SMW_ROOT>data/      e.g. fs:/vol/external01/smw/data/   (contents of supermariowar/data)
//     settings  -> <SMW_ROOT>           e.g. fs:/vol/external01/smw/
#include <SDL3/SDL.h>

namespace {
const char* const kRoot = SMW_ROOT;   // must end in '/'
}

// Called at static-init time (RootDataDirectory is initialized from it), before main(), so this must only return a string.
extern "C" const char* __wrap_SDL_GetBasePath(void)
{
    return kRoot;   // static; the caller never frees SDL_GetBasePath() results
}

// Called once from main_game() -> ensureSettingsDir(), long after startup.
extern "C" char* __wrap_SDL_GetPrefPath(const char* /*org*/, const char* /*app*/)
{
    return SDL_strdup(kRoot);   // caller releases with SDL_free()
}
