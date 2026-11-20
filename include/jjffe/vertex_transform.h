#ifndef JJFFE_VERTEX_TRANSFORM_H
#define JJFFE_VERTEX_TRANSFORM_H

#include <cstdint>
#include <vector>
#include "jjffe/math.h"
#include "jjffe/ship_model.h"
#include "jjffe/mesh_parser.h"

namespace jjffe {

// Projected vertex with screen coordinates and depth
struct ProjectedVertex {
    int16_t screenX;        // Screen X coordinate
    int16_t screenY;        // Screen Y coordinate
    int32_t depth;          // Camera-space Z depth (for Z-buffer / Painter's)
    int32_t camX;           // Camera-space X (for FUNC_001777 sort key)
    int32_t camY;           // Camera-space Y (for FUNC_001777 sort key)
    int32_t worldX;         // World-space X (for FUNC_001481 face cull)
    int32_t worldY;         // World-space Y
    int32_t worldZ;         // World-space Z
    bool visible;           // True if in front of camera and on screen
};

// Transform result for a polygon
struct TransformedPoly {
    ProjectedVertex verts[4];   // Projected vertices (up to 4)
    int vertexCount;            // Number of vertices (2=line, 4=quad)
    int16_t normalX, normalY;   // Screen-space normal for back-face culling
    bool backFacing;            // True if polygon faces away from camera
    uint16_t texInfo;           // Texture info from mesh
    uint8_t normalIdx;          // Normal index from mesh
    uint8_t vertexIdx[4];       // Mesh vertex indices (draw order); dig/VIS
    MeshPolyType type;          // Polygon type from mesh parser
    bool afterSpecialPoly;      // Follows 0xAB gate (flat window / special material)
    uint16_t specialParam;      // 0xAB threshold; thruster size in mesh; after
                                // transform: draw tex word 0x123/0x65 (FUNC_001738)
    uint32_t sortKey;           // FUNC_001777-style Painter's sort key (larger = farther)
    int meshIndex;              // Bytecode order (stable tie-break; later draws on top)
    int16_t contourId;          // Complex721 Contour group (-1 = none)
    uint16_t attachParentModel; // Nested sprite grandparent (0 = direct child)
    bool visible;               // True if polygon should be drawn
    // Glow/flare (GlowPoints): byte-pair vertex indices stored in parser.
    uint16_t glowVertCount;     // Number of byte-pair vertices
    uint16_t glowVertOffset;    // Index into glow vertex store
    int8_t faceLight[3];        // renderCtx+0xf4 in effect when emitted
    uint8_t shadeIdx = 0;       // R556: Complex721 record payload low byte (shade index)
};

// ASM render ctx+0x130 (camera zoom) for FUNC_001777.
// Dump-ab / ship-demo use 0 → final key >>= 3. Set 3 for unshifted unit tests.
void SetPainterSortZoom(int zoom);
int GetPainterSortZoom();

// Original Painter's sort key from FUNC_001777:
//   base = ((|camX| + |camY|) >> 3) + camZ
//   then >>= (3 - zoom) when zoom <= 3 (left-shift + saturate when zoom > 3)
uint32_t ComputePainterSortKey(int32_t camX, int32_t camY, int32_t camZ);

// Current A/B pose index (0-7), selects per-pose GDB camModel for culling.
extern int g_abPoseIndex;

// Current A/B ship model index, for vertex trace attribution.
extern int g_abShipIndex;

// Glow/flare screen coordinate store (pre-transformed during TransformShipVertices).
const int16_t* GetGlowScreenX();
const int16_t* GetGlowScreenY();
const bool* GetGlowScreenVisible();

// Vertex cache: stores transformed vertices by model index.
// Populated during TransformShipVertices, read by NavLight (op 0x01).
// Live iff entry.genCounter == GetVertexCacheRenderGen() (ASM renderCtx+0xf0).
bool GetCachedVertex(int index, ProjectedVertex& out);

// ASM renderCtx+0xf0 — stamped into each cache entry at +0x14 on transform.
// Mesh interpret sets this to 2; future bank / child paths may bump it.
int32_t GetVertexCacheRenderGen();
void SetVertexCacheRenderGen(int32_t gen);

// ASM DATA_009200 window base in 36-byte entry units (0 until op 0x1E).
// FUNC_001767 advances both halves together: source +n verts, cache +n*2 entries.
int GetVertexCacheBankBase();
void SetVertexCacheBankBase(int entryBase);
// Source-vertex bank (ASM renderCtx+0x150 += n*6). Reset with cache each ship.
int GetVertexBankVerts();
void SetVertexBankVerts(int nVerts);
// Positive op 0x1E: advance source +n verts and cache +n*2 entries together.
void AdvanceVertexBank(int nVerts);
// Deprecated alias — same as AdvanceVertexBank.
void AdvanceVertexCacheBank(int nUnits);

// FUNC_001481 face-normal cull: determines if a face is front-facing.
// Tests dot(worldNormal, worldVertex - cameraPos). If >= 0, front-facing.
// shipRot:     ship rotation matrix (to transform normal to world space)
// shipModel:   ship model (for normal data lookup)
// normalIdx:   normal index from mesh bytecode
// vertWorldX/Y/Z: world-space position of a representative vertex (p3 / first vert)
// camera:      camera (for position)
// Returns true if the face is BACK-FACING (should be culled).
// GDB oracle (2026-07-12) confirms ASM FUNC_001481 uses param_2 = normalIdx
// (bytecode byte 6), reads byte-0 vertex association from normal entry,
// and applies FUNC_001685 14-bit position normalization + VecMatTMul.
// See PARITY_JOURNAL.md for per-vertex port attempt details.
bool ComputeFaceFacing(const ShipModel* shipModel, uint8_t normalIdx,
                       const int32_t* shipRot, const int32_t* shipPos,
                       const Camera3D* camera, int overrideVertIdx = -1);

// FUNC_001685 camera model-space position computation.
// Computes camera-in-model-space{ camMx, camMy, camMz} and lodShift
// from ship position/rotation. Used by both ComputeFaceFacing and
// the facing flags table population (FUNC_001481).
void ComputeCamModelPos(const ShipModel* shipModel,
                        const int32_t* shipRot,
                        const int32_t* shipPos,
                        int32_t& camMx, int32_t& camMy, int32_t& camMz,
                        int& lodShift);

// Model-space face cull matching ASM FUNC_001481 + FUNC_001685.
// Computes camera-in-model-space with FUNC_001475 normalization,
// then tests in model space with ASM cull direction (dot >= 0 = cull).
bool ComputeFaceFacingModel(
    const ShipModel* shipModel,
    uint8_t normalIdx, uint8_t vertIdx,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    int scaleRelated);

// Transform and project ship model vertices
// Returns projected vertices for each polygon in the mesh
// shipModel: ship model with vertex data
// meshData: parsed mesh polygons
// shipRot: ship rotation matrix (9 int32_t values)
// shipPos: ship position (3 int32_t values)
// camera: camera for projection
// outPolys: output array of transformed polygons (must be large enough)
// Returns: number of polygons written to outPolys (cull via TransformedPoly.backFacing)
const int32_t* GetAbLastShipPos();
const Camera3D* GetAbLastCamera();

int TransformShipVertices(
    const ShipModel* shipModel,
    const MeshData& meshData,
    const int32_t* shipRot,
    const int32_t* shipPos,
    const Camera3D* camera,
    TransformedPoly* outPolys,
    int maxPolys
);

// Check if a polygon is back-facing (screen-space cross product)
// Returns true if polygon faces away from camera
bool IsBackFacing(const ProjectedVertex& v0, const ProjectedVertex& v1,
                  const ProjectedVertex& v2, const ProjectedVertex& v3);

// Check if a polygon is on screen (at least partially visible)
bool IsOnScreen(const ProjectedVertex* verts, int count, int margin = 10);

// R779: does the polygon's AABB overlap the viewport? IsOnScreen() requires at least
// one VERTEX inside the margin, which rejects a triangle that covers the screen with
// every corner outside it (hull 17's big panel: (255,-34) (-57,32) (369,114)). The
// renderer ORs the two tests; see vertex_transform.cpp for the measured effect.
bool PolyTouchesScreen(const ProjectedVertex* verts, int count, int margin);

// Get normal vector from ship model with mirror handling
bool GetNormal(const ShipModel* shipModel, int index, int32_t& nx, int32_t& ny, int32_t& nz);

// Resolve a mesh vertex index to model-space ShipVertex (mirror flags, midpoints,
// thruster jitter bases). Public wrap of the transform-path lookup — use this for
// emitter harvest; do not invent a second resolver (§7.5 / V9).
bool LookupShipVertex(const ShipModel* shipModel, int index, ShipVertex& out);

// Set facing-parse context + FUNC_001480 dirty bits for AB_FACE_GATE.
// IsFar(neg) lazily runs FUNC_001481 when byte3 bit1 is set (ASM 001751).
void PrecomputeFacingFlags(const ShipModel* shipModel, const int32_t* shipRot,
                           const int32_t* shipPos, const Camera3D* camera);
// Sprite-child enter/leave: switch ShipModel + camModel for Contour GateFar.
void PushFacingParseContext(const ShipModel* childModel);
void PushFacingParseContext(const ShipModel* childModel, uint8_t attachVert, uint16_t spriteOp);
void PopFacingParseContext();
// Lazy FUNC_001481 for one index (clears dirty bit1, sets far bit0).
void LazyComputeFacingCull(int normalIdx);
int32_t AttachVertexDepthForGate(uint8_t vert);

// Resolved FUNC_001780 barriers for the current TransformShipVertices call.
struct ResolvedBarrier {
    int streamIndex;
    uint8_t bandMode;   // 0 = close, 1 = open
    uint32_t sortKey;
};

const std::vector<ResolvedBarrier>& GetFrameBarriers();

// Sprite748/749 child tag: bit15 set; model id in bits 5..15 (11 bits, max 466
// ships). Low 5 bits preserve TexQuadPair UV flags (mirror/primary/mixedParity).
// Old encoding OR'd modelId into low bits and broke UV for child #23 (0x17 has
// bit0+bit1 set → false mirror+primary).
inline uint16_t PackSpriteChildParam(int modelId, uint16_t uvFlags)
{
    return static_cast<uint16_t>(0x8000u | ((modelId & 0x7FFu) << 5) |
                                 (uvFlags & 0x1Fu));
}

inline int SpriteChildModelId(uint16_t specialParam)
{
    return (specialParam & 0x7FFF) >> 5;
}

inline uint16_t SpriteChildUvFlags(uint16_t specialParam)
{
    return static_cast<uint16_t>(specialParam & 0x1Fu);
}

inline bool IsSpriteChildParam(uint16_t specialParam)
{
    return (specialParam & 0x8000u) != 0;
}

// ---------------------------------------------------------------------------
// Renderer-neutral world-space face output (Ogre HW hull + tests).
// Positions come from ProjectedVertex.world*; normals are authored ShipNormal
// rotated by shipRot (16.16), with analytical cross-product fallback.
// ---------------------------------------------------------------------------

enum class WorldFaceKind : uint8_t {
    Hull = 0,         // flat / textured tris & quads, DirLine ribbons
    Punch = 1,        // 0x2000 / 0x20ee local cutouts with valid world geometry
    ScreenHybrid = 2, // thrusters, glows, glints — screen overlays
    ContourHull = 3,  // Complex721 Contours tessellated to world tris (GPU)
};

struct WorldFaceVert {
    float x = 0, y = 0, z = 0;     // world units (fixed >> 16)
    float nx = 0, ny = 0, nz = 1;  // unit world normal (flat per face)
    float u = 0, v = 0;            // texel-normalized 0..1 when textured
    uint32_t color = 0xFFFFFFFFu;  // ARGB material / lit preview colour
};

struct WorldFace {
    WorldFaceKind kind = WorldFaceKind::Hull;
    MeshPolyType type = MeshPolyType::EndMarker;
    uint16_t texInfo = 0;
    uint8_t normalIdx = 0;
    uint8_t vertexCount = 0;
    WorldFaceVert verts[4]{};
    bool worldValid = false;       // false → screen-only / skip HW hull
    bool authoredNormal = false;   // true if GetNormal succeeded
    bool backFacing = false;       // FUNC_001481 result (diagnostic override)
    bool textured = false;
    uint32_t sortKey = 0;
    int meshIndex = 0;
    int sourceIndex = -1;          // index into TransformedPoly array
    int16_t contourId = -1;
    int spriteChildModel = -1;     // -1 = parent hull
    int8_t faceLight[3] = {0, 0, 0};
};

// Build world faces from TransformShipVertices output.
// shipRotFp16: 16.16 rotation (MatScaleSin32ToFp16 of the transform matrix).
// model: for GetNormal / material colour; may be null (analytical normals only).
// outFaces capacity = maxFaces; returns count written.
// includeHidden: when true, keep polys soft-culled as invisible (needed for
// D3D orbit viewers — soft screen visibility ≠ GPU world draw set).
int BuildWorldShipFaces(const TransformedPoly* polys, int polyCount,
                        const ShipModel* model, const int32_t* shipRotFp16,
                        WorldFace* outFaces, int maxFaces,
                        bool includeHidden = false);

// Sprite749 / nested-child poses for HW (.x) instancing — same attach frames
// as TransformShipVertices plainSprite path (no soft raster).
struct SpriteChildInstance {
    int modelId = -1;
    int32_t pos[3]{};
    int32_t rot[9]{};
    int scaleRelated = 0;
};

int CollectSpriteChildInstances(const ShipModel* parentModel, const MeshData& mesh,
                                const int32_t* shipRot, const int32_t* shipPos,
                                const Camera3D* camera, int parentScale,
                                SpriteChildInstance* out, int maxOut);

// Rotate a model-space normal by a 16.16 matrix and unitize into floats.
bool TransformAuthoredNormal(const ShipModel* model, uint8_t normalIdx,
                             const int32_t* shipRotFp16,
                             float& nx, float& ny, float& nz);

// Analytical unit normal from three world-space fixed-point positions.
bool AnalyticalFaceNormal(int32_t ax, int32_t ay, int32_t az,
                          int32_t bx, int32_t by, int32_t bz,
                          int32_t cx, int32_t cy, int32_t cz,
                          float& nx, float& ny, float& nz);

} // namespace jjffe

#endif // JJFFE_VERTEX_TRANSFORM_H
