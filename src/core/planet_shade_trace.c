#include "jjffe/planet_shade_trace.h"
#include "jjffe/planet_dpal_probe.h"
#include "jjffe/planet_midpoint_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char jjffe_planet_side_asm840[] = "asm840";
const char jjffe_planet_site_838_entry[] = "838_entry";
const char jjffe_planet_site_877_exit57[] = "877_exit57";
const char jjffe_planet_site_877_exit58[] = "877_exit58";
const char jjffe_planet_site_877_exit[] = "877_exit";

/* AB_1F_879_ASM_DEBUG=1 — per FUNC_001879 call: local8, baseShade, local_c,
 * shadeTable gradient, and the six verts. Mirrors the C++ AB_1F_879_DEBUG. */
void jjffe_planet_879_asm_debug(int32_t local8, uint32_t baseShade, uint32_t local_c,
                                const void* shadeTable,
                                const void* p2, const void* p3, const void* p4,
                                const void* p5, const void* p6, const void* p7)
{
    static int s_n = 0;
    if (!getenv("AB_1F_879_ASM_DEBUG") || s_n >= 40)
        return;
    const uint8_t* tb = (const uint8_t*)shadeTable;
    const int16_t* v2 = (const int16_t*)p2;
    const int16_t* v3 = (const int16_t*)p3;
    const int16_t* v4 = (const int16_t*)p4;
    const int16_t* v5 = (const int16_t*)p5;
    const int16_t* v6 = (const int16_t*)p6;
    const int16_t* v7 = (const int16_t*)p7;
    fprintf(stderr,
            "879asm n=%d local8=0x%x baseShade=0x%x local_c=0x%x "
            "tbl=[%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x] "
            "p2=(%d,%d) p3=(%d,%d) p4=(%d,%d) p5=(%d,%d) p6=(%d,%d) p7=(%d,%d)\n",
            s_n, (unsigned)local8, (unsigned)baseShade, (unsigned)local_c,
            tb[0], tb[1], tb[2],
            tb[4], tb[5], tb[6],
            tb[8], tb[9], tb[10],
            tb[12], tb[13], tb[14],
            tb[16], tb[17], tb[18],
            tb[20], tb[21], tb[22],
            tb[24], tb[25], tb[26],
            (int)v2[0], (int)v2[1], (int)v3[0], (int)v3[1],
            (int)v4[0], (int)v4[1], (int)v5[0], (int)v5[1],
            (int)v6[0], (int)v6[1], (int)v7[0], (int)v7[1]);
    ++s_n;
}

static void pack838_refresh_enabled(void);
static void operand838_refresh_enabled(void);
static void d265_refresh_enabled(void);

int g_jjffe_planet_840_gate_enabled = 0;

static void gate840_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_840_GATE_TRACE");
    g_jjffe_planet_840_gate_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void gate840_init_from_env(void)
{
    gate840_refresh_enabled();
}
#endif

typedef struct {
    int n;
    int negUvar7;
    int cvar1Hist[4];
    int32_t firstLx, firstLy, firstLz;
} Gate840Session;

static FILE* g_gate840Fp = NULL;
static Gate840Session g_gate840Asm;
static Gate840Session g_gate840Cpp;

typedef struct Shade807Session {
    int jobs;
    int texSamples;
    int fillSamples;
    int texHist[256];
    int fillHist[256];
} Shade807Session;

static struct {
    int active;
    int logDetail;
    char side[12];
    uint32_t sortKey;
    uint8_t pre16[16];
    int texHist[256];
    int fillHist[256];
    int texSamples;
    int fillSamples;
    int maxTex;
    int seqAsm;
    int seqCpp;
} g_shade807;

static Shade807Session g_sessionAsm;
static Shade807Session g_sessionCpp;

static int shade_seq_for_side(const char* side)
{
    if (!side)
        return 0;
    return (side[0] == 'a') ? g_shade807.seqAsm++ : g_shade807.seqCpp++;
}

static int shade_seq_cap(const char* side)
{
    return (side && side[0] == 'a') ? g_shade807.seqAsm : g_shade807.seqCpp;
}

static Shade807Session* session_for_side(const char* side)
{
    if (!side)
        return NULL;
    return (side[0] == 'a') ? &g_sessionAsm : &g_sessionCpp;
}

static int count_distinct(const int* hist)
{
    int n = 0;
    for (int i = 0; i < 256; ++i) {
        if (hist[i])
            n++;
    }
    return n;
}

static int max_index(const int* hist)
{
    for (int i = 255; i >= 0; --i) {
        if (hist[i])
            return i;
    }
    return -1;
}

static void print_hist_band(const int* hist, int from, int to)
{
    fprintf(stderr, "[");
    for (int i = from; i <= to; ++i) {
        if (i > from)
            fprintf(stderr, ",");
        fprintf(stderr, "%d", hist[i]);
    }
    fprintf(stderr, "]");
}

static void merge_session(Shade807Session* session)
{
    if (!session)
        return;
    session->jobs++;
    session->texSamples += g_shade807.texSamples;
    session->fillSamples += g_shade807.fillSamples;
    for (int i = 0; i < 256; ++i) {
        session->texHist[i] += g_shade807.texHist[i];
        session->fillHist[i] += g_shade807.fillHist[i];
    }
}

void jjffe_planet_shade807_begin(const void* job, const char* side, const uint8_t* palPre16)
{
    if (!getenv("AB_1F_SHADE_STRIP_TRACE") || !job || !side)
        return;

    const uint8_t* p = (const uint8_t*)job;
    g_shade807.active = 1;
    g_shade807.logDetail = (shade_seq_cap(side) < 16);
    strncpy(g_shade807.side, side, sizeof(g_shade807.side) - 1);
    g_shade807.side[sizeof(g_shade807.side) - 1] = '\0';
    g_shade807.sortKey = ((uintptr_t)p >= 8u) ? *(const uint32_t*)(p - 8) : 0u;
    memset(g_shade807.texHist, 0, sizeof(g_shade807.texHist));
    memset(g_shade807.fillHist, 0, sizeof(g_shade807.fillHist));
    g_shade807.texSamples = 0;
    g_shade807.fillSamples = 0;
    g_shade807.maxTex = -1;
    if (palPre16)
        memcpy(g_shade807.pre16, palPre16, 16);
    else
        memset(g_shade807.pre16, 0, 16);
}

void jjffe_planet_tex_trace_note(uint8_t texel)
{
    if (!g_shade807.active)
        return;
    g_shade807.texHist[texel]++;
    g_shade807.texSamples++;
    if ((int)texel > g_shade807.maxTex)
        g_shade807.maxTex = (int)texel;
}

void jjffe_planet_fill_trace_note(uint8_t palIndex)
{
    if (!g_shade807.active)
        return;
    g_shade807.fillHist[palIndex]++;
    g_shade807.fillSamples++;
}

void jjffe_planet_shade807_finish(const void* job, const char* side, const uint8_t* palPost16)
{
    if (!getenv("AB_1F_SHADE_STRIP_TRACE") || !job || !side || !palPost16)
        return;
    if (!g_shade807.active || strcmp(g_shade807.side, side) != 0)
        return;

    const uint8_t* p = (const uint8_t*)job;
    const int16_t* vv = (const int16_t*)(p + 4);
    const int seq = shade_seq_for_side(side);
    const int fillMax = max_index(g_shade807.fillHist);
    const int fillDistinct = count_distinct(g_shade807.fillHist);

    merge_session(session_for_side(side));

    if (g_shade807.logDetail) {
        fprintf(stderr,
                "SHADE807 %s seq=%d sortKey=%u verts=(%d,%d)(%d,%d)(%d,%d)"
                " texSamples=%d texMax=%d fillSamples=%d fillDistinct=%d fillMax=%d",
                side, seq, (unsigned)g_shade807.sortKey,
                (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3], (int)vv[4], (int)vv[5],
                g_shade807.texSamples, g_shade807.maxTex,
                g_shade807.fillSamples, fillDistinct, fillMax);
        fprintf(stderr, " pre252=");
        for (int i = 0; i < 16; ++i)
            fprintf(stderr, "%s%u", i ? "," : "", (unsigned)g_shade807.pre16[i]);
        fprintf(stderr, " post252=");
        for (int i = 0; i < 16; ++i)
            fprintf(stderr, "%s%u", i ? "," : "", (unsigned)palPost16[i]);
        fprintf(stderr, " texHist0_6=");
        print_hist_band(g_shade807.texHist, 0, 6);
        if (g_shade807.maxTex > 6) {
            fprintf(stderr, " texHist7_15=");
            print_hist_band(g_shade807.texHist, 7, 15);
        }
        fprintf(stderr, " fillHist0_15=");
        print_hist_band(g_shade807.fillHist, 0, 15);
        fprintf(stderr, "\n");
    }
    g_shade807.active = 0;
}

static FILE* g_807palFp = NULL;

static void pal807_open_if_needed(void)
{
    if (g_807palFp)
        return;
    const char* env = getenv("AB_1F_807_PAL_TRACE");
    if (!env || !env[0])
        return;
    g_807palFp = fopen(env, "a");
    if (!g_807palFp)
        return;
    if (ftell(g_807palFp) == 0) {
        fprintf(g_807palFp,
                "sortKey\tside\trgb0\trgb1\trgb2\trgb3\trgb4\trgb5\trgb6"
                "\tpal0\tpal1\tpal2\tpal3\tpal4\tpal5\tpal6\n");
    }
}

void jjffe_planet_shade807_pal_trace(const void* job, const char* side, const uint8_t* shadePal7)
{
    if (!getenv("AB_1F_807_PAL_TRACE") || !job || !side || !shadePal7)
        return;

    pal807_open_if_needed();
    if (!g_807palFp)
        return;

    const uint8_t* p = (const uint8_t*)job;
    const uint32_t sortKey =
        ((uintptr_t)p >= 8u) ? *(const uint32_t*)(p - 8) : 0u;

    fprintf(g_807palFp, "%u\t%s", (unsigned)sortKey, side);
    for (int i = 0; i < 7; ++i) {
        const uint16_t rgb =
            (uint16_t)(*(const uint32_t*)(p + 0x14 + i * 4) & 0xffffu);
        fprintf(g_807palFp, "\t%u", (unsigned)rgb);
    }
    for (int i = 0; i < 7; ++i)
        fprintf(g_807palFp, "\t%u", (unsigned)shadePal7[i]);
    fprintf(g_807palFp, "\n");
    fflush(g_807palFp);
}

static void print_session(const char* side, const Shade807Session* session)
{
    if (!session || session->jobs == 0)
        return;

    const int fillDistinct = count_distinct(session->fillHist);
    const int fillMax = max_index(session->fillHist);
    int texMax = -1;
    for (int i = 255; i >= 0; --i) {
        if (session->texHist[i]) {
            texMax = i;
            break;
        }
    }

    fprintf(stderr,
            "FILL807_SESSION %s jobs=%d texSamples=%d texMax=%d"
            " fillSamples=%d fillDistinct=%d fillMax=%d",
            side, session->jobs, session->texSamples, texMax,
            session->fillSamples, fillDistinct, fillMax);
    fprintf(stderr, " fillHist0_31=");
    print_hist_band(session->fillHist, 0, 31);
    fprintf(stderr, " nonzeroFill=");
    int first = 1;
    for (int i = 0; i < 256; ++i) {
        if (!session->fillHist[i])
            continue;
        fprintf(stderr, "%s%d:%d", first ? "" : ",", i, session->fillHist[i]);
        first = 0;
    }
    fprintf(stderr, "\n");
}

void jjffe_planet_shade807_session_report(void)
{
    if (!getenv("AB_1F_SHADE_STRIP_TRACE"))
        return;
    print_session("asm807", &g_sessionAsm);
    print_session("cpp807", &g_sessionCpp);
}

#if defined(FFEASM_SHIP_DEMO)
void jjffe_planet_shade807_begin_asm(const void* job, const uint8_t* pal252)
{
    jjffe_planet_shade807_begin(job, "asm807", pal252);
}

void jjffe_planet_shade807_finish_asm(const void* job, const uint8_t* pal252)
{
    jjffe_planet_shade807_pal_trace(job, "asm807", pal252);
    jjffe_planet_shade807_finish(job, "asm807", pal252);
}
#endif

void jjffe_planet_840_trace_emit(const char* side,
                                 int ri2,
                                 int ri3,
                                 int local14b,
                                 const uint32_t* tableBase,
                                 const uint32_t* shades7)
{
    const char* env = getenv("AB_1F_840_TRACE");
    if (!env || !env[0] || !side || !shades7)
        return;

    static int s_seqAsm = 0;
    static int s_seqCpp = 0;
    int* seq = (side[0] == 'a') ? &s_seqAsm : &s_seqCpp;
    if (*seq >= 16)
        return;

    fprintf(stderr,
            "TRACE840 %s seq=%d ri2=%d ri3=%d local14b=%d table=%p",
            side, *seq, ri2, ri3, local14b, (const void*)tableBase);
    for (int i = 0; i < 7; ++i)
        fprintf(stderr, " shade%d=0x%04x", i, (unsigned)(shades7[i] & 0xffffu));
    fprintf(stderr, "\n");
    (*seq)++;
}

static Gate840Session* gate840_session_for_side(const char* side)
{
    if (!side)
        return NULL;
    return (side[0] == 'a') ? &g_gate840Asm : &g_gate840Cpp;
}

static void gate840_open_if_needed(void)
{
    if (g_gate840Fp)
        return;
    const char* env = getenv("AB_1F_840_GATE_TRACE");
    if (!env || !env[0])
        return;
    g_gate840Fp = fopen(env, "w");
    if (!g_gate840Fp)
        return;
    fprintf(g_gate840Fp,
            "seq\tside\tsortKey\tvx\tvy\tvz\tmetric_raw\tgate_num\td263\td267\t"
            "cvar1\tivar3\tmix0\tmix1\tmix2\n");
}

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
                                 int32_t mix2)
{
    if (!getenv("AB_1F_840_GATE_TRACE") || !side)
        return;

    gate840_refresh_enabled();

    gate840_open_if_needed();
    Gate840Session* session = gate840_session_for_side(side);
    if (session) {
        session->n++;
        if (cvar1 >= 0 && cvar1 < 4)
            session->cvar1Hist[cvar1]++;
    }

    if (!g_gate840Fp)
        return;

    static int s_seqAsm = 0;
    static int s_seqCpp = 0;
    int seq = (side[0] == 'a') ? s_seqAsm++ : s_seqCpp++;
    fprintf(g_gate840Fp,
            "%d\t%s\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            seq, side, (unsigned)sort_key, (int)vx, (int)vy, (int)vz,
            (int)metric_raw, (int)gate_num, (int)d263, (int)d267, cvar1, ivar3,
            (int)mix0, (int)mix1, (int)mix2);
}

static void gate840_print_session(const char* label, const Gate840Session* session)
{
    if (!session || session->n == 0)
        return;
    fprintf(stderr,
            "GATE840_SESSION %s jobs=%d negUvar7=%d light=(%d,%d,%d) cvar1={0:%d,1:%d,2:%d,3:%d}\n",
            label, session->n, session->negUvar7,
            (int)session->firstLx, (int)session->firstLy, (int)session->firstLz,
            session->cvar1Hist[0], session->cvar1Hist[1],
            session->cvar1Hist[2], session->cvar1Hist[3]);
}

void jjffe_planet_840_gate_trace_close(void)
{
    if (!getenv("AB_1F_840_GATE_TRACE"))
        return;
    gate840_print_session("asm840", &g_gate840Asm);
    gate840_print_session("cpp840", &g_gate840Cpp);
    if (g_gate840Fp) {
        fclose(g_gate840Fp);
        g_gate840Fp = NULL;
    }
}

void jjffe_planet_840_gate_trace_frame_begin(void)
{
    const char* last = getenv("AB_1F_840_GATE_TRACE_LAST");
    if (!last || !last[0] || !g_gate840Fp)
        return;
    fclose(g_gate840Fp);
    g_gate840Fp = NULL;
}

void jjffe_planet_840_trace_emit_v(const char* side,
                                   const uint32_t* shades7,
                                   const uint32_t* tableBase,
                                   int local14b,
                                   const void* vert2,
                                   const void* vert3)
{
    const int ri2 = jjffe_planet_root_index(vert2);
    const int ri3 = jjffe_planet_root_index(vert3);
    jjffe_planet_840_trace_emit(side, ri2, ri3, local14b, tableBase, shades7);
}

int g_jjffe_planet_local14b_enabled = 0;

static FILE* g_local14bFp = NULL;
static int g_local14bHistAsm[32];
static int g_local14bHistCpp[32];
static int g_local14bNegMixAsm = 0;
static int g_local14bNegMixCpp = 0;

static void local14b_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_LOCAL14B_TRACE");
    g_jjffe_planet_local14b_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void local14b_init_from_env(void)
{
    local14b_refresh_enabled();
}
#endif

static void local14b_open_if_needed(void)
{
    if (g_local14bFp)
        return;
    const char* env = getenv("AB_1F_LOCAL14B_TRACE");
    if (!env || !env[0])
        return;
    g_local14bFp = fopen(env, "w");
    if (!g_local14bFp)
        return;
    fprintf(g_local14bFp,
            "seq\tside\tsortKey\tlocal14b\tmixSum\td0\td1\td2\ttable0\tri2\tri3\n");
}

void jjffe_planet_local14b_trace_emit(const char* side,
                                      uint32_t sort_key,
                                      int local14b,
                                      int32_t d0,
                                      int32_t d1,
                                      int32_t d2,
                                      uint32_t table0)
{
    jjffe_planet_local14b_trace_emit_v(side, sort_key, local14b, d0, d1, d2, table0, NULL, NULL);
}

void jjffe_planet_local14b_trace_emit_v(const char* side,
                                         uint32_t sort_key,
                                         int local14b,
                                         int32_t d0,
                                         int32_t d1,
                                         int32_t d2,
                                         uint32_t table0,
                                         const void* vert2,
                                         const void* vert3)
{
    if (!getenv("AB_1F_LOCAL14B_TRACE") || !side)
        return;

    local14b_refresh_enabled();
    local14b_open_if_needed();

    const int32_t mix_sum = d0 + d1 + d2;
    int* hist = (side[0] == 'a') ? g_local14bHistAsm : g_local14bHistCpp;
    if (local14b >= 0 && local14b < 32)
        hist[local14b]++;
    if (mix_sum < 0) {
        if (side[0] == 'a')
            g_local14bNegMixAsm++;
        else
            g_local14bNegMixCpp++;
    }

    if (!g_local14bFp)
        return;

    static int s_seqAsm = 0;
    static int s_seqCpp = 0;
    const int seq = (side[0] == 'a') ? s_seqAsm++ : s_seqCpp++;
    const int ri2 = jjffe_planet_root_index(vert2);
    const int ri3 = jjffe_planet_root_index(vert3);
    fprintf(g_local14bFp,
            "%d\t%s\t%u\t%d\t%d\t%d\t%d\t%d\t0x%08x\t%d\t%d\n",
            seq, side, (unsigned)sort_key, local14b, (int)mix_sum,
            (int)d0, (int)d1, (int)d2, (unsigned)table0, ri2, ri3);
}

static void local14b_print_hist(const char* label, const int* hist, int neg_mix)
{
    int total = 0;
    for (int i = 0; i < 32; ++i)
        total += hist[i];
    if (total == 0)
        return;
    fprintf(stderr, "LOCAL14B_SESSION %s n=%d negMix=%d hist={", label, total, neg_mix);
    int first = 1;
    for (int i = 0; i < 32; ++i) {
        if (!hist[i])
            continue;
        if (!first)
            fprintf(stderr, ",");
        fprintf(stderr, "%d:%d", i, hist[i]);
        first = 0;
    }
    fprintf(stderr, "}\n");
}

void jjffe_planet_local14b_trace_close(void)
{
    if (!getenv("AB_1F_LOCAL14B_TRACE"))
        return;
    local14b_print_hist("asm840", g_local14bHistAsm, g_local14bNegMixAsm);
    local14b_print_hist("cpp840", g_local14bHistCpp, g_local14bNegMixCpp);
    if (g_local14bFp) {
        fclose(g_local14bFp);
        g_local14bFp = NULL;
    }
}

int g_jjffe_planet_807_strip_enabled = 0;

typedef struct {
    int armed;
    char side[12];
    uint32_t sort_key;
    int cvar1;
    uint32_t draw_tex;
    int table_id;
    const uint32_t* table_base;
    int ivar3;
    int32_t local8;
    uint32_t d265;
} Planet807StripArm;

static Planet807StripArm g_807stripArm;

typedef struct {
    int n;
    int flat;
    int zero;
    int cvar1_hist[4];
    int table846;
    int table847;
    int table849;
} Strip807Session;

static FILE* g_807stripFp = NULL;
static Strip807Session g_807stripAsm;
static Strip807Session g_807stripCpp;

static void strip807_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_807_STRIP_TRACE");
    g_jjffe_planet_807_strip_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void strip807_init_from_env(void)
{
    strip807_refresh_enabled();
}
#endif

static void strip807_open_if_needed(void)
{
    if (g_807stripFp)
        return;
    const char* env = getenv("AB_1F_807_STRIP_TRACE");
    if (!env || !env[0])
        return;
    g_807stripFp = fopen(env, "w");
    if (!g_807stripFp)
        return;
    fprintf(g_807stripFp,
            "seq\tside\tsortKey\tcvar1\tdrawTex\ttableId\ttableBase\ttable0"
            "\tivar3\tlocal8\td265\tiVar2"
            "\tt0\tt1\tt2\tt3\tt4\tt5\tt6"
            "\tsum0\tsum1\tsum2\tsum3\tsum4\tsum5\tsum6"
            "\trem0\trem1\trem2\trem3\trem4\trem5\trem6"
            "\traw0\traw1\traw2\traw3\traw4\traw5\traw6"
            "\tin0\tin1\tin2\tin3\tin4\tin5\tin6"
            "\tpr0\tpr1\tpr2\tpr3\tpr4\tpr5\tpr6"
            "\ts0\ts1\ts2\ts3\ts4\ts5\ts6\n");
}

static uint32_t strip838_blend_sum(int ivar3,
                                   int32_t local8,
                                   uint32_t d265,
                                   const uint32_t* tbl,
                                   int idx)
{
    if (!tbl || idx < 0 || idx >= 7)
        return 0u;
    const uint32_t t = tbl[idx] & 0xffffffu;
    if (ivar3 == 0)
        return (t + (uint32_t)local8) & 0xffffffu;
    const uint32_t d265m = d265 & 0xffffu;
    const int32_t blended =
        (int32_t)tbl[idx] * (8 - ivar3) + local8 * 8 + (int32_t)(d265m * (uint32_t)ivar3);
    return (uint32_t)blended;
}

static uint32_t strip838_dpal_input(int ivar3,
                                    int32_t local8,
                                    uint32_t d265,
                                    const uint32_t* tbl,
                                    int idx)
{
    if (!tbl || idx < 0 || idx >= 7)
        return 0u;
    if (ivar3 == 0)
        return (tbl[idx] + (uint32_t)local8) & 0xffffffu;
    const uint32_t d265m = d265 & 0xffffu;
    const int32_t blended =
        (int32_t)tbl[idx] * (8 - ivar3) + local8 * 8 +
        (int32_t)(d265m * (uint32_t)ivar3);
    return ((uint32_t)(blended >> 3)) & 0xf0f0fu;
}

static Strip807Session* strip807_session_for_side(const char* side)
{
    if (!side)
        return NULL;
    return (side[0] == 'a') ? &g_807stripAsm : &g_807stripCpp;
}

static int strip807_is_flat(const uint32_t* strip7)
{
    if (!strip7)
        return 0;
    const uint32_t v0 = strip7[0] & 0xffffu;
    for (int i = 1; i < 7; ++i) {
        if ((strip7[i] & 0xffffu) != v0)
            return 0;
    }
    return 1;
}

static int strip807_is_zero(const uint32_t* strip7)
{
    if (!strip7)
        return 0;
    for (int i = 0; i < 7; ++i) {
        if ((strip7[i] & 0xffffu) != 0)
            return 0;
    }
    return 1;
}

void jjffe_planet_807_strip_trace_arm_v(const char* side,
                                        uint32_t sort_key,
                                        int cvar1,
                                        uint32_t draw_tex,
                                        int table_id,
                                        const uint32_t* table_base,
                                        int ivar3,
                                        int32_t local8,
                                        uint32_t d265)
{
    if (!side)
        return;

    strip807_refresh_enabled();
    pack838_refresh_enabled();
    operand838_refresh_enabled();
    d265_refresh_enabled();
    g_807stripArm.armed = 1;
    strncpy(g_807stripArm.side, side, sizeof(g_807stripArm.side) - 1);
    g_807stripArm.side[sizeof(g_807stripArm.side) - 1] = '\0';
    g_807stripArm.sort_key = sort_key;
    g_807stripArm.cvar1 = cvar1;
    g_807stripArm.draw_tex = draw_tex;
    g_807stripArm.table_id = table_id;
    g_807stripArm.table_base = table_base;
    g_807stripArm.ivar3 = ivar3;
    g_807stripArm.local8 = local8;
    g_807stripArm.d265 = d265 & 0xffffu;
}

void jjffe_planet_807_strip_trace_finish(const uint32_t* strip7)
{
    if (!getenv("AB_1F_807_STRIP_TRACE") || !strip7 || !g_807stripArm.armed)
        return;

    strip807_open_if_needed();

    const char* side = g_807stripArm.side;
    Strip807Session* session = strip807_session_for_side(side);
    if (session) {
        session->n++;
        if (strip807_is_flat(strip7))
            session->flat++;
        if (strip807_is_zero(strip7))
            session->zero++;
        if (g_807stripArm.cvar1 >= 0 && g_807stripArm.cvar1 < 4)
            session->cvar1_hist[g_807stripArm.cvar1]++;
        if (g_807stripArm.table_id == 846)
            session->table846++;
        else if (g_807stripArm.table_id == 847)
            session->table847++;
        else if (g_807stripArm.table_id == 849)
            session->table849++;
    }

    if (g_807stripFp) {
        static int s_seqAsm = 0;
        static int s_seqCpp = 0;
        const int seq = (side[0] == 'a') ? s_seqAsm++ : s_seqCpp++;
        const uint32_t table0 =
            g_807stripArm.table_base ? (g_807stripArm.table_base[0] & 0xffffffu) : 0u;
        const int ivar3 = g_807stripArm.ivar3;
        const int32_t local8 = g_807stripArm.local8;
        const uint32_t d265 = g_807stripArm.d265;
        const uint32_t iVar2 =
            (ivar3 != 0) ? (d265 * (uint32_t)ivar3) : 0u;
        fprintf(g_807stripFp,
                "%d\t%s\t%u\t%d\t%u\t%d\t%p\t0x%06x\t%d\t%d\t0x%04x\t%u",
                seq, side, (unsigned)g_807stripArm.sort_key, g_807stripArm.cvar1,
                (unsigned)g_807stripArm.draw_tex, g_807stripArm.table_id,
                (const void*)g_807stripArm.table_base, (unsigned)table0,
                ivar3, (int)local8, (unsigned)d265, (unsigned)iVar2);
        uint32_t probe_in[7];
        uint32_t probe_out[7];
        uint32_t blend_sum[7];
        for (int i = 0; i < 7; ++i) {
            const uint32_t t = g_807stripArm.table_base
                ? (g_807stripArm.table_base[i] & 0xffffffu)
                : 0u;
            fprintf(g_807stripFp, "\t0x%06x", (unsigned)t);
            blend_sum[i] = strip838_blend_sum(ivar3, local8, d265,
                                              g_807stripArm.table_base, i);
            probe_in[i] = strip838_dpal_input(ivar3, local8, d265,
                                              g_807stripArm.table_base, i);
            probe_out[i] = jjffe_planet_dpal32_probe(probe_in[i]);
        }
        for (int i = 0; i < 7; ++i)
            fprintf(g_807stripFp, "\t%u", (unsigned)blend_sum[i]);
        for (int i = 0; i < 7; ++i)
            fprintf(g_807stripFp, "\t%u", (unsigned)(blend_sum[i] & 7u));
        for (int i = 0; i < 7; ++i)
            fprintf(g_807stripFp, "\t0x%04x", (unsigned)(strip7[i] & 0xffffu));
        for (int i = 0; i < 7; ++i)
            fprintf(g_807stripFp, "\t0x%06x", (unsigned)probe_in[i]);
        for (int i = 0; i < 7; ++i)
            fprintf(g_807stripFp, "\t0x%04x", (unsigned)(probe_out[i] & 0xffffu));
        for (int i = 0; i < 7; ++i) {
            uint32_t s = strip7[i] & 0xffffu;
            const uint32_t pr = probe_out[i] & 0xffffu;
            if (s > 0xfffu || s != pr)
                s = pr;
            fprintf(g_807stripFp, "\t0x%04x", (unsigned)s);
        }
        fputc('\n', g_807stripFp);
    }

    g_807stripArm.armed = 0;
}

static void strip807_print_session(const char* label, const Strip807Session* session)
{
    if (!session || session->n == 0)
        return;
    fprintf(stderr,
            "807STRIP_SESSION %s n=%d flat=%d zero=%d"
            " cvar1={0:%d,1:%d,2:%d,3:%d}"
            " table={846:%d,847:%d,849:%d}\n",
            label, session->n, session->flat, session->zero,
            session->cvar1_hist[0], session->cvar1_hist[1],
            session->cvar1_hist[2], session->cvar1_hist[3],
            session->table846, session->table847, session->table849);
}

void jjffe_planet_807_strip_trace_close(void)
{
    if (!getenv("AB_1F_807_STRIP_TRACE"))
        return;
    strip807_print_session("asm840", &g_807stripAsm);
    strip807_print_session("cpp840", &g_807stripCpp);
    if (g_807stripFp) {
        fclose(g_807stripFp);
        g_807stripFp = NULL;
    }
}

static FILE* g_838pushFp = NULL;
static int g_838pushSeq = 0;
static FILE* g_838blendFp = NULL;
static int g_838blendSeq = 0;

typedef struct {
    int armed;
    char side[12];
    uint32_t sort_key;
    int site_id;
    int ebx_at_push;
} Planet838CtxArm;

static Planet838CtxArm g_838ctxArm;

void jjffe_planet_838_push_trace_v(const char* side,
                                   uint32_t sort_key,
                                   int site_id,
                                   int ebx_at_push)
{
    const char* env = getenv("AB_1F_840_838_PUSH_TRACE");
    if (!env || !env[0])
        return;

    g_838ctxArm.armed = 1;
    if (side) {
        strncpy(g_838ctxArm.side, side, sizeof(g_838ctxArm.side) - 1);
        g_838ctxArm.side[sizeof(g_838ctxArm.side) - 1] = '\0';
    } else {
        g_838ctxArm.side[0] = '\0';
    }
    g_838ctxArm.sort_key = sort_key;
    g_838ctxArm.site_id = site_id;
    g_838ctxArm.ebx_at_push = ebx_at_push;

    if (!g_838pushFp) {
        g_838pushFp = fopen(env, "w");
        if (!g_838pushFp)
            return;
        fprintf(g_838pushFp, "seq\tside\tsortKey\tsite_id\tebx_at_push\n");
    }

    fprintf(g_838pushFp,
            "%d\t%s\t%u\t%d\t%d\n",
            g_838pushSeq++,
            side ? side : "?",
            (unsigned)sort_key,
            site_id,
            ebx_at_push);
    fflush(g_838pushFp);
}

void jjffe_planet_838_blend_trace_v(const char* side,
                                    int branch,
                                    int param2,
                                    uint32_t eax_pre326,
                                    int iter)
{
    const char* env = getenv("AB_1F_838_BLEND_TRACE");
    if (!env || !env[0])
        return;

    if (!g_838blendFp) {
        g_838blendFp = fopen(env, "w");
        if (!g_838blendFp)
            return;
        fprintf(g_838blendFp,
                "seq\tside\tsortKey\tsite_id\tebx_at_push\tbranch\tparam2\t"
                "eax_pre326\titer\n");
    }

    uint32_t sort_key = 0u;
    int site_id = -1;
    int ebx_at_push = -1;
    if (g_838ctxArm.armed) {
        sort_key = g_838ctxArm.sort_key;
        site_id = g_838ctxArm.site_id;
        ebx_at_push = g_838ctxArm.ebx_at_push;
        g_838ctxArm.armed = 0;
    }

    fprintf(g_838blendFp,
            "%d\t%s\t%u\t%d\t%d\t%d\t%d\t0x%06x\t%d\n",
            g_838blendSeq++,
            side ? side : "?",
            (unsigned)sort_key,
            site_id,
            ebx_at_push,
            branch,
            param2,
            (unsigned)(eax_pre326 & 0xffffffu),
            iter);
    fflush(g_838blendFp);
}

void jjffe_planet_838_push_trace_close(void)
{
    if (!getenv("AB_1F_840_838_PUSH_TRACE"))
        return;
    if (g_838pushFp) {
        fclose(g_838pushFp);
        g_838pushFp = NULL;
    }
    g_838pushSeq = 0;
}

void jjffe_planet_838_blend_trace_close(void)
{
    if (!getenv("AB_1F_838_BLEND_TRACE"))
        return;
    if (g_838blendFp) {
        fclose(g_838blendFp);
        g_838blendFp = NULL;
    }
    g_838blendSeq = 0;
    g_838ctxArm.armed = 0;
}

int g_jjffe_planet_838_pack_enabled = 0;

static FILE* g_838packFp = NULL;
static int g_838packSeqAsm = 0;
static int g_838packSeqCpp = 0;

static void pack838_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_838_PACK_TRACE");
    g_jjffe_planet_838_pack_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void pack838_init_from_env(void)
{
    pack838_refresh_enabled();
}
#endif

static void pack838_open_if_needed(void)
{
    if (g_838packFp)
        return;
    const char* env = getenv("AB_1F_838_PACK_TRACE");
    if (!env || !env[0])
        return;
    g_838packFp = fopen(env, "w");
    if (!g_838packFp)
        return;
    fprintf(g_838packFp,
            "seq\tside\tsortKey\tcvar1\ttableId\tbranch\tparam2\tidx\tblend_sum\trem\tpre326\tstored_rgb16\n");
}

void jjffe_planet_838_pack_trace_v(const char* side,
                                   int branch,
                                   int param2,
                                   int idx,
                                   int32_t blend_sum,
                                   uint32_t pre326,
                                   uint32_t stored_rgb16)
{
    if (!getenv("AB_1F_838_PACK_TRACE") || !side)
        return;

    pack838_refresh_enabled();
    pack838_open_if_needed();
    if (!g_838packFp)
        return;

    uint32_t sort_key = 0u;
    int cvar1 = -1;
    int table_id = -1;
    if (g_807stripArm.armed) {
        sort_key = g_807stripArm.sort_key;
        cvar1 = g_807stripArm.cvar1;
        table_id = g_807stripArm.table_id;
    } else if (g_838ctxArm.sort_key) {
        sort_key = g_838ctxArm.sort_key;
    }

    const int rem = (int)(blend_sum & 7);
    const int seq = (side[0] == 'a') ? g_838packSeqAsm++ : g_838packSeqCpp++;
    fprintf(g_838packFp,
            "%d\t%s\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t0x%06x\t0x%04x\n",
            seq,
            side,
            (unsigned)sort_key,
            cvar1,
            table_id,
            branch,
            param2,
            idx,
            (int)blend_sum,
            rem,
            (unsigned)(pre326 & 0xffffffu),
            (unsigned)(stored_rgb16 & 0xffffu));
    fflush(g_838packFp);
}

void jjffe_planet_838_pack_trace_close(void)
{
    if (!getenv("AB_1F_838_PACK_TRACE"))
        return;
    if (g_838packFp) {
        fclose(g_838packFp);
        g_838packFp = NULL;
    }
    g_838packSeqAsm = 0;
    g_838packSeqCpp = 0;
}

int g_jjffe_planet_838_operand_enabled = 0;

static FILE* g_838operandFp = NULL;
static int g_838operandSeqAsm = 0;
static int g_838operandSeqCpp = 0;

static void operand838_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_838_OPERAND_TRACE");
    const char* d265 = getenv("AB_1F_D265_TRACE");
    g_jjffe_planet_838_operand_enabled = (env && env[0]) ? 1 : 0;
    if (d265 && d265[0])
        g_jjffe_planet_d265_enabled = 1;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void operand838_init_from_env(void)
{
    operand838_refresh_enabled();
}
#endif

static void operand838_open_if_needed(void)
{
    if (g_838operandFp)
        return;
    const char* env = getenv("AB_1F_838_OPERAND_TRACE");
    if (!env || !env[0])
        return;
    g_838operandFp = fopen(env, "w");
    if (!g_838operandFp)
        return;
    fprintf(g_838operandFp,
            "seq\tside\tsortKey\tcvar1\ttableId\tkind\tparam2\tidx"
            "\tparam3_in\tlocal8_x8\td265_term\tebp10_accum"
            "\ttbl\tmul_factor\tblend_sum\n");
}

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
                                      int32_t blend_sum)
{
    if (!getenv("AB_1F_838_OPERAND_TRACE") || !side)
        return;

    operand838_refresh_enabled();
    operand838_open_if_needed();
    if (!g_838operandFp)
        return;

    uint32_t sort_key = 0u;
    int cvar1 = -1;
    int table_id = -1;
    if (g_807stripArm.armed) {
        sort_key = g_807stripArm.sort_key;
        cvar1 = g_807stripArm.cvar1;
        table_id = g_807stripArm.table_id;
    } else if (g_838ctxArm.sort_key) {
        sort_key = g_838ctxArm.sort_key;
    }

    const int seq = (side[0] == 'a') ? g_838operandSeqAsm++ : g_838operandSeqCpp++;
    fprintf(g_838operandFp,
            "%d\t%s\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            seq,
            side,
            (unsigned)sort_key,
            cvar1,
            table_id,
            kind,
            param2,
            idx,
            (int)param3_in,
            (int)local8_x8,
            (int)d265_term,
            (int)ebp10_accum,
            (int)tbl,
            (int)mul_factor,
            (int)blend_sum);
    fflush(g_838operandFp);
}

void jjffe_planet_838_operand_trace_close(void)
{
    if (!getenv("AB_1F_838_OPERAND_TRACE"))
        return;
    if (g_838operandFp) {
        fclose(g_838operandFp);
        g_838operandFp = NULL;
    }
    g_838operandSeqAsm = 0;
    g_838operandSeqCpp = 0;
}

int g_jjffe_planet_d265_enabled = 0;

static FILE* g_d265Fp = NULL;
static int g_d265SeqAsm = 0;
static int g_d265SeqCpp = 0;

static void d265_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_D265_TRACE");
    g_jjffe_planet_d265_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void d265_init_from_env(void)
{
    d265_refresh_enabled();
}
#endif

void jjffe_planet_d265_trace_v(const char* side, const char* site, uint32_t d262, uint32_t d265)
{
    if (!getenv("AB_1F_D265_TRACE") || !side || !site)
        return;

    d265_refresh_enabled();
    if (!g_d265Fp) {
        g_d265Fp = fopen(getenv("AB_1F_D265_TRACE"), "w");
        if (!g_d265Fp)
            return;
        fprintf(g_d265Fp, "seq\tside\tsite\td262\td265\n");
    }

    const int seq = (side[0] == 'a') ? g_d265SeqAsm++ : g_d265SeqCpp++;
    fprintf(g_d265Fp, "%d\t%s\t%s\t%u\t%u\n", seq, side, site, (unsigned)d262, (unsigned)d265);
    fflush(g_d265Fp);
}

void jjffe_planet_d265_trace_close(void)
{
    if (!getenv("AB_1F_D265_TRACE"))
        return;
    if (g_d265Fp) {
        fclose(g_d265Fp);
        g_d265Fp = NULL;
    }
    g_d265SeqAsm = 0;
    g_d265SeqCpp = 0;
}

int g_jjffe_planet_877_enabled = 0;

static FILE* g_877Fp = NULL;
static int g_877Asm057 = 0;
static int g_877Asm058 = 0;

static void planet_877_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_877_TRACE");
    const char* pref = getenv("AB_1F_877_PREFSTORE_TRACE");
    const char* d265 = getenv("AB_1F_D265_TRACE");
    const char* cam = getenv("AB_1F_877_CAMOFF_TRACE");
    g_jjffe_planet_877_enabled =
        ((env && env[0]) || (pref && pref[0]) || (d265 && d265[0]) || (cam && cam[0])) ? 1 : 0;
}

static int planet_877_env_is_path(const char* env)
{
    return env && (strchr(env, '/') != NULL || strstr(env, ".tsv") != NULL);
}

static void planet_877_open_if_needed(void)
{
    if (g_877Fp)
        return;
    const char* env = getenv("AB_1F_877_TRACE");
    if (!planet_877_env_is_path(env))
        return;
    g_877Fp = fopen(env, "w");
    if (!g_877Fp)
        return;
    fprintf(g_877Fp, "side\tbranch\tparam5\tgateLeft\tgateRight\t263\tcamLen\t267\tparam6\n");
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void planet_877_init_from_env(void)
{
    planet_877_refresh_enabled();
}
#endif

void jjffe_planet_877_trace_emit(const char* side,
                                 int branch,
                                 int32_t d263,
                                 int32_t camLen,
                                 int32_t d267,
                                 int32_t param6,
                                 uint32_t param5,
                                 uint32_t gateLeft,
                                 uint32_t gateRight)
{
    const char* env = getenv("AB_1F_877_TRACE");
    if (!env || !env[0] || !side)
        return;

    planet_877_refresh_enabled();
    if (!g_jjffe_planet_877_enabled)
        return;

    planet_877_open_if_needed();

    if (side[0] == 'a') {
        if (branch == 57)
            g_877Asm057++;
        else if (branch == 58)
            g_877Asm058++;
    }

    fprintf(stderr,
            "877 %s: branch=%d param5=%08x gate=%u/%u 263=%d camLen=%d 267=%d param6=%d\n",
            side, branch, (unsigned)param5, (unsigned)gateLeft, (unsigned)gateRight, (int)d263,
            (int)camLen, (int)d267, (int)param6);

    if (g_877Fp) {
        fprintf(g_877Fp, "%s\t%d\t%x\t%u\t%u\t%d\t%d\t%d\t%d\n", side, branch, (unsigned)param5,
                (unsigned)gateLeft, (unsigned)gateRight, (int)d263, (int)camLen, (int)d267,
                (int)param6);
    }
}

void jjffe_planet_877_trace(int branch,
                            int32_t d263,
                            int32_t camLen,
                            int32_t d267,
                            int32_t param6,
                            uint32_t param5)
{
    jjffe_planet_877_trace_emit("asm", branch, d263, camLen, d267, param6, param5, 0u, 0u);
}

void jjffe_planet_877_trace_close(void)
{
    const char* env = getenv("AB_1F_877_TRACE");
    if (!env || !env[0])
        return;

    if (g_877Asm057 || g_877Asm058) {
        fprintf(stderr, "877_SESSION asm branch057=%d branch058=%d\n",
                g_877Asm057, g_877Asm058);
    }

    if (g_877Fp) {
        fclose(g_877Fp);
        g_877Fp = NULL;
    }
}

static FILE* g_877camFp = NULL;
static int g_877camSeqAsm = 0;
static int g_877camSeqCpp = 0;

void jjffe_planet_877_camoff_trace_v(const char* side,
                                     int32_t cam0, int32_t cam1, int32_t cam2,
                                     int32_t camShift, int32_t sumSq, int32_t isqrtRaw,
                                     int32_t camLen)
{
    const char* env = getenv("AB_1F_877_CAMOFF_TRACE");
    if (!env || !env[0] || !side)
        return;
    if (!g_877camFp) {
        g_877camFp = fopen(env, "w");
        if (!g_877camFp)
            return;
        fprintf(g_877camFp,
                "seq\tside\tcam0\tcam1\tcam2\tcamShift\tsumSq\tisqrtRaw\tcamLen\n");
    }
    int* seq = (side[0] == 'a') ? &g_877camSeqAsm : &g_877camSeqCpp;
    fprintf(g_877camFp, "%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            *seq, side, cam0, cam1, cam2, camShift, sumSq, isqrtRaw, camLen);
    ++(*seq);
    fflush(g_877camFp);
    fprintf(stderr,
            "877cam %s: cam=(%d,%d,%d) shift=%d sumSq=%d isqrt=%d camLen=%d\n",
            side, cam0, cam1, cam2, camShift, sumSq, isqrtRaw, camLen);
}

void jjffe_planet_877_camoff_trace_close(void)
{
    if (g_877camFp) {
        fclose(g_877camFp);
        g_877camFp = NULL;
    }
    g_877camSeqAsm = 0;
    g_877camSeqCpp = 0;
}

static FILE* g_877prefFp = NULL;
static int g_877prefSeqAsm = 0;
static int g_877prefSeqCpp = 0;

void jjffe_planet_877_prefstore_v(const char* side, int32_t bc, int32_t b8, int32_t b4)
{
    const char* env = getenv("AB_1F_877_PREFSTORE_TRACE");
    if (!env || !env[0] || !side)
        return;

    if (!g_877prefFp) {
        g_877prefFp = fopen(env, "w");
        if (!g_877prefFp)
            return;
        fprintf(g_877prefFp, "seq\tside\tbc\tb8\tb4\n");
    }

    const int seq = (side[0] == 'a') ? g_877prefSeqAsm++ : g_877prefSeqCpp++;
    fprintf(g_877prefFp, "%d\t%s\t%d\t%d\t%d\n", seq, side, (int)bc, (int)b8, (int)b4);
    fflush(g_877prefFp);
}

void jjffe_planet_877_prefstore_close(void)
{
    if (!getenv("AB_1F_877_PREFSTORE_TRACE"))
        return;
    if (g_877prefFp) {
        fclose(g_877prefFp);
        g_877prefFp = NULL;
    }
    g_877prefSeqAsm = 0;
    g_877prefSeqCpp = 0;
}

/* ---- AB_1F_805_JOB_TRACE / AB_1F_806_JOB_TRACE --------------------------
 * One row per 805/806 job execution, shared by ffeasm (ffe.asm hook) and the
 * C++ FUNC_001805/001806. The job layout (same on both sides):
 *   805: +4..+0x10 4 screen verts (int16 x,y), +0x14 drawTex u32,
 *        +0x18..+0x30 7 shade words u32 (low16 used by DPal).
 *   806: +4..+0x10 verts, +0x14..+0x20 4 UV corners, +0x24 drawTexSx i16,
 *        +0x26 palOffset i16, +0x28 pairs*2 u32, +0x2c.. dither words u16. */
int g_jjffe_planet_805_job_enabled = 0;
int g_jjffe_planet_806_job_enabled = 0;

static void job805_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_805_JOB_TRACE");
    g_jjffe_planet_805_job_enabled = (env && env[0]) ? 1 : 0;
}

static void job806_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_806_JOB_TRACE");
    g_jjffe_planet_806_job_enabled = (env && env[0]) ? 1 : 0;
}

#if defined(FFEASM_SHIP_DEMO)
__attribute__((constructor)) static void job805806_init_from_env(void)
{
    job805_refresh_enabled();
    job806_refresh_enabled();
}
#endif

static FILE* g_job805Fp = NULL;
static FILE* g_job806Fp = NULL;

/* Back-buffer access for the dpx column:
 * ASM build: DATA_007773 is a struct whose first dword is the buffer pointer.
 * C++ build: the intro sets it via jjffe_planet_set_backbuf. */
const void* g_jjffe_planet_805_backbuf = NULL;

void jjffe_planet_set_backbuf(const void* p)
{
    g_jjffe_planet_805_backbuf = p;
}

#if defined(FFEASM_SHIP_DEMO)
extern uint32_t DATA_007773;
const void* jjffe_job_trace_backbuf(void)
{
    return (const void*)(uintptr_t)(*(const uint32_t*)&DATA_007773);
}
#else
const void* jjffe_job_trace_backbuf(void)
{
    return g_jjffe_planet_805_backbuf;
}
#endif

void jjffe_planet_805_job_trace(uint32_t clock, const void* job)
{
    if (!job)
        return;
    if (!getenv("AB_1F_805_JOB_TRACE"))
        return;
    job805_refresh_enabled();
    if (!g_job805Fp) {
        g_job805Fp = fopen(getenv("AB_1F_805_JOB_TRACE"), "w");
        if (!g_job805Fp)
            return;
        fprintf(g_job805Fp,
                "seq\tclock\tsortKey\tdrawTex\ts0\ts1\ts2\ts3\ts4\ts5\ts6\t"
                "v0x\tv0y\tv1x\tv1y\tv2x\tv2y\tv3x\tv3y\tdpx\n");
    }
    const uint8_t* p = (const uint8_t*)job;
    const uint32_t sortKey = ((uintptr_t)p >= 8u) ? *(const uint32_t*)(p - 8) : 0u;
    const uint32_t drawTex = *(const uint32_t*)(p + 0x14);
    const int16_t* v = (const int16_t*)(p + 4);
    const uint32_t* s32 = (const uint32_t*)(p + 0x18);
    const unsigned s0 = (unsigned)(s32[0] & 0xffffu);
    const unsigned s1 = (unsigned)(s32[1] & 0xffffu);
    const unsigned s2 = (unsigned)(s32[2] & 0xffffu);
    const unsigned s3 = (unsigned)(s32[3] & 0xffffu);
    const unsigned s4 = (unsigned)(s32[4] & 0xffffu);
    const unsigned s5 = (unsigned)(s32[5] & 0xffffu);
    const unsigned s6 = (unsigned)(s32[6] & 0xffffu);
    int dpx = 0;
    const uint8_t* bb = (const uint8_t*)jjffe_job_trace_backbuf();
    if (bb) {
        for (int i = 0; i < 64000; ++i)
            if (bb[i])
                ++dpx;
    }
    static int seq = 0;
    fprintf(g_job805Fp,
            "%d\t0x%x\t%u\t0x%x\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            seq++, (unsigned)clock, (unsigned)sortKey, (unsigned)drawTex,
            s0, s1, s2, s3, s4, s5, s6,
            (int)v[0], (int)v[1], (int)v[2], (int)v[3],
            (int)v[4], (int)v[5], (int)v[6], (int)v[7], dpx);
    fflush(g_job805Fp);
}

void jjffe_planet_806_job_trace(uint32_t clock, const void* job)
{
    if (!job)
        return;
    if (!getenv("AB_1F_806_JOB_TRACE"))
        return;
    job806_refresh_enabled();
    if (!g_job806Fp) {
        g_job806Fp = fopen(getenv("AB_1F_806_JOB_TRACE"), "w");
        if (!g_job806Fp)
            return;
        fprintf(g_job806Fp,
                "seq\tclock\tsortKey\ttexSx\tpalOff\tpairs\t"
                "v0x\tv0y\tv1x\tv1y\tv2x\tv2y\tv3x\tv3y\t"
                "t1x\tt1y\tt2x\tt2y\tt3x\tt3y\tt4x\tt4y\td0\tdpx\n");
    }
    const uint8_t* p = (const uint8_t*)job;
    const uint32_t sortKey = ((uintptr_t)p >= 8u) ? *(const uint32_t*)(p - 8) : 0u;
    const int16_t drawTexSx = *(const int16_t*)(p + 0x24);
    const int16_t palOff = *(const int16_t*)(p + 0x26);
    const int pairs = *(const int32_t*)(p + 0x28) >> 1;
    const int16_t* v = (const int16_t*)(p + 4);
    const int16_t* t = (const int16_t*)(p + 0x14);
    const uint16_t* d = (const uint16_t*)(p + 0x2c);
    int dpx = 0;
    const uint8_t* bb = (const uint8_t*)jjffe_job_trace_backbuf();
    if (bb) {
        for (int i = 0; i < 64000; ++i)
            if (bb[i])
                ++dpx;
    }
    static int seq = 0;
    fprintf(g_job806Fp,
            "%d\t0x%x\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d\n",
            seq++, (unsigned)clock, (unsigned)sortKey, (int)drawTexSx, (int)palOff, pairs,
            (int)v[0], (int)v[1], (int)v[2], (int)v[3],
            (int)v[4], (int)v[5], (int)v[6], (int)v[7],
            (int)t[0], (int)t[1], (int)t[2], (int)t[3],
            (int)t[4], (int)t[5], (int)t[6], (int)t[7],
            pairs > 0 ? (unsigned)d[0] : 0u, dpx);
    fflush(g_job806Fp);
}

void jjffe_planet_805_job_trace_close(void)
{
    if (g_job805Fp) {
        fclose(g_job805Fp);
        g_job805Fp = NULL;
    }
}

void jjffe_planet_806_job_trace_close(void)
{
    if (g_job806Fp) {
        fclose(g_job806Fp);
        g_job806Fp = NULL;
    }
}

/* ---- AB_1F_807_JOB_TRACE / AB_1F_808_JOB_TRACE --------------------------
 * 807: +4..+0xc 3 verts (int16 x,y), +0x10 drawTex u32, +0x14..+0x30 7 shade
 *      words u32 (low16 used by DPal).
 * 808: +4..+0xc 3 verts, +0x10..+0x14 depth dither words, +0x16 depth,
 *      +0x18..+0x20 3 depth words, +0x24.. dither pairs (2 * dword[+0x20]). */
int g_jjffe_planet_807_job_enabled = 0;
int g_jjffe_planet_808_job_enabled = 0;

static void job807_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_807_JOB_TRACE");
    g_jjffe_planet_807_job_enabled = (env && env[0]) ? 1 : 0;
}

static void job808_refresh_enabled(void)
{
    const char* env = getenv("AB_1F_808_JOB_TRACE");
    g_jjffe_planet_808_job_enabled = (env && env[0]) ? 1 : 0;
}

__attribute__((constructor)) static void job807808_init_from_env(void)
{
    job807_refresh_enabled();
    job808_refresh_enabled();
}

static FILE* g_job807Fp = NULL;
static FILE* g_job808Fp = NULL;

void jjffe_planet_807_job_trace(uint32_t clock, const void* job)
{
    if (!job)
        return;
    if (!getenv("AB_1F_807_JOB_TRACE"))
        return;
    job807_refresh_enabled();
    if (!g_job807Fp) {
        g_job807Fp = fopen(getenv("AB_1F_807_JOB_TRACE"), "w");
        if (!g_job807Fp)
            return;
        fprintf(g_job807Fp,
                "seq\tclock\tdrawTex\ts0\ts1\ts2\ts3\ts4\ts5\ts6\t"
                "v0x\tv0y\tv1x\tv1y\tv2x\tv2y\tdpx\n");
    }
    const uint8_t* p = (const uint8_t*)job;
    const uint32_t drawTex = *(const uint32_t*)(p + 0x10);
    const int16_t* v = (const int16_t*)(p + 4);
    const uint32_t* s32 = (const uint32_t*)(p + 0x14);
    int dpx = 0;
    const uint8_t* bb = (const uint8_t*)jjffe_job_trace_backbuf();
    if (bb) {
        for (int i = 0; i < 64000; ++i)
            if (bb[i])
                ++dpx;
    }
    static int seq = 0;
    fprintf(g_job807Fp,
            "%d\t0x%x\t0x%x\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            seq++, (unsigned)clock, (unsigned)drawTex,
            (unsigned)(s32[0] & 0xffffu), (unsigned)(s32[1] & 0xffffu),
            (unsigned)(s32[2] & 0xffffu), (unsigned)(s32[3] & 0xffffu),
            (unsigned)(s32[4] & 0xffffu), (unsigned)(s32[5] & 0xffffu),
            (unsigned)(s32[6] & 0xffffu),
            (int)v[0], (int)v[1], (int)v[2], (int)v[3], (int)v[4], (int)v[5], dpx);
    fflush(g_job807Fp);
}

void jjffe_planet_808_job_trace(uint32_t clock, const void* job)
{
    if (!job)
        return;
    if (!getenv("AB_1F_808_JOB_TRACE"))
        return;
    job808_refresh_enabled();
    if (!g_job808Fp) {
        g_job808Fp = fopen(getenv("AB_1F_808_JOB_TRACE"), "w");
        if (!g_job808Fp)
            return;
        fprintf(g_job808Fp,
                "seq\tclock\td12\td16\td1a\tv0x\tv0y\tv1x\tv1y\tv2x\tv2y\tdpx\n");
    }
    const uint8_t* p = (const uint8_t*)job;
    const int16_t* v = (const int16_t*)(p + 4);
    const int16_t d12 = *(const int16_t*)(p + 0x12);
    const int16_t d16 = *(const int16_t*)(p + 0x16);
    const int16_t d1a = *(const int16_t*)(p + 0x1a);
    int dpx = 0;
    const uint8_t* bb = (const uint8_t*)jjffe_job_trace_backbuf();
    if (bb) {
        for (int i = 0; i < 64000; ++i)
            if (bb[i])
                ++dpx;
    }
    static int seq = 0;
    fprintf(g_job808Fp,
            "%d\t0x%x\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
            seq++, (unsigned)clock, (int)d12, (int)d16, (int)d1a,
            (int)v[0], (int)v[1], (int)v[2], (int)v[3], (int)v[4], (int)v[5], dpx);
    fflush(g_job808Fp);
}
