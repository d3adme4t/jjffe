#ifndef JJFFE_FACE_ANALYSIS_H
#define JJFFE_FACE_ANALYSIS_H

#include "jjffe/ship_export.h"
#include "jjffe/ship_face_semantic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace jjffe {

// HW plan W2 — FaceRecord / Finding analysis over exported geometry.
// Policy D: analysis never mutates export/. Writes live under analysis/.

struct FacePlane {
    float n[3] = {0, 0, 1};
    float d = 0;  // n·x + d = 0  (d = -n·p0)
};

struct FaceAabb {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
};

enum FaceRecordFlag : uint32_t {
    kFaceFlagHull = 1u << 0,
    kFaceFlagOverlay = 1u << 1,
    kFaceFlagPunch = 1u << 2,
    kFaceFlagContour = 1u << 3,
    kFaceFlagLine = 1u << 4,
    kFaceFlagEmitterDerived = 1u << 5,
    kFaceFlagSuspect = 1u << 6,
    kFaceFlagReverse = 1u << 7,
};

struct FaceRecord {
    int model = -1;
    uint32_t primitive = 0;
    FaceSemantic cls = FaceSemantic::Unknown;
    int material = -1;
    int sourceOrder = -1;
    int triIndex = -1;          // index into ShipExportMesh tri arrays
    float v[3][3]{};            // triangle verts (export world)
    FacePlane plane;
    FaceAabb bounds;
    float area = 0.f;
    uint32_t flags = 0;
};

enum class FindingType : uint8_t {
    CoplanarOverlay = 0,
    PunchDepthRisk,
    NonplanarContour,
    Sliver,
    EmbeddedSurface,  // W3 GFWN ensemble
    Unknown
};

enum class RemediationHint : uint8_t {
    RenderSidecar = 0,
    GeometryCow,
    ContourFix,
    Review,
    None
};

struct Finding {
    int model = -1;
    uint32_t a = 0;  // probe primitive
    uint32_t b = 0;  // receiver / related (0 if N/A)
    FindingType type = FindingType::Unknown;
    float confidence = 0.f;
    float severity = 0.f;
    std::string evidence;
    RemediationHint hint = RemediationHint::None;
    std::string id;  // stable "finding-NNNN" within a ship analysis
};

struct ShipAnalysis {
    int shipIndex = -1;
    std::string name;
    bool valid = false;
    std::string error;
    int faceCount = 0;
    int findingCount = 0;
    int coplanarOverlay = 0;
    int punchDepthRisk = 0;
    int nonplanarContour = 0;
    int sliver = 0;
    int embeddedSurface = 0;           // W3
    float orientationCoherence = 0.f;  // W3 hygiene
    int hygieneKeptTris = 0;
    bool insideUsable = false;
    std::string sourceAssetHash;  // W4 sha256 binding
    std::vector<FaceRecord> faces;
    std::vector<Finding> findings;
};

// Graded thresholds (world = fixed>>16). B2 flag band stays 0.001; punch risk
// opens to Eagle's ≈0.00157 near-miss.
constexpr float kAnalysisCoplanarEps = 0.001f;
constexpr float kAnalysisPunchRiskEps = 0.0025f;
constexpr float kAnalysisNearVert = 0.20f;
constexpr float kAnalysisNormalCos = 0.866f;  // ~30°
constexpr float kAnalysisMinConfidence = 0.55f;
constexpr float kAnalysisSliverAspect = 40.f;

const char* FindingTypeName(FindingType t);
const char* RemediationHintName(RemediationHint h);

// Build FaceRecords + Findings for one ship (BuildExportMesh internally).
bool AnalyzeShipFaces(int shipIndex, ShipAnalysis* out);

// Same, reusing an already-built export mesh (no second parse).
bool AnalyzeShipFacesMesh(const ShipExportMesh& mesh, ShipAnalysis* out);

// Write analysis/<idx>_<name>.analysis.json (creates analysis/ as needed).
// Optional sourceAssetHash override; otherwise uses a.sourceAssetHash.
bool WriteShipAnalysisJson(const ShipAnalysis& a, const char* analysisDir);

// Structural + regression anchors for ctest ExportAnalysis. Returns 0 on pass.
int ShipAnalysisCheckAnchors(const ShipAnalysis* fleet, int nShips);

}  // namespace jjffe

#endif
