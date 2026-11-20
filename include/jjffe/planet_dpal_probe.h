#ifndef JJFFE_PLANET_DPAL_PROBE_H
#define JJFFE_PLANET_DPAL_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Env AB_1F_DPAL_PROBE=1 — stderr table + lookup for planet greys.
 * map770: 4096-byte RGB444→(slot+1), 0=unmapped (ASM DATA_007770).
 * sec9139 / rev9140: secondary + reverse tables (ASM DATA_009139/140). */
void jjffe_dpal_probe_tables(const char* side,
                             const uint8_t* map770,
                             const uint8_t* sec9139,
                             const uint8_t* rev9140);

/* When sec/rev unavailable (ffeasm), pass native DPal16toPal callback. */
void jjffe_dpal_probe_native(const char* side,
                             const uint8_t* map770,
                             uint8_t (*dpal16)(uint16_t));

/* Replicate ASM FUNC_001327_DPal16toPal using live tables. */
uint8_t jjffe_dpal_lookup16(uint16_t rgb444,
                            const uint8_t* map770,
                            const uint8_t* sec9139,
                            const uint8_t* rev9140);

/* ASM FUNC_001326_DPal32to16 pack (return value only). */
uint32_t jjffe_planet_dpal32_probe(uint32_t rgb_dword);

#ifdef __cplusplus
}
#endif

#endif
