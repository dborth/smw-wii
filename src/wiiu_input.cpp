// Controller support for the Wii U port, without modifying upstream (hooked in with -Wl,--wrap, see cmake/wiiu.cmake).
// SMW_SAVE_DIR comes from cmake/wiiu.cmake. Copied from wii_input.cpp on purpose: the two consoles share no code.
//
// The SDL3 Wii U (wut) joystick driver does the hard part (src/joystick/wiiu/SDL_sysjoystick.c in dborth/SDL, branch
// wut-port): there is one joystick per Wii U player (GamePad + Wii Remote channel 0 = player 1, channel n = player
// n + 1), every one of them has the SAME layout whatever is plugged in, and a Wii Remote held sideways is already
// rotated there (D-pad and buttons). So nothing here depends on the controller type, and nothing here rotates anything.
//
// What upstream gets wrong, and this file fixes:
//  * init_joysticks() runs once at startup, ignores the instance IDs SDL3 returns and calls
//    SDL_OpenJoystick(i) with an *index*. The pads may only show up after the first event pump,
//    so nothing is ever opened -- and SDL only delivers button events for opened joysticks.
//  * With no controls.sdl2.bin every player is bound to the *keyboard* configuration, so "Press any key"
//    waits for a USB keyboard that isn't there.
//
// What this does: watches the events the game reads (SDL_PollEvent / SDL_WaitEvent). When a joystick is
// ADDED it opens it and binds it to the game player with the same number as the joystick's SDL player index
// (the Wii U player), with the default buttons below. On REMOVED it closes it and gives the player back to the
// keyboard. Players can still rebind everything in SMW's own controls menu.
//
// Device ids: upstream's input code is inconsistent about what a "device" is. The controls menu (and the
// saved controls file, and "iDevice >= joystickcount") treat it as an INDEX 0..joystickcount-1, while
// CPlayerInput::Update() compares it with SDL's joystick *instance id* (event.jbutton.which). We make everything
// use the index: iDevice is the pad's position in g_pads, joystick events are rewritten to carry that index in
// `which`, and SDL_GetJoystickNameForID / SDL_GetJoystickFromPlayerIndex are wrapped to take an index too.
#include <SDL3/SDL.h>

#include "GameValues.h"   // CGameValues, CInputPlayerControl, JOY_* constants (upstream src/common)

#include <sys/stat.h>

extern CGameValues game_values;       // upstream, common/global.cpp
extern SDL_Joystick** joysticks;      // upstream, common/global.cpp (used by the controls menu)
extern int joystickcount;

namespace {

constexpr int kMaxPads = 4;   // the driver exposes one joystick per Wii U player

struct Pad {
    SDL_JoystickID id = 0;
    SDL_Joystick* js = nullptr;
    int player = -1;   // game player 0..3 (= the Wii U player), or -1 if SDL gave no player index
};

Pad g_pads[kMaxPads];
int g_num_pads = 0;
bool g_initial_scan_done = false;

// Raw button numbers of the driver (WIIU_BTN_* in SDL_sysjoystick.c), named for the Wii U Pro Controller. Only the
// ones used below. On a sideways Wii Remote the driver maps 2 -> A, 1 -> B, Wii Remote A -> X, Wii Remote B -> Y;
// a Nunchuk's C is X and Z is Y.
enum { BTN_A = 0, BTN_B = 1, BTN_X = 2, BTN_Y = 3, BTN_MINUS = 8, BTN_PLUS = 9 };

constexpr int B(int n) { return JOY_BUTTON_START + n; }

// The D-pad (hat 0) is the one direction input every source has; the Wii Remote has no stick. Players with a stick
// can rebind to it in the controls menu.
//
// Game order: left, right, jump, down, turbo, powerup, start, cancel
constexpr int kGameKeys[NUM_KEYS] = { JOY_HAT_LEFT, JOY_HAT_RIGHT, B(BTN_A), JOY_HAT_DOWN, B(BTN_B), B(BTN_X), B(BTN_PLUS), B(BTN_MINUS) };
// Menu order: up, down, left, right, select, cancel, random, fast-scroll
constexpr int kMenuKeys[NUM_KEYS] = { JOY_HAT_UP, JOY_HAT_DOWN, JOY_HAT_LEFT, JOY_HAT_RIGHT, B(BTN_A), B(BTN_B), B(BTN_X), B(BTN_Y) };

bool controls_file_exists()
{
    struct stat st;
    return stat(SMW_SAVE_DIR "controls.sdl2.bin", &st) == 0;
}

// The controls menu and the "saved device is still connected?" check both read these globals.
void publish_joystick_array()
{
    // The old array is deliberately leaked (a few pointers): upstream may still hold it.
    SDL_Joystick** arr = new SDL_Joystick*[g_num_pads > 0 ? g_num_pads : 1];
    for (int i = 0; i < g_num_pads; ++i) arr[i] = g_pads[i].js;
    joysticks = arr;
    joystickcount = g_num_pads;
}

int find_pad(SDL_JoystickID id)
{
    for (int i = 0; i < g_num_pads; ++i)
        if (g_pads[i].id == id) return i;
    return -1;
}

void on_joystick_added(SDL_JoystickID id)
{
    if (find_pad(id) >= 0 || g_num_pads >= kMaxPads) return;

    SDL_Joystick* js = SDL_OpenJoystick(id);
    if (!js) return;

    const int idx = g_num_pads;   // device index as upstream sees it (see "Device ids" above)
    Pad& pad = g_pads[g_num_pads++];
    pad.id = id;
    pad.js = js;

    const int player = SDL_GetJoystickPlayerIndex(js);   // the Wii U player, fixed by the driver
    pad.player = (player >= 0 && player < 4) ? player : -1;

    if (pad.player >= 0) {
        CInputPlayerControl& cfg = game_values.inputConfiguration[pad.player][1];   // [1] = joystick config
        cfg.iDevice = static_cast<short>(idx);

        if (!controls_file_exists()) {   // first run: install our defaults; otherwise keep what the player saved
            for (int i = 0; i < NUM_KEYS; ++i) {
                cfg.inputGameControls[0].keys[i] = kGameKeys[i];
                cfg.inputGameControls[1].keys[i] = kMenuKeys[i];
            }
        }
        game_values.playerInput.inputControls[pad.player] = &cfg;
    }

    publish_joystick_array();
}

void on_joystick_removed(SDL_JoystickID id)
{
    const int i = find_pad(id);
    if (i < 0) return;

    const int player = g_pads[i].player;
    if (g_pads[i].js) SDL_CloseJoystick(g_pads[i].js);

    if (player >= 0)   // back to the keyboard until the pad returns
        game_values.playerInput.inputControls[player] = &game_values.inputConfiguration[player][0];

    for (int j = i; j + 1 < g_num_pads; ++j) g_pads[j] = g_pads[j + 1];
    --g_num_pads;
    g_pads[g_num_pads] = Pad{};
    // Pads after the removed one moved down by one: keep their players' device index in step.
    for (int j = i; j < g_num_pads; ++j)
        if (g_pads[j].player >= 0)
            game_values.inputConfiguration[g_pads[j].player][1].iDevice = static_cast<short>(j);
    publish_joystick_array();
}

// Pick up pads that already existed before the first event was read.
void initial_scan()
{
    if (g_initial_scan_done) return;
    g_initial_scan_done = true;

    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    for (int i = 0; i < n; ++i) on_joystick_added(ids[i]);
    SDL_free(ids);
}

// Joystick input events carry SDL's instance id; upstream wants the device index (see "Device ids" above).
void remap_which(SDL_Event& e)
{
    SDL_JoystickID* which = nullptr;
    switch (e.type) {
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: which = &e.jaxis.which; break;
    case SDL_EVENT_JOYSTICK_BALL_MOTION: which = &e.jball.which; break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: which = &e.jhat.which; break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP: which = &e.jbutton.which; break;
    default: return;
    }
    const int i = find_pad(*which);
    *which = (i >= 0) ? static_cast<SDL_JoystickID>(i) : 0x7fffffffu;   // unknown pad: matches no player
}

void observe(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_JOYSTICK_ADDED) on_joystick_added(e.jdevice.which);
    else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) on_joystick_removed(e.jdevice.which);
}

} // namespace

extern "C" bool __real_SDL_PollEvent(SDL_Event* event);
extern "C" bool __real_SDL_WaitEvent(SDL_Event* event);

extern "C" bool __wrap_SDL_PollEvent(SDL_Event* event)
{
    initial_scan();
    const bool got = __real_SDL_PollEvent(event);
    if (got && event) { remap_which(*event); observe(*event); }
    return got;
}

extern "C" bool __wrap_SDL_WaitEvent(SDL_Event* event)
{
    initial_scan();
    const bool got = __real_SDL_WaitEvent(event);
    if (got && event) { remap_which(*event); observe(*event); }
    return got;
}

// Upstream's controls menu passes device INDICES (0..joystickcount-1) to this and builds a std::string from the
// result; with SDL3 instance ids that is NULL and std::string(nullptr) throws.
extern "C" const char* __real_SDL_GetJoystickNameForID(SDL_JoystickID id);
extern "C" const char* __wrap_SDL_GetJoystickNameForID(SDL_JoystickID id)
{
    if (id < static_cast<SDL_JoystickID>(g_num_pads) && g_pads[id].js)
        if (const char* n = SDL_GetJoystickName(g_pads[id].js)) return n;
    const char* n = __real_SDL_GetJoystickNameForID(id);
    return n ? n : "Unknown controller";   // never NULL
}

// Same index convention (used for the team-colour LED on the team select screen).
extern "C" SDL_Joystick* __real_SDL_GetJoystickFromPlayerIndex(int player_index);
extern "C" SDL_Joystick* __wrap_SDL_GetJoystickFromPlayerIndex(int player_index)
{
    if (player_index >= 0 && player_index < g_num_pads) return g_pads[player_index].js;
    return __real_SDL_GetJoystickFromPlayerIndex(player_index);
}
