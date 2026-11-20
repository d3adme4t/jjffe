#ifndef JJFFE_SYSGEN_H
#define JJFFE_SYSGEN_H

#include "jjffe/phys_obj.h"

#include <cstdint>
#include <cstddef>

#ifdef __GNUC__
#define SYSGEN_PACKED __attribute__((packed))
#else
#define SYSGEN_PACKED
#endif

namespace jjffe {

// F875 writes a terminator AT +0x26c8; 0x26c8 / 0x44 = 146 is the terminator's *slot index*,
// so the table must hold 147 entries (0..146). With 146 the terminator landed 4 bytes past the
// array - a global-buffer-overflow ASan caught (core.cpp:12476, `End(146)`), which silently
// clobbered whatever global followed in .bss (in one Release layout: g_videoState, crashing
// LoadGamePalette during boot). Found in 4.166 while landing the 892 retarget.
constexpr int kSysObjHandcodedCapacity = 147;
// F881 bails at objcount >= 0x3c (60); keep a little headroom.
constexpr int kSysObjGeneratedCapacity = 64;
constexpr size_t kSysObjSize = 0x44;

// csysgen.txt system object types (word at SysObj +0x0a).
enum class SysObjType : uint16_t {
    kBinarySystem     = 0x00,  // co-orbital point
    kAsteroidalBody   = 0x01,
    kSmallBarrenRock  = 0x02,  // small barren sphere of rock
    kBarrenPlanetoid  = 0x03,  // barren rocky planetoid
    kRockyThinAtmo    = 0x04,  // rocky planet with a thin atmosphere
    kVolcanicWorld    = 0x05,
    kAmmoniaWeather   = 0x06,
    kMethaneWeather   = 0x07,
    kWaterWeather     = 0x08,
    kTerraformedSmall = 0x09,
    kIndigenousLife   = 0x0a,
    kTerraformedIntro = 0x0b,
    kRockyThickAtmo   = 0x0c,
    kSmallGasGiant    = 0x0d,
    kMediumGasGiant   = 0x0e,
    kLargeGasGiant    = 0x0f,
    kVeryLargeGiant   = 0x10,
    kBrownDwarf       = 0x11,
    kTypeMFlare       = 0x12,
    kTypeMFaintRed    = 0x13,
    kTypeMRed         = 0x14,
    kTypeKOrange      = 0x15,
    kTypeGYellow      = 0x16,
    kTypeFWhite       = 0x17,
    kTypeAHotWhite    = 0x18,
    kWhiteDwarf       = 0x19,
    kRedGiant         = 0x1a,
    kBrightGiant      = 0x1b,
    kTypeBHotBlue     = 0x1c,
    kSupergiant       = 0x1d,
    kBlueSupergiant   = 0x1e,
    kContactBinary    = 0x1f,
    kTradingPost      = 0x20,  // orbital trading post
    kOrbitalStation   = 0x21,
    kOrbitalCity      = 0x22,
    kStarport         = 0x23,
};

// 68-byte SysObj / HandCodedObject (csysgen.txt). Packed because orbRadius sits at +0x0e.
struct SYSGEN_PACKED SysObj {
    int32_t  mass;           // +0x00 earth masses (0 = terminator)
    uint32_t randseed;       // +0x04
    uint16_t parentIndex;    // +0x08
    uint16_t type;           // +0x0a SysObjType
    uint16_t modelIndex;     // +0x0c
    int32_t  orbRadius;      // +0x0e
    int16_t  latitude;       // +0x12
    int32_t  orbVel;         // +0x14
    uint32_t tempPtr;        // +0x18 PhysObj* after materialize (32-bit record)
    uint32_t lightOutput;    // +0x1c
    uint16_t unk20;          // +0x20
    int16_t  longitude;      // +0x22
    uint16_t temperature;    // +0x24 kelvin
    char     name[20];       // +0x26
    uint16_t unk3a;          // +0x3a
    uint16_t unk3c;          // +0x3c
    uint8_t  rotSpeed;       // +0x3e
    uint8_t  treeLevel;      // +0x3f
    uint16_t unk40;          // +0x40
    int16_t  orientation;    // +0x42 written then forced 0 in F873
};

static_assert(sizeof(SysObj) == kSysObjSize, "SysObj must be 68 bytes");
static_assert(offsetof(SysObj, mass) == 0x00, "mass +0x00");
static_assert(offsetof(SysObj, randseed) == 0x04, "randseed +0x04");
static_assert(offsetof(SysObj, parentIndex) == 0x08, "parent +0x08");
static_assert(offsetof(SysObj, type) == 0x0a, "type +0x0a");
static_assert(offsetof(SysObj, modelIndex) == 0x0c, "model +0x0c");
static_assert(offsetof(SysObj, orbRadius) == 0x0e, "orbradius +0x0e");
static_assert(offsetof(SysObj, latitude) == 0x12, "latitude +0x12");
static_assert(offsetof(SysObj, orbVel) == 0x14, "orbvel +0x14");
static_assert(offsetof(SysObj, tempPtr) == 0x18, "tempptr +0x18");
static_assert(offsetof(SysObj, lightOutput) == 0x1c, "light +0x1c");
static_assert(offsetof(SysObj, unk20) == 0x20, "unk +0x20");
static_assert(offsetof(SysObj, longitude) == 0x22, "longitude +0x22");
static_assert(offsetof(SysObj, temperature) == 0x24, "temp +0x24");
static_assert(offsetof(SysObj, name) == 0x26, "name +0x26");
static_assert(offsetof(SysObj, unk3a) == 0x3a, "unk +0x3a");
static_assert(offsetof(SysObj, unk3c) == 0x3c, "unk +0x3c");
static_assert(offsetof(SysObj, rotSpeed) == 0x3e, "rotspeed +0x3e");
static_assert(offsetof(SysObj, treeLevel) == 0x3f, "treelevel +0x3f");
static_assert(offsetof(SysObj, unk40) == 0x40, "unk +0x40");
static_assert(offsetof(SysObj, orientation) == 0x42, "orientation +0x42");

// Known SysGenParams prefix. Tail after name is undocumented.
struct SYSGEN_PACKED SysGenParams {
    uint32_t randseed1;    // +0x00 position-dependent seed (0 → handcoded path)
    uint32_t randseed2;    // +0x04 also total mass?
    uint32_t numstars;     // +0x08 from b[D8954] & 7
    uint32_t systemcode;   // +0x0c
    uint32_t extra;        // +0x10 from [D8962]
    char     name[20];     // +0x14
};

static_assert(offsetof(SysGenParams, randseed1) == 0x00, "randseed1 +0x00");
static_assert(offsetof(SysGenParams, randseed2) == 0x04, "randseed2 +0x04");
static_assert(offsetof(SysGenParams, numstars) == 0x08, "numstars +0x08");
static_assert(offsetof(SysGenParams, systemcode) == 0x0c, "systemcode +0x0c");
static_assert(offsetof(SysGenParams, extra) == 0x10, "extra +0x10");
static_assert(offsetof(SysGenParams, name) == 0x14, "params name +0x14");

void     SysObjInit(SysObj* obj);
void     SysObjSetName(SysObj* obj, const char* name);
const char* SysObjName(const SysObj* obj);
uint16_t SysObjTypeValue(const SysObj* obj);
uint16_t SysObjModel(const SysObj* obj);
uint16_t SysObjParent(const SysObj* obj);

SysObj*       SysObjHandcodedTable();
SysObj*       SysObjGeneratedTable();
int           SysObjBufferCapacity();      // handcoded table: 147 (146 slots + F875 terminator)
int           SysObjGeneratedCapacity();

const SysObj* SysObjFindByName(const SysObj* table, int cap, const char* name);

void SysGenRequestHandcodedInit();
bool SysGenIsHandcodedDirty();
void SysGenClearHandcodedDirty();
void SysGenBeginHandcodedEmit(SysObj* dest, int cap);
void SysGenEndHandcodedEmit();
bool SysGenEmitActive();
SysObj* SysGenEmitDest();
int SysGenEmitCapacity();

int  SysGenGenerateHandcodedSol(SysObj* out, int cap); // count; terminator not counted
bool SysGenFind(const SysObj* table, int cap, const char* name, SysObj* out);

void SysGenSetMaterializeOutput(PhysObj* out, int cap);
PhysObj* SysGenMaterializeDest();
int  SysGenMaterializeCapacity();

int  SysGenMaterialize(const SysObj* sys, int sysCount, PhysObj* out, int outCap);

// seed==0 → Sol handcoded; nonzero is later procedural.
bool SysGenGenerateSystem(const SysGenParams& params, SysObj* out, int cap, int* outCount);
bool SysGenMaterializeSystem(const SysObj* sys, int count, void* physOut, int physCap);

} // namespace jjffe

#endif // JJFFE_SYSGEN_H
