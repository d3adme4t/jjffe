#include "jjffe/intro_cast.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/platform.h"
#include "jjffe/draw_main.h"
#include "jjffe/draw.h"
#include "jjffe/sound.h"
#include "jjffe/math.h"
#include "jjffe/ship_model.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/planet_render.h"
#include "jjffe/planet_ab.h"
#include "jjffe/data_globals.h"
#include "jjffe/string.h"
#include "jjffe/job_trace.h"
#include "jjffe/planet_792_chain_trace.h"
#include "jjffe/planet_subdiv_trace.h"
#include "jjffe/planet_mid_scr_trace.h"
#include "jjffe/planet_mid_else_trace.h"
#include "jjffe/timer.h"
#include "jjffe/data.h"
#include "data_blob.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

namespace jjffe {

void FUNC_000959(int32_t param);
void FUNC_000961(void);
void FUNC_000962(void);
void FUNC_001682(intptr_t param);
void FUNC_001786(void);
void FUNC_001789(void);
void FUNC_000939(intptr_t p1, uint32_t* vec6, uint32_t flags);
void SetBackBufferPtr(uint8_t* ptr);
void SetTitleClock(uint32_t clock);
uint8_t* GetTitleObject();
void SetIntroProductionFlag(uint32_t v);
void EnsureHudTextDraw(void);

// ASM dword thresholds from FUNC_000962 (ffe.asm JUMP_004026…).
// Clock is dword [DATA_008804]; title starts at 0x400 after splash.
// Kept for scene gates (planet/station/interior/battle); credit text itself
// is painted by FUNC_000962 via blob string IDs.
namespace {

// Logo gate 0x795cc is owned by FUNC_000962 (no duplicate blit here).
constexpr uint32_t kCreditsStart  = 0x8276e;
constexpr uint32_t kStoryUntil    = 0x24fa42;
constexpr uint32_t kSndUntil      = 0x34e808;
constexpr uint32_t kInteriorAfter = 0x3bbba5;
constexpr uint32_t kInteriorUntil = 0x3f868c;
constexpr uint32_t kPlanetAfter   = 0x48a0b3;
constexpr uint32_t kVidPostAfter  = 0x588e79;
constexpr uint32_t kJournalUntil  = 0x67ba12;
constexpr uint32_t kEliteAfter    = 0x77a7d7;
constexpr uint32_t kLoopReset     = 0x7e7b76;

// Title types written by FUNC_000960 each frame (object+0x82 overlay).
constexpr int kTitleShipLogo    = 0xb1; // 177 — pre-credits
constexpr int kTitleShipCredits = 0xb0; // 176 — credits stretch

struct Star { int16_t x, y; uint8_t speed; uint8_t bright; };

uint32_t s_clock = 0x400;
// Cast pacing vs the original release: the analyst measured our Elite ending
// at ~2:25 (cast ≈143.5s at 999 ticks) where the original shows it at 2:43
// (cast ≈153s after its 10s boot logo). 999 * 143.5/153 ≈ 937 lands the
// ending at ~2:43. Override: INTRO_SPEED.
int s_ticksPerFrame = 937;
constexpr int kDefaultTicksPerFrame = 937;
// Intro player (INTRO_PLAYER=1): pause/seek/strip.
bool s_paused = false;
bool s_playerEnabled = false;
constexpr int kPlayerStripHeight = 80;
// ~2:43 cast ⇒ kLoopReset / 163 ≈ ticks per display-second for mm:ss.
constexpr uint32_t kCastLengthSec = 163;
// _asmmain splash (ffe.asm:68461–68538) before title clock 0x400.
// ASM waits for 'y'. Only the --intro viewer auto-advances after
// kSplashAutoFrames (~550 frames ≈ the original release's 10s GameTek boot
// segment so the cast starts where the original's does).
bool s_splashActive = true;
int s_splashFrames = 0;
constexpr int kSplashAutoFrames = 550;
// --intro viewer: splash auto-advance, ESC quits, speed/seek/HUD dig keys.
// Off = the default boot, which takes only the _asmmain key handling.
bool s_viewerMode = false;
// splashTimeout = 100 (ffe.asm 68543): Timer50Hz ticks during which
// GuiReadAllInput drops key presses (ffe.asm 85923).
constexpr uint32_t kSplashTimeoutMs = 100 * 20;
bool s_splashTimeoutActive = false;
uint32_t s_splashTimeoutEnd = 0;
int s_actionId = -1;
bool s_setPhaseOBands = false;
uint8_t s_prevKeys[256];
int s_inputPoll = 0;
// JJFFE_INTRO_KEYS="<poll>:<scancode>,..." — headless key presses, fed in
// as if the scancode had just gone down on that IntroCastHandleInput poll.
struct InjectedKey { int poll; int scancode; };
InjectedKey s_injectedKeys[16];
int s_injectedKeyCount = 0;
bool s_songStarted = false;
bool s_helpVisible = false;
bool s_shipAnim = true;
bool s_titleInited = false;
int s_shipVis = -1;
int s_shipType = -1;
int s_shipPolys = 0; // last ParseMeshBytecode polygon count (title 3D path)
bool s_usedStarFallback = false;
bool s_useBridgeFallback = true; // cleared once job paint is non-trivial
int s_realPaintPixels = 0;
bool s_suppressSoftPaint = false; // HW collect: update title state, skip RenderShipFlat
Star s_stars[80];
int32_t s_shipDist = 0x14820; // ASM DATA_007235[2] title ObjectStruct+0x36
Camera3D s_camera{};
const char* s_dumpFramePath = nullptr;
uint32_t s_dumpFrameClock = 0xffffffffu;
bool s_dumpFrameDone = false;

// One-shot back-buffer dump once the clock reaches the target (A/B vs ffeasm).
void MaybeDumpIntroFrame()
{
    if (s_dumpFrameDone || !s_dumpFramePath || !s_dumpFramePath[0]) return;
    if (s_clock < s_dumpFrameClock) return;
    // Match tools/ffe_intro_walk.gdb.tpl FlipScreen #5 quit:
    //   flip 1 often still at the pre-jump clock; flips 2–5 at target.
    // When INTRO_START_CLOCK already equals the dump clock, there is no
    // cold first flip — use JJFFE_DUMP_HOLD_FRAMES=4 (see mercury 808
    // capture) so job counts match ASM's four target-clock redraws.
    static int s_dumpHoldFrames = 0;
    int need = 5;
    if (const char* d = getenv("JJFFE_DUMP_HOLD_FRAMES")) {
        char* end = nullptr;
        long v = strtol(d, &end, 0);
        if (end != d && v > 0) need = (int)v;
    }
    if (++s_dumpHoldFrames < need) return;
    s_dumpFrameDone = true;
    if (!g_videoState) return;
    FILE* f = fopen(s_dumpFramePath, "wb");
    if (!f) {
        fprintf(stderr, "intro frame dump: cannot open %s\n", s_dumpFramePath);
        return;
    }
    fwrite(g_videoState->backBuffer, 1, SCREEN_WIDTH * SCREEN_HEIGHT, f);
    fclose(f);
    // Companion palette dump in the ASM's SDL_Color layout (pPalette[256] =
    // {r,g,b,pad}) so the frame can be compared in colour space: the dynamic
    // bank is allocated per frame by DPalBuild, so raw index bytes differ
    // between the port and ffeasm even when the picture matches.
    {
        char ppath[1024];
        snprintf(ppath, sizeof(ppath), "%s.pal", s_dumpFramePath);
        // Display palette (not g_videoState->palette, which stops at the static
        // 128 — the dynamic bank lives in the platform backend).
        PlatformDumpPalette(ppath);
    }
    // Optional DPal build-product dump (cell-diff vs the ffeasm tables at the
    // same flip): AB_DPAL_DUMP_DIR=<dir> writes sec/rev/rgb/chg/cnt.
    if (const char* dd = getenv("AB_DPAL_DUMP_DIR")) {
        if (dd[0] && g_videoState) {
            struct { const char* ext; const void* p; size_t n; } parts[] = {
                {"sec", g_videoState->dPalSecondary, sizeof(g_videoState->dPalSecondary)},
                {"rev", g_videoState->dPalReverse, sizeof(g_videoState->dPalReverse)},
                {"rgb", g_videoState->dPalRGB, sizeof(g_videoState->dPalRGB)},
                {"chg", g_videoState->dPalChanged, sizeof(g_videoState->dPalChanged)},
                // DATA_007770 (RGB444 -> slot+1, 0 = unmapped). FUNC_001326 only
                // records a mark when map[cell] == 0, so this table decides which
                // cells are reported at all — it is the thing to diff against
                // ffeasm (tools/ffe_dpal_map.gdb.tpl) when mark counts disagree.
                {"map", g_videoState->dPalMap, sizeof(g_videoState->dPalMap)},
            };
            for (const auto& part : parts) {
                char fpath[1200];
                snprintf(fpath, sizeof(fpath), "%s/cpp_%s.bin", dd, part.ext);
                if (FILE* pf = fopen(fpath, "wb")) {
                    fwrite(part.p, 1, part.n, pf);
                    fclose(pf);
                }
            }
            char cpath[1200];
            snprintf(cpath, sizeof(cpath), "%s/cpp_cnt.txt", dd);
            if (FILE* pf = fopen(cpath, "w")) {
                fprintf(pf, "%d\n", g_videoState->dPalChangedCount);
                fclose(pf);
            }
            fprintf(stderr, "DPAL-DUMP clock=0x%x -> %s\n", s_clock, dd);
        }
    }
    fprintf(stderr, "INTRO-DUMP clock=0x%x -> %s\n", s_clock, s_dumpFramePath);
    // Also flush the A/B sample tables at the same clock so the 869/270
    // cull/paint traces capture the target frame without a full-run exit.
    if (const char* cull869 = getenv("AB_1F_869_CULL_TRACE")) {
        if (cull869[0]) {
            char leg[512];
            snprintf(leg, sizeof(leg), "%s.legacy", cull869);
            PlanetAbDump869CullTrace(leg);
            fprintf(stderr, "INTRO-DUMP 869 -> %s\n", leg);
        }
    }
    // Subdiv samples live in RAM until flush — intro never hit planet_render's
    // close path, so AB_1F_SUBDIV_TRACE stayed 0-byte without this.
    if (const char* subdiv = getenv("AB_1F_SUBDIV_TRACE")) {
        if (subdiv[0]) {
            jjffe_planet_subdiv_trace_close();
            fprintf(stderr, "INTRO-DUMP subdiv -> %s\n", subdiv);
        }
    }
    if (const char* leaf = getenv("AB_1F_LEAF_TRACE")) {
        if (leaf[0]) {
            PlanetAbDumpLeafTrace(leaf);
            fprintf(stderr, "INTRO-DUMP leaf -> %s\n", leaf);
        }
    }
    if (const char* midScr = getenv("AB_1F_MID_SCR_TRACE")) {
        if (midScr[0]) {
            jjffe_planet_mid_scr_trace_close();
            fprintf(stderr, "INTRO-DUMP mid_scr -> %s\n", midScr);
        }
    }
    if (const char* midElse = getenv("AB_1F_MID_ELSE_TRACE")) {
        if (midElse[0]) {
            jjffe_planet_mid_else_trace_close();
            fprintf(stderr, "INTRO-DUMP mid_else -> %s\n", midElse);
        }
    }
}

// JJFFE_DEBUG_INTRO=1 — sampled per-frame title ship dump (headless gate).
void MaybeDumpIntroShip()
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    static uint32_t s_dumpFrame = 0;
    if ((++s_dumpFrame % 15u) != 1u) return;
    fprintf(stderr, "INTRO-SHIP type=%d polys=%d vis=%d fallback=%d\n",
            s_shipType, s_shipPolys, s_shipVis, s_usedStarFallback ? 1 : 0);
}

// ASM _asmmain splash lines (pSplash1–pSplash9) via TextWriteDefaultColor.
struct SplashLine {
    int x;
    int y;
    const char* text;
};

constexpr SplashLine kSplashLines[] = {
    {123,  30, "JJFFE version 2.8a7"},
    { 75,  50, "Original game copyright Frontier Developments"},
    { 67,  60, "This version recompiled and modified by John Jordan"},
    { 66,  80, "This is a replacement executable not a complete game"},
    { 54,  90, "Visit http://jaj22.org.uk/jjffe/ for more information"},
    { 59, 100, "Visit http://www.eliteclub.co.uk/ to get shareware FFE"},
    { 59, 120, "This is a reverse engineered version of a commercial game"},
    { 72, 130, "It is illegal to own it even if you own the original"},
    { 68, 140, "Press \"y\" if you accept this or any other key to exit"},
};

void MaybeDumpIntroSplash()
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    static uint32_t s_splashDumpFrame = 0;
    if ((++s_splashDumpFrame % 15u) != 1u) return;
    fprintf(stderr, "INTRO-SPLASH clock=0x%x\n", s_clock);
}

void MaybeDumpIntroTitleOnce()
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    static bool s_titleDumped = false;
    if (s_titleDumped) return;
    s_titleDumped = true;
    fprintf(stderr, "INTRO-TITLE clock=0x%x\n", s_clock);
}

void EndIntroSplash()
{
    if (!s_splashActive) return;
    s_splashActive = false;
    s_clock = 0x400;
    SetTitleClock(s_clock);
    MaybeDumpIntroTitleOnce();
}

// JJFFE_DUMP_SPLASH_FRAME=<path>: one-shot splash back buffer + <path>.pal
// (A/B vs captures/ffe_flow_splash.raw).
void MaybeDumpSplashFrame()
{
    static bool s_done = false;
    if (s_done || !g_videoState) return;
    const char* path = getenv("JJFFE_DUMP_SPLASH_FRAME");
    if (!path || !path[0]) return;
    s_done = true;
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "splash frame dump: cannot open %s\n", path);
        return;
    }
    fwrite(g_videoState->backBuffer, 1, SCREEN_WIDTH * SCREEN_HEIGHT, f);
    fclose(f);
    char ppath[1024];
    snprintf(ppath, sizeof(ppath), "%s.pal", path);
    PlatformDumpPalette(ppath);
    fprintf(stderr, "SPLASH-DUMP -> %s\n", path);
}

void DrawIntroSplash(uint8_t* buf)
{
    SetBackBufferPtr(buf);
    for (const auto& line : kSplashLines) {
        TextWriteDefaultColor(reinterpret_cast<const uint8_t*>(line.text),
                              line.x, line.y, 0x0f);
    }
    MaybeDumpIntroSplash();
    MaybeDumpSplashFrame();
}

// Background bodies (additive; title #176 Sprite749 is ships/asteroids only).
// Logo phase (#177) has no body — gate only after kCreditsStart.
constexpr int kIntroPlanetMercury = 120;
constexpr int kIntroPlanetEarth = 129;
// Coriolis family (OBJECT_CATEGORY 72–80). Title orbit card #355 nests #73 —
// that child is the station-shaped mesh that painted incomplete (~110 polys /
// 30 Sprite749 stubs) without KeepAll. Full expand → ~365 hull polys.
constexpr int kIntroStationModel = 73;
// Dock / city interiors (OBJECT_CATEGORY_INVENTORY + FFED3D Models/):
//   #227 docking-bay entrance, #233 final room, #271 city/dock piece.
// FUNC_000962 only paints the "Ship Interiors" credit text in
// [kInteriorAfter, kInteriorUntil); the enclosed bay view shares that window
// (reference 1:46–1:53). Prefer #233 (largest enclosed bay room).
constexpr int kIntroInteriorModel = 233;
constexpr int kIntroInteriorEntranceModel = 227;
// Reference cast shifts Mercury→Earth at ~0:54. FUNC_000962 has no body-id
// gate; the Music slide ends at kMusicUntil (0x2740cc ≈0:51 on the
// 0x400→kLoopReset≈2:45 map) — closest match, but the 45s headless dig ends
// ~one dump shy. Use kStoryUntil (0x24fa42 ≈0:48), the prior documented
// slide-end in the same late-staff cluster (Story overlaps Music). kSndAfter
// (≈0:59) is the next slide. Station approach (reference 1:25–1:35) is gated
// at kSndUntil (0x34e808): end of the overlapping Sound Drivers credit, and
// the last documented slide-end the 60s headless dig reliably crosses after
// Earth has been visible. Prefer kIntroUntil/kInteriorUntil when the dig
// horizon moves (those sit closer to the video window).
constexpr uint32_t kEarthAfter = kStoryUntil; // 0x24fa42
constexpr uint32_t kStationAfter = kSndUntil; // 0x34e808
// Combat / destruction (reference 2:05–2:36). Lasers already paint from title
// fleet PlainLines near the end of Ship Interiors; the missing piece is the
// hit → FUNC_000939 explosion (type 0x29) + debris path. Gate the battle
// window from interior exit through Journal so headless digs that reach
// interior also reach destruction FX.
constexpr uint32_t kBattleAfter = kInteriorUntil;   // 0x3f868c
constexpr uint32_t kDebrisPhaseAfter = kPlanetAfter; // 0x48a0b3 — hull break
constexpr uint32_t kBigBoomAfter = kVidPostAfter;    // 0x588e79 — large spheres
constexpr uint32_t kBattleUntil = kJournalUntil;     // 0x67ba12
// Debris/log meshes (OBJECT_CATEGORY_INVENTORY 106–111).
constexpr int kIntroDebrisModels[] = {106, 107, 108, 109};
constexpr int kIntroDebrisModelCount =
    static_cast<int>(sizeof(kIntroDebrisModels) / sizeof(kIntroDebrisModels[0]));
// ASM title ObjectStruct+0x9c spins the constellation ships; the planet body
// keeps a fixed orientation (mesh mode-2 rocky shade). Do not feed live title yaw.
constexpr int32_t kIntroPlanetYaw = 0;
constexpr int32_t kIntroPlanetPitch = 0;
// A/B calibration knobs vs the ffeasm ground-truth frames (see INTRO_CAST.md
// "ffeasm ground-truth frame walk"): override the planet pose per run.
//   INTRO_PLANET_DIST_SCALE=0.5  — multiply the autofit distance (closer = bigger)
//   INTRO_PLANET_YAW / _PITCH    — override the fixed pose
int32_t IntroPlanetYawForPhase()
{
    if (const char* e = getenv("INTRO_PLANET_YAW")) {
        char* end = nullptr;
        long v = strtol(e, &end, 0);
        if (end != e)
            return static_cast<int32_t>(v);
    }
    return kIntroPlanetYaw;
}
int32_t IntroPlanetPitchForPhase()
{
    if (const char* e = getenv("INTRO_PLANET_PITCH")) {
        char* end = nullptr;
        long v = strtol(e, &end, 0);
        if (end != e)
            return static_cast<int32_t>(v);
    }
    return kIntroPlanetPitch;
}
int32_t IntroPlanetDistForPhase(int32_t autofitDist)
{
    if (const char* e = getenv("INTRO_PLANET_DIST")) {
        char* end = nullptr;
        long v = strtol(e, &end, 0);
        if (end != e && v > 0)
            return static_cast<int32_t>(v);
    }
    if (const char* e = getenv("INTRO_PLANET_DIST_SCALE")) {
        char* end = nullptr;
        double s = strtod(e, &end);
        if (end != e && s > 0)
            return static_cast<int32_t>(static_cast<double>(autofitDist) * s);
    }
    return autofitDist;
}
constexpr int32_t kIntroStationYaw = 0x1000;
constexpr int32_t kIntroStationPitch = 0x0400;
constexpr int32_t kIntroInteriorYaw = 0x0800;
constexpr int32_t kIntroInteriorPitch = 0x0200;

int s_stationModel = -1;
int s_stationPolys = 0;
int s_stationVis = 0;
int32_t s_stationDrawDist = 0; // exterior autofit Z used by TryDrawShipMesh
int32_t s_stationMinZ = 0;     // min camera-space Z across projected verts
int32_t s_stationMaxZ = 0;
uint32_t s_stationRadius = 0;
uint32_t s_stationScaleRelated = 0;
int s_interiorModel = -1;
int s_interiorPolys = 0;
int s_interiorVis = 0;
bool s_interiorWasActive = false;

// Intro-local combat FX (bridge). FUNC_000939 allocates type-0x29 effect
// objects into the stub pool; the intro soft path does not run the full
// object renderer, so expanding spheres + debris meshes are painted here
// with the existing DrawCircle / ParseMesh→RenderShipFlat paths.
constexpr int kMaxIntroExplosions = 8;
constexpr int kMaxIntroDebris = 12;
struct IntroExplosionFx {
    int32_t pos[3];
    int radiusPx;
    int maxRadiusPx;
    int lifetime;
    bool active;
    bool big;
};
struct IntroDebrisFx {
    int modelId;
    int32_t pos[3];
    int32_t vel[3];
    int32_t yaw;
    int lifetime;
    bool active;
};
IntroExplosionFx s_introExplosions[kMaxIntroExplosions]{};
IntroDebrisFx s_introDebris[kMaxIntroDebris]{};
int s_introHitFlashFrames = 0;
int32_t s_introHitFlashPos[3]{};
uint32_t s_introLastHitClock = 0;
int s_introCombatHits = 0;
int s_introDebrisSpawnedTotal = 0;

// Combat orchestration (battle window): look-at track + target evade state.
constexpr int kIntroCombatShooterId = 34; // Viper
constexpr int kIntroCombatTargetId = 31;  // Saker
int32_t s_introTrack[3]{};               // INTRO-CAMERA track= look-at point
int32_t s_combatShooterPos[3]{};
int32_t s_combatTargetPos[3]{};
int32_t s_combatTargetYaw = 0;
int32_t s_combatTargetPitch = 0;
int32_t s_combatTargetVel[3]{};          // base path + evade jink
int32_t s_evadeYaw = 0;
int32_t s_evadePitch = 0;
int32_t s_evadeVel[3]{};
int32_t s_evadePos[3]{};                 // integrated jink offset (decays to 0)
uint32_t s_evadeTickClock = 0xffffffffu; // once-per-clock tick gate

int IntroBackgroundPlanetModel()
{
    return (s_clock >= kEarthAfter) ? kIntroPlanetEarth : kIntroPlanetMercury;
}

// Station scene window. The upper bound matters: past kPlanetAfter the walk
// draws the planet and ffeasm's job trace executes no FUNC_001805/001807 face
// jobs at all (ACCT at 0x48a1ef: entries=16 backface=8 fallback=1 marks=0), so
// an unbounded station pass parks the station on screen for the rest of the
// cast — the "the same scene keeps repeating" report. With the bound the port's
// ink mask at 0x48a1ef is identical to ffeasm's (IoU 1.000).
// INTRO_STATION_OPEN_END=1 restores the old unbounded behaviour for A/B.
bool IntroStationPhaseActive()
{
    if (const char* e = getenv("INTRO_STATION_OPEN_END"))
        if (e[0] == '1')
            return s_clock >= kStationAfter;
    return s_clock >= kStationAfter && s_clock < kPlanetAfter;
}

// Shares FUNC_000962 "Ship Interiors" credit window (kInteriorAfter..Until).
bool IntroInteriorPhaseActive()
{
    return s_clock >= kInteriorAfter && s_clock < kInteriorUntil;
}

// Combat destruction window (reference 2:05–2:36): after interior exit.
bool IntroBattlePhaseActive()
{
    return s_clock >= kBattleAfter && s_clock < kBattleUntil;
}

bool IsIntroStationModel(int modelId)
{
    return modelId >= 72 && modelId <= 80;
}

bool IsIntroInteriorModel(int modelId)
{
    return modelId == 227 || modelId == 233 || modelId == 271;
}

bool IsIntroDebrisModel(int modelId)
{
    return modelId >= 106 && modelId <= 111;
}

void MaybeDumpIntroPlanet(int modelId, int pixels, int disc, int filled)
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    // Caller already gates to every 15th credits frame.
    const double fill = (disc > 0) ? (static_cast<double>(filled) / disc) : 0.0;
    fprintf(stderr,
            "INTRO-PLANET model=%d clock=0x%x yaw=0x%x pass=background beforeShips=1 "
            "pixels=%d disc=%d filled=%d fill=%.3f\n",
            modelId, s_clock, (unsigned)(kIntroPlanetYaw & 0xffff),
            pixels, disc, filled, fill);
}

// Count non-clear pixels written by the planet draw inside an approximate disc.
static void MeasureIntroPlanetFill(const uint8_t* before, int* pixelsOut,
                                   int* discOut, int* filledOut)
{
    *pixelsOut = *discOut = *filledOut = 0;
    if (!g_videoState || !before) return;
    const uint8_t* buf = g_videoState->backBuffer;
    int64_t sx = 0, sy = 0;
    int x0 = 319, y0 = 199, x1 = 0, y1 = 0;
    int changed = 0;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            const int i = y * SCREEN_WIDTH + x;
            if (buf[i] == before[i]) continue;
            ++changed;
            sx += x;
            sy += y;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }
    *pixelsOut = changed;
    if (changed == 0) return;
    const double cx = static_cast<double>(sx) / changed;
    const double cy = static_cast<double>(sy) / changed;
    const double rx = (x1 - x0) * 0.5;
    const double ry = (y1 - y0) * 0.5;
    const double r = (rx > ry ? rx : ry);
    if (r < 1.0) return;
    const double rInner = r * 0.85;
    const double r2 = rInner * rInner;
    int disc = 0, filled = 0;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            if (dx * dx + dy * dy > r2) continue;
            ++disc;
            const int i = y * SCREEN_WIDTH + x;
            if (buf[i] != before[i]) ++filled;
        }
    }
    *discOut = disc;
    *filledOut = filled;
}

// Enqueue Mercury (pre-kEarthAfter) or Earth (from kEarthAfter) into the current
// FUNC_001786 job frame (before title paint). Returns true if FUNC_001682 was
// called. Caller must keep s_introPlanetObj live until FUNC_001789. Title
// 0xb0/0xb1 paint via the host hook writes the back buffer immediately, so
// intro flushes planet jobs before FUNC_000961 so ships overwrite the body.
constexpr size_t kIntroPlanetObjSize = 0x152;
alignas(16) static uint8_t s_introPlanetObj[kIntroPlanetObjSize];

static bool IntroPlanetBackgroundActive();

bool EnqueueIntroPlanetBackground()
{
    // Dig-only host body (INTRO_PLANET=1). Default OFF — a centered icosa does
    // not match the title Contour ring (correct place + rotation). Earth looks
    // OK from ClearUpperBuf fill + dense Contour (0x121…); Mercury still only
    // gets a thin Contour ring until the title 0x1F leaf / band gap is fixed.
    if (!IntroPlanetBackgroundActive())
        return false;

    const int modelId = IntroBackgroundPlanetModel();
    const ShipModel* model = GetShipModel(modelId);
    if (!model || !IsPlanetBodyModel(modelId)) return false;

    const int du = PlanetBodyFitDistUnits(model->radius ? model->radius : 64,
                                          model->scaleRelated);
    const int32_t dist =
        IntroPlanetDistForPhase(static_cast<int32_t>(du * (FP_ONE / 2)));

    EnqueuePlanetInterpreter(s_introPlanetObj, modelId,
                             IntroPlanetYawForPhase(),
                             IntroPlanetPitchForPhase(), dist);
    return true;
}

// Logging / gate — never enqueues by itself.
static bool IntroPlanetBackgroundActive()
{
    // Host Mercury disc is OFF by default on this branch: it parks a flat grey
    // icosa at screen center while the title Contour ring (correct place,
    // rotating) lives elsewhere. Earth needs no host — ClearUpperBuf fill
    // (pal 152) + dense Contour (drawTex 0x121…) already read as a body.
    // Dig: INTRO_PLANET=1 re-enables host (Mercury, or Earth too).
    if (!getenv("INTRO_PLANET") || getenv("INTRO_PLANET")[0] != '1')
        return false;
    if (getenv("INTRO_PLANET_OFF") && getenv("INTRO_PLANET_OFF")[0] == '1')
        return false;
    if (s_clock < kCreditsStart) return false;
    if (IntroStationPhaseActive()) return false;
    if (s_suppressSoftPaint) return false;

    const int modelId = IntroBackgroundPlanetModel();
    const ShipModel* model = GetShipModel(modelId);
    return model && IsPlanetBodyModel(modelId);
}

bool TryDrawShipMesh(int shipIndex, int32_t yaw, int32_t pitch, bool titleGates);
bool TryDrawShipMeshAt(int shipIndex, int32_t yaw, int32_t pitch,
                       const int32_t worldPos[3], bool titleGates);

void MaybeDumpIntroStation()
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    static uint32_t s_stationSnapFrame = 0;
    if ((++s_stationSnapFrame % 15u) != 1u) return;
    fprintf(stderr,
            "INTRO-STATION model=%d clock=0x%x polys=%d vis=%d "
            "dist=%d radius=%u sr=%u minZ=%d maxZ=%d "
            "cam=%d,%d,%d pass=background beforeShips=1\n",
            s_stationModel, s_clock, s_stationPolys, s_stationVis,
            (int)s_stationDrawDist, s_stationRadius, s_stationScaleRelated,
            (int)s_stationMinZ, (int)s_stationMaxZ,
            (int)s_camera.position.x, (int)s_camera.position.y,
            (int)s_camera.position.z);
}

// Scrub mesh-parse / ALU globals left by the close-up interior expand so the
// exterior station (#73) never inherits near-plane distance or BitClear state.
static void RestoreIntroStationExteriorState()
{
    SetMeshParseGameObj(nullptr);
    SetMeshParseKeepAllSpriteChildren(false);
    SetMeshParseForceBitClearEnter(false);
    SetMeshParseSuppressStationChildren(false);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
}

void MaybeDumpIntroInteriorTransition()
{
    const bool inside = IntroInteriorPhaseActive();
    if (!inside && s_interiorWasActive)
        RestoreIntroStationExteriorState();
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (e && e[0] == '1') {
        if (inside && !s_interiorWasActive) {
            fprintf(stderr,
                    "INTRO-INTERIOR enter clock=0x%x model=%d polys=%d vis=%d "
                    "cam=%d,%d,%d\n",
                    s_clock, s_interiorModel, s_interiorPolys, s_interiorVis,
                    (int)s_camera.position.x, (int)s_camera.position.y,
                    (int)s_camera.position.z);
        } else if (!inside && s_interiorWasActive) {
            fprintf(stderr,
                    "INTRO-INTERIOR exit clock=0x%x model=%d "
                    "cam=%d,%d,%d\n",
                    s_clock, s_interiorModel,
                    (int)s_camera.position.x, (int)s_camera.position.y,
                    (int)s_camera.position.z);
        }
    }
    s_interiorWasActive = inside;
}

// Station meshes (72–80) are Sprite749 trees: without KeepAll, title-card
// station #73 stays at ~110 polys / 30 stubs; with KeepAll+BitClear enter the
// full hull (~365) lands. Same path as title ships — ParseMesh → RenderShipFlat.
// During [kInteriorAfter, kInteriorUntil) swap in dock interior #233 (bay room)
// so the view reads as the enclosed docking bay, then restore #73 after exit.
bool DrawIntroStationBackground()
{
    if (!IntroStationPhaseActive()) return false;
    if (s_suppressSoftPaint) return false;

    const bool interior = IntroInteriorPhaseActive();
    // Leaving the bay: scrub parse/ALU before exterior #73 expand so a leaked
    // near-plane distance cannot produce screen-spanning grey polys.
    if (!interior && s_interiorWasActive)
        RestoreIntroStationExteriorState();

    int modelId = interior ? kIntroInteriorModel : kIntroStationModel;
    const ShipModel* model = GetShipModel(modelId);
    if (!model || !model->meshDataOffset) {
        if (!interior) return false;
        modelId = kIntroInteriorEntranceModel;
        model = GetShipModel(modelId);
        if (!model || !model->meshDataOffset) return false;
    }

    const int32_t savedDist = s_shipDist;
    const int savedPolys = s_shipPolys;
    const int savedVis = s_shipVis;
    const int savedType = s_shipType;

    // Frame the exterior station so the full hull stays in front of the near
    // plane across the title tracking orbit:
    //   autofit (180px) for on-screen size, then max with
    //   hullHalf + orbitPad + nearMargin so cam translation cannot sink
    //   vertices behind the camera. Snapshot radius/sr before ParseMesh —
    //   Sprite749 child expand can clobber live ObjectStruct fields.
    const uint32_t fitRadius = model->radius ? model->radius : 64;
    uint32_t fitScaleRelated = model->scaleRelated;
    if (fitScaleRelated > 24)
        fitScaleRelated = 24;
    const int du = AbAutoFitDistUnits(fitRadius, fitScaleRelated, 180,
                                      kAbAutoZoomMaxDistUnitsBody);
    int64_t autofitDist = static_cast<int64_t>(du) * (FP_ONE / 2);
    const int64_t hullHalf = static_cast<int64_t>(fitRadius) << fitScaleRelated;
    // Orbit envelope from UpdateIntroCameraFromTitle (radius=oz/16, station
    // accents oz/32 + radius/4 on Y). Pad covers |cam| across the phase.
    const int32_t oz = DATA_007235[2] != 0 ? DATA_007235[2] : 0x14820;
    const int64_t orbitPad =
        static_cast<int64_t>(oz / 8) + (oz / 16) + (oz / 32) + 0x1000;
    int64_t safeDist = hullHalf + orbitPad + ASM_PROJ_NEAR + 0x1000;
    int64_t exterior64 = autofitDist > safeDist ? autofitDist : safeDist;
    // Autofit assumes cam at origin; push by orbit so the margin holds when
    // the tracking camera translates toward the hull.
    exterior64 += orbitPad;
    if (exterior64 > 0x7fffffff)
        exterior64 = 0x7fffffff;
    if (exterior64 < 1)
        exterior64 = 1;
    const int32_t exteriorDist = static_cast<int32_t>(exterior64);
    s_shipDist = interior ? static_cast<int32_t>(du * (FP_ONE / 4))
                          : exteriorDist;

    const int32_t yaw = interior ? kIntroInteriorYaw : kIntroStationYaw;
    const int32_t pitch = interior ? kIntroInteriorPitch : kIntroStationPitch;
    bool ok = TryDrawShipMesh(modelId, yaw, pitch, /*titleGates=*/false);
    if (!ok && interior && modelId != kIntroInteriorEntranceModel) {
        const ShipModel* ent = GetShipModel(kIntroInteriorEntranceModel);
        if (ent && ent->meshDataOffset) {
            const int du2 = AbAutoFitDistUnits(ent->radius ? ent->radius : 64,
                                               ent->scaleRelated, 180);
            s_shipDist = du2 * (FP_ONE / 4);
            ok = TryDrawShipMesh(kIntroInteriorEntranceModel, yaw, pitch, false);
            if (ok)
                modelId = kIntroInteriorEntranceModel;
        }
    }
    if (interior) {
        s_interiorModel = modelId;
        s_interiorPolys = s_shipPolys;
        s_interiorVis = s_shipVis;
    } else {
        s_stationModel = modelId;
        s_stationPolys = s_shipPolys;
        s_stationVis = s_shipVis;
        s_stationDrawDist = exteriorDist;
        s_stationRadius = fitRadius;
        s_stationScaleRelated = fitScaleRelated;
        if (ok)
            MaybeDumpIntroStation();
    }

    s_shipDist = savedDist;
    s_shipPolys = savedPolys;
    s_shipVis = savedVis;
    s_shipType = savedType;
    return ok;
}

void MaybeDumpIntroPlanetAfterFlush(const uint8_t* before, bool snapThisFrame)
{
    if (!snapThisFrame) return;
    int pixels = 0, disc = 0, filled = 0;
    MeasureIntroPlanetFill(before, &pixels, &disc, &filled);
    MaybeDumpIntroPlanet(IntroBackgroundPlanetModel(), pixels, disc, filled);
}

// ASM title yaw (FUNC_000961): DATA_009129 = clock>>10 (pre-credits) or
// (clock - 0x8276e)>>10 (credits). Same rule whether or not 000961 has run yet.
int32_t TitleYawFromClock(uint32_t clock)
{
    if (clock < kCreditsStart)
        return static_cast<int32_t>(clock >> 10);
    return static_cast<int32_t>((clock - kCreditsStart) >> 10);
}

void MaybeDumpIntroCamera()
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    static uint32_t s_camSnapFrame = 0;
    if ((++s_camSnapFrame % 15u) != 1u) return;
    const int32_t* m = s_camera.rotation.m;
    // View dir ≈ third column of the FP16 view matrix (world +Z through cam).
    fprintf(stderr,
            "INTRO-CAMERA clock=0x%x pos=%d,%d,%d rot=<%d,%d,%d,%d,%d,%d,%d,%d,%d> "
            "view=%d,%d,%d track=%d,%d,%d\n",
            s_clock,
            (int)s_camera.position.x, (int)s_camera.position.y, (int)s_camera.position.z,
            (int)m[0], (int)m[1], (int)m[2], (int)m[3], (int)m[4],
            (int)m[5], (int)m[6], (int)m[7], (int)m[8],
            (int)m[2], (int)m[5], (int)m[8],
            (int)s_introTrack[0], (int)s_introTrack[1], (int)s_introTrack[2]);
}

// World-space dogfight locus (title fleet depth). Not camera-relative — a
// cam-relative track would feedback when the orbit biases toward itself.
static void ComputeIntroCombatBasePositions(int32_t shooterOut[3],
                                            int32_t targetOut[3],
                                            int32_t* baseYawOut,
                                            int32_t baseVelOut[3])
{
    const int32_t yaw = TitleYawFromClock(s_clock);
    const int32_t sn = Sin32(static_cast<uint32_t>(yaw)) >> 15;
    const int32_t cs = Sin32(static_cast<uint32_t>(yaw + 0x4000)) >> 15;
    const int32_t orbit = (s_shipDist != 0) ? (s_shipDist / 4) : 0x5000;
    const int32_t depth = (s_shipDist != 0) ? (s_shipDist / 5) : 0x5000;
    shooterOut[0] = static_cast<int32_t>(((int64_t)orbit * sn) >> 16);
    shooterOut[1] = 0;
    shooterOut[2] = depth;
    targetOut[0] = static_cast<int32_t>(((int64_t)orbit * (-sn)) >> 16);
    targetOut[1] = 0x20;
    targetOut[2] = depth + 0x200;
    if (baseYawOut)
        *baseYawOut = yaw;
    if (baseVelOut) {
        // Parent velocity ≈ yaw tangent (debris / base flight path).
        baseVelOut[0] = sn >> 4;
        baseVelOut[1] = 0x40;
        baseVelOut[2] = (-cs) >> 4;
    }
}

static void TickIntroEvadeOnce()
{
    // UpdateIntroCameraFromTitle can run twice per frame; tick once per clock.
    if (s_evadeTickClock == s_clock)
        return;
    s_evadeTickClock = s_clock;

    if (!IntroBattlePhaseActive()) {
        s_evadeYaw = s_evadePitch = 0;
        s_evadeVel[0] = s_evadeVel[1] = s_evadeVel[2] = 0;
        s_evadePos[0] = s_evadePos[1] = s_evadePos[2] = 0;
        return;
    }

    // Integrate jink, then spring the offset back toward the base path.
    for (int i = 0; i < 3; i++) {
        s_evadePos[i] += s_evadeVel[i] >> 8;
        s_evadePos[i] = (s_evadePos[i] * 15) / 16;
        s_evadeVel[i] = (s_evadeVel[i] * 14) / 16;
    }
    s_evadeYaw = (s_evadeYaw * 14) / 16;
    s_evadePitch = (s_evadePitch * 14) / 16;
}

static void RefreshIntroCombatantPose()
{
    int32_t baseYaw = 0;
    int32_t baseVel[3]{};
    int32_t shooter[3]{};
    int32_t target[3]{};
    ComputeIntroCombatBasePositions(shooter, target, &baseYaw, baseVel);

    s_combatShooterPos[0] = shooter[0];
    s_combatShooterPos[1] = shooter[1];
    s_combatShooterPos[2] = shooter[2];

    s_combatTargetPos[0] = target[0] + s_evadePos[0];
    s_combatTargetPos[1] = target[1] + s_evadePos[1];
    s_combatTargetPos[2] = target[2] + s_evadePos[2];
    s_combatTargetYaw = baseYaw + s_evadeYaw;
    s_combatTargetPitch = s_evadePitch;
    s_combatTargetVel[0] = baseVel[0] + s_evadeVel[0];
    s_combatTargetVel[1] = baseVel[1] + s_evadeVel[1];
    s_combatTargetVel[2] = baseVel[2] + s_evadeVel[2];

    if (IntroBattlePhaseActive()) {
        // Prefer the impacted vessel after the first hit; else dogfight mid.
        if (s_introCombatHits > 0) {
            s_introTrack[0] = s_combatTargetPos[0];
            s_introTrack[1] = s_combatTargetPos[1];
            s_introTrack[2] = s_combatTargetPos[2];
        } else {
            s_introTrack[0] = (s_combatShooterPos[0] + s_combatTargetPos[0]) / 2;
            s_introTrack[1] = (s_combatShooterPos[1] + s_combatTargetPos[1]) / 2;
            s_introTrack[2] = (s_combatShooterPos[2] + s_combatTargetPos[2]) / 2;
        }
    } else {
        // Passive orbit look-at (origin / constellation center).
        s_introTrack[0] = 0;
        s_introTrack[1] = 0;
        s_introTrack[2] = 0;
    }
}

static void ApplyIntroHitEvade()
{
    const int32_t yaw = TitleYawFromClock(s_clock);
    const int32_t sn = Sin32(static_cast<uint32_t>(yaw)) >> 15;
    const int32_t cs = Sin32(static_cast<uint32_t>(yaw + 0x4000)) >> 15;
    const int sign = (s_introCombatHits & 1) ? 1 : -1;
    // Visible jink: yaw/pitch kick + velocity impulse, then TickIntroEvadeOnce
    // decays back to the base path (no teleport).
    s_evadeYaw += sign * 0x0c00;
    s_evadePitch += sign * 0x0600;
    s_evadeVel[0] += (sn >> 2) * sign;
    s_evadeVel[1] += 0x280;
    s_evadeVel[2] += ((-cs) >> 2) * sign;
}

// Root cause (ASM title path):
// - FUNC_000960 memcpy(DATA_007234 → DATA_008831, 24) is the *shade* ramp, not a
//   camera matrix; DATA_007231..233 → DATA_008821..823 are the light vector.
// - Motion is the title ObjectStruct (DATA_009127): yaw at +0x9c (DATA_009129 =
//   clock>>10) and position from DATA_007235 at +0x26/+0x2e/+0x36. ASM keeps the
//   viewer near the origin and spins the object; soft ships already apply that
//   yaw to shipRot. Drive Camera3D from the same yaw so station/ship draws via
//   TransformShipVertices get a tracking orbit (do NOT put yaw on the view
//   matrix — that would cancel shipRot(yaw) and freeze the constellation).
void UpdateIntroCameraFromTitle()
{
    // Yaw follows FUNC_000961's clock rule (ObjectStruct+0x9c / DATA_009129).
    // Prefer the live field once 000961 has written it; otherwise derive from clock.
    uint8_t* obj = GetTitleObject();
    int32_t yaw = TitleYawFromClock(s_clock);
    int32_t ox = DATA_007235[0];
    int32_t oy = DATA_007235[1];
    int32_t oz = DATA_007235[2];
    if (obj) {
        yaw = static_cast<int32_t>(*reinterpret_cast<uint16_t*>(obj + 0x9c));
        if (yaw == 0)
            yaw = TitleYawFromClock(s_clock);
        ox = *reinterpret_cast<int32_t*>(obj + 0x26);
        oy = *reinterpret_cast<int32_t*>(obj + 0x2e);
        oz = *reinterpret_cast<int32_t*>(obj + 0x36);
        if (oz == 0)
            oz = DATA_007235[2];
    }

    // ASM-faithful viewer: fixed at origin, identity view. Contour flecks at
    // journal/Video-Actors clocks (0x6f0000–0x70c000) match ffeasm only with
    // this — the invent tracking orbit + perpetual Earth flyover pitch culls
    // the 0x8019 band silhouette while ASM (fixed cam + spinning object) still
    // paints the shade ramp (VGA 186–188). Dig: INTRO_ASM_CAMERA=0 restores
    // the old parallax orbit (station approach accent).
    static const bool s_asmCamera = []() {
        const char* e = getenv("INTRO_ASM_CAMERA");
        // Default ON — parity with ffeasm fixed viewer (INTRO_CAST.md).
        if (!e) return true;
        return e[0] != '0';
    }();
    if (s_asmCamera) {
        TickIntroEvadeOnce();
        RefreshIntroCombatantPose();
        CameraSetPosition(&s_camera, 0, 0, 0);
        // Identity view at soft FP16 scale (same path as the orbit branch).
        Mat33 view{};
        MatBuildYZT(&view, 0, 0);
        MatScaleSin32ToFp16(&view);
        for (int i = 0; i < 9; ++i)
            s_camera.rotation.m[i] = view.m[i];
        (void)ox;
        (void)oy;
        (void)oz;
        (void)yaw;
        return;
    }

    int32_t camX = 0;
    int32_t camY = 0;
    int32_t camZ = 0;
    int32_t pitch = 0;
    int32_t viewYaw = yaw >> 5; // ~1/32 of DATA_009129

    // Docking-bay interior (FUNC_000962 Ship Interiors window): jump the
    // viewer inside the bay — near-origin first-person with a tight yaw sway.
    // Exterior tracking orbit resumes after kInteriorUntil (indoor→outdoor).
    if (IntroInteriorPhaseActive()) {
        const int32_t sn = Sin32(static_cast<uint32_t>(yaw)) >> 15;
        const int32_t cs = Sin32(static_cast<uint32_t>(yaw + 0x4000)) >> 15;
        // Small FP offsets (<< exterior radius~oz/16) so INTRO-CAMERA shows
        // a clear jump inside, then back out after the phase.
        camX = static_cast<int32_t>(((int64_t)0x80 * sn) >> 16);
        camY = 0x20;
        camZ = static_cast<int32_t>(((int64_t)0x40 * (FP_ONE - cs)) >> 17);
        pitch = kIntroInteriorPitch;
        viewYaw = yaw >> 4; // slightly tighter look-around inside
        TickIntroEvadeOnce();
        RefreshIntroCombatantPose(); // track stays at orbit center
    } else {
        // Tracking orbit in the XZ plane from title yaw; radius << title Z so the
        // constellation and station stay on-screen (parallax, not a full fly-away).
        const int32_t radius = (oz != 0) ? (oz / 16) : (DATA_007235[2] / 16);
        const int32_t sn = Sin32(static_cast<uint32_t>(yaw)) >> 15; // ~±FP_ONE
        const int32_t cs = Sin32(static_cast<uint32_t>(yaw + 0x4000)) >> 15;
        camX = static_cast<int32_t>(((int64_t)radius * sn) >> 16) + (ox / 8);
        camY = oy / 8;
        camZ = static_cast<int32_t>(((int64_t)radius * (FP_ONE - cs)) >> 17);

        // Phase accents (same clock gates as Mercury→Earth→station).
        if (IntroStationPhaseActive()) {
            camZ += oz / 32;
            camY -= radius / 4;
            pitch = 0x0400; // slight look-down on approach
        } else if (s_clock >= kEarthAfter) {
            camY -= radius / 3; // low-altitude flyover
            pitch = 0x0200;
        }

        // Battle window: pan/cut the orbit toward the dogfight look-at.
        // Outside this gate the passive XZ orbit is unchanged.
        if (IntroBattlePhaseActive()) {
            TickIntroEvadeOnce();
            RefreshIntroCombatantPose();
            camX += s_introTrack[0] / 3;
            camY += s_introTrack[1] / 4;
            camZ += s_introTrack[2] / 16;
            // Dynamic pan: bias view yaw/pitch toward the track point.
            const int32_t dx = s_introTrack[0] - camX;
            const int32_t dy = s_introTrack[1] - camY;
            viewYaw += dx >> 9;
            pitch += 0x0300 + (dy >> 10);
        } else {
            TickIntroEvadeOnce();
            RefreshIntroCombatantPose(); // clears track to orbit center
        }
    }

    CameraSetPosition(&s_camera, camX, camY, camZ);

    // Pitch from phase + a small fraction of title yaw so the view matrix
    // changes every frame (INTRO-CAMERA). Full yaw stays on shipRot — putting
    // it on the camera would cancel TransformShipVertices' object spin.
    // CameraSetOrientation builds Sin32-scale matrices; soft path needs FP16.
    Mat33 view{};
    MatBuildYZT(&view, pitch, viewYaw);
    MatScaleSin32ToFp16(&view);
    for (int i = 0; i < 9; ++i)
        s_camera.rotation.m[i] = view.m[i];
}

// Title camera Z must match ASM FUNC_000959 (DATA_007235), not AbAutoFit.
// Autofit for #176 (r=64,sc=13) → du≈17 → Z≈0x110000 (~13× farther) — too small.
// Dig overrides: INTRO_SHIP_Z=<fixed> or INTRO_AUTOFIT=1 (old framing).
void ApplyTitleShipDistance(const ShipModel* drawModel)
{
    if (const char* zEnv = getenv("INTRO_SHIP_Z")) {
        char* end = nullptr;
        long v = strtol(zEnv, &end, 0);
        if (end != zEnv && v != 0) {
            s_shipDist = static_cast<int32_t>(v);
            return;
        }
    }
    if (getenv("INTRO_AUTOFIT") && getenv("INTRO_AUTOFIT")[0] == '1' && drawModel) {
        int du = AbAutoFitDistUnits(drawModel->radius ? drawModel->radius : 64,
                                    drawModel->scaleRelated);
        s_shipDist = du * FP_ONE;
        return;
    }
    if (uint8_t* title = GetTitleObject()) {
        int32_t z = *reinterpret_cast<int32_t*>(title + 0x36);
        if (z != 0) {
            s_shipDist = z;
            return;
        }
    }
    s_shipDist = DATA_007235[2]; // ffedat / blob: 0x14820
}

// At clock >= 0x7e7b76 the ASM title does ClearBuf; FlipScreen; ClearBuf —
// one blank frame — then restarts the cast at clock 0 (the remastered build
// loops). The ORIGINAL 1995 release instead holds the final static credits,
// so the harness holds by default (matching the review reference); the ASM
// loop is available via INTRO_LOOP_END=1.
bool IntroCastLoopReset()
{
    if (s_clock < kLoopReset) return false;
    // ASM FUNC_000961 tail: past 0x7e7b75 it clears the screen, flips, clears
    // again and sets DATA_008804 = 0 (bumping the 0x9130/0x9131 cycle digits
    // too) — the cast is an attract loop and starts over from the pre-credits
    // logo. The port used to *hold* the clock at kEliteAfter + 1 instead, which
    // parks the last scene on screen forever: that is the "same scene repeating"
    // the interactive run shows. INTRO_HOLD_END=1 restores the old hold for A/B.
    const char* hold = getenv("INTRO_HOLD_END");
    if (hold && hold[0] == '1') {
        s_clock = kEliteAfter + 1;
        return false;
    }
    s_clock = 0;
    return true; // caller's frame clear is the blank; skip painting this frame
}

// Shared by IntroCastDraw + IntroCastDrawUiOnly — one paint per frame.
// FUNC_000962 is registered at g_callbackTable[8][4] but is not dispatched
// from the --intro path (ProcessActiveObjects never runs here); call it
// directly so credits come from blob string IDs 0x941b–0x942d.
void DrawIntroCredits(uint8_t* buf)
{
    if (s_clock < kCreditsStart) return;
    EnsureHudTextDraw();
    SetBackBufferPtr(buf);
    SetTitleClock(s_clock);
    FUNC_000962();
}

void DrawStars(uint8_t* buf)
{
    for (auto& st : s_stars) {
        st.y = static_cast<int16_t>(st.y + st.speed);
        if (st.y >= 200) {
            st.y = 0;
            st.x = static_cast<int16_t>(rand() % 320);
        }
        if (st.x >= 0 && st.x < 320 && st.y >= 0 && st.y < 200)
            buf[st.y * 320 + st.x] = st.bright;
    }
}

// Soft disc — stand-in when title model has no poly mesh (ASM #176 dumps 0px;
// #177 is a view-independent blob). Keeps clock-driven motion visible.
void DrawStarBlob(uint8_t* buf, int cx, int cy, int radius, uint8_t color)
{
    if (radius < 2) radius = 2;
    if (radius > 40) radius = 40;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int r2 = x * x + y * y;
            if (r2 > radius * radius) continue;
            int px = cx + x;
            int py = cy + y;
            if (px < 0 || px >= 320 || py < 0 || py >= 200) continue;
            uint8_t c = color;
            if (r2 > (radius * radius * 3) / 4)
                c = static_cast<uint8_t>(color > 4 ? color - 3 : 1);
            buf[py * 320 + px] = c;
        }
    }
}

bool TryDrawShipMeshAt(int shipIndex, int32_t yaw, int32_t pitch,
                       const int32_t worldPos[3], bool titleGates)
{
    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) return false;

    Mat33 rotMat;
    MatBuildYZT(&rotMat, yaw, pitch);
    if (getenv("AB_FP16_VERT") && getenv("AB_FP16_VERT")[0] == '1')
        MatScaleSin32ToFp16(&rotMat);

    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    int32_t shipPos[3] = {worldPos[0], worldPos[1], worldPos[2]};

    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) return false;
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;

    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipPos[2] != 0 ? shipPos[2] : s_shipDist));
    const bool stationBody = IsIntroStationModel(shipIndex);
    const bool interiorBody = IsIntroInteriorModel(shipIndex);
    const bool debrisBody = IsIntroDebrisModel(shipIndex);
    // Title KeepAll + station phase: orbit card #355 expands #73 at fleet Z
    // (0x14820), putting the camera inside that hull. Suppress station-family
    // children so only the autofit background station is painted.
    const bool suppressTitleStation =
        titleGates && IntroStationPhaseActive() && !stationBody && !interiorBody &&
        !(getenv("AB_ALLOW_TITLE_STATION") && getenv("AB_ALLOW_TITLE_STATION")[0] == '1');
    if (titleGates || stationBody || interiorBody || debrisBody) {
        if (titleGates)
            SetMeshParseGameObj(GetTitleObject());
        SetMeshParseKeepAllSpriteChildren(true);
        if (stationBody || interiorBody || debrisBody)
            SetMeshParseForceBitClearEnter(true);
        if (suppressTitleStation)
            SetMeshParseSuppressStationChildren(true);
    }
    const int prevParseModel = GetMeshParseModelId();
    SetMeshParseModelId(shipIndex);
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseModelId(prevParseModel);
    if (titleGates || stationBody || interiorBody || debrisBody) {
        SetMeshParseGameObj(nullptr);
        SetMeshParseKeepAllSpriteChildren(false);
        SetMeshParseForceBitClearEnter(false);
        if (suppressTitleStation)
            SetMeshParseSuppressStationChildren(false);
    }
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    s_shipPolys = mesh.valid ? static_cast<int>(mesh.polygons.size()) : 0;
    if (!mesh.valid || mesh.polygons.empty()) return false;

    constexpr int kMaxTitlePolys = 4096;
    static TransformedPoly polys[kMaxTitlePolys];
    int n = TransformShipVertices(model, mesh, shipRot, shipPos, &s_camera, polys,
                                  kMaxTitlePolys);
    s_shipVis = n;
    if (n <= 0) return false;

    // Station clip diagnostic: min camera-space Z across all projected verts
    // (including near-rejected ones — TransformVertex stores vz in depth).
    if (stationBody && !interiorBody) {
        int32_t minZ = 0x7fffffff;
        int32_t maxZ = -0x7fffffff;
        for (int i = 0; i < n; i++) {
            for (int v = 0; v < polys[i].vertexCount; v++) {
                const int32_t z = polys[i].verts[v].depth;
                if (z < minZ) minZ = z;
                if (z > maxZ) maxZ = z;
            }
        }
        if (minZ != 0x7fffffff) {
            s_stationMinZ = minZ;
            s_stationMaxZ = maxZ;
        }
    }

    RenderShipFlat(polys, n, model, shipRot);
    return true;
}

bool TryDrawShipMesh(int shipIndex, int32_t yaw, int32_t pitch, bool titleGates)
{
    int32_t shipPos[3] = {0, 0, s_shipDist};
    return TryDrawShipMeshAt(shipIndex, yaw, pitch, shipPos, titleGates);
}

void DrawTitleShipBridge(uint8_t* buf)
{
    uint8_t* obj = GetTitleObject();
    if (!obj) {
        s_shipVis = -2;
        return;
    }

    s_shipType = static_cast<int>(*reinterpret_cast<uint32_t*>(obj + 0x82));
    // DATA_009129 = clock>>10 (uint16); Sin32 full circle is 0x10000.
    int32_t yaw = static_cast<int32_t>(*reinterpret_cast<uint16_t*>(obj + 0x9c));
    int32_t pitch = 0;

    // Keep object matrix in sync with title spin (ASM stores angle at +0x9c).
    Mat33 rotMat;
    MatBuildYZT(&rotMat, yaw, pitch);
    memcpy(obj, rotMat.m, sizeof(rotMat.m));

    s_usedStarFallback = false;

    // Fallback bridge when job-pipeline paint is empty. Prefer real title type
    // with Sprite749/BitClear enter; INTRO_TITLE_BODY / INTRO_BRIDGE_SAKER override.
    int drawIndex = s_shipType > 0 ? s_shipType : 31;
    bool titleGates = (drawIndex == 0xb0 || drawIndex == 0xb1);
    if (getenv("INTRO_BRIDGE_SAKER") && getenv("INTRO_BRIDGE_SAKER")[0] == '1') {
        drawIndex = 31;
        titleGates = false;
    } else if (const char* body = getenv("INTRO_TITLE_BODY")) {
        drawIndex = atoi(body);
        titleGates = (drawIndex == s_shipType);
    } else if (getenv("INTRO_TITLE_FULL") && getenv("INTRO_TITLE_FULL")[0] == '1') {
        drawIndex = s_shipType;
        titleGates = true;
    }

    const ShipModel* drawModel = GetShipModel(drawIndex);
    ApplyTitleShipDistance(drawModel);
    if (TryDrawShipMesh(drawIndex, yaw, pitch, titleGates)) {
        s_usedStarFallback = (drawIndex == 31);
        MaybeDumpIntroShip();
        return;
    }

    int cx = 160 + static_cast<int>((Sin32(yaw) >> 22));
    int cy = 90 + static_cast<int>((Sin32(yaw + 0x4000) >> 23));
    DrawStarBlob(buf, cx, cy, 14, 0x0F);
    s_shipVis = 0;
    s_shipPolys = 0;
    s_usedStarFallback = true;
    MaybeDumpIntroShip();
}

// Host paint for FUNC_001682 title types — C++ mesh expand with live ALU gates.
bool TitleObjectPaintHook(uint8_t* obj, uint32_t type)
{
    if (!obj || !g_videoState) return false;
    int32_t yaw = static_cast<int32_t>(*reinterpret_cast<uint16_t*>(obj + 0x9c));
    Mat33 rotMat;
    MatBuildYZT(&rotMat, yaw, 0);
    memcpy(obj, rotMat.m, sizeof(rotMat.m));
    s_shipType = static_cast<int>(type);
    const ShipModel* drawModel = GetShipModel(static_cast<int>(type));
    ApplyTitleShipDistance(drawModel);
    if (s_suppressSoftPaint) {
        // HW path: ObjectStruct/yaw/dist updated; .x draw uses CollectHwInstances.
        s_usedStarFallback = false;
        return true;
    }
    if (TryDrawShipMesh(static_cast<int>(type), yaw, 0, true)) {
        s_usedStarFallback = false;
        MaybeDumpIntroShip();
        return true;
    }
    s_shipPolys = 0;
    MaybeDumpIntroShip();
    return false;
}

static bool IsTitleParent(int modelId)
{
    return modelId == 176 || modelId == 177;
}

static bool IsTitleOrbitCard(int modelId)
{
    return modelId >= 346 && modelId <= 363;
}

static void EmitHwInstance(const SpriteChildInstance& sp, IntroCastHwInstance* out,
                           int* written, int maxOut)
{
    if (*written >= maxOut) return;
    // Dedupe against already written
    const int32_t q0 = sp.pos[0] & ~0x1FF;
    const int32_t q1 = sp.pos[1] & ~0x1FF;
    const int32_t q2 = sp.pos[2] & ~0x1FF;
    for (int i = 0; i < *written; i++) {
        if (out[i].modelId == sp.modelId &&
            (out[i].pos[0] & ~0x1FF) == q0 &&
            (out[i].pos[1] & ~0x1FF) == q1 &&
            (out[i].pos[2] & ~0x1FF) == q2)
            return;
    }
    out[*written].modelId = sp.modelId;
    for (int k = 0; k < 3; k++) out[*written].pos[k] = sp.pos[k];
    for (int k = 0; k < 9; k++) out[*written].rot[k] = sp.rot[k];
    out[*written].scaleRelated = sp.scaleRelated;
    (*written)++;
}

// Recurse Sprite749 cards → nested fleet. Card stubs with no children still
// emit a stand-in pose so the logo-phase (#177) is not empty navy.
static void CollectFromModelRecursive(int shipIndex, const int32_t* shipRot,
                                      const int32_t* shipPos, int parentScale,
                                      IntroCastHwInstance* out, int* written,
                                      int maxOut, int depth)
{
    if (*written >= maxOut || depth > 4) return;
    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) return;

    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) return;
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;

    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(s_shipDist));
    // HW intro: expand every BitClear orbit band (soft paint uses live ALU so
    // only one card shows). Attach verts keep cards spatially distinct.
    SetMeshParseForceBitClearEnter(true);
    if (depth == 0)
        SetMeshParseGameObj(GetTitleObject());
    SetMeshParseKeepAllSpriteChildren(true);
    const int prevParseModel = GetMeshParseModelId();
    SetMeshParseModelId(shipIndex);
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseModelId(prevParseModel);
    SetMeshParseGameObj(nullptr);
    SetMeshParseForceBitClearEnter(false);
    SetMeshParseKeepAllSpriteChildren(false);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    if (!mesh.valid) return;

    constexpr int kMaxSprites = 160;
    SpriteChildInstance sprites[kMaxSprites];
    const int n = CollectSpriteChildInstances(
        model, mesh, shipRot, shipPos, &s_camera, parentScale, sprites, kMaxSprites);

    // KeepAllSpriteChildren already flattens nested fleets into the root list.
    // Only recurse into orbit cards when this level has no fleet leaves (logo
    // stubs #361–#363), otherwise card recursion is redundant and very slow.
    int fleetHere = 0;
    for (int i = 0; i < n; i++) {
        const int id = sprites[i].modelId;
        if (!IsTitleParent(id) && !IsTitleOrbitCard(id))
            fleetHere++;
    }

    for (int i = 0; i < n && *written < maxOut; i++) {
        const int id = sprites[i].modelId;
        if (IsTitleParent(id)) continue;

        if (IsTitleOrbitCard(id)) {
            if (fleetHere > 0)
                continue;
            const int before = *written;
            CollectFromModelRecursive(id, sprites[i].rot, sprites[i].pos,
                                      sprites[i].scaleRelated, out, written,
                                      maxOut, depth + 1);
            if (*written == before)
                EmitHwInstance(sprites[i], out, written, maxOut);
            continue;
        }

        EmitHwInstance(sprites[i], out, written, maxOut);
    }
}

static int CollectFromTitleType(int shipIndex, IntroCastHwInstance* out, int maxOut)
{
    if (!out || maxOut <= 0) return 0;

    uint8_t* obj = GetTitleObject();
    int32_t yaw = 0;
    if (obj)
        yaw = static_cast<int32_t>(*reinterpret_cast<uint16_t*>(obj + 0x9c));

    Mat33 rotMat;
    MatBuildYZT(&rotMat, yaw, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    int32_t shipPos[3] = {0, 0, s_shipDist};

    const ShipModel* model = GetShipModel(shipIndex);
    const int parentScale =
        model ? static_cast<int>(model->scaleRelated) : 13;

    int written = 0;
    CollectFromModelRecursive(shipIndex, shipRot, shipPos, parentScale, out,
                              &written, maxOut, 0);
    return written;
}

// Manhattan distance in world fixed-point (laser hit test).
static int32_t IntroWorldManhattan(const int32_t a[3], const int32_t b[3])
{
    auto abs32 = [](int32_t v) -> int32_t { return v < 0 ? -v : v; };
    return abs32(a[0] - b[0]) + abs32(a[1] - b[1]) + abs32(a[2] - b[2]);
}

static void MaybeDumpIntroCombat(const char* kind, int targetId, int shooterId,
                                 int32_t ix, int32_t iy, int32_t iz)
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    fprintf(stderr,
            "INTRO-COMBAT %s clock=0x%x hit=%d target=%d shooter=%d "
            "impact=%d,%d,%d flash=%d targetYaw=%d targetVel=%d,%d,%d\n",
            kind, s_clock, s_introCombatHits, targetId, shooterId,
            (int)ix, (int)iy, (int)iz, s_introHitFlashFrames,
            (int)s_combatTargetYaw,
            (int)s_combatTargetVel[0], (int)s_combatTargetVel[1],
            (int)s_combatTargetVel[2]);
}

// Project intro world coords through s_camera (same path as TransformShipVertices).
static bool ProjectIntroWorld(const int32_t pos[3], ProjResult* pr)
{
    Point3D wp{pos[0], pos[1], pos[2]};
    return ProjectPoint(&s_camera, &wp, pr) && pr->visible;
}

// Perspective scale: screen radius ∝ radiusPx * refZ / z (task: radiusPx / z).
static int IntroFxScreenRadius(int radiusPx, int32_t viewZ)
{
    const int32_t z = (viewZ > ASM_PROJ_NEAR) ? viewZ : ASM_PROJ_NEAR;
    const int32_t refZ = (s_shipDist != 0) ? (s_shipDist / 5) : 0x4000;
    int screenR = static_cast<int>(((int64_t)radiusPx * refZ) / z);
    if (screenR < 1) screenR = 1;
    if (screenR > 80) screenR = 80;
    return screenR;
}

static void MaybeDumpIntroExplosion(const IntroExplosionFx& ex, int sx, int sy)
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    fprintf(stderr,
            "INTRO-EXPLOSION spawn clock=0x%x pos=%d,%d,%d screen=%d,%d "
            "radius=%d max=%d big=%d lifetime=%d\n",
            s_clock, (int)ex.pos[0], (int)ex.pos[1], (int)ex.pos[2],
            sx, sy, ex.radiusPx, ex.maxRadiusPx, ex.big ? 1 : 0, ex.lifetime);
}

static void MaybeDumpIntroDebris(int count, int targetId, const int32_t pos[3],
                                 const int32_t vel[3], int sx, int sy)
{
    const char* e = getenv("JJFFE_DEBUG_INTRO");
    if (!e || e[0] != '1') return;
    fprintf(stderr,
            "INTRO-DEBRIS spawn clock=0x%x count=%d total=%d parent=%d "
            "pos=%d,%d,%d screen=%d,%d vel=%d,%d,%d\n",
            s_clock, count, s_introDebrisSpawnedTotal, targetId,
            (int)pos[0], (int)pos[1], (int)pos[2], sx, sy,
            (int)vel[0], (int)vel[1], (int)vel[2]);
}

static void SpawnIntroExplosion(const int32_t pos[3], bool big)
{
    for (int i = 0; i < kMaxIntroExplosions; i++) {
        if (s_introExplosions[i].active) continue;
        IntroExplosionFx& ex = s_introExplosions[i];
        ex.pos[0] = pos[0];
        ex.pos[1] = pos[1];
        ex.pos[2] = pos[2];
        ex.radiusPx = big ? 8 : 2;
        ex.maxRadiusPx = big ? 72 : 28;
        ex.lifetime = big ? 90 : 45;
        ex.active = true;
        ex.big = big;
        // ASM FUNC_000939: type 0x29 explosion/projectile at impact (vec6 =
        // pos[3]+vel[3]). flags|1 forces spawn past the PRNG gate.
        uint32_t vec6[6] = {
            static_cast<uint32_t>(pos[0]), static_cast<uint32_t>(pos[1]),
            static_cast<uint32_t>(pos[2]), 0, 0, 0};
        FUNC_000939(0, vec6, big ? 3u : 1u);
        ProjResult pr{};
        int sx = PROJ_CENTER_X, sy = PROJ_CENTER_Y;
        if (ProjectIntroWorld(ex.pos, &pr)) {
            sx = pr.sx;
            sy = pr.sy;
        }
        MaybeDumpIntroExplosion(ex, sx, sy);
        return;
    }
}

static int SpawnIntroDebris(const int32_t parentPos[3], const int32_t parentVel[3],
                            int parentYaw)
{
    int spawned = 0;
    for (int i = 0; i < kIntroDebrisModelCount && spawned < 4; i++) {
        int slot = -1;
        for (int d = 0; d < kMaxIntroDebris; d++) {
            if (!s_introDebris[d].active) { slot = d; break; }
        }
        if (slot < 0) break;
        IntroDebrisFx& deb = s_introDebris[slot];
        deb.modelId = kIntroDebrisModels[i % kIntroDebrisModelCount];
        // Scatter around impact; inherit parent velocity (ASM debris inherits
        // parent ObjectStruct vel at +0x3e / spawn from FUNC_000939 path).
        const int32_t scatter = (0x400 + (i * 0x300)) * (i & 1 ? -1 : 1);
        deb.pos[0] = parentPos[0] + scatter;
        deb.pos[1] = parentPos[1] + (scatter / 2);
        deb.pos[2] = parentPos[2] + (i * 0x200);
        deb.vel[0] = parentVel[0] + (scatter >> 2);
        deb.vel[1] = parentVel[1] + ((i - 1) * 0x80);
        deb.vel[2] = parentVel[2] + (0x100 * (i + 1));
        deb.yaw = parentYaw + (i * 0x1000);
        deb.lifetime = 120;
        deb.active = true;
        spawned++;
        s_introDebrisSpawnedTotal++;
    }
    return spawned;
}

// Laser-vs-hull: title fleet PlainLines already paint the beams. During the
// battle window, treat nearby fleet pairs as a laser corridor (endpoint near
// the target hull) and fire the FUNC_000939 destruction path the soft intro
// never ran. Camera tracking + target evade live in RefreshIntroCombatantPose /
// ApplyIntroHitEvade (battle-gated); this path paints FX and applies hit jinks.
static void UpdateAndDrawIntroCombat(uint8_t* buf)
{
    if (!IntroBattlePhaseActive() || s_suppressSoftPaint) return;
    if (!buf || !g_videoState) return;

    // Keep evade/pose fresh even if camera update ran earlier this frame.
    TickIntroEvadeOnce();
    RefreshIntroCombatantPose();

    // Tick active FX every battle frame.
    if (s_introHitFlashFrames > 0)
        s_introHitFlashFrames--;

    for (int i = 0; i < kMaxIntroExplosions; i++) {
        IntroExplosionFx& ex = s_introExplosions[i];
        if (!ex.active) continue;
        ex.lifetime--;
        if (ex.radiusPx < ex.maxRadiusPx)
            ex.radiusPx += ex.big ? 3 : 2;
        if (ex.lifetime <= 0) {
            ex.active = false;
            continue;
        }
        ProjResult pr{};
        if (ProjectIntroWorld(ex.pos, &pr)) {
            // FUNC_001612_DrawCircle — filled expanding sphere at projected
            // impact; diameter scales with depth (not viewport-center / unscaled).
            int16_t cy = pr.sy;
            const int screenR = IntroFxScreenRadius(ex.radiusPx, pr.z);
            const uint8_t color = ex.big ? static_cast<uint8_t>(0x1f)
                                        : static_cast<uint8_t>(0x1c);
            DrawCircle(pr.sx, &cy, screenR * 2, color);
            DrawStarBlob(buf, pr.sx, pr.sy,
                         screenR > 4 ? screenR / 3 : 2,
                         static_cast<uint8_t>(0x0f));
        }
    }

    for (int i = 0; i < kMaxIntroDebris; i++) {
        IntroDebrisFx& deb = s_introDebris[i];
        if (!deb.active) continue;
        deb.lifetime--;
        deb.pos[0] += deb.vel[0] >> 8;
        deb.pos[1] += deb.vel[1] >> 8;
        deb.pos[2] += deb.vel[2] >> 8;
        deb.yaw += 0x80;
        if (deb.lifetime <= 0) {
            deb.active = false;
            continue;
        }
        const int savedPolys = s_shipPolys;
        const int savedVis = s_shipVis;
        const int savedType = s_shipType;
        // World-space fragment at impact + inherited velocity (no identity pose).
        TryDrawShipMeshAt(deb.modelId, deb.yaw, 0, deb.pos, false);
        s_shipPolys = savedPolys;
        s_shipVis = savedVis;
        s_shipType = savedType;
    }

    // Visible dogfight hulls omitted here — title constellation already paints
    // the fleet; evade is carried on impact pose + targetYaw/targetVel dumps.

    if (s_introHitFlashFrames > 0) {
        ProjResult pr{};
        if (ProjectIntroWorld(s_introHitFlashPos, &pr)) {
            // Hit-state flash (ASM obj+0x14f flash counter via FUNC_000937).
            const int flashR = IntroFxScreenRadius(10 + s_introHitFlashFrames, pr.z);
            DrawStarBlob(buf, pr.sx, pr.sy, flashR, static_cast<uint8_t>(0x1f));
        }
    }

    // Pace hits so the dig emits combat/explosion/debris without spamming.
    const uint32_t sinceHit = s_clock - s_introLastHitClock;
    const uint32_t hitPeriod = (s_clock >= kBigBoomAfter) ? 0x18000u : 0x28000u;
    if (s_introLastHitClock != 0 && sinceHit < hitPeriod && s_introCombatHits > 0)
        return;

    // Struck hull world position (laser endpoint on the receiving ship).
    // Keep FX in title fleet space (DATA_007235 / s_shipDist), not the station
    // autofit Z — that framing distance is ~25× farther and collapses every
    // lateral offset to the viewport center under ProjectPoint.
    int32_t impact[3] = {
        s_combatTargetPos[0], s_combatTargetPos[1], s_combatTargetPos[2]};
    // Near-station boom (reference 2:31): nudge toward station lateral center
    // (world X≈0) while staying at fleet depth so the projected screen point
    // still tracks the impact and is clearly off-center.
    if (s_clock >= kBigBoomAfter && IntroStationPhaseActive()) {
        const int32_t depth =
            (s_shipDist != 0) ? (s_shipDist / 5) : 0x5000;
        impact[0] = (s_combatTargetPos[0] * 2) / 3;
        impact[1] = (s_combatTargetPos[1] * 2) / 3;
        impact[2] = depth;
    }

    const int32_t hitDist =
        IntroWorldManhattan(s_combatShooterPos, s_combatTargetPos);
    // Always true at title orbit spacing — documents the laser-vs-hull test.
    constexpr int32_t kHitThresh = FP_ONE * 256;
    if (hitDist <= 0 || hitDist > kHitThresh) {
        impact[0] = s_combatTargetPos[0];
        impact[1] = s_combatTargetPos[1];
        impact[2] = s_combatTargetPos[2];
    }

    s_introCombatHits++;
    s_introLastHitClock = s_clock;
    s_introHitFlashFrames = 12;
    s_introHitFlashPos[0] = impact[0];
    s_introHitFlashPos[1] = impact[1];
    s_introHitFlashPos[2] = impact[2];

    ApplyIntroHitEvade();
    // Refresh pose so the dump's targetYaw/targetVel reflect the post-hit jink.
    RefreshIntroCombatantPose();

    MaybeDumpIntroCombat("hit", kIntroCombatTargetId, kIntroCombatShooterId,
                         impact[0], impact[1], impact[2]);

    const bool big = (s_clock >= kBigBoomAfter) || (s_introCombatHits >= 3);
    SpawnIntroExplosion(impact, big);

    // Debris from the dogfight beat (reference 2:20) — also on first hit so
    // short headless digs still see INTRO-DEBRIS.
    if (s_clock >= kDebrisPhaseAfter || s_introCombatHits == 1 ||
        s_introDebrisSpawnedTotal == 0) {
        const int nDeb = SpawnIntroDebris(impact, s_combatTargetVel,
                                          s_combatTargetYaw);
        if (nDeb > 0) {
            ProjResult pr{};
            int sx = PROJ_CENTER_X, sy = PROJ_CENTER_Y;
            if (ProjectIntroWorld(impact, &pr)) {
                sx = pr.sx;
                sy = pr.sy;
            }
            MaybeDumpIntroDebris(nDeb, kIntroCombatTargetId, impact,
                                 s_combatTargetVel, sx, sy);
        }
    }
}

void ClearIntroPlayerFx()
{
    s_introLastHitClock = 0;
    s_introCombatHits = 0;
    s_introDebrisSpawnedTotal = 0;
    s_introHitFlashFrames = 0;
    s_evadeTickClock = 0xffffffffu;
    for (int i = 0; i < kMaxIntroExplosions; i++)
        s_introExplosions[i].active = false;
    for (int i = 0; i < kMaxIntroDebris; i++)
        s_introDebris[i].active = false;
}

// --- INTRO_PLAYER strip (drawn in 32-bit window pixels, not the 320x200 dump) ---

struct StripBtn {
    int x, y, w, h;
    const char* label;
    enum Id : int {
        Play = 0,
        StepBack,
        StepFwd,
        Slow,
        Normal,
        Fast,
        Count
    } id;
};

constexpr int kScrubX = 8;
constexpr int kScrubW = 624;
constexpr int kScrubY = 56;
constexpr int kScrubH = 14;

StripBtn s_stripBtns[StripBtn::Count] = {
    {8, 28, 84, 22, "Play", StripBtn::Play},
    {100, 28, 40, 22, "-1", StripBtn::StepBack},
    {144, 28, 40, 22, "+1", StripBtn::StepFwd},
    {200, 28, 48, 22, "1/4", StripBtn::Slow},
    {252, 28, 40, 22, "1x", StripBtn::Normal},
    {296, 28, 48, 22, "4x", StripBtn::Fast},
};

static void StripPutPixel(uint32_t* pixels, int pitch, int x, int y, int y0, int y1,
                          uint32_t c)
{
    if (x < 0 || x >= 640 || y < y0 || y >= y1) return;
    pixels[y * pitch + x] = c;
}

static void StripFillRect(uint32_t* pixels, int pitch, int x, int y, int w, int h,
                          int y0, int y1, uint32_t c)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++)
            StripPutPixel(pixels, pitch, x + col, y + row, y0, y1, c);
    }
}

// 5x7 glyphs for strip labels (rows packed as bit7=left).
static const uint8_t kStripFont[96][7] = {
    // space through / (indices 0..15 for ' '..'/')
    {0,0,0,0,0,0,0}, //  
    {0x20,0x20,0x20,0x20,0x20,0x00,0x20}, // !
    {0x50,0x50,0x00,0x00,0x00,0x00,0x00}, // "
    {0x50,0xF8,0x50,0xF8,0x50,0x00,0x00}, // #
    {0x20,0x78,0xA0,0x70,0x28,0xF0,0x20}, // $
    {0xC8,0xC8,0x10,0x20,0x40,0x98,0x98}, // %
    {0x40,0xA0,0xA0,0x40,0xA8,0x90,0x68}, // &
    {0x20,0x20,0x00,0x00,0x00,0x00,0x00}, // '
    {0x10,0x20,0x40,0x40,0x40,0x20,0x10}, // (
    {0x40,0x20,0x10,0x10,0x10,0x20,0x40}, // )
    {0x00,0x20,0xA8,0x70,0xA8,0x20,0x00}, // *
    {0x00,0x20,0x20,0xF8,0x20,0x20,0x00}, // +
    {0x00,0x00,0x00,0x00,0x20,0x20,0x40}, // ,
    {0x00,0x00,0x00,0xF8,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x60,0x60}, // .
    {0x08,0x08,0x10,0x20,0x40,0x80,0x80}, // /
    {0x70,0x88,0x98,0xA8,0xC8,0x88,0x70}, // 0
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70}, // 1
    {0x70,0x88,0x08,0x30,0x40,0x80,0xF8}, // 2
    {0x70,0x88,0x08,0x30,0x08,0x88,0x70}, // 3
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10}, // 4
    {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70}, // 5
    {0x30,0x40,0x80,0xF0,0x88,0x88,0x70}, // 6
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x40}, // 7
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70}, // 8
    {0x70,0x88,0x88,0x78,0x08,0x10,0x60}, // 9
    {0x00,0x60,0x60,0x00,0x60,0x60,0x00}, // :
    {0x00,0x60,0x60,0x00,0x60,0x20,0x40}, // ;
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08}, // <
    {0x00,0x00,0xF8,0x00,0xF8,0x00,0x00}, // =
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40}, // >
    {0x70,0x88,0x08,0x10,0x20,0x00,0x20}, // ?
    {0x70,0x88,0x08,0x68,0xA8,0xA8,0x70}, // @
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x88}, // A
    {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0}, // B
    {0x70,0x88,0x80,0x80,0x80,0x88,0x70}, // C
    {0xF0,0x88,0x88,0x88,0x88,0x88,0xF0}, // D
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8}, // E
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80}, // F
    {0x70,0x88,0x80,0xB8,0x88,0x88,0x70}, // G
    {0x88,0x88,0x88,0xF8,0x88,0x88,0x88}, // H
    {0x70,0x20,0x20,0x20,0x20,0x20,0x70}, // I
    {0x38,0x10,0x10,0x10,0x10,0x90,0x60}, // J
    {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88}, // K
    {0x80,0x80,0x80,0x80,0x80,0x80,0xF8}, // L
    {0x88,0xD8,0xA8,0x88,0x88,0x88,0x88}, // M
    {0x88,0xC8,0xA8,0x98,0x88,0x88,0x88}, // N
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70}, // O
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80}, // P
    {0x70,0x88,0x88,0x88,0xA8,0x90,0x68}, // Q
    {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88}, // R
    {0x70,0x88,0x80,0x70,0x08,0x88,0x70}, // S
    {0xF8,0x20,0x20,0x20,0x20,0x20,0x20}, // T
    {0x88,0x88,0x88,0x88,0x88,0x88,0x70}, // U
    {0x88,0x88,0x88,0x88,0x88,0x50,0x20}, // V
    {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88}, // W
    {0x88,0x88,0x50,0x20,0x50,0x88,0x88}, // X
    {0x88,0x88,0x50,0x20,0x20,0x20,0x20}, // Y
    {0xF8,0x08,0x10,0x20,0x40,0x80,0xF8}, // Z
};

static void StripDrawChar(uint32_t* pixels, int pitch, int x, int y, int y0, int y1,
                          char ch, uint32_t c)
{
    unsigned idx = static_cast<unsigned char>(ch);
    if (idx < 32 || idx >= 32 + 96) return;
    // Only filled through 'Z' above; map a-z to A-Z.
    if (idx >= 'a' && idx <= 'z') idx = static_cast<unsigned>(idx - 'a' + 'A');
    if (idx < 32 || idx > 'Z') {
        // Allow digits and punctuation in the table (space..Z).
        if (idx < 32 || idx > 'Z') return;
    }
    const int fi = static_cast<int>(idx) - 32;
    if (fi < 0 || fi >= 96) return;
    // Table only fully defined through Z (index 90-32=58). Guard sparse tail.
    if (fi > 'Z' - 32) return;
    for (int row = 0; row < 7; row++) {
        const uint8_t bits = kStripFont[fi][row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x80 >> col))
                StripPutPixel(pixels, pitch, x + col, y + row, y0, y1, c);
        }
    }
}

static void StripDrawText(uint32_t* pixels, int pitch, int x, int y, int y0, int y1,
                          const char* s, uint32_t c)
{
    for (; s && *s; ++s) {
        StripDrawChar(pixels, pitch, x, y, y0, y1, *s, c);
        x += 6;
    }
}

void IntroPlayerPaintStrip(uint32_t* pixels, int pitchWords, int stripY, int stripH,
                           int width)
{
    (void)width;
    const int y0 = stripY;
    const int y1 = stripY + stripH;
    const uint32_t white = 0x00e8e8e8u;
    const uint32_t dim = 0x00606070u;
    const uint32_t accent = 0x0030a0e0u;
    const uint32_t btnBg = 0x00303840u;
    const uint32_t btnOn = 0x00206040u;

    s_stripBtns[StripBtn::Play].label = s_paused ? "Play" : "Pause";

    char line[64];
    const uint32_t sec =
        (kLoopReset > 0)
            ? static_cast<uint32_t>((static_cast<uint64_t>(s_clock) * kCastLengthSec) /
                                    kLoopReset)
            : 0u;
    std::snprintf(line, sizeof(line), "0x%06X  %02u:%02u  spd=%d",
                  s_clock & 0xffffffu, sec / 60u, sec % 60u, s_ticksPerFrame);
    StripDrawText(pixels, pitchWords, 8, stripY + 6, y0, y1, line, white);

    for (int i = 0; i < StripBtn::Count; i++) {
        const StripBtn& b = s_stripBtns[i];
        bool on = false;
        if (b.id == StripBtn::Slow && s_ticksPerFrame <= kDefaultTicksPerFrame / 4 + 10)
            on = true;
        if (b.id == StripBtn::Normal &&
            s_ticksPerFrame > kDefaultTicksPerFrame / 2 &&
            s_ticksPerFrame < kDefaultTicksPerFrame * 2)
            on = true;
        if (b.id == StripBtn::Fast && s_ticksPerFrame >= kDefaultTicksPerFrame * 4 - 10)
            on = true;
        if (b.id == StripBtn::Play && !s_paused) on = true;
        StripFillRect(pixels, pitchWords, b.x, stripY + b.y, b.w, b.h, y0, y1,
                      on ? btnOn : btnBg);
        for (int t = 0; t < b.w; t++) {
            StripPutPixel(pixels, pitchWords, b.x + t, stripY + b.y, y0, y1, dim);
            StripPutPixel(pixels, pitchWords, b.x + t, stripY + b.y + b.h - 1, y0, y1,
                          dim);
        }
        for (int t = 0; t < b.h; t++) {
            StripPutPixel(pixels, pitchWords, b.x, stripY + b.y + t, y0, y1, dim);
            StripPutPixel(pixels, pitchWords, b.x + b.w - 1, stripY + b.y + t, y0, y1,
                          dim);
        }
        StripDrawText(pixels, pitchWords, b.x + 6, stripY + b.y + 7, y0, y1, b.label,
                      white);
    }

    StripFillRect(pixels, pitchWords, kScrubX, stripY + kScrubY, kScrubW, kScrubH, y0,
                  y1, dim);
    const uint32_t thumbX =
        kScrubX +
        static_cast<uint32_t>((static_cast<uint64_t>(s_clock) * (kScrubW - 4)) /
                              (kLoopReset ? kLoopReset : 1u));
    StripFillRect(pixels, pitchWords, static_cast<int>(thumbX), stripY + kScrubY, 4,
                  kScrubH, y0, y1, accent);
}

bool IntroPlayerHitTest(int winX, int winY)
{
    if (!s_playerEnabled) return false;
    const int stripY = 400;
    if (winY < stripY) return false;
    const int ly = winY - stripY;

    // Scrubber
    if (ly >= kScrubY && ly < kScrubY + kScrubH && winX >= kScrubX &&
        winX < kScrubX + kScrubW) {
        const uint32_t clock = static_cast<uint32_t>(
            (static_cast<uint64_t>(winX - kScrubX) * kLoopReset) /
            static_cast<uint32_t>(kScrubW));
        IntroCastSetClock(clock);
        return true;
    }

    for (int i = 0; i < StripBtn::Count; i++) {
        const StripBtn& b = s_stripBtns[i];
        if (winX >= b.x && winX < b.x + b.w && ly >= b.y && ly < b.y + b.h) {
            switch (b.id) {
            case StripBtn::Play:
                s_paused = !s_paused;
                break;
            case StripBtn::StepBack:
                IntroCastStep(-1);
                break;
            case StripBtn::StepFwd:
                IntroCastStep(1);
                break;
            case StripBtn::Slow:
                IntroCastSetSpeed(kDefaultTicksPerFrame / 4);
                break;
            case StripBtn::Normal:
                IntroCastSetSpeed(kDefaultTicksPerFrame);
                break;
            case StripBtn::Fast:
                IntroCastSetSpeed(kDefaultTicksPerFrame * 4);
                break;
            default:
                break;
            }
            return true;
        }
    }
    return true; // absorb all strip clicks
}

} // namespace

void IntroCastSetPaused(bool paused)
{
    s_paused = paused;
}

bool IntroCastGetPaused()
{
    return s_paused;
}

void IntroCastSetClock(uint32_t clock)
{
    if (clock >= kLoopReset)
        clock = kLoopReset - 1;
    s_clock = clock;
    s_splashActive = false;
    SetTitleClock(s_clock);
    if (uint8_t* title = GetTitleObject()) {
        *reinterpret_cast<uint16_t*>(title + 0x9c) =
            static_cast<uint16_t>(TitleYawFromClock(s_clock));
    }
    ClearIntroPlayerFx();
}

void IntroCastStep(int frames)
{
    const int64_t delta =
        static_cast<int64_t>(frames) * static_cast<int64_t>(s_ticksPerFrame);
    int64_t next = static_cast<int64_t>(s_clock) + delta;
    if (next < 0) next = 0;
    if (next >= static_cast<int64_t>(kLoopReset))
        next = static_cast<int64_t>(kLoopReset) - 1;
    IntroCastSetClock(static_cast<uint32_t>(next));
}

uint32_t IntroCastGetLoopEnd()
{
    return kLoopReset;
}

bool IntroCastPlayerEnabled()
{
    return s_playerEnabled;
}

void IntroCastInit()
{
    s_splashActive = true;
    s_splashFrames = 0;
    // Title clock starts at 0x400 only after splash (ASM _asmmain → mov [ebx],0x400).
    // Hold at 0 during splash so dumps show the phase clearly.
    s_clock = 0;
    if (const char* start = getenv("INTRO_START_CLOCK")) {
        char* end = nullptr;
        unsigned long v = strtoul(start, &end, 0);
        if (end != start) {
            s_clock = static_cast<uint32_t>(v);
            s_splashActive = false; // dig override skips splash
        }
    }
    if (const char* spd = getenv("INTRO_SPEED")) {
        char* end = nullptr;
        long v = strtol(spd, &end, 0);
        if (end != spd && v > 0)
            IntroCastSetSpeed(static_cast<int>(v));
    }
    s_splashTimeoutActive = false;
    s_actionId = -1;
    // Keys already down at boot are not new presses.
    memset(s_prevKeys, 0, sizeof(s_prevKeys));
    s_inputPoll = 0;
    s_injectedKeyCount = 0;
    if (const char* keys = getenv("JJFFE_INTRO_KEYS")) {
        const char* p = keys;
        while (*p && s_injectedKeyCount < 16) {
            char* end = nullptr;
            const long poll = strtol(p, &end, 0);
            if (end == p || *end != ':') break;
            p = end + 1;
            const long sc = strtol(p, &end, 0);
            if (end == p) break;
            s_injectedKeys[s_injectedKeyCount++] = {static_cast<int>(poll),
                                                    static_cast<int>(sc & 0xff)};
            p = (*end == ',') ? end + 1 : end;
        }
    }
    s_paused = false;
    s_playerEnabled = false;
    if (const char* pl = getenv("INTRO_PLAYER")) {
        if (pl[0] == '1') {
            s_playerEnabled = true;
            PlatformSetControlStripHeight(kPlayerStripHeight);
            PlatformSetControlStripPainter(IntroPlayerPaintStrip);
        }
    }
    s_songStarted = false;
    s_helpVisible = false; // H toggles dig HUD; cast presents clean by default
    s_shipAnim = true;
    s_titleInited = false;
    s_shipVis = -1;
    s_shipType = -1;
    s_shipPolys = 0;
    s_usedStarFallback = false;
    s_useBridgeFallback = true;
    s_realPaintPixels = 0;
    for (auto& st : s_stars) {
        st.x = static_cast<int16_t>(rand() % 320);
        st.y = static_cast<int16_t>(rand() % 200);
        st.speed = static_cast<uint8_t>(1 + (rand() % 3));
        // Prefer bright greys/whites so stars read on navy (Ogre) or black (SDL).
        st.bright = static_cast<uint8_t>(0x70 + (rand() % 0x80));
    }

    CameraInit(&s_camera);
    CameraSetPosition(&s_camera, 0, 0, 0);

    // A/B instrumentation: AB_JOB_TRACE=<path> opens the job trace the ASM
    // binary emits natively (jjffe_job_trace_open); JJFFE_DUMP_INTRO_FRAME dumps
    // the finished back buffer once s_clock reaches JJFFE_DUMP_INTRO_CLOCK.
    if (const char* jt = getenv("AB_JOB_TRACE"))
        if (jt[0])
            jjffe_job_trace_open(jt);
    if (const char* ct = getenv("AB_1F_792_CHAIN_TRACE"))
        if (ct[0])
            jjffe_planet_792_chain_open(ct);
    s_dumpFramePath = getenv("JJFFE_DUMP_INTRO_FRAME");
    s_dumpFrameClock = 0xffffffffu;
    s_dumpFrameDone = false;
    if (const char* dc = getenv("JJFFE_DUMP_INTRO_CLOCK")) {
        char* end = nullptr;
        unsigned long v = strtoul(dc, &end, 0);
        if (end != dc)
            s_dumpFrameClock = static_cast<uint32_t>(v);
    }
    s_introTrack[0] = s_introTrack[1] = s_introTrack[2] = 0;
    s_evadeYaw = s_evadePitch = 0;
    s_evadeVel[0] = s_evadeVel[1] = s_evadeVel[2] = 0;
    s_evadePos[0] = s_evadePos[1] = s_evadePos[2] = 0;
    s_evadeTickClock = 0xffffffffu;
    s_introLastHitClock = 0;
    s_introCombatHits = 0;
    s_introDebrisSpawnedTotal = 0;
    s_introHitFlashFrames = 0;
    for (int i = 0; i < kMaxIntroExplosions; i++)
        s_introExplosions[i].active = false;
    for (int i = 0; i < kMaxIntroDebris; i++)
        s_introDebris[i].active = false;
    InitMeshOpcodeHandlers();
    SetTextureRasterDebug(false);
    SetDebugFaceColors(false);
    if (g_videoState) {
        // Real FUNC_001330 port (median cut); the AB_DPAL_LEGACY oracle blob
        // was stale for the intro (map[4]=0x85 → fill 0x84 vs ASM 0xE8).
        unsetenv("AB_DPAL_LEGACY");
        DPalClear();
        DPalBuild(g_videoState->palette, 0);
    }

    // DATA_007807 → FUNC_001682 is wired in InitFunctionPointers; reaffirm here.
    SetRenderCallback(reinterpret_cast<intptr_t>(&FUNC_001682));
    // ASM: the main loop walks the mesh tree through the real FUNC_001683
    // vertex-job path (no host paint hook). The face renderers (1712/1708)
    // call DPal32to16 to mark shade cells for the per-frame DPalBuild, so the
    // host paint hook must NOT be installed — it draws the title objects
    // directly and skips the DPal mark path, leaving every host-painted object
    // (asteroids, station) resolving to palette 0 = black.
    // AB_INTRO_HOOK=1 re-installs the old host hook for A/B comparison only.
    if (getenv("AB_INTRO_HOOK") && getenv("AB_INTRO_HOOK")[0] == '1')
        SetTitleObjectPaintHook(TitleObjectPaintHook);
    // Painter path for title constellations (see ship_renderer titleConstellation).
    s_setPhaseOBands = getenv("AB_PHASE_O_BANDS") == nullptr;
    if (s_setPhaseOBands)
        setenv("AB_PHASE_O_BANDS", "0", 0);

    // Seed title object at 0x400 even while splash holds the display clock at 0.
    SetTitleClock(s_splashActive ? 0x400u : s_clock);
    FUNC_000959(0);
    s_titleInited = true;
    if (!s_splashActive)
        MaybeDumpIntroTitleOnce();

    const ShipModel* m176 = GetShipModel(kTitleShipCredits);
    const ShipModel* m177 = GetShipModel(kTitleShipLogo);
    ApplyTitleShipDistance(m176);

    SoundPlaySong(0);
    s_songStarted = true;
    fprintf(stderr,
            "IntroCast: title 3D FUNC_000959/961→001682 "
            "(#177 job path; #176 host mesh paint) + timed credits.\n"
            "  models: #176 meshOff=%u #177 meshOff=%u dist=%d\n",
            m176 ? m176->meshDataOffset : 0,
            m177 ? m177->meshDataOffset : 0,
            (int)s_shipDist);
    if (s_viewerMode)
        fprintf(stderr,
                "  SPACE/ENTER=start ESC=quit  -/= speed  F=end  S=toggle 3D  H=HUD\n");
    if (s_playerEnabled) {
        fprintf(stderr,
                "  INTRO_PLAYER: Space=pause  ,/.=step  [/]=seek±1s  Enter=start\n"
                "  strip: play/step/speed + scrubber (seek is clock-driven, not full sim rewind)\n");
    }
}

void IntroCastSetSpeed(int ticksPerFrame)
{
    if (ticksPerFrame < 100) ticksPerFrame = 100;
    if (ticksPerFrame > 200000) ticksPerFrame = 200000;
    s_ticksPerFrame = ticksPerFrame;
}

void IntroCastSetShipAnim(bool enabled) { s_shipAnim = enabled; }
bool IntroCastGetShipAnim() { return s_shipAnim; }

int32_t IntroCastGetShipDist() { return s_shipDist; }

uint32_t IntroCastGetClock()
{
    return s_clock;
}

int IntroCastCollectHwInstances(IntroCastHwInstance* out, int maxOut)
{
    if (!out || maxOut <= 0 || !s_titleInited) return 0;

    s_suppressSoftPaint = true;
    FUNC_001786();
    SetTitleClock(s_clock);
    FUNC_000961();
    UpdateIntroCameraFromTitle();
    s_suppressSoftPaint = false;

    int type = s_shipType;
    if (type != 0xb0 && type != 0xb1) {
        // Before first 000961 tick, infer from clock like FUNC_000961.
        type = (s_clock < kCreditsStart) ? 0xb1 : 0xb0;
        s_shipType = type;
    }
    ApplyTitleShipDistance(GetShipModel(type));
    return CollectFromTitleType(type, out, maxOut);
}

// INTRO_VERBOSE=1 — one line per frame so a hang can be pinned to the exact
// scene/clock. Format:
//   INTRO v f=<frame> clock=0x<hex> phase=<name> type=<176|177> <flags> px=<n> ship=<n> marks=<n>
// and, when the clock stops advancing for kStuckFrames in a row, a STUCK line
// with the same fields (that is the frame the intro wedged on).
static const char* IntroPhaseName(uint32_t clock)
{
    if (clock < kCreditsStart) return "pre-credits";
    if (clock < kStoryUntil) return "credits";
    if (clock < kSndUntil) return "story";
    if (clock < kInteriorAfter) return "pre-interior";
    if (clock < kInteriorUntil) return "interior";
    if (clock < kPlanetAfter) return "post-interior";
    if (clock < kVidPostAfter) return "planet";
    if (clock < kJournalUntil) return "video-post";
    if (clock < kEliteAfter) return "journal";
    if (clock < kLoopReset) return "elite";
    return "loop-end";
}

static bool IntroVerboseEnabled()
{
    static const int on = []() {
        const char* e = getenv("INTRO_VERBOSE");
        return (e && e[0] == '1') ? 1 : 0;
    }();
    return on != 0;
}

// Scene progress on by default, one compact line every kProgressFrames frames
// and at every phase change: proves the cast is advancing (the credits stretch
// alone is ~6 minutes of the same view, so a silent console reads as a hang).
// INTRO_VERBOSE=1 gives the same line for every frame; INTRO_QUIET=1 silences
// this summary only.
static void IntroProgressLog()
{
    static uint64_t frameNo = 0;
    static const char* lastPhase = nullptr;
    static uint32_t lastClockMs = 0;
    static uint32_t lastFrame = 0;
    constexpr uint64_t kProgressFrames = 150;

    ++frameNo;

    // Stall detector on the default path: the clock must advance every frame.
    // 300 frozen frames (6 s at 50 fps) is a real wedge, not a slow frame.
    static uint32_t stuckClock = 0xffffffffu;
    static uint64_t stuckSince = 0;
    static uint64_t lastStuckReport = 0;
    if (s_clock != stuckClock) {
        stuckClock = s_clock;
        stuckSince = frameNo;
    } else if (frameNo - stuckSince == 300 && frameNo - lastStuckReport > 300) {
        lastStuckReport = frameNo;
        fprintf(stderr,
                "INTRO STUCK f=%llu clock=0x%x held for %llu frames (%s) — "
                "phase=%s type=%d planet=%d station=%d interior=%d battle=%d "
                "px=%d (run with INTRO_VERBOSE=1 for the per-frame detail)\n",
                (unsigned long long)frameNo, s_clock,
                (unsigned long long)(frameNo - stuckSince), "see fields",
                IntroPhaseName(s_clock), s_shipType,
                IntroPlanetBackgroundActive() ? 1 : 0,
                IntroStationPhaseActive() ? 1 : 0,
                IntroInteriorPhaseActive() ? 1 : 0,
                IntroBattlePhaseActive() ? 1 : 0, s_realPaintPixels);
    }

    const char* phase = IntroPhaseName(s_clock);
    const bool phaseChanged = (lastPhase != phase);
    if (!phaseChanged && (frameNo % kProgressFrames) != 0)
        return;
    lastPhase = phase;

    const uint32_t nowMs = PlatformGetTicks();
    double fps = 0.0;
    if (lastClockMs != 0 && frameNo > lastFrame && nowMs > lastClockMs)
        fps = (double)(frameNo - lastFrame) * 1000.0 / (double)(nowMs - lastClockMs);
    lastClockMs = nowMs;
    lastFrame = (uint32_t)frameNo;

    const bool quiet = []() {
        const char* e = getenv("INTRO_QUIET");
        return (e && e[0] == '1') ? 1 : 0;
    }() != 0;
    if (quiet && !phaseChanged)
        return;

    fprintf(stderr,
            "INTRO PROGRESS f=%llu clock=0x%x (%us) phase=%s fps=%.1f type=%d "
            "planet=%d station=%d interior=%d battle=%d px=%d\n",
            (unsigned long long)frameNo, s_clock, s_clock >> 16, phase, fps,
            s_shipType, IntroPlanetBackgroundActive() ? 1 : 0,
            IntroStationPhaseActive() ? 1 : 0, IntroInteriorPhaseActive() ? 1 : 0,
            IntroBattlePhaseActive() ? 1 : 0, s_realPaintPixels);
}

static void IntroVerboseLogFrame(const char* where)
{
    if (!IntroVerboseEnabled())
        return;
    static uint32_t lastClock = 0xffffffffu;
    static int sameClock = 0;
    static uint64_t frameNo = 0;

    if (s_clock == lastClock) {
        ++sameClock;
    } else {
        sameClock = 0;
        lastClock = s_clock;
    }
    ++frameNo;

    const bool stuck = (sameClock == 120) || (sameClock % 600 == 0 && sameClock > 0);
    fprintf(stderr,
            "%s v f=%llu clock=0x%x phase=%s type=%d planet=%d station=%d "
            "interior=%d battle=%d bridge=%d px=%d marks=%d song=%d\n",
            stuck ? "INTRO-STUCK" : "INTRO", (unsigned long long)frameNo, s_clock,
            IntroPhaseName(s_clock), s_shipType,
            IntroPlanetBackgroundActive() ? 1 : 0, IntroStationPhaseActive() ? 1 : 0,
            IntroInteriorPhaseActive() ? 1 : 0, IntroBattlePhaseActive() ? 1 : 0,
            s_useBridgeFallback ? 1 : 0, s_realPaintPixels, DPalGetChangedCount(),
            (s_songStarted && !SoundSongDone()) ? 1 : 0);
    if (sameClock == 120)
        fprintf(stderr,
                "INTRO-STUCK: clock 0x%x repeated %d frames (%s) — scene is not "
                "advancing; last frame fields above.\n",
                s_clock, sameClock, where);
}

void IntroCastDrawUiOnly()
{
    if (!g_videoState) return;
    uint8_t* buf = g_videoState->backBuffer;
    if (PlatformIsGpuDraw())
        memset(buf, 0xFF, SCREEN_WIDTH * SCREEN_HEIGHT);
    else
        memset(buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);

    if (s_splashActive) {
        // Soft/SDL splash needs a black clear (not GPU transparent 0xFF).
        memset(buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
        DrawIntroSplash(buf);
        if (s_viewerMode && ++s_splashFrames >= kSplashAutoFrames)
            EndIntroSplash();
        return;
    }

    // ASM: blank frame at the loop reset (ClearBuf; FlipScreen; ClearBuf).
    if (IntroCastLoopReset()) return;

    SetBackBufferPtr(buf);
    // Keep camera in sync even on the HW/UI-only path (task: IntroCastDrawUiOnly).
    UpdateIntroCameraFromTitle();
    MaybeDumpIntroCamera();
    MaybeDumpIntroInteriorTransition();
    DrawStars(buf);

    // Logo 0x42 blit lives in FUNC_000962 (clock > 0x795cc && DATA_008810==0).
    DrawIntroCredits(buf);

    if (s_helpVisible) {
        DrawStringSimple(buf, 2, 2, "INTRO CAST HW  SPACE=start  -/=speed  H=HUD", 0x07);
        char hud[96];
        std::snprintf(hud, sizeof(hud), "clock=%u type=%d dist=%d",
                      s_clock, s_shipType, (int)s_shipDist);
        DrawStringSimple(buf, 2, 190, hud, 0x08);
    }

    IntroVerboseLogFrame("IntroCastDrawUiOnly");
    if (!s_paused)
        s_clock += static_cast<uint32_t>(s_ticksPerFrame);

    if (s_songStarted && SoundSongDone())
        SoundPlaySong(0);
}

void IntroCastDraw()
{
    if (!g_videoState) return;
    uint8_t* buf = g_videoState->backBuffer;
    // GPU: 0xFF → transparent in OgreBlitSoftBackBuffer (navy clear shows through).
    // Soft/SDL: solid black clear (VGA 0).
    if (PlatformIsGpuDraw())
        memset(buf, 0xFF, SCREEN_WIDTH * SCREEN_HEIGHT);
    else
        memset(buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);

    // Additive splash before title clock 0x400 (ASM _asmmain).
    if (s_splashActive) {
        memset(buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
        DrawIntroSplash(buf);
        if (s_viewerMode && ++s_splashFrames >= kSplashAutoFrames)
            EndIntroSplash();
        return;
    }

    // ASM: blank frame at the loop reset (ClearBuf; FlipScreen; ClearBuf).
    if (IntroCastLoopReset()) return;

    // JUMP_003840 order: arena reset → background planet → title 3D → overlays.
    // Title 0xb0/0xb1 use TitleObjectPaintHook (immediate soft paint), so planet
    // jobs must flush before FUNC_000961 or the body would cover the ships.
    SetBackBufferPtr(buf);
    if (s_shipAnim && s_titleInited) {
        // Sync ObjectStruct yaw from this frame's clock before camera + paints so
        // station/ship TransformShipVertices see the tracking orbit immediately.
        SetTitleClock(s_clock);
        if (uint8_t* title = GetTitleObject()) {
            *reinterpret_cast<uint16_t*>(title + 0x9c) =
                static_cast<uint16_t>(TitleYawFromClock(s_clock));
        }
        UpdateIntroCameraFromTitle();
        MaybeDumpIntroCamera();

        const char* dbg = getenv("JJFFE_DEBUG_INTRO");
        const bool wantDiag = dbg && dbg[0] == '1';
        static uint32_t s_planetSnapFrame = 0;
        // Snapshot only on credits frames that will emit INTRO-PLANET.
        const bool mayPlanet = (s_clock >= kCreditsStart) && !s_suppressSoftPaint;
        const bool snapThisFrame =
            wantDiag && mayPlanet && ((++s_planetSnapFrame % 15u) == 1u);

        uint8_t before[SCREEN_SIZE];
        if (snapThisFrame && g_videoState)
            std::memcpy(before, g_videoState->backBuffer, SCREEN_SIZE);

        FUNC_001786();
        const bool stationDrawn = DrawIntroStationBackground();
        int pixelsBeforeShips = 0;
        const uint8_t clear = PlatformIsGpuDraw() ? 0xFFu : 0u;
        if (stationDrawn) {
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                if (buf[i] != clear) pixelsBeforeShips++;
            }
        }
        SetTitleClock(s_clock);
        FUNC_000961();
        // ASM main loop (JUMP_003844): after the title tick — DPalBuild, then
        // FUNC_001618_ClearUpperBuf fills the upper 50560 bytes with the
        // render-state colour (DATA_009222 = DPal of the {0,0,4} Earth record),
        // then FUNC_001789 paints the jobs on top. The ASM calls these
        // unconditionally every frame (not gated on any flag).
        DPalBuild(g_videoState->palette, 0);
        ClearUpperBuf();
        // Mercury body host after ClearUpperBuf (title Contour ring alone left
        // a hollow disc; early enqueue was wiped by this clear).
        UpdateIntroCameraFromTitle();
        const bool planetQueued = !stationDrawn && EnqueueIntroPlanetBackground();
        if (planetQueued && snapThisFrame)
            MaybeDumpIntroPlanetAfterFlush(before, snapThisFrame);
        MaybeDumpIntroInteriorTransition();
        FUNC_001789();
        if (planetQueued)
            PlanetAbGetTrace()->active = false;

        // Bridge gate uses ship-added pixels only (ignore background planet).
        s_realPaintPixels = 0;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            if (buf[i] != clear) s_realPaintPixels++;
        }
        const int shipPixels = s_realPaintPixels - pixelsBeforeShips;

        // AB_INTRO_PX=1 — per-frame painted-pixel attribution. The single px
        // number cannot say *which* pass lost pixels, so scene "resets" (a
        // steady drain then a one-frame snap back) were unattributable. This
        // splits background/station (pre), title jobs (ships), combat FX and
        // credits text, and carries the hull stats for the same frame.
        static const bool s_pxTrace = []() {
            const char* e = getenv("AB_INTRO_PX");
            return e && e[0] == '1';
        }();
        if (shipPixels > 200)
            s_useBridgeFallback = false;
        else if (getenv("INTRO_FORCE_BRIDGE") && getenv("INTRO_FORCE_BRIDGE")[0] == '1')
            s_useBridgeFallback = true;

        if (s_useBridgeFallback) {
            // Stars only under fallback — real title scene paints its own space.
            DrawStars(buf);
            DrawTitleShipBridge(buf);
        }

        // Combat destruction FX — OFF by default (faithfulness).
        //
        // The port invented a paced hit every hitPeriod ticks (0x28000 / 0x18000
        // once past kBigBoomAfter) inside the whole battle window
        // (0x3f868c..0x67ba12), each spawning an expanding FUNC_000939-style
        // sphere, debris and a camera jink. ffeasm does not do that: a
        // natural-pace tap on FUNC_000939 (tools/capture_ffeasm_fx.sh) records
        // **zero** spawns through the first 9,000 flips of the cast (clock
        // 0x44459f), and with the FX skipped the port's frame ink matches
        // ffeasm exactly at 0x48a1ef (IoU 1.000) while with them it is ~12,000
        // pixels higher. On screen that pacing reads as "the same scene
        // repeating" every ~3.5 s.
        //
        // INTRO_COMBAT_FX=1 re-enables the invented pass (it is kept, not
        // deleted: the real cast does have a dogfight beat to be ported from
        // the ASM later). AB_INTRO_NOFX=1 stays as the explicit "off" knob.
        static const bool s_combatFxEnv = []() {
            const char* e = getenv("INTRO_COMBAT_FX");
            return e && e[0] == '1';
        }();
        if (s_combatFxEnv && !getenv("AB_INTRO_NOFX"))
            UpdateAndDrawIntroCombat(buf);

        if (s_pxTrace) {
            int pxAfterFx = 0;
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                if (buf[i] != clear) pxAfterFx++;
            }
            fprintf(stderr,
                    "INTRO-PX clock=0x%x f=%u pre=%d total=%d ships=%d fx=%d "
                    "station=%d planet=%d polys=%d vis=%d type=%d\n",
                    s_clock, s_clock / static_cast<uint32_t>(s_ticksPerFrame),
                    pixelsBeforeShips, s_realPaintPixels, shipPixels,
                    pxAfterFx - s_realPaintPixels, stationDrawn ? 1 : 0,
                    planetQueued ? 1 : 0, s_shipPolys, s_shipVis, s_shipType);
        }
    } else {
        DrawStars(buf);
    }

    // Logo 0x42 blit lives in FUNC_000962 (clock > 0x795cc && DATA_008810==0).
    DrawIntroCredits(buf);

    if (s_helpVisible) {
        char hud[128];
        std::snprintf(hud, sizeof(hud),
                      "clock=%u spd=%d type=%d vis=%d px=%d%s",
                      s_clock, s_ticksPerFrame, s_shipType, s_shipVis,
                      s_realPaintPixels,
                      s_useBridgeFallback
                          ? (s_usedStarFallback ? " fb" : " bridge")
                          : " job");
        DrawStringSimple(buf, 2, 190, hud, 0x08);
        DrawStringSimple(buf, 2, 2, "INTRO CAST  SPACE=start  S=3D  -/=speed  H=HUD", 0x07);
    }

    // A/B vs ffeasm: dump the finished frame once the clock reaches the target.
    MaybeDumpIntroFrame();
    if (getenv("AB_1F_FRAME_COUNTERS")) {
        extern int g_ab_frame_1786, g_ab_frame_1789, g_ab_frame_1840;
        fprintf(stderr,
                "AB-FRAMECOUNTS 1786=%d 1789=%d 1840=%d clock=0x%x\n",
                g_ab_frame_1786, g_ab_frame_1789, g_ab_frame_1840, s_clock);
    }
    IntroVerboseLogFrame("IntroCastDraw");
    IntroProgressLog();
    // ASM ffe_intro_walk.gdb.tpl re-forces DATA_008804=$target every
    // FUNC_000961 while counting FlipScreens to 5. Pin the title clock the
    // same way while a dump hold is in progress so Earth settles.
    if (!s_dumpFrameDone && s_dumpFramePath && s_dumpFramePath[0] &&
        s_clock >= s_dumpFrameClock && s_dumpFrameClock != 0xffffffffu) {
        s_clock = s_dumpFrameClock;
        SetTitleClock(s_clock);
    } else if (!s_paused) {
        s_clock += static_cast<uint32_t>(s_ticksPerFrame);
    }

    if (s_songStarted && SoundSongDone())
        SoundPlaySong(0);
}

namespace {

// The scancode that went up→down since the last poll (the DOS driver's
// last-key byte read by FUNC_001429_GuiReadAllInput), or a JJFFE_INTRO_KEYS
// injection for this poll; -1 = none.
int IntroNextKeyPress(const uint8_t* keyStates)
{
    int scancode = -1;
    for (int i = 1; i < 256; i++) {
        if (keyStates[i] == 0 && s_prevKeys[i] != 0) {
            scancode = i;
            break;
        }
    }
    std::memcpy(s_prevKeys, keyStates, sizeof(s_prevKeys));
    for (int i = 0; i < s_injectedKeyCount; i++) {
        if (s_injectedKeys[i].poll == s_inputPoll)
            scancode = s_injectedKeys[i].scancode;
    }
    ++s_inputPoll;
    return scancode;
}

// FUNC_001429_GuiReadAllInput (ffe.asm 85933–85940): scancode → DATA_007688
// action, upper (unshifted) half unless LSHIFT or RSHIFT is down; 0xff (no
// action) and no key both yield 0.
uint8_t IntroGuiAction(int scancode, const uint8_t* keyStates)
{
    const uint8_t* blob = data::GetDataBlob();
    if (scancode < 0 || !blob) return 0;
    const unsigned half = keyStates[KEY_LSHIFT] & keyStates[KEY_RSHIFT] & 0x80u;
    const uint8_t a =
        blob[data::OFFSET_DATA_007688 + ((static_cast<unsigned>(scancode) & 0x7fu) | half)];
    return (a == 0xff) ? 0 : a;
}

int32_t BlobI32(uint32_t offset)
{
    int32_t v = 0;
    if (const uint8_t* blob = data::GetDataBlob())
        std::memcpy(&v, blob + offset, sizeof(v));
    return v;
}

} // namespace

void IntroCastSetViewerMode(bool viewer)
{
    s_viewerMode = viewer;
}

bool IntroCastInSplash()
{
    return s_splashActive;
}

int IntroCastGetActionId()
{
    return s_actionId;
}

void IntroCastShutdown()
{
    if (s_setPhaseOBands) {
        unsetenv("AB_PHASE_O_BANDS");
        s_setPhaseOBands = false;
    }
}

int IntroCastHandleInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    // A/B capture mode: exit right after the one-shot frame dump (keeps the
    // job trace to the frames that matter).
    if (s_dumpFrameDone)
        return -1;

    const bool dig = s_viewerMode || s_playerEnabled;
    if (dig && keyStates[KEY_ESCAPE] == 0)
        return -1;

    uint8_t action = IntroGuiAction(IntroNextKeyPress(keyStates), keyStates);

    // .splashloop (ffe.asm 68530–68541): the first GUI action leaves the loop;
    // 'y' continues, any other action is FUNC_001529_SysExit.
    if (s_splashActive) {
        if (action == 'y') {
            EndIntroSplash();
            s_splashTimeoutActive = true;
            s_splashTimeoutEnd = TimerGetTimeStamp() + kSplashTimeoutMs;
        } else if (action != 0) {
            return -1;
        }
        return 0;
    }
    if (s_splashTimeoutActive) {
        if (static_cast<int32_t>(TimerGetTimeStamp() - s_splashTimeoutEnd) < 0)
            action = 0;
        else
            s_splashTimeoutActive = false;
    }

    // FUNC_000962 'V' gate (DATA_007237): reset before the credits, set on
    // action 0x56 (Shift+V). Gated text is 0x942d = "English Release 1.1
    // (Remastered Version)" at (10,60) — the production banner (0x941b) draws
    // UNGATED in the first credit window. Sticky through the rest of the cast.
    if (s_clock < kCreditsStart)
        SetIntroProductionFlag(0);
    else if (action == 'V')
        SetIntroProductionFlag(1);

    // JUMP_003842 (ffe.asm 68622–68635): '1'–'5' pick edi from DATA_007191
    // (landing in DATA_007199 = {0,1,2,3,6,5}); 'V' is ignored; any other
    // action takes the default id [DATA_007199].
    if (action >= '1' && action <= '5') {
        s_actionId = BlobI32(data::OFFSET_DATA_007191 + action * 4u);
        return 1;
    }
    if (!dig && action != 0 && action != 'V') {
        s_actionId = BlobI32(data::OFFSET_DATA_007199);
        return 1;
    }
    if (!dig)
        return 0;

    // INTRO_PLAYER: strip click (edge-triggered) — absorb so game doesn't see it.
    if (s_playerEnabled) {
        static bool prevLmb = false;
        const int buttons = InputMouseReadButtons();
        const bool lmb = (buttons & 1) != 0;
        if (lmb && !prevLmb) {
            int mx = 0, my = 0;
            PlatformGetWindowMousePos(&mx, &my);
            if (my >= 400)
                IntroPlayerHitTest(mx, my);
        }
        prevLmb = lmb;
    }

    // Viewer start: Enter always; Space only when player mode is off.
    if (keyStates[KEY_ENTER] == 0 ||
        (!s_playerEnabled && keyStates[KEY_SPACE] == 0)) {
        s_actionId = BlobI32(data::OFFSET_DATA_007199);
        return 1;
    }

    static bool prevMinus = true, prevPlus = true, prevR = true, prevF = true;
    static bool prevH = true, prevS = true;
    static bool prevSpace = true, prevComma = true, prevPeriod = true;
    static bool prevLBr = true, prevRBr = true;
    bool minus = keyStates[KEY_MINUS] == 0;
    bool plus = keyStates[KEY_EQUALS] == 0 || keyStates[KEY_PLUS] == 0;
    bool rKey = keyStates[KEY_R] == 0;
    bool fKey = keyStates[KEY_F] == 0;
    bool hKey = keyStates[KEY_H] == 0;
    bool sKey = keyStates[KEY_S] == 0;
    if (minus && !prevMinus) IntroCastSetSpeed(s_ticksPerFrame / 2);
    if (plus && !prevPlus) IntroCastSetSpeed(s_ticksPerFrame * 2);
    if (rKey && !prevR) IntroCastSetSpeed(1200);
    if (fKey && !prevF) s_clock = kEliteAfter;
    if (hKey && !prevH) s_helpVisible = !s_helpVisible;
    if (sKey && !prevS) s_shipAnim = !s_shipAnim;
    prevMinus = minus; prevPlus = plus; prevR = rKey; prevF = fKey;
    prevH = hKey; prevS = sKey;

    if (s_playerEnabled) {
        const bool space = keyStates[KEY_SPACE] == 0;
        const bool comma = keyStates[KEY_COMMA] == 0;
        const bool period = keyStates[KEY_PERIOD] == 0;
        const bool lbr = keyStates[KEY_LEFTBRACKET] == 0;
        const bool rbr = keyStates[KEY_RIGHTBRACKET] == 0;
        if (space && !prevSpace) s_paused = !s_paused;
        if (comma && !prevComma) IntroCastStep(-1);
        if (period && !prevPeriod) IntroCastStep(1);
        // ±1s of cast time ≈ kLoopReset / kCastLengthSec ticks.
        const int ticksPerSec =
            static_cast<int>(kLoopReset / kCastLengthSec);
        if (lbr && !prevLBr)
            IntroCastSetClock(s_clock > static_cast<uint32_t>(ticksPerSec)
                                  ? s_clock - static_cast<uint32_t>(ticksPerSec)
                                  : 0u);
        if (rbr && !prevRBr)
            IntroCastSetClock(s_clock + static_cast<uint32_t>(ticksPerSec));
        prevSpace = space;
        prevComma = comma;
        prevPeriod = period;
        prevLBr = lbr;
        prevRBr = rbr;
    }

    return 0;
}

} // namespace jjffe
