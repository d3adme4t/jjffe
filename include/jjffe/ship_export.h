#ifndef JJFFE_SHIP_EXPORT_H
#define JJFFE_SHIP_EXPORT_H

#include "jjffe/ship_renderer.h"
#include "jjffe/vertex_transform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jjffe {

// OBJ / Blender scale: world (fixed>>16) is tiny; 100 puts a shuttle at ~7 units.
constexpr float kShipExportObjScale = 100.0f;

// Locked export policy (D): geometry layer is an exact bytecode freeze.
// Overlay (0x1000 non-missile) → o *_decal; Punch → o *_punch; same verts.
// OBJ cannot encode painter order — depth bias belongs in the viewer
// (ShipModelAsset three-pass + normal bias / blender_decal_setup.py),
// not as an emit nudge. decalEpsilon is an optional screenshot hack; default 0.
// Do not plane-snap or delete hull faces.

// One lattice step in export world units: int8 vert * 256 → 16.16 → * scale.
// (256 / 65536) * kShipExportObjScale = 0.390625. Sub-lattice insets are
// painter offsets; viewer normal bias must stay in (maxInset, lattice).
constexpr float kShipExportLatticeUnit = 0.390625f;
// Provisional overlay normal bias until golden maxOverlayInset is frozen.
// Fleet audit (v10): overlay maxSub ≈ 0.172 → midpoint with lattice ≈ 0.281.
constexpr float kShipExportOverlayNormalBias = 0.28f;

struct ShipExportOptions {
    float decalEpsilon = 0.f;  // world units along outward geometric n
    // 0 → ShipExportCanonicalDist (fit clamp). Else absolute view-Z for both
    // ParseMeshBytecode LOD gates and TransformShipVertices pose.
    // Near (FP_ONE/8) is NOT always more hull tris — probe before changing CI.
    int32_t captureDist = 0;
};

// B3 viewer extras (D3D glass/punch numbers). Engines apply these; geometry
// Overlay: slope-scale + constant, depth write off, draw after hull.
constexpr float kShipExportPainterDepthBias = -4.0e-4f;
constexpr float kShipExportPainterSlopeScale = -1.0f;
// Punch windshields stay coplanar (vertex lift is a zoom-scaling seam).
// Constant GPU bias only — slope-scale is screen-space and changes with EYE.
constexpr float kShipExportPunchDepthBias = -4.0e-4f;
constexpr float kShipExportPunchSlopeScale = 0.0f;

// Fleet punch clearance (viewer-only, Policy D). Gap is OBJ units.
// nudge = max(0, maxGap) + slack when maxGap > epsilon; else 0.
constexpr float kShipExportPunchClearanceSlackObj = 0.25f;
constexpr float kShipExportPunchClearanceEpsilonObj = 0.05f;
// Local rear-plate burial only. Larger gaps are unbounded plane fits
// (windshield vs a distant hull facet) — treat as ineligible.
constexpr float kShipExportPunchClearanceMaxObj = 2.0f;

// Overlay vs hull (windshields, #14 capsule). Same idea as punch clearance:
// measure burial, add slack. Slack is tiny — overlay already LEQUAL-after-hull.
constexpr float kShipExportOverlayClearanceSlackObj = 0.02f;
constexpr float kShipExportOverlayClearanceMinObj = 0.008f;

// One welded vertex in export space (model origin, world units = fixed >> 16).
struct ShipExportVert {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 0, nz = 1;
    float u = 0, v = 0;
};

struct ShipExportMaterial {
    std::string name;
    std::string pngName;  // empty → unlit Kd only
    uint32_t kdArgb = 0xFFC0C0D0u;
    bool textured = false;
    bool transparent = false;
    bool punch = false;
    bool decal = false;  // 0x1000 surface overlay; verts unmoved
    uint16_t texInfo = 0;
    uint16_t bmpIndex = 0;
    uint16_t matIndex = 0;
    ShipBakedTexture baked;
};

// One Complex721 Bézier or edge, exported as an OBJ polyline (not a filled tri).
struct ShipExportPolyline {
    uint16_t texInfo = 0;
    int16_t contourId = -1;
    std::vector<ShipExportVert> pts;
};

struct ShipExportSpriteChild {
    int modelId = -1;
    float pos[3]{};
    float rot[9]{};
    int scaleRelated = 0;
};

// Emitter description harvested from MeshData (not TransformedPoly / ScreenHybrid).
// Positions are model-origin export space (same as hull verts: fixed>>16).
enum class ShipExportEmitterKind : uint8_t {
    NavLight = 0,
    Thruster = 1,
    Glow = 2,
};

struct ShipExportEmitter {
    ShipExportEmitterKind kind = ShipExportEmitterKind::NavLight;
    float x = 0, y = 0, z = 0;       // primary attach
    float x1 = 0, y1 = 0, z1 = 0;    // thruster second tip (axis)
    float rgb[3] = {1.f, 1.f, 1.f};   // 0..1
    float size = 0.f;                // thruster specialParam (raw)
    int vertexIdx = -1;
    int vertexIdx1 = -1;
    bool blink = false;
    uint16_t texInfo = 0;
};

// Stable triangle identity for sidecars / analysis (HW plan W1).
// Authored: (sourceIndex << 8) | (fanOrdinal & 0x7f) [| 0x80 if reverse side].
// Generated (no TransformedPoly): kShipExportPrimGeneratedBase | sequential.
constexpr uint32_t kShipExportPrimGeneratedBase = 0x80000000u;

inline uint32_t ShipExportMakePrimitiveId(int sourceIndex, int fanOrdinal,
                                          bool reverseSide)
{
    if (sourceIndex < 0) return 0;  // caller assigns generated ids
    uint32_t id = ((uint32_t)sourceIndex << 8) | ((uint32_t)fanOrdinal & 0x7fu);
    if (reverseSide) id |= 0x80u;
    return id;
}

struct ShipExportTriMeta {
    uint32_t primitiveId = 0;
    int sourceOrder = -1;   // TransformedPoly index (== painter order)
    uint8_t fanOrdinal = 0;
    bool reverseSide = false;
};

struct ShipExportMesh {
    int shipIndex = -1;
    std::string name;
    int32_t shipDist = 0;
    int worldFaceCount = 0;   // exported Hull+Punch+Line faces
    int hullTriCount = 0;     // after quad→tri, degenerates dropped (one-sided)
    int punchTriCount = 0;
    int decalTriCount = 0;    // 0x1000 overlays (not missile hulls); no vert offset
    int contourTriCount = 0;  // always 0 — filled ContourHull is paused
    int contourCurveCount = 0;
    int contourPtCount = 0;
    int lineTriCount = 0;     // one-sided ribbon tris (exported double-sided)
    int spriteChildCount = 0;
    int emitterCount = 0;
    int expectedTriCount = 0; // hull+punch+decal, independent count (T7)
    // Diagnostics: authored GetNormal vs geometric winding *before* DCC fixes.
    // Tex*Pair same-order ^1 expansion makes half the faces antiparallel to the
    // X-mirrored authored normal — not a GetNormal index bug (verified Krait).
    int authoredOpposedCount = 0; // residual cos(winding, authored) < -0.5 after Pair fix (fingerprint)
    int windingFlipCount = 0;     // residual flips inventing glTF CCW from authored n (fingerprint)
    int pairHandednessFixCount = 0; // conditional Tex*Pair swaps when cos<0 (also fingerprint)
    int vnSplitCount = 0;         // post-pass index swaps / vert splits (should stay 0)
    // Min |cos(winding, authored)| over faces with authored normals (after Pair,
    // before flip). Near-zero ⇒ geometric winding ⊥ authored n (often non-planar).
    float minAbsWindingCos = 1.f;
    // Faces with |cos| in [0.15, 0.25] — monitor only (flip uses authored sign, no threshold).
    int ambiguousCosBandCount = 0;
    // Quads: FFE scanline-fills 4 points; export must invent a diagonal (policy D tension).
    // Split is always 0-2 / 0-2-3 (fan); diag13Count stays 0 until a chooser exists.
    int nonPlanarQuadCount = 0;   // |v3 plane-dev from v0v1v2| > 1e-3 world units
    float maxNonPlanarDev = 0.f;  // max |out-of-plane| over quads (world units ≈ lattice)
    // Non-planar quads whose two diag-02 tris disagree on authored-sign flip
    // (cos0*cos1 < 0) — uniform face flip leaves one half opposed → tear risk.
    int nonPlanarFlipDisagreeCount = 0;
    int quadDiag02Count = 0;      // fans using diagonal 0–2
    int quadDiag13Count = 0;      // fans using diagonal 1–3 (currently unused)
    // Sub-lattice insets: |vert→nearest hull plane| in (0, lattice). Fingerprint
    // for viewer normal-bias constant (must clear max, stay < kShipExportLatticeUnit).
    float maxOverlaySubLatticeInset = 0.f;
    float maxPunchSubLatticeInset = 0.f;
    int overlaySubLatticeVertCount = 0;
    int punchSubLatticeVertCount = 0;
    // T14 diagnostic: tris whose X-mirror positions exist but no partner shares
    // the X-mirrored winding (allows two-sided coplanar plates). Not a hard
    // CI gate until the fleet census is classified.
    int mirrorWindingFailCount = 0;
    float contourPlanarity = 0;
    bool truncated = false;
    bool hasSpriteChild = false;
    bool missileFlat = false;  // all-0x1000 body; viewer two-sided (ASM 001481 bypass)
    bool valid = false;
    std::string error;
    std::vector<ShipExportVert> verts;
    std::vector<uint32_t> indices;     // triangle list
    std::vector<int> triMaterial;      // per triangle
    std::vector<ShipExportTriMeta> triMeta;  // parallel to triMaterial (W1)
    std::vector<ShipExportMaterial> materials;
    std::vector<ShipExportPolyline> curves;
    std::vector<ShipExportSpriteChild> sprites;
    std::vector<ShipExportEmitter> emitters;
};

// Canonical capture distance (§5): fit units, clamped to [FP_ONE/8, FP_ONE/2].
int32_t ShipExportCanonicalDist(const ShipModel* model);

const char* ShipExportName(int shipIndex);

// Parse → transform → hull/punch/line weld. Contours are harvested as
// polylines (no filled ContourHull). Requires VideoInit + InitShipModels +
// InitMeshOpcodeHandlers + a loaded data blob.
bool BuildExportMesh(int shipIndex, ShipExportMesh* out);
bool BuildExportMesh(int shipIndex, ShipExportMesh* out,
                     const ShipExportOptions& opt);

bool WriteRgbaPng(const char* path, int width, int height, const uint32_t* argb);

// Device-free PNG encode (RGBA8). Used by OBJ sidecars and embedded glTF images.
bool EncodeRgbaPng(int width, int height, const uint32_t* argb,
                   std::vector<uint8_t>* out);

// Writes model.obj, model.mtl, mat_*.png, children.json, emitters.json, and
// primitives.json into dir.
bool WriteExportObjMtl(const ShipExportMesh& mesh, const char* dir);

// primitives.json only (also invoked from WriteExportObjMtl).
bool WriteExportPrimitivesJson(const ShipExportMesh& mesh, const char* dir);

// Writes model.glb (glTF 2.0 binary) into dir — same geometry as OBJ, textures
// embedded, line materials doubleSided, punch/decal in material extras,
// emitter empty nodes with extras (kind/color/size). Root extras carry
// trianglePrimitiveIds[] matching mesh triangle order.
bool WriteExportGlb(const ShipExportMesh& mesh, const char* dir);

// Structural checks used by ctest (T1/T2/T4/T7). Returns 0 on pass.
int ShipExportCheckInvariants(const ShipExportMesh& mesh, const ShipModel* model);

// W1: unique primitiveIds, triMeta parallel to tris, two builds byte-identical.
// Returns 0 on pass.
int ShipExportCheckPrimitiveIds(const ShipExportMesh& mesh);

} // namespace jjffe

#endif
