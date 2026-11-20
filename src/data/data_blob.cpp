#include "jjffe/data.h"
#include "data_blob.h"
#include <cstring>
#include <cstdlib>

namespace jjffe {
namespace data {

// Global data blob
static const uint8_t* g_dataBlob = nullptr;
static size_t g_dataBlobSize = 0;

// Offsets for commonly referenced data (from data_offset_map.txt)
// These are extracted from the NASM assembly and converted to blob offsets
static constexpr uint32_t OFFSET_MISSION_DATA = 59636;     // DATA_001697
static constexpr uint32_t OFFSET_SINE_TABLE   = 274456;    // DATA_007975
static constexpr uint32_t OFFSET_PALETTE      = OFFSET_DATA_007768;
static constexpr uint32_t OFFSET_FONT_DATA    = OFFSET_DATA_007750;
static constexpr uint32_t OFFSET_04681        = 272592;    // DATA_004681
static constexpr uint32_t OFFSET_08240        = 339992;    // DATA_008240
static constexpr uint32_t OFFSET_08241        = 340376;    // DATA_008241

void InitData(const uint8_t* blob, size_t size)
{
    g_dataBlob = blob;
    g_dataBlobSize = size;
}

const uint8_t* GetDataBlock(uint32_t offset)
{
    if (!g_dataBlob || offset >= g_dataBlobSize) {
        return nullptr;
    }
    return g_dataBlob + offset;
}

size_t GetDataBlockSize(uint32_t offset)
{
    // Size is not directly stored - caller should know expected size
    // or we could use the offset map
    if (offset >= g_dataBlobSize) {
        return 0;
    }
    return g_dataBlobSize - offset;
}

const uint8_t* GetDataBlob()
{
    return g_dataBlob;
}

size_t GetDataBlobSize()
{
    return g_dataBlobSize;
}

// Mission data (DATA_001697)
const uint8_t* GetMissionData()
{
    return GetDataBlock(OFFSET_MISSION_DATA);
}

size_t GetMissionDataSize()
{
    return GetDataBlockSize(OFFSET_MISSION_DATA);
}

// Sine lookup table (DATA_007975 - 65536 bytes)
// Used by FUNC_001913_Sin16 for fast trigonometry
const uint8_t* GetSineTable()
{
    return GetDataBlock(OFFSET_SINE_TABLE);
}

const int32_t* GetSineTable32()
{
    return reinterpret_cast<const int32_t*>(GetDataBlock(OFFSET_SINE_TABLE));
}

} // namespace data
} // namespace jjffe
