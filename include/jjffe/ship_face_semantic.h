#ifndef JJFFE_SHIP_FACE_SEMANTIC_H
#define JJFFE_SHIP_FACE_SEMANTIC_H

#include "jjffe/vertex_transform.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jjffe {

// Recovered role of one bytecode face. Stage 1 assigns only labels we can
// justify from texInfo / kind / missile census — not adjacency. Window and
// EngineCap are reserved; do not invent them from 0x1000 alone.
enum class FaceSemantic : uint8_t {
    Hull = 0,
    Overlay,       // 0x1000 surface decal (non-missile)
    Punch,         // 0x2000 / 0x3000 / 0x20ee cutout
    MissileHull,   // 0x1000 on an all-0x1000 missile body
    Line,          // 2-vert hull (DirLine / PlainLine)
    Contour,       // Complex721 harvested as polyline
    SpriteChild,   // Sprite749 instance (not exploded child faces)
    ScreenHybrid,  // thruster / glow / nav / glint
    EngineCap,     // reserved (Stage 1 unused)
    Window,        // reserved (Stage 1 unused)
    Unknown
};

struct FaceCensusRow {
    int face = -1;           // WorldFace index, or synthetic (< 0)
    int meshIndex = 0;
    int sourceIndex = -1;
    uint16_t texInfo = 0;
    MeshPolyType type = MeshPolyType::EndMarker;
    FaceSemantic semantic = FaceSemantic::Unknown;
    WorldFaceKind kind = WorldFaceKind::Hull;
    int vertexCount = 0;
    bool backFacing = false;
    bool visible = false;    // TransformedPoly.visible (not a pixel oracle)
    bool worldValid = false;
    bool afterSpecialPoly = false;
    uint8_t normalIdx = 0;
    uint32_t sortKey = 0;
    int spriteChild = -1;
    int16_t contourId = -1;
    // Stage 2 — front-door participation. Visibility stays unset.
    bool submittedSoft = false;
    bool culledSoft = false;
    bool submittedOgre = false;
    bool culledOgre = false;
};

const char* FaceSemanticName(FaceSemantic s);
const char* FaceExportBucketName(FaceSemantic s);
const char* MeshPolyTypeName(MeshPolyType t);

// Same census as ShipPrepareFlatPunchGates, no renderer globals.
bool FaceSemanticMissileFlat(const TransformedPoly* polys, int nPoly);
bool FaceSemanticCapsuleSilhouette(const TransformedPoly* polys, int nPoly);

FaceSemantic ClassifyFaceSemantic(const WorldFace& f, bool missileFlat);

// Soft: RenderShipFlat visible[] collect. Ogre: HW hull door (not punch-skip).
void ClassifyFaceParticipation(const WorldFace& f, const TransformedPoly* src,
                               bool missileFlat, bool capsule,
                               bool* submittedSoft, bool* culledSoft,
                               bool* submittedOgre, bool* culledOgre);

// Parse → transform → world faces + contour/sprite rows. Ships 0–71 Stage 1.
bool CollectShipFaceCensus(int shipIndex, std::vector<FaceCensusRow>* out,
                           bool* missileFlat, std::string* error);

// Same area/sliver gates as the OBJ emitter (Stage 1.5).
bool FaceExportTriDegenerate(float ax, float ay, float az,
                             float bx, float by, float bz,
                             float cx, float cy, float cz);
bool FaceExportTriSliver(float ax, float ay, float az,
                         float bx, float by, float bz,
                         float cx, float cy, float cz);

struct SemanticReconcileBucket {
    int faces = 0;
    int tris = 0;
    int exported = 0;
    int degenerate = 0;
    int sliver = 0;
    int skipped = 0;  // !worldValid / not an export kind / vc<3
    int lost = 0;     // tris not in exported+degenerate+sliver+skipped
};

struct FaceReconcileDrop {
    int face = -1;
    int meshIndex = 0;
    uint16_t texInfo = 0;
    FaceSemantic semantic = FaceSemantic::Unknown;
    const char* reason = "";
};

struct ShipExportReconcile {
    int shipIndex = -1;
    bool missileFlat = false;
    bool valid = false;
    std::string error;
    SemanticReconcileBucket bySem[16]{};
    int meshHull = 0, meshPunch = 0, meshDecal = 0, meshLine = 0;
    int mismatchHull = 0, mismatchPunch = 0, mismatchDecal = 0, mismatchLine = 0;
    std::vector<FaceReconcileDrop> drops;  // non-exported Overlay/Punch/Hull
};

bool CollectShipExportReconcile(int shipIndex, ShipExportReconcile* out);

// Phase B2 — coplanar Hull↔Overlay / Hull↔Punch census (world units, fixed>>16).
// Per overlay/punch vert: |signed dist| to nearest nearby hull plane.
// Face distance = worst vert residual. Nearby = MinVertVert < kCoplanarNearVert.
constexpr float kCoplanarEpsilon = 0.001f;     // flag band (≈ punch nudge)
constexpr float kCoplanarNearVert = 0.20f;     // spatial gate (world)

struct CoplanarHit {
    int probeFace = -1;          // WorldFace index (overlay or punch)
    int nearestHull = -1;
    int meshIndex = 0;
    uint16_t texInfo = 0;
    FaceSemantic semantic = FaceSemantic::Unknown;
    int vertexCount = 0;
    float distance = 1e30f;      // worst per-vert nearest-plane residual
    float centroidDist = 1e30f;
    bool flagged = false;        // distance < kCoplanarEpsilon
};

struct ShipCoplanarCensus {
    int shipIndex = -1;
    bool valid = false;
    bool missileFlat = false;
    std::string error;
    int hullFaces = 0;
    int overlayFaces = 0;
    int punchFaces = 0;
    int overlayFlagged = 0;
    int punchFlagged = 0;
    std::vector<CoplanarHit> hits;  // one per overlay/punch with a hull neighbor
};

bool CollectShipCoplanarCensus(int shipIndex, ShipCoplanarCensus* out);

// Phase B1 — CPU raster participation at the canonical export pose.
// Hull is drawn first (Z-write). Overlay/Punch are tested, not written.
//   LESS     = typical DCC/GPU default (ties lose → coplanar burial)
//   LEQUAL   = Ogre HW (draw-after-hull; ties win)
//   bias     = probe camera-z minus 0.001 world (B3-scale)
constexpr int kRasterWidth = 320;
constexpr int kRasterHeight = 200;
constexpr float kRasterZEps = 1.f;  // ProjectPoint z is 16.16; ignore 1-unit noise
constexpr float kRasterPainterZBias = 0.001f * 65536.f;  // 0.001 world → camera z

struct RasterFaceHit {
    int face = -1;
    int meshIndex = 0;
    uint16_t texInfo = 0;
    FaceSemantic semantic = FaceSemantic::Unknown;
    int vertexCount = 0;
    bool backFacing = false;
    bool submittedSoft = false;
    bool submittedOgre = false;
    int pixCovered = 0;
    int pixPassLess = 0;
    int pixPassLEqual = 0;
    int pixPassBias = 0;
    bool rasterized = false;          // pixCovered > 0
    bool rejectedLess = false;        // covered, none pass LESS
    bool rejectedLEqual = false;
    bool rejectedBias = false;
};

struct ShipRasterCensus {
    int shipIndex = -1;
    bool valid = false;
    bool missileFlat = false;
    std::string error;
    int hullFaces = 0;
    int hullRasterized = 0;
    int overlayFaces = 0;
    int overlaySubmitted = 0;
    int overlayRasterized = 0;
    int overlayRejectedLess = 0;
    int overlayRejectedLEqual = 0;
    int overlayRejectedBias = 0;
    int overlayPix = 0;
    int overlayPixLess = 0;
    int overlayPixLEqual = 0;
    int overlayPixBias = 0;
    int punchFaces = 0;
    int punchSubmitted = 0;
    int punchRasterized = 0;
    int punchRejectedLess = 0;
    int punchRejectedLEqual = 0;
    int punchRejectedBias = 0;
    int punchPix = 0;
    int punchPixLess = 0;
    int punchPixLEqual = 0;
    int punchPixBias = 0;
    std::vector<RasterFaceHit> hits;  // overlay + punch
};

bool CollectShipRasterCensus(int shipIndex, ShipRasterCensus* out);

} // namespace jjffe

#endif
