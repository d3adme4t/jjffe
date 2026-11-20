#ifndef JJFFE_PLANET_MIDPOINT_TRACE_H
#define JJFFE_PLANET_MIDPOINT_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enable via jjffe_planet_mid_trace_open() or env AB_1F_MIDPOINT_TRACE=/path */
extern int g_jjffe_planet_mid_trace_enabled;

extern int g_jjffe_planet_mid_asm_n;
extern int g_jjffe_planet_mid_asm_simple;
extern int g_jjffe_planet_mid_asm_else;
extern int g_jjffe_planet_mid_asm_samples[4096 * 11];
extern int g_jjffe_planet_mid_hook_eff5;
extern int g_jjffe_planet_mid_hook_branch;
extern int g_jjffe_planet_mid_hook_lod2;
extern int g_jjffe_planet_mid_hook_lod3;
extern int g_jjffe_planet_mid_hook_param5;
extern int g_jjffe_planet_mid_hook_mix2;
extern int g_jjffe_planet_mid_hook_mix3;
extern int g_jjffe_planet_mid_hook_term846;
extern int g_jjffe_planet_mid_hook_avg;
extern uint32_t g_jjffe_planet_mid_hook_sortKey;
extern int g_jjffe_planet_mid_hook_ri2;
extern int g_jjffe_planet_mid_hook_ri3;
extern int g_jjffe_planet_mid_hook_fn;
extern int g_jjffe_planet_mid_hook_action;
extern int g_jjffe_planet_mid_hook_d260a;
extern int g_jjffe_planet_mid_hook_d260b;
extern int g_jjffe_planet_mid_hook_tu0;
extern int g_jjffe_planet_mid_hook_tu1;
extern int g_jjffe_planet_mid_hook_gate;
extern int g_jjffe_planet_mid_hook_iter;

/* Stack recs base during FUNC_001876 tessellation (for root-index edge id). */
void jjffe_planet_set_876_recs(const void* recs12x48);
int jjffe_planet_root_index(const void* vertRec);
void jjffe_planet_mid_hook_set_parent_verts(const void* p2, const void* p3);

void jjffe_planet_mid_trace_open(const char* path);
void jjffe_planet_mid_trace_close(void);
void jjffe_planet_mid_trace_flush(void);

/* C++ midpoint exit hook (855/857/858/863). action: 0=skip 1=create 2=aux */
void jjffe_planet_mid_trace_vertex(int fn, int action, int branch, int param5,
                                   const void* midVert);

/* 863 DATA_009260 slot: action 2=aux write 3=aux null 4=far apply 5=far skip */
void jjffe_planet_mid_trace_863_slot(int action, int param5, int iter,
                                     int d260a, int d260b, int tu0, int tu1, int gate);

/* C++ FUNC_001855 exit hook (after FUNC_001829). */
void jjffe_planet_mid_trace_855(int lod2, int lod3, int param5, int eff5, int branch,
                               const void* midVert);

/* ASM midpoint hook (32-bit ffeasm only). Set g_jjffe_planet_mid_hook_fn/action/branch/param5 first. */
void jjffe_planet_mid_trace_fn_from_asm(const void* midVert);

/* ASM FUNC_001855 hook scratch (32-bit ffeasm only). */
void jjffe_planet_mid_trace_855_mid(const void* midVert);

/* ASM FUNC_001863 aux-loop slot hook (32-bit ffeasm only). */
void jjffe_planet_mid_trace_863_slot_asm(void);

/* AB_1F_SUBDIV_DUMP=<path>: the last frame's 0x1F subdivision - FUNC_001876
 * roots (H/R), each face's roots after FUNC_001834 (F) and every midpoint
 * call (M). Same format as the ffeasm dump in tools/ffe_flow_surface.gdb;
 * compare with tools/subdiv_diff.py. */
int jjffe_subdiv_dump_enabled(void);
void jjffe_subdiv_dump_frame_begin(void);
void jjffe_subdiv_dump_876(const void* recs12x48, const int32_t* p6, const int32_t* p7,
                           int p12, int mode, int seed, int local10, int localC, int d9269);
void jjffe_subdiv_dump_face(int face, const void* v1, const void* v2, const void* v3,
                            int leaf0Type, int leaf1Type);
void jjffe_subdiv_dump_mid(int fnIdx, int depth, int created, const void* edge,
                           const void* p2, const void* p3, const void* p4, const void* mid);

#ifdef __cplusplus
}
#endif

#endif
