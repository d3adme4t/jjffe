#ifndef JJFFE_PLAYER_PAD_H
#define JJFFE_PLAYER_PAD_H

// Task 4.20 — the landed player on the starport pad.
//
// LP64-safe port of the new-game player placement (ffe.asm):
//   FUNC_000998 default start (JUMP_004144, DATA_008881 not 1..3):
//     FUNC_000996(model 0x1f, seed 0xde27) → player slot via FUNC_000928,
//     +0x56 = 0x6c (Hope), FUNC_000994(player, 0x6a = Old Blackelk),
//     FUNC_000997 (turn the ship 180° about its up axis).
//   FUNC_000994: starport matrix + `+0x3e` → player, FUNC_000650 pad offset,
//     then up-row × player `+0x140` (the ship's pad clearance).
//   FUNC_001677: global position `+0x26` (parent-frame rotation for `+0x57`).

#include "jjffe/phys_obj.h"
#include "jjffe/sysgen_proc.h"

#include <cstdint>

namespace jjffe {

// Immediates of FUNC_000998 JUMP_004144 (ffe.asm 74412-74447).
constexpr int32_t  kStartPlayerModel      = 0x1f;
constexpr uint32_t kStartPlayerSeed       = 0xde27;
constexpr uint8_t  kStartPlayerParentSlot = 0x6c;
constexpr int      kStartStarportSlot     = 0x6a;

struct PlayerPadResult {
    int     slot = -1;          // FUNC_000928 slot (DATA_008857)
    int     starportSlot = -1;  // FUNC_000994 arg 2 (DATA_008804 + 0xc5)
    int     pad = -1;           // FUNC_000650 pad index (DATA_008804 + 0xf4)
    int64_t padOffset[3] = {};  // FUNC_000650 offset, parent frame, FFE units
};

// FUNC_000650: claim a pad on `starport` for ship slot `shipIndex` and return
// its offset. `playerIndex` is DATA_008857. Returns 0, or -1 when no pad is
// free (the ASM's "launch instead" path).
int PlayerPadClaim(PhysObj* starport, int shipIndex, int playerIndex, int64_t offset[3],
                   int* padOut);

// FUNC_000998 default start into a table built by SysGenBuildSpawnTable.
// Sets the fields FUNC_000996/994/997 write on the way (not the FUNC_000791
// type-0x12 name, and not ai +0xff, which a later message handler sets).
bool PlayerPadSpawnDefault(SpawnSlotTable* table, PlayerPadResult* out);

// FUNC_001677 (uncached): global position of `slot` through its parents.
void PhysObjGlobalPos(const SpawnSlotTable& table, int slot, int64_t out[3]);

}  // namespace jjffe

#endif  // JJFFE_PLAYER_PAD_H
