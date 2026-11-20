#ifndef JJFFE_DPAL_LUMA_H
#define JJFFE_DPAL_LUMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Remap RGB444 entries that DPal leaves invisible (unmapped or VGA0) to a
// pure-grey slot of matching luminance. Restores freckle contrast for materials
// whose shade RGB is chroma the oracle never mapped (Falcon mat1/19, etc.).
//
// dpal_map_4096 uses ASM encoding: 0 = unmapped, N+1 = VGA index N.
// No-op when AB_DPAL_STRICT=1 (A/B oracle parity).
void jjffe_dpal_apply_luminance_fallback(uint8_t* dpal_map_4096);

#ifdef __cplusplus
}
#endif

#endif
