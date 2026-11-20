#include "jjffe/mission_data.h"
#include "jjffe/mission_data_table.h"
#include "jjffe/data.h"
#include <cstring>

namespace jjffe {
namespace data {

static bool g_missionLoaded = false;
static const uint8_t* g_missionTable = nullptr;
static const uint8_t* g_shipTable = nullptr;

bool LoadMissionData()
{
    if (g_missionLoaded) {
        return true;
    }
    
    const uint8_t* blob = GetDataBlob();
    if (!blob) {
        return false;
    }
    
    // Set mission struct table pointer
    g_missionTable = blob;  // Offsets are relative to blob start
    
    // Set ship object table pointer
    g_shipTable = blob;
    
    g_missionLoaded = true;
    return true;
}

void UnloadMissionData()
{
    g_missionLoaded = false;
    g_missionTable = nullptr;
    g_shipTable = nullptr;
}

// Mission struct table access

const uint8_t* GetMissionStruct(int index)
{
    if (!g_missionLoaded || index < 0 || index >= (int)MISSION_TABLE_COUNT) {
        return nullptr;
    }
    
    uint32_t offset = MISSION_TABLE_OFFSETS[index];
    if (offset == 0) {
        return nullptr;
    }
    
    const uint8_t* blob = GetDataBlob();
    if (!blob || offset >= GetDataBlobSize()) {
        return nullptr;
    }
    
    return blob + offset;
}

int GetMissionStructCount()
{
    return MISSION_TABLE_COUNT;
}

// Ship object table access

const uint8_t* GetShipObject(int index)
{
    if (!g_missionLoaded || index < 0 || index >= (int)SHIP_OBJECT_TABLE_COUNT) {
        return nullptr;
    }
    
    uint32_t offset = SHIP_OBJECT_TABLE_OFFSETS[index];
    if (offset == 0) {
        return nullptr;
    }
    
    const uint8_t* blob = GetDataBlob();
    if (!blob || offset >= GetDataBlobSize()) {
        return nullptr;
    }
    
    return blob + offset;
}

int GetShipObjectCount()
{
    return SHIP_OBJECT_TABLE_COUNT;
}

// Ship object field accessors

uint8_t GetShipType(const uint8_t* shipData)
{
    if (!shipData) return 0;
    return shipData[0];  // First byte is type/flags
}

const char* GetShipName(const uint8_t* shipData, char* buffer, int bufferSize)
{
    if (!shipData || !buffer || bufferSize < 2) {
        if (buffer && bufferSize > 0) buffer[0] = '\0';
        return buffer;
    }
    
    // Name is at offset 1 (after type byte)
    const uint8_t* namePtr = shipData + 1;
    int maxLen = bufferSize - 1;
    
    // Copy name until null terminator or max length
    int i;
    for (i = 0; i < maxLen && namePtr[i] != 0; i++) {
        buffer[i] = (char)namePtr[i];
    }
    buffer[i] = '\0';
    
    return buffer;
}

uint8_t GetShipFlags(const uint8_t* shipData)
{
    if (!shipData) return 0;
    return shipData[0];  // First byte contains flags
}

} // namespace data
} // namespace jjffe
