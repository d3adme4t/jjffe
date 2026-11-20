#include "jjffe/planet_paint_gate_trace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_paint_gate_enabled = 0;
int g_jjffe_planet_paint_gate_asm_n = 0;

int g_jjffe_pg_hook_depth = 0;
int g_jjffe_pg_hook_cvar1 = 0;
int g_jjffe_pg_hook_area48 = 0;
void* g_jjffe_pg_hook_v1 = NULL;
void* g_jjffe_pg_hook_v2 = NULL;
void* g_jjffe_pg_hook_v3 = NULL;

typedef struct {
    int side; /* 0=C++, 1=ASM */
    int depth;
    int iVar4;
    int cVar1;
    int area48;
    int gate;
    int lod0, lod1, lod2;
    int z0, z1, z2;
    int mix0, mix1, mix2;
    int sx0, sy0, sx1, sy1, sx2, sy2;
} PaintGateSample;

static PaintGateSample s_samples[512];
static int s_sampleN = 0;
static int s_cppN = 0;
static int s_asmN = 0;
static int s_cppSubN = 0;
static FILE* s_fp = NULL;
static char s_path[1024];

static int vert_i32(const void* vp, int off)
{
    if (!vp)
        return 0;
    return *(const int32_t*)((const uint8_t*)vp + off);
}

static int16_t vert_i16(const void* vp, int off)
{
    if (!vp)
        return 0;
    return *(const int16_t*)((const uint8_t*)vp + off);
}

static int eff_depth(int depth, int cVar1)
{
    if (cVar1 != 0 && cVar1 != 3)
        return depth - 1;
    return depth;
}

static int infer_gate(int depth, int iVar4, int area48, int lod0, int lod1, int lod2)
{
    if (depth > 0xd)
        return 1;
    if (lod0 < iVar4 && lod1 < iVar4 && lod2 < iVar4)
        return 2;
    if (area48 >= 0 && area48 < 0x96 && iVar4 > 8)
        return 3;
    return 0;
}

static void ensure_open(void)
{
    if (g_jjffe_planet_paint_gate_enabled)
        return;
    const char* env = getenv("AB_1F_869_PAINT_GATE_TRACE");
    if (!env || !env[0])
        return;
    jjffe_planet_paint_gate_trace_open(env);
}

static void append_sample(int side, int depth, int iVar4, int cVar1, int area48, int gate,
                          const void* v1, const void* v2, const void* v3)
{
    if (depth != 2)
        return;
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    PaintGateSample* s = &s_samples[s_sampleN++];
    s->side = side;
    s->depth = depth;
    s->iVar4 = iVar4;
    s->cVar1 = cVar1;
    s->area48 = area48;
    s->gate = gate;
    s->lod0 = vert_i32(v1, 0x44);
    s->lod1 = vert_i32(v2, 0x44);
    s->lod2 = vert_i32(v3, 0x44);
    s->z0 = vert_i32(v1, 0x0c);
    s->z1 = vert_i32(v2, 0x0c);
    s->z2 = vert_i32(v3, 0x0c);
    s->mix0 = vert_i32(v1, 0x3c);
    s->mix1 = vert_i32(v2, 0x3c);
    s->mix2 = vert_i32(v3, 0x3c);
    s->sx0 = vert_i16(v1, 0);
    s->sy0 = vert_i16(v1, 2);
    s->sx1 = vert_i16(v2, 0);
    s->sy1 = vert_i16(v2, 2);
    s->sx2 = vert_i16(v3, 0);
    s->sy2 = vert_i16(v3, 2);
    if (side)
        ++s_asmN;
    else
        ++s_cppN;
}

void jjffe_planet_paint_gate_trace_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = '\0';
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_paint_gate_trace: cannot open %s\n", path);
        return;
    }
    s_sampleN = 0;
    s_cppN = 0;
    s_asmN = 0;
    s_cppSubN = 0;
    g_jjffe_planet_paint_gate_asm_n = 0;
    g_jjffe_planet_paint_gate_enabled = 1;
    fprintf(stderr, "planet_paint_gate_trace: writing %s\n", path);
}

void jjffe_planet_paint_gate_trace_close(void)
{
    jjffe_planet_paint_gate_trace_flush();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_paint_gate_enabled = 0;
    s_path[0] = '\0';
}

void jjffe_planet_paint_gate_trace_flush(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp, "# paint_gate depth=2 cpp_paint=%d cpp_sub=%d asm_paint=%d total=%d\n",
            s_cppN, s_cppSubN, s_asmN, s_sampleN);
    fprintf(s_fp,
            "side\tdepth\tiVar4\tcVar1\tarea48\tgate\t"
            "lod0\tlod1\tlod2\tz0\tz1\tz2\tmix0\tmix1\tmix2\t"
            "sx0\tsy0\tsx1\tsy1\tsx2\tsy2\n");
    for (int i = 0; i < s_sampleN; ++i) {
        const PaintGateSample* s = &s_samples[i];
        fprintf(s_fp,
                "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                s->side ? "asm" : "cpp", s->depth, s->iVar4, s->cVar1, s->area48, s->gate,
                s->lod0, s->lod1, s->lod2, s->z0, s->z1, s->z2,
                s->mix0, s->mix1, s->mix2,
                s->sx0, s->sy0, s->sx1, s->sy1, s->sx2, s->sy2);
    }
    fflush(s_fp);
}

void jjffe_planet_paint_gate_note(int depth, int iVar4, int cVar1, int area48, int gateReason,
                                  const void* v1, const void* v2, const void* v3)
{
    ensure_open();
    if (!g_jjffe_planet_paint_gate_enabled)
        return;
    append_sample(0, depth, iVar4, cVar1, area48, gateReason, v1, v2, v3);
}

void jjffe_planet_paint_gate_subdivide_note(int depth, int iVar4, int cVar1, int area48,
                                            const void* v1, const void* v2, const void* v3)
{
    ensure_open();
    if (!g_jjffe_planet_paint_gate_enabled || depth != 2)
        return;
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    PaintGateSample* s = &s_samples[s_sampleN++];
    s->side = 0;
    s->depth = depth;
    s->iVar4 = iVar4;
    s->cVar1 = cVar1;
    s->area48 = area48;
    s->gate = -1;
    s->lod0 = vert_i32(v1, 0x44);
    s->lod1 = vert_i32(v2, 0x44);
    s->lod2 = vert_i32(v3, 0x44);
    s->z0 = vert_i32(v1, 0x0c);
    s->z1 = vert_i32(v2, 0x0c);
    s->z2 = vert_i32(v3, 0x0c);
    s->mix0 = vert_i32(v1, 0x3c);
    s->mix1 = vert_i32(v2, 0x3c);
    s->mix2 = vert_i32(v3, 0x3c);
    s->sx0 = vert_i16(v1, 0);
    s->sy0 = vert_i16(v1, 2);
    s->sx1 = vert_i16(v2, 0);
    s->sy1 = vert_i16(v2, 2);
    s->sx2 = vert_i16(v3, 0);
    s->sy2 = vert_i16(v3, 2);
    ++s_cppSubN;
}

void jjffe_planet_paint_gate_asm_note(void)
{
    ensure_open();
    if (!g_jjffe_planet_paint_gate_enabled)
        return;
    const int depth = g_jjffe_pg_hook_depth;
    const int cVar1 = g_jjffe_pg_hook_cvar1;
    const int area48 = g_jjffe_pg_hook_area48;
    const int iVar4 = eff_depth(depth, cVar1);
    const int lod0 = vert_i32(g_jjffe_pg_hook_v1, 0x44);
    const int lod1 = vert_i32(g_jjffe_pg_hook_v2, 0x44);
    const int lod2 = vert_i32(g_jjffe_pg_hook_v3, 0x44);
    const int gate = infer_gate(depth, iVar4, area48, lod0, lod1, lod2);
    append_sample(1, depth, iVar4, cVar1, area48, gate,
                    g_jjffe_pg_hook_v1, g_jjffe_pg_hook_v2, g_jjffe_pg_hook_v3);
    ++g_jjffe_planet_paint_gate_asm_n;
}
