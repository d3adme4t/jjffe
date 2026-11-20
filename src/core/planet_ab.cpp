#include "jjffe/planet_ab.h"
#include "jjffe/data_globals.h"
#include "jjffe/draw.h"
#include "jjffe/platform.h"
#include "jjffe/video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

namespace jjffe {

static PlanetAbTrace g_trace;
static PlanetAbConfig g_cfg;

struct LeafSample {
    int depth = 0;
    int area2x = 0;
    int lod0 = 0, lod1 = 0, lod2 = 0;
    int reason = 0;
    int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
};
static LeafSample g_leaves[2048];
static int g_leafN = 0;

struct Cull869Sample {
    int reason = 0;
    int depth = 0;
    int local20 = 0;
    int margin = 0;
    int area48 = 0;
    int effDepth = 0;
    int cVar1 = 0;
    int gateReason = 0;
    int mix0 = 0, mix1 = 0, mix2 = 0;
    int z0 = 0, z1 = 0, z2 = 0;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0, cx2 = 0, cy2 = 0;
    int lod0 = 0, lod1 = 0, lod2 = 0;
    int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
};
static Cull869Sample g_cull869[2048];
static int g_cull869N = 0;
static int g_cull869ByReason[32]{};
static int g_cull869PaintN = 0;
static int g_rootVertMix[12]{};
static int g_rootVertZ[12]{};
static int g_rootVertN = 0;

static bool EnvEnabled(const char* name)
{
    const char* v = getenv(name);
    return v && v[0] != '\0' && v[0] != '0';
}

static bool Cull869TraceEnabled()
{
    return EnvEnabled("AB_1F_869_CULL_TRACE") || EnvEnabled("AB_1F_270_PAINT_TRACE")
        || EnvEnabled("AB_1F_DRAW_DEBUG");
}

bool PlanetAb869TraceEnabled()
{
    return Cull869TraceEnabled();
}

PlanetAbConfig PlanetAbLoadConfig()
{
    PlanetAbConfig cfg;
    if (const char* v = getenv("AB_1F_TRACE_FACE")) {
        cfg.traceFace = atoi(v);
        if (cfg.traceFace < 0 || cfg.traceFace > 19)
            cfg.traceFace = -1;
    }
    cfg.force868 = EnvEnabled("AB_1F_FORCE_1868");
    cfg.force869 = EnvEnabled("AB_1F_FORCE_1869");
    cfg.disableMixSkip = EnvEnabled("AB_1F_DISABLE_MIX_SKIP");
    if (const char* v = getenv("AB_1F_MIDPOINT_VARIANT"))
        cfg.midpointVariant = atoi(v);
    cfg.dumpRowSpans = EnvEnabled("AB_1F_DUMP_ROW_SPANS");
    g_cfg = cfg;
    return cfg;
}

const PlanetAbConfig& PlanetAbGetConfig()
{
    return g_cfg;
}

void PlanetAbResetTrace()
{
    g_trace = {};
    g_trace.activeFace = -1;
    g_trace.active = false;
    g_leafN = 0;
    g_cull869N = 0;
    g_cull869PaintN = 0;
    g_rootVertN = 0;
    std::memset(g_cull869ByReason, 0, sizeof(g_cull869ByReason));
    std::memset(g_rootVertMix, 0, sizeof(g_rootVertMix));
    std::memset(g_rootVertZ, 0, sizeof(g_rootVertZ));
}

void PlanetAbResetFacePass()
{
    g_trace.activeFace = -1;
    std::fill(std::begin(g_trace.faces), std::end(g_trace.faces), PlanetFaceTrace{});
    PlanetAbResetMidpointTrace();
}

PlanetAbTrace* PlanetAbGetTrace()
{
    return &g_trace;
}

static PlanetFaceTrace* ActiveFace()
{
    if (g_trace.activeFace < 0 || g_trace.activeFace >= 20)
        return nullptr;
    return &g_trace.faces[g_trace.activeFace];
}

void PlanetAbBeginFace(int faceIndex, uint32_t fn833Ret, uint32_t param10, int param11)
{
    if (faceIndex < 0 || faceIndex >= 20)
        return;
    // Keep only the latest icosa pass when leaf-tracing (intro holds re-draw).
    if (faceIndex == 0 && EnvEnabled("AB_1F_LEAF_TRACE"))
        g_leafN = 0;
    g_trace.activeFace = faceIndex;
    PlanetFaceTrace& f = g_trace.faces[faceIndex];
    f.face = faceIndex;
    f.fn833Ret = fn833Ret;
    f.param10 = param10;
    f.param11 = param11;
    f.path835 = 0;
}

void PlanetAbNote835Path(int pathCode)
{
    if (PlanetFaceTrace* f = ActiveFace())
        f->path835 = pathCode;
}

void PlanetAbNoteMixSkip()
{
    ++g_trace.totalMixSkip;
    if (PlanetFaceTrace* f = ActiveFace())
        ++f->mixSkipHits;
}

void PlanetAbNote868Call()
{
    ++g_trace.total868Calls;
    if (PlanetFaceTrace* f = ActiveFace())
        ++f->calls868;
}

void PlanetAbNote868EarlyReturn()
{
    if (PlanetFaceTrace* f = ActiveFace())
        ++f->early868;
}

void PlanetAbNote868LeafPaint()
{
    ++g_trace.total868Leaf;
    if (PlanetFaceTrace* f = ActiveFace())
        ++f->leafPaint868;
}

void PlanetAbNote868LeafDetail(int depth, int area2x, int lod0, int lod1, int lod2,
                               int reason, int sx0, int sy0, int sx1, int sy1,
                               int sx2, int sy2)
{
    // Record when face-trace is active OR AB_1F_LEAF_TRACE is set (intro digs
    // never activate PlanetAbGetTrace for title #445 Mercury).
    if (!g_trace.active && !EnvEnabled("AB_1F_LEAF_TRACE"))
        return;
    if (g_leafN >= (int)(sizeof(g_leaves) / sizeof(g_leaves[0])))
        return;
    LeafSample& s = g_leaves[g_leafN++];
    s.depth = depth;
    s.area2x = area2x;
    s.lod0 = lod0;
    s.lod1 = lod1;
    s.lod2 = lod2;
    s.reason = reason;
    s.sx0 = sx0; s.sy0 = sy0;
    s.sx1 = sx1; s.sy1 = sy1;
    s.sx2 = sx2; s.sy2 = sy2;
}

void PlanetAbDumpLeafTrace(const char* path)
{
    if (!path)
        return;
    FILE* f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "i\tdepth\tarea2x\tlod0\tlod1\tlod2\treason\tsx0\tsy0\tsx1\tsy1\tsx2\tsy2\n");
    for (int i = 0; i < g_leafN; ++i) {
        const LeafSample& s = g_leaves[i];
        fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                i, s.depth, s.area2x, s.lod0, s.lod1, s.lod2, s.reason,
                s.sx0, s.sy0, s.sx1, s.sy1, s.sx2, s.sy2);
    }
    fprintf(f, "# n=%d DATA_009269=%u DATA_007837=%u\n",
            g_leafN, (unsigned)DATA_009269, (unsigned)DATA_007837);
    fclose(f);
}

void PlanetAbNote869Cull(int reason, int depth, int local20, int margin, int area48,
                         int16_t* v1, int16_t* v2, int16_t* v3)
{
    if (!Cull869TraceEnabled())
        return;
    if (depth > 3 && !EnvEnabled("AB_1F_869_PARENT_TRACE"))
        return;
    if (EnvEnabled("AB_1F_869_PARENT_TRACE")) {
        auto mix = [](int16_t* v) -> int {
            return v ? *(int32_t*)((intptr_t)v + 0x3c) : 0;
        };
        const int m0 = mix(v1), m1 = mix(v2), m2 = mix(v3);
        static const int w[] = {
            -23366516, -16567464, -353485, 12932394, 36220995, 63924339, 95250570,
            126576801, -114538325, 58254165, 71819994, -57995625, -110232862, 74892572
        };
        bool hit = false;
        for (int m : {m0, m1, m2}) {
            for (int ww : w) {
                if (m == ww) { hit = true; break; }
            }
            if (hit) break;
        }
        if (!hit)
            return;
    }
    if (reason >= 0 && reason < 32)
        ++g_cull869ByReason[reason];
    if (g_cull869N >= (int)(sizeof(g_cull869) / sizeof(g_cull869[0])))
        return;
    Cull869Sample& s = g_cull869[g_cull869N++];
    s.reason = reason;
    s.depth = depth;
    s.local20 = local20;
    s.margin = margin;
    s.area48 = area48;
    if (v1) {
        s.mix0 = *(int32_t*)((intptr_t)v1 + 0x3c);
        s.z0 = *(int32_t*)((intptr_t)v1 + 0x0c);
        s.cx0 = *(int32_t*)((intptr_t)v1 + 0x04);
        s.cy0 = *(int32_t*)((intptr_t)v1 + 0x08);
        s.lod0 = *(int32_t*)((intptr_t)v1 + 0x44);
        s.sx0 = v1[0]; s.sy0 = v1[1];
    }
    if (v2) {
        s.mix1 = *(int32_t*)((intptr_t)v2 + 0x3c);
        s.z1 = *(int32_t*)((intptr_t)v2 + 0x0c);
        s.cx1 = *(int32_t*)((intptr_t)v2 + 0x04);
        s.cy1 = *(int32_t*)((intptr_t)v2 + 0x08);
        s.lod1 = *(int32_t*)((intptr_t)v2 + 0x44);
        s.sx1 = v2[0]; s.sy1 = v2[1];
    }
    if (v3) {
        s.mix2 = *(int32_t*)((intptr_t)v3 + 0x3c);
        s.z2 = *(int32_t*)((intptr_t)v3 + 0x0c);
        s.cx2 = *(int32_t*)((intptr_t)v3 + 0x04);
        s.cy2 = *(int32_t*)((intptr_t)v3 + 0x08);
        s.lod2 = *(int32_t*)((intptr_t)v3 + 0x44);
        s.sx2 = v3[0]; s.sy2 = v3[1];
    }
    if (reason == 12 && v1 && v2 && v3) {
        const int cVar1 = (*(int32_t*)((intptr_t)v1 + 0x3c) <= 0) +
                          (*(int32_t*)((intptr_t)v2 + 0x3c) <= 0) +
                          (*(int32_t*)((intptr_t)v3 + 0x3c) <= 0);
        s.cVar1 = cVar1;
        s.effDepth = (cVar1 != 0 && cVar1 != 3) ? depth - 1 : depth;
    }
}

void PlanetAbNote869Paint(int depth, int effDepth, int cVar1, int area48, int gateReason,
                          int16_t* v1, int16_t* v2, int16_t* v3)
{
    if (!Cull869TraceEnabled())
        return;
    if (EnvEnabled("AB_1F_869_PARENT_TRACE")) {
        auto mix = [](int16_t* v) -> int {
            return v ? *(int32_t*)((intptr_t)v + 0x3c) : 0;
        };
        const int m0 = mix(v1), m1 = mix(v2), m2 = mix(v3);
        static const int w[] = {
            -23366516, -16567464, -353485, 12932394, 36220995, 63924339, 95250570,
            126576801, -114538325, 58254165, 71819994, -57995625, -110232862, 74892572
        };
        bool hit = false;
        for (int m : {m0, m1, m2}) {
            for (int ww : w) {
                if (m == ww) { hit = true; break; }
            }
            if (hit) break;
        }
        if (!hit)
            return;
    }
    ++g_cull869PaintN;
    if (g_cull869N >= (int)(sizeof(g_cull869) / sizeof(g_cull869[0])))
        return;
    Cull869Sample& s = g_cull869[g_cull869N++];
    s.reason = 99; // leaf paint via DATA_009270
    s.depth = depth;
    s.effDepth = effDepth;
    s.cVar1 = cVar1;
    s.area48 = area48;
    s.gateReason = gateReason;
    if (v1) {
        s.mix0 = *(int32_t*)((intptr_t)v1 + 0x3c);
        s.lod0 = *(int32_t*)((intptr_t)v1 + 0x44);
        s.z0 = *(int32_t*)((intptr_t)v1 + 0x0c);
        s.cx0 = *(int32_t*)((intptr_t)v1 + 0x04);
        s.cy0 = *(int32_t*)((intptr_t)v1 + 0x08);
        s.sx0 = v1[0]; s.sy0 = v1[1];
    }
    if (v2) {
        s.mix1 = *(int32_t*)((intptr_t)v2 + 0x3c);
        s.lod1 = *(int32_t*)((intptr_t)v2 + 0x44);
        s.z1 = *(int32_t*)((intptr_t)v2 + 0x0c);
        s.cx1 = *(int32_t*)((intptr_t)v2 + 0x04);
        s.cy1 = *(int32_t*)((intptr_t)v2 + 0x08);
        s.sx1 = v2[0]; s.sy1 = v2[1];
    }
    if (v3) {
        s.mix2 = *(int32_t*)((intptr_t)v3 + 0x3c);
        s.lod2 = *(int32_t*)((intptr_t)v3 + 0x44);
        s.z2 = *(int32_t*)((intptr_t)v3 + 0x0c);
        s.cx2 = *(int32_t*)((intptr_t)v3 + 0x04);
        s.cy2 = *(int32_t*)((intptr_t)v3 + 0x08);
        s.sx2 = v3[0]; s.sy2 = v3[1];
    }
}

void PlanetAbNote869Subdivide(int depth, int effDepth, int cVar1, int area48,
                              int local20, int margin,
                              int16_t* v1, int16_t* v2, int16_t* v3)
{
    /* depth>3 skipped unless AB_1F_869_PARENT_TRACE — need deep parents for face forks */
    if (!Cull869TraceEnabled())
        return;
    if (depth > 3 && !EnvEnabled("AB_1F_869_PARENT_TRACE"))
        return;
    if (EnvEnabled("AB_1F_869_PARENT_TRACE")) {
        auto mix = [](int16_t* v) -> int {
            return v ? *(int32_t*)((intptr_t)v + 0x3c) : 0;
        };
        const int m0 = mix(v1), m1 = mix(v2), m2 = mix(v3);
        static const int w[] = {
            -23366516, -16567464, -353485, 12932394, 36220995, 63924339, 95250570,
            126576801, -114538325, 58254165, 71819994, -57995625, -110232862, 74892572
        };
        bool hit = false;
        for (int m : {m0, m1, m2}) {
            for (int ww : w) {
                if (m == ww) { hit = true; break; }
            }
            if (hit) break;
        }
        if (!hit)
            return;
    }
    ++g_cull869ByReason[14];
    if (g_cull869N >= (int)(sizeof(g_cull869) / sizeof(g_cull869[0])))
        return;
    Cull869Sample& s = g_cull869[g_cull869N++];
    s.reason = 14;
    s.depth = depth;
    s.effDepth = effDepth;
    s.cVar1 = cVar1;
    s.area48 = area48;
    s.local20 = local20;
    s.margin = margin;
    if (v1) {
        s.mix0 = *(int32_t*)((intptr_t)v1 + 0x3c);
        s.lod0 = *(int32_t*)((intptr_t)v1 + 0x44);
        s.z0 = *(int32_t*)((intptr_t)v1 + 0x0c);
        s.cx0 = *(int32_t*)((intptr_t)v1 + 0x04);
        s.cy0 = *(int32_t*)((intptr_t)v1 + 0x08);
        s.sx0 = v1[0]; s.sy0 = v1[1];
    }
    if (v2) {
        s.mix1 = *(int32_t*)((intptr_t)v2 + 0x3c);
        s.lod1 = *(int32_t*)((intptr_t)v2 + 0x44);
        s.z1 = *(int32_t*)((intptr_t)v2 + 0x0c);
        s.cx1 = *(int32_t*)((intptr_t)v2 + 0x04);
        s.cy1 = *(int32_t*)((intptr_t)v2 + 0x08);
        s.sx1 = v2[0]; s.sy1 = v2[1];
    }
    if (v3) {
        s.mix2 = *(int32_t*)((intptr_t)v3 + 0x3c);
        s.lod2 = *(int32_t*)((intptr_t)v3 + 0x44);
        s.z2 = *(int32_t*)((intptr_t)v3 + 0x0c);
        s.cx2 = *(int32_t*)((intptr_t)v3 + 0x04);
        s.cy2 = *(int32_t*)((intptr_t)v3 + 0x08);
        s.sx2 = v3[0]; s.sy2 = v3[1];
    }
}

void PlanetAbNote876RootVert(int index, int mix0x3c, int camZ)
{
    if (!Cull869TraceEnabled())
        return;
    if (index < 0 || index >= 12)
        return;
    g_rootVertMix[index] = mix0x3c;
    g_rootVertZ[index] = camZ;
    if (index + 1 > g_rootVertN)
        g_rootVertN = index + 1;
}

void PlanetAbDump869CullTrace(const char* path)
{
    if (!path)
        return;
    FILE* f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "i\treason\tdepth\teffDepth\tcVar1\tgate\tlocal20\tmargin\tarea48\t"
                "mix0\tmix1\tmix2\tz0\tz1\tz2\tcx0\tcy0\tcx1\tcy1\tcx2\tcy2\t"
                "lod0\tlod1\tlod2\tsx0\tsy0\tsx1\tsy1\tsx2\tsy2\n");
    for (int i = 0; i < g_cull869N; ++i) {
        const Cull869Sample& s = g_cull869[i];
        fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                i, s.reason, s.depth, s.effDepth, s.cVar1, s.gateReason,
                s.local20, s.margin, s.area48,
                s.mix0, s.mix1, s.mix2,
                s.z0, s.z1, s.z2,
                s.cx0, s.cy0, s.cx1, s.cy1, s.cx2, s.cy2,
                s.lod0, s.lod1, s.lod2,
                s.sx0, s.sy0, s.sx1, s.sy1, s.sx2, s.sy2);
    }
    fprintf(f, "# n=%d paint=%d DATA_009269=%u DATA_007837=%u\n",
            g_cull869N, g_cull869PaintN, (unsigned)DATA_009269, (unsigned)DATA_007837);
    fprintf(f, "# by_reason");
    for (int r = 0; r < 32; ++r) {
        if (g_cull869ByReason[r])
            fprintf(f, " r%d=%d", r, g_cull869ByReason[r]);
    }
    fprintf(f, "\n");

    if (g_rootVertN > 0) {
        fprintf(f, "# root_vert i\tmix0x3c\tcamZ\tmix_lt1\n");
        int rootMixLt1 = 0;
        for (int i = 0; i < g_rootVertN; ++i) {
            const int lt1 = g_rootVertMix[i] < 1 ? 1 : 0;
            rootMixLt1 += lt1;
            fprintf(f, "# root\t%d\t%d\t%d\t%d\n", i, g_rootVertMix[i], g_rootVertZ[i], lt1);
        }
        fprintf(f, "# root_mix_lt1=%d/%d\n", rootMixLt1, g_rootVertN);
    }

    int paintByDepth[16]{};
    int paintByCVar1[4]{};
    int paintByGate[4]{};
    std::vector<int> paintAreas;
    paintAreas.reserve(g_cull869PaintN);
    for (int i = 0; i < g_cull869N; ++i) {
        const Cull869Sample& s = g_cull869[i];
        if (s.reason != 99)
            continue;
        if (s.depth >= 0 && s.depth < 16)
            ++paintByDepth[s.depth];
        if (s.cVar1 >= 0 && s.cVar1 < 4)
            ++paintByCVar1[s.cVar1];
        if (s.gateReason >= 0 && s.gateReason < 4)
            ++paintByGate[s.gateReason];
        paintAreas.push_back(s.area48 >= 0 ? s.area48 : -s.area48);
    }
    fprintf(f, "# paint_by_depth");
    for (int d = 0; d < 16; ++d) {
        if (paintByDepth[d])
            fprintf(f, " d%d=%d", d, paintByDepth[d]);
    }
    fprintf(f, "\n# paint_by_cVar1 c0=%d c1=%d c2=%d c3=%d\n",
            paintByCVar1[0], paintByCVar1[1], paintByCVar1[2], paintByCVar1[3]);
    fprintf(f, "# paint_by_gate fallback=%d depth13=%d lod=%d small=%d\n",
            paintByGate[0], paintByGate[1], paintByGate[2], paintByGate[3]);
    if (!paintAreas.empty()) {
        std::sort(paintAreas.begin(), paintAreas.end());
        const int med = paintAreas[paintAreas.size() / 2];
        int64_t sum = 0;
        int lt50 = 0;
        for (int a : paintAreas) {
            sum += a;
            if (a < 50)
                ++lt50;
        }
        fprintf(f, "# paint_area n=%d median=%d sum=%lld lt50=%d\n",
                (int)paintAreas.size(), med, (long long)sum, lt50);
        for (int d = 0; d < 16; ++d) {
            if (!paintByDepth[d])
                continue;
            std::vector<int> byDepth;
            for (int i = 0; i < g_cull869N; ++i) {
                const Cull869Sample& s = g_cull869[i];
                if (s.reason == 99 && s.depth == d)
                    byDepth.push_back(s.area48 >= 0 ? s.area48 : -s.area48);
            }
            if (byDepth.empty())
                continue;
            std::sort(byDepth.begin(), byDepth.end());
            fprintf(f, "# paint_area_d%d median=%d\n", d, byDepth[byDepth.size() / 2]);
        }
    }
    fclose(f);
}

void PlanetAbSnapshotPools(int nodes816, int nodes820)
{
    if (PlanetFaceTrace* f = ActiveFace()) {
        f->nodes816 = nodes816;
        f->nodes820 = nodes820;
    }
}

void PlanetAbNoteJob778(int words)
{
    ++g_trace.totalJobs778;
    if (words >= 0 && words < 32)
        ++g_trace.jobs778ByWords[words];
}

void PlanetAbNote842()
{
    ++g_trace.total842;
}

void PlanetAbNote840()
{
    ++g_trace.total840;
}

static void MeasureBuffer(const uint8_t* buf, int* pixels, double* cx, double* cy,
                          int bbox[4])
{
    int64_t sx = 0;
    int64_t sy = 0;
    *pixels = 0;
    bbox[0] = SCREEN_WIDTH - 1;
    bbox[1] = SCREEN_HEIGHT - 1;
    bbox[2] = 0;
    bbox[3] = 0;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            const uint8_t p = buf[y * SCREEN_WIDTH + x];
            if (p == 0)
                continue;
            ++(*pixels);
            sx += x;
            sy += y;
            if (x < bbox[0]) bbox[0] = x;
            if (y < bbox[1]) bbox[1] = y;
            if (x > bbox[2]) bbox[2] = x;
            if (y > bbox[3]) bbox[3] = y;
        }
    }
    if (*pixels > 0) {
        *cx = (double)sx / *pixels;
        *cy = (double)sy / *pixels;
    } else {
        bbox[0] = bbox[1] = bbox[2] = bbox[3] = 0;
    }
}

static void BuildRowSpans(const uint8_t* buf, PlanetRowSpan out[SCREEN_HEIGHT])
{
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        out[y].y = y;
        out[y].x0 = SCREEN_WIDTH;
        out[y].x1 = -1;
        out[y].count = 0;
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            if (buf[y * SCREEN_WIDTH + x] == 0)
                continue;
            ++out[y].count;
            if (x < out[y].x0) out[y].x0 = x;
            if (x > out[y].x1) out[y].x1 = x;
        }
        if (out[y].count == 0) {
            out[y].x0 = 0;
            out[y].x1 = -1;
        }
    }
}

bool PlanetAbLoadBmp320x200(const char* path, uint8_t* out)
{
    if (!path || !out)
        return false;
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t hdr[54];
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(f);
        return false;
    }
    const uint32_t offset = *(uint32_t*)(hdr + 10);
    const int32_t width = *(int32_t*)(hdr + 18);
    const int32_t height = *(int32_t*)(hdr + 22);
    const uint16_t bpp = *(uint16_t*)(hdr + 28);
    if (width != SCREEN_WIDTH || (height != SCREEN_HEIGHT && height != -SCREEN_HEIGHT) ||
        bpp != 8) {
        fclose(f);
        return false;
    }
    fseek(f, (long)offset, SEEK_SET);
    uint8_t raw[SCREEN_SIZE];
    if (fread(raw, 1, SCREEN_SIZE, f) != SCREEN_SIZE) {
        fclose(f);
        return false;
    }
    fclose(f);
    if (height > 0) {
        for (int y = 0; y < SCREEN_HEIGHT; ++y)
            std::memcpy(out + y * SCREEN_WIDTH,
                        raw + (SCREEN_HEIGHT - 1 - y) * SCREEN_WIDTH, SCREEN_WIDTH);
    } else {
        std::memcpy(out, raw, SCREEN_SIZE);
    }
    return true;
}

bool PlanetAbSaveBmp320x200(const char* path, const uint8_t* pixels,
                            const uint8_t* palette384)
{
    if (!path || !pixels)
        return false;
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const int rowPad = (4 - (SCREEN_WIDTH % 4)) % 4;
    const int rowSize = SCREEN_WIDTH + rowPad;
    const int imageSize = rowSize * SCREEN_HEIGHT;
    const int paletteSize = 256 * 4;
    const int fileSize = 54 + paletteSize + imageSize;
    uint8_t hdr[54]{};
    hdr[0] = 'B';
    hdr[1] = 'M';
    *(uint32_t*)(hdr + 2) = (uint32_t)fileSize;
    *(uint32_t*)(hdr + 10) = 54 + paletteSize;
    *(int32_t*)(hdr + 18) = SCREEN_WIDTH;
    *(int32_t*)(hdr + 22) = -SCREEN_HEIGHT;
    *(uint16_t*)(hdr + 26) = 1;
    *(uint16_t*)(hdr + 28) = 8;
    *(uint32_t*)(hdr + 34) = (uint32_t)imageSize;
    fwrite(hdr, 1, 54, f);
    for (int i = 0; i < 256; ++i) {
        uint8_t rgb[3] = {0, 0, 0};
        if (palette384) {
            rgb[0] = palette384[i * 3 + 0] << 2;
            rgb[1] = palette384[i * 3 + 1] << 2;
            rgb[2] = palette384[i * 3 + 2] << 2;
        } else {
            rgb[0] = rgb[1] = rgb[2] = (uint8_t)(i ? (32 + i * 223 / 255) : 0);
        }
        fputc(rgb[2], f);
        fputc(rgb[1], f);
        fputc(rgb[0], f);
        fputc(0, f);
    }
    uint8_t pad[3] = {0, 0, 0};
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        fwrite(pixels + y * SCREEN_WIDTH, 1, SCREEN_WIDTH, f);
        if (rowPad)
            fwrite(pad, 1, rowPad, f);
    }
    fclose(f);
    return true;
}

void PlanetAbCompareRegion(const uint8_t* cpp, const uint8_t* asmBuf,
                           PlanetRegionDiff* out)
{
    if (!cpp || !asmBuf || !out)
        return;
    PlanetRegionDiff d{};
    uint8_t asmOnlyMask[SCREEN_SIZE]{};
    int bestArea = 0;
    int bestX0 = 0, bestY0 = 0, bestW = 0, bestH = 0;

    for (int i = 0; i < SCREEN_SIZE; ++i) {
        const uint8_t c = cpp[i];
        const uint8_t a = asmBuf[i];
        if (c == 0 && a == 0)
            continue;
        if (c == 0 && a > 0) {
            ++d.asmOnly;
            asmOnlyMask[i] = 1;
        } else if (c > 0 && a == 0) {
            ++d.cppOnly;
        } else if (c == a) {
            ++d.bothSame;
        } else {
            ++d.bothDifferent;
        }
    }

    MeasureBuffer(asmBuf, &d.asmPixels, &d.asmCx, &d.asmCy, d.asmBbox);
    MeasureBuffer(cpp, &d.cppPixels, &d.cppCx, &d.cppCy, d.cppBbox);
    BuildRowSpans(asmBuf, d.asmRowSpans);
    BuildRowSpans(cpp, d.cppRowSpans);

    uint8_t seen[SCREEN_SIZE]{};
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            const int i = y * SCREEN_WIDTH + x;
            if (!asmOnlyMask[i] || seen[i])
                continue;
            int minX = x, maxX = x, minY = y, maxY = y;
            int area = 0;
            std::queue<std::pair<int, int>> q;
            q.push({x, y});
            seen[i] = 1;
            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();
                ++area;
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
                static const int dx[4] = {1, -1, 0, 0};
                static const int dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + dx[k];
                    const int ny = cy + dy[k];
                    if (nx < 0 || nx >= SCREEN_WIDTH || ny < 0 || ny >= SCREEN_HEIGHT)
                        continue;
                    const int ni = ny * SCREEN_WIDTH + nx;
                    if (!asmOnlyMask[ni] || seen[ni])
                        continue;
                    seen[ni] = 1;
                    q.push({nx, ny});
                }
            }
            if (area > bestArea) {
                bestArea = area;
                bestX0 = minX;
                bestY0 = minY;
                bestW = maxX - minX + 1;
                bestH = maxY - minY + 1;
            }
        }
    }
    d.largestAsmOnlyArea = bestArea;
    d.largestAsmOnlyX0 = bestX0;
    d.largestAsmOnlyY0 = bestY0;
    d.largestAsmOnlyW = bestW;
    d.largestAsmOnlyH = bestH;
    *out = d;
}

static void WriteMaskPpm(const char* path, const uint8_t* mask, uint8_t r, uint8_t g,
                         uint8_t b)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        if (mask[i]) {
            fputc(r, f);
            fputc(g, f);
            fputc(b, f);
        } else {
            fputc(0, f);
            fputc(0, f);
            fputc(0, f);
        }
    }
    fclose(f);
}

void PlanetAbWriteDiffPpm(const char* dir, const uint8_t* cpp, const uint8_t* asmBuf,
                          const PlanetRegionDiff& diff)
{
    (void)diff;
    if (!dir || !cpp || !asmBuf)
        return;
    char path[512];
    uint8_t mask[SCREEN_SIZE];
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        const uint8_t c = cpp[i];
        const uint8_t a = asmBuf[i];
        mask[i] = (c == 0 && a > 0) ? 1 : 0;
    }
    snprintf(path, sizeof(path), "%s/asm_only.ppm", dir);
    WriteMaskPpm(path, mask, 255, 64, 64);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        const uint8_t c = cpp[i];
        const uint8_t a = asmBuf[i];
        mask[i] = (c > 0 && a == 0) ? 1 : 0;
    }
    snprintf(path, sizeof(path), "%s/cpp_only.ppm", dir);
    WriteMaskPpm(path, mask, 64, 64, 255);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        const uint8_t c = cpp[i];
        const uint8_t a = asmBuf[i];
        mask[i] = (c > 0 && a > 0 && c == a) ? 1 : 0;
    }
    snprintf(path, sizeof(path), "%s/both_same.ppm", dir);
    WriteMaskPpm(path, mask, 64, 255, 64);
    for (int i = 0; i < SCREEN_SIZE; ++i) {
        const uint8_t c = cpp[i];
        const uint8_t a = asmBuf[i];
        mask[i] = (c > 0 && a > 0 && c != a) ? 1 : 0;
    }
    snprintf(path, sizeof(path), "%s/both_different.ppm", dir);
    WriteMaskPpm(path, mask, 255, 255, 64);
}

void PlanetAbPrintRegionDiff(const PlanetRegionDiff& d)
{
    printf("  region: asm_only=%d cpp_only=%d both_same=%d both_diff=%d\n",
           d.asmOnly, d.cppOnly, d.bothSame, d.bothDifferent);
    printf("  counts: asm_px=%d cpp_px=%d\n", d.asmPixels, d.cppPixels);
    printf("  centroid: asm=(%.1f,%.1f) cpp=(%.1f,%.1f)\n",
           d.asmCx, d.asmCy, d.cppCx, d.cppCy);
    printf("  bbox asm=[%d,%d %d,%d] cpp=[%d,%d %d,%d]\n",
           d.asmBbox[0], d.asmBbox[1], d.asmBbox[2], d.asmBbox[3],
           d.cppBbox[0], d.cppBbox[1], d.cppBbox[2], d.cppBbox[3]);
    printf("  largest_asm_only: area=%d at (%d,%d) size=%dx%d\n",
           d.largestAsmOnlyArea, d.largestAsmOnlyX0, d.largestAsmOnlyY0,
           d.largestAsmOnlyW, d.largestAsmOnlyH);
}

void PlanetAbDumpFaceTrace(const char* path)
{
    if (!path)
        return;
    FILE* f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "face\t833ret\tpath835\tparam10\tparam11\tmix_skip\tcalls868\t"
                "early868\tleaf868\tn816\tn820\n");
    for (int i = 0; i < 20; ++i) {
        const PlanetFaceTrace& t = g_trace.faces[i];
        if (t.calls868 == 0 && t.path835 == 0 && t.mixSkipHits == 0)
            continue;
        fprintf(f, "%d\t%u\t%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                t.face, t.fn833Ret, t.path835, t.param10, t.param11,
                t.mixSkipHits, t.calls868, t.early868, t.leafPaint868,
                t.nodes816, t.nodes820);
    }
    fprintf(f, "# totals\t868_calls=%d\t868_leaf=%d\tmix_skip=%d\tlod_c=%d\tlod_msb=%d\n",
            g_trace.total868Calls, g_trace.total868Leaf, g_trace.totalMixSkip,
            g_trace.lodC, g_trace.lodMsb);
    fprintf(f, "# jobs\t778_total=%d\t840=%d\t842=%d\t778_0x19=%d\t778_0xc=%d\t778_0xd=%d\t778_0x16=%d\t778_0x14=%d\n",
            g_trace.totalJobs778, g_trace.total840, g_trace.total842,
            g_trace.jobs778ByWords[0x19], g_trace.jobs778ByWords[0xc],
            g_trace.jobs778ByWords[0xd], g_trace.jobs778ByWords[0x16],
            g_trace.jobs778ByWords[0x14]);
    fclose(f);
}

void PlanetAbDumpRowSpans(const char* path, const PlanetRegionDiff& diff)
{
    if (!path)
        return;
    FILE* f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "y\tasm_x0\tasm_x1\tasm_count\tcpp_x0\tcpp_x1\tcpp_count\n");
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        const PlanetRowSpan& a = diff.asmRowSpans[y];
        const PlanetRowSpan& c = diff.cppRowSpans[y];
        if (a.count == 0 && c.count == 0)
            continue;
        fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                y, a.x0, a.x1, a.count, c.x0, c.x1, c.count);
    }
    fclose(f);
}

static bool MidpointTraceEnabled()
{
    return EnvEnabled("AB_1F_MIDPOINT_TRACE");
}

static PlanetMidpointBranchSample g_midSamples[2048];
static int g_midSampleN = 0;
static int g_mid851Simple = 0;
static int g_mid851Else = 0;
static int g_mid855Simple = 0;
static int g_mid855Else = 0;
static int g_mid834Faces = 0;
static int g_mid834LastParam6 = -1;
static int g_mid834LastFn278 = 0;
static int g_mid834LastFn279 = 0;
static int g_mid834PerFace[20][3]; // param6, fn278, fn279
static int g_midFnCalls[870];      // indexed by fn id e.g. 849..867

void PlanetAbResetMidpointTrace()
{
    g_midSampleN = 0;
    g_mid851Simple = 0;
    g_mid851Else = 0;
    g_mid855Simple = 0;
    g_mid855Else = 0;
    g_mid834Faces = 0;
    g_mid834LastParam6 = -1;
    g_mid834LastFn278 = 0;
    g_mid834LastFn279 = 0;
    std::memset(g_mid834PerFace, 0, sizeof(g_mid834PerFace));
    std::memset(g_midFnCalls, 0, sizeof(g_midFnCalls));
}

void PlanetAbNote834Midpoint(int param6, int fn278, int fn279)
{
    if (!MidpointTraceEnabled())
        return;
    g_mid834Faces++;
    g_mid834LastParam6 = param6;
    g_mid834LastFn278 = fn278;
    g_mid834LastFn279 = fn279;
    const int face = PlanetAbGetTrace()->activeFace;
    if (face >= 0 && face < 20) {
        g_mid834PerFace[face][0] = param6;
        g_mid834PerFace[face][1] = fn278;
        g_mid834PerFace[face][2] = fn279;
    }
}

void PlanetAbNoteMidpointFnCall(int fnId)
{
    if (!MidpointTraceEnabled())
        return;
    if (fnId >= 0 && fnId < (int)(sizeof(g_midFnCalls) / sizeof(g_midFnCalls[0])))
        ++g_midFnCalls[fnId];
}

void PlanetAbNoteMidpointBranch(int fn, int param5, int eff5,
                                int lod2, int lod3, int mix2, int mix3,
                                int branch, int outLod, int outMix, int z2, int z3)
{
    if (!MidpointTraceEnabled())
        return;
    if (fn == 851) {
        if (branch == 0)
            ++g_mid851Simple;
        else
            ++g_mid851Else;
    } else if (fn == 855) {
        if (branch == 0)
            ++g_mid855Simple;
        else
            ++g_mid855Else;
    }
    if (g_midSampleN >= (int)(sizeof(g_midSamples) / sizeof(g_midSamples[0])))
        return;
    PlanetMidpointBranchSample& s = g_midSamples[g_midSampleN++];
    s.fn = fn;
    s.param5 = param5;
    s.eff5 = eff5;
    s.lod2 = lod2;
    s.lod3 = lod3;
    s.mix2 = mix2;
    s.mix3 = mix3;
    s.branch = branch;
    s.outLod = outLod;
    s.outMix = outMix;
    s.z2 = z2;
    s.z3 = z3;
}

void PlanetAbDumpMidpointTrace(const char* path)
{
    if (!path || !MidpointTraceEnabled())
        return;
    FILE* f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# 834_faces=%d last_param6=%d fn278=%d fn279=%d\n",
            g_mid834Faces, g_mid834LastParam6, g_mid834LastFn278, g_mid834LastFn279);
    fprintf(f, "# fn_calls");
    for (int fn = 849; fn <= 855; ++fn)
        fprintf(f, " %d=%d", fn, g_midFnCalls[fn]);
    fprintf(f, "\n");
    fprintf(f, "# per_face face param6 fn278 fn279\n");
    for (int fi = 0; fi < 20; ++fi) {
        if (g_mid834PerFace[fi][0] || g_mid834PerFace[fi][1] || g_mid834PerFace[fi][2])
            fprintf(f, "# face%d\t%d\t%d\t%d\n", fi,
                    g_mid834PerFace[fi][0], g_mid834PerFace[fi][1], g_mid834PerFace[fi][2]);
    }
    fprintf(f, "# 851 simple=%d else=%d | 855 simple=%d else=%d | samples=%d\n",
            g_mid851Simple, g_mid851Else, g_mid855Simple, g_mid855Else, g_midSampleN);
    fprintf(f, "fn\tp5\teff5\tlod2\tlod3\tmix2\tmix3\tbranch\toutLod\toutMix\tz2\tz3\n");
    for (int i = 0; i < g_midSampleN; ++i) {
        const PlanetMidpointBranchSample& s = g_midSamples[i];
        fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                s.fn, s.param5, s.eff5, s.lod2, s.lod3, s.mix2, s.mix3,
                s.branch, s.outLod, s.outMix, s.z2, s.z3);
    }
    fclose(f);
}

// ---- Contour limb fill (non-ASM quality) ---------------------------------
// Contour digons sit ~8px outside rocky edges; ClearUpperBuf fill shows in the
// annulus (and is ALSO the upper-buffer sky index — never flood Contour quads).
// Fit a disc from Contour verts, inset Contour onto rocky, then replace only
// fill pixels in a thin outer annulus by sampling inward body colour.

static constexpr int kLimbMaxPts = 512;
static int16_t g_limbXs[kLimbMaxPts];
static int16_t g_limbYs[kLimbMaxPts];
static int g_limbN = 0;
static uint32_t g_limbClock = 0xffffffffu;

static bool LimbFillEnabled()
{
    const char* e = getenv("AB_1F_LIMB_FILL");
    // Default OFF — Earth/Mercury intro showed Contour-ring halos when the
    // annulus fill painted the Contour–fill band (and inset warped digons).
    // Opt in: AB_1F_LIMB_FILL=1.
    if (!e || !e[0])
        return false;
    return !(e[0] == '0' || e[0] == 'n' || e[0] == 'N' || e[0] == 'f' ||
             e[0] == 'F');
}

static void LimbReset()
{
    g_limbN = 0;
}

static void LimbNotePoint(int x, int y)
{
    if (g_limbN >= kLimbMaxPts)
        return;
    g_limbXs[g_limbN] = static_cast<int16_t>(x);
    g_limbYs[g_limbN] = static_cast<int16_t>(y);
    ++g_limbN;
}

static bool LimbDisc(int& cx, int& cy, int& rmax)
{
    if (g_limbN < 24)
        return false;
    int64_t sumX = 0, sumY = 0;
    for (int i = 0; i < g_limbN; ++i) {
        sumX += g_limbXs[i];
        sumY += g_limbYs[i];
    }
    cx = static_cast<int>(sumX / g_limbN);
    cy = static_cast<int>(sumY / g_limbN);
    int rmax2 = 0;
    for (int i = 0; i < g_limbN; ++i) {
        const int dx = (int)g_limbXs[i] - cx;
        const int dy = (int)g_limbYs[i] - cy;
        const int r2 = dx * dx + dy * dy;
        if (r2 > rmax2)
            rmax2 = r2;
    }
    rmax = 0;
    while (rmax * rmax < rmax2)
        ++rmax;
    return rmax >= 24;
}

void PlanetAbNoteContourLimb(int x0, int y0, int x1, int y1, int x2, int y2,
                             int x3, int y3)
{
    if (!LimbFillEnabled())
        return;
    if (GetGameClock() != g_limbClock) {
        LimbReset();
        g_limbClock = GetGameClock();
    }
    LimbNotePoint(x0, y0);
    LimbNotePoint(x1, y1);
    LimbNotePoint(x2, y2);
    LimbNotePoint(x3, y3);
}

void PlanetAbContourInsetVerts(int16_t* xy8)
{
    // Opt-in only (AB_1F_LIMB_INSET=1). Uniform/outer inset on live Contour
    // digons produced thick textured rings on Earth/Mercury title shots.
    const char* e = getenv("AB_1F_LIMB_INSET");
    if (!e || !e[0] || e[0] == '0' || e[0] == 'n' || e[0] == 'N' ||
        e[0] == 'f' || e[0] == 'F')
        return;
    int cx = 0, cy = 0, rmax = 0;
    if (!xy8 || !LimbFillEnabled() || !LimbDisc(cx, cy, rmax))
        return;
    int r2[4];
    for (int i = 0; i < 4; ++i) {
        const int dx = (int)xy8[i * 2] - cx;
        const int dy = (int)xy8[i * 2 + 1] - cy;
        r2[i] = dx * dx + dy * dy;
    }
    int a = 0, b = 1;
    if (r2[1] > r2[a]) { b = a; a = 1; }
    for (int i = 2; i < 4; ++i) {
        if (r2[i] > r2[a]) { b = a; a = i; }
        else if (r2[i] > r2[b]) { b = i; }
    }
    constexpr int kNum = 92, kDen = 100;
    for (int i : {a, b}) {
        const int x = xy8[i * 2];
        const int y = xy8[i * 2 + 1];
        xy8[i * 2] = static_cast<int16_t>(
            cx + (int)(((int64_t)x - cx) * kNum / kDen));
        xy8[i * 2 + 1] = static_cast<int16_t>(
            cy + (int)(((int64_t)y - cy) * kNum / kDen));
    }
}

void PlanetAbLimbFillApply()
{
    if (!LimbFillEnabled()) {
        LimbReset();
        return;
    }
    int cx = 0, cy = 0, rmax = 0;
    if (!g_videoState || !g_videoState->backBuffer || !LimbDisc(cx, cy, rmax)) {
        LimbReset();
        return;
    }
    uint8_t* buf = g_videoState->backBuffer;
    const uint8_t fill = static_cast<uint8_t>(GetRenderFillColor() & 0xffu);
    const uint8_t fillHi = static_cast<uint8_t>(fill ^ 0x40u);
    auto isFill = [&](uint8_t c) { return c == fill || c == fillHi; };

    // Halo band: ClearUpperBuf fill between rocky and Contour. Sky shares the
    // fill index — never paint outside Contour rmax.
    const int rOuter = rmax + 1;
    const int rInner = rmax > 32 ? rmax - 28 : rmax / 2;
    const int rSeed = rInner + 12; // rocky fringe; Contour sits near rmax
    const int rOuter2 = rOuter * rOuter;
    const int rInner2 = rInner * rInner;
    const int rSeed2 = rSeed * rSeed;

    // Sample only from rocky/fringe (r <= rSeed). Contour-gap nebula and Contour
    // digon colours at the outer ring must not propagate into the halo.
    auto sampleRocky = [&](int x, int y) -> uint8_t {
        const int steps = rmax > 0 ? rmax : 1;
        for (int s = 1; s <= steps; ++s) {
            const int nx = x + ((cx - x) * s) / steps;
            const int ny = y + ((cy - y) * s) / steps;
            if (nx < 0 || nx >= 320 || ny < 0 || ny >= 158)
                break;
            const int ndx = nx - cx, ndy = ny - cy;
            if (ndx * ndx + ndy * ndy > rSeed2)
                continue; // still outside rocky fringe
            const uint8_t c = buf[ny * 320 + nx];
            if (!isFill(c) && c != 0 && c != 15)
                return c;
        }
        return fill;
    };

    static int s_dbg;
    static uint8_t s_before[64000];
    const char* dumpPath = getenv("AB_1F_LIMB_FILL_DUMP");
    if (dumpPath && dumpPath[0] && s_dbg < 1)
        std::memcpy(s_before, buf, 64000);

    int painted = 0;
    const int y0 = std::max(0, cy - rOuter);
    const int y1 = std::min(157, cy + rOuter);
    const int x0 = std::max(0, cx - rOuter);
    const int x1 = std::min(319, cx + rOuter);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int dx = x - cx, dy = y - cy;
            const int r2 = dx * dx + dy * dy;
            if (r2 > rOuter2 || r2 < rInner2)
                continue;
            const int idx = y * 320 + x;
            if (!isFill(buf[idx]))
                continue;
            const uint8_t sample = sampleRocky(x, y);
            if (!isFill(sample) && sample != buf[idx]) {
                buf[idx] = sample;
                ++painted;
            }
        }
    }
    if (getenv("AB_1F_LIMB_FILL_DBG") && s_dbg < 8) {
        fprintf(stderr,
                "LIMBFILL clock=0x%x c=(%d,%d) rmax=%d ring=[%d,%d] painted=%d "
                "contourN=%d fill=%u/%u\n",
                (unsigned)GetGameClock(), cx, cy, rmax, rInner, rOuter, painted,
                g_limbN, (unsigned)fill, (unsigned)fillHi);
    }
    if (dumpPath && dumpPath[0] && s_dbg < 1) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s_before.raw", dumpPath);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(s_before, 1, 64000, f);
            std::fclose(f);
        }
        std::snprintf(path, sizeof(path), "%s_after.raw", dumpPath);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(buf, 1, 64000, f);
            std::fclose(f);
        }
        std::snprintf(path, sizeof(path), "%s_after.raw.pal", dumpPath);
        PlatformDumpPalette(path);
        fprintf(stderr, "LIMBFILL dump %s_{before,after}.raw painted=%d\n",
                dumpPath, painted);
    }
    if (getenv("AB_1F_LIMB_FILL_DBG") || (dumpPath && dumpPath[0]))
        ++s_dbg;
    LimbReset();
}

} // namespace jjffe
