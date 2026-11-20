#ifndef JJFFE_FACE_INSIDE_H
#define JJFFE_FACE_INSIDE_H

#include "jjffe/face_analysis.h"

#include <cstdint>
#include <vector>

namespace jjffe {

// HW plan W3 — Generalized Fast Winding Number ensemble for "embedded"
// surface detection. Runs on an analysis-only hygiened hull copy (policy D).

constexpr float kInsideWindingThresh = 0.55f;   // ~inside if |w| above this
constexpr float kInsideScoreThresh = 0.72f;     // ensemble → EMBEDDED_SURFACE
constexpr float kInsideOrientCoherenceMin = 0.55f;  // below → invalidate FWN
constexpr float kInsideBarnesHutTheta = 0.5f;

struct HygieneStats {
    int inputTris = 0;
    int keptTris = 0;
    int droppedZeroArea = 0;
    int droppedDupe = 0;
    float orientationCoherence = 0.f;  // 0..1 fraction agreeing with majority
    bool usable = false;               // enough tris + coherence
};

struct InsideSample {
    float score = 0.f;       // 0 outside .. 1 inside
    float confidence = 0.f;  // 0..1
    float winding = 0.f;
    float closestSign = 0.f;  // +1 outside-ish, -1 inside-ish
    float rayInside = 0.f;    // 0..1 fraction of rays saying inside
    bool invalidated = false; // orientation too incoherent for FWN
};

struct ShipInsideReport {
    HygieneStats hygiene;
    int probed = 0;
    int embedded = 0;
    std::vector<Finding> findings;  // EMBEDDED_SURFACE only
};

// Build hygiened hull triangle soup from FaceRecords (hull only, front side).
HygieneStats BuildHygieneHull(const std::vector<FaceRecord>& faces,
                              std::vector<float>* outVerts,  // xyz*3 per tri
                              std::vector<float>* outNormals);

// Query one point against the hygiened hull (BVH + FWN + ray + closest).
// Rebuilds BVH each call — prefer AnalyzeShipInside for fleet work.
InsideSample QueryInside(const float p[3], const float* verts, int nTris,
                         const float* normals, float orientCoherence);

// Probe overlay / punch / suspect faces; emit EMBEDDED_SURFACE findings.
// Skips primitives already tagged COPLANAR_OVERLAY (painter-order surface).
// Builds hygiene + BVH once per ship.
bool AnalyzeShipInside(const ShipAnalysis& base, ShipInsideReport* out);

// Synthetic unit cube [−1,1]^3 with a buried plate at z=0 — positive control.
void BuildSyntheticBuriedPlate(std::vector<FaceRecord>* faces,
                               uint32_t* buriedPrimOut);

}  // namespace jjffe

#endif
