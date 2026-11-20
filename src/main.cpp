#include "jjffe/platform.h"
#if defined(JJFFE_USE_OGRE)
#include "jjffe/ogre/ogre_backend.h"
#endif
#if defined(JJFFE_USE_D3D9)
#include "jjffe/d3d/d3d_backend.h"
#include "jjffe/d3d/d3d_demo.h"
#endif
#include "jjffe/sound.h"
#include "jjffe/data_loader.h"
#include "jjffe/game_state.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/timer.h"
#include "jjffe/game_loop.h"
#include "jjffe/demo3d.h"
#include "jjffe/planet_render.h"
#include "jjffe/intro_cast.h"
#include "jjffe/core/engine.h"
#include "jjffe/core/gameplay.h"
#include "jjffe/core/sound_callback.h"
#include "data_blob.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif
#if defined(JJFFE_USE_SDL) || defined(JJFFE_USE_OGRE) || defined(JJFFE_USE_D3D9)
#define JJFFE_HAS_PLATFORM 1
#endif

namespace {

// Desktop launches often use cwd=$HOME; data_blob lives next to the binary (FFE/)
// or under the repo. Try cwd first, then argv0 /proc/self/exe directories.
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

// The blob's offsets are compiled in (data_blob.h), so a copy of another
// generation (e.g. one left beside an older binary in FFE/) is skipped.
bool TryDataBlob(const std::string& path, std::string* out)
{
    if (path.empty() || !FileReadable(path.c_str())) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    long size = -1;
    if (f && std::fseek(f, 0, SEEK_END) == 0) size = std::ftell(f);
    if (f) std::fclose(f);
    if (size != static_cast<long>(jjffe::data::BLOB_TOTAL_SIZE)) {
        std::fprintf(stderr, "Skipping %s: %ld bytes, this build's data blob is %u\n",
                     path.c_str(), size, static_cast<unsigned>(jjffe::data::BLOB_TOTAL_SIZE));
        return false;
    }
    *out = path;
    return true;
}

std::string FindDataBlobPath(const char* argv0)
{
    std::string found;
    // cwd (FFE/, jjffe/, or FFED3D/ with sibling FFE/)
    if (TryDataBlob("data_blob/data_blob.bin", &found)) return found;
    if (TryDataBlob("../FFE/data_blob/data_blob.bin", &found)) return found;
    if (TryDataBlob("../data_blob/data_blob.bin", &found)) return found;
    if (TryDataBlob("../jjffe/data_blob/data_blob.bin", &found)) return found;

    auto tryBeside = [&](const std::string& dir) {
        if (dir.empty()) return false;
        return TryDataBlob(dir + "/data_blob/data_blob.bin", &found) ||
               TryDataBlob(dir + "/../data_blob/data_blob.bin", &found) ||
               TryDataBlob(dir + "/../jjffe/data_blob/data_blob.bin", &found) ||
               TryDataBlob(dir + "/../FFE/data_blob/data_blob.bin", &found);
    };

    if (argv0 && tryBeside(DirnameOf(argv0))) return found;

#if defined(__linux__)
    char exe[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        if (tryBeside(DirnameOf(exe))) return found;
    }
#endif
    return {};
}

} // namespace

int main(int argc, char* argv[])
{
#if defined(JJFFE_USE_OGRE)
    printf("JJFFE v2.8 - Ogre/Vulkan\n");
#elif defined(JJFFE_USE_D3D9)
    printf("JJFFE v2.8 - D3D9/DXVK (FFED3D models)\n");
#else
    printf("JJFFE v2.8 - C++ Port\n");
    printf("ASM to C++ conversion in progress...\n");
#endif

    bool startAtShipSelect = false;
    bool startAtIntroCast = false;
    bool startMission = false;
    bool legacyShipDemo = false;
    bool exportAssetDemo = false;
    bool useExportAssets = false;
    int shipDemoIndex = -1;
    int planetDemoIndex = -1;
    int missionStartIndex = -1;
    [[maybe_unused]] bool solDemo = false;
    const char* exportAssetRoot = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--demo") == 0) {
            startAtShipSelect = true;
        } else if (std::strcmp(argv[i], "--ship") == 0 && i + 1 < argc) {
            shipDemoIndex = std::atoi(argv[++i]);
            legacyShipDemo = false;
            exportAssetDemo = false;
        } else if (std::strcmp(argv[i], "--legacy-ship") == 0 && i + 1 < argc) {
            shipDemoIndex = std::atoi(argv[++i]);
            legacyShipDemo = true;
            exportAssetDemo = false;
        } else if (std::strcmp(argv[i], "--export-ship") == 0 && i + 1 < argc) {
            shipDemoIndex = std::atoi(argv[++i]);
            exportAssetDemo = true;
            legacyShipDemo = false;
        } else if (std::strcmp(argv[i], "--use-export") == 0) {
            useExportAssets = true;
        } else if (std::strcmp(argv[i], "--export-dir") == 0 && i + 1 < argc) {
            exportAssetRoot = argv[++i];
        } else if (std::strcmp(argv[i], "--sol") == 0) {
            solDemo = true;
        } else if (std::strcmp(argv[i], "--planet") == 0 && i + 1 < argc) {
            planetDemoIndex = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--intro") == 0) {
            startAtIntroCast = true;
        } else if (std::strcmp(argv[i], "--mission") == 0) {
            startMission = true;
        } else if (std::strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            missionStartIndex = std::atoi(argv[++i]);
        }
    }

    jjffe::SetDemoAssetShipsFromEnv();
    if (exportAssetRoot) {
        jjffe::SetDemoExportAssetRoot(exportAssetRoot);
    }

    // --- SysInit: Load data blob (cwd or beside the executable) ---
    const std::string dataPath = FindDataBlobPath(argc > 0 ? argv[0] : nullptr);
    const bool dataOk = !dataPath.empty() && jjffe::data::LoadDataBlob(dataPath.c_str());
    if (!dataOk) {
        fprintf(stderr,
                "Warning: Failed to load data_blob/data_blob.bin\n"
                "  Tried cwd, ../FFE/, and paths next to this binary.\n"
                "  Soft mesh / --legacy-ship / --intro need the blob "
                "(e.g. ffe106/FFE/data_blob or jjffe/data_blob).\n");
    } else {
        printf("Data blob path: %s\n", dataPath.c_str());
    }

    const bool bootFlow = !startAtIntroCast && !startAtShipSelect && !startMission &&
                          shipDemoIndex < 0 && planetDemoIndex < 0 && !solDemo;
    if (!dataOk && (legacyShipDemo || startAtIntroCast || startAtShipSelect || bootFlow)) {
        fprintf(stderr,
                "Fatal: the default boot / --legacy-ship / --intro / --demo require "
                "data_blob (internal meshes).\n"
                "  Example: cd .../ffe106/FFE && JJFFE_D3D_DATA=../FFED3D "
                "SDL_VIDEODRIVER=x11 /path/to/ffelnxd3d --demo\n");
        return 1;
    }

    // --- Platform init ---
#ifdef JJFFE_HAS_PLATFORM
    jjffe::PlatformVideoInit();
#if defined(JJFFE_USE_OGRE)
    if (!jjffe::ogre::OgreGetRoot() || !jjffe::ogre::OgreGetRenderWindow()) {
        fprintf(stderr, "Fatal: Ogre-Next init failed. Check data/ogre-next/plugins.cfg and Vulkan.\n");
        return 1;
    }
#endif
#if defined(JJFFE_USE_D3D9)
    if (!jjffe::d3d::D3dGetDevice()) {
        fprintf(stderr,
                "Fatal: D3D9/DXVK init failed. Set DXVK_WSI_DRIVER=SDL2 and "
                "JJFFE_D3D_DATA=/path/to/ffe106/FFED3D\n");
        return 1;
    }
#endif
    jjffe::PlatformInputInit();
    jjffe::PlatformTimerInit();
#endif

    // JJFFE_NO_MUSIC=1 (R1162): no music backend, so SoundCheckInit reports no MIDI - as ffeasm's
    // SDL shim always does (sdl/sdlsound.cpp, *pMidi = 0). With MIDI present FUNC_001902 picks a
    // random song through FUNC_000922, a game-RNG draw ffeasm never makes, which shifts every later
    // draw. Live runs compared against ffeasm set it; normal play keeps its music.
    const char* noMusicEnv = getenv("JJFFE_NO_MUSIC");
    const bool noMusic = noMusicEnv && noMusicEnv[0] == '1';
    jjffe::RegisterSoundCallbacks(
        jjffe::PlatformSoundInit,
        jjffe::PlatformSoundCleanup,
        jjffe::SoundPlaySample,
        jjffe::SoundStopAllSamples,
        noMusic ? nullptr : jjffe::PlatformMusicInit,
        noMusic ? nullptr : jjffe::PlatformMusicCleanup,
        noMusic ? nullptr : jjffe::PlatformMusicPlaySong,
        noMusic ? nullptr : jjffe::PlatformMusicStopSong,
        noMusic ? nullptr : jjffe::PlatformMusicSongDone,
        noMusic ? nullptr : jjffe::PlatformMusicSetVolume
    );

    jjffe::VideoInit();
    jjffe::SoundInit();
    jjffe::InputInit();
    jjffe::TimerInit();
    jjffe::GameInit();
    printf("Initialization complete.\n");

    jjffe::InitGameState();

    // Default boot = _asmmain: splash → intro cast → FUNC_000147 (landed at Hope).
    jjffe::GameLoopInit();
    if (planetDemoIndex >= 0 && shipDemoIndex < 0)
        shipDemoIndex = planetDemoIndex;
#if defined(JJFFE_USE_D3D9)
    if (planetDemoIndex >= 0)
        jjffe::d3d::D3dSetPlanetOverride(planetDemoIndex);
    if (solDemo) {
        jjffe::d3d::D3dSetSolSystem(true);
        if (shipDemoIndex < 0) shipDemoIndex = 141;
        printf("Starting Sol system instance view --sol.\n");
        printf("  32 sphere-eligible PhysObjs; stations SKIP_MESH.\n");
        printf("  LEFT/RIGHT center prev/next body (tree order).\n");
    }
#endif
    if (shipDemoIndex >= 0) {
        if (legacyShipDemo) {
            printf("Starting legacy D3D ship demo --legacy-ship %d.\n", shipDemoIndex);
            printf("  Internal mesh → D3D triangles (FFED3D DrawReal* style), not .x.\n");
            jjffe::SetDemoLegacySoft(true);
        } else if (exportAssetDemo) {
            printf("Starting export-asset orbit --export-ship %d.\n", shipDemoIndex);
            printf("  Loads export/<idx>_name/ (hull→punch→overlay + normal bias).\n");
            if (exportAssetRoot) {
                printf("  Export root: %s\n", exportAssetRoot);
            }
            jjffe::SetDemoExportAsset(true);
        } else {
            printf("Starting 3D demo --ship %d.\n", shipDemoIndex);
            if (planetDemoIndex >= 0 && planetDemoIndex == shipDemoIndex)
                printf("  Planet model: 0x1F mesh interpreter path.\n");
            if (useExportAssets) {
                jjffe::SetDemoUseExport(true);
                printf("  --use-export: prefer export/ assets (production painter).\n");
            } else if (jjffe::DemoShipUsesExportAsset(shipDemoIndex)) {
                printf("  JJFFE_ASSET_SHIPS: render #%d from export/.\n",
                       shipDemoIndex);
            }
            if (exportAssetRoot) {
                printf("  Export root: %s\n", exportAssetRoot);
            }
        }
        printf("  A/D yaw, Q/E pitch, W/S zoom, P=spin, ESC=quit.\n");
        jjffe::SetDemoShipIndex(shipDemoIndex);
        jjffe::SetDemoDirectLaunch(true);
        jjffe::InitMeshInterpreterTable();
        jjffe::InitDemo3D();
        jjffe::GameLoopTransition(jjffe::GamePhase::Demo3D);
    } else if (startAtIntroCast) {
        printf("Starting ASM-timed intro cast (--intro).\n");
#if defined(JJFFE_USE_OGRE)
        printf("  Ogre path: soft cast on navy (same as JJFFE_OGRE_SHIP_MODE=sw).\n");
#elif defined(JJFFE_USE_D3D9)
        printf("  D3D path: legacy soft cast only (internal meshes; .x hybrid later).\n");
        printf("  Asset viewers: --ship N / --legacy-ship N / --export-ship N / "
               "--planet N.\n");
#endif
        jjffe::IntroCastSetViewerMode(true);
        jjffe::IntroCastInit();
        jjffe::GameLoopTransition(jjffe::GamePhase::IntroCast);
    } else if (startAtShipSelect) {
        printf("Starting at ship select (--demo).\n");
        jjffe::InitDemoShipSelect();
        jjffe::GameLoopTransition(jjffe::GamePhase::DemoShipSelect);
    } else if (startMission) {
        if (missionStartIndex >= 0) {
            printf("Starting mission directly (--mission --start %d).\n", missionStartIndex);
            jjffe::GameLoopSetMissionStart(missionStartIndex);
        } else {
            printf("Starting mission directly (--mission).\n");
        }
        jjffe::GameLoopTransition(jjffe::GamePhase::Mission);
    } else {
        printf("Splash: press \"y\" to accept, any other key exits.\n");
        printf("Intro: 1-5 pick a start (DATA_007199), any other key starts "
               "landed at Hope.\n");
#if defined(JJFFE_USE_OGRE)
        printf("Binary: ffelnxogre (Ogre-Next 4 / Vulkan via SDL2 WSI).\n");
        printf("Or: ./ffelnxogre --intro   for cast/credits on navy space.\n");
#elif defined(JJFFE_USE_D3D9)
        printf("Binary: ffelnxd3d (D3D9 via DXVK Native + FFED3D MODELS).\n");
        printf("Or: ./ffelnxd3d --demo | --ship 31 | --planet 129 | --sol | --intro\n");
#else
        printf("Or: ./ffelnxsdl --mission | --intro | --demo | --ship 31 | --planet 126\n");
#endif
        jjffe::IntroCastSetViewerMode(false);
        jjffe::IntroCastInit();
        jjffe::GameLoopTransition(jjffe::GamePhase::IntroCast);
    }

    while (jjffe::GameLoopGetPhase() != jjffe::GamePhase::Shutdown) {
#ifdef JJFFE_HAS_PLATFORM
        if (!jjffe::PlatformProcessEvents()) {
            jjffe::GameLoopTransition(jjffe::GamePhase::Shutdown);
            break;
        }
#endif
        if (!jjffe::GameLoopUpdate()) break;
    }

    jjffe::GameLoopShutdown();

    // --- Shutdown ---
    jjffe::GameShutdown();
    jjffe::TimerCleanup();
    jjffe::InputCleanup();
    jjffe::SoundCleanup();
    jjffe::DispatchPlatformSoundCleanup();
    jjffe::VideoCleanup();
#ifdef JJFFE_HAS_PLATFORM
    jjffe::PlatformTimerCleanup();
    jjffe::PlatformInputCleanup();
    jjffe::PlatformSoundCleanup();
    jjffe::PlatformVideoCleanup();
#endif
    jjffe::data::UnloadDataBlob();
    printf("Shutdown complete.\n");
    return 0;
}
