// ASM DATA_007811..007814 are contiguous labels into one material table.
// Labeled SIZE_DATA_007811 is only 8; the table continues through 007814
// (4440 bytes = 148 entries × 0x1E). FUNC_001691/708/712 index this block.
#include "jjffe/data_globals.h"
#include "jjffe/data_loader.h"
#include "data_blob.h"

#include <cstdint>
#include <cstring>

namespace jjffe {

uint8_t DATA_007811[kMaterialTableBytes] = {};
uint8_t* DATA_007812 = DATA_007811 + 8;  // shade0.R of mat 0
uint8_t* DATA_007813 = DATA_007811 + 9;  // shade0.G of mat 0
uint8_t* DATA_007814 = DATA_007811 + 10; // shade0.B of mat 0

void InitMaterialTableFromBlob()
{
    const uint8_t* blob = data::GetLoadedBlob();
    if (!blob)
        return;
    const uint32_t off = data::OFFSET_DATA_007811;
    const uint32_t need = static_cast<uint32_t>(kMaterialTableBytes);
    if (off + need > data::GetLoadedBlobSize())
        return;
    std::memcpy(DATA_007811, blob + off, need);
    DATA_007812 = DATA_007811 + 8;
    DATA_007813 = DATA_007811 + 9;
    DATA_007814 = DATA_007811 + 10;
}

} // namespace jjffe
