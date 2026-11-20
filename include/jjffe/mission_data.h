#ifndef JJFFE_MISSION_DATA_INTEGRATION_H
#define JJFFE_MISSION_DATA_INTEGRATION_H

#include <cstdint>

namespace jjffe {
namespace data {

// Load mission data structures from data blob
bool LoadMissionData();

// Unload mission data
void UnloadMissionData();

// Mission struct table access
const uint8_t* GetMissionStruct(int index);
int GetMissionStructCount();

// Ship object table access
const uint8_t* GetShipObject(int index);
int GetShipObjectCount();

// Ship object field accessors
uint8_t GetShipType(const uint8_t* shipData);
const char* GetShipName(const uint8_t* shipData, char* buffer, int bufferSize);
uint8_t GetShipFlags(const uint8_t* shipData);

} // namespace data
} // namespace jjffe

#endif // JJFFE_MISSION_DATA_INTEGRATION_H
