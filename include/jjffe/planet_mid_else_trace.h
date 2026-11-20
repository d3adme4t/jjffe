#ifndef JJFFE_PLANET_MID_ELSE_TRACE_H
#define JJFFE_PLANET_MID_ELSE_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AB_1F_MID_ELSE_TRACE=<path> — FUNC_001855 else-path 847(cam) screen. */
extern int g_jjffe_planet_mid_else_enabled;

extern void* g_else_hook_p2;
extern void* g_else_hook_p3;
extern void* g_else_hook_child;
extern int g_else_hook_param5;
extern int g_else_hook_t839;
extern int g_else_hook_t840;

typedef struct JjffeMidElse855Row {
    int param5;
    int32_t mix2;
    int32_t mix3;
    int32_t out_mix;
    int32_t term846;
    int32_t avg_mix;
    int32_t p2_28;
    int32_t p2_2c;
    int32_t p2_30;
    int32_t p3_28;
    int32_t p3_2c;
    int32_t p3_30;
    int32_t t840;
    int32_t c28;
    int32_t c2c;
    int32_t c30;
    int32_t t839;
    int32_t p2_04;
    int32_t p2_08;
    int32_t p2_0c;
    int32_t p3_04;
    int32_t p3_08;
    int32_t p3_0c;
    int32_t term521_x;
    int32_t avg04;
    int32_t cam04;
    int32_t term521_y;
    int32_t avg08;
    int32_t cam08;
    int32_t term521_z;
    int32_t avg0c;
    int32_t cam0c;
    int branch847; /* 0=z<0x40  1=z<=0x10000  2=large */
    int32_t num_x;
    int32_t den_z;
    int32_t num_y;
    int32_t proj_x;
    int32_t proj_y;
    int16_t out_sx;
    int16_t out_sy;
} JjffeMidElse855Row;

void jjffe_planet_mid_else_trace_open(const char* path);
void jjffe_planet_mid_else_trace_close(void);

void jjffe_planet_mid_else_trace_fill_847(int32_t cam_x,
                                          int32_t cam_y,
                                          int32_t cam_z,
                                          JjffeMidElse855Row* row);

void jjffe_planet_mid_else_trace_note(const char* side, const JjffeMidElse855Row* row);

void jjffe_planet_mid_else_trace_asm(void);

#ifdef __cplusplus
}
#endif

#endif
