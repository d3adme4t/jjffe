#include "jjffe/planet_mid_else_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_mid_else_enabled = 0;

void* g_else_hook_p2 = NULL;
void* g_else_hook_p3 = NULL;
void* g_else_hook_child = NULL;
int g_else_hook_param5 = 0;
int g_else_hook_t839 = 0;
int g_else_hook_t840 = 0;

extern int g_jjffe_planet_mid_hook_term846;
extern int g_jjffe_planet_mid_hook_avg;
extern int g_jjffe_planet_mid_hook_mix2;
extern int g_jjffe_planet_mid_hook_mix3;

static int s_seq_asm = 0;
static int s_seq_cpp = 0;
static FILE* s_fp = NULL;

static int32_t vert_i32(const void* vp, int off)
{
    if (!vp)
        return 0;
    return *(const int32_t*)((const uint8_t*)vp + off);
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

/* FUNC_001521 — (a*b) >> 31 */
static uint32_t fp_mul_high31(int32_t a, int32_t b)
{
    const int64_t product = (int64_t)a * (int64_t)b;
    const uint32_t high = (uint32_t)((uint64_t)product >> 32);
    const uint32_t low = (uint32_t)product;
    return (high << 1) | (low >> 31);
}

static int sar1_pair(int32_t a, int32_t b)
{
    return (a + b) >> 1;
}

void jjffe_planet_mid_else_trace_fill_847(int32_t cam_x,
                                          int32_t cam_y,
                                          int32_t cam_z,
                                          JjffeMidElse855Row* row)
{
    if (!row)
        return;
    row->cam04 = cam_x;
    row->cam08 = cam_y;
    row->cam0c = cam_z;
    row->branch847 = 0;
    row->num_x = 0;
    row->den_z = 0;
    row->num_y = 0;
    row->proj_x = 0;
    row->proj_y = 0;
    row->out_sx = 8;
    row->out_sy = 8;

    if (cam_z < 0x40) {
        row->branch847 = 0;
        return;
    }

    int iVar1;
    int iVar2;
    if (cam_z < 0x10001) {
        row->branch847 = 1;
        row->num_x = cam_x << 8;
        row->num_y = cam_y << 8;
        row->den_z = cam_z;
        iVar1 = row->num_x / cam_z;
        iVar2 = row->num_y / cam_z;
    } else {
        row->branch847 = 2;
        row->den_z = cam_z >> 8;
        row->num_x = cam_x;
        row->num_y = cam_y;
        iVar1 = cam_x / row->den_z;
        iVar2 = cam_y / row->den_z;
    }
    row->proj_x = iVar1;
    row->proj_y = iVar2;
    row->out_sx = (int16_t)(int16_t)(uint16_t)(uint32_t)(iVar1 + 0xa0);
    row->out_sy = (int16_t)(int16_t)(uint16_t)(uint32_t)(0x4f - iVar2);
}

static void write_header(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp,
            "seq\tside\tp5\tmix2\tmix3\toutMix\t"
            "term846\tavgMix\t"
            "p2_28\tp2_2c\tp2_30\tp3_28\tp3_2c\tp3_30\t"
            "t840\tc28\tc2c\tc30\tt839\t"
            "p2_04\tp2_08\tp2_0c\tp3_04\tp3_08\tp3_0c\t"
            "t521x\tavg04\tcam04\t"
            "t521y\tavg08\tcam08\t"
            "t521z\tavg0c\tcam0c\t"
            "br847\tnumX\tdenZ\tnumY\tprojX\tprojY\t"
            "outSx\toutSy\n");
}

static void open_if_needed(void)
{
    if (s_fp)
        return;
    const char* env = getenv("AB_1F_MID_ELSE_TRACE");
    if (!env || !env[0])
        return;
    s_fp = fopen(env, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_mid_else_trace: cannot open %s\n", env);
        return;
    }
    g_jjffe_planet_mid_else_enabled = 1;
    fprintf(stderr, "planet_mid_else_trace: writing %s\n", env);
    write_header();
}

static void append_row(const char* side, const JjffeMidElse855Row* row)
{
    if (!g_jjffe_planet_mid_else_enabled || !side || !row || !s_fp)
        return;

    const int seq = (side[0] == 'a') ? s_seq_asm++ : s_seq_cpp++;
    fprintf(s_fp,
            "%d\t%s\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\n",
            seq, side, row->param5, (int)row->mix2, (int)row->mix3, (int)row->out_mix,
            row->term846, row->avg_mix,
            row->p2_28, row->p2_2c, row->p2_30, row->p3_28, row->p3_2c, row->p3_30,
            row->t840, row->c28, row->c2c, row->c30, row->t839,
            row->p2_04, row->p2_08, row->p2_0c, row->p3_04, row->p3_08, row->p3_0c,
            row->term521_x, row->avg04, row->cam04,
            row->term521_y, row->avg08, row->cam08,
            row->term521_z, row->avg0c, row->cam0c,
            row->branch847, row->num_x, row->den_z, row->num_y, row->proj_x, row->proj_y,
            (int)row->out_sx, (int)row->out_sy);
    fflush(s_fp);
}

void jjffe_planet_mid_else_trace_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_mid_else_trace: cannot open %s\n", path);
        return;
    }
    s_seq_asm = 0;
    s_seq_cpp = 0;
    g_jjffe_planet_mid_else_enabled = 1;
    fprintf(stderr, "planet_mid_else_trace: writing %s\n", path);
    write_header();
}

void jjffe_planet_mid_else_trace_close(void)
{
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_mid_else_enabled = 0;
}

void jjffe_planet_mid_else_trace_note(const char* side, const JjffeMidElse855Row* row)
{
    open_if_needed();
    append_row(side, row);
}

void jjffe_planet_mid_else_trace_asm(void)
{
    open_if_needed();
    if (!g_else_hook_child || !g_else_hook_p2 || !g_else_hook_p3)
        return;

    const void* p2 = g_else_hook_p2;
    const void* p3 = g_else_hook_p3;
    const void* child = g_else_hook_child;
    const int p5 = g_else_hook_param5;
    const int32_t t839 = (int32_t)g_else_hook_t839;

    JjffeMidElse855Row row;
    memset(&row, 0, sizeof(row));
    row.param5 = p5;
    row.mix2 = g_jjffe_planet_mid_hook_mix2;
    row.mix3 = g_jjffe_planet_mid_hook_mix3;
    row.out_mix = vert_i32(child, 0x3c);
    row.term846 = g_jjffe_planet_mid_hook_term846;
    row.avg_mix = g_jjffe_planet_mid_hook_avg;

    row.p2_28 = vert_i32(p2, 0x28);
    row.p2_2c = vert_i32(p2, 0x2c);
    row.p2_30 = vert_i32(p2, 0x30);
    row.p3_28 = vert_i32(p3, 0x28);
    row.p3_2c = vert_i32(p3, 0x2c);
    row.p3_30 = vert_i32(p3, 0x30);

    /* T840 loaded in asm hook scratch — recompute c28 from parents if child fields set. */
    row.t840 = (int32_t)g_else_hook_t840;
    row.c28 = vert_i32(child, 0x28);
    row.c2c = vert_i32(child, 0x2c);
    row.c30 = vert_i32(child, 0x30);
    row.t839 = t839;

    row.p2_04 = vert_i32(p2, 0x04);
    row.p2_08 = vert_i32(p2, 0x08);
    row.p2_0c = vert_i32(p2, 0x0c);
    row.p3_04 = vert_i32(p3, 0x04);
    row.p3_08 = vert_i32(p3, 0x08);
    row.p3_0c = vert_i32(p3, 0x0c);

    row.term521_x = (int32_t)fp_mul_high31(row.c28, t839);
    row.avg04 = sar1_pair(row.p2_04, row.p3_04);
    row.cam04 = vert_i32(child, 0x04);

    row.term521_y = (int32_t)fp_mul_high31(row.c2c, t839);
    row.avg08 = sar1_pair(row.p2_08, row.p3_08);
    row.cam08 = vert_i32(child, 0x08);

    row.term521_z = (int32_t)fp_mul_high31(row.c30, t839);
    row.avg0c = sar1_pair(row.p2_0c, row.p3_0c);
    row.cam0c = vert_i32(child, 0x0c);

    jjffe_planet_mid_else_trace_fill_847(row.cam04, row.cam08, row.cam0c, &row);
    row.out_sx = vert_sx(child);
    row.out_sy = vert_sy(child);

    append_row("asm855", &row);
}
