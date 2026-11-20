#ifndef JJFFE_PLANET_792_CHAIN_TRACE_H
#define JJFFE_PLANET_792_CHAIN_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_792_CHAIN_TRACE=<path> — FUNC_001792 band-runner callback chain. */
extern int g_jjffe_planet_792_chain_enabled;

void jjffe_planet_792_chain_open(const char* path);
void jjffe_planet_792_chain_close(void);

/* CPP: one row per callback invoke (pre/post combined). */
void jjffe_planet_792_chain_note_cpp(intptr_t band,
                                     int step,
                                     void* job,
                                     void* cb,
                                     const char* fn,
                                     int d834_in,
                                     void* next,
                                     int d834_out,
                                     const char* note);

void jjffe_planet_792_chain_note_cpp_side(const char* side,
                                          intptr_t band,
                                          int step,
                                          void* job,
                                          void* cb,
                                          const char* fn,
                                          int d834_in,
                                          void* next,
                                          int d834_out,
                                          const char* note);

/* Band loop exited without invoking job at `next` (DATA_007834==2 at head). */
void jjffe_planet_792_chain_note_exit(const char* side, intptr_t band, int step, void* next);

/* ASM hooks around `call [job]` in FUNC_001792 / FUNC_001788. */
void jjffe_planet_792_chain_pre_asm_side(const char* side,
                                         intptr_t band,
                                         void* job,
                                         int d834_in);
void jjffe_planet_792_chain_post_asm_side(const char* side, void* next, int d834_out);
void jjffe_planet_792_chain_pre_asm(intptr_t band, void* job, int d834_in);
void jjffe_planet_792_chain_post_asm(void* next, int d834_out);
void jjffe_planet_792_chain_note_exit_asm(intptr_t band, void* next);

extern const char jjffe_side_asm792[];
extern const char jjffe_side_asm788[];

#ifdef __cplusplus
}
#endif

#endif
