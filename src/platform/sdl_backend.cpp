#include "jjffe/platform.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/draw.h"
#include <SDL.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace jjffe {

// SDL 1.2 state
SDL_Surface* g_screen = nullptr;
SDL_Surface* g_backSurface = nullptr;
bool g_sdlInitialized = false;
static uint32_t g_palette[256];
static int g_controlStripHeight = 0;
static PlatformStripPainterFn g_stripPainter = nullptr;

void PlatformSetControlStripHeight(int height)
{
    if (height < 0) height = 0;
    if (height == g_controlStripHeight && g_sdlInitialized) return;
    g_controlStripHeight = height;
    if (!g_sdlInitialized) return;
    const int winH = 400 + g_controlStripHeight;
    g_screen = SDL_SetVideoMode(640, winH, 32,
                                SDL_SWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE);
    if (!g_screen) {
        fprintf(stderr,
                "PlatformSetControlStripHeight: SDL_SetVideoMode(640,%d) failed: %s\n",
                winH, SDL_GetError());
    }
}

int PlatformGetControlStripHeight()
{
    return g_controlStripHeight;
}

void PlatformSetControlStripPainter(PlatformStripPainterFn fn)
{
    g_stripPainter = fn;
}

// SIGTERM/SIGINT handling (definition below, installed right after SDL_Init).
static volatile sig_atomic_t g_terminateRequested;
static void PlatformTerminateHandler(int sig);

bool PlatformIsGpuDraw()
{
    return false;
}

bool PlatformIsD3D()
{
    return PlatformIsGpuDraw();
}

uint32_t PlatformGetTicks()
{
    return SDL_GetTicks();
}

void PlatformDelay(uint32_t ms)
{
    SDL_Delay(ms);
}

// Platform video initialization (SDL 1.2)
void PlatformVideoInit()
{
    if (g_sdlInitialized) return;
    
    // Center window
    setenv("SDL_VIDEO_CENTERED", "1", 0);
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    // Take SIGTERM/SIGINT back from SDL (see PlatformTerminateHandler).
    signal(SIGTERM, PlatformTerminateHandler);
    signal(SIGINT, PlatformTerminateHandler);
    
    // Create 640x400 window (2x scale of 320x200); +strip when INTRO_PLAYER.
    const int winH = 400 + g_controlStripHeight;
    g_screen = SDL_SetVideoMode(640, winH, 32,
                                SDL_SWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE);
    if (!g_screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }
    
    // Set window title
    SDL_WM_SetCaption("JJFFE v2.8 - C++ Port", "JJFFE");
    
    // Fill screen with blue to confirm window is visible
    SDL_FillRect(g_screen, nullptr, SDL_MapRGB(g_screen->format, 0, 0, 128));
    SDL_Flip(g_screen);
    
    // Create 320x200 back buffer surface for indexed rendering
    g_backSurface = SDL_CreateRGBSurface(
        SDL_SWSURFACE,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        8,  // 8-bit indexed
        0, 0, 0, 0
    );
    
    if (!g_backSurface) {
        fprintf(stderr, "SDL_CreateRGBSurface failed: %s\n", SDL_GetError());
        SDL_FreeSurface(g_screen);
        SDL_Quit();
        return;
    }
    
    // Initialize palette to black
    memset(g_palette, 0, sizeof(g_palette));
    SDL_Color colors[256];
    for (int i = 0; i < 256; i++) {
        colors[i].r = colors[i].g = colors[i].b = 0;
    }
    SDL_SetColors(g_backSurface, colors, 0, 256);
    
    g_sdlInitialized = true;
}

void PlatformVideoCleanup()
{
    if (g_backSurface) {
        SDL_FreeSurface(g_backSurface);
        g_backSurface = nullptr;
    }
    if (g_screen) {
        SDL_FreeSurface(g_screen);
        g_screen = nullptr;
    }
    if (g_sdlInitialized) {
        SDL_Quit();
        g_sdlInitialized = false;
    }
}

// Push the full display palette (g_palette) onto the SDL 8-bit surface.
// Live blit used to read SDL's copy exclusively; one-entry SDL_SetColors can
// drift from g_palette (A/B dumps use g_palette → dark fill 187, live window
// showed slot-180 orange for the same indices). g_palette is the source of truth.
static void SyncSdlPaletteFromG()
{
    if (!g_backSurface || !g_backSurface->format || !g_backSurface->format->palette)
        return;
    SDL_Color colors[256];
    for (int i = 0; i < 256; i++) {
        const uint32_t c = g_palette[i];
        colors[i].r = (uint8_t)(c >> 16);
        colors[i].g = (uint8_t)(c >> 8);
        colors[i].b = (uint8_t)c;
        colors[i].unused = 0;
    }
    SDL_SetColors(g_backSurface, colors, 0, 256);
}

// Set palette entry (SDL 1.2)
void PlatformSetPaletteEntry(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < 0 || index >= 256) return;

    g_palette[index] = (r << 16) | (g << 8) | b;

    if (!g_backSurface) return;
    SDL_Color color;
    color.r = r;  // Already 8-bit
    color.g = g;
    color.b = b;
    color.unused = 0;
    SDL_SetColors(g_backSurface, &color, index, 1);
}

bool PlatformGetPaletteEntry(int index, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (index < 0 || index >= 256 || !r || !g || !b) return false;
    const uint32_t c = g_palette[index];
    *r = static_cast<uint8_t>(c >> 16);
    *g = static_cast<uint8_t>(c >> 8);
    *b = static_cast<uint8_t>(c);
    return true;
}

// ── Termination signals ────────────────────────────────────────────────
// SDL 1.2 installs its own SIGINT/SIGTERM handlers (unless SDL_INIT_NOPARACHUTE
// is passed) that call SDL_Quit from inside the signal context — not
// async-signal-safe, and observed to leave the process spinning at 100% CPU
// forever in later intro phases instead of exiting. Install our own handler
// *after* SDL_Init so it wins: the first signal asks the main loop to quit
// (PlatformProcessEvents returns false), a second one exits hard.
static void PlatformTerminateHandler(int sig)
{
    if (g_terminateRequested)
        _exit(128 + sig); // second signal: hard exit, async-signal-safe
    g_terminateRequested = 1;
}

// A/B diagnostics: dump the live display palette in the ASM's pPalette[256]
// SDL_Color layout ({r,g,b,pad}) so a captured back buffer can be resolved to
// RGB. g_videoState->palette only holds the static 128 entries, so the dynamic
// bank (>= 0x80, allocated per frame by DPalBuild) is only visible here.
void PlatformDumpPalette(const char* path)
{
    if (!path || !path[0]) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < 256; i++) {
        const uint32_t c = g_palette[i];
        const uint8_t rgba[4] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8),
                                 (uint8_t)c, 0};
        fwrite(rgba, 1, 4, f);
    }
    fclose(f);
}

// Platform video blit (called from FlipScreen)
void PlatformVideoBlit()
{
    if (!g_sdlInitialized || !g_screen || !g_backSurface) return;
    if (!g_videoState) return;

    // Optional: audit BEFORE Sync — catches one-entry SDL_SetColors drift vs
    // g_palette. JJFFE_PAL_AUDIT=1 log once; =N also every N flips.
    if (const char* audit = getenv("JJFFE_PAL_AUDIT")) {
        if (audit[0] && audit[0] != '0') {
            static int s_flip = 0;
            static int s_logged = 0;
            ++s_flip;
            const uint8_t fill = (uint8_t)(GetRenderFillColor() & 0xffu);
            const uint32_t gc = g_palette[fill];
            SDL_Color* sdlPal = g_backSurface->format->palette
                                    ? g_backSurface->format->palette->colors
                                    : nullptr;
            const uint8_t sr = sdlPal ? sdlPal[fill].r : 0;
            const uint8_t sg = sdlPal ? sdlPal[fill].g : 0;
            const uint8_t sb = sdlPal ? sdlPal[fill].b : 0;
            const uint8_t gr = (uint8_t)(gc >> 16);
            const uint8_t gg = (uint8_t)(gc >> 8);
            const uint8_t gb = (uint8_t)gc;
            int hist[256] = {};
            const uint8_t* bb = g_videoState->backBuffer;
            for (int i = 0; i < SCREEN_SIZE; i += 16)
                hist[bb[i]]++;
            int dom = 0;
            for (int i = 1; i < 256; i++)
                if (hist[i] > hist[dom])
                    dom = i;
            const bool mismatch = !sdlPal || sr != gr || sg != gg || sb != gb;
            const uint32_t dc = g_palette[dom];
            const bool orangeDom =
                ((dc >> 16) == 166 && ((dc >> 8) & 0xff) == 85 && (dc & 0xff) == 52);
            const bool interesting = mismatch || orangeDom ||
                                     (s_logged < 3 && (fill >= 128 || dom >= 128));
            int every = 1;
            {
                char* end = nullptr;
                long v = strtol(audit, &end, 0);
                if (end != audit && v > 0)
                    every = (int)v;
            }
            if (interesting &&
                (s_logged < 8 || (every > 1 && (s_flip % every) == 0))) {
                fprintf(stderr,
                        "PAL-AUDIT flip=%d fill=%u g=(%u,%u,%u) sdl=(%u,%u,%u)%s "
                        "dom=%u dRGB=(%u,%u,%u)\n",
                        s_flip, (unsigned)fill, gr, gg, gb, sr, sg, sb,
                        mismatch ? " MISMATCH" : "", (unsigned)dom,
                        (unsigned)(dc >> 16), (unsigned)((dc >> 8) & 0xff),
                        (unsigned)(dc & 0xff));
                ++s_logged;
            }
        }
    }

    // Keep SDL's logical palette aligned with g_palette (dump source of truth).
    SyncSdlPaletteFromG();


    // Copy C++ back buffer to SDL surface
    SDL_LockSurface(g_backSurface);
    uint8_t* dst = (uint8_t*)g_backSurface->pixels;
    const uint8_t* src = g_videoState->backBuffer;

    for (int row = 0; row < SCREEN_HEIGHT; row++) {
        memcpy(dst + row * g_backSurface->pitch, src + row * SCREEN_WIDTH, SCREEN_WIDTH);
    }

    SDL_UnlockSurface(g_backSurface);

    // Lock screen surface for direct pixel access
    SDL_LockSurface(g_screen);

    uint32_t* screenPixels = (uint32_t*)g_screen->pixels;
    int screenPitch = g_screen->pitch / 4;

    // Convert 8-bit indexed → 32-bit RGB via g_palette (same source as
    // PlatformDumpPalette / A/B .pal), not a possibly-stale SDL copy.
    for (int sy = 0; sy < SCREEN_HEIGHT; sy++) {
        const uint8_t* srcRow =
            (const uint8_t*)g_backSurface->pixels + sy * g_backSurface->pitch;

        for (int dy = 0; dy < 2; dy++) {
            uint32_t* dstRow = screenPixels + (sy * 2 + dy) * screenPitch;
            for (int sx = 0; sx < SCREEN_WIDTH; sx++) {
                const uint8_t idx = srcRow[sx];
                const uint32_t c = g_palette[idx];
                const uint32_t rgb = SDL_MapRGB(g_screen->format,
                                               (uint8_t)(c >> 16),
                                               (uint8_t)(c >> 8),
                                               (uint8_t)c);
                dstRow[sx * 2 + 0] = rgb;
                dstRow[sx * 2 + 1] = rgb;
            }
        }
    }

    // Control strip below the 640x400 game blit (INTRO_PLAYER transport).
    if (g_controlStripHeight > 0 && g_screen->h > 400) {
        const uint32_t dark = SDL_MapRGB(g_screen->format, 18, 18, 22);
        const int stripH = g_controlStripHeight;
        for (int y = 400; y < 400 + stripH && y < g_screen->h; y++) {
            uint32_t* row = screenPixels + y * screenPitch;
            for (int x = 0; x < 640 && x < g_screen->w; x++)
                row[x] = dark;
        }
        if (g_stripPainter)
            g_stripPainter(screenPixels, screenPitch, 400, stripH, 640);
    }

    SDL_UnlockSurface(g_screen);

    // Update the full screen
    SDL_UpdateRect(g_screen, 0, 0, 0, 0);
}

// Process SDL events
bool PlatformProcessEvents()
{
    // SIGTERM/SIGINT (see PlatformTerminateHandler): quit at a frame boundary
    // rather than dying inside a signal context.
    if (g_terminateRequested) return false;

    if (!g_sdlInitialized) return true;
    
    // Process input events (keyboard, mouse, quit flag)
    InputProcessEvents();
    
    // Check if quit was requested
    return !InputIsQuitRequested();
}

} // namespace jjffe
