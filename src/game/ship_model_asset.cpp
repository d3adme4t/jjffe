#include "jjffe/ship_model_asset.h"

#include "jjffe/export_approval.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace jjffe {

ShipAssetPassState ShipAssetPassFor(ShipAssetPass pass, float overlayBias,
                                    float punchBias)
{
    ShipAssetPassState st;
    // Viewer two-sided: thin wings, missile fins, and inverted hull plates.
    // Closed bodies still hide interiors via hull Z-write. Export unchanged.
    st.cullBack = false;
    st.frontCCW = true;
    switch (pass) {
    case ShipAssetPass::Hull:
        st.depthWrite = true;
        st.depthFunc = ShipAssetDepthFunc::Less;
        st.normalBias = 0.f;
        break;
    case ShipAssetPass::Punch:
        // Coplanar with hull. Vertex lift (0.02 / 0.28) is a screen-space seam
        // that grows as EYE drops. Z-test is GPU constant bias in D3D.
        st.depthWrite = true;
        st.depthFunc = ShipAssetDepthFunc::LEqual;
        st.normalBias = punchBias;
        break;
    case ShipAssetPass::Overlay:
        st.depthWrite = false;
        st.depthFunc = ShipAssetDepthFunc::LEqual;
        st.normalBias = overlayBias;
        break;
    case ShipAssetPass::Line:
        st.depthWrite = false;
        st.depthFunc = ShipAssetDepthFunc::LEqual;
        // Nose frames / canopy ribs sit on or near hull — same viewer bias.
        st.normalBias = overlayBias;
        break;
    }
    return st;
}

float ShipExportRecommendedOverlayBias(float maxOverlayInset)
{
    if (maxOverlayInset <= 0.f) return kShipExportOverlayClearanceSlackObj;
    const float lo = maxOverlayInset + 1e-4f;
    const float hi = kShipExportLatticeUnit - 1e-4f;
    if (lo >= hi) return (kShipExportLatticeUnit + maxOverlayInset) * 0.5f;
    return 0.5f * (lo + hi);
}

const ShipAssetGroup* ShipAssetGroupFor(const ShipModelAsset& a, ShipAssetPass p)
{
    switch (p) {
    case ShipAssetPass::Hull: return &a.hull;
    case ShipAssetPass::Punch: return &a.punch;
    case ShipAssetPass::Overlay: return &a.overlay;
    case ShipAssetPass::Line: return &a.line;
    }
    return nullptr;
}

float ShipAssetComputePunchAftNudgeObj(const ShipAssetGroup& punch,
                                      const ShipAssetGroup& overlay,
                                      float clearanceSlackObj)
{
    const ShipAssetGroup emptyHull{};
    return ShipAssetComputePunchClearance(punch, overlay, emptyHull,
                                          clearanceSlackObj,
                                          kShipExportPunchClearanceEpsilonObj)
        .recommendedNudgeObj;
}

namespace {

struct AftPlane {
    float nx, ny, nz;
    float cx, cy, cz;
    float ax, ay, bx, by, cx2, cy2;  // XY triangle for overlap
    int kind = 0;
};

bool PunchVertOverPlane(float px, float py, const AftPlane& pl)
{
    const float v0x = pl.bx - pl.ax, v0y = pl.by - pl.ay;
    const float v1x = pl.cx2 - pl.ax, v1y = pl.cy2 - pl.ay;
    const float v2x = px - pl.ax, v2y = py - pl.ay;
    const float den = v0x * v1y - v1x * v0y;
    if (std::fabs(den) < 1e-20f) return false;
    const float u = (v2x * v1y - v1x * v2y) / den;
    const float v = (v0x * v2y - v2x * v0y) / den;
    const float m = 0.25f;
    return u >= -m && v >= -m && (u + v) <= 1.f + m;
}

void CollectAftPlanes(const ShipAssetGroup& g, int kind, std::vector<AftPlane>* out)
{
    if (!out) return;
    for (size_t t = 0; t + 2 < g.indices.size(); t += 3) {
        const uint32_t i0 = g.indices[t];
        const uint32_t i1 = g.indices[t + 1];
        const uint32_t i2 = g.indices[t + 2];
        if (i0 >= g.verts.size() || i1 >= g.verts.size() || i2 >= g.verts.size())
            continue;
        const ShipExportVert& A = g.verts[i0];
        const ShipExportVert& B = g.verts[i1];
        const ShipExportVert& C = g.verts[i2];
        float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-12f) continue;
        nx /= len;
        ny /= len;
        nz /= len;
        if (nz > 0.f) {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        const float cx = (A.x + B.x + C.x) * (1.f / 3.f);
        const float cy = (A.y + B.y + C.y) * (1.f / 3.f);
        const float cz = (A.z + B.z + C.z) * (1.f / 3.f);
        if (cz >= 0.f || nz > -0.4f) continue;
        out->push_back({nx, ny, nz, cx, cy, cz, A.x, A.y, B.x, B.y, C.x, C.y,
                        kind});
    }
}

}  // namespace

ShipPunchClearance ShipAssetComputePunchClearance(const ShipAssetGroup& punch,
                                                  const ShipAssetGroup& overlay,
                                                  const ShipAssetGroup& hull,
                                                  float slackObj,
                                                  float epsilonObj,
                                                  float maxEligibleGapObj)
{
    ShipPunchClearance r;
    r.punchTris = (int)punch.indices.size() / 3;
    if (punch.verts.empty()) return r;

    std::vector<AftPlane> aft;
    CollectAftPlanes(overlay, 0, &aft);
    r.overlayAftPlanes = (int)aft.size();
    CollectAftPlanes(hull, 1, &aft);
    r.hullAftPlanes = (int)aft.size() - r.overlayAftPlanes;
    if (aft.empty()) return r;

    const float scale = kShipExportObjScale;
    r.clusters.reserve((size_t)std::max(0, r.punchTris));
    for (size_t t = 0; t + 2 < punch.indices.size(); t += 3) {
        const uint32_t ia = punch.indices[t];
        const uint32_t ib = punch.indices[t + 1];
        const uint32_t ic = punch.indices[t + 2];
        if (ia >= punch.verts.size() || ib >= punch.verts.size() ||
            ic >= punch.verts.size())
            continue;
        ShipPunchClearanceCluster cl;
        cl.tri = (int)(t / 3);
        cl.cx = (punch.verts[ia].x + punch.verts[ib].x + punch.verts[ic].x) / 3.f;
        cl.cy = (punch.verts[ia].y + punch.verts[ib].y + punch.verts[ic].y) / 3.f;
        cl.cz = (punch.verts[ia].z + punch.verts[ib].z + punch.verts[ic].z) / 3.f;
        if (cl.cz >= 0.f) continue;  // windshield / nose punches stay put
        float triGap = 0.f;
        int kind = 0;
        for (int vi = 0; vi < 3; vi++) {
            const uint32_t idx = (vi == 0) ? ia : (vi == 1) ? ib : ic;
            const ShipExportVert& p = punch.verts[idx];
            float bestGap = 1e30f;
            int bestKind = 0;
            bool any = false;
            for (const AftPlane& pl : aft) {
                if (std::fabs(pl.nz) < 1e-8f) continue;
                if (!PunchVertOverPlane(p.x, p.y, pl)) continue;
                const float zPlane =
                    pl.cz -
                    (pl.nx * (p.x - pl.cx) + pl.ny * (p.y - pl.cy)) / pl.nz;
                const float gapObj = (p.z - zPlane) * scale;
                if (gapObj < bestGap) {
                    bestGap = gapObj;
                    bestKind = pl.kind;
                    any = true;
                }
            }
            if (any && bestGap > triGap) {
                triGap = bestGap;
                kind = bestKind;
            }
        }
        if (triGap > maxEligibleGapObj) continue;  // not a local rear plate
        cl.maxGapObj = triGap;
        cl.blockingKind = kind;
        r.clusters.push_back(cl);
        if (kind == 0 && triGap > r.maxGapObj) r.maxGapObj = triGap;
        if (kind == 1 && triGap > r.hullMaxGapObj) r.hullMaxGapObj = triGap;
    }

    const float eps = epsilonObj > 0.f ? epsilonObj : 0.f;
    if (r.maxGapObj > eps) {
        const float slack = slackObj > 0.f ? slackObj : 0.f;
        r.recommendedNudgeObj = r.maxGapObj + slack;
    }
    return r;
}

void ShipAssetStampPunchClearance(ShipModelAsset* asset)
{
    if (!asset || !asset->valid) return;
    const ShipPunchClearance r = ShipAssetComputePunchClearance(
        asset->punch, asset->overlay, asset->hull);
    asset->punchMaxGapObj = r.maxGapObj;
    asset->punchClearanceComputedObj = r.recommendedNudgeObj;
    asset->punchAftPlaneCount = r.overlayAftPlanes + r.hullAftPlanes;
    if (!asset->punchClearancePolicyOverride)
        asset->punchAftNudgeObj = r.recommendedNudgeObj;
}

static bool HullPointInTri(float px, float py, float pz, const ShipExportVert& A,
                           const ShipExportVert& B, const ShipExportVert& C,
                           float nx, float ny, float nz);

static float ClearanceBiasAgainstHull(const ShipAssetGroup& decal,
                                      const ShipAssetGroup& hull,
                                      float fromInset)
{
    float analytic = fromInset;
    if (!decal.indices.empty() && !hull.indices.empty()) {
        float minFront = 1e30f;
        bool hit = false;
        const float scale = kShipExportObjScale;
        const size_t nHull = hull.indices.size() / 3;
        const size_t nDec = decal.indices.size() / 3;
        for (size_t ot = 0; ot < nDec; ot++) {
            const ShipExportVert& OA = decal.verts[decal.indices[ot * 3 + 0]];
            const ShipExportVert& OB = decal.verts[decal.indices[ot * 3 + 1]];
            const ShipExportVert& OC = decal.verts[decal.indices[ot * 3 + 2]];
            float onx = (OB.y - OA.y) * (OC.z - OA.z) -
                        (OB.z - OA.z) * (OC.y - OA.y);
            float ony = (OB.z - OA.z) * (OC.x - OA.x) -
                        (OB.x - OA.x) * (OC.z - OA.z);
            float onz = (OB.x - OA.x) * (OC.y - OA.y) -
                        (OB.y - OA.y) * (OC.x - OA.x);
            const float oL = std::sqrt(onx * onx + ony * ony + onz * onz);
            if (oL < 1e-12f) continue;
            onx /= oL;
            ony /= oL;
            onz /= oL;
            const ShipExportVert* ov[3] = {&OA, &OB, &OC};
            for (int vi = 0; vi < 3; vi++) {
                const ShipExportVert& p = *ov[vi];
                float bestAbs = 1e30f;
                float bestSigned = 0.f;
                bool any = false;
                for (size_t ht = 0; ht < nHull; ht++) {
                    const ShipExportVert& A = hull.verts[hull.indices[ht * 3 + 0]];
                    const ShipExportVert& B = hull.verts[hull.indices[ht * 3 + 1]];
                    const ShipExportVert& C = hull.verts[hull.indices[ht * 3 + 2]];
                    float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
                    float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
                    float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
                    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len < 1e-12f) continue;
                    nx /= len;
                    ny /= len;
                    nz /= len;
                    if (nx * onx + ny * ony + nz * onz < 0.f) {
                        nx = -nx;
                        ny = -ny;
                        nz = -nz;
                    }
                    const float dWorld =
                        (p.x - A.x) * nx + (p.y - A.y) * ny + (p.z - A.z) * nz;
                    const float qx = p.x - nx * dWorld;
                    const float qy = p.y - ny * dWorld;
                    const float qz = p.z - nz * dWorld;
                    if (!HullPointInTri(qx, qy, qz, A, B, C, nx, ny, nz))
                        continue;
                    const float dObj = dWorld * scale;
                    const float ad = std::fabs(dObj);
                    if (ad < bestAbs) {
                        bestAbs = ad;
                        bestSigned = dObj;
                        any = true;
                    }
                }
                if (any) {
                    hit = true;
                    if (bestSigned < minFront) minFront = bestSigned;
                }
            }
        }
        if (hit) {
            analytic = kShipExportOverlayClearanceSlackObj - minFront;
            if (analytic < kShipExportOverlayClearanceMinObj)
                analytic = kShipExportOverlayClearanceMinObj;
        }
    }
    float b = analytic > fromInset ? analytic : fromInset;
    if (b > kShipExportOverlayNormalBias) b = kShipExportOverlayNormalBias;
    if (b < kShipExportOverlayClearanceMinObj)
        b = kShipExportOverlayClearanceMinObj;
    return b;
}

void ShipAssetStampOverlayClearance(ShipModelAsset* asset)
{
    if (!asset || !asset->valid) return;
    const float fromInset =
        ShipExportRecommendedOverlayBias(asset->maxOverlayInset);
    asset->overlayNormalBias =
        ClearanceBiasAgainstHull(asset->overlay, asset->hull, fromInset);
}

void ShipAssetStampPunchNormalBias(ShipModelAsset* asset)
{
    if (!asset || !asset->valid) return;
    // Nose 0x2000 stays coplanar. 0.02 vertex park was a zoom-scaling seam
    // (#15 CAM Z:4 vs Z:3). GPU constant depth bias wins LEQUAL vs hull.
    asset->punchNormalBias = 0.f;
}

void ShipAssetOffsetPunchVerts(ShipAssetGroup* punch, float noseBiasObj,
                               float aftBiasObj)
{
    if (!punch || punch->indices.size() < 3) return;
    const float inv = 1.f / kShipExportObjScale;
    for (size_t t = 0; t + 2 < punch->indices.size(); t += 3) {
        const uint32_t i0 = punch->indices[t];
        const uint32_t i1 = punch->indices[t + 1];
        const uint32_t i2 = punch->indices[t + 2];
        if (i0 >= punch->verts.size() || i1 >= punch->verts.size() ||
            i2 >= punch->verts.size())
            continue;
        const float cz = (punch->verts[i0].z + punch->verts[i1].z +
                          punch->verts[i2].z) *
                         (1.f / 3.f);
        const float b = (cz >= 0.f) ? noseBiasObj : aftBiasObj;
        if (b == 0.f) continue;
        const uint32_t ids[3] = {i0, i1, i2};
        for (uint32_t id : ids) {
            ShipExportVert& v = punch->verts[id];
            float nx = v.nx, ny = v.ny, nz = v.nz;
            const float nL = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nL < 1e-8f) continue;
            const float s = (b * inv) / nL;
            v.x += nx * s;
            v.y += ny * s;
            v.z += nz * s;
        }
    }
}

void ShipAssetParkPunchWindshields(ShipAssetGroup* punch,
                                   const ShipAssetGroup& hull, float slackObj)
{
    if (!punch || punch->indices.size() < 3 || hull.indices.size() < 3) return;
    const float slackW =
        (slackObj > 0.f ? slackObj : kShipExportOverlayClearanceMinObj) /
        kShipExportObjScale;
    const size_t nH = hull.indices.size() / 3;
    for (size_t t = 0; t + 2 < punch->indices.size(); t += 3) {
        const uint32_t i0 = punch->indices[t];
        const uint32_t i1 = punch->indices[t + 1];
        const uint32_t i2 = punch->indices[t + 2];
        if (i0 >= punch->verts.size() || i1 >= punch->verts.size() ||
            i2 >= punch->verts.size())
            continue;
        ShipExportVert& A = punch->verts[i0];
        ShipExportVert& B = punch->verts[i1];
        ShipExportVert& C = punch->verts[i2];
        const float cz = (A.z + B.z + C.z) * (1.f / 3.f);
        if (cz < 0.f) continue;  // aft 0x20ee stays on OffsetPunchVerts
        const float pcx = (A.x + B.x + C.x) * (1.f / 3.f);
        const float pcy = (A.y + B.y + C.y) * (1.f / 3.f);
        const float pcz = (A.z + B.z + C.z) * (1.f / 3.f);
        float pnx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float pny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float pnz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float pL = std::sqrt(pnx * pnx + pny * pny + pnz * pnz);
        if (pL < 1e-12f) continue;
        pnx /= pL;
        pny /= pL;
        pnz /= pL;
        float bestAbs = 1e30f;
        float hxN = 0.f, hyN = 0.f, hzN = 0.f, signedW = 0.f;
        bool hit = false;
        for (size_t ht = 0; ht < nH; ht++) {
            const ShipExportVert& HA = hull.verts[hull.indices[ht * 3 + 0]];
            const ShipExportVert& HB = hull.verts[hull.indices[ht * 3 + 1]];
            const ShipExportVert& HC = hull.verts[hull.indices[ht * 3 + 2]];
            float nx = (HB.y - HA.y) * (HC.z - HA.z) - (HB.z - HA.z) * (HC.y - HA.y);
            float ny = (HB.z - HA.z) * (HC.x - HA.x) - (HB.x - HA.x) * (HC.z - HA.z);
            float nz = (HB.x - HA.x) * (HC.y - HA.y) - (HB.y - HA.y) * (HC.x - HA.x);
            const float nL = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nL < 1e-12f) continue;
            nx /= nL;
            ny /= nL;
            nz /= nL;
            const float hcx = (HA.x + HB.x + HC.x) * (1.f / 3.f);
            const float hcy = (HA.y + HB.y + HC.y) * (1.f / 3.f);
            const float hcz = (HA.z + HB.z + HC.z) * (1.f / 3.f);
            if (nx * hcx + ny * hcy + nz * hcz < 0.f) {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
            if (std::fabs(nx * pnx + ny * pny + nz * pnz) < 0.95f) continue;
            const float s = nx * (pcx - HA.x) + ny * (pcy - HA.y) + nz * (pcz - HA.z);
            const float ad = std::fabs(s);
            if (ad > 0.02f) continue;  // not the local windshield plate
            if (ad < bestAbs) {
                bestAbs = ad;
                hxN = nx;
                hyN = ny;
                hzN = nz;
                signedW = s;
                hit = true;
            }
        }
        if (!hit) continue;
        const float need = slackW - signedW;
        if (need <= 0.f) continue;
        A.x += hxN * need;
        A.y += hyN * need;
        A.z += hzN * need;
        B.x += hxN * need;
        B.y += hyN * need;
        B.z += hzN * need;
        C.x += hxN * need;
        C.y += hyN * need;
        C.z += hzN * need;
    }
}

static void AppendTri(ShipAssetGroup* g, const ShipExportVert& a,
                      const ShipExportVert& b, const ShipExportVert& c,
                      uint32_t primitiveId = 0, int sourceOrder = -1,
                      uint32_t kdArgb = 0xFFC0C0D0u, bool recordId = false,
                      int materialIndex = -1)
{
    const uint32_t base = (uint32_t)g->verts.size();
    g->verts.push_back(a);
    g->verts.push_back(b);
    g->verts.push_back(c);
    g->indices.push_back(base + 0);
    g->indices.push_back(base + 1);
    g->indices.push_back(base + 2);
    g->triMaterials.push_back(materialIndex);
    if (recordId) {
        g->primitiveIds.push_back(primitiveId);
        g->sourceOrders.push_back(sourceOrder);
        g->triKdArgb.push_back(kdArgb);
    }
}

// Eagle Mk2/Mk3 (and similar): authored vn can point into the mesh; export
// aligns winding to vn → GPU cull shows the interior. Repair:
//   1) Twin pairs (nearby, opposite n): if the *outer* face is inward vs
//      centroid, flip both — restores inverted tip sandwiches.
//   2) Unpaired tip end-caps that point outward while a farther tip exists on
//      the same side: flip (inner plate wearing an outer normal).
//   3) Unpaired faces on the AABB shell whose normal points back into the
//      box — flip. Do NOT use centroid-outward alone: concave valleys
//      (Stowmaster dual-arrowhead cleft) are inward vs centroid but correct.
static int RepairStronglyInwardGroup(ShipAssetGroup* g, float cx, float cy,
                                     float cz)
{
    if (!g || g->pass == ShipAssetPass::Line) return 0;
    const size_t nTri = g->indices.size() / 3;
    if (nTri == 0 || g->verts.empty()) return 0;

    struct TriInfo {
        float fx, fy, fz;
        float nx, ny, nz;
    };
    std::vector<TriInfo> info(nTri);
    float xmin = 1e30f, xmax = -1e30f, ymin = 1e30f, ymax = -1e30f,
          zmin = 1e30f, zmax = -1e30f;
    for (size_t t = 0; t < nTri; t++) {
        const uint32_t ia = g->indices[t * 3 + 0];
        const uint32_t ib = g->indices[t * 3 + 1];
        const uint32_t ic = g->indices[t * 3 + 2];
        const ShipExportVert& a = g->verts[ia];
        const ShipExportVert& b = g->verts[ib];
        const ShipExportVert& c = g->verts[ic];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (L > 1e-12f) {
            nx /= L;
            ny /= L;
            nz /= L;
        }
        const float fx = (a.x + b.x + c.x) * (1.f / 3.f);
        const float fy = (a.y + b.y + c.y) * (1.f / 3.f);
        const float fz = (a.z + b.z + c.z) * (1.f / 3.f);
        info[t] = {fx, fy, fz, nx, ny, nz};
        if (fx < xmin) xmin = fx;
        if (fx > xmax) xmax = fx;
        if (fy < ymin) ymin = fy;
        if (fy > ymax) ymax = fy;
        if (fz < zmin) zmin = fz;
        if (fz > zmax) zmax = fz;
    }
    const float tolX = std::max(0.003f, 0.05f * (xmax - xmin));
    const float tolY = std::max(0.003f, 0.05f * (ymax - ymin));
    const float tolZ = std::max(0.003f, 0.05f * (zmax - zmin));

    auto outwardDot = [&](size_t t) -> float {
        const TriInfo& ti = info[t];
        float ox = ti.fx - cx, oy = ti.fy - cy, oz = ti.fz - cz;
        const float oL = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (oL < 1e-8f) return 0.f;
        return (ti.nx * ox + ti.ny * oy + ti.nz * oz) / oL;
    };
    auto radius2 = [&](size_t t) -> float {
        const float dx = info[t].fx - cx, dy = info[t].fy - cy,
                    dz = info[t].fz - cz;
        return dx * dx + dy * dy + dz * dz;
    };
    // True when the face sits on the AABB envelope but its normal points
    // back into the box (convex-hull exterior wrongly facing inward).
    auto aabbShellInward = [&](size_t t) -> bool {
        const TriInfo& ti = info[t];
        if (ti.fx >= xmax - tolX && ti.nx < -0.5f) return true;
        if (ti.fx <= xmin + tolX && ti.nx > 0.5f) return true;
        if (ti.fy >= ymax - tolY && ti.ny < -0.5f) return true;
        if (ti.fy <= ymin + tolY && ti.ny > 0.5f) return true;
        if (ti.fz >= zmax - tolZ && ti.nz < -0.5f) return true;
        if (ti.fz <= zmin + tolZ && ti.nz > 0.5f) return true;
        return false;
    };
    auto flipTri = [&](size_t t) {
        std::swap(g->indices[t * 3 + 1], g->indices[t * 3 + 2]);
        for (int k = 0; k < 3; k++) {
            ShipExportVert& v = g->verts[g->indices[t * 3 + k]];
            v.nx = -v.nx;
            v.ny = -v.ny;
            v.nz = -v.nz;
        }
        info[t].nx = -info[t].nx;
        info[t].ny = -info[t].ny;
        info[t].nz = -info[t].nz;
    };

    // Twin: within 5 OBJ units, similar Y, opposite normals. Pick nearest.
    constexpr float kTwinDist2 = 0.05f * 0.05f;
    constexpr float kMaxDy = 0.01f;  // 1 OBJ unit
    std::vector<int> twinOf(nTri, -1);
    for (size_t t = 0; t < nTri; t++) {
        int best = -1;
        float bestD = 1e30f;
        for (size_t u = 0; u < nTri; u++) {
            if (u == t) continue;
            const float dx = info[t].fx - info[u].fx;
            const float dy = info[t].fy - info[u].fy;
            const float dz = info[t].fz - info[u].fz;
            if (std::fabs(dy) > kMaxDy) continue;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > kTwinDist2) continue;
            const float nd = info[t].nx * info[u].nx + info[t].ny * info[u].ny +
                             info[t].nz * info[u].nz;
            if (nd >= -0.85f) continue;
            if (d2 < bestD) {
                bestD = d2;
                best = (int)u;
            }
        }
        if (best >= 0 && twinOf[t] < 0 && twinOf[best] < 0) {
            twinOf[t] = best;
            twinOf[best] = (int)t;
        }
    }

    std::vector<char> done(nTri, 0);
    int flipped = 0;
    for (size_t t = 0; t < nTri; t++) {
        if (done[t]) continue;
        const int u = twinOf[t];
        if (u >= 0) {
            const size_t outer = radius2(t) >= radius2((size_t)u) ? t : (size_t)u;
            if (outwardDot(outer) < 0.f) {
                flipTri(t);
                flipTri((size_t)u);
                flipped += 2;
            }
            done[t] = 1;
            done[(size_t)u] = 1;
            continue;
        }

        const TriInfo& ti = info[t];
        const bool tipCap =
            std::fabs(ti.fx) > 0.10f && std::fabs(ti.nx) > 0.70f;
        // Inner tip plate wearing an outer normal (points away from centerline
        // while a farther tip exists on this side).
        if (tipCap && ti.nx * ti.fx > 0.f) {
            bool hasFarther = false;
            for (size_t v = 0; v < nTri; v++) {
                if (v == t) continue;
                if (std::fabs(info[v].fy - ti.fy) > kMaxDy) continue;
                if (info[v].fx * ti.fx <= 0.f) continue;  // opposite wing
                if (std::fabs(info[v].fx) <= std::fabs(ti.fx) + 0.002f) continue;
                if (std::fabs(info[v].nx) < 0.50f) continue;
                hasFarther = true;
                break;
            }
            if (hasFarther) {
                flipTri(t);
                flipped++;
                done[t] = 1;
                continue;
            }
        }

        if (aabbShellInward(t)) {
            flipTri(t);
            flipped++;
        }
        done[t] = 1;
    }
    return flipped;
}

static void AccumulateFaceCentroid(const ShipAssetGroup& g, float* cx, float* cy,
                                   float* cz, int* nFaces)
{
    const size_t nTri = g.indices.size() / 3;
    for (size_t t = 0; t < nTri; t++) {
        const ShipExportVert& a = g.verts[g.indices[t * 3 + 0]];
        const ShipExportVert& b = g.verts[g.indices[t * 3 + 1]];
        const ShipExportVert& c = g.verts[g.indices[t * 3 + 2]];
        *cx += (a.x + b.x + c.x) * (1.f / 3.f);
        *cy += (a.y + b.y + c.y) * (1.f / 3.f);
        *cz += (a.z + b.z + c.z) * (1.f / 3.f);
        (*nFaces)++;
    }
}

static int RepairAssetOutwardWinding(ShipModelAsset* a)
{
    if (!a) return 0;
    float cx = 0, cy = 0, cz = 0;
    int nFaces = 0;
    AccumulateFaceCentroid(a->hull, &cx, &cy, &cz, &nFaces);
    AccumulateFaceCentroid(a->punch, &cx, &cy, &cz, &nFaces);
    AccumulateFaceCentroid(a->overlay, &cx, &cy, &cz, &nFaces);
    if (nFaces <= 0) return 0;
    const float inv = 1.f / (float)nFaces;
    cx *= inv;
    cy *= inv;
    cz *= inv;
    return RepairStronglyInwardGroup(&a->hull, cx, cy, cz) +
           RepairStronglyInwardGroup(&a->punch, cx, cy, cz) +
           RepairStronglyInwardGroup(&a->overlay, cx, cy, cz);
}

static bool HullPointInTri(float px, float py, float pz, const ShipExportVert& A,
                           const ShipExportVert& B, const ShipExportVert& C,
                           float nx, float ny, float nz)
{
    float ap[3] = {A.x - px, A.y - py, A.z - pz};
    float bp[3] = {B.x - px, B.y - py, B.z - pz};
    float cp[3] = {C.x - px, C.y - py, C.z - pz};
    auto crdot = [&](const float* u, const float* v) {
        const float cx = u[1] * v[2] - u[2] * v[1];
        const float cy = u[2] * v[0] - u[0] * v[2];
        const float cz = u[0] * v[1] - u[1] * v[0];
        return nx * cx + ny * cy + nz * cz;
    };
    const float a0 = crdot(bp, cp);
    const float a1 = crdot(cp, ap);
    const float a2 = crdot(ap, bp);
    const float host = a0 + a1 + a2;
    if (std::fabs(host) < 1e-20f) return false;
    const float m = 0.05f;
    const float s = host >= 0.f ? 1.f : -1.f;
    return s * a0 >= -m * std::fabs(host) && s * a1 >= -m * std::fabs(host) &&
           s * a2 >= -m * std::fabs(host);
}

// Viewer-only: hull tris that sit on a larger parallel hull face (Sidewinder
// rear diamond vs rear cap) z-fight under LESS. Promote the inset to overlay
// so painter LEQUAL+normalBias wins. Export mesh unchanged.
static int PromoteEmbeddedHullToOverlay(ShipModelAsset* a)
{
    if (!a) return 0;
    ShipAssetGroup& hull = a->hull;
    const size_t nTri = hull.indices.size() / 3;
    if (nTri < 2) return 0;

    struct TriG {
        ShipExportVert a, b, c;
        float nx, ny, nz, cx, cy, cz, area;
        uint32_t pid = 0;
        int order = -1;
        uint32_t kd = 0xFFC0C0D0u;
        int mat = -1;
        bool hasMeta = false;
    };
    std::vector<TriG> tris;
    tris.reserve(nTri);
    const bool haveMeta = hull.primitiveIds.size() == nTri &&
                          hull.sourceOrders.size() == nTri &&
                          hull.triKdArgb.size() == nTri;
    const bool haveMat = hull.triMaterials.size() == nTri;
    for (size_t t = 0; t < nTri; t++) {
        const ShipExportVert& A = hull.verts[hull.indices[t * 3 + 0]];
        const ShipExportVert& B = hull.verts[hull.indices[t * 3 + 1]];
        const ShipExportVert& C = hull.verts[hull.indices[t * 3 + 2]];
        const float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z;
        const float vx = C.x - A.x, vy = C.y - A.y, vz = C.z - A.z;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-12f) continue;
        nx /= len;
        ny /= len;
        nz /= len;
        TriG g;
        g.a = A;
        g.b = B;
        g.c = C;
        g.nx = nx;
        g.ny = ny;
        g.nz = nz;
        g.cx = (A.x + B.x + C.x) / 3.f;
        g.cy = (A.y + B.y + C.y) / 3.f;
        g.cz = (A.z + B.z + C.z) / 3.f;
        g.area = 0.5f * len;
        if (haveMeta) {
            g.pid = hull.primitiveIds[t];
            g.order = hull.sourceOrders[t];
            g.kd = hull.triKdArgb[t];
            g.hasMeta = true;
        }
        if (haveMat) g.mat = hull.triMaterials[t];
        tris.push_back(g);
    }

    std::vector<char> promote(tris.size(), 0);
    int n = 0;
    constexpr float kNd = 0.98f;
    for (size_t i = 0; i < tris.size(); i++) {
        for (size_t h = 0; h < tris.size(); h++) {
            if (i == h) continue;
            const TriG& inset = tris[i];
            const TriG& host = tris[h];
            if (inset.area >= host.area * 0.5f) continue;
            const float nd = inset.nx * host.nx + inset.ny * host.ny +
                             inset.nz * host.nz;
            if (nd < kNd) continue;
            auto dist = [&](const ShipExportVert& v) {
                return std::fabs(host.nx * (v.x - host.a.x) +
                                 host.ny * (v.y - host.a.y) +
                                 host.nz * (v.z - host.a.z));
            };
            if (dist(inset.a) > 0.001f || dist(inset.b) > 0.001f ||
                dist(inset.c) > 0.001f)
                continue;
            if (!HullPointInTri(inset.cx, inset.cy, inset.cz, host.a, host.b,
                                host.c, host.nx, host.ny, host.nz))
                continue;
            promote[i] = 1;
            n++;
            break;
        }
    }
    if (n == 0) return 0;

    ShipAssetGroup keep;
    keep.pass = ShipAssetPass::Hull;
    keep.materialHint = -1;
    for (size_t i = 0; i < tris.size(); i++) {
        ShipAssetGroup* dst = promote[i] ? &a->overlay : &keep;
        if (promote[i]) dst->pass = ShipAssetPass::Overlay;
        if (dst->materialHint < 0 && tris[i].mat >= 0)
            dst->materialHint = tris[i].mat;
        AppendTri(dst, tris[i].a, tris[i].b, tris[i].c, tris[i].pid,
                  tris[i].order, tris[i].kd, tris[i].hasMeta, tris[i].mat);
    }
    a->hull = std::move(keep);
    return n;
}

static float GroupTriArea(const ShipAssetGroup& g, size_t t)
{
    const ShipExportVert& A = g.verts[g.indices[t * 3 + 0]];
    const ShipExportVert& B = g.verts[g.indices[t * 3 + 1]];
    const ShipExportVert& C = g.verts[g.indices[t * 3 + 2]];
    const float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
    const float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
    const float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    return 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz);
}

// Viper wings/top are 0x1000 flats (untextured overlay) as large as the hull.
// Viewer skins them with the hull map. Small coplanar 0x1000 decals stay overlay.
static int AbsorbLargeUntexturedOverlayIntoHull(ShipModelAsset* a)
{
    if (!a) return 0;
    ShipAssetGroup& ov = a->overlay;
    const size_t nOv = ov.indices.size() / 3;
    if (nOv == 0) return 0;
    float maxHull = 0.f;
    const size_t nHull = a->hull.indices.size() / 3;
    for (size_t t = 0; t < nHull; t++)
        maxHull = std::max(maxHull, GroupTriArea(a->hull, t));
    if (maxHull < 1e-12f) return 0;

    struct HullTri {
        ShipExportVert a, b, c;
        float nx, ny, nz;
    };
    std::vector<HullTri> hull;
    hull.reserve(nHull);
    for (size_t t = 0; t < nHull; t++) {
        HullTri h;
        h.a = a->hull.verts[a->hull.indices[t * 3 + 0]];
        h.b = a->hull.verts[a->hull.indices[t * 3 + 1]];
        h.c = a->hull.verts[a->hull.indices[t * 3 + 2]];
        float nx = (h.b.y - h.a.y) * (h.c.z - h.a.z) - (h.b.z - h.a.z) * (h.c.y - h.a.y);
        float ny = (h.b.z - h.a.z) * (h.c.x - h.a.x) - (h.b.x - h.a.x) * (h.c.z - h.a.z);
        float nz = (h.b.x - h.a.x) * (h.c.y - h.a.y) - (h.b.y - h.a.y) * (h.c.x - h.a.x);
        const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (L < 1e-12f) continue;
        h.nx = nx / L;
        h.ny = ny / L;
        h.nz = nz / L;
        hull.push_back(h);
    }

    const bool haveMeta = ov.primitiveIds.size() == nOv &&
                          ov.sourceOrders.size() == nOv &&
                          ov.triKdArgb.size() == nOv;
    const bool haveMat = ov.triMaterials.size() == nOv;
    std::vector<char> absorb(nOv, 0);
    int n = 0;
    constexpr float kArea = 0.50f;
    constexpr float kNd = 0.98f;
    for (size_t t = 0; t < nOv; t++) {
        int mi = haveMat ? ov.triMaterials[t] : ov.materialHint;
        if (mi >= 0 && mi < (int)a->materials.size()) {
            const ShipExportMaterial& mat = a->materials[(size_t)mi];
            if (mat.textured && !mat.baked.argb.empty()) continue;
        }
        if (GroupTriArea(ov, t) < kArea * maxHull) continue;
        const ShipExportVert& A = ov.verts[ov.indices[t * 3 + 0]];
        const ShipExportVert& B = ov.verts[ov.indices[t * 3 + 1]];
        const ShipExportVert& C = ov.verts[ov.indices[t * 3 + 2]];
        float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (L < 1e-12f) continue;
        nx /= L;
        ny /= L;
        nz /= L;
        const float cx = (A.x + B.x + C.x) / 3.f;
        const float cy = (A.y + B.y + C.y) / 3.f;
        const float cz = (A.z + B.z + C.z) / 3.f;
        bool onHull = false;
        for (const HullTri& h : hull) {
            const float nd = nx * h.nx + ny * h.ny + nz * h.nz;
            if (nd < kNd) continue;
            auto dist = [&](const ShipExportVert& v) {
                return std::fabs(h.nx * (v.x - h.a.x) + h.ny * (v.y - h.a.y) +
                                 h.nz * (v.z - h.a.z));
            };
            if (dist(A) > 0.001f || dist(B) > 0.001f || dist(C) > 0.001f)
                continue;
            if (!HullPointInTri(cx, cy, cz, h.a, h.b, h.c, h.nx, h.ny, h.nz))
                continue;
            onHull = true;
            break;
        }
        if (onHull) continue;
        absorb[t] = 1;
        n++;
    }
    if (n == 0) return 0;

    const int hullMat = a->hull.materialHint;
    ShipAssetGroup keepOv;
    keepOv.pass = ShipAssetPass::Overlay;
    keepOv.materialHint = -1;
    for (size_t t = 0; t < nOv; t++) {
        ShipExportVert A = ov.verts[ov.indices[t * 3 + 0]];
        ShipExportVert B = ov.verts[ov.indices[t * 3 + 1]];
        ShipExportVert C = ov.verts[ov.indices[t * 3 + 2]];
        const uint32_t pid = haveMeta ? ov.primitiveIds[t] : 0;
        const int order = haveMeta ? ov.sourceOrders[t] : -1;
        const uint32_t kd = haveMeta ? ov.triKdArgb[t] : 0xFFC0C0D0u;
        const int mi = haveMat ? ov.triMaterials[t] : ov.materialHint;
        if (absorb[t]) {
            A.u = 0.008f;
            A.v = 0.008f;
            B.u = 0.992f;
            B.v = 0.008f;
            C.u = 0.992f;
            C.v = 0.992f;
            const int destMat = hullMat >= 0 ? hullMat : mi;
            if (a->hull.materialHint < 0) a->hull.materialHint = destMat;
            AppendTri(&a->hull, A, B, C, pid, order, kd, haveMeta, destMat);
        } else {
            if (keepOv.materialHint < 0 && mi >= 0) keepOv.materialHint = mi;
            AppendTri(&keepOv, A, B, C, pid, order, kd, haveMeta, mi);
        }
    }
    a->overlay = std::move(keepOv);
    return n;
}

// Engine-plate hull that sits on 0x20ee/0x2000 punch planes keeps the hull
// map; original FFE leaves that bulkhead untextured. Tag those tris with a
// flat material so D3D can upload them without the skin.
static int UntextureHullCoplanarWithPunch(ShipModelAsset* a)
{
    if (!a) return 0;
    const size_t nPunch = a->punch.indices.size() / 3;
    const size_t nHull = a->hull.indices.size() / 3;
    if (nPunch == 0 || nHull == 0) return 0;
    if (a->hull.triMaterials.size() != nHull) return 0;

    struct Plane {
        float nx, ny, nz, px, py, pz;
    };
    std::vector<Plane> punches;
    punches.reserve(nPunch);
    for (size_t t = 0; t < nPunch; t++) {
        int mi = a->punch.materialHint;
        if (a->punch.triMaterials.size() == nPunch) mi = a->punch.triMaterials[t];
        uint16_t ti = 0;
        if (mi >= 0 && mi < (int)a->materials.size())
            ti = a->materials[(size_t)mi].texInfo;
        // Windshield 0x2000 punches sit on textured front plates. Only the
        // engine 0x20ee plane should lose hull skin (Viper rear bulkhead).
        if (ti != 0x20ee) continue;
        const ShipExportVert& A = a->punch.verts[a->punch.indices[t * 3 + 0]];
        const ShipExportVert& B = a->punch.verts[a->punch.indices[t * 3 + 1]];
        const ShipExportVert& C = a->punch.verts[a->punch.indices[t * 3 + 2]];
        float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (L < 1e-12f) continue;
        punches.push_back({nx / L, ny / L, nz / L, A.x, A.y, A.z});
    }
    if (punches.empty()) return 0;

    int flatMi = -1;
    for (size_t i = 0; i < a->materials.size(); i++) {
        const ShipExportMaterial& m = a->materials[i];
        if (!m.textured && !m.punch && !m.decal && m.name == "hull_punch_plane") {
            flatMi = (int)i;
            break;
        }
    }
    // Not hull-skin white (textured Kd is 0xFFFFFFFF). Steel grey so the
    // aft bulkhead doesn't flash as a light after the map is stripped.
    constexpr uint32_t kHullPunchPlaneKd = 0xFF555E6Au;
    if (flatMi < 0) {
        ShipExportMaterial m;
        m.name = "hull_punch_plane";
        m.kdArgb = kHullPunchPlaneKd;
        m.textured = false;
        flatMi = (int)a->materials.size();
        a->materials.push_back(m);
    } else {
        a->materials[(size_t)flatMi].kdArgb = kHullPunchPlaneKd;
        a->materials[(size_t)flatMi].textured = false;
    }

    int n = 0;
    for (size_t t = 0; t < nHull; t++) {
        const ShipExportVert& A = a->hull.verts[a->hull.indices[t * 3 + 0]];
        const ShipExportVert& B = a->hull.verts[a->hull.indices[t * 3 + 1]];
        const ShipExportVert& C = a->hull.verts[a->hull.indices[t * 3 + 2]];
        float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (L < 1e-12f) continue;
        nx /= L;
        ny /= L;
        nz /= L;
        const float cx = (A.x + B.x + C.x) / 3.f;
        const float cy = (A.y + B.y + C.y) / 3.f;
        const float cz = (A.z + B.z + C.z) / 3.f;
        bool cop = false;
        for (const Plane& p : punches) {
            const float nd = nx * p.nx + ny * p.ny + nz * p.nz;
            if (std::fabs(nd) < 0.95f) continue;
            const float d = std::fabs(p.nx * (cx - p.px) + p.ny * (cy - p.py) +
                                      p.nz * (cz - p.pz));
            if (d > 0.002f) continue;
            cop = true;
            break;
        }
        if (!cop) continue;
        a->hull.triMaterials[t] = flatMi;
        if (a->hull.triKdArgb.size() == nHull)
            a->hull.triKdArgb[t] = a->materials[(size_t)flatMi].kdArgb;
        n++;
    }
    return n;
}

bool BuildShipModelAsset(const ShipExportMesh& mesh, ShipModelAsset* out)
{
    if (!out) return false;
    *out = ShipModelAsset{};
    if (!mesh.valid) {
        out->error = "invalid mesh";
        return false;
    }
    out->shipIndex = mesh.shipIndex;
    out->name = mesh.name;
    out->maxOverlayInset = mesh.maxOverlaySubLatticeInset;
    out->maxPunchInset = mesh.maxPunchSubLatticeInset;

    out->hull.pass = ShipAssetPass::Hull;
    out->punch.pass = ShipAssetPass::Punch;
    out->overlay.pass = ShipAssetPass::Overlay;
    out->line.pass = ShipAssetPass::Line;
    out->materials = mesh.materials;

    const int nTri = (int)mesh.triMaterial.size();
    for (int t = 0; t < nTri; t++) {
        const int mi = mesh.triMaterial[(size_t)t];
        if (mi < 0 || mi >= (int)mesh.materials.size()) continue;
        const ShipExportMaterial& mat = mesh.materials[(size_t)mi];
        const ShipExportVert& a = mesh.verts[mesh.indices[(size_t)t * 3 + 0]];
        const ShipExportVert& b = mesh.verts[mesh.indices[(size_t)t * 3 + 1]];
        const ShipExportVert& c = mesh.verts[mesh.indices[(size_t)t * 3 + 2]];
        ShipAssetGroup* g = &out->hull;
        if (mat.decal) g = &out->overlay;
        else if (mat.punch) g = &out->punch;
        else if (mat.name.rfind("line_", 0) == 0) g = &out->line;
        if (g->materialHint < 0) g->materialHint = mi;
        uint32_t pid = 0;
        int order = -1;
        if (t < (int)mesh.triMeta.size()) {
            pid = mesh.triMeta[(size_t)t].primitiveId;
            order = mesh.triMeta[(size_t)t].sourceOrder;
        }
        AppendTri(g, a, b, c, pid, order, mat.kdArgb, /*recordId=*/true, mi);
    }
    out->emitters = mesh.emitters;
    out->curves = mesh.curves;
    out->sprites = mesh.sprites;
    out->missileFlat = mesh.missileFlat;
    RepairAssetOutwardWinding(out);
    PromoteEmbeddedHullToOverlay(out);
    AbsorbLargeUntexturedOverlayIntoHull(out);
    UntextureHullCoplanarWithPunch(out);
    out->valid = true;
    ShipAssetStampPunchClearance(out);
    ShipAssetStampOverlayClearance(out);
    ShipAssetStampPunchNormalBias(out);
    return true;
}

static bool ParseEmittersJson(const char* path, std::vector<ShipExportEmitter>* out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return true;  // optional
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    std::string text((size_t)sz, '\0');
    if (std::fread(text.data(), 1, (size_t)sz, f) != (size_t)sz) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    // emitters.json writes pos in OBJ units (*kShipExportObjScale). Store world
    // so PackEmitters / D3D upload can *scale again like hull verts.
    const float inv = 1.f / kShipExportObjScale;
    size_t pos = 0;
    while (true) {
        size_t k = text.find("\"kind\":", pos);
        if (k == std::string::npos) break;
        ShipExportEmitter e;
        char kind[32] = {};
        if (sscanf(text.c_str() + k, "\"kind\": \"%31[^\"]\"", kind) != 1) {
            pos = k + 7;
            continue;
        }
        if (std::strcmp(kind, "thruster") == 0)
            e.kind = ShipExportEmitterKind::Thruster;
        else if (std::strcmp(kind, "navlight") == 0 ||
                 std::strcmp(kind, "nav_light") == 0)
            e.kind = ShipExportEmitterKind::NavLight;
        else if (std::strcmp(kind, "glow") == 0)
            e.kind = ShipExportEmitterKind::Glow;
        else {
            pos = k + 7;
            continue;
        }
        const char* p = text.c_str() + k;
        const char* posTag = std::strstr(p, "\"pos\":");
        if (posTag && posTag < p + 400) {
            sscanf(posTag, "\"pos\": [%f, %f, %f]", &e.x, &e.y, &e.z);
            e.x *= inv;
            e.y *= inv;
            e.z *= inv;
        }
        const char* pos1Tag = std::strstr(p, "\"pos1\":");
        if (pos1Tag && pos1Tag < p + 400) {
            sscanf(pos1Tag, "\"pos1\": [%f, %f, %f]", &e.x1, &e.y1, &e.z1);
            e.x1 *= inv;
            e.y1 *= inv;
            e.z1 *= inv;
        }
        const char* sizeTag = std::strstr(p, "\"size\":");
        if (sizeTag && sizeTag < p + 400) sscanf(sizeTag, "\"size\": %f", &e.size);
        const char* colTag = std::strstr(p, "\"color\":");
        if (colTag && colTag < p + 400) {
            sscanf(colTag, "\"color\": [%f, %f, %f]", &e.rgb[0], &e.rgb[1],
                   &e.rgb[2]);
        }
        const char* texTag = std::strstr(p, "\"texInfo\":");
        if (texTag && texTag < p + 400) {
            unsigned tex = 0;
            if (sscanf(texTag, "\"texInfo\": %u", &tex) == 1)
                e.texInfo = (uint16_t)tex;
        }
        // emitters.json may still carry palette-baked thruster RGB (dark green).
        // Prefer FUNC_001692 nibbles when texInfo is flat-RGB (0x20ee → cyan).
        if (e.kind == ShipExportEmitterKind::Thruster &&
            (e.texInfo & 0xF000u) == 0x2000u) {
            e.rgb[0] = ((e.texInfo >> 8) & 0xF) / 15.0f;
            e.rgb[1] = ((e.texInfo >> 4) & 0xF) / 15.0f;
            e.rgb[2] = (e.texInfo & 0xF) / 15.0f;
        }
        out->push_back(e);
        pos = k + 7;
    }
    return true;
}

static bool ParseChildrenJson(const char* path,
                              std::vector<ShipExportSpriteChild>* out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return true;  // optional
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        std::fclose(f);
        return false;
    }
    std::string text((size_t)sz, '\0');
    if (std::fread(text.data(), 1, (size_t)sz, f) != (size_t)sz) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    // children.json writes pos in OBJ units (*kShipExportObjScale). Store world.
    const float inv = 1.f / kShipExportObjScale;
    size_t pos = 0;
    while (true) {
        size_t k = text.find("\"modelId\":", pos);
        if (k == std::string::npos) break;
        ShipExportSpriteChild c;
        int modelId = -1;
        float px = 0, py = 0, pz = 0;
        float r[9] = {};
        int scaleRelated = 0;
        const char* p = text.c_str() + k;
        if (sscanf(p,
                   "\"modelId\": %d, \"pos\": [%f, %f, %f], "
                   "\"rot\": [%f, %f, %f, %f, %f, %f, %f, %f, %f], "
                   "\"scaleRelated\": %d",
                   &modelId, &px, &py, &pz, &r[0], &r[1], &r[2], &r[3], &r[4],
                   &r[5], &r[6], &r[7], &r[8], &scaleRelated) < 14) {
            pos = k + 10;
            continue;
        }
        c.modelId = modelId;
        c.pos[0] = px * inv;
        c.pos[1] = py * inv;
        c.pos[2] = pz * inv;
        for (int i = 0; i < 9; i++) c.rot[i] = r[i];
        c.scaleRelated = scaleRelated;
        out->push_back(c);
        pos = k + 10;
    }
    return true;
}

bool LoadShipModelAssetDir(const char* dir, ShipModelAsset* out)
{
    if (!out || !dir) return false;
    *out = ShipModelAsset{};
    char objPath[1200];
    std::snprintf(objPath, sizeof(objPath), "%s/model.obj", dir);

    std::ifstream in(objPath);
    if (!in) {
        out->error = "missing model.obj";
        return false;
    }

    // Parse basename for ship index/name: .../027_krait
    std::string base = dir;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\'))
        base.pop_back();
    size_t slash = base.find_last_of("/\\");
    std::string leaf = (slash == std::string::npos) ? base : base.substr(slash + 1);
    if (leaf.size() >= 3 && std::isdigit((unsigned char)leaf[0])) {
        out->shipIndex = std::atoi(leaf.c_str());
        size_t us = leaf.find('_');
        out->name = (us == std::string::npos) ? leaf : leaf.substr(us + 1);
    }

    out->hull.pass = ShipAssetPass::Hull;
    out->punch.pass = ShipAssetPass::Punch;
    out->overlay.pass = ShipAssetPass::Overlay;
    out->line.pass = ShipAssetPass::Line;

    std::vector<float> vpos, vnrm, vuv;
    std::string objName = "hull";
    std::string usemtl;
    auto classify = [](const std::string& o,
                       const std::string& mtl) -> ShipAssetPass {
        if (o.find("_punch") != std::string::npos ||
            mtl.rfind("punch_", 0) == 0)
            return ShipAssetPass::Punch;
        if (o.find("_decal") != std::string::npos ||
            mtl.find("_decal") != std::string::npos)
            return ShipAssetPass::Overlay;
        if (o.find("line_") != std::string::npos ||
            o.find("_line") != std::string::npos ||
            mtl.rfind("line_", 0) == 0)
            return ShipAssetPass::Line;
        return ShipAssetPass::Hull;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'o' && line[1] == ' ') {
            objName = line.substr(2);
            while (!objName.empty() &&
                   (objName.back() == '\r' || objName.back() == ' '))
                objName.pop_back();
            continue;
        }
        if (line.rfind("usemtl ", 0) == 0) {
            usemtl = line.substr(7);
            while (!usemtl.empty() &&
                   (usemtl.back() == '\r' || usemtl.back() == ' '))
                usemtl.pop_back();
            continue;
        }
        if (line[0] == 'v' && line[1] == ' ') {
            float x = 0, y = 0, z = 0;
            sscanf(line.c_str() + 2, "%f %f %f", &x, &y, &z);
            // OBJ is scaled by kShipExportObjScale — convert back to world.
            const float inv = 1.f / kShipExportObjScale;
            vpos.push_back(x * inv);
            vpos.push_back(y * inv);
            vpos.push_back(z * inv);
            continue;
        }
        if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
            float x = 0, y = 0, z = 0;
            sscanf(line.c_str() + 3, "%f %f %f", &x, &y, &z);
            vnrm.push_back(x);
            vnrm.push_back(y);
            vnrm.push_back(z);
            continue;
        }
        if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            float u = 0, v = 0;
            sscanf(line.c_str() + 3, "%f %f", &u, &v);
            vuv.push_back(u);
            vuv.push_back(v);
            continue;
        }
        if (line[0] == 'f' && line[1] == ' ') {
            // f v/vt/vn  (1-based)
            int vi[4] = {}, ti[4] = {}, ni[4] = {};
            int n = 0;
            const char* p = line.c_str() + 2;
            while (*p && n < 4) {
                while (*p == ' ') p++;
                if (!*p || *p == '\r') break;
                int v = 0, vt = 0, vn = 0;
                if (sscanf(p, "%d/%d/%d", &v, &vt, &vn) >= 1) {
                    vi[n] = v;
                    ti[n] = vt;
                    ni[n] = vn;
                    n++;
                }
                while (*p && *p != ' ') p++;
            }
            if (n < 3) continue;
            auto makeVert = [&](int k) -> ShipExportVert {
                ShipExportVert vert;
                const int v = vi[k] - 1;
                const int vn = ni[k] - 1;
                const int vt = ti[k] - 1;
                if (v >= 0 && (size_t)v * 3 + 2 < vpos.size()) {
                    vert.x = vpos[(size_t)v * 3 + 0];
                    vert.y = vpos[(size_t)v * 3 + 1];
                    vert.z = vpos[(size_t)v * 3 + 2];
                }
                if (vn >= 0 && (size_t)vn * 3 + 2 < vnrm.size()) {
                    vert.nx = vnrm[(size_t)vn * 3 + 0];
                    vert.ny = vnrm[(size_t)vn * 3 + 1];
                    vert.nz = vnrm[(size_t)vn * 3 + 2];
                }
                if (vt >= 0 && (size_t)vt * 2 + 1 < vuv.size()) {
                    vert.u = vuv[(size_t)vt * 2 + 0];
                    vert.v = vuv[(size_t)vt * 2 + 1];
                }
                return vert;
            };
            ShipAssetGroup* g = &out->hull;
            switch (classify(objName, usemtl)) {
            case ShipAssetPass::Punch: g = &out->punch; break;
            case ShipAssetPass::Overlay: g = &out->overlay; break;
            case ShipAssetPass::Line: g = &out->line; break;
            default: break;
            }
            // fan triangulate
            for (int i = 1; i + 1 < n; i++) {
                AppendTri(g, makeVert(0), makeVert(i), makeVert(i + 1));
            }
        }
    }

    char emPath[1200];
    std::snprintf(emPath, sizeof(emPath), "%s/emitters.json", dir);
    ParseEmittersJson(emPath, &out->emitters);

    char kidsPath[1200];
    std::snprintf(kidsPath, sizeof(kidsPath), "%s/children.json", dir);
    ParseChildrenJson(kidsPath, &out->sprites);

    out->overlayNormalBias = kShipExportOverlayNormalBias;
    RepairAssetOutwardWinding(out);
    PromoteEmbeddedHullToOverlay(out);
    AbsorbLargeUntexturedOverlayIntoHull(out);
    UntextureHullCoplanarWithPunch(out);
    out->valid = !out->hull.indices.empty() || !out->overlay.indices.empty() ||
                 !out->punch.indices.empty() || !out->line.indices.empty();
    if (!out->valid) out->error = "no triangles";
    else {
        ShipAssetStampPunchClearance(out);
        ShipAssetStampOverlayClearance(out);
        ShipAssetStampPunchNormalBias(out);
    }
    return out->valid;
}

bool AttachShipRenderPolicy(ShipModelAsset* asset, const char* approvedDir,
                            const std::string& sourceAssetHash,
                            const std::unordered_set<uint32_t>& knownPrims)
{
    if (!asset || !asset->valid) return false;
    asset->hasRenderPolicy = false;
    asset->policyClassCount = 0;
    asset->primPolicies.clear();
    asset->punchClearancePolicyOverride = false;
    asset->sourceAssetHash = sourceAssetHash;
    asset->renderPolicyNote = "none";

    if (!approvedDir || !approvedDir[0] || sourceAssetHash.empty()) {
        asset->renderPolicyNote = "none (no approved dir or hash)";
        return false;
    }
    if (asset->shipIndex < 0 || asset->name.empty()) {
        asset->renderPolicyNote = "none (missing ship identity)";
        return false;
    }

    char path[1200];
    std::snprintf(path, sizeof(path), "%s/%03d_%s.render_policy.json",
                  approvedDir, asset->shipIndex, asset->name.c_str());
    RenderPolicySidecar pol;
    if (!LoadRenderPolicyJson(path, &pol)) {
        asset->renderPolicyNote =
            std::string("none (") +
            (pol.error.empty() ? "no sidecar" : pol.error) + ")";
        return false;
    }

    const auto v = ValidateRenderPolicy(pol, sourceAssetHash, knownPrims);
    if (!v.ok) {
        asset->renderPolicyNote =
            (v.code == ApprovalValidateResult::Code::HashMismatch)
                ? ("stale: " + v.error)
                : ("invalid: " + v.error);
        return false;
    }

    for (const auto& kv : pol.renderClasses) {
        ShipAssetPrimPolicy pp;
        pp.receiver = kv.second.receiver;
        pp.layer = kv.second.layer;
        pp.depthWrite = kv.second.depthWrite;
        pp.customDepthBias = true;
        pp.depthBiasConstant = kv.second.biasConstant;
        pp.depthBiasSlope = kv.second.biasSlope;
        asset->primPolicies[kv.first] = pp;
    }
    asset->hasRenderPolicy = true;
    asset->policyClassCount = (int)asset->primPolicies.size();
    if (pol.punchAftNudgeObj != 0.f) {
        asset->punchAftNudgeObj = pol.punchAftNudgeObj;
        asset->punchClearancePolicyOverride = true;
    }
    asset->renderPolicyNote =
        "applied by=" + pol.approvedBy +
        " classes=" + std::to_string(asset->policyClassCount);
    if (asset->punchAftNudgeObj != 0.f) {
        asset->renderPolicyNote +=
            " punchAft=" + std::to_string(asset->punchAftNudgeObj);
        if (!asset->punchClearancePolicyOverride)
            asset->renderPolicyNote += " (analytic)";
    }
    return true;
}

ShipAssetPassState ShipAssetResolvePassState(const ShipModelAsset& asset,
                                             ShipAssetPass pass,
                                             uint32_t primitiveId)
{
    ShipAssetPassState st = ShipAssetPassFor(pass, asset.overlayNormalBias,
                                             asset.punchNormalBias);
    if (!asset.hasRenderPolicy || primitiveId == 0) return st;
    auto it = asset.primPolicies.find(primitiveId);
    if (it == asset.primPolicies.end()) return st;
    const ShipAssetPrimPolicy& pp = it->second;
    st.depthWrite = pp.depthWrite;
    if (!st.depthWrite) st.depthFunc = ShipAssetDepthFunc::LEqual;
    if (pp.customDepthBias) {
        st.customDepthBias = true;
        st.depthBiasConstant = pp.depthBiasConstant;
        st.depthBiasSlope = pp.depthBiasSlope;
    }
    return st;
}

bool ShipAssetPlanPassRanges(ShipModelAsset* asset, ShipAssetPass pass,
                             std::vector<ShipAssetDrawRange>* ranges)
{
    if (!asset || !ranges) return false;
    ranges->clear();
    ShipAssetGroup* g = nullptr;
    switch (pass) {
    case ShipAssetPass::Punch: g = &asset->punch; break;
    case ShipAssetPass::Overlay: g = &asset->overlay; break;
    case ShipAssetPass::Line: g = &asset->line; break;
    case ShipAssetPass::Hull: g = &asset->hull; break;
    }
    if (!g || g->indices.size() < 3) return true;

    const int nTri = (int)g->indices.size() / 3;
    const bool haveIds =
        (int)g->primitiveIds.size() == nTri &&
        (int)g->sourceOrders.size() == nTri;

    // Default: single range with pass defaults (no per-prim policy needed).
    if (!haveIds || !asset->hasRenderPolicy ||
        (pass != ShipAssetPass::Overlay && pass != ShipAssetPass::Punch)) {
        ShipAssetDrawRange r;
        r.startIndex = 0;
        r.indexCount = (int)g->indices.size();
        r.state = ShipAssetPassFor(pass, asset->overlayNormalBias,
                                    asset->punchNormalBias);
        ranges->push_back(r);
        return true;
    }

    struct TriKey {
        int tri = 0;
        uint32_t receiver = 0;
        int layer = 0;
        int sourceOrder = 0;
        uint32_t prim = 0;
    };
    std::vector<TriKey> keys((size_t)nTri);
    for (int t = 0; t < nTri; t++) {
        const uint32_t prim = g->primitiveIds[(size_t)t];
        TriKey k;
        k.tri = t;
        k.prim = prim;
        k.sourceOrder = g->sourceOrders[(size_t)t];
        auto it = asset->primPolicies.find(prim);
        if (it != asset->primPolicies.end()) {
            k.receiver = it->second.receiver;
            k.layer = it->second.layer;
        }
        keys[(size_t)t] = k;
    }
    std::stable_sort(keys.begin(), keys.end(), [](const TriKey& a, const TriKey& b) {
        if (a.receiver != b.receiver) return a.receiver < b.receiver;
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.sourceOrder != b.sourceOrder) return a.sourceOrder < b.sourceOrder;
        return a.tri < b.tri;
    });

    // Permute indices / ids / orders into sorted order.
    // Verts (and triKdArgb keyed by AppendTri vert blocks) stay put so
    // D3dUploadAssetGroup can still color by vi/3.
    std::vector<uint32_t> newIdx;
    std::vector<uint32_t> newPrim;
    std::vector<int> newOrder;
    newIdx.reserve(g->indices.size());
    newPrim.reserve((size_t)nTri);
    newOrder.reserve((size_t)nTri);
    for (const TriKey& k : keys) {
        const size_t base = (size_t)k.tri * 3;
        newIdx.push_back(g->indices[base + 0]);
        newIdx.push_back(g->indices[base + 1]);
        newIdx.push_back(g->indices[base + 2]);
        newPrim.push_back(g->primitiveIds[(size_t)k.tri]);
        newOrder.push_back(g->sourceOrders[(size_t)k.tri]);
    }
    g->indices.swap(newIdx);
    g->primitiveIds.swap(newPrim);
    g->sourceOrders.swap(newOrder);

    // Emit contiguous ranges where resolved state is identical.
    auto stateEq = [](const ShipAssetPassState& a, const ShipAssetPassState& b) {
        return a.depthWrite == b.depthWrite && a.depthFunc == b.depthFunc &&
               a.customDepthBias == b.customDepthBias &&
               a.depthBiasConstant == b.depthBiasConstant &&
               a.depthBiasSlope == b.depthBiasSlope &&
               a.cullBack == b.cullBack;
    };

    ShipAssetDrawRange cur;
    cur.startIndex = 0;
    cur.indexCount = 0;
    cur.state = ShipAssetResolvePassState(*asset, pass, g->primitiveIds[0]);
    for (int t = 0; t < nTri; t++) {
        const ShipAssetPassState st =
            ShipAssetResolvePassState(*asset, pass, g->primitiveIds[(size_t)t]);
        if (t > 0 && !stateEq(st, cur.state)) {
            ranges->push_back(cur);
            cur.startIndex = t * 3;
            cur.indexCount = 0;
            cur.state = st;
        }
        cur.indexCount += 3;
    }
    if (cur.indexCount > 0) ranges->push_back(cur);
    return true;
}

bool CollectShipPunchClearance(int shipIndex, ShipPunchClearance* out,
                               ShipModelAsset* assetOut)
{
    if (!out) return false;
    *out = ShipPunchClearance{};
    ShipExportMesh mesh;
    if (!BuildExportMesh(shipIndex, &mesh) || !mesh.valid) return false;
    ShipModelAsset asset;
    if (!BuildShipModelAsset(mesh, &asset) || !asset.valid) return false;
    *out = ShipAssetComputePunchClearance(asset.punch, asset.overlay, asset.hull);
    if (assetOut) *assetOut = std::move(asset);
    return true;
}

namespace {

void PunchVisRotate(float x, float y, float z, float yawDeg, float pitchDeg,
                    float* ox, float* oy, float* oz)
{
    const float yr = yawDeg * 0.01745329252f;
    const float pr = pitchDeg * 0.01745329252f;
    const float cy = std::cos(yr), sy = std::sin(yr);
    const float cp = std::cos(pr), sp = std::sin(pr);
    const float x1 = x * cy + z * sy;
    const float z1 = -x * sy + z * cy;
    *ox = x1;
    *oy = y * cp - z1 * sp;
    *oz = y * sp + z1 * cp;
}

void PunchVisFillTri(std::vector<float>* zbuf, std::vector<uint8_t>* idbuf,
                     int w, int h, float x0, float y0, float z0, float x1,
                     float y1, float z1, float x2, float y2, float z2,
                     uint8_t id, bool writeZ, bool* anyPass)
{
    const int xmin = std::max(0, (int)std::floor(std::min({x0, x1, x2})));
    const int xmax = std::min(w - 1, (int)std::ceil(std::max({x0, x1, x2})));
    const int ymin = std::max(0, (int)std::floor(std::min({y0, y1, y2})));
    const int ymax = std::min(h - 1, (int)std::ceil(std::max({y0, y1, y2})));
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-8f) return;
    const float invA = 1.f / area;
    for (int y = ymin; y <= ymax; y++) {
        for (int x = xmin; x <= xmax; x++) {
            const float px = (float)x + 0.5f, py = (float)y + 0.5f;
            const float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * invA;
            const float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * invA;
            const float w2 = 1.f - w0 - w1;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
            const float z = w0 * z0 + w1 * z1 + w2 * z2;
            const size_t i = (size_t)y * (size_t)w + (size_t)x;
            if (z >= (*zbuf)[i]) continue;
            if (anyPass) *anyPass = true;
            if (idbuf) (*idbuf)[i] = id;
            if (writeZ) (*zbuf)[i] = z;
        }
    }
}

int PunchVisRaster(const ShipModelAsset& asset, float yawDeg, float pitchDeg,
                   float punchAftNudgeObj)
{
    constexpr int kW = 320, kH = 200;
    constexpr float kEye = 2.0f;
    constexpr float kF = 256.f;
    std::vector<float> zbuf((size_t)kW * (size_t)kH, 1e30f);
    std::vector<uint8_t> idbuf((size_t)kW * (size_t)kH, 0);

    auto drawGroup = [&](const ShipAssetGroup& g, uint8_t id, bool writeZ,
                         float dz) {
        for (size_t t = 0; t + 2 < g.indices.size(); t += 3) {
            const uint32_t ia = g.indices[t], ib = g.indices[t + 1],
                           ic = g.indices[t + 2];
            if (ia >= g.verts.size() || ib >= g.verts.size() ||
                ic >= g.verts.size())
                continue;
            float sx[3], sy[3], sz[3];
            const ShipExportVert* vs[3] = {&g.verts[ia], &g.verts[ib],
                                           &g.verts[ic]};
            bool behind = false;
            for (int k = 0; k < 3; k++) {
                float x, y, z;
                PunchVisRotate(vs[k]->x, vs[k]->y, vs[k]->z + dz, yawDeg,
                               pitchDeg, &x, &y, &z);
                const float depth = kEye - z;
                if (depth < 0.05f) {
                    behind = true;
                    break;
                }
                sx[k] = 160.f + kF * x / depth;
                sy[k] = 100.f + kF * y / depth;
                sz[k] = depth;
            }
            if (behind) continue;
            PunchVisFillTri(&zbuf, &idbuf, kW, kH, sx[0], sy[0], sz[0], sx[1],
                            sy[1], sz[1], sx[2], sy[2], sz[2], id, writeZ,
                            nullptr);
        }
    };

    drawGroup(asset.hull, 1, true, 0.f);
    drawGroup(asset.overlay, 2, true, 0.f);
    const float dz = -punchAftNudgeObj / kShipExportObjScale;
    drawGroup(asset.punch, 3, false, dz);
    int n = 0;
    for (uint8_t v : idbuf)
        if (v == 3) n++;
    return n;
}

}  // namespace

int ShipAssetPunchVisiblePixels(const ShipModelAsset& asset, float yawDeg,
                                float pitchDeg, float punchAftNudgeObj)
{
    if (!asset.valid || asset.punch.indices.size() < 3) return 0;
    return PunchVisRaster(asset, yawDeg, pitchDeg, punchAftNudgeObj);
}

bool ShipAssetCollectPunchVisibility(const ShipModelAsset& asset, float yawDeg,
                                     float pitchDeg,
                                     ShipPunchVisibilityPose* out)
{
    if (!out) return false;
    out->yawDeg = yawDeg;
    out->pitchDeg = pitchDeg;
    out->punchPixBefore = ShipAssetPunchVisiblePixels(asset, yawDeg, pitchDeg, 0.f);
    out->punchPixAfter =
        ShipAssetPunchVisiblePixels(asset, yawDeg, pitchDeg, asset.punchAftNudgeObj);
    return true;
}

}  // namespace jjffe
