#include "jjffe/vertex_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_vtrace_enabled = 0;

static FILE* s_fp = NULL;
static int s_seq = 0;
static int s_hdr = 0;

static void ensure_header(void)
{
    if (!s_fp || s_hdr)
        return;
    fputs("kind\tseq\tship\tpose\tidx\tbase\tf0\tf1\tf2\tf3\tf4\tf5\tf6\tf7\tf8\tscrX\tscrY\tvis\tnote\n",
          s_fp);
    fflush(s_fp);
    s_hdr = 1;
}

void jjffe_vtrace_open(const char* path)
{
    jjffe_vtrace_close();
    if (!path || !path[0])
        return;
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "vtrace: cannot open %s\n", path);
        return;
    }
    s_seq = 0;
    s_hdr = 0;
    g_jjffe_vtrace_enabled = 1;
    ensure_header();
    fprintf(stderr, "vtrace: writing %s\n", path);
}

void jjffe_vtrace_close(void)
{
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_vtrace_enabled = 0;
    s_seq = 0;
    s_hdr = 0;
}

static void ensure_open(void)
{
    if (!s_fp) {
        const char* env = getenv("AB_VTRACE");
        if (env && env[0]) {
            /* ship_demo uses AB_VTRACE as a directory of per-pose TSVs;
             * only open when it looks like a file path (has a suffix). */
            const char* slash = strrchr(env, '/');
            const char* base = slash ? slash + 1 : env;
            if (strchr(base, '.') != NULL)
                jjffe_vtrace_open(env);
        }
    }
}

void jjffe_vtrace_frame(int ship, int pose, uint32_t base,
                        const int32_t* rot, const int32_t* pos,
                        int scaleRelated)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();
    char note[128];
    snprintf(note, sizeof(note), "pos=(%d,%d,%d) scale=%d",
             pos ? pos[0] : 0, pos ? pos[1] : 0, pos ? pos[2] : 0,
             scaleRelated);
    fprintf(s_fp, "FRAME\t%d\t%d\t%d\t-1\t0x%08x"
            "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
            "\t-\t-\t-\t%s\n",
            s_seq++, ship, pose, base,
            rot ? rot[0] : 0, rot ? rot[1] : 0, rot ? rot[2] : 0,
            rot ? rot[3] : 0, rot ? rot[4] : 0, rot ? rot[5] : 0,
            rot ? rot[6] : 0, rot ? rot[7] : 0, rot ? rot[8] : 0,
            note);
    fflush(s_fp);
}

/* Debug: count FUNC_001478 calls per model. Temporary. */
static int s_f1478_count = 0;
void jjffe_debug_f1478_enter(void)
{
    if (!g_jjffe_vtrace_enabled) return;
    s_f1478_count++;
}
void jjffe_debug_f1478_reset(void) { s_f1478_count = 0; }
int jjffe_debug_f1478_get(void) { return s_f1478_count; }

/* Wrapper: calls ASM FUNC_001538 to get ObjectStruct pointer.
 * Only available in the ASM build (ffeasm). Returns NULL in C++ build. */
void* jjffe_get_ship_objectstruct(int modelIndex)
{
#ifdef FFEASM_SHIP_DEMO
    extern void* FUNC_001538(int);
    return FUNC_001538(modelIndex);
#else
    (void)modelIndex;
    return NULL;
#endif
}

/* Capture buffer: stores cache data during render before pointer is cleared */
static uint8_t s_captureBuf[256 * 36];
static int s_captureValid = 0;
static uint32_t s_captureBase = 0;

void jjffe_capture_cache(uint32_t base, int nEntries)
{
    if (base == 0 || nEntries <= 0 || nEntries > 256) return;
    const uint8_t* src = (const uint8_t*)(uintptr_t)base;
    memcpy(s_captureBuf, src, nEntries * 36);
    s_captureValid = 1;
    s_captureBase = base;
}

/* Called from ASM FUNC_001774 right before FUNC_001746 resets the arena.
 * Captures the populated vertex cache. */
void jjffe_vtrace_capture(uint32_t base, int nEntries)
{
    jjffe_capture_cache(base, nEntries);
}

int jjffe_capture_is_valid(void) { return s_captureValid; }
uint32_t jjffe_capture_get_base(void) { return s_captureBase; }
const uint8_t* jjffe_capture_get_buf(void) { return s_captureBuf; }
void jjffe_capture_reset(void) { s_captureValid = 0; s_captureBase = 0; }

void jjffe_vtrace_snapshot(uint32_t base, int nEntries)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    if (base == 0) return;
    ensure_header();

    const uint8_t* p = (const uint8_t*)(uintptr_t)base;

    for (int e = 0; e < nEntries; e++) {
        const int32_t* f = (const int32_t*)(p + e * 36);
        /* Skip entries where ALL fields are zero — truly unused. */
        int allZero = 1;
        for (int j = 0; j < 9; j++) { if (f[j] != 0) { allZero = 0; break; } }
        if (allZero) continue;
        int16_t scrX = *(const int16_t*)(p + e * 36 + 0);
        int16_t scrY = *(const int16_t*)(p + e * 36 + 2);
        int vis = (f[3] > 0 && f[3] < 0x10000) ? 1 : 0;
        fprintf(s_fp, "VERT\t%d\t-\t-\t%d\t0x%08x"
                "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
                "\t%d\t%d\t%d\t\n",
                s_seq++, e, base,
                f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8],
                scrX, scrY, vis);
    }
    fflush(s_fp);
}

void jjffe_vtrace_read(uint32_t base, int idx)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();

    const uint8_t* p = (const uint8_t*)(uintptr_t)base;
    const int32_t* f = (const int32_t*)(p + idx * 36);
    int16_t scrX = *(const int16_t*)(p + idx * 36 + 0);
    int16_t scrY = *(const int16_t*)(p + idx * 36 + 2);
    int vis = (f[3] > 0 && f[3] < 0x10000) ? 1 : 0;
    fprintf(s_fp, "VERT\t%d\t-\t-\t%d\t0x%08x"
            "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
            "\t%d\t%d\t%d\tREAD\n",
            s_seq++, idx, base,
            f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8],
            scrX, scrY, vis);
    fflush(s_fp);
}

void jjffe_vtrace_bank(uint32_t newBase, uint32_t prevBase, int shiftEntries)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();
    fprintf(s_fp, "BANK\t%d\t-\t-\t-1\t0x%08x"
            "\t-\t-\t-\t-\t-\t-\t-\t-\t-"
            "\t-\t-\t-\tshift=+%d prev=0x%08x\n",
            s_seq++, newBase, shiftEntries, prevBase);
    fflush(s_fp);
}

void jjffe_vtrace_cpp_frame(int ship, int pose,
                            const int32_t* rot, const int32_t* pos,
                            int scaleRelated)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();
    char note[128];
    snprintf(note, sizeof(note), "pos=(%d,%d,%d) scale=%d cpp",
             pos ? pos[0] : 0, pos ? pos[1] : 0, pos ? pos[2] : 0,
             scaleRelated);
    fprintf(s_fp, "FRAME\t%d\t%d\t%d\t-1\t0x00000000"
            "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
            "\t-\t-\t-\t%s\n",
            s_seq++, ship, pose,
            rot ? rot[0] : 0, rot ? rot[1] : 0, rot ? rot[2] : 0,
            rot ? rot[3] : 0, rot ? rot[4] : 0, rot ? rot[5] : 0,
            rot ? rot[6] : 0, rot ? rot[7] : 0, rot ? rot[8] : 0,
            note);
    fflush(s_fp);
}

void jjffe_vtrace_cpp_vert(int idx,
                           const int32_t* model, const int32_t* world,
                           const int32_t* cam,
                           int16_t scrX, int16_t scrY, int vis)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();
    fprintf(s_fp, "VERT\t%d\t-\t-\t%d\t0x00000000"
            "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
            "\t%d\t%d\t%d\tcpp\n",
            s_seq++, idx,
            model ? model[0] : 0, model ? model[1] : 0, model ? model[2] : 0,
            world ? world[0] : 0, world ? world[1] : 0, world ? world[2] : 0,
            cam ? cam[0] : 0, cam ? cam[1] : 0, cam ? cam[2] : 0,
            scrX, scrY, vis);
    fflush(s_fp);
}

/* ASM-order variant: f0-f8 in ASM cache layout for direct comparison.
 * asm_fields[0] = packed screen (scrY:16 | scrX:16)
 * asm_fields[1-5] = 0 (unused in C++)
 * asm_fields[6] = worldX, asm_fields[7] = worldY, asm_fields[8] = worldZ
 */
void jjffe_vtrace_cpp_vert_asm_order(int idx, const int32_t* asm_fields,
                                     int16_t scrX, int16_t scrY, int vis)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    if (!s_fp) return;
    ensure_header();
    fprintf(s_fp, "VERT\t%d\t-\t-\t%d\t0x00000000"
            "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
            "\t%d\t%d\t%d\tcpp\n",
            s_seq++, idx,
            asm_fields[0], asm_fields[1], asm_fields[2],
            asm_fields[3], asm_fields[4], asm_fields[5],
            asm_fields[6], asm_fields[7], asm_fields[8],
            scrX, scrY, vis);
    fflush(s_fp);
}

/* Debug: dump mirror case vertex data from ASM. Temporary. */
void jjffe_debug_mirror_vert(int idx, int32_t w0, int32_t w1, int32_t w2, int32_t w3,
                              int32_t worldX, int32_t worldY, int32_t worldZ,
                              int16_t scrX, int16_t scrY)
{
    if (!g_jjffe_vtrace_enabled) return;
    ensure_open();
    fprintf(stderr, "ASM_MIRROR[%d] raw=(%d,%d,%d,%d) world=(%d,%d,%d) scr=(%d,%d)\n",
            idx, w0, w1, w2, w3, worldX, worldY, worldZ, scrX, scrY);
}

/* Phase 0 probe — raw hex dump of N entries at base.
 * Prints to stderr so it doesn't pollute the TSV trace.
 * Temporary; delete after layout is confirmed. */
void jjffe_vtrace_probe(uint32_t baseAddr, int nEntries)
{
    ensure_open();
    if (!g_jjffe_vtrace_enabled) return;
    const uint8_t* p = (const uint8_t*)(uintptr_t)baseAddr;
    fprintf(stderr, "VCPROBE base=0x%08x entries=%d stride=36\n", baseAddr, nEntries);
    for (int e = 0; e < nEntries; e++) {
        fprintf(stderr, "  [%3d]", e);
        for (int b = 0; b < 36; b++)
            fprintf(stderr, " %02x", p[e * 36 + b]);
        /* Decode known fields */
        const int32_t* f = (const int32_t*)(p + e * 36);
        int16_t off0 = *(const int16_t*)(p + e * 36 + 0);
        int16_t off2 = *(const int16_t*)(p + e * 36 + 2);
        fprintf(stderr, "  | s=(%d,%d) d=%d gen=%d w=(%d,%d,%d)",
                off0, off2, f[3], f[5], f[6], f[7], f[8]);
        fprintf(stderr, "\n");
    }
}
