#ifndef JJFFE_SHIP_MODEL_ASSET_H
#define JJFFE_SHIP_MODEL_ASSET_H

#include "jjffe/ship_export.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jjffe {

// CPU-side ship built from export/ (OBJ groups or ShipExportMesh).
// Renderer backends (D3D / Ogre / GL) upload these groups and apply
// ShipAssetPassState — geometry stays exact (policy D); bias is viewer-only.

enum class ShipAssetPass : uint8_t {
    Hull = 0,
    Punch = 1,
    Overlay = 2,
    Line = 3,
};

enum class ShipAssetDepthFunc : uint8_t {
    Less = 0,
    LEqual = 1,
};

struct ShipAssetPassState {
    bool depthWrite = true;
    ShipAssetDepthFunc depthFunc = ShipAssetDepthFunc::Less;
    float normalBias = 0.f;  // OBJ units along unit normal at upload; 0 for hull
    bool cullBack = false;         // viewer two-sided (export winding unchanged)
    bool frontCCW = true;  // glTF / export winding
    // W5c: when true, D3DRS_DEPTHBIAS / SLOPESCALEDEPTHBIAS use these instead
    // of the pass defaults from kShipExportPainter*.
    bool customDepthBias = false;
    float depthBiasConstant = 0.f;
    float depthBiasSlope = 0.f;
};

// Resolve pass state. overlayBias is overlay/line only (analytic park, ~0.02).
// Punch windshields are coplanar (punchBias 0) — a world offset scales with
// zoom. Punch still depth-writes so overlay cannot bury 0x20ee. D3D: overlay
// gets painter slope-scale; punch gets constant GPU bias only.
// All passes are two-sided (cull off); hull Z-write still hides closed interiors.
ShipAssetPassState ShipAssetPassFor(
    ShipAssetPass pass,
    float overlayBias = kShipExportOverlayNormalBias,
    float punchBias = 0.f);

struct ShipAssetGroup {
    ShipAssetPass pass = ShipAssetPass::Hull;
    std::vector<ShipExportVert> verts;
    std::vector<uint32_t> indices;  // triangle list
    // Parallel to indices/3 — stable export primitive ids (W1). Empty if
    // loaded from OBJ without primitives.json.
    std::vector<uint32_t> primitiveIds;
    // Parallel to primitiveIds — TransformedPoly / painter order (W1/W5d).
    std::vector<int> sourceOrders;
    // Parallel to nTri — export material Kd (matches glTF/OBJ). Empty → solid.
    std::vector<uint32_t> triKdArgb;
    // Parallel to nTri — index into ShipModelAsset::materials. -1 unknown.
    std::vector<int> triMaterials;
    int materialHint = -1;          // first material index if known
};

struct ShipAssetPrimPolicy {
    uint32_t receiver = 0;
    int layer = 0;
    bool depthWrite = false;
    bool customDepthBias = false;
    float depthBiasConstant = 0.f;
    float depthBiasSlope = 0.f;
};

struct ShipModelAsset {
    int shipIndex = -1;
    std::string name;
    float overlayNormalBias = kShipExportOverlayNormalBias;
    float punchNormalBias = 0.f;
    float maxOverlayInset = 0.f;
    float maxPunchInset = 0.f;
    ShipAssetGroup hull;
    ShipAssetGroup punch;
    ShipAssetGroup overlay;
    ShipAssetGroup line;
    std::vector<ShipExportEmitter> emitters;
    std::vector<ShipExportPolyline> curves;
    std::vector<ShipExportSpriteChild> sprites;
    // All-0x1000 missile body (ASM FUNC_001481 bypass). Viewer draws two-sided.
    bool missileFlat = false;
    // Export materials (Kd + baked textures) for GPU tint/tex parity with glTF.
    std::vector<ShipExportMaterial> materials;
    // W5c — optional approved render policy (never mutates export/).
    bool hasRenderPolicy = false;
    int policyClassCount = 0;
    std::string sourceAssetHash;
    std::string renderPolicyNote;  // "none" / "applied" / "stale:…" / "invalid:…"
    std::unordered_map<uint32_t, ShipAssetPrimPolicy> primPolicies;
    // Renderer contract: OBJ-unit aft translate (−Z). Analytic unless a
    // non-zero render_policy.punchAftNudgeObj overrides.
    float punchAftNudgeObj = 0.f;
    float punchClearanceComputedObj = 0.f;  // pre-override analytic
    float punchMaxGapObj = 0.f;             // max tip-vs-plane burial (OBJ)
    int punchAftPlaneCount = 0;
    bool punchClearancePolicyOverride = false;
    bool valid = false;
    std::string error;
};

// Midpoint bias that clears `maxOverlayInset` and stays under one lattice unit.
// Coplanar overlay (inset ≤ 0) uses overlay-clearance slack, not the 0.28 fleet
// constant (that lifts #14 windshields off the hull).
float ShipExportRecommendedOverlayBias(float maxOverlayInset);

// Build from an in-memory export mesh (no disk round-trip).
bool BuildShipModelAsset(const ShipExportMesh& mesh, ShipModelAsset* out);

// Load from an export directory (model.obj + emitters.json).
// Groups by object name: *_punch → punch, *_decal → overlay, line_* → line,
// else hull (curves stay in mesh.curves if loaded from BuildExportMesh only;
// OBJ polylines are skipped — use BuildShipModelAsset for curves).
bool LoadShipModelAssetDir(const char* dir, ShipModelAsset* out);

// Draw order for opaque ship geometry (emitters / curves are separate).
inline constexpr ShipAssetPass kShipAssetDrawOrder[] = {
    ShipAssetPass::Hull,
    ShipAssetPass::Punch,
    ShipAssetPass::Overlay,
    ShipAssetPass::Line,
};

const ShipAssetGroup* ShipAssetGroupFor(const ShipModelAsset& a, ShipAssetPass p);

// One punch triangle vs its nearest aft overlay/hull plane.
struct ShipPunchClearanceCluster {
    int tri = -1;
    float cx = 0.f, cy = 0.f, cz = 0.f;  // world centroid
    float maxGapObj = 0.f;
    int blockingKind = 0;  // 0=overlay, 1=hull
};

struct ShipPunchClearance {
    float maxGapObj = 0.f;           // overlay-plane burial (drives nudge)
    float hullMaxGapObj = 0.f;       // diagnostic; does not move punch
    float recommendedNudgeObj = 0.f;
    int punchTris = 0;
    int overlayAftPlanes = 0;
    int hullAftPlanes = 0;
    std::vector<ShipPunchClearanceCluster> clusters;
};

struct ShipPunchVisibilityPose {
    float yawDeg = 0.f;
    float pitchDeg = 0.f;
    int punchPixBefore = 0;
    int punchPixAfter = 0;
};

// Fleet-wide analytic: punch tips vs nearest aft-facing overlay (then hull)
// plane. Units OBJ. Returns 0 when maxGap ≤ epsilon (unaffected ships).
ShipPunchClearance ShipAssetComputePunchClearance(
    const ShipAssetGroup& punch, const ShipAssetGroup& overlay,
    const ShipAssetGroup& hull,
    float slackObj = kShipExportPunchClearanceSlackObj,
    float epsilonObj = kShipExportPunchClearanceEpsilonObj,
    float maxGapObj = kShipExportPunchClearanceMaxObj);

// Thin wrapper: overlay-only (no hull), for callers that have no hull group.
float ShipAssetComputePunchAftNudgeObj(
    const ShipAssetGroup& punch, const ShipAssetGroup& overlay,
    float clearanceSlackObj = kShipExportPunchClearanceSlackObj);

// Stamp overlayNormalBias from hull overlap + inset (does not mutate export/).
void ShipAssetStampOverlayClearance(ShipModelAsset* asset);

// Stamp punchNormalBias for nose 0x2000. Always 0 — a world offset is a
// zoom-scaling seam. D3D wins the z-test with constant GPU depth bias.
void ShipAssetStampPunchNormalBias(ShipModelAsset* asset);

// Viewer-only: offset punch verts along authored normals. Nose (centroid z>=0)
// uses noseBiasObj; aft 0x20ee keeps aftBiasObj. Does not mutate export/.
void ShipAssetOffsetPunchVerts(ShipAssetGroup* punch, float noseBiasObj,
                               float aftBiasObj);

// Move nose 0x2000 verts onto the overlapping hull plate (outward n) plus slack.
// Shuttle glass sits inside a larger front plate with opposing winding — a
// fixed vertex lift either buries the pane or floats it as EYE changes.
void ShipAssetParkPunchWindshields(ShipAssetGroup* punch,
                                   const ShipAssetGroup& hull,
                                   float slackObj = kShipExportOverlayClearanceMinObj);

// Stamp punchClearanceComputedObj / punchAftNudgeObj on a built asset
// (does not apply policy override).
void ShipAssetStampPunchClearance(ShipModelAsset* asset);

// Build export mesh + asset and compute clearance (ships 0–71 census).
bool CollectShipPunchClearance(int shipIndex, ShipPunchClearance* out,
                               ShipModelAsset* assetOut = nullptr);

// CPU z-buffer (320×200): hull+overlay write, then count punch pixels that
// pass LESS. yaw/pitch in HUD degrees (180 = rear). Optional aft nudge on punch.
int ShipAssetPunchVisiblePixels(const ShipModelAsset& asset, float yawDeg,
                                float pitchDeg, float punchAftNudgeObj);

bool ShipAssetCollectPunchVisibility(const ShipModelAsset& asset, float yawDeg,
                                     float pitchDeg,
                                     ShipPunchVisibilityPose* out);

// W5c: load approved/<idx>_<name>.render_policy.json, validate hash + prims.
// On failure: hasRenderPolicy=false and note explains why (runtime falls back).
// Does not read or write export/.
bool AttachShipRenderPolicy(ShipModelAsset* asset, const char* approvedDir,
                            const std::string& sourceAssetHash,
                            const std::unordered_set<uint32_t>& knownPrims);

// W5c: ShipAssetPassFor defaults, overridden by sidecar renderClasses[prim].
ShipAssetPassState ShipAssetResolvePassState(const ShipModelAsset& asset,
                                             ShipAssetPass pass,
                                             uint32_t primitiveId);

// Contiguous draw range after ShipAssetPlanPassRanges (IB index units).
struct ShipAssetDrawRange {
    int startIndex = 0;  // into group.indices
    int indexCount = 0;  // multiple of 3
    ShipAssetPassState state;
};

// W5d (light): sort overlay/punch tris by (receiver, layer, sourceOrder) and
// emit contiguous state ranges for the GPU. No-op if no primitiveIds.
bool ShipAssetPlanPassRanges(ShipModelAsset* asset, ShipAssetPass pass,
                             std::vector<ShipAssetDrawRange>* ranges);

}  // namespace jjffe

#endif
