#pragma once
// FUNC_000574 terrain-height query (ffe.asm 36511-39135): FUNC_000574 →
// FUNC_000575 → FUNC_000576 → FUNC_000579 → FUNC_000582 (parent model +0x34
// collision program) → FUNC_000581 (terrain) → FUNC_001876 collision mode.
// LP64-safe port operating on PhysObj raw buffers (core.cpp keeps the 32-bit
// arena versions).
#include "jjffe/phys_obj.h"

#include <cstdint>

namespace jjffe {

// Branches of the query that are not ported. Reaching one records its bit in
// TerrainQueryUnported() and the query continues as documented per bit.
enum TerrainQueryUnportedBit : uint32_t {
    kTqSphere577 = 1u << 0,    // FUNC_000575: parent +0x14c >= 0 → FUNC_000577 (returns 0)
    kTqBounce578 = 1u << 1,    // FUNC_000575: FUNC_000576 == 0, DATA_008707 == 0 → FUNC_000578 (skipped)
    kTqResponse2410 = 1u << 2, // FUNC_000575 JUMP_002410 contact response (skipped)
    kTqSubObject580 = 1u << 3, // retired: FUNC_000580 (FUNC_000582 type 0x80) is ported
    kTqNoNormals583 = 1u << 4, // FUNC_000583 without a normal table, or index 0 (treated as inside)
    kTqNo876 = 1u << 5,        // FUNC_001876 tables not initialised (terrain command skipped)
};

// FUNC_000574(out, pos, obj, parent): terrain distance of `obj` at the
// parent-relative position `pos` (3×int64, mutated in place like the ASM's
// FUNC_001664_Vec64Abs), or -1 when there is no terrain hit.
int32_t FUNC_000574_TerrainHeight(int64_t pos[3], PhysObj* obj, PhysObj* parent);

// DATA_008696..DATA_008707 for FUNC_000574_TerrainHeight and TerrainQueryFunc576:
// copied in before the walk and back after it.
struct TerrainQueryState {
    uint8_t flags696;
    uint8_t flags697;
    uint8_t hitIndex698;
    uint8_t d700;
    uint8_t hitCmd701;
    PhysObj* hitObj702;
    uint32_t alt703;
    uint32_t alt704;
    uint32_t alt705;
    uint32_t alt706;
    uint8_t mode707;
};

// FUNC_000576(obj, parent, base) (ffe.asm 37229-37404) on 0x152-byte objects.
int TerrainQueryFunc576(PhysObj* obj, PhysObj* parent, const int64_t base[3], TerrainQueryState* io);

// core.cpp's DATA_008696..DATA_008707 (the labels the replay binds), read into
// and written from a TerrainQueryState.
void TerrainQueryLoadLabels(TerrainQueryState* io);
void TerrainQueryStoreLabels(const TerrainQueryState& io);

uint32_t TerrainQueryUnported();
void TerrainQueryResetUnported();

} // namespace jjffe
