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

extern "C" void smw_log_str(const char* s);   // src/wii_paths.cpp
extern "C" void smw_log_mem(const char* tag);  // src/wii_paths.cpp

namespace {
	
void dump_sdl_state(const char* tag);
void dump_bindings(const char* tag);

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
    dump_sdl_state("added");
    dump_bindings("added");
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


// ---- bring-up diagnostics (remove once input works) ---------------------------------------------------
unsigned g_ev_counts[4];       // joystick button/hat/axis/other seen by the wrapper
unsigned g_polls = 0;
int g_axis_logged = 0;

void dump_bindings(const char* tag)
{
    for (int p = 0; p < 4; ++p) {
        const CInputPlayerControl* c = game_values.playerInput.inputControls[p];
        if (!c) { logf("[diag:%s] player %d: inputControls = NULL", tag, p + 1); continue; }
        logf("[diag:%s] player %d: iDevice=%d menu keys=%d,%d,%d,%d,%d,%d,%d,%d", tag, p + 1, (int)c->iDevice,
             c->inputGameControls[1].keys[0], c->inputGameControls[1].keys[1], c->inputGameControls[1].keys[2],
             c->inputGameControls[1].keys[3], c->inputGameControls[1].keys[4], c->inputGameControls[1].keys[5],
             c->inputGameControls[1].keys[6], c->inputGameControls[1].keys[7]);
    }
}

void dump_sdl_state(const char* tag)
{
    int nwin = 0;
    SDL_Window** wins = SDL_GetWindows(&nwin);
    SDL_free(wins);
    logf("[diag:%s] windows=%d keyboardFocus=%s joystickEventsEnabled=%d BTN_DOWN enabled=%d HAT enabled=%d "
         "AXIS enabled=%d joystickcount=%d", tag, nwin, SDL_GetKeyboardFocus() ? "YES" : "NO (SDL drops button-down!)",
         (int)SDL_JoystickEventsEnabled(), (int)SDL_EventEnabled(SDL_EVENT_JOYSTICK_BUTTON_DOWN),
         (int)SDL_EventEnabled(SDL_EVENT_JOYSTICK_HAT_MOTION), (int)SDL_EventEnabled(SDL_EVENT_JOYSTICK_AXIS_MOTION),
         joystickcount);
}

// Logs what upstream's CPlayerInput::Update(event, 1) will do with this event.
void diag_event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_QUIT) {
        logf("[diag] SDL_EVENT_QUIT received (power/reset request or window close) -> game will shut down");
        return;
    }
    {   // the first few non-joystick events, to see what else arrives around a key press
        static int other = 0;
        const bool joy = e.type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && e.type <= SDL_EVENT_JOYSTICK_UPDATE_COMPLETE;
        if (!joy && other < 60) { ++other; logf("[diag] event type=0x%x", (unsigned)e.type); }
    }
    const bool btn = e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e.type == SDL_EVENT_JOYSTICK_BUTTON_UP;
    const bool hat = e.type == SDL_EVENT_JOYSTICK_HAT_MOTION;
    const bool axis = e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION;
    if (e.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && e.type <= SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED) {
        static int n = 0;
        if (n++ < 20) logf("[diag] GAMEPAD event type=0x%x (a gamepad is open somewhere)", (unsigned)e.type);
        return;
    }
    if (!btn && !hat && !axis) return;

    if (axis) {
        if (std::abs(e.jaxis.value) < 16384 || g_axis_logged >= 40) return;   // only deadzone-crossing, capped
        ++g_axis_logged;
    }
    ++g_ev_counts[btn ? 0 : hat ? 1 : 2];

    const SDL_JoystickID which = btn ? e.jbutton.which : hat ? e.jhat.which : e.jaxis.which;
    int matches = 0;
    int matched_players[4] = {0, 0, 0, 0};
    for (int p = 0; p < 4; ++p) {
        const CInputPlayerControl* c = game_values.playerInput.inputControls[p];
        if (!c || (SDL_JoystickID)c->iDevice != which) continue;
        for (int k = 0; k < NUM_KEYS; ++k) {
            const int key = c->inputGameControls[1].keys[k];
            bool m = false;
            if (btn) m = key == e.jbutton.button + JOY_BUTTON_START;
            else if (hat) m = (key == JOY_HAT_UP && (e.jhat.value & SDL_HAT_UP)) || (key == JOY_HAT_DOWN && (e.jhat.value & SDL_HAT_DOWN)) ||
                              (key == JOY_HAT_LEFT && (e.jhat.value & SDL_HAT_LEFT)) || (key == JOY_HAT_RIGHT && (e.jhat.value & SDL_HAT_RIGHT));
            else m = e.jaxis.axis < 2;
            if (m) { ++matches; matched_players[p] = 1; }
        }
    }
    if (btn)
        logf("[diag] BUTTON %s which=%u button=%d -> %d menu-key match(es) (players %d%d%d%d)",
             e.jbutton.down ? "DOWN" : "UP", (unsigned)which, (int)e.jbutton.button, matches,
             matched_players[0], matched_players[1], matched_players[2], matched_players[3]);
    else if (hat)
        logf("[diag] HAT which=%u value=0x%x -> %d match(es)", (unsigned)which, (unsigned)e.jhat.value, matches);
    else
        logf("[diag] AXIS which=%u axis=%d value=%d", (unsigned)which, (int)e.jaxis.axis, (int)e.jaxis.value);
}

// Every ~10 s of polling: are events arriving at all, and does SDL's own joystick state change?
void diag_heartbeat()
{
    ++g_polls;
    static Uint64 last_ms = 0;
    const Uint64 now = SDL_GetTicks();
    if (g_polls <= 3) logf("[diag] poll #%u at t=%llu ms", g_polls, (unsigned long long)now);   // first few calls
    if (now - last_ms < 2000) return;   // then every 2 s of wall time, however slow the frame rate
    last_ms = now;
    logf("[diag] heartbeat t=%llu ms: polls=%u events seen: button=%u hat=%u axis(>deadzone)=%u pads=%d",
         (unsigned long long)now, g_polls,
         g_ev_counts[0], g_ev_counts[1], g_ev_counts[2], g_num_pads);
    dump_sdl_state("hb");
    for (int i = 0; i < g_num_pads; ++i) {
        if (!g_pads[i].js) continue;
        char b[64]; int n = 0;
        const int nb = SDL_GetNumJoystickButtons(g_pads[i].js);
        for (int k = 0; k < nb && n < 60; ++k) b[n++] = SDL_GetJoystickButton(g_pads[i].js, k) ? '1' : '0';
        b[n] = 0;
        logf("[diag]   pad id=%u polled button state: %s hat0=0x%x", (unsigned)g_pads[i].id, b,
             (unsigned)SDL_GetJoystickHat(g_pads[i].js, 0));
    }
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
    diag_event(e);
    if (e.type == SDL_EVENT_JOYSTICK_ADDED) on_joystick_added(e.jdevice.which);
    else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) on_joystick_removed(e.jdevice.which);
}

} // namespace

extern "C" bool __real_SDL_PollEvent(SDL_Event* event);
extern "C" bool __real_SDL_WaitEvent(SDL_Event* event);

extern "C" bool __wrap_SDL_PollEvent(SDL_Event* event)
{
    initial_scan();
    diag_heartbeat();
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

// Logs the text of every message box (upstream shows its "unexpectedly crashed" box, including the error
// message, from main()'s catch handlers), then lets SDL handle it as before.
extern "C" bool __real_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message,
                                                SDL_Window* window);
extern "C" bool __wrap_SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char* title, const char* message,
                                                SDL_Window* window)
{
    smw_log_mem("at error box");
    logf("[diag] SDL_ShowSimpleMessageBox title='%s'", title ? title : "");
    const char* p = message ? std::strstr(message, "The error message:") : nullptr;
    if (!p) p = message;
    while (p && *p) {
        char chunk[201];
        size_t n = std::strlen(p);
        if (n > 200) n = 200;
        std::memcpy(chunk, p, n);
        chunk[n] = 0;
        logf("[diag]   %s", chunk);
        p += n;
    }
    return __real_SDL_ShowSimpleMessageBox(flags, title, message, window);
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
