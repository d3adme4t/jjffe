#include "jjffe/dpal_luma.h"

#include <stdlib.h>

void jjffe_dpal_apply_luminance_fallback(uint8_t* dpal_map_4096)
{
    if (!dpal_map_4096)
        return;
    const char* strict = getenv("AB_DPAL_STRICT");
    if (strict && strict[0] == '1')
        return;

    // Pure-grey RGB444 → existing map slot (N+1). Prefer oracle greys.
    uint8_t greySlot[16];
    for (int gn = 0; gn < 16; gn++) {
        const int idx = (gn << 8) | (gn << 4) | gn;
        greySlot[gn] = dpal_map_4096[idx];
    }
    // Fill missing greys from nearest mapped neighbour (still pure grey).
    for (int gn = 1; gn < 16; gn++) {
        if (greySlot[gn] != 0)
            continue;
        int best = -1, bestDist = 99;
        for (int o = 1; o < 16; o++) {
            if (greySlot[o] == 0)
                continue;
            const int d = gn > o ? gn - o : o - gn;
            if (d < bestDist) {
                bestDist = d;
                best = o;
            }
        }
        if (best > 0)
            greySlot[gn] = greySlot[best];
        else
            // Absolute VGA bank (slot+0x80), matching DPalSet / median-cut.
            greySlot[gn] = (uint8_t)(gn + 0x80 + 1);
    }

    for (int i = 0; i < 4096; i++) {
        const int r = (i >> 8) & 0xF;
        const int g = (i >> 4) & 0xF;
        const int b = i & 0xF;
        if ((r | g | b) == 0)
            continue; // keep black

        const uint8_t m = dpal_map_4096[i];
        const uint8_t vga = (m == 0) ? 0 : (uint8_t)(m - 1);
        // Unmapped, or collapsed to VGA0 / dynamic-bank black (slot 0 →
        // absolute 0x80). Earth mesh shades (RGB444 0x620 etc.) nearest-
        // match palette[0] under legacy hole-fill and would otherwise stay
        // black while ASM paints them into a maroon dynamic slot (~168).
        const uint8_t slot = (uint8_t)(vga & 0x3f);
        if (m != 0 && slot != 0)
            continue;

        int gn = r;
        if (g > gn)
            gn = g;
        if (b > gn)
            gn = b;
        if (gn < 1)
            gn = 1;
        dpal_map_4096[i] = greySlot[gn];
    }
}
