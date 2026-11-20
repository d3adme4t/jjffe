// ab_plotstamp.cpp - env-gated per-pixel plot attribution for ship A/B digs.
// AB_PLOTBOX=x0,y0,x1,y1 arms it (default off; dig-only, prints to stderr).
#include "jjffe/ab_plotstamp.h"
#include <cstdio>
#include <cstdlib>

namespace jjffe {
namespace {
int g_box[4] = {0, 0, -1, -1};
bool g_init = false;
bool g_armed = false;
int g_n = 0;
int g_face = -1, g_vc = 0;
uint32_t g_tex = 0;
int32_t g_cx = 0, g_cy = 0;

void ensureInit() {
    if (g_init) return;
    g_init = true;
    if (const char* b = getenv("AB_PLOTBOX")) {
        if (sscanf(b, "%d,%d,%d,%d", &g_box[0], &g_box[1], &g_box[2], &g_box[3]) == 4 &&
            g_box[2] >= g_box[0])
            g_armed = true;
    }
}
}  // namespace

bool AbPlotArmed() {
    ensureInit();
    return g_armed;
}

void AbPlotSetFace(int meshIndex, int vertexCount, uint32_t texInfo,
                   int32_t camX, int32_t camY) {
    g_face = meshIndex;
    g_vc = vertexCount;
    g_tex = texInfo;
    g_cx = camX;
    g_cy = camY;
}

void AbPlotStamp(int x, int y, unsigned val, const char* kind) {
    AbPlotNoteWriter(x, y, val, kind);
    // R1201 dig: rare-event log for fixed-palette index 23 on the left edge
    // (bypasses AB_PLOTBOX's 4000-stamp cap, which otherwise silences late frames).
    if (val == 23 && x < 5 && y >= 60 && y <= 100) {
        static int s_i23 = 0;
        if (const char* tr = getenv("JJFFE_TRACE_IDX23")) {
            if (tr[0] && tr[0] != '0' && s_i23 < 200) {
                fprintf(stderr,
                        "IDX23 kind=%s face=%d n=%d tex=0x%04x at=(%d,%d) cam=(%d,%d)\n",
                        kind, g_face, g_vc, g_tex, x, y, g_cx, g_cy);
                ++s_i23;
            }
        }
    }
    if (!AbPlotArmed()) return;
    if (x < g_box[0] || x > g_box[2] || y < g_box[1] || y > g_box[3]) return;
    if (g_n < 4000) {
        fprintf(stderr, "PLOT kind=%s face=%d n=%d tex=0x%04x at=(%d,%d) val=%u cam=(%d,%d)\n",
                kind, g_face, g_vc, g_tex, x, y, val, g_cx, g_cy);
        ++g_n;
    }
}

namespace {
int g_px = -1, g_py = -1;
bool g_pixInit = false;
const char* g_lastKind[64][64] = {};
unsigned g_lastVal[64][64] = {};

void ensurePix() {
    if (g_pixInit) return;
    g_pixInit = true;
    if (const char* b = getenv("AB_PLOTPIX")) {
        sscanf(b, "%d,%d", &g_px, &g_py);
    }
}
}  // namespace

void AbPlotNoteWriter(int x, int y, unsigned val, const char* kind) {
    ensurePix();
    if (g_px < 0 || x != g_px || y != g_py) return;
    if (x >= 0 && x < 64 && y >= 0 && y < 64) {
        g_lastKind[x][y] = kind;
        g_lastVal[x][y] = val;
    }
}
const char* AbPlotLastWriter(int x, int y) {
    ensurePix();
    if (x != g_px || y != g_py) return "offtarget";
    if (g_n == 0) return "nowriters";
    return g_lastKind[x][y] ? g_lastKind[x][y] : "-";
}
unsigned AbPlotLastVal(int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return 0;
    return g_lastVal[x][y];
}

void AbPlotStampSpan(int y, int x0, int n, unsigned val, const char* kind) {
    if (getenv("AB_PLOTPIX")) {
        static int wsn = 0;
        ensurePix();
        if (g_px >= 0 && y == g_py && x0 <= g_px && g_px < x0 + n && wsn < 24) {
            fprintf(stderr, "WPIXSP kind=%s x0=%d n=%d val=%u\n", kind, x0, n, val);
            ++wsn;
        }
    }
    if (val == 23 && y >= 70 && y <= 95 && x0 < 5 && n > 0 && n <= 8) {
        static int s_i23s = 0;
        if (const char* tr = getenv("JJFFE_TRACE_IDX23")) {
            if (tr[0] && tr[0] != '0' && s_i23s < 400) {
                fprintf(stderr,
                        "IDX23SPAN kind=%s y=%d x0=%d n=%d face=%d tex=0x%04x\n",
                        kind, y, x0, n, g_face, g_tex);
                ++s_i23s;
            }
        }
    }
    for (int i = 0; i < n; i++) AbPlotNoteWriter(x0 + i, y, val, kind);
    if (!AbPlotArmed()) return;
    if (y < g_box[1] || y > g_box[3]) return;
    int a = x0 > g_box[0] ? x0 : g_box[0];
    int b = x0 + n - 1 < g_box[2] ? x0 + n - 1 : g_box[2];
    for (int x = a; x <= b && g_n < 4000; x++) {
        fprintf(stderr, "PLOT kind=%s face=%d n=%d tex=0x%04x at=(%d,%d) val=%u cam=(%d,%d)\n",
                kind, g_face, g_vc, g_tex, x, y, val, g_cx, g_cy);
        ++g_n;
    }
}
}  // namespace jjffe
