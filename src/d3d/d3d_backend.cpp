#include "jjffe/d3d/d3d_backend.h"
#include "jjffe/d3d/d3d_demo.h"
#include "jjffe/platform.h"
#include "jjffe/game_loop.h"

#include <SDL.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 400;

SDL_Window* g_window = nullptr;
IDirect3D9* g_d3d = nullptr;
IDirect3DDevice9* g_device = nullptr;
bool g_ready = false;
bool g_platformReady = false;
bool g_quit = false;
std::string g_dataRoot;

uint8_t g_keyStates[512]{};
int g_lastKey = 0;
int g_mouseX = 0, g_mouseY = 0;
int g_mouseRelX = 0, g_mouseRelY = 0;
int g_mouseButtons = 0;

bool FileReadable(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string DirnameOf(const char* path)
{
    if (!path || !path[0]) return {};
    std::string p(path);
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos) return {};
    if (slash == 0) return "/";
    return p.substr(0, slash);
}

bool LooksLikeDataRoot(const std::string& dir)
{
    if (dir.empty()) return false;
    char probe[1024];
    std::snprintf(probe, sizeof(probe), "%s/MODELS/31/model.X", dir.c_str());
    if (FileReadable(probe)) return true;
    std::snprintf(probe, sizeof(probe), "%s/MODELS/31/model.x", dir.c_str());
    if (FileReadable(probe)) return true;
    std::snprintf(probe, sizeof(probe), "%s/MODELS/Sphere.X", dir.c_str());
    if (FileReadable(probe)) return true;
    std::snprintf(probe, sizeof(probe), "%s/models/sphere.X", dir.c_str());
    return FileReadable(probe);
}

std::string ResolveDataRoot()
{
    if (const char* env = std::getenv("JJFFE_D3D_DATA")) {
        if (LooksLikeDataRoot(env)) return env;
        fprintf(stderr, "D3D: JJFFE_D3D_DATA=%s has no MODELS/\n", env);
    }
    if (LooksLikeDataRoot(".")) return ".";

    static const char* kCandidates[] = {
        "/home/alex/Src/Reverse/games/ffe106/FFED3D",
        "../ffe106/FFED3D",
        "../../ffe106/FFED3D",
        "resources",
        "../ffed3d/resources",
        "../../ffed3d/resources",
    };
    for (const char* c : kCandidates) {
        if (LooksLikeDataRoot(c)) return c;
    }

#if defined(__linux__)
    char exe[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        const std::string dir = DirnameOf(exe);
        for (const char* rel : {"/../ffe106/FFED3D", "/../../ffe106/FFED3D"}) {
            const std::string tryPath = dir + rel;
            if (LooksLikeDataRoot(tryPath)) return tryPath;
        }
    }
#endif
    return {};
}

int MapDosToSdl(int dos)
{
    switch (dos) {
    case 0x01: return SDL_SCANCODE_ESCAPE;
    case 0x1C: return SDL_SCANCODE_RETURN;
    case 0x39: return SDL_SCANCODE_SPACE;
    case 0x11: return SDL_SCANCODE_W;
    case 0x1F: return SDL_SCANCODE_S;
    case 0x1E: return SDL_SCANCODE_A;
    case 0x20: return SDL_SCANCODE_D;
    case 0x10: return SDL_SCANCODE_Q;
    case 0x12: return SDL_SCANCODE_E;
    case 0x19: return SDL_SCANCODE_P;
    case 0x14: return SDL_SCANCODE_T;
    case 0x23: return SDL_SCANCODE_H;
    case 0x31: return SDL_SCANCODE_N;
    case 0x48: case 0xC8: return SDL_SCANCODE_UP;
    case 0x50: case 0xD0: return SDL_SCANCODE_DOWN;
    case 0x4B: case 0xCB: return SDL_SCANCODE_LEFT;
    case 0x4D: case 0xCD: return SDL_SCANCODE_RIGHT;
    case 0x49: case 0xC9: return SDL_SCANCODE_PAGEUP;
    case 0x51: case 0xD1: return SDL_SCANCODE_PAGEDOWN;
    default: return 0;
    }
}

} // namespace

namespace jjffe {
namespace d3d {

const char* D3dGetDataRoot() { return g_dataRoot.c_str(); }
SDL_Window* D3dGetWindow() { return g_window; }
IDirect3DDevice9* D3dGetDevice() { return g_device; }
int D3dGetWidth() { return kWidth; }
int D3dGetHeight() { return kHeight; }

bool D3dInit()
{
    if (g_ready) return true;

    setenv("DXVK_WSI_DRIVER", "SDL2", 0);

    g_dataRoot = ResolveDataRoot();
    if (g_dataRoot.empty()) {
        fprintf(stderr,
                "D3D: could not find MODELS/ (set JJFFE_D3D_DATA to ffe106/FFED3D)\n");
    } else {
        printf("D3D data root: %s\n", g_dataRoot.c_str());
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "D3D: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // DXVK Native SDL2 WSI needs SDL_WINDOW_VULKAN for CreateSurface.
    g_window = SDL_CreateWindow(
        "JJFFE D3D9/DXVK",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
    if (!g_window) {
        fprintf(stderr, "D3D: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        fprintf(stderr, "D3D: Direct3DCreate9 failed (is libdxvk_d3d9 installed?)\n");
        return false;
    }

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferWidth = kWidth;
    pp.BackBufferHeight = kHeight;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.hDeviceWindow = reinterpret_cast<HWND>(g_window);
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HWND hwnd = reinterpret_cast<HWND>(g_window);
    HRESULT hr = g_d3d->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
        &pp, &g_device);
    if (FAILED(hr)) {
        hr = g_d3d->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
            &pp, &g_device);
    }
    if (FAILED(hr) || !g_device) {
        fprintf(stderr, "D3D: CreateDevice failed (hr=0x%08lx)\n", (unsigned long)hr);
        return false;
    }

    g_device->SetRenderState(D3DRS_ZENABLE, TRUE);
    // FFED3D .x ships often mix winding / single-sided panels (Viper rear half).
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_device->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    g_device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    g_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    D3DLIGHT9 light{};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
    light.Ambient = {0.35f, 0.35f, 0.40f, 1.0f};
    // Key light from camera-left / above so globes aren't a lit polar cap only.
    light.Direction = {0.35f, -0.55f, 0.75f};
    g_device->SetLight(0, &light);
    g_device->LightEnable(0, TRUE);
    g_device->SetRenderState(D3DRS_AMBIENT, 0x00606070);

    if (!D3dDemoInit(kWidth, kHeight)) {
        fprintf(stderr, "D3D: demo init failed\n");
        return false;
    }

    g_ready = true;
    printf("D3D9/DXVK ready (%dx%d, WSI=SDL2)\n", kWidth, kHeight);
    return true;
}

void D3dShutdown()
{
    D3dDemoShutdown();
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
    if (g_d3d) {
        g_d3d->Release();
        g_d3d = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    if (g_ready) SDL_Quit();
    g_ready = false;
}

void D3dBeginFrame(float r, float g, float b)
{
    if (!g_device) return;
    const DWORD clear = D3DCOLOR_COLORVALUE(r, g, b, 1.0f);
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.0f, 0);
    g_device->BeginScene();
}

void D3dEndFrame()
{
    if (!g_device) return;
    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);
}

void D3dPresentFrame()
{
    if (!g_ready || !g_device) return;

    switch (GameLoopGetPhase()) {
    case GamePhase::Menu:
        D3dDrawMenu(GameLoopGetFrameCount(), GameLoopGetFps());
        break;
    case GamePhase::DemoShipSelect:
        D3dDrawShipSelect();
        break;
    case GamePhase::Demo3D:
        D3dDrawDemo();
        break;
    case GamePhase::IntroCast:
        D3dDrawIntroCast();
        break;
    default:
        D3dBeginFrame(0.02f, 0.04f, 0.10f);
        D3dEndFrame();
        break;
    }
}

} // namespace d3d

bool PlatformIsGpuDraw() { return true; }
bool PlatformIsD3D() { return true; }

uint32_t PlatformGetTicks() { return SDL_GetTicks(); }
void PlatformDelay(uint32_t ms) { SDL_Delay(ms); }

void PlatformVideoInit()
{
    if (g_platformReady) return;
    if (!d3d::D3dInit()) {
        fprintf(stderr, "PlatformVideoInit: D3D9/DXVK init failed\n");
        return;
    }
    g_platformReady = true;
}

void PlatformVideoCleanup()
{
    d3d::D3dShutdown();
    g_platformReady = false;
}

void PlatformVideoBlit() { d3d::D3dPresentFrame(); }
void PlatformSetPaletteEntry(int, uint8_t, uint8_t, uint8_t) {}

bool PlatformProcessEvents()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            g_quit = true;
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode < 512)
                g_keyStates[ev.key.keysym.scancode] = 1;
            break;
        case SDL_KEYUP:
            if (ev.key.keysym.scancode < 512)
                g_keyStates[ev.key.keysym.scancode] = 0;
            break;
        case SDL_MOUSEMOTION:
            g_mouseRelX += ev.motion.xrel;
            g_mouseRelY += ev.motion.yrel;
            g_mouseX = ev.motion.x;
            g_mouseY = ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            g_mouseButtons = (int)SDL_GetMouseState(nullptr, nullptr);
            break;
        default:
            break;
        }
    }
    return !g_quit;
}

void PlatformInputInit() {}
void PlatformInputCleanup() {}

void InputKeybReadStates(uint8_t* pKeys)
{
    memset(pKeys, 0x80, 256);
    for (int dos = 0; dos < 256; dos++) {
        int sc = MapDosToSdl(dos);
        if (sc > 0 && sc < 512 && g_keyStates[sc])
            pKeys[dos] = 0x00;
    }
    auto sync = [&](int a, int b) {
        if (pKeys[a] == 0x00) pKeys[b] = 0x00;
        if (pKeys[b] == 0x00) pKeys[a] = 0x00;
    };
    sync(0x48, 0xC8);
    sync(0x50, 0xD0);
    sync(0x4B, 0xCB);
    sync(0x4D, 0xCD);
}

int InputKeybGetLastKey() { return g_lastKey; }
void InputKeybSetLastKey(int key) { g_lastKey = key; }
int InputMouseReadButtons() { return g_mouseButtons; }

void InputMouseReadMickeys(int* pXMick, int* pYMick)
{
    if (pXMick) *pXMick = g_mouseRelX;
    if (pYMick) *pYMick = g_mouseRelY;
    g_mouseRelX = 0;
    g_mouseRelY = 0;
}

void InputMouseReadPos(int* pXPos, int* pYPos)
{
    if (pXPos) *pXPos = g_mouseX;
    if (pYPos) *pYPos = g_mouseY;
}

void InputJoyReadPos(int* xpos, int* ypos)
{
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}
int InputJoyReadButtons() { return 0; }

void PlatformSoundInit() {}
void PlatformSoundCleanup() {}
void PlatformMusicInit() {}
void PlatformMusicCleanup() {}
void PlatformMusicPlaySong(int) {}
void PlatformMusicStopSong() {}
bool PlatformMusicSongDone() { return true; }
void PlatformMusicSetVolume(int) {}
void PlatformTimerInit() {}
void PlatformTimerCleanup() {}

} // namespace jjffe
