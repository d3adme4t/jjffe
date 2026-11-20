#ifndef JJFFE_PLANET_ROOT_SEED_H
#define JJFFE_PLANET_ROOT_SEED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capture (ASM) or load (C++) icosa root +0x3c mix from FUNC_001876.
 * Loader canonicalizes mix to +1/0/-1 unless AB_1F_ROOT_MIX_RAW=1.
 * Only +0x3c is applied in C++ (LOD at +0x44 comes from FUNC_001829). */
extern int g_jjffe_planet_root_seed_enabled;

void jjffe_planet_root_seed_open(const char* path);
void jjffe_planet_root_seed_close(void);

/* verts: 12 * 0x48-byte planet vertex records (ASM stack layout). */
void jjffe_planet_root_seed_asm_capture(const void* verts);

/* Load TSV written by capture; returns 1 on success. */
int jjffe_planet_root_seed_load(const char* path);

int jjffe_planet_root_seed_loaded(void);
int32_t jjffe_planet_root_seed_mix(int index);
int32_t jjffe_planet_root_seed_lod(int index);

/* Full 12-root dump after FUNC_001876 (+ seed apply on C++).
 * Env AB_1F_ROOT_STATE_TRACE=/path → TSV with sx,sy,cam,mix,lod per root. */
void jjffe_planet_root_state_write_from_env(const void* verts12x48, const char* side);

/* ASM FUNC_001876 post-root-loop hook (checks env internally).
 * verts12x48 must be the true 0x48-byte record base (ASM ebp-0x814 / f7e4), not cam+4. */
void jjffe_planet_root_state_asm_hook(const void* verts12x48);

/* Env AB_1F_MAT_TRACE=1 → one-shot stderr dump of 9-element matrix.
 * renderCtx+0x14 for ctx dumps; use jjffe_planet_mat_trace_mat9 for gameObj base. */
void jjffe_planet_mat_trace_from_env(const void* renderCtx, const char* side);
void jjffe_planet_mat_trace_mat9(const int32_t* m, const char* side);

extern const char jjffe_planet_mat_side_asm[];
extern const char jjffe_planet_mat_side_asm683[];
extern const char jjffe_planet_mat_side_asmobj[];
extern const char jjffe_planet_mat_side_asm807[];

#ifdef __cplusplus
}
#endif

#endif
