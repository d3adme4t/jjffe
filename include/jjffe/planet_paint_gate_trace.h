#ifndef JJFFE_PLANET_PAINT_GATE_TRACE_H
#define JJFFE_PLANET_PAINT_GATE_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_869_PAINT_GATE_TRACE=/path — depth-2 FUNC_001869 paint gate samples */
extern int g_jjffe_planet_paint_gate_enabled;
extern int g_jjffe_planet_paint_gate_asm_n;

/* ASM scratch (set before jjffe_planet_paint_gate_asm_note). */
extern int g_jjffe_pg_hook_depth;
extern int g_jjffe_pg_hook_cvar1;
extern int g_jjffe_pg_hook_area48;
extern void* g_jjffe_pg_hook_v1;
extern void* g_jjffe_pg_hook_v2;
extern void* g_jjffe_pg_hook_v3;

void jjffe_planet_paint_gate_trace_open(const char* path);
void jjffe_planet_paint_gate_trace_close(void);
void jjffe_planet_paint_gate_trace_flush(void);

void jjffe_planet_paint_gate_note(int depth, int iVar4, int cVar1, int area48, int gateReason,
                                  const void* v1, const void* v2, const void* v3);

/* depth-2 subdivide path (869 falls through to midpoint split). */
void jjffe_planet_paint_gate_subdivide_note(int depth, int iVar4, int cVar1, int area48,
                                            const void* v1, const void* v2, const void* v3);

/* Called from ASM at JUMP_007464 when depth==2 (reads g_jjffe_pg_hook_*). */
void jjffe_planet_paint_gate_asm_note(void);

#ifdef __cplusplus
}
#endif

#endif
