#ifndef JJFFE_DATA_H
#define JJFFE_DATA_H

#include <cstdint>
#include <cstddef>

namespace jjffe {
namespace data {

// Data blob - contains all DATA_* from ffebmp.asm and ffedat.asm
// Organized as offset/size pairs for each DATA_ label

// Initialize data blob (load from file or embed)
void InitData(const uint8_t* blob, size_t size);

// Get pointer to a specific data block by offset
const uint8_t* GetDataBlock(uint32_t offset);

// Get size of a specific data block
size_t GetDataBlockSize(uint32_t offset);

// Get raw pointer to data blob
const uint8_t* GetDataBlob();
size_t GetDataBlobSize();

// Commonly referenced data blocks
// These correspond to the GLOBAL DATA_* extern references in ffe.asm

// DATA_001697 - Mission data pointer table
const uint8_t* GetMissionData();
size_t GetMissionDataSize();

// DATA_004681 - (to be identified)
// DATA_007975 - Sine lookup table (64KB, used by FUNC_001913_Sin16)
const uint8_t* GetSineTable();
const int32_t* GetSineTable32();

// DATA_008240 / DATA_008241 - (to be identified)

} // namespace data
} // namespace jjffe

#endif // JJFFE_DATA_H
