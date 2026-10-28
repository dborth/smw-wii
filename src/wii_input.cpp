// Controller support for the Wii port, without modifying upstream (hooked in with -Wl,--wrap, see CMakeLists.txt).
//
// Why this is needed (all in upstream's input code, nothing wrong with SDL here):
//  * init_joysticks() runs once at startup, ignores the instance IDs SDL3 returns and calls
//    SDL_OpenJoystick(i) with an *index*. On the Wii the pads only show up after the first event pump,
//    so nothing is ever opened -- and SDL only delivers button events for opened joysticks.
//  * With no controls.sdl2.bin every player is bound to the *keyboard* configuration, so "Press any key"
//    waits for a USB keyboard that isn't there.
//
// What this does: watches the events the game reads (SDL_PollEvent / SDL_WaitEvent). When a joystick is
// ADDED it opens it, picks defaults for that controller type, and binds it to the first player that is
// still on the keyboard. On REMOVED it closes it and gives the player back to the keyboard.
// Players can still rebind everything in SMW's own controls menu.
//
// Device ids: upstream's input code is inconsistent about what a "device" is. The controls menu (and the
// saved controls file, and "iDevice >= joystickcount") treat it as an INDEX 0..joystickcount-1, while
// CPlayerInput::Update() compares it with SDL's joystick *instance id* (event.jbutton.which, e.g. 3 and 4 on
// the Wii because ids 1 and 2 belong to the keyboard and mouse). We make everything use the index: iDevice is
// the pad's position in g_pads, joystick events are rewritten to carry that index in `which`, and
// SDL_GetJoystickNameForID / SDL_GetJoystickFromPlayerIndex are wrapped to take an index too.
//
// Controller layouts come from SDL's OGC joystick driver (button/axis/hat order):
//   GameCube  : buttons A,B,X,Y,L,R,Z,Start (0-7) | axes 0/1 = main stick, 2/3 = C-stick, 4/5 = L/R | hat 0 = D-pad
//   Wiimote   : buttons A,B,1,2,-,+,Home (0-6)    | hat 0 = D-pad
//   + Classic : adds X=9, Y=10, L=11, R=12, ZL=13, ZR=14  | axes 0/1 = left stick
//   + Nunchuk : adds Z=7, C=8                             | axes 0/1 = nunchuk stick
#include <SDL3/SDL.h>

#include "GameValues.h"   // CGameValues, CInputPlayerControl, JOY_* constants (upstream src/common)

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#ifndef SMW_WII_ROOT
#define SMW_WII_ROOT "sd:/smw/"
#endif

extern CGameValues game_values;       // upstream, common/global.cpp
extern SDL_Joystick** joysticks;      // upstream, common/global.cpp (used by the controls menu)
extern int joystickcount;

extern "C" void smw_log_str(const char* s);   // src/wii_hooks.cpp
extern "C" void smw_fatal(const char* why);   // src/wii_boot.cpp (on-screen error report)

namespace {

constexpr int kMaxPads = 8;   // 4 GameCube + 4 Wiimotes

enum class Layout { GameCube, WiimoteSideways, Classic, Nunchuk };

struct Pad {
    SDL_JoystickID id = 0;
    SDL_Joystick* js = nullptr;
    int player = -1;   // 0..3, or -1 if all players were already taken
};

Pad g_pads[kMaxPads];
int g_num_pads = 0;
bool g_initial_scan_done = false;

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    smw_log_str(buf);
}

constexpr int B(int n) { return JOY_BUTTON_START + n; }

struct Keys { int game[NUM_KEYS]; int menu[NUM_KEYS]; };

// Game order: left, right, jump, down, turbo, powerup, start, cancel
// Menu order: up, down, left, right, select, cancel, random, fast-scroll
Keys keys_for(Layout l)
{
    switch (l) {
    case Layout::GameCube:
        return { { JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, JOY_STICK_1_DOWN, B(1) /*B*/, B(2) /*X*/, B(7) /*Start*/, B(3) /*Y*/ },
                 { JOY_STICK_1_UP, JOY_STICK_1_DOWN, JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, B(1) /*B*/, B(2) /*X*/, B(3) /*Y*/ } };
    case Layout::Classic:
        return { { JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, JOY_STICK_1_DOWN, B(1) /*B*/, B(9) /*X*/, B(5) /*+*/, B(4) /*-*/ },
                 { JOY_STICK_1_UP, JOY_STICK_1_DOWN, JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, B(1) /*B*/, B(9) /*X*/, B(10) /*Y*/ } };
    case Layout::Nunchuk:
        return { { JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, JOY_STICK_1_DOWN, B(1) /*B*/, B(7) /*Z*/, B(5) /*+*/, B(4) /*-*/ },
                 { JOY_STICK_1_UP, JOY_STICK_1_DOWN, JOY_STICK_1_LEFT, JOY_STICK_1_RIGHT, B(0) /*A*/, B(1) /*B*/, B(7) /*Z*/, B(8) /*C*/ } };
    case Layout::WiimoteSideways:
    default:
        // Wiimote held sideways, NES style (D-pad on the left, buttons 1 and 2 under the right thumb).
        // Turned 90 degrees, the D-pad's physical Up points screen-left, Right points up, Down points
        // screen-right and Left points down; SDL reports the *physical* direction in hat 0.
        return { { JOY_HAT_UP /*screen left*/, JOY_HAT_DOWN /*screen right*/, B(3) /*2*/, JOY_HAT_LEFT /*screen down*/, B(2) /*1*/, B(0) /*A*/, B(5) /*+*/, B(4) /*-*/ },
                 { JOY_HAT_RIGHT /*up*/, JOY_HAT_LEFT /*down*/, JOY_HAT_UP /*left*/, JOY_HAT_DOWN /*right*/, B(3) /*2*/, B(2) /*1*/, B(0) /*A*/, B(1) /*B*/ } };
    }
}

const char* layout_name(Layout l)
{
    switch (l) {
    case Layout::GameCube: return "GameCube pad";
    case Layout::Classic: return "Wiimote + Classic Controller";
    case Layout::Nunchuk: return "Wiimote + Nunchuk";
    default: return "Wiimote (sideways)";
    }
}

// Names come from SDL's OGC driver: "GameCube N", "Wiimote N", "Wiimote N + Classic", "Wiimote N + Nunchuk".
Layout classify(const char* name)
{
    if (!name) return Layout::WiimoteSideways;
    if (std::strstr(name, "GameCube")) return Layout::GameCube;
    if (std::strstr(name, "Classic")) return Layout::Classic;
    if (std::strstr(name, "Nunchuk")) return Layout::Nunchuk;
    return Layout::WiimoteSideways;
}

bool controls_file_exists()
{
    struct stat st;
    return stat(SMW_WII_ROOT "controls.sdl2.bin", &st) == 0;
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

// A player is "free" while it is still on the keyboard configuration.
int first_free_player()
{
    for (int p = 0; p < 4; ++p) {
        const CInputPlayerControl* c = game_values.playerInput.inputControls[p];
        if (!c || c->iDevice == DEVICE_KEYBOARD) {
            bool taken = false;   // (not claimed by a joystick we already placed)
            for (int i = 0; i < g_num_pads; ++i)
                if (g_pads[i].player == p) taken = true;
            if (!taken) return p;
        }
    }
    return -1;
}

void on_joystick_added(SDL_JoystickID id)
{
    if (find_pad(id) >= 0 || g_num_pads >= kMaxPads) return;

    SDL_Joystick* js = SDL_OpenJoystick(id);
    if (!js) {
        logf("[input] SDL_OpenJoystick(%u) failed: %s", (unsigned)id, SDL_GetError());
        return;
    }

    const char* name = SDL_GetJoystickName(js);
    const Layout layout = classify(name);

    const int idx = g_num_pads;         // device index as upstream sees it (see "Device ids" above)
    Pad& pad = g_pads[g_num_pads];
    pad = Pad{};                        // clear whatever a previously removed pad left in this slot
    pad.id = id;
    pad.js = js;
    pad.player = first_free_player();   // decided before this pad is counted, so it can't "take" its own slot
    ++g_num_pads;

    if (pad.player >= 0) {
        CInputPlayerControl& cfg = game_values.inputConfiguration[pad.player][1];   // [1] = joystick config
        cfg.iDevice = static_cast<short>(idx);

        if (!controls_file_exists()) {   // first run: install our defaults; otherwise keep what the player saved
            const Keys k = keys_for(layout);
            for (int i = 0; i < NUM_KEYS; ++i) {
                cfg.inputGameControls[0].keys[i] = k.game[i];
                cfg.inputGameControls[1].keys[i] = k.menu[i];
            }
        }
        game_values.playerInput.inputControls[pad.player] = &cfg;
    }

    publish_joystick_array();
    logf("[input] joystick added: id=%u '%s' (%d buttons, %d axes, %d hats) -> %s, %s", (unsigned)id,
         name ? name : "?", SDL_GetNumJoystickButtons(js), SDL_GetNumJoystickAxes(js),
         SDL_GetNumJoystickHats(js), layout_name(layout),
         pad.player >= 0 ? "player slot assigned" : "no free player slot");
    if (pad.player >= 0) logf("[input]   bound to player %d", pad.player + 1);
}

void on_joystick_removed(SDL_JoystickID id)
{
    const int i = find_pad(id);
    if (i < 0) return;

    const int player = g_pads[i].player;
    if (g_pads[i].js) SDL_CloseJoystick(g_pads[i].js);

    if (player >= 0)   // back to the keyboard so the slot can be claimed again
        game_values.playerInput.inputControls[player] = &game_values.inputConfiguration[player][0];

    for (int j = i; j + 1 < g_num_pads; ++j) g_pads[j] = g_pads[j + 1];
    --g_num_pads;
    g_pads[g_num_pads] = Pad{};
    // Pads after the removed one moved down by one: keep their players' device index in step.
    for (int j = i; j < g_num_pads; ++j)
        if (g_pads[j].player >= 0)
            game_values.inputConfiguration[g_pads[j].player][1].iDevice = static_cast<short>(j);
    publish_joystick_array();
    logf("[input] joystick removed: id=%u (was player %d)", (unsigned)id, player + 1);
}

// Pick up pads that already existed before the first event was read.
void initial_scan()
{
    if (g_initial_scan_done) return;
    g_initial_scan_done = true;

    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    logf("[input] initial scan: %d joystick(s)", n);
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

// SDL has no message-box backend on the Wii. Upstream shows its "unexpectedly crashed" box, including the error
// message, from main()'s catch handlers; put that text on the crash screen (src/wii_boot.cpp) and in the log instead.
// Linked in with -Wl,--wrap=SDL_ShowSimpleMessageBox.
extern "C" bool __wrap_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags /*flags*/, const char* title, const char* message,
                                                SDL_Window* /*window*/)
{
    const char* p = message ? std::strstr(message, "The error message:") : nullptr;
    if (!p) p = message ? message : title;
    logf("[error] %s: %.300s", title ? title : "", p ? p : "");
    smw_fatal(p ? p : "unknown error");
    return true;
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
