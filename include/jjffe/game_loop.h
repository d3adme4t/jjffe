#ifndef JJFFE_GAME_LOOP_H
#define JJFFE_GAME_LOOP_H

#include <cstdint>

namespace jjffe {

enum class GamePhase : uint8_t {
    IntroCast = 0,  // _asmmain splash + intro cast (the default boot)
    Menu,
    DemoShipSelect,
    Demo3D,
    Briefing,
    Mission,
    Shutdown,
};

struct GameLoop {
    GamePhase currentPhase;
    GamePhase previousPhase;
    uint32_t frameCount;
    uint32_t currentFps;
    uint32_t fpsAccum;
    uint32_t fpsLastTime;
};

void GameLoopInit();
// Returns false when phase is Shutdown (caller should exit).
bool GameLoopUpdate();
void GameLoopShutdown();
void GameLoopTransition(GamePhase next);
// FUNC_000147's argument for the Mission phase. The intro menu sets it from the
// chosen action id; --start N sets it from the command line so the live binary
// (and the sanitizer smoke) can reach the same sections the replay scenarios use.
void GameLoopSetMissionStart(int32_t startIndex);
// R1188: one mission render frame, branching on the render state exactly as the ASM main loop does
// (ffe.asm:68798-68814): the 3D pass while DATA_008809 bit0 is set or DATA_008630 < 0, else the
// panel/map branch, which SKIPS DrawMission so the open module's own screen stays up. Extracted from
// GameLoopUpdate so render-parity harnesses draw the same two paths live play does instead of calling
// DrawMission unconditionally (test_view_mode_camera used to, and pinned Hope 3D frames as "F2/F3").
void MissionRenderFrame();
// R1194 / Task 4.223: one full mission iteration body shared by GameLoopUpdate and the panel-capture
// harness — FUNC_001786 (arena reset), UpdateMission, HandleMissionInput, MissionRenderFrame — so the
// harness enqueues and flushes panel jobs exactly as live --mission does. Returns HandleMissionInput's
// result (-1 back to menu, 0 stay). Does not FlipScreen/TimerAdvance; the caller (GameLoopUpdate or a
// harness that drives GameLoopUpdate) owns those.
int MissionRunFrame();
GamePhase GameLoopGetPhase();
uint32_t GameLoopGetFrameCount();
uint32_t GameLoopGetFps();

} // namespace jjffe

#endif // JJFFE_GAME_LOOP_H
