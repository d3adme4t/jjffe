#ifndef JJFFE_PLANET_MID_SCR_TRACE_H
#define JJFFE_PLANET_MID_SCR_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_MID_SCR_TRACE=<path> — FUNC_001855 simple-path screen nudge (avg+dither). */
extern int g_jjffe_planet_mid_scr_enabled;

/* ASM hook scratch (set in ffe.asm before jjffe_planet_mid_scr_trace_asm). */
extern int g_scr_hook_edge_swap;
extern int g_scr_hook_param5;
extern void* g_scr_hook_p2;
extern void* g_scr_hook_p3;
extern void* g_scr_hook_p4;
extern void* g_scr_hook_child;

void jjffe_planet_mid_scr_trace_open(const char* path);
void jjffe_planet_mid_scr_trace_close(void);

/* C++ FUNC_001855 simple path — all dither fields precomputed. */
void jjffe_planet_mid_scr_trace_note(const char* side,
                                     int param5,
                                     int edge_swap,
                                     const void* p2,
                                     const void* p3,
                                     const void* p4,
                                     const void* child,
                                     int avg_x,
                                     int avg_y,
                                     int dither_on,
                                     int i6b,
                                     int i8,
                                     int side_pref,
                                     int vx_lt,
                                     int vy_lt,
                                     int tbl_idx,
                                     int delta_x,
                                     int delta_y);

/* ASM: reads g_scr_hook_* then emits one row (recomputes dither index). */
void jjffe_planet_mid_scr_trace_asm(void);

#ifdef __cplusplus
}
#endif

#endif
