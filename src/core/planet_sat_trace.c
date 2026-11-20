#include "jjffe/planet_sat_trace.h"
#include <stdio.h>
#include <stdlib.h>

int g_jjffe_planet_sat_enabled = 0;

static FILE* s_fp = NULL;

static void ensure_open(void)
{
    if (g_jjffe_planet_sat_enabled)
        return;
    const char* env = getenv("AB_1F_SAT_TRACE");
    if (!env || !env[0])
        return;
    jjffe_planet_sat_trace_open(env);
}

void jjffe_planet_sat_trace_open(const char* path)
{
    if (!path || !*path)
        return;
    s_fp = fopen(path, "w");
    if (!s_fp)
        return;
    g_jjffe_planet_sat_enabled = 1;
    fprintf(s_fp, "event\tbase\thdr0\thdr1\tnonnull\tallocEnd");
    for (int i = 0; i < 0x14; ++i)
        fprintf(s_fp, "\tptr%02d", i);
    fprintf(s_fp, "\n");
}

void jjffe_planet_sat_trace_close(void)
{
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_sat_enabled = 0;
}

static void write_row(const char* event, uintptr_t base, int allocEnd)
{
    if (!s_fp || !base)
        return;
    const uint32_t* words = (const uint32_t*)base;
    uint32_t hdr0 = words[0];
    uint32_t hdr1 = words[1];
    int nonnull = 0;
    for (int i = 0; i < 0x14; ++i) {
        uint32_t p = *(const uint32_t*)(base + 8 + (size_t)i * 4);
        if (p)
            ++nonnull;
    }
    fprintf(s_fp, "%s\t%p\t%u\t%u\t%d\t%d", event, (void*)base, hdr0, hdr1, nonnull, allocEnd);
    for (int i = 0; i < 0x14; ++i) {
        uint32_t p = *(const uint32_t*)(base + 8 + (size_t)i * 4);
        fprintf(s_fp, "\t%p", (void*)(uintptr_t)p);
    }
    fprintf(s_fp, "\n");
    fflush(s_fp);
}

void jjffe_planet_sat_note_823(uintptr_t base, int allocEnd, const int16_t* stream)
{
    (void)stream;
    ensure_open();
    if (!g_jjffe_planet_sat_enabled)
        return;
    write_row("823", base, allocEnd);
}

void jjffe_planet_sat_note_876(uintptr_t local18)
{
    ensure_open();
    if (!g_jjffe_planet_sat_enabled)
        return;
    write_row("876", local18, -1);
}
