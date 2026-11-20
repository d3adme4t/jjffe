#include "jjffe/planet_subdiv_trace.h"
#include "jjffe/planet_midpoint_trace.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_jjffe_planet_subdiv_enabled = 0;

int g_jjffe_subdiv_hook_event = 0;
int g_jjffe_subdiv_hook_depth = 0;
int g_jjffe_subdiv_hook_face = 0;
int g_jjffe_subdiv_hook_p5 = 0;
int g_jjffe_subdiv_hook_ri2 = 0;
int g_jjffe_subdiv_hook_ri3 = 0;
int g_jjffe_subdiv_hook_reason = 0;
int g_jjffe_subdiv_hook_e2hit = 0;
int g_jjffe_subdiv_hook_action = 0;
int g_jjffe_subdiv_hook_area48 = 0;
void* g_jjffe_subdiv_hook_v1 = NULL;
void* g_jjffe_subdiv_hook_v2 = NULL;
void* g_jjffe_subdiv_hook_v3 = NULL;
void* g_jjffe_subdiv_hook_edge = NULL;
uintptr_t g_jjffe_subdiv_hook_e0 = 0;
uintptr_t g_jjffe_subdiv_hook_e1 = 0;
uintptr_t g_jjffe_subdiv_hook_e2 = 0;
uintptr_t g_jjffe_subdiv_hook_e3 = 0;
int g_jjffe_subdiv_hook_raw6 = 0;
int g_jjffe_subdiv_hook_adj6 = 0;
int g_jjffe_subdiv_hook_idx278 = 0;
int g_jjffe_subdiv_hook_idx279 = 0;
int g_jjffe_subdiv_hook_sat = 0;

int g_jjffe_subdiv_active_face = -1;
int g_jjffe_subdiv_cur_depth = -1;

typedef struct {
    const char* event;
    int side;
    int depth;
    int face;
    int p5;
    int ri2;
    int ri3;
    int reason;
    int e2hit;
    int action;
    int area48;
    int sx0, sy0, sx1, sy1, sx2, sy2;
    /* 855 edge-cache dig */
    int slotIdx;
    int evLo;
    int evHi;
    int canonLo;
    int canonHi;
    int slotVisit;
    uintptr_t edgePtr;
    uintptr_t e0;
    uintptr_t e1;
    uintptr_t e2;
    uintptr_t e3;
} SubdivSample;

static SubdivSample s_samples[16384];
static int s_sampleN = 0;
static int s_cppN = 0;
static int s_asmN = 0;
static int s_n876 = 0;
static int s_n869 = 0;
static int s_nR13 = 0;
static int s_n855Skip = 0;
static int s_n855Create = 0;
static int s_n834 = 0;
static FILE* s_fp = NULL;

static const void* s_local988[30];
static int s_edgeVerts[60];
static int s_slotVisit[30];

static int vert_i16(const void* vp, int off)
{
    if (!vp)
        return 0;
    return *(const int16_t*)((const uint8_t*)vp + off);
}

static int edge_slot_index(const void* edgeSlot)
{
    if (!edgeSlot)
        return -1;
    for (int i = 0; i < 30; ++i) {
        if (s_local988[i] == edgeSlot)
            return i;
    }
    return -1;
}

static void edge_fields(const void* edgeSlot, int slotIdx,
                        uintptr_t* e0, uintptr_t* e1, uintptr_t* e2, uintptr_t* e3,
                        int* evLo, int* evHi)
{
    *e0 = *e1 = *e2 = *e3 = 0;
    *evLo = *evHi = -1;
    if (!edgeSlot)
        return;
    const uintptr_t* e = (const uintptr_t*)edgeSlot;
    *e0 = e[0];
    *e1 = e[1];
    *e2 = e[2];
    *e3 = e[3];
    if (slotIdx >= 0 && slotIdx < 30) {
        *evLo = s_edgeVerts[slotIdx * 2];
        *evHi = s_edgeVerts[slotIdx * 2 + 1];
    }
}

static void ensure_open(void)
{
    if (g_jjffe_planet_subdiv_enabled)
        return;
    const char* env = getenv("AB_1F_834_DISPATCH_TRACE");
    if (!env || !env[0])
        env = getenv("AB_1F_SUBDIV_TRACE");
    if (!env || !env[0])
        return;
    jjffe_planet_subdiv_trace_open(env);
}

static void append855(int side, int depth, int face, int p5, int ri2, int ri3,
                      int e2hit, int action, const void* v2, const void* v3,
                      void* edgeSlot, uintptr_t e0, uintptr_t e1, uintptr_t e2, uintptr_t e3)
{
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    const int slotIdx = edge_slot_index(edgeSlot);
    int evLo = -1;
    int evHi = -1;
    if (slotIdx >= 0 && slotIdx < 30) {
        evLo = s_edgeVerts[slotIdx * 2];
        evHi = s_edgeVerts[slotIdx * 2 + 1];
    }
    int canonLo = ri2;
    int canonHi = ri3;
    if (canonLo > canonHi) {
        const int t = canonLo;
        canonLo = canonHi;
        canonHi = t;
    }
    int slotVisit = 0;
    if (slotIdx >= 0 && slotIdx < 30)
        slotVisit = ++s_slotVisit[slotIdx];

    SubdivSample* s = &s_samples[s_sampleN++];
    s->event = "855";
    s->side = side;
    s->depth = depth;
    s->face = face;
    s->p5 = p5;
    s->ri2 = ri2;
    s->ri3 = ri3;
    s->reason = 0;
    s->e2hit = e2hit;
    s->action = action;
    s->area48 = 0;
    s->sx0 = 0;
    s->sy0 = 0;
    s->sx1 = vert_i16(v2, 0);
    s->sy1 = vert_i16(v2, 2);
    s->sx2 = vert_i16(v3, 0);
    s->sy2 = vert_i16(v3, 2);
    s->slotIdx = slotIdx;
    s->evLo = evLo;
    s->evHi = evHi;
    s->canonLo = canonLo;
    s->canonHi = canonHi;
    s->slotVisit = slotVisit;
    s->edgePtr = (uintptr_t)edgeSlot;
    s->e0 = e0;
    s->e1 = e1;
    s->e2 = e2;
    s->e3 = e3;
    if (side)
        ++s_asmN;
    else
        ++s_cppN;
}

static void append(int side, const char* event, int depth, int face, int p5,
                   int ri2, int ri3, int reason, int e2hit, int action, int area48,
                   const void* v1, const void* v2, const void* v3)
{
    if (s_sampleN >= (int)(sizeof(s_samples) / sizeof(s_samples[0])))
        return;
    SubdivSample* s = &s_samples[s_sampleN++];
    s->event = event;
    s->side = side;
    s->depth = depth;
    s->face = face;
    s->p5 = p5;
    s->ri2 = ri2;
    s->ri3 = ri3;
    s->reason = reason;
    s->e2hit = e2hit;
    s->action = action;
    s->area48 = area48;
    s->sx0 = vert_i16(v1, 0);
    s->sy0 = vert_i16(v1, 2);
    s->sx1 = vert_i16(v2, 0);
    s->sy1 = vert_i16(v2, 2);
    s->sx2 = vert_i16(v3, 0);
    s->sy2 = vert_i16(v3, 2);
    s->slotIdx = -1;
    s->evLo = s->evHi = -1;
    s->canonLo = s->canonHi = -1;
    s->slotVisit = 0;
    s->edgePtr = 0;
    s->e0 = s->e1 = s->e2 = s->e3 = 0;
    if (side)
        ++s_asmN;
    else
        ++s_cppN;
}

void jjffe_planet_subdiv_set_local988(const void* const* slots, const int* edgeVerts)
{
    memset(s_local988, 0, sizeof(s_local988));
    memset(s_edgeVerts, 0, sizeof(s_edgeVerts));
    memset(s_slotVisit, 0, sizeof(s_slotVisit));
    if (slots) {
        for (int i = 0; i < 30; ++i)
            s_local988[i] = slots[i];
    }
    if (edgeVerts)
        memcpy(s_edgeVerts, edgeVerts, sizeof(s_edgeVerts));
}

void jjffe_planet_subdiv_trace_open(const char* path)
{
    if (!path || !path[0])
        return;
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    s_fp = fopen(path, "w");
    if (!s_fp) {
        fprintf(stderr, "planet_subdiv_trace: cannot open %s\n", path);
        return;
    }
    s_sampleN = 0;
    s_cppN = 0;
    s_asmN = 0;
    s_n876 = 0;
    s_n869 = 0;
    s_nR13 = 0;
    s_n855Skip = 0;
    s_n855Create = 0;
    s_n834 = 0;
    memset(s_slotVisit, 0, sizeof(s_slotVisit));
    g_jjffe_subdiv_active_face = -1;
    g_jjffe_subdiv_cur_depth = -1;
    g_jjffe_planet_subdiv_enabled = 1;
    fprintf(stderr, "planet_subdiv_trace: writing %s\n", path);
}

void jjffe_planet_subdiv_trace_flush(void)
{
    if (!s_fp)
        return;
    fprintf(s_fp,
            "# subdiv cpp=%d asm=%d 876=%d 869=%d r13=%d 834=%d 855skip=%d 855create=%d\n",
            s_cppN, s_asmN, s_n876, s_n869, s_nR13, s_n834, s_n855Skip, s_n855Create);
    fprintf(s_fp,
            "event\tside\tdepth\tface\tp5\tri2\tri3\treason\te2hit\taction\tarea48\t"
            "sx0\tsy0\tsx1\tsy1\tsx2\tsy2\t"
            "slot\tevLo\tevHi\tcanonLo\tcanonHi\tslotVisit\tedgePtr\te0\te1\te2\te3\n");
    for (int i = 0; i < s_sampleN; ++i) {
        const SubdivSample* s = &s_samples[i];
        fprintf(s_fp,
                "%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t%llx\t%llx\t%llx\t%llx\t%llx\n",
                s->event, s->side ? "asm" : "cpp", s->depth, s->face, s->p5,
                s->ri2, s->ri3, s->reason, s->e2hit, s->action, s->area48,
                s->sx0, s->sy0, s->sx1, s->sy1, s->sx2, s->sy2,
                s->slotIdx, s->evLo, s->evHi, s->canonLo, s->canonHi, s->slotVisit,
                (unsigned long long)s->edgePtr, (unsigned long long)s->e0,
                (unsigned long long)s->e1, (unsigned long long)s->e2,
                (unsigned long long)s->e3);
    }
    fflush(s_fp);
}

void jjffe_planet_subdiv_trace_close(void)
{
    jjffe_planet_subdiv_trace_flush();
    if (s_fp) {
        fclose(s_fp);
        s_fp = NULL;
    }
    g_jjffe_planet_subdiv_enabled = 0;
    jjffe_planet_subdiv_set_local988(NULL, NULL);
}

void jjffe_planet_subdiv_note_876(int faceIndex, int param12)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    g_jjffe_subdiv_active_face = faceIndex;
    ++s_n876;
    append(0, "876", -1, faceIndex, -1, -1, -1, param12, 0, 0, 0, NULL, NULL, NULL);
}

/* Overload used by C++ icosa: record root-face screen verts for A/B. */
void jjffe_planet_subdiv_note_876_verts(int faceIndex, int param12,
                                        const void* v0, const void* v1, const void* v2)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    g_jjffe_subdiv_active_face = faceIndex;
    ++s_n876;
    append(0, "876", -1, faceIndex, -1, -1, -1, param12, 0, 0, 0, v0, v1, v2);
}

void jjffe_planet_subdiv_note_876_local1c(int local10, int camZ, int local1c)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    append(0, "876_lc", local10, -1, local1c, -1, -1, local1c, 0, 0, camZ, NULL, NULL, NULL);
}

void jjffe_planet_subdiv_note_834(int raw6, int p5, int adj6, int idx278, int idx279, int sat)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    ++s_n834;
    append(0, "834", -1, g_jjffe_subdiv_active_face, p5, adj6, idx278, raw6, idx279, sat, 0,
           NULL, NULL, NULL);
}

void jjffe_planet_subdiv_note_869_entry(int depth, int area48,
                                        const void* v1, const void* v2, const void* v3)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    g_jjffe_subdiv_cur_depth = depth;
    ++s_n869;
    append(0, "869", depth, g_jjffe_subdiv_active_face, -1, -1, -1, 0, 0, 0, area48,
           v1, v2, v3);
}

void jjffe_planet_subdiv_note_869_r13(int depth, int area48,
                                      const void* v1, const void* v2, const void* v3)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    ++s_nR13;
    append(0, "869_r13", depth, g_jjffe_subdiv_active_face, -1, -1, -1, 13, 0, 0, area48,
           v1, v2, v3);
}

void jjffe_planet_subdiv_note_855(void* edgeSlot, int p5, int ri2, int ri3, int e2hit, int action,
                                  const void* v2, const void* v3)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    if (e2hit)
        ++s_n855Skip;
    if (action)
        ++s_n855Create;
    uintptr_t e0 = 0, e1 = 0, e2 = 0, e3 = 0;
    int evLo = -1, evHi = -1;
    const int slotIdx = edge_slot_index(edgeSlot);
    edge_fields(edgeSlot, slotIdx, &e0, &e1, &e2, &e3, &evLo, &evHi);
    (void)evLo;
    (void)evHi;
    append855(0, g_jjffe_subdiv_cur_depth, g_jjffe_subdiv_active_face, p5, ri2, ri3,
              e2hit, action, v2, v3, edgeSlot, e0, e1, e2, e3);
}

void jjffe_planet_subdiv_note_mid_fn(int fn, void* edgeSlot, int p5, int ri2, int ri3, int e2hit,
                                     int action, const void* v2, const void* v3)
{
    char ev[8];
    snprintf(ev, sizeof(ev), "%d", fn);
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    if (e2hit)
        ++s_n855Skip;
    if (action == 1)
        ++s_n855Create;
    uintptr_t e0 = 0, e1 = 0, e2 = 0, e3 = 0;
    const int slotIdx = edge_slot_index(edgeSlot);
    edge_fields(edgeSlot, slotIdx, &e0, &e1, &e2, &e3, NULL, NULL);
    append855(0, g_jjffe_subdiv_cur_depth, g_jjffe_subdiv_active_face, p5, ri2, ri3,
              e2hit, action, v2, v3, edgeSlot, e0, e1, e2, e3);
    (void)ev;
}

void jjffe_planet_subdiv_asm_note(void)
{
    ensure_open();
    if (!g_jjffe_planet_subdiv_enabled)
        return;
    const int ev = g_jjffe_subdiv_hook_event;
    if (ev == 1) {
        g_jjffe_subdiv_active_face = g_jjffe_subdiv_hook_face;
    } else if (ev == 2) {
        g_jjffe_subdiv_cur_depth = g_jjffe_subdiv_hook_depth;
    }
    if (ev == 4) {
        g_jjffe_subdiv_hook_ri2 = jjffe_planet_root_index(g_jjffe_subdiv_hook_v2);
        g_jjffe_subdiv_hook_ri3 = jjffe_planet_root_index(g_jjffe_subdiv_hook_v3);
    }
    const char* name = "unk";
    if (ev == 1) {
        ++s_n876;
        name = "876";
    } else if (ev == 2) {
        ++s_n869;
        name = "869";
    } else if (ev == 3) {
        ++s_nR13;
        name = "869_r13";
    } else if (ev == 4) {
        if (g_jjffe_subdiv_hook_e2hit)
            ++s_n855Skip;
        if (g_jjffe_subdiv_hook_action)
            ++s_n855Create;
        name = "855";
    } else if (ev == 5) {
        ++s_n834;
        name = "834";
    } else if (ev == 6) {
        name = "876_lc";
    }
    if (ev == 4) {
        append855(1, g_jjffe_subdiv_cur_depth, g_jjffe_subdiv_active_face,
                  g_jjffe_subdiv_hook_p5, g_jjffe_subdiv_hook_ri2, g_jjffe_subdiv_hook_ri3,
                  g_jjffe_subdiv_hook_e2hit, g_jjffe_subdiv_hook_action,
                  g_jjffe_subdiv_hook_v2, g_jjffe_subdiv_hook_v3,
                  g_jjffe_subdiv_hook_edge,
                  g_jjffe_subdiv_hook_e0, g_jjffe_subdiv_hook_e1,
                  g_jjffe_subdiv_hook_e2, g_jjffe_subdiv_hook_e3);
        return;
    }
    if (ev == 5) {
        append(1, "834", -1, g_jjffe_subdiv_active_face,
               g_jjffe_subdiv_hook_p5, g_jjffe_subdiv_hook_adj6, g_jjffe_subdiv_hook_idx278,
               g_jjffe_subdiv_hook_raw6, g_jjffe_subdiv_hook_idx279, g_jjffe_subdiv_hook_sat, 0,
               NULL, NULL, NULL);
        return;
    }
    if (ev == 6) {
        append(1, "876_lc", g_jjffe_subdiv_hook_depth, -1,
               g_jjffe_subdiv_hook_p5, -1, -1, g_jjffe_subdiv_hook_reason, 0, 0,
               g_jjffe_subdiv_hook_area48, NULL, NULL, NULL);
        return;
    }
    const int face = (ev == 1) ? g_jjffe_subdiv_hook_face : g_jjffe_subdiv_active_face;
    append(1, name, g_jjffe_subdiv_hook_depth, face,
           g_jjffe_subdiv_hook_p5, g_jjffe_subdiv_hook_ri2, g_jjffe_subdiv_hook_ri3,
           g_jjffe_subdiv_hook_reason, g_jjffe_subdiv_hook_e2hit,
           g_jjffe_subdiv_hook_action, g_jjffe_subdiv_hook_area48,
           g_jjffe_subdiv_hook_v1, g_jjffe_subdiv_hook_v2, g_jjffe_subdiv_hook_v3);
}
