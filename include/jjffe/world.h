#ifndef JJFFE_WORLD_H
#define JJFFE_WORLD_H

#include "jjffe/sysgen.h"

namespace jjffe {

// World list: 53 Sol bodies + the generated spawn system (<= 64 records).
constexpr int kWorldBodyCapacity = 128;

// Session-owned Sol SysObj/PhysObj lists. Separate from Phase 60 GameObj ships.
// `startIndex` is FUNC_000147's parameter: the spawn system is its DATA_006820
// record's system (SysGenBuildSpawnTable).
bool WorldLoadSol(int startIndex = 0);
void WorldShutdown();
int  WorldBodyCount();
// First index of the Gateway spawn system (the generated cold-boot table,
// task 4.18). Sol is [0, this). Flight view draws only Gateway — see
// docs/FLIGHT_SCENE_GAP.md (tasks 4.5 / 4.18).
int  WorldGatewayIndex();
// PhysObj index of the Gateway system star (model 139). Equals WorldGatewayIndex()
// under dense packing; under AB_SLOT_ORDER_FIX it is the ASM pool slot (114) while
// WorldGatewayIndex() is the lowest occupied spawn-body slot (contiguous walk).
int  WorldGatewayStarIndex();
// Gateway render space = FFE >> this, about the spawn planet (Hope).
int  WorldRenderShift();
// Gateway body relative to the spawn planet in FFE units, in the planet's rotating frame (R1140;
// surface starports on the parent's terrain sphere). False for Sol bodies.
bool WorldGetAnchorRel(int index, int64_t out[3]);
const SysObj*  WorldSysObjs();
const PhysObj* WorldPhysObjs();
// Scaled int32 16.16 render positions (not raw FFE Vec64).
void WorldGetPos(int index, int32_t* x, int32_t* y, int32_t* z);
// FFE-scale global Vec64 (PhysObj+0x26). Not render space.
bool WorldGetFfeGlobal(int index, int64_t out[3]);
// Running totals of WorldGetPos (compressed render space) and
// WorldGetAnchorRel (FFE) calls. DrawMission logs the difference across its
// body draw passes; tools/units_audit_ctest.sh asserts which space they read.
uint64_t WorldRenderPosReads();
uint64_t WorldAnchorRelReads();
// Advance orbital angles by `steps` mission frames (FUNC_001510 + FUNC_001507).
// AB_WORLD_ORBIT_DELTA=1 switches the step from a constant 0x1000 to the live
// clock (R1 law: angle = 8*angVel*time; docs/PHASE_64_WORLDGEN_CHARTER.md).
void WorldAdvanceOrbits(int steps);
// Advance one orbital body to an explicit time-of-day / date pair. The ASM
// stamps +0xa8/+0xac with the clock and adds 8*angVel*delta_time to +0xba
// (measured: scratch/worldgen/findings_r1.md). Inert for starports and bodies
// with +0xc2 == 0.
void WorldOrbitAdvanceBody(PhysObj* p, uint32_t timeLo, uint32_t timeHi);
// The same, over the whole world table.
void WorldAdvanceOrbitsTo(uint32_t timeLo, uint32_t timeHi);
// Task 4.20: the FUNC_000998 default-start player on the Old Blackelk pad
// (include/jjffe/player_pad.h). Its parent is the spawn planet, so +0x3e is
// the offset from the render anchor in FFE units and +0x00 is its Q31 matrix
// in the planet frame. Null if the placement failed.
const PhysObj* WorldSpawnPlayer();
// Task 4.27: the new-game traffic ships (ASM slots 70..86, FUNC_000688 via
// include/jjffe/traffic_spawn.h), in slot order, parents remapped to world
// indices (0 = system frame). Kept apart from the body list, which the
// system-generation gates count. Ships do not move (traffic AI is not ported).
int WorldShipCount();
const PhysObj* WorldShipObjs();
// Ship relative to the spawn planet in FFE units (parent chain via +0x3e).
bool WorldGetShipAnchorRel(int ship, int64_t out[3]);
// R1140: a body's drawing matrix in the anchor's (Hope's) rotating frame - the camera's frame.
// Unchanged for bodies already in it (+0x57 children of Hope); otherwise its global matrix * Hope^T.
void WorldMatrixInAnchorFrame(const PhysObj* p, int32_t out[9]);
const PhysObj* WorldFind(const char* name);
const char* WorldNearestName(int32_t px, int32_t py, int32_t pz);

} // namespace jjffe

#endif // JJFFE_WORLD_H
