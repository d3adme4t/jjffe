#ifndef JJFFE_PLANET_SUBDIV_TRACE_H
#define JJFFE_PLANET_SUBDIV_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_SUBDIV_TRACE=/path — unified 876 dispatch / 869 entry / r13 / 855 e[2] */
extern int g_jjffe_planet_subdiv_enabled;

/* ASM scratch (set before jjffe_planet_subdiv_asm_note). */
extern int g_jjffe_subdiv_hook_event; /* 1=876 2=869 3=r13 4=855 5=834 6=876_lc */
extern int g_jjffe_subdiv_hook_depth;
extern int g_jjffe_subdiv_hook_face;
extern int g_jjffe_subdiv_hook_p5;
extern int g_jjffe_subdiv_hook_ri2;
extern int g_jjffe_subdiv_hook_ri3;
extern int g_jjffe_subdiv_hook_reason;
extern int g_jjffe_subdiv_hook_e2hit;
extern int g_jjffe_subdiv_hook_action;
extern int g_jjffe_subdiv_hook_area48;
extern void* g_jjffe_subdiv_hook_v1;
extern void* g_jjffe_subdiv_hook_v2;
extern void* g_jjffe_subdiv_hook_v3;
extern void* g_jjffe_subdiv_hook_edge;
extern uintptr_t g_jjffe_subdiv_hook_e0;
extern uintptr_t g_jjffe_subdiv_hook_e1;
extern uintptr_t g_jjffe_subdiv_hook_e2;
extern uintptr_t g_jjffe_subdiv_hook_e3;
extern int g_jjffe_subdiv_hook_raw6;
extern int g_jjffe_subdiv_hook_adj6;
extern int g_jjffe_subdiv_hook_idx278;
extern int g_jjffe_subdiv_hook_idx279;
extern int g_jjffe_subdiv_hook_sat;

/* Persists across 869 entry (ASM clears hook_face on 869). */
extern int g_jjffe_subdiv_active_face;
extern int g_jjffe_subdiv_cur_depth;

void jjffe_planet_subdiv_trace_open(const char* path);
void jjffe_planet_subdiv_trace_close(void);
void jjffe_planet_subdiv_trace_flush(void);

/* Register root local988[0..29] after 876 edge-pool init. */
void jjffe_planet_subdiv_set_local988(const void* const* slots, const int* edgeVerts);

void jjffe_planet_subdiv_note_876(int faceIndex, int param12);
void jjffe_planet_subdiv_note_876_verts(int faceIndex, int param12,
                                        const void* v0, const void* v1, const void* v2);
void jjffe_planet_subdiv_note_876_local1c(int local10, int camZ, int local1c);
void jjffe_planet_subdiv_note_834(int raw6, int p5, int adj6, int idx278, int idx279, int sat);
void jjffe_planet_subdiv_note_869_entry(int depth, int area48,
                                        const void* v1, const void* v2, const void* v3);
void jjffe_planet_subdiv_note_869_r13(int depth, int area48,
                                      const void* v1, const void* v2, const void* v3);
void jjffe_planet_subdiv_note_855(void* edgeSlot, int p5, int ri2, int ri3, int e2hit, int action,
                                  const void* v2, const void* v3);
void jjffe_planet_subdiv_note_mid_fn(int fn, void* edgeSlot, int p5, int ri2, int ri3, int e2hit,
                                     int action, const void* v2, const void* v3);

void jjffe_planet_subdiv_asm_note(void);

#ifdef __cplusplus
}
#endif

#endif
