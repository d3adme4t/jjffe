#include "jjffe/planet_dpal_probe.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t jjffe_dpal_lookup16(uint16_t rgb444,
                            const uint8_t* map770,
                            const uint8_t* sec9139,
                            const uint8_t* rev9140)
{
    const uint16_t idx = rgb444 & 0xffffu;
    if (!map770 || !sec9139 || !rev9140)
        return 0;
    const uint8_t slot = map770[idx];
    if (slot != 0)
        return rev9140[slot - 1];
    return rev9140[sec9139[idx]];
}

void jjffe_dpal_probe_tables(const char* side,
                             const uint8_t* map770,
                             const uint8_t* sec9139,
                             const uint8_t* rev9140)
{
    if (!getenv("AB_1F_DPAL_PROBE") || !side || !map770 || !sec9139 || !rev9140)
        return;

    const char* strict = getenv("AB_DPAL_STRICT");
    const char* legacy = getenv("AB_DPAL_LEGACY");
    fprintf(stderr,
            "DPAL_PROBE %s strict=%s legacy=%s\n",
            side,
            (strict && strict[0] == '1') ? "1" : "0",
            (legacy && legacy[0] == '1') ? "1" : "0");

    static const uint16_t kProbe[] = {0x0444, 0x0557, 0x0666, 0x0667, 0x0888};
    for (int t = 0; t < 5; ++t) {
        const uint16_t rgb = kProbe[t];
        const uint8_t slot = map770[rgb];
        const uint8_t sec = sec9139[rgb];
        const uint8_t pal = jjffe_dpal_lookup16(rgb, map770, sec9139, rev9140);
        fprintf(stderr,
                "DPAL_PROBE %s rgb=0x%04x slot=%u sec=%u pal=%u\n",
                side, (unsigned)rgb, (unsigned)slot, (unsigned)sec, (unsigned)pal);
    }
}

void jjffe_dpal_probe_native(const char* side,
                             const uint8_t* map770,
                             uint8_t (*dpal16)(uint16_t))
{
    if (!getenv("AB_1F_DPAL_PROBE") || !side || !map770 || !dpal16)
        return;

    const char* strict = getenv("AB_DPAL_STRICT");
    const char* legacy = getenv("AB_DPAL_LEGACY");
    fprintf(stderr,
            "DPAL_PROBE %s strict=%s legacy=%s native=1\n",
            side,
            (strict && strict[0] == '1') ? "1" : "0",
            (legacy && legacy[0] == '1') ? "1" : "0");

    static const uint16_t kProbe[] = {0x0444, 0x0557, 0x0666, 0x0667, 0x0888};
    for (int t = 0; t < 5; ++t) {
        const uint16_t rgb = kProbe[t];
        const uint8_t slot = map770[rgb];
        const uint8_t pal = dpal16(rgb);
        fprintf(stderr,
                "DPAL_PROBE %s rgb=0x%04x slot=%u pal=%u\n",
                side, (unsigned)rgb, (unsigned)slot, (unsigned)pal);
    }
}

uint32_t jjffe_planet_dpal32_probe(uint32_t rgb_dword)
{
    const uint32_t v = rgb_dword & 0xffffffu;
    return (((v & 0xffu) << 8) + (((v >> 8) & 0xffu) << 4) + ((v >> 16) & 0xffu)) & 0xffffu;
}
