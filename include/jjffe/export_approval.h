#ifndef JJFFE_EXPORT_APPROVAL_H
#define JJFFE_EXPORT_APPROVAL_H

#include "jjffe/face_analysis.h"
#include "jjffe/ship_export.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jjffe {

constexpr int kRenderPolicyVersion = 1;

struct RenderClassEntry {
    std::string cls;       // OverlayCoplanar, PunchDepthBias, EmbeddedReview, …
    uint32_t receiver = 0; // W1 prim id (0 = none)
    int layer = 0;
    float biasConstant = 0.f;
    float biasSlope = 0.f;
    bool depthWrite = false;
};

struct RenderPolicySidecar {
    std::string sourceAssetHash;
    int policyVersion = kRenderPolicyVersion;
    std::string approvedBy;
    std::vector<std::string> createdFromFindings;
    // key = primitive id
    std::unordered_map<uint32_t, RenderClassEntry> renderClasses;
    // Viewer-only: translate punch verts aft (−Z export / OBJ units) so
    // 0x20ee nozzles clear rear decal gaps. 0 = authored pose (default).
    // Per-ship — different models need different gap clearance.
    float punchAftNudgeObj = 0.f;
    std::string path;  // last load path (optional)
    bool valid = false;
    std::string error;
};

struct RemediationSidecar {
    std::string sourceAssetHash;
    int policyVersion = kRenderPolicyVersion;
    std::string approvedBy;
    std::vector<std::string> createdFromFindings;
    // finding id → hint name
    std::unordered_map<std::string, std::string> remediations;
    bool valid = false;
    std::string error;
};

struct ApprovalValidateResult {
    bool ok = false;
    std::string error;
    enum class Code : int {
        Ok = 0,
        Io = 1,
        Parse = 2,
        HashMismatch = 3,
        UnknownPrimitive = 4,
        NoFindings = 5,
        MissingField = 6,
    } code = Code::Ok;
};

// Canonical in-memory hash of an export mesh (stable across machines).
// Prefer this when export/ is not on disk yet.
std::string ComputeShipExportAssetHash(const ShipExportMesh& mesh);

// Hash immutable on-disk export folder. Prefer model.glb; else length-prefixed
// concatenation of model.obj + primitives.json + emitters.json (sorted names).
std::string ComputeExportDirAssetHash(const char* exportShipDir);

// Per-ship manifest.json + materials.json inside an export ship directory.
bool WriteExportShipManifest(const ShipExportMesh& mesh, const char* dir,
                             const char* sourceAssetHash);
bool WriteExportMaterialsJson(const ShipExportMesh& mesh, const char* dir);

// Build policy + remediation from analysis findings (does not write).
bool BuildApprovalSidecars(const ShipAnalysis& analysis,
                           const std::string& sourceAssetHash,
                           const char* approvedBy, RenderPolicySidecar* policy,
                           RemediationSidecar* rem);

bool WriteRenderPolicyJson(const RenderPolicySidecar& p, const char* path);
bool WriteRemediationSidecarJson(const RemediationSidecar& r, const char* path);

bool LoadRenderPolicyJson(const char* path, RenderPolicySidecar* out);

// Staleness + schema gates (W4.7). knownPrims = W1 ids from export mesh.
ApprovalValidateResult ValidateRenderPolicy(
    const RenderPolicySidecar& policy, const std::string& currentAssetHash,
    const std::unordered_set<uint32_t>& knownPrims);

// Convenience: approve analysis → write approved/<idx>_<name>.*.json
// Optional: copy model.glb → approved/<name>.fixed.glb when writeFixedGlb.
bool ApproveShipAnalysis(const ShipAnalysis& analysis,
                         const char* exportShipDir, const char* approvedDir,
                         const char* approvedBy, bool writeFixedGlb,
                         std::string* errOut);

// Collect W1 primitive ids from mesh.
void CollectExportPrimitiveIds(const ShipExportMesh& mesh,
                               std::unordered_set<uint32_t>* out);

// Structural ctest gate. Returns 0 on pass.
int ExportApprovalCheckGates();

}  // namespace jjffe

#endif
