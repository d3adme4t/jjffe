#include "jjffe/demo3d.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/draw.h"
#include "jjffe/draw_main.h"
#include "jjffe/math.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/platform.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/planet_render.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef JJFFE_USE_SDL
#include <SDL.h>
#endif

namespace jjffe {

static Camera3D camera;
static int32_t shipYaw = 0;
static int32_t shipPitch = 0;
static int32_t shipDist = FP_ONE / 2;
static int s_fitDistUnits = 1;
static bool s_initialized = false;
static bool s_paused = false;
static bool s_keyPWasDown = false;
static bool s_keyF12WasDown = false;
static bool s_keyDigitWasDown[8] = {};
static int s_demoShipIndex = SHIP_INDEX_DEMO;

// Match asm_demo/ship_demo.cpp viewer rates (same A/D→yaw, Q/E→pitch mapping).
static constexpr int32_t kYawStep = 0x80;     /* ~0.7°/frame */
static constexpr int32_t kPitchStep = 0x60;
static constexpr int32_t kZoomStep = FP_ONE / 32;
static constexpr int32_t kSpinStep = 0x40;
// LMB drag: ~1 key-step per pixel (same sense as A/D and Q/E).
static constexpr int32_t kMouseYawPerPix = 0x80;
static constexpr int32_t kMousePitchPerPix = 0x60;

// Sparse starfield behind the ship. Adder canopy 0x20ee punches VGA0 (ASM
// DPal), which is invisible on a pure-black clear — stars make the windshield
// read as glass. A/B captures keep a black clear (ab_capture_shuttle).
static void FillDemoStarfield(uint8_t* backBuffer)
{
    memset(backBuffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT);
    // Deterministic pseudo-stars (stable across frames).
    for (int n = 0; n < 96; n++) {
        uint32_t h = 2654435761u * (uint32_t)(n + 1);
        int x = (int)((h >> 3) % SCREEN_WIDTH);
        int y = (int)((h >> 13) % SCREEN_HEIGHT);
        // Dim greys from the VGA ramp — avoid bright HUD colors.
        uint8_t c = (h & 1u) ? 0x02 : 0x01;
        if ((h & 7u) == 0)
            c = 0x04;
        backBuffer[y * SCREEN_WIDTH + x] = c;
    }
}

#ifdef JJFFE_USE_SDL
static constexpr Uint32 kTargetFrameMs = 33; /* ~30 Hz — same as ASM ship_demo */
#endif

// --- Ship select -----------------------------------------------------------

static int s_selectList[SHIP_MODEL_COUNT];
static int s_selectCount = 0;
static int s_selectCursor = 0;   // index into s_selectList
static int s_selectScroll = 0;   // first visible row
static bool s_selectKeyHeld[256] = {};

static constexpr int kSelectVisibleRows = 12;
static constexpr int kSelectRowH = 12;

// Names from srctools/shipidx.txt (indices 0–71). Beyond that: "Ship N".
static const char* const kShipNames[] = {
    "intro text?",           // 0
    "?",                     // 1
    "intro text?",           // 2
    "?",                     // 3
    "Mine",                  // 4
    "Mine",                  // 5
    "Missile",               // 6
    "Smart Missile",         // 7
    "Naval Missile",         // 8
    "Assault Missile",       // 9
    "Assault Missile",       // 10
    "Assault Missile",       // 11
    "Mycoid Missile",        // 12
    "Nuclear Missile",       // 13
    "Escape Capsule",        // 14
    "Interplanetary Shuttle",// 15
    "StowMaster Fighter",    // 16
    "Unknown (thargoid)",    // 17
    "Lifter",                // 18
    "Osprey attack fighter", // 19
    "Falcon attack fighter", // 20
    "Hawk airfighter",       // 21
    "Kestrel airfighter",    // 22
    "Eagle long range fighter", // 23
    "Eagle Mk2 fighter",     // 24
    "Eagle Mk3 fighter",     // 25
    "Sidewinder",            // 26
    "Krait Assault Craft",   // 27
    "Gecko",                 // 28
    "Adder",                 // 29
    "Viper Defence Craft",   // 30
    "Saker III fighter",     // 31
    "Osprey X attack fighter", // 32
    "Merlin attack fighter", // 33
    "Viper Defence MkII",    // 34
    "Gyr attack fighter",    // 35
    "Cobra Mk I",            // 36
    "Moray Starboat",        // 37
    "Cobra Mk III",          // 38
    "Constrictor",           // 39
    "Asp Explorer",          // 40
    "Lanner",                // 41
    "Harris fighter",        // 42
    "Spar attack fighter",   // 43
    "Wyvern Explorer",       // 44
    "Skeet cruiser",         // 45
    "Turner Class",          // 46
    "Lanner II",             // 47
    "Harrier",               // 48
    "Unknown (thargoid)",    // 49
    "Unknown (thargoid)",    // 50
    "Unknown (thargoid)",    // 51
    "Unknown (thargoid)",    // 52
    "Transporter",           // 53
    "Lion Transport",        // 54
    "Tiger Trader",          // 55
    "Imperial Courier",      // 56
    "Python Freighter",      // 57
    "Imperial Trader",       // 58
    "Anaconda Freighter",    // 59
    "Puma Clipper",          // 60
    "Boa Freighter",         // 61
    "Panther Clipper",       // 62
    "Unknown (thargoid)",    // 63
    "Tiercel Freighter",     // 64
    "Imperial Explorer",     // 65
    "IMRA Command Ship",     // 66
    "Mantis Transport",      // 67
    "Griffin Carrier",       // 68
    "Thargoid Transporter",  // 69
    "Lynx Bulk Carrier",     // 70
    "Long-range Cruiser",    // 71
};
static constexpr int kNamedShipCount =
    static_cast<int>(sizeof(kShipNames) / sizeof(kShipNames[0]));

static const char* ShipDisplayName(int index, char* buf, int bufSize)
{
    if (index >= 0 && index < kNamedShipCount) {
        return kShipNames[index];
    }
    snprintf(buf, bufSize, "Ship %d", index);
    return buf;
}

static void ClampSelectScroll()
{
    if (s_selectCursor < s_selectScroll) {
        s_selectScroll = s_selectCursor;
    }
    if (s_selectCursor >= s_selectScroll + kSelectVisibleRows) {
        s_selectScroll = s_selectCursor - kSelectVisibleRows + 1;
    }
    if (s_selectScroll < 0) s_selectScroll = 0;
    int maxScroll = s_selectCount - kSelectVisibleRows;
    if (maxScroll < 0) maxScroll = 0;
    if (s_selectScroll > maxScroll) s_selectScroll = maxScroll;
}

void InitDemoShipSelect()
{
    s_selectCount = 0;
    s_selectCursor = 0;
    s_selectScroll = 0;
    memset(s_selectKeyHeld, 0, sizeof(s_selectKeyHeld));
    // Enter is still down from Main Menu → 3D Demo; Esc may be down when
    // returning from the viewer. Ignore until release or the list is skipped.
    s_selectKeyHeld[KEY_ENTER] = true;
    s_selectKeyHeld[KEY_ESCAPE] = true;
    s_selectKeyHeld[KEY_UP] = true;
    s_selectKeyHeld[KEY_DOWN] = true;
    s_selectKeyHeld[KEY_LEFT] = true;
    s_selectKeyHeld[KEY_RIGHT] = true;
    s_selectKeyHeld[KEY_PGUP] = true;
    s_selectKeyHeld[KEY_PGDN] = true;
    s_selectKeyHeld[KEY_W] = true;
    s_selectKeyHeld[KEY_S] = true;
    s_selectKeyHeld[KEY_A] = true;
    s_selectKeyHeld[KEY_D] = true;

    for (int i = 0; i < SHIP_MODEL_COUNT; i++) {
        const ShipModel* m = GetShipModel(i);
        if (!m || !m->meshDataOffset) continue;
        s_selectList[s_selectCount++] = i;
    }

    // Prefer Saker III (or last demo ship) as initial highlight
    int prefer = s_demoShipIndex;
    for (int i = 0; i < s_selectCount; i++) {
        if (s_selectList[i] == prefer) {
            s_selectCursor = i;
            break;
        }
    }
    ClampSelectScroll();
    printf("Demo ship select: %d models with mesh data\n", s_selectCount);
}

void DrawDemoShipSelect()
{
    uint8_t* backBuffer = g_videoState->backBuffer;
    memset(backBuffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT);

    DrawStringLarge(backBuffer, 70, 8, "Select Ship", 0x0F);
    DrawStringSimple(backBuffer, 10, 28, "W/S move  PgUp/PgDn page  ENTER=demo  ESC=menu", 0x0D);

    if (s_selectCount == 0) {
        DrawStringSimple(backBuffer, 40, 80, "No ship models with mesh data", 0x0E);
        return;
    }

    char nameBuf[48];
    char line[80];
    int baseY = 44;
    int end = s_selectScroll + kSelectVisibleRows;
    if (end > s_selectCount) end = s_selectCount;

    for (int row = s_selectScroll; row < end; row++) {
        int shipIdx = s_selectList[row];
        const char* name = ShipDisplayName(shipIdx, nameBuf, sizeof(nameBuf));
        int y = baseY + (row - s_selectScroll) * kSelectRowH;
        int x = 20;

        snprintf(line, sizeof(line), "%3d  %s", shipIdx, name);

        if (row == s_selectCursor) {
            int rectW = MeasureStringWidth(line) + 8;
            if (rectW > 300) rectW = 300;
            FillRect(backBuffer, x - 4, y - 2, rectW, 12, 0x1F);
            DrawStringSimple(backBuffer, x, y, line, 0x00);
        } else {
            DrawStringSimple(backBuffer, x, y, line, 0x0E);
        }
    }

    // Scroll position hint
    snprintf(line, sizeof(line), "%d / %d", s_selectCursor + 1, s_selectCount);
    DrawStringSimple(backBuffer, 240, 180, line, 0x0D);
}

int HandleDemoShipSelectInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    auto edge = [&](int key) -> bool {
        if (keyStates[key] == 0) {
            if (!s_selectKeyHeld[key]) {
                s_selectKeyHeld[key] = true;
                return true;
            }
        } else {
            s_selectKeyHeld[key] = false;
        }
        return false;
    };

    if (s_selectCount > 0) {
        if (edge(KEY_UP) || edge(KEY_W)) {
            s_selectCursor--;
            if (s_selectCursor < 0) s_selectCursor = s_selectCount - 1;
            ClampSelectScroll();
        }
        if (edge(KEY_DOWN) || edge(KEY_S)) {
            s_selectCursor++;
            if (s_selectCursor >= s_selectCount) s_selectCursor = 0;
            ClampSelectScroll();
        }
        // Page with PgUp/PgDn (also Left/Right or A/D)
        if (edge(KEY_PGUP) || edge(KEY_LEFT) || edge(KEY_A)) {
            s_selectCursor -= kSelectVisibleRows;
            if (s_selectCursor < 0) s_selectCursor = 0;
            ClampSelectScroll();
        }
        if (edge(KEY_PGDN) || edge(KEY_RIGHT) || edge(KEY_D)) {
            s_selectCursor += kSelectVisibleRows;
            if (s_selectCursor >= s_selectCount) s_selectCursor = s_selectCount - 1;
            ClampSelectScroll();
        }
        if (edge(KEY_ENTER)) {
            s_demoShipIndex = s_selectList[s_selectCursor];
            return 1;
        }
    }

    if (edge(KEY_ESCAPE)) {
        return -1;
    }

    return 0;
}

int GetDemoShipIndex()
{
    return s_demoShipIndex;
}

int GetDemoShipSelectCount()
{
    return s_selectCount;
}

int GetDemoShipSelectCursor()
{
    return s_selectCursor;
}

int GetDemoShipSelectAt(int row)
{
    if (row < 0 || row >= s_selectCount) return -1;
    return s_selectList[row];
}

const char* GetDemoShipDisplayName(int shipIdx, char* buf, size_t buflen)
{
    return ShipDisplayName(shipIdx, buf, static_cast<int>(buflen));
}

int GetDemoShipSelectScroll()
{
    return s_selectScroll;
}

void SetDemoShipIndex(int idx)
{
    s_demoShipIndex = idx;
}

static bool s_directLaunch = false;
void SetDemoDirectLaunch(bool direct)
{
    s_directLaunch = direct;
}
bool IsDemoDirectLaunch()
{
    return s_directLaunch;
}

static bool s_legacySoft = false;
void SetDemoLegacySoft(bool legacy)
{
    s_legacySoft = legacy;
}
bool IsDemoLegacySoft()
{
    return s_legacySoft;
}

static bool s_exportAsset = false;
static char s_exportAssetRoot[512] = "export";
void SetDemoExportAsset(bool on)
{
    s_exportAsset = on;
}
bool IsDemoExportAsset()
{
    return s_exportAsset;
}
void SetDemoExportAssetRoot(const char* root)
{
    if (!root || !root[0]) {
        std::snprintf(s_exportAssetRoot, sizeof(s_exportAssetRoot), "export");
        return;
    }
    std::snprintf(s_exportAssetRoot, sizeof(s_exportAssetRoot), "%s", root);
}
const char* GetDemoExportAssetRoot()
{
    return s_exportAssetRoot;
}

static bool s_useExport = false;
static bool s_assetShipsAll = false;
static bool s_assetShipsParsed = false;
static int s_assetShipIds[128];
static int s_assetShipCount = 0;

void SetDemoUseExport(bool on)
{
    s_useExport = on;
}
bool IsDemoUseExport()
{
    return s_useExport;
}

void SetDemoAssetShipsFromEnv()
{
    s_assetShipsParsed = true;
    s_assetShipsAll = false;
    s_assetShipCount = 0;
    const char* env = std::getenv("JJFFE_ASSET_SHIPS");
    if (!env || !env[0]) return;
    if (std::strcmp(env, "all") == 0 || std::strcmp(env, "ALL") == 0) {
        s_assetShipsAll = true;
        return;
    }
    const char* p = env;
    while (*p && s_assetShipCount < (int)(sizeof(s_assetShipIds) / sizeof(s_assetShipIds[0]))) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        char* end = nullptr;
        long v = std::strtol(p, &end, 10);
        if (end == p) break;
        s_assetShipIds[s_assetShipCount++] = (int)v;
        p = end;
    }
}

bool DemoShipUsesExportAsset(int shipIdx)
{
    if (s_exportAsset) return true;  // --export-ship audit mode
    if (s_useExport) return true;    // --use-export
    if (!s_assetShipsParsed) SetDemoAssetShipsFromEnv();
    if (s_assetShipsAll) return true;
    for (int i = 0; i < s_assetShipCount; i++) {
        if (s_assetShipIds[i] == shipIdx) return true;
    }
    return false;
}

// --- Demo view -------------------------------------------------------------

// A/B preset poses (degrees) — keys 1-8
struct AbPose { int yawDeg; int pitchDeg; int dist; const char* name; };
static const AbPose s_abPoses[8] = {
    {180,   0, 1, "front"},
    {  0,   0, 1, "back"},
    { 90,   0, 1, "left"},
    {270,   0, 1, "right"},
    {180,  90, 1, "top"},
    {180, -90, 1, "bottom"},
    {135,   0, 1, "diag_front_left"},
    {225,   0, 1, "diag_front_right"},
};

static int32_t DegToFp(int deg)
{
    return (int32_t)((deg * 65536LL) / 360);
}

static void ApplyAbPose(int index)
{
    if (index < 0 || index >= 8) return;
    g_abPoseIndex = index;
    shipYaw = DegToFp(s_abPoses[index].yawDeg);
    shipPitch = DegToFp(s_abPoses[index].pitchDeg);
    shipDist = s_fitDistUnits * (FP_ONE / 2);
    s_paused = true;
    printf("A/B pose %d: %s (yaw=%d pitch=%d distUnits=%d) [PAUSED]\n",
           index + 1, s_abPoses[index].name,
           s_abPoses[index].yawDeg, s_abPoses[index].pitchDeg,
           s_fitDistUnits);
}

static void RefreshDemoAutoFit()
{
    const ShipModel* model = GetShipModel(s_demoShipIndex);
    if (model && (IsPlanetBodyModel(s_demoShipIndex) || IsPlanetBodyScale(model->scaleRelated)))
        s_fitDistUnits = PlanetBodyFitDistUnits(model->radius, model->scaleRelated);
    else if (model)
        s_fitDistUnits = AbAutoFitDistUnits(model->radius, model->scaleRelated);
    else
        s_fitDistUnits = 1;
    shipDist = s_fitDistUnits * (FP_ONE / 2);
}

bool InitDemo3D()
{
    CameraInit(&camera);
    CameraSetPosition(&camera, 0, 0, 0);
    shipYaw = DegToFp(180);
    shipPitch = 0;
    RefreshDemoAutoFit();
    InitMeshOpcodeHandlers();
    SetTextureRasterDebug(false);
    SetDebugFaceColors(false);
    // Phase B0: DPal must be built so ShadeRgbNibbleToPal / DPal16toPal resolve colors.
    // Median-cut (FIX_REV=2b) is default for cell-diff work, but returns 0x80+ VGA
    // bank indices that paint black/wrong on soft+D3D demos until the Q2 dump
    // closes. Pin oracle blob unless the caller already set AB_DPAL_LEGACY.
    if (g_videoState) {
        if (!getenv("AB_DPAL_LEGACY"))
            setenv("AB_DPAL_LEGACY", "1", 0);
        DPalClear();
        DPalBuild(g_videoState->palette, 0);
    }
    s_initialized = true;
    s_paused = true;
    s_keyPWasDown = false;
    s_keyF12WasDown = false;
    memset(s_keyDigitWasDown, 0, sizeof(s_keyDigitWasDown));

    char nameBuf[48];
    const char* name = ShipDisplayName(s_demoShipIndex, nameBuf, sizeof(nameBuf));
    printf("Demo3D ship %d (%s) distUnits=%d: LMB-drag orbit, 1-8=poses, F12=shot, P=spin, A/D yaw, Q/E pitch, ESC=select\n",
           s_demoShipIndex, name, s_fitDistUnits);
    return true;
}

void DrawDemo3D()
{
#ifdef JJFFE_USE_SDL
    const Uint32 frameStart = SDL_GetTicks();
#endif
    uint8_t* backBuffer = g_videoState->backBuffer;
    FillDemoStarfield(backBuffer);

    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    if (keyStates[KEY_A] == 0) shipYaw -= kYawStep;
    if (keyStates[KEY_D] == 0) shipYaw += kYawStep;
    if (keyStates[KEY_W] == 0) {
        shipDist -= kZoomStep;
        int32_t zmin = FP_ONE / 4;
        const ShipModel* zm = GetShipModel(s_demoShipIndex);
        if (zm && (IsPlanetBodyModel(s_demoShipIndex) || IsPlanetBodyScale(zm->scaleRelated))) {
            const int64_t bodyMin = static_cast<int64_t>(s_fitDistUnits) * (FP_ONE / 2) / 2;
            if (bodyMin > zmin)
                zmin = static_cast<int32_t>(bodyMin);
        }
        if (shipDist < zmin)
            shipDist = zmin;
    }
    if (keyStates[KEY_S] == 0) {
        shipDist += kZoomStep;
        int32_t zmax = 20 * FP_ONE;
        const ShipModel* zm = GetShipModel(s_demoShipIndex);
        if (zm && (IsPlanetBodyModel(s_demoShipIndex) || IsPlanetBodyScale(zm->scaleRelated))) {
            int64_t cap = static_cast<int64_t>(s_fitDistUnits) * (FP_ONE / 2) * 4;
            if (cap > 20000000)
                cap = 20000000;
            if (cap > zmax)
                zmax = static_cast<int32_t>(cap);
        }
        if (shipDist > zmax)
            shipDist = zmax;
    }
    if (keyStates[KEY_Q] == 0) shipPitch -= kPitchStep;
    if (keyStates[KEY_E] == 0) shipPitch += kPitchStep;

    // LMB drag orbit (demo-only). Always consume mickeys so release doesn't
    // apply a stale delta. Match asm_demo/ship_demo.cpp RunViewer.
    {
        int mx = 0, my = 0;
        InputMouseReadMickeys(&mx, &my);
        if ((InputMouseReadButtons() & 1) != 0 && (mx != 0 || my != 0)) {
            shipYaw += static_cast<int32_t>(mx) * kMouseYawPerPix;
            shipPitch += static_cast<int32_t>(my) * kMousePitchPerPix;
            s_paused = true;
        }
    }

    if (keyStates[KEY_P] == 0) {
        if (!s_keyPWasDown) {
            s_paused = !s_paused;
            s_keyPWasDown = true;
        }
    } else {
        s_keyPWasDown = false;
    }

    if (keyStates[KEY_F12] == 0) {
        if (!s_keyF12WasDown) {
            ScrnShotSave();
            printf("Screenshot saved (scrnshotNNN.bmp)\n");
            s_keyF12WasDown = true;
        }
    } else {
        s_keyF12WasDown = false;
    }

    static const uint8_t digitKeys[8] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    for (int i = 0; i < 8; i++) {
        if (keyStates[digitKeys[i]] == 0) {
            if (!s_keyDigitWasDown[i]) {
                ApplyAbPose(i);
                s_keyDigitWasDown[i] = true;
            }
        } else {
            s_keyDigitWasDown[i] = false;
        }
    }

    if (!s_paused) shipYaw += kSpinStep;

    Mat33 rotMat;
    // Match ASM FUNC_001675_MatBuildYZT(obj, yaw, pitch). Default: Sin32-scale
    // for FixedMulHigh vertex muls. AB_FP16_VERT=1 → scale to 16.16 + >>16.
    MatBuildYZT(&rotMat, shipYaw, shipPitch);
    if (getenv("AB_FP16_VERT") && getenv("AB_FP16_VERT")[0] == '1')
        MatScaleSin32ToFp16(&rotMat);

    for (int i = 0; i < 9; i++) camera.rotation.m[i] = 0;
    camera.rotation.m[0] = FP_ONE;
    camera.rotation.m[4] = FP_ONE;
    camera.rotation.m[8] = FP_ONE;

    const ShipModel* model = GetShipModel(s_demoShipIndex);
    if (!model || !model->meshDataOffset) {
        DrawStringSimple(backBuffer, 10, 10, "No ship model", 0x0F);
        return;
    }

    if (IsPlanetBodyModel(s_demoShipIndex) || IsPlanetBodyScale(model->scaleRelated)) {
        InitMeshInterpreterTable();
        DrawPlanetInterpreter(s_demoShipIndex, shipYaw, shipPitch, shipDist);

        char txt[80];
        char nameBuf[48];
        int yawDeg = (int)(((shipYaw & 0xFFFF) * 360LL) >> 16);
        int pitchDeg = (int)(((shipPitch & 0xFFFF) * 360LL) >> 16);
        snprintf(txt, sizeof(txt), "PLANET Z:%d YAW:%d PITCH:%d %s",
                 (int)(shipDist / 256), yawDeg, pitchDeg,
                 s_paused ? "[PAUSED]" : "");
        DrawStringSimple(backBuffer, 10, 10, txt, 0x0F);
        const char* name = GetDemoShipDisplayName(s_demoShipIndex, nameBuf, sizeof(nameBuf));
        snprintf(txt, sizeof(txt), "#%d %s  0x1F interpreter  ESC=ships",
                 s_demoShipIndex, name);
        DrawStringSimple(backBuffer, 10, 25, txt, 0x0D);
#ifdef JJFFE_USE_SDL
        const Uint32 elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < kTargetFrameMs)
            SDL_Delay(kTargetFrameMs - elapsed);
#endif
        return;
    }

    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    int32_t shipPos[3] = {0, 0, shipDist};

    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) return;

    size_t meshSize = MeshBytecodeSize(s_demoShipIndex);
    if (meshSize == 0)
        meshSize = 1024;

    // Thrusters sit in bit-clear stubs; harvest is still a silhouette hack that
    // sprays long billboard needles on Adder — leave off until FUNC_001738 matches.
    // FUNC_001751: ctx+0xc = |viewZ|, ctx+0x148 = ObjectStruct.scaleRelated.
    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    const int prevParseModel = GetMeshParseModelId();
    SetMeshParseModelId(s_demoShipIndex);
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseModelId(prevParseModel);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    if (!mesh.valid || mesh.polygons.empty()) return;

    TransformedPoly polys[256];
    int visibleCount = TransformShipVertices(model, mesh, shipRot, shipPos, &camera, polys, 256);

    if (visibleCount > 0) {
        RenderShipFlat(polys, visibleCount, model, shipRot);
    }

    char txt[80];
    char nameBuf[48];
    int yawDeg = (int)(((shipYaw & 0xFFFF) * 360LL) >> 16);
    int pitchDeg = (int)(((shipPitch & 0xFFFF) * 360LL) >> 16);

    snprintf(txt, sizeof(txt), "CAM Z:%d YAW:%d PITCH:%d POLY:%d %s",
             (int)(shipDist / 256), yawDeg, pitchDeg, visibleCount,
             s_paused ? "[PAUSED]" : "");
    DrawStringSimple(backBuffer, 10, 10, txt, 0x0F);

    int windows = 0;
    for (int i = 0; i < visibleCount; i++) {
        if (polys[i].type == MeshPolyType::FlatQuad && polys[i].afterSpecialPoly)
            windows++;
        // Adder canopy: flat RGB black (0x2000), not an 0xAB FlatQuad.
        else if ((polys[i].texInfo >> 14) == 0 &&
                 (polys[i].texInfo & 0x0FFF) == 0 &&
                 polys[i].texInfo != 0 &&
                 polys[i].type != MeshPolyType::Thruster)
            windows++;
    }
    const char* name = ShipDisplayName(s_demoShipIndex, nameBuf, sizeof(nameBuf));
    snprintf(txt, sizeof(txt), "#%d %s  WIN:%d  ESC=ships", s_demoShipIndex, name, windows);
    DrawStringSimple(backBuffer, 10, 25, txt, 0x0D);

    (void)s_initialized;
#ifdef JJFFE_USE_SDL
    const Uint32 elapsed = SDL_GetTicks() - frameStart;
    if (elapsed < kTargetFrameMs)
        SDL_Delay(kTargetFrameMs - elapsed);
#endif
}

} // namespace jjffe
