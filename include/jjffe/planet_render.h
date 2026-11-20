#ifndef JJFFE_PLANET_RENDER_H
#define JJFFE_PLANET_RENDER_H

#include <cstdint>

namespace jjffe {

// ASM mesh interpreter jump table (DATA_007816) + render callback slots.
void InitMeshInterpreterTable();

// Icosa tables (DATA_007892/893/894), render modes (DATA_007843), mesh pools.
void InitPlanetMeshTables();
void InitPlanet877ColumnTable();
void InitPlanetDitherMixTables();
// bias837 = DATA_007837 from FUNC_001876 (distance bump when param_12==0).
void PlanetSetLodMsb(int32_t msbC, int32_t bias837 = -1);
void PlanetBindShadeCallbacks(intptr_t ctx, int lodMode, void* shadeFn, uint8_t zoom);
// Reset vertex-cache arena (DATA_007823) before a standalone planet draw.
void PlanetResetDrawState();
void FUNC_001816(void);
void FUNC_001817(void);

// ASM FUNC_001877 — planet shade scale setup (DATA_009263/267) before icosa faces.
void FUNC_001877(intptr_t ctx, int* camOff, int* colTri, uint32_t meshWord0, int param_5,
                 int param_6);

// Models 117–148 and scaleRelated≥16 use opcode 0x1F (FUNC_001876 satellite chain).
bool IsPlanetBodyModel(int modelIndex);
bool IsPlanetBodyScale(uint32_t scaleRelated);

// Match ffeasm / FUNC_001683 scale-14 parking for int32-safe 0x1F verts.
int PlanetBodyFitDistUnits(uint32_t radius, uint32_t scaleRelated);

// Terrain sphere radius of a 0x1F body in scale-14 units (WORL cam space):
// the longest FUNC_001874 column vector (operand vertex − origin vertex),
// which is the radius FUNC_001876 builds the icosa at. ShipModel +0x1C is
// only the FUNC_001686 bounding radius (Hope: 90 vs 99 model units).
// Falls back to radius<<14 when the mesh does not open with a plain 0x1F.
int32_t PlanetSurfaceRadiusUnits(int modelIndex);

// Fill a 0x152 paint GameObj for FUNC_001682 (model, pose, distance).
// `obj` must remain live until the enclosing FUNC_001789 flush completes.
// Demo/intro: yaw/pitch look-at with pos (0,0,dist) — parks on optical axis.
void SetupPlanetPaintGameObj(uint8_t* obj, int modelIndex, int32_t yaw, int32_t pitch,
                             int32_t dist);

// Flight/world: camera-space Vec64 at +0x26/+0x2e/+0x36, optional 16.16 camera
// basis (columns = right/up/fwd). Stored as Q31 R — SpecialVecMatMul does R^T*v
// so the body stays world-aligned (pitch about right, not a vertical spin).
// `seed` is the body's PhysObj +0xa0: the ASM hands FUNC_001683 the PhysObj
// itself, and FUNC_001876 / 001828 / 001769 read [ctx+0x14c]+0xa0 as the
// terrain seed.
void SetupPlanetPaintGameObjAt(uint8_t* obj, int modelIndex, int32_t camX, int32_t camY,
                               int32_t camZ, int32_t seed, const int32_t* camRot16 = nullptr);

// The paint GameObj FUNC_001683 treats as a flight (WORL) body: real camera
// XYZ, no demo fit-Z park, FUNC_001680 zoom. Set only around its FUNC_001682.
// Only the A/B parked path (JJFFE_PLANET_PARKED=1) goes through it.
void SetWorldPlacePaintObj(const void* obj);

// The object FUNC_001876 reads the terrain seed (+0xa0) from. FUNC_001683 sets
// it to the body it paints and never clears it.
void SetRenderGameObj(const void* obj);

// FUNC_001680's view split (core.cpp, ffe.asm 102162-102301). `pos` is the
// camera-space position in body space, `scale` goes to ctx+0x148, `zoom` to
// ctx+0x130, `lod` to obj+0x88. MissionView1680 returns false past the model's
// far LOD; MissionDraw1680 is the rest of FUNC_001680 (cull, star-size disc or
// FUNC_001773).
struct Mission1680View {
    int32_t pos[3];
    int32_t scale;
    int32_t zoom;
    int lod;
};
bool MissionView1680(uint8_t* obj, const uint8_t* ship, const uint8_t* cam, const int64_t vec[3],
                     Mission1680View* view);
void MissionDraw1680(uint8_t* obj, const uint8_t* ship, const uint8_t* cam,
                     const Mission1680View& view);

// Reset planet draw state and FUNC_001682 into the *current* job frame
// (caller owns FUNC_001786 / FUNC_001789). `obj` must outlive the flush.
void EnqueuePlanetInterpreter(uint8_t* obj, int modelIndex, int32_t yaw, int32_t pitch,
                              int32_t dist);

// Flight: enqueue at camera-space position (world-aligned body). Same job-frame rules.
void EnqueuePlanetInterpreterAt(uint8_t* obj, int modelIndex, int32_t camX, int32_t camY,
                                int32_t camZ, int32_t seed, const int32_t* camRot16 = nullptr);

// Flight: the 0x1F body through FUNC_001680 into the current job frame. `vec`
// is body − camera in FFE units (FUNC_001678), `cam` the FUNC_001680 camera
// object, `zoomBias` the body's +0x7e. When `phys` is set, the paint object's
// +0x00 matrix is the body's day-spin matrix in Hope's frame
// (WorldMatrixInAnchorFrame) — same rule as MissionRenderObject1680 for
// installations/stars. Without it the matrix stays Q31 identity (MatBuildYZT
// 0,0), which freezes vertex-cache obj0 across Hope's spin (R1202). Returns
// false when nothing is enqueued: past the far LOD, or the port-only culls
// (the whole bounding sphere behind the camera, or the camera deep inside it).
struct PhysObj;
bool EnqueuePlanetInterpreter1680(uint8_t* obj, int modelIndex, const uint8_t* cam,
                                  const int64_t vec[3], int32_t seed, int32_t zoomBias,
                                  const PhysObj* phys = nullptr);

// Draw via FUNC_001786 → EnqueuePlanetInterpreter → FUNC_001789.
void DrawPlanetInterpreter(int modelIndex, int32_t yaw, int32_t pitch, int32_t dist);

} // namespace jjffe

#endif
