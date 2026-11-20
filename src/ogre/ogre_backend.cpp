#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ogre/ogre_ui.h"
#include "jjffe/ogre/ogre_demo.h"
#include "jjffe/ogre/ogre_ship_hw.h"
#include "jjffe/game_loop.h"
#include "jjffe/platform.h"

#include <Ogre.h>
#include <OgreArchiveManager.h>
#include <OgreCamera.h>
#include <OgreConfigFile.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>
#include <OgreItem.h>
#include <OgreImage2.h>
#include <OgreManualObject2.h>
#include <OgreRoot.h>
#include <OgreWindow.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorWorkspace.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <vector>

namespace jjffe {
namespace ogre {

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 400;

SDL_Window* s_window = nullptr;
Ogre::Root* s_root = nullptr;
Ogre::Window* s_renderWindow = nullptr;
Ogre::SceneManager* s_sceneMgr = nullptr;
Ogre::Camera* s_camera = nullptr;
Ogre::CompositorWorkspace* s_workspace = nullptr;
Ogre::ColourValue s_clearColour(16.0f / 255.0f, 11.0f / 255.0f, 67.0f / 255.0f, 1.0f);

Ogre::ManualObject* s_uiMO = nullptr;
Ogre::ManualObject* s_hullMO = nullptr;
Ogre::ManualObject* s_contourMO = nullptr;
Ogre::ManualObject* s_glintMO = nullptr;
Ogre::ManualObject* s_punchMO = nullptr;
Ogre::ManualObject* s_starsMO = nullptr;
std::map<std::string, Ogre::ManualObject*> s_texMOs;
std::vector<Ogre::ManualObject*> s_painterMOs;
bool s_quitAfterShot = false;

constexpr const char* kUnlitMat = "JjffeUnlit";
constexpr const char* kUnlitUiMat = "JjffeUnlitUI";
constexpr const char* kWorkspaceName = "JjffeWorkspace";

bool FindPath(const char* relative, char* out, size_t outSz)
{
    const char* candidates[] = {
        "data/ogre-next/",
        "../data/ogre-next/",
        "data/ogre/", // legacy fallback
        nullptr,
    };
    for (int i = 0; candidates[i]; i++) {
        snprintf(out, outSz, "%s%s", candidates[i], relative);
        FILE* f = fopen(out, "r");
        if (f) {
            fclose(f);
            return true;
        }
    }
    // Directory probe for Hlms root (no single file).
    for (int i = 0; candidates[i]; i++) {
        snprintf(out, outSz, "%s", candidates[i]);
        char probe[640];
        snprintf(probe, sizeof(probe), "%sHlms/Unlit", out);
        FILE* f = fopen((std::string(probe) + "/Any/Unlit_piece_vs.any").c_str(), "r");
        if (!f) {
            // try listing via opendir equivalent — open a known Any file
            continue;
        }
        fclose(f);
        return true;
    }
    snprintf(out, outSz, "%s", relative);
    return false;
}

bool FindConfigPath(const char* name, char* out, size_t outSz)
{
    return FindPath(name, out, outSz);
}

bool FindHlmsRoot(char* out, size_t outSz)
{
    const char* candidates[] = {
        "data/ogre-next/",
        "../data/ogre-next/",
        nullptr,
    };
    for (int i = 0; candidates[i]; i++) {
        char probe[640];
        snprintf(probe, sizeof(probe), "%sHlms/Unlit/Any", candidates[i]);
        // Any folder existence via opening a dirent-less check: try Common piece
        snprintf(probe, sizeof(probe), "%sHlms/Common/Any/Main/200.MatrixPalette_piece_vs.any",
                 candidates[i]);
        FILE* f = fopen(probe, "r");
        if (f) {
            fclose(f);
            snprintf(out, outSz, "%s", candidates[i]);
            return true;
        }
        // Broader: Unlit Any folder with any .any
        snprintf(probe, sizeof(probe), "%sHlms/Unlit", candidates[i]);
        // Use opendir
        DIR* d = opendir(probe);
        if (d) {
            closedir(d);
            snprintf(out, outSz, "%s", candidates[i]);
            return true;
        }
    }
    snprintf(out, outSz, "data/ogre-next/");
    return false;
}

Ogre::HlmsUnlit* GetUnlit()
{
    if (!s_root) return nullptr;
    return static_cast<Ogre::HlmsUnlit*>(
        s_root->getHlmsManager()->getHlms(Ogre::HLMS_UNLIT));
}

void CreateUnlitDatablock(const char* name, bool depthCheck, bool depthWrite)
{
    Ogre::HlmsUnlit* hlms = GetUnlit();
    if (!hlms) return;
    if (hlms->getDatablock(name)) return;

    Ogre::HlmsMacroblock macro;
    macro.mDepthCheck = depthCheck;
    macro.mDepthWrite = depthWrite;
    macro.mDepthFunc = Ogre::CMPF_LESS_EQUAL;
    macro.mCullMode = Ogre::CULL_NONE;
    Ogre::HlmsBlendblock blend;
    blend.mSourceBlendFactor = Ogre::SBF_ONE;
    blend.mDestBlendFactor = Ogre::SBF_ZERO;

    auto* db = static_cast<Ogre::HlmsUnlitDatablock*>(
        hlms->createDatablock(name, name, macro, blend, Ogre::HlmsParamVec()));
    // Vertex colours from ManualObject::colour (no constant diffuse).
    db->setUseColour(false);
}

void CreateUnlitMaterials()
{
    CreateUnlitDatablock(kUnlitMat, true, true);
    CreateUnlitDatablock(kUnlitUiMat, false, false);
    CreateUnlitDatablock("JjffeUnlitVC", true, true);
    CreateUnlitDatablock("JjffeHwContour", true, false);
}

void RegisterHlmsUnlit()
{
    char hlmsRoot[512];
    if (!FindHlmsRoot(hlmsRoot, sizeof(hlmsRoot))) {
        fprintf(stderr, "Ogre-Next: Hlms Media not found under data/ogre-next/Hlms\n");
    }
    Ogre::String root(hlmsRoot);
    if (!root.empty() && root.back() != '/')
        root += '/';

    Ogre::String mainFolderPath;
    Ogre::StringVector libraryFoldersPaths;
    Ogre::HlmsUnlit::getDefaultPaths(mainFolderPath, libraryFoldersPaths);

    auto& am = Ogre::ArchiveManager::getSingleton();
    Ogre::Archive* archiveUnlit = am.load(root + mainFolderPath, "FileSystem", true);
    Ogre::ArchiveVec libs;
    for (const auto& p : libraryFoldersPaths)
        libs.push_back(am.load(root + p, "FileSystem", true));

    auto* hlmsUnlit = OGRE_NEW Ogre::HlmsUnlit(archiveUnlit, &libs);
    s_root->getHlmsManager()->registerHlms(hlmsUnlit);
}

void SetupCompositor(Ogre::Camera* cam)
{
    auto* cm = s_root->getCompositorManager2();
    if (s_workspace) {
        cm->removeWorkspace(s_workspace);
        s_workspace = nullptr;
    }
    if (!cm->hasWorkspaceDefinition(kWorkspaceName)) {
        cm->createBasicWorkspaceDef(kWorkspaceName, s_clearColour, Ogre::IdString());
    }
    s_workspace = cm->addWorkspace(s_sceneMgr, s_renderWindow->getTexture(), cam,
                                   kWorkspaceName, true);
}

Ogre::ManualObject* MakeMO(bool identityDefault, uint8_t queue)
{
    auto* mo = s_sceneMgr->createManualObject(Ogre::SCENE_DYNAMIC);
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    mo->setRenderQueueGroup(queue);
    if (mo->isAttached())
        mo->detachFromParent();
    auto* node = s_sceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode();
    node->attachObject(mo);
    (void)identityDefault;
    return mo;
}

} // namespace

bool OgreScriptedCaptureDone() { return s_quitAfterShot; }

const char* OgreUnlitMaterial() { return kUnlitMat; }
const char* OgreUnlitUiMaterial() { return kUnlitUiMat; }

SDL_Window* OgreGetWindow() { return s_window; }
Ogre::Root* OgreGetRoot() { return s_root; }
Ogre::SceneManager* OgreGetSceneManager() { return s_sceneMgr; }
Ogre::Window* OgreGetRenderWindow() { return s_renderWindow; }
Ogre::Camera* OgreGetCamera() { return s_camera; }
void* OgreGetViewport() { return nullptr; }

void OgreSetWindowTitleTag(const char* tag)
{
    if (!s_window) return;
    char title[96];
    snprintf(title, sizeof(title), "JJFFE v2.8 - Ogre-Next/Vulkan [%s]",
             tag && tag[0] ? tag : "soft");
    SDL_SetWindowTitle(s_window, title);
}

int OgreGetWidth() { return kWidth; }
int OgreGetHeight() { return kHeight; }

void OgreReconnectWorkspace(Ogre::Camera* cam)
{
    if (!s_root || !s_renderWindow || !s_sceneMgr || !cam) return;
    SetupCompositor(cam);
}

void OgreApplyIdentityProjection(Ogre::ManualObject* mo, bool identity)
{
    if (!mo) return;
    const unsigned n = mo->getNumSections();
    for (unsigned i = 0; i < n; i++) {
        auto* sec = mo->getSection(i);
        if (!sec) continue;
        sec->setUseIdentityProjection(identity);
        sec->setUseIdentityView(identity);
    }
}

void OgreEndManualTriangles(Ogre::ManualObject* mo, bool identityProjection)
{
    if (!mo) return;
    const size_t n = mo->getCurrentVertexCount();
    for (uint32_t i = 0; i < (uint32_t)n; ++i)
        mo->index(i);
    mo->end();
    if (identityProjection)
        OgreApplyIdentityProjection(mo, true);
}

Ogre::ManualObject* OgreUiManual() { return s_uiMO; }
Ogre::ManualObject* OgreHullManual() { return s_hullMO; }
Ogre::ManualObject* OgreContourManual() { return s_contourMO; }
Ogre::ManualObject* OgreGlintManual() { return s_glintMO; }
Ogre::ManualObject* OgrePunchManual() { return s_punchMO; }
Ogre::ManualObject* OgreStarsManual() { return s_starsMO; }

Ogre::ManualObject* OgreTexManual(const char* materialName)
{
    if (!materialName || !s_sceneMgr) return nullptr;
    auto it = s_texMOs.find(materialName);
    if (it != s_texMOs.end()) return it->second;
    auto* mo = MakeMO(true, 12);
    s_texMOs[materialName] = mo;
    return mo;
}

void OgreClearTexManuals()
{
    for (auto& kv : s_texMOs) {
        if (kv.second) kv.second->clear();
    }
}

Ogre::ManualObject* OgrePainterPolyManual(int index, uint16_t priority)
{
    if (!s_sceneMgr || index < 0) return nullptr;
    while ((int)s_painterMOs.size() <= index) {
        // Encode priority into queue band (higher priority → later queue).
        uint8_t q = static_cast<uint8_t>(10 + (priority % 40));
        s_painterMOs.push_back(MakeMO(true, q));
    }
    return s_painterMOs[(size_t)index];
}

void OgreClearPainterPolyManuals(int keepCount)
{
    if (keepCount < 0) keepCount = 0;
    for (size_t i = (size_t)keepCount; i < s_painterMOs.size(); i++) {
        if (s_painterMOs[i]) s_painterMOs[i]->clear();
    }
}

bool OgreEnsureTexturedDatablock(const char* datablockName, const char* textureName,
                                 bool depthCheck, bool depthWrite, bool alphaTest)
{
    Ogre::HlmsUnlit* hlms = GetUnlit();
    if (!hlms || !datablockName || !textureName) return false;
    if (hlms->getDatablock(datablockName)) return true;

    Ogre::HlmsMacroblock macro;
    macro.mDepthCheck = depthCheck;
    macro.mDepthWrite = depthWrite;
    macro.mDepthFunc = Ogre::CMPF_LESS_EQUAL;
    macro.mCullMode = Ogre::CULL_NONE;

    Ogre::HlmsBlendblock blend;
    if (alphaTest) {
        blend.mSourceBlendFactor = Ogre::SBF_SOURCE_ALPHA;
        blend.mDestBlendFactor = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA;
    } else {
        blend.mSourceBlendFactor = Ogre::SBF_ONE;
        blend.mDestBlendFactor = Ogre::SBF_ZERO;
    }

    Ogre::HlmsParamVec params;
    if (alphaTest)
        params.push_back(std::make_pair("alpha_test", "greater 0.01"));

    auto* db = static_cast<Ogre::HlmsUnlitDatablock*>(
        hlms->createDatablock(datablockName, datablockName, macro, blend, params));
    db->setUseColour(false);
    db->setTexture(0, textureName);
    return true;
}

bool OgreInit()
{
    if (s_root) return true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "Ogre-Next: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    s_window = SDL_CreateWindow(
        "JJFFE v2.8 - Ogre-Next/Vulkan [soft]",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s_window) {
        fprintf(stderr, "Ogre-Next: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    char pluginsPath[512];
    if (!FindConfigPath("plugins.cfg", pluginsPath, sizeof(pluginsPath))) {
        fprintf(stderr, "Ogre-Next: plugins.cfg not found (need data/ogre-next/plugins.cfg)\n");
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
        SDL_Quit();
        return false;
    }

    try {
        s_root = OGRE_NEW Ogre::Root(nullptr, pluginsPath, "", "ogre.log", "JJFFE");

        Ogre::RenderSystem* rs = nullptr;
        const auto& renderers = s_root->getAvailableRenderers();
        for (auto* r : renderers) {
            if (r->getName().find("Vulkan") != Ogre::String::npos) {
                rs = r;
                break;
            }
        }
        if (!rs && !renderers.empty())
            rs = renderers.front();
        if (!rs) {
            fprintf(stderr, "Ogre-Next: no RenderSystem available\n");
            OgreShutdown();
            return false;
        }
        s_root->setRenderSystem(rs);
        s_root->initialise(false);

        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (!SDL_GetWindowWMInfo(s_window, &wmInfo)) {
            fprintf(stderr, "Ogre-Next: SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
            OgreShutdown();
            return false;
        }

        Ogre::NameValuePairList misc;
        misc["title"] = "JJFFE";
#if defined(SDL_VIDEO_DRIVER_X11)
        if (wmInfo.subsystem == SDL_SYSWM_X11) {
            misc["parentWindowHandle"] =
                Ogre::StringConverter::toString(size_t(wmInfo.info.x11.window));
            misc["SDL2x11"] =
                Ogre::StringConverter::toString(size_t(&wmInfo.info.x11));
        }
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
        if (wmInfo.subsystem == SDL_SYSWM_WAYLAND) {
            misc["externalWlDisplay"] =
                Ogre::StringConverter::toString(size_t(wmInfo.info.wl.display));
            misc["externalWlSurface"] =
                Ogre::StringConverter::toString(size_t(wmInfo.info.wl.surface));
        }
#endif

        s_renderWindow = s_root->createRenderWindow("JJFFE", kWidth, kHeight, false, &misc);

        RegisterHlmsUnlit();
        Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups(true);

        const size_t numThreads = 1;
        s_sceneMgr = s_root->createSceneManager(Ogre::ST_GENERIC, numThreads, "jjffeScene");

        s_camera = s_sceneMgr->createCamera("mainCam");
        s_camera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
        s_camera->setOrthoWindow((float)kWidth, (float)kHeight);
        s_camera->setNearClipDistance(0.05f);
        s_camera->setFarClipDistance(80.0f);
        s_camera->setAutoAspectRatio(true);
        s_camera->setPosition((float)kWidth * 0.5f, (float)kHeight * 0.5f, 30.0f);
        s_camera->lookAt(Ogre::Vector3((float)kWidth * 0.5f, (float)kHeight * 0.5f, 0.0f));

        SetupCompositor(s_camera);
        CreateUnlitMaterials();

        s_starsMO = MakeMO(true, 0);
        s_hullMO = MakeMO(true, 10);
        s_contourMO = MakeMO(true, 20);
        s_glintMO = MakeMO(true, 30);
        s_punchMO = MakeMO(true, 40);
        s_uiMO = MakeMO(true, 50);

        if (!OgreUiInit(kWidth, kHeight)) {
            OgreShutdown();
            return false;
        }
        if (!OgreDemoInit(kWidth, kHeight)) {
            OgreShutdown();
            return false;
        }
        OgreShipHwInit();
        OgreShipHwSetActive(OgreGetShipRenderMode() == ShipRenderMode::Hardware);

        printf("Ogre-Next/Vulkan initialized (%dx%d, WSI=SDL2)\n", kWidth, kHeight);
        return true;
    } catch (const Ogre::Exception& e) {
        fprintf(stderr, "Ogre-Next exception: %s\n", e.getFullDescription().c_str());
        OgreShutdown();
        return false;
    }
}

void OgreShutdown()
{
    OgreShipHwShutdown();
    OgreDemoShutdown();
    OgreUiShutdown();

    if (s_root && s_workspace) {
        s_root->getCompositorManager2()->removeWorkspace(s_workspace);
        s_workspace = nullptr;
    }

    s_texMOs.clear();
    s_painterMOs.clear();
    s_uiMO = s_hullMO = s_contourMO = s_glintMO = s_punchMO = s_starsMO = nullptr;

    if (s_root) {
        if (s_sceneMgr) {
            s_root->destroySceneManager(s_sceneMgr);
            s_sceneMgr = nullptr;
        }
        s_camera = nullptr;
        s_renderWindow = nullptr;
        OGRE_DELETE s_root;
        s_root = nullptr;
    }

    if (s_window) {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }
    SDL_Quit();
}

void OgreBeginFrame(float r, float g, float b)
{
    s_clearColour = Ogre::ColourValue(r, g, b, 1.0f);
    if (s_uiMO) s_uiMO->clear();
    if (s_hullMO) s_hullMO->clear();
    if (s_contourMO) s_contourMO->clear();
    if (s_glintMO) s_glintMO->clear();
    if (s_punchMO) s_punchMO->clear();
    if (s_starsMO) s_starsMO->clear();
    OgreClearTexManuals();
    OgreClearPainterPolyManuals(0);
    OgreShipHwClear();
    OgreUiBegin();
}

void OgreEndFrame()
{
    OgreUiFlush();
    if (s_root)
        s_root->renderOneFrame();

    static long s_frame = 0;
    s_frame++;
    const char* shot = getenv("JJFFE_OGRE_SHOT");
    if (shot && shot[0] && s_renderWindow) {
        const char* atEnv = getenv("JJFFE_OGRE_SHOT_FRAME");
        const long at = atEnv && atEnv[0] ? strtol(atEnv, nullptr, 10) : 30;
        if (s_frame == at) {
            try {
                s_renderWindow->setWantsToDownload(true);
                Ogre::Image2 img;
                Ogre::TextureGpu* tex = s_renderWindow->getTexture();
                if (tex)
                    img.convertFromTexture(tex, 0u, 0u);
                img.save(shot, 0, 0);
                fprintf(stderr, "Ogre-Next: wrote screenshot %s (frame %ld)\n", shot, s_frame);
            } catch (const Ogre::Exception& e) {
                fprintf(stderr, "Ogre-Next: screenshot failed: %s\n",
                        e.getFullDescription().c_str());
            }
        }
        if (s_frame >= at)
            s_quitAfterShot = true;
    }
}

void OgrePresentFrame()
{
    if (!s_root || !s_renderWindow) return;

    GamePhase phase = GameLoopGetPhase();
    switch (phase) {
    case GamePhase::Menu:
        OgreDrawMenu(GameLoopGetFrameCount(), GameLoopGetFps());
        break;
    case GamePhase::DemoShipSelect:
        OgreDrawShipSelect();
        break;
    case GamePhase::Demo3D:
        OgreDrawDemo();
        break;
    case GamePhase::IntroCast:
        OgreDrawIntroCast();
        break;
    case GamePhase::Briefing:
    case GamePhase::Mission:
        OgreDrawBriefingStub();
        break;
    default:
        OgreBeginFrame(0.1f, 0.0f, 0.1f);
        OgreEndFrame();
        break;
    }
}

} // namespace ogre
} // namespace jjffe

// ---------------------------------------------------------------------------
// Platform* implementations for the Ogre binary (replaces sdl_backend.cpp).
// ---------------------------------------------------------------------------

namespace jjffe {

static bool s_platformReady = false;
static uint8_t s_keyStates[512] = {};
static int s_mouseX = 0, s_mouseY = 0, s_mouseButtons = 0;
static int s_mouseRelX = 0, s_mouseRelY = 0;
static int s_lastKey = 0xFF;
static bool s_quit = false;

static int MapDosToSdl(int dos)
{
    switch (dos) {
    case 0x01: return SDL_SCANCODE_ESCAPE;
    case 0x02: return SDL_SCANCODE_1;
    case 0x03: return SDL_SCANCODE_2;
    case 0x04: return SDL_SCANCODE_3;
    case 0x05: return SDL_SCANCODE_4;
    case 0x06: return SDL_SCANCODE_5;
    case 0x07: return SDL_SCANCODE_6;
    case 0x08: return SDL_SCANCODE_7;
    case 0x09: return SDL_SCANCODE_8;
    case 0x0A: return SDL_SCANCODE_9;
    case 0x0B: return SDL_SCANCODE_0;
    case 0x10: return SDL_SCANCODE_Q;
    case 0x11: return SDL_SCANCODE_W;
    case 0x12: return SDL_SCANCODE_E;
    case 0x13: return SDL_SCANCODE_R;
    case 0x14: return SDL_SCANCODE_T;
    case 0x1C: return SDL_SCANCODE_RETURN;
    case 0x1D: return SDL_SCANCODE_LCTRL;
    case 0x1E: return SDL_SCANCODE_A;
    case 0x1F: return SDL_SCANCODE_S;
    case 0x20: return SDL_SCANCODE_D;
    case 0x21: return SDL_SCANCODE_F;
    case 0x2A: return SDL_SCANCODE_LSHIFT;
    case 0x39: return SDL_SCANCODE_SPACE;
    case 0x48: case 0xC8: return SDL_SCANCODE_UP;
    case 0x50: case 0xD0: return SDL_SCANCODE_DOWN;
    case 0x4B: case 0xCB: return SDL_SCANCODE_LEFT;
    case 0x4D: case 0xCD: return SDL_SCANCODE_RIGHT;
    case 0x49: case 0xC9: return SDL_SCANCODE_PAGEUP;
    case 0x51: case 0xD1: return SDL_SCANCODE_PAGEDOWN;
    case 0x19: return SDL_SCANCODE_P;
    case 0x23: return SDL_SCANCODE_H;
    case 0x31: return SDL_SCANCODE_N;
    case 0x58: return SDL_SCANCODE_F12;
    default: return 0;
    }
}

bool PlatformIsGpuDraw()
{
    return true;
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

void PlatformVideoInit()
{
    if (s_platformReady) return;
    if (!ogre::OgreInit()) {
        fprintf(stderr, "PlatformVideoInit: Ogre-Next init failed\n");
        return;
    }
    s_platformReady = true;
}

void PlatformVideoCleanup()
{
    ogre::OgreShutdown();
    s_platformReady = false;
}

void PlatformVideoBlit()
{
    ogre::OgrePresentFrame();
}

void PlatformSetPaletteEntry(int /*index*/, uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/)
{
}

bool PlatformProcessEvents()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            s_quit = true;
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.scancode < 512)
                s_keyStates[ev.key.keysym.scancode] = 1;
            break;
        case SDL_KEYUP:
            if (ev.key.keysym.scancode < 512)
                s_keyStates[ev.key.keysym.scancode] = 0;
            break;
        case SDL_MOUSEMOTION:
            s_mouseRelX += ev.motion.xrel;
            s_mouseRelY += ev.motion.yrel;
            s_mouseX = ev.motion.x;
            s_mouseY = ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            s_mouseButtons = SDL_GetMouseState(nullptr, nullptr);
            break;
        default:
            break;
        }
    }
    return !s_quit && !ogre::OgreScriptedCaptureDone();
}

void PlatformInputInit() {}
void PlatformInputCleanup() {}

void InputKeybReadStates(uint8_t* pKeys)
{
    memset(pKeys, 0x80, 256);
    for (int dos = 0; dos < 256; dos++) {
        int sc = MapDosToSdl(dos);
        if (sc > 0 && sc < 512 && s_keyStates[sc])
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

int InputKeybGetLastKey() { return s_lastKey; }
void InputKeybSetLastKey(int key) { s_lastKey = key; }

int InputMouseReadButtons() { return s_mouseButtons; }

void InputMouseReadMickeys(int* pXMick, int* pYMick)
{
    if (pXMick) *pXMick = s_mouseRelX;
    if (pYMick) *pYMick = s_mouseRelY;
    s_mouseRelX = 0;
    s_mouseRelY = 0;
}

void InputMouseReadPos(int* pXPos, int* pYPos)
{
    if (pXPos) *pXPos = s_mouseX;
    if (pYPos) *pYPos = s_mouseY;
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
