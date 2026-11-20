#include <cstdlib>
#include "jjffe/game_loop.h"
#include "jjffe/platform.h"
#include "jjffe/video.h"
#include "jjffe/draw_main.h"


// 4.441: minimal station-services screen (front-end UI, docs/FRONTEND_UI_PLAN.md).
// First increment: validate the HOOK and the panel chrome. 4.440 settled the key
// convention (0x80 = fresh press edge, 0 = held); 4.436 measured the panel
// (fill colour 245, rows 0..71, full width) from the ASM reference frame
// captures/4.421/asm_fb_frame6.raw. Entries and the 193 highlight come next.
// 4.444: split into POLL and PAINT so the overlay wraps the phase switch
// (4.443: the frame is a switch(phase) and each case polls its own keys, so an
// overlay must run at function level, poll first and paint last).
static bool s_stationMenuActive = false;
static int  s_stationMenuSel = 0;
// R1131: assume F4 starts released, so a pressed state on the very
// first poll (whatever the input backend reports) cannot fire the
// edge and open the page at startup.
static bool s_stationMenuPrevF4 = true;

static void StationMenuPoll()
{
    uint8_t keys[256] = {};
    for (int i = 0; i < 256; ++i) keys[i] = 0;
    jjffe::InputKeybReadStates(keys);
    // DOS key convention: 0 = pressed, 0x80 = released. The 4.440 version
    // tested `== 0x80` as "fresh press", i.e. the *released* state: on the
    // first poll (every key released) that fired immediately and the station
    // page opened at startup. Edge on the pressed state instead (R1131).
    const bool f4 = keys[0x3e] == 0x00;          // 0 = pressed
    if (f4 && !s_stationMenuPrevF4) s_stationMenuActive = !s_stationMenuActive;
    s_stationMenuPrevF4 = f4;
}

// 4.445: panel text - the title and the five station-services entries, whose
// strings carry their subtitle as a \r-separated second line (DATA_005664..668).
static const char* const s_stationEntries[5] = {
    "Shipyard", "Bulletin Board", "Stockmarket", "Upgrades", "Repairs and Servicing"
};

static void StationMenuPaint()
{
    if (!s_stationMenuActive) return;
    uint8_t* fb = jjffe::GetBackBuffer();
    if (!fb) return;
    jjffe::FillRect(fb, 0, 0, jjffe::SCREEN_WIDTH, 72, 245);   // measured chrome (4.436)
    // 4.464: the station backdrop below the panel is a FLAT colour-214 fill in the
    // reference (96.5% of rows 86..157, 4.463's follow-up); the port was leaving it
    // black, which alone accounted for most of the 98.7% viewport difference.
    jjffe::FillRect(fb, 0, 86, jjffe::SCREEN_WIDTH, 158 - 86, 214);
    // 4.449: the reference page is WORD-WRAPPED TEXT, not discrete lines (4.448
    // measured occupancy graduating 25 -> 146 px/row with no empty rows), so the
    // panel wraps a single text body down the measured area.
    // 4.458: the page body is the bulletin board's ADVERT COPY, taken from the
    // extracted services tables through BulletinAdvertLine() - no duplication.
    // 4.462: the page is the station's LIVE ADVERT LISTING - one template line per
    // advert in the mission table (4.460/4.461), which is the reference page's shape
    // (many short lines, 4.448). Falls back to the extracted advert copy if none.
    static char s_body[2048];
    if (s_body[0] == '\0') {
        int n2 = 0;
        const int live = jjffe::BulletinAdvertCount();
        if (live > 0) {
            for (int a = 0; a < live && a < 24 && n2 < 1900; ++a) {
                const char* t = "Advert has an ad. on this bulletin board.  ";
                for (const char* u = t; *u && n2 < 1900; ++u) s_body[n2++] = *u;
            }
        } else {
            for (int a = 0; a < 3; ++a) {
                const char* t = jjffe::BulletinAdvertLine(a);
                if (!t) break;
                for (const char* u = t; *u && n2 < 1900; ++u) s_body[n2++] = (*u == 0x0d || *u == 0x0a) ? ' ' : *u;
                if (n2 < 1900) s_body[n2++] = ' ';
            }
        }
        s_body[n2] = '\0';
        if (n2 == 0) { const char* t = "Bulletin Board"; while (*t && n2 < 1900) s_body[n2++] = *t++; s_body[n2] = 0; }
    }
    int row = 1;
    int wrapChars = 26, pitch = 5;
    if (const char* e = getenv("JJFFE_UI_WRAP")) { const int v = atoi(e); if (v > 8 && v < 80) wrapChars = v; }
    if (const char* e = getenv("JJFFE_UI_PITCH")) { const int v = atoi(e); if (v > 1 && v < 24) pitch = v; }
    char line[80];
    int n = 0;
    const char* kBody = s_body;
    for (const char* q = kBody; ; ++q) {
        const char c = *q;
        const bool brk = (c == '\0' || c == ' ');
        if (brk && n > 0 && (n >= wrapChars || c == '\0')) {
            line[n] = '\0';
            jjffe::DrawStringLarge(fb, 2, row, line, 3);
            row += pitch;
            n = 0;
            if (c == '\0' || row > 70) break;
            continue;
        }
        if (c == '\0') break;
        if (brk) { if (n < 62) line[n++] = ' '; continue; }
        if (n < 62) line[n++] = c;
    }
}

#include "jjffe/asm_longjmp.h"
#include "jjffe/intro_cast.h"
#include "jjffe/sound.h"
#include "jjffe/menu.h"
#include "jjffe/briefing.h"
#include "jjffe/demo3d.h"
#include "jjffe/mission.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/span_arena.h"   // g_bss8804 render state (task 4.217)
#include "jjffe/data_globals.h" // DATA_008630 (3D-view module flag)
#include "jjffe/replay_hooks.h"  // g_replayTimerHook (JJFFE_FIXED_DT_MS)

// Defined in core.cpp / mission.cpp (jjffe namespace), exported by no header; needed by the
// mission-start activation and the render-state branch (task 4.217, ffe.asm:68571 + 68798-68814).
namespace jjffe {
void ProcessActiveObjects();      // FUNC_000154: action-4 to every open module
void FUNC_001632_UpdatePtr();
void FUNC_001786();               // job-arena reset (ffe.asm:68684, loop top)
void FUNC_000956(int32_t);         // ship/cabin asset render (ffe.asm:3e55c)
int32_t MissionViewMode(void);     // DATA_008872, the view selector (core.cpp)
void FUNC_001789();               // job-queue flush (ffe.asm:68640)
void FUNC_001637_FlipScreen();    // core flip (ffe.asm:68646)
void FUNC_001637_FlipScreenNoPresent();   // the same, minus the present (core.cpp, R1159b)
void MissionProjectPoolObjects();   // FUNC_001536's projections into the pool (core.cpp, R1160)
void FUNC_001330_DPalBuild(void); // DPal refresh (ffe.asm:68638)
void FUNC_001573();               // ClearUpperBuf + FUNC_001574: activate flight mode (ffe.asm:68571)
void MissionFrameDumpHooksIfDrawing();   // mission.cpp capture hooks, guarded (task 4.217)
}
#include "jjffe/platform.h"
#include "jjffe/ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace jjffe {

static GameLoop g_loop;
static bool s_missionInitialized = false;
// FUNC_000147's argument (ffe.asm:68669). Paths without the intro menu start
// as its default key does: DATA_007199[0] = 0.
static int32_t s_missionStartParam = 0;

void GameLoopSetMissionStart(int32_t startIndex) { s_missionStartParam = startIndex; }

static const char* BootStepName(GamePhase phase)
{
    switch (phase) {
    case GamePhase::IntroCast:      return IntroCastInSplash() ? "splash" : "intro";
    case GamePhase::Menu:           return "menu";
    case GamePhase::DemoShipSelect: return "demo-select";
    case GamePhase::Demo3D:         return "demo3d";
    case GamePhase::Briefing:       return "briefing";
    case GamePhase::Mission:        return "mission";
    case GamePhase::Shutdown:       return "shutdown";
    }
    return "unknown";
}

// JJFFE_DUMP_PHASE=1: one "BOOT-PHASE <step> frame=<n>" stderr line per
// change of step (the splash is reported apart from the intro cast).
static void TraceBootStep()
{
    static const bool s_on = []() {
        const char* e = getenv("JJFFE_DUMP_PHASE");
        return e && e[0] && e[0] != '0';
    }();
    if (!s_on) return;
    static const char* s_last = nullptr;
    const char* step = BootStepName(g_loop.currentPhase);
    if (step == s_last) return;
    s_last = step;
    fprintf(stderr, "BOOT-PHASE %s frame=%u\n", step, g_loop.frameCount);
}

// JJFFE_FIXED_DT_MS=<ms>: every game-loop iteration is exactly <ms> of wall time instead of the
// real clock - no frame-rate wait - so a live capture is machine-independent and runs on the same
// clock as tools/asm_replay_scenario.py ("dt": 20 there). The loop calls TimerAdvance twice per
// iteration (UpdateMission's, then the trailing one that only tops up the wall time since it), so
// the step goes to the first call of each iteration and the second gets 0.
static uint32_t s_fixedDtMs = 0;
static uint32_t FixedDtTimer()
{
    static uint32_t s_servedFrame = ~0u;
    if (s_servedFrame == g_loop.frameCount) return 0;
    s_servedFrame = g_loop.frameCount;
    return s_fixedDtMs;
}

void GameLoopInit()
{
    if (const char* e = getenv("JJFFE_FIXED_DT_MS"); e && e[0]) {
        s_fixedDtMs = static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
        if (s_fixedDtMs) g_replayTimerHook = FixedDtTimer;
    }
    g_loop.currentPhase = GamePhase::IntroCast;
    g_loop.previousPhase = GamePhase::IntroCast;
    g_loop.frameCount = 0;
    g_loop.currentFps = 0;
    g_loop.fpsAccum = 0;
    g_loop.fpsLastTime = PlatformGetTicks();
}

void GameLoopTransition(GamePhase next)
{
    // T-59-03: Validate next phase is within enum range
    if (static_cast<uint8_t>(next) > static_cast<uint8_t>(GamePhase::Shutdown)) {
        fprintf(stderr, "GameLoopTransition: invalid phase %u, ignoring\n",
                static_cast<uint8_t>(next));
        return;
    }
    g_loop.previousPhase = g_loop.currentPhase;
    g_loop.currentPhase = next;
}

GamePhase GameLoopGetPhase()
{
    return g_loop.currentPhase;
}

uint32_t GameLoopGetFrameCount()
{
    return g_loop.frameCount;
}

uint32_t GameLoopGetFps()
{
    return g_loop.currentFps;
}

// JJFFE_DUMP_LIVE=<path> / _AFTER=<n> / _EXIT=1 for the 3D viewer phases.
// Writes the software back buffer plus <path>.pal, mirroring the mission dump.
static void Demo3DFrameDumpHook()
{
    const char* path = getenv("JJFFE_DUMP_LIVE");
    if (!path || !path[0] || !g_videoState) return;
    static long s_frames = 0;
    static bool s_done = false;
    ++s_frames;
    if (s_done) return;
    const char* afterEnv = getenv("JJFFE_DUMP_LIVE_AFTER");
    if (!afterEnv || !afterEnv[0]) return;              // F12-only in live play
    if (s_frames < std::atol(afterEnv)) return;
    s_done = true;
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "demo dump: cannot open %s\n", path);
        return;
    }
    fwrite(g_videoState->backBuffer, 1, 320 * 200, f);
    fclose(f);
    char ppath[1024];
    snprintf(ppath, sizeof(ppath), "%s.pal", path);
    PlatformDumpPalette(ppath);
    fprintf(stderr, "DEMO-DUMP(live, frame %ld) -> %s\n", s_frames, path);
    if (const char* exitEnv = getenv("JJFFE_DUMP_LIVE_EXIT"); exitEnv && exitEnv[0] == '1')
        GameLoopTransition(GamePhase::Shutdown);
}

// R1188: one mission render frame, branching on the render state exactly as the ASM main loop does
// (ffe.asm:68798-68814). DATA_008809 bit0 set (or DATA_008630 < 0) -> the 3D pass; otherwise a
// map/panel is up and the frame SKIPS DrawMission so that module's own screen stays put. Shared with
// test_view_mode_camera, which used to call DrawMission unconditionally and so pinned Hope 3D frames
// as its "F2"/"F3" baselines (they were ~50,000 px from ffeasm's real panels).
void MissionRenderFrame()
{
    const bool sceneActive = DATA_008630 < 0;
    // R1214: AB_DPAL_CALL_GUARD=1 — match ASM JUMP_003849 (ffe.asm:68798-68808):
    //   movsx eax, byte [DATA_008809]; test ah,0x1; jnz JUMP_003855
    // Sign bit set (byte < 0) → JUMP_003855 calls [DATA_007632]; non-negative
    // falls through with no build. The port's else branch was modeled on
    // title-loop JUMP_003844 (always builds), so F4→panel (rs809=0, m630=127)
    // emitted the flip≈7 chg=1 rebuild ASM never runs in fr 6–90. Do NOT skip
    // the whole else branch: view-mode-6 panels keep rs809=0xfe (bit7 set,
    // bit0 clear) and ASM still builds there. Default off.
    static int s_callGuard = -1;
    if (s_callGuard < 0) {
        const char* e = getenv("AB_DPAL_CALL_GUARD");
        s_callGuard = (e && e[0] == '1') ? 1 : 0;
    }
    // R1227: AB_PANEL_RETURN_PROBE=1 logs branch/order for frames in
    // AB_PANEL_RETURN_LO..HI (default 95..105). Cap 40. Cached getenv.
    // (Fix itself lives in FUNC_000281 — ffedat DATA_007206=5.)
    static int s_prProbe = -1;
    static unsigned s_prLo = 95, s_prHi = 105;
    static int s_prLogs = 0;
    if (s_prProbe < 0) {
        const char* e = getenv("AB_PANEL_RETURN_PROBE");
        s_prProbe = (e && e[0] == '1') ? 1 : 0;
        if (const char* p = getenv("AB_PANEL_RETURN_LO")) s_prLo = (unsigned)atoi(p);
        if (const char* p = getenv("AB_PANEL_RETURN_HI")) s_prHi = (unsigned)atoi(p);
    }
    auto prLog = [&](const char* where, bool built, bool clearedRev) {
        if (!s_prProbe || s_prLogs >= 40) return;
        const unsigned fr = g_loop.frameCount;
        if (fr < s_prLo || fr > s_prHi) return;
        int occNz = 0;
        if (g_videoState) {
            for (int i = 0; i < DPAL_MAP_SIZE; ++i)
                if (g_videoState->dPalSecondary[i]) ++occNz;
        }
        fprintf(stderr,
                "AB_PANEL_RETURN_PROBE %s fr=%u rs809=%#x m630=%d scene=%d "
                "bit0=%d buildsArg=%d built=%d clearRev=%d "
                "sky=%u,%u,%u,%u occNz=%d chg=%d\n",
                where, fr, static_cast<unsigned>(g_bss8804[0xc]),
                static_cast<int>(static_cast<int8_t>(DATA_008630)),
                sceneActive ? 1 : 0, (g_bss8804[0xc] & 1) ? 1 : 0,
                ((g_bss8804[0x0c] & 1) == 0) ? 1 : 0, built ? 1 : 0,
                clearedRev ? 1 : 0,
                static_cast<unsigned>(DATA_008814),
                static_cast<unsigned>(DATA_008815),
                static_cast<unsigned>(DATA_008816),
                static_cast<unsigned>(DATA_008817 & 0xffu), occNz,
                g_videoState ? g_videoState->dPalChangedCount : -1);
        ++s_prLogs;
    };
    if ((g_bss8804[0xc] & 1) || sceneActive) {
        if (PlatformIsGpuDraw()) return;
        // R1216: drop any leftover SHIFT_FIX arm from a prior panel-idle frame so
        // the outer FlipScreen's DPalSet is not skipped on the first flight frame.
        DPalNotifyPanelIdleSkip(false);
        // R1152 mode 6: FUNC_001054's case 6 blits the cabin (FUNC_000956(0)) before rendering, and
        // JUMP_003855 skips ClearUpperBuf on those frames because that blit covers the viewport.
        if (MissionViewMode() == 6) FUNC_000956(0);
        DPalClearReverse();
        prLog("3D-pre", /*built=*/false, /*clearedRev=*/true);
        // R1160/R1161: FUNC_001536's projections into the pool land BEFORE DrawMission, as in the ASM.
        if (sceneActive) MissionProjectPoolObjects();
        DrawMission();   // 4.409: DrawMission's tail owns JUMP_003855's DPalBuild
        prLog("3D-post", /*built=*/true, /*clearedRev=*/true);
        // R1159b: the ASM's 3D frame ends in FUNC_001637_FlipScreen, whose FUNC_001432 turns a click on
        // the view into action 0xff; everything but the present, which the loop's flip does once.
        FUNC_001637_FlipScreenNoPresent();
    } else {
        // Panel / non-3D mission frames. ASM still takes JUMP_003855 when
        // DATA_008809 is signed-negative (e.g. 0xfe); only rs809>=0 is idle.
        DPalSetSceneBuilds((g_bss8804[0x0c] & 1) == 0);  // 4.408
        const bool rs809Neg = static_cast<int8_t>(g_bss8804[0xc]) < 0;
        const bool doBuild = !s_callGuard || rs809Neg;
        prLog("panel-pre", doBuild, /*clearedRev=*/false);
        if (doBuild) {
            // Guard off: historical always-build. Guard on: ASM sign-bit only.
            FUNC_001330_DPalBuild();            // [DATA_007632] (ffe.asm:68808 / 68638)
        }
        prLog("panel-post", doBuild, /*clearedRev=*/false);
        FUNC_001789();   // Task 4.258: flush AFTER the module pass - paints the grid over the background
        // R1215/R1216 SHIFT_FIX + PANEL_INSTALL coherent law (both default-off):
        // ASM fall-through never reaches FUNC_001637/DPalSet; port still needs
        // FUNC_001637 for FlipScreenPrePresent (GUI/input). Arm skip BEFORE that
        // FlipScreen so PANEL_INSTALL's inline DPalSet is suppressed on the
        // synthetic idle flip; clear across ProcessActiveObjects so module
        // FlipScreens install+XOR (ASM frames 6/16/26/41/58); re-arm for the
        // outer GameLoop FlipScreen. SHIFT wins on armed game-loop flips;
        // PANEL_INSTALL wins on unarmed FUNC_001637 (modules).
        static int s_shiftFix = -1;
        if (s_shiftFix < 0) {
            const char* e = getenv("AB_DPAL_SHIFT_FIX");
            s_shiftFix = (e && e[0] == '1') ? 1 : 0;
        }
        const bool panelIdle = s_shiftFix && !rs809Neg;
        if (panelIdle) DPalNotifyPanelIdleSkip(true);
        else DPalNotifyPanelIdleSkip(false);
        FUNC_001637_FlipScreen();            // (ffe.asm:68646) — ASM fall-through omits this
        if (panelIdle) DPalNotifyPanelIdleSkip(false);  // allow module FlipScreen DPalSets
        ProcessActiveObjects();              // FUNC_000154 (ffe.asm:68802)
        FUNC_001632_UpdatePtr();             // (ffe.asm:68803)
        if (panelIdle) DPalNotifyPanelIdleSkip(true);   // suppress outer GameLoop FlipScreen
        MissionFrameDumpHooksIfDrawing();    // capture tools stay alive on map frames
    }
}

// Task 4.223: env-gated probe so the harness and live --mission can be diffed frame-for-frame.
// JJFFE_PANEL_PROBE=1 prints one line per MissionRunFrame (before and after the render).
static int JobQueueDepthBeforeFlush()
{
    // Count pending job-queue nodes via arena-offset links (same walk as ship_renderer probes).
    // Call this BEFORE MissionRenderFrame's FUNC_001789 flush.
    extern uint32_t* WalkQueueHead();
    extern const uint8_t* JobArenaBase();
    uint32_t* head = WalkQueueHead();
    if (!head || head[0] == 0) return 0;
    int nodes = 0;
    uint32_t off = head[0];
    while (off != 0 && nodes < 512) {
        uint32_t* node = reinterpret_cast<uint32_t*>(
            const_cast<uint8_t*>(JobArenaBase()) + (off - 1));
        off = node[1];
        ++nodes;
    }
    return nodes;
}

static uint32_t DPalSignature()
{
    if (!g_videoState) return 0;
    // Full 256*3 palette (round-1 hashed only 384 bytes and collided across frames).
    uint32_t h = 2166136261u;
    for (int i = 0; i < PALETTE_ENTRIES * 3; ++i) {
        h ^= g_videoState->palette[i];
        h *= 16777619u;
    }
    return h;
}

static void MaybePanelProbe(const char* where, int jobs)
{
    // Cached once: this runs twice per mission frame, so a per-call getenv is avoidable churn.
    static const bool s_probe = [] {
        const char* e = getenv("JJFFE_PANEL_PROBE");
        return e && e[0] && e[0] != '0';
    }();
    if (!s_probe) return;
    fprintf(stderr,
            "PANEL-PROBE %s frame=%u DATA_008630=%d DATA_008809=0x%02x DATA_008641=0x%02x "
            "DATA_009054=%u jobs=%d dpal=0x%08x\n",
            where, g_loop.frameCount, static_cast<int>(static_cast<int8_t>(DATA_008630)),
            static_cast<unsigned>(g_bss8804[0xc]), static_cast<unsigned>(DATA_008641),
            static_cast<unsigned>(DATA_009054), jobs, DPalSignature());
}

int MissionRunFrame()
{
    // ffe.asm:68684 — arena reset BEFORE the module pass enqueues this frame's jobs. Without it the
    // panel branch's FUNC_001789 can replay the previous 3D frame's jobs over the page (4.258), and
    // a harness that skipped this call never painted the panel chrome (Task 4.223).
    // Lives inside GameLoopUpdate's AsmLongJmp try/catch with the rest of the mission body; a longjmp
    // from here ends the frame the same way one from UpdateMission/HandleMissionInput does.
    FUNC_001786();
    UpdateMission();
    const int jobsPre = JobQueueDepthBeforeFlush();
    MaybePanelProbe("pre-render", jobsPre);
    const int inputResult = HandleMissionInput();
    MissionRenderFrame();
    MaybePanelProbe("post-render", JobQueueDepthBeforeFlush());
    return inputResult;
}

bool GameLoopUpdate()
{
    TraceBootStep();
    if (g_loop.currentPhase == GamePhase::Shutdown) {
        return false;
    }

    if (!g_videoState) return false;

    const bool gpu = PlatformIsGpuDraw();
    // The ASM main loop never clears the whole buffer: a 3D-view frame clears the upper area
    // itself (DrawMission), and a panel frame keeps the page its module drew once (F2 ship
    // status, ffe.asm:68808-68812). Wiping it here left those pages black.
    if (!gpu && g_loop.currentPhase != GamePhase::Mission) {
        uint8_t* backBuffer = g_videoState->backBuffer;
        memset(backBuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT);
    }

    StationMenuPoll();   // 4.444: front-end overlay wraps the phase dispatch

    // T-59-01: Switch on enum with default case for safety
    switch (g_loop.currentPhase) {
    case GamePhase::IntroCast: {
        if (!gpu) IntroCastDraw();
        int input = IntroCastHandleInput();
        if (input == -1) {
            GameLoopTransition(GamePhase::Shutdown);
        } else if (input == 1) {
            // ffe.asm 68661–68669: SoundStopSong, then FUNC_000147(edi).
            const int actionId = IntroCastGetActionId();
            s_missionStartParam = actionId;
            if (actionId == 0) {
                fprintf(stderr, "IntroCast: action id 0 -> FUNC_000147(0) (landed at Hope)\n");
            } else {
                fprintf(stderr,
                        "IntroCast: action id %d -> FUNC_000147(%d) (the port's own world "
                        "setup is still the id-0 landed spawn)\n", actionId, actionId);
            }
            SoundStopSong();
            IntroCastShutdown();
            MenuResetInput();
            GameLoopTransition(GamePhase::Mission);
        }
        break;
    }
    case GamePhase::Menu: {
        if (!gpu) DrawMenuScreen(g_loop.frameCount, g_loop.currentFps);
        int menuAction = HandleMenuInput();
        if (menuAction == -1) {
            GameLoopTransition(GamePhase::Shutdown);
        } else if (menuAction > 0) {
            MenuProcessAction(menuAction);
        }
        break;
    }
    case GamePhase::DemoShipSelect: {
        if (!gpu) DrawDemoShipSelect();
        int sel = HandleDemoShipSelectInput();
        if (sel == -1) {
            MenuResetInput();
            GameLoopTransition(GamePhase::Menu);
        } else if (sel == 1) {
            // D3D ship browser uses internal meshes (same list as soft), not .x.
            if (PlatformIsD3D())
                SetDemoLegacySoft(true);
            InitDemo3D();
            GameLoopTransition(GamePhase::Demo3D);
        }
        break;
    }
    case GamePhase::Demo3D: {
        if (!gpu) DrawDemo3D();
        // Asset/visual diagnosis for the 3D viewers. The mission-side dump
        // (mission.cpp MaybeDumpMissionFrameLive) sits in the mission loop, so
        // --ship / --planet / --demo never reached it: a dump configured for one
        // of those modes silently wrote nothing. Same env contract here.
        if (!gpu) Demo3DFrameDumpHook();
        uint8_t keyStates[256];
        InputKeybReadStates(keyStates);
        // Edge-trigger ESC so holding the key doesn't spam re-init
        static bool escHeld = false;
        if (keyStates[KEY_ESCAPE] == 0) {
            if (!escHeld) {
                escHeld = true;
                if (IsDemoDirectLaunch()) {
                    GameLoopTransition(GamePhase::Shutdown);
                } else {
                    InitDemoShipSelect();
                    GameLoopTransition(GamePhase::DemoShipSelect);
                }
            }
        } else {
            escHeld = false;
        }
        break;
    }
    case GamePhase::Briefing: {
        if (!gpu) DrawBriefing();
        int briefingResult = HandleBriefingInput();
        if (briefingResult == -1) {
            GameLoopTransition(GamePhase::Menu);
        } else if (briefingResult == 1) {
            GameLoopTransition(GamePhase::Mission);
        }
        break;
    }
    case GamePhase::Mission: {
        if (!s_missionInitialized) {
            // Task 4.217: ffe.asm:68571 - the mission-loop entry (JUMP_003838, the DATA_009125
            // setjmp restart point) calls FUNC_001573 (ClearUpperBuf + FUNC_001574: restore the
            // render state and set DATA_008809 = 0xff) BEFORE FUNC_000147 starts the mission. The
            // port's live boot (IntroCast -> action id 0 -> InitMission) bypassed the title-loop
            // entry that carries this call, so landed frames ran with DATA_008809 = 0x00 while the
            // ASM lands at 0xff (measured both sides, 4.215/4.216: port writers 1573/1574/964/968
            // = 0 calls across the whole landing vs the ASM's 2).
            FUNC_001573();
            // R1132: starts 1-5 load their own world (WorldLoadSol start N),
            // but InitMission's spawn is the id-0 Hope landing. With a
            // non-default start the spawn fails ("Hope missing?"), the player
            // is never placed, and a later key press dereferenced that broken
            // state (segfault after the intro menu / --mission --start 1-5).
            // Until the per-start spawn exists, clamp to the supported default.
            if (s_missionStartParam != 0) {
                fprintf(stderr,
                        "Mission: start %d is not supported yet (the spawn is the id-0 "
                        "Hope landing); starting 0\n", s_missionStartParam);
                s_missionStartParam = 0;
            }
            InitMission(s_missionStartParam);
            s_missionInitialized = true;
        }
        int missionResult = 0;
        // The ASM's _LongJmp (jjffe/asm_longjmp.h): to DATA_009126 the frame
        // just ends; to DATA_009125 (game over, FUNC_000916 / FUNC_000917) the
        // title loop takes over, past the splash, its clock at 0x400.
        // Frame-count exit that does not depend on the drawing branch: the live
        // dump hook (JJFFE_DUMP_LIVE_AFTER) only fires on frames that take the 3D
        // pass, so a section that sits on a panel/map screen never reaches it and
        // a timed run has no clean exit (measured: section 5 runs 985 frames in
        // 20 s yet never dumps). This counts mission iterations instead.
        if (const char* exitAfter = getenv("JJFFE_LIVE_EXIT_AFTER")) {
            static long s_liveFrames = 0;
            if (++s_liveFrames >= std::atol(exitAfter)) {
                GameLoopTransition(GamePhase::Shutdown);
            }
        }
        try {
            // R1194 / Task 4.223: MissionRunFrame is the shared body (FUNC_001786 + UpdateMission +
            // HandleMissionInput + MissionRenderFrame). Live play and the panel-capture harness must
            // call the same function so panel jobs enqueue and flush identically.
            missionResult = MissionRunFrame();
            // After the draw, so the frame a dump captured is the last one run.
            if (MissionFrameDumpedExit()) missionResult = -2;
        } catch (const AsmLongJmp& jump) {
            if (jump.target == AsmJumpTarget::TitleLoop) {
                ShutdownMission();
                s_missionInitialized = false;
                IntroCastSetViewerMode(false);
                IntroCastInit();
                IntroCastSetClock(0x400);
                GameLoopTransition(GamePhase::IntroCast);
            }
            break;
        }
        if (missionResult == -2) {
            // JJFFE_DUMP_MISSION_FRAME capture: exit process after one frame.
            ShutdownMission();
            s_missionInitialized = false;
            GameLoopTransition(GamePhase::Shutdown);
        } else if (missionResult == -1) {
            ShutdownMission();
            s_missionInitialized = false;
            GameLoopTransition(GamePhase::Menu);
        }
        break;
    }
    case GamePhase::Shutdown:
        break;
    default:
        // T-59-01: Invalid enum value — stay in current phase
        fprintf(stderr, "GameLoopUpdate: unknown phase %u, staying in current\n",
                static_cast<uint8_t>(g_loop.currentPhase));
        break;
    }

    // FPS counter update
    g_loop.fpsAccum++;
    {
        uint32_t currentTime = PlatformGetTicks();
        if (currentTime - g_loop.fpsLastTime >= 1000) {
            g_loop.currentFps = g_loop.fpsAccum;
            g_loop.fpsAccum = 0;
            g_loop.fpsLastTime = currentTime;
        }
    }

    StationMenuPaint();   // 4.444: overlay paints last, above the phase output
    FlipScreen();
    TimerAdvance();
    g_loop.frameCount++;
    TraceBootStep();
    return g_loop.currentPhase != GamePhase::Shutdown;
}

void GameLoopShutdown()
{
    // Cleanup will grow as more phases are added
}

} // namespace jjffe
