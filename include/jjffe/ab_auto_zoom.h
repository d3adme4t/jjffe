// Shared A/B camera distance from ObjectStruct radius + scaleRelated.
// Used by C++ ab_capture_shuttle / Demo3D and ASM ffeasm --ship-demo.
//
// Projection (FUNC_001479): screen ≈ (modelCoord << scaleRelated << 8) / shipZ
// with shipZ = distUnits * (FP_ONE/2) = distUnits * 0x8000.
// Bounding-sphere radius at distUnits=1:
//   px = (radius << scaleRelated << 8) / 0x8000
//
// Default target 250px keeps Saker III (#31, ≈250) at distUnits=1 so existing
// glassbox dumps stay unchanged. Larger ships zoom out only (never closer).

#ifndef JJFFE_AB_AUTO_ZOOM_H
#define JJFFE_AB_AUTO_ZOOM_H

#include <cstdint>

namespace jjffe {

constexpr int kAbAutoZoomTargetPx = 250;
constexpr int kAbAutoZoomMaxDistUnits = 64;
/* 0x1F bodies use scaleRelated≈22; du=64 still parks the camera inside the
 * sphere. Cap so s_dist = du*(FP_ONE/2) and 4× zoom-out stay in int32. */
constexpr int kAbAutoZoomMaxDistUnitsBody = 40000;

inline int AbAutoFitDistUnits(uint32_t radius, uint32_t scaleRelated,
                              int targetPx = kAbAutoZoomTargetPx,
                              int maxDistUnits = kAbAutoZoomMaxDistUnits)
{
    if (radius == 0 || targetPx <= 0)
        return 1;
    if (scaleRelated > 24)
        scaleRelated = 24;
    if (maxDistUnits < 1)
        maxDistUnits = 1;
    const uint64_t scaled = static_cast<uint64_t>(radius) << scaleRelated;
    const uint64_t pxAt1 = (scaled << 8) / 0x8000u;
    if (pxAt1 <= static_cast<uint64_t>(targetPx))
        return 1;
    const uint64_t needZ = (scaled << 8) / static_cast<uint64_t>(targetPx);
    int du = static_cast<int>((needZ + 0x7FFFu) / 0x8000u);
    if (du < 1)
        du = 1;
    if (du > maxDistUnits)
        du = maxDistUnits;
    return du;
}

} // namespace jjffe

#endif
