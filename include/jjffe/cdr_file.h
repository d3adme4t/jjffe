#ifndef JJFFE_CDR_FILE_H
#define JJFFE_CDR_FILE_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace jjffe {

// Save file format constants
static constexpr uint32_t SAVE_FILE_MAGIC = 0x4A464645;  // "JFFE" in little-endian
static constexpr uint32_t CURRENT_SAVE_VERSION = 1;
static constexpr size_t MAX_COMMANDER_DISPLAY_NAME = 64;

// Save file header struct — field ordering avoids padding (96 bytes total)
// uint64_t placed first to avoid alignment padding before it
struct SaveFileHeader {
    uint64_t timestamp;                      // Unix epoch seconds (time_t)
    uint32_t magic;                          // 0x4A464645 ("JFFE")
    uint32_t version;                        // CURRENT_SAVE_VERSION
    uint32_t flags;                          // Reserved (0)
    uint32_t gameProgress;                   // Progress indicator (mission number, etc.)
    uint32_t payloadSize;                    // Size of mangled payload following header
    char commanderName[MAX_COMMANDER_DISPLAY_NAME]; // Null-terminated display name
};

static_assert(sizeof(SaveFileHeader) == 96,
              "SaveFileHeader must be 96 bytes (8+4+4+4+4+4+64 + 4 trailing alignment padding)");

class CdrFile {
public:
    static constexpr uint8_t MANGLE_MAGIC = 0x11;
    static constexpr uint32_t SCRAMBLE_SEED = 0x12345678;
    static constexpr size_t MAX_ZERO_RUN = 0xFF;

    static constexpr size_t SAVE_DATA_SIZE = 0x984A;
    static constexpr size_t STATE_BLOCK_SIZE = 0x556A;
    static constexpr size_t EXTRA_DATA_SIZE = 0x51;
    static constexpr size_t SHIP_DATA_ENTRIES = 6;
    static constexpr size_t SHIP_DATA_ENTRY_SIZE = 4;
    static constexpr size_t COMMANDER_NAME_SIZE = 0x40;

    static constexpr size_t MAX_FILENAME = 96;
    static constexpr size_t LAST_LOADED_SIZE = 24;

    enum ErrorCode : int {
        ERR_NONE = 0,
        ERR_DISK_FULL = 2,
        ERR_DISK_READ_ONLY = 3,
        ERR_NO_DISK = 5,
        ERR_CRC = 8,
        ERR_GENERAL = 0x10,
        ERR_NOT_COMMANDER = 0x13,
        ERR_DISK_ERROR_2 = 0x15,
        ERR_DISK_ERROR_3 = 0x17,
        ERR_FORMAT = 0x19,
        ERR_INTERNAL = 0x1F,
        ERR_NO_FILE = 0x50,
    };

    // Core algorithms (already implemented)
    static int StrChrNon(const char* src, const char* end, char matchChar);
    static int CompressZeros(uint8_t* dst, const uint8_t* src, size_t srcLen);
    static int ExpandZeros(uint8_t* dst, const uint8_t* src, size_t dstLen);
    static void Scramble(uint16_t* data, size_t byteLen);
    static void Unscramble(uint16_t* data, size_t byteLen);

    // Mangle/Unmangle (FUNC_000170, FUNC_000171)
    static int MangleData(uint8_t* output, const uint8_t* gameState,
                          const uint8_t* shipData, const uint8_t* commanderName,
                          const uint8_t* stateBlock, const uint8_t* extraData);
    static bool UnmangleData(const uint8_t* input, size_t inputLen,
                             uint8_t* gameState, uint8_t* shipData,
                             uint8_t* commanderName, uint8_t* stateBlock,
                             uint8_t* extraData);

    // Save/Load (FUNC_000174-000177)
    static bool SaveInternal(const char* filename, const void* data, size_t dataLen, int* errorCode);
    static bool SaveInternal2(const char* filepath, const void* data, size_t dataLen, int* errorCode);
    static bool LoadInternal(const char* filename, void* data, size_t maxLen, int* errorCode);
    static bool LoadInternal2(const char* filepath, void* data, size_t maxLen, int* errorCode);

    // Delete (FUNC_000173)
    static bool DeleteInternal(const char* filename, int* errorCode);

    // Confirm (FUNC_000172)
    static bool ConfirmYN();

    // State management (FUNC_000178)
    static void SetLastLoaded(const uint32_t* data);
    static const uint32_t* GetLastLoaded();

    // Error display (FUNC_000180-000183)
    static void DispErrorInternal(int messageId, const void* params);
    static void DispErrorCode(int errorCode);
    static void DispErrorDirect(int messageId);

    // File extension (FUNC_000184)
    static void IncFileExtension();
    static void SetFileExtension(char tens, char ones);
    static void GetFileExtension(char* tens, char* ones);

    // Scramble key
    static void SetScrambleKey(uint32_t key) { s_scrambleKey = key; }
    static uint32_t GetScrambleKey() { return s_scrambleKey; }

    // Save file header operations
    static bool WriteSaveHeader(uint8_t* buffer, size_t bufferSize, const SaveFileHeader& header);
    static const SaveFileHeader* ReadSaveHeader(const uint8_t* buffer, size_t bufferSize);
    static bool ValidateSaveHeader(const SaveFileHeader& header);

    // Save/Load game (header + payload)
    static bool SaveGame(const char* filepath, const SaveFileHeader& header, int* errorCode);
    static bool LoadGame(const char* filepath, SaveFileHeader* outHeader, int* errorCode);

private:
    static uint32_t s_scrambleKey;
    static uint32_t s_lastLoaded[LAST_LOADED_SIZE / 4];
    static char s_fileExtTens;
    static char s_fileExtOnes;
};

} // namespace jjffe

#endif
