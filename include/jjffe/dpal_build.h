#ifndef JJFFE_DPAL_BUILD_H
#define JJFFE_DPAL_BUILD_H

#include <cstdint>

namespace jjffe {

// FUNC_001330 median-cut + stamp into dPalSecondary / dPalReverse / dPalRGB.
// Occupancy is seeded via DPal32to16 (DATA_009139 dual role). Q2 box centroids
// match JUMP_005271: ((min+max-1)*2)+1 in 6-bit DAC space.
void DPalBuildMedianCut(const uint8_t* palette, int baseIndex);

} // namespace jjffe

#endif
