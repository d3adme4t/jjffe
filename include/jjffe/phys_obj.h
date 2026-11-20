#ifndef JJFFE_PHYS_OBJ_H
#define JJFFE_PHYS_OBJ_H

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace jjffe {

// ASM PhysObj is 0x152 bytes (same size as Phase 60 GameObj scaffolding).
// Named GameObj fields are NOT these offsets — use this overlay for SysGen.
constexpr size_t kPhysObjSize = 0x152;

enum class PhysObjControl : uint8_t {
    kIntro         = 0x00,
    kOrbital       = 0x01,  // planets, stations, starports
    kPlayer        = 0x03,
    kAiShip        = 0x0b,
    kMilitaryBase  = 0x0e,
};

enum PhysObjFlag : uint8_t {
    kPhysFlagLightIndexMask = 0x07,
    kPhysFlagLight          = 0x08,
    kPhysFlagDockable       = 0x10,  // station or starport
    kPhysFlagSurfaceStarport= 0x20,  // type 0x23
    kPhysFlagOpenAir        = 0x40,  // military-range models 0x51–0x5a
    kPhysFlagComplex        = 0x80,
};

struct PhysObj {
    uint8_t raw[kPhysObjSize];
};

static_assert(sizeof(PhysObj) == kPhysObjSize, "PhysObj must be 0x152");

inline void PhysObjInit(PhysObj* obj)
{
    if (obj) std::memset(obj->raw, 0, kPhysObjSize);
}

inline uint8_t PhysObjGetU8(const PhysObj* obj, size_t off)
{
    return (obj && off < kPhysObjSize) ? obj->raw[off] : 0;
}

inline void PhysObjSetU8(PhysObj* obj, size_t off, uint8_t v)
{
    if (obj && off < kPhysObjSize) obj->raw[off] = v;
}

inline uint16_t PhysObjGetU16(const PhysObj* obj, size_t off)
{
    uint16_t v = 0;
    if (obj && off + 1 < kPhysObjSize) std::memcpy(&v, obj->raw + off, 2);
    return v;
}

inline void PhysObjSetU16(PhysObj* obj, size_t off, uint16_t v)
{
    if (obj && off + 1 < kPhysObjSize) std::memcpy(obj->raw + off, &v, 2);
}

inline int32_t PhysObjGetI32(const PhysObj* obj, size_t off)
{
    int32_t v = 0;
    if (obj && off + 3 < kPhysObjSize) std::memcpy(&v, obj->raw + off, 4);
    return v;
}

inline void PhysObjSetI32(PhysObj* obj, size_t off, int32_t v)
{
    if (obj && off + 3 < kPhysObjSize) std::memcpy(obj->raw + off, &v, 4);
}

inline uint8_t PhysObjGetParent(const PhysObj* obj) { return PhysObjGetU8(obj, 0x56); }
inline void    PhysObjSetParent(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0x56, v); }

inline int32_t PhysObjGetModel(const PhysObj* obj) { return PhysObjGetI32(obj, 0x82); }
inline void    PhysObjSetModel(PhysObj* obj, int32_t v) { PhysObjSetI32(obj, 0x82, v); }

inline uint8_t PhysObjGetIndex(const PhysObj* obj) { return PhysObjGetU8(obj, 0x86); }
inline void    PhysObjSetIndex(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0x86, v); }

inline uint8_t PhysObjGetControl(const PhysObj* obj) { return PhysObjGetU8(obj, 0x87); }
inline void    PhysObjSetControl(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0x87, v); }

inline uint8_t PhysObjGetFlags(const PhysObj* obj) { return PhysObjGetU8(obj, 0x14c); }
inline void    PhysObjSetFlags(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0x14c, v); }
inline void    PhysObjOrFlags(PhysObj* obj, uint8_t v) { PhysObjSetFlags(obj, static_cast<uint8_t>(PhysObjGetFlags(obj) | v)); }

// +0xfe dock-obj index; +0xff AI mode (0x24 = docked/landed).
inline uint8_t PhysObjGetDockObj(const PhysObj* obj) { return PhysObjGetU8(obj, 0xfe); }
inline void    PhysObjSetDockObj(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0xfe, v); }

inline uint8_t PhysObjGetAiMode(const PhysObj* obj) { return PhysObjGetU8(obj, 0xff); }
inline void    PhysObjSetAiMode(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0xff, v); }

inline int32_t PhysObjGetSeed(const PhysObj* obj) { return PhysObjGetI32(obj, 0xa0); }
inline void    PhysObjSetSeed(PhysObj* obj, int32_t v) { PhysObjSetI32(obj, 0xa0, v); }

inline int32_t PhysObjGetMass(const PhysObj* obj) { return PhysObjGetI32(obj, 0xa4); }
inline void    PhysObjSetMass(PhysObj* obj, int32_t v) { PhysObjSetI32(obj, 0xa4, v); }

inline int32_t PhysObjGetOrbRadius(const PhysObj* obj) { return PhysObjGetI32(obj, 0xb0); }
inline void    PhysObjSetOrbRadius(PhysObj* obj, int32_t v) { PhysObjSetI32(obj, 0xb0, v); }

inline uint8_t PhysObjGetRotSpeed(const PhysObj* obj) { return PhysObjGetU8(obj, 0xc8); }
inline void    PhysObjSetRotSpeed(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0xc8, v); }

inline uint8_t PhysObjGetTreeLevel(const PhysObj* obj) { return PhysObjGetU8(obj, 0xcb); }
inline void    PhysObjSetTreeLevel(PhysObj* obj, uint8_t v) { PhysObjSetU8(obj, 0xcb, v); }

inline uint16_t PhysObjGetOrientation(const PhysObj* obj) { return PhysObjGetU16(obj, 0xcc); }
inline void     PhysObjSetOrientation(PhysObj* obj, uint16_t v) { PhysObjSetU16(obj, 0xcc, v); }

inline uint16_t PhysObjGetTemp(const PhysObj* obj) { return PhysObjGetU16(obj, 0xe0); }
inline void     PhysObjSetTemp(PhysObj* obj, uint16_t v) { PhysObjSetU16(obj, 0xe0, v); }

// +0x124 … +0x137: 20-byte name (matches SysObj.name).
inline const char* PhysObjGetName(const PhysObj* obj)
{
    return obj ? reinterpret_cast<const char*>(obj->raw + 0x124) : "";
}

inline void PhysObjSetName(PhysObj* obj, const char* name)
{
    if (!obj) return;
    std::memset(obj->raw + 0x124, 0, 20);
    if (!name) return;
    int i = 0;
    for (; i < 19 && name[i]; ++i) {
        obj->raw[0x124 + i] = static_cast<uint8_t>(name[i]);
    }
    obj->raw[0x124 + i] = 0;
}

inline const PhysObj* PhysObjFindByName(const PhysObj* table, int cap, const char* name)
{
    if (!table || !name || cap <= 0) return nullptr;
    for (int i = 0; i < cap; ++i) {
        if (std::strcmp(PhysObjGetName(&table[i]), name) == 0) return &table[i];
    }
    return nullptr;
}

} // namespace jjffe

#endif // JJFFE_PHYS_OBJ_H
