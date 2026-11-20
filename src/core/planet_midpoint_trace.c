#include "jjffe/planet_midpoint_trace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_mid_trace_enabled = 0;
int g_jjffe_planet_mid_hook_eff5 = 0;
int g_jjffe_planet_mid_hook_branch = 0;
int g_jjffe_planet_mid_hook_lod2 = 0;
int g_jjffe_planet_mid_hook_lod3 = 0;
int g_jjffe_planet_mid_hook_param5 = 0;
int g_jjffe_planet_mid_hook_mix2 = 0;
int g_jjffe_planet_mid_hook_mix3 = 0;
int g_jjffe_planet_mid_hook_term846 = 0;
int g_jjffe_planet_mid_hook_avg = 0;
uint32_t g_jjffe_planet_mid_hook_sortKey = 0;
int g_jjffe_planet_mid_hook_ri2 = -1;
int g_jjffe_planet_mid_hook_ri3 = -1;
int g_jjffe_planet_mid_hook_fn = 855;
int g_jjffe_planet_mid_hook_action = 1;
int g_jjffe_planet_mid_hook_d260a = 0;
int g_jjffe_planet_mid_hook_d260b = 0;
int g_jjffe_planet_mid_hook_tu0 = 0;
int g_jjffe_planet_mid_hook_tu1 = 0;
int g_jjffe_planet_mid_hook_gate = 0;
int g_jjffe_planet_mid_hook_iter = 0;

static const uint8_t* s_876_recs = NULL;

void jjffe_planet_set_876_recs(const void* recs12x48)
{
    s_876_recs = (const uint8_t*)recs12x48;
}

int jjffe_planet_root_index(const void* vertRec)
{
    if (!s_876_recs || !vertRec)
        return -1;
    const intptr_t d = (const uint8_t*)vertRec - s_876_recs;
    if (d < 0 || (d % 0x48) != 0)
        return -1;
    const int i = (int)(d / 0x48);
    return (i >= 0 && i < 12) ? i : -1;
}

void jjffe_planet_mid_hook_set_parent_verts(const void* p2, const void* p3)
{
    g_jjffe_planet_mid_hook_ri2 = jjffe_planet_root_index(p2);
    g_jjffe_planet_mid_hook_ri3 = jjffe_planet_root_index(p3);
}

/* ASM FUNC_001855 ring (legacy counters; samples via jjffe_planet_mid_trace_855_mid). */
int g_jjffe_planet_mid_asm_n = 0;
int g_jjffe_planet_mid_asm_simple = 0;
int g_jjffe_planet_mid_asm_else = 0;
int g_jjffe_planet_mid_asm_eff5_hist[8];
int g_jjffe_planet_mid_asm_samples[4096 * 11];

typedef struct {
    int fn;
    int action;
    int param5;
    int eff5;
    int lod2;
    int lod3;
    int branch;
    int sx;
    int sy;
    int camZ;
    int outLod;
    int outMix;
    int mix2;
    int mix3;
    int hash;
    int diff;
    int term846;
    int avg;
    uint32_t sortKey;
    int midSeq;
    int ri2;
    int ri3;
    int d260a;
    int d260b;
    int tu0;
    int tu1;
    int gate;
    int iter;
} MidSample;

static MidSample s_samples[8192];
static int s_sampleN = 0;
static int s_midSeq = 0;
static int s_fnCreate[896];
static int s_fnSkip[896];
static int s_fnAux[896];
static int s_simple855 = 0;
static int s_else855 = 0;
static FILE* s_fp = NULL;
static char s_path[1024];

/* FUNC_001824 — same as core.cpp (planet_midpoint_trace links without jjffe_core in ffeasm). */
static int mid_hash824(uint32_t param_1)
{
    const uint32_t uVar1 = (param_1 >> 0x1b | param_1 << 5) + param_1;
    return (int)(param_1 + (uVar1 >> 0x10 | uVar1 << 16));
}

static void mid_chain_fill(MidSample* s)
{
    s->mix2 = 0;
    s->mix3 = 0;
    s->hash = 0;
    s->diff = 0;
    s->term846 = 0;
    s->avg = 0;
    s->mix2 = g_jjffe_planet_mid_hook_mix2;
    s->mix3 = g_jjffe_planet_mid_hook_mix3;
    if (s->branch != 1 || s->param5 != 2)
        return;

    const uint32_t xorv = (uint32_t)s->mix2 ^ (uint32_t)s->mix3;
    s->hash = mid_hash824(xorv);
    int d = s->mix2 - s->mix3;
    if (d < 0)
        d = -d;
    s->diff = d;
    s->term846 = g_jjffe_planet_mid_hook_term846;
    s->avg = g_jjffe_planet_mid_hook_avg;
}

static void ensure_open(void)
{
    if (g_jjffe_planet_mid_trace_enabled)
        return;
    const char* env = getenv("AB_1F_MIDPOINT_TRACE");
    if (!env || !env[0])
        return;
    jjffe_planet_mid_trace_open(env);
}

void jjffe_planet_mid_trace_open(const char* path)
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
        fprintf(stderr, "planet_mid_trace: cannot open %s\n", path);
        return;
    }
    s_sampleN = 0;
    s_midSeq = 0;
    s_simple855 = 0;
    s_else855 = 0;
    memset(s_fnCreate, 0, sizeof(s_fnCreate));
    memset(s_fnSkip, 0, sizeof(s_fnSkip));
    memset(s_fnAux, 0, sizeof(s_fnAux));
    g_jjffe_planet_mid_asm_n = 0;
    g_jjffe_planet_mid_asm_simple = 0;
    g_jjffe_planet_mid_asm_else = 0;
    memset(g_jjffe_planet_mid_asm_eff5_hist, 0, sizeof(g_jjffe_planet_mid_asm_eff5_hist));
    g_jjffe_planet_mid_trace_enabled = 1;
    fprintf(stderr, "planet_mid_trace: writing %s\n", path);
}

void jjffe_planet_mid_trace_close(void)
{
    jjffe_planet_mid_trace_flush();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_mid_trace_enabled = 0;
    s_path[0] = '\0';
}

void jjffe_planet_mid_trace_flush(void)
{
    if (!s_fp)
        return;
    const int sampleN = s_sampleN;
    const int simple855 = s_simple855;
    const int else855 = s_else855;
    fprintf(s_fp, "# side=trace fn855_calls=%d simple=%d else=%d samples=%d",
            simple855 + else855, simple855, else855, sampleN);
    for (int fn = 855; fn <= 863; ++fn) {
        if (s_fnCreate[fn] || s_fnSkip[fn] || s_fnAux[fn]) {
            fprintf(s_fp, " fn%d_create=%d fn%d_skip=%d fn%d_aux=%d",
                    fn, s_fnCreate[fn], fn, s_fnSkip[fn], fn, s_fnAux[fn]);
        }
    }
    fprintf(s_fp, "\n");
    fprintf(s_fp,
            "fn\taction\tp5\teff5\tlod2\tlod3\tbranch\tsx\tsy\tcamZ\toutLod\toutMix\t"
            "mix2\tmix3\thash\tdiff\tterm846\tavg\tsortKey\tmidSeq\tri2\tri3\t"
            "d260a\td260b\ttu0\ttu1\tgate\titer\n");
    for (int i = 0; i < sampleN; ++i) {
        const MidSample* s = &s_samples[i];
        fprintf(s_fp,
                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\n",
                s->fn, s->action, s->param5, s->eff5, s->lod2, s->lod3, s->branch,
                s->sx, s->sy, s->camZ, s->outLod, s->outMix,
                s->mix2, s->mix3, s->hash, s->diff, s->term846, s->avg,
                (unsigned)s->sortKey, s->midSeq, s->ri2, s->ri3,
                s->d260a, s->d260b, s->tu0, s->tu1, s->gate, s->iter);
    }
    fflush(s_fp);
}

static void record_vertex(int fn, int action, int branch, int param5, const void* midVert)
{
    if (!g_jjffe_planet_mid_trace_enabled)
        return;
    if (fn >= 0 && fn < (int)(sizeof(s_fnCreate) / sizeof(s_fnCreate[0]))) {
        if (action == 0)
            ++s_fnSkip[fn];
        else if (action == 1)
            ++s_fnCreate[fn];
        else if (action >= 2)
            ++s_fnAux[fn];
    }
    if (fn == 855) {
        if (branch)
            ++s_else855;
        else
            ++s_simple855;
    }
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    MidSample* s = &s_samples[s_sampleN++];
    s->midSeq = s_midSeq++;
    s->sortKey = g_jjffe_planet_mid_hook_sortKey;
    s->ri2 = g_jjffe_planet_mid_hook_ri2;
    s->ri3 = g_jjffe_planet_mid_hook_ri3;
    s->d260a = g_jjffe_planet_mid_hook_d260a;
    s->d260b = g_jjffe_planet_mid_hook_d260b;
    s->tu0 = g_jjffe_planet_mid_hook_tu0;
    s->tu1 = g_jjffe_planet_mid_hook_tu1;
    s->gate = g_jjffe_planet_mid_hook_gate;
    s->iter = g_jjffe_planet_mid_hook_iter;
    s->fn = fn;
    s->action = action;
    s->param5 = param5;
    s->eff5 = g_jjffe_planet_mid_hook_eff5;
    s->lod2 = g_jjffe_planet_mid_hook_lod2;
    s->lod3 = g_jjffe_planet_mid_hook_lod3;
    s->branch = branch;
    s->sx = 0;
    s->sy = 0;
    s->camZ = 0;
    s->outLod = 0;
    s->outMix = 0;
    if (midVert) {
        const uint8_t* vp = (const uint8_t*)midVert;
        s->sx = *(const int16_t*)(vp + 0);
        s->sy = *(const int16_t*)(vp + 2);
        s->camZ = *(const int32_t*)(vp + 0x0c);
        s->outLod = *(const int32_t*)(vp + 0x44);
        s->outMix = *(const int32_t*)(vp + 0x3c);
    }
    mid_chain_fill(s);
}

void jjffe_planet_mid_trace_vertex(int fn, int action, int branch, int param5,
                                   const void* midVert)
{
    ensure_open();
    record_vertex(fn, action, branch, param5, midVert);
}

void jjffe_planet_mid_trace_fn_from_asm(const void* midVert)
{
    ensure_open();
    record_vertex(g_jjffe_planet_mid_hook_fn, g_jjffe_planet_mid_hook_action,
                  g_jjffe_planet_mid_hook_branch, g_jjffe_planet_mid_hook_param5, midVert);
}

void jjffe_planet_mid_trace_855(int lod2, int lod3, int param5, int eff5, int branch,
                               const void* midVert)
{
    g_jjffe_planet_mid_hook_lod2 = lod2;
    g_jjffe_planet_mid_hook_lod3 = lod3;
    g_jjffe_planet_mid_hook_param5 = param5;
    g_jjffe_planet_mid_hook_eff5 = eff5;
    g_jjffe_planet_mid_hook_branch = branch;
    jjffe_planet_mid_trace_vertex(855, 1, branch, param5, midVert);
}

void jjffe_planet_mid_trace_863_slot(int action, int param5, int iter,
                                     int d260a, int d260b, int tu0, int tu1, int gate)
{
    ensure_open();
    g_jjffe_planet_mid_hook_d260a = d260a;
    g_jjffe_planet_mid_hook_d260b = d260b;
    g_jjffe_planet_mid_hook_tu0 = tu0;
    g_jjffe_planet_mid_hook_tu1 = tu1;
    g_jjffe_planet_mid_hook_gate = gate;
    g_jjffe_planet_mid_hook_iter = iter;
    g_jjffe_planet_mid_hook_param5 = param5;
    record_vertex(863, action, gate, param5, NULL);
}

void jjffe_planet_mid_trace_863_slot_asm(void)
{
    ensure_open();
    record_vertex(863, g_jjffe_planet_mid_hook_action, g_jjffe_planet_mid_hook_gate,
                  g_jjffe_planet_mid_hook_param5, NULL);
}

void jjffe_planet_mid_trace_855_mid(const void* midVert)
{
    ensure_open();
    if (!g_jjffe_planet_mid_trace_enabled)
        return;
    record_vertex(855, g_jjffe_planet_mid_hook_action, g_jjffe_planet_mid_hook_branch,
                  g_jjffe_planet_mid_hook_param5, midVert);
}

/* ---- AB_1F_SUBDIV_DUMP (task 4.16) ---- */

static FILE* s_sdFp = NULL;
static int s_sdChecked = 0;
static int s_sdCall = -1;

static FILE* subdiv_fp(void)
{
    if (!s_sdChecked) {
        s_sdChecked = 1;
        const char* env = getenv("AB_1F_SUBDIV_DUMP");
        if (env && env[0])
            s_sdFp = fopen(env, "w");
    }
    return s_sdFp;
}

int jjffe_subdiv_dump_enabled(void)
{
    return subdiv_fp() != NULL;
}

void jjffe_subdiv_dump_frame_begin(void)
{
    if (!subdiv_fp())
        return;
    const char* env = getenv("AB_1F_SUBDIV_DUMP");
    s_sdFp = freopen(env, "w", s_sdFp);
    s_sdCall = -1;
}

static void subdiv_vert(FILE* f, const void* vp)
{
    if (!vp) {
        fprintf(f, "\t-");
        return;
    }
    const uint8_t* v = (const uint8_t*)vp;
    fprintf(f, "\t%d\t%d", (int)*(const int16_t*)v, (int)*(const int16_t*)(v + 2));
    for (int off = 0x04; off <= 0x0c; off += 4)
        fprintf(f, "\t%d", (int)*(const int32_t*)(v + off));
    for (int off = 0x28; off <= 0x30; off += 4)
        fprintf(f, "\t%d", (int)*(const int32_t*)(v + off));
    for (int off = 0x34; off <= 0x3a; off += 2)
        fprintf(f, "\t%d", (int)*(const int16_t*)(v + off));
    for (int off = 0x3c; off <= 0x44; off += 4)
        fprintf(f, "\t%d", (int)*(const int32_t*)(v + off));
}

void jjffe_subdiv_dump_876(const void* recs12x48, const int32_t* p6, const int32_t* p7,
                           int p12, int mode, int seed, int local10, int localC, int d9269)
{
    FILE* f = subdiv_fp();
    if (!f || !recs12x48 || !p6 || !p7)
        return;
    ++s_sdCall;
    fprintf(f, "H\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", s_sdCall, p12, mode, seed,
            local10, localC, d9269, (int)p6[0], (int)p6[1], (int)p6[2]);
    for (int i = 0; i < 9; ++i)
        fprintf(f, "\t%d", (int)p7[i]);
    fprintf(f, "\n");
    for (int i = 0; i < 12; ++i) {
        const uint8_t* rec = (const uint8_t*)recs12x48 + i * 0x48;
        fprintf(f, "R\t%d\t%d\t%llx", s_sdCall, i, (unsigned long long)(uintptr_t)rec);
        subdiv_vert(f, rec);
        fprintf(f, "\n");
    }
}

void jjffe_subdiv_dump_face(int face, const void* v1, const void* v2, const void* v3,
                            int leaf0Type, int leaf1Type)
{
    FILE* f = subdiv_fp();
    if (!f)
        return;
    const void* v[3] = {v1, v2, v3};
    for (int i = 0; i < 3; ++i) {
        fprintf(f, "F\t%d\t%d\t%d\t%d\t%d\t%llx", s_sdCall, face, i, leaf0Type, leaf1Type,
                (unsigned long long)(uintptr_t)v[i]);
        subdiv_vert(f, v[i]);
        fprintf(f, "\n");
    }
}

void jjffe_subdiv_dump_mid(int fnIdx, int depth, int created, const void* edge,
                           const void* p2, const void* p3, const void* p4, const void* mid)
{
    FILE* f = subdiv_fp();
    if (!f)
        return;
    fprintf(f, "M\t%d\t%d\t%d\t%d\t%llx\t%llx\t%llx\t%llx\t%llx", s_sdCall, fnIdx, depth, created,
            (unsigned long long)(uintptr_t)edge, (unsigned long long)(uintptr_t)p2,
            (unsigned long long)(uintptr_t)p3, (unsigned long long)(uintptr_t)p4,
            (unsigned long long)(uintptr_t)mid);
    subdiv_vert(f, mid);
    fprintf(f, "\n");
}
