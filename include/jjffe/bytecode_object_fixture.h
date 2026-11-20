/*
 * Object-setup harness fixture — raw PhysObj bytes at ASM offsets.
 *
 * NOT GameObj scaffolding (game_state.h named fields sit at offset 0 and do
 * not match ASM). Lookup: FUNC_001532(i, base) = i*0x152 + base + 0x74.
 *
 * Arena covers slots 0..0x72 so FUNC_000928 (scan 0x72→1) can allocate.
 * First kObjectCount slots are planted; remaining flag bytes stay 0 so Pass 1
 * claims slot 0x72. Digest hashes the full arena (allocated object included).
 *
 * Shared by ffeasm --bytecode-trace and C++ golden / replay.
 */
#ifndef JJFFE_BYTECODE_OBJECT_FIXTURE_H
#define JJFFE_BYTECODE_OBJECT_FIXTURE_H

#include "jjffe/bytecode_digest.h"
#include "jjffe/phys_obj.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace jjffe {
namespace bytecode_object_fixture {

inline constexpr size_t kObjectHeaderSize = 0x74; /* FUNC_001532 +0x74 */
inline constexpr size_t kSlotIndexMax = 0x72;     /* FUNC_000928 scan top */
inline constexpr size_t kSlotCount = kSlotIndexMax + 1; /* 0..0x72 inclusive */
inline constexpr size_t kObjectCount = 4;         /* planted seed objects */
inline constexpr size_t kFixtureBytes =
    kObjectHeaderSize + kSlotCount * jjffe::kPhysObjSize;

/* ASM PhysObj field offsets used by the fixture (scalars, not pointers). */
inline constexpr size_t kOffPosX = 0x08;     /* planted i32 (matrix _13 in full layout) */
inline constexpr size_t kOffShipId = 0x56;   /* parent / ship id byte */
inline constexpr size_t kOffOwnerId = 0x86;  /* object index / owner byte */
inline constexpr size_t kOffTargetId = 0x88; /* target-related byte (exp-dist in full layout) */
inline constexpr size_t kOffHull = 0xb1;     /* hull byte (pitch hi in ship layout) */
inline constexpr size_t kOffFlags = 0x14c;

/*
 * Dword slots scrubbed → null/non-null before hashing.
 * Real PhysObj target/owner links are usually *byte indices* (+0xfe / +0x86);
 * when an opcode stores an object* / list head it lands in a dword — scrub those.
 * Scaffolding plants process-local pointers here to prove address-independence.
 */
inline constexpr size_t kScrubOwnerLink = 0x11a;  /* harness "owner" object* slot */
inline constexpr size_t kScrubTargetLink = 0x11e; /* harness "target" object* slot */

inline uint8_t* ObjectAt(uint8_t* base, size_t index)
{
    return base + kObjectHeaderSize + index * jjffe::kPhysObjSize;
}

inline const uint8_t* ObjectAt(const uint8_t* base, size_t index)
{
    return base + kObjectHeaderSize + index * jjffe::kPhysObjSize;
}

inline void PutU8(uint8_t* obj, size_t off, uint8_t v) { obj[off] = v; }

inline void PutI32(uint8_t* obj, size_t off, int32_t v)
{
    std::memcpy(obj + off, &v, 4);
}

inline void PutU32(uint8_t* obj, size_t off, uint32_t v)
{
    std::memcpy(obj + off, &v, 4);
}

/* Zero-fill full arena, plant first kObjectCount; leave slots 4..0x72 free. */
inline void FillObjectFixture(uint8_t* base)
{
    std::memset(base, 0, kFixtureBytes);
    for (size_t i = 0; i < kObjectCount; ++i)
        base[i] = 1; /* occupancy / flag byte in header */

    static const uint8_t kShipId[kObjectCount] = {0x10, 0x20, 0x30, 0x40};
    static const uint8_t kHull[kObjectCount] = {0x64, 0x32, 0x48, 0x16};
    static const uint8_t kFlags[kObjectCount] = {0x08, 0x10, 0x20, 0x40};
    static const uint8_t kTarget[kObjectCount] = {0x01, 0x00, 0x02, 0x03};
    static const int32_t kPosX[kObjectCount] = {
        0x00010000, 0x00020000, 0x00030000, -0x00010000};

    for (size_t i = 0; i < kObjectCount; ++i) {
        uint8_t* o = ObjectAt(base, i);
        PutU8(o, kOffShipId, kShipId[i]);
        PutU8(o, kOffOwnerId, static_cast<uint8_t>(i));
        PutU8(o, kOffTargetId, kTarget[i]);
        PutU8(o, kOffHull, kHull[i]);
        PutU8(o, kOffFlags, kFlags[i]);
        PutI32(o, kOffPosX, kPosX[i]);
    }
}

/*
 * Plant process-local pointers into scrubbed dword slots so ASM vs C++
 * addresses differ but digests still match after ScrubObjectList.
 * `marker` must remain live until HashObjectList returns.
 */
inline void PlantPointerScrubProof(uint8_t* base, void* marker)
{
    const uint32_t bits =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(marker));
    for (size_t i = 0; i < kObjectCount; ++i) {
        uint8_t* o = ObjectAt(base, i);
        PutU32(o, kScrubOwnerLink, bits ^ static_cast<uint32_t>(i * 0x01010101u));
        PutU32(o, kScrubTargetLink, bits ^ static_cast<uint32_t>(0xA5A5A5A5u + i));
    }
}

inline void ScrubObjectList(uint8_t* dst, const uint8_t* src)
{
    std::memcpy(dst, src, kFixtureBytes);
    /* Scrub every slot — allocators claim high indices (e.g. 0x72); leaving
     * those owner/target dwords raw made ASM vs C++ digests diverge on
     * process pointers alone (seen on ops 0x32/0x33/0x34). */
    for (size_t i = 0; i < kSlotCount; ++i) {
        uint8_t* o = ObjectAt(dst, i);
        for (size_t off : {kScrubOwnerLink, kScrubTargetLink}) {
            uint32_t v = 0;
            std::memcpy(&v, o + off, 4);
            uint32_t sur = v ? 1u : 0u;
            std::memcpy(o + off, &sur, 4);
        }
    }
}

inline uint32_t HashObjectList(const uint8_t* base)
{
    /* Heap — full arena is ~39KB; avoid large stack frame in fork children. */
    std::vector<uint8_t> scrubbed(kFixtureBytes);
    ScrubObjectList(scrubbed.data(), base);
    return bytecode_digest::Fnv1a32(scrubbed.data(), kFixtureBytes);
}

} // namespace bytecode_object_fixture
} // namespace jjffe

#endif
