#ifndef JJFFE_PLANET_SAT_TRACE_H
#define JJFFE_PLANET_SAT_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_SAT_TRACE=/path — dump local18 header + 20 face pointers at 823 exit and 876 entry */
extern int g_jjffe_planet_sat_enabled;

void jjffe_planet_sat_trace_open(const char* path);
void jjffe_planet_sat_trace_close(void);
void jjffe_planet_sat_note_823(uintptr_t base, int allocEnd, const int16_t* stream);
void jjffe_planet_sat_note_876(uintptr_t local18);

#ifdef __cplusplus
}

namespace jjffe {

// FUNC_001823 stores uint32 byte-offsets into the sat tree arena (ASM stored
// absolute 32-bit pointers in the same slots). Positive slot words must be
// resolved through this before dereference or recursive sat descent.
int* PlanetSatResolve(int32_t slot);

}  // namespace jjffe
#endif

#endif
