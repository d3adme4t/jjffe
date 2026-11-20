#include "jjffe/contour_tessellate.h"
#include "jjffe/contour_bezier.h"
#include "jjffe/math.h"

#include <clipper2/clipper.h>
#include <mapbox/earcut.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace jjffe {

// ---------------------------------------------------------------------------
// Phase 1 — ContourTessellator (world-space fixed-point Bézier + rings)
// ---------------------------------------------------------------------------

void ContourTessellator::EvaluateBezier3D(const ContourPoint3D& p0,
                                          const ContourPoint3D& p1,
                                          const ContourPoint3D& p2,
                                          const ContourPoint3D& p3,
                                          ContourRing& outRing)
{
    // Dense samples: world>>16 spans are tiny vs soft screen pixels, so force
    // at least 16 segments (sVar6>=4) for visible Contour curves.
    const int32_t a[3] = {p0.x, p0.y, p0.z};
    const int32_t b[3] = {p1.x, p1.y, p1.z};
    const int32_t c[3] = {p2.x, p2.y, p2.z};
    const int32_t d[3] = {p3.x, p3.y, p3.z};
    std::vector<ContourPt3> samples;
    ContourBezierSample3iMin(a, b, c, d, /*minStepBits=*/4, samples);
    outRing.points.reserve(outRing.points.size() + samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        if (i == 0 && !outRing.points.empty()) {
            const ContourPoint3D& last = outRing.points.back();
            if (last.x == samples[0].x && last.y == samples[0].y &&
                last.z == samples[0].z)
                continue;
        }
        outRing.points.push_back(
            {samples[i].x, samples[i].y, samples[i].z});
    }
}

namespace {

// Quantize Contour endpoints so bezier samples + bridge verts that share a
// soft Contour vertex still meet after 16.16 transform noise.
// 1/64 world-unit: below distinct Contour spacing, above fp jitter.
constexpr int32_t kEndpointQuantum = FP_ONE / 64;

struct ContourSeg {
    ContourPoint3D a{}, b{};
    bool used = false;
};

struct EndpointKey {
    int32_t x = 0, y = 0, z = 0;
    bool operator==(const EndpointKey& o) const
    {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct EndpointKeyHash {
    size_t operator()(const EndpointKey& k) const
    {
        return (size_t)k.x * 73856093u ^ (size_t)k.y * 19349663u ^
               (size_t)k.z * 83492791u;
    }
};

EndpointKey QuantizeEndpoint(const ContourPoint3D& p)
{
    auto q = [](int32_t v) -> int32_t {
        const int32_t half = kEndpointQuantum / 2;
        if (v >= 0) return (v + half) / kEndpointQuantum;
        return -((-v + half) / kEndpointQuantum);
    };
    return {q(p.x), q(p.y), q(p.z)};
}

// Edge-walk Contour soup (bezier polylines + bridge edges) into closed rings.
// Follows real shared endpoints — does NOT invent petal-close chords.
// Open chains are dropped (caller may fall back to even-odd).
void ChainContourSegments(std::vector<ContourSeg>& segs,
                          std::vector<ContourRing>& outRings, int* openOut,
                          std::vector<ContourRing>* openRings)
{
    int openChains = 0;
    using Adj = std::unordered_map<EndpointKey, std::vector<int>, EndpointKeyHash>;
    Adj at;
    at.reserve(segs.size() * 2);
    for (int i = 0; i < (int)segs.size(); i++) {
        at[QuantizeEndpoint(segs[i].a)].push_back(i);
        at[QuantizeEndpoint(segs[i].b)].push_back(i);
    }

    auto otherEnd = [](const ContourSeg& s, const EndpointKey& tip,
                       ContourPoint3D& outPt) -> bool {
        if (QuantizeEndpoint(s.a) == tip) {
            outPt = s.b;
            return true;
        }
        if (QuantizeEndpoint(s.b) == tip) {
            outPt = s.a;
            return true;
        }
        return false;
    };

    for (int seed = 0; seed < (int)segs.size(); seed++) {
        if (segs[seed].used) continue;
        ContourRing ring;
        ring.points.push_back(segs[seed].a);
        ring.points.push_back(segs[seed].b);
        segs[seed].used = true;
        const EndpointKey startKey = QuantizeEndpoint(segs[seed].a);
        EndpointKey tip = QuantizeEndpoint(segs[seed].b);

        bool closed = false;
        for (;;) {
            if (tip == startKey && ring.points.size() >= 3) {
                closed = true;
                break;
            }
            int next = -1;
            auto it = at.find(tip);
            if (it != at.end()) {
                for (int si : it->second) {
                    if (!segs[si].used) {
                        next = si;
                        break;
                    }
                }
            }
            if (next < 0) break;
            ContourPoint3D nxt{};
            if (!otherEnd(segs[next], tip, nxt)) break;
            segs[next].used = true;
            tip = QuantizeEndpoint(nxt);
            // Skip duplicate when next seg starts at previous tip.
            if (ring.points.back().x != nxt.x || ring.points.back().y != nxt.y ||
                ring.points.back().z != nxt.z)
                ring.points.push_back(nxt);
        }

        if (closed && ring.points.size() >= 3) {
            // Drop duplicate close point if walk re-added start.
            if (ring.points.size() >= 2) {
                const ContourPoint3D& f = ring.points.front();
                const ContourPoint3D& b = ring.points.back();
                if (f.x == b.x && f.y == b.y && f.z == b.z)
                    ring.points.pop_back();
            }
            if (ring.points.size() >= 3)
                outRings.push_back(std::move(ring));
        } else {
            openChains++;
            if (openRings && ring.points.size() >= 3)
                openRings->push_back(std::move(ring));
        }
    }
    if (openOut) *openOut = openChains;
}

} // namespace

std::vector<ContourRing> ContourTessellator::ExtractRings(
    const TransformedPoly* const* pieces, int nPieces)
{
    std::vector<ContourRing> rings;
    if (nPieces <= 0) return rings;

    const uint16_t tex = pieces[0]->texInfo;
    const bool punchDigons =
        tex == 0x20ee && !IsContourGlintPoly(*pieces[0]);

    int nQuad = 0;
    int nEdge = 0;
    for (int i = 0; i < nPieces; i++) {
        if (pieces[i]->vertexCount == 4) nQuad++;
        if (pieces[i]->vertexCount == 2) nEdge++;
    }

    auto toPt = [](const ProjectedVertex& v) -> ContourPoint3D {
        return {v.worldX, v.worldY, v.worldZ};
    };

    // Pure digon pairs only (no bridge edges): closed rings for leftover
    // planar plates. Puma's 4-quad+bridge butterfly is a ruled strip
    // (EmitPetalStrips), not an earcut ring.
    if (nEdge == 0 && nQuad >= 2 && (nQuad % 2) == 0) {
        bool allQuad = true;
        for (int i = 0; i < nPieces; i++)
            if (pieces[i]->vertexCount != 4) {
                allQuad = false;
                break;
            }
        if (allQuad) {
            for (int i = 0; i + 1 < nPieces; i += 2) {
                const TransformedPoly& up = *pieces[i];
                const TransformedPoly& lo = *pieces[i + 1];
                ContourRing upper, lower;
                EvaluateBezier3D(toPt(up.verts[0]), toPt(up.verts[1]),
                                 toPt(up.verts[2]), toPt(up.verts[3]), upper);
                EvaluateBezier3D(toPt(lo.verts[0]), toPt(lo.verts[1]),
                                 toPt(lo.verts[2]), toPt(lo.verts[3]), lower);
                if (upper.points.size() < 2 || lower.points.size() < 2)
                    continue;
                ContourRing ring = upper;
                for (size_t k = lower.points.size(); k-- > 1;) {
                    if (k + 1 == lower.points.size()) continue;
                    ring.points.push_back(lower.points[k]);
                }
                ring.isHole = punchDigons;
                if (ring.points.size() >= 3) rings.push_back(std::move(ring));
            }
            return rings;
        }
    }

    // Mixed Contour soup (Puma: bezier petals + bridge edges). Edge-walk by
    // shared endpoints into closed rings → TessellateRings (Clipper2+earcut).
    // Do NOT invent petal-close chords; only follow real segments. If chaining
    // fails to close, return empty → BuildContourWorldFaces even-odd fallback.
    std::vector<ContourSeg> segs;
    segs.reserve((size_t)nPieces * 16);
    auto pushPoly = [&](const ContourRing& poly) {
        for (size_t i = 1; i < poly.points.size(); i++) {
            const ContourPoint3D& a = poly.points[i - 1];
            const ContourPoint3D& b = poly.points[i];
            if (a.x == b.x && a.y == b.y && a.z == b.z) continue;
            segs.push_back({a, b, false});
        }
    };

    for (int i = 0; i < nPieces; i++) {
        const TransformedPoly& p = *pieces[i];
        if (p.vertexCount == 4) {
            ContourRing bez;
            EvaluateBezier3D(toPt(p.verts[0]), toPt(p.verts[1]),
                             toPt(p.verts[2]), toPt(p.verts[3]), bez);
            pushPoly(bez);
        } else if (p.vertexCount == 2) {
            ContourPoint3D a = toPt(p.verts[0]), b = toPt(p.verts[1]);
            if (a.x != b.x || a.y != b.y || a.z != b.z)
                segs.push_back({a, b, false});
        } else if (p.vertexCount == 3) {
            ContourPoint3D a = toPt(p.verts[0]), b = toPt(p.verts[1]),
                           c = toPt(p.verts[2]);
            if (a.x != b.x || a.y != b.y || a.z != b.z)
                segs.push_back({a, b, false});
            if (b.x != c.x || b.y != c.y || b.z != c.z)
                segs.push_back({b, c, false});
            if (c.x != a.x || c.y != a.y || c.z != a.z)
                segs.push_back({c, a, false});
        }
    }

    if (segs.empty()) return rings;

    // Soft Contour close (001796): cursor→start. Skip on wide multi-quad
    // Contours (Puma butterfly) — forced close punches even-odd holes.
    // Use screen AABB (same criterion as TessellateEvenOddEdges / Ogre).
    float aabbX0 = 1e9f, aabbY0 = 1e9f, aabbX1 = -1e9f, aabbY1 = -1e9f;
    float firstX = 0, firstY = 0, lastX = 0, lastY = 0;
    bool haveFirst = false;
    ContourPoint3D firstPt{}, lastPt{};
    bool haveFirstPt = false;
    for (int i = 0; i < nPieces; i++) {
        const TransformedPoly& p = *pieces[i];
        auto grow = [&](float x, float y) {
            aabbX0 = std::min(aabbX0, x);
            aabbY0 = std::min(aabbY0, y);
            aabbX1 = std::max(aabbX1, x);
            aabbY1 = std::max(aabbY1, y);
        };
        if (p.vertexCount == 4) {
            grow((float)p.verts[0].screenX, (float)p.verts[0].screenY);
            grow((float)p.verts[3].screenX, (float)p.verts[3].screenY);
            if (!haveFirst) {
                firstX = (float)p.verts[0].screenX;
                firstY = (float)p.verts[0].screenY;
                firstPt = toPt(p.verts[0]);
                haveFirst = true;
                haveFirstPt = true;
            }
            lastX = (float)p.verts[3].screenX;
            lastY = (float)p.verts[3].screenY;
            lastPt = toPt(p.verts[3]);
        } else if (p.vertexCount == 2) {
            grow((float)p.verts[0].screenX, (float)p.verts[0].screenY);
            grow((float)p.verts[1].screenX, (float)p.verts[1].screenY);
            if (!haveFirst) {
                firstX = (float)p.verts[0].screenX;
                firstY = (float)p.verts[0].screenY;
                firstPt = toPt(p.verts[0]);
                haveFirst = true;
                haveFirstPt = true;
            }
            lastX = (float)p.verts[1].screenX;
            lastY = (float)p.verts[1].screenY;
            lastPt = toPt(p.verts[1]);
        }
    }
    const float spanW = aabbX1 - aabbX0;
    const float spanH = aabbY1 - aabbY0;
    const bool wideContour = spanW * 5.0f >= spanH * 9.0f;
    const bool digonWideClose = (nQuad == 2) && wideContour;
    if (haveFirstPt && (!wideContour || digonWideClose)) {
        const double dx = (double)(lastPt.x - firstPt.x);
        const double dy = (double)(lastPt.y - firstPt.y);
        const double dz = (double)(lastPt.z - firstPt.z);
        if (dx * dx + dy * dy + dz * dz > 1.0)
            segs.push_back({lastPt, firstPt, false});
    }
    (void)firstX;
    (void)firstY;
    (void)lastX;
    (void)lastY;

    int openChains = 0;
    std::vector<ContourRing> open;
    ChainContourSegments(segs, rings, &openChains, &open);
    for (ContourRing& r : rings)
        r.isHole = punchDigons;
    // Promote only nearly-closed open chains. A single bezier stroke has
    // distant endpoints — filling it is the curved-sail artifact.
    for (ContourRing& r : open) {
        if (r.points.size() < 4) continue;
        const ContourPoint3D& a = r.points.front();
        const ContourPoint3D& b = r.points.back();
        const double dx = (double)(a.x - b.x) / (double)FP_ONE;
        const double dy = (double)(a.y - b.y) / (double)FP_ONE;
        const double dz = (double)(a.z - b.z) / (double)FP_ONE;
        const double gap = std::sqrt(dx * dx + dy * dy + dz * dz);
        double ext = 0;
        for (const ContourPoint3D& p : r.points) {
            const double ex = (double)(p.x - a.x) / (double)FP_ONE;
            const double ey = (double)(p.y - a.y) / (double)FP_ONE;
            const double ez = (double)(p.z - a.z) / (double)FP_ONE;
            const double d = std::sqrt(ex * ex + ey * ey + ez * ez);
            if (d > ext) ext = d;
        }
        if (gap > 0.15 * std::max(ext, 1e-6)) continue;
        r.isHole = punchDigons;
        rings.push_back(std::move(r));
    }

    if (std::getenv("AB_DUMP_CONTOUR_CHAIN") &&
        std::getenv("AB_DUMP_CONTOUR_CHAIN")[0] == '1') {
        fprintf(stderr,
                "ContourChain: pieces=%d nQuad=%d nEdge=%d segs=%zu rings=%zu "
                "open=%d wide=%d tex=%04x\n",
                nPieces, nQuad, nEdge, segs.size(), rings.size(), openChains,
                (int)wideContour, tex);
    }

    // Empty rings → caller falls back to TessellateEvenOddEdges.
    (void)nQuad;
    return rings;
}

// ---------------------------------------------------------------------------
// Phase 2–3 — Clipper2 + earcut → ContourHull
// ---------------------------------------------------------------------------

namespace {

struct PlaneBasis {
    float ox = 0, oy = 0, oz = 0;
    float ux = 1, uy = 0, uz = 0;
    float vx = 0, vy = 1, vz = 0;
    float nx = 0, ny = 0, nz = 1;
};

void Normalize3(float& x, float& y, float& z)
{
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-8f) {
        x = 0;
        y = 0;
        z = 1;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

bool BuildPlaneBasis(const ContourRing& ring, float nx, float ny, float nz,
                     PlaneBasis& out)
{
    if (ring.points.size() < 3) return false;
    out.ox = (float)ring.points[0].x / (float)FP_ONE;
    out.oy = (float)ring.points[0].y / (float)FP_ONE;
    out.oz = (float)ring.points[0].z / (float)FP_ONE;
    out.nx = nx;
    out.ny = ny;
    out.nz = nz;
    Normalize3(out.nx, out.ny, out.nz);

    float ax = 1, ay = 0, az = 0;
    if (std::fabs(out.nx) > 0.9f) {
        ax = 0;
        ay = 1;
        az = 0;
    }
    out.ux = ay * out.nz - az * out.ny;
    out.uy = az * out.nx - ax * out.nz;
    out.uz = ax * out.ny - ay * out.nx;
    Normalize3(out.ux, out.uy, out.uz);
    out.vx = out.ny * out.uz - out.nz * out.uy;
    out.vy = out.nz * out.ux - out.nx * out.uz;
    out.vz = out.nx * out.uy - out.ny * out.ux;
    Normalize3(out.vx, out.vy, out.vz);
    return true;
}

void WorldToPlane(const PlaneBasis& b, int32_t wx, int32_t wy, int32_t wz,
                  double& u, double& v)
{
    const float x = (float)wx / (float)FP_ONE - b.ox;
    const float y = (float)wy / (float)FP_ONE - b.oy;
    const float z = (float)wz / (float)FP_ONE - b.oz;
    u = (double)(x * b.ux + y * b.uy + z * b.uz);
    v = (double)(x * b.vx + y * b.vy + z * b.vz);
}

void PlaneToWorld(const PlaneBasis& b, double u, double v, float& x, float& y,
                  float& z)
{
    x = b.ox + (float)u * b.ux + (float)v * b.vx;
    y = b.oy + (float)u * b.uy + (float)v * b.vy;
    z = b.oz + (float)u * b.uz + (float)v * b.vz;
}

int EmitFanTris(const TransformedPoly& p, float nx, float ny, float nz,
                WorldFace* out, int maxOut, int& written, int16_t cid,
                int meshIndex, int sourceIndex, uint16_t tex, uint8_t nidx)
{
    if (p.vertexCount < 3 || written >= maxOut) return 0;
    auto emit = [&](int i0, int i1, int i2) {
        if (written >= maxOut) return;
        WorldFace& f = out[written++];
        f = WorldFace{};
        f.kind = WorldFaceKind::ContourHull;
        f.type = MeshPolyType::ComplexFill;
        f.texInfo = tex;
        f.normalIdx = nidx;
        f.vertexCount = 3;
        f.worldValid = true;
        f.authoredNormal = true;
        f.contourId = cid;
        f.meshIndex = meshIndex;
        f.sourceIndex = sourceIndex;
        const int idx[3] = {i0, i1, i2};
        for (int k = 0; k < 3; k++) {
            const ProjectedVertex& v = p.verts[idx[k]];
            f.verts[k].x = (float)v.worldX / (float)FP_ONE;
            f.verts[k].y = (float)v.worldY / (float)FP_ONE;
            f.verts[k].z = (float)v.worldZ / (float)FP_ONE;
            f.verts[k].nx = nx;
            f.verts[k].ny = ny;
            f.verts[k].nz = nz;
        }
    };
    emit(0, 1, 2);
    return 1;
}

void FillContourTri(WorldFace& f, const ContourPoint3D& a, const ContourPoint3D& b,
                    const ContourPoint3D& c, float nx, float ny, float nz,
                    const TransformedPoly& head, int sourceIndex)
{
    f = WorldFace{};
    f.kind = WorldFaceKind::ContourHull;
    f.type = MeshPolyType::ComplexFill;
    f.texInfo = head.texInfo;
    f.normalIdx = head.normalIdx;
    f.vertexCount = 3;
    f.worldValid = true;
    f.authoredNormal = true;
    f.contourId = head.contourId;
    f.meshIndex = head.meshIndex;
    f.sourceIndex = sourceIndex;
    f.sortKey = head.sortKey;
    f.faceLight[0] = head.faceLight[0];
    f.faceLight[1] = head.faceLight[1];
    f.faceLight[2] = head.faceLight[2];
    const ContourPoint3D* p[3] = {&a, &b, &c};
    for (int k = 0; k < 3; k++) {
        f.verts[k].x = (float)p[k]->x / (float)FP_ONE;
        f.verts[k].y = (float)p[k]->y / (float)FP_ONE;
        f.verts[k].z = (float)p[k]->z / (float)FP_ONE;
        f.verts[k].nx = nx;
        f.verts[k].ny = ny;
        f.verts[k].nz = nz;
    }
}

ContourPoint3D SamplePoly(const std::vector<ContourPoint3D>& pts, float t)
{
    if (pts.empty()) return {};
    if (pts.size() == 1 || t <= 0.f) return pts.front();
    if (t >= 1.f) return pts.back();
    const float f = t * (float)(pts.size() - 1);
    const int i = (int)f;
    if (i >= (int)pts.size() - 1) return pts.back();
    const float u = f - (float)i;
    const ContourPoint3D& a = pts[(size_t)i];
    const ContourPoint3D& b = pts[(size_t)i + 1];
    ContourPoint3D o;
    o.x = a.x + (int32_t)std::lround((double)(b.x - a.x) * (double)u);
    o.y = a.y + (int32_t)std::lround((double)(b.y - a.y) * (double)u);
    o.z = a.z + (int32_t)std::lround((double)(b.z - a.z) * (double)u);
    return o;
}

double Dist2Pt(const ContourPoint3D& a, const ContourPoint3D& b)
{
    const double dx = (double)a.x - (double)b.x;
    const double dy = (double)a.y - (double)b.y;
    const double dz = (double)a.z - (double)b.z;
    return dx * dx + dy * dy + dz * dz;
}

int EmitRuledStrip(const ContourRing& upper, ContourRing lower, float nx,
                   float ny, float nz, WorldFace* out, int maxOut, int& written,
                   const TransformedPoly& head, int sourceIndex)
{
    if (upper.points.size() < 2 || lower.points.size() < 2) return 0;
    if (Dist2Pt(upper.points.front(), lower.points.back()) <
        Dist2Pt(upper.points.front(), lower.points.front()))
        std::reverse(lower.points.begin(), lower.points.end());

    // Reject pairs whose curves do not run alongside each other.
    const double invFp = 1.0 / (double)FP_ONE;
    const double end0 = Dist2Pt(upper.points.front(), lower.points.front());
    const double end1 = Dist2Pt(upper.points.back(), lower.points.back());
    double uLen = 0, lLen = 0;
    for (size_t i = 1; i < upper.points.size(); i++)
        uLen += std::sqrt(Dist2Pt(upper.points[i - 1], upper.points[i])) * invFp;
    for (size_t i = 1; i < lower.points.size(); i++)
        lLen += std::sqrt(Dist2Pt(lower.points[i - 1], lower.points[i])) * invFp;
    const double span = std::max(uLen, lLen);
    const double sep = std::sqrt(std::max(end0, end1)) * invFp;
    if (span < 1e-6 || sep > 2.0 * span) return 0;

    const int n = std::max((int)upper.points.size(), (int)lower.points.size());
    if (n < 2) return 0;
    int added = 0;
    for (int i = 0; i < n - 1; i++) {
        const float t0 = (float)i / (float)(n - 1);
        const float t1 = (float)(i + 1) / (float)(n - 1);
        const ContourPoint3D a = SamplePoly(upper.points, t0);
        const ContourPoint3D b = SamplePoly(upper.points, t1);
        const ContourPoint3D c = SamplePoly(lower.points, t1);
        const ContourPoint3D d = SamplePoly(lower.points, t0);
        const ContourPoint3D* tris[2][3] = {{&a, &b, &c}, {&a, &c, &d}};
        for (int t = 0; t < 2; t++) {
            if (written >= maxOut) return added;
            float tnx = nx, tny = ny, tnz = nz;
            AnalyticalFaceNormal(tris[t][0]->x, tris[t][0]->y, tris[t][0]->z,
                                 tris[t][1]->x, tris[t][1]->y, tris[t][1]->z,
                                 tris[t][2]->x, tris[t][2]->y, tris[t][2]->z,
                                 tnx, tny, tnz);
            FillContourTri(out[written], *tris[t][0], *tris[t][1], *tris[t][2],
                           tnx, tny, tnz, head, sourceIndex);
            written++;
            added++;
        }
    }
    return added;
}

int EmitPetalStrips(const TransformedPoly* const* pieces, int nPieces, float nx,
                    float ny, float nz, WorldFace* out, int maxOut, int& written,
                    const TransformedPoly& head, int sourceIndex)
{
    int nQuad = 0, nEdge = 0;
    for (int i = 0; i < nPieces; i++) {
        if (pieces[i]->vertexCount == 4) nQuad++;
        if (pieces[i]->vertexCount == 2) nEdge++;
    }
    if (nQuad < 2 || (nQuad % 2) != 0 || nEdge > nQuad) return 0;

    auto toPt = [](const ProjectedVertex& v) -> ContourPoint3D {
        return {v.worldX, v.worldY, v.worldZ};
    };
    std::vector<const TransformedPoly*> quads;
    quads.reserve((size_t)nQuad);
    for (int i = 0; i < nPieces; i++)
        if (pieces[i]->vertexCount == 4) quads.push_back(pieces[i]);
    if ((int)quads.size() != nQuad) return 0;

    auto shareScore = [&](const TransformedPoly& a, const TransformedPoly& b) {
        EndpointKey ak[2] = {QuantizeEndpoint(toPt(a.verts[0])),
                             QuantizeEndpoint(toPt(a.verts[3]))};
        EndpointKey bk[2] = {QuantizeEndpoint(toPt(b.verts[0])),
                             QuantizeEndpoint(toPt(b.verts[3]))};
        int s = 0;
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++)
                if (ak[i] == bk[j]) s++;
        return s;
    };

    std::vector<char> used((size_t)nQuad, 0);
    int added = 0;
    for (int i = 0; i < nQuad; i++) {
        if (used[i]) continue;
        int best = -1, bestS = 0;
        for (int j = i + 1; j < nQuad; j++) {
            if (used[j]) continue;
            const int s = shareScore(*quads[i], *quads[j]);
            if (s > bestS) {
                bestS = s;
                best = j;
            }
        }
        // Digons share both ends. A single shared tip (Puma nose) pairs
        // unrelated curves into the long Blender needles.
        if (best < 0 || bestS < 2) continue;
        used[i] = used[best] = 1;
        ContourRing upper, lower;
        ContourTessellator::EvaluateBezier3D(
            toPt(quads[i]->verts[0]), toPt(quads[i]->verts[1]),
            toPt(quads[i]->verts[2]), toPt(quads[i]->verts[3]), upper);
        ContourTessellator::EvaluateBezier3D(
            toPt(quads[best]->verts[0]), toPt(quads[best]->verts[1]),
            toPt(quads[best]->verts[2]), toPt(quads[best]->verts[3]), lower);
        added += EmitRuledStrip(upper, lower, nx, ny, nz, out, maxOut, written,
                                head, sourceIndex);
        if (std::getenv("AB_DUMP_CONTOUR_CHAIN") &&
            std::getenv("AB_DUMP_CONTOUR_CHAIN")[0] == '1') {
            auto dump = [](const char* tag, const TransformedPoly* p) {
                fprintf(stderr, "  %s", tag);
                for (int k = 0; k < 4; k++)
                    fprintf(stderr, " (%.3f,%.3f,%.3f)",
                            (float)p->verts[k].worldX / (float)FP_ONE,
                            (float)p->verts[k].worldY / (float)FP_ONE,
                            (float)p->verts[k].worldZ / (float)FP_ONE);
                fprintf(stderr, "\n");
            };
            fprintf(stderr, "PetalStrip nU=%zu nL=%zu\n", upper.points.size(),
                    lower.points.size());
            dump("up", quads[i]);
            dump("lo", quads[best]);
        }
    }
    return added;
}

void FitRingNormal(const ContourRing& ring, float& nx, float& ny, float& nz)
{
    if (ring.points.size() < 3) return;
    double cx = 0, cy = 0, cz = 0;
    for (const ContourPoint3D& p : ring.points) {
        cx += (double)p.x / (double)FP_ONE;
        cy += (double)p.y / (double)FP_ONE;
        cz += (double)p.z / (double)FP_ONE;
    }
    const double inv = 1.0 / (double)ring.points.size();
    cx *= inv;
    cy *= inv;
    cz *= inv;

    auto residual = [&](float nnx, float nny, float nnz) -> float {
        float maxAbs = 0, maxExt = 0;
        for (const ContourPoint3D& p : ring.points) {
            const float x = (float)p.x / (float)FP_ONE - (float)cx;
            const float y = (float)p.y / (float)FP_ONE - (float)cy;
            const float z = (float)p.z / (float)FP_ONE - (float)cz;
            const float ext = std::sqrt(x * x + y * y + z * z);
            if (ext > maxExt) maxExt = ext;
            const float d = std::fabs(x * nnx + y * nny + z * nnz);
            if (d > maxAbs) maxAbs = d;
        }
        return maxAbs / std::max(maxExt, 1e-6f);
    };

    const float headerR = residual(nx, ny, nz);

    int iFar = 0;
    double bestD = -1;
    for (int i = 0; i < (int)ring.points.size(); i++) {
        const double x = (double)ring.points[i].x / (double)FP_ONE - cx;
        const double y = (double)ring.points[i].y / (double)FP_ONE - cy;
        const double z = (double)ring.points[i].z / (double)FP_ONE - cz;
        const double d = x * x + y * y + z * z;
        if (d > bestD) {
            bestD = d;
            iFar = i;
        }
    }
    float ux = (float)((double)ring.points[iFar].x / (double)FP_ONE - cx);
    float uy = (float)((double)ring.points[iFar].y / (double)FP_ONE - cy);
    float uz = (float)((double)ring.points[iFar].z / (double)FP_ONE - cz);
    Normalize3(ux, uy, uz);

    int iOff = 0;
    float bestC = -1;
    for (int i = 0; i < (int)ring.points.size(); i++) {
        float vx = (float)((double)ring.points[i].x / (double)FP_ONE - cx);
        float vy = (float)((double)ring.points[i].y / (double)FP_ONE - cy);
        float vz = (float)((double)ring.points[i].z / (double)FP_ONE - cz);
        const float cxp = uy * vz - uz * vy;
        const float cyp = uz * vx - ux * vz;
        const float czp = ux * vy - uy * vx;
        const float cl = std::sqrt(cxp * cxp + cyp * cyp + czp * czp);
        if (cl > bestC) {
            bestC = cl;
            iOff = i;
        }
    }
    float vx = (float)((double)ring.points[iOff].x / (double)FP_ONE - cx);
    float vy = (float)((double)ring.points[iOff].y / (double)FP_ONE - cy);
    float vz = (float)((double)ring.points[iOff].z / (double)FP_ONE - cz);
    float fnx = uy * vz - uz * vy;
    float fny = uz * vx - ux * vz;
    float fnz = ux * vy - uy * vx;
    Normalize3(fnx, fny, fnz);
    if (fnx * nx + fny * ny + fnz * nz < 0) {
        fnx = -fnx;
        fny = -fny;
        fnz = -fnz;
    }
    const float fitR = residual(fnx, fny, fnz);
    if (fitR + 0.02f < headerR) {
        nx = fnx;
        ny = fny;
        nz = fnz;
    }
}

float RingPlanarity(const ContourRing& ring, float nx, float ny, float nz)
{
    if (ring.points.empty()) return 0.f;
    double cx = 0, cy = 0, cz = 0;
    for (const ContourPoint3D& p : ring.points) {
        cx += (double)p.x / (double)FP_ONE;
        cy += (double)p.y / (double)FP_ONE;
        cz += (double)p.z / (double)FP_ONE;
    }
    const double inv = 1.0 / (double)ring.points.size();
    cx *= inv;
    cy *= inv;
    cz *= inv;
    float maxAbs = 0, maxExt = 0;
    for (const ContourPoint3D& p : ring.points) {
        const float x = (float)p.x / (float)FP_ONE - (float)cx;
        const float y = (float)p.y / (float)FP_ONE - (float)cy;
        const float z = (float)p.z / (float)FP_ONE - (float)cz;
        const float ext = std::sqrt(x * x + y * y + z * z);
        if (ext > maxExt) maxExt = ext;
        const float d = std::fabs(x * nx + y * ny + z * nz);
        if (d > maxAbs) maxAbs = d;
    }
    return maxAbs / std::max(maxExt, 1e-6f);
}

int EmitLiftedPolygon(const ContourRing& fill, const ContourRing* holes,
                      int nHoles, float nx, float ny, float nz, WorldFace* out,
                      int maxOut, int& written, const TransformedPoly& head,
                      int sourceIndex)
{
    if (fill.points.size() < 3 || written >= maxOut) return 0;

    float pnx = nx, pny = ny, pnz = nz;
    FitRingNormal(fill, pnx, pny, pnz);
    // Twisted leftover soup rings become the Blender ribbons — skip them.
    if (RingPlanarity(fill, pnx, pny, pnz) > 0.18f) return 0;

    PlaneBasis basis;
    if (!BuildPlaneBasis(fill, pnx, pny, pnz, basis)) return 0;

    std::vector<std::vector<std::array<double, 2>>> polygon;
    std::vector<ContourPoint3D> pts;

    auto appendRing = [&](const ContourRing& ring, bool outer) -> bool {
        std::vector<std::array<double, 2>> uv;
        std::vector<ContourPoint3D> local;
        uv.reserve(ring.points.size());
        local.reserve(ring.points.size());
        for (const ContourPoint3D& p : ring.points) {
            double u = 0, v = 0;
            WorldToPlane(basis, p.x, p.y, p.z, u, v);
            if (!uv.empty()) {
                const double du = uv.back()[0] - u;
                const double dv = uv.back()[1] - v;
                if (du * du + dv * dv < 1e-16) continue;
            }
            uv.push_back({u, v});
            local.push_back(p);
        }
        if (uv.size() >= 3) {
            const double du = uv.front()[0] - uv.back()[0];
            const double dv = uv.front()[1] - uv.back()[1];
            if (du * du + dv * dv < 1e-16) {
                uv.pop_back();
                local.pop_back();
            }
        }
        if (uv.size() < 3) return false;
        double area2 = 0;
        double uMin = uv[0][0], uMax = uv[0][0], vMin = uv[0][1], vMax = uv[0][1];
        for (size_t i = 0, n = uv.size(); i < n; i++) {
            const auto& a = uv[i];
            const auto& b = uv[(i + 1) % n];
            area2 += a[0] * b[1] - b[0] * a[1];
            if (a[0] < uMin) uMin = a[0];
            if (a[0] > uMax) uMax = a[0];
            if (a[1] < vMin) vMin = a[1];
            if (a[1] > vMax) vMax = a[1];
        }
        // Stroke / banana: skip fills whose 2D area is a sliver of the bbox.
        if (outer) {
            const double span = std::max(uMax - uMin, vMax - vMin);
            if (span > 1e-8 && std::fabs(area2) * 0.5 < 0.02 * span * span)
                return false;
        }
        // earcut wants CCW outer, CW holes.
        const bool wantCcw = outer;
        if ((area2 < 0) == wantCcw) {
            std::reverse(uv.begin(), uv.end());
            std::reverse(local.begin(), local.end());
        }
        polygon.push_back(std::move(uv));
        pts.insert(pts.end(), local.begin(), local.end());
        return true;
    };

    if (!appendRing(fill, true)) return 0;
    for (int h = 0; h < nHoles; h++)
        appendRing(holes[h], false);
    if (pts.size() < 3) return 0;

    const std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);
    int added = 0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (written >= maxOut) return added;
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        if (i0 >= pts.size() || i1 >= pts.size() || i2 >= pts.size()) continue;
        if (i0 == i1 || i1 == i2 || i2 == i0) continue;

        float tnx = pnx, tny = pny, tnz = pnz;
        AnalyticalFaceNormal(pts[i0].x, pts[i0].y, pts[i0].z, pts[i1].x,
                             pts[i1].y, pts[i1].z, pts[i2].x, pts[i2].y,
                             pts[i2].z, tnx, tny, tnz);

        WorldFace& f = out[written++];
        f = WorldFace{};
        f.kind = WorldFaceKind::ContourHull;
        f.type = MeshPolyType::ComplexFill;
        f.texInfo = head.texInfo;
        f.normalIdx = head.normalIdx;
        f.vertexCount = 3;
        f.worldValid = true;
        f.authoredNormal = true;
        f.contourId = head.contourId;
        f.meshIndex = head.meshIndex;
        f.sourceIndex = sourceIndex;
        f.sortKey = head.sortKey;
        f.faceLight[0] = head.faceLight[0];
        f.faceLight[1] = head.faceLight[1];
        f.faceLight[2] = head.faceLight[2];
        const uint32_t idx[3] = {i0, i1, i2};
        for (int k = 0; k < 3; k++) {
            const ContourPoint3D& p = pts[idx[k]];
            f.verts[k].x = (float)p.x / (float)FP_ONE;
            f.verts[k].y = (float)p.y / (float)FP_ONE;
            f.verts[k].z = (float)p.z / (float)FP_ONE;
            f.verts[k].nx = tnx;
            f.verts[k].ny = tny;
            f.verts[k].nz = tnz;
        }
        added++;
    }
    return added;
}

int TessellateRings(const std::vector<ContourRing>& rings, float nx, float ny,
                    float nz, WorldFace* out, int maxOut, int& written,
                    const TransformedPoly& head, int sourceIndex)
{
    std::vector<ContourRing> fills, punches;
    for (const ContourRing& r : rings) {
        if (r.points.size() < 3) continue;
        if (r.isHole)
            punches.push_back(r);
        else
            fills.push_back(r);
    }
    if (fills.empty() || written >= maxOut) return 0;

    // Lifted earcut: triangulate in a (PCA) plane for connectivity, but emit
    // the original 3D bezier samples. PlaneToWorld on a curved Puma wing was
    // flattening the silhouette into the header plane — those are the Blender
    // sails. Clipper2 union is skipped; each fill is its own patch.
    int added = 0;
    for (const ContourRing& fill : fills) {
        added += EmitLiftedPolygon(fill, punches.empty() ? nullptr : punches.data(),
                                   (int)punches.size(), nx, ny, nz, out, maxOut,
                                   written, head, sourceIndex);
    }
    return added;
}

// Soft/Ogre Contour fill: even-odd of open bezier+edge soup in the face plane.
// Puma butterfly (4 quads + bridge edge) skips inventing petal close chords.
int TessellateEvenOddEdges(const TransformedPoly* const* pieces, int nPieces,
                           float nx, float ny, float nz, WorldFace* out,
                           int maxOut, int& written, const TransformedPoly& head,
                           int sourceIndex)
{
    if (nPieces <= 0 || written >= maxOut) return 0;

    auto toPt = [](const ProjectedVertex& v) -> ContourPoint3D {
        return {v.worldX, v.worldY, v.worldZ};
    };

    struct Seg {
        ContourPoint3D a, b;
    };
    std::vector<Seg> segs;
    ContourRing scratch;
    for (int i = 0; i < nPieces; i++) {
        const TransformedPoly& p = *pieces[i];
        if (p.vertexCount == 4) {
            scratch.points.clear();
            ContourTessellator::EvaluateBezier3D(toPt(p.verts[0]), toPt(p.verts[1]),
                                                 toPt(p.verts[2]), toPt(p.verts[3]),
                                                 scratch);
            for (size_t k = 1; k < scratch.points.size(); k++)
                segs.push_back({scratch.points[k - 1], scratch.points[k]});
        } else if (p.vertexCount == 2) {
            segs.push_back({toPt(p.verts[0]), toPt(p.verts[1])});
        } else if (p.vertexCount == 3) {
            segs.push_back({toPt(p.verts[0]), toPt(p.verts[1])});
            segs.push_back({toPt(p.verts[1]), toPt(p.verts[2])});
            segs.push_back({toPt(p.verts[2]), toPt(p.verts[0])});
        }
    }
    if (segs.empty()) return 0;

    // Soft wide multi-quad Contours skip 001796 close; digon Contours close.
    int nQuads = 0;
    for (int i = 0; i < nPieces; i++)
        if (pieces[i]->vertexCount == 4) nQuads++;
    ContourPoint3D first = segs.front().a;
    ContourPoint3D last = segs.back().b;
    // Approximate Contour AABB in world for wide test (use plane UV later).
    PlaneBasis basis;
    ContourRing seed;
    seed.points.push_back(first);
    seed.points.push_back(segs.front().b);
    seed.points.push_back(last);
    if (!BuildPlaneBasis(seed, nx, ny, nz, basis)) return 0;

    double uMin = 1e300, uMax = -1e300, vMin = 1e300, vMax = -1e300;
    auto growUV = [&](const ContourPoint3D& p) {
        double u, v;
        WorldToPlane(basis, p.x, p.y, p.z, u, v);
        if (u < uMin) uMin = u;
        if (u > uMax) uMax = u;
        if (v < vMin) vMin = v;
        if (v > vMax) vMax = v;
    };
    for (const Seg& s : segs) {
        growUV(s.a);
        growUV(s.b);
    }
    const double spanU = uMax - uMin;
    const double spanV = vMax - vMin;
    const bool wideContour = spanU * 5.0 >= spanV * 9.0;
    const bool digonWideClose = (nQuads == 2) && wideContour;
    if (!wideContour || digonWideClose) {
        const double dx = (double)(last.x - first.x);
        const double dy = (double)(last.y - first.y);
        const double dz = (double)(last.z - first.z);
        if (dx * dx + dy * dy + dz * dz > 1.0)
            segs.push_back({last, first});
    }

    // Soft Contours (C721SpanFlush) fill even-odd spans per screen scanline.
    // Plane-UV AA row tiles are the ContourHull analogue: one strip per
    // integer V, abutting ±0.5 (no y+1.5 overlap). True-edge trap/fan ports
    // punched holes and spikes on Puma/Eagle — dig only via JJFFE_CONTOUR_TE=1.
    constexpr double kScale = 128.0;
    const int y0 = (int)std::floor(vMin * kScale);
    const int y1 = (int)std::ceil(vMax * kScale);
    if (y1 < y0) return 0;
    std::vector<std::vector<float>> hits((size_t)(y1 - y0 + 1));

    for (const Seg& s : segs) {
        double uA, vA, uB, vB;
        WorldToPlane(basis, s.a.x, s.a.y, s.a.z, uA, vA);
        WorldToPlane(basis, s.b.x, s.b.y, s.b.z, uB, vB);
        float xA = (float)(uA * kScale), yA = (float)(vA * kScale);
        float xB = (float)(uB * kScale), yB = (float)(vB * kScale);
        if (yA == yB) {
            // Horizontal: two hits so even-odd covers the segment (soft).
            const int y = (int)std::floor(yA);
            if (y >= y0 && y <= y1) {
                hits[(size_t)(y - y0)].push_back(xA);
                hits[(size_t)(y - y0)].push_back(xB);
            }
            continue;
        }
        if (yA > yB) {
            std::swap(xA, xB);
            std::swap(yA, yB);
        }
        const int ya = (int)std::ceil(yA);
        const int yb = (int)std::floor(yB);
        for (int y = ya; y <= yb; y++) {
            if (y < y0 || y > y1) continue;
            const float t = ((float)y - yA) / (yB - yA);
            hits[(size_t)(y - y0)].push_back(xA + t * (xB - xA));
        }
    }

    int added = 0;

    auto emitWorldTri = [&](float ax, float ay, float az, float bx, float by,
                            float bz, float cx, float cy, float cz) {
        if (written >= maxOut) return;
        WorldFace& f = out[written++];
        f = WorldFace{};
        f.kind = WorldFaceKind::ContourHull;
        f.type = MeshPolyType::ComplexFill;
        f.texInfo = head.texInfo;
        f.normalIdx = head.normalIdx;
        f.vertexCount = 3;
        f.worldValid = true;
        f.authoredNormal = true;
        f.contourId = head.contourId;
        f.meshIndex = head.meshIndex;
        f.sourceIndex = sourceIndex;
        f.sortKey = head.sortKey;
        f.faceLight[0] = head.faceLight[0];
        f.faceLight[1] = head.faceLight[1];
        f.faceLight[2] = head.faceLight[2];
        f.verts[0] = {ax, ay, az, basis.nx, basis.ny, basis.nz};
        f.verts[1] = {bx, by, bz, basis.nx, basis.ny, basis.nz};
        f.verts[2] = {cx, cy, cz, basis.nx, basis.ny, basis.nz};
        added++;
    };

    auto emitSpanAA = [&](float xL, float xR, float yTop, float yBot) {
        if (written + 2 > maxOut) return;
        double u0 = xL / kScale, u1 = xR / kScale;
        double v0 = yTop / kScale, v1 = yBot / kScale;
        float p00x, p00y, p00z, p10x, p10y, p10z, p01x, p01y, p01z, p11x, p11y,
            p11z;
        PlaneToWorld(basis, u0, v0, p00x, p00y, p00z);
        PlaneToWorld(basis, u1, v0, p10x, p10y, p10z);
        PlaneToWorld(basis, u0, v1, p01x, p01y, p01z);
        PlaneToWorld(basis, u1, v1, p11x, p11y, p11z);
        emitWorldTri(p00x, p00y, p00z, p10x, p10y, p10z, p01x, p01y, p01z);
        emitWorldTri(p10x, p10y, p10z, p11x, p11y, p11z, p01x, p01y, p01z);
    };

    auto emitAARows = [&]() {
        for (int y = y0; y <= y1; y++) {
            auto& row = hits[(size_t)(y - y0)];
            if (row.size() < 2) continue;
            std::sort(row.begin(), row.end());
            const float fy0 = (float)y - 0.5f;
            const float fy1 = (float)y + 0.5f;
            for (size_t i = 0; i + 1 < row.size(); i += 2) {
                if (row[i + 1] - row[i] < 0.25f) continue;
                emitSpanAA(row[i], row[i + 1], fy0, fy1);
            }
        }
    };

    // Default = AA (soft-analogue). Dig: JJFFE_CONTOUR_TE=1 for trap strips.
    const bool useTE = getenv("JJFFE_CONTOUR_TE") &&
                       getenv("JJFFE_CONTOUR_TE")[0] == '1';
    if (!useTE) {
        emitAARows();
        return added;
    }

    // True-edge dig: adjacent-scanline trapezoids (not silhouette fan).
    struct RowSpans {
        int iy = 0;
        float y = 0.f;
        std::vector<std::pair<float, float>> spans;
    };
    std::vector<RowSpans> rows;
    rows.reserve((size_t)(y1 - y0 + 1));
    for (int y = y0; y <= y1; y++) {
        auto& row = hits[(size_t)(y - y0)];
        if (row.size() < 2) continue;
        std::sort(row.begin(), row.end());
        RowSpans rs;
        rs.iy = y;
        rs.y = (float)y / (float)kScale;
        for (size_t i = 0; i + 1 < row.size(); i += 2) {
            if (row[i + 1] - row[i] < 0.25f) continue;
            rs.spans.push_back(
                {row[i] / (float)kScale, row[i + 1] / (float)kScale});
        }
        if (!rs.spans.empty())
            rows.push_back(std::move(rs));
    }

    auto emitTrapUV = [&](float u0, float v0, float u1, float v1, float u2,
                          float v2, float u3, float v3) {
        float p0x, p0y, p0z, p1x, p1y, p1z, p2x, p2y, p2z, p3x, p3y, p3z;
        PlaneToWorld(basis, u0, v0, p0x, p0y, p0z);
        PlaneToWorld(basis, u1, v1, p1x, p1y, p1z);
        PlaneToWorld(basis, u2, v2, p2x, p2y, p2z);
        PlaneToWorld(basis, u3, v3, p3x, p3y, p3z);
        emitWorldTri(p0x, p0y, p0z, p1x, p1y, p1z, p3x, p3y, p3z);
        emitWorldTri(p1x, p1y, p1z, p2x, p2y, p2z, p3x, p3y, p3z);
    };

    auto emitIsolatedRow = [&](const RowSpans& rs) {
        const float fy0 = (float)rs.iy - 0.5f;
        const float fy1 = (float)rs.iy + 0.5f;
        for (const auto& sp : rs.spans)
            emitSpanAA(sp.first * (float)kScale, sp.second * (float)kScale, fy0,
                       fy1);
    };

    auto pairSpans = [&](const RowSpans& a, const RowSpans& b,
                         std::vector<int>& mapB) {
        const size_t n = a.spans.size();
        mapB.assign(n, -1);
        std::vector<char> used(b.spans.size(), 0);
        for (size_t i = 0; i < n; i++) {
            const float midA = 0.5f * (a.spans[i].first + a.spans[i].second);
            int best = -1;
            float bestD = 1e30f;
            for (size_t j = 0; j < b.spans.size(); j++) {
                if (used[j]) continue;
                const float midB =
                    0.5f * (b.spans[j].first + b.spans[j].second);
                const float d = std::fabs(midA - midB);
                if (d < bestD) {
                    bestD = d;
                    best = (int)j;
                }
            }
            if (best < 0) continue;
            used[(size_t)best] = 1;
            mapB[i] = best;
        }
    };

    for (size_t ri = 0; ri < rows.size(); ri++) {
        if (ri + 1 >= rows.size() || rows[ri + 1].iy != rows[ri].iy + 1 ||
            rows[ri + 1].spans.size() != rows[ri].spans.size()) {
            const bool hasAbove =
                ri > 0 && rows[ri].iy == rows[ri - 1].iy + 1 &&
                rows[ri].spans.size() == rows[ri - 1].spans.size();
            if (!hasAbove)
                emitIsolatedRow(rows[ri]);
            continue;
        }

        const RowSpans& top = rows[ri];
        const RowSpans& bot = rows[ri + 1];
        std::vector<int> mapB;
        pairSpans(top, bot, mapB);
        for (size_t si = 0; si < top.spans.size(); si++) {
            const int bj = mapB[si];
            if (bj < 0) continue;
            const auto& t = top.spans[si];
            const auto& b = bot.spans[(size_t)bj];
            if (!(t.first < t.second && b.first < b.second)) continue;
            if (t.first > b.second || b.first > t.second)
                continue;
            emitTrapUV(t.first, top.y, t.second, top.y, b.second, bot.y,
                       b.first, bot.y);
        }
    }
    return added;
}

} // namespace

bool Contour3dEnabled()
{
    const char* e = std::getenv("JJFFE_CONTOUR_3D");
    return !(e && e[0] == '0' && e[1] == '\0');
}

namespace {
bool g_contourRowTiles = true;
}

void SetContourRowTilesEnabled(bool enabled) { g_contourRowTiles = enabled; }
bool ContourRowTilesEnabled() { return g_contourRowTiles; }

int BuildContourWorldFaces(const TransformedPoly* polys, int polyCount,
                           const ShipModel* model, const int32_t* shipRotFp16,
                           WorldFace* outFaces, int maxFaces)
{
    if (!Contour3dEnabled() || !polys || !outFaces || maxFaces <= 0)
        return 0;

    int written = 0;
    int groupsSeen = 0;
    std::vector<char> done((size_t)polyCount, 0);

    for (int i = 0; i < polyCount; i++) {
        if (done[i]) continue;
        const TransformedPoly& p = polys[i];
        if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
        if (IsContourGlintPoly(p)) continue;
        // Match soft ship_renderer Contour skips (AB_C721_KEEP_VIPER=1 dig).
        // Viper MkII #34: Contours 0x1000 / 0x4028 paint solid fills that bury
        // authentic 0x2000 windshield punches on ContourHull.
        if ((p.texInfo == 0x1000 || p.texInfo == 0x4028) &&
            !(getenv("AB_C721_KEEP_VIPER") &&
              getenv("AB_C721_KEEP_VIPER")[0] == '1')) {
            for (int j = 0; j < polyCount; j++) {
                if (polys[j].type == MeshPolyType::ComplexFill &&
                    polys[j].contourId == p.contourId)
                    done[j] = 1;
            }
            continue;
        }

        const TransformedPoly* group[64];
        int ng = 0;
        for (int j = 0; j < polyCount && ng < 64; j++) {
            if (polys[j].type != MeshPolyType::ComplexFill) continue;
            if (polys[j].contourId != p.contourId) continue;
            if (IsContourGlintPoly(polys[j])) continue;
            group[ng++] = &polys[j];
            done[j] = 1;
        }
        if (ng == 0) continue;
        groupsSeen++;

        for (int a = 0; a < ng; a++) {
            for (int b = a + 1; b < ng; b++) {
                if (group[b]->meshIndex < group[a]->meshIndex) {
                    const TransformedPoly* tmp = group[a];
                    group[a] = group[b];
                    group[b] = tmp;
                }
            }
        }

        float nx = 0, ny = 0, nz = 1;
        const bool authored =
            TransformAuthoredNormal(model, group[0]->normalIdx, shipRotFp16, nx,
                                    ny, nz);
        if (!authored && group[0]->vertexCount >= 3) {
            AnalyticalFaceNormal(
                group[0]->verts[0].worldX, group[0]->verts[0].worldY,
                group[0]->verts[0].worldZ, group[0]->verts[1].worldX,
                group[0]->verts[1].worldY, group[0]->verts[1].worldZ,
                group[0]->verts[2].worldX, group[0]->verts[2].worldY,
                group[0]->verts[2].worldZ, nx, ny, nz);
        }

        bool anyQuad = false, anyEdge = false;
        for (int g = 0; g < ng; g++) {
            if (group[g]->vertexCount == 4) anyQuad = true;
            if (group[g]->vertexCount == 2) anyEdge = true;
        }
        if (!anyQuad && !anyEdge) {
            for (int g = 0; g < ng; g++) {
                if (group[g]->vertexCount == 3)
                    EmitFanTris(*group[g], nx, ny, nz, outFaces, maxFaces,
                                written, group[0]->contourId,
                                group[g]->meshIndex,
                                (int)(group[g] - polys), group[0]->texInfo,
                                group[0]->normalIdx);
            }
            continue;
        }

        const int beforeStrips = written;
        EmitPetalStrips(group, ng, nx, ny, nz, outFaces, maxFaces, written,
                        *group[0], (int)(group[0] - polys));
        if (written > beforeStrips) continue;

        std::vector<ContourRing> rings =
            ContourTessellator::ExtractRings(group, ng);
        if (!rings.empty()) {
            TessellateRings(rings, nx, ny, nz, outFaces, maxFaces, written,
                            *group[0], (int)(group[0] - polys));
        } else if (ContourRowTilesEnabled()) {
            // Soft Contour edge soup (Puma 4×bezier + bridge, etc.).
            TessellateEvenOddEdges(group, ng, nx, ny, nz, outFaces, maxFaces,
                                   written, *group[0],
                                   (int)(group[0] - polys));
        }
    }
    if (getenv("JJFFE_CONTOUR_3D_DEBUG") &&
        getenv("JJFFE_CONTOUR_3D_DEBUG")[0] == '1') {
        fprintf(stderr, "Contour3D: groups=%d wrote %d ContourHull tris\n",
                groupsSeen, written);
    }
    return written;
}

} // namespace jjffe
