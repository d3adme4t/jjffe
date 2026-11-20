#include "jjffe/job_trace.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_job_trace_enabled = 0;

static FILE* s_fp = NULL;
static int s_seq = 0;
static int s_hdr = 0;

/* ASM flush banding: list-node sortKey while inside FUNC_001792…001793. */
static uint32_t s_bandKey = 0;
static int s_inBand = 0;

/* Last FUNC_001696 opcode word (mesh 0x06 control). */
static uint32_t s_meshOp = 0;
static int s_haveMeshOp = 0;

/* Draw-job submit sequence (words 5/6/12/13) → meshIndex column on ASM ENQ. */
static int s_submitSeq = 0;

static void ensure_header(void)
{
    if (!s_fp || s_hdr)
        return;
    fputs("seq\tphase\tjobType\tsortKey\tmeshIndex\ttexInfo\tmaterial\tcb\tnote\n", s_fp);
    fflush(s_fp);
    s_hdr = 1;
}

void jjffe_job_trace_open(const char* path)
{
    if (g_jjffe_job_trace_enabled && s_fp)
        return;
    jjffe_job_trace_close();
    if (!path || !path[0])
        return;
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "job_trace: cannot open %s\n", path);
        return;
    }
    s_seq = 0;
    s_hdr = 0;
    s_bandKey = 0;
    s_inBand = 0;
    s_meshOp = 0;
    s_haveMeshOp = 0;
    s_submitSeq = 0;
    g_jjffe_job_trace_enabled = 1;
    ensure_header();
    fprintf(stderr, "job_trace: writing %s\n", path);
}

void jjffe_job_trace_close(void)
{
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_job_trace_enabled = 0;
    s_seq = 0;
    s_hdr = 0;
    s_bandKey = 0;
    s_inBand = 0;
    s_meshOp = 0;
    s_haveMeshOp = 0;
    s_submitSeq = 0;
}

void jjffe_job_trace_flush(void)
{
    if (s_fp)
        fflush(s_fp);
}

void jjffe_job_trace_set_mesh_op(uint32_t opcode)
{
    s_meshOp = opcode;
    s_haveMeshOp = 1;
}

static void emit(const char* phase, const char* jobType, uint32_t sortKey,
                 int meshIndex, uint32_t texInfo, int material, uint32_t cb,
                 const char* note)
{
    if (!g_jjffe_job_trace_enabled)
        return;
    if (!s_fp) {
        const char* env = getenv("AB_JOB_TRACE");
        if (env && env[0])
            jjffe_job_trace_open(env);
        if (!s_fp)
            return;
    }
    ensure_header();
    fprintf(s_fp, "%d\t%s\t%s\t%u\t%d\t%u\t%d\t0x%x\t%s\n",
            s_seq++, phase, jobType ? jobType : "?", sortKey, meshIndex,
            texInfo, material, cb, note ? note : "");
    fflush(s_fp);
}

#if defined(FFEASM_SHIP_DEMO)
extern char FUNC_001683[], FUNC_001696[], FUNC_001708[], FUNC_001712[],
    FUNC_001738[], FUNC_001749[], FUNC_001750[], FUNC_001781[], FUNC_001786[],
    FUNC_001790[], FUNC_001792[], FUNC_001793[], FUNC_001794[], FUNC_001795[],
    FUNC_001796[], FUNC_001797[], FUNC_001798[], FUNC_001799[], FUNC_001800[],
    FUNC_001804[], FUNC_001805[], FUNC_001806[], FUNC_001807[], FUNC_001808[],
    FUNC_001809[];
/* Pixel back buffer (ffedat DATA_009226); DATA_007773[0] also points here. */
extern uint8_t DATA_009226[];

#include "mat_fp.inc"

static int count_vga22(void)
{
    const uint8_t* buf = DATA_009226;
    int n = 0;
    for (int i = 0; i < 320 * 200; i++)
        if (buf[i] == 22)
            n++;
    return n;
}

/* Courier #56 tip leading-edge dig: VGA6 in left tip bbox. */
static int count_tip6(void)
{
    const uint8_t* buf = DATA_009226;
    int n = 0;
    for (int y = 5; y <= 45; y++)
        for (int x = 0; x < 25; x++)
            if (buf[y * 320 + x] == 6)
                n++;
    return n;
}

static int s_v22_before = -1;
static int s_v22_pending = 0;
static uint32_t s_v22_cb = 0;
static uint32_t s_v22_tex = 0;
static int s_v22_mat = -1;
static uint32_t s_v22_key = 0;
static char s_v22_name[16];
static int s_tip6_before = -1;
static int s_tip6_pending = 0;

static const char* cb_name(uint32_t cbAbs)
{
    struct {
        const char* n;
        char* p;
    }     tab[] = {
        {"001790", FUNC_001790}, {"001792", FUNC_001792}, {"001793", FUNC_001793},
        {"001794", FUNC_001794}, {"001795", FUNC_001795}, {"001796", FUNC_001796},
        {"001797", FUNC_001797}, {"001798", FUNC_001798}, {"001799", FUNC_001799},
        {"001800", FUNC_001800},
        {"001804", FUNC_001804}, {"001805", FUNC_001805}, {"001806", FUNC_001806},
        {"001807", FUNC_001807}, {"001808", FUNC_001808}, {"001809", FUNC_001809},
    };
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i) {
        if ((uint32_t)(uintptr_t)tab[i].p == cbAbs)
            return tab[i].n;
    }
    return NULL;
}

/* Map return address → containing FUNC_* label (largest start ≤ ret). */
static const char* caller_name(uint32_t retAbs)
{
    struct {
        const char* n;
        char* p;
    } tab[] = {
        {"001683", FUNC_001683}, {"001696", FUNC_001696}, {"001708", FUNC_001708},
        {"001712", FUNC_001712}, {"001738", FUNC_001738}, {"001749", FUNC_001749},
        {"001750", FUNC_001750}, {"001781", FUNC_001781}, {"001786", FUNC_001786},
        {"001790", FUNC_001790}, {"001792", FUNC_001792},
    };
    const char* best = NULL;
    uint32_t bestAddr = 0;
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); ++i) {
        uint32_t a = (uint32_t)(uintptr_t)tab[i].p;
        if (a <= retAbs && a >= bestAddr) {
            bestAddr = a;
            best = tab[i].n;
        }
    }
    return best;
}

static int mat_from_tex_and_shade0(uint32_t texWord, uint16_t shade0)
{
    int found = -1;
    uint16_t s = (uint16_t)(shade0 & 0x0FFF);
    for (int i = 0; i < kMatFpCount; ++i) {
        if (kMatFp[i].tw != texWord || kMatFp[i].s0 != s)
            continue;
        if (found < 0)
            found = kMatFp[i].mat;
    }
    return found;
}

static uint32_t load_u32(uint32_t addr)
{
    return *(volatile uint32_t*)(uintptr_t)addr;
}

static uint16_t load_u16(uint32_t addr)
{
    return *(volatile uint16_t*)(uintptr_t)addr;
}
#else
static const char* cb_name(uint32_t cbAbs)
{
    (void)cbAbs;
    return NULL;
}
__attribute__((unused)) static const char* caller_name(uint32_t retAbs)
{
    (void)retAbs;
    return NULL;
}
#endif

void jjffe_job_trace_enq(int words, uint32_t sortKey, int linkFlag, uint32_t retAbs)
{
    int submit = -1;
    if (words == 5 || words == 6 || words == 12 || words == 13)
        submit = s_submitSeq++;

    char note[160];
#if defined(FFEASM_SHIP_DEMO)
    const char* who = caller_name(retAbs);
    if (s_haveMeshOp) {
        snprintf(note, sizeof(note),
                 "words=%d link=%d caller=%s ret=0x%x op=0x%x ctrlCase=%u",
                 words, linkFlag, who ? who : "?", retAbs, s_meshOp, s_meshOp >> 13);
    } else {
        snprintf(note, sizeof(note), "words=%d link=%d caller=%s ret=0x%x",
                 words, linkFlag, who ? who : "?", retAbs);
    }
#else
    (void)retAbs;
    snprintf(note, sizeof(note), "words=%d link=%d", words, linkFlag);
#endif
    emit("ENQ", "asm_alloc", sortKey, submit, 0, -1, retAbs, note);
}

void jjffe_job_trace_exec(uint32_t cbAbs, uint32_t argPtr)
{
    const char* name = cb_name(cbAbs);
    uint32_t sortKey = 0;
    uint32_t texInfo = 0;
    int material = -1;
    char note[256];

#if defined(FFEASM_SHIP_DEMO)
    if (name && strcmp(name, "001792") == 0) {
        s_bandKey = load_u32(argPtr - 8);
        s_inBand = 1;
        sortKey = s_bandKey;
        snprintf(note, sizeof(note), "fn=%s bandKey=%u cxxKey≈%u", name, sortKey,
                 sortKey * 8u);
    } else if (name && strcmp(name, "001793") == 0) {
        sortKey = s_bandKey;
        snprintf(note, sizeof(note), "fn=%s bandKey=%u", name, sortKey);
        s_inBand = 0;
    } else if (name && (strcmp(name, "001804") == 0 || strcmp(name, "001805") == 0 ||
                        strcmp(name, "001806") == 0 || strcmp(name, "001807") == 0 ||
                        strcmp(name, "001808") == 0 || strcmp(name, "001809") == 0)) {
        if (s_inBand)
            sortKey = s_bandKey;
        else
            sortKey = load_u32(argPtr - 8);

        uint16_t shade0 = 0;
        /* FUNC_001805 job: +4..+10 verts (x,y)×4, +0x14 drawTexWord, +0x18 shades */
        const int16_t* vv = (const int16_t*)(uintptr_t)(argPtr + 4);
        if (strcmp(name, "001805") == 0) {
            texInfo = load_u32(argPtr + 0x14);
            shade0 = load_u16(argPtr + 0x18);
            material = mat_from_tex_and_shade0(texInfo, shade0);
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)",
                     name, (unsigned)shade0, sortKey * 8u,
                     s_inBand ? " band" : " list",
                     (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
                     (int)vv[4], (int)vv[5], (int)vv[6], (int)vv[7]);
        } else if (strcmp(name, "001807") == 0) {
            /* FUNC_001807: +4/+8/+0xc tris verts, +0x10 drawTexWord, +0x14 shades */
            texInfo = load_u32(argPtr + 0x10);
            shade0 = load_u16(argPtr + 0x14);
            material = mat_from_tex_and_shade0(texInfo, shade0);
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s verts=(%d,%d)(%d,%d)(%d,%d)",
                     name, (unsigned)shade0, sortKey * 8u,
                     s_inBand ? " band" : " list",
                     (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
                     (int)vv[4], (int)vv[5]);
        } else if (strcmp(name, "001804") == 0) {
            texInfo = load_u16(argPtr + 0x10);
            shade0 = (uint16_t)texInfo;
            /* Flat DrawTriangle: +4/+8/+0xc = (x,y)×3. */
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s verts=(%d,%d)(%d,%d)(%d,%d)",
                     name, (unsigned)shade0, sortKey * 8u,
                     s_inBand ? " band" : " list",
                     (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
                     (int)vv[4], (int)vv[5]);
        } else if (strcmp(name, "001809") == 0) {
            texInfo = load_u16(argPtr + 0x14);
            shade0 = (uint16_t)texInfo;
            /* Flat DrawQuad: +4..+10 = (x,y)×4 screen verts (FUNC_001607). */
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)",
                     name, (unsigned)shade0, sortKey * 8u,
                     s_inBand ? " band" : " list",
                     (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
                     (int)vv[4], (int)vv[5], (int)vv[6], (int)vv[7]);
        } else if (strcmp(name, "001806") == 0 || strcmp(name, "001808") == 0) {
            /* 806/808 Det-UV tris: +4/+8/+0xc screen verts (same as 807). */
            texInfo = load_u32(argPtr + 0x14);
            shade0 = load_u16(argPtr + 0x18);
            material = mat_from_tex_and_shade0(texInfo, shade0);
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s verts=(%d,%d)(%d,%d)(%d,%d)",
                     name, (unsigned)shade0, sortKey * 8u,
                     s_inBand ? " band" : " list",
                     (int)vv[0], (int)vv[1], (int)vv[2], (int)vv[3],
                     (int)vv[4], (int)vv[5]);
        } else {
            snprintf(note, sizeof(note),
                     "fn=%s shade0=0x%x cxxKey≈%u%s", name, (unsigned)shade0,
                     sortKey * 8u, s_inBand ? " band" : " list");
        }
    } else if (name && strcmp(name, "001799") == 0) {
        /* Flat DrawSpans: +4 = RGB16 for DPal → fill color. */
        if (s_inBand)
            sortKey = s_bandKey;
        uint16_t rgb = load_u16(argPtr + 4);
        snprintf(note, sizeof(note), "fn=%s rgb16=0x%x", name, (unsigned)rgb);
    } else if (name && (strcmp(name, "001795") == 0 || strcmp(name, "001796") == 0)) {
        if (s_inBand)
            sortKey = s_bandKey;
        const int16_t* p = (const int16_t*)(uintptr_t)(argPtr + 4);
        snprintf(note, sizeof(note), "fn=%s p0=(%d,%d) p1=(%d,%d)", name,
                 (int)p[0], (int)p[1], (int)p[2], (int)p[3]);
    } else if (name && strcmp(name, "001797") == 0) {
        /* Bezier Contour edge: four packed screen points at +4..+16. */
        if (s_inBand)
            sortKey = s_bandKey;
        const int16_t* p = (const int16_t*)(uintptr_t)(argPtr + 4);
        snprintf(note, sizeof(note),
                 "fn=%s p0=(%d,%d) p1=(%d,%d) p2=(%d,%d) p3=(%d,%d)", name,
                 (int)p[0], (int)p[1], (int)p[2], (int)p[3],
                 (int)p[4], (int)p[5], (int)p[6], (int)p[7]);
    } else {
        if (s_inBand)
            sortKey = s_bandKey;
        snprintf(note, sizeof(note), "fn=%s arg=0x%x", name ? name : "?", argPtr);
    }
#else
    (void)argPtr;
    snprintf(note, sizeof(note), "arg=0x%x", argPtr);
#endif

    emit("EXEC", name ? name : "cb", sortKey, -1, texInfo, material, cbAbs, note);

#if defined(FFEASM_SHIP_DEMO)
    /* Snapshot VGA22 before the draw cb; after_draw emits the delta. */
    if (getenv("AB_JOB_TRACE_VGA22") && getenv("AB_JOB_TRACE_VGA22")[0] == '1' && name &&
        (strncmp(name, "00179", 5) == 0 || strncmp(name, "00180", 5) == 0)) {
        s_v22_before = count_vga22();
        s_v22_pending = 1;
        s_v22_cb = cbAbs;
        s_v22_tex = texInfo;
        s_v22_mat = material;
        s_v22_key = sortKey;
        snprintf(s_v22_name, sizeof(s_v22_name), "%s", name);
    } else {
        s_v22_pending = 0;
    }
    if (getenv("AB_JOB_TRACE_TIP6") && getenv("AB_JOB_TRACE_TIP6")[0] == '1' && name &&
        (strncmp(name, "00179", 5) == 0 || strncmp(name, "00180", 5) == 0 ||
         strcmp(name, "cb") == 0)) {
        s_tip6_before = count_tip6();
        s_tip6_pending = 1;
        if (!s_v22_pending) {
            s_v22_cb = cbAbs;
            s_v22_tex = texInfo;
            s_v22_mat = material;
            s_v22_key = sortKey;
            snprintf(s_v22_name, sizeof(s_v22_name), "%s", name ? name : "cb");
        }
    } else {
        s_tip6_pending = 0;
    }
#endif
}

void jjffe_job_trace_after_draw(void)
{
#if defined(FFEASM_SHIP_DEMO)
    if (getenv("AB_JOB_TRACE_TIP6") && getenv("AB_JOB_TRACE_TIP6")[0] == '1' &&
        s_tip6_pending) {
        int after = count_tip6();
        s_tip6_pending = 0;
        if (s_tip6_before >= 0 && after >= 0) {
            int d = after - s_tip6_before;
            if (d != 0) {
                char note[80];
                snprintf(note, sizeof(note), "dTip6=%+d total=%d", d, after);
                emit("DELTA", s_v22_name, s_v22_key, -1, s_v22_tex, s_v22_mat,
                     s_v22_cb, note);
            }
        }
    }
    if (!getenv("AB_JOB_TRACE_VGA22") || getenv("AB_JOB_TRACE_VGA22")[0] != '1')
        return;
    int after = count_vga22();
    if (!s_v22_pending) {
        /* Still report total so silent skips are visible. */
        char note[48];
        snprintf(note, sizeof(note), "nopending total=%d", after);
        emit("DELTA", "?", 0, -1, 0, -1, 0, note);
        return;
    }
    s_v22_pending = 0;
    if (s_v22_before < 0 || after < 0)
        return;
    char note[64];
    snprintf(note, sizeof(note), "dVga22=%+d total=%d", after - s_v22_before, after);
    emit("DELTA", s_v22_name, s_v22_key, -1, s_v22_tex, s_v22_mat, s_v22_cb, note);
#else
    (void)0;
#endif
}

void jjffe_job_trace_barrier(int mode, uint32_t sortKey, uint32_t retAbs)
{
    char note[160];
#if defined(FFEASM_SHIP_DEMO)
    const char* who = caller_name(retAbs);
    if (s_haveMeshOp) {
        snprintf(note, sizeof(note),
                 "mode=%d caller=%s ret=0x%x op=0x%x ctrlCase=%u slot=%u",
                 mode, who ? who : "?", retAbs, s_meshOp, s_meshOp >> 13,
                 s_meshOp & 0x1f);
    } else {
        snprintf(note, sizeof(note), "mode=%d caller=%s ret=0x%x", mode,
                 who ? who : "?", retAbs);
    }
#else
    (void)retAbs;
    snprintf(note, sizeof(note), "mode=%d", mode);
#endif
    emit("BARRIER", "barrier", sortKey, -1, s_haveMeshOp ? s_meshOp : 0, -1, retAbs,
         note);
}

void jjffe_job_trace_cpp_enq(uint32_t sortKey, int meshIndex, uint32_t texInfo,
                             int material, const char* jobType)
{
    emit("ENQ", jobType, sortKey, meshIndex, texInfo, material, 0, "pre_sort");
}

void jjffe_job_trace_cpp_exec(uint32_t sortKey, int meshIndex, uint32_t texInfo,
                              int material, const char* jobType, const char* note)
{
    emit("EXEC", jobType, sortKey, meshIndex, texInfo, material, 0,
         note ? note : "painter");
}
