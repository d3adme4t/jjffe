#include "jjffe/player_spawn.h"

#include "jjffe/world.h"

#include <cstring>

namespace jjffe {

static PhysObj s_spawnPlayer = {};
static bool    s_spawnReady  = false;

bool SpawnPlayerLanded()
{
    s_spawnReady = false;
    PhysObjInit(&s_spawnPlayer);

    if (WorldBodyCount() <= 0) {
        if (!WorldLoadSol()) return false;
    }

    const PhysObj* hope = WorldFind("Hope");
    if (!hope) return false;

    PhysObjSetName(&s_spawnPlayer, "TY-198");
    PhysObjSetModel(&s_spawnPlayer, 0x1f);
    PhysObjSetControl(&s_spawnPlayer,
                      static_cast<uint8_t>(PhysObjControl::kPlayer));
    PhysObjSetFlags(&s_spawnPlayer, kPhysFlagComplex);
    PhysObjSetAiMode(&s_spawnPlayer, 0x24);
    PhysObjSetParent(&s_spawnPlayer, PhysObjGetIndex(hope));

    s_spawnReady = true;
    return true;
}

const PhysObj* PlayerSpawnPhysObj()
{
    return s_spawnReady ? &s_spawnPlayer : nullptr;
}

const PhysObj* PlayerSpawnParentPhysObj()
{
    if (!s_spawnReady) return nullptr;
    const uint8_t parentIdx = PhysObjGetParent(&s_spawnPlayer);
    const PhysObj* table = WorldPhysObjs();
    const int n = WorldBodyCount();
    if (!table || parentIdx >= n) return nullptr;
    return &table[parentIdx];
}

bool PlayerSpawnGetSnapshot(PlayerSpawnSnapshot* out)
{
    if (!out || !s_spawnReady) return false;
    const PhysObj* parent = PlayerSpawnParentPhysObj();
    if (!parent) return false;

    std::memset(out, 0, sizeof(*out));
    std::strncpy(out->name, PhysObjGetName(&s_spawnPlayer), sizeof(out->name) - 1);
    out->model   = PhysObjGetModel(&s_spawnPlayer);
    out->control = PhysObjGetControl(&s_spawnPlayer);
    out->flags   = PhysObjGetFlags(&s_spawnPlayer);
    out->aiMode  = PhysObjGetAiMode(&s_spawnPlayer);

    std::strncpy(out->parentName, PhysObjGetName(parent),
                 sizeof(out->parentName) - 1);
    out->parentModel   = PhysObjGetModel(parent);
    out->parentControl = PhysObjGetControl(parent);
    out->parentFlags   = PhysObjGetFlags(parent);
    return true;
}

} // namespace jjffe
