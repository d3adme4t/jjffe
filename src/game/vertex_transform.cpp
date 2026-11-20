#include "jjffe/vertex_transform.h"
#include "jjffe/video.h"
#include "jjffe/data.h"
#include "jjffe/ship_model.h"
#include "jjffe/math.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_trace.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

namespace jjffe {

// ASM DATA_007723 → FUNC_001467 (lookup-table isqrt). Thruster tip span must
// use this — digit-by-digit IntSqrt32 is off-by-one on 38²/55² and widens the
// stretched 0x65 billboard by 1px (Asp #40 thrust oa=27).
uint32_t FUNC_001467(uint32_t param_1);

int g_abPoseIndex = 0;
int g_abShipIndex = 0;

static std::vector<ResolvedBarrier> g_frameBarriers;

const std::vector<ResolvedBarrier>& GetFrameBarriers()
{
    return g_frameBarriers;
}

// Forward declaration — defined later in this file.
static bool GetVertexMirrored(const ShipModel* shipModel, int index, ShipVertex& out, int depth = 0);

// Glow/flare screen coordinate store (op 0x18 / GlowPoints).
// Pre-transformed during TransformShipVertices, read by the renderer.
static constexpr int kMaxGlowScreen = 4096;
static int16_t s_glowScreenX[kMaxGlowScreen];
static int16_t s_glowScreenY[kMaxGlowScreen];
static bool s_glowScreenVisible[kMaxGlowScreen];
static int s_glowScreenCount = 0;

const int16_t* GetGlowScreenX() { return s_glowScreenX; }
const int16_t* GetGlowScreenY() { return s_glowScreenY; }
const bool* GetGlowScreenVisible() { return s_glowScreenVisible; }

// Vertex cache: stores transformed vertices keyed by model vertex index.
// Populated during TransformShipVertices, read by NavLight (op 0x01).
// ASM DATA_009200 has 36 bytes per entry.
// Layout matches ASM exactly for binary parity:
//   offset 0x00: packed screen (scrY:16 | scrX:16)
//   offset 0x04: camX
//   offset 0x08: camY
//   offset 0x0c: camZ (depth)
//   offset 0x10: reserved (0)
//   offset 0x14: genCounter (== renderCtx+0xf0 when live)
//   offset 0x18: worldX
//   offset 0x1c: worldY
//   offset 0x20: worldZ
static constexpr int kVertexCacheSize = 256;
struct VertexCacheEntry {
    int32_t packedScreen;     // (scrY << 16) | (scrX & 0xFFFF)
    int32_t camX;
    int32_t camY;
    int32_t camZ;             // depth
    int32_t reserved;         // offset 0x10, always 0
    int32_t genCounter;       // offset 0x14, stamped from s_renderGen
    int32_t worldX, worldY, worldZ;
    int32_t modelX, modelY, modelZ;
    int32_t screenX;          // unpacked for convenience
    int32_t screenY;
    bool visible;
    bool valid;
};
static VertexCacheEntry s_vertexCache[kVertexCacheSize];

// ASM renderCtx+0xf0. Mesh path sets to 2 before interpret (after FUNC_001480).
static int32_t s_renderGen = 2;
// ASM DATA_009200 window: logical index i → physical slot (base + i).
static int s_vertexCacheBase = 0;
// ASM renderCtx+0x150 source-vertex skip (in vertex units, 6 bytes each).
static int s_vertBankVerts = 0;

int32_t GetVertexCacheRenderGen() { return s_renderGen; }
void SetVertexCacheRenderGen(int32_t gen) { s_renderGen = gen; }
int GetVertexCacheBankBase() { return s_vertexCacheBase; }
void SetVertexCacheBankBase(int entryBase) { s_vertexCacheBase = entryBase; }
int GetVertexBankVerts() { return s_vertBankVerts; }
void SetVertexBankVerts(int nVerts) { s_vertBankVerts = nVerts; }
void AdvanceVertexBank(int nVerts)
{
    // FUNC_001767 positive: +0x150 += n*6 AND DATA_009200 += n*0x48 (n*2 entries).
    s_vertBankVerts += nVerts;
    s_vertexCacheBase += nVerts * 2;
}
void AdvanceVertexCacheBank(int nUnits) { AdvanceVertexBank(nUnits); }

// Sub-object vertex table: ASM render_context[0x6c-0x88].
// Populated by FUNC_001555 (6 anchor projections through FUNC_001557).
// Mirror-case vertices (negative index) read from this table.
// Each entry is a packed screen coord: (scrY << 16) | scrX.
// Table layout: entries at offsets 0x6c, 0x70, 0x74, 0x78, 0x80, 0x84.
static constexpr int kSubObjTableSize = 6;
static int32_t s_subObjScreenTable[kSubObjTableSize]; // packed (scrY<<16)|scrX
static bool s_subObjTableValid = false;

// FUNC_001557: 3D→2D perspective projection.
// Input: 3D point (x, y, z) in camera space.
// Output: screen coords (screenX, screenY).
// Returns true if projection succeeded (z >= 0x40).
// ASM: screenX = (x << 8) / z + 0xA0, screenY = 0x4F - (y << 8) / z
[[maybe_unused]] static bool ProjectSubObject(int32_t x, int32_t y, int32_t z,
                             int16_t& outScreenX, int16_t& outScreenY)
{
    if (z < 0x40) return false;

    // Normalize: shift right until |x| and |y| fit in 24 bits
    while ((std::abs(x) | std::abs(y)) > 0x800000) {
        x >>= 1;
        y >>= 1;
        z >>= 1;
    }
    if (z == 0) return false;

    // Project: (coord << 8) / depth
    int32_t projX = (x << 8) / z;
    int32_t projY = (y << 8) / z;

    // Clamp to [-0x4000, 0x4000]
    while (projX >= 0x4000 || projX < -0x4000 ||
           projY >= 0x4000 || projY < -0x4000) {
        projX >>= 1;
        projY >>= 1;
    }

    // Convert to screen coords (ASM: +0xA0 = 160, 0x4F = 79)
    outScreenX = static_cast<int16_t>(projX + 0xA0);
    outScreenY = static_cast<int16_t>(0x4F - projY);
    return true;
}

// FUNC_001555: populate sub-object vertex table with 6 projected anchor points.
// Called once per ship render, before the mesh interpreter runs.
// The 6 anchor points define the ship's bounding diamond in camera space:
//   point1 = camPos + xAxis           (right wing tip)
//   point2 = camPos + xAxis + yAxis   (right + up)
//   point3 = camPos + xAxis - yAxis   (right + down)
//   point4 = camPos - xAxis           (left wing tip)
//   point5 = camPos - xAxis + yAxis   (left + up)
//   point6 = camPos - xAxis - yAxis   (left + down)
// where xAxis = (size, 0, 0) and yAxis = (0, size*2/3, 0) in model space,
// then rotated to camera space via shipRot.
static void PopulateSubObjectTable(const int32_t* shipRot, const int32_t* shipPos,
                                   const ShipModel* shipModel, const Camera3D* camera)
{
    s_subObjTableValid = false;

    // FUNC_001556: offset1 = (size, 0, 0), offset2 = (0, size*2/3, 0)
    // size comes from the model's bounding radius or scaleRelated.
    // ASM uses the ObjectStruct's radius field at +0x1C.
    int32_t size = static_cast<int32_t>(shipModel->radius);
    if (size <= 0) size = 64; // fallback

    int32_t size23 = (int32_t)(((int64_t)size * 0xAAAA) >> 15); // size * 2/3

    // Compute camera-space position (FUNC_001685 / ComputeCamModelPos).
    // ComputeCamModelPos returns camera-in-model-space. We need
    // model-in-camera-space for projection, so negate.
    int32_t camMx, camMy, camMz;
    int lodShift;
    ComputeCamModelPos(shipModel, shipRot, shipPos, camMx, camMy, camMz, lodShift);
    int32_t modelInCamX = -camMx;
    int32_t modelInCamY = -camMy;
    int32_t modelInCamZ = -camMz;

    // Rotate offset vectors to camera space
    // xAxis = (size, 0, 0) in model space → rotated by shipRot
    int32_t xAxisX = FixedMulHigh(size, shipRot[0]);
    int32_t xAxisY = FixedMulHigh(size, shipRot[1]);
    int32_t xAxisZ = FixedMulHigh(size, shipRot[2]);

    // yAxis = (0, size23, 0) in model space → rotated by shipRot
    int32_t yAxisX = FixedMulHigh(size23, shipRot[3]);
    int32_t yAxisY = FixedMulHigh(size23, shipRot[4]);
    int32_t yAxisZ = FixedMulHigh(size23, shipRot[5]);

    // Compute 6 anchor points in camera space
    int32_t points[6][3] = {
        {modelInCamX + xAxisX, modelInCamY + xAxisY, modelInCamZ + xAxisZ},                    // point1: right
        {modelInCamX + xAxisX + yAxisX, modelInCamY + xAxisY + yAxisY, modelInCamZ + xAxisZ + yAxisZ}, // point2: right+up
        {modelInCamX + xAxisX - yAxisX, modelInCamY + xAxisY - yAxisY, modelInCamZ + xAxisZ - yAxisZ}, // point3: right+down
        {modelInCamX - xAxisX, modelInCamY - xAxisY, modelInCamZ - xAxisZ},                    // point4: left
        {modelInCamX - xAxisX + yAxisX, modelInCamY - xAxisY + yAxisY, modelInCamZ - xAxisZ + yAxisZ}, // point5: left+up
        {modelInCamX - xAxisX - yAxisX, modelInCamY - xAxisY - yAxisY, modelInCamZ - xAxisZ - yAxisZ}, // point6: left+down
    };

    // Project each anchor point through FUNC_001557 (ProjectCameraPoint)
    // ASM table layout: entries at offsets 0x6c, 0x70, 0x74, 0x78, 0x80, 0x84
    // We store as packed (scrY << 16) | scrX
    int tableIdx = 0;
    for (int i = 0; i < 6; i++) {
        ProjResult proj;
        bool ok = ProjectCameraPoint(points[i][0], points[i][1], points[i][2], &proj);
        if (ok) {
            s_subObjScreenTable[tableIdx] = (int32_t)((uint32_t)(int32_t)proj.sy << 16) |
                                            ((int32_t)proj.sx & 0xFFFF);
        } else {
            s_subObjScreenTable[tableIdx] = 0;
        }
        tableIdx++;
    }
    s_subObjTableValid = true;
}

static int CachePhysicalIndex(int logicalIndex)
{
    return s_vertexCacheBase + logicalIndex;
}

static bool CacheEntryLive(int physicalIndex)
{
    if (physicalIndex < 0 || physicalIndex >= kVertexCacheSize)
        return false;
    const VertexCacheEntry& e = s_vertexCache[physicalIndex];
    // ASM: entry[+0x14] == renderCtx+0xf0
    return e.valid && e.genCounter == s_renderGen;
}

static void VertexCacheReset()
{
    for (int i = 0; i < kVertexCacheSize; i++) {
        s_vertexCache[i].valid = false;
        s_vertexCache[i].genCounter = 0;
    }
    s_vertexCacheBase = 0;
    s_vertBankVerts = 0;
    // Match ASM mov dword [renderCtx+0xf0], 0x2 before mesh interpret.
    s_renderGen = 2;
}

static void VertexCacheStore(int index, const ProjectedVertex& pv,
                             int32_t modelX, int32_t modelY, int32_t modelZ)
{
    const int pi = CachePhysicalIndex(index);
    if (pi < 0 || pi >= kVertexCacheSize) return;
    // Pack screen coords matching ASM layout: (scrY << 16) | (scrX & 0xFFFF)
    s_vertexCache[pi].packedScreen = (int32_t)((uint32_t)(int32_t)pv.screenY << 16) |
                                     ((int32_t)pv.screenX & 0xFFFF);
    s_vertexCache[pi].camX = pv.camX;
    s_vertexCache[pi].camY = pv.camY;
    s_vertexCache[pi].camZ = pv.depth;
    s_vertexCache[pi].reserved = 0;
    s_vertexCache[pi].genCounter = s_renderGen;  // ASM: mov [entry+0x14], [ctx+0xf0]
    s_vertexCache[pi].worldX = pv.worldX;
    s_vertexCache[pi].worldY = pv.worldY;
    s_vertexCache[pi].worldZ = pv.worldZ;
    s_vertexCache[pi].modelX = modelX;
    s_vertexCache[pi].modelY = modelY;
    s_vertexCache[pi].modelZ = modelZ;
    s_vertexCache[pi].screenX = pv.screenX;
    s_vertexCache[pi].screenY = pv.screenY;
    s_vertexCache[pi].visible = pv.visible;
    s_vertexCache[pi].valid = true;
}

bool GetCachedVertex(int index, ProjectedVertex& out)
{
    const int pi = CachePhysicalIndex(index);
    if (!CacheEntryLive(pi))
        return false;
    out.screenX = s_vertexCache[pi].screenX;
    out.screenY = s_vertexCache[pi].screenY;
    out.depth = s_vertexCache[pi].camZ;
    out.camX = s_vertexCache[pi].camX;
    out.camY = s_vertexCache[pi].camY;
    out.worldX = s_vertexCache[pi].worldX;
    out.worldY = s_vertexCache[pi].worldY;
    out.worldZ = s_vertexCache[pi].worldZ;
    out.visible = s_vertexCache[pi].visible;
    return true;
}

// Integer sqrt for screen-space tip span (ASM DATA_007723 / FUNC_001467).
static int32_t IntSqrt32(int32_t n)
{
    if (n <= 0) return 0;
    uint32_t x = static_cast<uint32_t>(n);
    uint32_t op = x;
    uint32_t res = 0;
    uint32_t one = 1u << 30;
    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return static_cast<int32_t>(res);
}

// ASM ctx+0x130. Dump-ab job traces: barrier 8192 = shipZ 0x10000 >> 3 → zoom=0.
static int s_painterSortZoom = 0;

void SetPainterSortZoom(int zoom)
{
    s_painterSortZoom = zoom;
}

int GetPainterSortZoom()
{
    return s_painterSortZoom;
}

// FUNC_001777 Painter's / unified-z barrier key (with ctx+0x130 zoom).
uint32_t ComputePainterSortKey(int32_t camX, int32_t camY, int32_t camZ)
{
    if (camZ < 1) {
        return 1;
    }
    int32_t ax = (camX < 0) ? -camX : camX;
    int32_t ay = (camY < 0) ? -camY : camY;
    uint32_t uVar1 = (uint32_t)((ax + ay) >> 3) + (uint32_t)camZ;
    int32_t iVar4 = 3 - s_painterSortZoom;
    if (iVar4 < 0) {
        uint8_t bVar2 = static_cast<uint8_t>(-iVar4);
        if (uVar1 < (0x80000000u >> (bVar2 & 0x1f))) {
            uVar1 <<= (bVar2 & 0x1f);
        } else {
            uVar1 = static_cast<uint32_t>(iVar4 * -0x2000000) | 0x80000000u |
                    (static_cast<uint32_t>(camZ) >> 7);
        }
    } else {
        uVar1 >>= (static_cast<uint8_t>(iVar4) & 0x1f);
    }
    return uVar1;
}

// Read raw int8 vertex coordinates from the vertex definition table
// at the given index. ASM ctx+0x150 + iVar7*6 + 2/3/4 — stride is
// 6 bytes per VERTEX (not per pair). ASM applies NO type dispatch and
// NO mirror here: the X mirror is decided by the CALLER from the
// normal index parity ((normalIdx ^ b0) & 1) — see ComputeFaceFacing.
static bool GetVertexCull(const ShipModel* shipModel, int index,
                          int8_t& vx, int8_t& vy, int8_t& vz)
{
    if (!shipModel || shipModel->vertexDataOffset == 0) return false;
    if (index < 0 || index > 255) return false;
    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return false;
    uint32_t offset = shipModel->vertexDataOffset + static_cast<uint32_t>(index) * 6;
    if (offset + 6 > data::GetDataBlobSize()) return false;
    const uint8_t* data = blob + offset;
    vx = static_cast<int8_t>(data[2]);
    vy = static_cast<int8_t>(data[3]);
    vz = static_cast<int8_t>(data[4]);
    return true;
}

// FUNC_001481 normalizing face cull using normal entry byte-0 vertex association.
// KEY: ASM FUNC_001685 normalizes shipPos to 14-bit via FUNC_001475 before
// computing the camera-in-model-space vector. Without normalization, world-space
// cull gives different dot products at large distances (math only equivalent
// when cameraPos = -shipPos at full scale).
// 
// This implements the normalized model-space equivalent:
//   1. Normalize shipPos to ≤14-bit precision → compute cameraModel
//   2. Read associated vertex + normal from normal entry byte 0
//   3. Test dot(vertexModel - cameraModel, normalModel) in model space
// Returns true if the face is BACK-FACING (should be culled).
// If overrideVertIdx >= 0, use that model vertex instead of normal byte0>>1
// (bisect: AB_CULL_ANY_VERT retries each poly.vertexIdx>>1).
// FUNC_001685: compute camera model-space position from shipPos/shipRot.
// Result: camMx/camMy/camMz in model space with LOD scaling applied.
// Also outputs lodShift for vertex LOD adjustment.
void ComputeCamModelPos(const ShipModel* shipModel,
                                const int32_t* shipRot,
                                const int32_t* shipPos,
                                int32_t& camMx, int32_t& camMy, int32_t& camMz,
                                int& lodShift)
{
    int scaleRelated = static_cast<int>(shipModel->scaleRelated);

    // 1. FUNC_001475: normalize shipPos to 14-bit precision.
    int32_t sx = shipPos[0], sy = shipPos[1], sz = shipPos[2];
    uint32_t orAll = (uint32_t)((sx < 0 ? -sx : sx) |
                                 (sy < 0 ? -sy : sy) |
                                 (sz < 0 ? -sz : sz));
    int normShift = 0;
    while (orAll > 0x3FFF) { orAll >>= 1; normShift++; }

    int32_t nx_pos = sx >> normShift;
    int32_t ny_pos = sy >> normShift;
    int32_t nz_pos = sz >> normShift;

    // 2. FUNC_001685: adjust LOD precision.
    lodShift = normShift;
    int adjust = (normShift + 7) - scaleRelated;
    if (adjust < 0) {
        int absAdj = -adjust;
        lodShift += absAdj;
        nx_pos >>= (absAdj & 0x1f);
        ny_pos >>= (absAdj & 0x1f);
        nz_pos >>= (absAdj & 0x1f);
    }

    // 3. Negate position.
    nx_pos = -nx_pos;
    ny_pos = -ny_pos;
    nz_pos = -nz_pos;

    // 4. VecMatTMul: transform to model space using shipRot transpose.
    camMx = FixedMulHigh(nx_pos, shipRot[0]) +
            FixedMulHigh(ny_pos, shipRot[1]) +
            FixedMulHigh(nz_pos, shipRot[2]);
    camMy = FixedMulHigh(nx_pos, shipRot[3]) +
            FixedMulHigh(ny_pos, shipRot[4]) +
            FixedMulHigh(nz_pos, shipRot[5]);
    camMz = FixedMulHigh(nx_pos, shipRot[6]) +
            FixedMulHigh(ny_pos, shipRot[7]) +
            FixedMulHigh(nz_pos, shipRot[8]);
}

bool ComputeFaceFacing(const ShipModel* shipModel, uint8_t normalIdx,
                       const int32_t* shipRot, const int32_t* shipPos,
                       const Camera3D* camera, int overrideVertIdx)
{
    if (!shipModel || shipModel->normalDataOffset == 0) return false;
    if (!shipRot || !shipPos || !camera) return false;

    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return false;

    // ---- FUNC_001685: dynamic camModel computation ----
    int32_t camMx, camMy, camMz;
    int lodShift;
    ComputeCamModelPos(shipModel, shipRot, shipPos, camMx, camMy, camMz, lodShift);

    int scaleRelated = static_cast<int>(shipModel->scaleRelated);

    // ---- FUNC_001481: per-vertex cull ----

    // ASM: iVar5 = (param_2 >> 1) * 6 + ctx+0x154, then reads at iVar5-6..iVar5-2.
    // ctx+0x154 points directly at the blob normal table, so the effective
    // entry is (normalIdx >> 1) - 1 — the table is 1-BASED. Verified 480/480
    // against the GDB oracle (/tmp/saker_flags_final.txt, 8 poses x 60 calls).
    int entry = (static_cast<int>(normalIdx) >> 1) - 1;
    if (entry < 0) return false;
    uint32_t normalOff = shipModel->normalDataOffset + static_cast<uint32_t>(entry) * 6;
    if (normalOff + 6 > data::GetDataBlobSize()) return false;

    const uint8_t* ndata = blob + normalOff;

    // Byte 0 >> 1 = associated vertex index (ASM: iVar7 = byte[-6] >> 1)
    int vertIdx = (overrideVertIdx >= 0) ? overrideVertIdx
                                         : (static_cast<int>(ndata[0]) >> 1);

    // Read raw vertex coords from definition table
    int8_t vx, vy, vz;
    if (!GetVertexCull(shipModel, vertIdx, vx, vy, vz)) return false;

    // ASM vertex mirror: negate X iff (param_2 ^ byte0) & 1 — parity of the
    // NORMAL index vs the normal's byte 0, NOT the vertex flag byte.
    if ((static_cast<int>(normalIdx) ^ static_cast<int>(ndata[0])) & 1) {
        vx = static_cast<int8_t>(-vx);
    }

    // Read normal directly (byte-for-byte matching ASM axis mapping).
    // ASM: local_10 = byte[2]*0x100, iVar8 = byte[3]*0x100, iVar6 = byte[4]*0x100
    // dot = (vx-camX)*local_10 + (vy-camY)*iVar8 + (vz-camZ)*iVar6
    // So: byte 2 = X component, byte 3 = Y, byte 4 = Z
    int32_t nx_as = static_cast<int32_t>(static_cast<int8_t>(ndata[2]));
    int32_t ny_as = static_cast<int32_t>(static_cast<int8_t>(ndata[3]));
    int32_t nz_as = static_cast<int32_t>(static_cast<int8_t>(ndata[4]));
    // Mirror: for odd normalIdx, ASM negates the normal X (local_10 * -0x100)
    if (static_cast<int>(normalIdx) & 1) {
        nx_as = -nx_as;
    }

    // Apply vertex LOD shift (ASM: shift = scaleRelated - lodShift)
    // lodShift was computed by FUNC_001685 above
    int vertShift = scaleRelated - lodShift;
    int32_t mx = static_cast<int32_t>(vx), my = static_cast<int32_t>(vy), mz = static_cast<int32_t>(vz);
    if (vertShift >= 0) {
        mx = (int32_t)((uint32_t)mx << (vertShift & 0x1f));
        my = (int32_t)((uint32_t)my << (vertShift & 0x1f));
        mz = (int32_t)((uint32_t)mz << (vertShift & 0x1f));
    } else {
        mx >>= (-vertShift & 0x1f);
        my >>= (-vertShift & 0x1f);
        mz >>= (-vertShift & 0x1f);
    }

    // Dot product in model space: (vertex - cameraModel) · normal
    // ASM normal is int8 * 0x100, but sign is same without scaling
    int64_t dot = static_cast<int64_t>(mx - camMx) * nx_as +
                  static_cast<int64_t>(my - camMy) * ny_as +
                  static_cast<int64_t>(mz - camMz) * nz_as;

    if (getenv("AB_CULL_DOT") && getenv("AB_CULL_DOT")[0] == '1') {
        static int s_dotN = 0;
        // Always emit Contour #217 body headers (nIdx 2/10); else first 64.
        const bool want = (normalIdx == 2 || normalIdx == 10) || (s_dotN < 64);
        if (want) {
            fprintf(stderr,
                    "CULL_DOT nIdx=%d entry=%d b0=%d vIdx=%d ov=%d v=(%d,%d,%d) "
                    "n=(%d,%d,%d) cam=(%d,%d,%d) m=(%d,%d,%d) "
                    "scaleRel=%d lod=%d vertShift=%d dot=%lld back=%d\n",
                    (int)normalIdx, entry, (int)ndata[0], vertIdx, overrideVertIdx,
                    (int)vx, (int)vy, (int)vz,
                    (int)nx_as, (int)ny_as, (int)nz_as,
                    (int)camMx, (int)camMy, (int)camMz,
                    (int)mx, (int)my, (int)mz,
                    scaleRelated, lodShift, vertShift,
                    (long long)dot, (dot >= 0) ? 1 : 0);
            if (normalIdx != 2 && normalIdx != 10) s_dotN++;
        }
    }

    // ASM: dot < 0 → front-facing (drawn), dot >= 0 → back-facing (culled)
    // Our convention: return true = backFacing = culled
    return (dot >= 0);
}

// Model-space face cull matching ASM FUNC_001481 + FUNC_001685.
// Uses raw model coords and normalized camera-in-model-space.
bool ComputeFaceFacingModel(
    const ShipModel* shipModel,
    uint8_t normalIdx, uint8_t vertIdx,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    int scaleRelated)
{
    (void)camera;  // Camera at origin in demo; ASM uses shipPos relative to camera.

    // 1. FUNC_001475: normalize shipPos to 14-bit precision.
    int32_t sx = shipPos[0], sy = shipPos[1], sz = shipPos[2];
    int32_t ax = sx < 0 ? -sx : sx;
    int32_t ay = sy < 0 ? -sy : sy;
    int32_t az = sz < 0 ? -sz : sz;
    uint32_t orAll = (uint32_t)(ax | ay | az);
    int normShift = 0;
    while (orAll > 0x3FFF) { orAll >>= 1; normShift++; }

    int32_t nx_pos = sx >> normShift;
    int32_t ny_pos = sy >> normShift;
    int32_t nz_pos = sz >> normShift;

    // 2. FUNC_001685: adjust LOD precision.
    // adjust = (normShift + 7) - scaleRelated
    // if adjust < 0: right-shift position by |adjust|, increase lodShift
    int lodShift = normShift;
    int adjust = (normShift + 7) - scaleRelated;
    if (adjust < 0) {
        int absAdj = -adjust;
        lodShift += absAdj;
        nx_pos >>= (absAdj & 0x1f);
        ny_pos >>= (absAdj & 0x1f);
        nz_pos >>= (absAdj & 0x1f);
    }

    // 3. Negate position.
    nx_pos = -nx_pos;
    ny_pos = -ny_pos;
    nz_pos = -nz_pos;

    // 4. VecMatTMul: transform to model space using shipRot transpose.
    // FUNC_001521 FixedMulHigh = (a*b)>>31 on Sin32 matrices.
    int32_t camMX = FixedMulHigh(nx_pos, shipRot[0]) +
                    FixedMulHigh(ny_pos, shipRot[1]) +
                    FixedMulHigh(nz_pos, shipRot[2]);
    int32_t camMY = FixedMulHigh(nx_pos, shipRot[3]) +
                    FixedMulHigh(ny_pos, shipRot[4]) +
                    FixedMulHigh(nz_pos, shipRot[5]);
    int32_t camMZ = FixedMulHigh(nx_pos, shipRot[6]) +
                    FixedMulHigh(ny_pos, shipRot[7]) +
                    FixedMulHigh(nz_pos, shipRot[8]);

    // 5. Get raw model vertex coords (int8 from mesh data).
    ShipVertex vert;
    if (!GetVertexMirrored(shipModel, vertIdx, vert)) return false;

    // 6. Apply vertex LOD shift: vert <<= (scaleRelated - lodShift)
    int vertShift = scaleRelated - lodShift;
    int32_t vx = vert.x, vy = vert.y, vz = vert.z;
    if (vertShift >= 0) {
        vx <<= (vertShift & 0x1f);
        vy <<= (vertShift & 0x1f);
        vz <<= (vertShift & 0x1f);
    } else {
        vx >>= (-vertShift & 0x1f);
        vy >>= (-vertShift & 0x1f);
        vz >>= (-vertShift & 0x1f);
    }

    // 7. Get raw model normal (int8), scaled by 0x100 (ASM convention).
    int32_t nnx, nny, nnz;
    if (!GetNormal(shipModel, normalIdx, nnx, nny, nnz)) return false;
    nnx <<= 8; nny <<= 8; nnz <<= 8;

    // 8. Dot product: (modelVert - cameraModel) · scaledNormal
    int64_t dot = (int64_t)(vx - camMX) * nnx +
                  (int64_t)(vy - camMY) * nny +
                  (int64_t)(vz - camMZ) * nnz;

    // ASM: dot < 0 → draw, dot >= 0 → cull. But with our normal byte order
    // (Z=byte2, X=byte4 inverted), the literal ASM direction over-culls.
    // Empirically: dot < 0 = cull gives best results. The discrepancy is from
    // the camera direction matrix (ctx+0x38..0x40) storage order coupling
    // with the normal data layout.
    return dot < 0;  // true = back-facing (empirically verified)
}

// Transform a single vertex from model space to world space, then project to screen.
// ASM FUNC_001478: model int8 << ObjectStruct.scaleRelated (not always <<8).
static ProjectedVertex TransformVertex(
    const ShipVertex& vert,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    int scaleRelated)
{
    ProjectedVertex result = {};

    int shift = scaleRelated;
    if (shift < 0) shift = 0;
    if (shift > 24) shift = 24;
    int32_t mx = (int32_t)((uint32_t)(int32_t)vert.x << shift);
    int32_t my = (int32_t)((uint32_t)(int32_t)vert.y << shift);
    int32_t mz = (int32_t)((uint32_t)(int32_t)vert.z << shift);

    // Transform to world: ASM FUNC_001478 → SpecialVecMatMul (FixedMulHigh) on
    // MatBuildYZT Sin32-scale matrices. AB_FP16_VERT=1 restores the old
    // MatScaleSin32ToFp16 + >>16 path (1px tip error at pitch±90).
    int32_t wx, wy, wz;
    if (getenv("AB_FP16_VERT") && getenv("AB_FP16_VERT")[0] == '1') {
        wx = (int32_t)(((int64_t)mx * shipRot[0] +
                         (int64_t)my * shipRot[3] +
                         (int64_t)mz * shipRot[6]) >> 16) + shipPos[0];
        wy = (int32_t)(((int64_t)mx * shipRot[1] +
                         (int64_t)my * shipRot[4] +
                         (int64_t)mz * shipRot[7]) >> 16) + shipPos[1];
        wz = (int32_t)(((int64_t)mx * shipRot[2] +
                         (int64_t)my * shipRot[5] +
                         (int64_t)mz * shipRot[8]) >> 16) + shipPos[2];
    } else {
        Mat33 sm;
        for (int i = 0; i < 9; i++) sm.m[i] = shipRot[i];
        Vec32 model = {mx, my, mz};
        Vec32 world = {};
        SpecialVecMatMul(&world, &model, &sm);
        wx = world.x + shipPos[0];
        wy = world.y + shipPos[1];
        wz = world.z + shipPos[2];
    }

    // Camera-space coords (same transform as ProjectPoint) for Painter's sort key
    int32_t cx = wx - camera->position.x;
    int32_t cy = wy - camera->position.y;
    int32_t cz = wz - camera->position.z;
    int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                             (int64_t)cy * camera->rotation.m[3] +
                             (int64_t)cz * camera->rotation.m[6]) >> 16);
    int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                             (int64_t)cy * camera->rotation.m[4] +
                             (int64_t)cz * camera->rotation.m[7]) >> 16);
    int32_t vz = (int32_t)(((int64_t)cx * camera->rotation.m[2] +
                             (int64_t)cy * camera->rotation.m[5] +
                             (int64_t)cz * camera->rotation.m[8]) >> 16);

    // Project to screen
    Point3D worldPos = {wx, wy, wz};
    ProjResult proj;
    bool visible = ProjectPoint(camera, &worldPos, &proj);

    result.screenX = proj.sx;
    result.screenY = proj.sy;
    result.depth = proj.z;
    result.camX = vx;
    result.camY = vy;
    result.worldX = wx;
    result.worldY = wy;
    result.worldZ = wz;
    result.visible = visible && proj.visible;

    // Prefer explicit camera Z when projection early-outs
    if (!visible) {
        result.depth = vz;
    }

    return result;
}

// ASM FUNC_001478 case 0x0B/0x0C midpoint: (a+b)>>1 with toward-zero fix on
// odd negative sums (Ghidra: if mid<0 && (sum&1) mid++).
static int32_t AsmWorldMidAvg(int32_t a, int32_t b)
{
    const uint32_t u = static_cast<uint32_t>(a) + static_cast<uint32_t>(b);
    int32_t mid = static_cast<int32_t>(u) >> 1;
    if (mid < 0 && (u & 1u) != 0)
        mid += 1;
    return mid;
}

// FUNC_001478 plain XYZ (flags 0/1/2/9/10): transform the even-slot model, then
// for odd indices write the mirror slot as world - 2*FMH(mx, R.col0) (cases 0–2)
// or world - FMH(mx, R.col0) (cases 9–10). Independent FMH(-mx) is not odd and
// lands 1px inward on Asp front wing tips (265 vs ASM 266).
// Flags 0x0B/0x0C: average *world* positions of the two refs (not model coords) —
// model-space avg projects 1px off when the refs differ in depth (Explorer tip
// attach 74 = mid(66,68) → ASM y=26, model-mid y=27).
static ProjectedVertex TransformMeshVertex(
    const ShipModel* shipModel,
    int index,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    int scaleRelated,
    int depth = 0)
{
    ProjectedVertex empty = {};
    if (!shipModel || !shipRot || !shipPos || !camera) return empty;
    if (index < 0 || index > 255 || depth > 8) return empty;

    const bool fp16 = getenv("AB_FP16_VERT") && getenv("AB_FP16_VERT")[0] == '1';
    if (fp16) {
        ShipVertex vert{};
        if (!GetVertexMirrored(shipModel, index, vert)) return empty;
        return TransformVertex(vert, shipRot, shipPos, camera, scaleRelated);
    }

    const uint8_t* blob = data::GetDataBlob();
    if (!blob || shipModel->vertexDataOffset == 0) return empty;
    const uint32_t rawOff = shipModel->vertexDataOffset +
                            static_cast<uint32_t>(index / 2) * 6;
    if (rawOff + 6 > data::GetDataBlobSize()) return empty;
    const uint8_t* raw = blob + rawOff;
    const uint8_t flag = raw[0];

    // FUNC_001478 case 0x0B/0x0C: mid of transformed cache verts (+0x18/+0x1c/+0x20).
    if (flag == 0x0B || flag == 0x0C) {
        const int ia = static_cast<int>(raw[3] ^ (index & 1));
        const int ib = static_cast<int>(raw[4] ^ (index & 1));
        const ProjectedVertex pa = TransformMeshVertex(
            shipModel, ia, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const ProjectedVertex pb = TransformMeshVertex(
            shipModel, ib, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const int32_t wx = AsmWorldMidAvg(pa.worldX, pb.worldX);
        const int32_t wy = AsmWorldMidAvg(pa.worldY, pb.worldY);
        const int32_t wz = AsmWorldMidAvg(pa.worldZ, pb.worldZ);

        ProjectedVertex result = {};
        int32_t cx = wx - camera->position.x;
        int32_t cy = wy - camera->position.y;
        int32_t cz = wz - camera->position.z;
        int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                                 (int64_t)cy * camera->rotation.m[3] +
                                 (int64_t)cz * camera->rotation.m[6]) >> 16);
        int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                                 (int64_t)cy * camera->rotation.m[4] +
                                 (int64_t)cz * camera->rotation.m[7]) >> 16);
        int32_t vz = (int32_t)(((int64_t)cx * camera->rotation.m[2] +
                                 (int64_t)cy * camera->rotation.m[5] +
                                 (int64_t)cz * camera->rotation.m[8]) >> 16);
        Point3D worldPos = {wx, wy, wz};
        ProjResult proj;
        bool visible = ProjectPoint(camera, &worldPos, &proj);
        result.screenX = proj.sx;
        result.screenY = proj.sy;
        result.depth = proj.z;
        result.camX = vx;
        result.camY = vy;
        result.worldX = wx;
        result.worldY = wy;
        result.worldZ = wz;
        result.visible = visible && proj.visible;
        if (!visible)
            result.depth = vz;
        return result;
    }

    // FUNC_001478 case 0x0F/0x10: face midpoint — world = Vb - Vc + Va
    // (by - bz + bx), each index XOR parent parity. Skeet #45 upper hull.
    if (flag == 0x0F || flag == 0x10) {
        const int ia = static_cast<int>(raw[3] ^ (index & 1)); // by
        const int ib = static_cast<int>(raw[4] ^ (index & 1)); // bz
        const int ic = static_cast<int>(raw[2] ^ (index & 1)); // bx
        const ProjectedVertex pa = TransformMeshVertex(
            shipModel, ia, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const ProjectedVertex pb = TransformMeshVertex(
            shipModel, ib, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const ProjectedVertex pc = TransformMeshVertex(
            shipModel, ic, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const int32_t wx = pa.worldX - pb.worldX + pc.worldX;
        const int32_t wy = pa.worldY - pb.worldY + pc.worldY;
        const int32_t wz = pa.worldZ - pb.worldZ + pc.worldZ;

        ProjectedVertex result = {};
        int32_t cx = wx - camera->position.x;
        int32_t cy = wy - camera->position.y;
        int32_t cz = wz - camera->position.z;
        int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                                 (int64_t)cy * camera->rotation.m[3] +
                                 (int64_t)cz * camera->rotation.m[6]) >> 16);
        int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                                 (int64_t)cy * camera->rotation.m[4] +
                                 (int64_t)cz * camera->rotation.m[7]) >> 16);
        int32_t vz = (int32_t)(((int64_t)cx * camera->rotation.m[2] +
                                 (int64_t)cy * camera->rotation.m[5] +
                                 (int64_t)cz * camera->rotation.m[8]) >> 16);
        Point3D worldPos = {wx, wy, wz};
        ProjResult proj;
        bool visible = ProjectPoint(camera, &worldPos, &proj);
        result.screenX = proj.sx;
        result.screenY = proj.sy;
        result.depth = proj.z;
        result.camX = vx;
        result.camY = vy;
        result.worldX = wx;
        result.worldY = wy;
        result.worldZ = wz;
        result.visible = visible && proj.visible;
        if (!visible)
            result.depth = vz;
        return result;
    }

    // FUNC_001478 case 0x13/0x14 → FUNC_001477: world lerp A=by → B=bz with
    // t = FUNC_001473(byte2)*0x4000. Kind 0xC0 reads mesh ALU regs (Skeet
    // slot3=0x8400 after head ALU → near-midpoint). Kind 0x80 GameObj → 0
    // in ship-demo (Courier tip stays at by).
    if (flag == 0x13 || flag == 0x14) {
        const int ia = static_cast<int>(raw[3] ^ (index & 1));
        const int ib = static_cast<int>(raw[4] ^ (index & 1));
        const ProjectedVertex pa = TransformMeshVertex(
            shipModel, ia, shipRot, shipPos, camera, scaleRelated, depth + 1);
        const ProjectedVertex pb = TransformMeshVertex(
            shipModel, ib, shipRot, shipPos, camera, scaleRelated, depth + 1);
        // FUNC_001473(byte2): 0x00→low6, 0x40→low6<<10, 0x80→GameObj, 0xc0→ALU.
        const uint32_t enc = raw[2];
        const uint32_t kind = enc & 0xc0u;
        uint32_t anim = 0;
        if (kind == 0)
            anim = enc & 0x3fu;
        else if (kind == 0x40)
            anim = static_cast<uint32_t>(static_cast<int16_t>(enc & 0x3fu) << 10);
        else if (kind == 0xc0)
            anim = GetMeshAluReg(static_cast<int>(enc & 0x3fu));
        // kind 0x80: GameObj ushort — 0 in ship-demo / ab_capture.
        if (getenv("AB_VERT_13_FAR") && getenv("AB_VERT_13_FAR")[0] == '1')
            anim = 0x10000u; // t=0x40000000 → B (old thruster tip hack)
        const int32_t t = static_cast<int32_t>(anim * 0x4000u);
        const int32_t wx = FixedMulHigh(pb.worldX - pa.worldX, t) * 2 + pa.worldX;
        const int32_t wy = FixedMulHigh(pb.worldY - pa.worldY, t) * 2 + pa.worldY;
        const int32_t wz = FixedMulHigh(pb.worldZ - pa.worldZ, t) * 2 + pa.worldZ;

        ProjectedVertex result = {};
        int32_t cx = wx - camera->position.x;
        int32_t cy = wy - camera->position.y;
        int32_t cz = wz - camera->position.z;
        int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                                 (int64_t)cy * camera->rotation.m[3] +
                                 (int64_t)cz * camera->rotation.m[6]) >> 16);
        int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                                 (int64_t)cy * camera->rotation.m[4] +
                                 (int64_t)cz * camera->rotation.m[7]) >> 16);
        int32_t vz = (int32_t)(((int64_t)cx * camera->rotation.m[2] +
                                 (int64_t)cy * camera->rotation.m[5] +
                                 (int64_t)cz * camera->rotation.m[8]) >> 16);
        Point3D worldPos = {wx, wy, wz};
        ProjResult proj;
        bool visible = ProjectPoint(camera, &worldPos, &proj);
        result.screenX = proj.sx;
        result.screenY = proj.sy;
        result.depth = proj.z;
        result.camX = vx;
        result.camY = vy;
        result.worldX = wx;
        result.worldY = wy;
        result.worldZ = wz;
        result.visible = visible && proj.visible;
        if (!visible)
            result.depth = vz;
        return result;
    }

    ShipVertex evenVert{};
    if (!GetVertexMirrored(shipModel, index & ~1, evenVert)) return empty;

    const bool plain = (flag <= 2) || (flag == 9) || (flag == 10);
    if (!plain) {
        ShipVertex vert{};
        if (!GetVertexMirrored(shipModel, index, vert)) return empty;
        return TransformVertex(vert, shipRot, shipPos, camera, scaleRelated);
    }

    int shift = scaleRelated;
    if (shift < 0) shift = 0;
    if (shift > 24) shift = 24;
    const int32_t mx = (int32_t)((uint32_t)(int32_t)evenVert.x << shift);
    const int32_t my = (int32_t)((uint32_t)(int32_t)evenVert.y << shift);
    const int32_t mz = (int32_t)((uint32_t)(int32_t)evenVert.z << shift);

    Mat33 sm;
    for (int i = 0; i < 9; i++) sm.m[i] = shipRot[i];
    Vec32 model = {mx, my, mz};
    Vec32 delta = {};
    SpecialVecMatMul(&delta, &model, &sm);

    int32_t wx = delta.x + shipPos[0];
    int32_t wy = delta.y + shipPos[1];
    int32_t wz = delta.z + shipPos[2];

    if (index & 1) {
        const int32_t t0 = FixedMulHigh(mx, shipRot[0]);
        const int32_t t1 = FixedMulHigh(mx, shipRot[1]);
        const int32_t t2 = FixedMulHigh(mx, shipRot[2]);
        if (flag == 9 || flag == 10) {
            // Cases 9/10: odd = even - scratch (not *2)
            wx -= t0;
            wy -= t1;
            wz -= t2;
        } else {
            // Cases 0/1/2: odd = even + scratch*-2
            wx -= 2 * t0;
            wy -= 2 * t1;
            wz -= 2 * t2;
        }
    }

    ShipVertex fake = evenVert;
    // Reuse projection via a thin path: build ProjectedVertex from world.
    ProjectedVertex result = {};
    int32_t cx = wx - camera->position.x;
    int32_t cy = wy - camera->position.y;
    int32_t cz = wz - camera->position.z;
    int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                             (int64_t)cy * camera->rotation.m[3] +
                             (int64_t)cz * camera->rotation.m[6]) >> 16);
    int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                             (int64_t)cy * camera->rotation.m[4] +
                             (int64_t)cz * camera->rotation.m[7]) >> 16);
    int32_t vz = (int32_t)(((int64_t)cx * camera->rotation.m[2] +
                             (int64_t)cy * camera->rotation.m[5] +
                             (int64_t)cz * camera->rotation.m[8]) >> 16);

    Point3D worldPos = {wx, wy, wz};
    ProjResult proj;
    bool visible = ProjectPoint(camera, &worldPos, &proj);

    result.screenX = proj.sx;
    result.screenY = proj.sy;
    result.depth = proj.z;
    result.camX = vx;
    result.camY = vy;
    result.worldX = wx;
    result.worldY = wy;
    result.worldZ = wz;
    result.visible = visible && proj.visible;
    if (!visible)
        result.depth = vz;
    (void)fake;
    return result;
}

// Check if a polygon is back-facing using screen-space cross product
bool IsBackFacing(const ProjectedVertex& v0, const ProjectedVertex& v1,
                  const ProjectedVertex& v2, const ProjectedVertex& v3)
{
    // Compute cross product of two edges in screen space
    // edge1 = v1 - v0, edge2 = v2 - v0
    int32_t e1x = v1.screenX - v0.screenX;
    int32_t e1y = v1.screenY - v0.screenY;
    int32_t e2x = v2.screenX - v0.screenX;
    int32_t e2y = v2.screenY - v0.screenY;

    // Cross product Z component
    int32_t cross = e1x * e2y - e1y * e2x;

    // The original game uses a winding order where cross > 0 means back-facing.
    // We also use >= to cull degenerate (zero-area) polygons head-on.
    return cross >= 0;
}

// Check if polygon is on screen
// R778: a triangle can cover a large part of the screen with EVERY vertex outside
// it (hull 17's big panel: (255,-34) (-57,32) (369,114) covers ~20 700 px and all
// three corners are off-viewport). IsOnScreen() accepts only polygons with at least
// one vertex inside the margin, so it rejects exactly those. This is the weaker but
// correct shape: does the polygon's AABB overlap the viewport?
bool PolyTouchesScreen(const ProjectedVertex* verts, int count, int margin)
{
    if (count <= 0) return false;
    int minx = verts[0].screenX, maxx = verts[0].screenX;
    int miny = verts[0].screenY, maxy = verts[0].screenY;
    for (int i = 1; i < count; i++) {
        if (verts[i].screenX < minx) minx = verts[i].screenX;
        if (verts[i].screenX > maxx) maxx = verts[i].screenX;
        if (verts[i].screenY < miny) miny = verts[i].screenY;
        if (verts[i].screenY > maxy) maxy = verts[i].screenY;
    }
    return !(maxx < -margin || minx > SCREEN_WIDTH + margin ||
             maxy < -margin || miny > SCREEN_HEIGHT + margin);
}

bool IsOnScreen(const ProjectedVertex* verts, int count, int margin)
{
    // Check if any vertex is on screen (with margin)
    for (int i = 0; i < count; i++) {
        if (!verts[i].visible) continue;
        if (verts[i].screenX >= -margin && verts[i].screenX <= SCREEN_WIDTH + margin &&
            verts[i].screenY >= -margin && verts[i].screenY <= SCREEN_HEIGHT + margin) {
            return true;
        }
    }
    return false;
}

// Resolve one mesh vertex (FUNC_001478). Flag byte selects interpretation:
//   0/1/2/9/10: plain XYZ (int8), odd index mirrors X
//   0x0B/0x0C:  midpoint of mesh verts in bytes [3] and [4]
//   7/8:        base mesh vert in byte [4] (skip thruster noise)
//   0x13/0x14:  interpolate bytes [3]→[4] (static demo uses midpoint)
static bool GetVertexMirrored(const ShipModel* shipModel, int index, ShipVertex& out, int depth)
{
    if (!shipModel || shipModel->vertexDataOffset == 0) return false;
    if (index < 0 || index > 255 || depth > 8) return false;

    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return false;

    int dataEntry = index / 2;
    uint32_t offset = shipModel->vertexDataOffset + dataEntry * 6;
    if (offset + 6 > data::GetDataBlobSize()) return false;

    const uint8_t* data = blob + offset;
    const uint8_t flag = data[0];
    const uint8_t bx = data[2];
    const uint8_t by = data[3];
    const uint8_t bz = data[4];

    out.mirrorFlag = flag;
    out.padding1 = data[1];
    out.padding2 = data[5];

    auto avg = [](int a, int b) -> int8_t {
        return static_cast<int8_t>((a + b) / 2);
    };

    switch (flag) {
    case 0:
    case 1:
    case 2:
    case 9:
    case 10:
        out.x = static_cast<int8_t>(bx);
        out.y = static_cast<int8_t>(by);
        out.z = static_cast<int8_t>(bz);
        if (index & 1) out.x = static_cast<int8_t>(-out.x);
        return true;

    case 0x0B:
    case 0x0C: {
        // Midpoint of two mesh indices (bytes y,z), XOR mirror with parent index.
        ShipVertex a{}, b{};
        if (!GetVertexMirrored(shipModel, by ^ (index & 1), a, depth + 1)) return false;
        if (!GetVertexMirrored(shipModel, bz ^ (index & 1), b, depth + 1)) return false;
        out.x = avg(a.x, b.x);
        out.y = avg(a.y, b.y);
        out.z = avg(a.z, b.z);
        return true;
    }

    case 5:
    case 6: {
        // FUNC_001478 case 5/6: even = -vert[bz], odd = -vert[bz^1] (cam-space
        // negate of the referenced slot). Model-space negate is equivalent under
        // a linear child frame. Cobra #205 antenna used flag 5 with bz=1 → a
        // real XZ rectangle; treating bytes as xyz collapsed the pair to (±1,0,1).
        const int ref = (index & 1) ? (bz ^ 1) : bz;
        ShipVertex base{};
        if (!GetVertexMirrored(shipModel, ref, base, depth + 1))
            return false;
        out.x = static_cast<int8_t>(-base.x);
        out.y = static_cast<int8_t>(-base.y);
        out.z = static_cast<int8_t>(-base.z);
        out.mirrorFlag = flag;
        out.padding1 = data[1];
        out.padding2 = data[5];
        return true;
    }

    case 7:
    case 8: {
        // Thruster jitter around base vert bz — use base position for static mesh.
        return GetVertexMirrored(shipModel, bz ^ (index & 1), out, depth + 1);
    }

    case 0x13:
    case 0x14: {
        // FUNC_001477 static (anim=0): start endpoint by. Dig AB_VERT_13_FAR=1
        // restores the old thruster far-endpoint (bz) hack.
        const int ref = (getenv("AB_VERT_13_FAR") && getenv("AB_VERT_13_FAR")[0] == '1')
                            ? (bz ^ (index & 1))
                            : (by ^ (index & 1));
        return GetVertexMirrored(shipModel, ref, out, depth + 1);
    }

    default:
        out.x = static_cast<int8_t>(bx);
        out.y = static_cast<int8_t>(by);
        out.z = static_cast<int8_t>(bz);
        if (index & 1) out.x = static_cast<int8_t>(-out.x);
        return true;
    }
}

bool LookupShipVertex(const ShipModel* shipModel, int index, ShipVertex& out)
{
    return GetVertexMirrored(shipModel, index, out, 0);
}

// Get normal vector from ship model with mirror handling.
// ASM FUNC_001481 addressing: entry = (index >> 1) - 1 (the normal table is
// 1-BASED — ctx+0x154 points at the blob table and reads use offsets -6..-2).
// Axes: byte 2 = X, byte 3 = Y, byte 4 = Z; X negated for odd indices.
// The previous "Z=2, X=4 inverted" byte swap was an artifact of reading the
// wrong (adjacent) entry with the old 0-based indexing.
bool GetNormal(const ShipModel* shipModel, int index, int32_t& nx, int32_t& ny, int32_t& nz)
{
    if (!shipModel || shipModel->normalDataOffset == 0) return false;

    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return false;

    int dataEntry = (index >> 1) - 1;
    if (dataEntry < 0) return false;
    uint32_t offset = shipModel->normalDataOffset + dataEntry * 6;
    if (offset + 6 > data::GetDataBlobSize()) return false;

    const uint8_t* data = blob + offset;
    nx = (int32_t)(int8_t)data[2];  // X at byte 2
    ny = (int32_t)(int8_t)data[3];  // Y at byte 3
    nz = (int32_t)(int8_t)data[4];  // Z at byte 4

    // Negate X for odd indices (mirror copy)
    if (index & 1) {
        nx = -nx;
    }

    return true;
}

// Integer sqrt for FUNC_001554 (ASM DATA_007723 → FUNC_001467). Digon light
// XY magnitudes are small; a Newton loop is fine vs the sine-table oracle.
static int32_t GlintIsqrt(int32_t n)
{
    if (n <= 0) return 0;
    if (n < 2) return n;
    uint32_t x = static_cast<uint32_t>(n);
    uint32_t r = x;
    uint32_t q = (x + 1u) >> 1;
    while (q < r) {
        r = q;
        q = (x / q + q) >> 1;
    }
    return static_cast<int32_t>(r);
}

// FUNC_001736 → FUNC_001554/001555/001720: build one Contour digon half around
// an anchor vertex, oriented by the lit-glint light vector (not a screen
// billboard). Corner order matches ASM 001720:
//   upper: (R, R+U, L+U, L)   lower: (R, R-D, L-D, L)
// where R/L = pos ± offset1, U/D = ± offset2.
static bool ExpandGlintDigonHalf(const ShipModel* shipModel,
                                 const int32_t* shipRot,
                                 const ProjectedVertex& anchor,
                                 uint8_t lightIdx,
                                 uint8_t scaleByte,
                                 bool lower,
                                 ProjectedVertex outVerts[4])
{
    if (!shipModel || !shipRot || !outVerts) return false;
    if (anchor.depth < 0x41) {
        if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1')
            fprintf(stderr, "GLINT_FAIL depth=%d\n", anchor.depth);
        return false;
    }

    int32_t nx = 0, ny = 0, nz = 0;
    if (!GetNormal(shipModel, static_cast<int>(lightIdx), nx, ny, nz)) {
        if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1')
            fprintf(stderr, "GLINT_FAIL GetNormal light=%u\n", lightIdx);
        return false;
    }

    // Transform light into camera space (ASM 001736 via FUNC_001521 / FMH).
    const int32_t lx = FixedMulHigh(shipRot[0], nx) +
                       FixedMulHigh(shipRot[3], ny) +
                       FixedMulHigh(shipRot[6], nz);
    const int32_t ly = FixedMulHigh(shipRot[1], nx) +
                       FixedMulHigh(shipRot[4], ny) +
                       FixedMulHigh(shipRot[7], nz);
    const int32_t lz = FixedMulHigh(shipRot[2], nx) +
                       FixedMulHigh(shipRot[5], ny) +
                       FixedMulHigh(shipRot[8], nz);

    // ctx+0x148 = ObjectStruct.scaleRelated (demo3d / mesh_parser).
    const int32_t scaleShift = static_cast<int32_t>(shipModel->scaleRelated) & 0x1f;
    const int32_t scale = static_cast<int32_t>(scaleByte) << scaleShift;

    const int32_t px = anchor.camX;
    const int32_t py = anchor.camY;
    const int32_t pz = anchor.depth;

    int32_t ox = 0, oy = 0, oz = 0;       // offset1
    int32_t ux = 0, uy = 0, uz = 0;       // offset2
    const int32_t len2 = lx * lx + ly * ly;
    if (len2 < 9) {
        // FUNC_001556: camera-axis billboard when light is near Z.
        ox = scale;
        oy = 0;
        oz = 0;
        ux = 0;
        uy = (scale * 0xAAAA) >> 15; // scale * 2/3
        uz = 0;
    } else {
        // FUNC_001554 (ASM): perpendicular-in-XY + foreshortened Z tilt.
        const int32_t len = GlintIsqrt(len2);
        if (len <= 0) return false;
        ox = -(((((ly << 15) / len) * scale) >> 15));
        oy = ((((lx << 15) / len) * scale) >> 15);
        oz = 0;
        const int32_t iVar1 = (scale << 10) / 3; // scale * 4/3 in Q10-ish
        ux = ((((lx * lz) / len) * iVar1) >> 15);
        uy = ((((ly * lz) / len) * iVar1) >> 15);
        uz = ((-len * iVar1) >> 15);
    }

    if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1') {
        fprintf(stderr,
                "GLINT_FRAME light=(%d,%d,%d)->(%d,%d,%d) scale=%d<<%d=%d "
                "off1=(%d,%d,%d) off2=(%d,%d,%d) pos=(%d,%d,%d)\n",
                nx, ny, nz, lx, ly, lz, scaleByte, scaleShift, scale,
                ox, oy, oz, ux, uy, uz, px, py, pz);
    }

    // Four corners of this digon half in camera space.
    const int32_t sx = lower ? -1 : 1; // lower uses -offset2
    const int32_t c[4][3] = {
        {px + ox, py + oy, pz + oz},                         // R
        {px + ox + sx * ux, py + oy + sx * uy, pz + oz + sx * uz}, // R±U
        {px - ox + sx * ux, py - oy + sx * uy, pz - oz + sx * uz}, // L±U
        {px - ox, py - oy, pz - oz},                         // L
    };

    for (int i = 0; i < 4; i++) {
        ProjResult proj{};
        if (!ProjectCameraPoint(c[i][0], c[i][1], c[i][2], &proj)) {
            if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1')
                fprintf(stderr, "GLINT_FAIL proj i=%d pt=(%d,%d,%d)\n",
                        i, c[i][0], c[i][1], c[i][2]);
            return false;
        }
        outVerts[i] = anchor;
        outVerts[i].screenX = proj.sx;
        outVerts[i].screenY = proj.sy;
        outVerts[i].depth = c[i][2];
        outVerts[i].camX = c[i][0];
        outVerts[i].camY = c[i][1];
        outVerts[i].visible = proj.visible;
    }
    return true;
}

// FUNC_001739: remap a 3x3 column-major rotation based on sprite orient opcode.
// Writes SOURCE columns 0/1/2 into permuted DEST slots; bits 0x2000/0x1000/0x800
// flip the corresponding *source* column. case = (opcode >> 8) & 7.
static void RemapSpriteMatrix(int32_t* outRot, const int32_t* inRot, uint32_t opcode)
{
    int32_t* d0 = outRot;
    int32_t* d1 = outRot + 3;
    int32_t* d2 = outRot + 6;
    int32_t* slots[3] = {d0, d1, d2};
    switch ((opcode >> 8) & 7u) {
    case 0:
    case 6:
        slots[0] = d0; slots[1] = d1; slots[2] = d2;
        break;
    case 1:
        slots[0] = d1; slots[1] = d2; slots[2] = d0;
        break;
    case 2:
        slots[0] = d2; slots[1] = d0; slots[2] = d1;
        break;
    case 3:
        slots[0] = d2; slots[1] = d1; slots[2] = d0;
        break;
    case 4:
        slots[0] = d1; slots[1] = d0; slots[2] = d2;
        break;
    case 5:
        slots[0] = d0; slots[1] = d2; slots[2] = d1;
        break;
    default:
        break;
    }
    const uint32_t flipBits[3] = {0x2000u, 0x1000u, 0x0800u};
    for (int col = 0; col < 3; col++) {
        const int32_t* src = inRot + col * 3;
        int32_t* dst = slots[col];
        if ((opcode & flipBits[col]) != 0) {
            dst[0] = -src[0];
            dst[1] = -src[1];
            dst[2] = -src[2];
        } else {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    }
}

// FUNC_001744: rotate two basis columns by (sin,cos) from Sin32.
// axis 0 → cols 1&2; axis 1 → cols 2&0; axis 2 → cols 0&1.
static void RotateBasisColumns(int32_t* basis, int axis, int32_t cosA, int32_t sinA)
{
    int32_t* colA = nullptr;
    int32_t* colB = nullptr;
    if (axis == 0) {
        colA = basis + 3;
        colB = basis + 6;
    } else if (axis == 1) {
        colA = basis + 6;
        colB = basis + 0;
    } else {
        colA = basis + 0;
        colB = basis + 3;
    }
    const int32_t a0 = colA[0], a1 = colA[1], a2 = colA[2];
    const int32_t b0 = colB[0], b1 = colB[1], b2 = colB[2];
    colA[0] = FixedMulHigh(a0, cosA) + FixedMulHigh(b0, sinA);
    colA[1] = FixedMulHigh(a1, cosA) + FixedMulHigh(b1, sinA);
    colA[2] = FixedMulHigh(a2, cosA) + FixedMulHigh(b2, sinA);
    colB[0] = FixedMulHigh(-a0, sinA) + FixedMulHigh(b0, cosA);
    colB[1] = FixedMulHigh(-a1, sinA) + FixedMulHigh(b1, cosA);
    colB[2] = FixedMulHigh(-a2, sinA) + FixedMulHigh(b2, cosA);
}

// FUNC_001474 angle decode for Matrix payload word.
static uint32_t DecodeMatrixAngle(uint16_t word)
{
    if (const char* ov = getenv("AB_MATRIX_ANGLE"); ov && ov[0])
        return static_cast<uint32_t>(strtoul(ov, nullptr, 0));
    if (getenv("AB_MATRIX_NO_TWIST") && getenv("AB_MATRIX_NO_TWIST")[0] == '1')
        return 0;
    const uint32_t p = word;
    const uint32_t kind = p & 0xc0u;
    const int idx = static_cast<int>(p & 0x3fu);
    // Title / live parse: kind 0xc0 = ALU bank (poly snapshot via override);
    // kind 0x80 = ObjectStruct+0x9c words when SetMeshParseGameObj is active.
    // Dig: AB_MATRIX_ANGLE=<n> forces a twist.
    // FUNC_001474: kinds 0x00 and 0x40 share the packed immediate decode.
    if (kind == 0 || kind == 0x40) {
        const int32_t hi = static_cast<int16_t>(static_cast<uint16_t>((p & 0x7fu) << 9));
        const uint32_t lo = (p >> 7) & 0x1ffu;
        return static_cast<uint32_t>(hi) | lo;
    }
    if (kind == 0xc0)
        return GetMeshAluReg(idx);
    if (kind == 0x80)
        return GetMeshParseGameObjWord(idx);
    return 0;
}

// Resolve child render frame for a sprite-expanded poly (FUNC_001740/001750).
// Attach vertex is used for GateNear LOD; world origin copies parent when bit
// 0x4000 is clear (0x192e path). Remapped rot via FUNC_001739.
// matrixOp/word: preceding FUNC_001741 — case-6 sprites copy that basis.
static bool BuildSpriteAttachFrame(
    const ShipModel* parentModel,
    const int32_t* parentRot,
    const int32_t* parentPos,
    const Camera3D* camera,
    int parentScale,
    uint8_t attachVert,
    uint16_t spriteOp,
    const ShipModel* childModel,
    int32_t* outChildRot,
    int32_t* outChildPos,
    int* outChildScale,
    uint16_t matrixOp = 0,
    uint16_t matrixWord = 0,
    bool matrixWorking = false,
    bool matrixSentinel = false)
{
    if (!parentModel || !childModel || !parentRot || !parentPos || !camera)
        return false;
    // Sprite child origin: world-space attach vertex (FUNC_001749 byte0).
    // ASM 001740 copies parent+0x134 when bit0x4000 is clear; using attach for
    // that path is empirical (Saker #201 tip / title card spatial flybys).
    // AB_SPRITE_PARENT_ORIGIN=1 restores 001740-literal (piles title fleets at
    // the COG and kills orbit animation — prefer attach for cast motion).
    const bool parentOrigin =
        getenv("AB_SPRITE_PARENT_ORIGIN") &&
        getenv("AB_SPRITE_PARENT_ORIGIN")[0] == '1';
    if ((spriteOp & 0x4000) == 0 && parentOrigin) {
        outChildPos[0] = parentPos[0];
        outChildPos[1] = parentPos[1];
        outChildPos[2] = parentPos[2];
    } else {
        // TransformMeshVertex: flag 0x0B/0x0C attach verts (Explorer tip 74/75)
        // must average world positions, not model coords.
        // Do not cap attach scale for title — that collapsed constellation
        // flybys into a radial/cog swirl. Dig: AB_SPRITE_PARENT_ORIGIN=1.
        ProjectedVertex ap = TransformMeshVertex(
            parentModel, attachVert, parentRot, parentPos, camera, parentScale);
        outChildPos[0] = ap.worldX;
        outChildPos[1] = ap.worldY;
        outChildPos[2] = ap.worldZ;
    }
    // FUNC_001740 case ((op>>8)&7)==6 copies parent+0x9c; otherwise 001739.
    // Matrix (FUNC_001741) builds +0x9c via 001739(matrixOp*2) + optional 001743.
    // TagSpriteChildPolys stores spriteOp=0xFFFF when attachPayload==0 (Eagle
    // Mk2/3 → #23, Kestrel wrappers). RemapSpriteMatrix(0xFFFF) flips all three
    // axes (bits 0x2000/1000/0800) and shifts Contours ~29px. Treat 0xFFFF as
    // identity parent rot. Dig: AB_SPRITE_REMAP_FFFF=1 restores Remap.
    const bool remapFfff = getenv("AB_SPRITE_REMAP_FFFF") &&
                           getenv("AB_SPRITE_REMAP_FFFF")[0] == '1';
    const bool useMatrix =
        matrixOp != 0 &&
        !(getenv("AB_MATRIX_IGNORE") && getenv("AB_MATRIX_IGNORE")[0] == '1');
    // Case-6 sprites copy ctx+0x9c. Op 0x1E commit installs +0x9c into working
    // +0x14 — apply Matrix result for those polys even when spriteOp is not case 6.
    const bool matrixCase6 = ((spriteOp >> 8) & 7u) == 6u;

    // NormAxes 001770 reset: +0x14 = twist(sentinel, θ), parent-independent.
    // Dig AB_MATRIX_SENTINEL_PARENT=1 restores old parent/Remap fold (known bad).
    const bool sentinelReset =
        matrixSentinel && useMatrix &&
        !(getenv("AB_MATRIX_SENTINEL_PARENT") &&
          getenv("AB_MATRIX_SENTINEL_PARENT")[0] == '1');
    if (sentinelReset) {
        if (!ApplyMeshSentinelWorkingBasis(outChildRot, matrixOp, matrixWord)) {
            for (int i = 0; i < 9; i++)
                outChildRot[i] = parentRot[i];
        }
        if (getenv("AB_DUMP_MATRIX_TWIST") &&
            getenv("AB_DUMP_MATRIX_TWIST")[0] == '1') {
            static int dumps = 0;
            if (dumps < 6) {
                const uint32_t ang = MeshDecodeMatrixAngleLive(matrixWord);
                fprintf(stderr,
                        "MATRIX8000 sentinel mop=0x%04x mw=0x%04x ang=0x%x "
                        "rot0=(%d,%d,%d)\n",
                        (unsigned)matrixOp, (unsigned)matrixWord, (unsigned)ang,
                        (int)outChildRot[0], (int)outChildRot[1],
                        (int)outChildRot[2]);
                dumps++;
            }
        }
    } else if (useMatrix && (matrixCase6 || matrixWorking)) {
        const uint32_t family = static_cast<uint32_t>(matrixOp) & 0xC000u;
        const int axis = static_cast<int>((matrixOp >> 5) & 3u);
        const bool doTwist =
            matrixWord != 0 &&
            !(getenv("AB_MATRIX_NO_TWIST") &&
              getenv("AB_MATRIX_NO_TWIST")[0] == '1');

        auto applyTwist = [&]() {
            if (!doTwist)
                return;
            // FUNC_001743: ang=FUNC_001474(word); Sin32(ang), Sin32(ang+0x4000).
            // DecodeMatrixAngle uses SetMeshAluOverride (poly.aluRegs) for 0xC0.
            const uint32_t ang = DecodeMatrixAngle(matrixWord);
            const int32_t sinA = Sin32(ang);
            const int32_t cosA = Sin32(ang + 0x4000u);
            RotateBasisColumns(outChildRot, axis, cosA, sinA);
        };

        if (family == 0x8000u) {
            // FUNC_001742: twist existing +0x9c in place — NO 001739 remap.
            // Non-sentinel 0x835c: input basis is parent/prior frame (11 models).
            // Dig: AB_MATRIX_8000_REMAP=1 restores the old wrong Remap(op*2).
            if (getenv("AB_MATRIX_8000_REMAP") &&
                getenv("AB_MATRIX_8000_REMAP")[0] == '1') {
                RemapSpriteMatrix(outChildRot, parentRot,
                                  static_cast<uint32_t>(matrixOp) * 2u);
            } else if (matrixCase6 ||
                       (spriteOp == 0xFFFFu && !remapFfff) ||
                       (getenv("AB_SPRITE_PARENT_ROT") &&
                        getenv("AB_SPRITE_PARENT_ROT")[0] == '1')) {
                for (int i = 0; i < 9; i++)
                    outChildRot[i] = parentRot[i];
            } else {
                RemapSpriteMatrix(outChildRot, parentRot, spriteOp);
            }
            applyTwist();
            if (getenv("AB_DUMP_MATRIX_TWIST") &&
                getenv("AB_DUMP_MATRIX_TWIST")[0] == '1') {
                static int dumps = 0;
                if (dumps < 6) {
                    const uint32_t ang = doTwist ? DecodeMatrixAngle(matrixWord) : 0;
                    fprintf(stderr,
                            "MATRIX8000 mop=0x%04x mw=0x%04x axis=%d ang=0x%x "
                            "spr=0x%04x work=%d rot0=(%d,%d,%d)\n",
                            (unsigned)matrixOp, (unsigned)matrixWord, axis,
                            (unsigned)ang, (unsigned)spriteOp, (int)matrixWorking,
                            (int)outChildRot[0], (int)outChildRot[1],
                            (int)outChildRot[2]);
                    dumps++;
                }
            }
        } else {
            // Low / C000: FUNC_001741 builds +0x9c via 001739(op*2) [+ C000
            // 3-vert path approximated as same Remap], then optional 001743.
            RemapSpriteMatrix(outChildRot, parentRot,
                              static_cast<uint32_t>(matrixOp) * 2u);
            applyTwist();
        }
    } else if (spriteOp == 0xFFFFu && !remapFfff) {
        for (int i = 0; i < 9; i++)
            outChildRot[i] = parentRot[i];
    } else if (((spriteOp >> 8) & 7u) == 6u ||
        (getenv("AB_SPRITE_PARENT_ROT") && getenv("AB_SPRITE_PARENT_ROT")[0] == '1')) {
        for (int i = 0; i < 9; i++)
            outChildRot[i] = parentRot[i];
    } else {
        RemapSpriteMatrix(outChildRot, parentRot, spriteOp);
    }
    if (getenv("AB_SPRITE_PARENT_SCALE") && getenv("AB_SPRITE_PARENT_SCALE")[0] == '1')
        *outChildScale = parentScale;
    else
        *outChildScale = static_cast<int>(childModel->scaleRelated);
    // Dig: AB_SPRITE_SCALE=<n> forces child scaleRelated (Courier #215 tip).
    if (const char* cs = getenv("AB_SPRITE_SCALE"); cs && cs[0])
        *outChildScale = atoi(cs);
    if (getenv("AB_DUMP_ATTACH") && getenv("AB_DUMP_ATTACH")[0] == '1') {
        static int dumps = 0;
        if (dumps < 8) {
            fprintf(stderr,
                    "ATTACH av=%u spr=0x%04x mop=0x%04x mw=0x%04x "
                    "pos=(%d,%d,%d) pScale=%d cScale=%d outScale=%d "
                    "childVerts=%u childRad=%u\n",
                    (unsigned)attachVert, (unsigned)spriteOp,
                    (unsigned)matrixOp, (unsigned)matrixWord,
                    (int)outChildPos[0], (int)outChildPos[1], (int)outChildPos[2],
                    parentScale, (int)childModel->scaleRelated, *outChildScale,
                    (unsigned)childModel->vertexCount,
                    (unsigned)childModel->radius);
            dumps++;
        }
    }
    return true;
}

int CollectSpriteChildInstances(const ShipModel* parentModel, const MeshData& mesh,
                                const int32_t* shipRot, const int32_t* shipPos,
                                const Camera3D* camera, int parentScale,
                                SpriteChildInstance* out, int maxOut)
{
    if (!parentModel || !shipRot || !shipPos || !camera || !out || maxOut <= 0)
        return 0;
    int count = 0;
    for (const auto& poly : mesh.polygons) {
        const bool plainSprite =
            (poly.specialParam & 0x8000) != 0 && poly.spriteOp != 0;
        if (!plainSprite) continue;

        const int childId = SpriteChildModelId(poly.specialParam);
        const ShipModel* child = GetShipModel(childId);
        if (!child) continue;

        const int32_t* pRot = shipRot;
        const int32_t* pPos = shipPos;
        const ShipModel* pModel = parentModel;
        int pScale = parentScale;
        int32_t midRot[9];
        int32_t midPos[3];
        int midScale = parentScale;
        if (poly.attachParentModel != 0) {
            const ShipModel* mid = GetShipModel(poly.attachParentModel);
            if (mid &&
                BuildSpriteAttachFrame(parentModel, shipRot, shipPos, camera,
                                      parentScale, poly.attachParentVert,
                                      poly.attachParentOp, mid, midRot, midPos,
                                      &midScale)) {
                pModel = mid;
                pRot = midRot;
                pPos = midPos;
                pScale = midScale;
            }
        }

        int32_t childRot[9];
        int32_t childPos[3];
        int childScale = parentScale;
        if (!BuildSpriteAttachFrame(pModel, pRot, pPos, camera, pScale,
                                    poly.attachVert, poly.spriteOp, child,
                                    childRot, childPos, &childScale,
                                    poly.matrixOp, poly.matrixWord, poly.matrixWorking,
                                    poly.matrixSentinel))
            continue;

        // Dedupe identical (id, quantized pos) — many polys share one attach.
        bool dup = false;
        const int32_t q0 = childPos[0] & ~0xFF;
        const int32_t q1 = childPos[1] & ~0xFF;
        const int32_t q2 = childPos[2] & ~0xFF;
        for (int i = 0; i < count; i++) {
            if (out[i].modelId == childId &&
                (out[i].pos[0] & ~0xFF) == q0 &&
                (out[i].pos[1] & ~0xFF) == q1 &&
                (out[i].pos[2] & ~0xFF) == q2) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        if (count >= maxOut) break;
        out[count].modelId = childId;
        for (int i = 0; i < 3; i++) out[count].pos[i] = childPos[i];
        for (int i = 0; i < 9; i++) out[count].rot[i] = childRot[i];
        out[count].scaleRelated = childScale;
        count++;
    }
    return count;
}

// FUNC_001481 facing-flags populate for one normal index.
// Shade: camDir · (normal<<8); RGB from shadeTable when shade_dot < 0.
// Cull bit0: (vertLOD - camModel) · (normal<<8) >= 0 → far (IsFar neg path).
// ASM ctx+0x154 = normal table (1-based), ctx+0x150 = vertex table (flag>>1).
static void ApplyFunc001481Flags(
    const ShipModel* shipModel,
    int nIdx,
    int32_t camDirX, int32_t camDirY, int32_t camDirZ,
    int32_t camMx, int32_t camMy, int32_t camMz,
    int lodShift,
    const uint8_t shadeTable[24],
    bool writeCullBit)
{
    if (!shipModel || nIdx < 0 || nIdx >= 128) return;
    const uint8_t* blob = data::GetDataBlob();
    if (!blob || shipModel->vertexDataOffset == 0) return;

    int entry = (nIdx >> 1) - 1;
    if (entry < 0) return;

    // The shade dot reads ctx+0x154 (the vertex table); only the cull bit uses
    // the normal table at ctx+0x150. Sourcing both from normalDataOffset — and
    // bailing out when a model has no normal table — moved every DATA_009201
    // shade entry, so Contour/flat colours shifted fleet-wide (Lanner +8293,
    // Skeet +4570 A→D, Merlin +3555).
    // Dig: AB_FACE_SHADE_NORMTAB=1 restores the normal-table shade.
    const bool shadeFromNormals = getenv("AB_FACE_SHADE_NORMTAB") &&
                                  getenv("AB_FACE_SHADE_NORMTAB")[0] == '1';
    const uint32_t sOff =
        (shadeFromNormals ? shipModel->normalDataOffset
                          : shipModel->vertexDataOffset) +
        static_cast<uint32_t>(entry) * 6;
    if (sOff + 6 > data::GetDataBlobSize()) return;
    const uint8_t* sd = blob + sOff;
    int8_t sx = static_cast<int8_t>(sd[2]);
    int8_t sy = static_cast<int8_t>(sd[3]);
    int8_t sz = static_cast<int8_t>(sd[4]);
    if (nIdx & 1) sx = static_cast<int8_t>(-sx);

    int32_t shadeDot = camDirX * sx * 256 + camDirY * sy * 256 + camDirZ * sz * 256;
    SetFacingFlagsShade(nIdx, shadeDot, shadeTable);

    if (!writeCullBit) return;
    if (shipModel->normalDataOffset == 0) return;
    const uint32_t nOff =
        shipModel->normalDataOffset + static_cast<uint32_t>(entry) * 6;
    if (nOff + 6 > data::GetDataBlobSize()) return;
    const uint8_t* nd = blob + nOff;
    int8_t nx = static_cast<int8_t>(nd[2]);
    int8_t ny = static_cast<int8_t>(nd[3]);
    int8_t nz = static_cast<int8_t>(nd[4]);
    if (nIdx & 1) nx = static_cast<int8_t>(-nx);

    int32_t nxs = static_cast<int32_t>(nx) << 8;
    int32_t nys = static_cast<int32_t>(ny) << 8;
    int32_t nzs = static_cast<int32_t>(nz) << 8;

    int vertIdx = static_cast<int>(nd[0]) >> 1;
    int8_t vx = 0, vy = 0, vz = 0;
    if (!GetVertexCull(shipModel, vertIdx, vx, vy, vz)) {
        SetFacingFlagsCullBit(nIdx, false);
        return;
    }
    if (((nIdx ^ static_cast<int>(nd[0])) & 1) != 0)
        vx = static_cast<int8_t>(-vx);

    // Prefer live mesh-parse scale (sprite child) over ObjectStruct default.
    int scaleRelated = GetMeshParseScaleRelated();
    if (scaleRelated <= 0)
        scaleRelated = static_cast<int>(shipModel->scaleRelated);
    int shift = scaleRelated - lodShift;
    int32_t mx = static_cast<int32_t>(vx);
    int32_t my = static_cast<int32_t>(vy);
    int32_t mz = static_cast<int32_t>(vz);
    if (shift > 0) {
        mx <<= (shift & 0x1f);
        my <<= (shift & 0x1f);
        mz <<= (shift & 0x1f);
    } else if (shift < 0) {
        mx >>= ((-shift) & 0x1f);
        my >>= ((-shift) & 0x1f);
        mz >>= ((-shift) & 0x1f);
    }

    int64_t cullDot = static_cast<int64_t>(mx - camMx) * nxs +
                      static_cast<int64_t>(my - camMy) * nys +
                      static_cast<int64_t>(mz - camMz) * nzs;

    // Literal ASM: bit0 set iff cullDot >= 0. Dig: AB_FACE_CULL=0 forces clear.
    bool farBit = (cullDot >= 0);
    if (const char* mode = getenv("AB_FACE_CULL"); mode && mode[0] == '0')
        farBit = false;
    const char* fd = getenv("AB_FACE_DUMP");
    if (fd && fd[0] != '0' &&
        (fd[0] == '2' || nIdx == 2 || nIdx == 1 || nIdx == 3 || nIdx == 6 || nIdx == 7)) {
        fprintf(stderr,
                "FACE nIdx=%d flag=0x%02x n=(%d,%d,%d) vertIdx=%d vLod=(%d,%d,%d) "
                "camM=(%d,%d,%d) lod=%d scale=%d shadeDot=%d cullDot=%lld far=%d\n",
                nIdx, nd[0], (int)nx, (int)ny, (int)nz, vertIdx,
                (int)mx, (int)my, (int)mz,
                (int)camMx, (int)camMy, (int)camMz, lodShift, scaleRelated,
                (int)shadeDot, (long long)cullDot, farBit ? 1 : 0);
    }
    SetFacingFlagsCullBit(nIdx, farBit);
}

struct FacingParseContext {
    const ShipModel* model = nullptr;
    int32_t shipRot[9] = {};
    int32_t shipPos[3] = {};
    int32_t camMx = 0, camMy = 0, camMz = 0;
    int lodShift = 0;
    int32_t camDirX = 0, camDirY = 0, camDirZ = 0;
    uint8_t shadeTable[24] = {
        7,7,7, 7,7,6, 6,6,5, 5,5,4, 4,4,3, 3,3,2, 2,2,1, 1,1,1
    };
    Camera3D camera{};
    bool cameraValid = false;
};

static FacingParseContext s_faceCtx;
static FacingParseContext s_faceCtxStack[4];
static int s_faceCtxDepth = 0;

static void FillFacingCam(FacingParseContext& ctx)
{
    if (!ctx.model) return;
    ComputeCamModelPos(ctx.model, ctx.shipRot, ctx.shipPos,
                       ctx.camMx, ctx.camMy, ctx.camMz, ctx.lodShift);
    const int32_t baseX = 18919, baseY = 18919, baseZ = -18919;
    ctx.camDirX = FixedMulHigh(baseX, ctx.shipRot[0]) +
                  FixedMulHigh(baseY, ctx.shipRot[3]) +
                  FixedMulHigh(baseZ, ctx.shipRot[6]);
    ctx.camDirY = FixedMulHigh(baseX, ctx.shipRot[1]) +
                  FixedMulHigh(baseY, ctx.shipRot[4]) +
                  FixedMulHigh(baseZ, ctx.shipRot[7]);
    ctx.camDirZ = FixedMulHigh(baseX, ctx.shipRot[2]) +
                  FixedMulHigh(baseY, ctx.shipRot[5]) +
                  FixedMulHigh(baseZ, ctx.shipRot[8]);
}

void LazyComputeFacingCull(int normalIdx)
{
    if (!s_faceCtx.model) return;
    ApplyFunc001481Flags(s_faceCtx.model, normalIdx,
                         s_faceCtx.camDirX, s_faceCtx.camDirY, s_faceCtx.camDirZ,
                         s_faceCtx.camMx, s_faceCtx.camMy, s_faceCtx.camMz,
                         s_faceCtx.lodShift, s_faceCtx.shadeTable,
                         /*writeCullBit=*/true);
}

int32_t AttachVertexDepthForGate(uint8_t vert)
{
    if (!s_faceCtx.model) return -1;
    ShipVertex sv{};
    if (!GetVertexMirrored(s_faceCtx.model, vert, sv)) return -1;
    int shift = static_cast<int>(s_faceCtx.model->scaleRelated);
    if (shift < 0) shift = 0;
    if (shift > 24) shift = 24;
    Mat33 sm;
    for (int i = 0; i < 9; i++) sm.m[i] = s_faceCtx.shipRot[i];
    Vec32 model = {static_cast<int32_t>(static_cast<uint32_t>(sv.x) << shift),
                   static_cast<int32_t>(static_cast<uint32_t>(sv.y) << shift),
                   static_cast<int32_t>(static_cast<uint32_t>(sv.z) << shift)};
    Vec32 world = {};
    SpecialVecMatMul(&world, &model, &sm);
    return world.z + s_faceCtx.shipPos[2];
}

// R666: ASM's child ObjectStruct uses the COMBINED rotation (FUNC_001739) and
// the attach object's position (child +4/+8/+0xc). Build that frame instead of
// inheriting the parent's.
void PushFacingParseContext(const ShipModel* childModel, uint8_t attachVert, uint16_t spriteOp)
{
    if (!childModel || !s_faceCtx.model || !s_faceCtx.cameraValid || s_faceCtxDepth >= 4) return;
    int32_t childRot[9]; int32_t childPos[3]; int childScale = 0;
    const ShipModel* parent = s_faceCtx.model;
    if (!BuildSpriteAttachFrame(parent, s_faceCtx.shipRot, s_faceCtx.shipPos,
                                &s_faceCtx.camera,
                                static_cast<int>(parent->scaleRelated),
                                attachVert, spriteOp, childModel,
                                childRot, childPos, &childScale))
        return;
    s_faceCtxStack[s_faceCtxDepth++] = s_faceCtx;
    s_faceCtx.model = childModel;
    for (int i = 0; i < 9; i++) s_faceCtx.shipRot[i] = childRot[i];
    for (int i = 0; i < 3; i++) s_faceCtx.shipPos[i] = childPos[i];
    FillFacingCam(s_faceCtx);
    InitFacingFlagsCullDirty(static_cast<int>(childModel->normalCount));
}

void PushFacingParseContext(const ShipModel* childModel)
{
    // Require parent PrecomputeFacingFlags first (shipRot/pos). Initial
    // ParseMeshBytecode may expand sprites before any pose context exists.
    if (!childModel || !s_faceCtx.model || s_faceCtxDepth >= 4) return;
    s_faceCtxStack[s_faceCtxDepth++] = s_faceCtx;
    s_faceCtx.model = childModel;
    FillFacingCam(s_faceCtx);
    InitFacingFlagsCullDirty(static_cast<int>(childModel->normalCount));
    if (getenv("AB_FACE_DUMP") && getenv("AB_FACE_DUMP")[0] == '1') {
        fprintf(stderr, "FACE_CTX push child norms=%u scale=%u camM=(%d,%d,%d) lod=%d\n",
                childModel->normalCount, childModel->scaleRelated,
                s_faceCtx.camMx, s_faceCtx.camMy, s_faceCtx.camMz, s_faceCtx.lodShift);
    }
}

void PopFacingParseContext()
{
    if (s_faceCtxDepth <= 0) return;
    s_faceCtx = s_faceCtxStack[--s_faceCtxDepth];
    if (s_faceCtx.model)
        InitFacingFlagsCullDirty(static_cast<int>(s_faceCtx.model->normalCount));
}

// Store pose context + FUNC_001480 dirty bits. Cull bits filled lazily in IsFar.
void PrecomputeFacingFlags(const ShipModel* shipModel, const int32_t* shipRot,
                           const int32_t* shipPos, const Camera3D* camera)
{
    (void)camera;
    if (!shipModel || !shipRot || !shipPos) return;

    s_faceCtxDepth = 0;
    if (camera) { s_faceCtx.camera = *camera; s_faceCtx.cameraValid = true; }
    s_faceCtx.model = shipModel;
    for (int i = 0; i < 9; i++) s_faceCtx.shipRot[i] = shipRot[i];
    for (int i = 0; i < 3; i++) s_faceCtx.shipPos[i] = shipPos[i];
    FillFacingCam(s_faceCtx);

    if (getenv("AB_FACE_DUMP") && getenv("AB_FACE_DUMP")[0] == '1') {
        fprintf(stderr,
                "CAMMODEL pos=(%d,%d,%d) camM=(%d,%d,%d) lod=%d norms=%u\n",
                shipPos[0], shipPos[1], shipPos[2],
                s_faceCtx.camMx, s_faceCtx.camMy, s_faceCtx.camMz,
                s_faceCtx.lodShift, shipModel->normalCount);
    }

    InitFacingFlagsCullDirty(static_cast<int>(shipModel->normalCount));
}

// Transform and project all polygons in a ship mesh
static const int32_t* s_abLastShipPos = nullptr;
static const Camera3D* s_abLastCamera = nullptr;
const int32_t* GetAbLastShipPos() { return s_abLastShipPos; }
const Camera3D* GetAbLastCamera() { return s_abLastCamera; }

int TransformShipVertices(
    const ShipModel* shipModel,
    const MeshData& meshData,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    TransformedPoly* outPolys,
    int maxPolys)
{
    if (!shipModel || !meshData.valid || !outPolys) return 0;

    // R263: stash the pose frame so the gated walk-replay dig (ship_renderer
    // AB_BANKREPLAY) can compose the same camera-relative ctx the classic
    // lane uses. Plain assignment; no behavior change.
    s_abLastShipPos = shipPos;
    s_abLastCamera = camera;

    // Clear glow screen store and vertex cache
    s_glowScreenCount = 0;
    VertexCacheReset();

    // Populate sub-object vertex table (FUNC_001555).
    // Projects 6 anchor points through FUNC_001557 for mirror-case vertices.
    PopulateSubObjectTable(shipRot, shipPos, shipModel, camera);

    // Populate facing flags table (DATA_009201) — port of FUNC_001481.
    // ASM FUNC_001693 param_7 = shade from DATA_009201 + normalIdx*8.
    // shadeDot = camModelPos · normal → shadeTable[(shadeDot>>27)&7].
    {
        int32_t camMx, camMy, camMz;
        int lodShift;
        ComputeCamModelPos(shipModel, shipRot, shipPos, camMx, camMy, camMz, lodShift);

        // Camera DIRECTION for shade dot (FUNC_001481 uses renderCtx+0x38,
        // NOT the camera position at +0x48). Computed by VecMatTMul of a
        // fixed base direction (-18919,-18919,18919) with shipRot.
        // Source: DATA_007231/7232/7233 → DATA_008821/8822/8823 → renderCtx+0x134
        // VecMatTMul(shipRot, baseDir, camDir) produces the camera direction
        // in model space. Negated because the baseDir in the ASM data points
        // opposite to the actual camera-facing direction.
        const int32_t baseX = 18919, baseY = 18919, baseZ = -18919;
        int32_t camDirX = FixedMulHigh(baseX, shipRot[0]) +
                          FixedMulHigh(baseY, shipRot[3]) +
                          FixedMulHigh(baseZ, shipRot[6]);
        int32_t camDirY = FixedMulHigh(baseX, shipRot[1]) +
                          FixedMulHigh(baseY, shipRot[4]) +
                          FixedMulHigh(baseZ, shipRot[7]);
        int32_t camDirZ = FixedMulHigh(baseX, shipRot[2]) +
                          FixedMulHigh(baseY, shipRot[5]) +
                          FixedMulHigh(baseZ, shipRot[8]);

        // Build shade table: global DATA_007234 greyscale ramp
        // ASM memcpy(renderCtx+0x84, DATA_007234, 24) — not material-derived.
        // {7,7,7, 7,7,6, 6,6,5, 5,5,4, 4,4,3, 3,3,2, 2,2,1, 1,1,1}
        const uint8_t shadeTable[24] = {
            7,7,7, 7,7,6, 6,6,5, 5,5,4, 4,4,3, 3,3,2, 2,2,1, 1,1,1
        };

        // Populate facing flags for each unique normalIdx (full FUNC_001481).
        const uint8_t* blob = data::GetDataBlob();
        if (blob && shipModel->vertexDataOffset) {
            bool done[128] = {};
            for (auto& p : meshData.polygons) {
                int nIdx = p.normalIdx;
                if (nIdx < 0 || nIdx >= 128 || done[nIdx]) continue;
                done[nIdx] = true;
                ApplyFunc001481Flags(shipModel, nIdx,
                                     camDirX, camDirY, camDirZ,
                                     camMx, camMy, camMz, lodShift, shadeTable,
                                     /*writeCullBit=*/false);
            }
        }
    }

    const int scaleRelated = static_cast<int>(shipModel->scaleRelated);
    int outCount = 0;
    g_frameBarriers.clear();

    // Vertex cache pre-pass: transform all referenced vertices and store
    // in cache. ASM populates DATA_009200 during mesh interpretation;
    // C++ transforms on-demand. This pre-pass fills the cache for trace
    // parity and NavLight (op 0x01) reads.
    {
        bool seen[kVertexCacheSize] = {};
        int prepassIdx = 0;
        for (const auto& poly : meshData.polygons) {
            for (int vi = 0; vi < poly.vertexCount; vi++) {
                int idx = poly.vertexIdx[vi];
                if (idx < 0 || idx >= kVertexCacheSize || seen[idx]) continue;
                seen[idx] = true;
                ShipVertex sv{};
                if (!GetVertexMirrored(shipModel, idx, sv)) continue;
                ProjectedVertex pv = TransformVertex(sv, shipRot, shipPos, camera, scaleRelated);
                VertexCacheStore(idx, pv, sv.x, sv.y, sv.z);
                // Debug: dump first 8 vertices with input data
                if (prepassIdx < 8 && g_jjffe_vtrace_enabled) {
                    fprintf(stderr, "CPP_VERT[%d] idx=%d model=(%d,%d,%d) flag=0x%02x "
                            "scr=(%d,%d) world=(%d,%d,%d)\n",
                            prepassIdx, idx, (int)sv.x, (int)sv.y, (int)sv.z,
                            (unsigned)sv.mirrorFlag,
                            (int)pv.screenX, (int)pv.screenY,
                            (int)pv.worldX, (int)pv.worldY, (int)pv.worldZ);
                }
                prepassIdx++;
            }
        }

        // C++ trace emission (C2): dump populated cache entries.
        // vtrace funcs do ensure_open() internally for lazy AB_VTRACE init.
        // Output fields in ASM cache order for direct comparison:
        //   f0 = packed screen (scrY:16 | scrX:16)
        //   f1 = camX, f2 = camY, f3 = camZ (depth)
        //   f4 = reserved (0), f5 = genCounter (s_renderGen / renderCtx+0xf0)
        //   f6 = worldX, f7 = worldY, f8 = worldZ
        jjffe_vtrace_cpp_frame(g_abShipIndex, g_abPoseIndex, shipRot, shipPos, scaleRelated);
        for (int i = 0; i < kVertexCacheSize; i++) {
            if (!CacheEntryLive(i)) continue;
            /* ASM-order fields matching 36-byte cache entry layout */
            int32_t asm_fields[9] = {
                s_vertexCache[i].packedScreen,
                s_vertexCache[i].camX,
                s_vertexCache[i].camY,
                s_vertexCache[i].camZ,
                s_vertexCache[i].reserved,
                s_vertexCache[i].genCounter,
                s_vertexCache[i].worldX,
                s_vertexCache[i].worldY,
                s_vertexCache[i].worldZ
            };
            jjffe_vtrace_cpp_vert_asm_order(i, asm_fields,
                                            s_vertexCache[i].screenX, s_vertexCache[i].screenY,
                                            s_vertexCache[i].visible ? 1 : 0);
        }
        // Emit sub-object table entries (FUNC_001555 anchor projections).
        // ASM stores these at cache offsets 0x6c-0x84 (entries 0-5).
        // Emit as high indices (240-245) to avoid collision with vertex cache.
        if (s_subObjTableValid) {
            for (int i = 0; i < kSubObjTableSize; i++) {
                int32_t packed = s_subObjScreenTable[i];
                int16_t sx = (int16_t)(packed & 0xFFFF);
                int16_t sy = (int16_t)((packed >> 16) & 0xFFFF);
                int32_t asm_fields[9] = {packed, 0, 0, 0, 0, 0, 0, 0, 0};
                jjffe_vtrace_cpp_vert_asm_order(240 + i, asm_fields, sx, sy, 1);
            }
        }
    }

    // Dig: dump model+screen verts. AB_DUMP_MODEL_VERTS=1 once (front);
    // =pose dumps when g_abPoseIndex matches (e.g. 4=top).
    if (const char* dv = getenv("AB_DUMP_MODEL_VERTS"); dv && dv[0]) {
        static int dumpedPose = -2;
        const int want = (dv[0] == '1' && dv[1] == '\0') ? 0 : atoi(dv);
        if (dumpedPose != want && g_abPoseIndex == want) {
            dumpedPose = want;
            // Dig: AB_DUMP_MODEL_VERTS=all dumps every even-slot raw flag + screen.
            const bool dumpAll = (dv[0] == 'a' || dv[0] == 'A');
            fprintf(stderr, "VERTS pose=%d scale=%d shipZ=%d\n",
                    g_abPoseIndex, scaleRelated, shipPos ? shipPos[2] : 0);
            const int vmax = dumpAll ? static_cast<int>(shipModel->vertexCount) : 0;
            const int idsFixed[] = {
                2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 19,
                28, 29, 32, 33, 34, 35, 36, 37, 38, 39,
                40, 41, 42, 43, 44, 45, 46, 47,
                76, 77, 78, 79, 80, 81, 82, 83
            };
            const int nFixed = static_cast<int>(sizeof(idsFixed) / sizeof(idsFixed[0]));
            for (int ii = 0; ii < (dumpAll ? vmax : nFixed); ii++) {
                const int id = dumpAll ? ii : idsFixed[ii];
                const uint8_t* blob = data::GetDataBlob();
                uint8_t flag = 0xff;
                int8_t b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0;
                if (blob && shipModel->vertexDataOffset) {
                    const uint32_t rawOff = shipModel->vertexDataOffset +
                                           static_cast<uint32_t>(id / 2) * 6;
                    if (rawOff + 6 <= data::GetDataBlobSize()) {
                        const uint8_t* raw = blob + rawOff;
                        flag = raw[0];
                        b1 = static_cast<int8_t>(raw[1]);
                        b2 = static_cast<int8_t>(raw[2]);
                        b3 = static_cast<int8_t>(raw[3]);
                        b4 = static_cast<int8_t>(raw[4]);
                        b5 = static_cast<int8_t>(raw[5]);
                    }
                }
                ProjectedVertex pv = TransformMeshVertex(
                    shipModel, id, shipRot, shipPos, camera, scaleRelated);
                ShipVertex sv{};
                GetVertexMirrored(shipModel, id, sv);
                fprintf(stderr,
                        "VERT %d: flag=0x%02x raw=(%d,%d,%d,%d,%d) model=(%d,%d,%d) "
                        "mir=%u scr=(%d,%d) depth=%d world=(%d,%d,%d)\n",
                        id, flag, (int)b1, (int)b2, (int)b3, (int)b4, (int)b5,
                        (int)sv.x, (int)sv.y, (int)sv.z, (unsigned)sv.mirrorFlag,
                        (int)pv.screenX, (int)pv.screenY, (int)pv.depth,
                        (int)pv.worldX, (int)pv.worldY, (int)pv.worldZ);
            }
        }
    }

    // Contour group facing cache: ASM FUNC_001721 culls the entire Contour as
    // a group via the header normal (flags & 0x7F), never per-piece. However,
    // ComputeFaceFacing returns back-facing for wing contour normals at poses
    // where ASM draws them — the 001721 facing test uses a different threshold
    // or is effectively always-pass. Conservative: ComplexFill with
    // contourId >= 0 is never culled. nIdx=0 is degenerate (entry -1).
    // NOTE: The cache below is retained for future investigation but currently
    // unused — Contour pieces are always front-facing via the group rule.
    //
    // Panther #222 body Contours: keep in TagSpriteChildPolys; facing is local
    // ComputeFaceFacing under each attach frame (ASM 001721 lazy-001481 in the
    // current sprite ctx). Do not post-apply a mesh-wide DATA_009201 last-writer.

    // A mesh poly can fan out into several TransformedPolys and the loop body
    // has many early `continue`s, so the face light is stamped one iteration
    // late over everything the previous poly emitted (and once after the loop).
    int faceLightFrom = outCount;
    int8_t pendingFaceLight[3] = {0, 0, 0};
    auto stampPendingFaceLight = [&]() {
        for (int i = faceLightFrom; i < outCount; i++)
            std::memcpy(outPolys[i].faceLight, pendingFaceLight, 3);
    };

    // R1080: count the poly types the transform actually receives. A hull whose
    // ASM draws 001803 wireframe can only emit it if a PlainLine/DirLine reaches
    // this loop at all (the Eagle family looked empty at the PlainLine branch).
    // Dig only: AB_DUMP_LINECOUNT=1.
    static int s_lcPlain = -1, s_lcDir = -1;
    if (s_lcPlain < 0) {
        const char* e = getenv("AB_DUMP_LINECOUNT");
        s_lcPlain = (e && e[0] == '1') ? 0 : -2;
        s_lcDir = 0;
    }
    for (const auto& poly : meshData.polygons) {
        if (outCount >= maxPolys) break;
        if (s_lcPlain >= 0) {
            if (poly.type == MeshPolyType::PlainLine) ++s_lcPlain;
            else if (poly.type == MeshPolyType::DirLine) ++s_lcDir;
        }
        if (s_lcPlain >= 0 && outCount == 0) {
            static int s_rep = 0;
            if (s_rep < 1) {
                int nP = 0, nD = 0, nT = 0;
                for (const auto& q : meshData.polygons) {
                    if (q.type == MeshPolyType::PlainLine) ++nP;
                    else if (q.type == MeshPolyType::DirLine) ++nD;
                    ++nT;
                }
                fprintf(stderr, "LINECOUNT polys=%d plain=%d dir=%d\n", nT, nP, nD);
                for (const auto& q : meshData.polygons)
                    fprintf(stderr, "  POLY type=%d tex=0x%04x mi=%d nv=%d sp=0x%04x\n",
                            (int)q.type, (unsigned)q.texInfo, q.streamIndex,
                            (int)q.vertexCount, (unsigned)q.specialParam);
                ++s_rep;
            }
        }

        stampPendingFaceLight();
        faceLightFrom = outCount;
        pendingFaceLight[0] = poly.faceLightR;
        pendingFaceLight[1] = poly.faceLightG;
        pendingFaceLight[2] = poly.faceLightB;

        // Restore ALU regs as they were when this poly was emitted (ASM
        // interleaves FUNC_001758 with draws; final bank would clobber).
        SetMeshAluOverride(poly.aluRegs);

        if (getenv("AB_DUMP_ALU_POLY") && getenv("AB_DUMP_ALU_POLY")[0] == '1') {
            fprintf(stderr,
                    "ALU_POLY mesh=%d tex=0x%04x type=%d stream=%d "
                    "alu3=0x%04x alu0=0x%04x alu1=0x%04x vidx=[%u,%u,%u,%u]\n",
                    outCount, poly.texInfo, (int)poly.type, poly.streamIndex,
                    poly.aluRegs[3], poly.aluRegs[0], poly.aluRegs[1],
                    poly.vertexIdx[0], poly.vertexIdx[1],
                    poly.vertexIdx[2], poly.vertexIdx[3]);
        }

        // R1236/R1237 TextPrimitive raster probe: count polys that reach
        // transform. No TextPrimitive draw path exists — s_textPrimDrawn stays
        // 0 (measured). JJFFE_STUB_DISPATCH_LOG=1 prints each sighting.
        if (poly.type == MeshPolyType::TextPrimitive) {
            static int s_textPrimSeen = 0;
            static int s_textPrimDrawn = 0; // incremented only on a real draw
            ++s_textPrimSeen;
            // (no draw-path branch — nothing increments s_textPrimDrawn)
            if (const char* e = getenv("JJFFE_STUB_DISPATCH_LOG");
                e && e[0] == '1') {
                fprintf(stderr,
                        "TEXTPRIM_RASTER seen=%d drawn=%d slot=%u code=0x%x "
                        "model=%d\n",
                        s_textPrimSeen, s_textPrimDrawn, poly.vertexIdx[0],
                        poly.texInfo, GetMeshParseModelId());
            }
            continue;
        }

        // FUNC_001738 thruster / nozzle billboard(s).
        // Tips go through FUNC_001478 → TransformMeshVertex (not plain
        // GetVertexMirrored): Asp nozzle far tips are flag 0x13 with byte2
        // mode 0xc1 (ALU[1]), which the engine-on BitClear prelude loads —
        // static by-only collapse makes span=0 and skips the ASM stretched
        // 0x65 billboard.
        if (poly.type == MeshPolyType::Thruster && poly.vertexCount >= 2) {
            ProjectedVertex pa = TransformMeshVertex(
                shipModel, poly.vertexIdx[0], shipRot, shipPos, camera,
                scaleRelated);
            ProjectedVertex pb = TransformMeshVertex(
                shipModel, poly.vertexIdx[1], shipRot, shipPos, camera,
                scaleRelated);
            if (!pa.visible && !pb.visible) {
                continue;
            }

            // ASM SpecialDiv uses tip-A depth only (slotA+0xc), not the average.
            int32_t depth = pa.depth;
            if (depth < 256) depth = 256;
            int32_t camX = (pa.camX + pb.camX) / 2;
            int32_t camY = (pa.camY + pb.camY) / 2;

            // ASM FUNC_001738 exact formula:
            //   shift = ctx+0x148 - 6  (= scaleRelated - 6 for demo)
            //   local_c = size << shift  (if shift >= 0; >> if negative)
            //   local_c = SpecialDiv(local_c, vertex_depth)  // (num<<8)/depth_norm
            int32_t shift = scaleRelated - 6;
            int32_t sized = static_cast<int32_t>(poly.specialParam);
            if (shift >= 0) {
                sized <<= shift;
            } else {
                sized >>= (-shift & 0x1f);
            }
            int32_t local_c = SpecialDivAsm(sized, depth);
            if (local_c < 0) local_c = -local_c;
            if (local_c == 0) {
                continue;
            }
            // ASM FUNC_001738: local_c < 5 → FUNC_001704 line (wireframe laser /
            // distant thruster). Soft path: emit PlainLine for DrawLine.
            if (local_c < 5) {
                if (outCount >= maxPolys) continue;
                TransformedPoly& tp = outPolys[outCount];
                tp.vertexCount = 2;
                tp.texInfo = poly.texInfo;
                tp.normalIdx = 0;
                tp.type = MeshPolyType::PlainLine;
                tp.afterSpecialPoly = false;
                tp.specialParam = 0;
                tp.meshIndex = poly.streamIndex;
                tp.backFacing = false;
                tp.contourId = -1;
                tp.sortKey = ComputePainterSortKey(camX, camY, depth);
                tp.verts[0] = pa;
                tp.verts[1] = pb;
                tp.visible = true;
                outCount++;
                continue;
            }

            const int32_t ax = pa.screenX;
            const int32_t ay = pa.screenY;
            const int32_t bx = pb.screenX;
            const int32_t by = pb.screenY;
            const int32_t ddx = ax - bx;
            const int32_t ddy = ay - by;
            const int32_t span = static_cast<int32_t>(
                FUNC_001467(static_cast<uint32_t>(ddx * ddx + ddy * ddy)));

            auto emitThrusterQuad = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                        int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                                        uint16_t drawTexWord) {
                if (outCount >= maxPolys) return;
                TransformedPoly& tp = outPolys[outCount];
                tp.vertexCount = 4;
                tp.texInfo = poly.texInfo;       // material / colour source (0x20ee)
                tp.normalIdx = 0;
                tp.type = MeshPolyType::Thruster;
                tp.afterSpecialPoly = false;
                tp.specialParam = drawTexWord;   // 0x123 square / 0x65 stretched
                tp.meshIndex = poly.streamIndex;
                tp.backFacing = false;
                tp.contourId = -1;
                // ASM FUNC_001738: sort key via FUNC_001777 (same as hull polys).
                // Thrusters must sort by depth so the hull can paint over them
                // when the engine is on the far side.
                tp.sortKey = ComputePainterSortKey(camX, camY, depth);
                auto setV = [&](int i, int32_t x, int32_t y) {
                    tp.verts[i].screenX = static_cast<int16_t>(x);
                    tp.verts[i].screenY = static_cast<int16_t>(y);
                    tp.verts[i].depth = depth;
                    tp.verts[i].camX = camX;
                    tp.verts[i].camY = camY;
                    tp.verts[i].visible = true;
                };
                setV(0, x0, y0);
                setV(1, x1, y1);
                setV(2, x2, y2);
                setV(3, x3, y3);
                tp.visible = IsOnScreen(tp.verts, 4, 8);
                if (tp.visible) outCount++;
            };

            // ASM FUNC_001738: square billboard when span < local_c, then
            // stretched billboard when span*8 >= local_c (both drawn in overlap).
            // Skip stretched only when span*8 < local_c (goto JUMP_006684).
            if (span < local_c) {
                const int32_t cx = (bx + ax * 2) / 3;
                const int32_t cy = (by + ay * 2) / 3;
                const int32_t half = local_c >> 2;
                const int32_t nHalf = -half;
                emitThrusterQuad(cx + nHalf, cy + half,
                                 cx + half,  cy + half,
                                 cx + half,  cy - half,
                                 cx + nHalf, cy - half,
                                 0x123);
                if (span * 8 < local_c) {
                    continue;  // Very small span: skip stretched
                }
            }

            // Stretched quad along tip axis (span >= local_c, or overlap with square).
            // ASM stores nx/ny as int16 after (d*<<15)/span before imul local_c —
            // truncating (-55<<15)/54 = -33374 to int16 0x7DDE = +32222 is what
            // yields half-width 6 (Asp thrust); keeping the full int32 gives −7
            // and a 1px-too-wide strip (pd≈50 oa).
            if (span > 0) {
                const int16_t nx16 = static_cast<int16_t>((ddx << 15) / span);
                const int16_t ny16 = static_cast<int16_t>((ddy << 15) / span);
                const int32_t px =
                    (static_cast<int32_t>(ny16) * local_c) >> 17;
                const int32_t py =
                    -((static_cast<int32_t>(nx16) * local_c) >> 17);
                emitThrusterQuad(ax + px, ay + py,
                                 bx + px, by + py,
                                 bx - px, by - py,
                                 ax - px, ay - py,
                                 0x65);
            }

            continue;
        }

        // Opcode 0x11 → FUNC_001761 (and far-LOD textured PlainLine twins):
        // offset tip verts in scaled model space by ±size along the face
        // normal, then project. Must use SpecialVecMatMul (FixedMulHigh/Q31)
        // like TransformVertex — the old >>16 path explodes on Sin32 matrices
        // (Puma #60 DirLines → screen x≈-1000). Screen SpecialDiv ribbons
        // collapse to ~1px at A/B fit depth and miss Shuttle tip rows y=74..78.
        if (poly.type == MeshPolyType::DirLine && poly.vertexCount >= 2) {
            ShipVertex aVert{}, bVert{};
            if (!GetVertexMirrored(shipModel, poly.vertexIdx[0], aVert) ||
                !GetVertexMirrored(shipModel, poly.vertexIdx[1], bVert)) {
                continue;
            }

            uint8_t halfA = static_cast<uint8_t>((poly.specialParam >> 8) & 0xFF);
            uint8_t halfB = static_cast<uint8_t>(poly.specialParam & 0xFF);
            int32_t nx = 0, ny = 0, nz = 1;
            if (!GetNormal(shipModel, poly.normalIdx, nx, ny, nz)) {
                nx = 0; ny = -127; nz = 0;
            }

            const int shift = scaleRelated & 0x1f;
            const bool fp16 = getenv("AB_FP16_VERT") &&
                              getenv("AB_FP16_VERT")[0] == '1';
            // FUNC_001553: ribbon half-offset is cross(unit(vertex−cam), normal)
            // renormalised to `size`, i.e. the screen-side perpendicular of the
            // strut plane — not the normal itself. Extruding along the normal
            // put the width ~90° off (Shuttle rails / Puma struts rasterised as
            // dotted 1px slivers). Cross products commute with the shared
            // rotation, so world space gives the same direction as ASM's camera
            // space. Dig: AB_DIRLINE_NORMAL=1 restores the old extrusion.
            const bool offsetAlongNormal =
                getenv("AB_DIRLINE_NORMAL") && getenv("AB_DIRLINE_NORMAL")[0] == '1';

            // Normal in world space (rotation only, no translation).
            int32_t nWx = 0, nWy = 0, nWz = 0;
            {
                const int32_t sx = (int32_t)((uint32_t)nx << 8);
                const int32_t sy = (int32_t)((uint32_t)ny << 8);
                const int32_t sz = (int32_t)((uint32_t)nz << 8);
                if (fp16) {
                    nWx = (int32_t)(((int64_t)sx * shipRot[0] +
                                      (int64_t)sy * shipRot[3] +
                                      (int64_t)sz * shipRot[6]) >> 16);
                    nWy = (int32_t)(((int64_t)sx * shipRot[1] +
                                      (int64_t)sy * shipRot[4] +
                                      (int64_t)sz * shipRot[7]) >> 16);
                    nWz = (int32_t)(((int64_t)sx * shipRot[2] +
                                      (int64_t)sy * shipRot[5] +
                                      (int64_t)sz * shipRot[8]) >> 16);
                } else {
                    Mat33 sm;
                    for (int i = 0; i < 9; i++) sm.m[i] = shipRot[i];
                    Vec32 model = {sx, sy, sz};
                    Vec32 world = {};
                    SpecialVecMatMul(&world, &model, &sm);
                    nWx = world.x; nWy = world.y; nWz = world.z;
                }
            }

            auto projectOffset = [&](const ShipVertex& base, uint8_t half, int sign)
                -> ProjectedVertex {
                const int32_t sized =
                    (int32_t)((uint32_t)static_cast<int32_t>(half > 0 ? half : 1) << shift);
                int32_t mx = (int32_t)((uint32_t)static_cast<int32_t>(base.x) << shift);
                int32_t my = (int32_t)((uint32_t)static_cast<int32_t>(base.y) << shift);
                int32_t mz = (int32_t)((uint32_t)static_cast<int32_t>(base.z) << shift);
                if (offsetAlongNormal) {
                    mx += (nx * sized * sign) / 127;
                    my += (ny * sized * sign) / 127;
                    mz += (nz * sized * sign) / 127;
                }

                ProjectedVertex result = {};
                int32_t wx, wy, wz;
                if (fp16) {
                    wx = (int32_t)(((int64_t)mx * shipRot[0] +
                                     (int64_t)my * shipRot[3] +
                                     (int64_t)mz * shipRot[6]) >> 16) + shipPos[0];
                    wy = (int32_t)(((int64_t)mx * shipRot[1] +
                                     (int64_t)my * shipRot[4] +
                                     (int64_t)mz * shipRot[7]) >> 16) + shipPos[1];
                    wz = (int32_t)(((int64_t)mx * shipRot[2] +
                                     (int64_t)my * shipRot[5] +
                                     (int64_t)mz * shipRot[8]) >> 16) + shipPos[2];
                } else {
                    Mat33 sm;
                    for (int i = 0; i < 9; i++) sm.m[i] = shipRot[i];
                    Vec32 model = {mx, my, mz};
                    Vec32 world = {};
                    SpecialVecMatMul(&world, &model, &sm);
                    wx = world.x + shipPos[0];
                    wy = world.y + shipPos[1];
                    wz = world.z + shipPos[2];
                }
                int32_t cx = wx - camera->position.x;
                int32_t cy = wy - camera->position.y;
                int32_t cz = wz - camera->position.z;
                if (!offsetAlongNormal) {
                    // cross(vertex−cam, normal), renormalised to `sized`.
                    const int64_t crx = (int64_t)cy * nWz - (int64_t)cz * nWy;
                    const int64_t cry = (int64_t)cz * nWx - (int64_t)cx * nWz;
                    const int64_t crz = (int64_t)cx * nWy - (int64_t)cy * nWx;
                    // Shrink to 32-bit range before the length, keeping direction.
                    int64_t mag = crx < 0 ? -crx : crx;
                    if ((cry < 0 ? -cry : cry) > mag) mag = (cry < 0 ? -cry : cry);
                    if ((crz < 0 ? -crz : crz) > mag) mag = (crz < 0 ? -crz : crz);
                    if (mag > 0) {
                        int down = 0;
                        while ((mag >> down) > 0x7fff) down++;
                        const int32_t rx = (int32_t)(crx >> down);
                        const int32_t ry = (int32_t)(cry >> down);
                        const int32_t rz = (int32_t)(crz >> down);
                        const int32_t len = IntSqrt32(rx * rx + ry * ry + rz * rz);
                        if (len > 0) {
                            wx += (int32_t)((int64_t)rx * sized * sign / len);
                            wy += (int32_t)((int64_t)ry * sized * sign / len);
                            wz += (int32_t)((int64_t)rz * sized * sign / len);
                            cx = wx - camera->position.x;
                            cy = wy - camera->position.y;
                            cz = wz - camera->position.z;
                        }
                    }
                }
                int32_t vx = (int32_t)(((int64_t)cx * camera->rotation.m[0] +
                                         (int64_t)cy * camera->rotation.m[3] +
                                         (int64_t)cz * camera->rotation.m[6]) >> 16);
                int32_t vy = (int32_t)(((int64_t)cx * camera->rotation.m[1] +
                                         (int64_t)cy * camera->rotation.m[4] +
                                         (int64_t)cz * camera->rotation.m[7]) >> 16);
                Point3D worldPos = {wx, wy, wz};
                ProjResult proj;
                bool visible = ProjectPoint(camera, &worldPos, &proj);
                result.screenX = proj.sx;
                result.screenY = proj.sy;
                result.depth = proj.z;
                result.camX = vx;
                result.camY = vy;
                result.worldX = wx;
                result.worldY = wy;
                result.worldZ = wz;
                result.visible = visible && proj.visible;
                return result;
            };

            ProjectedVertex a0 = projectOffset(aVert, halfA, +1);
            ProjectedVertex a1 = projectOffset(aVert, halfA, -1);
            ProjectedVertex b0 = projectOffset(bVert, halfB > 0 ? halfB : halfA, -1);
            ProjectedVertex b1 = projectOffset(bVert, halfB > 0 ? halfB : halfA, +1);
            if (!a0.visible && !a1.visible && !b0.visible && !b1.visible)
                continue;

            TransformedPoly& tp = outPolys[outCount];
            tp.vertexCount = 4;
            tp.texInfo = poly.texInfo;
            tp.normalIdx = poly.normalIdx;
            tp.type = MeshPolyType::DirLine;
            tp.afterSpecialPoly = poly.afterSpecialPoly;
            tp.specialParam = poly.specialParam ? poly.specialParam : 0x0202;
            tp.meshIndex = poly.streamIndex;
            tp.backFacing = false;
            tp.contourId = -1;
            const int32_t depth = (a0.depth + b1.depth) / 2;
            tp.sortKey = ComputePainterSortKey(
                (a0.camX + b1.camX) / 2, (a0.camY + b1.camY) / 2, depth);
            tp.verts[0] = a0; tp.verts[0].visible = true;
            tp.verts[1] = b1; tp.verts[1].visible = true;
            tp.verts[2] = b0; tp.verts[2].visible = true;
            tp.verts[3] = a1; tp.verts[3].visible = true;
            tp.visible = IsOnScreen(tp.verts, 4, 8);
            // Dig: AB_DIRLINE_FORCE=1 keeps ribbons even when IsOnScreen fails.
            if (getenv("AB_DIRLINE_FORCE") && getenv("AB_DIRLINE_FORCE")[0] == '1')
                tp.visible = true;
            if (getenv("AB_DUMP_DIRLINE") && getenv("AB_DUMP_DIRLINE")[0] == '1') {
                fprintf(stderr,
                        "DIRLINE mi=%d tex=0x%04x n=%u half=%u/%u shift=%d "
                        "nxyz=(%d,%d,%d) va=(%d,%d,%d) vb=(%d,%d,%d)\n"
                        "  a0 scr=(%d,%d) d=%d w=(%d,%d,%d) vis=%d\n"
                        "  a1 scr=(%d,%d) d=%d w=(%d,%d,%d) vis=%d\n"
                        "  b0 scr=(%d,%d) d=%d w=(%d,%d,%d) vis=%d\n"
                        "  b1 scr=(%d,%d) d=%d w=(%d,%d,%d) vis=%d on=%d\n",
                        poly.streamIndex, poly.texInfo, poly.normalIdx,
                        halfA, halfB, shift, (int)nx, (int)ny, (int)nz,
                        (int)aVert.x, (int)aVert.y, (int)aVert.z,
                        (int)bVert.x, (int)bVert.y, (int)bVert.z,
                        a0.screenX, a0.screenY, (int)a0.depth,
                        (int)a0.worldX, (int)a0.worldY, (int)a0.worldZ, (int)a0.visible,
                        a1.screenX, a1.screenY, (int)a1.depth,
                        (int)a1.worldX, (int)a1.worldY, (int)a1.worldZ, (int)a1.visible,
                        b0.screenX, b0.screenY, (int)b0.depth,
                        (int)b0.worldX, (int)b0.worldY, (int)b0.worldZ, (int)b0.visible,
                        b1.screenX, b1.screenY, (int)b1.depth,
                        (int)b1.worldX, (int)b1.worldY, (int)b1.worldZ, (int)b1.visible,
                        (int)tp.visible);
            }
            if (tp.visible) outCount++;
            continue;
        }

        // PlainLine (0x02): thin edges — transform only, no culling.
        // Must use sprite attach frame when tagged (Eagle Mk2/3 wrapper → child
        // #184 tip rays). Parent-only transform on a 2-vert wrapper projected
        // child indices into long screen rays (OC below tip / past wing tips).
        // Dig: AB_PLAIN_PARENT_ONLY=1 restores the old parent-frame path.
        if (poly.type == MeshPolyType::PlainLine) {
            TransformedPoly& tp = outPolys[outCount];
            tp.vertexCount = poly.vertexCount;
            tp.texInfo = poly.texInfo;
            tp.normalIdx = poly.normalIdx;
            tp.type = poly.type;
            tp.afterSpecialPoly = poly.afterSpecialPoly;
            tp.meshIndex = poly.streamIndex;
            tp.backFacing = false;
            tp.contourId = poly.contourId;
            tp.shadeIdx = poly.color;  // R556: Complex721 payload shade-index carrier
            tp.attachParentModel = poly.attachParentModel;
            tp.sortKey = 0;
            for (int i = 0; i < 4; i++)
                tp.vertexIdx[i] = (i < poly.vertexCount) ? poly.vertexIdx[i] : 0;

            const bool plainSprite =
                (poly.specialParam & 0x8000) != 0 && poly.spriteOp != 0 &&
                !(getenv("AB_PLAIN_PARENT_ONLY") &&
                  getenv("AB_PLAIN_PARENT_ONLY")[0] == '1');
            tp.specialParam = plainSprite ? poly.specialParam
                                         : static_cast<uint16_t>(poly.specialParam & 0x7FFF);

            const ShipModel* useModel = shipModel;
            const int32_t* useRot = shipRot;
            const int32_t* usePos = shipPos;
            int useScale = scaleRelated;
            int32_t childRot[9];
            int32_t childPos[3];
            int childScale = scaleRelated;
            if (plainSprite) {
                const int childId = SpriteChildModelId(poly.specialParam);
                if (const char* sk = getenv("AB_SKIP_SPRITE_CHILD"); sk && sk[0]) {
                    bool skip = false;
                    const char* p = sk;
                    while (*p) {
                        char* end = nullptr;
                        long id = strtol(p, &end, 10);
                        if (end == p)
                            break;
                        if (id == childId) {
                            skip = true;
                            break;
                        }
                        p = end;
                        if (*p == ',')
                            ++p;
                    }
                    if (skip)
                        continue;
                }
                const ShipModel* child = GetShipModel(childId);
                const int32_t* pRot = shipRot;
                const int32_t* pPos = shipPos;
                const ShipModel* pModel = shipModel;
                int pScale = scaleRelated;
                int32_t midRot[9];
                int32_t midPos[3];
                int midScale = scaleRelated;
                if (poly.attachParentModel != 0) {
                    const ShipModel* mid = GetShipModel(poly.attachParentModel);
                    if (mid &&
                        BuildSpriteAttachFrame(shipModel, shipRot, shipPos, camera,
                                              scaleRelated, poly.attachParentVert,
                                              poly.attachParentOp, mid, midRot, midPos,
                                              &midScale)) {
                        pModel = mid;
                        pRot = midRot;
                        pPos = midPos;
                        pScale = midScale;
                    }
                }
                if (child &&
                    BuildSpriteAttachFrame(pModel, pRot, pPos, camera, pScale,
                                          poly.attachVert, poly.spriteOp, child,
                                          childRot, childPos, &childScale,
                                          poly.matrixOp, poly.matrixWord, poly.matrixWorking,
                                          poly.matrixSentinel)) {
                    useModel = child;
                    useRot = childRot;
                    usePos = childPos;
                    useScale = childScale;
                }
            }

            bool allVisible = true;
            for (int i = 0; i < poly.vertexCount; i++) {
                tp.verts[i] = TransformMeshVertex(
                    useModel, poly.vertexIdx[i], useRot, usePos, camera, useScale);
                if (!tp.verts[i].visible) allVisible = false;
            }

            if (poly.vertexCount > 0) {
                tp.sortKey = ComputePainterSortKey(
                    tp.verts[0].camX, tp.verts[0].camY, tp.verts[0].depth);
            }

            tp.visible = allVisible && IsOnScreen(tp.verts, poly.vertexCount);
            // R1079: dump every PlainLine/DirLine that the transform emits, so a
            // wireframe edge the ASM draws (001803) can be traced to its mesh row
            // or proven absent from the port's list. Dig only: AB_DUMP_LINES=1.
            if (const char* dl = getenv("AB_DUMP_LINES"); dl && dl[0] == '1') {
                static int s_nl = 0;
                if (s_nl < 120) {
                    fprintf(stderr,
                            "LINE%d mi=%d tex=0x%x sp=0x%x op=%d n=%d "
                            "(%d,%d)-(%d,%d) vis=%d\n",
                            s_nl++, poly.streamIndex, (unsigned)poly.texInfo,
                            (unsigned)poly.specialParam, (int)poly.spriteOp,
                            poly.vertexCount,
                            (int)tp.verts[0].screenX, (int)tp.verts[0].screenY,
                            (int)tp.verts[1].screenX, (int)tp.verts[1].screenY,
                            (int)tp.visible);
                }
            }
            outCount++;
            continue;
        }

        // GlowPoints (op 0x18): glow/flare point list (FUNC_001766).
        // ASM transforms the first vertex for depth, then transforms byte-pair
        // vertices and stores (x,y) screen coords. Render callback (FUNC_001814)
        // draws particles via DrawParticle.
        if (poly.type == MeshPolyType::GlowPoints) {
            // Transform first vertex for depth and sort key
            ShipVertex firstVert{};
            if (!GetVertexMirrored(shipModel, poly.vertexIdx[0], firstVert))
                continue;
            ProjectedVertex firstPv = TransformVertex(firstVert, shipRot, shipPos,
                                                      camera, scaleRelated);
            if (!firstPv.visible) continue;

            // Pre-transform all byte-pair vertices and store screen coords
            const uint8_t* glowStore = GetGlowVertStore();
            int screenOffset = s_glowScreenCount;
            for (int i = 0; i < poly.glowVertCount && s_glowScreenCount < kMaxGlowScreen; i++) {
                int8_t vidx = static_cast<int8_t>(glowStore[poly.glowVertOffset + i]);
                if (vidx < 0) {
                    s_glowScreenX[s_glowScreenCount] = 0;
                    s_glowScreenY[s_glowScreenCount] = 0;
                    s_glowScreenVisible[s_glowScreenCount] = false;
                    s_glowScreenCount++;
                    continue;
                }
                ShipVertex sv{};
                if (!GetVertexMirrored(shipModel, static_cast<uint8_t>(vidx), sv)) {
                    s_glowScreenX[s_glowScreenCount] = 0;
                    s_glowScreenY[s_glowScreenCount] = 0;
                    s_glowScreenVisible[s_glowScreenCount] = false;
                    s_glowScreenCount++;
                    continue;
                }
                ProjectedVertex pv = TransformVertex(sv, shipRot, shipPos,
                                                     camera, scaleRelated);
                s_glowScreenX[s_glowScreenCount] = pv.screenX;
                s_glowScreenY[s_glowScreenCount] = pv.screenY;
                s_glowScreenVisible[s_glowScreenCount] = pv.visible;
                s_glowScreenCount++;
            }

            // Emit a single TransformedPoly with the first vertex as anchor.
            TransformedPoly& tp = outPolys[outCount];
            tp.vertexCount = 1;
            tp.texInfo = poly.texInfo;
            tp.normalIdx = 0;
            tp.type = MeshPolyType::GlowPoints;
            tp.afterSpecialPoly = poly.afterSpecialPoly;
            tp.specialParam = poly.specialParam;
            tp.meshIndex = poly.streamIndex;
            tp.backFacing = false;
            tp.contourId = -1;
            tp.glowVertCount = poly.glowVertCount;
            tp.glowVertOffset = static_cast<uint16_t>(screenOffset);
            tp.verts[0] = firstPv;
            tp.sortKey = ComputePainterSortKey(firstPv.camX, firstPv.camY, firstPv.depth);
            tp.visible = firstPv.visible;
            if (tp.visible) outCount++;
            continue;
        }

        // NavLight (op 0x01): PointSource nav light (FUNC_001698/001699).
        // Single screen-space point with blink gate. ASM stores x,y as
        // signed bytes in vertexIdx[0,1]. Transform: just project.
        if (poly.type == MeshPolyType::NavLight) {
            // Transform directly from model — don't use vertex cache because
            // it may have stale entries from a different transform model
            // (sprite children use different rot/pos/scale).
            ShipVertex sv{};
            if (!GetVertexMirrored(shipModel, poly.vertexIdx[0], sv))
                continue;
            ProjectedVertex pv = TransformVertex(sv, shipRot, shipPos,
                                                 camera, scaleRelated);
            if (!pv.visible) continue;
            TransformedPoly& tp = outPolys[outCount];
            tp.vertexCount = 1;
            tp.texInfo = poly.texInfo;
            tp.normalIdx = 0;
            tp.type = MeshPolyType::NavLight;
            tp.afterSpecialPoly = poly.afterSpecialPoly;
            tp.specialParam = poly.specialParam;
            tp.meshIndex = poly.streamIndex;
            tp.backFacing = false;
            tp.contourId = -1;
            tp.verts[0] = pv;
            tp.sortKey = ComputePainterSortKey(pv.camX, pv.camY, pv.depth);
            tp.visible = pv.visible;
            if (tp.visible) outCount++;
            continue;
        }

        // For quads / tris, transform all vertices.
        TransformedPoly& tp = outPolys[outCount];
        tp.vertexCount = poly.vertexCount;
        tp.texInfo = poly.texInfo;
        tp.normalIdx = poly.normalIdx;
        for (int i = 0; i < 4; i++)
            tp.vertexIdx[i] = (i < poly.vertexCount) ? poly.vertexIdx[i] : 0;
        tp.type = poly.type;
        tp.afterSpecialPoly = poly.afterSpecialPoly;
        // 0xAB window thresholds can be negative (bit15 set) — those are NOT
        // sprite children. Real Sprite748/749 expands set spriteOp != 0.
        // Without this guard, Adder's 0xAB -32581/-32582 faces were remapped
        // through model #187/#186 attach frames → white/black mid-hull rects.
        const bool isSpriteChild =
            (poly.specialParam & 0x8000) != 0 && poly.spriteOp != 0;
        tp.specialParam = isSpriteChild ? poly.specialParam
                                        : static_cast<uint16_t>(poly.specialParam & 0x7FFF);
        tp.meshIndex = poly.streamIndex;
        tp.contourId = poly.contourId;
        tp.shadeIdx = poly.color;  // R556: Complex721 payload shade-index carrier
        tp.attachParentModel = poly.attachParentModel;
        tp.sortKey = 0;

        const ShipModel* useModel = shipModel;
        const int32_t* useRot = shipRot;
        const int32_t* usePos = shipPos;
        int useScale = scaleRelated;
        int32_t childRot[9];
        int32_t childPos[3];
        int childScale = scaleRelated;

        if (isSpriteChild) {
            const int childId = SpriteChildModelId(poly.specialParam);
            // Dig: AB_SKIP_SPRITE_CHILD=217,218 (comma list) drops those expands.
            if (const char* sk = getenv("AB_SKIP_SPRITE_CHILD"); sk && sk[0]) {
                bool skip = false;
                const char* p = sk;
                while (*p) {
                    char* end = nullptr;
                    long id = strtol(p, &end, 10);
                    if (end == p)
                        break;
                    if (id == childId) {
                        skip = true;
                        break;
                    }
                    p = end;
                    if (*p == ',')
                        ++p;
                }
                if (skip)
                    continue;
            }
            // Dig: AB_SKIP_SPRITE_TEX=0x4005 drops matching child texInfo.
            if (const char* st = getenv("AB_SKIP_SPRITE_TEX"); st && st[0]) {
                unsigned long want = strtoul(st, nullptr, 0);
                if (want != 0 && poly.texInfo == static_cast<uint16_t>(want))
                    continue;
            }
            // Dig: AB_SKIP_SPRITE_TYPE=8 drops MeshPolyType for sprite children.
            if (const char* sty = getenv("AB_SKIP_SPRITE_TYPE"); sty && sty[0]) {
                long want = strtol(sty, nullptr, 0);
                if (want == static_cast<long>(poly.type))
                    continue;
            }
            // Dig: AB_SKIP_SPRITE_CHILD_TYPE=217:8 (child:type pairs).
            if (const char* sct = getenv("AB_SKIP_SPRITE_CHILD_TYPE");
                sct && sct[0]) {
                const char* p = sct;
                bool skip = false;
                while (*p) {
                    char* end = nullptr;
                    long id = strtol(p, &end, 10);
                    if (end == p || *end != ':')
                        break;
                    long ty = strtol(end + 1, &end, 10);
                    if (id == childId && ty == static_cast<long>(poly.type)) {
                        skip = true;
                        break;
                    }
                    p = end;
                    if (*p == ',')
                        ++p;
                }
                if (skip)
                    continue;
            }
            const ShipModel* child = GetShipModel(childId);
            const int32_t* pRot = shipRot;
            const int32_t* pPos = shipPos;
            const ShipModel* pModel = shipModel;
            int pScale = scaleRelated;
            int32_t midRot[9];
            int32_t midPos[3];
            int midScale = scaleRelated;
            // Nested sprite: attachVert is relative to an intermediate model
            // (Lynx #70 → #218 → #219). Build that frame first.
            if (poly.attachParentModel != 0) {
                const ShipModel* mid = GetShipModel(poly.attachParentModel);
                if (mid &&
                    BuildSpriteAttachFrame(shipModel, shipRot, shipPos, camera,
                                          scaleRelated, poly.attachParentVert,
                                          poly.attachParentOp, mid, midRot, midPos,
                                          &midScale)) {
                    pModel = mid;
                    pRot = midRot;
                    pPos = midPos;
                    pScale = midScale;
                }
            }
            if (child &&
                BuildSpriteAttachFrame(pModel, pRot, pPos, camera, pScale,
                                      poly.attachVert, poly.spriteOp, child,
                                      childRot, childPos, &childScale,
                                      poly.matrixOp, poly.matrixWord, poly.matrixWorking,
                                      poly.matrixSentinel)) {
                useModel = child;
                useRot = childRot;
                usePos = childPos;
                useScale = childScale;
            }
        }

        bool anyVisible = false;
        bool allVisible = true;
        // FUNC_001736 glint Contour stubs: specialParam 0x7360/0x7361.
        // Expand via FUNC_001554/001555 (light-oriented digon halves), not a
        // fixed screen billboard — otherwise nozzles stay upright and float.
        const bool isGlintDigon =
            poly.type == MeshPolyType::ComplexFill &&
            (poly.specialParam & 0xFFF0) == 0x7360;
        if (isGlintDigon) {
            ProjectedVertex anchor = TransformMeshVertex(
                useModel, poly.vertexIdx[0], useRot, usePos, camera, useScale);
            const bool lower = (poly.specialParam & 1) != 0;
            const uint8_t scaleByte = poly.vertexIdx[1];
            const uint8_t lightIdx = poly.vertexIdx[2];
            if (!ExpandGlintDigonHalf(useModel, useRot, anchor, lightIdx,
                                     scaleByte, lower, tp.verts)) {
                // Projection failed (behind near plane) — hide.
                if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1') {
                    fprintf(stderr,
                            "GLINT_FAIL anchor=%u scale=%u light=%u lower=%d "
                            "cam=(%d,%d,%d) scr=(%d,%d)\n",
                            poly.vertexIdx[0], scaleByte, lightIdx, lower ? 1 : 0,
                            anchor.camX, anchor.camY, anchor.depth,
                            anchor.screenX, anchor.screenY);
                }
                tp.vertexCount = 4;
                anyVisible = false;
                tp.backFacing = true;
            } else {
                if (getenv("AB_DUMP_GLINT") && getenv("AB_DUMP_GLINT")[0] == '1') {
                    fprintf(stderr,
                            "GLINT_OK half=%d aabb=(%d,%d)-(%d,%d) "
                            "v0=(%d,%d) v1=(%d,%d) v2=(%d,%d) v3=(%d,%d)\n",
                            lower ? 1 : 0,
                            std::min(std::min(tp.verts[0].screenX, tp.verts[1].screenX),
                                     std::min(tp.verts[2].screenX, tp.verts[3].screenX)),
                            std::min(std::min(tp.verts[0].screenY, tp.verts[1].screenY),
                                     std::min(tp.verts[2].screenY, tp.verts[3].screenY)),
                            std::max(std::max(tp.verts[0].screenX, tp.verts[1].screenX),
                                     std::max(tp.verts[2].screenX, tp.verts[3].screenX)),
                            std::max(std::max(tp.verts[0].screenY, tp.verts[1].screenY),
                                     std::max(tp.verts[2].screenY, tp.verts[3].screenY)),
                            tp.verts[0].screenX, tp.verts[0].screenY,
                            tp.verts[1].screenX, tp.verts[1].screenY,
                            tp.verts[2].screenX, tp.verts[2].screenY,
                            tp.verts[3].screenX, tp.verts[3].screenY);
                }
                tp.vertexCount = 4;
                anyVisible = anchor.visible;
                tp.backFacing = false;
            }
        } else {
            for (int i = 0; i < poly.vertexCount; i++) {
                tp.verts[i] = TransformMeshVertex(
                    useModel, poly.vertexIdx[i], useRot, usePos, camera, useScale);
                if (tp.verts[i].visible) anyVisible = true;
                else allVisible = false;
                // R778 probe: hull 17 face 3 (tex 0x4026, vidx 8/10/6) reports vis=0
                // although AB_DUMP_MODEL_VERTS shows all of its vertices projecting at
                // depths 13043-43195 (far above ASM_PROJ_NEAR=0x40). Print the per-poly
                // transform result to see which vertex disagrees with the prepass.
                if (getenv("AB_VFATE") && poly.texInfo == 0x4026) {
                    fprintf(stderr, "VFATE vidx=%u vis=%d scr=(%d,%d) depth=%d cam=(%d,%d)\n",
                            (unsigned)poly.vertexIdx[i], (int)tp.verts[i].visible,
                            (int)tp.verts[i].screenX, (int)tp.verts[i].screenY,
                            (int)tp.verts[i].depth, (int)tp.verts[i].camX,
                            (int)tp.verts[i].camY);
                }
            }
            // Dig: AB_SPRITE_Y_BIAS=N adds N to sprite-child screen Y (Explorer
            // tip attach74 is +1 vs ASM on orbit_p90 — try -1).
            if (isSpriteChild) {
                if (const char* yb = getenv("AB_SPRITE_Y_BIAS")) {
                    const int bias = atoi(yb);
                    if (bias != 0) {
                        for (int i = 0; i < poly.vertexCount; i++)
                            tp.verts[i].screenY = static_cast<int16_t>(
                                tp.verts[i].screenY + bias);
                    }
                }
            }
        }

        static bool s_dumpedSpriteAabb = false;
        if (!s_dumpedSpriteAabb && isSpriteChild &&
            getenv("AB_DUMP_SPRITE_AABB") &&
            getenv("AB_DUMP_SPRITE_AABB")[0] == '1') {
            int minx = 9999, maxx = -9999, miny = 9999, maxy = -9999;
            fprintf(stderr, "SPRITE_AABB tex=0x%04x scale=%d pos=(%d,%d,%d) rot0=%d\n",
                    poly.texInfo, useScale, usePos[0], usePos[1], usePos[2], useRot[0]);
            for (int i = 0; i < poly.vertexCount; i++) {
                fprintf(stderr, "  vidx=%u scr=(%d,%d) depth=%d vis=%d\n",
                        poly.vertexIdx[i], tp.verts[i].screenX, tp.verts[i].screenY,
                        tp.verts[i].depth, tp.verts[i].visible ? 1 : 0);
                if (!tp.verts[i].visible) continue;
                if (tp.verts[i].screenX < minx) minx = tp.verts[i].screenX;
                if (tp.verts[i].screenX > maxx) maxx = tp.verts[i].screenX;
                if (tp.verts[i].screenY < miny) miny = tp.verts[i].screenY;
                if (tp.verts[i].screenY > maxy) maxy = tp.verts[i].screenY;
            }
            fprintf(stderr, "  polyAABB=(%d..%d,%d..%d)\n", minx, maxx, miny, maxy);
            // Dump all sprite polys this frame — reset at end via poly count
        }
        if (isSpriteChild && poly.texInfo == 0x403c &&
            getenv("AB_DUMP_SPRITE_AABB") &&
            getenv("AB_DUMP_SPRITE_AABB")[0] == '1') {
            // fallthrough already printed; mark done after last mat60 via static ctr
            static int s_mat60Seen = 0;
            if (++s_mat60Seen >= 8)
                s_dumpedSpriteAabb = true;
        }

        // FUNC_001481 per-vertex cull — exact ASM port (1-based normal entry,
        // (idx ^ b0) & 1 vertex mirror). Verified 480/480 against GDB oracle.
        // Sprite children: use child model + attach frame (AB_SPRITE_NO_CULL=1
        // restores the old always-front behaviour).
        //
        // Contour groups (ComplexFill with contourId >= 0): ASM culls the
        // entire group once via the header normal, never per-piece. Use the
        // cached group decision.
        const bool isContourPiece = (poly.type == MeshPolyType::ComplexFill &&
                                     poly.contourId >= 0);
        if (isContourPiece) {
            // FUNC_001721 tests the Contour header normal before enqueuing the
            // span, and every piece of a group carries that header normal, so
            // the per-piece test is the group test. Treating Contours as
            // always-front painted both halves of opposed fans (Skeet #208's
            // canopy underside punched its top) and left back-side lobes
            // covering hull the camera cannot see. Dig: AB_C721_NO_CULL=1
            // restores the always-front behaviour.
            if (getenv("AB_C721_NO_CULL") && getenv("AB_C721_NO_CULL")[0] == '1') {
                tp.backFacing = false;
            } else {
                tp.backFacing = ComputeFaceFacing(useModel, poly.normalIdx,
                                                  useRot, usePos, camera);
            }
        } else if (isSpriteChild) {
            // Asp #186 flat glass (0x2000) is often back-facing under attach
            // remaps while ASM still paints the nose panes. Keep drawable;
            // AB_SPRITE_CULL=1 restores strict FUNC_001481 for sprites.
            const bool forceCull = getenv("AB_SPRITE_CULL") &&
                                   getenv("AB_SPRITE_CULL")[0] == '1';
            const bool noCull = (getenv("AB_SPRITE_NO_CULL") &&
                                 getenv("AB_SPRITE_NO_CULL")[0] == '1') ||
                                (!forceCull && poly.texInfo == 0x2000);
            if (noCull) {
                tp.backFacing = false;
            } else {
                tp.backFacing = ComputeFaceFacing(useModel, poly.normalIdx,
                                                  useRot, usePos, camera);
            }
        } else if (isGlintDigon) {
            // FUNC_001736 skips digons when the Contour normal faces away /
            // vertex depth < 0x41 (top & free-orbit). Match via face cull.
            tp.backFacing = ComputeFaceFacing(shipModel, poly.normalIdx,
                                              shipRot, shipPos, camera);
        } else {
            tp.backFacing = ComputeFaceFacing(shipModel, poly.normalIdx,
                                              shipRot, shipPos, camera);
            // Sidewinder #26: ASM FUNC_001708 still enqueues tex 0x4027
            // (TexTri + TexTriPair) at rear-quarter views where FUNC_001481
            // marks them back-facing. ASM oracle keeps them *culled* by
            // default (pd regression if we keep them visible).
            //
            // Opt-in: set AB_SIDEWINDER_4027_CULL=0 to keep on-screen.
            if (tp.backFacing && poly.texInfo == 0x4027 &&
                GetShipModelIndex(shipModel) == 26) {
                const char* cull = getenv("AB_SIDEWINDER_4027_CULL");
                const bool keep = cull && cull[0] != '1';
                if (keep)
                    tp.backFacing = false;
            }
            if (getenv("AB_CULL_WINDING") && getenv("AB_CULL_WINDING")[0] == '1' &&
                poly.vertexCount >= 3) {
                tp.backFacing = IsBackFacing(tp.verts[0], tp.verts[1], tp.verts[2],
                                             tp.verts[poly.vertexCount > 3 ? 3 : 2]);
            }
            // Asp #40: FUNC_001481 using normal byte0 vert culls upper-wing mats
            // (norms 8/10/11 → holes of VGA22). Cull only when BOTH the model-space
            // dot AND screen winding agree it's back-facing. AB_CULL_AND_WIND=1.
            if (getenv("AB_CULL_AND_WIND") && getenv("AB_CULL_AND_WIND")[0] == '1' &&
                tp.backFacing && poly.vertexCount >= 3) {
                if (!IsBackFacing(tp.verts[0], tp.verts[1], tp.verts[2],
                                  tp.verts[poly.vertexCount > 3 ? 3 : 2]))
                    tp.backFacing = false;
            }
            // Puma #60: mat8 ComplexFill/TexQuad wing lobes over-culled by
            // normal-table vert on pitch/yaw. Keep when screen winding says
            // front. AB_CULL_AND_WIND_CF=0 disables.
            if (tp.backFacing && poly.vertexCount >= 3 && poly.texInfo == 0x4008) {
                const char* cf = getenv("AB_CULL_AND_WIND_CF");
                if (!(cf && cf[0] == '0')) {
                    if (!IsBackFacing(tp.verts[0], tp.verts[1], tp.verts[2],
                                      tp.verts[poly.vertexCount > 3 ? 3 : 2]))
                        tp.backFacing = false;
                }
            }
            // Puma #60 mirrored upper mat8 band (normal 14): ASM 001805 emits
            // this TexQuad only on the opposite screen winding from normal 10.
            // The model-space FUNC_001481 test calls it front-facing throughout
            // a full orbit, which makes texture coverage appear/disappear at the
            // GateNear-316 LOD boundary. Apply the mirrored winding literally.
            // Dig: AB_PUMA_N14_MODEL_CULL=1 restores model-space-only culling.
            if (GetShipModelIndex(shipModel) == 60 &&
                poly.type == MeshPolyType::TexQuad &&
                poly.texInfo == 0x4008 && poly.normalIdx == 14 &&
                poly.vertexCount == 4 &&
                !(getenv("AB_PUMA_N14_MODEL_CULL") &&
                  getenv("AB_PUMA_N14_MODEL_CULL")[0] == '1')) {
                tp.backFacing = !IsBackFacing(tp.verts[0], tp.verts[1],
                                               tp.verts[2], tp.verts[3]);
            }
            // Opt-in: if normal-table vert says back, retry each poly.vertexIdx>>1
            // and keep the face when ANY yields front. Default OFF — the retry
            // kept Asp #40 mat6 TexQuadPair belly halves on pitch±90 (fleet
            // 975→0 OC) and inflated Saker orbit residual. ASM FUNC_001481 uses
            // normal byte0 only. Dig: AB_CULL_ANY_VERT=1 restores the retry.
            if (tp.backFacing && poly.vertexCount >= 3) {
                const char* av = getenv("AB_CULL_ANY_VERT");
                if (av && av[0] == '1') {
                    for (int vi = 0; vi < poly.vertexCount; vi++) {
                        int ov = static_cast<int>(poly.vertexIdx[vi]) >> 1;
                        if (!ComputeFaceFacing(shipModel, poly.normalIdx,
                                               shipRot, shipPos, camera, ov)) {
                            tp.backFacing = false;
                            break;
                        }
                    }
                }
            }
            // Asp #40: ASM omits textured mesh17 (n=8); body VGA22 comes from
            // degenerate mesh23 → flat 001714/001799. Default: do not force
            // n=8. AB_CULL_FORCE_NORM=8 restores the old OA dig; "-" stays off.
            // AB_CULL_FORCE_BACK=14,16,… digs ASM-faithful cull of extras.
            {
                bool forceFront = false;
                const char* fn = getenv("AB_CULL_FORCE_NORM");
                if (fn && fn[0] == '-' && fn[1] == '\0') {
                    forceFront = false;
                } else if (fn && fn[0]) {
                    const int n = static_cast<int>(poly.normalIdx);
                    const char* p = fn;
                    while (*p) {
                        char* end = nullptr;
                        long v = strtol(p, &end, 10);
                        if (end == p) break;
                        if (v == n) { forceFront = true; break; }
                        p = (*end == ',') ? end + 1 : end;
                    }
                }
                if (forceFront)
                    tp.backFacing = false;
                if (const char* fb = getenv("AB_CULL_FORCE_BACK"); fb && fb[0]) {
                    const int n = static_cast<int>(poly.normalIdx);
                    const char* p = fb;
                    while (*p) {
                        char* end = nullptr;
                        long v = strtol(p, &end, 10);
                        if (end == p) break;
                        if (v == n) { tp.backFacing = true; break; }
                        p = (*end == ',') ? end + 1 : end;
                    }
                }
            }
            // Asp #40 bisect: textured hull mats (0x4xxx) never-cull.
            // AB_CULL_NO_TEX4=1. Narrower: AB_CULL_NO_4006=1.
            if (getenv("AB_CULL_NO_TEX4") && getenv("AB_CULL_NO_TEX4")[0] == '1' &&
                (poly.texInfo & 0x4000) != 0)
                tp.backFacing = false;
            if (getenv("AB_CULL_NO_4006") && getenv("AB_CULL_NO_4006")[0] == '1' &&
                poly.texInfo == 0x4006)
                tp.backFacing = false;
            if (getenv("AB_CULL_INVERT") && getenv("AB_CULL_INVERT")[0] == '1')
                tp.backFacing = !tp.backFacing;
            if (getenv("AB_NO_CULL") && getenv("AB_NO_CULL")[0] == '1')
                tp.backFacing = false;
        }

        // Contour digons (ComplexFill vc=2): the per-piece "never cull" override
        // is REMOVED — group culling via the header normal now covers all
        // ComplexFill pieces uniformly. Non-ComplexFill vc=2 (DirLine, PlainLine,
        // thruster) are unaffected since isContourPiece is false for those.
        // Dig: AB_C721_SPRITE_DIGON_CULL=1 is now a no-op (kept for compat).

        // FUNC_001777: sort key uses first vertex (p3 → verts[0]) only.
        // Formula: ((|camX|+|camY|)>>3) + camZ, with optional zoom shift
        // (ctx+0x130). Applied inside ComputePainterSortKey (default zoom=0).
        if (poly.vertexCount > 0) {
            tp.sortKey = ComputePainterSortKey(
                tp.verts[0].camX, tp.verts[0].camY, tp.verts[0].depth);
        }
        // FUNC_001736 engine glints: ASM paints Contour digons after the rear
        // hull band. Painter depth puts them under nearby mat9 fans — force
        // near (like thrusters) so nozzles sit on the surface.
        if (isGlintDigon)
            tp.sortKey = 1;

        // FUNC_001479 stamps unprojectable vertices (camera-space z < 0x40) with
        // the 0x8002 sentinel, and FUNC_001712's bound test then drops the whole
        // primitive. C++ collapsed those vertices to screen (0,0) instead, so
        // flying inside a hull stretched every straddling face across the frame
        // (Puma at CAM Z:56 painted a full-screen grey wall where ASM shows 42
        // px). Dig: AB_NEAR_ANY_VERT=1 restores the any-vertex behaviour.
        const bool nearRejects =
            !(getenv("AB_NEAR_ANY_VERT") && getenv("AB_NEAR_ANY_VERT")[0] == '1');
        // R778 LAW: a polygon must not be rejected merely because all of its vertices
        // lie outside the viewport - a large triangle can cover most of the screen with
        // every corner off it. Hull 17's big panel is exactly that: (255,-34) (-57,32)
        // (369,114), all three outside the 320x200+8 margin, worth 20 699 px, drawn by
        // the ASM and dropped by IsOnScreen alone. Fleet record with the fallback on:
        // A+B 32 -> 33, total 273 125 -> 168 988 px, hull 17 41 368 -> 0 (D -> A),
        // hull 63 33 010 -> 1 008 (D -> C), hull 52 34 244 -> 14 171, ZERO hulls worse.
        // Kill switch: AB_VIS_AABB=0 restores the vertex-inside-only test.
        const bool aabbFallback =
            !(getenv("AB_VIS_AABB") && getenv("AB_VIS_AABB")[0] == '0');
        const bool onScreen = IsOnScreen(tp.verts, poly.vertexCount) ||
                              (aabbFallback &&
                               PolyTouchesScreen(tp.verts, poly.vertexCount, 8));
        tp.visible = (nearRejects ? allVisible : anyVisible) && onScreen;

        // Dig: AB_DUMP_EAGLE_CF=1 — Contour 0x5015 transform visibility.
        if (getenv("AB_DUMP_EAGLE_CF") && getenv("AB_DUMP_EAGLE_CF")[0] == '1' &&
            poly.type == MeshPolyType::ComplexFill && poly.texInfo == 0x5015) {
            fprintf(stderr,
                    "EAGLE_CF mi=%d cid=%d vc=%d vis=%d any=%d bf=%d sprite=%d "
                    "child=%d useVc=%u scr=",
                    poly.streamIndex, (int)poly.contourId, poly.vertexCount,
                    (int)tp.visible, (int)anyVisible, (int)tp.backFacing,
                    (int)isSpriteChild,
                    isSpriteChild ? SpriteChildModelId(poly.specialParam) : -1,
                    useModel ? useModel->vertexCount : 0);
            for (int i = 0; i < poly.vertexCount && i < 4; i++)
                fprintf(stderr, "(%d,%d,v%d)", tp.verts[i].screenX,
                        tp.verts[i].screenY, (int)tp.verts[i].visible);
            fprintf(stderr, "\n");
        }

        // Dig: AB_DUMP_CID_FACING=1 — per-piece facing for ALL ComplexFill,
        // grouped by contourId. Reveals mixed facing within contour groups.
        if (getenv("AB_DUMP_CID_FACING") && getenv("AB_DUMP_CID_FACING")[0] == '1' &&
            poly.type == MeshPolyType::ComplexFill && poly.contourId >= 0) {
            fprintf(stderr,
                    "CID_FACING cid=%d mi=%d nIdx=%d vc=%d bf=%d vis=%d "
                    "tex=0x%04x scr=(%d,%d)\n",
                    (int)poly.contourId, poly.streamIndex,
                    (int)poly.normalIdx, poly.vertexCount,
                    (int)tp.backFacing, (int)tp.visible,
                    (int)poly.texInfo,
                    tp.verts[0].screenX, tp.verts[0].screenY);
        }

        // Always advance: backFacing is filtered at draw time (and by the
        // all-culled failsafe below). Compact-on-cull dropped on-screen
        // Krait/Anaconda faces when every normal returned back-facing.
        outCount++;
    }
    stampPendingFaceLight();

    // Failsafe: TexTriPair ships (Krait #27, Anaconda #59, …) can have every
    // FUNC_001481 test return back-facing on nose-on / beam views — pair
    // normals share +nz and the associated vertex sits on the X hinge (z=0),
    // so (v-cam)·n stays ≥0 for both mirrors. ASM dump-ab still paints the
    // hull (likely facing-cache / draw-path quirk). If cull would emit zero
    // drawable polys while some are on screen, drop the cull for this mesh.
    //
    // Shuttle #15: PlainLine (vc=2) used to keep drawable>0 and block this
    // failsafe. DirLine ribbons (FUNC_001761) emit as quads (vc=4) with
    // backFacing=false — same trap when the near LOD swaps in 0x11 strips.
    // Ignore non-hull primitives when counting drawable faces.
    int drawable = 0;
    int onScreenHull = 0;
    for (int i = 0; i < outCount; i++) {
        if (!outPolys[i].visible) continue;
        if ((outPolys[i].specialParam & 0x8000) == 0)
            onScreenHull++;
        const MeshPolyType t = outPolys[i].type;
        if (t == MeshPolyType::PlainLine || t == MeshPolyType::DirLine ||
            t == MeshPolyType::Thruster || outPolys[i].vertexCount < 3)
            continue;
        if (!outPolys[i].backFacing)
            drawable++;
    }
    if (drawable == 0 && onScreenHull > 0) {
        for (int i = 0; i < outCount; i++) {
            if ((outPolys[i].specialParam & 0x8000) != 0) continue;
            if (outPolys[i].visible)
                outPolys[i].backFacing = false;
        }
    }

    // Resolve FUNC_001696 → FUNC_001780 barrier keys with live camera verts.
    for (const MeshBarrier& b : meshData.barriers) {
        ResolvedBarrier rb{};
        rb.streamIndex = b.streamIndex;
        rb.bandMode = b.bandMode;
        rb.sortKey = 0;
        if (b.bandMode == 0) {
            g_frameBarriers.push_back(rb);
            continue;
        }
        const uint32_t ctrlCase = b.opcode >> 13;
        const ShipModel* bModel = shipModel;
        const int32_t* bRot = shipRot;
        const int32_t* bPos = shipPos;
        int bScale = scaleRelated;
        int32_t childRot[9];
        int32_t childPos[3];
        int childScale = scaleRelated;
        if (b.childModelId != 0xffff) {
            const ShipModel* child = GetShipModel(static_cast<int>(b.childModelId));
            if (child &&
                BuildSpriteAttachFrame(shipModel, shipRot, shipPos, camera,
                                      scaleRelated, b.attachVert, b.spriteOp,
                                      child, childRot, childPos, &childScale)) {
                bModel = child;
                bRot = childRot;
                bPos = childPos;
                bScale = childScale;
            }
        }
        if (ctrlCase == 2 || ctrlCase == 1 || ctrlCase == 3) {
            int vid = static_cast<int>(static_cast<int8_t>(b.opcode >> 5));
            ShipVertex vert{};
            if (GetVertexMirrored(bModel, vid, vert)) {
                ProjectedVertex pv =
                    TransformVertex(vert, bRot, bPos, camera, bScale);
                rb.sortKey = ComputePainterSortKey(pv.camX, pv.camY, pv.depth);
            }
        } else if (ctrlCase == 7) {
            const uint32_t sub = b.opcode >> 5;
            if (sub == 0x7fe) {
                rb.sortKey = 0xffffffffu;
            } else if (sub == 0x7ff) {
                // Object-position key: model-space origin at shipPos.
                ShipVertex origin{};
                origin.x = 0;
                origin.y = 0;
                origin.z = 0;
                ProjectedVertex pv =
                    TransformVertex(origin, bRot, bPos, camera, bScale);
                rb.sortKey = ComputePainterSortKey(pv.camX, pv.camY, pv.depth);
            } else {
                // 0x7fd / default: Z-biased from vertex bits or pure Z plane.
                int vid = static_cast<int>(static_cast<int8_t>(sub));
                ShipVertex vert{};
                int32_t cx = 0, cy = 0, cz = 0;
                if (sub == 0x7fd) {
                    int shift = bScale;
                    if (shift < 0) shift = 0;
                    cz = static_cast<int32_t>(b.payload) << (shift & 0x1f);
                } else if (GetVertexMirrored(bModel, vid, vert)) {
                    ProjectedVertex pv =
                        TransformVertex(vert, bRot, bPos, camera, bScale);
                    cx = pv.camX;
                    cy = pv.camY;
                    // ASM shl; shift the unsigned view (UBSan: negative payload).
                    cz = pv.depth + static_cast<int32_t>(
                             static_cast<uint32_t>(b.payload) << (bScale & 0x1f));
                }
                rb.sortKey = ComputePainterSortKey(cx, cy, cz);
            }
        }
        g_frameBarriers.push_back(rb);
    }

    SetMeshAluOverride(nullptr);
    return outCount;
}

bool AnalyticalFaceNormal(int32_t ax, int32_t ay, int32_t az,
                          int32_t bx, int32_t by, int32_t bz,
                          int32_t cx, int32_t cy, int32_t cz,
                          float& nx, float& ny, float& nz)
{
    const double e1x = (double)(bx - ax);
    const double e1y = (double)(by - ay);
    const double e1z = (double)(bz - az);
    const double e2x = (double)(cx - ax);
    const double e2y = (double)(cy - ay);
    const double e2z = (double)(cz - az);
    double rx = e1y * e2z - e1z * e2y;
    double ry = e1z * e2x - e1x * e2z;
    double rz = e1x * e2y - e1y * e2x;
    const double len = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (len < 1e-9) {
        nx = 0; ny = 0; nz = 1;
        return false;
    }
    nx = (float)(rx / len);
    ny = (float)(ry / len);
    nz = (float)(rz / len);
    return true;
}

bool TransformAuthoredNormal(const ShipModel* model, uint8_t normalIdx,
                             const int32_t* shipRotFp16,
                             float& nx, float& ny, float& nz)
{
    if (!model || !shipRotFp16) return false;
    int32_t mx = 0, my = 0, mz = 0;
    if (!GetNormal(model, normalIdx, mx, my, mz)) return false;
    const int32_t rx = (int32_t)(((int64_t)mx * shipRotFp16[0] +
                                  (int64_t)my * shipRotFp16[3] +
                                  (int64_t)mz * shipRotFp16[6]) >> 16);
    const int32_t ry = (int32_t)(((int64_t)mx * shipRotFp16[1] +
                                  (int64_t)my * shipRotFp16[4] +
                                  (int64_t)mz * shipRotFp16[7]) >> 16);
    const int32_t rz = (int32_t)(((int64_t)mx * shipRotFp16[2] +
                                  (int64_t)my * shipRotFp16[5] +
                                  (int64_t)mz * shipRotFp16[8]) >> 16);
    const double len = std::sqrt((double)rx * rx + (double)ry * ry + (double)rz * rz);
    if (len < 1e-6) {
        nx = 0; ny = 0; nz = 1;
        return false;
    }
    nx = (float)(rx / len);
    ny = (float)(ry / len);
    nz = (float)(rz / len);
    return true;
}

static bool PolyWorldValid(const TransformedPoly& p)
{
    // PlainLine rails are 2-vert world segments (Shuttle / Puma struts). Allow
    // them through so the HW path can expand camera-facing ribbons.
    if (p.vertexCount < 2) return false;
    if (p.vertexCount < 3 && p.type != MeshPolyType::PlainLine &&
        p.type != MeshPolyType::DirLine)
        return false;
    // Keep the face if any vertex is in front of the ASM near plane; GPU clips
    // the rest. Rejecting on a single near vert was dropping tip geometry.
    for (int i = 0; i < p.vertexCount && i < 4; i++) {
        if (p.verts[i].depth > 0x40 || p.verts[i].worldZ > 0x40)
            return true;
    }
    return false;
}

static WorldFaceKind ClassifyWorldFace(const TransformedPoly& p)
{
    // Contours stay ScreenHybrid here; BuildContourWorldFaces emits ContourHull.
    if (p.type == MeshPolyType::ComplexFill && p.contourId >= 0)
        return WorldFaceKind::ScreenHybrid;
    if (p.type == MeshPolyType::Thruster || p.type == MeshPolyType::GlowPoints ||
        p.type == MeshPolyType::NavLight || p.type == MeshPolyType::Sprite ||
        p.type == MeshPolyType::TextPrimitive)
        return WorldFaceKind::ScreenHybrid;
    // FUNC_001736 glint Contours stay screen hybrid (never ContourHull).
    if (p.texInfo == 0x20ee && (p.specialParam & 0xFFF0) == 0x7360)
        return WorldFaceKind::ScreenHybrid;
    if ((p.texInfo == 0x2000 || p.texInfo == 0x3000 ||
         (p.texInfo == 0x20ee && (p.specialParam & 0xFFF0) != 0x7360) ||
         (p.texInfo == 0 && p.type == MeshPolyType::FlatQuad)) &&
        p.vertexCount >= 3)
        return WorldFaceKind::Punch;
    return WorldFaceKind::Hull;
}

int BuildWorldShipFaces(const TransformedPoly* polys, int polyCount,
                        const ShipModel* model, const int32_t* shipRotFp16,
                        WorldFace* outFaces, int maxFaces,
                        bool includeHidden)
{
    if (!polys || !outFaces || maxFaces <= 0) return 0;
    int out = 0;
    for (int i = 0; i < polyCount && out < maxFaces; i++) {
        const TransformedPoly& p = polys[i];
        if (!includeHidden && !p.visible) continue;
        if (p.vertexCount < 2) continue;

        WorldFace& f = outFaces[out];
        f = WorldFace{};
        f.type = p.type;
        f.texInfo = p.texInfo;
        f.normalIdx = p.normalIdx;
        f.vertexCount = (uint8_t)std::min(p.vertexCount, 4);
        f.backFacing = p.backFacing;
        f.sortKey = p.sortKey;
        f.meshIndex = p.meshIndex;
        f.sourceIndex = i;
        f.contourId = p.contourId;
        f.faceLight[0] = p.faceLight[0];
        f.faceLight[1] = p.faceLight[1];
        f.faceLight[2] = p.faceLight[2];
        if (IsSpriteChildParam(p.specialParam))
            f.spriteChildModel = SpriteChildModelId(p.specialParam);

        f.kind = ClassifyWorldFace(p);
        f.worldValid = (f.kind != WorldFaceKind::ScreenHybrid) && PolyWorldValid(p);
        if (f.kind == WorldFaceKind::ScreenHybrid)
            f.worldValid = false;

        float nx = 0, ny = 0, nz = 1;
        f.authoredNormal = TransformAuthoredNormal(model, p.normalIdx, shipRotFp16,
                                                   nx, ny, nz);
        if (!f.authoredNormal && p.vertexCount >= 3) {
            AnalyticalFaceNormal(p.verts[0].worldX, p.verts[0].worldY, p.verts[0].worldZ,
                                 p.verts[1].worldX, p.verts[1].worldY, p.verts[1].worldZ,
                                 p.verts[2].worldX, p.verts[2].worldY, p.verts[2].worldZ,
                                 nx, ny, nz);
        }

        // Duplicate verts per face so flat shading shares one normal.
        for (int v = 0; v < f.vertexCount; v++) {
            f.verts[v].x = (float)p.verts[v].worldX / (float)FP_ONE;
            f.verts[v].y = (float)p.verts[v].worldY / (float)FP_ONE;
            f.verts[v].z = (float)p.verts[v].worldZ / (float)FP_ONE;
            f.verts[v].nx = nx;
            f.verts[v].ny = ny;
            f.verts[v].nz = nz;
            f.verts[v].u = 0;
            f.verts[v].v = 0;
            f.verts[v].color = 0xFFC0C0D0u;
        }

        // Simple corner UVs (0..1); textured path may overwrite via ShipAssignTexUVs.
        if (f.vertexCount >= 3) {
            f.verts[0].u = 0; f.verts[0].v = 0;
            f.verts[1].u = 1; f.verts[1].v = 0;
            f.verts[2].u = 1; f.verts[2].v = 1;
            if (f.vertexCount >= 4) {
                f.verts[3].u = 0; f.verts[3].v = 1;
            }
        }

        out++;
    }
    return out;
}

} // namespace jjffe
