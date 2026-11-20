#include "jjffe/planet_cull_trace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_cull_enabled = 0;

int g_jjffe_cull_hook_reason = 0;
int g_jjffe_cull_hook_depth = 0;
int g_jjffe_cull_hook_effDepth = 0;
int g_jjffe_cull_hook_cvar1 = 0;
int g_jjffe_cull_hook_gate = 0;
int g_jjffe_cull_hook_local20 = 0;
int g_jjffe_cull_hook_margin = 0;
int g_jjffe_cull_hook_area48 = 0;
void* g_jjffe_cull_hook_v1 = NULL;
void* g_jjffe_cull_hook_v2 = NULL;
void* g_jjffe_cull_hook_v3 = NULL;

typedef struct {
    int reason;
    int depth;
    int effDepth;
    int cVar1;
    int gate;
    int local20;
    int margin;
    int area48;
    int mix0, mix1, mix2;
    int z0, z1, z2;
    int cx0, cy0, cx1, cy1, cx2, cy2;
    int lod0, lod1, lod2;
    int sx0, sy0, sx1, sy1, sx2, sy2;
} CullSample;

static CullSample s_samples[4096];
static int s_sampleN = 0;
static int s_byReason[32];
static int s_paintN = 0;
static FILE* s_fp = NULL;

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

static int watch_mix(int m)
{
    /* Far + near fork verts (exclusive faces + reconstructed parent corners). */
    static const int w[] = {
        -23366516, -16567464, -353485, 12932394, 36220995, 63924339, 95250570,
        126576801, -114538325, 58254165, 71819994, -57995625, -110232862, 74892572
    };
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); ++i) {
        if (m == w[i])
            return 1;
    }
    return 0;
}

static int parent_trace_on(void)
{
    const char* e = getenv("AB_1F_869_PARENT_TRACE");
    return e && e[0] && e[0] != '0';
}

static void append(int reason, int depth, int effDepth, int cVar1, int gate,
                   int local20, int margin, int area48,
                   const void* v1, const void* v2, const void* v3)
{
    const int mix0 = vert_i32(v1, 0x3c);
    const int mix1 = vert_i32(v2, 0x3c);
    const int mix2 = vert_i32(v3, 0x3c);
    if (parent_trace_on()) {
        if (!watch_mix(mix0) && !watch_mix(mix1) && !watch_mix(mix2))
            return;
    } else if (depth > 3) {
        return;
    }
    if (reason >= 0 && reason < 32)
        ++s_byReason[reason];
    if (reason == 99)
        ++s_paintN;
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    CullSample* s = &s_samples[s_sampleN++];
    s->reason = reason;
    s->depth = depth;
    s->effDepth = effDepth;
    s->cVar1 = cVar1;
    s->gate = gate;
    s->local20 = local20;
    s->margin = margin;
    s->area48 = area48;
    s->mix0 = mix0;
    s->mix1 = mix1;
    s->mix2 = mix2;
    s->z0 = vert_i32(v1, 0x0c);
    s->z1 = vert_i32(v2, 0x0c);
    s->z2 = vert_i32(v3, 0x0c);
    s->cx0 = vert_i32(v1, 0x04);
    s->cy0 = vert_i32(v1, 0x08);
    s->cx1 = vert_i32(v2, 0x04);
    s->cy1 = vert_i32(v2, 0x08);
    s->cx2 = vert_i32(v3, 0x04);
    s->cy2 = vert_i32(v3, 0x08);
    s->lod0 = vert_i32(v1, 0x44);
    s->lod1 = vert_i32(v2, 0x44);
    s->lod2 = vert_i32(v3, 0x44);
    s->sx0 = vert_i16(v1, 0);
    s->sy0 = vert_i16(v1, 2);
    s->sx1 = vert_i16(v2, 0);
    s->sy1 = vert_i16(v2, 2);
    s->sx2 = vert_i16(v3, 0);
    s->sy2 = vert_i16(v3, 2);
}

void jjffe_planet_cull_trace_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_cull_trace: cannot open %s\n", path);
        return;
    }
    s_sampleN = 0;
    s_paintN = 0;
    memset(s_byReason, 0, sizeof(s_byReason));
    g_jjffe_planet_cull_enabled = 1;
    fprintf(stderr, "planet_cull_trace: writing %s\n", path);
}

void jjffe_planet_cull_trace_flush(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp, "i\treason\tdepth\teffDepth\tcVar1\tgate\tlocal20\tmargin\tarea48\t"
                  "mix0\tmix1\tmix2\tz0\tz1\tz2\tcx0\tcy0\tcx1\tcy1\tcx2\tcy2\t"
                  "lod0\tlod1\tlod2\tsx0\tsy0\tsx1\tsy1\tsx2\tsy2\n");
    for (int i = 0; i < s_sampleN; ++i) {
        const CullSample* s = &s_samples[i];
        fprintf(s_fp, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                      "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                i, s->reason, s->depth, s->effDepth, s->cVar1, s->gate,
                s->local20, s->margin, s->area48,
                s->mix0, s->mix1, s->mix2,
                s->z0, s->z1, s->z2,
                s->cx0, s->cy0, s->cx1, s->cy1, s->cx2, s->cy2,
                s->lod0, s->lod1, s->lod2,
                s->sx0, s->sy0, s->sx1, s->sy1, s->sx2, s->sy2);
    }
    fprintf(s_fp, "# n=%d paint=%d side=asm\n", s_sampleN, s_paintN);
    fprintf(s_fp, "# by_reason");
    for (int r = 0; r < 32; ++r) {
        if (s_byReason[r])
            fprintf(s_fp, " r%d=%d", r, s_byReason[r]);
    }
    fprintf(s_fp, "\n");
    int paintByDepth[16] = {0};
    int lod0zero = 0;
    for (int i = 0; i < s_sampleN; ++i) {
        const CullSample* s = &s_samples[i];
        if (s->reason == 99 && s->depth >= 0 && s->depth < 16)
            ++paintByDepth[s->depth];
        if (s->reason == 99 && s->lod0 == 0)
            ++lod0zero;
    }
    fprintf(s_fp, "# paint_by_depth");
    for (int d = 0; d < 16; ++d) {
        if (paintByDepth[d])
            fprintf(s_fp, " d%d=%d", d, paintByDepth[d]);
    }
    fprintf(s_fp, "\n# paint_lod0_zero=%d\n", lod0zero);
    fflush(s_fp);
}

void jjffe_planet_cull_trace_close(void)
{
    jjffe_planet_cull_trace_flush();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_cull_enabled = 0;
}

void jjffe_planet_cull_asm_note(void)
{
    if (!g_jjffe_planet_cull_enabled)
        return;
    append(g_jjffe_cull_hook_reason, g_jjffe_cull_hook_depth,
           g_jjffe_cull_hook_effDepth, g_jjffe_cull_hook_cvar1,
           g_jjffe_cull_hook_gate, g_jjffe_cull_hook_local20,
           g_jjffe_cull_hook_margin, g_jjffe_cull_hook_area48,
           g_jjffe_cull_hook_v1, g_jjffe_cull_hook_v2, g_jjffe_cull_hook_v3);
}
