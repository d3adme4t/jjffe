#ifndef JJFFE_DATA_LOADER_H
#define JJFFE_DATA_LOADER_H

#include "jjffe/data.h"
#include <cstdint>

namespace jjffe {
namespace data {

// Load data blob from binary file
bool LoadDataBlob(const char* path);

// Unload data blob
void UnloadDataBlob();

// Check if data is loaded
bool IsDataLoaded();

// Get raw blob pointer
const uint8_t* GetLoadedBlob();
size_t GetLoadedBlobSize();

// Accessors for key data blocks
const uint8_t* GetSineTableData();
size_t GetSineTableDataSize();

const uint8_t* GetFontData();
size_t GetFontDataSize();

const uint8_t* GetPaletteData();
size_t GetPaletteDataSize();

const uint8_t* GetMissionDataRaw();
size_t GetMissionDataRawSize();

} // namespace data
} // namespace jjffe

#endif // JJFFE_DATA_LOADER_H
