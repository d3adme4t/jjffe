#ifndef JJFFE_PLAYER_SPAWN_H
#define JJFFE_PLAYER_SPAWN_H

#include "jjffe/phys_obj.h"

#include <cstdint>

namespace jjffe {

// ASM-faithful landed spawn (ffeasm postintro/spawn): TY-198 at Hope.
struct PlayerSpawnSnapshot {
    char     name[20];
    int32_t  model;
    uint8_t  control;
    uint8_t  flags;
    uint8_t  aiMode;
    char     parentName[20];
    int32_t  parentModel;
    uint8_t  parentControl;
    uint8_t  parentFlags;
};

// Requires Hope in the world (WorldLoadSol). Fills the spawn PhysObj and
// points parent (+0x56) at Hope. Returns false if Hope is missing.
bool SpawnPlayerLanded();

const PhysObj* PlayerSpawnPhysObj();
const PhysObj* PlayerSpawnParentPhysObj();
bool PlayerSpawnGetSnapshot(PlayerSpawnSnapshot* out);

} // namespace jjffe

#endif // JJFFE_PLAYER_SPAWN_H
