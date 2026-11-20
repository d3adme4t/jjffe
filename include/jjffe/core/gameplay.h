#ifndef JJFFE_CORE_GAMEPLAY_H
#define JJFFE_CORE_GAMEPLAY_H

#include "jjffe/core/engine.h"

namespace jjffe {

// ─────────────────────────────────────────────
// Object lifecycle
// ─────────────────────────────────────────────

void InitObjectFromShipData(void* obj, intptr_t shipData);
void* CreateObjectWithParams(int32_t param1, int32_t param2, int32_t param3,
                             int32_t param4, int32_t param5, int32_t param6,
                             int16_t param7, uint8_t param8, uint16_t param9);
void SetupObjectTarget(void* obj, void* target);
void FindAndShift(int32_t* obj);
void ShiftArrayEntries(int32_t index);

// ─────────────────────────────────────────────
// Game state orchestration
// ─────────────────────────────────────────────

void IterateObjectsWithCallback(void* base, void (*callback)(void*, void*), void* param);
void InitGameState();
void InitGameStateWithParam(int32_t param);
// R1123: harness-only PRNG pin (see core.cpp); the live game seeds via FUNC_000146.
void AbSetRngState(uint32_t a, uint32_t b);
void ResetMissionClock(void);
// FUNC_000919's pre-loop stores: DATA_007706 = 0x3e2, DATA_009155/9156 = 0.
void ResetMissionTimebase(void);
uint32_t AdvancePRNG(void);
void PrngSeed(uint32_t state818, uint32_t state820);
void PrngGetState(uint32_t* state818, uint32_t* state820);
void FUNC_000148(int32_t param_1, int32_t param_2);
void FUNC_000149(void* param_1, int32_t param_2, int32_t param_3);
void ProcessKeyboardInput();
void ProcessObjectCallbacks(void* base, void* param);
void UpdateGameState(void* param);
// Mission frame clock (ffe.asm:68692-68796): DATA_009155 = DATA_009156,
// DATA_009156 = 0, DATA_008804 += DATA_009155 (carry into DATA_008807),
// DATA_009122 = DATA_009157, DATA_009157 = 0, DATA_008799 += DATA_009122,
// then FUNC_000152.
void MissionFrameClockTick(void);
// JJFFE_FRAME_CLOCK=1 (A/B): the pre-4.37 0x1000-per-frame DATA_008804.
bool MissionClockFrameStepped(void);
// The 'f' overlay (ffe.asm:68817-69100): FPS and the JJ debug lines, last
// thing before FlipScreen; nothing unless FUNC_001056 toggled fpsEnabled.
void MissionFpsOverlay(void);
// DATA_008872: the F6-F11 view-mode selector FUNC_001054 switches on (0 =
// forward/cockpit, 1 = rear, 2..6 = side/map/orbital/missile/combat-computer -
// see FUNC_001054's own comment). Not yet range-checked here; callers treat
// anything but a known mode as mode 0.
int32_t MissionViewMode(void);
// R1180: DATA_008870 game-mode state (0x30 landed / 0x24 climbing / 0x26 flight).
int32_t MissionGameModeState(void);

// MissionViewCamera — the live-loop mirror of FUNC_001054's scratch-object
// math (the ASM's per-view-mode camera transform) WITHOUT its render side
// effect. Fills outQ31AndDelta[0..8] with the camera basis (right/up/forward
// columns, Q31, the ASM scratch layout at +0x00..+0x20) and [9..11] with the
// camera's position delta relative to the player object. skipPlayerSlot
// receives the DATA_008838 semantics: the player's pool slot when the hull
// must NOT be drawn (first-person), or 0 when it should be (chase).
void MissionViewCamera(int32_t outQ31AndDelta[12], int32_t* skipPlayerSlot);

// MissionViewScratch — FUNC_001054's transformed scratch object itself (0x152 bytes: Q31
// basis, 64-bit +0x3e position with the per-mode steps applied), the camera object the
// ASM's scene passes render from. False when there is no player object.
// Case 6's scanner grid: a SECOND player copy (unrotated matrix, the player's own position) drawn
// as model 0xa7 through FUNC_001680 with the case-6 camera; +0x9e comes from DATA_008712 <
// 0x7a1200 (measured 1, DATA_008712 = 53440 at frame 120). JUMP_004338 also fills three output
// dwords via FUNC_000802 before drawing; those read zero in the ASM at this frame, and the port
// has no caller for that function yet, so they stay zero here too.
struct MissionViewGrid {
    uint8_t obj[0x152];
    bool active;
};

// grid may be null when only the camera is wanted.
bool MissionViewScratch(uint8_t out[0x152], MissionViewGrid* grid = nullptr);

// Orbital camera globals for modes 2/3/4 (FUNC_001054 cases 2-4): pan angles
// and zoom scale, set by arrow keys / numpad when in those modes via
// FUNC_001038's orbital branch. Exposed so UpdateMission can re-derive the
// same camera transform without calling FUNC_001054 directly.
int32_t MissionOrbitalAngleX(void);   // DATA_008893 — horizontal pan (mode 4)
int32_t MissionOrbitalAngleY(void);   // DATA_008894 — vertical pan (mode 4)
int32_t MissionOrbitalScale(void);    // DATA_008895 — zoom (>>16 for int scale)
int32_t MissionSideMapAngleX(void);   // DATA_008927 — side/map horizontal angle
int32_t MissionSideMapAngleY(void);   // DATA_008928 — side/map vertical angle

// FUNC_001513 — 2D rotation helper used by FUNC_001054 modes 2/3/4/6 to rotate
// two basis vectors (param_1, param_2) by angle param_3 using sin/cos lookup.
// Signature matches the ASM's DATA_007745 function pointer.
void FUNC_001513(int32_t* vecA, int32_t* vecB, int32_t angle);

// fpsEnabled / framesPerSec (ffe.asm:68415-68417), for tests.
int MissionFpsEnabled(void);
void SetMissionFpsEnabled(int on);
uint32_t MissionFramesPerSec(void);
struct GameStateTickStats {
    uint32_t action3;    // FUNC_000144(i, 3) dispatches (DATA_008624 set)
    uint32_t action3Held; // registered action 3 not dispatched (module 3)
    uint32_t action10;   // FUNC_000144(i, 10, secs) dispatches (DATA_008623 bit 0)
    uint32_t gateFires;  // whole-second gate crossings
    uint32_t lastSecs;   // secs of the last crossing
    uint32_t clock8804;  // DATA_008804
    uint32_t date8807;   // DATA_008807
    uint32_t mark8808;   // DATA_008808
};
GameStateTickStats GetGameStateTickStats(void);
// FUNC_001023 blink state: DATA_007246/247 arms, DATA_007252 phase, and the
// visibility bit of console buttons 0x49 / 0x4f it shows and hides.
struct AutotargetBlinkState {
    uint32_t arm7246, arm7247, phase7252;
    bool button49Shown, button4fShown;
};
AutotargetBlinkState GetAutotargetBlinkState(void);
void SetAutotargetBlinkArms(uint32_t arm7246, uint32_t arm7247);
// GUI function pointers DATA_007674..007682 (ffedat.asm:23321-23338) and the
// hot-area list DATA_007683 they edit: 5 dwords per area (left, top, right,
// bottom, id), terminated by -1.
struct GuiPointerTable {
    intptr_t processInput;       // DATA_007674 → FUNC_001432
    intptr_t clearHotAreas;      // DATA_007675 → FUNC_001438
    intptr_t setConsoleHotAreas; // DATA_007676 → FUNC_001439
    intptr_t addHotAreas;        // DATA_007677 → FUNC_001440
    intptr_t addHotSquare;       // DATA_007679 → FUNC_001442
    intptr_t removeHotArea;      // DATA_007680 → FUNC_001443
    intptr_t setXYAccum;         // DATA_007681 → FUNC_001444
    intptr_t getXYAccum;         // DATA_007682 → FUNC_001445
};
GuiPointerTable GetGuiPointerTable(void);
const int32_t* GetGuiHotAreaList(void);
void ResetObjectFlags(void* base);
// Test-only: force/clear DATA_008835 bit 1, ResetObjectFlags' guard.
void DebugSetRenderObjectPassFlag(bool set);
void ProcessActiveObjects();  // FUNC_000154

// ─────────────────────────────────────────────
// Combat
// ─────────────────────────────────────────────

bool CombatDisplayHandler(int32_t param1, int32_t param2);
void BuildCombatDisplay(int32_t* entry);
void CombatDisplayUpdate(int32_t* entry);
int32_t AddCombatLogEntry(uint8_t param1, uint8_t param2, int32_t param3, int32_t param4,
                          int32_t param5, int32_t param6, int32_t param7,
                          uint8_t param8, uint8_t param9, const char* text, int32_t param11);
int32_t CombatAction641(void* param1, void* param2);
int32_t CombatAction642(void* param1, void* param2);
int32_t CombatAction643(void* param1, void* param2);
int32_t CombatAction644(void* param1, void* param2);
int32_t CombatAction645(void* param1, void* param2);
void AddToCommandQueue(void* param1, int32_t* param2, int32_t param3, int32_t param4, int32_t param5, int32_t param6, int32_t param7, int32_t param8);
void UpdateObjectPosition(int32_t* param1);
void GameStateHandler648(int32_t* param1);

// ─────────────────────────────────────────────
// Game mode dispatch + rendering orchestration
// ─────────────────────────────────────────────

void GameModeDispatcher(uint8_t mode);
void GameModeHandler3();
void DisplayCommandList();
void DisplayMessageList();
void DisplayEquipment();
void DisplayCrewList();
void DisplayCombatLog();
void DisplayShipList();

// ─────────────────────────────────────────────
// UI subsystems
// ─────────────────────────────────────────────

void DisplayTextWrapper(int32_t code, void* param1, int32_t param2, int32_t param3, int32_t param4, void* param5);
void BlitAndDisplayWrapper(int32_t param1, int32_t param2);
void SaveStateAndDisplay();
void EquipmentHandler(void* param1, intptr_t param2);
void DisplayAndReset(const ConsoleDisplayEntry* entry);
void BuildEquipmentList(int32_t* entry);
void SetupEquipmentPanel(int32_t* entry);
void DisplayAndFlip(int32_t param);
void SimpleWrapper0539();
void DisplaySetup(int32_t param);
void DisplayAndClear(int32_t param);
void ConsoleActionDispatcher();

// ─────────────────────────────────────────────
// Queue/wrapper infrastructure
// ─────────────────────────────────────────────

void QueueAndAdvance(int32_t* entry, int32_t count);
void ClearArrayEntry(int32_t param);
void ResetAndQueue(void* param);
void SetTimerValue();

// ─────────────────────────────────────────────
// Phase 47 game object processing
// ─────────────────────────────────────────────

void GameObjectProcessorInit();
void GameObjectProcessorCallback(void* obj, void* param);
void DispatchFunctionByIndex(void* param1, void* param2);
void ComplexObjectProcessor(void* param1);
void RandomFunctionCaller();
void AddEntryToArray(int32_t param1, void* param2, void* param3, void* param4, void* param5, void* param6);
uint32_t ObjectLookupFunction(uint32_t objId, uint32_t base);
void ObjectProcessor626(int32_t param1);
void ObjectProcessor627(int32_t param1);
void ObjectProcessor628(int32_t param1);

// ─────────────────────────────────────────────
// Phase 48 state/combat
// ─────────────────────────────────────────────

void GameStateWrapper629();
void GameStateWrapper630(int32_t param1);
void GameStateWrapper631(int32_t param1);
void ObjectLookupAndDispatch(int32_t param1, int32_t param2);
void ComplexArrayProcessor();
void ConditionalDispatch634(int32_t param1, int32_t param2, int32_t* param3);
uint32_t TableLookup635(int32_t param1, uint16_t* param2);
uint32_t StateManager636(void* param1, void* param2);
uint32_t StateManager637(void* param1, void* param2);
uint32_t ComplexStateManager638(void* param1, void* param2);
uint32_t FlagCheck639(void* param1, void* param2);
uint32_t FlagCheck640(void* param1, void* param2);

// ─────────────────────────────────────────────
// Phase 50 object management
// ─────────────────────────────────────────────

uint32_t* RemoveObjectFromList(uint32_t param1);
int32_t CheckObjectMatch(int8_t param1, void* param2);
void ResetObjectState(void* param1);
int32_t SetObjectState(int8_t param1, void* param2);

// ─────────────────────────────────────────────
// Phase 51 handlers
// ─────────────────────────────────────────────

int32_t ComplexObjectHandler(void* param1, void* param2, void* param3, int32_t* param4);
void QueueObjectCommand(void* param1, int8_t param2, int32_t param3, int32_t param4);
void Wrapper658(void* param1, void* param2);
void EmptyFunction660();

// ─────────────────────────────────────────────
// Phase 52 wrappers + init
// ─────────────────────────────────────────────

uint32_t* FUNC_000661(uint32_t* a);
uint32_t* FUNC_000662(uint32_t* a);
uint32_t* FUNC_000663(uint32_t* a);
void Wrapper665(void* param1, void* param2);
void Wrapper666(void* param1, void* param2);
void Wrapper667(void* param1, void* param2);
void Wrapper668(void* param1, void* param2);
int32_t InitializeObject669(int32_t param1, void* param2, int8_t param3);
int32_t InitializeObject670(int32_t param1, void* param2);
intptr_t FUNC_000672(int32_t param1, int32_t param2, int32_t param3,
                     int32_t param4, int32_t param5);
void* CreateComplexObject672(void* param1, void* param2, void* param3, void* param4, void* param5);

// ─────────────────────────────────────────────
// Phase 52-53 object processing
// ─────────────────────────────────────────────

void ProcessObjects673(int32_t* param1);
void ProcessObjects674();
void ProcessObjects675(intptr_t param1, void* param2);
void ProcessObjects676(int32_t param1, void* param2);
void ProcessObject677(int32_t param1, uint8_t** param2);
void ProcessObject678(int32_t param1, uint8_t** param2);
void ProcessObjects679();
void ProcessObjects680();
void ProcessObjects692();
void ProcessObjects693();
void ProcessObjects694(int8_t param1);

// ─────────────────────────────────────────────
// Phase 54 object utility functions
// ─────────────────────────────────────────────

void ProcessObject702(intptr_t param1, int32_t param2);
void ProcessObject703(intptr_t param1, int32_t param2, intptr_t param3);

// ─────────────────────────────────────────────
// Phase 56: Object processing functions (FUNC_000710-0x799)
// ─────────────────────────────────────────────

void ProcessObjects710();
void ProcessObject711(int32_t param1);
void ProcessObject712(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject713(int32_t param1, int32_t param2, int32_t param3);
void ProcessObjects715();
void ProcessObject716(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject717(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject718(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject719(int32_t param1);
void ProcessObject720(int32_t param1, int32_t param2);
void ProcessObject721(int32_t param1, int32_t param2);
int32_t ProcessObject722(int32_t param1, int32_t param2);
void ProcessObject723(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject724(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject725(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject726(int32_t* param1, int32_t* param2, uint32_t param3, uint8_t param4);
void ProcessObject727(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject728(void* param1);
void ProcessObject729(int32_t param1, int32_t param2, int32_t param3);
void ProcessObject730(int32_t param1);
void ProcessObject731(int32_t param1, int32_t param2, int32_t param3, char param4);
void ProcessObject733(int32_t param1);
void ProcessObject734(int32_t param1);
void ProcessObject736(int32_t param1);
void ProcessObject737(int32_t param1);
void ProcessObject738(int32_t param1);
int32_t ProcessObject739(int32_t* param1, uint32_t* param2, uint32_t* param3, uint32_t* param4);
void ProcessObject742(int32_t param1);
void ProcessObject743(int32_t param1);
void ProcessObject744(int32_t param1, int32_t param2);
void ProcessObject745(int32_t param1, int32_t param2);
void ProcessObject747(int32_t param1);
void ProcessObject748(int32_t param1);
void ProcessObject749(int32_t param1);
void ProcessObject750(int32_t param1);
void ProcessObject751(int32_t param1);
void ProcessObject752(int32_t param1);
void ProcessObject755(int32_t param1);
void ProcessObject756(void* param1);
void ProcessObject757(int32_t param1);
void ProcessObject758(int32_t param1);
void ProcessObject759(int32_t param1);
void ProcessObject760(int32_t param1);
void ProcessObject761(int32_t param1);
void ProcessObject762(int32_t param1);
void ProcessObject763(int32_t param1);
void ProcessObject764(int32_t param1);
void ProcessObject765(int32_t param1);
void ProcessObject766(int32_t param1);
void ProcessObject767(int32_t param1);
void ProcessObject768(int32_t param1);
void ProcessObject769(int32_t param1);
void ProcessObject771(int32_t param1, int32_t param2, int32_t param3);
int32_t ProcessObject772(int32_t param1);
intptr_t FUNC_000773(intptr_t param1, int32_t param2, int32_t param3);
int32_t FUNC_000772(int32_t param1);
uint32_t ProcessObject774(int32_t param1, int32_t* param2);
int32_t ProcessObject775(int32_t param1);
char* ProcessObject791(uint32_t param1, int32_t param2, intptr_t param3, uint32_t param4, char* param5);
void ProcessObject792(int32_t param1, int32_t param2, int32_t param3);
uint8_t* ProcessObject793(int32_t param1, int32_t param2, int32_t param3, uint32_t* param4, int32_t param5);
int32_t ProcessObject794(int32_t param1, int32_t param2, int32_t param3, int32_t param4, int32_t param5);
uint8_t* ProcessObject795(uint8_t* param1, uint32_t* state1, uint32_t* state2);
uint8_t* ProcessObject796(uint8_t* param1, uint32_t* state1, uint32_t* state2);
uint8_t* ProcessObject797(uint8_t* param1, uint32_t* state1, uint32_t* state2);
void ProcessObject798(uint8_t** cursor, uint32_t* state1, uint32_t* state2);
void ProcessObject799(uint8_t** cursor, uint32_t* state1, uint32_t* state2);

// ─────────────────────────────────────────────
// Lookup helpers
// ─────────────────────────────────────────────

int32_t LookupTimeValue(int32_t param);
void ChatLogAdd(const char* text, uint8_t param2, int32_t param3);
void DisplayWrapper304(uint8_t param1, void* param2, void* param3);
void DisplayWrapper305(uint8_t param1, void* param2, void* param3);

// ─────────────────────────────────────────────
// Mission HUD (FUNC_001111 scanner / aiming cross)
// ─────────────────────────────────────────────

void FUNC_001111(void);
void EnableMissionHud(void);
void EnsureHudTextDraw(void);
// DATA_008804 += units with the date carry; the JJFFE_FRAME_CLOCK=1 path only.
void AdvanceMissionClock(uint32_t units);
// Mission HUD: altitude → DATA_008867; enables Set←Act mirror (no autopilot yet).

// Cockpit console chrome (FUNC_000272's pieces; DrawMission's pre-4.36
// JJFFE_DUP_CONSOLE path calls them directly)
void FUNC_000261_ConsoleDrawButtons(void);
void FUNC_000262_ConsoleSetButtonImage(int32_t id, int32_t image);
void FUNC_000263_ConsoleShowButton(int32_t id);
void FUNC_000264_ConsoleHideButton(int32_t id);
void FUNC_000270(void);
void FUNC_000274(void);
void BindMissionPlayerObject(void* obj);
// The 0x152-byte objects the synthesised console player's +0x56 indexes: the
// world table (§4.18). Bound only with JJFFE_SYNTH_CONSOLE_PLAYER=1; FUNC_001099
// then resolves the parent name there. nullptr is the ASM's DATA_009133 lookup.
void BindMissionObjectTable(const void* objs, int32_t count);
// DATA_008861, the player object the console readers see.
const void* GetMissionPlayerObject(void);
// JJFFE_SYNTH_CONSOLE_PLAYER=1 (A/B): the pre-4.39 synthesised console player.
bool ConsolePlayerSynthesised(void);
// JJFFE_PRE443_HUD=1 (A/B): FUNC_001099 without its velocity marker, parent
// name and speed units, as before task 4.43.
bool HudPre443(void);
// JJFFE_CONSOLE_PANEL_SEED=1 (A/B): the pre-4.40 FUNC_000331 button seed,
// without FUNC_001000's FUNC_001003 and FUNC_000996's button 0x18.
bool ConsolePanelSeeded(void);
// DATA_005100 followed by the 82 x 0x3e ConButton records (ffeasm layout).
constexpr size_t kConsoleButtonTableBytes = 4 + 82 * 0x3e;
void GetConsoleButtonTable(uint8_t out[kConsoleButtonTableBytes]);
uint32_t GetConsoleModeIconBmp(void);

// The SysMenu option bytes at DATA_008804+0x3f4c (FUNC_000219 seeds them from
// DATA_004895). Offset 6 is DATA_009019, the "Background stars" toggle.
constexpr int kSysMenuFlagBytes = 0x32;
constexpr int kSysMenuFlagBackgroundStars = 6;
uint8_t SysMenuFlag(int offset);
// FUNC_001775 for a mission camera object: model 0x13b on the camera.
void MissionFUNC_001775(const uint8_t* cam);

// Phase 3 — combat HUD / Set-Act / location text (not FUNC_000272)
void FUNC_001099(void);

// FUNC_000953 — the real damage applier (ffe.asm:71367-71470). Writes a real PhysObj
// (stride 0x152): +0xe0 shield/health (damage subtracted, clamped at 0), +0xe4 second
// shield/energy, +0x14e damage flag = 0xa. Task 4.150: exposed so the player's weapon can
// damage a real traffic ship instead of Phase-60 GameObj scaffolding.
uint32_t FUNC_000953(int32_t obj, int32_t param_2, uint32_t param_3);

// PlayerFireReal — task 4.151: one ASM fire frame on the live path. Sets the fire key the way
// the input layer does (0 = pressed, ffe.asm:85740-) and calls the ASM's own fire dispatcher
// FUNC_001040 (ffe.asm:77011), which gates on weapon mount/recharge/mode and spawns a real
// bolt through FUNC_000931 (ffe.asm:69727). Replaces the Phase-60 scaffolding
// CombatFireWeapon() for the live loop.
void PlayerFireReal();

// PlayerActionDispatch — the ASM flight action keys on the live path. Routes the
// caller's already-polled 256-byte key buffer through the DATA_007688 scancode→
// action table (as FUNC_001429 does) to the ported action sub-functions:
// ECM (E/;/Insert), missile (M), energy bomb (B), escape capsule (X), mining
// machine (D), radar mapper (/), FPS toggle (F). Fire (Space) is not included —
// it keeps its own PlayerFireReal path. See core.cpp for the ffe.asm references.
void PlayerActionDispatch(const uint8_t* keyStates);

// FUNC_001056 — the view/time keys (0xf5-0xff) and the flight action keys. In ffeasm it is reached from
// the per-state frame handlers (FUNC_001050/1051/1055/1089/1090), one frame after FUNC_001429 read the
// key. R1174/R1175: UpdateMission calls it after the tick; PlayerActionDispatch no longer does.
void FUNC_001056(intptr_t arg);

// PlayerSteerAsm — the ASM's own rotation physics on the live ship's matrix.
// Default ON (JJFFE_ASM_STEER=0 restores the Phase-60 scaffolding rotation).
// Runs FUNC_001038 (input → turn rates at obj+0xb0/b2/b4) then FUNC_000585 (spend
// the rates into the matrix at obj+0x00..0x20, the same call shape FUNC_001041
// uses), bridging the scaffold's 16.16 rotation to the ASM's Q31 transposed
// matrix. Returns true when the ASM path owned the rotation this frame; the
// caller then skips its own integrator. Returns false with no steering key held,
// so an idle frame is byte-identical to the scaffolding path.
bool PlayerSteerAsm(const uint8_t* keyStates, int32_t rot9[9]);

} // namespace jjffe

#endif // JJFFE_CORE_GAMEPLAY_H
