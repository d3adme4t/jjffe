#ifndef JJFFE_VERTEX_TRACE_H
#define JJFFE_VERTEX_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vertex cache trace — captures ASM DATA_009200 and C++ s_vertexCache
 * for differential comparison. Format: TSV, one event per line.
 *
 * Record types:
 *   FRAME — once per (ship, pose) at cache reset
 *   VERT  — one per populated cache entry (snapshot or read)
 *   BANK  — on op 0x1E base pointer shift
 *
 * Enable: jjffe_vtrace_open(path) or env AB_VTRACE=/path/to/file.tsv
 */

extern int g_jjffe_vtrace_enabled;

void jjffe_vtrace_open(const char* path);
void jjffe_vtrace_close(void);

/* H1 — ASM: cache reset / C++: VertexCacheReset.
 * rot = 9 int32 (shipRot), pos = 3 int32 (shipPos). */
void jjffe_vtrace_frame(int ship, int pose, uint32_t base,
                        const int32_t* rot, const int32_t* pos,
                        int scaleRelated);

/* H2 — ASM: whole-cache snapshot after mesh interpreter.
 * Reads nEntries × 36 bytes starting at base. */
void jjffe_vtrace_snapshot(uint32_t base, int nEntries);

/* H3 — ASM: FUNC_001699 PointSource cache read. */
void jjffe_vtrace_read(uint32_t base, int idx);

/* H4 — ASM: op 0x1E bank shift. */
void jjffe_vtrace_bank(uint32_t newBase, uint32_t prevBase, int shiftEntries);

/* C++ mirror hooks — same file format. */
void jjffe_vtrace_cpp_frame(int ship, int pose,
                            const int32_t* rot, const int32_t* pos,
                            int scaleRelated);
void jjffe_vtrace_cpp_vert(int idx,
                           const int32_t* model, const int32_t* world,
                           const int32_t* cam,
                           int16_t scrX, int16_t scrY, int vis);
/* ASM-order variant: f0-f8 in ASM cache layout for direct comparison. */
void jjffe_vtrace_cpp_vert_asm_order(int idx, const int32_t* asm_fields,
                                     int16_t scrX, int16_t scrY, int vis);

/* Phase 0 probe — raw hex dump of N entries. Temporary; delete after layout
 * is confirmed. */
void jjffe_vtrace_probe(uint32_t baseAddr, int nEntries);

#ifdef __cplusplus
}
#endif

#endif /* JJFFE_VERTEX_TRACE_H */
