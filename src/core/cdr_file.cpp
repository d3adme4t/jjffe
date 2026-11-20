#include "jjffe/cdr_file.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cctype>

namespace jjffe {

// Save file header operations

bool CdrFile::WriteSaveHeader(uint8_t* buffer, size_t bufferSize, const SaveFileHeader& header)
{
    if (bufferSize < sizeof(SaveFileHeader)) {
        return false;
    }
    std::memcpy(buffer, &header, sizeof(SaveFileHeader));
    return true;
}

const SaveFileHeader* CdrFile::ReadSaveHeader(const uint8_t* buffer, size_t bufferSize)
{
    if (bufferSize < sizeof(SaveFileHeader)) {
        return nullptr;
    }
    return reinterpret_cast<const SaveFileHeader*>(buffer);
}

bool CdrFile::ValidateSaveHeader(const SaveFileHeader& header)
{
    if (header.magic != SAVE_FILE_MAGIC) {
        return false;
    }
    if (header.version > CURRENT_SAVE_VERSION) {
        return false;
    }
    if (header.payloadSize == 0) {
        return false;
    }
    return true;
}

// SaveGame / LoadGame

// Total size of unmangled payload components
static constexpr size_t MAX_MANGLED_SIZE =
    CdrFile::SAVE_DATA_SIZE +
    CdrFile::SHIP_DATA_ENTRIES * CdrFile::SHIP_DATA_ENTRY_SIZE +
    CdrFile::COMMANDER_NAME_SIZE +
    CdrFile::STATE_BLOCK_SIZE +
    CdrFile::EXTRA_DATA_SIZE + 1024;  // Compression/scramble overhead

bool CdrFile::SaveGame(const char* filepath, const SaveFileHeader& header, int* errorCode)
{
    // Prepare zero-filled game state buffers
    uint8_t* gameState = new uint8_t[CdrFile::SAVE_DATA_SIZE];
    uint8_t* shipData = new uint8_t[CdrFile::SHIP_DATA_ENTRIES * 8];
    uint8_t* commanderName = new uint8_t[CdrFile::COMMANDER_NAME_SIZE];
    uint8_t* stateBlock = new uint8_t[CdrFile::STATE_BLOCK_SIZE];
    uint8_t* extraData = new uint8_t[CdrFile::EXTRA_DATA_SIZE];

    std::memset(gameState, 0, CdrFile::SAVE_DATA_SIZE);
    std::memset(shipData, 0, CdrFile::SHIP_DATA_ENTRIES * 8);
    std::memset(commanderName, 0, CdrFile::COMMANDER_NAME_SIZE);
    std::memcpy(commanderName, header.commanderName, CdrFile::COMMANDER_NAME_SIZE);
    std::memset(stateBlock, 0, CdrFile::STATE_BLOCK_SIZE);
    std::memset(extraData, 0, CdrFile::EXTRA_DATA_SIZE);

    // Mangle the payload first to get size
    uint8_t* payloadBuf = new uint8_t[MAX_MANGLED_SIZE];
    int mangledSize = MangleData(payloadBuf, gameState, shipData, commanderName, stateBlock, extraData);

    delete[] gameState;
    delete[] shipData;
    delete[] commanderName;
    delete[] stateBlock;
    delete[] extraData;

    if (mangledSize <= 0) {
        delete[] payloadBuf;
        if (errorCode) *errorCode = ERR_GENERAL;
        return false;
    }

    // Build header with correct payloadSize
    SaveFileHeader writeHeader = header;
    writeHeader.payloadSize = static_cast<uint32_t>(mangledSize);

    // Allocate buffer for header + payload
    size_t totalSize = sizeof(SaveFileHeader) + static_cast<size_t>(mangledSize);
    uint8_t* buffer = new uint8_t[totalSize];

    // Write header
    if (!WriteSaveHeader(buffer, totalSize, writeHeader)) {
        delete[] payloadBuf;
        delete[] buffer;
        if (errorCode) *errorCode = ERR_GENERAL;
        return false;
    }

    // Copy payload after header
    std::memcpy(buffer + sizeof(SaveFileHeader), payloadBuf, mangledSize);
    delete[] payloadBuf;

    // Write to disk
    bool result = SaveInternal2(filepath, buffer, totalSize, errorCode);
    delete[] buffer;
    return result;
}

bool CdrFile::LoadGame(const char* filepath, SaveFileHeader* outHeader, int* errorCode)
{
    // Get file size first
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        if (errorCode) *errorCode = ERR_NO_FILE;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize < (long)sizeof(SaveFileHeader)) {
        fclose(f);
        if (errorCode) *errorCode = ERR_FORMAT;
        return false;
    }

    size_t readSize = static_cast<size_t>(fileSize);

    // Allocate read buffer
    uint8_t* readBuf = new uint8_t[readSize];
    size_t actualRead = fread(readBuf, 1, readSize, f);
    fclose(f);

    if (actualRead != readSize) {
        delete[] readBuf;
        if (errorCode) *errorCode = ERR_FORMAT;
        return false;
    }

    // Read and validate header
    const SaveFileHeader* hdrPtr = ReadSaveHeader(readBuf, readSize);
    if (!hdrPtr) {
        delete[] readBuf;
        if (errorCode) *errorCode = ERR_FORMAT;
        return false;
    }

    if (!ValidateSaveHeader(*hdrPtr)) {
        delete[] readBuf;
        if (errorCode) *errorCode = ERR_FORMAT;
        return false;
    }

    // Copy header to output
    if (outHeader) {
        std::memcpy(outHeader, hdrPtr, sizeof(SaveFileHeader));
    }

    // Extract payload
    const uint8_t* payloadPtr = readBuf + sizeof(SaveFileHeader);
    size_t payloadSize = readSize - sizeof(SaveFileHeader);

    if (payloadSize == 0) {
        delete[] readBuf;
        if (errorCode) *errorCode = ERR_FORMAT;
        return false;
    }

    // Allocate destination buffers
    uint8_t* gameState = new uint8_t[CdrFile::SAVE_DATA_SIZE];
    uint8_t* shipData = new uint8_t[CdrFile::SHIP_DATA_ENTRIES * 8];
    uint8_t* commanderName = new uint8_t[CdrFile::COMMANDER_NAME_SIZE];
    uint8_t* stateBlock = new uint8_t[CdrFile::STATE_BLOCK_SIZE];
    uint8_t* extraData = new uint8_t[CdrFile::EXTRA_DATA_SIZE];

    std::memset(gameState, 0, CdrFile::SAVE_DATA_SIZE);
    std::memset(shipData, 0, CdrFile::SHIP_DATA_ENTRIES * 8);
    std::memset(commanderName, 0, CdrFile::COMMANDER_NAME_SIZE);
    std::memset(stateBlock, 0, CdrFile::STATE_BLOCK_SIZE);
    std::memset(extraData, 0, CdrFile::EXTRA_DATA_SIZE);

    // Unmangle payload
    bool unmangleOk = UnmangleData(payloadPtr, payloadSize, gameState, shipData, commanderName, stateBlock, extraData);

    delete[] gameState;
    delete[] shipData;
    delete[] commanderName;
    delete[] stateBlock;
    delete[] extraData;
    delete[] readBuf;

    if (!unmangleOk) {
        if (errorCode) *errorCode = ERR_CRC;
        return false;
    }

    if (errorCode) *errorCode = ERR_NONE;
    return true;
}

uint32_t CdrFile::s_scrambleKey = 0;
uint32_t CdrFile::s_lastLoaded[LAST_LOADED_SIZE / 4] = {};
char CdrFile::s_fileExtTens = '0';
char CdrFile::s_fileExtOnes = '0';

int CdrFile::StrChrNon(const char* src, const char* end, char matchChar)
{
    const char* p = src;
    const char* limit = (src + 0xFF < end) ? src + 0xFF : end;
    
    while (p < limit && *p == matchChar) {
        ++p;
    }
    
    return static_cast<int>(p - src);
}

int CdrFile::CompressZeros(uint8_t* dst, const uint8_t* src, size_t srcLen)
{
    const uint8_t* srcEnd = src + srcLen;
    uint8_t* dstStart = dst;
    
    while (src < srcEnd) {
        uint8_t byte = *src++;
        *dst++ = byte;
        
        if (byte == 0) {
            uint8_t zeroCount = static_cast<uint8_t>(StrChrNon(
                reinterpret_cast<const char*>(src),
                reinterpret_cast<const char*>(srcEnd),
                '\0'
            ));
            *dst++ = zeroCount;
            src += zeroCount;
        }
    }
    
    return static_cast<int>(dst - dstStart);
}

int CdrFile::ExpandZeros(uint8_t* dst, const uint8_t* src, size_t dstLen)
{
    const uint8_t* srcStart = src;
    size_t remaining = dstLen;
    
    while (remaining > 0) {
        uint8_t byte = *src++;
        *dst++ = byte;
        --remaining;
        
        if (byte == 0) {
            uint8_t zeroCount = *src++;
            size_t actualZeros = (remaining < zeroCount) ? remaining : zeroCount;
            
            for (size_t i = 0; i < actualZeros; ++i) {
                *dst++ = 0;
            }
            
            remaining -= actualZeros;
        }
    }
    
    return static_cast<int>(src - srcStart);
}

void CdrFile::Scramble(uint16_t* data, size_t byteLen)
{
    int32_t key = static_cast<int32_t>(s_scrambleKey + SCRAMBLE_SEED);
    size_t wordCount = byteLen / 2;
    
    for (size_t i = 0; i < wordCount; ++i) {
        uint16_t original = data[i];
        data[i] ^= static_cast<uint16_t>(key);
        // ASM keeps this in a 32-bit register (wrap); compute unsigned.
        key = static_cast<int32_t>((static_cast<uint32_t>(key) +
                                    static_cast<uint32_t>(static_cast<int16_t>(original))) * 2u);
    }
}

void CdrFile::Unscramble(uint16_t* data, size_t byteLen)
{
    int32_t key = static_cast<int32_t>(s_scrambleKey + SCRAMBLE_SEED);
    size_t wordCount = byteLen / 2;
    
    for (size_t i = 0; i < wordCount; ++i) {
        data[i] ^= static_cast<uint16_t>(key);
        uint16_t decrypted = data[i];
        // Mirror of Scramble: 32-bit register wrap (UBSan: signed overflow).
        key = static_cast<int32_t>((static_cast<uint32_t>(key) +
                                    static_cast<uint32_t>(static_cast<int16_t>(decrypted))) * 2u);
    }
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000170
// (core.cpp, CdrFileMangleData).
int CdrFile::MangleData(uint8_t* output, const uint8_t* gameState,
                        const uint8_t* shipData, const uint8_t* commanderName,
                        const uint8_t* stateBlock, const uint8_t* extraData)
{
    output[0] = MANGLE_MAGIC;
    
    int compressed = CompressZeros(output + 1, gameState, SAVE_DATA_SIZE);
    
    uint8_t* ptr = output + 1 + compressed;
    
    for (size_t i = 0; i < SHIP_DATA_ENTRIES; i++) {
        memcpy(ptr, shipData + i * 8 + 4, SHIP_DATA_ENTRY_SIZE);
        ptr += SHIP_DATA_ENTRY_SIZE;
    }
    
    memcpy(ptr, commanderName, COMMANDER_NAME_SIZE);
    ptr += COMMANDER_NAME_SIZE;
    
    int stateCompressed = CompressZeros(ptr, stateBlock, STATE_BLOCK_SIZE);
    ptr += stateCompressed;
    
    int extraCompressed = CompressZeros(ptr, extraData, EXTRA_DATA_SIZE);
    ptr += extraCompressed;
    
    size_t totalLen = ptr - output;
    if (totalLen & 1) {
        totalLen++;
        *ptr++ = 0;
    }
    
    Scramble(reinterpret_cast<uint16_t*>(output), totalLen);

    uint32_t checksum = s_scrambleKey;
    memcpy(ptr, &checksum, 4);
    ptr += 4;

    // Append data integrity checksum (sum of all unscrambled bytes)
    uint32_t dataChecksum = 0;
    for (size_t i = 0; i < totalLen; i++) {
        dataChecksum += output[i];
    }
    memcpy(ptr, &dataChecksum, 4);

    return static_cast<int>(totalLen + 8);  // 4-byte scramble key + 4-byte data checksum
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000171
// (core.cpp, CdrFileUnmangleData).
bool CdrFile::UnmangleData(const uint8_t* input, size_t inputLen,
                           uint8_t* gameState, uint8_t* shipData,
                           uint8_t* commanderName, uint8_t* stateBlock,
                           uint8_t* extraData)
{
    // New format: [scrambled data] [scramble key 4B] [data checksum 4B] = 8 byte trailer
    // Old format:  [scrambled data] [scramble key 4B] = 4 byte trailer
    const size_t NEW_TRAILER = 8;
    const size_t OLD_TRAILER = 4;

    if (inputLen < OLD_TRAILER + 1) {
        return false;
    }

    bool hasDataChecksum = inputLen >= NEW_TRAILER + 1;
    size_t trailerSize = hasDataChecksum ? NEW_TRAILER : OLD_TRAILER;
    size_t scrambledLen = inputLen - trailerSize;

    // Ensure scrambled length is even (needed for word-based unscramble)
    if (scrambledLen & 1) {
        return false;
    }

    // Read stored checksums
    uint32_t storedChecksum;
    memcpy(&storedChecksum, input + scrambledLen, 4);

    uint32_t storedDataChecksum = 0;
    if (hasDataChecksum) {
        memcpy(&storedDataChecksum, input + scrambledLen + 4, 4);
    }

    // Verify data integrity checksum on RAW (scrambled) input before unscrambling
    if (hasDataChecksum) {
        uint32_t computedDataChecksum = 0;
        for (size_t i = 0; i < scrambledLen; i++) {
            computedDataChecksum += input[i];
        }
        if (storedDataChecksum != computedDataChecksum) {
            return false;
        }
    }

    // Copy and unscramble payload
    uint8_t* tempBuf = new uint8_t[scrambledLen];
    memcpy(tempBuf, input, scrambledLen);
    Unscramble(reinterpret_cast<uint16_t*>(tempBuf), scrambledLen);

    // Verify scramble key
    uint32_t computedChecksum = s_scrambleKey;
    if (storedChecksum != computedChecksum) {
        delete[] tempBuf;
        return false;
    }

    if (tempBuf[0] != MANGLE_MAGIC) {
        delete[] tempBuf;
        return false;
    }

    int expanded = ExpandZeros(gameState, tempBuf + 1, SAVE_DATA_SIZE);

    const uint8_t* ptr = tempBuf + 1 + expanded;

    for (size_t i = 0; i < SHIP_DATA_ENTRIES; i++) {
        memcpy(shipData + i * 8 + 4, ptr, SHIP_DATA_ENTRY_SIZE);
        ptr += SHIP_DATA_ENTRY_SIZE;
    }

    memcpy(commanderName, ptr, COMMANDER_NAME_SIZE);
    ptr += COMMANDER_NAME_SIZE;

    int stateExpanded = ExpandZeros(stateBlock, ptr, STATE_BLOCK_SIZE);
    ptr += stateExpanded;
    
    ExpandZeros(extraData, ptr, EXTRA_DATA_SIZE);
    
    delete[] tempBuf;
    return true;
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000174
// (core.cpp, CdrFileSaveInternal).
bool CdrFile::SaveInternal(const char* filename, const void* data, size_t dataLen, int* errorCode)
{
    char filepath[MAX_FILENAME];
    snprintf(filepath, sizeof(filepath), "%s", filename);
    return SaveInternal2(filepath, data, dataLen, errorCode);
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000175
// (core.cpp, CdrFileSaveInternal2).
bool CdrFile::SaveInternal2(const char* filepath, const void* data, size_t dataLen, int* errorCode)
{
    FILE* f = fopen(filepath, "wb");
    if (!f) {
        if (errorCode) *errorCode = ERR_DISK_FULL;
        return false;
    }
    
    size_t written = fwrite(data, 1, dataLen, f);
    fclose(f);
    
    if (written != dataLen) {
        if (errorCode) *errorCode = ERR_DISK_FULL;
        return false;
    }
    
    if (errorCode) *errorCode = ERR_NONE;
    return true;
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000176
// (core.cpp, CdrFileLoadInternal).
bool CdrFile::LoadInternal(const char* filename, void* data, size_t maxLen, int* errorCode)
{
    char filepath[MAX_FILENAME];
    snprintf(filepath, sizeof(filepath), "%s", filename);
    return LoadInternal2(filepath, data, maxLen, errorCode);
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000177
// (core.cpp, CdrFileLoadInternal2).
bool CdrFile::LoadInternal2(const char* filepath, void* data, size_t maxLen, int* errorCode)
{
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        if (errorCode) *errorCode = ERR_NO_FILE;
        return false;
    }
    
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    size_t toRead = (fileSize > (long)maxLen) ? maxLen : (size_t)fileSize;
    size_t readBytes = fread(data, 1, toRead, f);
    fclose(f);
    
    if (readBytes == 0) {
        if (errorCode) *errorCode = ERR_NO_FILE;
        return false;
    }
    
    if (errorCode) *errorCode = ERR_NONE;
    return true;
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000173
// (core.cpp, CdrFileDeleteInternal).
bool CdrFile::DeleteInternal(const char* filename, int* errorCode)
{
    if (remove(filename) != 0) {
        if (errorCode) *errorCode = ERR_DISK_READ_ONLY;
        return false;
    }
    if (errorCode) *errorCode = ERR_NONE;
    return true;
}

int32_t FUNC_000172_CdrFileConfirmYN(void);

// FUNC_000172_CdrFileConfirmYN
bool CdrFile::ConfirmYN()
{
    return FUNC_000172_CdrFileConfirmYN() != 0;
}

// FUNC_000178_CdrFileSetLastLoaded
void CdrFile::SetLastLoaded(const uint32_t* data)
{
    memcpy(s_lastLoaded, data, LAST_LOADED_SIZE);
}

const uint32_t* CdrFile::GetLastLoaded()
{
    return s_lastLoaded;
}

void FUNC_000181_CdrFileDispErrorInternal(int32_t, intptr_t);
void FUNC_000182_CdrFileDispErrorCode(int);
void FUNC_000183_CdrFileDispErrorDirect(int32_t);

// FUNC_000181_CdrFileDispErrorInternal
void CdrFile::DispErrorInternal(int messageId, const void* params)
{
    FUNC_000181_CdrFileDispErrorInternal(messageId, reinterpret_cast<intptr_t>(params));
}

// FUNC_000182_CdrFileDispErrorCode
void CdrFile::DispErrorCode(int errorCode)
{
    FUNC_000182_CdrFileDispErrorCode(errorCode);
}

// FUNC_000183_CdrFileDispErrorDirect
void CdrFile::DispErrorDirect(int messageId)
{
    FUNC_000183_CdrFileDispErrorDirect(messageId);
}

// The port's own ".jff" format (ui.cpp's menu layer), not the ASM's FUNC_000184
// (core.cpp, CdrFileIncFileExtension).
void CdrFile::IncFileExtension()
{
    s_fileExtOnes++;
    if (s_fileExtOnes > '9') {
        s_fileExtOnes = '0';
        s_fileExtTens++;
        if (s_fileExtTens > '9') {
            s_fileExtTens = '0';
        }
    }
}

void CdrFile::SetFileExtension(char tens, char ones)
{
    s_fileExtTens = tens;
    s_fileExtOnes = ones;
}

void CdrFile::GetFileExtension(char* tens, char* ones)
{
    if (tens) *tens = s_fileExtTens;
    if (ones) *ones = s_fileExtOnes;
}

} // namespace jjffe
