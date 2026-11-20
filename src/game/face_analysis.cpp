#include "jjffe/face_analysis.h"

#include "jjffe/export_approval.h"
#include "jjffe/face_inside.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>

namespace jjffe {

const char* FindingTypeName(FindingType t)
{
    switch (t) {
    case FindingType::CoplanarOverlay: return "COPLANAR_OVERLAY";
    case FindingType::PunchDepthRisk: return "PUNCH_DEPTH_RISK";
    case FindingType::NonplanarContour: return "NONPLANAR_CONTOUR";
    case FindingType::Sliver: return "SLIVER";
    case FindingType::EmbeddedSurface: return "EMBEDDED_SURFACE";
    default: return "UNKNOWN";
    }
}

const char* RemediationHintName(RemediationHint h)
{
    switch (h) {
    case RemediationHint::RenderSidecar: return "render_sidecar";
    case RemediationHint::GeometryCow: return "geometry_cow";
    case RemediationHint::ContourFix: return "contour_fix";
    case RemediationHint::Review: return "review";
    default: return "none";
    }
}

namespace {

bool EnsureDir(const char* path)
{
    if (!path || !path[0]) return false;
    struct stat st {};
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

float TriArea(const float a[3], const float b[3], const float c[3])
{
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const float cx = uy * vz - uz * vy;
    const float cy = uz * vx - ux * vz;
    const float cz = ux * vy - uy * vx;
    return 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
}

bool TriPlane(const float a[3], const float b[3], const float c[3], FacePlane* p)
{
    const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12f) return false;
    nx /= len;
    ny /= len;
    nz /= len;
    p->n[0] = nx;
    p->n[1] = ny;
    p->n[2] = nz;
    p->d = -(nx * a[0] + ny * a[1] + nz * a[2]);
    return true;
}

void TriAabb(const float a[3], const float b[3], const float c[3], FaceAabb* box)
{
    for (int i = 0; i < 3; i++) {
        box->min[i] = std::min(a[i], std::min(b[i], c[i]));
        box->max[i] = std::max(a[i], std::max(b[i], c[i]));
    }
}

float TriAspect(const float a[3], const float b[3], const float c[3])
{
    auto len = [](const float* p, const float* q) {
        const float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const float e0 = len(a, b), e1 = len(b, c), e2 = len(c, a);
    const float mx = std::max(e0, std::max(e1, e2));
    const float mn = std::max(1e-12f, std::min(e0, std::min(e1, e2)));
    return mx / mn;
}

FaceSemantic ClassFromMat(const ShipExportMaterial& m)
{
    if (m.decal) return FaceSemantic::Overlay;
    if (m.punch) return FaceSemantic::Punch;
    if (m.name.rfind("line_", 0) == 0) return FaceSemantic::Line;
    return FaceSemantic::Hull;
}

uint32_t FlagsFromClass(FaceSemantic s, bool reverse)
{
    uint32_t f = 0;
    switch (s) {
    case FaceSemantic::Overlay: f |= kFaceFlagOverlay; break;
    case FaceSemantic::Punch: f |= kFaceFlagPunch; break;
    case FaceSemantic::Line: f |= kFaceFlagLine; break;
    case FaceSemantic::Contour: f |= kFaceFlagContour; break;
    default: f |= kFaceFlagHull; break;
    }
    if (reverse) f |= kFaceFlagReverse;
    return f;
}

float Clampf(float x, float lo, float hi)
{
    return std::max(lo, std::min(hi, x));
}

}  // namespace

bool AnalyzeShipFaces(int shipIndex, ShipAnalysis* out)
{
    if (!out) return false;
    ShipExportMesh mesh;
    if (!BuildExportMesh(shipIndex, &mesh) || !mesh.valid) {
        *out = ShipAnalysis{};
        out->shipIndex = shipIndex;
        out->error = mesh.error.empty() ? "BuildExportMesh failed" : mesh.error;
        return false;
    }
    return AnalyzeShipFacesMesh(mesh, out);
}

bool AnalyzeShipFacesMesh(const ShipExportMesh& mesh, ShipAnalysis* out)
{
    if (!out) return false;
    *out = ShipAnalysis{};
    out->shipIndex = mesh.shipIndex;
    out->name = mesh.name;
    if (!mesh.valid) {
        out->error = "invalid mesh";
        return false;
    }

    const int nTri = (int)mesh.indices.size() / 3;
    out->faces.reserve((size_t)nTri);
    // WorldFace.sourceIndex → first front-side export primitive id.
    std::unordered_map<int, uint32_t> primBySource;
    for (int t = 0; t < nTri; t++) {
        const int mi =
            (t < (int)mesh.triMaterial.size()) ? mesh.triMaterial[(size_t)t] : -1;
        if (mi < 0 || mi >= (int)mesh.materials.size()) continue;
        FaceRecord fr;
        fr.model = mesh.shipIndex;
        fr.triIndex = t;
        fr.material = mi;
        if (t < (int)mesh.triMeta.size()) {
            fr.primitive = mesh.triMeta[(size_t)t].primitiveId;
            fr.sourceOrder = mesh.triMeta[(size_t)t].sourceOrder;
            if (!mesh.triMeta[(size_t)t].reverseSide && fr.sourceOrder >= 0 &&
                primBySource.find(fr.sourceOrder) == primBySource.end())
                primBySource[fr.sourceOrder] = fr.primitive;
        }
        const ShipExportVert& va = mesh.verts[mesh.indices[(size_t)t * 3 + 0]];
        const ShipExportVert& vb = mesh.verts[mesh.indices[(size_t)t * 3 + 1]];
        const ShipExportVert& vc = mesh.verts[mesh.indices[(size_t)t * 3 + 2]];
        fr.v[0][0] = va.x;
        fr.v[0][1] = va.y;
        fr.v[0][2] = va.z;
        fr.v[1][0] = vb.x;
        fr.v[1][1] = vb.y;
        fr.v[1][2] = vb.z;
        fr.v[2][0] = vc.x;
        fr.v[2][1] = vc.y;
        fr.v[2][2] = vc.z;
        fr.area = TriArea(fr.v[0], fr.v[1], fr.v[2]);
        if (!TriPlane(fr.v[0], fr.v[1], fr.v[2], &fr.plane)) continue;
        TriAabb(fr.v[0], fr.v[1], fr.v[2], &fr.bounds);
        fr.cls = ClassFromMat(mesh.materials[(size_t)mi]);
        const bool rev =
            (t < (int)mesh.triMeta.size()) && mesh.triMeta[(size_t)t].reverseSide;
        fr.flags = FlagsFromClass(fr.cls, rev);
        if (fr.area < 1e-10f) fr.flags |= kFaceFlagSuspect;
        out->faces.push_back(fr);
    }
    out->faceCount = (int)out->faces.size();

    auto pushFinding = [&](FindingType type, uint32_t a, uint32_t b, float conf,
                           float sev, const char* evidence, RemediationHint hint) {
        if (conf < kAnalysisMinConfidence) return;
        Finding f;
        f.model = mesh.shipIndex;
        f.a = a;
        f.b = b;
        f.type = type;
        f.confidence = Clampf(conf, 0.f, 1.f);
        f.severity = Clampf(sev, 0.f, 1.f);
        f.evidence = evidence;
        f.hint = hint;
        char id[32];
        std::snprintf(id, sizeof(id), "finding-%04d",
                      (int)out->findings.size() + 1);
        f.id = id;
        out->findings.push_back(std::move(f));
        if (type == FindingType::CoplanarOverlay) out->coplanarOverlay++;
        else if (type == FindingType::PunchDepthRisk) out->punchDepthRisk++;
        else if (type == FindingType::Sliver) out->sliver++;
        else if (type == FindingType::NonplanarContour) out->nonplanarContour++;
    };

    // Face-level B2 census is the coplanar oracle: export triangulation can
    // invent different planes on non-planar quads (gecko/viper overlays).
    ShipCoplanarCensus census;
    if (CollectShipCoplanarCensus(mesh.shipIndex, &census) && census.valid) {
        // Need WorldFace.sourceIndex for W1 prim mapping — rebuild lightly.
        // CoplanarHit.probeFace is a WorldFace index; CollectShipFaceCensus
        // rows carry sourceIndex at the same face index for world faces.
        std::vector<FaceCensusRow> rows;
        bool miss = false;
        std::string err;
        std::unordered_map<int, int> faceToSource;  // WorldFace idx → sourceIndex
        if (CollectShipFaceCensus(mesh.shipIndex, &rows, &miss, &err)) {
            for (const FaceCensusRow& r : rows) {
                if (r.face >= 0 && r.sourceIndex >= 0)
                    faceToSource[r.face] = r.sourceIndex;
            }
        }

        auto primForFace = [&](int faceIdx) -> uint32_t {
            auto it = faceToSource.find(faceIdx);
            if (it != faceToSource.end()) {
                auto pit = primBySource.find(it->second);
                if (pit != primBySource.end()) return pit->second;
                return ShipExportMakePrimitiveId(it->second, 0, false);
            }
            // Stable fallback keyed on WorldFace index (not W1, but unique).
            return 0x40000000u | (uint32_t)faceIdx;
        };

        for (const CoplanarHit& h : census.hits) {
            const uint32_t probePrim = primForFace(h.probeFace);
            const uint32_t hullPrim =
                (h.nearestHull >= 0) ? primForFace(h.nearestHull) : 0u;
            char ev[192];
            if (h.semantic == FaceSemantic::Overlay) {
                const bool flagged = h.flagged;
                const bool near = h.distance < 4.f * kAnalysisCoplanarEps;
                if (!flagged && !near) continue;
                const float conf =
                    flagged
                        ? (0.85f +
                           0.10f * (1.f - Clampf(h.distance / kAnalysisCoplanarEps,
                                                 0.f, 1.f)))
                        : (0.55f +
                           0.30f * (1.f - Clampf(h.distance /
                                                     (4.f * kAnalysisCoplanarEps),
                                                 0.f, 1.f)));
                const float sev =
                    1.f - Clampf(h.distance / (4.f * kAnalysisCoplanarEps), 0.f,
                                 1.f);
                std::snprintf(ev, sizeof(ev),
                              "B2 overlay face=%d hull=%d dist=%.5f flagged=%d",
                              h.probeFace, h.nearestHull, h.distance,
                              flagged ? 1 : 0);
                pushFinding(FindingType::CoplanarOverlay, probePrim, hullPrim,
                            conf, sev, ev, RemediationHint::RenderSidecar);
            } else if (h.semantic == FaceSemantic::Punch) {
                if (!(h.flagged || h.distance < kAnalysisPunchRiskEps)) continue;
                const float conf =
                    0.60f +
                    0.35f * (1.f - Clampf(h.distance / kAnalysisPunchRiskEps, 0.f,
                                          1.f));
                const float sev =
                    1.f - Clampf(h.distance / kAnalysisPunchRiskEps, 0.f, 1.f);
                std::snprintf(ev, sizeof(ev),
                              "B2 punch face=%d hull=%d dist=%.5f flagged=%d",
                              h.probeFace, h.nearestHull, h.distance,
                              h.flagged ? 1 : 0);
                pushFinding(FindingType::PunchDepthRisk, probePrim, hullPrim,
                            conf, sev, ev, RemediationHint::RenderSidecar);
            }
        }
    }

    // Sliver: export-tri aspect ratio.
    for (const FaceRecord& fr : out->faces) {
        if (fr.flags & kFaceFlagReverse) continue;
        if (fr.area < 1e-12f) continue;
        const float aspect = TriAspect(fr.v[0], fr.v[1], fr.v[2]);
        if (aspect < kAnalysisSliverAspect) continue;
        char ev[96];
        std::snprintf(ev, sizeof(ev), "tri aspect=%.1f area=%.6g", aspect,
                      fr.area);
        pushFinding(FindingType::Sliver, fr.primitive, 0,
                    Clampf((aspect - kAnalysisSliverAspect) /
                               (kAnalysisSliverAspect * 3.f),
                           0.55f, 1.f),
                    Clampf(aspect / (kAnalysisSliverAspect * 4.f), 0.f, 1.f), ev,
                    RemediationHint::Review);
    }

    if (mesh.nonPlanarQuadCount > 0 && mesh.maxNonPlanarDev > 0.05f) {
        char ev[128];
        std::snprintf(ev, sizeof(ev),
                      "nonPlanarQuads=%d maxDev=%.4f flipDisagree=%d",
                      mesh.nonPlanarQuadCount, mesh.maxNonPlanarDev,
                      mesh.nonPlanarFlipDisagreeCount);
        pushFinding(FindingType::NonplanarContour, 0, 0,
                    Clampf(mesh.maxNonPlanarDev / 2.f, 0.55f, 1.f),
                    Clampf((float)mesh.nonPlanarFlipDisagreeCount / 4.f, 0.f, 1.f),
                    ev, RemediationHint::ContourFix);
    }

    // W3 — GFWN ensemble (after coplanar findings so overlays can be skipped).
    ShipInsideReport inside;
    if (AnalyzeShipInside(*out, &inside)) {
        out->orientationCoherence = inside.hygiene.orientationCoherence;
        out->hygieneKeptTris = inside.hygiene.keptTris;
        out->insideUsable = inside.hygiene.usable;
        for (Finding& f : inside.findings) {
            char id[32];
            std::snprintf(id, sizeof(id), "finding-%04d",
                          (int)out->findings.size() + 1);
            f.id = id;
            out->findings.push_back(std::move(f));
            out->embeddedSurface++;
        }
    }

    out->sourceAssetHash = ComputeShipExportAssetHash(mesh);
    out->findingCount = (int)out->findings.size();
    out->valid = true;
    return true;
}

bool WriteShipAnalysisJson(const ShipAnalysis& a, const char* analysisDir)
{
    if (!a.valid || !analysisDir) return false;
    if (!EnsureDir(analysisDir)) return false;
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%03d_%s.analysis.json", analysisDir,
                  a.shipIndex, a.name.c_str());
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    fprintf(f,
            "{\n  \"shipIndex\": %d,\n  \"name\": \"%s\",\n"
            "  \"policyVersion\": 1,\n  \"faceCount\": %d,\n"
            "  \"findingCount\": %d,\n"
            "  \"sourceAssetHash\": \"%s\",\n"
            "  \"hygiene\": {\"keptTris\": %d, \"orientationCoherence\": %.4f, "
            "\"insideUsable\": %s},\n"
            "  \"counts\": {\"coplanarOverlay\": %d, \"punchDepthRisk\": %d, "
            "\"nonplanarContour\": %d, \"sliver\": %d, \"embeddedSurface\": %d},\n"
            "  \"findings\": [\n",
            a.shipIndex, a.name.c_str(), a.faceCount, a.findingCount,
            a.sourceAssetHash.c_str(), a.hygieneKeptTris, a.orientationCoherence,
            a.insideUsable ? "true" : "false", a.coplanarOverlay,
            a.punchDepthRisk, a.nonplanarContour, a.sliver, a.embeddedSurface);
    for (size_t i = 0; i < a.findings.size(); i++) {
        const Finding& g = a.findings[i];
        std::string ev = g.evidence;
        for (size_t p = 0; (p = ev.find('"', p)) != std::string::npos; p += 2)
            ev.replace(p, 1, "\\\"");
        fprintf(f,
                "    {\"id\": \"%s\", \"type\": \"%s\", \"a\": %u, \"b\": %u, "
                "\"confidence\": %.4f, \"severity\": %.4f, \"hint\": \"%s\", "
                "\"evidence\": \"%s\"}%s\n",
                g.id.c_str(), FindingTypeName(g.type), g.a, g.b, g.confidence,
                g.severity, RemediationHintName(g.hint), ev.c_str(),
                (i + 1 < a.findings.size()) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

int ShipAnalysisCheckAnchors(const ShipAnalysis* fleet, int nShips)
{
    if (!fleet || nShips <= 0) return 1;

    auto findShip = [&](int idx) -> const ShipAnalysis* {
        for (int i = 0; i < nShips; i++)
            if (fleet[i].shipIndex == idx && fleet[i].valid) return &fleet[i];
        return nullptr;
    };

    int clusterHits = 0;
    for (int idx : {27, 28, 30, 34, 37}) {
        const ShipAnalysis* a = findShip(idx);
        if (!a || a->coplanarOverlay <= 0) return 2;
        clusterHits += a->coplanarOverlay;
    }
    // B2 flagged 12 overlay faces; accept ≥5 (one per cluster ship).
    if (clusterHits < 5) return 3;

    for (int idx : {23, 24, 25}) {
        const ShipAnalysis* a = findShip(idx);
        if (!a || a->punchDepthRisk <= 0) return 4;
    }
    return 0;
}

}  // namespace jjffe
