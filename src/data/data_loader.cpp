#include "jjffe/data_loader.h"
#include "data_blob.h"
#include <cstdio>
#include <cstring>

namespace jjffe {
namespace data {

static uint8_t* g_loadedBlob = nullptr;
static size_t g_loadedSize = 0;
static bool g_isLoaded = false;

// Offset map entries
struct OffsetEntry {
    const char* name;
    uint32_t offset;
    uint32_t size;
};

// Key data block offsets (from data_offset_map.txt)
static constexpr uint32_t OFFSET_MISSION_DATA = 59636;     // DATA_001697
static constexpr uint32_t OFFSET_SINE_TABLE   = 274456;    // DATA_007975
static constexpr uint32_t OFFSET_PALETTE      = OFFSET_DATA_007768;
static constexpr uint32_t OFFSET_FONT_DATA    = OFFSET_DATA_007750;
static constexpr uint32_t OFFSET_04681        = 272592;    // DATA_004681
static constexpr uint32_t OFFSET_08240        = 339992;    // DATA_008240
static constexpr uint32_t OFFSET_08241        = 340376;    // DATA_008241

bool LoadDataBlob(const char* path)
{
    if (g_isLoaded) {
        return true;
    }
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open data blob: %s\n", path);
        return false;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0) {
        fprintf(stderr, "Invalid data blob size: %ld\n", size);
        fclose(file);
        return false;
    }
    
    g_loadedBlob = new uint8_t[size];
    if (!g_loadedBlob) {
        fprintf(stderr, "Failed to allocate %ld bytes for data blob\n", size);
        fclose(file);
        return false;
    }
    
    size_t read = fread(g_loadedBlob, 1, size, file);
    fclose(file);
    
    if ((long)read != size) {
        fprintf(stderr, "Failed to read data blob: expected %ld, got %zu\n", size, read);
        delete[] g_loadedBlob;
        g_loadedBlob = nullptr;
        return false;
    }
    
    g_loadedSize = (size_t)size;
    g_isLoaded = true;
    
    // Initialize the data module with the loaded blob
    InitData(g_loadedBlob, g_loadedSize);
    
    printf("Loaded data blob: %zu bytes\n", g_loadedSize);
    return true;
}

void UnloadDataBlob()
{
    if (g_loadedBlob) {
        delete[] g_loadedBlob;
        g_loadedBlob = nullptr;
        g_loadedSize = 0;
        g_isLoaded = false;
    }
}

bool IsDataLoaded()
{
    return g_isLoaded;
}

const uint8_t* GetLoadedBlob()
{
    return g_loadedBlob;
}

size_t GetLoadedBlobSize()
{
    return g_loadedSize;
}

// Accessors for key data blocks

const uint8_t* GetSineTableData()
{
    return GetDataBlock(OFFSET_SINE_TABLE);
}

size_t GetSineTableDataSize()
{
    return 65536;  // Known size
}

const uint8_t* GetFontData()
{
    return GetDataBlock(OFFSET_FONT_DATA);
}

size_t GetFontDataSize()
{
    return 1480;  // Approximate
}

const uint8_t* GetPaletteData()
{
    return GetDataBlock(OFFSET_PALETTE);
}

size_t GetPaletteDataSize()
{
    return 384;  // 128 entries * 3 bytes (RGB)
}

const uint8_t* GetMissionDataRaw()
{
    return GetDataBlock(OFFSET_MISSION_DATA);
}

size_t GetMissionDataRawSize()
{
    return GetDataBlockSize(OFFSET_MISSION_DATA);
}

} // namespace data
} // namespace jjffe
