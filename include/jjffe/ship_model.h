#ifndef JJFFE_SHIP_MODEL_H
#define JJFFE_SHIP_MODEL_H

#include <cstdint>

namespace jjffe {

// Ship model ObjectStruct - matches original32-bit DOS layout (68 bytes = 0x44)
// On64-bit systems, pointer fields are stored as uint32_t offsets into blob
// Use accessor methods to get actual pointers
struct ShipModel {
    uint32_t meshDataOffset;       // +0x00: Offset to polygon bytecode stream
    uint32_t vertexDataOffset;     // +0x04: Offset to vertex coordinate table
    uint32_t vertexCount;          // +0x08: Number of vertices (including mirrors)
    uint32_t normalDataOffset;     // +0x0C: Offset to face normal vectors
    uint32_t normalCount;          // +0x10: Number of normals
    uint32_t scaleRelated;         // +0x14: Detail level / scale related
    uint32_t scaleFactor;          // +0x18: Distance scaling exponent
    uint32_t radius;               // +0x1C: Bounding sphere radius
    uint32_t vanishingSize;        // +0x20: LOD threshold / vanishing size
    uint8_t starColor[4];          // +0x24: Distant star color [R, G, B, 0]
    uint32_t starSize;             // +0x28: Distant star size
    uint32_t viewZOffset;          // +0x2C: Camera Z offset (fixed-point, *256)
    uint32_t viewYOffset;          // +0x30: Camera Y offset (fixed-point, *256)
    uint32_t collisionDataOffset;  // +0x34: Offset to collision hull data
    uint32_t specsDataOffset;      // +0x38: Offset to ship specifications
    uint32_t reserved1;            // +0x3C: Reserved
    uint32_t reserved2;            // +0x40: Reserved / animation keypoints
};

static_assert(sizeof(ShipModel) == 68, "ShipModel must be 68 bytes (0x44)");

// Ship specifications (48 bytes from srctools/shuttle.txt)
struct ShipSpecs {
    int16_t fthrust;       // +0x00: Forward thrust
    int16_t rthrust;       // +0x02: Reverse thrust
    int16_t gunmount;      // +0x04: Gun mounting
    int16_t mass;          // +0x06: Mass
    int16_t intcap;        // +0x08: Internal capacity
    int16_t price;         // +0x0A: Price
    int16_t zoomFactor;    // +0x0C: Zoom factor
    int16_t id;            // +0x0E: Ship ID
    int16_t crew;          // +0x10: Crew size
    int16_t missiles;      // +0x12: Missile count
    int16_t drive;         // +0x14: Drive type
    int16_t elitePoints;   // +0x16: Elite points
    int16_t viewZOff;      // +0x18: View Z offset
    int16_t viewYOff;      // +0x1A: View Y offset
    int16_t unknown1;      // +0x1C
    int16_t unknown2;      // +0x1E
    int16_t fgunZOff;      // +0x20: Forward gun Z offset
    int16_t fgunYOff;      // +0x22: Forward gun Y offset
    int16_t rgunZOff;      // +0x24: Rear gun Z offset
    int16_t rgunYOff;      // +0x26: Rear gun Y offset
    int16_t ttgunZOff;     // +0x28: Top turret gun Z offset
    int16_t ttgunYOff;     // +0x2A: Top turret gun Y offset
    int16_t btgunZOff;     // +0x2C: Bottom turret gun Z offset
    int16_t btgunYOff;     // +0x2E: Bottom turret gun Y offset
};

static_assert(sizeof(ShipSpecs) == 48, "ShipSpecs must be 48 bytes");

// Vertex format (6 bytes each)
struct ShipVertex {
    uint8_t mirrorFlag;    // 1 = has YZ-plane mirror copy at index+1
    uint8_t padding1;
    int8_t x;              // Signed 8-bit X coordinate (left)
    int8_t y;              // Signed 8-bit Y coordinate (up)
    int8_t z;              // Signed 8-bit Z coordinate (forward)
    uint8_t padding2;
};

static_assert(sizeof(ShipVertex) == 6, "ShipVertex must be 6 bytes");

// Normal format (same as vertex: 6 bytes each)
struct ShipNormal {
    uint8_t mirrorFlag;    // 1 = has YZ-plane mirror copy at index+1
    uint8_t padding1;
    int8_t x;              // Normal X component
    int8_t y;              // Normal Y component
    int8_t z;              // Normal Z component
    uint8_t padding2;
};

static_assert(sizeof(ShipNormal) == 6, "ShipNormal must be 6 bytes");

// Initialize ship model data from blob
// Populates DATA_004681 with resolved pointers for known ship types
void InitShipModels();

// Get ship model by index (0-465)
// Returns nullptr if index is out of range or model not loaded
const ShipModel* GetShipModel(int index);

// Get the index of a ship model (reverse lookup from pointer)
// Returns -1 if the pointer is not from the s_shipModels array
int GetShipModelIndex(const ShipModel* model);

// Get ship model for a game object
// Reads model index from GameObj+0x82 and looks up in DATA_004681
const ShipModel* GetShipModelForGameObj(const void* gameObj);

// Get actual pointer from ShipModel offset field
// The offset is relative to the data blob base
const uint8_t* GetShipModelPointer(uint32_t offset);

// Low-level accessors for DATA_004681 table (implemented in core.cpp)
// These are used internally by InitShipModels
void SetShipModelPtr(int32_t index, uint8_t* ptr);
uint8_t* GetShipModelPtr(int32_t index);

// Accessors for rendering globals (implemented in core.cpp)
void SetRenderCallback(intptr_t fn);
intptr_t GetRenderCallback();
void SetBackBufferPtr(uint8_t* ptr);

// Title-scene clock / ObjectStruct (FUNC_000959…001682 path)
void SetTitleClock(uint32_t clock);
uint32_t GetTitleClock();
uint8_t* GetTitleObject();

// Optional host paint for title types #176/#177 (x64 mesh/job path incomplete).
// Return true if the hook painted the object (skips FUNC_001683).
using TitleObjectPaintFn = bool (*)(uint8_t* obj, uint32_t type);
void SetTitleObjectPaintHook(TitleObjectPaintFn fn);

// Facing flags table (DATA_009201) — per-polygon material shade from FUNC_001481
void GetFacingFlagsShade(int normalIdx, int8_t* outR, int8_t* outG, int8_t* outB);
void SetFacingFlagsShade(int normalIdx, int32_t dotProduct, const uint8_t shadeTable[24]);
void SetFacingFlagsCullBit(int normalIdx, bool farBit);
bool GetFacingFlagsCull(int normalIdx);
// FUNC_001480: indices 0..1 clear bit0/1; 2..normalCount-1 set bit0|bit1 (dirty).
void InitFacingFlagsCullDirty(int normalCount);
// FUNC_001751: byte3 bit1 ⇒ need lazy FUNC_001481 before reading bit0.
bool FacingFlagsCullDirty(int normalIdx);

} // namespace jjffe

#endif // JJFFE_SHIP_MODEL_H
