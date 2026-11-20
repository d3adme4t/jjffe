#ifndef JJFFE_CORE_ENGINE_H
#define JJFFE_CORE_ENGINE_H

#include <cstdint>
#include <cstddef>
#include "jjffe/core/math.h"

namespace jjffe {

// ─────────────────────────────────────────────
// Struct declarations
// ─────────────────────────────────────────────

struct CommandNode {
    CommandNode* left;
    CommandNode* right;
    int32_t key[3];
    void (*callback)(void*, void*);
    void* param1;
    void* param2;
};

extern CommandNode* g_commandTreeRoot;

struct ConsoleDisplayEntry {
    int32_t stringCode;
    int32_t param1;
    int32_t param2;
    int32_t param3;
    int32_t pad1;
    int32_t pad2;
    int32_t flags;
    int32_t stringOffset;
    int32_t pad3;
    int32_t extraParam;
    int32_t pad4;
    const char* stringPtr;
};

struct ConsoleSlot {
    int32_t state;
    int32_t position;
    void* bytecodePtr;
    int32_t bytecodePos;
    void* missionData;
    int32_t bytecodeEnd;
    void* allocatedMem;
    int32_t pad1;
    int32_t pad2;
};

constexpr int MAX_CONSOLE_SLOTS = 150;
extern ConsoleSlot g_consoleSlots[MAX_CONSOLE_SLOTS];
extern uint8_t g_gameMode;

struct CommandNodeExtended {
    CommandNodeExtended* left;
    CommandNodeExtended* right;
    int32_t key[3];
    int32_t extraKey1;
    int32_t extraKey2;
    int32_t extraKey3;
};

struct BytecodeOpcodeInfo {
    int32_t opcodeId;
    int32_t param1Type;
    int32_t param2Type;
    int32_t hasSubInstructions;
};

struct MemoryPoolNode {
    MemoryPoolNode* next;
    int32_t field1;
    int32_t field2;
    int32_t field3;
};

// ─────────────────────────────────────────────
// Command dispatch
// ─────────────────────────────────────────────

void DispatchCommand(int32_t* key, void* context1, void* context2);
int32_t CompareKeys32(const int32_t* a, const int32_t* b);
int CompareKeysExtended(const int32_t* a, const int32_t* b);
void RegisterCommandHandler(int32_t key1, int32_t key2, int32_t key3,
                           int32_t extra1, int32_t extra2, int32_t extra3);
void UnregisterCommandHandler(int32_t key1, int32_t key2, int32_t key3,
                             int32_t extra1, int32_t extra2, int32_t extra3);
void CommandTreeFreeRecursive(CommandNode* node);
void CommandTreeClearAll();
void CollectMatchingCallbacks(void** listHead, void* treeNode, const int32_t* key);
void FUNC_000048(int32_t param1, int32_t param2, int32_t param3);

// ─────────────────────────────────────────────
// Console slot system
// ─────────────────────────────────────────────

void ConsoleSlotUpdate();
void FormatAndDisplayText(const ConsoleDisplayEntry* entry, int withSound);
void DisplayWithSound(const ConsoleDisplayEntry* entry);
void EventPreprocess(const ConsoleDisplayEntry* entry);
void DisplayTextEntry(const ConsoleDisplayEntry* entry);
void ConsoleSetupButton(const ConsoleDisplayEntry* state);
void UpdateDisplayFromMission(ConsoleDisplayEntry* entry);
void ResetConsoleState();
void AddToConsoleQueue(const int32_t* entry);
void SetAndAddToQueue(int32_t value, int32_t* entry);
void SetGameStateValue(int32_t index, int32_t value);
void ResetConsoleInternals();
void CallCallbackFromTable(int32_t index, int32_t type, void* param);
void ExecuteCallbackList();
void InitConsolePanel(int32_t param1, int32_t param2, int32_t textCode,
                     int32_t param4, int32_t param5, int32_t param6);
void UpdateConsoleButtonStates();
void UpdateSlotState(uint32_t slotIndex);
void* AllocateConsoleSlot(int32_t missionIndex, int32_t param);
int32_t InitConsoleSlotWithBytecode(uint8_t** ip, void* context, int32_t missionIndex, int32_t param);
void FinalizeConsoleSlot(ConsoleSlot* slot);
void CleanupConsoleSlots();
void HandleSlotCompletion(int32_t* slot);

// ─────────────────────────────────────────────
// Bytecode VM
// ─────────────────────────────────────────────

void ProcessBytecodeInstruction(int** ip, ConsoleSlot* slot,
                               void (*callback)(int32_t, int32_t, int32_t, void*, ConsoleSlot*, int32_t),
                               int32_t instructionIndex);
void SetupBytecodeContext(ConsoleSlot* slot,
                         void (*callback)(int32_t, int32_t, int32_t, void*, ConsoleSlot*, int32_t));
void ProcessBytecodeOpcode(uint8_t** ip, void* context, int32_t param);
uint32_t BC_Read32(uint8_t** ip);
uint32_t BC_ReadGameState(uint32_t index);
void* GetMissionDataPtr(uint32_t index);
int32_t GetMissionDataOffset(uint32_t index);
void BC_ExecuteCallback(int32_t* state, int32_t param);
void* CreateObjectWithParams(int32_t param1, int32_t param2, int32_t param3,
                             int32_t param4, int32_t param5, int32_t param6,
                             int16_t param7, uint8_t param8, uint16_t param9);

// ─────────────────────────────────────────────
// Object traversal/query
// ─────────────────────────────────────────────

int32_t IterateObjects(void* objects, int32_t (*callback)(void*));
int32_t FindObject(void* objects, int32_t (*callback)(void*), int32_t maxCount);
int32_t FindObjectReverse(void* base, int32_t (*callback)(int32_t), int32_t param);
bool IsObjectFlagSet(void* obj);
bool IsObjectTypeValid(void* obj);
bool IsObjectTypeValid2(void* obj);

// ─────────────────────────────────────────────
// Memory pools
// ─────────────────────────────────────────────

MemoryPoolNode* MemoryPoolAllocate();
void MemoryPoolFree(MemoryPoolNode* node);
uint8_t* MemoryPoolAllocateEx(int32_t index, void* data, int32_t flag);
MemoryPoolNode* SecondaryPoolAllocate();
void SecondaryPoolFree(MemoryPoolNode* node);

// ─────────────────────────────────────────────
// Game state primitives
// ─────────────────────────────────────────────

void SetGameStateParam(int32_t paramType, int32_t value);
int32_t GetGameStateArrayValue(int32_t index);
void InitGameplayState();
void GameplayStateTransition(uint8_t state);
uint32_t AdvanceGameTimer(uint32_t counter, uint32_t* accumulator, uint32_t increment, uint32_t modulus, int32_t commandParam);
int32_t GetTimeOfDay(int32_t* counter);
void UpdateMissionTimers();
int32_t IdentityPassthrough(int32_t value);

// ─────────────────────────────────────────────
// Callback infrastructure
// ─────────────────────────────────────────────

void DrawTextCallback(uint32_t param1, void* param2, void* param3);

// ─────────────────────────────────────────────
// Simple wrappers
// ─────────────────────────────────────────────

void ResetWrapper();
void ClearFlagAndExecute();
void DecrementAndDisplay(void* obj);
void IncrementAndDisplay(void* obj);
void DisplayCounterValue(int32_t counter, int32_t* entry);
void AdvanceWrapper();
void DisplayAndAdvance(int32_t code, const int32_t* entry);
void SimpleWrapper0385();
void SimpleWrapper0523();
void BlitFromPool(int32_t param);

// ─────────────────────────────────────────────
// "nothing" placeholder functions
// ─────────────────────────────────────────────

void FUNC_000987_nothing();
inline void nothing_0x3db() { FUNC_000987_nothing(); }
void nothing_0x583();
void nothing_0x585();
void nothing_0x58c();
void nothing_0x58d();
void nothing_0x58f();
void nothing_0x590();
void nothing_0x592();
void nothing_0x5f5();
void nothing_0x5f6();

} // namespace jjffe

#endif // JJFFE_CORE_ENGINE_H
