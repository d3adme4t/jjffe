#include "jjffe/planet_root_seed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char jjffe_planet_mat_side_asm[] = "asm";
const char jjffe_planet_mat_side_asm683[] = "asm683";
const char jjffe_planet_mat_side_asmobj[] = "asmobj";
const char jjffe_planet_mat_side_asm807[] = "asm807";

int g_jjffe_planet_root_seed_enabled = 0;

static int32_t s_mix[12];
static int32_t s_lod[12];
static int s_loaded = 0;
static int s_captureN = 0;
static FILE* s_fp = NULL;

static int vert_i32(const void* vp, int off)
{
    return *(const int32_t*)((const uint8_t*)vp + off);
}

void jjffe_planet_root_seed_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_root_seed: cannot open %s\n", path);
        return;
    }
    s_captureN = 0;
    g_jjffe_planet_root_seed_enabled = 1;
    fprintf(stderr, "planet_root_seed: capturing %s\n", path);
}

void jjffe_planet_root_seed_close(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp, "# planet root seed side=asm n=%d\n", s_captureN);
    fprintf(s_fp, "i\tmix0x3c\tlod0x44\n");
    for (int i = 0; i < s_captureN && i < 12; ++i)
        fprintf(s_fp, "%d\t%d\t%d\n", i, (int)s_mix[i], (int)s_lod[i]);
    fclose(s_fp);
    s_fp = NULL;
    g_jjffe_planet_root_seed_enabled = 0;
    fprintf(stderr, "planet_root_seed: wrote %d roots\n", s_captureN);
}

void jjffe_planet_root_seed_asm_capture(const void* verts)
{
    if (!g_jjffe_planet_root_seed_enabled || !verts)
        return;
    const uint8_t* base = (const uint8_t*)verts;
    s_captureN = 12;
    for (int i = 0; i < 12; ++i) {
        const uint8_t* rec = base + i * 0x48;
        s_mix[i] = vert_i32(rec, 0x3c);
        s_lod[i] = vert_i32(rec, 0x44);
    }
}

static int32_t canonical_mix(int32_t mix)
{
    if (mix == 0)
        return 0;
    return mix < 0 ? -1 : 1;
}

int jjffe_planet_root_seed_load(const char* path)
{
    if (!path || !path[0])
        return 0;
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "planet_root_seed: cannot read %s\n", path);
        return 0;
    }
    memset(s_mix, 0, sizeof(s_mix));
    memset(s_lod, 0, sizeof(s_lod));
    const int rawMix = getenv("AB_1F_ROOT_MIX_RAW") != NULL;
    int n = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == 'i')
            continue;
        int i = 0, mix = 0, lod = 0;
        const int got = sscanf(line, "%d\t%d\t%d", &i, &mix, &lod);
        if (got < 2)
            continue;
        if (i < 0 || i >= 12)
            continue;
        s_mix[i] = (int32_t)mix;
        if (got >= 3)
            s_lod[i] = (int32_t)lod;
        if (i + 1 > n)
            n = i + 1;
    }
    fclose(f);
    if (n < 12) {
        fprintf(stderr, "planet_root_seed: only %d/12 rows in %s\n", n, path);
        return 0;
    }
    if (!rawMix) {
        for (int i = 0; i < 12; ++i)
            s_mix[i] = canonical_mix(s_mix[i]);
    }
    s_loaded = 1;
    fprintf(stderr, "planet_root_seed: loaded %s%s\n", path,
            rawMix ? " (raw mix)" : " (sign-canonical mix)");
    return 1;
}

int jjffe_planet_root_seed_loaded(void)
{
    return s_loaded;
}

int32_t jjffe_planet_root_seed_mix(int index)
{
    if (index < 0 || index >= 12)
        return 0;
    return s_mix[index];
}

int32_t jjffe_planet_root_seed_lod(int index)
{
    if (index < 0 || index >= 12)
        return 0;
    return s_lod[index];
}

static FILE* s_state_fp = NULL;
static char s_state_path[1024];
static int s_state_asm_done = 0;

static int16_t vert_i16(const void* vp, int off)
{
    return *(const int16_t*)((const uint8_t*)vp + off);
}

void jjffe_planet_root_state_write_from_env(const void* verts12x48, const char* side)
{
    if (!verts12x48)
        return;
    const char* env = getenv("AB_1F_ROOT_STATE_TRACE");
    if (!env || !env[0])
        return;
    if (!s_state_fp || strcmp(s_state_path, env) != 0) {
        if (s_state_fp) {
            fclose(s_state_fp);
            s_state_fp = NULL;
        }
        strncpy(s_state_path, env, sizeof(s_state_path) - 1);
        s_state_path[sizeof(s_state_path) - 1] = '\0';
        const int exists = access(env, F_OK) == 0;
        s_state_fp = fopen(env, exists ? "a" : "w");
        if (!s_state_fp) {
            fprintf(stderr, "planet_root_state: cannot open %s\n", env);
            return;
        }
        if (!exists) {
            fprintf(stderr, "planet_root_state: writing %s\n", env);
            fprintf(s_state_fp,
                    "# planet root state (FUNC_001876 post-seed)\n"
                    "side\ti\tsx\tsy\tcamX\tcamY\tcamZ\tu28\tu2c\tu30\tmix0x3c\tlod0x44\n");
        } else {
            fprintf(stderr, "planet_root_state: appending %s side=%s\n", env,
                    side ? side : "?");
        }
    }
    const uint8_t* base = (const uint8_t*)verts12x48;
    for (int i = 0; i < 12; ++i) {
        const uint8_t* rec = base + i * 0x48;
        fprintf(s_state_fp,
                "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                side ? side : "?",
                i,
                (int)vert_i16(rec, 0),
                (int)vert_i16(rec, 2),
                vert_i32(rec, 0x04),
                vert_i32(rec, 0x08),
                vert_i32(rec, 0x0c),
                vert_i32(rec, 0x28),
                vert_i32(rec, 0x2c),
                vert_i32(rec, 0x30),
                vert_i32(rec, 0x3c),
                vert_i32(rec, 0x44));
    }
    fflush(s_state_fp);
}

void jjffe_planet_root_state_asm_hook(const void* verts12x48)
{
    if (s_state_asm_done)
        return;
    s_state_asm_done = 1;
    jjffe_planet_root_state_write_from_env(verts12x48, "asm");
}

static int s_mat_traced(const char* side)
{
    static const char* seen[16];
    static int n = 0;
    if (!side)
        return 1;
    for (int i = 0; i < n; ++i) {
        if (strcmp(seen[i], side) == 0)
            return 1;
    }
    if (n < 16)
        seen[n++] = side;
    return 0;
}

void jjffe_planet_mat_trace_mat9(const int32_t* m, const char* side)
{
    if (!m || !getenv("AB_1F_MAT_TRACE"))
        return;
    if (s_mat_traced(side))
        return;
    fprintf(stderr, "MAT %s", side ? side : "?");
    for (int i = 0; i < 9; ++i)
        fprintf(stderr, "\t%d(0x%08x)", (int)m[i], (unsigned)(uint32_t)m[i]);
    fprintf(stderr, "\n");
}

void jjffe_planet_mat_trace_from_env(const void* renderCtx, const char* side)
{
    if (!renderCtx)
        return;
    jjffe_planet_mat_trace_mat9(
        (const int32_t*)((const uint8_t*)renderCtx + 0x14), side);
}
