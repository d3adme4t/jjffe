#include "jjffe/face_inside.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace jjffe {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-8f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

Vec3 V(float x, float y, float z) { return {x, y, z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Len(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Norm(Vec3 a)
{
    const float L = Len(a);
    if (L < kEps) return {0, 0, 1};
    return a * (1.f / L);
}

float Clampf(float x, float lo, float hi)
{
    return std::max(lo, std::min(hi, x));
}

// Exact solid angle of triangle (a,b,c) as seen from p (van Oosterom/Strackee).
float TriSolidAngle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 A = Norm(a - p);
    Vec3 B = Norm(b - p);
    Vec3 C = Norm(c - p);
    const float triple = Dot(A, Cross(B, C));
    const float denom = 1.f + Dot(A, B) + Dot(B, C) + Dot(C, A);
    // atan2 keeps the sign of the solid angle (orientation).
    return 2.f * std::atan2(triple, denom);
}

struct TriRef {
    int idx = 0;
    Vec3 a, b, c;
    Vec3 n;
    Vec3 centroid;
    float area = 0;
    FaceAabb box;
};

void ExpandBox(FaceAabb* box, Vec3 p)
{
    box->min[0] = std::min(box->min[0], p.x);
    box->min[1] = std::min(box->min[1], p.y);
    box->min[2] = std::min(box->min[2], p.z);
    box->max[0] = std::max(box->max[0], p.x);
    box->max[1] = std::max(box->max[1], p.y);
    box->max[2] = std::max(box->max[2], p.z);
}

FaceAabb EmptyBox()
{
    FaceAabb b;
    for (int i = 0; i < 3; i++) {
        b.min[i] = std::numeric_limits<float>::infinity();
        b.max[i] = -std::numeric_limits<float>::infinity();
    }
    return b;
}

float BoxExtent(const FaceAabb& b)
{
    return std::max(b.max[0] - b.min[0],
                    std::max(b.max[1] - b.min[1], b.max[2] - b.min[2]));
}

Vec3 BoxCenter(const FaceAabb& b)
{
    return {(b.min[0] + b.max[0]) * 0.5f, (b.min[1] + b.max[1]) * 0.5f,
            (b.min[2] + b.max[2]) * 0.5f};
}

struct BvhNode {
    FaceAabb box;
    int left = -1, right = -1;
    int triBegin = 0, triCount = 0;  // leaf: range into ordered[]
    Vec3 dipoleC;                   // area-weighted centroid
    Vec3 dipoleN;                   // area-weighted normal (for approx)
    float area = 0;
};

struct Bvh {
    std::vector<TriRef> tris;
    std::vector<int> ordered;
    std::vector<BvhNode> nodes;
};

int BuildNode(Bvh* bvh, int begin, int end)
{
    BvhNode node;
    node.box = EmptyBox();
    node.triBegin = begin;
    node.triCount = end - begin;
    Vec3 cSum{};
    Vec3 nSum{};
    float aSum = 0;
    for (int i = begin; i < end; i++) {
        const TriRef& t = bvh->tris[bvh->ordered[(size_t)i]];
        ExpandBox(&node.box, t.a);
        ExpandBox(&node.box, t.b);
        ExpandBox(&node.box, t.c);
        cSum = cSum + t.centroid * t.area;
        nSum = nSum + t.n * t.area;
        aSum += t.area;
    }
    node.area = aSum;
    if (aSum > kEps) {
        node.dipoleC = cSum * (1.f / aSum);
        node.dipoleN = Norm(nSum);
    }
    const int id = (int)bvh->nodes.size();
    bvh->nodes.push_back(node);

    if (end - begin <= 4) return id;  // leaf

    // Split longest axis at median centroid.
    int axis = 0;
    const float ex = node.box.max[0] - node.box.min[0];
    const float ey = node.box.max[1] - node.box.min[1];
    const float ez = node.box.max[2] - node.box.min[2];
    if (ey >= ex && ey >= ez) axis = 1;
    else if (ez >= ex && ez >= ey) axis = 2;

    std::sort(bvh->ordered.begin() + begin, bvh->ordered.begin() + end,
              [&](int ia, int ib) {
                  const Vec3& ca = bvh->tris[(size_t)ia].centroid;
                  const Vec3& cb = bvh->tris[(size_t)ib].centroid;
                  const float* pa = &ca.x;
                  const float* pb = &cb.x;
                  return pa[axis] < pb[axis];
              });
    const int mid = (begin + end) / 2;
    const int L = BuildNode(bvh, begin, mid);
    const int R = BuildNode(bvh, mid, end);
    bvh->nodes[(size_t)id].left = L;
    bvh->nodes[(size_t)id].right = R;
    bvh->nodes[(size_t)id].triCount = 0;  // mark internal
    return id;
}

bool BuildBvh(const float* verts, int nTris, const float* normals, Bvh* out)
{
    out->tris.clear();
    out->ordered.clear();
    out->nodes.clear();
    out->tris.reserve((size_t)nTris);
    for (int i = 0; i < nTris; i++) {
        TriRef t;
        t.idx = i;
        t.a = V(verts[i * 9 + 0], verts[i * 9 + 1], verts[i * 9 + 2]);
        t.b = V(verts[i * 9 + 3], verts[i * 9 + 4], verts[i * 9 + 5]);
        t.c = V(verts[i * 9 + 6], verts[i * 9 + 7], verts[i * 9 + 8]);
        t.n = V(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        t.centroid = (t.a + t.b + t.c) * (1.f / 3.f);
        const Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        t.area = 0.5f * Len(Cross(e1, e2));
        t.box = EmptyBox();
        ExpandBox(&t.box, t.a);
        ExpandBox(&t.box, t.b);
        ExpandBox(&t.box, t.c);
        out->tris.push_back(t);
        out->ordered.push_back(i);
    }
    if (out->tris.empty()) return false;
    BuildNode(out, 0, (int)out->ordered.size());
    return true;
}

float WindingApproxCluster(Vec3 p, const BvhNode& n)
{
    // Monopole: solid angle ≈ area * cosθ / r^2  (signed by normal).
    Vec3 d = n.dipoleC - p;
    const float r2 = Dot(d, d);
    if (r2 < kEps) return 0.f;
    const float r = std::sqrt(r2);
    const float c = Dot(n.dipoleN, d) / r;  // cos of angle between n and (c-p)
    return n.area * c / r2;
}

float WindingTraverse(Vec3 p, const Bvh& bvh, int nodeId)
{
    const BvhNode& n = bvh.nodes[(size_t)nodeId];
    if (n.triCount > 0) {
        // Leaf: exact.
        float w = 0.f;
        for (int i = 0; i < n.triCount; i++) {
            const TriRef& t = bvh.tris[(size_t)bvh.ordered[(size_t)(n.triBegin + i)]];
            w += TriSolidAngle(p, t.a, t.b, t.c);
        }
        return w;
    }
    // Barnes-Hut: if angular size small, approximate.
    const Vec3 c = BoxCenter(n.box);
    const float dist = Len(c - p);
    const float extent = BoxExtent(n.box);
    if (dist > kEps && extent / dist < kInsideBarnesHutTheta) {
        return WindingApproxCluster(p, n);
    }
    float w = 0.f;
    if (n.left >= 0) w += WindingTraverse(p, bvh, n.left);
    if (n.right >= 0) w += WindingTraverse(p, bvh, n.right);
    return w;
}

// Closest triangle: return signed distance (n·(p-a)) and whether closest.
float ClosestNormalSign(Vec3 p, const Bvh& bvh)
{
    float bestD2 = std::numeric_limits<float>::infinity();
    float bestSign = 0.f;
    for (const TriRef& t : bvh.tris) {
        // Point–triangle distance (crude: clamp to verts + centroid + edges).
        auto consider = [&](Vec3 q, Vec3 nrm) {
            const Vec3 d = p - q;
            const float d2 = Dot(d, d);
            if (d2 < bestD2) {
                bestD2 = d2;
                bestSign = Dot(nrm, d) >= 0.f ? 1.f : -1.f;
            }
        };
        consider(t.a, t.n);
        consider(t.b, t.n);
        consider(t.c, t.n);
        consider(t.centroid, t.n);
        // Closest on plane, then check barycentric roughly via edges.
        const float planeD = Dot(t.n, p - t.a);
        Vec3 proj = p - t.n * planeD;
        // If proj is outside triangle, fall back already covered by verts.
        const Vec3 v0 = t.b - t.a, v1 = t.c - t.a, v2 = proj - t.a;
        const float d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1);
        const float d20 = Dot(v2, v0), d21 = Dot(v2, v1);
        const float inv = 1.f / std::max(kEps, d00 * d11 - d01 * d01);
        const float u = (d11 * d20 - d01 * d21) * inv;
        const float v = (d00 * d21 - d01 * d20) * inv;
        if (u >= 0.f && v >= 0.f && u + v <= 1.f) {
            consider(proj, t.n);
        }
    }
    return bestSign;
}

bool RayHitTri(Vec3 o, Vec3 dir, Vec3 a, Vec3 b, Vec3 c, float* tOut)
{
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 pvec = Cross(dir, e2);
    const float det = Dot(e1, pvec);
    if (std::fabs(det) < kEps) return false;
    const float inv = 1.f / det;
    const Vec3 tvec = o - a;
    const float u = Dot(tvec, pvec) * inv;
    if (u < 0.f || u > 1.f) return false;
    const Vec3 qvec = Cross(tvec, e1);
    const float v = Dot(dir, qvec) * inv;
    if (v < 0.f || u + v > 1.f) return false;
    const float t = Dot(e2, qvec) * inv;
    if (t <= kEps) return false;
    *tOut = t;
    return true;
}

float RayParityInside(Vec3 p, const Bvh& bvh)
{
    // Several directions with small jitter; majority vote.
    static const Vec3 dirs[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        {0.577f, 0.577f, 0.577f}, {-0.577f, 0.577f, -0.577f},
        {0.577f, -0.577f, 0.577f}, {-0.577f, -0.577f, -0.577f},
    };
    int insideVotes = 0;
    const int nDirs = (int)(sizeof(dirs) / sizeof(dirs[0]));
    for (int d = 0; d < nDirs; d++) {
        Vec3 dir = Norm(dirs[d]);
        // Jitter origin slightly to avoid coplanar/edge hits.
        const float j = 1e-4f * (float)((d % 3) + 1);
        Vec3 o = p + V(j * ((d & 1) ? 1.f : -1.f), j * ((d & 2) ? 1.f : -1.f),
                       j * ((d & 4) ? 1.f : -1.f));
        int hits = 0;
        for (const TriRef& t : bvh.tris) {
            float tt = 0;
            if (RayHitTri(o, dir, t.a, t.b, t.c, &tt)) hits++;
        }
        if (hits % 2 == 1) insideVotes++;
    }
    return (float)insideVotes / (float)nDirs;
}

}  // namespace

HygieneStats BuildHygieneHull(const std::vector<FaceRecord>& faces,
                              std::vector<float>* outVerts,
                              std::vector<float>* outNormals)
{
    HygieneStats st;
    if (!outVerts || !outNormals) return st;
    outVerts->clear();
    outNormals->clear();

    struct Key {
        int x, y, z;
        bool operator==(const Key& o) const
        {
            return x == o.x && y == o.y && z == o.z;
        }
    };
    auto quant = [](float v) -> int {
        return (int)std::lround(v * 1024.f);
    };
    auto triKey = [&](const FaceRecord& f) -> std::array<Key, 3> {
        std::array<Key, 3> k;
        for (int i = 0; i < 3; i++)
            k[(size_t)i] = {quant(f.v[i][0]), quant(f.v[i][1]), quant(f.v[i][2])};
        std::sort(k.begin(), k.end(), [](const Key& a, const Key& b) {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });
        return k;
    };

    std::vector<std::array<Key, 3>> seen;
    int agree = 0, orientVotes = 0;

    // First pass: majority outward vs centroid of all hull tris.
    Vec3 centroid{};
    int nHull = 0;
    for (const FaceRecord& f : faces) {
        if (!(f.flags & kFaceFlagHull)) continue;
        if (f.flags & kFaceFlagReverse) continue;
        st.inputTris++;
        if (f.area < 1e-12f) {
            st.droppedZeroArea++;
            continue;
        }
        centroid = centroid + V(f.v[0][0] + f.v[1][0] + f.v[2][0],
                                f.v[0][1] + f.v[1][1] + f.v[2][1],
                                f.v[0][2] + f.v[1][2] + f.v[2][2]) *
                                  (1.f / 3.f);
        nHull++;
    }
    if (nHull > 0) centroid = centroid * (1.f / (float)nHull);

    for (const FaceRecord& f : faces) {
        if (!(f.flags & kFaceFlagHull)) continue;
        if (f.flags & kFaceFlagReverse) continue;
        if (f.area < 1e-12f) continue;
        const auto key = triKey(f);
        bool dupe = false;
        for (const auto& s : seen) {
            if (s == key) {
                dupe = true;
                break;
            }
        }
        if (dupe) {
            st.droppedDupe++;
            continue;
        }
        seen.push_back(key);

        Vec3 n = V(f.plane.n[0], f.plane.n[1], f.plane.n[2]);
        const Vec3 fc = V((f.v[0][0] + f.v[1][0] + f.v[2][0]) / 3.f,
                          (f.v[0][1] + f.v[1][1] + f.v[2][1]) / 3.f,
                          (f.v[0][2] + f.v[1][2] + f.v[2][2]) / 3.f);
        const float outward = Dot(n, fc - centroid);
        orientVotes++;
        if (outward >= 0.f) agree++;

        for (int i = 0; i < 3; i++) {
            outVerts->push_back(f.v[i][0]);
            outVerts->push_back(f.v[i][1]);
            outVerts->push_back(f.v[i][2]);
        }
        outNormals->push_back(n.x);
        outNormals->push_back(n.y);
        outNormals->push_back(n.z);
        st.keptTris++;
    }

    st.orientationCoherence =
        (orientVotes > 0) ? (float)agree / (float)orientVotes : 0.f;
    // Coherence is max(agree, 1-agree) — either consistent orientation is fine.
    st.orientationCoherence =
        std::max(st.orientationCoherence, 1.f - st.orientationCoherence);
    st.usable = st.keptTris >= 4 && st.orientationCoherence >= 0.45f;
    return st;
}

InsideSample QueryAgainstBvh(const float p[3], const Bvh& bvh,
                             float orientCoherence)
{
    InsideSample s;
    if (bvh.tris.empty()) {
        s.invalidated = true;
        return s;
    }
    if (orientCoherence < kInsideOrientCoherenceMin) {
        s.invalidated = true;
        return s;
    }

    const Vec3 P = V(p[0], p[1], p[2]);
    const int nTris = (int)bvh.tris.size();
    float w = 0.f;
    if (nTris <= 64) {
        for (const TriRef& t : bvh.tris)
            w += TriSolidAngle(P, t.a, t.b, t.c);
    } else {
        w = WindingTraverse(P, bvh, 0);
    }
    const float winding = w / (4.f * kPi);
    s.winding = winding;

    s.closestSign = ClosestNormalSign(P, bvh);
    s.rayInside = RayParityInside(P, bvh);

    const float wInside =
        Clampf(std::fabs(winding) / kInsideWindingThresh, 0.f, 1.f);
    const float closestInside = (s.closestSign < 0.f) ? 1.f : 0.f;
    const float ray = s.rayInside;

    s.score = 0.50f * wInside + 0.25f * closestInside + 0.25f * ray;
    const float votes =
        (wInside > 0.5f ? 1.f : 0.f) + (closestInside > 0.5f ? 1.f : 0.f) +
        (ray > 0.5f ? 1.f : 0.f);
    s.confidence = Clampf(0.35f + 0.22f * votes, 0.f, 1.f);
    if (orientCoherence < 0.7f) s.confidence *= 0.7f;
    return s;
}

InsideSample QueryInside(const float p[3], const float* verts, int nTris,
                         const float* normals, float orientCoherence)
{
    InsideSample s;
    if (!verts || !normals || nTris <= 0) {
        s.invalidated = true;
        return s;
    }
    Bvh bvh;
    if (!BuildBvh(verts, nTris, normals, &bvh)) {
        s.invalidated = true;
        return s;
    }
    return QueryAgainstBvh(p, bvh, orientCoherence);
}

bool AnalyzeShipInside(const ShipAnalysis& base, ShipInsideReport* out)
{
    if (!out) return false;
    *out = ShipInsideReport{};
    // Caller may invoke before setting ShipAnalysis::valid (AnalyzeShipFacesMesh).
    if (base.faces.empty()) return true;

    std::vector<float> verts, normals;
    out->hygiene = BuildHygieneHull(base.faces, &verts, &normals);
    if (!out->hygiene.usable) return true;

    Bvh bvh;
    const int nTris = (int)verts.size() / 9;
    if (!BuildBvh(verts.data(), nTris, normals.data(), &bvh)) return true;

    std::vector<uint32_t> skipPrims;
    for (const Finding& f : base.findings) {
        if (f.type == FindingType::CoplanarOverlay) skipPrims.push_back(f.a);
    }
    auto isSkipped = [&](uint32_t p) {
        for (uint32_t s : skipPrims)
            if (s == p) return true;
        return false;
    };

    // Deduplicate by primitive so one finding per exported face group.
    std::vector<uint32_t> seenPrim;
    for (const FaceRecord& fr : base.faces) {
        if (fr.flags & kFaceFlagReverse) continue;
        // Probe overlays / suspects only. Punches are intentionally recessed
        // and would false-positive as EMBEDDED; they already have PUNCH_DEPTH_RISK.
        // Coplanar overlays are skipped below (painter-order surface, not buried).
        const bool probe =
            (fr.flags & (kFaceFlagOverlay | kFaceFlagSuspect)) != 0;
        if (!probe) continue;
        if (isSkipped(fr.primitive)) continue;
        bool already = false;
        for (uint32_t s : seenPrim)
            if (s == fr.primitive) {
                already = true;
                break;
            }
        if (already) continue;
        seenPrim.push_back(fr.primitive);

        float samples[7][3];
        int nSamp = 0;
        auto add = [&](float x, float y, float z) {
            samples[nSamp][0] = x;
            samples[nSamp][1] = y;
            samples[nSamp][2] = z;
            nSamp++;
        };
        add((fr.v[0][0] + fr.v[1][0] + fr.v[2][0]) / 3.f,
            (fr.v[0][1] + fr.v[1][1] + fr.v[2][1]) / 3.f,
            (fr.v[0][2] + fr.v[1][2] + fr.v[2][2]) / 3.f);
        for (int i = 0; i < 3; i++) add(fr.v[i][0], fr.v[i][1], fr.v[i][2]);
        for (int i = 0; i < 3; i++) {
            const int j = (i + 1) % 3;
            add(0.5f * (fr.v[i][0] + fr.v[j][0]),
                0.5f * (fr.v[i][1] + fr.v[j][1]),
                0.5f * (fr.v[i][2] + fr.v[j][2]));
        }

        float scoreSum = 0.f, confSum = 0.f;
        int valid = 0;
        for (int si = 0; si < nSamp; si++) {
            // Nudge along face normal so coplanar surface samples aren't
            // counted as "inside" the hull volume.
            float q[3] = {samples[si][0] + fr.plane.n[0] * 1e-3f,
                          samples[si][1] + fr.plane.n[1] * 1e-3f,
                          samples[si][2] + fr.plane.n[2] * 1e-3f};
            InsideSample is =
                QueryAgainstBvh(q, bvh, out->hygiene.orientationCoherence);
            if (is.invalidated) continue;
            scoreSum += is.score;
            confSum += is.confidence;
            valid++;
        }
        out->probed++;
        if (valid == 0) continue;
        const float score = scoreSum / (float)valid;
        const float conf = confSum / (float)valid;
        if (score < kInsideScoreThresh || conf < kAnalysisMinConfidence) continue;

        Finding f;
        f.model = base.shipIndex;
        f.a = fr.primitive;
        f.b = 0;
        f.type = FindingType::EmbeddedSurface;
        f.confidence = Clampf(conf, 0.f, 1.f);
        f.severity = Clampf(score, 0.f, 1.f);
        char ev[160];
        std::snprintf(ev, sizeof(ev),
                      "inside_score=%.3f conf=%.3f orient=%.2f samples=%d", score,
                      conf, out->hygiene.orientationCoherence, valid);
        f.evidence = ev;
        f.hint = RemediationHint::Review;
        char id[32];
        std::snprintf(id, sizeof(id), "finding-emb-%04d", out->embedded + 1);
        f.id = id;
        out->findings.push_back(std::move(f));
        out->embedded++;
    }
    return true;
}

void BuildSyntheticBuriedPlate(std::vector<FaceRecord>* faces,
                               uint32_t* buriedPrimOut)
{
    if (!faces) return;
    faces->clear();
    auto addTri = [&](float ax, float ay, float az, float bx, float by, float bz,
                      float cx, float cy, float cz, FaceSemantic cls,
                      uint32_t prim, uint32_t flags) {
        FaceRecord f;
        f.model = -1;
        f.primitive = prim;
        f.cls = cls;
        f.flags = flags;
        f.v[0][0] = ax;
        f.v[0][1] = ay;
        f.v[0][2] = az;
        f.v[1][0] = bx;
        f.v[1][1] = by;
        f.v[1][2] = bz;
        f.v[2][0] = cx;
        f.v[2][1] = cy;
        f.v[2][2] = cz;
        // Plane / area.
        const float ux = bx - ax, uy = by - ay, uz = bz - az;
        const float vx = cx - ax, vy = cy - ay, vz = cz - az;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-12f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        f.plane.n[0] = nx;
        f.plane.n[1] = ny;
        f.plane.n[2] = nz;
        f.plane.d = -(nx * ax + ny * ay + nz * az);
        f.area = 0.5f * len;
        for (int i = 0; i < 3; i++) {
            f.bounds.min[i] =
                std::min(f.v[0][i], std::min(f.v[1][i], f.v[2][i]));
            f.bounds.max[i] =
                std::max(f.v[0][i], std::max(f.v[1][i], f.v[2][i]));
        }
        faces->push_back(f);
    };

    // Unit cube [-1,1]^3, outward faces (two tris each).
    auto face = [&](float ax, float ay, float az, float bx, float by, float bz,
                    float cx, float cy, float cz, float dx, float dy, float dz,
                    uint32_t base) {
        addTri(ax, ay, az, bx, by, bz, cx, cy, cz, FaceSemantic::Hull, base,
               kFaceFlagHull);
        addTri(ax, ay, az, cx, cy, cz, dx, dy, dz, FaceSemantic::Hull, base + 1,
               kFaceFlagHull);
    };
    // +Z / -Z / +Y / -Y / +X / -X
    face(-1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1, 100);
    face(-1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 110);
    face(-1, 1, -1, -1, 1, 1, 1, 1, 1, 1, 1, -1, 120);
    face(-1, -1, -1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 130);
    face(1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 140);
    face(-1, -1, -1, -1, -1, 1, -1, 1, 1, -1, 1, -1, 150);

    // Buried plate near origin (overlay class so AnalyzeShipInside probes it).
    const uint32_t buried = 9999;
    addTri(-0.3f, -0.3f, 0.f, 0.3f, -0.3f, 0.f, 0.3f, 0.3f, 0.f,
           FaceSemantic::Overlay, buried, kFaceFlagOverlay);
    addTri(-0.3f, -0.3f, 0.f, 0.3f, 0.3f, 0.f, -0.3f, 0.3f, 0.f,
           FaceSemantic::Overlay, buried + 1, kFaceFlagOverlay);
    if (buriedPrimOut) *buriedPrimOut = buried;
}

}  // namespace jjffe
