#include "jjffe/planet_mid_scr_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_mid_scr_enabled = 0;

int g_scr_hook_edge_swap = 0;
int g_scr_hook_param5 = 0;
void* g_scr_hook_p2 = NULL;
void* g_scr_hook_p3 = NULL;
void* g_scr_hook_p4 = NULL;
void* g_scr_hook_child = NULL;

typedef struct {
    char side[8];
    int seq;
    int p5;
    int edge_swap;
    int32_t mix2;
    int32_t mix3;
    int32_t mix4;
    int32_t out_mix;
    int16_t p2x, p2y, p3x, p3y, p4x, p4y;
    int avg_x, avg_y;
    int dither_on;
    int i6b, i8, side_pref;
    int vx_lt, vy_lt, tbl_idx;
    int delta_x, delta_y;
    int16_t out_sx, out_sy;
} ScrSample;

static ScrSample s_samples[16384];
static int s_sampleN = 0;
static int s_seqAsm = 0;
static int s_seqCpp = 0;
static FILE* s_fp = NULL;

static int32_t vert_mix(const void* vp)
{
    if (!vp)
        return 0;
    return *(const int32_t*)((const uint8_t*)vp + 0x3c);
}

static int16_t vert_sx(const void* vp)
{
    if (!vp)
        return 0;
    return *(const int16_t*)vp;
}

static int16_t vert_sy(const void* vp)
{
    if (!vp)
        return 0;
    return *(const int16_t*)((const uint8_t*)vp + 2);
}

static int32_t vert_z(const void* vp)
{
    if (!vp)
        return 0;
    return *(const int32_t*)((const uint8_t*)vp + 0x0c);
}

static void open_if_needed(void)
{
    if (s_fp)
        return;
    const char* env = getenv("AB_1F_MID_SCR_TRACE");
    if (!env || !env[0])
        return;
    s_fp = fopen(env, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_mid_scr_trace: cannot open %s\n", env);
        return;
    }
    g_jjffe_planet_mid_scr_enabled = 1;
    fprintf(stderr, "planet_mid_scr_trace: writing %s\n", env);
}

static void append_row(const char* side,
                       int p5,
                       int edge_swap,
                       const void* p2,
                       const void* p3,
                       const void* p4,
                       const void* child,
                       int avg_x,
                       int avg_y,
                       int dither_on,
                       int i6b,
                       int i8,
                       int side_pref,
                       int vx_lt,
                       int vy_lt,
                       int tbl_idx,
                       int delta_x,
                       int delta_y)
{
    if (!g_jjffe_planet_mid_scr_enabled || !side || !child)
        return;
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;

    ScrSample* s = &s_samples[s_sampleN++];
    memset(s, 0, sizeof(*s));
    strncpy(s->side, side, sizeof(s->side) - 1);
    s->seq = (side[0] == 'a') ? s_seqAsm++ : s_seqCpp++;
    s->p5 = p5;
    s->edge_swap = edge_swap;
    s->mix2 = vert_mix(p2);
    s->mix3 = vert_mix(p3);
    s->mix4 = vert_mix(p4);
    s->out_mix = vert_mix(child);
    s->p2x = vert_sx(p2);
    s->p2y = vert_sy(p2);
    s->p3x = vert_sx(p3);
    s->p3y = vert_sy(p3);
    s->p4x = vert_sx(p4);
    s->p4y = vert_sy(p4);
    s->avg_x = avg_x;
    s->avg_y = avg_y;
    s->dither_on = dither_on;
    s->i6b = i6b;
    s->i8 = i8;
    s->side_pref = side_pref;
    s->vx_lt = vx_lt;
    s->vy_lt = vy_lt;
    s->tbl_idx = tbl_idx;
    s->delta_x = delta_x;
    s->delta_y = delta_y;
    s->out_sx = vert_sx(child);
    s->out_sy = vert_sy(child);
}

void jjffe_planet_mid_scr_trace_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_mid_scr_trace: cannot open %s\n", path);
        return;
    }
    s_sampleN = 0;
    s_seqAsm = 0;
    s_seqCpp = 0;
    g_jjffe_planet_mid_scr_enabled = 1;
    fprintf(stderr, "planet_mid_scr_trace: writing %s\n", path);
}

static void flush_rows(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp, "# mid_scr simple855 n=%d\n", s_sampleN);
    fprintf(s_fp,
            "seq\tside\tp5\tedgeSwap\t"
            "mix2\tmix3\tmix4\toutMix\t"
            "p2x\tp2y\tp3x\tp3y\tp4x\tp4y\t"
            "avgX\tavgY\tditherOn\t"
            "i6b\ti8\tsidePref\tvxLt\tvyLt\ttblIdx\t"
            "deltaX\tdeltaY\toutSx\toutSy\n");
    for (int i = 0; i < s_sampleN; ++i) {
        const ScrSample* s = &s_samples[i];
        fprintf(s_fp,
                "%d\t%s\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\n",
                s->seq, s->side, s->p5, s->edge_swap,
                (int)s->mix2, (int)s->mix3, (int)s->mix4, (int)s->out_mix,
                (int)s->p2x, (int)s->p2y, (int)s->p3x, (int)s->p3y, (int)s->p4x, (int)s->p4y,
                s->avg_x, s->avg_y, s->dither_on,
                s->i6b, s->i8, s->side_pref, s->vx_lt, s->vy_lt, s->tbl_idx,
                s->delta_x, s->delta_y, (int)s->out_sx, (int)s->out_sy);
    }
    fflush(s_fp);
}

void jjffe_planet_mid_scr_trace_close(void)
{
    flush_rows();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_mid_scr_enabled = 0;
}

void jjffe_planet_mid_scr_trace_note(const char* side,
                                     int param5,
                                     int edge_swap,
                                     const void* p2,
                                     const void* p3,
                                     const void* p4,
                                     const void* child,
                                     int avg_x,
                                     int avg_y,
                                     int dither_on,
                                     int i6b,
                                     int i8,
                                     int side_pref,
                                     int vx_lt,
                                     int vy_lt,
                                     int tbl_idx,
                                     int delta_x,
                                     int delta_y)
{
    open_if_needed();
    append_row(side, param5, edge_swap, p2, p3, p4, child,
               avg_x, avg_y, dither_on, i6b, i8, side_pref,
               vx_lt, vy_lt, tbl_idx, delta_x, delta_y);
}

/* Recompute dither fields from parent screens (matches FUNC_001855 / ASM JUMP_007212). */
static void recompute_dither(const void* p2,
                             const void* p3,
                             const void* p4,
                             int* dither_on,
                             int* i6b,
                             int* i8,
                             int* side_pref,
                             int* vx_lt,
                             int* vy_lt,
                             int* tbl_idx,
                             int* delta_x,
                             int* delta_y)
{
    *dither_on = 0;
    *i6b = 0;
    *i8 = 0;
    *side_pref = 0;
    *vx_lt = 0;
    *vy_lt = 0;
    *tbl_idx = 0;
    *delta_x = 0;
    *delta_y = 0;
    if (!p2 || !p3 || !p4)
        return;
    if (vert_z(p2) < 0x40 || vert_z(p3) < 0x40)
        return;
    if (vert_z(p4) <= 0x40)
        return;

    *dither_on = 1;
    const int16_t sVar2 = vert_sx(p2);
    const int16_t sVar1 = vert_sx(p3);
    const int16_t vy2 = vert_sy(p2);
    const int16_t vy3 = vert_sy(p3);
    *i6b = ((int32_t)vert_sx(p4) - (int32_t)sVar2) * ((int32_t)vy3 - (int32_t)vy2);
    *i8 = ((int32_t)sVar1 - (int32_t)sVar2) * ((int32_t)vert_sy(p4) - (int32_t)vy2);
    *side_pref = (*i6b - *i8 != 0 && *i8 <= *i6b) ? 1 : 0;
    *vx_lt = (sVar1 < sVar2) ? 1 : 0;
    *vy_lt = (vy3 < vy2) ? 1 : 0;
    *tbl_idx = *side_pref * 4 + *vx_lt * 8 + *vy_lt * 0x10;
}

void jjffe_planet_mid_scr_trace_asm(void)
{
    open_if_needed();
    if (!g_scr_hook_child)
        return;

    const int avg_x = (((int32_t)vert_sx(g_scr_hook_p2) + (int32_t)vert_sx(g_scr_hook_p3)) >> 1);
    const int avg_y = (((int32_t)vert_sy(g_scr_hook_p2) + (int32_t)vert_sy(g_scr_hook_p3)) >> 1);
    int dither_on, i6b, i8, side_pref, vx_lt, vy_lt, tbl_idx, delta_x, delta_y;
    recompute_dither(g_scr_hook_p2, g_scr_hook_p3, g_scr_hook_p4,
                     &dither_on, &i6b, &i8, &side_pref, &vx_lt, &vy_lt,
                     &tbl_idx, &delta_x, &delta_y);
    if (dither_on) {
        delta_x = (int)vert_sx(g_scr_hook_child) - avg_x;
        delta_y = (int)vert_sy(g_scr_hook_child) - avg_y;
    }

    append_row("asm855", g_scr_hook_param5, g_scr_hook_edge_swap,
               g_scr_hook_p2, g_scr_hook_p3, g_scr_hook_p4, g_scr_hook_child,
               avg_x, avg_y, dither_on, i6b, i8, side_pref,
               vx_lt, vy_lt, tbl_idx, delta_x, delta_y);
}
