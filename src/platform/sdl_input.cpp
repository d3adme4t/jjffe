#include "jjffe/input.h"
#include "jjffe/platform.h"
#include "jjffe/sound.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace jjffe {

// SDL input state
static uint8_t g_sdlKeyStates[512] = {0};
static int g_mouseX = 0;
static int g_mouseY = 0;
static int g_mouseButtons = 0;
static int g_mouseRelX = 0;
static int g_mouseRelY = 0;
static int g_sensitivity = 100;
static int g_repeatRate = 30;
static int g_repeatDelay = 300;
static uint32_t g_keyTimes[256] = {0};
static uint8_t g_keyStates[256] = {0};
static int g_lastKey = 0xFF;
static bool g_inputInitialized = false;
static bool g_quitRequested = false;
static uint32_t g_initTime = 0;

// Key conversion table: DOS scancode → SDL 1.2 keysym.
// Extended (E0) keys use high-bit scancodes: UP=0xC8, DOWN=0xD0, LEFT=0xCB, RIGHT=0xCD.
static const int pKeybConvTable[256] = {
    /* 0x00 */ 0, SDLK_ESCAPE, SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6,
    /* 0x08 */ SDLK_7, SDLK_8, SDLK_9, SDLK_0, SDLK_MINUS, SDLK_EQUALS, SDLK_BACKSPACE, SDLK_TAB,
    /* 0x10 */ SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i,
    /* 0x18 */ SDLK_o, SDLK_p, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_RETURN, SDLK_LCTRL, SDLK_a, SDLK_s,
    /* 0x20 */ SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k, SDLK_l, SDLK_SEMICOLON,
    /* 0x28 */ SDLK_QUOTE, SDLK_HASH, SDLK_LSHIFT, SDLK_BACKSLASH, SDLK_z, SDLK_x, SDLK_c, SDLK_v,
    /* 0x30 */ SDLK_b, SDLK_n, SDLK_m, SDLK_COMMA, SDLK_PERIOD, SDLK_SLASH, SDLK_RSHIFT, SDLK_KP_MULTIPLY,
    /* 0x38 */ SDLK_LALT, SDLK_SPACE, SDLK_CAPSLOCK, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5,
    /* 0x40 */ SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_NUMLOCK, SDLK_SCROLLOCK, SDLK_KP7,
    /* 0x48 */ SDLK_KP8, SDLK_KP9, SDLK_KP_MINUS, SDLK_KP4, SDLK_KP5, SDLK_KP6, SDLK_KP_PLUS, SDLK_KP1,
    /* 0x50 */ SDLK_KP2, SDLK_KP3, SDLK_KP0, SDLK_KP_PERIOD, 0, 0, 0, SDLK_F11,
    /* 0x58 */ SDLK_F12, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x68 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x78 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x80 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x88 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x90 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x98 */ 0, 0, 0, 0, SDLK_KP_ENTER, SDLK_RCTRL, 0, 0,
    /* 0xA0 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xA8 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xB0 */ 0, 0, 0, 0, 0, SDLK_KP_DIVIDE, 0, SDLK_SYSREQ,
    /* 0xB8 */ SDLK_RALT, 0, 0, 0, 0, 0, 0, 0,
    /* 0xC0 */ 0, 0, 0, 0, 0, 0, 0, SDLK_HOME,
    /* 0xC8 */ SDLK_UP, SDLK_PAGEUP, 0, SDLK_LEFT, 0, SDLK_RIGHT, 0, SDLK_END,
    /* 0xD0 */ SDLK_DOWN, SDLK_PAGEDOWN, SDLK_INSERT, SDLK_DELETE, 0, 0, 0, 0,
    /* 0xD8 */ 0, 0, 0, SDLK_LSUPER, SDLK_RSUPER, SDLK_MENU, 0, 0,
    /* 0xE0 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xE8 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xF0 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0xF8 */ 0, 0, 0, 0, 0, 0, 0, 0,
};

// Process SDL events - called by PlatformProcessEvents
void InputProcessEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym < 512) {
                    g_sdlKeyStates[event.key.keysym.sym] = 1;
                }
                break;
            case SDL_KEYUP:
                if (event.key.keysym.sym < 512) {
                    g_sdlKeyStates[event.key.keysym.sym] = 0;
                }
                break;
            case SDL_MOUSEMOTION:
                // Accumulate — multiple motion events per frame are common
                // while dragging (demo orbit, menus).
                g_mouseRelX += event.motion.xrel;
                g_mouseRelY += event.motion.yrel;
                g_mouseX = event.motion.x;
                g_mouseY = event.motion.y;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                g_mouseButtons = SDL_GetMouseState(nullptr, nullptr);
                break;
            case SDL_ACTIVEEVENT:
                // Phase 56: Handle window focus for music pause/resume
                if (event.active.state & SDL_APPACTIVE) {
                    SoundHandleFocusChange(event.active.gain != 0);
                }
                break;
            case SDL_QUIT:
                // Ignore early quit events (SDL 1.2 / Wayland compatibility)
                if (g_initTime == 0) {
                    g_initTime = SDL_GetTicks();
                }
                if (SDL_GetTicks() - g_initTime > 3000) {
                    g_quitRequested = true;
                }
                break;
        }
    }
}

// Process SDL events (legacy, kept for tests)
static void ProcessSDLEvents()
{
    InputProcessEvents();
}

// Platform input initialization
void PlatformInputInit()
{
    if (g_inputInitialized) return;
    
    memset(g_sdlKeyStates, 0, sizeof(g_sdlKeyStates));
    memset(g_keyStates, 0x80, sizeof(g_keyStates));
    memset(g_keyTimes, 0, sizeof(g_keyTimes));
    g_lastKey = 0xFF;
    g_mouseX = 160;
    g_mouseY = 100;
    g_mouseButtons = 0;
    g_mouseRelX = 0;
    g_mouseRelY = 0;
    g_quitRequested = false;
    g_initTime = SDL_GetTicks();
    
    g_inputInitialized = true;
}

void PlatformInputCleanup()
{
    g_inputInitialized = false;
}

// R1245: JJFFE_LIVE_HOLD="<first>-<end>:<scancode>[,...]" — the live twin of
// REPLAY_HOLD. While loop frame+1 is in [first, end), InputKeybReadStates
// reports that scancode as down (0x00). Needed for fireab Space (0x39) holds;
// without it the R1243 recipe's JJFFE_LIVE_HOLD was a silent no-op. Inert unset.
uint32_t GameLoopGetFrameCount();  // game_loop.cpp
namespace {
constexpr int kMaxLiveHolds = 32;
struct LiveHold { int first, end, scancode; };
LiveHold s_liveHolds[kMaxLiveHolds];
int s_liveHoldCount = -1;

void ParseLiveHolds()
{
    s_liveHoldCount = 0;
    const char* e = getenv("JJFFE_LIVE_HOLD");
    if (!e || !e[0]) return;
    while (*e && s_liveHoldCount < kMaxLiveHolds) {
        int first = 0, end = 0, code = 0, n = 0;
        if (std::sscanf(e, "%d-%d:%i%n", &first, &end, &code, &n) != 3) {
            // R1245 closeout (checker MINOR): never silently ignore — a typo'd
            // hold must look like one (ParseLiveKeys precedent).
            std::fprintf(stderr, "JJFFE_LIVE_HOLD: malformed entry '%s' ignored\n", e);
            break;
        }
        s_liveHolds[s_liveHoldCount++] = {first, end, code & 0xff};
        e += n;
        if (*e == ',') ++e;
        else break;
    }
    if (*e && s_liveHoldCount >= kMaxLiveHolds)
        std::fprintf(stderr, "JJFFE_LIVE_HOLD: truncated at %d entries\n", kMaxLiveHolds);
}

// Mark currently-scheduled LIVE_HOLD scancodes in a 256-byte scratch (1 = down).
void LiveHoldsThisFrame(uint8_t down[256])
{
    std::memset(down, 0, 256);
    if (s_liveHoldCount < 0) ParseLiveHolds();
    if (s_liveHoldCount <= 0) return;
    // +1 like LIVE_KEYS/CLICKS/JOY (R1175): scenario frame F is polled at loop F-1.
    const int frame = static_cast<int>(GameLoopGetFrameCount()) + 1;
    for (int i = 0; i < s_liveHoldCount; ++i) {
        if (s_liveHolds[i].first <= frame && frame < s_liveHolds[i].end)
            down[s_liveHolds[i].scancode & 0xff] = 1;
    }
}
}  // namespace

// Read keyboard states
void InputKeybReadStates(uint8_t* pKeys)
{
    if (!g_inputInitialized) return;
    
    ProcessSDLEvents();
    
    uint32_t curtime = SDL_GetTicks();
    uint8_t liveDown[256];
    LiveHoldsThisFrame(liveDown);
    
    for (int i = 0; i < 256; i++) {
        int sdlKey = pKeybConvTable[i];
        const bool heldInject = liveDown[i] != 0;
        if ((sdlKey > 0 && g_sdlKeyStates[sdlKey] == 1) || heldInject) {
            if (g_keyStates[i] == 0x80) {
                // Initial press (physical or LIVE_HOLD edge into the range)
                g_lastKey = i;
                g_keyStates[i] = 0;
                g_keyTimes[i] = curtime + g_repeatDelay;
            } else if (g_keyStates[i] == 0) {
                // Hold - check for repeat
                if (g_keyTimes[i] < curtime) {
                    g_keyStates[i] = 0xFF;
                }
            }
            if (g_keyStates[i] == 0xFF) {
                // Repeat
                if (g_keyTimes[i] < curtime) {
                    g_lastKey = i;
                    g_keyTimes[i] += g_repeatRate;
                }
            }
        } else {
            g_keyStates[i] = 0x80;
        }
    }
    
    for (int i = 0; i < 256; i++) {
        pKeys[i] = (g_keyStates[i] == 0x80) ? 0x80 : 0x00;
    }
}

// Get last key pressed
// Task 4.209: headless key injection for the LIVE capture path, mirroring JJFFE_INTRO_KEYS's
// "<poll>:<scancode>" format but counted in live game-loop frames (g_loop.frameCount at the flip).
// Format: JJFFE_LIVE_KEYS="<frame>:<scancode>[,...]". A fired key is returned once and consumed,
// exactly like a physical press arriving that frame; without the env var this is inert.
//
// R1190: the schedule used to be capped at 16 entries, and ParseLiveKeys stopped silently at the
// cap. A scenario with more keys then lost the TAIL of its script: the flyf3 replay has 18 keys and
// its last two - 600:0x3d (the F3 press) and 650:0x3b (Esc) - were never even parsed, so F3 was
// never delivered and the map never opened. That looked exactly like a game-logic bug ("lost F3")
// and was reproduced as a 53,440-pixel frame divergence. flyb has exactly 16 keys, which is why it
// worked. Raise the cap and WARN instead of truncating silently, so a long script fails loudly.
namespace {
constexpr int kMaxLiveKeys = 256;
struct LiveKey { int frame; int scancode; };
LiveKey s_liveKeys[kMaxLiveKeys];
int s_liveKeyCount = 0;
int s_liveKeysParsed = -1;   // parse lazily, once
uint32_t s_livePollFrame = 0xffffffffu;   // no frame yet: a scenario frame 1 key fires in loop frame 0

void ParseLiveKeys()
{
    s_liveKeysParsed = 0;
    const char* e = getenv("JJFFE_LIVE_KEYS");
    if (!e || !e[0]) return;
    const char* p = e;
    while (*p && s_liveKeyCount < kMaxLiveKeys) {
        char* end = nullptr;
        const long fr = strtol(p, &end, 0);
        if (end == p || *end != ':') break;
        p = end + 1;
        const long code = strtol(p, &end, 0);
        if (end == p) break;
        s_liveKeys[s_liveKeyCount++] = {static_cast<int>(fr), static_cast<int>(code)};
        p = (*end == ',') ? end + 1 : end;
        if (p == end) break;
    }
    // Never truncate silently: a dropped key is invisible in the frame and looks like a game bug.
    if (*p && s_liveKeyCount >= kMaxLiveKeys) {
        std::fprintf(stderr,
                     "JJFFE_LIVE_KEYS: more than %d keys; the rest of the schedule is IGNORED\n",
                     kMaxLiveKeys);
    }
    s_liveKeysParsed = s_liveKeyCount;
}
}  // namespace

int InputKeybGetLastKey()
{
    extern uint32_t GameLoopGetFrameCount();  // game_loop.cpp:81 (the live loop's own counter)
    if (s_liveKeysParsed < 0) ParseLiveKeys();
    const uint32_t frame = GameLoopGetFrameCount();
    if (frame != s_livePollFrame) {          // advance the injection schedule once per frame
        s_livePollFrame = frame;
        for (int i = 0; i < s_liveKeyCount; ++i) {
            // +1 (R1175), as for clicks: ffeasm reads a scenario frame n key at e=n+1, and the loop's frame
            // n - 1 carries that stamp. Every scripted key takes it, whatever consumes the key.
            if (s_liveKeys[i].frame == static_cast<int>(frame) + 1) {
                g_lastKey = s_liveKeys[i].scancode;   // physical-press semantics: set, then return
                s_liveKeys[i].frame = -1;             // fire exactly once
                break;
            }
        }
    }
    return g_lastKey;
}

// Set last key
void InputKeybSetLastKey(int key)
{
    g_lastKey = key;
}

// R1159: headless click injection for the LIVE capture path, the mouse half of JJFFE_LIVE_KEYS and
// the port side of tools/asm_replay_scenario.py's "clicks": JJFFE_LIVE_CLICKS="<frame>:<x>:<y>[,...]"
// (up to 16, 320x200 coordinates). During a listed frame InputMouseReadPos gives (x, y) and
// InputMouseReadButtons 1; every other frame (0, 0) and 0, as the ASM harness does. Inert unset.
uint32_t GameLoopGetFrameCount();   // game_loop.cpp (the live loop's own counter)
namespace {
struct LiveClick { int frame, x, y; };
LiveClick s_liveClicks[16];
int s_liveClickCount = -1;   // parse lazily, once
bool s_liveClicksSet = false;

const LiveClick* LiveClickNow(bool* active)
{
    if (s_liveClickCount < 0) {
        s_liveClickCount = 0;
        const char* e = getenv("JJFFE_LIVE_CLICKS");
        s_liveClicksSet = e && e[0];
        int f = 0, x = 0, y = 0, n = 0;
        while (e && e[0] && s_liveClickCount < 16 && std::sscanf(e, "%i:%i:%i%n", &f, &x, &y, &n) == 3) {
            s_liveClicks[s_liveClickCount++] = {f, x, y};
            e += n;
            if (*e != ',') break;
            ++e;
        }
    }
    *active = s_liveClicksSet;
    // +1 (R1169): a click is read at the flip, and the loop's frame n flips at the DATA_008811 of
    // scenario frame n + 1 (the same numbering JJFFE_POKE_PLAYER allows for). JJFFE_LIVE_KEYS takes the
    // same +1 (R1175).
    const int frame = static_cast<int>(GameLoopGetFrameCount()) + 1;
    for (int i = 0; i < s_liveClickCount; ++i)
        if (s_liveClicks[i].frame == frame) return &s_liveClicks[i];
    return nullptr;
}
}  // namespace

// Read mouse buttons
int InputMouseReadButtons()
{
    bool injected = false;
    const LiveClick* c = LiveClickNow(&injected);
    if (injected) return c ? 1 : 0;
    if (!g_inputInitialized) return 0;
    
    int buttons = SDL_GetMouseState(nullptr, nullptr);
    return (buttons & 1) | ((buttons >> 1) & 2);
}

// Read mouse relative movement (mickeys)
void InputMouseReadMickeys(int* pXMick, int* pYMick)
{
    if (!g_inputInitialized || !pXMick || !pYMick) return;
    
    *pXMick = (g_mouseRelX * g_sensitivity) / 100;
    *pYMick = (g_mouseRelY * g_sensitivity) / 100;
    
    g_mouseRelX = 0;
    g_mouseRelY = 0;
}

// Read mouse position
void InputMouseReadPos(int* pXPos, int* pYPos)
{
    bool injected = false;
    const LiveClick* c = LiveClickNow(&injected);
    if (injected && pXPos && pYPos) {
        *pXPos = c ? c->x : 0;
        *pYPos = c ? c->y : 0;
        return;
    }
    if (!g_inputInitialized || !pXPos || !pYPos) return;
    
    SDL_GetMouseState(pXPos, pYPos);
    
    // Scale to 320x200 (game area only; strip Y>=400 maps past 200 and clamps).
    *pXPos = (*pXPos * 320) / 640;
    *pYPos = (*pYPos * 200) / 400;
}

void PlatformGetWindowMousePos(int* x, int* y)
{
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    if (x) *x = mx;
    if (y) *y = my;
}

// Read joystick position
void InputJoyReadPos(int* xpos, int* ypos)
{
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

// R1178: headless joystick-button injection for the LIVE capture path, the gamepad half of
// JJFFE_LIVE_KEYS. Format "JJFFE_LIVE_JOY=<fromFrame>-<toFrame>:<bitmask>" (single range): the given
// SDL button bits read as held on every poll from loop frame `fromFrame` through `toFrame`, mirroring a
// physical button being pressed and held. Button bit N is (1<<N), so bit 1 (the fire trigger ffeasm's
// gate tests via DATA_009163) is mask 2. Inert without the env var.
int InputJoyReadButtons()
{
    // R1178: parse once into file-scope state (the earlier version cached only a flag and refilled the
    // caller's locals just on the first call, so every later poll saw an empty range).
    static int from = -1, to = -1, mask = 0, parsed = -1;
    if (parsed < 0) {
        parsed = 0;
        const char* e = getenv("JJFFE_LIVE_JOY");
        if (e && e[0]) {
            int f = 0, t = 0, m = 0;
            if (std::sscanf(e, "%d-%d:%i", &f, &t, &m) == 3) { from = f; to = t; mask = m; parsed = 1; }
        }
    }
    if (!parsed) return 0;
    const uint32_t frame = GameLoopGetFrameCount();
    // +1 like keys/clicks (R1175/R1177): the scenario's frame F is polled at loop frame F-1.
    return (static_cast<int>(frame) + 1 >= from && static_cast<int>(frame) + 1 <= to) ? mask : 0;
}

// Check if quit was requested
bool InputIsQuitRequested()
{
    return g_quitRequested;
}

} // namespace jjffe
