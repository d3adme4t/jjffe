#include "jjffe/planet_792_chain_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_792_chain_enabled = 0;

static FILE* s_fp = NULL;
static int s_seq_asm = 0;
static int s_seq_cpp = 0;
static int s_band_seq = 0;
static intptr_t s_cur_band = 0;
static int s_cur_step = 0;

#if defined(FFEASM_SHIP_DEMO)
extern char FUNC_001790[], FUNC_001792[], FUNC_001793[], FUNC_001794[],
    FUNC_001795[], FUNC_001796[], FUNC_001797[], FUNC_001798[], FUNC_001799[],
    FUNC_001800[], FUNC_001804[], FUNC_001805[], FUNC_001806[], FUNC_001807[],
    FUNC_001808[], FUNC_001809[];

static const char* cb_name(uintptr_t cb)
{
    struct {
        const char* n;
        char* p;
    } tab[] = {
        {"001790", FUNC_001790}, {"001792", FUNC_001792}, {"001793", FUNC_001793},
        {"001794", FUNC_001794}, {"001795", FUNC_001795}, {"001796", FUNC_001796},
        {"001797", FUNC_001797}, {"001798", FUNC_001798}, {"001799", FUNC_001799},
        {"001800", FUNC_001800},
        {"001804", FUNC_001804}, {"001805", FUNC_001805}, {"001806", FUNC_001806},
        {"001807", FUNC_001807}, {"001808", FUNC_001808}, {"001809", FUNC_001809},
    };
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i) {
        if ((uintptr_t)tab[i].p == cb)
            return tab[i].n;
    }
    return "?";
}

static void fmt805_note(void* job, char* note, size_t note_sz)
{
    if (!job || !note_sz)
        return;
    const int16_t* vv = (const int16_t*)((const uint8_t*)job + 4);
    snprintf(note, note_sz, "verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)",
             (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
             (int)vv[4], (int)vv[5], (int)vv[6], (int)vv[7]);
}

static intptr_t s_asm_band = 0;
static int s_asm_step = 0;
static void* s_asm_job = NULL;
static void* s_asm_cb = NULL;
static const char* s_asm_fn = "?";
static int s_asm_d834_in = 0;
static char s_asm_note[128];
static const char* s_asm_side = "asm792";
#endif

const char jjffe_side_asm792[] = "asm792";
const char jjffe_side_asm788[] = "asm788";

static void open_if_needed(void)
{
    if (s_fp)
        return;
    const char* env = getenv("AB_1F_792_CHAIN_TRACE");
    if (!env || !env[0])
        return;
    s_fp = fopen(env, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_792_chain_trace: cannot open %s\n", env);
        return;
    }
    g_jjffe_planet_792_chain_enabled = 1;
    fprintf(stderr, "planet_792_chain_trace: writing %s\n", env);
    fprintf(s_fp,
            "seq\tside\tband\tbandSeq\tstep\tjob\tcb\tfn\td834_in\tnext\td834_out\tevent\tnote\n");
    fflush(s_fp);
}

static void emit_row(const char* side,
                     intptr_t band,
                     int step,
                     void* job,
                     void* cb,
                     const char* fn,
                     int d834_in,
                     void* next,
                     int d834_out,
                     const char* event,
                     const char* note)
{
    if (!g_jjffe_planet_792_chain_enabled || !s_fp || !side)
        return;

    if (band != s_cur_band) {
        s_cur_band = band;
        s_band_seq++;
        s_cur_step = 0;
    }
    if (step >= 0)
        s_cur_step = step;

    const int seq = (side[0] == 'a') ? s_seq_asm++ : s_seq_cpp++;
    fprintf(s_fp,
            "%d\t%s\t%p\t%d\t%d\t%p\t%p\t%s\t%d\t%p\t%d\t%s\t%s\n",
            seq, side, (void*)band, s_band_seq, s_cur_step,
            job, cb, fn ? fn : "?", d834_in, next, d834_out,
            event ? event : "step", note ? note : "");
    fflush(s_fp);
}

void jjffe_planet_792_chain_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_792_chain_trace: cannot open %s\n", path);
        return;
    }
    s_seq_asm = 0;
    s_seq_cpp = 0;
    s_band_seq = 0;
    s_cur_band = 0;
    s_cur_step = 0;
    g_jjffe_planet_792_chain_enabled = 1;
    fprintf(stderr, "planet_792_chain_trace: writing %s\n", path);
    fprintf(s_fp,
            "seq\tside\tband\tbandSeq\tstep\tjob\tcb\tfn\td834_in\tnext\td834_out\tevent\tnote\n");
    fflush(s_fp);
}

void jjffe_planet_792_chain_close(void)
{
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_792_chain_enabled = 0;
}

void jjffe_planet_792_chain_note_cpp(intptr_t band,
                                     int step,
                                     void* job,
                                     void* cb,
                                     const char* fn,
                                     int d834_in,
                                     void* next,
                                     int d834_out,
                                     const char* note)
{
    jjffe_planet_792_chain_note_cpp_side("cpp792", band, step, job, cb, fn, d834_in, next,
                                         d834_out, note);
}

void jjffe_planet_792_chain_note_cpp_side(const char* side,
                                          intptr_t band,
                                          int step,
                                          void* job,
                                          void* cb,
                                          const char* fn,
                                          int d834_in,
                                          void* next,
                                          int d834_out,
                                          const char* note)
{
    open_if_needed();
    emit_row(side ? side : "cpp792", band, step, job, cb, fn, d834_in, next, d834_out, "step",
               note);
}

void jjffe_planet_792_chain_note_exit(const char* side,
                                      intptr_t band,
                                      int step,
                                      void* next)
{
    open_if_needed();
    emit_row(side ? side : "?", band, step, NULL, NULL, "", -1, next, 2, "exit", "");
}

#if defined(FFEASM_SHIP_DEMO)
void jjffe_planet_792_chain_pre_asm_side(const char* side, intptr_t band, void* job, int d834_in)
{
    open_if_needed();
    if (!job)
        return;
    s_asm_side = side ? side : "asm792";
    if (band != s_asm_band) {
        s_asm_band = band;
        s_asm_step = 0;
    }
    s_asm_job = job;
    s_asm_cb = *(void**)job;
    s_asm_fn = cb_name((uintptr_t)(uintptr_t)s_asm_cb);
    s_asm_d834_in = d834_in;
    s_asm_note[0] = '\0';
    if (s_asm_fn[0] == '0' && s_asm_fn[1] == '0' && s_asm_fn[2] == '1' &&
        s_asm_fn[3] == '8' && s_asm_fn[4] == '0' && s_asm_fn[5] == '5')
        fmt805_note(job, s_asm_note, sizeof(s_asm_note));
}

void jjffe_planet_792_chain_post_asm_side(const char* side, void* next, int d834_out)
{
    (void)side;
    if (!g_jjffe_planet_792_chain_enabled || !s_asm_job)
        return;
    emit_row(s_asm_side, s_asm_band, s_asm_step, s_asm_job, s_asm_cb, s_asm_fn,
               s_asm_d834_in, next, d834_out, "step", s_asm_note);
    s_asm_step++;
    s_asm_job = NULL;
}

void jjffe_planet_792_chain_pre_asm(intptr_t band, void* job, int d834_in)
{
    jjffe_planet_792_chain_pre_asm_side("asm792", band, job, d834_in);
}

void jjffe_planet_792_chain_post_asm(void* next, int d834_out)
{
    jjffe_planet_792_chain_post_asm_side("asm792", next, d834_out);
}

void jjffe_planet_792_chain_note_exit_asm(intptr_t band, void* next)
{
    open_if_needed();
    emit_row(s_asm_side ? s_asm_side : "asm792", band, s_asm_step, NULL, NULL, "", -1, next, 2,
             "exit", "");
}
#else
void jjffe_planet_792_chain_pre_asm_side(const char* side,
                                         intptr_t band,
                                         void* job,
                                         int d834_in)
{
    (void)side;
    (void)band;
    (void)job;
    (void)d834_in;
}

void jjffe_planet_792_chain_post_asm_side(const char* side, void* next, int d834_out)
{
    (void)side;
    (void)next;
    (void)d834_out;
}

void jjffe_planet_792_chain_pre_asm(intptr_t band, void* job, int d834_in)
{
    (void)band;
    (void)job;
    (void)d834_in;
}

void jjffe_planet_792_chain_post_asm(void* next, int d834_out)
{
    (void)next;
    (void)d834_out;
}

void jjffe_planet_792_chain_note_exit_asm(intptr_t band, void* next)
{
    (void)band;
    (void)next;
}
#endif
