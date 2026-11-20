#ifndef JJFFE_PLANET_SHADE_TRACE_H
#define JJFFE_PLANET_SHADE_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_SHADE_STRIP_TRACE=1 — per-807 job:
 * pre/post DATA_009252[0..15] (or C++ shadePal), texel histogram, max texel. */
void jjffe_planet_shade807_begin(const void* job, const char* side, const uint8_t* palPre16);
void jjffe_planet_tex_trace_note(uint8_t texel);
void jjffe_planet_fill_trace_note(uint8_t palIndex);
void jjffe_planet_shade807_finish(const void* job, const char* side, const uint8_t* palPost16);
void jjffe_planet_shade807_session_report(void);

/* AB_1F_807_PAL_TRACE=<path> — post-001327 shadePal[0..6] keyed by sortKey. */
void jjffe_planet_shade807_pal_trace(const void* job, const char* side, const uint8_t* shadePal7);

#if defined(FFEASM_SHIP_DEMO)
void jjffe_planet_shade807_begin_asm(const void* job, const uint8_t* pal252);
void jjffe_planet_shade807_finish_asm(const void* job, const uint8_t* pal252);
#endif

/* AB_1F_840_TRACE=1 — cVar1==3 (flat planet leaf) enqueue. */
void jjffe_planet_840_trace_emit(const char* side,
                                 int ri2,
                                 int ri3,
                                 int local14b,
                                 const uint32_t* tableBase,
                                 const uint32_t* shades7);

void jjffe_planet_840_trace_emit_v(const char* side,
                                   const uint32_t* shades7,
                                   const uint32_t* tableBase,
                                   int local14b,
                                   const void* vert2,
                                   const void* vert3);

extern const char jjffe_planet_side_asm840[];

extern int g_jjffe_planet_840_gate_enabled;

/* AB_1F_840_GATE_TRACE=<path> — 840 gate: sortKey, metric, cvar1, three +0x3c mixes. */
void jjffe_planet_840_gate_trace(const char* side,
                                 uint32_t sort_key,
                                 int32_t vx,
                                 int32_t vy,
                                 int32_t vz,
                                 int32_t metric_raw,
                                 int32_t gate_num,
                                 int32_t d263,
                                 int32_t d267,
                                 int cvar1,
                                 int ivar3,
                                 int32_t mix0,
                                 int32_t mix1,
                                 int32_t mix2);
void jjffe_planet_840_gate_trace_close(void);
/* With AB_1F_840_GATE_TRACE_LAST=1, restart the trace file at each mission
 * frame, so it holds only the last frame drawn (the dumped one). */
void jjffe_planet_840_gate_trace_frame_begin(void);

/* AB_1F_LOCAL14B_TRACE=<path> — cVar1==3: local14b, three +0x3c depths, DATA_007846 row. */
extern int g_jjffe_planet_local14b_enabled;

void jjffe_planet_local14b_trace_emit(const char* side,
                                      uint32_t sort_key,
                                      int local14b,
                                      int32_t d0,
                                      int32_t d1,
                                      int32_t d2,
                                      uint32_t table0);

void jjffe_planet_local14b_trace_emit_v(const char* side,
                                         uint32_t sort_key,
                                         int local14b,
                                         int32_t d0,
                                         int32_t d1,
                                         int32_t d2,
                                         uint32_t table0,
                                         const void* vert2,
                                         const void* vert3);

void jjffe_planet_local14b_trace_close(void);

/* AB_1F_807_STRIP_TRACE=<path> — 807 enqueue: 24-bit tbl, ivar3/local8/d265, DPal probe, strip. */
extern int g_jjffe_planet_807_strip_enabled;

void jjffe_planet_807_strip_trace_arm_v(const char* side,
                                        uint32_t sort_key,
                                        int cvar1,
                                        uint32_t draw_tex,
                                        int table_id,
                                        const uint32_t* table_base,
                                        int ivar3,
                                        int32_t local8,
                                        uint32_t d265);

void jjffe_planet_807_strip_trace_finish(const uint32_t* strip7);
void jjffe_planet_807_strip_trace_close(void);

/* AB_1F_840_838_PUSH_TRACE=<path> — 840 site A–D: ebx immediately before 838 push ebx. */
void jjffe_planet_838_push_trace_v(const char* side,
                                   uint32_t sort_key,
                                   int site_id,
                                   int ebx_at_push);

/* AB_1F_838_BLEND_TRACE=<path> — 838 iter 0: branch, [ebp+0xc], eax post-and. */
void jjffe_planet_838_blend_trace_v(const char* side,
                                    int branch,
                                    int param2,
                                    uint32_t eax_pre326,
                                    int iter);
void jjffe_planet_838_push_trace_close(void);
void jjffe_planet_838_blend_trace_close(void);

/* AB_1F_838_PACK_TRACE=<path> — 838 strip store: blend_sum, rem, pre326, rgb16. */
extern int g_jjffe_planet_838_pack_enabled;

void jjffe_planet_838_pack_trace_v(const char* side,
                                   int branch,
                                   int param2,
                                   int idx,
                                   int32_t blend_sum,
                                   uint32_t pre326,
                                   uint32_t stored_rgb16);
void jjffe_planet_838_pack_trace_close(void);

/* AB_1F_838_OPERAND_TRACE=<path> — 838 blend operands: accum build + per-iter tbl/mul/sum. */
extern int g_jjffe_planet_838_operand_enabled;

void jjffe_planet_838_operand_trace_v(const char* side,
                                      int kind,
                                      int param2,
                                      int idx,
                                      int32_t param3_in,
                                      int32_t local8_x8,
                                      int32_t d265_term,
                                      int32_t ebp10_accum,
                                      int32_t tbl,
                                      int32_t mul_factor,
                                      int32_t blend_sum);
void jjffe_planet_838_operand_trace_close(void);

/* AB_1F_D265_TRACE=<path> — DATA_009262/009265 at 877 exit and 838 entry. */
extern int g_jjffe_planet_d265_enabled;

void jjffe_planet_d265_trace_v(const char* side, const char* site, uint32_t d262, uint32_t d265);
void jjffe_planet_d265_trace_close(void);

/* AB_1F_877_TRACE[=path] — one line per FUNC_001877: branch, 263, camLen, 267, param6. */
extern int g_jjffe_planet_877_enabled;

void jjffe_planet_877_trace_emit(const char* side,
                                 int branch,
                                 int32_t d263,
                                 int32_t camLen,
                                 int32_t d267,
                                 int32_t param6,
                                 uint32_t param5,
                                 uint32_t gateLeft,
                                 uint32_t gateRight);

void jjffe_planet_877_trace(int branch,
                            int32_t d263,
                            int32_t camLen,
                            int32_t d267,
                            int32_t param6,
                            uint32_t param5);

void jjffe_planet_877_trace_close(void);

/* AB_1F_877_CAMOFF_TRACE=<path> — camOff vector + camLen isqrt inputs at 877. */
void jjffe_planet_877_camoff_trace_v(const char* side,
                                     int32_t cam0, int32_t cam1, int32_t cam2,
                                     int32_t camShift, int32_t sumSq, int32_t isqrtRaw,
                                     int32_t camLen);
void jjffe_planet_877_camoff_trace_close(void);

/* AB_1F_877_PREFSTORE_TRACE=<path> — post-fstore bc/b8/b4 oracle at ffe.asm:132141. */
void jjffe_planet_877_prefstore_v(const char* side, int32_t bc, int32_t b8, int32_t b4);
void jjffe_planet_877_prefstore_close(void);

/* AB_1F_879_ASM_DEBUG=1 — per 879 call: local8, baseShade, local_c, shadeTable, verts. */
void jjffe_planet_879_asm_debug(int32_t local8, uint32_t baseShade, uint32_t local_c,
                                const void* shadeTable,
                                const void* p2, const void* p3, const void* p4,
                                const void* p5, const void* p6, const void* p7);

/* AB_1F_805_JOB_TRACE=<path> / AB_1F_806_JOB_TRACE=<path> — one TSV row per
 * 805/806 job EXECUTION (clock, sortKey, drawTex, 7 raw shade words, verts /
 * UV corners + dither words). Shared by ffeasm (hook in ffe.asm) and the C++
 * FUNC_001805/001806 so the two execution streams diff 1:1. */
extern int g_jjffe_planet_805_job_enabled;
extern int g_jjffe_planet_806_job_enabled;

void jjffe_planet_805_job_trace(uint32_t clock, const void* job);
void jjffe_planet_806_job_trace(uint32_t clock, const void* job);
void jjffe_planet_807_job_trace(uint32_t clock, const void* job);
void jjffe_planet_808_job_trace(uint32_t clock, const void* job);
void jjffe_planet_805_job_trace_close(void);
void jjffe_planet_806_job_trace_close(void);
void jjffe_planet_set_backbuf(const void* p);
const void* jjffe_job_trace_backbuf(void);
extern int g_jjffe_planet_807_job_enabled;
extern int g_jjffe_planet_808_job_enabled;

#ifdef __cplusplus
}
#endif

#endif
