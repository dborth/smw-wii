// Hard-wires Super Mario War's file locations on the Wii U, without modifying upstream.
// Hooked in with -Wl,--wrap=SDL_GetBasePath / -Wl,--wrap=SDL_GetPrefPath (see cmake/wiiu.cmake).
// SMW_CONTENT_DIR and SMW_SAVE_DIR come from cmake/wiiu.cmake.
//
//     game data -> <SMW_CONTENT_DIR>data/   e.g. fs:/vol/content/data/   (read-only: bundled in SuperMarioWar-WiiU.wuhb)
//     settings  -> <SMW_SAVE_DIR>           e.g. fs:/vol/external01/smw/ (SD card, writable)
//
// Upstream appends "data" to SDL_GetBasePath() for the game data, and keeps everything it writes (options,
// controls) under SDL_GetPrefPath(), so the two can live on different devices.
#include <SDL3/SDL.h>

namespace {
const char* const kContent = SMW_CONTENT_DIR;   // must end in '/'
const char* const kSave    = SMW_SAVE_DIR;      // must end in '/'
}

// Called at static-init time (RootDataDirectory is initialized from it), before main(), so this must only return a string.
extern "C" const char* __wrap_SDL_GetBasePath(void)
{
    return kContent;   // static; the caller never frees SDL_GetBasePath() results
}

// Called once from main_game() -> ensureSettingsDir(), long after startup.
extern "C" char* __wrap_SDL_GetPrefPath(const char* /*org*/, const char* /*app*/)
{
    return SDL_strdup(kSave);   // caller releases with SDL_free()
}
