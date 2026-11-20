#ifndef JJFFE_JOB_TRACE_H
#define JJFFE_JOB_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phase O scheduler traces. Format (TSV, one event per line):
 *   seq  phase  jobType  sortKey  meshIndex  texInfo  material  cb  note
 * phase: ENQ | EXEC | BARRIER
 * ASM ENQ meshIndex = submitSeq for draw-sized jobs (words 5/6/12/13).
 * ASM BARRIER/ENQ notes include caller + last 0x06 opcode (ctrlCase).
 * Enable: jjffe_job_trace_open(path) or env AB_JOB_TRACE=/path/to/file.tsv
 */

extern int g_jjffe_job_trace_enabled;

void jjffe_job_trace_open(const char* path);
void jjffe_job_trace_close(void);
void jjffe_job_trace_flush(void);

/* ASM: FUNC_001696 stores opcode word before calling 001780 */
void jjffe_job_trace_set_mesh_op(uint32_t opcode);

/* ASM: FUNC_001778(words, sortKey, linkFlag); retAbs = return address */
void jjffe_job_trace_enq(int words, uint32_t sortKey, int linkFlag, uint32_t retAbs);

/* ASM: before callback dispatch in FUNC_001788 / FUNC_001792.
 * On FFEASM builds, peeks job payload: sortKey (list/band), draw texWord,
 * material (shade0 fingerprint). */
void jjffe_job_trace_exec(uint32_t cbAbs, uint32_t argPtr);
/* ASM: after `call [job]` — emit VGA22 delta when AB_JOB_TRACE_VGA22=1. */
void jjffe_job_trace_after_draw(void);

/* ASM: FUNC_001780(mode, sortKey); retAbs = return address */
void jjffe_job_trace_barrier(int mode, uint32_t sortKey, uint32_t retAbs);

/* C++ immediate painter path */
void jjffe_job_trace_cpp_enq(uint32_t sortKey, int meshIndex, uint32_t texInfo,
                             int material, const char* jobType);
/* note: optional extra (e.g. screen verts); NULL → "painter" */
void jjffe_job_trace_cpp_exec(uint32_t sortKey, int meshIndex, uint32_t texInfo,
                              int material, const char* jobType, const char* note);

#ifdef __cplusplus
}
#endif

#endif /* JJFFE_JOB_TRACE_H */
