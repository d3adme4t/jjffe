#ifndef JJFFE_PLANET_CULL_TRACE_H
#define JJFFE_PLANET_CULL_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

extern int g_jjffe_planet_cull_enabled;

/* ASM scratch — set before jjffe_planet_cull_asm_note(). */
extern int g_jjffe_cull_hook_reason;
extern int g_jjffe_cull_hook_depth;
extern int g_jjffe_cull_hook_effDepth;
extern int g_jjffe_cull_hook_cvar1;
extern int g_jjffe_cull_hook_gate;
extern int g_jjffe_cull_hook_local20;
extern int g_jjffe_cull_hook_margin;
extern int g_jjffe_cull_hook_area48;
extern void* g_jjffe_cull_hook_v1;
extern void* g_jjffe_cull_hook_v2;
extern void* g_jjffe_cull_hook_v3;

void jjffe_planet_cull_trace_open(const char* path);
void jjffe_planet_cull_trace_close(void);
void jjffe_planet_cull_trace_flush(void);
void jjffe_planet_cull_asm_note(void);

#ifdef __cplusplus
}
#endif

#endif
