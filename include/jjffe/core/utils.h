#ifndef JJFFE_CORE_UTILS_H
#define JJFFE_CORE_UTILS_H

#include <cstdio>
#include "jjffe/core/engine.h"

namespace jjffe {

// ─────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────

struct BufferedFileStream {
    FILE* file;
    uint8_t* buffer;
    int bufferSize;
    int bytesRead;
    int bufferPos;
};

size_t BufferedFileRead(BufferedFileStream* stream);
size_t BufferedFileReadBytes(BufferedFileStream* stream, void* dest, size_t count);
void BufferedFileSkip(BufferedFileStream* stream, size_t count);
void SoundStreamClose();
int32_t SoundStreamControl(int32_t command, uint32_t param);
long SoundStreamGetUsedBlocks(void);

// ─────────────────────────────────────────────
// Data loading
// ─────────────────────────────────────────────

bool LoadBitmapDataFile(const char* filename);
int32_t ParseDataHeader(int32_t size, uint32_t* data);
int32_t LoadDataBlock(uint32_t size, uint32_t* data);
int32_t LoadImageData(int32_t size, uint32_t* data);
void LoadFloatData();

// ─────────────────────────────────────────────
// String operations
// ─────────────────────────────────────────────

int32_t StringCompareCaseInsensitive(const char* a, const char* b);
intptr_t FindStringInTable(intptr_t dst, int32_t len, int32_t value, const char* name);
int64_t ParseInteger(char** str, int32_t* pos);
int32_t ParseValue(char** str, int32_t* pos, int32_t param);
char* ExpandBracketCodes(char* dst, int32_t maxLen, const char* src);
void ProcessTextFile(int32_t index);

// ─────────────────────────────────────────────
// Text display
// ─────────────────────────────────────────────

void DisplayTextWithIcon(int32_t param);
void DisplayMissionInfo(int32_t index);
void ProcessMissionInput();
void DisplayMissionMenu();
void ShowMissionMenu();

// ─────────────────────────────────────────────
// Save/load
// ─────────────────────────────────────────────

int32_t SaveGameState(void* file, uint32_t* errorCode);
int32_t LoadGameState(void* file, uint32_t* errorCode);
int32_t SaveLoadSlotData(void* file, int32_t isLoad, uint32_t* errorCode);
bool SaveExtendedState(void* file);
bool LoadExtendedState(void* file);

// ─────────────────────────────────────────────
// Mission data CRUD
// ─────────────────────────────────────────────

void IterateMissionObjects(int32_t index);
void HideConsoleButtons();
void SendKeyCodes();
bool FindInMissionList(int32_t index, int32_t value);
bool CheckMissionFlag(int32_t flag, int32_t param);
void ClearMissionFlag(int32_t param);
void ProcessMissionEvent(int32_t param);
void HandleMissionTimer(int32_t* state);
void InitMissionData(uint8_t state);
int32_t GetMissionTimestamp(void* obj, int32_t index);
void SetMissionArrayValue(int32_t index, int32_t value);
int32_t GetMissionArrayValue(int32_t index);
int32_t GetMissionValue(int32_t index, int32_t listIndex, int32_t searchValue);
int32_t Wrapper96(int32_t param1, int32_t param2, int32_t param3);
void AddMissionEntry(int32_t index, int32_t param2, int32_t param3, int32_t param4, int32_t param5);
void SetMissionTimer(int32_t index, int32_t value);
int32_t GetOrCreateMissionId(int32_t index);

// ─────────────────────────────────────────────
// Logging
// ─────────────────────────────────────────────

void LogMessage(const char* format, ...);
void LogCleanup();

// ─────────────────────────────────────────────
// Binary I/O
// ─────────────────────────────────────────────

int32_t ReadInt32LE();
int16_t ReadInt16LE();
uint32_t ReadUInt32LE();

// ─────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────

void SetConfig130();
void SetConfig131();
void SetConfig132();
void SetConfig133();
void SetConfig134();
void SetConfig135();
void SetConfig136();
void SetConfig137();
void SetConfig138();
void SetConfig139();
void SetConfig140();
void SetConfig141();
void SetConfig142();
void InitConfigDefaults();

} // namespace jjffe

#endif // JJFFE_CORE_UTILS_H

namespace jjffe {
// 4.473: distance-scaled sound sample (conversion queue item 001759, docs/CONVERSION_QUEUE.md).
intptr_t FUNC_001759(intptr_t param_1, intptr_t param_2, unsigned param_3);
}
