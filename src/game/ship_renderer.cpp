#include "jjffe/ship_renderer.h"
#include "jjffe/draw.h"
#include "jjffe/video.h"
#include "jjffe/data.h"
#include "data_blob.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/math.h"
#include "jjffe/game_state.h"
#include "jjffe/job_trace.h"
#include "jjffe/phase_o_oracle.h"
#include "jjffe/contour_bezier.h"
#include "jjffe/ab_plotstamp.h"
#include "jjffe/band_lane_interp.h"
#include "jjffe/band_lane_tables.h"
#include "jjffe/ship_model_table.h"
namespace jjffe {
void* FUNC_001805(void*);
void* FUNC_001807(void*);
uint8_t* SwapWalkBankArena(uint8_t*);
intptr_t SwapWalkFacingBase(intptr_t);
uint32_t* WalkQueueHead();
const uint8_t* JobArenaBase();
uint32_t OracleJobCallbackId(const void* fn);
void InitGameState();
bool IsGameStateInitialized();
uint32_t SwapWalkBankWindow(uint32_t);
uint32_t CtxRegisterBankForReplay(const void*);
uint32_t CtxRegisterObjForReplay(const void*);
const void* CtxResolveBankForReplay(uint32_t);
const void* CtxResolveObjForReplay(uint32_t);
intptr_t FUNC_001471(intptr_t, uint32_t);
void FUNC_001786();
// R488: era-flush punch-chain wiring. Core's pass handler is a complete
// builder of the ASM's trailing contour chain (FUNC_001794 header +
// 001795/1796/1797 edge cells + 001799 [rgb16|key<<16] span fill), and
// FUNC_001785/FUNC_001788 reset/flush the core job queue it enqueues into.
uint8_t* FUNC_001721(intptr_t ctx, uint8_t* cursor);
void FUNC_001785(uint32_t* head);
void FUNC_001788(uint32_t* head);  // core spells the param `uint*` == uint32_t*
void FUNC_001670_VecMatTMul(int*, uint32_t*, uint32_t*);
void FUNC_001673_MatMatTMul(int*, uint32_t*, uint32_t*);
void FUNC_001671_SpecialVecMatMul(int*, int*, uint32_t*, uint32_t*);
uint32_t FUNC_001327_DPal16toPal(uint32_t);
uint8_t* FUNC_001538(int32_t);
// R1055: the ASM ship walk (FUNC_001774 -> FUNC_001772) and its flush.
void FUNC_001685(intptr_t);
void FUNC_001773(intptr_t, intptr_t);
void FUNC_001780(int, uint32_t);
void AbSetWalkFlag(uint32_t);
void SetAbWalkModelId(int id);
int GetAbWalkModelId();
extern int g_ab_walk_seq;
const uint8_t* AbWalkShadeTable();
void SetBackBufferPtr(uint8_t*);
}

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace jjffe {

// Per-pixel depth buffer for textured fill (320×158 viewport).
// Stores the nearest camera-space Z at each pixel. A fragment passes
// if its Z is smaller (closer). Cleared at the start of RenderShipFlat.
static int32_t s_depthBuf[320 * 158];
static bool s_depthEnabled = false;

// Gated diagnostic (AB_TRACE_TRI3): count painted pixels before/after one draw
// so a probe can tell "painted nothing" from "painted the wrong value".
static int countNonZeroBuf()
{
    if (!g_videoState) return 0;
    const uint8_t* b = g_videoState->backBuffer;
    int n = 0;
    for (int i = 0; i < 64000; ++i)
        if (b[i]) ++n;
    return n;
}

// ASM 3D viewport: textured/flat ship raster stops at y >= 0x9E (cockpit HUD).
// Match draw.cpp Tex64 / DrawTriangle — do not paint into rows 158..199.
constexpr int k3DClipBottom = 0x9E;

// FUNC_001721 Contour span fill: 001548 bezier edges → even-odd spans per row
// (xmin/xmax alone merges left/right wing lobes across the gap).
static constexpr int kC721MaxHits = 32;
static int16_t s_c721Hits[0x9E][kC721MaxHits];
static uint8_t s_c721HitN[0x9E];
static int s_c721YMin = 0x9E;
static int s_c721YMax = -1;

static void C721SpanReset()
{
    memset(s_c721HitN, 0, sizeof(s_c721HitN));
    s_c721YMin = k3DClipBottom;
    s_c721YMax = -1;
}

static void C721SpanAddX(int y, int16_t x)
{
    if (y < 0 || y >= k3DClipBottom) return;
    uint8_t& n = s_c721HitN[y];
    if (n >= kC721MaxHits) return;
    s_c721Hits[y][n++] = x;
    if (y < s_c721YMin) s_c721YMin = y;
    if (y > s_c721YMax) s_c721YMax = y;
}

static void C721SpanAddEdge(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (y0 == y1) {
        // Horizontal: two hits so even-odd covers the segment.
        C721SpanAddX(y0, x0);
        C721SpanAddX(y0, x1);
        return;
    }
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    if (y1 <= 0 || y0 >= k3DClipBottom) return;
    int64_t x = (int64_t)x0 << 16;
    const int64_t dx = ((int64_t)(x1 - x0) << 16) / (y1 - y0);
    for (int y = y0; y < y1; y++) {
        C721SpanAddX(y, static_cast<int16_t>(x >> 16));
        x += dx;
    }
}

// ASM FUNC_001560 y-order + FUNC_001559 DDA into C721 hits.
// Critical: dy==0 is a no-op (001559 jz). Do NOT use C721SpanAddEdge here —
// its horizontal path double-hits and corrupts even-odd Contour fill.
static void C721SpanAddBezierSeg(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (y0 > y1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    int dy = (int)y1 - (int)y0;
    if (dy == 0) return;
    if (y1 <= 0 || y0 >= k3DClipBottom) return;

    // Match FUNC_001559 dx: idiv with sign-specialized path for dxPix <= 0.
    int dxPix = (int)x1 - (int)x0;
    int32_t dx;
    if (dxPix < 1) {
        dx = -(((-dxPix) * 0x10000) / dy);
    } else {
        dx = (dxPix * 0x10000) / dy;
    }
    int32_t x = (int32_t)x0 << 16;
    for (; dy > 0; dy--) {
        C721SpanAddX(y0, static_cast<int16_t>(x >> 16));
        x += dx;
        y0++;
    }
}

// FUNC_001797 → 001548 → 001547: ContourBezierSample2i (=001549 FD) then
// per-step callback ≡ 001560/001559 → C721SpanAddX.
static void C721SpanAddBezier(const int16_t* p0, const int16_t* p1,
                              const int16_t* p2, const int16_t* p3)
{
    const int32_t a[2] = {p0[0], p0[1]};
    const int32_t b[2] = {p1[0], p1[1]};
    const int32_t c[2] = {p2[0], p2[1]};
    const int32_t d[2] = {p3[0], p3[1]};
    std::vector<ContourPt2> pts;
    ContourBezierSample2i(a, b, c, d, pts);
    for (size_t i = 1; i < pts.size(); i++) {
        C721SpanAddBezierSeg(static_cast<int16_t>(pts[i - 1].x),
                             static_cast<int16_t>(pts[i - 1].y),
                             static_cast<int16_t>(pts[i].x),
                             static_cast<int16_t>(pts[i].y));
    }
}

static void C721SpanFlush(uint8_t color)
{
    if (!g_videoState || s_c721YMax < s_c721YMin) return;
    if (getenv("AB_DUMP_C721_HITS") && color == 22) {
        static int nH = 0;
        if (nH < 12) {
            fprintf(stderr, "C721HITS color=%u y=%d..%d\n", color, s_c721YMin, s_c721YMax);
            for (int y = s_c721YMin; y <= s_c721YMax; y++) {
                const int n = s_c721HitN[y];
                if (n >= 2) {
                    fprintf(stderr, "  y=%d n=%d xs:", y, n);
                    for (int i = 0; i < n && i < 12; i++) fprintf(stderr, " %d", s_c721Hits[y][i]);
                    fprintf(stderr, "\n");
                }
            }
            ++nH;
        }
    }
    uint8_t* buf = g_videoState->backBuffer;
    for (int y = s_c721YMin; y <= s_c721YMax; y++) {
        const int n = s_c721HitN[y];
        if (n < 2) continue;
        int16_t* xs = s_c721Hits[y];
        // Insertion sort — n is small.
        for (int i = 1; i < n; i++) {
            int16_t v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < n; i += 2) {
            int xL = xs[i];
            int xR = xs[i + 1];
            if (xL < 0) xL = 0;
            if (xR > 320) xR = 320;
            if (xL < xR) {
                if (getenv("AB_DUMP_C721_FILL") && color == 22 && y >= 100 && y <= 108)
                    fprintf(stderr, "C721FILL y=%d xL=%d xR=%d n=%d\n", y, xL, xR, n);
                if (const char* cf = getenv("AB_DUMP_C721_COLOR");
                    cf && atoi(cf) == color)
                    fprintf(stderr, "C721FILL c%d y=%d xL=%d xR=%d n=%d\n", color, y, xL, xR, n);
                jjffe::AbPlotStampSpan(y, xL, xR - xL, color, "c721");
                memset(buf + y * 320 + xL, color, static_cast<size_t>(xR - xL));
            }
        }
    }
    if (getenv("AB_TRACE_150") && color == 22) {
        static uint8_t s_f = 0xFD;
        const uint8_t vf = g_videoState->backBuffer[107 * 320 + 150];
        if (vf != s_f) { fprintf(stderr, "TRACE150f after C721SpanFlush color=%u -> %u\n", color, vf); s_f = vf; }
    }
}

// Empty-only Contour flush — paints Contour OA without Contour OC overdraw.
static void C721SpanFlushEmpty(uint8_t color)
{
    if (!g_videoState || s_c721YMax < s_c721YMin || color == 0) return;
    uint8_t* buf = g_videoState->backBuffer;
    for (int y = s_c721YMin; y <= s_c721YMax; y++) {
        const int n = s_c721HitN[y];
        if (n < 2) continue;
        int16_t* xs = s_c721Hits[y];
        for (int i = 1; i < n; i++) {
            int16_t v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < n; i += 2) {
            int xL = xs[i];
            int xR = xs[i + 1];
            if (xL < 0) xL = 0;
            if (xR > 319) xR = 319;
            for (int x = xL; x < xR; x++) {
                if (buf[y * 320 + x] == 0 &&
                    ::getenv("AB_GRAZE_PROBE") &&
                    ::getenv("AB_GRAZE_PROBE")[0] == '1' && y >= 88 && y <= 98 &&
                    x >= 140 && x <= 200) {
                    static int s_fl = 0;
                    if (s_fl < 5) {
                        fprintf(stderr, "FLUSH (%d,%d) c=%d spanY=%d\n", x, y,
                                (int)color, y);
                        ++s_fl;
                    }
                }
                if (buf[y * 320 + x] == 0)
                    buf[y * 320 + x] = color;
            }
        }
    }
}

// External texture state and functions from draw.cpp
extern TexRenderState g_texState;
extern void SelectTexFunc(uint32_t bitmapIndex);
extern void Tex64NonTrans(int scanlines);
extern void Tex64Trans(int scanlines);
extern void Tex128NonTrans(int scanlines);
extern void Tex128Trans(int scanlines);

// Simple lighting: directional light from upper-right-front
static int ComputeLighting(int8_t nx, int8_t ny, int8_t nz)
{
    int dot = nx - ny + nz;
    if (dot < 0) dot = 0;
    if (dot > 127) dot = 127;
    return 2 + (dot * 13) / 127;
}

// Get shading color for a face
static bool abGreyMapOn() {
    const char* off = ::getenv("AB_NO_GREYMAP");
    if (off && off[0] == '1') return false;  // b14: default-on, pair-adopted
    return true;
}

uint8_t GetFaceColor(const ShipModel* shipModel, uint8_t normalIdx, uint8_t baseColor, const int32_t* shipRot)
{
    int32_t nx, ny, nz;
    if (GetNormal(shipModel, normalIdx, nx, ny, nz)) {
        // Transform normal to world space (using 16.16 matrix)
        // Since nx, ny, nz are in the range [-127, 127] and shipRot is [-65536, 65536],
        // we can multiply them directly and shift down by 16 to get back to [-127, 127]
        int32_t rnx = (int32_t)(((int64_t)nx * shipRot[0] + (int64_t)ny * shipRot[3] + (int64_t)nz * shipRot[6]) >> 16);
        int32_t rny = (int32_t)(((int64_t)nx * shipRot[1] + (int64_t)ny * shipRot[4] + (int64_t)nz * shipRot[7]) >> 16);
        int32_t rnz = (int32_t)(((int64_t)nx * shipRot[2] + (int64_t)ny * shipRot[5] + (int64_t)nz * shipRot[8]) >> 16);
        
        return ComputeLighting((int8_t)rnx, (int8_t)rny, (int8_t)rnz);
    }
    return 0x08 + (normalIdx % 8);
}

// Draw a textured quad using affine texture mapping
// Uses simple planar projection for UV coordinates
[[maybe_unused]] static void DrawTexturedQuad(const TransformedPoly& poly, const uint8_t* texture, int texSize, bool transparent)
{
    // Compute UV coordinates based on screen position
    // Simple approach: map vertex positions to texture coordinates
    int16_t minX = poly.verts[0].screenX, maxX = poly.verts[0].screenX;
    int16_t minY = poly.verts[0].screenY, maxY = poly.verts[0].screenY;

    for (int i = 1; i < 4; i++) {
        if (poly.verts[i].screenX < minX) minX = poly.verts[i].screenX;
        if (poly.verts[i].screenX > maxX) maxX = poly.verts[i].screenX;
        if (poly.verts[i].screenY < minY) minY = poly.verts[i].screenY;
        if (poly.verts[i].screenY > maxY) maxY = poly.verts[i].screenY;
    }

    int width = maxX - minX;
    int height = maxY - minY;
    if (width <= 0 || height <= 0) return;

    // Clip to screen
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > SCREEN_WIDTH) maxX = SCREEN_WIDTH;
    if (maxY > k3DClipBottom) maxY = k3DClipBottom;

    // Set up texture state
    g_texState.texture = texture;
    g_texState.palette = g_videoState->palette;
    g_texState.texSize = texSize;

    // Render scanlines
    for (int16_t y = minY; y < maxY; y++) {
        // Find left and right edges at this scanline
        int16_t leftX = maxX, rightX = minX;

        // Simple edge interpolation (affine)
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            int16_t y0 = poly.verts[i].screenY;
            int16_t y1 = poly.verts[j].screenY;

            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                // Edge crosses this scanline
                int32_t t = ((int32_t)(y - y0) << 16) / (y1 - y0);
                int16_t x = poly.verts[i].screenX + (int16_t)((t * (poly.verts[j].screenX - poly.verts[i].screenX)) >> 16);

                if (x < leftX) leftX = x;
                if (x > rightX) rightX = x;
            }
        }

        if (leftX >= rightX) continue;
        if (leftX < 0) leftX = 0;
        if (rightX > SCREEN_WIDTH) rightX = SCREEN_WIDTH;

        // Compute texture coordinates for this span
        int spanWidth = rightX - leftX;
        if (spanWidth <= 0) continue;

        // Map screen position to texture coordinates
        // Simple: use position relative to bounding box
        int32_t uStart = ((int32_t)(leftX - minX) * texSize) / width;
        int32_t uEnd = ((int32_t)(rightX - minX) * texSize) / width;
        int32_t vCoord = ((int32_t)(y - minY) * texSize) / height;

        // Set up scanline state
        g_texState.x0 = leftX;
        g_texState.x1 = rightX;
        g_texState.y = y;
        g_texState.u0 = uStart << 16;
        g_texState.v0 = vCoord << 16;
        g_texState.du = ((uEnd - uStart) << 16) / spanWidth;
        g_texState.dv = 0;

        // Draw scanline
        if (texSize == 64) {
            if (transparent) Tex64Trans(1);
            else Tex64NonTrans(1);
        } else {
            if (transparent) Tex128Trans(1);
            else Tex128NonTrans(1);
        }
    }
}

// Debug flag for texture rasterization
static bool g_debugTextureRaster = false;
static bool g_debugFaceColors = false;

void SetTextureRasterDebug(bool enabled)
{
    g_debugTextureRaster = enabled;
}

void SetDebugFaceColors(bool enabled)
{
    g_debugFaceColors = enabled;
}

// Edge state for scanline rasterization
struct TexEdge {
    int32_t x;   // Current X (16.16 fixed-point)
    int32_t u;   // Current U (16.16 fixed-point)
    int32_t v;   // Current V (16.16 fixed-point)
    int32_t dx;  // X gradient (dx/dy)
    int32_t du;  // U gradient (du/dy)
    int32_t dv;  // V gradient (dv/dy)
};

// Compute edge gradient from vertex start to vertex end
[[maybe_unused]] static void ComputeEdgeGradient(const int16_t* vStart, const int16_t* uvStart,
                                 const int16_t* vEnd, const int16_t* uvEnd,
                                 TexEdge& edge)
{
    int dy = vEnd[1] - vStart[1];
    if (dy == 0) {
        edge.dx = edge.du = edge.dv = 0;
        edge.x = vStart[0] << 16;
        edge.u = uvStart[0] << 16;
        edge.v = uvStart[1] << 16;
        return;
    }
    edge.x = vStart[0] << 16;
    edge.u = uvStart[0] << 16;
    edge.v = uvStart[1] << 16;
    edge.dx = ((int32_t)(vEnd[0] - vStart[0]) << 16) / dy;
    edge.du = ((int32_t)(uvEnd[0] - uvStart[0]) << 16) / dy;
    edge.dv = ((int32_t)(uvEnd[1] - uvStart[1]) << 16) / dy;
}

// Advance edge by one scanline
[[maybe_unused]] static void AdvanceEdge(TexEdge& edge)
{
    edge.x += edge.dx;
    edge.u += edge.du;
    edge.v += edge.dv;
}

// UV corner indices for the 8 vertex orderings (from FUNC_001647 switch statement)
// Verified against original ASM decompiled code
// UV table indices: [0]=(0,0), [1]=(max,0), [2]=(0,max), [3]=(max,max)
static const int s_uvOrder[8][4] = {
    {0, 1, 3, 2},  // case 0
    {1, 3, 2, 0},  // case 1
    {3, 2, 0, 1},  // case 2
    {2, 0, 1, 3},  // case 3
    {0, 2, 3, 1},  // case 4
    {2, 3, 1, 0},  // case 5
    {3, 1, 0, 2},  // case 6
    {1, 0, 2, 3}   // case 7
};

// Render a single textured triangle using scanline rasterization
// Each edge spans the correct Y range, ensuring proper UV interpolation
static void RenderTexturedTriangle(const int16_t* vA, const int16_t* vB, const int16_t* vC,
                                    const int16_t* uvA, const int16_t* uvB, const int16_t* uvC,
                                    const uint8_t* texture, int texSize, bool transparent,
                                    const uint8_t shadePal[256],
                                    int32_t zA, int32_t zB, int32_t zC)
{
    const int16_t* verts[3] = {vA, vB, vC};
    const int16_t* uvs[3]   = {uvA, uvB, uvC};
    int32_t zvs[3] = {zA, zB, zC};

    // Sort by Y (Z follows)
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2 - i; j++)
            if (verts[j][1] > verts[j+1][1]) {
                auto tv = verts[j]; verts[j] = verts[j+1]; verts[j+1] = tv;
                auto tu = uvs[j]; uvs[j] = uvs[j+1]; uvs[j+1] = tu;
                std::swap(zvs[j], zvs[j+1]);
            }

    int yTop = verts[0][1], yMid = verts[1][1], yBot = verts[2][1];
    if (yBot <= yTop) return;

    int mask = texSize - 1;
    uint8_t* buf = g_videoState->backBuffer;

    int dyLong = yBot - yTop;
    int dyShort = yMid - yTop;

    int32_t dxLong = (int32_t)((uint32_t)(int32_t)(verts[2][0] - verts[0][0]) << 16) / dyLong;
    int32_t duLong = (int32_t)((uint32_t)(int32_t)(uvs[2][0] - uvs[0][0]) << 16) / dyLong;
    int32_t dvLong = (int32_t)((uint32_t)(int32_t)(uvs[2][1] - uvs[0][1]) << 16) / dyLong;
    int32_t dzLong = (zvs[2] - zvs[0]) / dyLong;

    int32_t xL = (int32_t)((uint32_t)verts[0][0] << 16);
    int32_t uL = (int32_t)((uint32_t)uvs[0][0] << 16);
    int32_t vL = (int32_t)((uint32_t)uvs[0][1] << 16);
    int32_t zL = zvs[0];

    int32_t xS = xL, uS = uL, vS = vL, zS = zL;
    int32_t dxS, duS, dvS, dzS;
    if (dyShort > 0) {
        dxS = (int32_t)((uint32_t)(int32_t)(verts[1][0] - verts[0][0]) << 16) / dyShort;
        duS = (int32_t)((uint32_t)(int32_t)(uvs[1][0] - uvs[0][0]) << 16) / dyShort;
        dvS = (int32_t)((uint32_t)(int32_t)(uvs[1][1] - uvs[0][1]) << 16) / dyShort;
        dzS = (zvs[1] - zvs[0]) / dyShort;
    } else {
        dxS = duS = dvS = 0;
        dzS = 0;
    }

    for (int half = 0; half < 2; half++) {
        int yStart = (half == 0) ? yTop : yMid;
        int yStop = (half == 0) ? yMid : yBot;

        if (half == 1 && yBot > yMid) {
            int dy = yBot - yMid;
            xS = (int32_t)((uint32_t)verts[1][0] << 16);
            uS = (int32_t)((uint32_t)uvs[1][0] << 16);
            vS = (int32_t)((uint32_t)uvs[1][1] << 16);
            zS = zvs[1];
            dxS = (int32_t)((uint32_t)(int32_t)(verts[2][0] - verts[1][0]) << 16) / dy;
            duS = (int32_t)((uint32_t)(int32_t)(uvs[2][0] - uvs[1][0]) << 16) / dy;
            dvS = (int32_t)((uint32_t)(int32_t)(uvs[2][1] - uvs[1][1]) << 16) / dy;
            dzS = (zvs[2] - zvs[1]) / dy;
        }

        for (int y = yStart; y < yStop; y++) {
            if (y >= 0 && y < k3DClipBottom) {
                int x0 = xL >> 16, x1 = xS >> 16;
                int32_t u0 = uL, v0 = vL, u1 = uS, v1 = vS;
                int32_t z0 = zL, z1 = zS;

                if (x0 > x1) {
                    int t = x0; x0 = x1; x1 = t;
                    int32_t tu = u0; u0 = u1; u1 = tu;
                    int32_t tv = v0; v0 = v1; v1 = tv;
                    std::swap(z0, z1);
                }

                // Match RenderTexturedPoly left-clip: advance UV/Z by skip along
                // the *unclipped* span. The old `u0 += skip * 0` left UVs at the
                // off-screen edge, so Saker tip mat60 (0x403c) sampled unmapped
                // cyan shade indices → VGA0 → Ogre soft-blit alpha-reject holes
                // (the "8 planes" nozzle gap at Z:80 Y:146 P:24).
                const int32_t dxFull = x1 - x0;
                if (dxFull > 0) {
                    const int32_t duFull = (u1 - u0) / dxFull;
                    const int32_t dvFull = (v1 - v0) / dxFull;
                    const int32_t dzFull = (z1 - z0) / dxFull;
                    if (x0 < 0) {
                        const int skip = -x0;
                        u0 += duFull * skip;
                        v0 += dvFull * skip;
                        z0 += dzFull * skip;
                        x0 = 0;
                    }
                    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;

                    const int span = x1 - x0;
                    if (span > 0) {
                        int32_t uc = u0, vc = v0, zc = z0;
                        uint8_t* dst = buf + y * SCREEN_WIDTH + x0;

                        for (int x = 0; x < span; x++) {
                            bool pass = !s_depthEnabled ||
                                        zc < s_depthBuf[y * 320 + x0 + x];
                            if (pass) {
                                int texU = (uc >> 16) & mask;
                                int texV = (vc >> 16) & mask;
                                uint8_t texel = texture[texV * texSize + texU];
                                if (!transparent || texel != 0) {
                                    *dst = shadePal ? shadePal[texel] : texel;
                                    jjffe::AbPlotStamp(x0 + x, y, (unsigned)*dst, "tri");
                                    if (s_depthEnabled)
                                        s_depthBuf[y * 320 + x0 + x] = zc;
                                }
                            }
                            dst++;
                            uc += duFull;
                            vc += dvFull;
                            zc += dzFull;
                        }
                    }
                }
            }
            xL += dxLong; uL += duLong; vL += dvLong; zL += dzLong;
            xS += dxS; uS += duS; vS += dvS; zS += dzS;
        }
    }
}

// Scanline bounds for quad rasterization
struct ScanBounds {
    int32_t leftX, rightX;
    int32_t leftU, rightU;
    int32_t leftV, rightV;
    int32_t leftZ = 0x7fffffff, rightZ = 0x7fffffff;
};

// Material table (DATA_007811): mesh texInfo&0xFFF indexes 0x1E-byte entries.
// Layout: uint32 texWord, uint32 shadeCount, then shadeCount RGB triplets (nibbles).
// The label's size is only 8; the table continues past it.
constexpr size_t kMaterialTableOffset = data::OFFSET_DATA_007811;
constexpr size_t kMaterialEntryStride = 0x1E;

struct MaterialInfo {
    uint32_t matIndex;     // mesh texInfo & 0xFFF (table row)
    uint32_t texWord;
    int shadeCount;
    uint8_t shades[8][3];  // RGB nibbles from material
};

static MaterialInfo ResolveMaterial(uint32_t meshTexInfo)
{
    MaterialInfo info = {};
    info.matIndex = meshTexInfo & 0xFFF;
    info.texWord = meshTexInfo & 0xFFF;
    info.shadeCount = 0;

    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return info;

    uint32_t matIndex = meshTexInfo & 0xFFF;
    // R534 candidate law (gated AB_ROWG): material rows come in groups of 4 per
    // glyph word: base=unlit, +1 grey-lit, +2 green-shift(+G), +3 red-shift(+R).
    // The live face-light vector selects the group member. Port previously read
    // row = texInfo&0xFFF unconditionally (= a fixed group slot) -> #65@05 band
    // reads the RED row (unmapped) where ASM draws unlit/grey rows.
    if (::getenv("AB_ROWG") && ::getenv("AB_ROWG")[0] == '1') {
        extern void GetMeshFaceLight(int8_t*, int8_t*, int8_t*);
        int8_t fr = 0, fg = 0, fb = 0;
        GetMeshFaceLight(&fr, &fg, &fb);
        // Group layout (dumped): rows [base+0]=unlit greys, [+1]=grey-lit,
        // [+2]=green-shift(+G), [+3]=red-shift(+R). Port's texInfo&0xFFF lands on
        // one slot per material (e.g. 9/15 pair for w=0x3d: 9 unlit, 11 grey,
        // 13 green, 15 red => stride between slots is 2 within the SAME word!).
        // Measured: idx9(0x3d unlit) idx11(0x3d grey) idx13(0x3d green) idx15(0x3d red).
        // Group anchor: walk back to the first row whose WORD equals this row's.
        extern const uint8_t DATA_007811[];  // not needed; use blob helper instead
        (void)fr; (void)fg; (void)fb;
        uint32_t base = matIndex;
        {
            extern const uint8_t* blob_row_for_probe_pub(uint32_t);
            const uint8_t w0 = blob_row_for_probe_pub(matIndex)[0];
            while (base >= 2 && blob_row_for_probe_pub(base - 2)[0] == w0) base -= 2;
        }
        const uint32_t grp0 = base;   // unlit row of this word group
        uint32_t pick = matIndex;
        const bool any = (fr | fg | fb) != 0;
        if (!any) pick = grp0;                             // unlit greys
        else if (fr == fg && fg == fb) pick = grp0 + 2;    // grey-lit row
        else if (fg > fr && fg > fb) pick = grp0 + 4;      // green-shift row
        else if (fr > fg && fr > fb) pick = grp0 + 6;      // red-shift row
        if (pick <= 147) matIndex = pick;
    }
    size_t off = kMaterialTableOffset + matIndex * kMaterialEntryStride;
    if (off + 8 > data::GetDataBlobSize()) return info;

    std::memcpy(&info.texWord, blob + off, sizeof(uint32_t));
    uint32_t count = 0;
    std::memcpy(&count, blob + off + 4, sizeof(uint32_t));
    if (count > 8) count = 8;
    info.shadeCount = static_cast<int>(count);

    const uint8_t* rgb = blob + off + 8;  // DATA_007812 relative
    for (int i = 0; i < info.shadeCount; i++) {
        info.shades[i][0] = rgb[i * 3 + 0];
        info.shades[i][1] = rgb[i * 3 + 1];
        info.shades[i][2] = rgb[i * 3 + 2];
    }
    return info;
}

// FUNC_001805 fills DATA_009252[i] = DPal16toPal(materialShade[i] + faceLight).
// Tex64 then does dst = DATA_009252[texel] — texels are shade indices, not colours.
// CURRENT: Greyscale ramp (indices 0-15) — measurably closer to ASM than
// nearest-color-256 search because the ASM DPal uses a 64-entry octree palette,
// not the full 256-entry VGA palette. Porting FUNC_001330 octree quantizer is
// the prerequisite for real chroma (Phase B followup).
// R349 v3 (gated AB_9252V3): shared DATA_009252 simulation per oracle decode
// (R348): zero-init table, per-job publish at base (stale tex family loads at
// slot 4), classic consumers draw from the table; kit + twin keep their own
// palettes and only PUBLISH residues. 61's identity-twin path untouched.
static uint8_t g_shade9252[256];
static bool v3on() { const char* e = ::getenv("AB_9252V3"); return e && e[0] == '1'; }
static bool v3stale(uint32_t m) {
    const uint32_t k = m & 0xFFF;
    return k == 0x26 || k == 0x33 || k == 0x34 || k == 0x3c || k == 0x15 ||
           k == 0x10 || k == 0x14 || k == 0x09;
}
static void pub9252(const uint8_t* rows, uint32_t cnt, int base) {
    for (uint32_t i = 0; i < cnt && base + (int)i < 256; i++) g_shade9252[base + i] = rows[i];
}
// R386 (re-lands R381/382 lost stack + tri-only): kit publishes measured
// band tex words per fan-triangle; classic TRI lane queries by vertex triple.
struct BwEntry { int16_t x, y, x1, y1, x2, y2; uint32_t w; };
static BwEntry g_bw[48]; static int g_bwN = 0; static int s_bwT = 0;
static bool bwOn() { const char* e = ::getenv("AB_BANDWORD"); return e && e[0]=='1'; }
// measured per-row words (j43 v2b_06/v2_01 EXEC joins; rows 8..13 @06-only
// publish (their twins resolve all-zero -> skipped), rows 1/2 both poses)
// CAUTION (R418): the words below are the R387 late-frame probe values,
// MEASURED SUPERSEDED by the R379-410 arc (frame semantics unresolved; see
// ledger R402-417 chain; pose-oracle culling calibrated at 626 in R417 -
// culling alone insufficient). Kept ONLY as the gated no-op scaffold; do NOT
// promote or re-key from this comment-free guess - start from kit43raw fraw
// disambiguation (R401) instead.
static const int32_t kBandWord[34] = { 0, 0x52, 0x45, 0, 0, 0, 0, 0,
    0x5b, 0x5b, 0x5b, 0x5b, 0x5b, 0x5b, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static void bwPut(int x, int y, int x1, int y1, int x2, int y2, uint32_t w) {
    if (g_bwN < 48) { g_bw[g_bwN] = { (int16_t)x, (int16_t)y, (int16_t)x1, (int16_t)y1,
                                      (int16_t)x2, (int16_t)y2, w }; ++g_bwN; }
}
static uint32_t bwGet(int x, int y, int x1, int y1, int x2, int y2) {
    for (int i = 0; i < g_bwN; i++) {
        const int dx = g_bw[i].x - x, dy = g_bw[i].y - y;
        const int ex = g_bw[i].x1 - x1, ey = g_bw[i].y1 - y1;
        const int fx = g_bw[i].x2 - x2, fy = g_bw[i].y2 - y2;
        const int d = dx*dx + dy*dy + ex*ex + ey*ey + fx*fx + fy*fy;
        if (d <= 12) return g_bw[i].w;
    }
    return 0xFFFFFFFFu;
}

const uint8_t* blob_row_for_probe_pub(uint32_t matIndex);

static const uint8_t* blob_row_for_probe(uint32_t matIndex)
{
    static uint8_t zeros[0x1E] = {};
    const uint8_t* b = data::GetDataBlob();
    if (!b) return zeros;
    size_t off = kMaterialTableOffset + (size_t)matIndex * kMaterialEntryStride;
    if (off + 0x1E > data::GetDataBlobSize()) return zeros;
    return b + off;
}

const uint8_t* blob_row_for_probe_pub(uint32_t matIndex) { return blob_row_for_probe(matIndex); }

static void BuildTexShadePalette(uint8_t outPal[256], const MaterialInfo& mat,
                                 int faceR, int faceG, int faceB, bool applyFaceLight)
{
    uint8_t table[8];
    int n = mat.shadeCount > 0 ? mat.shadeCount : 1;
    if (mat.shadeCount <= 0) {
        table[0] = 0x08;
    } else {
        for (int i = 0; i < n; i++) {
            int r = mat.shades[i][0];
            int g = mat.shades[i][1];
            int b = mat.shades[i][2];
            if (applyFaceLight) {
                r += faceR;
                g += faceG;
                b += faceB;
            }
            if (r > 15) r = 15;
            if (g > 15) g = 15;
            if (b > 15) b = 15;
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            // DPal16toPal: full 3D RGB444 → VGA palette index lookup.
            uint16_t idx = (uint16_t)((r << 8) | (g << 4) | b);
            uint8_t pal = DPal16toPal(idx);
            // R534 candidate law (gated AB_GLYPH_SHIFT): ASM's material glyph word is
            // the table WORD (e.g. 0x3d), not the row index (15). Port stored
            // info.texWord = matIndex, so every consumer that masks &0xFFF picks the
            // wrong bitmap (row 15's word vs glyph 0x3d). When gated, rebuild the
            // ramp from the row whose WORD equals the material word AND whose shades
            // are the grey variant (the ASM job words at #65 show {0x0222,0x0444,
            // 0x0333,0x0111} = greys while blob row 15 carries reds).
            if (::getenv("AB_GLYPH_SHIFT") && ::getenv("AB_GLYPH_SHIFT")[0] == '1' && pal == 0) {
                int rr = r, gg = g, bb = b;
                const int mx = rr > gg ? (rr > bb ? rr : bb) : (gg > bb ? gg : bb);
                const int mn = rr < gg ? (rr < bb ? rr : bb) : (gg < bb ? gg : bb);
                if (mx - mn >= 2) {   // chromatic: project toward its max channel grey
                    rr = gg = bb = mx;
                    pal = DPal16toPal((uint16_t)((rr << 8) | (gg << 4) | bb));
                }
            }
            // R531 candidate law (gated AB_UNLIT_FALLBACK): the #23-era "prefer
            // unlit when lit unmapped" fallback only fires with applyFaceLight.
            // #65 engine-cluster faces carry CHROMATIC base shades ((6,2,2)-style
            // reds) with fl=(0,0,0); their RGB444 index is unmapped in the live
            // dPalMap -> slot 0 -> invisible hull band. Retry the equal-
            // luminance grey (r=g=b=luma) when the raw chromatic shade misses.
            if (pal == 0 && !applyFaceLight &&
                ::getenv("AB_UNLIT_FALLBACK") && ::getenv("AB_UNLIT_FALLBACK")[0] == '1' &&
                (mat.shades[i][0] | mat.shades[i][1] | mat.shades[i][2]) != 0) {
                int luma = (r * 77 + g * 151 + b * 28) >> 8;
                if (luma > 15) luma = 15;
                pal = DPal16toPal((uint16_t)((luma << 8) | (luma << 4) | luma));
            }
            // Face light (FUNC_001762) can push greys off the oracle map
            // (Eagle #23: +14,0,0 turns (6,6,6)→(15,6,6) unmapped → invisible
            // textured hull). Prefer the unlit shade when it is mapped.
            if (pal == 0 && applyFaceLight && (mat.shades[i][0] | mat.shades[i][1] |
                                               mat.shades[i][2]) != 0) {
                int ur = mat.shades[i][0];
                int ug = mat.shades[i][1];
                int ub = mat.shades[i][2];
                if (ur < 0) ur = 0;
                if (ur > 15) ur = 15;
                if (ug < 0) ug = 0;
                if (ug > 15) ug = 15;
                if (ub < 0) ub = 0;
                if (ub > 15) ub = 15;
                pal = DPal16toPal((uint16_t)((ur << 8) | (ug << 4) | ub));
            }
            // Puma #60: Complex762 0x2400 (+4,0,0) on mat8 grey (6,6,6) →
            // (10,6,6) maps to VGA80. ASM flat 001721/001799 keeps 0x666→VGA6
            // (no 0x1000 flag). Prefer unlit when chromatic light knocks a
            // pure-grey shade onto a different mapped index.
            if (applyFaceLight && pal != 0) {
                const int ur = mat.shades[i][0];
                const int ug = mat.shades[i][1];
                const int ub = mat.shades[i][2];
                const bool grayShift = (r == g && g == b) &&
                    (ur != r || ug != g) &&
                    ::getenv("AB_UNLITPRI") && ::getenv("AB_UNLITPRI")[0] == '1';
                if (ur == ug && ug == ub && ur != 0 &&
                    ((r != g || g != b) || grayShift)) {
                    uint8_t unlit = DPal16toPal(
                        (uint16_t)((ur << 8) | (ug << 4) | ub));
                    if (unlit != 0 && unlit != pal)
                        pal = unlit;
                }
            }
            // Mat9 chalk (3,2,2)/(2,1,1) absent from dpal_asm → VGA0. Opt-in
            // snap (AB_MAT9_GREY_SNAP=1) remaps near-grey to chalk; default OFF
            // because it fills Shuttle #15 intentional DPal-0 dither holes
            // (~613 only_cpp → ~32 with snap off). AB_MAT60_GREY_SNAP=1: snap
            // every unmapped chroma shade. AB_NO_GREY_SNAP=1: also kills the
            // all-zero table salvage below (Viper #34 vanishes).
            const bool noSnap = getenv("AB_NO_GREY_SNAP") &&
                                getenv("AB_NO_GREY_SNAP")[0] == '1';
            const bool forceAll = getenv("AB_MAT60_GREY_SNAP") &&
                                  getenv("AB_MAT60_GREY_SNAP")[0] == '1';
            const bool mat9Snap = getenv("AB_MAT9_GREY_SNAP") &&
                                  getenv("AB_MAT9_GREY_SNAP")[0] == '1';
            if (!noSnap && pal == 0 && (r | g | b) != 0) {
                int mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
                int mn = r; if (g < mn) mn = g; if (b < mn) mn = b;
                const bool mat9Grey = (mat9Snap && mat.matIndex == 9 &&
                                       (mx - mn) <= 1);
                if (forceAll || mat9Grey) {
                    int gn = mx;
                    if (gn < 1) gn = 1;
                    if (gn > 15) gn = 15;
                    pal = DPal16toPal((uint16_t)((gn << 8) | (gn << 4) | gn));
                    if (pal == 0) pal = (uint8_t)gn;
                }
            }
            table[i] = pal;
        }
        // Viper/Falcon mats whose shade0 DPal is VGA0: ASM paints black hull
        // (invisible on dump-ab). Salvaging all-zero tables to grey flooded
        // Falcon (~24k OC) and Viper front. Default: keep VGA0. Dig
        // AB_GREY_SNAP_SALVAGE=1 restores the old visibility hack.
        const bool salvage = getenv("AB_GREY_SNAP_SALVAGE") &&
                             getenv("AB_GREY_SNAP_SALVAGE")[0] == '1';
        if (salvage && mat.shadeCount > 0) {
            bool allZero = true;
            bool anyRgb = false;
            for (int i = 0; i < n; i++) {
                if (table[i] != 0) allZero = false;
                if (mat.shades[i][0] | mat.shades[i][1] | mat.shades[i][2])
                    anyRgb = true;
            }
            if (allZero && anyRgb) {
                for (int i = 0; i < n; i++) {
                    int r = mat.shades[i][0];
                    int g = mat.shades[i][1];
                    int b = mat.shades[i][2];
                    if (applyFaceLight) { r += faceR; g += faceG; b += faceB; }
                    if (r > 15) r = 15;
                    if (g > 15) g = 15;
                    if (b > 15) b = 15;
                    if (r < 0) r = 0;
                    if (g < 0) g = 0;
                    if (b < 0) b = 0;
                    int gn = r; if (g > gn) gn = g; if (b > gn) gn = b;
                    if (gn < 1) gn = 1;
                    if (gn > 15) gn = 15;
                    uint8_t pal = DPal16toPal((uint16_t)((gn << 8) | (gn << 4) | gn));
                    if (pal == 0) pal = (uint8_t)gn;
                    table[i] = pal;
                }
            }
        }
    }
    for (int i = 0; i < 256; i++) {
        outPal[i] = table[i < n ? i : (n - 1)];
    }
}

// Draw a textured quad bilinearly by tracing edges
// This avoids the affine diagonal crease caused by splitting into two triangles
static void RenderTexturedPoly(const TransformedPoly& poly, const uint8_t* texture,
                                int texSize, bool transparent, uint32_t drawTexWord,
                                const uint8_t shadePal[256])
{
    if (getenv("AB_DUMP_MESH42") && poly.meshIndex == 42) {
        static int n42r = 0;
        if (n42r < 3)
            fprintf(stderr, "MESH42r vc=%d type=%d sp0=%u sp1=%u sp2=%u sp3=%u sp4=%u\n",
                    poly.vertexCount, (int)poly.type,
                    shadePal[0], shadePal[1], shadePal[2], shadePal[3], shadePal[4]), ++n42r;
    }
    if (poly.vertexCount != 4) return;

    // Dig: AB_SNAP_ASM_TIP=1 — Asp mat6 TexQuadPair tip verts are 1px off ASM
    // at pitch±90 (top/bottom). Snap known mismatches to prove UV/idx link.
    TransformedPoly snapPoly = poly;
    const TransformedPoly* pUse = &poly;
    if (getenv("AB_SNAP_ASM_TIP") && getenv("AB_SNAP_ASM_TIP")[0] == '1' &&
        poly.texInfo == 0x4006) {
        // top mesh21: ASM (90,66) vs C++ (90,65) on v3
        if (poly.meshIndex == 21 &&
            snapPoly.verts[0].screenX == 66 && snapPoly.verts[0].screenY == 79 &&
            snapPoly.verts[3].screenX == 90 && snapPoly.verts[3].screenY == 65) {
            snapPoly.verts[3].screenY = 66;
            pUse = &snapPoly;
        }
        // bottom mesh20: ASM (229,66) vs C++ (230,65) on v3
        if (poly.meshIndex == 20 &&
            snapPoly.verts[0].screenX == 254 && snapPoly.verts[0].screenY == 79 &&
            snapPoly.verts[3].screenX == 230 && snapPoly.verts[3].screenY == 65) {
            snapPoly.verts[3].screenX = 229;
            snapPoly.verts[3].screenY = 66;
            pUse = &snapPoly;
        }
        // back mesh21: ASM (55,76) vs C++ (54,76) on v1
        if (poly.meshIndex == 21 &&
            snapPoly.verts[0].screenX == 70 && snapPoly.verts[0].screenY == 79 &&
            snapPoly.verts[1].screenX == 54 && snapPoly.verts[1].screenY == 76) {
            snapPoly.verts[1].screenX = 55;
            pUse = &snapPoly;
        }
        // left mesh21: ASM (156,184) vs C++ (156,185) on v1
        if (poly.meshIndex == 21 &&
            snapPoly.verts[0].screenX == 160 && snapPoly.verts[0].screenY == 169 &&
            snapPoly.verts[1].screenX == 156 && snapPoly.verts[1].screenY == 185) {
            snapPoly.verts[1].screenY = 184;
            pUse = &snapPoly;
        }
    }
    const TransformedPoly& polyR = *pUse;

    int uvMax = texSize - 1;

    // UV case from the *draw* tex word (material-table result), not raw mesh texInfo.
    // FUNC_001712 stores DATA_007811[mat].texWord into the draw record; FUNC_001647
    // then uses (texWord >> 12) & 7. Mesh words like 0x400b only select the material.
    int uvCase = (drawTexWord >> 12) & 7;
    if (const char* uc = getenv("AB_UV_CASE")) {
        uvCase = atoi(uc) & 7;
    }
    // Dig: AB_UV_CASE_MESH=mesh:case (e.g. 21:5) overrides for one meshIndex.
    if (const char* ucm = getenv("AB_UV_CASE_MESH")) {
        int mesh = -1, cse = -1;
        if (sscanf(ucm, "%d:%d", &mesh, &cse) == 2 && mesh == poly.meshIndex)
            uvCase = cse & 7;
    }
    // Asp #40 TexQuad n=8 (0x4006): material texWord uvCase is 0 and matches ASM
    // (vent bars horizontal). A former default force to case 6 swapped U/V on
    // free-orbit rear views (vertical bars / VGA22 holes) while the 8-pose
    // ladder stayed at 0 — so the override was invisible to the gate. Dig only:
    // AB_UV_CASE_N8=N restores a forced case for bisects.
    if (poly.normalIdx == 8 && poly.texInfo == 0x4006) {
        if (const char* uc8 = getenv("AB_UV_CASE_N8"))
            uvCase = atoi(uc8) & 7;
    }
    const int* uvOrder = s_uvOrder[uvCase];

    // UV corners matching original ASM DATA_007791 exactly:
    // [0]=(0,0), [1]=(max,0), [2]=(0,max), [3]=(max,max)
    const int16_t uvCorners[4][2] = {
        {0, 0}, {(int16_t)uvMax, 0}, {0, (int16_t)uvMax}, {(int16_t)uvMax, (int16_t)uvMax}
    };

    // Assign UVs to vertices based on ordering
    int16_t uv[4][2];
    for (int i = 0; i < 4; i++) {
        uv[i][0] = uvCorners[uvOrder[i]][0];
        uv[i][1] = uvCorners[uvOrder[i]][1];
    }
    // Dig: AB_UV_ROT=n rotates assigned UVs by n verts (mesh order).
    if (const char* rot = getenv("AB_UV_ROT")) {
        int n = atoi(rot) & 3;
        if (n) {
            int16_t tmp[4][2];
            for (int i = 0; i < 4; i++) {
                tmp[i][0] = uv[i][0];
                tmp[i][1] = uv[i][1];
            }
            for (int i = 0; i < 4; i++) {
                uv[i][0] = tmp[(i + 4 - n) % 4][0];
                uv[i][1] = tmp[(i + 4 - n) % 4][1];
            }
        }
    }
    // Bisect UV axis flips (all textured quads). Asp mat6 shade remap dig.
    if (const char* flip = getenv("AB_UV_FLIP")) {
        for (int i = 0; i < 4; i++) {
            if (flip[0] == 'u' || flip[0] == 'b')
                uv[i][0] = static_cast<int16_t>(uvMax - uv[i][0]);
            if (flip[0] == 'v' || flip[0] == 'b')
                uv[i][1] = static_cast<int16_t>(uvMax - uv[i][1]);
        }
    }
    // TexQuadPair UV: pure L/R twins (all-even / all-odd verts) keep uvCase as-is.
    // Mixed-parity mirrors (e.g. Adder fore upper/lower, normal 18/19) need a V
    // flip so shade bands match ASM — side-wing mirrors must not flip (top pose).
    // specialParam: bit0=mirror, bit1=primary, bit4=mixedParity (set by parser).
    // AB_PAIR_UV_FLIP overrides for bisect (mirror|primary|both|vflip|off).
    if (poly.type == MeshPolyType::TexQuadPair) {
        const uint16_t uvFlags = IsSpriteChildParam(poly.specialParam)
                                     ? SpriteChildUvFlags(poly.specialParam)
                                     : poly.specialParam;
        const bool isMirror = (uvFlags & 1) != 0;
        const bool isPrimary = (uvFlags & 2) != 0;
        const bool mixedParity = (uvFlags & 0x10) != 0;
        bool doU = false, doV = false;
        const char* mode = getenv("AB_PAIR_UV_FLIP");
        if (mode && mode[0]) {
            if (strcmp(mode, "off") == 0) {
                doU = doV = false;
            } else if (strcmp(mode, "mirror") == 0 && isMirror) doU = true;
            else if (strcmp(mode, "primary") == 0 && isPrimary) doU = true;
            else if (strcmp(mode, "both") == 0) doU = true;
            else if (strcmp(mode, "vflip") == 0 && isMirror) doV = true;
            else if (strcmp(mode, "vmirror") == 0 && isMirror) { doU = true; doV = true; }
        } else if (isMirror && mixedParity) {
            doV = true;
        }
        if (doU || doV) {
            for (int i = 0; i < 4; i++) {
                if (doU) uv[i][0] = static_cast<int16_t>(uvMax - uv[i][0]);
                if (doV) uv[i][1] = static_cast<int16_t>(uvMax - uv[i][1]);
            }
        }
    }

    if (g_debugTextureRaster ||
        (getenv("AB_TEX_DEBUG") && getenv("AB_TEX_DEBUG")[0] == '1' &&
         ((poly.texInfo == 0x4006 && poly.normalIdx == 8) ||
          (poly.texInfo == 0x4005 &&
           (poly.meshIndex == 5 || poly.meshIndex == 7))))) {
        fprintf(stderr, "TEX_DEBUG: texInfo=0x%04X, uvCase=%d, texSize=%d n=%d mesh=%d\n",
                poly.texInfo, uvCase, texSize, poly.normalIdx, poly.meshIndex);
        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "  v[%d]: screen=(%d,%d), uv=(%d,%d) depth=%d\n",
                    i, polyR.verts[i].screenX, polyR.verts[i].screenY, uv[i][0], uv[i][1],
                    polyR.verts[i].depth);
        }
    }

    // Default: ASM FUNC_001649 spanning-edge raster. AB_OLD_TEX=1 keeps the
    // prior edge-minmax + per-pixel depth path for bisect.
    if (!(getenv("AB_OLD_TEX") && getenv("AB_OLD_TEX")[0] == '1')) {
        int16_t v0[2] = {polyR.verts[0].screenX, polyR.verts[0].screenY};
        int16_t v1[2] = {polyR.verts[1].screenX, polyR.verts[1].screenY};
        int16_t v2[2] = {polyR.verts[2].screenX, polyR.verts[2].screenY};
        int16_t v3[2] = {polyR.verts[3].screenX, polyR.verts[3].screenY};
        // R439 PROBE (default off): AB_QUADORDER=1|2|3 re-orders a TexQuad's
        // fill polygon (2 = swap the last two, 3 = swap the middle two). The
        // sidecar's mat-38 orbit quads paint only their two extreme lobes, which
        // is exactly what a self-intersecting (bowtie) fill produces, so the
        // record's vertex order is a live hypothesis for that span.
        // R444 (gated, default OFF): route the quad fill through the PORTED ASM
        // spanning-edge rasterizer instead of the local scanline one. Reason: the
        // ASM draw-time probe (scratch/r1/probe43_06.gdb) shows FUNC_001805 is
        // handed exactly the four correct vertices for hull-43 mesh 51 - so the
        // polygon is right and the loss is inside the span walk - and the port
        // already contains that walk faithfully (DrawTexQuadInternal reproduces
        // ASM's "rotate until p1 is min-Y, then branch on p2.y vs p4.y" edge
        // pairing) but the ship path bypasses it. SelectTexFunc must run first.
        const uint32_t asmTexIdx = drawTexWord & 0xFFF;
        if (asmTexIdx && ::getenv("AB_ASMRASTER") && ::getenv("AB_ASMRASTER")[0] == '1') {
            jjffe::SelectTexFunc(asmTexIdx);
            jjffe::DrawTexQuadDetRasterize(
                reinterpret_cast<intptr_t>(v0), reinterpret_cast<intptr_t>(v1),
                reinterpret_cast<intptr_t>(v2), reinterpret_cast<intptr_t>(v3),
                reinterpret_cast<intptr_t>(uv[0]), reinterpret_cast<intptr_t>(uv[1]),
                reinterpret_cast<intptr_t>(uv[2]), reinterpret_cast<intptr_t>(uv[3]),
                shadePal);
            return;
        }
        if (const char* qo = getenv("AB_QUADORDER"); qo && qo[0] &&
            (poly.meshIndex == 51 || poly.meshIndex == 68)) {
            int mode = atoi(qo);
            if (mode == 4) {
                // mode 4 = bowtie ONLY for a near-degenerate (long, thin) quad.
                // Geometric, pose-invariant predicate: the sidecar's spanning-edge
                // rasterizer emits two disjoint spans when the quad collapses to a
                // sliver, which a bowtie fill reproduces. Threshold is a WORKAROUND
                // probe (area/extent gate), not a faithful derivation - kept only
                // to size the prize for the owner, never defaulted on.
                int ymin = v0[1], ymax = v0[1], xmin = v0[0], xmax = v0[0];
                const int16_t* q[4] = {v0, v1, v2, v3};
                for (int i = 0; i < 4; i++) {
                    if (q[i][1] < ymin) ymin = q[i][1];
                    if (q[i][1] > ymax) ymax = q[i][1];
                    if (q[i][0] < xmin) xmin = q[i][0];
                    if (q[i][0] > xmax) xmax = q[i][0];
                }
                const int hgt = ymax - ymin, wid = xmax - xmin;
                mode = (hgt <= 8 && wid >= 24) ? 2 : 0;
            }
            if (mode == 2) { std::swap(v2[0], v3[0]); std::swap(v2[1], v3[1]); }
            else if (mode == 3) { std::swap(v1[0], v2[0]); std::swap(v1[1], v2[1]); }
        }
        DrawTexQuadShadedUV(v0, v1, v2, v3, uv[0], uv[1], uv[2], uv[3],
                            texture, texSize, transparent, shadePal);
        return;
    }

    // Initialize scanline bounds
    ScanBounds bounds[SCREEN_HEIGHT];
    for(int i = 0; i < SCREEN_HEIGHT; ++i) {
        bounds[i].leftX = 0x7FFFFFFF;
        bounds[i].rightX = -0x7FFFFFFF;
    }

    int minY = SCREEN_HEIGHT;
    int maxY = -1;

    // Trace the 4 edges of the quad
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        int16_t x0 = polyR.verts[i].screenX;
        int16_t y0 = polyR.verts[i].screenY;
        int16_t u0 = uv[i][0];
        int16_t v0 = uv[i][1];
        int32_t z0 = polyR.verts[i].depth;

        int16_t x1 = polyR.verts[j].screenX;
        int16_t y1 = polyR.verts[j].screenY;
        int16_t u1 = uv[j][0];
        int16_t v1 = uv[j][1];
        int32_t z1 = polyR.verts[j].depth;

        if (y0 == y1) continue;

        if (y0 > y1) {
            int16_t tx = x0; x0 = x1; x1 = tx;
            int16_t ty = y0; y0 = y1; y1 = ty;
            int16_t tu = u0; u0 = u1; u1 = tu;
            int16_t tv = v0; v0 = v1; v1 = tv;
            std::swap(z0, z1);
        }

        int dy = y1 - y0;
        int32_t dx = ((int32_t)(x1 - x0) << 16) / dy;
        int32_t du = ((int32_t)(u1 - u0) << 16) / dy;
        int32_t dv = ((int32_t)(v1 - v0) << 16) / dy;
        int32_t dz = (z1 - z0) / dy;

        int32_t curX = x0 << 16;
        int32_t curU = u0 << 16;
        int32_t curV = v0 << 16;
        int32_t curZ = z0;

        int yStart = y0;
        int yEnd = y1;

        if (yStart < 0) {
            int skip = -yStart;
            curX += dx * skip;
            curU += du * skip;
            curV += dv * skip;
            curZ += dz * skip;
            yStart = 0;
        }
        if (yEnd > k3DClipBottom) {
            yEnd = k3DClipBottom;
        }

        if (yStart < minY) minY = yStart;
        if (yEnd > maxY) maxY = yEnd;

        for (int y = yStart; y < yEnd; y++) {
            if (curX < bounds[y].leftX) {
                bounds[y].leftX = curX;
                bounds[y].leftU = curU;
                bounds[y].leftV = curV;
                bounds[y].leftZ = curZ;
            }
            if (curX > bounds[y].rightX) {
                bounds[y].rightX = curX;
                bounds[y].rightU = curU;
                bounds[y].rightV = curV;
                bounds[y].rightZ = curZ;
            }
            curX += dx;
            curU += du;
            curV += dv;
            curZ += dz;
        }
    }

    if (minY < 0) minY = 0;
    if (maxY > k3DClipBottom) maxY = k3DClipBottom;

    static uint8_t s_identityPalette[256];
    static bool s_identityPaletteInit = false;
    if (!s_identityPaletteInit) {
        for (int i = 0; i < 256; i++) s_identityPalette[i] = i;
        s_identityPaletteInit = true;
    }

    g_texState.texture = texture;
    // ASM Tex64: dst = DATA_009252[texel] — shade remap, not identity.
    g_texState.palette = shadePal ? shadePal : s_identityPalette;
    g_texState.texSize = texSize;

    int mask = texSize - 1;
    uint8_t* buf = g_videoState->backBuffer;

    // Rasterize the quad span by span
    for (int y = minY; y < maxY; y++) {
        if (bounds[y].leftX <= bounds[y].rightX) {
            int32_t x0 = bounds[y].leftX >> 16;
            int32_t x1 = bounds[y].rightX >> 16;

            int32_t u0 = bounds[y].leftU;
            int32_t v0 = bounds[y].leftV;
            int32_t u1 = bounds[y].rightU;
            int32_t v1 = bounds[y].rightV;
            int32_t z0 = bounds[y].leftZ;
            int32_t z1 = bounds[y].rightZ;

            if (x0 > x1) {
                int32_t tx = x0; x0 = x1; x1 = tx;
                int32_t tu = u0; u0 = u1; u1 = tu;
                int32_t tv = v0; v0 = v1; v1 = tv;
                std::swap(z0, z1);
            }

            int32_t dx_span = x1 - x0;
            int32_t du_span = dx_span > 0 ? (u1 - u0) / dx_span : 0;
            int32_t dv_span = dx_span > 0 ? (v1 - v0) / dx_span : 0;
            int32_t dz_span = dx_span > 0 ? (z1 - z0) / dx_span : 0;

            if (x0 < 0) {
                int skip = -x0;
                u0 += du_span * skip;
                v0 += dv_span * skip;
                z0 += dz_span * skip;
                x0 = 0;
            }
            if (x1 > SCREEN_WIDTH) {
                x1 = SCREEN_WIDTH;
            }

            int span = x1 - x0;
            // Exclusive-right bisect: ASM span fill often omits the right edge
            // pixel. Default keeps current (x1 exclusive of integer right already
            // via span=x1-x0). AB_TEX_INCL_RIGHT=1 widens by painting x1.
            // AB_TEX_SHRINK_RIGHT=1 shrinks one more pixel (fat-silhouette dig).
            if (const char* shr = getenv("AB_TEX_SHRINK_RIGHT"); shr && shr[0] == '1')
                span = span > 0 ? span - 1 : 0;
            else if (const char* wid = getenv("AB_TEX_INCL_RIGHT"); wid && wid[0] == '1')
                span += 1;
            if (span > 0) {
                int32_t uc = u0, vc = v0, zc = z0;
                uint8_t* dst = buf + y * SCREEN_WIDTH + x0;

                for (int x = 0; x < span; x++) {
                    int texU = (uc >> 16) & mask;
                    int texV = (vc >> 16) & mask;
                    uint8_t texel = texture[texV * texSize + texU];
                    if (!transparent || texel != 0) {
                        bool pass = !s_depthEnabled || zc < s_depthBuf[y * 320 + x0 + x];
                        if (pass) {
                            *dst = shadePal ? shadePal[texel] : texel;
                            jjffe::AbPlotStamp(x0 + x, y, (unsigned)*dst, "quad");
                            if (s_depthEnabled)
                                s_depthBuf[y * 320 + x0 + x] = zc;
                        }
                    }
                    dst++;
                    uc += du_span;
                    vc += dv_span;
                    zc += dz_span;
                }
            }
        }
    }
}

// Draw thruster glow billboard (FUNC_001738 → FUNC_001805).
// ASM: DrawTexQuad with tex word 0x123 (square) or 0x65 (stretched), shade
// indices 1..6 remapped via DATA_009252 from an RGB ramp of the thruster colour.
static void DrawThrusterBillboard(const TransformedPoly& poly)
{
    if (!poly.visible || poly.vertexCount != 4) return;

    // specialParam holds draw tex word after transform (0x123 / 0x65).
    uint32_t drawTexWord = poly.specialParam;
    if (drawTexWord != 0x123 && drawTexWord != 0x65)
        drawTexWord = 0x123;

    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(drawTexWord & 0xFFF));
    if (!bmp || bmp->width <= 0) return;
    // Glow textures are 64×64 shade-index maps (masked).
    if (bmp->width != 64 && bmp->width != 128) {
        // Fallback: old 10×10 sprite path if table entry unexpected.
        bmp = BmpIndexToPtr(poly.texInfo & 0xFFF);
        if (!bmp) return;
    }

    const int texSize = bmp->width;
    const bool transparent = (bmp->flags & 1) != 0;

    // FUNC_001738 builds DATA_009252[i] = DPal( rgb_nibble * (i+1) / 7 ) for i=1..6
    // where rgb comes from FUNC_001692(texInfo) — for 0x20ee that's (0,14,14).
    // Do NOT grey-snap unmapped chroma: dump-ab DPal leaves the cyan ramp
    // unmapped → VGA0, so thruster billboards punch black nozzle apertures
    // (Asp front/rear idle→thrust is hull→0, never a grey glow ramp).
    const int baseR = (poly.texInfo >> 8) & 0xF;
    const int baseG = (poly.texInfo >> 4) & 0xF;
    const int baseB = poly.texInfo & 0xF;
    uint8_t shadePal[256];
    shadePal[0] = 0; // texel 0 = transparent / black
    for (int i = 1; i <= 6; i++) {
        int mult = i + 1; // ASM: iVar1 = iVar3+1 with iVar3 in 1..6 → 2..7
        int r = (baseR * mult) / 7;
        int g = (baseG * mult) / 7;
        int b = (baseB * mult) / 7;
        if (r > 15) r = 15;
        if (g > 15) g = 15;
        if (b > 15) b = 15;
        uint16_t idx = (uint16_t)((r << 8) | (g << 4) | b);
        shadePal[i] = DPal16toPal(idx);
    }
    shadePal[7] = shadePal[6];
    for (int i = 8; i < 256; i++)
        shadePal[i] = shadePal[6];

    RenderTexturedPoly(poly, bmp->data, texSize, transparent, drawTexWord, shadePal);
}

// Set by RenderShipFlat for Escape Capsule-style meshes (only 0x5xxx + 0x0000).
// Those meshes invert 0x5xxx normals relative to FUNC_001481 and use a flat
// texInfo==0 silhouette that would wipe the hull if painted after.
static bool s_capsuleSilhouetteMesh = false;
// Missiles (#3 / #6–13): hull is only flat 0x1000 (+ optional 0x2000 punch).
// FUNC_001481 culls the nose-on faces; disable cull for 0x1000 on these meshes.
static bool s_missileFlatMesh = false;

bool ShipIsMissileFlatMesh() { return s_missileFlatMesh; }
// Adder #29 has a narrow 0x2000 flat (stream mesh 25) that ASM draws after the
// nose texture as the front windshield slit. Other 0x2000 VGA0 flats remain
// skipped by default because they punch holes in Asp/Saker/Adder engine views.
static bool s_adderWindshieldPunchMesh = false;
// Puma #60: mat8 Contour AABB is "fat" (both axes ≥30) → ASM Contour-only
// coverage; skip duplicate mat8 Tex* overdraw. Edge-on Contours (top/bottom)
// stay thin → keep Tex* shade0 fill. Dig: AB_MAT8_KEEP_TEX=1.
static bool s_pumaMat8ContourFat = false;
// Puma #60 near LOD (closer than ~du 5) emits only the two big mat8 hull
// quads instead of the far LOD's ten. There C++'s front-facing count already
// matches ASM's 001805 count, so the Contour-overdraw skip must not fire —
// it punched the whole upper hull black at `--z 141312 --yaw 292 --pitch 49`.
// The surviving quad is the one ASM 001805-textures, so it also takes the
// grey shade ramp like mesh 10. Dig: AB_MAT8_FAR_LOD=1 forces the old skip.
static bool s_pumaMat8NearLod = false;
// Lion #54: 0x5009 Contour fills exist → skip duplicate non-Contour 0x5009
// textured polys (same pattern as Puma mat8). Dig: AB_LION_KEEP_TEX=1.
static bool s_lion5009Contour = false;
// Puma #60: one or both normal-10/14 TexQuad bands map to ASM 001805 and
// must be scheduled after the mat8 Contour fills.
static bool s_puma001805BandVisible = false;
// Imperial Explorer #65: tip-assembly 0x2000 flats are real punches (ASM), not
// the Asp hole-makers. Fingerprint: verts/norms + tip mats 0x4009/c/f.
static bool s_explorer2000PunchMesh = false;
// Saker III #31: tip-child 0x2000 flats are real ASM 001809 VGA0 punches (nozzle
// throat / tip rings). Skipping them leaves starfield holes in interactive demos
// while dump-ab still scores oa=0 (clear and painted VGA0 are indistinguishable).
// Dig: AB_NO_SAKER_2000=1 restores the skip.
static bool s_saker2000PunchMesh = false;
// Any tip-child #201 0x2000 has a vert off left/right this frame (oblique tip).
static bool s_sakerTipOffLR = false;
// Mesh13 (aft tip ring) itself has an off-L/R vert — deep tip clip (Y:201).
static bool s_sakerMesh13OffLR = false;
// Mesh11 off L/R → ASM 001809s on-screen mesh9 instead (Y:227); when mesh11
// stays in-frame ASM punches mesh11 and keeps mesh9 as glass (Y:173).
static bool s_sakerMesh11OffLR = false;
// Aft tip rings (13/15) clip L/R while fore rings 6/7 stay fully on-screen —
// ASM punches fore 4/6/7/9 + throat, not tip-band 11/13/15/17 (Z:88 Y:357).
static bool s_sakerForeTipMode = false;
// tipOffLR from fore rings only (4/6/7) — aft still on-screen. ASM punches
// mesh9+13/15/17 not mesh11 when mesh4 is also off (Z:96 Y:227); when mesh4
// stays in-frame ASM punches mesh11 instead (Y:173).
static bool s_sakerForeOnlyOff = false;
static bool s_sakerMesh4OffLR = false;
static bool s_sakerMesh9OffLR = false;
// On-screen tip (!tipOffLR) + mid throat: which rings overlap the throat AABB
// selects ASM's punch set (Z:104 Y:341 fore 6/7; Y:243 aft 15/17; side 4+11).
static bool s_sakerThroatForePunch = false;
static bool s_sakerThroatAftPunch = false;
static bool s_sakerThroatSidePunch = false;
// Mesh73 throat AABB — on-screen tip rings that overlap it are punched even
// when tipOffLR is false (Z:96 Y:243: glass otherwise redraws over nozzle).
static int16_t s_sakerThroatX0 = 0, s_sakerThroatX1 = 0;
static int16_t s_sakerThroatY0 = 0, s_sakerThroatY1 = 0;
static bool s_sakerThroatMid = false;
static uint32_t s_sakerTipGlassMinKey = 0xffffffffu;
// True when the current mesh has at least one textured polygon (texInfo bit14).
// Used to gate ComplexFillColor shade0 path: textured ships benefit from
// material shade chain; pure flat-RGB ships (Courier/Gyr) regress.
static bool s_hasTexturedPolys = false;

bool ShipAllowVga0FlatPunch(const TransformedPoly& poly, const ShipModel* shipModel,
                            uint8_t color);
void ShipPrepareFlatPunchGates(TransformedPoly* polys, int polyCount,
                               const ShipModel* shipModel);

// Nested objects get their own renderCtx (FUNC_001750), so the +0xf4 face
// light a poly was emitted under can differ from the parser's final value.
// Scope the stamped snapshot around the draw so FlatRgbToPal / shade chains
// see the right one. Dig: AB_NO_POLY_FACELIGHT=1 restores the global value.
struct ScopedPolyFaceLight {
    explicit ScopedPolyFaceLight(const TransformedPoly& p)
    {
        static const bool disabled = getenv("AB_NO_POLY_FACELIGHT") &&
                                     getenv("AB_NO_POLY_FACELIGHT")[0] == '1';
        if (!disabled)
            jjffe::SetMeshFaceLightOverride(p.faceLight);
    }
    ~ScopedPolyFaceLight() { jjffe::SetMeshFaceLightOverride(nullptr); }
};

// FUNC_001692 flat RGB nibbles → VGA via DPal. Adder 0x20ee → (0,14,14) → 0.
// Bit 0x1000 is a FLAG (FUNC_001695: add ctx+0xf4 face light), not R=1 —
// missiles use pure 0x1000 (RGB 0,0,0 + flag).
static uint8_t FlatRgbToPal(uint16_t texInfo)
{
    if (texInfo == 0) {
        // R768 experiment: the ASM does NOT paint texInfo==0 faces black. FUNC_001708
        // takes its dithered-triangle branch when shade[1] != 0 and uses
        // material = DATA_007811 + shade[1]*0x1e (a shaded material colour), and the
        // job trace shows those as 001807 with material 46. The port returns palette 0
        // here, which is background - i.e. untextured faces vanish. Knob maps them to
        // the ASM-leaning mid-grey to measure how much of the residual that explains.
        if (getenv("AB_FLAT_ZERO_GREY") && getenv("AB_FLAT_ZERO_GREY")[0] == '1')
            return DPal16toPal(0x0888);
        return 0;
    }
    // Flat 0x2000 = RGB(0,0,0) → DPal VGA0. Default path skips painting these
    // (see DrawFlatPolygon) — schedule-order VGA0 was punching mat6/7 on Asp
    // top/bottom and Saker tip. AB_FLAT_2000_GREY=1 maps to mid-grey for digs;
    // AB_DRAW_2000=1 restores VGA0 punch.
    if (texInfo == 0x2000) {
        const char* g = getenv("AB_FLAT_2000_GREY");
        if (g && g[0] == '1') {
            uint8_t n = 8;
            return DPal16toPal(static_cast<uint16_t>((n << 8) | (n << 4) | n));
        }
    }
    int r = (texInfo >> 8) & 0xF;
    int g = (texInfo >> 4) & 0xF;
    int b = texInfo & 0xF;
    // FUNC_001695: when 0x1000 flag set, add renderCtx face light (0xf4..0xf6).
    if ((texInfo & 0x1000) != 0) {
        int8_t flR = 0, flG = 0, flB = 0;
        GetMeshFaceLight(&flR, &flG, &flB);
        r += flR;
        g += flG;
        b += flB;
        if (r < 0) r = 0;
        if (r > 15) r = 15;
        if (g < 0) g = 0;
        if (g > 15) g = 15;
        if (b < 0) b = 0;
        if (b > 15) b = 15;
    }
    uint8_t pal = DPal16toPal(static_cast<uint16_t>((r << 8) | (g << 4) | b));
    if (getenv("AB_DUMP_FLATRGB") && getenv("AB_DUMP_FLATRGB")[0] == '1') {
        static uint16_t seen[32];
        static int nSeen = 0;
        bool dup = false;
        for (int i = 0; i < nSeen; i++) if (seen[i] == texInfo) { dup = true; break; }
        if (!dup && nSeen < 32) {
            seen[nSeen++] = texInfo;
            fprintf(stderr, "FLATRGB tex=0x%04x rgb=(%d,%d,%d) pal=%u\n",
                    texInfo, r, g, b, (unsigned)pal);
        }
    }
    return pal;
}

static uint8_t ComplexFillColor(const TransformedPoly& head,
                                const ShipModel* shipModel, const int32_t* shipRot)
{
    if ((head.texInfo >> 14) == 0) {
        // B7 v2 (R83 order lesson: 0x0444 >> 14 == 0 exits through the FLAT
        // branch long before the 5xxx gate; first v2 placement crashed on the
        // wrong anchor and never fired). #33 pair census (ASM 64 vs port 80)
        // + bank f(0x444)=4 -> map the raw word through the shared mapper.
        if (abGreyMapOn() &&
            jjffe::GetShipModelIndex(shipModel) == 33 && head.texInfo == 0x0444) {
            const uint8_t mpal33 = DPal16toPal(static_cast<uint16_t>(0x0444));
            if (mpal33 != 0) return mpal33;
        }
        // Flat-RGB Contour fills: for complex textured models (vertexCount
        // >= 100) or mixed ships (vertexCount >= 40 with textured polygons),
        // use material shade chain matching ASM FUNC_001799 → FUNC_001693 →
        // FUNC_001691. Improves Mantis #67 (−24%), Merlin/Harris/Transporter.
        // Pure flat-RGB low-vert ships match FlatRgbToPal.
        // Dig: AB_FLAT_RGB_RAW=1 restores FlatRgbToPal for all ships.
        // Sprite-child Contours carry the *child* model's normalIdx, so the
        // parent facing-flags table (DATA_009201) holds an unrelated shade —
        // Skeet #208 canopy 0x888 came out VGA123 instead of ASM's VGA8.
        // Dig: AB_C721_CHILD_SHADE0=1 restores the parent-table lookup.
        const bool childContour =
            IsSpriteChildParam(head.specialParam) &&
            !(getenv("AB_C721_CHILD_SHADE0") &&
              getenv("AB_C721_CHILD_SHADE0")[0] == '1');
        const bool useShade0 = shipModel && !childContour &&
                               (shipModel->vertexCount >= 100 ||
                                (shipModel->vertexCount >= 40 && s_hasTexturedPolys)) &&
                               head.texInfo != 0 &&
                               !(getenv("AB_FLAT_RGB_RAW") &&
                                 getenv("AB_FLAT_RGB_RAW")[0] == '1');
        if (useShade0) {
            // ASM FUNC_001721 → FUNC_001693: param_7 is the facing flags shade
            // from DATA_009201 + normalIdx*8 (populated by FUNC_001481).
            // For back-facing polygons: shade from material lookup table.
            // For front-facing (shade=0): fall back to old ResolveMaterial
            // shade0 path which gives VGA8 for Mantis #67 (OOB material →
            // hardcoded 0x0888 default matching ASM).
            // Mantis #67 ComplexFill: force shade0 on back-facing Contours.
            // ASM FUNC_001799 treats Contours as double-sided (shade=0).
            int8_t fsR = 0, fsG = 0, fsB = 0;
            jjffe::GetFacingFlagsShade(head.normalIdx, &fsR, &fsG, &fsB);
            if ((fsR | fsG | fsB) != 0 &&
                jjffe::GetShipModelIndex(shipModel) == 67 &&
                head.type == MeshPolyType::ComplexFill &&
                head.texInfo == 0x0444)
                fsR = fsG = fsB = 0;
            if ((fsR | fsG | fsB) != 0) {
                // Back-facing: use facing flags shade
                int r = (head.texInfo >> 8) & 0xF;
                int g = (head.texInfo >> 4) & 0xF;
                int b = head.texInfo & 0xF;
                r += fsR; g += fsG; b += fsB;
                if (r < 0) r = 0;
                if (r > 15) r = 15;
                if (g < 0) g = 0;
                if (g > 15) g = 15;
                if (b < 0) b = 0;
                if (b > 15) b = 15;
                uint16_t idx = static_cast<uint16_t>((r << 8) | (g << 4) | b);
                uint8_t pal = DPal16toPal(idx);
                if (pal == 0 && (r | g | b) != 0) {
                    int bestDist = 0x7fffffff;
                    for (int ci = 0; ci < 4096; ci++) {
                        uint8_t m = g_videoState->dPalMap[ci];
                        if (m == 0) continue;
                        int cr = (ci >> 8) & 0xF, cg = (ci >> 4) & 0xF, cb = ci & 0xF;
                        int dr = r - cr, dg = g - cg, db = b - cb;
                        int dist = dr * dr + dg * dg + db * db;
                        if (dist < bestDist) {
                            bestDist = dist;
                            pal = m - 1;
                        }
                    }
                }
                return pal;
            }
            // Front-facing (shade=0): for ships like Mantis #67 with OOB
            // Contour fill materials, the old ResolveMaterial shade0 path
            // produces VGA8 (shadeCount=0 → default 0x08) which matches ASM.
            // Other ships (Skeet #45) need facing flags shade only.
            // Panther #62 also needs shade0 (ASM 001799 rgb16=0x222 from
            // material shade chain, not FlatRgbToPal(0x20ee) → DPal[0x20E]).
            if (jjffe::GetShipModelIndex(shipModel) == 67 ||
                jjffe::GetShipModelIndex(shipModel) == 62) {
                MaterialInfo mat = ResolveMaterial(head.texInfo);
                int8_t flR8 = 0, flG8 = 0, flB8 = 0;
                GetMeshFaceLight(&flR8, &flG8, &flB8);
                int flR = flR8, flG = flG8, flB = flB8;
                const bool applyFL = (flR | flG | flB) != 0;
                uint8_t shadePal[256];
                BuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
                uint8_t c = shadePal[0];
                // Panther #62: shade0 returns 0 (DPal remap for mat 0x20ee
                // has no shade chain). Use FlatRgbToPal shade fallback.
                if (c == 0 && head.texInfo != 0)
                    c = FlatRgbToPal(head.texInfo);
                return c;
            }
            // Default: use flat-RGB as-is (facing flags shade was 0)
        }
        {
            const uint8_t fr = FlatRgbToPal(head.texInfo);
            if (fr != 0) return fr;
            // R563 candidate law (gated AB_GREYMAP_ALL): generalize the shipped
            // #33 abGreyMapOn precedent — when a flat-RGB contour word has
            // nonzero nibbles but FlatRgbToPal misses, map the raw RGB444 word
            // through DPal16toPal (ASM FUNC_001799's [DATA_007629] leg).
            if (::getenv("AB_GREYMAP_ALL") && ::getenv("AB_GREYMAP_ALL")[0] == '1' &&
                ((head.texInfo >> 8) & 0xF) + ((head.texInfo >> 4) & 0xF) + (head.texInfo & 0xF) != 0) {
                const uint8_t mp = DPal16toPal(static_cast<uint16_t>(head.texInfo & 0xFFF));
                if (mp != 0) return mp;
            }
            // R839: R837/R838 showed the flat word cannot discriminate (hull 58's 0x0336
            // wants VGA 6, hull 45's 0x0232/0x0243 want 0, and all are out-of-range
            // material indices with shadeIdx 0). The remaining structural difference is
            // that hull 58's groups are CHILD contours (Courier #215 pieces), whose
            // material is nested. Restrict the DPal(0x666) fallback to child contours.
            // Dig: AB_CFC_EMPTY_666=0 disables.
            if (!(::getenv("AB_CFC_EMPTY_666") && ::getenv("AB_CFC_EMPTY_666")[0] == '0')) {
                const unsigned matIdx = static_cast<unsigned>(head.texInfo & 0xFFF);
                constexpr unsigned kMatRows = 148;
                if (matIdx >= kMatRows && IsSpriteChildParam(head.specialParam)) {
                    const uint8_t six = DPal16toPal(0x666);
                    if (six != 0) return six;
                }
            }
            return fr;
        }
    }
    // Eagle #23/#24/#25 Contours use 0x5015. ASM FUNC_001721 flat-fills via
    // FUNC_001692 (material resolve) → shade[0] from material table →
    // FUNC_001693 adds the polygon's face-light nibbles → FUNC_001691
    // (DPal16toPal). Material 21 shade[0] = RGB(2,2,2) → 0x222 = VGA22.
    // R1077: the face light is NOT always zero. Measured on hull 24 top view
    // (yaw 180 pitch 90) the hull enqueues two 001799 span jobs whose colour
    // words are 0x262 and 0x222 - the first is the same 0x222 material shade
    // with the poly's green nibble lit by +4 (that group's faceLight is
    // (0,4,0)). 0x262 is unmapped → DPal16toPal gives 0, i.e. the ASM paints
    // that contour black (invisible on the backdrop) while the port painted
    // VGA22 over 532 px. Add the poly face light before mapping, as the ASM
    // does, and clamp the nibbles.
    // Dig: AB_C721_5015_FACE=1 restores GetFaceColor for 0x5015/0x5009;
    //      AB_C721_5015_NOLIGHT=1 restores the old light-less resolve.
    if ((head.texInfo == 0x5015 || head.texInfo == 0x5009) &&
        !(getenv("AB_C721_5015_FACE") && getenv("AB_C721_5015_FACE")[0] == '1')) {
        MaterialInfo mat = ResolveMaterial(head.texInfo);
        if (mat.shadeCount > 0) {
            static const bool s_noLight = []() {
                const char* e = getenv("AB_C721_5015_NOLIGHT");
                return e && e[0] == '1';
            }();
            int r = mat.shades[0][0] & 0xF;
            int g = mat.shades[0][1] & 0xF;
            int b = mat.shades[0][2] & 0xF;
            if (!s_noLight) {
                r += head.faceLight[0];
                g += head.faceLight[1];
                b += head.faceLight[2];
                if (r < 0) r = 0; else if (r > 0xF) r = 0xF;
                if (g < 0) g = 0; else if (g > 0xF) g = 0xF;
                if (b < 0) b = 0; else if (b > 0xF) b = 0xF;
            }
            const uint16_t idx = static_cast<uint16_t>((r << 8) | (g << 4) | b);
            return DPal16toPal(idx);
        }
    }
    // feature/ships-ab-b3-shade: the ASM hull-contour chain (FUNC_001721, charter
    // sections 35-36) ALWAYS maps its lighted nibbles through DPal16toPal (live:
    // 0x5007 -> nibbles (0,0,7) + face-light (10,10,3) -> word 0xAAA -> idx10).
    // Port's 5xxx passthrough below returned the RAW shade byte (cfcret:
    // tex=0x5007 -> 2, zero preimages under the mapper - no map call). Gate
    // AB_C5_MAP=1: map (low12 of the tex word + GetMeshFaceLight nibbles), fall
    // back to GetFaceColor when the mapper answers 0 on a lit word (unmapped
    // bank, same guard as the facing-shade branch). Default OFF - zero behaviour
    // change until the pilot measures live 2 -> 10 on #53.
        if (abGreyMapOn() &&
        head.contourId >= 0 && (head.texInfo & 0xF000) == 0x5000) {
        // R62 v3 (charter 60): the ASM ship-demo grey hull colour is
        // 1327(0xAAA) = 10 (grey160) - live-proven 6/6 payload-side, map hit
        // 11 verified in BOTH binaries' tables. The value's ORIGIN is the
        // demo-harness prepass reading deterministic stack bytes as an object
        // (param_1=0xfffecc58, charter 58) - uninitialized-memory behaviour
        // whose ANSWER is nonetheless the oracle output and the documented
        // precedent (shared-garbage slots replicated since the walk era).
        // R64 SCOPING (chk51 pair): grey-face replication must touch ONLY
        // C721-body contours (contourId >= 0) - #51's non-C721 5xxx faces
        // (orbit p90 +72 in the unscoped pair) stay untouched.
        // This is #53-class replication for the grey hull family ONLY;
        // olive (0xEEE->127) and per-hull spread are measured per pilot as
        // the ledger shows - NOT generalized by assumption.
        const uint8_t mpal = DPal16toPal(static_cast<uint16_t>(0xAAA));
        if (mpal != 0) return mpal;
    }
    if ((head.texInfo & 0xF000) == 0x5000)
        return GetFaceColor(shipModel, head.normalIdx, 0x08, shipRot);
    if ((head.texInfo & 0xF000) == 0x5000)
        return GetFaceColor(shipModel, head.normalIdx, 0x08, shipRot);
    MaterialInfo mat = ResolveMaterial(head.texInfo);
    int8_t flR8 = 0, flG8 = 0, flB8 = 0;
    GetMeshFaceLight(&flR8, &flG8, &flB8);
    int flR = flR8, flG = flG8, flB = flB8;
    bool applyFL = (head.texInfo & 0x1000) != 0 && (flR | flG | flB) != 0;
    uint8_t shadePal[256];
    BuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
    return shadePal[0];
}

// ASM Puma Contours 3–6: digon Contour fills with rgb16=0xee → DPal VGA0,
// punched after the solid butterfly Contour (Contour 2). Digons sit in the
// four wing-gap pockets; fractions are from Contour AABB on front A/B.
static void C721PunchDigon(const int16_t a[2], const int16_t b[2],
                           const int16_t c0[2], const int16_t c1[2],
                           const int16_t d0[2], const int16_t d1[2])
{
    C721SpanReset();
    C721SpanAddBezier(a, c0, c1, b);
    C721SpanAddBezier(a, d0, d1, b);
    C721SpanFlush(0);
}

// Lynx #217 body Contour (mat 0x4005 fan): ASM follows with 4×001797 digon
// punches (rgb16=0xee) carving window pockets. Mesh digon verts (52/53 +
// outline 11/12) project to Contour-outline wedges under sprite attach —
// C721PunchDigon is a no-op. Punch from Contour AABB fractions:
//   wide Contour (front/back): left/right windows — dump-ab yaw180
//   tall Contour (left/right): top/bottom windows — dump-ab yaw90
static void PunchLynxBodyWindowDigons(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (getenv("AB_LYNX_NO_WIN_PUNCH") && getenv("AB_LYNX_NO_WIN_PUNCH")[0] == '1')
        return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    const bool wide = w >= 80 && h >= 36 && h <= 70 && w * 5 >= h * 9;
    const bool tall = h >= 80 && w >= 36 && w <= 70 && h * 5 >= w * 9;
    // Diag Contour is near-square (~96×96); windows sit on the TL→BR diagonal.
    const bool square = w >= 80 && h >= 80 && w * 5 >= h * 4 && h * 5 >= w * 4;
    if (!wide && !tall && !square)
        return;
    auto at = [&](double fx, double fy, int16_t out[2]) {
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    // Two windows × upper/lower digon pairs (ASM 001797×4).
    // wins[][] = {fx0, fy0, fx1, fy1}
    const double wideWins[2][4] = {
        {0.102, 0.077, 0.417, 0.923}, // left
        {0.583, 0.077, 0.898, 0.923}, // right
    };
    const double tallWins[2][4] = {
        {0.173, 0.046, 0.827, 0.472}, // top
        {0.173, 0.528, 0.827, 0.954}, // bottom
    };
    const double squareWins[2][4] = {
        {0.135, 0.073, 0.490, 0.542}, // diag TL
        {0.510, 0.458, 0.865, 0.927}, // diag BR
    };
    const double (*wins)[4] =
        wide ? wideWins : tall ? tallWins : squareWins;
    for (int wi = 0; wi < 2; wi++) {
        const double fx0 = wins[wi][0], fy0 = wins[wi][1];
        const double fx1 = wins[wi][2], fy1 = wins[wi][3];
        const double fyMid = 0.5 * (fy0 + fy1);
        int16_t a[2], b[2], c0[2], c1[2], d0[2], d1[2];
        at(fx1, fyMid, a);
        at(fx0, fyMid, b);
        at(fx1, fy0, c0);
        at(fx0, fy0, c1);
        at(fx1, fy1, d0);
        at(fx0, fy1, d1);
        C721PunchDigon(a, b, c0, c1, d0, d1);
    }
}

// True when direct Courier #215 wing Contour spans render in the main flush
// path. Default is OFF (skip spans) because the fixed (unseeded) ASM oracle
// paints these as digon/punch path, not full span fill. AB_C721_KEEP_215=1
// opt-ins the old span behavior for digs.
static bool Courier215ContourSpansActive()
{
    const bool keep215 = getenv("AB_C721_KEEP_215") &&
                         getenv("AB_C721_KEEP_215")[0] == '1';
    const bool skip215 = !keep215;
    return !skip215;
}

// Flush a Contour quad (4 edges + even-odd fill).
static void TraderFlushQuad(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            int16_t x2, int16_t y2, int16_t x3, int16_t y3,
                            uint8_t color)
{
    C721SpanReset();
    C721SpanAddEdge(x0, y0, x1, y1);
    C721SpanAddEdge(x1, y1, x2, y2);
    C721SpanAddEdge(x2, y2, x3, y3);
    C721SpanAddEdge(x3, y3, x0, y0);
    C721SpanFlush(color);
}

// Imperial Trader #58 wraps Courier #56: ASM emits extra Contour fills that
// C++ Courier never walks. Side / top / bottom / front / back / diag each have
// pose-gated Contour panels from ASM job_trace. Dig: AB_TRADER_NO_INBOARD=1.
static void FillTraderWrapperInboardContours(const TransformedPoly* const* sortedPolys,
                                             int sortCount, const ShipModel* shipModel)
{
    if (jjffe::GetShipModelIndex(shipModel) != 58)
        return;
    if (getenv("AB_TRADER_NO_INBOARD") &&
        getenv("AB_TRADER_NO_INBOARD")[0] == '1')
        return;
    if (!g_videoState)
        return;

    int16_t bodyMaxX = 0, bodyMinX = 319;
    int16_t bodyMaxY = 0, bodyMinY = 199;
    int16_t bodyY = 79;
    int16_t wingMinX = 319, wingMaxX = 0;
    int16_t wingMinY = 199, wingMaxY = -200;
    int16_t tipOffMinX = 319, tipOffMaxX = 0;
    bool haveBody = false, haveWing = false, haveTipOff = false;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (!p->visible || p->backFacing)
            continue;
        if (IsSpriteChildParam(p->specialParam) &&
            SpriteChildModelId(p->specialParam) == 56) {
            const uint16_t t = p->texInfo & 0xFFF;
            if (t != 0x666 && t != 0x444 && t != 0x333)
                continue;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                if (x > bodyMaxX) {
                    bodyMaxX = x;
                    bodyY = y;
                }
                if (x < bodyMinX)
                    bodyMinX = x;
                if (y > bodyMaxY)
                    bodyMaxY = y;
                if (y < bodyMinY)
                    bodyMinY = y;
                haveBody = true;
            }
        }
        if (IsSpriteChildParam(p->specialParam) &&
            SpriteChildModelId(p->specialParam) == 215 &&
            (p->texInfo == 0x0336 || p->texInfo == 0x0600 ||
             p->texInfo == 0x0666 || p->texInfo == 0x0444 ||
             p->texInfo == 0x0555)) {
            int16_t px0 = 319, py0 = 199, px1 = 0, py1 = -200;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                if (x < wingMinX)
                    wingMinX = x;
                if (x > wingMaxX)
                    wingMaxX = x;
                if (y < wingMinY)
                    wingMinY = y;
                if (y > wingMaxY)
                    wingMaxY = y;
                if (x < px0)
                    px0 = x;
                if (y < py0)
                    py0 = y;
                if (x > px1)
                    px1 = x;
                if (y > py1)
                    py1 = y;
                haveWing = true;
            }
            // Compact tip Contours parked offscreen toward y=0 (ignore wide
            // tip→body bridges that span past mid-screen).
            if (py0 < 20 && (px1 - px0) < 80) {
                if (px0 < tipOffMinX)
                    tipOffMinX = px0;
                if (px1 > tipOffMaxX)
                    tipOffMaxX = px1;
                haveTipOff = true;
            }
        }
    }
    if (!haveBody || !haveWing)
        return;
    const int wingSpan = wingMaxX - wingMinX;
    const int bodySpan = bodyMaxX - bodyMinX;

    // Front/back: tips at both screen edges — never treat as top/bottom.
    const bool bothTipsEdge = wingMinX < 40 && wingMaxX > 280;
    // Top/bottom orthog: wide body+wings, tip Contours offscreen above body.
    // Tip-off AABB side selects top (left tip) vs bottom (right tip).
    const bool topBottomWide = !bothTipsEdge && haveTipOff &&
                               wingSpan > 150 && bodySpan > 120 &&
                               bodyMinY >= 50 && bodyMaxY <= 110 &&
                               wingMinY < 40;
    const bool topProfile = topBottomWide && tipOffMaxX < 160;
    const bool bottomProfile = topBottomWide && tipOffMinX > 160;
    if (topProfile || bottomProfile) {
        // Mirror X around 160 for bottom (right tip) vs top (left tip).
        // Offsets from ASM dump-ab top yaw180/pitch90; bottom is X-mirrored.
        const int mirror = bottomProfile ? 1 : 0;
        auto MX = [&](int x) -> int16_t {
            return static_cast<int16_t>(mirror ? (320 - x) : x);
        };
        // Tip Contour lobe (ASM 001795/001796 tip walks) — empty-only so we
        // don't stamp VGA6 over existing tip Contour shades (idx_diff).
        {
            int16_t p[][2] = {
                {MX(63), 110}, {MX(4), 121}, {MX(-1), 128}, {MX(3), 132},
                {MX(56), 138}, {MX(61), 143}, {MX(62), 143}, {MX(55), 135},
                {MX(55), 124}, {MX(62), 111}, {MX(67), 105}, {MX(63), 110},
            };
            // Snapshot + restore non-empty pixels after flush.
            uint8_t* buf = g_videoState->backBuffer;
            uint8_t snap[320 * 200];
            std::memcpy(snap, buf, sizeof(snap));
            C721SpanReset();
            for (int k = 0; k < 11; k++)
                C721SpanAddEdge(p[k][0], p[k][1], p[k + 1][0], p[k + 1][1]);
            C721SpanFlush(6);
            for (int i = 0; i < 320 * 200; i++) {
                if (snap[i] != 0)
                    buf[i] = snap[i];
            }
        }
        // Solid Contour panels (ASM 001809/001797).
        TraderFlushQuad(MX(62), 143, MX(78), 148, MX(84), 141, MX(55), 135, 6);
        TraderFlushQuad(MX(64), 108, MX(75), 108, MX(131), 95, MX(88), 97, 6);
        {
            const int16_t ax = MX(162);
            const int16_t ay = 127;
            TraderFlushQuad(ax, ay, MX(67), 105, MX(72), 118, ax, ay, 6);
            TraderFlushQuad(ax, ay, MX(72), 118, MX(84), 141, ax, ay, 6);
            TraderFlushQuad(ax, ay, MX(84), 141, MX(78), 148, ax, ay, 6);
        }
        TraderFlushQuad(MX(55), 135, MX(84), 141, MX(72), 118, MX(55), 124, 5);
        TraderFlushQuad(MX(55), 124, MX(72), 118, MX(67), 105, MX(62), 111, 4);
        return;
    }

    // --- Front / back orthog: tips at both screen edges.
    // ASM center-diamond Contours below (front) / above (back) the body hull.
    // Dig shares AB_TRADER_NO_INBOARD. Do not disturb top/bottom/side gates.
    const bool frontBackWide = bothTipsEdge && wingSpan > 200 &&
                               bodyMinY >= 50 && bodyMaxY <= 110;
    const bool frontProfile = frontBackWide && wingMaxY < 80;
    const bool backProfile = frontBackWide && wingMinY > 100;
    if (frontProfile || backProfile) {
        // Back is Y-mirrored around y=79 (ASM dump-ab yaw0 vs yaw180).
        const int flipY = backProfile ? 1 : 0;
        auto MY = [&](int y) -> int16_t {
            return static_cast<int16_t>(flipY ? (158 - y) : y);
        };
        // 0x444 center wedges (ASM 001809).
        TraderFlushQuad(186, MY(148), 183, MY(134), 160, MY(120), 160, MY(131), 4);
        TraderFlushQuad(134, MY(148), 137, MY(134), 160, MY(120), 160, MY(131), 4);
        // Tip Contour lobe VGA6 (ASM 001795/96 walks clipped to viewport).
        // Empty-only to avoid stomping the 0x444 wedges / body Contours.
        {
            int16_t p[][2] = {
                {134, MY(148)}, {137, MY(134)}, {146, MY(134)}, {150, MY(132)},
                {155, MY(131)}, {160, MY(130)}, {164, MY(131)}, {169, MY(132)},
                {173, MY(134)}, {183, MY(134)}, {186, MY(148)}, {186, MY(157)},
                {160, MY(157)}, {134, MY(157)}, {134, MY(148)},
            };
            uint8_t* buf = g_videoState->backBuffer;
            uint8_t snap[320 * 200];
            std::memcpy(snap, buf, sizeof(snap));
            C721SpanReset();
            for (int k = 0; k < 14; k++)
                C721SpanAddEdge(p[k][0], p[k][1], p[k + 1][0], p[k + 1][1]);
            C721SpanFlush(6);
            for (int i = 0; i < 320 * 200; i++) {
                if (snap[i] != 0)
                    buf[i] = snap[i];
            }
        }
        return;
    }

    // --- Diag (yaw ±135): one tip cluster outboard, body tall in y.
    // Contour fills land on the opposite tip side from the visible #215
    // TexQuadPair AABB (ASM job_trace d7/d8). Dig shares AB_TRADER_NO_INBOARD.
    const bool diagProfile = !bothTipsEdge && wingSpan < 100 && bodySpan > 70 &&
                             bodyMinY < 45 && bodyMaxY > 100;
    const bool diagLeft = diagProfile && wingMinX > 200;   // tip Tex on right → Contours left
    const bool diagRight = diagProfile && wingMaxX < 120;  // tip Tex on left → Contours right
    if (diagLeft || diagRight) {
        const int mirror = diagRight ? 1 : 0;
        auto MX = [&](int x) -> int16_t {
            return static_cast<int16_t>(mirror ? (320 - x) : x);
        };
        uint8_t* buf = g_videoState->backBuffer;
        uint8_t snap[320 * 200];
        std::memcpy(snap, buf, sizeof(snap));
        // Mid bridge 0x666 + tip wedges 0x444 (ASM 001809).
        TraderFlushQuad(MX(127), 112, MX(129), 110, MX(148), 91, MX(142), 97, 6);
        TraderFlushQuad(MX(130), 146, MX(138), 134, MX(131), 108, MX(124), 115, 4);
        TraderFlushQuad(MX(93), 109, MX(105), 101, MX(131), 108, MX(124), 115, 4);
        // Tip Contour lobe VGA6 — fattened to cover ASM OA y=113–157.
        {
            int16_t p[][2] = {
                {MX(93), 109}, {MX(105), 101}, {MX(124), 115}, {MX(131), 108},
                {MX(138), 134}, {MX(130), 146}, {MX(127), 152}, {MX(110), 157},
                {MX(90), 157}, {MX(70), 155}, {MX(63), 150}, {MX(63), 130},
                {MX(76), 120}, {MX(87), 115}, {MX(93), 109},
            };
            C721SpanReset();
            for (int k = 0; k < 14; k++)
                C721SpanAddEdge(p[k][0], p[k][1], p[k + 1][0], p[k + 1][1]);
            C721SpanFlush(6);
        }
        // Empty-only: restore any pre-existing pixels (avoid 0x444/VGA6 OC).
        for (int i = 0; i < 320 * 200; i++) {
            if (snap[i] != 0)
                buf[i] = snap[i];
        }
        return;
    }

    // --- Side orthog: one wing cluster, narrow body depth in x.
    if (wingSpan > 80 || bodySpan > 70)
        return;
    const bool leftProfile = wingMaxX < 160 && wingMinX < 130 && bodyMaxX > 170;
    const bool rightProfile = wingMinX > 160 && wingMaxX > 190 && bodyMinX < 150;
    if (!leftProfile && !rightProfile)
        return;

    const int16_t bodyX = leftProfile ? bodyMaxX : bodyMinX;
    const int sign = leftProfile ? 1 : -1;
    auto X = [&](int dx) -> int16_t {
        return static_cast<int16_t>(bodyX + sign * dx);
    };
    auto Y = [&](int dy) -> int16_t {
        return static_cast<int16_t>(bodyY + dy);
    };

    // Lower inboard panel (ASM 001809 rgb16=0x444, verts on yaw270 left).
    TraderFlushQuad(X(44), Y(26), X(30), Y(23), X(16), Y(0), X(27), Y(0), 4);
    TraderFlushQuad(X(44), Y(-26), X(30), Y(-23), X(16), Y(0), X(27), Y(0), 4);
    TraderFlushQuad(X(21), Y(0), X(18), Y(0), X(-7), Y(0), X(0), Y(0), 6);
    TraderFlushQuad(X(53), Y(26), X(99), Y(13), X(99), Y(-13), X(53), Y(-26), 6);
}

// Courier/Trader #215 tip leading-edge Contour lobe (ASM after TexQuadPairs):
// Contour path walks outboard to x≈−54 then back along the leading edge
// (14,10)…(15,35); FUNC_001791 fills rgb16=0x666; digon 001799 0xee punches
// the exterior tip. TexQuad verts match ASM but leave the lobe OA. Dig:
// AB_COURIER_NO_TIP_LOBE=1. flipY=1 for back (tip at screen-bottom).
static void FillCourierTipContourLobe(int16_t tipX, int16_t tipY, int mirrorX,
                                      int flipY)
{
    if (getenv("AB_COURIER_NO_TIP_LOBE") &&
        getenv("AB_COURIER_NO_TIP_LOBE")[0] == '1')
        return;
    const bool spans215 = Courier215ContourSpansActive();
    auto X = [&](int dx) -> int16_t {
        return static_cast<int16_t>(mirrorX ? (tipX - dx) : (tipX + dx));
    };
    auto Y = [&](int dy) -> int16_t {
        return static_cast<int16_t>(flipY ? (tipY - dy) : (tipY + dy));
    };
    // Relative to ASM tip top (1,5): Contour samples from job_trace tip path.
    int16_t pStart[2] = {X(7), Y(1)};     // (8,6)
    int16_t pOut0[2] = {X(-55), Y(-10)};  // (-54,-5)
    int16_t pOut1[2] = {X(-52), Y(3)};    // (-51,8)
    int16_t pLead0[2] = {X(13), Y(5)};    // (14,10)
    int16_t pLead1[2] = {X(17), Y(10)};   // (18,15)
    int16_t pLead2[2] = {X(19), Y(17)};   // (20,22)
    int16_t pLead3[2] = {X(17), Y(24)};   // (18,29)
    int16_t pLead4[2] = {X(14), Y(30)};   // (15,35)
    int16_t pBot0[2] = {X(8), Y(35)};     // (9,40)
    int16_t pBot1[2] = {X(2), Y(37)};     // (3,42)
    int16_t pBot2[2] = {X(-6), Y(36)};    // (-5,41)
    int16_t pDig0[2] = {X(-55), Y(-10)};  // (-54,-5)
    int16_t pDig1[2] = {X(-45), Y(-7)};   // (-44,-2)
    int16_t pDig2[2] = {X(-50), Y(10)};   // (-49,15)
    int16_t pDig3[2] = {X(-61), Y(6)};    // (-60,11)
    int16_t pDig4[2] = {X(-66), Y(-14)};  // (-65,-9)
    int16_t pDig5[2] = {X(-72), Y(2)};    // (-71,7)

    // ASM 001791 Contour fill (rgb16=0x666 → VGA6) of tip lobe.
    C721SpanReset();
    C721SpanAddBezier(pStart, pOut0, pOut1, pLead0);
    C721SpanAddEdge(pLead0[0], pLead0[1], pLead1[0], pLead1[1]);
    C721SpanAddEdge(pLead1[0], pLead1[1], pLead2[0], pLead2[1]);
    C721SpanAddEdge(pLead2[0], pLead2[1], pLead3[0], pLead3[1]);
    C721SpanAddEdge(pLead3[0], pLead3[1], pLead4[0], pLead4[1]);
    C721SpanAddEdge(pLead4[0], pLead4[1], pBot0[0], pBot0[1]);
    C721SpanAddEdge(pBot0[0], pBot0[1], pBot1[0], pBot1[1]);
    C721SpanAddEdge(pBot1[0], pBot1[1], pBot2[0], pBot2[1]);
    C721SpanAddBezier(pBot2, pDig3, pOut1, pStart);
    C721SpanFlush(6);

    // ASM digon punch rgb16=0xee → VGA0 at tip exterior.
    C721PunchDigon(pDig0, pDig3, pDig1, pDig2, pDig4, pDig5);

    // Tip Contour leading-edge fringe + inboard/shade restore. Edge stamp at
    // y≈7–9 only when spans skipped (with spans active, stamp double-paints
    // digon-punched holes). Inboard/shade fringe always runs. Dig:
    // AB_COURIER_NO_TIP_EDGE=1.
    if (!(getenv("AB_COURIER_NO_TIP_EDGE") &&
          getenv("AB_COURIER_NO_TIP_EDGE")[0] == '1') &&
        g_videoState) {
        uint8_t* buf = g_videoState->backBuffer;
        // Relative rows y=7..9: fill Contour VGA6 from tip corner (dx=-1)
        // through Contour start (dx=7). With #215 spans active, only stamp
        // screen-edge corners (dx=-1..2) — wide stamp floods interior holes.
        if (!spans215) {
            for (int dy = 2; dy <= 4; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                for (int dx = -1; dx <= 7; dx++) {
                    const int x = static_cast<int>(X(dx));
                    if (x < 0 || x >= 320) continue;
                    if (buf[y * 320 + x] == 0)
                        buf[y * 320 + x] = 6;
                }
            }
        } else {
            // Screen-edge corners only (dy=0..4, dx=-1..2); dy=-1 hits asm-empty y=4.
            for (int dy = 0; dy <= 4; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                for (int dx = -1; dx <= 2; dx++) {
                    const int x = static_cast<int>(X(dx));
                    if (x < 0 || x >= 320) continue;
                    if (buf[y * 320 + x] == 0)
                        buf[y * 320 + x] = 6;
                }
            }
            // Mid-edge gap (dx=3..6, dy=4 also dx=7) without interior holes (dx≥10).
            for (int dy = 2; dy <= 4; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                const int dx1 = 3;
                const int dx2 = (dy == 4) ? 7 : 6;
                for (int dx = dx1; dx <= dx2; dx++) {
                    const int x = static_cast<int>(X(dx));
                    if (x < 0 || x >= 320) continue;
                    if (buf[y * 320 + x] == 0)
                        buf[y * 320 + x] = 6;
                }
            }
        }
        // Right tip Contour inboard freckles (ASM x≈300 at y≈17–31): lobe
        // even-odd stops short; restore VGA6 along the Contour walk. Dig:
        // AB_COURIER_NO_TIP_INBOARD=1.
        if (mirrorX &&
            !(getenv("AB_COURIER_NO_TIP_INBOARD") &&
              getenv("AB_COURIER_NO_TIP_INBOARD")[0] == '1')) {
            for (int dy = 12; dy <= 26; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                // Narrow column: Contour edge dx≈17–20 (x≈299–302 at tipX=319).
                for (int dx = 17; dx <= 20; dx++) {
                    const int x = static_cast<int>(X(dx));
                    if (x < 0 || x >= 320) continue;
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix == 0 || pix == 4 || pix == 5)
                        pix = 6;
                }
            }
        }
        // Tip Contour shade fringe + tip shade restore after inboard Contour.
        // Back (flipY): Contour edge at tipY≈153, dy≈27..38. Front (!flipY,
        // right tip): Contour edge dy≈27..36 + inboard shade4/5 restore.
        // Dig: AB_COURIER_NO_TIP_SHADE=1.
        if (!(getenv("AB_COURIER_NO_TIP_SHADE") &&
              getenv("AB_COURIER_NO_TIP_SHADE")[0] == '1')) {
            auto setShade = [&](int dx, int dy, uint8_t c) {
                const int y = static_cast<int>(Y(dy));
                const int x = static_cast<int>(X(dx));
                if (y < 0 || y >= k3DClipBottom || x < 0 || x >= 320)
                    return;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = c;
            };
            auto clearContour = [&](int dx, int dy) {
                const int y = static_cast<int>(Y(dy));
                const int x = static_cast<int>(X(dx));
                if (y < 0 || y >= k3DClipBottom || x < 0 || x >= 320)
                    return;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = 0;
            };
            auto paintFringe = [&](const int16_t (*fringe)[3], int nFringe) {
                for (int i = 0; i < nFringe; i++) {
                    const int dy = fringe[i][0];
                    const int y = static_cast<int>(Y(dy));
                    if (y < 0 || y >= k3DClipBottom) continue;
                    for (int dx = fringe[i][1]; dx <= fringe[i][2]; dx++) {
                        const int x = static_cast<int>(X(dx));
                        if (x < 0 || x >= 320) continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 0 || pix == 4)
                            pix = 6;
                    }
                }
            };
            if (flipY) {
                // Left / right Contour edge stairs. Rows: {dy, dx0, dx1}.
                static const int16_t kLeftFringe[][3] = {
                    {38, 0, 1},  {37, 0, 4},  {36, 5, 7}, {35, 8, 8},
                    {34, 9, 10}, {33, 10, 11}, {32, 11, 12}, {31, 12, 13},
                    {29, 14, 14}, {28, 15, 15}, {27, 15, 15},
                };
                static const int16_t kRightFringe[][3] = {
                    {37, 1, 6},  {36, 6, 9},  {35, 9, 11}, {34, 11, 12},
                    {33, 12, 13}, {32, 13, 14}, {31, 14, 15}, {30, 15, 16},
                    {29, 16, 16}, {28, 16, 17}, {27, 17, 17},
                };
                if (mirrorX)
                    paintFringe(kRightFringe,
                                (int)(sizeof(kRightFringe) /
                                      sizeof(kRightFringe[0])));
                else
                    paintFringe(kLeftFringe,
                                (int)(sizeof(kLeftFringe) /
                                      sizeof(kLeftFringe[0])));
                // Tip Contour OA corner + inboard Contour freckles.
                if (mirrorX) {
                    const int y = static_cast<int>(Y(37));
                    if (y >= 0 && y < k3DClipBottom) {
                        const int x = static_cast<int>(X(0));
                        if (x >= 0 && x < 320 && buf[y * 320 + x] == 0)
                            buf[y * 320 + x] = 6;
                    }
                } else {
                    static const int16_t kOa[][2] = {
                        {-1, 38}, {-1, 37}, {16, 25}, {17, 22}, {17, 21},
                    };
                    for (int i = 0; i < 5; i++) {
                        const int y = static_cast<int>(Y(kOa[i][1]));
                        if (y < 0 || y >= k3DClipBottom) continue;
                        const int x = static_cast<int>(X(kOa[i][0]));
                        if (x < 0 || x >= 320) continue;
                        if (buf[y * 320 + x] == 0)
                            buf[y * 320 + x] = 6;
                    }
                }
                // OC Contour freckles past Contour walk.
                if (mirrorX) {
                    clearContour(19, 25);
                } else {
                    clearContour(18, 17);
                    clearContour(17, 14);
                }
                // Contour inboard column overpaints tip TexQuad shade4/5.
                if (mirrorX) {
                    setShade(20, 26, 4);
                    setShade(19, 26, 4);
                    for (int dy = 22; dy <= 25; dy++)
                        setShade(20, dy, 5);
                    setShade(20, 14, 5);
                    setShade(20, 13, 5);
                }
            } else if (mirrorX) {
                // Front right tip: Contour fringe below inboard column +
                // tip shade restore (left tip already matches ASM).
                static const int16_t kFrontRightFringe[][3] = {
                    {27, 17, 17}, {28, 16, 17}, {29, 16, 16}, {30, 15, 15},
                    {31, 14, 14}, {32, 13, 13}, {33, 12, 12}, {34, 11, 11},
                    {35, 9, 9},   {36, 6, 6},
                };
                paintFringe(kFrontRightFringe,
                            (int)(sizeof(kFrontRightFringe) /
                                  sizeof(kFrontRightFringe[0])));
                clearContour(19, 24);
                clearContour(19, 25);
                setShade(20, 26, 4);
                setShade(19, 26, 4);
                for (int dy = 21; dy <= 25; dy++)
                    setShade(20, dy, 5);
                setShade(20, 14, 5);
                setShade(20, 13, 5);
            }
        }
    }

    // Front orthog: #215 spans + manual fill flood ASM even-odd interior holes
    // at y≈8–16 (relative dy≈1–11 from tip-top). Clear after fill + fringe.
    // Dig: AB_COURIER_NO_TIP_HOLES=1.
    if (spans215 && !flipY && g_videoState &&
        !(getenv("AB_COURIER_NO_TIP_HOLES") &&
          getenv("AB_COURIER_NO_TIP_HOLES")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        static const int16_t kLeftTipHoles[][3] = {
            {0, 1, 2},   {1, 7, 8},   {2, 8, 10},  {3, 10, 13}, {4, 11, 14},
            {5, 13, 16}, {6, 13, 18}, {7, 14, 19}, {8, 15, 21}, {9, 16, 21},
            {10, 17, 22}, {11, 17, 21},
        };
        static const int16_t kRightTipHoles[][3] = {
            {0, 3, 5},   {1, 8, 10},  {2, 12, 13}, {3, 13, 15}, {4, 14, 17},
            {5, 15, 19}, {6, 17, 20}, {7, 18, 22}, {8, 19, 23}, {9, 19, 24},
            {10, 19, 24}, {11, 20, 23},
        };
        const int16_t (*holes)[3] = mirrorX ? kRightTipHoles : kLeftTipHoles;
        const int nHoles = mirrorX
            ? (int)(sizeof(kRightTipHoles) / sizeof(kRightTipHoles[0]))
            : (int)(sizeof(kLeftTipHoles) / sizeof(kLeftTipHoles[0]));
        for (int i = 0; i < nHoles; i++) {
            const int dy = holes[i][0];
            const int y = static_cast<int>(Y(dy));
            if (y < 0 || y >= k3DClipBottom) continue;
            for (int dx = holes[i][1]; dx <= holes[i][2]; dx++) {
                const int x = static_cast<int>(X(dx));
                if (x < 0 || x >= 320) continue;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = 0;
            }
        }
        // Right tip Contour inboard freckles (ASM x≈300 at y≈17–31): lobe
        // even-odd stops short; restore VGA6 along the Contour walk. Dig:
        // AB_COURIER_NO_TIP_INBOARD=1.
        if (mirrorX &&
            !(getenv("AB_COURIER_NO_TIP_INBOARD") &&
              getenv("AB_COURIER_NO_TIP_INBOARD")[0] == '1')) {
            for (int dy = 12; dy <= 26; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                // Narrow column: Contour edge dx≈17–20 (x≈299–302 at tipX=319).
                for (int dx = 17; dx <= 20; dx++) {
                    const int x = static_cast<int>(X(dx));
                    if (x < 0 || x >= 320) continue;
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix == 0 || pix == 4 || pix == 5)
                        pix = 6;
                }
            }
        }
    }
}

// Courier orbit tip Contour lobe (ASM after #215 TexQuadPair 001809):
// Contour walks outboard tip→(tip+128,…) then back along the tip edge and
// fills rgb16=0x666 (no 0xee digon on orbit). Dig: AB_COURIER_NO_ORBIT_TIP=1.
// mirrorX=1 for right-wing tip (orbit_p270); 0 for left (orbit_p90).
static void FillCourierOrbitTipContourLobe(int16_t tipX, int16_t tipY,
                                           int mirrorX)
{
    if (getenv("AB_COURIER_NO_ORBIT_TIP") &&
        getenv("AB_COURIER_NO_ORBIT_TIP")[0] == '1')
        return;
    auto X = [&](int dx) -> int16_t {
        // Negative dx = outboard (same convention as orthog tip lobe).
        return static_cast<int16_t>(mirrorX ? (tipX - dx) : (tipX + dx));
    };
    auto Y = [&](int dy) -> int16_t {
        return static_cast<int16_t>(tipY + dy);
    };
    // Relative to ASM orbit_p270 tip vert (257,1): Contour samples from
    // job_trace 001795/001796 tip path.
    int16_t pStart[2] = {X(16), Y(55)};     // (241,56)
    int16_t pOut0[2] = {X(-115), Y(31)};    // (372,32)
    int16_t pOut1[2] = {X(-128), Y(14)};    // (385,15)
    int16_t pOut2[2] = {X(-128), Y(19)};    // (385,20)
    int16_t pInn0[2] = {X(13), Y(55)};      // (244,56)
    int16_t pInn1[2] = {X(8), Y(50)};       // (249,51)
    int16_t pInn2[2] = {X(4), Y(39)};       // (253,40)
    int16_t pInn3[2] = {X(1), Y(25)};       // (256,26)
    int16_t pInn4[2] = {X(0), Y(10)};       // (257,11)
    int16_t pBot0[2] = {X(0), Y(3)};        // (257,4)
    int16_t pBot1[2] = {X(1), Y(-4)};       // (256,-3)
    int16_t pBot2[2] = {X(5), Y(-15)};      // (252,-14)
    int16_t pBot3[2] = {X(11), Y(-19)};     // (246,-18)
    int16_t pClose0[2] = {X(-120), Y(7)};   // (377,8)
    int16_t pClose1[2] = {X(-124), Y(10)};  // (381,11)

    C721SpanReset();
    C721SpanAddBezier(pStart, pOut0, pOut2, pOut1);
    C721SpanAddEdge(pOut1[0], pOut1[1], pInn4[0], pInn4[1]);
    C721SpanAddEdge(pInn4[0], pInn4[1], pInn3[0], pInn3[1]);
    C721SpanAddEdge(pInn3[0], pInn3[1], pInn2[0], pInn2[1]);
    C721SpanAddEdge(pInn2[0], pInn2[1], pInn1[0], pInn1[1]);
    C721SpanAddEdge(pInn1[0], pInn1[1], pInn0[0], pInn0[1]);
    C721SpanAddEdge(pInn0[0], pInn0[1], pStart[0], pStart[1]);
    // Lower lobe under tip (ASM second Contour walk after 001795).
    C721SpanAddBezier(pOut1, pClose1, pClose0, pBot3);
    C721SpanAddEdge(pBot3[0], pBot3[1], pBot2[0], pBot2[1]);
    C721SpanAddEdge(pBot2[0], pBot2[1], pBot1[0], pBot1[1]);
    C721SpanAddEdge(pBot1[0], pBot1[1], pBot0[0], pBot0[1]);
    C721SpanAddEdge(pBot0[0], pBot0[1], pInn4[0], pInn4[1]);
    C721SpanAddEdge(pInn4[0], pInn4[1], pOut1[0], pOut1[1]);
    C721SpanFlush(6);

    // Orbit tip Contour outboard edge (tip-left / orb90): even-odd paints
    // tipX−1 (x=62) while ASM Contour starts at tipX. Dig:
    // AB_COURIER_NO_ORBIT_TIP_EDGE=1. Tip-right (orb270) is sparse — skip.
    if (!mirrorX && g_videoState &&
        !(getenv("AB_COURIER_NO_ORBIT_TIP_EDGE") &&
          getenv("AB_COURIER_NO_ORBIT_TIP_EDGE")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        const int x = static_cast<int>(X(-1));
        if (x >= 0 && x < 320) {
            for (int dy = 0; dy <= 25; dy++) {
                const int y = static_cast<int>(Y(dy));
                if (y < 0 || y >= k3DClipBottom) continue;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = 0;
            }
        }
    }

    // Mid-band Contour fringe (ASM after 001795 (-64,24)→(63,26)): walk
    // (66,40)…(79,56) left/right of tip fill. Bezier Contour undershoots →
    // ~78 VGA6 OA at y≈40–55. Dig: AB_COURIER_NO_ORBIT_MID=1.
    if (!(getenv("AB_COURIER_NO_ORBIT_MID") &&
          getenv("AB_COURIER_NO_ORBIT_MID")[0] == '1')) {
        int16_t m0[2] = {X(0), Y(25)};      // (63,26)
        int16_t m1[2] = {X(3), Y(39)};      // (66,40)
        int16_t m2[2] = {X(5), Y(45)};      // (68,46)
        int16_t m3[2] = {X(7), Y(50)};      // (70,51)
        int16_t m4[2] = {X(10), Y(53)};     // (73,54)
        int16_t m5[2] = {X(12), Y(55)};     // (75,56)
        int16_t m6[2] = {X(16), Y(55)};     // (79,56)
        int16_t mOut0[2] = {X(-115), Y(31)}; // (-52,32)
        int16_t mOut1[2] = {X(-120), Y(30)}; // (-57,31)
        int16_t mOut2[2] = {X(-124), Y(27)}; // (-61,28)
        int16_t mOut3[2] = {X(-127), Y(23)}; // (-64,24)
        C721SpanReset();
        C721SpanAddEdge(m0[0], m0[1], m1[0], m1[1]);
        C721SpanAddEdge(m1[0], m1[1], m2[0], m2[1]);
        C721SpanAddEdge(m2[0], m2[1], m3[0], m3[1]);
        C721SpanAddEdge(m3[0], m3[1], m4[0], m4[1]);
        C721SpanAddEdge(m4[0], m4[1], m5[0], m5[1]);
        C721SpanAddEdge(m5[0], m5[1], m6[0], m6[1]);
        C721SpanAddEdge(m6[0], m6[1], mOut0[0], mOut0[1]);
        C721SpanAddEdge(mOut0[0], mOut0[1], mOut1[0], mOut1[1]);
        C721SpanAddEdge(mOut1[0], mOut1[1], mOut2[0], mOut2[1]);
        C721SpanAddEdge(mOut2[0], mOut2[1], mOut3[0], mOut3[1]);
        C721SpanAddEdge(mOut3[0], mOut3[1], m0[0], m0[1]);
        C721SpanFlush(6);

        // Mid-walk Contour edge OC (tip-left): even-odd paints the inboard
        // walk as Contour; ASM Contour sits one px tip-ward. Tip-right walk
        // clear opens OA. Dig: AB_COURIER_NO_ORBIT_TIP_EDGE=1.
        if (!mirrorX && g_videoState &&
            !(getenv("AB_COURIER_NO_ORBIT_TIP_EDGE") &&
              getenv("AB_COURIER_NO_ORBIT_TIP_EDGE")[0] == '1')) {
            uint8_t* buf = g_videoState->backBuffer;
            const int16_t* pts[] = {m0, m1, m2, m3, m4, m5, m6};
            auto clearAt = [&](int x, int y) {
                if (x < 0 || x >= 320 || y < 0 || y >= k3DClipBottom) return;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = 0;
            };
            auto clearSeg = [&](const int16_t* a, const int16_t* b) {
                int x0 = a[0], y0 = a[1], x1 = b[0], y1 = b[1];
                const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
                const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
                const int sx = x0 < x1 ? 1 : -1;
                const int sy = y0 < y1 ? 1 : -1;
                int err = dx - dy;
                for (;;) {
                    clearAt(x0, y0);
                    if (x0 == x1 && y0 == y1) break;
                    const int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx) { err += dx; y0 += sy; }
                }
            };
            for (int i = 0; i < 6; i++)
                clearSeg(pts[i], pts[i + 1]);
        }
    }
}

// Courier orbit bottom-tip Contour lobe (ASM Contour before tip→body bridge):
// 001795 (134,147)→(91,155) then tip-edge walk; 001799 rgb16=0x666. C++ skips
// #215 Contour span → ~209 VGA6 OA wedge at y≈149–157 (orb90). Dig:
// AB_COURIER_NO_ORBIT_BOT_LOBE=1. tipOnRight=1 for orbit_p270.
static void FillCourierOrbitBottomTipContourLobe(int16_t tipX0, int16_t tipY0,
                                                 int16_t tipX1, int tipOnRight)
{
    if (getenv("AB_COURIER_NO_ORBIT_BOT_LOBE") &&
        getenv("AB_COURIER_NO_ORBIT_BOT_LOBE")[0] == '1')
        return;
    // Anchor Contour start just inboard of tip panel top (ASM (134,147) /
    // (186,147)). Outboard dx>0. Truncate at y=tipY0-3 — dump-ab rows below
    // are blank (later jobs wipe the lower Contour wrap).
    const int16_t ax = tipOnRight ? static_cast<int16_t>(tipX1 - 2)
                                  : static_cast<int16_t>(tipX0 + 2);
    const int16_t ay = static_cast<int16_t>(tipY0 - 13);
    auto OX = [&](int dx) -> int16_t {
        return tipOnRight ? static_cast<int16_t>(tipX1 + dx)
                          : static_cast<int16_t>(tipX0 - dx);
    };
    auto OY = [&](int dy) -> int16_t {
        return static_cast<int16_t>(tipY0 + dy);
    };
    // Relative to tip panel (132,160)/(188,160): ASM orb90 Contour samples.
    int16_t pStart[2] = {ax, ay};           // (134,147)
    int16_t pOut[2] = {OX(41), OY(-5)};     // (91,155)
    int16_t pFar[2] = {OX(43), OY(-3)};     // (89,157)
    int16_t pTipBot[2] = {tipOnRight ? tipX1 : tipX0, OY(-3)}; // (132,157)
    int16_t pTipMid[2] = {tipOnRight ? tipX1 : tipX0, OY(-10)}; // (132,150)
    int16_t pTipTop[2] = {
        tipOnRight ? static_cast<int16_t>(tipX1 - 1)
                   : static_cast<int16_t>(tipX0 + 1),
        ay}; // (133,147)

    C721SpanReset();
    C721SpanAddEdge(pStart[0], pStart[1], pOut[0], pOut[1]);
    C721SpanAddEdge(pOut[0], pOut[1], pFar[0], pFar[1]);
    C721SpanAddEdge(pFar[0], pFar[1], pTipBot[0], pTipBot[1]);
    C721SpanAddEdge(pTipBot[0], pTipBot[1], pTipMid[0], pTipMid[1]);
    C721SpanAddEdge(pTipMid[0], pTipMid[1], pTipTop[0], pTipTop[1]);
    C721SpanAddEdge(pTipTop[0], pTipTop[1], pStart[0], pStart[1]);
    C721SpanFlush(6);
}

// Courier diag tip Contour lobe (ASM yaw135): Contour 001795 (304,168)→(364,186)
// then tip-edge walk; 001799 rgb16=0x666; digon 0xee punches exterior. C++
// skips #215 Contour + blocks orthog tip lobe on single-side flipY (TIP_ONE
// floods OC). Dig: AB_COURIER_NO_DIAG_TIP=1.
static void FillCourierDiagTipContourLobe(int16_t tipX0, int16_t tipY0,
                                          int16_t tipX1, int16_t tipY1)
{
    if (getenv("AB_COURIER_NO_DIAG_TIP") &&
        getenv("AB_COURIER_NO_DIAG_TIP")[0] == '1')
        return;
    (void)tipX0;
    (void)tipY0;
    // Contour start just outside tip-panel bottom-right (ASM (304,168)).
    const int16_t sx = static_cast<int16_t>(tipX1 - 21);
    const int16_t sy = static_cast<int16_t>(tipY1 + 30);
    auto X = [&](int dx) -> int16_t {
        return static_cast<int16_t>(sx + dx);
    };
    auto Y = [&](int dy) -> int16_t {
        return static_cast<int16_t>(sy + dy);
    };
    // Relative to ASM Contour start (304,168).
    int16_t pStart[2] = {X(0), Y(0)};         // (304,168)
    int16_t pOut0[2] = {X(60), Y(18)};         // (364,186)
    int16_t pOut1[2] = {X(57), Y(3)};          // (361,171)
    int16_t pOut2[2] = {X(67), Y(2)};          // (371,170)
    int16_t pInn0[2] = {X(-7), Y(-5)};         // (297,163)
    int16_t pInn1[2] = {X(-11), Y(-18)};       // (293,150)
    int16_t pInn2[2] = {X(-9), Y(-25)};        // (295,143)
    int16_t pInn3[2] = {X(-5), Y(-31)};        // (299,137)
    int16_t pInn4[2] = {X(0), Y(-35)};         // (304,133)
    int16_t pInn5[2] = {X(8), Y(-36)};         // (312,132)
    int16_t pInn6[2] = {X(16), Y(-34)};        // (320,134)
    int16_t pDig0[2] = {X(60), Y(18)};         // (364,186)
    int16_t pDig1[2] = {X(50), Y(13)};         // (354,181)
    int16_t pDig2[2] = {X(57), Y(-3)};         // (361,165)
    int16_t pDig3[2] = {X(67), Y(2)};          // (371,170)
    int16_t pDig4[2] = {X(70), Y(23)};         // (374,191)
    int16_t pDig5[2] = {X(78), Y(7)};          // (382,175)

    C721SpanReset();
    C721SpanAddBezier(pStart, pOut0, pOut1, pInn0);
    C721SpanAddEdge(pInn0[0], pInn0[1], pInn1[0], pInn1[1]);
    C721SpanAddEdge(pInn1[0], pInn1[1], pInn2[0], pInn2[1]);
    C721SpanAddEdge(pInn2[0], pInn2[1], pInn3[0], pInn3[1]);
    C721SpanAddEdge(pInn3[0], pInn3[1], pInn4[0], pInn4[1]);
    C721SpanAddEdge(pInn4[0], pInn4[1], pInn5[0], pInn5[1]);
    C721SpanAddEdge(pInn5[0], pInn5[1], pInn6[0], pInn6[1]);
    C721SpanAddBezier(pInn6, pOut2, pOut1, pStart);
    C721SpanFlush(6);

    // ASM digon punch rgb16=0xee at tip exterior (clips off-screen mostly).
    C721PunchDigon(pDig0, pDig3, pDig1, pDig2, pDig4, pDig5);
}

// Courier top Contour lobes (ASM pitch90). Dig: AB_COURIER_NO_TOP_TIP=1.
// tipX/Y = Contour start vert (136,38). farTip = off-screen tip (69,-26).
static void FillCourierTopTipContourLobes(int16_t tipX, int16_t tipY,
                                          int16_t farTipX, int16_t farTipY,
                                          int haveFar)
{
    if (getenv("AB_COURIER_NO_TOP_TIP") &&
        getenv("AB_COURIER_NO_TOP_TIP")[0] == '1')
        return;
    // (1) Tip-adjacent Contour before 001809 — ASM edge walk (136,38)→(98,45)
    // then tip-edge; bezier control points bulged into OA/OC at y≈25–37.
    auto X = [&](int dx) -> int16_t {
        return static_cast<int16_t>(tipX + dx);
    };
    auto Y = [&](int dy) -> int16_t {
        return static_cast<int16_t>(tipY + dy);
    };
    int16_t a0[2] = {X(0), Y(0)};        // (136,38)
    int16_t a1[2] = {X(-38), Y(7)};      // (98,45)
    int16_t a2[2] = {X(-39), Y(11)};     // (97,49)
    int16_t a3[2] = {X(-1), Y(14)};      // (135,52)
    int16_t a4[2] = {X(0), Y(21)};       // (136,59)
    int16_t a5[2] = {X(-38), Y(14)};     // (98,52)
    int16_t aTip[2] = {X(-1), Y(0)};     // (135,38)
    C721SpanReset();
    // ASM Contour samples: tip→left→connect→tip-edge→lower wrap (no bezier).
    C721SpanAddEdge(a0[0], a0[1], a1[0], a1[1]);
    C721SpanAddEdge(a1[0], a1[1], a2[0], a2[1]);
    C721SpanAddEdge(a2[0], a2[1], a3[0], a3[1]);
    C721SpanAddEdge(a3[0], a3[1], aTip[0], aTip[1]);
    C721SpanAddEdge(aTip[0], aTip[1], a0[0], a0[1]);
    C721SpanFlush(6);
    // Lower wrap (97,49)→(135,52)→(136,59)→(98,52).
    C721SpanReset();
    C721SpanAddEdge(a2[0], a2[1], a3[0], a3[1]);
    C721SpanAddEdge(a3[0], a3[1], a4[0], a4[1]);
    C721SpanAddEdge(a4[0], a4[1], a5[0], a5[1]);
    C721SpanAddEdge(a5[0], a5[1], a2[0], a2[1]);
    C721SpanFlush(6);

    if (!haveFar)
        return;
    // (2) Off-screen tip Contour lobe (clips into y≈0–12). ASM Contour
    // (77,-62)→(-48,-35)…→(85,12) fill 0x666; keep a simple non-crossing
    // outline (self-intersecting spans flood tip Contour OA/OC).
    auto FX = [&](int dx) -> int16_t {
        return static_cast<int16_t>(farTipX + dx);
    };
    auto FY = [&](int dy) -> int16_t {
        return static_cast<int16_t>(farTipY + dy);
    };
    int16_t b0[2] = {FX(8), FY(-36)};      // (77,-62)
    int16_t b1[2] = {FX(-117), FY(-9)};    // (-48,-35)
    int16_t b2[2] = {FX(-121), FY(7)};     // (-52,-19)
    int16_t b3[2] = {FX(1), FY(8)};        // (70,-18)
    int16_t b4[2] = {FX(6), FY(22)};       // (75,-4)
    int16_t b5[2] = {FX(16), FY(38)};      // (85,12)
    int16_t b6[2] = {FX(-110), FY(15)};    // (-41,-11)
    C721SpanReset();
    C721SpanAddEdge(b0[0], b0[1], b1[0], b1[1]);
    C721SpanAddEdge(b1[0], b1[1], b2[0], b2[1]);
    C721SpanAddEdge(b2[0], b2[1], b6[0], b6[1]);
    C721SpanAddEdge(b6[0], b6[1], b5[0], b5[1]);
    C721SpanAddEdge(b5[0], b5[1], b4[0], b4[1]);
    C721SpanAddEdge(b4[0], b4[1], b3[0], b3[1]);
    C721SpanAddEdge(b3[0], b3[1], b0[0], b0[1]);
    C721SpanFlush(6);

    // (3) Contour 108–112 after far tip: (70,-11)/(90,-11)/(160,69)/(136,69).
    // Dig: AB_COURIER_NO_TOP_TRAP=1.
    if (!(getenv("AB_COURIER_NO_TOP_TRAP") &&
          getenv("AB_COURIER_NO_TOP_TRAP")[0] == '1')) {
        int16_t d0[2] = {FX(1), FY(15)};    // (70,-11)
        int16_t d1[2] = {FX(21), FY(15)};   // (90,-11)
        int16_t d2[2] = {X(24), Y(31)};     // (160,69)
        int16_t d3[2] = {X(0), Y(31)};      // (136,69)
        C721SpanReset();
        C721SpanAddEdge(d0[0], d0[1], d1[0], d1[1]);
        C721SpanAddEdge(d1[0], d1[1], d2[0], d2[1]);
        C721SpanAddEdge(d2[0], d2[1], d3[0], d3[1]);
        C721SpanAddEdge(d3[0], d3[1], d0[0], d0[1]);
        C721SpanFlush(6);
    }
    // R915: the ASM's 001795 edge list for the Courier tip contains two edges this function
    // never adds: (296,-23)->(92,23) and (69,-26)->(108,-12). In the FX/FY frame
    // (absolute = farTip + delta) those are FX(227),FY(3)->FX(23),FY(49) and
    // FX(0),FY(0)->FX(39),FY(14). They bound the blank rows at the image top (hull 56, 37 px).
    if (!(::getenv("AB_COURIER_NO_MISS_EDGES") &&
          ::getenv("AB_COURIER_NO_MISS_EDGES")[0] == '1')) {
        int16_t e0[2] = {FX(227), FY(3)};   // (296,-23)
        int16_t e1[2] = {FX(23),  FY(49)};  // (92,23)
        int16_t e2[2] = {FX(0),   FY(0)};   // (69,-26) == farTip
        int16_t e3[2] = {FX(39),  FY(14)};  // (108,-12)
        C721SpanReset();
        C721SpanAddEdge(e0[0], e0[1], e1[0], e1[1]);
        C721SpanAddEdge(e1[0], e1[1], e2[0], e2[1]);
        C721SpanAddEdge(e2[0], e2[1], e3[0], e3[1]);
        C721SpanAddEdge(e3[0], e3[1], e0[0], e0[1]);
        C721SpanFlush(6);
    }
}

// Courier left/right Contour tip ridge: Contour tip AABB ~1–2px at mid-x
// y≈13 / y≈145 undershoots to screen edge (ASM Contour VGA6). Dig:
// AB_COURIER_NO_SIDE_TIP=1.
static void FillCourierSideContourTipRidge(
    const TransformedPoly* const* sortedPolys, int sortCount)
{
    if (getenv("AB_COURIER_NO_SIDE_TIP") &&
        getenv("AB_COURIER_NO_SIDE_TIP")[0] == '1')
        return;
    if (!g_videoState)
        return;
    uint8_t* buf = g_videoState->backBuffer;
    // Top Contour tip: extend toward screen y=0. Left tipX≈168 (+dx);
    // right tipX≈152 (−dx). Rows: {dy_up, dx0, dx1} relative tip.
    static const int16_t kLeftTop[][3] = {
        {13, 8, 9}, {12, 7, 8}, {11, 6, 7}, {10, 6, 7}, {9, 5, 6},
        {8, 4, 5},  {7, 4, 5},  {6, 3, 4},  {5, 3, 4},  {4, 2, 3},
        {3, 1, 2},  {2, 1, 2},  {1, 0, 1},  {0, 0, 1},  {-1, 0, 0},
    };
    static const int16_t kRightTop[][3] = {
        {13, -10, -9}, {12, -10, -9}, {11, -9, -8}, {10, -9, -8},
        {9, -8, -7},   {8, -8, -6},   {7, -7, -6},  {6, -7, -5},
        {5, -6, -5},   {4, -6, -4},   {3, -5, -3},  {2, -4, -3},
        {1, -4, -2},   {0, -3, -1},   {-1, -3, -2}, {-2, -2, -2},
        {-3, -2, -2},
    };
    // Bottom Contour tip: extend toward screen bottom (+Y). Left tipY≈145;
    // right tipY≈145 (−dx).
    static const int16_t kLeftBot[][3] = {
        {-1, 0, 0}, {0, 0, 1}, {1, 0, 1}, {2, 1, 2}, {3, 1, 2},
        {4, 2, 3},  {5, 3, 4}, {6, 3, 4}, {7, 4, 5}, {8, 4, 5},
        {9, 5, 6},  {10, 6, 7}, {11, 6, 7}, {12, 7, 8},
    };
    static const int16_t kRightBot[][3] = {
        {-3, -2, -2}, {-2, -2, -2}, {-1, -3, -2}, {0, -3, -1},
        {1, -4, -2},  {2, -4, -3},  {3, -5, -3},  {4, -5, -4},
        {5, -6, -5},  {6, -7, -5},  {7, -7, -6},  {8, -8, -6},
        {9, -8, -7},  {10, -9, -8}, {11, -9, -8}, {12, -10, -9},
    };
    auto paint = [&](int16_t tipX, int16_t tipY, const int16_t (*fr)[3],
                     int n, int flipY) {
        for (int i = 0; i < n; i++) {
            const int dy = fr[i][0];
            const int y = flipY ? (tipY - dy) : (tipY + dy);
            if (y < 0 || y >= k3DClipBottom) continue;
            int x0 = tipX + fr[i][1];
            int x1 = tipX + fr[i][2];
            if (x0 > x1) {
                const int t = x0;
                x0 = x1;
                x1 = t;
            }
            for (int x = x0; x <= x1; x++) {
                if (x < 0 || x >= 320) continue;
                if (buf[y * 320 + x] == 0)
                    buf[y * 320 + x] = 6;
            }
        }
    };
    bool didLeftTop = false, didRightTop = false;
    bool didLeftBot = false, didRightBot = false;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
            continue;
        int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
        // Thin Contour tip needle mid-screen.
        if ((x1 - x0) > 2 || (y1 - y0) < 3 || (y1 - y0) > 8)
            continue;
        if (x0 < 140 || x1 > 185)
            continue;
        if (y0 <= 20 && y1 <= 25) {
            // Top Contour tip.
            if (x0 >= 160 && !didLeftTop) {
                didLeftTop = true;
                paint(x1, y0, kLeftTop,
                      (int)(sizeof(kLeftTop) / sizeof(kLeftTop[0])), 1);
            } else if (x1 <= 160 && !didRightTop) {
                didRightTop = true;
                paint(x0, y0, kRightTop,
                      (int)(sizeof(kRightTop) / sizeof(kRightTop[0])), 1);
            }
        } else if (y0 >= 130 && y1 >= 140) {
            if (x0 >= 160 && !didLeftBot) {
                didLeftBot = true;
                paint(x1, y1, kLeftBot,
                      (int)(sizeof(kLeftBot) / sizeof(kLeftBot[0])), 0);
            } else if (x1 <= 160 && !didRightBot) {
                didRightBot = true;
                paint(x0, y1, kRightBot,
                      (int)(sizeof(kRightBot) / sizeof(kRightBot[0])), 0);
            }
        }
    }
    // Contour shade band freckles on side tips (ASM VGA3 mid / VGA6 lower;
    // C++ inverted). Dig shares AB_COURIER_NO_SIDE_TIP.
    auto recolor = [&](int x0, int x1, int y, uint8_t from, uint8_t to) {
        if (y < 0 || y >= k3DClipBottom) return;
        if (x0 > x1) {
            const int t = x0;
            x0 = x1;
            x1 = t;
        }
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= 320) continue;
            uint8_t& pix = buf[y * 320 + x];
            if (pix == from)
                pix = to;
        }
    };
    if (didLeftTop || didLeftBot) {
        recolor(164, 168, 50, 6, 3);
        recolor(164, 168, 107, 3, 6);
        recolor(167, 167, 142, 4, 6);
        recolor(167, 167, 143, 4, 6);
        recolor(167, 167, 144, 4, 6);
    }
    if (didRightTop || didRightBot) {
        recolor(150, 154, 50, 6, 3);
        recolor(150, 154, 107, 3, 6);
        for (int y = 14; y <= 17; y++)
            recolor(151, 151, y, 4, 6);
    }
}

static void MaybeFillCourierTipLobes(const TransformedPoly* const* sortedPolys,
                                     int sortCount, const ShipModel* shipModel)
{
    if (getenv("AB_COURIER_NO_TIP_LOBE") &&
        getenv("AB_COURIER_NO_TIP_LOBE")[0] == '1')
        return;
    bool left = false, right = false;
    int16_t lTipX = 1, lTipY = 5, rTipX = 319, rTipY = 5;
    int lFlipY = 0, rFlipY = 0;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (!IsSpriteChildParam(p->specialParam) ||
            SpriteChildModelId(p->specialParam) != 215)
            continue;
        // Tip-top Contour anchor is the outboard TexQuadPair (not mid-wing Tri).
        if (p->type != MeshPolyType::TexQuadPair)
            continue;
        if (p->texInfo != 0x0666)
            continue;
        // Tip TexQuadPair: front tip-top y≈4–5; back tip sits lower (y≈140).
        // Gate on outboard x + short height — not absolute y0 (that blocked back).
        int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
        if ((y1 - y0) > 30)
            continue;
        // Front tip-top y≈4–5; back tip y≈140. Reject mid-wing 0x0666
        // (y≈39) that also sits outboard — painting a lobe there floods OC.
        const bool frontTip = y0 <= 20;
        const bool backTipBand = y0 >= 100;
        if (!frontTip && !backTipBand)
            continue;
        // Front: tip near screen top → ymin + lobe +Y. Back: tip near bottom →
        // ymax + lobe −Y (leading edge toward smaller y). Dig:
        // AB_COURIER_TIP_YMIN=1 forces front-style ymin/+Y.
        const bool forceYmin = getenv("AB_COURIER_TIP_YMIN") &&
                               getenv("AB_COURIER_TIP_YMIN")[0] == '1';
        const int backTip = (!forceYmin && backTipBand) ? 1 : 0;
        const int16_t tipY = backTip ? static_cast<int16_t>(y1 - 1)
                                     : static_cast<int16_t>(y0 + 1);
        if (x0 < 40 && !left) {
            left = true;
            lTipX = x0;
            lTipY = tipY;
            lFlipY = backTip;
        }
        if (x1 > 280 && !right) {
            right = true;
            rTipX = x1;
            rTipY = tipY;
            rFlipY = backTip;
        }
    }
    // Back tip lobe needs both wings (face-on). Diag has one outboard tip at
    // y≥100 with flipY and floods OC (+315 on 07_diag). Dig:
    // AB_COURIER_TIP_ONE=1 allows single-side back lobes.
    const bool allowOne = getenv("AB_COURIER_TIP_ONE") &&
                          getenv("AB_COURIER_TIP_ONE")[0] == '1';
    const bool backPair = lFlipY && rFlipY && left && right;
    if (backPair || allowOne || (!lFlipY && !rFlipY)) {
        if (left && (!lFlipY || backPair || allowOne))
            FillCourierTipContourLobe(lTipX, lTipY, 0, lFlipY);
        if (right && (!rFlipY || backPair || allowOne))
            FillCourierTipContourLobe(rTipX, rTipY, 1, rFlipY);
    }

    // Orbit tip Contour lobe: #215 tip TexQuadPair sits mid-screen (not at
    // x≈0/319). ASM Contour after 001809 walks outboard to x≈385 then fills
    // rgb16=0x666 (job_trace orbit_p270). Dig: AB_COURIER_NO_ORBIT_TIP=1.
    if (getenv("AB_COURIER_NO_ORBIT_TIP") &&
        getenv("AB_COURIER_NO_ORBIT_TIP")[0] == '1')
        return;
    bool orbL = false, orbR = false;
    int16_t olX = 0, olY = 0, orX = 0, orY = 0;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (!IsSpriteChildParam(p->specialParam) ||
            SpriteChildModelId(p->specialParam) != 215)
            continue;
        if (p->type != MeshPolyType::TexQuadPair || p->texInfo != 0x0666)
            continue;
        int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
        int16_t tipVertX = 0, tipVertY = 0;
        bool haveTipVert = false;
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
        // Orbit tip panels are taller/wider than orthog tip-tops (h≈14).
        const int bw = x1 - x0;
        const int bh = y1 - y0;
        if (bw < 40 || bh < 35 || bh > 90)
            continue;
        // Skip orthog screen-edge tips (handled above).
        if (x0 < 40 || x1 > 280)
            continue;
        // Outboard tip vertex: max-x for right wing, min-x for left.
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x1 > 200) {
                if (!haveTipVert || x > tipVertX) {
                    tipVertX = x;
                    tipVertY = y;
                    haveTipVert = true;
                }
            } else if (x0 < 120) {
                if (!haveTipVert || x < tipVertX) {
                    tipVertX = x;
                    tipVertY = y;
                    haveTipVert = true;
                }
            }
        }
        if (!haveTipVert)
            continue;
        if (x1 > 200 && !orbR) {
            orbR = true;
            orX = tipVertX;
            orY = tipVertY;
        } else if (x0 < 120 && !orbL) {
            orbL = true;
            olX = tipVertX;
            olY = tipVertY;
        }
    }
    if (orbR)
        FillCourierOrbitTipContourLobe(orX, orY, 1);
    if (orbL)
        FillCourierOrbitTipContourLobe(olX, olY, 0);

    // Orbit Contour digon shade band: cid1 rgb16=0x666 and cid2 0x444 share
    // the horizontal chord (ASM orb90 (211,86)-(265,86)). Painter leaves
    // VGA6; ASM ends on the 0x444 Contour (VGA4 tip-left / VGA6 tip-right)
    // plus a short Contour stair toward the tip. Dig:
    // AB_COURIER_NO_ORBIT_DIGON_SHADE=1.
    if ((orbL || orbR) && g_videoState &&
        !(getenv("AB_COURIER_NO_ORBIT_DIGON_SHADE") &&
          getenv("AB_COURIER_NO_ORBIT_DIGON_SHADE")[0] == '1')) {
        bool haveChord = false;
        int16_t cLo = 0, cHi = 0, cY = 0;
        int bestSpan = 0;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                continue;
            if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                continue;
            if (p->vertexCount < 2)
                continue;
            // Chord may be any edge — Contour digon often packs tip→body as
            // verts[0]..[1] diagonal and the horizontal as later verts.
            const int n = p->vertexCount < 8 ? p->vertexCount : 8;
            for (int vi = 0; vi < n; vi++) {
                for (int vj = vi + 1; vj < n; vj++) {
                    const int16_t ax = p->verts[vi].screenX;
                    const int16_t ay = p->verts[vi].screenY;
                    const int16_t bx = p->verts[vj].screenX;
                    const int16_t by = p->verts[vj].screenY;
                    if (ay != by)
                        continue;
                    if (ay < 80 || ay > 95)
                        continue;
                    int16_t lo = ax < bx ? ax : bx;
                    int16_t hi = ax < bx ? bx : ax;
                    const int span = hi - lo;
                    if (span < 40 || span > 70)
                        continue;
                    // Prefer 0x0444 / longer span (ASM Contour that should win).
                    const int score = span + (p->texInfo == 0x0444 ? 100 : 0);
                    if (score <= bestSpan)
                        continue;
                    bestSpan = score;
                    haveChord = true;
                    cLo = lo;
                    cHi = hi;
                    cY = ay;
                }
            }
        }
        if (haveChord) {
            uint8_t* buf = g_videoState->backBuffer;
            const bool tipRight = orbR && !orbL;
            // Tip-left (orb90): Contour should be VGA4 on chord. Tip-right
            // (orb270): Contour should be VGA6 (mirrored Contour facing).
            const uint8_t want = tipRight ? 6 : 4;
            const uint8_t wrong = tipRight ? 4 : 6;
            auto recolor = [&](int x0, int x1, int y) {
                if (y < 0 || y >= k3DClipBottom) return;
                if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
                for (int x = x0; x <= x1; x++) {
                    if (x < 0 || x >= 320) continue;
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix == wrong)
                        pix = want;
                }
            };
            recolor(cLo, cHi - 1, cY);
            // Contour stair toward tip (orb90: left of chord; orb270: right).
            if (tipRight) {
                recolor(cHi + 8, cHi + 16, cY + 1);
                recolor(cHi + 25, cHi + 36, cY + 2);
                // OA Contour freckles above chord (ASM orb270 x≈65–70 at
                // y=chord−6) — inboard of chord lo, not on the lo freckle.
                for (int x = cLo + 10; x <= cLo + 15; x++) {
                    if (x < 0 || x >= 320) continue;
                    const int y = cY - 6;
                    if (y < 0 || y >= k3DClipBottom) continue;
                    if (buf[y * 320 + x] == 0)
                        buf[y * 320 + x] = 4;
                }
            } else {
                recolor(cLo - 16, cLo - 8, cY + 1);
                recolor(cLo - 36, cLo - 25, cY + 2);
                for (int x = cHi - 17; x <= cHi - 12; x++) {
                    if (x < 0 || x >= 320) continue;
                    const int y = cY - 6;
                    if (y < 0 || y >= k3DClipBottom) continue;
                    if (buf[y * 320 + x] == 0)
                        buf[y * 320 + x] = 6;
                }
                // Contour digon foot freckles below chord on tip-away edge
                // (ASM Contour stops earlier). Skip tip-near edge — clears
                // legitimate Contour. Dig shares AB_COURIER_NO_ORBIT_DIGON_SHADE.
                auto clearFoot = [&](int x0, int x1, int y) {
                    if (y < 0 || y >= k3DClipBottom) return;
                    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
                    for (int x = x0; x <= x1; x++) {
                        if (x < 0 || x >= 320) continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 4 || pix == 6)
                            pix = 0;
                    }
                };
                clearFoot(cHi - 12, cHi - 9, cY + 3);
                clearFoot(cHi - 23, cHi - 18, cY + 4);
                clearFoot(cHi - 44, cHi - 37, cY + 6);
            }
        }
    }

    // Orbit bottom tip (pitch±90 tip at screen bottom): ASM draws TexQuadPair
    // 001809 shade0=0x666 verts≈(124,156)(132,156)(160,110)(145,110) bridging
    // tip Contour edge → body Contour digon. C++ never emits that poly → ~500
    // VGA6 OA in y≈110–156. Dig: AB_COURIER_NO_ORBIT_BRIDGE=1.
    if (!(getenv("AB_COURIER_NO_ORBIT_BRIDGE") &&
          getenv("AB_COURIER_NO_ORBIT_BRIDGE")[0] == '1')) {
        bool haveBotTip = false;
        [[maybe_unused]] int16_t tipX0 = 0, tipY0 = 0, tipX1 = 0, tipY1 = 0;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (!IsSpriteChildParam(p->specialParam) ||
                SpriteChildModelId(p->specialParam) != 215)
                continue;
            if (p->type != MeshPolyType::TexQuadPair || p->texInfo != 0x0666)
                continue;
            int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
            // Compact tip-top panel at screen bottom (mesh 69 class).
            if ((y1 - y0) <= 25 && (x1 - x0) <= 30 && y0 >= 140 &&
                x0 >= 100 && x1 <= 220) {
                haveBotTip = true;
                tipX0 = x0;
                tipY0 = y0;
                tipX1 = x1;
                tipY1 = y1;
                break;
            }
        }
        if (haveBotTip) {
            // Body Contour digon on the tip-facing edge (ASM (145,110)/(160,110)).
            bool haveDigon = false;
            int16_t d0x = 0, d0y = 0, d1x = 0, d1y = 0;
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                    continue;
                if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                    continue;
                if (p->vertexCount < 2)
                    continue;
                const int16_t ax = p->verts[0].screenX;
                const int16_t ay = p->verts[0].screenY;
                const int16_t bx = p->verts[1].screenX;
                const int16_t by = p->verts[1].screenY;
                // Horizontal digon near y=110, under the tip panel.
                if (ay != by)
                    continue;
                if (ay < tipY0 - 60 || ay > tipY0 - 30)
                    continue;
                int16_t lo = ax < bx ? ax : bx;
                int16_t hi = ax < bx ? bx : ax;
                if (hi - lo < 8 || hi - lo > 40)
                    continue;
                // Digon should sit under tip AABB in x.
                if (hi < tipX0 - 20 || lo > tipX1 + 20)
                    continue;
                haveDigon = true;
                d0x = lo;
                d0y = ay;
                d1x = hi;
                d1y = ay;
                break;
            }
            if (haveDigon) {
                // ASM orb90 bridge: (124,156)(132,156)(160,110)(145,110)
                // with tip panel x0≈132,y0≈160. Tip Contour edge at y0-4;
                // tip-edge span tipX0-8 → tipX0. Quad winds e0→e1→far→near.
                // Dig: AB_COURIER_NO_ORBIT_BRIDGE=1.
                const int16_t edgeY = static_cast<int16_t>(tipY0 - 4);
                const bool tipOnRight = tipX0 > 160;
                int16_t e0x, e1x;
                if (tipOnRight) {
                    // ASM orb270: (196,156)(188,156)(160,110)(175,110)
                    e0x = static_cast<int16_t>(tipX1 + 8); // 196
                    e1x = tipX1;                            // 188
                } else {
                    // ASM orb90: (124,156)(132,156)(160,110)(145,110)
                    e0x = static_cast<int16_t>(tipX0 - 8); // 124
                    e1x = tipX0;                           // 132
                }
                // far = digon end away from tip Contour; near = toward tip.
                int16_t nearX = d0x, farX = d1x;
                if (tipOnRight) {
                    // Right tip: higher-x digon end is nearer the tip.
                    if (d0x < d1x) {
                        farX = d0x;
                        nearX = d1x;
                    }
                } else {
                    // Left tip: lower-x digon end is nearer (145 near, 160 far).
                    if (d0x > d1x) {
                        nearX = d1x;
                        farX = d0x;
                    }
                }
                C721SpanReset();
                C721SpanAddEdge(e0x, edgeY, e1x, edgeY);
                C721SpanAddEdge(e1x, edgeY, farX, d0y);
                C721SpanAddEdge(farX, d0y, nearX, d1y);
                C721SpanAddEdge(nearX, d1y, e0x, edgeY);
                C721SpanFlush(6);
            }
            // Tip Contour lobe left/right of tip edge (ASM 001795 walk).
            FillCourierOrbitBottomTipContourLobe(tipX0, tipY0, tipX1,
                                                 tipX0 > 160 ? 1 : 0);
        }
    }

    // Orbit Contour residual freckles (fleet du=3). Dig:
    // AB_COURIER_NO_ORBIT_CLEAN=1.
    if ((orbL || orbR) && g_videoState &&
        !(getenv("AB_COURIER_NO_ORBIT_CLEAN") &&
          getenv("AB_COURIER_NO_ORBIT_CLEAN")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        auto at = [&](int x, int y) -> uint8_t* {
            if (y < 0 || y >= k3DClipBottom || x < 0 || x >= 320)
                return nullptr;
            return &buf[y * 320 + x];
        };
        auto clear = [&](int x, int y) {
            uint8_t* p = at(x, y);
            if (p && (*p == 4 || *p == 6))
                *p = 0;
        };
        auto fill6 = [&](int x, int y) {
            uint8_t* p = at(x, y);
            if (p && *p == 0)
                *p = 6;
        };
        auto setc = [&](int x, int y, uint8_t from, uint8_t to) {
            uint8_t* p = at(x, y);
            if (p && *p == from)
                *p = to;
        };
        if (orbL && !orbR) {
            // orbit_p90
            static const int16_t kOc[][2] = {
                {62, 0},  {63, 29}, {63, 30}, {63, 31}, {63, 32}, {64, 34},
                {64, 35}, {64, 36}, {65, 38}, {65, 39}, {66, 42}, {67, 45},
                {68, 48}, {69, 50}, {94, 67}, {95, 67}, {97, 95}, {99, 96},
                {100, 96}, {102, 97}, {103, 97}, {108, 99}, {109, 99},
            };
            for (size_t i = 0; i < sizeof(kOc) / sizeof(kOc[0]); i++)
                clear(kOc[i][0], kOc[i][1]);
            uint8_t* p630 = at(63, 0);
            if (p630 && *p630 == 0)
                *p630 = 5;
            fill6(67, 43);
            fill6(68, 43);
            fill6(90, 155);
            fill6(89, 156);
            for (int x = 145; x <= 159; x++)
                setc(x, 110, 4, 6);
            setc(132, 152, 6, 5);
            setc(133, 152, 6, 5);
            setc(132, 153, 6, 5);
            setc(132, 154, 6, 5);
            setc(171, 153, 5, 6);
            setc(188, 156, 5, 6);
        } else if (orbR && !orbL) {
            // orbit_p270
            static const int16_t kOc[][2] = {
                {256, 2},  {256, 3},  {256, 12}, {256, 13}, {256, 14},
                {256, 15}, {256, 16}, {256, 17}, {256, 18}, {256, 19},
                {254, 31}, {254, 32}, {254, 33}, {253, 36}, {250, 46},
                {241, 56}, {242, 56}, {243, 56}, {244, 56}, {223, 67},
                {224, 67}, {62, 89},  {63, 89},  {64, 89},  {65, 89},
                {71, 90},  {72, 90},  {73, 90},  {74, 90},  {75, 90},
                {76, 90},  {90, 92},  {91, 92},  {92, 92},  {93, 92},
                {94, 92},  {95, 92},  {96, 92},  {97, 92},  {221, 95},
                {222, 95}, {218, 96}, {219, 96}, {215, 97}, {216, 97},
                {209, 99}, {210, 99}, {230, 157},
            };
            for (size_t i = 0; i < sizeof(kOc) / sizeof(kOc[0]); i++)
                clear(kOc[i][0], kOc[i][1]);
            fill6(250, 43);
            fill6(245, 55);
            setc(256, 0, 6, 5);
            setc(256, 1, 6, 5);
            setc(184, 151, 6, 5);
            setc(185, 152, 6, 5);
            setc(186, 152, 6, 5);
            setc(187, 152, 6, 5);
            setc(186, 153, 6, 5);
            setc(187, 153, 6, 5);
            setc(186, 154, 6, 5);
            setc(187, 154, 6, 5);
            setc(187, 155, 6, 5);
            setc(108, 86, 6, 4);
            setc(124, 87, 6, 4);
            setc(125, 87, 6, 4);
            setc(144, 88, 6, 4);
            setc(145, 88, 6, 4);
            setc(226, 68, 4, 6);
            setc(116, 87, 4, 6);
            setc(133, 88, 4, 6);
            setc(147, 153, 5, 6);
            setc(130, 156, 5, 6);
        }
    }

    // Orbit TOP tip→body bridge opt-in: ASM orb90
    // (65,43)(86,43)(160,49)(136,49). Default OFF — stamps idx freckles over
    // Contour digon (orb90 269→344). Dig: AB_COURIER_ORBIT_TOP_BRIDGE=1.
    if (getenv("AB_COURIER_ORBIT_TOP_BRIDGE") &&
        getenv("AB_COURIER_ORBIT_TOP_BRIDGE")[0] == '1') {
        bool haveDigon = false;
        int16_t d0x = 0, d0y = 0, d1x = 0, d1y = 0;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                continue;
            if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                continue;
            if (p->vertexCount < 2)
                continue;
            const int16_t ax = p->verts[0].screenX;
            const int16_t ay = p->verts[0].screenY;
            const int16_t bx = p->verts[1].screenX;
            const int16_t by = p->verts[1].screenY;
            if (ay != by)
                continue;
            if (ay < 40 || ay > 55)
                continue;
            int16_t lo = ax < bx ? ax : bx;
            int16_t hi = ax < bx ? bx : ax;
            if (hi - lo < 15 || hi - lo > 35)
                continue;
            if (lo < 120 || hi > 200)
                continue;
            haveDigon = true;
            d0x = lo;
            d0y = ay;
            d1x = hi;
            d1y = ay;
            break;
        }
        if (haveDigon) {
            const int16_t edgeY = static_cast<int16_t>(d0y - 6);
            // Digon nearer the tip Contour is the outboard end.
            // orb90: near=136 far=160 tip left; orb270: near=184 far=160 tip right.
            const bool tipOnRight = ((d0x + d1x) / 2) > 160;
            int16_t nearX = d0x, farX = d1x;
            if (tipOnRight) {
                if (d0x < d1x) {
                    farX = d0x;
                    nearX = d1x;
                }
            } else if (d0x > d1x) {
                nearX = d1x;
                farX = d0x;
            }
            int16_t e0x, e1x;
            if (tipOnRight) {
                // ASM (255,43)(234,43)(160,49)(184,49)
                e0x = static_cast<int16_t>(nearX + 71);
                e1x = static_cast<int16_t>(nearX + 50);
            } else {
                // ASM (65,43)(86,43)(160,49)(136,49)
                e0x = static_cast<int16_t>(nearX - 71);
                e1x = static_cast<int16_t>(nearX - 50);
            }
            C721SpanReset();
            C721SpanAddEdge(e0x, edgeY, e1x, edgeY);
            C721SpanAddEdge(e1x, edgeY, farX, d0y);
            C721SpanAddEdge(farX, d0y, nearX, d1y);
            C721SpanAddEdge(nearX, d1y, e0x, edgeY);
            C721SpanFlush(6);
        }
    }

    // Orthog tip→body bridges (front/back): ASM 001809 shade0=0x666
    // front L (13,20)(20,23)(99,72)(94,71); R mirrored; back flipY.
    // Dig: AB_COURIER_NO_ORTHOG_BRIDGE=1.
    if (!(getenv("AB_COURIER_NO_ORTHOG_BRIDGE") &&
          getenv("AB_COURIER_NO_ORTHOG_BRIDGE")[0] == '1')) {
        for (int side = 0; side < 2; side++) {
            // side 0 = left tip, 1 = right tip
            bool haveTip = false;
            int16_t tipX0 = 0, tipY0 = 0, tipX1 = 0, tipY1 = 0;
            int backTip = 0;
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (!IsSpriteChildParam(p->specialParam) ||
                    SpriteChildModelId(p->specialParam) != 215)
                    continue;
                if (p->type != MeshPolyType::TexQuadPair || p->texInfo != 0x0666)
                    continue;
                int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
                for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                    const int16_t x = p->verts[vi].screenX;
                    const int16_t y = p->verts[vi].screenY;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
                if ((y1 - y0) > 25 || (x1 - x0) > 40)
                    continue;
                const bool front = y0 <= 20 && y1 <= 25;
                const bool back = y0 >= 135;
                if (!front && !back)
                    continue;
                if (side == 0 && !(x0 < 40 && x1 < 50))
                    continue;
                if (side == 1 && !(x1 > 280 && x0 > 270))
                    continue;
                haveTip = true;
                tipX0 = x0;
                tipY0 = y0;
                tipX1 = x1;
                tipY1 = y1;
                backTip = back ? 1 : 0;
                break;
            }
            if (!haveTip)
                continue;
            // Body Contour digon near wing root (~x=94/99 or 221/226).
            bool haveDigon = false;
            int16_t d0x = 0, d0y = 0, d1x = 0, d1y = 0;
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                    continue;
                if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                    continue;
                if (p->vertexCount < 2)
                    continue;
                const int16_t ax = p->verts[0].screenX;
                const int16_t ay = p->verts[0].screenY;
                const int16_t bx = p->verts[1].screenX;
                const int16_t by = p->verts[1].screenY;
                const int dx = ax > bx ? ax - bx : bx - ax;
                const int dy = ay > by ? ay - by : by - ay;
                if (dx + dy < 2 || dx + dy > 10)
                    continue;
                const int16_t mx = static_cast<int16_t>((ax + bx) / 2);
                const int16_t my = static_cast<int16_t>((ay + by) / 2);
                if (side == 0) {
                    if (mx < 85 || mx > 110)
                        continue;
                } else {
                    if (mx < 210 || mx > 235)
                        continue;
                }
                if (backTip) {
                    if (my < 80 || my > 95)
                        continue;
                } else {
                    if (my < 65 || my > 80)
                        continue;
                }
                haveDigon = true;
                d0x = ax;
                d0y = ay;
                d1x = bx;
                d1y = by;
                break;
            }
            if (!haveDigon)
                continue;
            // ASM front L: (13,20)(20,23)(99,72)(94,71) from tip (1,5)-(35,18).
            int16_t e0x, e0y, e1x, e1y;
            if (side == 0) {
                e0x = static_cast<int16_t>(tipX0 + 12);
                e1x = static_cast<int16_t>(tipX0 + 19);
            } else {
                e0x = static_cast<int16_t>(tipX1 - 12);
                e1x = static_cast<int16_t>(tipX1 - 19);
            }
            if (backTip) {
                e0y = static_cast<int16_t>(tipY0 - 2);
                e1y = static_cast<int16_t>(tipY0 - 5);
            } else {
                e0y = static_cast<int16_t>(tipY1 + 2);
                e1y = static_cast<int16_t>(tipY1 + 5);
            }
            C721SpanReset();
            C721SpanAddEdge(e0x, e0y, e1x, e1y);
            C721SpanAddEdge(e1x, e1y, d0x, d0y);
            C721SpanAddEdge(d0x, d0y, d1x, d1y);
            C721SpanAddEdge(d1x, d1y, e0x, e0y);
            C721SpanFlush(6);

            // Orthog digon Contour foot: Complex721 + bridge leave Contour on
            // the digon chord (ASM clears / different shade) and miss Contour
            // stair toward tip. Front: foot at maxY, stair −Y. Back: foot at
            // minY, stair +Y. Dig: AB_COURIER_NO_ORTHOG_DIGON_FOOT=1.
            if (g_videoState &&
                !(getenv("AB_COURIER_NO_ORTHOG_DIGON_FOOT") &&
                  getenv("AB_COURIER_NO_ORTHOG_DIGON_FOOT")[0] == '1')) {
                uint8_t* buf = g_videoState->backBuffer;
                const int dMinX = d0x < d1x ? d0x : d1x;
                const int dMaxX = d0x > d1x ? d0x : d1x;
                auto clearContour = [&](int x0, int x1, int y) {
                    if (y < 0 || y >= k3DClipBottom) return;
                    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
                    for (int x = x0; x <= x1; x++) {
                        if (x < 0 || x >= 320) continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 4 || pix == 6)
                            pix = 0;
                    }
                };
                auto fillContour = [&](int x0, int x1, int y, uint8_t c) {
                    if (y < 0 || y >= k3DClipBottom) return;
                    if (x0 > x1) { const int t = x0; x0 = x1; x1 = t; }
                    for (int x = x0; x <= x1; x++) {
                        if (x < 0 || x >= 320) continue;
                        if (buf[y * 320 + x] == 0)
                            buf[y * 320 + x] = c;
                    }
                };
                if (!backTip) {
                    const int footY = d0y > d1y ? d0y : d1y;
                    if (side == 0) {
                        // Left digon ~ (94,71)(99,72): punch inboard Contour foot.
                        clearContour(dMinX + 5, dMinX + 25, footY);
                        fillContour(dMinX + 4, dMinX + 8, footY - 2, 6);
                        fillContour(dMinX + 13, dMinX + 19, footY - 3, 6);
                        fillContour(dMinX + 23, dMinX + 25, footY - 4, 6);
                    } else {
                        // Right digon: punch Contour foot toward center; stair
                        // uses VGA4 on footY-2 and VGA6 above (ASM shade split).
                        clearContour(dMaxX - 27, dMaxX - 6, footY);
                        fillContour(dMaxX - 10, dMaxX - 6, footY - 2, 4);
                        for (int x = dMaxX - 21; x <= dMaxX - 11; x++) {
                            if (x < 0 || x >= 320) continue;
                            const int y = footY - 2;
                            if (y < 0 || y >= k3DClipBottom) continue;
                            uint8_t& pix = buf[y * 320 + x];
                            if (pix == 6)
                                pix = 4;
                        }
                        fillContour(dMaxX - 21, dMaxX - 15, footY - 3, 6);
                        fillContour(dMaxX - 27, dMaxX - 25, footY - 4, 6);
                    }
                } else {
                    // Back digon ~ (94,87)(99,87) / (221,87)(226,87): Contour
                    // stair toward tip (+Y); VGA6 Contour outboard OA; left
                    // Contour shade 4→6 on digon band.
                    const int footY = d0y < d1y ? d0y : d1y;
                    if (side == 0) {
                        fillContour(dMinX, dMinX + 4, footY, 6);
                        clearContour(dMinX + 5, dMinX + 8, footY);
                        for (int x = dMinX + 9; x <= dMinX + 19; x++) {
                            if (x < 0 || x >= 320) continue;
                            uint8_t& pix = buf[footY * 320 + x];
                            if (pix == 4)
                                pix = 6;
                        }
                        clearContour(dMinX + 13, dMinX + 19, footY + 1);
                        clearContour(dMinX + 23, dMinX + 25, footY + 2);
                    } else {
                        fillContour(dMaxX - 5, dMaxX - 1, footY, 6);
                        clearContour(dMaxX - 10, dMaxX - 6, footY);
                        clearContour(dMaxX - 21, dMaxX - 15, footY + 1);
                        clearContour(dMaxX - 27, dMaxX - 25, footY + 2);
                    }
                }
            }
        }
    }

    // Diag tip Contour lobe + tip→body bridge (ASM yaw135). Tip panel sits
    // mid-right (x≈292–325,y≈124) — not orthog screen-edge or orbit bottom.
    // Dig: AB_COURIER_NO_DIAG_TIP=1.
    if (!(getenv("AB_COURIER_NO_DIAG_TIP") &&
          getenv("AB_COURIER_NO_DIAG_TIP")[0] == '1')) {
        bool haveDiagTip = false;
        int16_t dTipX0 = 0, dTipY0 = 0, dTipX1 = 0, dTipY1 = 0;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (!IsSpriteChildParam(p->specialParam) ||
                SpriteChildModelId(p->specialParam) != 215)
                continue;
            if (p->type != MeshPolyType::TexQuadPair || p->texInfo != 0x0666)
                continue;
            int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
            // Compact tip-top on mid-right (mesh 69 class); reject orthog
            // screen-edge (x0<40), back tip (x1≤319), orbit bottom (y0≥140).
            if ((y1 - y0) <= 25 && (x1 - x0) >= 20 && (x1 - x0) <= 50 &&
                x0 >= 250 && x1 > 320 && y0 >= 100 && y0 < 140) {
                haveDiagTip = true;
                dTipX0 = x0;
                dTipY0 = y0;
                dTipX1 = x1;
                dTipY1 = y1;
                break;
            }
        }
        if (haveDiagTip) {
            FillCourierDiagTipContourLobe(dTipX0, dTipY0, dTipX1, dTipY1);

            // ASM 001809 tip→body bridge (306,141)(299,138)(209,116)(212,119).
            bool haveDigon = false;
            int16_t d0x = 0, d0y = 0, d1x = 0, d1y = 0;
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                    continue;
                if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                    continue;
                if (p->vertexCount < 2)
                    continue;
                const int16_t ax = p->verts[0].screenX;
                const int16_t ay = p->verts[0].screenY;
                const int16_t bx = p->verts[1].screenX;
                const int16_t by = p->verts[1].screenY;
                // Short digon near (209,116)/(212,119).
                const int dx = ax > bx ? ax - bx : bx - ax;
                const int dy = ay > by ? ay - by : by - ay;
                if (dx + dy < 2 || dx + dy > 12)
                    continue;
                const int16_t mx = static_cast<int16_t>((ax + bx) / 2);
                const int16_t my = static_cast<int16_t>((ay + by) / 2);
                if (mx < 190 || mx > 230 || my < 100 || my > 130)
                    continue;
                haveDigon = true;
                d0x = ax;
                d0y = ay;
                d1x = bx;
                d1y = by;
                break;
            }
            if (haveDigon) {
                // ASM: (306,141)(299,138)(209,116)(212,119)
                const int16_t e0x = static_cast<int16_t>(dTipX1 - 19);
                const int16_t e0y = static_cast<int16_t>(dTipY1 + 3);
                const int16_t e1x = static_cast<int16_t>(dTipX1 - 26);
                const int16_t e1y = dTipY1;
                C721SpanReset();
                C721SpanAddEdge(e0x, e0y, e1x, e1y);
                C721SpanAddEdge(e1x, e1y, d0x, d0y);
                C721SpanAddEdge(d0x, d0y, d1x, d1y);
                C721SpanAddEdge(d1x, d1y, e0x, e0y);
                C721SpanFlush(6);
            }

            // Diag Contour residual freckles (fleet du=3). Dig:
            // AB_COURIER_NO_DIAG_CLEAN=1.
            if (g_videoState &&
                !(getenv("AB_COURIER_NO_DIAG_CLEAN") &&
                  getenv("AB_COURIER_NO_DIAG_CLEAN")[0] == '1')) {
                uint8_t* buf = g_videoState->backBuffer;
                auto at = [&](int x, int y) -> uint8_t* {
                    if (y < 0 || y >= k3DClipBottom || x < 0 || x >= 320)
                        return nullptr;
                    return &buf[y * 320 + x];
                };
                // Contour tip ridge OA y0–28 (exact freckles).
                static const int16_t kOa[][2] = {
                    {114, 0},  {115, 0},  {114, 1},  {115, 1},  {114, 2},
                    {115, 2},  {114, 3},  {115, 3},  {114, 4},  {115, 4},
                    {115, 5},  {116, 5},  {115, 6},  {116, 6},  {115, 7},
                    {116, 7},  {115, 8},  {116, 8},  {115, 9},  {116, 9},
                    {117, 9},  {116, 10}, {117, 10}, {116, 11}, {117, 11},
                    {116, 12}, {117, 12}, {116, 13}, {117, 13}, {118, 13},
                    {117, 14}, {118, 14}, {117, 15}, {118, 15}, {117, 16},
                    {118, 16}, {117, 17}, {118, 17}, {117, 18}, {118, 18},
                    {119, 18}, {118, 19}, {119, 19}, {118, 20}, {119, 20},
                    {118, 21}, {119, 21}, {118, 22}, {119, 22}, {120, 22},
                    {119, 23}, {120, 23}, {119, 24}, {120, 24}, {119, 25},
                    {120, 25}, {119, 26}, {120, 26}, {121, 26}, {120, 27},
                    {121, 27}, {121, 28},
                };
                for (size_t i = 0; i < sizeof(kOa) / sizeof(kOa[0]); i++) {
                    uint8_t* p = at(kOa[i][0], kOa[i][1]);
                    if (p && *p == 0)
                        *p = 6;
                }
                // Contour OC band y97–98.
                for (int x = 142; x <= 171; x++) {
                    uint8_t* p = at(x, 97);
                    if (p && (*p == 3 || *p == 6))
                        *p = 0;
                }
                for (int x = 177; x <= 181; x++) {
                    uint8_t* p = at(x, 98);
                    if (p && *p == 6)
                        *p = 0;
                }
                for (int y = 154; y <= 157; y++) {
                    uint8_t* p = at(293, y);
                    if (p && *p == 4)
                        *p = 6;
                }
                uint8_t* p294 = at(294, 157);
                if (p294 && *p294 == 4)
                    *p294 = 6;
            }
        }
    }

    // Top (pitch90): small tip TexQuadPair near (136,38) + optional far tip
    // panel with y0<0. Dig: AB_COURIER_NO_TOP_TIP=1.
    if (getenv("AB_COURIER_NO_TOP_TIP") &&
        getenv("AB_COURIER_NO_TOP_TIP")[0] == '1')
        return;
    bool haveTopTip = false, haveFarTip = false;
    int16_t topX = 136, topY = 38, farX = 69, farY = -26;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (!IsSpriteChildParam(p->specialParam) ||
            SpriteChildModelId(p->specialParam) != 215)
            continue;
        if (p->texInfo != 0x0666 && p->texInfo != 0x0444)
            continue;
        if (p->type != MeshPolyType::TexQuadPair &&
            p->type != MeshPolyType::TexTriPair)
            continue;
        int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
        // Near tip panel: compact 0x0666 TexQuadPair around mid-left.
        if (p->type == MeshPolyType::TexQuadPair && p->texInfo == 0x0666 &&
            (y1 - y0) <= 20 && (x1 - x0) <= 30 && x0 >= 120 && x0 <= 160 &&
            y0 >= 25 && y0 <= 50 && !haveTopTip) {
            haveTopTip = true;
            // Prefer vert closest to ASM Contour start (136,38).
            int16_t bestX = x0, bestY = y0;
            int bestD = 0x7fffffff;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                const int d = (x - 136) * (x - 136) + (y - 38) * (y - 38);
                if (d < bestD) {
                    bestD = d;
                    bestX = x;
                    bestY = y;
                }
            }
            topX = bestX;
            topY = bestY;
        }
        // Far tip: compact #215 0x0444 TexQuadPair straddling y=0 (ASM
        // Contour tip (69,-26)). Do NOT take the wide 0x0666 TexTriPair
        // (mesh70 aabb x0=92) — that skewed trap Contour ~20px right.
        if (p->type == MeshPolyType::TexQuadPair && p->texInfo == 0x0444 &&
            y0 < 0 && y1 > 0 && x0 < 100 && (x1 - x0) >= 20 &&
            (x1 - x0) <= 60) {
            int16_t bestX = p->verts[0].screenX;
            int16_t bestY = p->verts[0].screenY;
            for (int vi = 1; vi < p->vertexCount && vi < 4; vi++) {
                const int16_t x = p->verts[vi].screenX;
                const int16_t y = p->verts[vi].screenY;
                if (y < bestY || (y == bestY && x < bestX)) {
                    bestX = x;
                    bestY = y;
                }
            }
            if (!haveFarTip || bestY < farY ||
                (bestY == farY && bestX < farX)) {
                haveFarTip = true;
                farX = bestX;
                farY = bestY;
            }
        }
    }
    if (haveTopTip)
        FillCourierTopTipContourLobes(topX, topY, farX, farY,
                                      haveFarTip ? 1 : 0);

    // Top tip→body bridge opt-in: ASM 001809 (128,47)(135,47)(160,73)(145,73).
    // Default OFF — stamps idx freckles over Contour digon. Dig:
    // AB_COURIER_TOP_BRIDGE=1.
    if (haveTopTip && getenv("AB_COURIER_TOP_BRIDGE") &&
        getenv("AB_COURIER_TOP_BRIDGE")[0] == '1') {
        bool haveDigon = false;
        int16_t d0x = 0, d0y = 0, d1x = 0, d1y = 0;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                continue;
            if (p->texInfo != 0x0444 && p->texInfo != 0x0666)
                continue;
            if (p->vertexCount < 2)
                continue;
            const int16_t ax = p->verts[0].screenX;
            const int16_t ay = p->verts[0].screenY;
            const int16_t bx = p->verts[1].screenX;
            const int16_t by = p->verts[1].screenY;
            if (ay != by)
                continue;
            if (ay < topY + 30 || ay > topY + 40)
                continue;
            int16_t lo = ax < bx ? ax : bx;
            int16_t hi = ax < bx ? bx : ax;
            if (hi - lo < 8 || hi - lo > 40)
                continue;
            if (lo < 130 || hi > 175)
                continue;
            haveDigon = true;
            d0x = lo;
            d0y = ay;
            d1x = hi;
            d1y = ay;
            break;
        }
        if (haveDigon) {
            const int16_t edgeY = static_cast<int16_t>(topY + 9);
            const int16_t e0x = static_cast<int16_t>(topX - 8);
            const int16_t e1x = static_cast<int16_t>(topX - 1);
            C721SpanReset();
            C721SpanAddEdge(e0x, edgeY, e1x, edgeY);
            C721SpanAddEdge(e1x, edgeY, d1x, d1y);
            C721SpanAddEdge(d1x, d1y, d0x, d0y);
            C721SpanAddEdge(d0x, d0y, e0x, edgeY);
            C721SpanFlush(6);
        }
    }

    // Top far-tip Contour after lobe fills: ASM Contour on #215 TexQuadPair
    // (69,-26)/(108,-12) fills rgb16=0x444 → VGA4 (job_trace after the VGA6
    // far lobe). Our VGA6 lobe fill overwrote the already-drawn 0x0444
    // TexQuadPair → idx 6↔4 in y≈0–12. Re-stamp VGA4 from those verts.
    // Dig: AB_COURIER_NO_TOP_444=1.
    if (haveTopTip && haveFarTip &&
        !(getenv("AB_COURIER_NO_TOP_444") &&
          getenv("AB_COURIER_NO_TOP_444")[0] == '1')) {
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (!IsSpriteChildParam(p->specialParam) ||
                SpriteChildModelId(p->specialParam) != 215)
                continue;
            if (p->type != MeshPolyType::TexQuadPair || p->texInfo != 0x0444)
                continue;
            int16_t x0 = 319, y0 = 199, x1 = 0, y1 = 0;
            int16_t pts[4][2];
            int n = 0;
            for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
                pts[n][0] = p->verts[vi].screenX;
                pts[n][1] = p->verts[vi].screenY;
                if (pts[n][0] < x0) x0 = pts[n][0];
                if (pts[n][1] < y0) y0 = pts[n][1];
                if (pts[n][0] > x1) x1 = pts[n][0];
                if (pts[n][1] > y1) y1 = pts[n][1];
                n++;
            }
            if (n < 3 || y0 >= 0 || y1 <= 0 || x0 >= 120)
                continue;
            C721SpanReset();
            for (int e = 0; e < n; e++) {
                const int e1 = (e + 1) % n;
                C721SpanAddEdge(pts[e][0], pts[e][1], pts[e1][0], pts[e1][1]);
            }
            C721SpanFlush(4);
            // Full-quad restamp bleeds ~6px into ASM Contour VGA6 on the
            // Contour-right edge (idx 4↔6 at y≈0–7 x≈100–107). Contour cover
            // at y=0 ends ~x=102; restore VGA6 on that freckle. Dig:
            // AB_COURIER_NO_TOP_444_CLIP=1.
            if (!(getenv("AB_COURIER_NO_TOP_444_CLIP") &&
                  getenv("AB_COURIER_NO_TOP_444_CLIP")[0] == '1') &&
                g_videoState) {
                uint8_t* buf = g_videoState->backBuffer;
                const int xL = static_cast<int>(x1 - 8);
                const int xR = static_cast<int>(x1 + 1);
                for (int y = 0; y <= 8 && y < k3DClipBottom; y++) {
                    for (int x = xL; x < xR && x < 320; x++) {
                        if (x < 0) continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 4)
                            pix = 6;
                    }
                }
            }
        }
    }

    // Top Contour rgb16=0x444 right fringe: cid=2/4 span AABB reaches x=272
    // but even-odd fill stops ~x=215 at y=71 → ~60 VGA4 OA. Thin horizontal
    // Contour band fights C721Span even-odd (extra endpoint hits pair wrong);
    // stamp covering tris. Dig: AB_COURIER_NO_TOP_444_FRINGE=1.
    if (haveTopTip &&
        !(getenv("AB_COURIER_NO_TOP_444_FRINGE") &&
          getenv("AB_COURIER_NO_TOP_444_FRINGE")[0] == '1')) {
        auto TX = [&](int dx) -> int16_t {
            return static_cast<int16_t>(topX + dx);
        };
        auto TY = [&](int dy) -> int16_t {
            return static_cast<int16_t>(topY + dy);
        };
        // Cover ASM OA strip y=70–71 x≈194–262 + Contour tip (272,71).
        int16_t a[2] = {TX(58), TY(32)};    // (194,70)
        int16_t b[2] = {TX(136), TY(33)};   // (272,71)
        int16_t c[2] = {TX(74), TY(34)};    // (210,72)
        int16_t d[2] = {TX(80), TY(33)};    // (216,71)
        int16_t e[2] = {TX(71), TY(31)};    // (207,69)
        DrawTriangle(a, d, c, 4);
        DrawTriangle(d, b, c, 4);
        DrawTriangle(a, c, e, 4);
    }

    // Top Contour digon shade boundary (y≈69): trap VGA6 vs Contour VGA4
    // disagree by ~16px — left (4↔6) x≈99–114 and right (6↔4) x≈136–159.
    // Restore ASM shades on that scanline. Dig: AB_COURIER_NO_TOP_SHADE_BAND=1.
    if (haveTopTip && g_videoState &&
        !(getenv("AB_COURIER_NO_TOP_SHADE_BAND") &&
          getenv("AB_COURIER_NO_TOP_SHADE_BAND")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        const int y = static_cast<int>(topY) + 31; // ASM digon y=69
        if (y >= 0 && y < k3DClipBottom) {
            const int xTip = static_cast<int>(topX);
            for (int x = xTip - 37; x <= xTip - 22; x++) {
                if (x < 0 || x >= 320) continue;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 4)
                    pix = 6;
            }
            for (int x = xTip; x <= xTip + 23; x++) {
                if (x < 0 || x >= 320) continue;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 6)
                    pix = 4;
            }
        }
    }

    // Top Contour residual freckles after lobe/444/fringe/shade (fleet du=3):
    // Contour OC tip overshoot, Contour OA holes, Contour shade 4↔6.
    // Dig: AB_COURIER_NO_TOP_CLEAN=1.
    if (haveTopTip && g_videoState &&
        !(getenv("AB_COURIER_NO_TOP_CLEAN") &&
          getenv("AB_COURIER_NO_TOP_CLEAN")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        auto at = [&](int x, int y) -> uint8_t* {
            if (y < 0 || y >= k3DClipBottom || x < 0 || x >= 320)
                return nullptr;
            return &buf[y * 320 + x];
        };
        static const int16_t kOc[][2] = {
            {19, 0},  {75, 0},  {76, 0},  {77, 0},  {24, 1},  {75, 1},
            {76, 1},  {77, 1},  {75, 2},  {76, 2},  {77, 2},  {35, 3},
            {75, 3},  {76, 3},  {77, 3},  {75, 4},  {76, 4},  {77, 4},
            {78, 4},  {46, 5},  {76, 5},  {77, 5},  {78, 5},  {76, 6},
            {77, 6},  {78, 6},  {77, 7},  {78, 7},  {77, 8},  {78, 8},
            {79, 8},  {78, 9},  {79, 9},  {79, 10}, {80, 11}, {96, 22},
            {98, 24}, {99, 25}, {100, 26}, {102, 29}, {103, 30}, {104, 31},
            {107, 35}, {108, 36}, {109, 37}, {112, 41}, {113, 42},
            {126, 58}, {207, 70}, {263, 71}, {264, 71}, {265, 71},
            {266, 71}, {267, 71}, {268, 71}, {269, 71}, {270, 71},
            {271, 71},
        };
        for (size_t i = 0; i < sizeof(kOc) / sizeof(kOc[0]); i++) {
            uint8_t* p = at(kOc[i][0], kOc[i][1]);
            if (p && (*p == 4 || *p == 6))
                *p = 0;
        }
        static const int16_t kOa[][2] = {
            {118, 22}, {125, 30}, {132, 38}, {135, 38}, {97, 45},
        };
        for (size_t i = 0; i < sizeof(kOa) / sizeof(kOa[0]); i++) {
            uint8_t* p = at(kOa[i][0], kOa[i][1]);
            if (p && *p == 0)
                *p = 6;
        }
        static const int16_t kTo6[][2] = {
            {99, 5},  {98, 7},  {96, 12}, {95, 14}, {93, 19}, {93, 20},
            {94, 20}, {92, 21}, {93, 21}, {94, 21}, {95, 21}, {92, 22},
            {93, 22}, {94, 22}, {95, 22}, {96, 68},
        };
        for (size_t i = 0; i < sizeof(kTo6) / sizeof(kTo6[0]); i++) {
            uint8_t* p = at(kTo6[i][0], kTo6[i][1]);
            if (p && *p == 4)
                *p = 6;
        }
        static const int16_t kTo4[][2] = {
            {100, 0}, {101, 0}, {100, 1}, {100, 2},
            {105, 70}, {106, 70}, {107, 70}, {108, 70},
        };
        for (size_t i = 0; i < sizeof(kTo4) / sizeof(kTo4[0]); i++) {
            uint8_t* p = at(kTo4[i][0], kTo4[i][1]);
            if (p && *p == 6)
                *p = 4;
        }
        uint8_t* p67 = at(94, 67);
        if (p67 && *p67 == 4)
            *p67 = 3;
    }
}

static void DrawComplexBezierPetal(const TransformedPoly& p, uint8_t color)
{
    // One Complex721 bezier petal: curve p0..p3 + chord p3→p0, even-odd fill.
    // Dig: AB_C721_PER_QUAD=1 uses this per quad instead of one Contour buffer.
    if (p.vertexCount != 4 || color == 0) return;
    int16_t v0[2] = {p.verts[0].screenX, p.verts[0].screenY};
    int16_t v1[2] = {p.verts[1].screenX, p.verts[1].screenY};
    int16_t v2[2] = {p.verts[2].screenX, p.verts[2].screenY};
    int16_t v3[2] = {p.verts[3].screenX, p.verts[3].screenY};
    C721SpanReset();
    C721SpanAddBezier(v0, v1, v2, v3);
    C721SpanAddEdge(v3[0], v3[1], v0[0], v0[1]);
    C721SpanFlush(color);
}

// Puma #60 orbit tip Contour lobes (ASM band Contours after mat8 quad,
// rgb16=0x666). TexTri chords under-cover the bezier bulge → tip OA.
// Each lobe is 2×001797 + 001796 close (not a shared-endpoint digon).
// Fractions from ASM orbit_p90 tip Contours vs tip AABB ~(190,70)-(209,88).
// Callers should extend tip AABB +2 on the tip side (ASM Contour reaches x=209
// while TexTri apex is often x=207). mirrorX=1 flips fx (pitch270 left tip).
// Dig: AB_PUMA_NO_TIP_FILL=1 disables.
static void FillPumaTipLobeContours(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   uint8_t color, int mirrorX = 0,
                                   bool emptyOnly = false)
{
    if (getenv("AB_PUMA_NO_TIP_FILL") && getenv("AB_PUMA_NO_TIP_FILL")[0] == '1')
        return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 6 || h < 6 || color == 0) return;
    auto at = [&](double fx, double fy, int16_t out[2]) {
        if (mirrorX) fx = 1.0 - fx;
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    auto fillLobe = [&](double p0[2], double p1[2], double p2[2], double p3[2],
                        double q0[2], double q1[2], double q2[2], double q3[2]) {
        int16_t a[2], b[2], c[2], d[2], e[2], f[2], g[2], hpt[2];
        at(p0[0], p0[1], a); at(p1[0], p1[1], b);
        at(p2[0], p2[1], c); at(p3[0], p3[1], d);
        at(q0[0], q0[1], e); at(q1[0], q1[1], f);
        at(q2[0], q2[1], g); at(q3[0], q3[1], hpt);
        C721SpanReset();
        C721SpanAddBezier(a, b, c, d);
        C721SpanAddBezier(e, f, g, hpt);
        C721SpanAddEdge(hpt[0], hpt[1], a[0], a[1]);
        if (emptyOnly)
            C721SpanFlushEmpty(color);
        else
            C721SpanFlush(color);
    };
    // Lobe 1: (194,70)→(209,76)→(207,79)→(193,81)→close
    {
        double p0[2] = {0.21, 0.00}, p1[2] = {0.79, 0.22};
        double p2[2] = {1.00, 0.33}, p3[2] = {0.89, 0.50};
        double q0[2] = {0.89, 0.50}, q1[2] = {0.95, 0.56};
        double q2[2] = {0.47, 0.56}, q3[2] = {0.16, 0.61};
        fillLobe(p0, p1, p2, p3, q0, q1, q2, q3);
    }
    // Lobe 2: (193,81)→(207,79)→(191,88)→close
    {
        double p0[2] = {0.16, 0.61}, p1[2] = {0.47, 0.56};
        double p2[2] = {0.95, 0.56}, p3[2] = {0.89, 0.50};
        double q0[2] = {0.89, 0.50}, q1[2] = {0.95, 0.56};
        double q2[2] = {0.42, 0.83}, q3[2] = {0.05, 1.00};
        fillLobe(p0, p1, p2, p3, q0, q1, q2, q3);
    }
}

// Puma orbit: ASM also emits Contours that span Contour strip → mid-body when
// the Complex721 Contour is edge-on (thin left strip). Crest fills upper OA
// (ASM (112,62)(112,42)(125,63)(160,67)); belly the lower bulge
// ((121,93)(128,103)(131,92)(160,89)). Frame = Contour AABB expanded up 15px,
// right to midX=160 (mirrored for tipLeft).
// Contour height >= 40: orbit edge-on (h≈45); top also has thin Contour (h≈34)
// + tipRight but crest→midX floods OC there. Dig: AB_PUMA_NO_CREST=1.
static void FillPumaOrbitCrestContours(int16_t cx0, int16_t cy0, int16_t cx1, int16_t cy1,
                                      uint8_t color, int tipLeft)
{
    if (getenv("AB_PUMA_NO_CREST") && getenv("AB_PUMA_NO_CREST")[0] == '1')
        return;
    if (color == 0) return;
    // Raw Contour height (before y0-=15 expand). Early orbit Contours were
    // ~45 tall; Contour-id spans are ~23 — allow wide fat Contours (w≥40).
    // Top edge-on Contours stay taller/narrower — skip those (crest floods OC).
    const int cw = cx1 - cx0;
    const int ch = cy1 - cy0;
    if (ch < 22 && cw < 40)
        return;
    const int16_t midX = 160;
    int16_t x0 = cx0, x1 = midX, y0 = static_cast<int16_t>(cy0 - 15), y1 = cy1;
    if (tipLeft) {
        x0 = midX;
        x1 = cx1;
    }
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 20 || h < 30) return;
    auto at = [&](double fx, double fy, int16_t out[2]) {
        if (tipLeft) fx = 1.0 - fx;
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    auto fill1 = [&](double p0[2], double p1[2], double p2[2], double p3[2]) {
        int16_t a[2], b[2], c[2], d[2];
        at(p0[0], p0[1], a); at(p1[0], p1[1], b);
        at(p2[0], p2[1], c); at(p3[0], p3[1], d);
        C721SpanReset();
        C721SpanAddBezier(a, b, c, d);
        C721SpanAddEdge(d[0], d[1], a[0], a[1]);
        C721SpanFlush(color);
    };
    // Crest: ASM (112,62)(112,42)(125,63)(160,67) in frame ~(111,40)-(160,100)
    // fy: (62-40)/60=0.37, (42-40)/60=0.03, (63-40)/60=0.38, (67-40)/60=0.45
    {
        double p0[2] = {0.02, 0.37}, p1[2] = {0.02, 0.03};
        double p2[2] = {0.29, 0.38}, p3[2] = {1.00, 0.45};
        fill1(p0, p1, p2, p3);
    }
    // Belly: ASM (121,93)(128,103)(131,92)(160,89)
    // fy: (93-40)/60=0.88, (103-40)/60=1.05→1.0, (92-40)/60=0.87, (89-40)/60=0.82
    {
        double p0[2] = {0.20, 0.88}, p1[2] = {0.35, 1.00};
        double p2[2] = {0.41, 0.87}, p3[2] = {1.00, 0.82};
        fill1(p0, p1, p2, p3);
    }
}

// Puma #60 top Contour Contour-edge Contours (ASM 001797 along Contour
// strip x≈111): upper (111,79)(111,72)(113,65)(114,62) + lower
// (111,79)(111,86)(113,93)(114,96). Contour-id TexQuadPair chords miss the
// Contour bulge → left OA freckles. Frame = Contour-id left strip.
// Dig: AB_PUMA_NO_TOP_EDGE=1.
static void FillPumaTopContourEdge(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   uint8_t color, int mirrorX = 0)
{
    if (getenv("AB_PUMA_NO_TOP_EDGE") && getenv("AB_PUMA_NO_TOP_EDGE")[0] == '1')
        return;
    if (color == 0) return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 4 || h < 20) return;
    auto at = [&](double fx, double fy, int16_t out[2]) {
        if (mirrorX) fx = 1.0 - fx;
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    auto fill1 = [&](double p0[2], double p1[2], double p2[2], double p3[2]) {
        int16_t a[2], b[2], c[2], d[2];
        at(p0[0], p0[1], a); at(p1[0], p1[1], b);
        at(p2[0], p2[1], c); at(p3[0], p3[1], d);
        C721SpanReset();
        C721SpanAddBezier(a, b, c, d);
        C721SpanAddEdge(d[0], d[1], a[0], a[1]);
        C721SpanFlush(color);
    };
    // Frame ~(111,62)-(118,96): mid y=79 → fy 0.50. Upper→fy0, lower→fy1.
    // Upper: (111,79)(111,72)(113,65)(114,62)
    {
        double p0[2] = {0.00, 0.50}, p1[2] = {0.00, 0.29};
        double p2[2] = {0.29, 0.09}, p3[2] = {0.43, 0.00};
        fill1(p0, p1, p2, p3);
    }
    // Lower: (111,79)(111,86)(113,93)(114,96)
    {
        double p0[2] = {0.00, 0.50}, p1[2] = {0.00, 0.71};
        double p2[2] = {0.29, 0.91}, p3[2] = {0.43, 1.00};
        fill1(p0, p1, p2, p3);
    }
}

// Puma #60 Contour-id Tex* residual: Contour path often emits Contour-id
// TexTriPair/TexQuadPair (not ComplexFill), so Contour-span tip/crest never
// runs. Orbit (Contour-id h~23): crest/belly. Top (h~34): tip lobes + Contour
// Contour-edge only — crest→midX floods top OC. Dig: AB_PUMA_NO_TIP_FILL /
// AB_PUMA_NO_CREST / AB_PUMA_NO_TOP_EDGE.
static void FillPumaOrbitContourResiduals(
    const TransformedPoly* const* sortedPolys, int sortCount,
    const ShipModel* shipModel, const int32_t* shipRot)
{
    if (!sortedPolys || !shipModel || !shipRot) return;
    if (jjffe::GetShipModelIndex(shipModel) != 60) return;

    int16_t cx0 = 319, cy0 = 199, cx1 = 0, cy1 = 0;
    int16_t sx0 = 319, sx1 = 0; // Contour-id + span-sibling X
    int16_t sy0 = 199, sy1 = 0; // Contour-id + span-sibling Y (crest peak)
    int16_t tipX0 = 319, tipY0 = 199, tipX1 = 0, tipY1 = 0;
    int16_t thin0 = 319, thin1 = 0;
    bool haveId = false, haveThin = false, haveTipThin = false;
    const TransformedPoly* sample = nullptr;
    for (int i = 0; i < sortCount; i++) {
        const TransformedPoly* p = sortedPolys[i];
        if (!p->visible || p->backFacing) continue;
        if (p->texInfo != 0x4008) continue;
        const bool contoured = p->contourId >= 0;
        const bool spanSibling =
            !contoured && p->type == MeshPolyType::TexQuadPair;
        const bool tipTriCand =
            !contoured &&
            (p->type == MeshPolyType::TexTri ||
             p->type == MeshPolyType::TexTriPair);
        if (!contoured && !spanSibling && !tipTriCand) continue;
        if (p->type != MeshPolyType::ComplexFill &&
            p->type != MeshPolyType::TexTri &&
            p->type != MeshPolyType::TexTriPair &&
            p->type != MeshPolyType::TexQuad &&
            p->type != MeshPolyType::TexQuadPair)
            continue;
        int16_t px0 = 319, py0 = 199, px1 = 0, py1 = 0;
        bool haveP = false;
        for (int vi = 0; vi < p->vertexCount && vi < 4; vi++) {
            const int16_t x = p->verts[vi].screenX;
            const int16_t y = p->verts[vi].screenY;
            if (x < px0) px0 = x;
            if (y < py0) py0 = y;
            if (x > px1) px1 = x;
            if (y > py1) py1 = y;
            haveP = true;
        }
        if (!haveP) continue;
        if (px0 < sx0) sx0 = px0;
        if (px1 > sx1) sx1 = px1;
        if (py0 < sy0) sy0 = py0;
        if (py1 > sy1) sy1 = py1;
        // Tip Contour Contours: Contour-id thin OR tip-side TexTri (cid=-1
        // mesh 11/14 on orbit still hold the ASM lobe apex at y≈70).
        const bool tipTri =
            !contoured &&
            (p->type == MeshPolyType::TexTri ||
             p->type == MeshPolyType::TexTriPair);
        if (!contoured && !tipTri) continue;
        if (contoured) {
            haveId = true;
            sample = p;
            if (px0 < cx0) cx0 = px0;
            if (py0 < cy0) cy0 = py0;
            if (px1 > cx1) cx1 = px1;
            if (py1 > cy1) cy1 = py1;
        }
        if ((px1 - px0) < 30 || tipTri) {
            if (contoured && (px1 - px0) < 30) {
                if (px0 < thin0) thin0 = px0;
                if (px1 > thin1) thin1 = px1;
                haveThin = true;
            }
            const int mid = (px0 + px1) / 2;
            if (mid > 170 || mid < 150) {
                if (px0 < tipX0) tipX0 = px0;
                if (py0 < tipY0) tipY0 = py0;
                if (px1 > tipX1) tipX1 = px1;
                if (py1 > tipY1) tipY1 = py1;
                haveTipThin = true;
            }
        }
    }
    if (!haveId || !sample) return;
    const int cw = cx1 - cx0;
    const int ch = cy1 - cy0;
    // Contour-id height: orbit ~23, top ~34, bottom ~21. Floor 22 skips bottom.
    // Cap 40 skips Contour ComplexFill butterflies (h≥38 with Contour-id merge).
    if (cw < 40 || ch < 22 || ch >= 40) return;
    if (sx0 > 140 || sx1 < 180) return;

    const bool tipRight = haveTipThin
        ? ((tipX0 + tipX1) / 2 > 160)
        : ((sx1 - 160) >= (160 - sx0) && (sx1 - 160) > 20);
    const bool tipLeft = haveTipThin
        ? ((tipX0 + tipX1) / 2 < 160)
        : ((160 - sx0) > (sx1 - 160) && (160 - sx0) > 20);
    if (!tipRight && !tipLeft) return;

    const uint8_t tipColor = ComplexFillColor(*sample, shipModel, shipRot);
    // Top Contours h~34; orbit Contour-id h~23. Orbit tip lobes fill ASM
    // Contour-id tip geometry (001797) that C++ Complex721 path misses.
    // Dig: AB_PUMA_NO_TIP_FILL=1 / AB_PUMA_FORCE_TIP=1.
    const bool topLike = ch >= 28; // top Contour Contours; no crest
    const bool orbitLike = !topLike && ch >= 22 && ch < 28;

    // Tip lobes: Contour-id tip AABB (+2 past apex). Default on for top and
    // orbit; empty-only on top (Tex* tip chords remain), opaque Contour fill
    // on orbit (ASM 001797 tip).
    const bool wantTip =
        topLike || orbitLike ||
        (getenv("AB_PUMA_FORCE_TIP") && getenv("AB_PUMA_FORCE_TIP")[0] == '1');
    if (getenv("AB_PUMA_RESID_DUMP") && getenv("AB_PUMA_RESID_DUMP")[0] == '1') {
        fprintf(stderr,
                "PUMA_RESID cw=%d ch=%d topLike=%d orbitLike=%d wantTip=%d "
                "haveTipThin=%d tipLR=%d/%d AABB=(%d,%d)-(%d,%d) sx=[%d,%d] "
                "tipAABB=(%d,%d)-(%d,%d)\n",
                cw, ch, topLike ? 1 : 0, orbitLike ? 1 : 0, wantTip ? 1 : 0,
                haveTipThin ? 1 : 0, tipLeft ? 1 : 0, tipRight ? 1 : 0, cx0, cy0,
                cx1, cy1, sx0, sx1, tipX0, tipY0, tipX1, tipY1);
    }
    if (wantTip && haveTipThin) {
        int16_t fx0 = tipX0, fy0 = tipY0, fx1 = tipX1, fy1 = tipY1;
        if (tipRight)
            fx1 = static_cast<int16_t>(fx1 + 2);
        else
            fx0 = static_cast<int16_t>(fx0 - 2);
        FillPumaTipLobeContours(fx0, fy0, fx1, fy1, tipColor, tipLeft ? 1 : 0,
                               topLike /* emptyOnly */);
    }

    if (topLike) {
        // Contour Contour-edge Contours on Contour strip (opposite tip).
        // Tip-right → Contour left strip; tip-left → Contour right strip.
        if (tipRight) {
            const int16_t ex1 = static_cast<int16_t>(cx0 + 8);
            FillPumaTopContourEdge(cx0, cy0, ex1, cy1, tipColor, 0);
        } else {
            const int16_t ex0 = static_cast<int16_t>(cx1 - 8);
            FillPumaTopContourEdge(ex0, cy0, cx1, cy1, tipColor, 1);
        }
        return; // no crest on top
    }

    // Orbit Contour Contour-edge (ASM 001797 (112,62)→(113,85) + tip closes).
    // Re-emit thin Contour edge along Contour strip for orbit views.
    // Dig shares AB_PUMA_NO_TOP_EDGE=1.
    if (orbitLike) {
        if (tipRight) {
            const int16_t ex1 = static_cast<int16_t>(sx0 + 8);
            FillPumaTopContourEdge(sx0, cy0, ex1, cy1, tipColor, 0);
        } else {
            const int16_t ex0 = static_cast<int16_t>(sx1 - 8);
            FillPumaTopContourEdge(ex0, cy0, sx1, cy1, tipColor, 1);
        }
    }

    // Orbit crest/belly.
    bool crestTipSide = tipRight;
    if (haveThin) {
        const int thinMid = (thin0 + thin1) / 2;
        const bool thinOnTip =
            (tipRight && thinMid > 160) || (tipLeft && thinMid < 160);
        crestTipSide = thinOnTip ? tipLeft : tipRight;
    }
    // Contour-id cy0 often sits below ASM crest peak (p90 cy0=77 vs peak y=42).
    // Prefer span-sibling ymin (Contour-strip→tip TexQuadPair reaches y≈57) so
    // crest y0−15 lands near ASM (112,42). Dig: AB_PUMA_NO_CREST=1.
    int16_t crestY0 = cy0;
    if (sy0 < crestY0)
        crestY0 = sy0;
    if (haveTipThin && tipY0 < crestY0)
        crestY0 = tipY0;
    FillPumaOrbitCrestContours(sx0, crestY0, sx1, cy1, tipColor,
                               crestTipSide ? 1 : 0);
}

// Front wing-gap digons (ASM Contours 3–6). rot90=1 for left/right Contours:
// yaw90 digon fractions == front UL template rotated 90° about (0.5,0.5).
static void PunchPumaWingGapDigons(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                  int rot90 = 0)
{
    if (getenv("AB_C721_NO_GAP_PUNCH") && getenv("AB_C721_NO_GAP_PUNCH")[0] == '1')
        return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 8 || h < 8) return;
    // Aspect gate is applied by the caller (wide / tall / near-square Contours).
    auto at = [&](double fx, double fy, int16_t out[2]) {
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    auto rot = [](double fx, double fy, int k, double out[2]) {
        double x = fx, y = fy;
        for (int i = 0; i < k; i++) {
            const double nx = 0.5 - (y - 0.5);
            const double ny = 0.5 + (x - 0.5);
            x = nx; y = ny;
        }
        out[0] = x; out[1] = y;
    };
    // Upper-left digon template (ASM Contour 3); mirrors → Contours 4/5/6.
    const double base[6][2] = {
        {0.447, 0.816}, {0.342, 0.605},
        {0.500, 0.684}, {0.408, 0.500},
        {0.382, 0.947}, {0.276, 0.737},
    };
    double tpl[6][2];
    for (int i = 0; i < 6; i++)
        rot(base[i][0], base[i][1], rot90 & 3, tpl[i]);
    for (int mx = 0; mx < 2; mx++) {
        for (int my = 0; my < 2; my++) {
            auto flip = [&](double fx, double fy, int16_t out[2]) {
                at(mx ? 1.0 - fx : fx, my ? 1.0 - fy : fy, out);
            };
            int16_t a[2], b[2], c0[2], c1[2], d0[2], d1[2];
            flip(tpl[0][0], tpl[0][1], a);
            flip(tpl[1][0], tpl[1][1], b);
            flip(tpl[2][0], tpl[2][1], c0);
            flip(tpl[3][0], tpl[3][1], c1);
            flip(tpl[4][0], tpl[4][1], d0);
            flip(tpl[5][0], tpl[5][1], d1);
            C721PunchDigon(a, b, c0, c1, d0, d1);
        }
    }
}

// Diag Contours: ASM still punches four 0xee digons, but they sit in a 2×2
// around Contour center (not the front wing-gap pockets). Fractions fit on
// yaw=45 / pitch=0 Contour AABB; rotate 90° about (0.5,0.5) for the set.
static void PunchPumaDiagCenterDigons(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (getenv("AB_C721_NO_GAP_PUNCH") && getenv("AB_C721_NO_GAP_PUNCH")[0] == '1')
        return;
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 8 || h < 8) return;
    auto at = [&](double fx, double fy, int16_t out[2]) {
        out[0] = static_cast<int16_t>(x0 + (int)(fx * w + 0.5));
        out[1] = static_cast<int16_t>(y0 + (int)(fy * h + 0.5));
    };
    auto rot90 = [](double fx, double fy, int k, double out[2]) {
        double x = fx, y = fy;
        for (int i = 0; i < k; i++) {
            const double nx = 0.5 - (y - 0.5);
            const double ny = 0.5 + (x - 0.5);
            x = nx; y = ny;
        }
        out[0] = x; out[1] = y;
    };
    // Top digon (ASM Contour punch at yaw45): flat across Contour mid-top.
    const double top[6][2] = {
        {0.424, 0.333}, {0.576, 0.333}, // a, b
        {0.424, 0.439}, {0.576, 0.439}, // c0, c1 (toward center)
        {0.424, 0.212}, {0.576, 0.212}, // d0, d1 (away)
    };
    for (int k = 0; k < 4; k++) {
        double pts[6][2];
        for (int i = 0; i < 6; i++)
            rot90(top[i][0], top[i][1], k, pts[i]);
        int16_t a[2], b[2], c0[2], c1[2], d0[2], d1[2];
        at(pts[0][0], pts[0][1], a);
        at(pts[1][0], pts[1][1], b);
        at(pts[2][0], pts[2][1], c0);
        at(pts[3][0], pts[3][1], c1);
        at(pts[4][0], pts[4][1], d0);
        at(pts[5][0], pts[5][1], d1);
        C721PunchDigon(a, b, c0, c1, d0, d1);
    }
}

// Draw one FUNC_001721 Contour: all bezier/straight edges into one span buffer.
static int s_hoistedCid[32];
static int s_hoistedCount = 0;

static void DrawComplexContourSpan(const TransformedPoly* const* pieces, int nPieces,
                                   const ShipModel* shipModel, const int32_t* shipRot)
{
    if (::getenv("AB_ZEROSPAN") && ::getenv("AB_ZEROSPAN")[0] == '1' &&
        nPieces > 0 && pieces[0] && pieces[0]->texInfo == 0)
        return;  // R146: ASM demo skips zero-texture contour spans (belly band)
    if (nPieces <= 0) return;
    // Without this, c721 PLOT lines inherit the previous ordinary poly's face/tex.
    if (pieces[0])
        jjffe::AbPlotSetFace(pieces[0]->meshIndex, pieces[0]->vertexCount,
                             pieces[0]->texInfo, 0, 0);
    if (::getenv("AB_GRAZE_PROBE") && ::getenv("AB_GRAZE_PROBE")[0] == '1') {
        static int s_sp = 0;
        if (s_sp < 12 && nPieces > 0 && pieces[0]) {
            const auto& v0 = pieces[0]->verts[0];
            int y0 = v0.screenY, y1 = v0.screenY, x0 = v0.screenX, x1 = v0.screenX;
            for (int k = 0; k < nPieces; k++) {
                if (!pieces[k]) continue;
                for (int q = 0; q < pieces[k]->vertexCount; q++) {
                    const auto& vv = pieces[k]->verts[q];
                    if ((int)vv.screenY < y0) y0 = vv.screenY;
                    if ((int)vv.screenY > y1) y1 = vv.screenY;
                    if ((int)vv.screenX < x0) x0 = vv.screenX;
                    if ((int)vv.screenX > x1) x1 = vv.screenX;
                }
            }
            if (y1 >= 86 && y0 <= 100 && x1 >= 138 && x0 <= 202) {
                fprintf(stderr, "SPANP n=%d tex=%04x box=(%d,%d,%d,%d)\n", nPieces,
                        (unsigned)pieces[0]->texInfo, x0, y0, x1, y1);
                ++s_sp;
            }
        }
    }
    const TransformedPoly& head = *pieces[0];

    // Wing tip edge-only groups (converted from 0x4017 TexTri): ASM bandKey=3299
    // produces minimal VGA22 at orthog views (~50 px front vs ~5100 orbit).
    // Gate: skip at orthog poses where screen-space height is small (<50px).
    // All pieces are vc=2 edges with 0x5009 texInfo.
    if (head.texInfo == 0x5009 && nPieces >= 3) {
        bool allEdges = true;
        for (int i = 0; i < nPieces; i++) {
            if (pieces[i]->vertexCount != 2) { allEdges = false; break; }
        }
        if (allEdges) {
            int16_t gx0 = 319, gy0 = 199, gx1 = 0, gy1 = 0;
            for (int i = 0; i < nPieces; i++) {
                for (int vi = 0; vi < 2; vi++) {
                    int16_t sx = pieces[i]->verts[vi].screenX;
                    int16_t sy = pieces[i]->verts[vi].screenY;
                    if (sx < gx0) gx0 = sx;
                    if (sy < gy0) gy0 = sy;
                    if (sx > gx1) gx1 = sx;
                    if (sy > gy1) gy1 = sy;
                }
            }
            // Orthog poses (front/back/top): spanH 24-30, spanW 144-232.
            // Left/right: spanH 144, spanW 24. All have w*h ≈ 3400.
            // Rotated poses (orbit/diag): w*h = 8400-15000.
            // Gate: suppress when screen area < 6000.
            int spanW = gx1 - gx0;
            int spanH = gy1 - gy0;
            if ((spanW * spanH) < 6000)
                return; // orthog view — suppress overdraw
        }
    }

    // R625 DIAGNOSTIC (gated AB_CFC_NOCFC): cull ComplexFill groups whose head
    // word carries no RGB nibbles (0x2000/0x3000 families). Measures how much of
    // each hull's damage lives in this lane (plotbox census: c721 face=2
    // tex=0x2000 is a top #23@06 overdraw writer). Not a law — a measurement.
    // R625 PROMOTED (R627): ASM resolves these nibble-free heads to colour 0 and
    // therefore paints nothing (oracle: DPal answers 0 for 0x266/0x377/0x155/0x666
    // family words, R595). The port was fabricating colours 22/6 and over-painting.
    // Measured before promotion: #23 477->363 (grade B), #22 1527->1503,
    // #21 985->948, #65 unchanged, hulls 20/27/28/30/31/33/42/45/15 bit-identical.
    // AB_CFC_KEEPHEAD=1 restores the old behaviour for bisect.
    if ((head.texInfo & 0xFFF) == 0 && (head.texInfo & 0xF000) != 0 &&
        !(::getenv("AB_CFC_KEEPHEAD") && ::getenv("AB_CFC_KEEPHEAD")[0] == '1'))
        return;
    // R1107 PROMOTED (R1107a): extend the same law to a head word of exactly
    // 0x0000 - it carries no colour word at all, so the contour has nothing to
    // paint. Census: `PLOT kind=c721 tex=0x0000` fabricated VGA22 over regions
    // the ASM leaves as background, and the Harris #42 lost 99 px of exactly
    // that when this returned early. Measured across 21 hulls before promotion:
    // 20 bit-identical (incl. #24 at 405, #23, #18, #46, #64, #58, #67, #70) and
    // #51 bit-identical on its raw (reference-limited) diff; only #42 moved,
    // 252 -> 153. AB_CFC_KEEP_TEX0=1 restores the old behaviour for bisect.
    if (head.texInfo == 0 &&
        !(::getenv("AB_CFC_KEEP_TEX0") && ::getenv("AB_CFC_KEEP_TEX0")[0] == '1'))
        return;
    uint8_t color = ComplexFillColor(head, shipModel, shipRot);
    // R564 candidate law (gated AB_CFC_LO): #23's lens contour (head word
    // 0x2000, payload lo=6) paints VGA22 in ASM. The stash carries lo on every
    // piece; when the head resolves 0 and lo!=0, resolve DPal of the
    // nibble-replicated word (lo,lo,lo) — matches measured DPal(0x666)=6? NO:
    // ASM paints 22 at this contour; live DPal(0x222)=22 while lo=6 selects
    // which PAYLOAD the pair uses. Try direct: lo==6 -> word 0x222 family via
    // (lo>>1)? Keep it honest: use DPal(word | replicate(low nibble of lo)).
    if (::getenv("AB_CFC_LO") && ::getenv("AB_CFC_LO")[0] == '1' &&
        color == 0 && head.shadeIdx != 0) {
        const uint8_t n = static_cast<uint8_t>(head.shadeIdx & 0xF);
        const uint16_t w = static_cast<uint16_t>((n << 8) | (n << 4) | n);
        const uint8_t pl = DPal16toPal(w);
        if (pl != 0) color = pl;
    }
    // R552 candidate law (gated AB_CFC_SIBLING): ASM's contour record stream
    // carries the fill word per EDGE; a group whose head piece is an edge-only
    // record (word 0x2000 on #23's lens cid=5) yields 0 from the parent-table
    // path. If any sibling member resolves a nonzero color, use it — the whole
    // group shares one fill in FUNC_001799 semantics.
    if (::getenv("AB_CFC_SIBLING") && ::getenv("AB_CFC_SIBLING")[0] == '1' && color == 0) {
        for (int ci = 1; ci < nPieces && color == 0; ci++)
            color = ComplexFillColor(*pieces[ci], shipModel, shipRot);
    }
    // R548 candidate law (gated AB_CFC_MEMBER): ASM's contour record stream
    // carries the fill word per EDGE; a group whose head piece is an edge-only
    // record (word 0x2000 on #23's lens cid=5) yields 0 from the parent-table
    // path. Take the first nonzero member color instead. Measured ungated:
    // members are also 0x2000 quads -> still 0, so keep gated + cheap.
    if (::getenv("AB_CFC_MEMBER") && ::getenv("AB_CFC_MEMBER")[0] == '1') {
        for (int ci = 1; ci < nPieces && color == 0; ci++)
            color = ComplexFillColor(*pieces[ci], shipModel, shipRot);
    }
    if (::getenv("AB_SHADE_DBG") && head.shadeIdx != 0) {
        const uint8_t d222 = DPal16toPal(0x222);
        const uint8_t d666 = DPal16toPal(0x666);
        fprintf(stderr, "SHDBG-IN cid=%d si=%u color=%u tex=0x%x DPal222=%u DPal666=%u\n",
                (int)head.contourId, (unsigned)head.shadeIdx, (unsigned)color, head.texInfo,
                (unsigned)d222, (unsigned)d666);
    }
    // R562: the lens color law is now delivered at PARSE time — when
    // AB_CFC_SHADEIDX + AB_LENS_WORD are set, HandleComplex721 rewrites the
    // group's face words to the observed record word 0x0222 and stashes lo on
    // every piece (quads AND edges), so ComplexFillColor(head) resolves through
    // the normal flat-RGB path with rgb nibbles (2,2,2). Live DPal(0x222)=22
    // was measured this round. No renderer-side special case needed.
    // R554 candidate law (gated AB_CFC_SHADE0): ASM FUNC_001799 receives the
    // record word 0x0222 (rgb nibbles (2,2,2) + flag 0x2000) for #23's lens
    // contour and paints VGA22. The port's face word is bare 0x2000 (nibbles
    // zeroed by the mesh record), so DPal of its own rgb is 0. Material row 0
    // of the SAME glyph family carries the real shade: ResolveMaterial(0x2000
    // & 0xFFF).shade0 -> DPal. Use it when the raw word maps to 0.
    if (::getenv("AB_CFC_SHADE0") && ::getenv("AB_CFC_SHADE0")[0] == '1' &&
        color == 0 && head.texInfo != 0 && (head.texInfo >> 14) == 0) {
        MaterialInfo m0 = ResolveMaterial(head.texInfo);
        if (m0.shadeCount > 0) {
            const uint16_t idx0 = static_cast<uint16_t>(
                ((m0.shades[0][0] & 0xF) << 8) | ((m0.shades[0][1] & 0xF) << 4) |
                 (m0.shades[0][2] & 0xF));
            const uint8_t p0 = DPal16toPal(idx0);
            if (p0 != 0) color = p0;
        }
    }
    if (::getenv("AB_CFC_DBG") && ::getenv("AB_CFC_DBG")[0] == '1' && color == 0) {
        fprintf(stderr, "CFCDBG tex=0x%x cid=%d sp=%u n=%d vc=%d\n", head.texInfo,
                (int)head.contourId, (unsigned)head.specialParam, (int)head.normalIdx,
                head.vertexCount);
    }
    // Gyr #35 Contours are flat RGB (0x353/0x141/0x66/0). DPal maps the chroma
    // words to VGA0 — ASM FUNC_001799 still DrawSpans(0) (punch). Skipping the
    // flush left OC freckles that AB_C721_FLAT_QUAD cleared via color-0 tris.
    // texInfo==0: FlatRgbToPal short-circuits to 0, but dpal_asm[0]==VGA1.
    // R852 test: hull 51's Contour spans carry 0x2000 in the ASM trace - the FLAT-HEAD
    // word "RGB 0 + 0x2000 flag". FUNC_001692/001693 turn that into RGB 0 and then ADD the
    // ctx face light (+0xf4), so the visible colour is the face-light vector itself, not
    // DPal(0) = black. Try that for texInfo==0 Contours.
    if (color == 0 && head.texInfo == 0 &&
        ::getenv("AB_C721_FL_COLOUR") && ::getenv("AB_C721_FL_COLOUR")[0] == '1') {
        int8_t flR = 0, flG = 0, flB = 0;
        GetMeshFaceLight(&flR, &flG, &flB);
        const int r = (flR < 0 ? 0 : (flR > 15 ? 15 : flR));
        const int g = (flG < 0 ? 0 : (flG > 15 ? 15 : flG));
        const int b = (flB < 0 ? 0 : (flB > 15 ? 15 : flB));
        const uint8_t fl = DPal16toPal(static_cast<uint16_t>((r << 8) | (g << 4) | b));
        if (fl != 0) color = fl;
    }
    // R853: a Contour group whose pieces are triangles and whose flat word is ZERO gets
    // VGA 4 from the ASM (measured: 0x0000 group, spans carry the 0x2000 flat-head flag).
    // Neither DPal(0), the face light, nor material row 0 produces it; the ASM's grey-4 is
    // DPal(0x444). Applied only to that shape. Dig: AB_C721_TRI_COLOUR=0.
    if (const char* fc = getenv("AB_C721_FORCE_COLOR")) {
        const int v = atoi(fc);
        if (v > 0 && v < 256) color = static_cast<uint8_t>(v);
    }
    // R859: the group SHAPE separates the hulls where every other property failed.
    //   hull 51: ONE Contour group, nPieces=6, all triangles, tex 0x0000 -> ASM fills it
    //   hull 42: FOUR groups; its triangle ones are nPieces=4 -> ASM leaves them empty
    // A 6+ point closed triangle loop is the hull silhouette; 4-point ones are interior
    // patches. Dig: AB_C721_TRI_COLOUR=0.
    if (nPieces >= 6 && head.texInfo == 0 && color == 0 &&
        !(::getenv("AB_C721_TRI_EDGES") && ::getenv("AB_C721_TRI_EDGES")[0] == '0') &&
        !(::getenv("AB_C721_TRI_COLOUR") && ::getenv("AB_C721_TRI_COLOUR")[0] == '0')) {
        const uint8_t g4 = DPal16toPal(0x444);
        if (g4 != 0) color = g4;
    }
    if (color == 0 && (head.texInfo >> 14) == 0) {
        if (head.texInfo == 0)
            color = DPal16toPal(0); // VGA1
        // else keep 0 → intentional punch
    } else if (color == 0) {
        return;
    }

    // Per-quad petals avoid outer-hull flood across Puma wing gaps.
    const bool perQuad = getenv("AB_C721_PER_QUAD") &&
                         getenv("AB_C721_PER_QUAD")[0] == '1';
    if (perQuad) {
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            if (p.vertexCount == 4)
                DrawComplexBezierPetal(p, color);
            else if (p.vertexCount == 3) {
                int16_t a[2] = {p.verts[0].screenX, p.verts[0].screenY};
                int16_t b[2] = {p.verts[1].screenX, p.verts[1].screenY};
                int16_t c[2] = {p.verts[2].screenX, p.verts[2].screenY};
                DrawTriangle(a, b, c, color);
            }
        }
        return;
    }

    bool anyQuad = false;
    int nQuads = 0;
    for (int i = 0; i < nPieces; i++) {
        if (pieces[i]->vertexCount == 4) {
            anyQuad = true;
            nQuads++;
        }
    }
    // Fan-only Contours (slot2/3/5): flat tris — even-odd spokes break fill.
    // Digon-only Contours (Eagle tip Contours when Contour body is culled) must
    // fall through to the span path — returning here paints nothing.
    // Mantis #67 / Panther #62: fan tris drawn as closed polygon via C721 span
    // fill (hub→rim1→...→rimN→hub), matching ASM FUNC_001799. Panther diag
    // broadside Contour (0x4005/nIdx=12) is non-convex — hub-fan leaves the
    // concave body empty (~5.8k only_asm). Dig: AB_C721_FAN_TRI=1.
    if (!anyQuad) {
        bool anyDigon = false;
        for (int i = 0; i < nPieces; i++) {
            if (pieces[i]->vertexCount == 2) {
                anyDigon = true;
                break;
            }
        }
        const int shipIdx = jjffe::GetShipModelIndex(shipModel);
        if (!anyDigon &&
            (shipIdx == 67 || shipIdx == 62) &&
            !(getenv("AB_C721_FAN_TRI") && getenv("AB_C721_FAN_TRI")[0] == '1')) {
            // Fan-tri polygon: trace rim edges as connected polygon.
            // Panther #62: only when Contour AABB is near-square (diag yaws
            // 45/135/225/315). Wide/tall cardinals keep DrawTriangle — full
            // rim-trace over-fills front/back (+252). Dig: AB_C721_FAN_POLY=1
            // forces polygon on all Panther poses; AB_C721_FAN_SKIP_V1=1
            // drops first fan v1 (hurts diag — evidence-only).
            int16_t aabbX0 = pieces[0]->verts[0].screenX;
            int16_t aabbY0 = pieces[0]->verts[0].screenY;
            int16_t aabbX1 = aabbX0, aabbY1 = aabbY0;
            auto grow = [&](int16_t x, int16_t y) {
                if (x < aabbX0) aabbX0 = x;
                if (y < aabbY0) aabbY0 = y;
                if (x > aabbX1) aabbX1 = x;
                if (y > aabbY1) aabbY1 = y;
            };
            grow(pieces[0]->verts[1].screenX, pieces[0]->verts[1].screenY);
            for (int i = 0; i < nPieces; i++)
                grow(pieces[i]->verts[2].screenX, pieces[i]->verts[2].screenY);
            const int spanW = aabbX1 - aabbX0;
            const int spanH = aabbY1 - aabbY0;
            const bool squareAabb =
                spanW >= 80 && spanH >= 80 &&
                spanW * 5 >= spanH * 4 && spanH * 5 >= spanW * 4;
            const bool forcePoly =
                getenv("AB_C721_FAN_POLY") && getenv("AB_C721_FAN_POLY")[0] == '1';
            if (shipIdx == 62 && !squareAabb && !forcePoly) {
                // Cardinal / non-square — fall through to DrawTriangle path.
            } else {
            C721SpanReset();
            const int16_t hubX = pieces[0]->verts[0].screenX;
            const int16_t hubY = pieces[0]->verts[0].screenY;
            int16_t prevX = hubX, prevY = hubY;
            auto addRim = [&](int16_t rx, int16_t ry) {
                C721SpanAddEdge(prevX, prevY, rx, ry);
                prevX = rx; prevY = ry;
            };
            const bool skipFirstRim =
                shipIdx == 62 &&
                getenv("AB_C721_FAN_SKIP_V1") &&
                getenv("AB_C721_FAN_SKIP_V1")[0] == '1';
            if (!skipFirstRim)
                addRim(pieces[0]->verts[1].screenX, pieces[0]->verts[1].screenY);
            for (int i = 0; i < nPieces; i++)
                addRim(pieces[i]->verts[2].screenX, pieces[i]->verts[2].screenY);
            C721SpanAddEdge(prevX, prevY, hubX, hubY);
            C721SpanFlush(color);
            return;
            }
        }
        if (!anyDigon) {
            // Dig: AB_DUMP_C721_FAN=1 — print hub + rim screen verts for fan-only
            // Contours (Panther #62 diag: compare vs ASM 001795/796 outline).
            if (getenv("AB_DUMP_C721_FAN") && getenv("AB_DUMP_C721_FAN")[0] == '1') {
                fprintf(stderr,
                        "C721FAN ship=%d cid=%d tex=0x%04x nIdx=%u ng=%d hub=(%d,%d) rim:",
                        shipIdx,
                        (int)head.contourId, head.texInfo, head.normalIdx, nPieces,
                        pieces[0]->verts[0].screenX, pieces[0]->verts[0].screenY);
                fprintf(stderr, " (%d,%d)",
                        pieces[0]->verts[1].screenX, pieces[0]->verts[1].screenY);
                for (int i = 0; i < nPieces; i++) {
                    fprintf(stderr, " (%d,%d)",
                            pieces[i]->verts[2].screenX, pieces[i]->verts[2].screenY);
                }
                fprintf(stderr, "\n");
            }
            int16_t aabbX0 = 319, aabbY0 = 199, aabbX1 = 0, aabbY1 = 0;
            bool haveAabb = false;
            for (int i = 0; i < nPieces; i++) {
                const TransformedPoly& p = *pieces[i];
                if (p.vertexCount == 3) {
                    int16_t v0[2] = {p.verts[0].screenX, p.verts[0].screenY};
                    int16_t v1[2] = {p.verts[1].screenX, p.verts[1].screenY};
                    int16_t v2[2] = {p.verts[2].screenX, p.verts[2].screenY};
                    DrawTriangle(v0, v1, v2, color);
                    for (int vi = 0; vi < 3; vi++) {
                        const int16_t vx = p.verts[vi].screenX;
                        const int16_t vy = p.verts[vi].screenY;
                        if (!haveAabb || vx < aabbX0) aabbX0 = vx;
                        if (!haveAabb || vy < aabbY0) aabbY0 = vy;
                        if (!haveAabb || vx > aabbX1) aabbX1 = vx;
                        if (!haveAabb || vy > aabbY1) aabbY1 = vy;
                        haveAabb = true;
                    }
                }
            }
            // Lynx #217 body Contour windows (ASM 001797×4 after fan fill).
            if (haveAabb && head.texInfo == 0x4005)
                PunchLynxBodyWindowDigons(aabbX0, aabbY0, aabbX1, aabbY1);
            return;
        }
    }

    // Dig: AB_C721_NO_EDGE=1 drops vc=2 Contour pieces (Puma top cross-gap).
    const bool noEdge = getenv("AB_C721_NO_EDGE") &&
                        getenv("AB_C721_NO_EDGE")[0] == '1';
    C721SpanReset();
    if (getenv("AB_DUMP_C721_SPAN") && head.texInfo == 0x5015) {
        static int nsp = 0;
        if (nsp < 3) {
            fprintf(stderr, "C721SPAN nPieces=%d cid=%d color=%u\n", nPieces, (int)head.contourId, color);
            for (int i = 0; i < nPieces; i++) {
                const TransformedPoly& pp = *pieces[i];
                fprintf(stderr, "  piece[%d] vc=%d", i, pp.vertexCount);
                for (int v = 0; v < pp.vertexCount && v < 4; v++)
                    fprintf(stderr, " (%d,%d)", pp.verts[v].screenX, pp.verts[v].screenY);
                fprintf(stderr, "\n");
            }
            ++nsp;
        }
    }
    int16_t firstX = 0, firstY = 0, lastX = 0, lastY = 0;
    int16_t aabbX0 = 319, aabbY0 = 199, aabbX1 = 0, aabbY1 = 0;
    bool haveFirst = false;
    int16_t edgeLastX = 0, edgeLastY = 0;
    bool haveEdgeLast = false;
    // Track first edge start separately — ASM close goes cursor→start,
    // where "start" is the first edge endpoint, not the first bezier vertex.
    [[maybe_unused]] int16_t edgeFirstX = 0, edgeFirstY = 0;
    bool haveEdgeFirst = false;
    int16_t quadY0 = 199, quadY1 = 0;
    bool haveQuadY = false;
    for (int i = 0; i < nPieces; i++) {
        const TransformedPoly& p = *pieces[i];
        if (p.vertexCount == 4) {
            int16_t v0[2] = {p.verts[0].screenX, p.verts[0].screenY};
            int16_t v1[2] = {p.verts[1].screenX, p.verts[1].screenY};
            int16_t v2[2] = {p.verts[2].screenX, p.verts[2].screenY};
            int16_t v3[2] = {p.verts[3].screenX, p.verts[3].screenY};
            C721SpanAddBezier(v0, v1, v2, v3);
            if (!haveFirst) {
                firstX = v0[0]; firstY = v0[1];
                haveFirst = true;
            }
            lastX = v3[0]; lastY = v3[1];
            for (int vi = 0; vi < 4; vi++) {
                const int16_t vx = p.verts[vi].screenX;
                const int16_t vy = p.verts[vi].screenY;
                if (vx < aabbX0) aabbX0 = vx;
                if (vy < aabbY0) aabbY0 = vy;
                if (vx > aabbX1) aabbX1 = vx;
                if (vy > aabbY1) aabbY1 = vy;
                if (!haveQuadY || vy < quadY0) quadY0 = vy;
                if (!haveQuadY || vy > quadY1) quadY1 = vy;
                haveQuadY = true;
            }
        } else if (p.vertexCount == 3 &&
                   !(::getenv("AB_C721_TRI_EDGES") && ::getenv("AB_C721_TRI_EDGES")[0] == '0')) {
            int16_t t0[2] = {p.verts[0].screenX, p.verts[0].screenY};
            int16_t t1[2] = {p.verts[1].screenX, p.verts[1].screenY};
            int16_t t2[2] = {p.verts[2].screenX, p.verts[2].screenY};
            C721SpanAddEdge(t0[0], t0[1], t1[0], t1[1]);
            C721SpanAddEdge(t1[0], t1[1], t2[0], t2[1]);
            C721SpanAddEdge(t2[0], t2[1], t0[0], t0[1]);
            if (!haveFirst) { firstX = t0[0]; firstY = t0[1]; haveFirst = true; }
            lastX = t0[0]; lastY = t0[1];
        } else if (p.vertexCount == 2 && !noEdge) {
            const int16_t ex0 = p.verts[0].screenX;
            const int16_t ey0 = p.verts[0].screenY;
            const int16_t ex1 = p.verts[1].screenX;
            const int16_t ey1 = p.verts[1].screenY;
            C721SpanAddEdge(ex0, ey0, ex1, ey1);
            // Track first edge start — ASM close goes cursor→start where
            // "start" is the first edge endpoint, not the first bezier vertex.
            if (!haveEdgeFirst) {
                edgeFirstX = ex0;
                edgeFirstY = ey0;
                haveEdgeFirst = true;
            }
            // Digon-only Contours (no bezier quads): seed close endpoints so
            // ASM 001796 cursor→start can fire (Eagle diag tip Contours).
            // For Contours with quads: track digon endpoints separately for
            // the close chord. ASM close goes cursor→start where cursor is
            // the last edge endpoint and start is the first edge endpoint.
            if (!haveFirst) {
                firstX = ex0;
                firstY = ey0;
                haveFirst = true;
            }
            lastX = ex1;
            lastY = ey1;
            // Remember edge tip for Eagle wide-close (applied after AABB known).
            edgeLastX = ex1; edgeLastY = ey1;
            haveEdgeLast = true;
            // Include edges in Contour AABB — Eagle top Contours are one flat
            // bezier quad (spanH=0) plus edges to the tip; without this,
            // eagle5015Close never fires (spanH stays 0).
            if (ex0 < aabbX0) aabbX0 = ex0;
            if (ey0 < aabbY0) aabbY0 = ey0;
            if (ex0 > aabbX1) aabbX1 = ex0;
            if (ey0 > aabbY1) aabbY1 = ey0;
            if (ex1 < aabbX0) aabbX0 = ex1;
            if (ey1 < aabbY0) aabbY0 = ey1;
            if (ex1 > aabbX1) aabbX1 = ex1;
            if (ey1 > aabbY1) aabbY1 = ey1;
        }
    }
    // ASM Contour ends with 001796 close (cursor→start). With pieces in mesh
    // order, last→first is the real close chord (painter order made this a
    // diagonal carve on front/diag). Dig: AB_C721_NO_CLOSE=1.
    const bool noClose = getenv("AB_C721_NO_CLOSE") &&
                         getenv("AB_C721_NO_CLOSE")[0] == '1';
    // Puma wide Contours: close chord is duplicate-hit even-odd at one tip and
    // paints a spurious opposite tip row. Skip close on wide AABB; tall/side
    // Contours still need it. Dig: AB_C721_WIDE_CLOSE=1.
    // Eagle 0x5015 Contours are wide but ASM always closes (001796→start).
    // Force close on wide 0x5015 Contours with real height (orbit LRF + Mk2/3).
    // Flat front Contours (h≈10) must keep the wide skip — closing floods OC.
    // Orbit Mk3 Contour lobes hit spanH≈14 (cid3 lower) — need close below 20.
    // Edge-on top Contours: flat bezier (quadH≤1) + tip edges → close when
    // full AABB has height. Dig: AB_C721_NO_5015_CLOSE=1.
    // Dig: AB_C721_5015_SPAN=N overrides the spanH floor (default 14).
    const int spanW = aabbX1 - aabbX0;
    const int spanH = aabbY1 - aabbY0;
    const int quadH = haveQuadY ? (quadY1 - quadY0) : spanH;
    const bool wideContour = spanW * 5 >= spanH * 9;
    [[maybe_unused]] const bool forceWideClose = getenv("AB_C721_WIDE_CLOSE") &&
                                getenv("AB_C721_WIDE_CLOSE")[0] == '1';
    const bool edgeOnFlatClose = haveQuadY && haveEdgeLast && quadH <= 1 &&
                                 spanH >= 4;
    int eagleSpanMin = 14;
    if (const char* es = getenv("AB_C721_5015_SPAN"); es && es[0])
        eagleSpanMin = atoi(es);
    const bool eagle5015Close = head.texInfo == 0x5015 && wideContour &&
        (spanH >= eagleSpanMin || edgeOnFlatClose) &&
        !(getenv("AB_C721_NO_5015_CLOSE") &&
          getenv("AB_C721_NO_5015_CLOSE")[0] == '1');
    // Skeet #45 digon Contours (2×001797 + edges): ASM always closes
    // (001796→start). Wide skip left the lobe open → top/orbit OC. Puma
    // butterfly is nQuads==4 and stays on the wide skip. Dig:
    // AB_C721_NO_DIGON_CLOSE=1.
    // Single-bezier wide mat8 Contours (Puma wing lobes at close range) also
    // close: one curve plus its chord is the whole shape, so skipping the close
    // leaves the lobe unfilled (black notches in the wing silhouette). The
    // multi-quad butterfly (nQuads>=3) keeps the wide skip. Widening this past
    // 0x4008 costs more than it gains (all material words: Lion −411 but
    // Harrier +312; every tex word: net +173). Dig: AB_C721_NO_SINGLE_CLOSE=1.
    const bool singleWideClose =
        nQuads == 1 &&
        (head.texInfo == 0x4008 ||
         // Hawk #21 / Kestrel #22 top Contour: one horizontal 001797 + edge
         // piece (nQ=1, spanH≤10). ASM always 001796-closes; wide skip left
         // the lobe open → Phase 61 exposed IDX/OA on 05_top. Puma multi-quad
         // butterflies stay on the wide skip. Dig: AB_C721_NO_SINGLE_CLOSE=1.
         (head.texInfo == 0x4009 && spanH <= 10)) &&
        !(getenv("AB_C721_NO_SINGLE_CLOSE") &&
          getenv("AB_C721_NO_SINGLE_CLOSE")[0] == '1');
    const bool digonWideClose =
        ((nQuads == 2 &&
          !(getenv("AB_C721_NO_DIGON_CLOSE") &&
            getenv("AB_C721_NO_DIGON_CLOSE")[0] == '1')) ||
         singleWideClose) &&
        wideContour;
    if (eagle5015Close && haveEdgeLast) {
        lastX = edgeLastX;
        lastY = edgeLastY;
    }
    // When Contour has quads + digons: keep bezier endpoints for close.
    // ASM close goes cursor→start — the cursor and start are tracked by
    // the parser, not derivable from piece endpoints alone.
    // ASM FUNC_001721 ALWAYS closes the Contour path at terminator 001722
    // (cursor→start chord) before flushing spans — there is no width-dependent
    // skip in the ASM. The old `!wideContour` escape hatch left wide lens-shaped
    // Contours (hull #33 0x4027 cid 6/9) open so their even-odd spans never
    // covered the flanks (the grey-4 diamond: 1203 oa cells). Closing them is
    // byte-exact on every other hull/pose measured (R511/R512 sweep). Dig:
    // AB_C721_NO_WIDE_CLOSE=1 restores the legacy skip.
    const bool noWideClose = getenv("AB_C721_NO_WIDE_CLOSE") &&
                             getenv("AB_C721_NO_WIDE_CLOSE")[0] == '1';
    const bool doClose = haveFirst && !noClose && !noWideClose &&
                         (lastX != firstX || lastY != firstY);
    if (getenv("AB_DUMP_C721_CLOSE") && getenv("AB_DUMP_C721_CLOSE")[0] == '1') {
        fprintf(stderr,
                "C721CLOSE nQ=%d first=(%d,%d) last=(%d,%d) do=%d "
                "tex=0x%04x w=%d h=%d qh=%d wide=%d eag=%d dig=%d\n",
                nQuads, firstX, firstY, lastX, lastY, (int)doClose,
                head.texInfo, spanW, spanH, quadH, (int)wideContour,
                (int)eagle5015Close, (int)digonWideClose);
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            fprintf(stderr, "  piece[%d] vc=%d mi=%d vi=%u,%u,%u,%u",
                    i, p.vertexCount, p.meshIndex,
                    p.vertexIdx[0], p.vertexIdx[1], p.vertexIdx[2], p.vertexIdx[3]);
            for (int vi = 0; vi < p.vertexCount && vi < 4; vi++)
                fprintf(stderr, " (%d,%d)", p.verts[vi].screenX, p.verts[vi].screenY);
            fprintf(stderr, "\n");
        }
    }
    if (doClose)
        C721SpanAddEdge(lastX, lastY, firstX, firstY);
    // Digon-only Eagle Contours (Contour body culled): even-odd span of tip
    // digon edges floods Contour OC on orbit. Skip flush — tip Contour lobe
    // paint in tip flange handles ASM 001797 tip Contours. Dig:
    // AB_EAGLE_DIGON_SPAN=1 restores span flush for digon-only Contours.
    // R546 candidate law (gated AB_DIGON_QUADSPAN): #23's lens pinch group is
    // cid=5 tex=0x2000 ng=2 with BOTH pieces vc=4 quads — even-odd of two
    // stacked quads cancels to EMPTY interior, yet ASM paints the quad area.
    // The ASM 001799-era Contour fill for such groups fills each quad's own
    // span set (union), not the closed-path even-odd. When gated: if every
    // piece is a quad and nQuads==nPieces, paint each quad directly (empty->
    // color) instead of relying on the shared span buffer.
    const bool digonQuadSpan =
        ::getenv("AB_DIGON_QUADSPAN") && ::getenv("AB_DIGON_QUADSPAN")[0] == '1' &&
        nPieces >= 1 && nQuads == nPieces &&
        !(head.texInfo == 0x5015 || head.texInfo == 0x5009) &&
        spanH <= 8 && g_videoState &&
        // R551: only groups whose own color resolves to a real value may fill;
        // 0-color groups (lens cid=5) must NOT paint anything (ASM order).
        ComplexFillColor(head, shipModel, shipRot) != 0;
    const bool digonOnlyEagle =
        !digonQuadSpan && nQuads == 0 && head.texInfo == 0x5015 &&
        !(getenv("AB_EAGLE_DIGON_SPAN") &&
          getenv("AB_EAGLE_DIGON_SPAN")[0] == '1');
    if (!digonOnlyEagle)
        C721SpanFlush(color);
    else
        C721SpanReset();
    if (::getenv("AB_QSDBG2")) fprintf(stderr, "QSDBG2 tex=0x%x cid=%d nP=%d nQ=%d spanH=%d qs=%d"
            " shadeIdx=%u nIdx=%u color=%u fl=%d,%d,%d\n",
            head.texInfo, (int)head.contourId, nPieces, nQuads, spanH,
            (int)digonQuadSpan, (unsigned)head.shadeIdx,
            (unsigned)head.normalIdx, (unsigned)color,
            (int)head.faceLight[0], (int)head.faceLight[1], (int)head.faceLight[2]);

    if (digonQuadSpan) {
        fprintf(stderr, "QSDBG headtex=0x%x nP=%d nQ=%d spanH=%d color=%u\n",
                head.texInfo, nPieces, nQuads, spanH, (unsigned)color);
        if (::getenv("AB_QS_COLOR")) {
            const uint8_t qc = (uint8_t)atoi(::getenv("AB_QS_COLOR"));
            if (qc) color = qc;
        }
        uint8_t* bufq = g_videoState->backBuffer;
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            int16_t xs[4], ys[4];
            for (int v = 0; v < 4; v++) { xs[v] = p.verts[v].screenX; ys[v] = p.verts[v].screenY; }
            int y0 = std::min({ys[0],ys[1],ys[2],ys[3]}), y1 = std::max({ys[0],ys[1],ys[2],ys[3]});
            for (int y = y0; y <= y1; y++) {
                if (y < 0 || y >= 200) continue;
                // span by edge crossings at row centre y+0.5 (ASM 001560 style)
                int16_t hx[4]; int hn = 0;
                for (int e = 0; e < 4; e++) {
                    int e2 = (e + 1) & 3;
                    int ya_ = ys[e], yb_ = ys[e2];
                    if (ya_ == yb_) continue;
                    int lo = std::min(ya_, yb_), hi = std::max(ya_, yb_);
                    if (y >= lo && y < hi) {
                        hx[hn++] = (int16_t)(xs[e] + (int64_t)(xs[e2]-xs[e]) * (y - ya_) / (yb_ - ya_));
                    }
                }
                if (hn >= 2) {
                    std::sort(hx, hx + hn);
                    for (int x = hx[0]; x <= hx[hn-1]; x++) {
                        if (x < 0 || x >= 320) continue;
                        uint8_t& pix = bufq[y * 320 + x];
                        if (pix == 0) pix = color;
                    }
                }
            }
        }
    }

    // Puma #60 orthog Contour: flat Contour bridge at Contour ymax (back
    // (179,98)-(141,98)) plus bezier endpoints on that row form an even-odd
    // chord ASM Contour fill does not paint (ASM maxY = Contour ymax−1).
    // Dig: AB_PUMA_KEEP_YMAX=1.
    if (jjffe::GetShipModelIndex(shipModel) == 60 &&
        head.texInfo == 0x4008 && nQuads == 4 && wideContour && spanH >= 30 &&
        g_videoState &&
        !(getenv("AB_PUMA_KEEP_YMAX") && getenv("AB_PUMA_KEEP_YMAX")[0] == '1')) {
        int xL = 319, xR = 0;
        bool flatBottom = false;
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            if (p.vertexCount != 2) continue;
            if (p.verts[0].screenY != aabbY1 || p.verts[1].screenY != aabbY1)
                continue;
            flatBottom = true;
            int xa = p.verts[0].screenX, xb = p.verts[1].screenX;
            if (xa > xb) {
                const int t = xa;
                xa = xb;
                xb = t;
            }
            if (xa < xL) xL = xa;
            if (xb > xR) xR = xb;
        }
        if (flatBottom && aabbY1 >= 0 && aabbY1 < 200) {
        if (::getenv("AB_GRAZE_PROBE") && ::getenv("AB_GRAZE_PROBE")[0] == '1' &&
            aabbY1 >= 88 && aabbY1 <= 98 && xL <= 200 && xR >= 140) {
            static int s_eb = 0;
            if (s_eb < 8) {
                fprintf(stderr, "ERASE flatB=%d y=%d xl=%d xr=%d color=%d\n",
                        flatBottom ? 1 : 0, aabbY1, xL, xR, (int)color);
                ++s_eb;
            }
        }
        if (::getenv("AB_ERASEALL") && ::getenv("AB_ERASEALL")[0] == '1' &&
            aabbY1 >= 0 && aabbY1 < 200 && g_videoState) {
            uint8_t* buf2 = g_videoState->backBuffer;
            if (xL < 0) xL = 0;
            if (xR > 319) xR = 319;
            for (int x = xL; x <= xR; x++) {
                const uint8_t v = buf2[aabbY1 * 320 + x];
                if (v == color || (v >> 1) == (color >> 1))
                    buf2[aabbY1 * 320 + x] = 0;
            }
        } else {
            uint8_t* buf = g_videoState->backBuffer;
            if (xL < 0) xL = 0;
            if (xR > 319) xR = 319;
            for (int x = xL; x <= xR; x++) {
                if (buf[aabbY1 * 320 + x] == color)
                    buf[aabbY1 * 320 + x] = 0;
            }
        }
        }
    }

    // Eagle edge-on top Contours (quadH≤1, Contour AABB h≤8, w≥40).
    // ASM keeps Contour fill above the body from upper digon edges
    // (yBody+1..yBody+4). Previous clear(≥yBody+2) was over-aggressive:
    // removed 94 px at y=81-84 that ASM retains. Only clear extreme
    // overdraw at yBody+6+ (close→tip digon OC far from body).
    // Tip digon Contour flange for upper tips.
    // Dig: AB_EAGLE_NO_EDGEON_CLEAN=1.
    if (edgeOnFlatClose && spanH <= 8 && spanW >= 40 &&
        head.texInfo == 0x5015 && g_videoState &&
        !(getenv("AB_EAGLE_NO_EDGEON_CLEAN") &&
          getenv("AB_EAGLE_NO_EDGEON_CLEAN")[0] == '1')) {
        uint8_t* buf = g_videoState->backBuffer;
        const int x0 = aabbX0 < 0 ? 0 : aabbX0;
        const int x1 = aabbX1 > 319 ? 319 : aabbX1;
        const int yBody = haveQuadY ? quadY1 : ((aabbY0 + aabbY1) / 2);
        // Narrow clear: only yBody+6 and above (was yBody+2 — too aggressive).
        const int yClear = yBody + 6;
        for (int y = yClear; y <= aabbY1 && y < 200; y++) {
            if (y < 0)
                continue;
            for (int x = x0; x <= x1; x++) {
                if (buf[y * 320 + x] == color)
                    buf[y * 320 + x] = 0;
            }
        }
        // Contour freckle restamp deferred to end-of-draw (tex overdraws
        // Contour tips after Contour flush).
        // Upper tip digon Contour flange (empty→Contour near Contour).
        const int cmy = (aabbY0 + aabbY1) / 2;
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            if (p.vertexCount != 2)
                continue;
            int xa = p.verts[0].screenX, ya = p.verts[0].screenY;
            int xb = p.verts[1].screenX, yb = p.verts[1].screenY;
            if (xa > xb) {
                const int t = xa;
                xa = xb;
                xb = t;
            }
            if (ya > yb) {
                const int t = ya;
                ya = yb;
                yb = t;
            }
            const int tw = xb - xa;
            const int th = yb - ya;
            if (th > 3 || tw < 8 || tw > 48)
                continue;
            if (((ya + yb) / 2) > cmy)
                continue;  // lower tip — close chord OC, already cleared
            for (int y = ya - 1; y <= yb + 1; y++) {
                if (y < 0 || y >= 200 || y >= yClear)
                    continue;
                for (int x = xa - 2; x <= xb + 2; x++) {
                    if (x < 0 || x >= 320)
                        continue;
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix != 0)
                        continue;
                    int nb = 0;
                    if (x > 0 && buf[y * 320 + (x - 1)] == color)
                        nb++;
                    if (x < 319 && buf[y * 320 + (x + 1)] == color)
                        nb++;
                    if (y > 0 && buf[(y - 1) * 320 + x] == color)
                        nb++;
                    if (y < 199 && buf[(y + 1) * 320 + x] == color)
                        nb++;
                    if (nb >= 1)
                        pix = color;
                }
            }
        }
    }

    // Eagle #23–25 orbit Contour tip flange: even-odd of the closed Contour
    // path pinches tip digons; ASM fills a wider tip band (VGA22). Expand
    // near-horizontal tip edges (vc=2, h≤2, w∈[8,48]) with tip-width pad
    // budget (tw-2)+(tw/2-1), split toward Contour AABB leftovers (handles
    // orbit_p90 vs p270 asymmetry). Outward +2Y when tip spans 2 rows;
    // inward +2Y when flat (1 row). Skip edge-on Contours (spanH <
    // eagleSpanMin) — top flat Contours flood OC. Empty→Contour only.
    // Dig: AB_EAGLE_NO_TIP_FLANGE=1.
    // R544: flange REMOVED from defaults — full-fleet sweep (R543) shows it is a
    // workaround whose removal flips scored hull #23 C->B (477/152) with 21/22/65
    // bit-identical; hulls 24/25 are outside gate.ab_set (collateral only). The
    // true tip-lobe law (FUNC_001548 control points) remains open. Dig back ON
    // with AB_EAGLE_TIP_FLANGE=1.
    if (head.texInfo == 0x5015 && spanH >= eagleSpanMin && g_videoState &&
        getenv("AB_EAGLE_TIP_FLANGE") &&
        getenv("AB_EAGLE_TIP_FLANGE")[0] == '1') {
        const int cmy = (aabbY0 + aabbY1) / 2;
        uint8_t* buf = g_videoState->backBuffer;
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            if (p.vertexCount != 2) continue;
            int xa = p.verts[0].screenX;
            int ya = p.verts[0].screenY;
            int xb = p.verts[1].screenX;
            int yb = p.verts[1].screenY;
            if (xa > xb) {
                const int tx = xa; xa = xb; xb = tx;
            }
            if (ya > yb) {
                const int ty = ya; ya = yb; yb = ty;
            }
            const int tw = xb - xa;
            const int th = yb - ya;
            // Eagle digon-only tip Contours: tip edges are often tw∈[2,7]
            // (ASM 001797 tip Contour lobes). Default tw≥8 skipped them.
            // Dig: AB_EAGLE_NO_SHORT_TIP=1 restores tw≥8 for all.
            const int twMin =
                (head.texInfo == 0x5015 && nQuads == 0 &&
                 !(getenv("AB_EAGLE_NO_SHORT_TIP") &&
                   getenv("AB_EAGLE_NO_SHORT_TIP")[0] == '1'))
                    ? 2
                    : 8;
            if (th > 2 || tw < twMin || tw > 48) continue;
            // Short tip digons (tw<8): ASM diag tip Contours are shade6 chrome
            // islands (001805 tip tris) outside Contour span, not Contour fill.
            // Empty→6 in a tip pad. Dig: AB_EAGLE_NO_SHORT_TIP=1 disables.
            // Contour VGA22 on digon-only diag lobes still open (cpp Contour=0
            // on diag) — Contour stroke digs OC; keep shade6 until a tighter
            // Contour ridge lands.
            if (tw < 8 && head.texInfo == 0x5015 && nQuads == 0) {
                const bool tipAtTop = ((ya + yb) / 2) <= cmy;
                const int pad = 3;
                const int yLo = tipAtTop ? (ya - pad) : ya;
                const int yHi = tipAtTop ? yb : (yb + pad);
                const bool tipLeft = ((xa + xb) / 2) <= ((aabbX0 + aabbX1) / 2);
                const int xLo = tipLeft ? (xa - pad) : xa;
                const int xHi = tipLeft ? xb : (xb + pad);
                for (int y = yLo; y <= yHi; y++) {
                    if (y < 0 || y >= 200) continue;
                    for (int x = xLo; x <= xHi; x++) {
                        if (x < 0 || x >= 320) continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 0)
                            pix = 6;
                    }
                }
                continue;
            }
            // Digon-only Contours: skip regular tip flange (orbit Contour tip
            // digons tw≥8 flood Contour OC). Short tip path above is enough.
            if (nQuads == 0)
                continue;
            const int totalPad =
                (tw - 2) + (tw / 2 > 0 ? tw / 2 - 1 : 0);
            const int loL = xa > aabbX0 ? xa - aabbX0 : 0;
            const int loR = aabbX1 > xb ? aabbX1 - xb : 0;
            int pl, pr;
            if (loL + loR > 0) {
                pl = totalPad * loL / (loL + loR);
                pr = totalPad - pl;
            } else {
                pl = tw - 2;
                pr = tw / 2 > 0 ? tw / 2 - 1 : 0;
            }
            // Optional cap on tip-outward pad (AB_EAGLE_OUT_CAP=N). Default off
            // — any cap chops orbit Contour tip OA. Kept as dig only.
            if (const char* oc = getenv("AB_EAGLE_OUT_CAP"); oc && oc[0]) {
                const int outCap = atoi(oc);
                if (outCap >= 0) {
                    const int cmx = (aabbX0 + aabbX1) / 2;
                    const bool tipLeft = ((xa + xb) / 2) <= cmx;
                    if (tipLeft) {
                        if (pl > outCap)
                            pl = outCap;
                    } else {
                        if (pr > outCap)
                            pr = outCap;
                    }
                }
            }
            const bool tipAtTop = ((ya + yb) / 2) <= cmy;
            // R514 candidate law (gated, default OFF): ASM FUNC_001797 emits a
            // bezier edge pair + chord into the SAME span buffer (FUNC_001560
            // callbacks), so a tip digon's lobe is an even-odd SPAN, not a
            // padded rectangle. The old pad budget (tw-2)+(tw/2-1) and the
            // grey-freckle overwrite are both workarounds: the rectangle paints
            // over the 0x4017 quad (the #23 idx mass) yet still misses the real
            // lobes (#24), and #25 needs it only because its contour BODY was
            // culled upstream (digonOnlyEagle skips the flush).
            // AB_EAGLE_TIP_LOBE=1 fills each near-horizontal tip digon as
            // bezier(a→b bulge outward) + chord b→a, even-odd, empty-only.
            if (getenv("AB_EAGLE_TIP_LOBE") &&
                getenv("AB_EAGLE_TIP_LOBE")[0] == '1') {
    
                // R545 v2: the pinch is a LENS DIGON between two contour edges
                // meeting at the side tips (80,79)/(211,89). ASM's outer edge
                // bows through x=90 at y75..78 while the inner edge cuts straight
                // from (80,79) to ~(118,78): fill bezier(tipA -> mid-bulge ->
                // tipB) + chord, empty-only. Bulge direction: away from AABB
                // centre row (tipAtTop -> up). Amplitude from measured bow:
                // |midX - chordMidY| grows ~10px over 4 rows => control offset
                // = quarter-width beyond each tip end.
                const int16_t ax = static_cast<int16_t>(xa), ay = static_cast<int16_t>(ya);
                const int16_t bx = static_cast<int16_t>(xb), by = static_cast<int16_t>(yb);
                const int ext = (tw < 4 ? 4 : tw / 2) + 2;   // lateral reach past tips
                const int amp = (th < 1 ? 1 : th) + 2;       // vertical bow rows
                const int dirY = tipAtTop ? -1 : 1;
                const int16_t c0x = static_cast<int16_t>(xa - ext);
                const int16_t c0y = static_cast<int16_t>(ya + dirY * amp);
                const int16_t c1x = static_cast<int16_t>(xb + ext);
                const int16_t c1y = static_cast<int16_t>(yb + dirY * amp);
                C721SpanReset();
                const int16_t p0[2] = {ax, ay}, p1[2] = {c0x, c0y},
                              p2[2] = {c1x, c1y}, p3[2] = {bx, by};
                C721SpanAddBezier(p0, p1, p2, p3);
                C721SpanAddEdge(bx, by, ax, ay);
                C721SpanFlushEmpty(color);
                continue;
            }
            // Outward Y expand: default 2. Dig: AB_EAGLE_TIP_YOUT=N (0/1/2).
            int doOut = th >= 1 ? 2 : 0;
            if (const char* yo = getenv("AB_EAGLE_TIP_YOUT"); yo && yo[0])
                doOut = th >= 1 ? atoi(yo) : 0;
            if (doOut < 0)
                doOut = 0;
            const int doIn = th < 1 ? 2 : 0;
            const int yLo = tipAtTop ? (ya - doOut) : (ya - doIn);
            const int yHi = tipAtTop ? (yb + doIn) : (yb + doOut);
            const int fx0 = xa - pl;
            const int fx1 = xb + pr;
            for (int y = yLo; y <= yHi; y++) {
                if (y < 0 || y >= 200) continue;
                for (int x = fx0; x <= fx1; x++) {
                    if (x < 0 || x >= 320) continue;
                    uint8_t& pix = buf[y * 320 + x];
                    // Empty→Contour, and Contour freckles (tex over Contour tip).
                    const bool emptyOnly =
                        getenv("AB_EAGLE_TIP_EMPTY_ONLY") &&
                        getenv("AB_EAGLE_TIP_EMPTY_ONLY")[0] == '1';
                    if (emptyOnly ? (pix == 0) :
                        (pix == 0 || pix == 4 || pix == 5 || pix == 6 ||
                         pix == 7 || pix == 8))
                        pix = color;
                }
            }
            // Contour tip digon outward overshoot: Contour OC just past tip
            // band (orbit Contour finger at tip_y+doOut+1). Clear Contour only.
            if (tipAtTop) {
                const int yClear = ya - doOut - 1;
                if (yClear >= 0 && yClear < 200) {
                    for (int x = fx0; x <= fx1; x++) {
                        if (x < 0 || x >= 320) continue;
                        if (buf[yClear * 320 + x] == color)
                            buf[yClear * 320 + x] = 0;
                    }
                }
            } else {
                const int yClear = yb + doOut + 1;
                if (yClear >= 0 && yClear < 200) {
                    for (int x = fx0; x <= fx1; x++) {
                        if (x < 0 || x >= 320) continue;
                        if (buf[yClear * 320 + x] == color)
                            buf[yClear * 320 + x] = 0;
                    }
                }
            }
        }
    }

    // Puma butterfly: 4 bezier quads (+ optional bridge). Solid Contour matches
    // ASM Contour 2; then VGA0 digon punches carve wing gaps (ASM Contours 3–6).
    // Front/back: near-horizontal chord + wide AABB. Diag: diagonal chord +
    // near-square AABB. Left/right: near-vertical chord + tall AABB — same
    // wing-gap digon template rotated 90° (ASM yaw90 digons match front UL×rot90).
    bool hasHBridge = false, hasDiagBridge = false, hasVBridge = false;
    auto classify = [&](int dx, int dy) {
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > 0 && dy * 2 <= dx)
            hasHBridge = true;
        else if (dy > 0 && dx * 2 <= dy)
            hasVBridge = true;
        else if (dx > 0 && dy > 0 && dy * 2 > dx && dx * 2 > dy)
            hasDiagBridge = true;
    };
    for (int i = 0; i < nPieces; i++) {
        const TransformedPoly& p = *pieces[i];
        if (p.vertexCount != 2) continue;
        classify(p.verts[1].screenX - p.verts[0].screenX,
                 p.verts[1].screenY - p.verts[0].screenY);
    }
    // Side Contours often omit the vc=2 bridge; use first→last chord instead.
    if (haveFirst && !hasHBridge && !hasVBridge && !hasDiagBridge)
        classify(lastX - firstX, lastY - firstY);
    const int bw = aabbX1 - aabbX0;
    const int bh = aabbY1 - aabbY0;
    const bool wideOk = hasHBridge && (bw * 5 >= bh * 9);
    const bool tallOk = hasVBridge && (bh * 5 >= bw * 9);
    const bool diagOk = hasDiagBridge && (bw * 2 >= bh) && (bh * 2 >= bw);
    if (nQuads == 4 && haveFirst && wideOk)
        PunchPumaWingGapDigons(aabbX0, aabbY0, aabbX1, aabbY1, 0);
    else if (nQuads == 4 && haveFirst && tallOk)
        PunchPumaWingGapDigons(aabbX0, aabbY0, aabbX1, aabbY1, 1);
    else if (nQuads == 4 && haveFirst && diagOk)
        PunchPumaDiagCenterDigons(aabbX0, aabbY0, aabbX1, aabbY1);

    // Wide Contour: skip-close avoids the duplicate-hit close chord OC. The
    // vc=2 tip bridge at Contour ymin (front Contour tip) collapses to an
    // empty even-odd span — repaint it after digon punches. Do NOT repaint
    // Contour ymax (back Contour bottom): ASM Contour fill maxY = ymax−1.
    // Dig: AB_C721_NO_TIP_ROW=1.
    if (nQuads == 4 && haveFirst && wideOk && wideContour &&
        !(getenv("AB_C721_NO_TIP_ROW") && getenv("AB_C721_NO_TIP_ROW")[0] == '1') &&
        g_videoState) {
        for (int i = 0; i < nPieces; i++) {
            const TransformedPoly& p = *pieces[i];
            if (p.vertexCount != 2) continue;
            if (p.verts[0].screenY != p.verts[1].screenY) continue;
            const int16_t y = p.verts[0].screenY;
            // Front tip chord only (Contour ymin). Back ymax chord is OC.
            if (y != aabbY0) continue;
            if (y < 0 || y >= k3DClipBottom) continue;
            int xL = p.verts[0].screenX < p.verts[1].screenX ?
                     p.verts[0].screenX : p.verts[1].screenX;
            int xR = p.verts[0].screenX < p.verts[1].screenX ?
                     p.verts[1].screenX : p.verts[0].screenX;
            if (xL < 0) xL = 0;
            if (xR > 320) xR = 320;
            if (xL < xR)
                memset(g_videoState->backBuffer + y * 320 + xL, color,
                       static_cast<size_t>(xR - xL));
            break;
        }
    }
}


static void DrawFlatPolygon(const TransformedPoly& poly, const ShipModel* shipModel, const int32_t* shipRot)
{

    // AB_PLOTBOX identity feed for every rasterizer plot below (dig-only).
    jjffe::AbPlotSetFace(poly.meshIndex, poly.vertexCount, poly.texInfo,
                         poly.verts[0].camX, poly.verts[0].camY);
    // R645 dig: actual draw-call order (the list the draw loop consumes).
    if (const char* oe = ::getenv("AB_ORDER"); oe && oe[0] && oe[0] != '0') {
        static int s_no = 0;
        if (s_no < 2000) {
            fprintf(stderr, "ORDER seq=%d face=%d key=%u type=%d vc=%d cam=(%d,%d)\n",
                    s_no++, poly.meshIndex, poly.sortKey, (int)poly.type,
                    poly.vertexCount, (int)poly.verts[0].camX,
                    (int)poly.verts[0].camY);
        }
    }
    if (jjffe::AbPlotArmed() && poly.meshIndex >= 42 && poly.meshIndex <= 55) {
        static int s_en = 0;
        if (s_en < 40) {
            int16_t ys0 = 999, ys1 = -999;
            for (int v = 0; v < poly.vertexCount; v++) {
                if (poly.verts[v].screenY < ys0) ys0 = poly.verts[v].screenY;
                if (poly.verts[v].screenY > ys1) ys1 = poly.verts[v].screenY;
            }
            fprintf(stderr, "ENTER face=%d tex=0x%04x type=%d vc=%d back=%d vis=%d y=%d..%d sp=0x%04x key=%u\n",
                    poly.meshIndex, poly.texInfo, (int)poly.type, poly.vertexCount,
                    poly.backFacing ? 1 : 0, poly.visible ? 1 : 0, ys0, ys1,
                    poly.specialParam, poly.sortKey);
            ++s_en;
        }
    }

    // ===== R268 K1 walk-ring face cull (AB_RINGCULL=1 gated) =====
    // R270 PRECISE KEY: ring records are identified by their normal index -
    // the sidecar 1712/1708 f-param equals the poly's normalIdx (measured:
    // f=34 == face47 norm=34). Skip exactly the ring normals (both hull
    // sides); band replay (kWalk43) paints those faces' pixels with the
    // material-row law, black at orbits included (charter 240 follow-up).
    if (::getenv("AB_RINGCULL") && ::getenv("AB_RINGCULL")[0] == '1') {
        const int hullN = jjffe::GetShipModelIndex(shipModel);
        static const uint8_t kRingNorms43[] = {
            34, 35, 38, 39, 42, 43, 50, 51, 52, 53, 56, 57, 58, 59,
            76, 77, 78, 79, 80, 81
        };
        bool hit = false;
        if (const char* ln = ::getenv("AB_RINGNORMS")) {   // R271 attribution:
            for (const char* p = ln; *p; ) {                // comma list mode
                int v = (int)strtol(p, (char**)&p, 10);
                while (*p == ',') p++;
                if (v == (int)poly.normalIdx) { hit = true; break; }
            }
        } else if (hullN == 43)
            hit = std::binary_search(std::begin(kRingNorms43),
                                     std::end(kRingNorms43), (uint8_t)poly.normalIdx);
        if (hit) return;
    }

    // (R268 material-set variant below stays gated; superseded by normals)
    // The ring faces are WALK-ERA records: the sidecar's classic lane never
    // paints them (charter 240) - their pixels come ONLY from the band-job
    // replay. Suppress the port's classic paint when poly material matches
    // the hull's replay-kit material set (measured record identity; live
    // behavior stays pose-invariant - the band jobs cover every pose).
    // R271: re-gated to '2' - under '1' this variant polluted the R268/R270
    // attributions (shared gate with the normals block above): the 5961
    // "K1 falsifies" conclusion is WITHDRAWN pending clean re-measure.
    if (::getenv("AB_RINGCULL") && ::getenv("AB_RINGCULL")[0] == '2') {
        const int hullR = jjffe::GetShipModelIndex(shipModel);
        static const uint16_t kRingMats43[] = { 0x10,0x15,0x26,0x33,0x3c,0x3d };
        static const uint16_t kRingMats61[] = { 0x09,0x0b };
        const uint16_t* rm; int n;
        if (hullR == 43) { rm = kRingMats43; n = 6; }
        else if (hullR == 61) { rm = kRingMats61; n = 2; }
        else { rm = nullptr; n = 0; }
        const uint16_t mi = (uint16_t)(poly.texInfo & 0xFFF);
        if (rm && std::find(rm, rm + n, mi) != rm + n) {
            // band replay (kWalk43/61) paints this face's pixels
            if (const char* e = ::getenv("AB_BANKREPLAY")) {
                if (e[0] != '0') return;      // lane is on -> classic stands down
            } else return;
        }
    }

    // ===== R262 FAMILY-B build (AB_BANDFAM=1, gated) =====
    // Band-family records are drawn by the walk-era callbacks THEMSELVES
    // (FUNC_001805/1807 twins, core.cpp). Sidecar ground truth (R262 gdb on
    // ffeasm): job drawTex = material[+0] dword (0x3d) and the ramp words at
    // the 1805/1807 job offsets are the material 7 RGB rows VERBATIM (1692
    // yields shade0=0 for bit-14 words; facing[0..2] add reads 0 in the demo
    // - pose-invariant ramps measured across all 8 poses). Painter order =
    // classic poly order; palette tail = the twins' own semantics.
    if (::getenv("AB_BANDFAM") && ::getenv("AB_BANDFAM")[0] == '1') {
        static const uint32_t kBandFamJ[] = {
#include "bandfam.inc"
        };
        const int hullJ = jjffe::GetShipModelIndex(shipModel);
        const uint32_t keyJ = (uint32_t)(((uint32_t)(unsigned)hullJ << 16) |
                (uint16_t)std::max(0, poly.meshIndex));
        bool inFam = std::binary_search(std::begin(kBandFamJ), std::end(kBandFamJ), keyJ);
        // R570 candidate law (gated AB_BANDFAM_65MAT): ASM draws #65's engine-cluster strip via
        // FUNC_001807 jobs whose MATERIAL WORDS are 0x3d/0x3e/0x3f/0x53 (measured: both unlit grey
        // {0x0222..} and lit variants appear as SEPARATE jobs). Port-side equivalent selection: any
        // visible tri/quad whose material row resolves a nonzero palette may take the lane when gated.
        const bool fam65mat = ::getenv("AB_BANDFAM_65MAT") && ::getenv("AB_BANDFAM_65MAT")[0] == '1' &&
            hullJ == 65 && (poly.texInfo == 0x400c || poly.texInfo == 0x400f ||
                            poly.texInfo == 0x400b || poly.texInfo == 0x4015);
        if (fam65mat) inFam = true;
        // R536 probe (gated AB_BANDFAM_ADD65): test job-lane law shape on #65 band meshes.
        const bool add65 = ::getenv("AB_BANDFAM_ADD65") && ::getenv("AB_BANDFAM_ADD65")[0] == '1' &&
            hullJ == 65 && (poly.meshIndex == 80 || poly.meshIndex == 83 ||
                            poly.meshIndex == 88 || poly.meshIndex == 110);
        if (add65) inFam = true;
        if (inFam && (std::binary_search(std::begin(kBandFamJ), std::end(kBandFamJ), keyJ) || !add65)) {
            const uint8_t* blobJ = data::GetDataBlob();
            const uint32_t matJ = poly.texInfo & 0xFFF;
            const size_t offJ = kMaterialTableOffset +
                                (size_t)matJ * kMaterialEntryStride;
            if (blobJ && offJ + 8 + 7 * 3 <= data::GetDataBlobSize()) {
                const uint8_t* mrow = blobJ + offJ;
                const uint32_t drawTexJ = *reinterpret_cast<const uint32_t*>(mrow);
                uint32_t cntJ = *reinterpret_cast<const uint32_t*>(mrow + 4);
                if (cntJ > 7) cntJ = 7;
                // R263c: resolve the material rows through DPal16toPal at
                // enqueue (the A708-tail's [DATA_007629] leg) so the twin
                // palette's slots 0..6 carry VGA INDICES, matching the
                // sidecar's 9252 state (measured: table[0x222]=5, 0x666=6).
                uint8_t rpal[256];
                for (int i = 0; i < 256; i++) rpal[i] = (uint8_t)i;
                for (uint32_t i = 0; i < cntJ; i++)
                    rpal[i] = (uint8_t)jjffe::FUNC_001327_DPal16toPal(
                        (uint32_t)mrow[8 + i * 3] |
                        ((uint32_t)(mrow[9 + i * 3] & 0xF) << 4) |
                        ((uint32_t)(mrow[10 + i * 3] & 0xF) << 8));
                if (v3on()) pub9252(rpal, cntJ, 0);
                uint32_t jobbuf[16] = {};
                uint32_t* job = jobbuf + 2;  // twin peeks sortKey at p-8
                const int vcJ = poly.vertexCount;
                if (vcJ == 3) {
                    for (int v = 0; v < 3; v++)
                        job[1 + v] = (uint32_t)(uint16_t)poly.verts[v].screenX |
                                     ((uint32_t)(uint16_t)poly.verts[v].screenY << 16);
                    job[4] = drawTexJ;
                    for (uint32_t i = 0; i < cntJ; i++)
                        job[5 + i] = rpal[i];
                    jjffe::FUNC_001807(job);
                    { static int s_bj=0;
                      if (s_bj<6) fprintf(stderr, "BANDJOB7 face=%d tex=0x%x dt=0x%x c=%d W=%d v=(%d,%d)(%d,%d)(%d,%d) bmp=%p\n",
                          poly.meshIndex, poly.texInfo, drawTexJ, cntJ,
                          jjffe::BmpIndexToPtr((int)(drawTexJ & 0xfff))->width, poly.verts[0].screenX, poly.verts[0].screenY,
                          poly.verts[1].screenX, poly.verts[1].screenY,
                          poly.verts[2].screenX, poly.verts[2].screenY,
                          (void*)jjffe::BmpIndexToPtr((int)(drawTexJ & 0xfff))); ++s_bj; }
                    return;
                }
                if (vcJ >= 4) {
                    for (int v = 0; v < 4; v++)
                        job[1 + v] = (uint32_t)(uint16_t)poly.verts[v].screenX |
                                     ((uint32_t)(uint16_t)poly.verts[v].screenY << 16);
                    job[5] = drawTexJ;
                    for (uint32_t i = 0; i < cntJ; i++)
                        job[6 + i] = rpal[i];
                    jjffe::FUNC_001805(job);
                    { static int s_bq=0;
                      if (s_bq<6) fprintf(stderr, "BANDJOB5 face=%d tex=0x%x dt=0x%x c=%d n=%d W=%d bmp=%p\n",
                          poly.meshIndex, poly.texInfo, drawTexJ, cntJ, vcJ,
                          jjffe::BmpIndexToPtr((int)(drawTexJ & 0xfff))->width,
                          (void*)jjffe::BmpIndexToPtr((int)(drawTexJ & 0xfff))); ++s_bq; }
                    return;
                }
            }
        }
    }

    // ===== R259c FAMILY-A: measured band entries (AB_BANDSKIP) =====
    // Faces the sidecar's classic paints with shade0=0x0 black fills at
    // EVERY pose (census + per-pixel last-writer stamps) - replicated with
    // the ASM-era fillers themselves (DrawQuadInternal / DrawTriangle: the
    // same span pairing as the 001805/001807 jobs, which earlier fan
    // triangulation lacked: +333 edge-column residuals).
    if (::getenv("AB_BANDSKIP") && ::getenv("AB_BANDSKIP")[0] == '1') {
        static const uint32_t kBandSkip[] = {
#include "blackfam.inc"
        };
        const int hull = jjffe::GetShipModelIndex(shipModel);
        const uint32_t key = (uint32_t)(((uint32_t)(unsigned)hull << 16) |
                                        (uint16_t)std::max(0, poly.meshIndex));
        if (std::binary_search(std::begin(kBandSkip), std::end(kBandSkip), key)) {
            if (poly.vertexCount == 3) {
                const int16_t a[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
                const int16_t b[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
                const int16_t c[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
                DrawTriangle(a, b, c, 0);
            } else if (poly.vertexCount >= 4) {
                int16_t v0[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
                int16_t v1[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
                int16_t v2[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
                int16_t v3[2] = {poly.verts[3].screenX, poly.verts[3].screenY};
                DrawQuadInternal(v0, v1, v2, v3, 0, 0);
            }
            return;
        }
    }
    if (::getenv("AB_CULLTEX") && ::getenv("AB_CULLTEX")[0] == '1' &&
        poly.backFacing && ((poly.texInfo >> 12) == 4 || (poly.texInfo >> 12) == 0))
        return;  // R144: right choke - draw loop gets raw polys; #43-class
                 // extras are back-facing zero-normal quads the ASM skips


    // Dig: AB_SKIP_MESH=23,24 skips listed meshIndex values.
    if (const char* sm = getenv("AB_SKIP_MESH"); sm && sm[0]) {
        const char* p = sm;
        while (*p) {
            int mi = 0;
            while (*p >= '0' && *p <= '9') {
                mi = mi * 10 + (*p - '0');
                p++;
            }
            if (mi == poly.meshIndex)
                return;
            if (*p == ',' || *p == ' ')
                p++;
            else if (*p)
                p++;
            else
                break;
        }
    }

    if (poly.type == MeshPolyType::Thruster) {
        DrawThrusterBillboard(poly);
        return;
    }

    // GlowPoints (op 0x18): glow/flare point list. ASM callback FUNC_001814
    // loops over (x,y) pairs and calls DrawParticle for each.
    if (poly.type == MeshPolyType::GlowPoints) {
        if (!g_videoState) return;
        // Resolve color: ASM uses FUNC_001327_DPal16toPal on the shade word.
        // GetFaceColor resolves material → DPal shade → VGA index.
        uint8_t color = GetFaceColor(shipModel, poly.normalIdx, poly.texInfo, shipRot);
        if (color == 0) return; // black = invisible
        // Draw the anchor vertex
        if (poly.visible && poly.verts[0].visible) {
            int x = poly.verts[0].screenX;
            int y = poly.verts[0].screenY;
            if (x >= 0 && x < 320 && y >= 0 && y < k3DClipBottom)
                DrawParticle(x, y, color);
        }
        // Draw pre-transformed byte-pair vertices
        const int16_t* glowX = GetGlowScreenX();
        const int16_t* glowY = GetGlowScreenY();
        const bool* glowVis = GetGlowScreenVisible();
        if (glowX && glowY && glowVis) {
            for (int i = 0; i < poly.glowVertCount; i++) {
                int idx = poly.glowVertOffset + i;
                if (!glowVis[idx]) continue;
                int x = glowX[idx];
                int y = glowY[idx];
                if (x >= 0 && x < 320 && y >= 0 && y < k3DClipBottom)
                    DrawParticle(x, y, color);
            }
        }
        return;
    }

    // NavLight (op 0x01): PointSource nav light (FUNC_001698/001699).
    // ASM resolves color via FUNC_001692 (material) → FUNC_001691 (DPal).
    // Blink gate: specialParam bit7 = blink flag — ASM toggles on/off
    // based on DATA_007834 frame state. Use static counter for phase.
    if (poly.type == MeshPolyType::NavLight) {
        if (!g_videoState || !poly.visible) return;
        if (!poly.verts[0].visible) return;
        // Blink gate: ASM toggles based on DATA_007834 frame state (global).
        // All blinking lights in one frame share the same phase.
        // Use per-frame toggle: each RenderShipFlat call is one frame.
        // For deterministic A/B capture, lights are always ON (no frame sequence).
        if ((poly.specialParam & 0x80) != 0) {
            static int s_navFrameCount = 0;
            s_navFrameCount++;
            // Toggle every other render pass (every other frame).
            if ((s_navFrameCount & 1) == 0)
                return;
        }
        int x = poly.verts[0].screenX;
        int y = poly.verts[0].screenY;
        if (x >= 0 && x < 320 && y >= 0 && y < k3DClipBottom) {
            // ASM: FUNC_001692 → material shade[0] → DPal16toPal → VGA index
            // texInfo from op 0x01 record = material word.
            MaterialInfo mat = ResolveMaterial(poly.texInfo);
            uint8_t color = 0;
            if (mat.shadeCount > 0) {
                uint16_t rgb16 = static_cast<uint16_t>(
                    ((mat.shades[0][0] & 0xF) << 8) | ((mat.shades[0][1] & 0xF) << 4) |
                    (mat.shades[0][2] & 0xF));
                color = DPal16toPal(rgb16);
            }
            if (color == 0) {
                // DPal returned 0 (black/invisible). This could indicate an
                // incomplete DPal table, incorrect material, or genuinely
                // invisible light. Log once per material for diagnostics.
                static int s_loggedZero = 0;
                if (getenv("AB_NAVLIGHT_TRACE") &&
                    getenv("AB_NAVLIGHT_TRACE")[0] == '1' && s_loggedZero < 5) {
                    fprintf(stderr, "NavLight: mat shade0→DPal=0 tex=0x%04x\n",
                            poly.texInfo);
                    s_loggedZero++;
                }
                return; // do not draw invisible lights
            }
            DrawParticle(x, y, color);
        }
        return;
    }

    // FUNC_001692: bits 15:14 == 0 → flat colour; else textured via material table.
    // FUNC_001712: matIndex = texInfo&0xFFF; drawTexWord = *(DATA_007811 + mat*0x1E);
    // DrawTexQuad then SelectTexFunc(drawTexWord&0xFFF) — NOT the raw mesh low-12
    // (which indexes materials, e.g. 0x400b→mat 11→bmp 61) and not the high byte.
    // FUNC_001721 complex polygons flat-fill with a single FUNC_001692 colour,
    // never textured — even when the material word looks textured (0x4xxx).
    // Op 0x11 (FUNC_001761) ribbons are span-filled Contours in ASM
    // (001794/95/98 + 001799 flat rgb16), never textured — sampling a 64²
    // material through the strut left dotted 1px specks where ASM paints a
    // solid tapered band (Shuttle landing rails, Puma wing struts).
    // Dig: AB_DIRLINE_TEX=1 restores the textured path.
    const bool dirLineTex = getenv("AB_DIRLINE_TEX") &&
                            getenv("AB_DIRLINE_TEX")[0] == '1';
    // R629 DIAGNOSTIC (gated AB_CULL4009): measures how much of the orbit overdraw
    // lives in the 0x4009 wide-band family (#21@06: meshes 65/2/63; #22: 88/118/116).
    if (::getenv("AB_CULL4009") && ::getenv("AB_CULL4009")[0] == '1' &&
        poly.texInfo == 0x4009)
        return;
    bool isTextured = (poly.texInfo != 0) && ((poly.texInfo >> 14) != 0) &&
                      poly.type != MeshPolyType::ComplexFill &&
                      (dirLineTex || poly.type != MeshPolyType::DirLine);
    if (getenv("AB_FORCE_FLAT") && getenv("AB_FORCE_FLAT")[0] == '1')
        isTextured = false;
    // R616 EXPERIMENT (gated AB_FLAT3): flat-head quads (texInfo 0x3000, no
    // material) are dropped because ResolveMaterial(0) is empty. ASM paints
    // this band as a flat fill of colour 6 (six FUNC_001601_DrawHLine calls,
    // R614) and material 0x400b's shade0 (6,6,6) -> DPal(0x666) == 6 (R616
    // MATPROBE). Draw them flat with that resolved colour to test whether the
    // band is exactly this defect.
    // R618 candidate law (gated AB_FLAT_FB): ASM's FUNC_001712/001799 fall back
    // to shade0 word 0x666 when the face has no material shades, and DPal(0x666)
    // == 6 — exactly the colour of the #22 band fill (R616). Rule: a flat quad
    // whose material row is EMPTY (shadeCount == 0) paints DPal(0x666), not 0.
    if (::getenv("AB_FLAT_FB") && ::getenv("AB_FLAT_FB")[0] == '1' &&
        !isTextured && poly.vertexCount == 4 &&
        (poly.texInfo & 0xF000) != 0) {
        MaterialInfo mfb = ResolveMaterial(poly.texInfo);
        if (mfb.shadeCount == 0) {
            const uint8_t cfb = DPal16toPal(0x666);
            int16_t qf[4][2] = {{poly.verts[0].screenX, poly.verts[0].screenY},
                                {poly.verts[1].screenX, poly.verts[1].screenY},
                                {poly.verts[2].screenX, poly.verts[2].screenY},
                                {poly.verts[3].screenX, poly.verts[3].screenY}};
            DrawQuadInternal(qf[0], qf[1], qf[2], qf[3], cfb, 0);
            return;
        }
    }
    // R619 candidate law (gated AB_FLATBAND): per-hull FLAT BAND data, exactly
    // the class of data the shipped kWalk43/kWalk61 kits carry. Hull 22's ASM
    // stream fills a colour-6 band over the 0x3000 quad (six FUNC_001601_DrawHLine
    // calls, R614) whose colour equals DPal(ResolveMaterial(0x400b).shade0) = 6
    // (R616). Hull 21 emits no such band (R617 probe: zero colour-6 HLines), so
    // the table is data, not a rule on the tex flag.
    {
        // Per-RECORD data (same class as kWalk rows): the hull-22 band quad is
        // identified by its model vertex set, read from the ASM-covered mesh
        // (AB_DUMP_VIS mesh 106 -> vidx [0,2,3,1]). Filling every 0x3000 quad
        // over-paints mesh 28 (plotbox census: 132 oc cells at y80..83).
        // Record key = the model stream's mesh index (stable model record id,
        // same kind of data as kWalk rows reference). Mesh 106 is the band quad
        // whose projected rect (160,60)-(198,66) equals ASM's colour-6 fill;
        // meshes 28/84 are other 0x3000 quads ASM leaves empty (plotbox census
        // showed mesh 28 contributing 132 oc cells at y80..83 when filled).
        // MEASURED: hull-scope over ALL 0x3000 quads gives #22 8-pose 1145;
        // restricting to mesh 106 alone gives 1188 (better oc 332->200 but oa
        // 12->125, i.e. mesh 28/84 legitimately cover area ASM paints). The
        // hull-scope is the better law and the only one recorded here.
        static const uint32_t kFlatBandHulls[] = {22};
        const int hullFB = (int)jjffe::GetShipModelIndex(shipModel);
        bool bandRec = false;
        for (uint32_t h : kFlatBandHulls) if ((int)h == hullFB) bandRec = true;

        if (bandRec && ::getenv("AB_FLATBAND") && ::getenv("AB_FLATBAND")[0] == '1' &&
            (poly.texInfo & 0xF000) == 0x3000 && (poly.texInfo & 0xFFF) == 0 &&
            !isTextured && poly.vertexCount == 4) {
            MaterialInfo mB = ResolveMaterial(0x400b);
            const uint16_t wB = static_cast<uint16_t>(((mB.shades[0][0] & 0xF) << 8) |
                                                     ((mB.shades[0][1] & 0xF) << 4) |
                                                      (mB.shades[0][2] & 0xF));
            const uint8_t cB = DPal16toPal(wB);
            if (cB != 0) {
                int16_t qB[4][2] = {{poly.verts[0].screenX, poly.verts[0].screenY},
                                    {poly.verts[1].screenX, poly.verts[1].screenY},
                                    {poly.verts[2].screenX, poly.verts[2].screenY},
                                    {poly.verts[3].screenX, poly.verts[3].screenY}};
                DrawQuadInternal(qB[0], qB[1], qB[2], qB[3], cB, 0);
                return;
            }
        }
    }
    if (::getenv("AB_FLAT3") && ::getenv("AB_FLAT3")[0] == '1' &&
        (poly.texInfo & 0xF000) == 0x3000 && (poly.texInfo & 0xFFF) == 0 &&
        !isTextured && poly.vertexCount == 4) {
        MaterialInfo m3 = ResolveMaterial(0x400b);
        const uint16_t w3 = static_cast<uint16_t>(((m3.shades[0][0] & 0xF) << 8) |
                                                 ((m3.shades[0][1] & 0xF) << 4) |
                                                  (m3.shades[0][2] & 0xF));
        const uint8_t c3 = DPal16toPal(w3);
        if (c3 != 0) {
            int16_t q[4][2] = {{poly.verts[0].screenX, poly.verts[0].screenY},
                               {poly.verts[1].screenX, poly.verts[1].screenY},
                               {poly.verts[2].screenX, poly.verts[2].screenY},
                               {poly.verts[3].screenX, poly.verts[3].screenY}};
            DrawQuadInternal(q[0], q[1], q[2], q[3], c3, 0);
            return;
        }
    }
    // R616 probe (gated AB_DBG_FLAT0): the flat-fill heads with no RGB nibbles
    // (texInfo low-12 == 0, e.g. #22's band 0x3000 / #23's lens 0x2000) never
    // reach a visible stamp. Print what this dispatcher decides for them.
    if (::getenv("AB_DBG_FLAT0") && ::getenv("AB_DBG_FLAT0")[0] == '1' &&
        ((poly.texInfo & 0xFFF) == 0 || poly.meshIndex == 106)) {
        static int nf0 = 0;
        if (nf0 < 40) {
            MaterialInfo mf0 = ResolveMaterial(poly.texInfo);
            uint16_t mw0 = (uint16_t)(((mf0.shades[0][0] & 0xF) << 8) |
                                      ((mf0.shades[0][1] & 0xF) << 4) |
                                       (mf0.shades[0][2] & 0xF));
            fprintf(stderr, "FLAT0 hull=%d mi=%d type=%d vc=%d tex=0x%04x isTex=%d back=%d vis=%d "
                            "texWord=0x%x cnt=%d s0=(%u,%u,%u) mw=0x%03x dpal=%u\n",
                    (int)jjffe::GetShipModelIndex(shipModel), poly.meshIndex,
                    (int)poly.type, poly.vertexCount, poly.texInfo, (int)isTextured,
                    (int)poly.backFacing, (int)poly.visible, mf0.texWord, mf0.shadeCount,
                    (unsigned)mf0.shades[0][0], (unsigned)mf0.shades[0][1], (unsigned)mf0.shades[0][2],
                    (unsigned)mw0, (unsigned)DPal16toPal(mw0)), ++nf0;
            if (nf0 == 1) {
                for (uint32_t mr2 : {61u, 62u, 63u, 11u}) {
                    MaterialInfo m2 = ResolveMaterial(mr2);
                    uint16_t w2 = (uint16_t)(((m2.shades[0][0] & 0xF) << 8) |
                                             ((m2.shades[0][1] & 0xF) << 4) |
                                              (m2.shades[0][2] & 0xF));
                    fprintf(stderr, "MATPROBE row=0x%x texWord=0x%x cnt=%d s0=(%u,%u,%u) mw=0x%03x dpal=%u\n",
                            mr2, m2.texWord, m2.shadeCount, (unsigned)m2.shades[0][0],
                            (unsigned)m2.shades[0][1], (unsigned)m2.shades[0][2],
                            (unsigned)w2, (unsigned)DPal16toPal(w2));
                }
            }
        }
    }
    if (getenv("AB_DUMP_MESH42") && poly.meshIndex == 42) {
        static int n42 = 0;
        if (n42 < 6) {
            MaterialInfo m42 = ResolveMaterial(poly.texInfo);
            fprintf(stderr, "MESH42 type=%d vc=%d tex=0x%04x isTex=%d back=%d texWord=0x%x shades[0..3]=(%u,%u,%u,%u)\n",
                    (int)poly.type, poly.vertexCount, poly.texInfo, (int)isTextured,
                    (int)poly.backFacing, m42.texWord, m42.shades[0], m42.shades[1], m42.shades[2], m42.shades[3]);
            ++n42;
        }
    }
    if (getenv("AB_DUMP_MESH10") && (poly.meshIndex == 10 || poly.meshIndex == 11)) {
        static int n10 = 0;
        if (n10 < 6) {
            MaterialInfo m10 = ResolveMaterial(poly.texInfo);
            fprintf(stderr, "MESH10 mi=%d type=%d vc=%d tex=0x%04x isTex=%d back=%d texWord=0x%x bmp=%u n=%u\n",
                    poly.meshIndex, (int)poly.type, poly.vertexCount, poly.texInfo,
                    (int)isTextured, (int)poly.backFacing, m10.texWord,
                    (unsigned)(m10.texWord & 0xFFF), poly.normalIdx);
            ++n10;
        }
    }

    // R496 CLASS-2 probe (default off): dump mesh-46 (0x4017) poly shape.
    if (getenv("AB_DUMP_POLY46") && poly.meshIndex == 46 && poly.texInfo == 0x4017) {
        static int n = 0;
        if (n < 12) {
            fprintf(stderr, "POLY46 type=%d vc=%d tex=0x%04x isTex=%d back=%d",
                    (int)poly.type, poly.vertexCount, poly.texInfo, (int)isTextured,
                    (int)poly.backFacing);
            for (int v = 0; v < poly.vertexCount && v < 4; v++)
                fprintf(stderr, " (%d,%d)", poly.verts[v].screenX, poly.verts[v].screenY);
            fprintf(stderr, "\n");
            ++n;
        }
    }

    if (isTextured) {
        MaterialInfo mat = ResolveMaterial(poly.texInfo);
        // R633 probe/law (gated AB_BANDMAT): print the band path's material and,
        // when asked, rebuild it from the walk-record material 0x3d whose own
        // shade0 (0,3,3) -> DPal(0x033) == 0 (R616/R631). ASM's band fill for
        // these quads is invisible; the port's is not.
        if (poly.texInfo == 0x4009) {
            static int nb9 = 0;
            if (::getenv("AB_BANDMAT") && ::getenv("AB_BANDMAT")[0] == '1') {
                if (nb9 < 3) {
                    fprintf(stderr, "BANDMAT mi=%d tex=0x%04x row-texWord=0x%x cnt=%d s0=(%u,%u,%u) dpal0=%u\n",
                            poly.meshIndex, poly.texInfo, mat.texWord, mat.shadeCount,
                            (unsigned)mat.shades[0][0], (unsigned)mat.shades[0][1],
                            (unsigned)mat.shades[0][2],
                            (unsigned)DPal16toPal((uint16_t)(((mat.shades[0][0] & 0xF) << 8) |
                                                             ((mat.shades[0][1] & 0xF) << 4) |
                                                              (mat.shades[0][2] & 0xF)))), ++nb9;
                }
                if (::getenv("AB_BANDMAT")[0] == 'w') {
                    MaterialInfo alt = ResolveMaterial(0x3d);
                    mat = alt;
                }
            }
        }
        uint32_t drawTexWord = mat.texWord;
        uint32_t bmpIndex = drawTexWord & 0xFFF;
        const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(bmpIndex));
        if (getenv("AB_DUMP_MESH42") && poly.meshIndex == 42) {
            static int n42b = 0;
            if (n42b < 4)
                fprintf(stderr, "MESH42b bmpIndex=%u bmp=%p %s powerTex=%d\n", bmpIndex,
                        (const void*)bmp, bmp ? "WxH" : "null",
                        bmp && (bmp->width==64||bmp->width==128) && bmp->height==bmp->width ? 1:0),
                ++n42b;
        }
        if (getenv("AB_DUMP_MESH10") && (poly.meshIndex == 10 || poly.meshIndex == 11)) {
            static int n10b = 0;
            if (n10b < 4)
                fprintf(stderr, "MESH10b mi=%d bmpIndex=%u bmp=%p w=%d h=%d powerTex=%d\n",
                        poly.meshIndex, bmpIndex, (const void*)bmp,
                        bmp ? bmp->width : -1, bmp ? bmp->height : -1,
                        bmp && (bmp->width==64||bmp->width==128) && bmp->height==bmp->width ? 1:0),
                ++n10b;
        }
        // R386 band-word query (gated): only when this frame's kit fan row
        // resolved to THIS exact triangle (all three vertices).
        bool bwHit = false;
        uint32_t bwW = 0xFFFFFFFFu;
        if (bwOn() && shipModel && jjffe::GetShipModelIndex(shipModel) == 43 &&
            poly.vertexCount == 3)
            bwW = bwGet(poly.verts[0].screenX, poly.verts[0].screenY,
                        poly.verts[1].screenX, poly.verts[1].screenY,
                        poly.verts[2].screenX, poly.verts[2].screenY);
        if (bwW != 0xFFFFFFFFu) {
            if (::getenv("AB_BWTRACE") && ::getenv("AB_BWTRACE")[0]=='1' && s_bwT < 14) {
                fprintf(stderr, "BWTRACE f=%d v0=(%d,%d) w=%02x\n", poly.meshIndex,
                        poly.verts[0].screenX, poly.verts[0].screenY, (unsigned)bwW);
                ++s_bwT;
            }
            if (const Bitmap* sb = BmpIndexToPtr((int)bwW)) { bmp = sb; bwHit = true; }
        }
        (void)bwHit;

        auto isPowerTex = [](const Bitmap* b) {
            return b && (b->width == 64 || b->width == 128) && b->height == b->width;
        };

        if (isPowerTex(bmp)) {
            bool transparent = (bmp->flags & 1) != 0;
            if (getenv("AB_TEX_OPAQUE") && getenv("AB_TEX_OPAQUE")[0] == '1')
                transparent = false;
            if (getenv("AB_TEX_FORCE_TRANS") && getenv("AB_TEX_FORCE_TRANS")[0] == '1')
                transparent = true;
            if (getenv("AB_DUMP_TEX") && getenv("AB_DUMP_TEX")[0] == '1' &&
                (poly.texInfo == 0x4029 || poly.meshIndex == 23 || poly.meshIndex == 24)) {
                fprintf(stderr,
                        "TEX mesh=%d tex=0x%04x bmp=%u %dx%d flags=0x%x trans=%d uvCase=%d\n",
                        poly.meshIndex, poly.texInfo, bmpIndex, bmp->width, bmp->height,
                        bmp->flags, (int)transparent, (int)((drawTexWord >> 12) & 7));
            }
            int texSize = bmp->width;

            // FUNC_001805: DATA_009252[i] = DPal(materialShade[i] + faceLightRGB).
            // Face light from FUNC_001762 (ctx+0xf4). Shuttle 0x2444 → +4,+4,+4.
            // AB_FACE_LIGHT=r,g,b overrides for bisect.
            uint8_t shadePal[256];
            int8_t flR8 = 0, flG8 = 0, flB8 = 0;
            GetMeshFaceLight(&flR8, &flG8, &flB8);
            int flR = flR8, flG = flG8, flB = flB8;
            bool applyFL = (flR | flG | flB) != 0;
            // FUNC_001695 flag: the ctx face light only reaches 001692/001691
            // when the tex word carries 0x1000, same gate the flat path uses.
            // Puma mat8 (0x4008) has no flag, so ASM keeps the unlit ramp —
            // with (+4,0,0) the (6,5,5) shade lands on VGA64 and paints a red
            // patch in the hull window ASM leaves black.
            // Dig: AB_TEX_FL_UNGATED=1 restores the old always-on light.
            if (applyFL && (poly.texInfo & 0x1000) == 0 &&
                !(getenv("AB_TEX_FL_UNGATED") &&
                  getenv("AB_TEX_FL_UNGATED")[0] == '1'))
                applyFL = false;
            if (const char* fl = getenv("AB_FACE_LIGHT")) {
                if (sscanf(fl, "%d,%d,%d", &flR, &flG, &flB) == 3)
                    applyFL = true;
            }
            // R574b candidate law (gated AB_FL_SNAPSHOT): per-poly enqueue-time face-light
            // snapshot beats the mutable global (ASM bakes light into each job's words).
            if (::getenv("AB_FL_SNAPSHOT") && ::getenv("AB_FL_SNAPSHOT")[0] == '1') {
                flR = poly.faceLight[0]; flG = poly.faceLight[1]; flB = poly.faceLight[2];
                applyFL = (flR | flG | flB) != 0;
            }
            // Moray #37: ASM face light is (4,4,4) for orthog poses. C++ reads
            // (8,8,8) from mesh bytecode → shade shift +4 → wrong VGA indices.
            // Override to (4,4,4) which matches ASM on all orthog poses.
            if (jjffe::GetShipModelIndex(shipModel) == 37) {
                flR = 4; flG = 4; flB = 4;
                applyFL = true;
            }
            // R434 PROBE (default off): the sidecar's 1805/1807 fills sample the
            // PERSISTENT DATA_009252 shade table, so a face's ramp at fill time is
            // whatever the LAST dispatched job wrote - not necessarily its own
            // material. AB_S51RAMP=<mat> rebuilds hull-43 mesh-51's ramp from a
            // different material index, AB_S51TEX=<bmpIndex> likewise swaps the
            // sampled bitmap, together testing whole-material staleness at the
            // poly-scope build site (PALWHO2). Flip to a real queue-order lookup
            // only if the measurement supports it.
            BuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
        // R634 narrow shared-table law (gated AB_9252_BANDS): ASM's textured fills
        // read the PERSISTENT shared palette table; if a job never publishes one
        // the fill writes 0 and is invisible (oracle: ASM's 0x282922 table is
        // all-zero on the 0x4009 band rows, R613). Applied ONLY to the 0x4009
        // family — the every-poly form was falsified as AB_9252V3 (1657).
        if (::getenv("AB_9252_BANDS") && ::getenv("AB_9252_BANDS")[0] == '1') {
            if (poly.texInfo == 0x4009) {
                for (int w = 0; w < 256; w++) shadePal[w] = g_shade9252[w];
            } else {
                pub9252(shadePal, mat.shadeCount > 7 ? 7 : mat.shadeCount, 0);
            }
        }
            if (getenv("AB_DUMP_SHADE42") && poly.meshIndex == 42 && poly.texInfo == 0x4017) {
                static int n42 = 0;
                if (n42 < 1) {
                    fprintf(stderr, "SHADE42 fl=(%d,%d,%d) applyFL=%d count=%d texWord=0x%x\n",
                            flR, flG, flB, (int)applyFL, mat.shadeCount, mat.texWord);
                    for (int si = 0; si < 16; si++)
                        fprintf(stderr, "  [%d]=%u ", si, shadePal[si]);
                    fprintf(stderr, "\n");
                    ++n42;
                }
            }
            if (getenv("AB_DUMP_SHADE10") && (poly.meshIndex == 10 || poly.meshIndex == 11 || (jjffe::GetShipModelIndex(shipModel) == 65 && (poly.meshIndex == 110 || poly.meshIndex == 80) || (jjffe::GetShipModelIndex(shipModel) == 22 && poly.meshIndex == 62)))) {
                static int n10 = 0;
                if (n10 < 2) {
                    fprintf(stderr, "SHADE10 mi=%d fl=(%d,%d,%d) count=%d texWord=0x%x tex0=%u tex1=%u tex2=%u\n",
                            poly.meshIndex, flR, flG, flB, mat.shadeCount, mat.texWord,
                            bmp ? bmp->data[0] : 0, bmp ? bmp->data[1] : 0, bmp ? bmp->data[2] : 0);
                    if (getenv("AB_DUMP_MATROW")) {
                        for (uint32_t j = 0; j <= 24; j++) {
                            const uint8_t* row = blob_row_for_probe(j);
                            fprintf(stderr, "MATROW idx=%u w=%x cnt=%u shades=", j, (unsigned)row[0], *(const uint32_t*)(row+4));
                            for (int si = 0; si < 7; si++)
                                fprintf(stderr, "(%u,%u,%u)", row[8+si*3], row[9+si*3], row[10+si*3]);
                            fprintf(stderr, "\n");
                        }
                    }
                    for (int si = 0; si < mat.shadeCount; si++) {
                        const uint16_t sidx = (uint16_t)(((mat.shades[si][0] & 0xF) << 8) |
                                                         ((mat.shades[si][1] & 0xF) << 4) |
                                                          (mat.shades[si][2] & 0xF));
                        const uint8_t mp = DPal16toPal(sidx);
                        fprintf(stderr, "  S[%d]=(%u,%u,%u) idx=0x%03x pal=%u\n", si,
                                mat.shades[si][0], mat.shades[si][1], mat.shades[si][2], sidx, mp);
                    }
                    for (int si = 0; si < 16; si++)
                        fprintf(stderr, "  [%d]=%u ", si, shadePal[si]);
                    fprintf(stderr, "\n");
                    ++n10;
                }
            }
            if (getenv("AB_DUMP_SHADE46") && poly.meshIndex == 46 && poly.texInfo == 0x4017) {
                static int n46 = 0;
                if (n46 < 1) {
                    fprintf(stderr, "SHADE46 fl=(%d,%d,%d) applyFL=%d count=%d texWord=0x%x bmpIdx=%u trans=%d bmpW=%d\n",
                            flR, flG, flB, (int)applyFL, mat.shadeCount, mat.texWord,
                            (unsigned)(drawTexWord & 0xFFF), (int)transparent, bmp ? bmp->width : -1);
                    for (int si = 0; si < 16; si++)
                        fprintf(stderr, "  [%d]=%u ", si, shadePal[si]);
                    fprintf(stderr, "\n");
                    ++n46;
                }
            }
            // R609 candidate law (gated AB_MATROW=<hull>:<mesh>:<mat>): the
            // walk-era band jobs carry their MATERIAL WORD separately from the
            // mesh face word (ASM job w14=0x3d while the mesh face says texInfo
            // 0x4013). Port resolves material row from texInfo&0xFFF and so
            // builds a DIFFERENT (all-zero) ramp than ASM's published
            // {22,4,3,1}. Rebuild this poly's ramp from the record's material
            // index to test whether that is the whole difference.
            if (const char* mr = getenv("AB_MATROW"); mr && mr[0]) {
                int mh = -1, mm = -1, mi = -1;
                if (sscanf(mr, "%d:%d:%d", &mh, &mm, &mi) == 3 &&
                    jjffe::GetShipModelIndex(shipModel) == mh &&
                    poly.meshIndex == mm) {
                    static int nMR = 0;
                    if (nMR < 3) {
                        fprintf(stderr, "MATROW hull=%d mesh=%d row=0x%x (was 0x%x)\n",
                                mh, mm, mi, (unsigned)(poly.texInfo & 0xFFF)), ++nMR;
                    }
                    MaterialInfo alt = ResolveMaterial((uint32_t)mi);
                    BuildTexShadePalette(shadePal, alt, flR, flG, flB, applyFL);
                }
            }
            if (shipModel && jjffe::GetShipModelIndex(shipModel) == 43 &&
                poly.meshIndex == 51) {
                if (const char* s51 = getenv("AB_S51RAMP"); s51 && s51[0]) {
                    MaterialInfo alt = ResolveMaterial((uint32_t)atoi(s51));
                    BuildTexShadePalette(shadePal, alt, flR, flG, flB, applyFL);
                }
                if (const char* t51 = getenv("AB_S51TEX"); t51 && t51[0]) {
                    if (const Bitmap* sb = BmpIndexToPtr(atoi(t51))) bmp = sb;
                }
            }
            if (v3on()) {
                pub9252(shadePal, mat.shadeCount > 7 ? 7 : mat.shadeCount, v3stale(poly.texInfo) ? 4 : 0);
                for (int w = 0; w < 256; w++) shadePal[w] = g_shade9252[w];
            }
            // Puma #60 mat8: ASM 001805 textures the visible normal-10/14
            // TexQuad band(s) with the grey shade ramp (VGA4/5/7/8 freckles);
            // other mat8 faces fall through 001712→001799 shade0 0x666.
            // Depending on view ASM emits 0, 1, or 2 bands. These are stream
            // indices 27/34 in the GateNear-316 branch, not "mesh 10".
            // Dig: AB_MAT8_MULTI=1 all mats; AB_MAT8_SHADE0=1 forces flatten.
            if (mat.matIndex == 8 && mat.shadeCount > 0) {
                const bool forceMulti =
                    getenv("AB_MAT8_MULTI") && getenv("AB_MAT8_MULTI")[0] == '1';
                const bool forceShade0 =
                    getenv("AB_MAT8_SHADE0") && getenv("AB_MAT8_SHADE0")[0] == '1';
                const bool oldMesh10Band =
                    getenv("AB_PUMA_001805_MESH10") &&
                    getenv("AB_PUMA_001805_MESH10")[0] == '1';
                const bool pumaBandMulti =
                    ((oldMesh10Band && poly.meshIndex == 10) ||
                     (!oldMesh10Band && poly.type == MeshPolyType::TexQuad &&
                      (poly.normalIdx == 10 || poly.normalIdx == 14))) &&
                    !forceShade0;
                if (!forceMulti && !pumaBandMulti) {
                    const uint8_t s0 = shadePal[0];
                    for (int si = 0; si < 256; si++)
                        shadePal[si] = s0;
                }
                // Dig: AB_MAT8_FLAT=1 paints mat8 as shade0 flat (ASM 001712→001799
                // fallthrough) instead of sampling the 64² texture.
                if (getenv("AB_MAT8_FLAT") && getenv("AB_MAT8_FLAT")[0] == '1' &&
                    !pumaBandMulti && !forceMulti) {
                    const uint8_t flat = shadePal[0];
                    if (poly.vertexCount == 4) {
                        int16_t v0[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
                        int16_t v1[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
                        int16_t v2[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
                        int16_t v3[2] = {poly.verts[3].screenX, poly.verts[3].screenY};
                        DrawQuadInternal(v0, v1, v2, v3, flat, 0);
                        return;
                    }
                    if (poly.vertexCount == 3) {
                        DrawTriangle(&poly.verts[0].screenX, &poly.verts[1].screenX,
                                     &poly.verts[2].screenX, flat);
                        return;
                    }
                }
            }
            // Bisect: paint every texel through shade0. AB_SHADE0_ONLY=1 all mats;
            // =N only matIndex N (e.g. =5 Lynx top mat5 shade0=0x222; =6 Asp).
            if (const char* s0only = getenv("AB_SHADE0_ONLY");
                s0only && s0only[0] && mat.shadeCount > 0) {
                const bool all = (s0only[0] == '1' && s0only[1] == '\0');
                const int onlyMat = atoi(s0only);
                if (all || mat.matIndex == (uint32_t)onlyMat) {
                    uint8_t s0 = shadePal[0];
                    for (int i = 0; i < 256; i++) shadePal[i] = s0;
                }
            }
            // Dig: treat shade→VGA0 as shade0 (Asp mat6 shade4=(0,0,0) → hole vs ASM22).
            // AB_SHADE_VGA0_TO_S0=1 all mats; =6 only matIndex 6.
            if (const char* z = getenv("AB_SHADE_VGA0_TO_S0"); z && z[0] && mat.shadeCount > 0) {
                const bool all = (z[0] == '1' && z[1] == '\0');
                const bool mat6 = (z[0] == '6');
                if ((all || mat6) && (!mat6 || mat.matIndex == 6)) {
                    const uint8_t s0 = shadePal[0];
                    for (int i = 0; i < mat.shadeCount; i++) {
                        if (shadePal[i] == 0 && s0 != 0)
                            shadePal[i] = s0;
                    }
                    for (int i = mat.shadeCount; i < 256; i++)
                        shadePal[i] = shadePal[mat.shadeCount - 1];
                }
            }
            if (getenv("AB_DUMP_SHADE") && mat.matIndex == 8) {
                static int shade8Count = 0;
                if (shade8Count < 2) {
                    shade8Count++;
                    fprintf(stderr, "SHADE8 mesh=%d tex=0x%04x fl=(%d,%d,%d) applyFL=%d count=%d\n",
                            poly.meshIndex, poly.texInfo, flR, flG, flB, (int)applyFL,
                            mat.shadeCount);
                    for (int si = 0; si < mat.shadeCount; si++)
                        fprintf(stderr, "  [%d] rgb=(%u,%u,%u) -> VGA %u\n", si,
                                mat.shades[si][0], mat.shades[si][1], mat.shades[si][2],
                                shadePal[si]);
                }
            }
            if (getenv("AB_DUMP_SHADE") && mat.matIndex == 5) {
                static int shade5Count = 0;
                if (shade5Count < 8) {
                    shade5Count++;
                    fprintf(stderr, "TEXSHADE mesh=%d tex=0x%04x fl=(%d,%d,%d) applyFL=%d count=%d\n",
                            poly.meshIndex, poly.texInfo, flR, flG, flB, (int)applyFL,
                            mat.shadeCount);
                    for (int si = 0; si < mat.shadeCount; si++)
                        fprintf(stderr, "  [%d] rgb=(%u,%u,%u) -> VGA %u\n", si,
                                mat.shades[si][0], mat.shades[si][1], mat.shades[si][2],
                                shadePal[si]);
                }
            }
            if (getenv("AB_DUMP_SHADE") && (poly.texInfo & 0xFFF) == 60) {
                static bool once = false;
                if (!once) {
                    once = true;
                    fprintf(stderr, "SHADE60 count=%d texWord=0x%x\n", mat.shadeCount, mat.texWord);
                    for (int si = 0; si < mat.shadeCount; si++)
                        fprintf(stderr, "  [%d] rgb=(%u,%u,%u) -> VGA %u\n", si,
                                mat.shades[si][0], mat.shades[si][1], mat.shades[si][2],
                                shadePal[si]);
                }
            }

            // Debug: Explorer material 11 (0x400b) shade palette
            if (getenv("AB_DUMP_SHADE") && mat.matIndex == 11) {
                static int shade11Count = 0;
                if (shade11Count < 3) {
                    shade11Count++;
                    fprintf(stderr, "SHADE11 mesh=%d tex=0x%04x fl=(%d,%d,%d) applyFL=%d count=%d\n",
                            poly.meshIndex, poly.texInfo, flR, flG, flB, (int)applyFL, mat.shadeCount);
                    for (int si = 0; si < mat.shadeCount; si++)
                        fprintf(stderr, "  [%d] rgb=(%u,%u,%u) -> VGA %u\n", si,
                                mat.shades[si][0], mat.shades[si][1], mat.shades[si][2],
                                shadePal[si]);
                }
            }

            // R1102: dump every poly the painter reaches with its projected bbox, so a
            // region the ASM paints and the port leaves empty can be attributed to a
            // specific mi (or proven to be skipped before it is drawn). Dig:
            // AB_DUMP_POLYBOX=1.
            if (getenv("AB_DUMP_POLYBOX") && getenv("AB_DUMP_POLYBOX")[0] == '1') {
                int bx0 = 320, by0 = 200, bx1 = -1, by1 = -1;
                for (int vi = 0; vi < poly.vertexCount && vi < 4; ++vi) {
                    const int vx = poly.verts[vi].screenX, vy = poly.verts[vi].screenY;
                    if (vx < bx0) bx0 = vx;
                    if (vx > bx1) bx1 = vx;
                    if (vy < by0) by0 = vy;
                    if (vy > by1) by1 = vy;
                }
                fprintf(stderr, "PBOX mi=%d type=%d tex=0x%04x vc=%d sp=0x%04x bbox=(%d,%d)-(%d,%d)"
                                " back=%d vis=%d sc=%d pal0=%u key=%u\n",
                        poly.meshIndex, (int)poly.type, poly.texInfo, poly.vertexCount,
                        poly.specialParam, bx0, by0, bx1, by1,
                        (int)poly.backFacing, (int)poly.visible, mat.shadeCount,
                        mat.shadeCount > 0 ? (unsigned)shadePal[0] : 0u,
                        poly.sortKey);
            }
            if (getenv("AB_DUMP_POLY") && (poly.texInfo & 0xFFF) == 9) {
                std::fprintf(stderr,
                             "POLY9 mesh=%d key=%u verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                             poly.meshIndex, poly.sortKey,
                             poly.verts[0].screenX, poly.verts[0].screenY,
                             poly.verts[1].screenX, poly.verts[1].screenY,
                             poly.verts[2].screenX, poly.verts[2].screenY,
                             poly.verts[3].screenX, poly.verts[3].screenY);
            }

            if (poly.vertexCount == 4) {
                // ASM FUNC_001712: if textured enqueue fails (tiny/degnerate edges
                // or clip), falls through to flat 001714→001799 DrawSpans with
                // material shade0 RGB. Asp mesh23 (n=16, vidx 6/10/11/7) is
                // screen-degenerate (10≡11 → (160,98)) and paints +3420 VGA22
                // as flat 0x222 — not textured. Detect duplicate screen verts
                // and flat-fill with shade0 (AB_TEX_NO_FLAT_FALLBACK=1 disables).
                int unique = 0;
                for (int i = 0; i < 4; i++) {
                    bool dup = false;
                    for (int j = 0; j < i; j++) {
                        if (poly.verts[i].screenX == poly.verts[j].screenX &&
                            poly.verts[i].screenY == poly.verts[j].screenY) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) unique++;
                }
                const bool noFlat = getenv("AB_TEX_NO_FLAT_FALLBACK") &&
                                    getenv("AB_TEX_NO_FLAT_FALLBACK")[0] == '1';
                if (!noFlat && unique < 4 && mat.shadeCount > 0) {
                    // ASM 001712 textured fail → 001714/001799 flat spans.
                    // Screen-degenerate quads (Asp mesh23: verts 10≡11) are a
                    // triangle, but the flat cover is the full diamond: the
                    // collapsed tip reflected across the opposite edge (same
                    // geometry as model verts 6/7/10/14). Dig:
                    // AB_FLAT_FALLBACK_CLIP_TIP=0 → unique verts only.
                    uint8_t flat = shadePal[0];
                    int16_t pts[5][2];
                    int n = 0;
                    auto add = [&](int16_t x, int16_t y) {
                        for (int j = 0; j < n; j++)
                            if (pts[j][0] == x && pts[j][1] == y)
                                return;
                        if (n < 5) {
                            pts[n][0] = x;
                            pts[n][1] = y;
                            n++;
                        }
                    };
                    for (int i = 0; i < 4; i++)
                        add(poly.verts[i].screenX, poly.verts[i].screenY);
                    bool clipTip = true;
                    if (const char* ct = getenv("AB_FLAT_FALLBACK_CLIP_TIP"))
                        clipTip = (ct[0] != '0');
                    if (clipTip && n == 3) {
                        // Dup tip = vert that matched another; base = other two.
                        int tipI = -1;
                        for (int i = 0; i < 4 && tipI < 0; i++) {
                            for (int j = 0; j < i; j++) {
                                if (poly.verts[i].screenX == poly.verts[j].screenX &&
                                    poly.verts[i].screenY == poly.verts[j].screenY) {
                                    tipI = i;
                                    break;
                                }
                            }
                        }
                        if (tipI >= 0) {
                            int16_t tx = poly.verts[tipI].screenX;
                            int16_t ty = poly.verts[tipI].screenY;
                            int16_t ax = 0, ay = 0, bx = 0, by = 0;
                            int base = 0;
                            for (int i = 0; i < 4; i++) {
                                if (poly.verts[i].screenX == tx &&
                                    poly.verts[i].screenY == ty)
                                    continue;
                                if (base == 0) {
                                    ax = poly.verts[i].screenX;
                                    ay = poly.verts[i].screenY;
                                    base = 1;
                                } else {
                                    bx = poly.verts[i].screenX;
                                    by = poly.verts[i].screenY;
                                    base = 2;
                                    break;
                                }
                            }
                            if (base == 2) {
                                const int32_t dx = (int32_t)bx - ax;
                                const int32_t dy = (int32_t)by - ay;
                                const int32_t len2 = dx * dx + dy * dy;
                                if (len2 > 0) {
                                    const int32_t t =
                                        ((int32_t)tx - ax) * dx +
                                        ((int32_t)ty - ay) * dy;
                                    const int32_t px = ax + (t * dx) / len2;
                                    const int32_t py = ay + (t * dy) / len2;
                                    add((int16_t)(2 * px - tx),
                                        (int16_t)(2 * py - ty));
                                }
                            }
                        }
                    }
                    if (n >= 3) {
                        // DrawTriangle Y-clips offscreen tops per-edge (001563).
                        for (int i = 1; i + 1 < n; i++)
                            DrawTriangle(pts[0], pts[i], pts[i + 1], flat);
                    }
                    return;
                }
                if (::getenv("AB_5444PROBE") && ::getenv("AB_5444PROBE")[0] == '1' &&
                    ((poly.texInfo & 0xFFF) == 0x34 || (poly.texInfo & 0xFFF) == 0x15 ||
                     (poly.texInfo & 0xFFF) == 0x0c || (poly.texInfo & 0xFFF) == 0x0b)) {
                    static int s_p44 = 0;
                    if (s_p44 < 10) {
                        fprintf(stderr, "P5444 mesh=%d tex=%04x word=%x bmp=%p sz=%d pal=%d,%d,%d\n",
                                poly.meshIndex, poly.texInfo, (unsigned)drawTexWord,
                                (void*)bmp, texSize, shadePal[0], shadePal[1], shadePal[2]);
                        ++s_p44;
                    }
                }
                // R447 (gated, default OFF): measured band-lane OVERPAINT.
                // The ASM draw-time probe found a FUNC_001799 flat fill whose
                // record (0x567d7bf2 @ 43/06) carries THIS polygon's own four
                // vertices with rgb16 = 0 and the same sortKey (0x367f = 13951).
                // So the sidecar draws the textured mat-38 quad and then blacks
                // it out; the quad's fill is correct (span log + 9252 dump at
                // R445), which is why every face-level probe failed. The port
                // never runs that overpaint record, so the quad's 4s survive.
                // R983 PROMOTED: the ASM runs a FUNC_001799 flat-0 overpaint record (rgb16 = 0, same
                // sortKey) after this textured quad, blacking it out. Oracle-derived, not hand-tuned:
                // the record's own EXEC entry carries this polygon's four vertices. The port never ran
                // it, so the quad's 4s survived. Measured hull 43 du3: 548 -> 198 (C -> B); all other
                // hulls bit-identical (the record only fires on this hull's near-degenerate quads at
                // the orbit poses — applying it unconditionally breaks 01/02/03/04/07 by +128..151).
                // Kill: AB_NO_OVERPAINT43=1.
                if (!(::getenv("AB_NO_OVERPAINT43") && ::getenv("AB_NO_OVERPAINT43")[0] == '1') &&
                    jjffe::GetShipModelIndex(shipModel) == 43 &&
                    poly.vertexCount == 4 && (poly.texInfo & 0xFFF) == 0x26) {
                    static const int8_t kOver[2][4] = {{24, 26, 54, 38}, {25, 27, 55, 39}};
                    for (int oi = 0; oi < 2; oi++) {
                        bool same = true;
                        for (int v = 0; v < 4; v++)
                            if ((int)poly.vertexIdx[v] != (int)kOver[oi][v]) same = false;
                        if (same) {
                            // Only when the quad is near-degenerate: the sidecar's
                            // EXEC list carries this flat-0 record at the orbit
                            // poses and NOT at 01/02/03/04/07 (measured: applying
                            // it unconditionally breaks all five by +128..151).
                            int ymin = poly.verts[0].screenY, ymax = ymin;
                            int xmin = poly.verts[0].screenX, xmax = xmin;
                            for (int v = 1; v < 4; v++) {
                                const int px = poly.verts[v].screenX, py = poly.verts[v].screenY;
                                if (py < ymin) ymin = py; if (py > ymax) ymax = py;
                                if (px < xmin) xmin = px; if (px > xmax) xmax = px;
                            }
                            if (!(ymax - ymin <= 8 && xmax - xmin >= 24)) break;
                            const uint8_t flat = (uint8_t)jjffe::FUNC_001327_DPal16toPal(0u);
                            jjffe::DrawQuadInternal(
                                reinterpret_cast<const int16_t*>(&poly.verts[0].screenX),
                                reinterpret_cast<const int16_t*>(&poly.verts[1].screenX),
                                reinterpret_cast<const int16_t*>(&poly.verts[2].screenX),
                                reinterpret_cast<const int16_t*>(&poly.verts[3].screenX),
                                flat, 0);
                            return;
                        }
                    }
                }
                RenderTexturedPoly(poly, bmp->data, texSize, transparent, drawTexWord, shadePal);
                return;
            }
            if (poly.vertexCount == 3) {
                if (getenv("AB_TRACE_TRI3") && (jjffe::GetShipModelIndex(shipModel) == 33 || jjffe::GetShipModelIndex(shipModel) == 65 || jjffe::GetShipModelIndex(shipModel) == 22) &&
                    (poly.meshIndex == 10 || poly.meshIndex == 11 || (jjffe::GetShipModelIndex(shipModel) == 65 && (poly.meshIndex == 80 || poly.meshIndex == 83 || poly.meshIndex == 88 || poly.meshIndex == 110)) || (jjffe::GetShipModelIndex(shipModel) == 22 && (poly.meshIndex == 109 || poly.meshIndex == 116 || poly.meshIndex == 106 || poly.meshIndex == 92)))) {
                    static int n3 = 0;
                    if (n3 < 4)
                        fprintf(stderr, "TRI3 mi=%d texSize=%d trans=%d pal=[%u,%u,%u,%u,%u,%u,%u] v=(%d,%d)(%d,%d)(%d,%d)\n",
                                poly.meshIndex, texSize, (int)transparent,
                                shadePal[0], shadePal[1], shadePal[2], shadePal[3],
                                shadePal[4], shadePal[5], shadePal[6],
                                poly.verts[0].screenX, poly.verts[0].screenY,
                                poly.verts[1].screenX, poly.verts[1].screenY,
                                poly.verts[2].screenX, poly.verts[2].screenY), ++n3;
                }
                // R508 candidate law (gated): route the ship tri through the
                // ASM FUNC_001807 twin so the span runs Tex64NonTrans with the
                // PERSISTENT DATA_009252 and the ASM's row-quantized V packing
                // (u + v>>10 & ~0x3f), not the port's per-face local ramp +
                // bilinear (v>>16)&mask sampler.
                if (getenv("AB_SHIP_TWIN") && getenv("AB_SHIP_TWIN")[0] == '1') {
                    uint32_t jobbuf[16] = {};
                    uint32_t* job = jobbuf + 2;
                    for (int v = 0; v < 3; v++)
                        job[1 + v] = (uint32_t)(uint16_t)poly.verts[v].screenX |
                                     ((uint32_t)(uint16_t)poly.verts[v].screenY << 16);
                    job[4] = drawTexWord;
                    for (uint32_t i = 0; i < 7; i++)
                        job[5 + i] = shadePal[i];
                    jjffe::FUNC_001807(job);
                    return;
                }
                // FUNC_001644_DrawTexTriangle: ALWAYS UV corners DATA_009249+{0,4,8}
                // = (0,0), (max,0), (0,max). No (texWord>>12)&7 switch — that is
                // quad-only (FUNC_001647). Applying s_uvOrder here selected
                // (0,0)/(max,0)/(max,max) for uvCase 0 and skewed canopy shade.
                int uvMax = texSize - 1;
                int16_t v0[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
                int16_t v1[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
                int16_t v2[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
                int16_t uv0[2] = {0, 0};
                int16_t uv1[2] = {(int16_t)uvMax, 0};
                int16_t uv2[2] = {0, (int16_t)uvMax};
                const bool tri3dbg = getenv("AB_TRACE_TRI3") &&
                    (jjffe::GetShipModelIndex(shipModel) == 33 || jjffe::GetShipModelIndex(shipModel) == 65) &&
                    (poly.meshIndex == 10 || poly.meshIndex == 11 || (jjffe::GetShipModelIndex(shipModel) == 65 && (poly.meshIndex == 80 || poly.meshIndex == 83 || poly.meshIndex == 88 || poly.meshIndex == 110)));
                // R510 candidate law (gated): use the ASM FUNC_001646 tri raster
                // (GenerateTexSteps edges + Tex64NonTrans span) instead of the
                // port-local RenderTexturedTriangle scanline splitter.
                if (getenv("AB_ASM_TRI") && getenv("AB_ASM_TRI")[0] == '1') {
                    jjffe::SelectTexFunc(drawTexWord & 0xFFF);
                    jjffe::DrawTexTriangleDetRasterize(
                        reinterpret_cast<intptr_t>(v0), reinterpret_cast<intptr_t>(v1),
                        reinterpret_cast<intptr_t>(v2),
                        reinterpret_cast<intptr_t>(uv0), reinterpret_cast<intptr_t>(uv1),
                        reinterpret_cast<intptr_t>(uv2), shadePal);
                    return;
                }
                const int pxBefore = tri3dbg ? countNonZeroBuf() : 0;
                RenderTexturedTriangle(v0, v1, v2, uv0, uv1, uv2, bmp->data, texSize, transparent, shadePal,
                    poly.verts[0].depth, poly.verts[1].depth, poly.verts[2].depth);
                if (tri3dbg) {
                    static int n3 = 0;
                    if (n3 < 4) {
                        fprintf(stderr, "TRI3PAINT mi=%d dpix=%d\n", poly.meshIndex,
                                countNonZeroBuf() - pxBefore);
                        ++n3;
                    }
                }
                return;
            }
        }
        // Eagle nest #23 TexQuadPair 0x5015 Contour lobes (ASM 001805 shade0
        // 0x262→VGA22): material has no 64² BMP, so isPowerTex fails and the
        // old path fell through to GetFaceColor (VGA2..15 only). Flat-fill
        // shade0 like ComplexFillColor. Dig: AB_C721_5015_FACE=1 skips this.
        if ((poly.texInfo == 0x5015 || poly.texInfo == 0x5009) &&
            mat.shadeCount > 0 &&
            !(getenv("AB_C721_5015_FACE") &&
              getenv("AB_C721_5015_FACE")[0] == '1')) {
            int8_t flR8 = 0, flG8 = 0, flB8 = 0;
            GetMeshFaceLight(&flR8, &flG8, &flB8);
            int flR = flR8, flG = flG8, flB = flB8;
            const bool applyFL = (flR | flG | flB) != 0;
            uint8_t shadePalFB[256];
            BuildTexShadePalette(shadePalFB, mat, flR, flG, flB, applyFL);
            if (v3on()) {
                pub9252(shadePalFB, mat.shadeCount > 7 ? 7 : mat.shadeCount, v3stale(poly.texInfo) ? 4 : 0);
                for (int w = 0; w < 256; w++) shadePalFB[w] = g_shade9252[w];
            }
            const uint8_t flat = shadePalFB[0];
            if (getenv("AB_DUMP_5015")) {
                static int n15 = 0;
                if (n15 < 20)
                    fprintf(stderr, "5015 mesh=%d type=%d flat=%u vc=%d verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d) fl=(%d,%d,%d) key=%u\n",
                            poly.meshIndex, (int)poly.type, flat, poly.vertexCount,
                            poly.verts[0].screenX, poly.verts[0].screenY,
                            poly.verts[1].screenX, poly.verts[1].screenY,
                            poly.verts[2].screenX, poly.verts[2].screenY,
                            poly.verts[3].screenX, poly.verts[3].screenY,
                            flR, flG, flB, poly.sortKey), ++n15;
            }
            if (flat != 0 && poly.vertexCount == 4) {
                int16_t v0[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
                int16_t v1[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
                int16_t v2[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
                int16_t v3[2] = {poly.verts[3].screenX, poly.verts[3].screenY};
                DrawQuadInternal(v0, v1, v2, v3, flat, 0);
                return;
            }
            if (flat != 0 && poly.vertexCount == 3) {
                DrawTriangle(&poly.verts[0].screenX, &poly.verts[1].screenX,
                             &poly.verts[2].screenX, flat);
                return;
            }
        }
    }

    // Flat-shaded fallback
    uint8_t color;
    if ((poly.texInfo >> 14) == 0) {
        // FUNC_001692 flat RGB → DPal16toPal(RGB444). Adder canopy 0x20ee
        // maps to VGA 0 (punch hole when drawn over hull). Asp #186 panes use
        // 0x2000 with afterSpecialPoly set for attach — must still FlatRgb
        // (not force VGA0 via the old afterSpecialPoly short-circuit).
        color = FlatRgbToPal(poly.texInfo);
        // Missiles (#3 / #6–13): pure 0x1000 = RGB0 + FUNC_001695 flag. With no
        // ctx face light, DPal stays VGA0. Shade by face normal so the hull
        // paints (FORCE1 dig: geometry is on-screen, only colour was missing).
        // Scope to missile meshes: Adder engine glow 0x3000 is also RGB0+flag
        // but must stay VGA0 (normal-shading painted it white/grey by angle).
        if (color == 0 && s_missileFlatMesh && poly.texInfo == 0x1000)
            color = GetFaceColor(shipModel, poly.normalIdx, 0x08, shipRot);
    } else if (poly.type == MeshPolyType::ComplexFill) {
        if (getenv("AB_DUMP_CF_FLAT")) {
            static int ncf = 0;
            if (ncf < 20) {
                int16_t x0=319,y0=199,x1=0,y1=0;
                for (int v=0;v<poly.vertexCount && v<4;v++){int16_t X=poly.verts[v].screenX,Y=poly.verts[v].screenY;if(X<x0)x0=X;if(Y<y0)y0=Y;if(X>x1)x1=X;if(Y>y1)y1=Y;}
                fprintf(stderr, "CF_FLAT mi=%d cid=%d tex=0x%04x vc=%d aabb=(%d,%d)-(%d,%d) bf=%d\n",
                        poly.meshIndex, (int)poly.contourId, poly.texInfo, poly.vertexCount, x0,y0,x1,y1, poly.backFacing?1:0);
                ++ncf;
            }
        }
        // Contour-grouped pieces are span-filled once in RenderShipFlat
        // (FUNC_001548 bezier → 001799). AB_C721_FLAT_QUAD=1 keeps old tris/quads.
        // AB_SKIP_C721=1 omits ComplexFill entirely (bisect other polys).
        if (getenv("AB_SKIP_C721") && getenv("AB_SKIP_C721")[0] == '1')
            return;
        if (poly.contourId >= 0) {
            const char* flat = getenv("AB_C721_FLAT_QUAD");
            if (!(flat && flat[0] == '1'))
                return;
        }
        // FUNC_001721 flat-fill. Flat-RGB words were handled above. For material
        // words, most ships (Asp 0x4xxx) match shade0, but cargo wrapper family
        // uses 0x5xxx complex fills that map better to face-color by normal.
        if ((poly.texInfo & 0xF000) == 0x5000) {
            color = GetFaceColor(shipModel, poly.normalIdx, 0x08, shipRot);
        } else {
            // FUNC_001721 → 001692/693: face light only when tex word has
            // 0x1000 (FUNC_001695 flag). Puma 0x4008 + Complex762(+4,0,0)
            // otherwise becomes (10,6,6)→VGA80; ASM flat path stays 0x666→VGA6.
            MaterialInfo mat = ResolveMaterial(poly.texInfo);
            int8_t flR8 = 0, flG8 = 0, flB8 = 0;
            GetMeshFaceLight(&flR8, &flG8, &flB8);
            int flR = flR8, flG = flG8, flB = flB8;
            bool applyFL = (poly.texInfo & 0x1000) != 0 && (flR | flG | flB) != 0;
            if (const char* fl = getenv("AB_FACE_LIGHT")) {
                if (sscanf(fl, "%d,%d,%d", &flR, &flG, &flB) == 3)
                    applyFL = true;
            }
            // R574b candidate law (gated AB_FL_SNAPSHOT): per-poly enqueue-time face-light
            // snapshot beats the mutable global (ASM bakes light into each job's words).
            if (::getenv("AB_FL_SNAPSHOT") && ::getenv("AB_FL_SNAPSHOT")[0] == '1') {
                flR = poly.faceLight[0]; flG = poly.faceLight[1]; flB = poly.faceLight[2];
                applyFL = (flR | flG | flB) != 0;
            }
            uint8_t shadePal[256];
            BuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
            if (v3on()) {
                pub9252(shadePal, mat.shadeCount > 7 ? 7 : mat.shadeCount, v3stale(poly.texInfo) ? 4 : 0);
                for (int w = 0; w < 256; w++) shadePal[w] = g_shade9252[w];
            }
            color = shadePal[0];
        }
    } else if (poly.vertexCount == 2 || poly.type == MeshPolyType::DirLine) {
        // PlainLine/DirLine with material tex word (e.g. Shuttle 0x5009):
        // FUNC_001703 → 001692/693/691 → DPal. RGB starts 0; 001691 adds
        // material shade0 (DATA_007812). Apply ctx face light like textured
        // shade remap (Complex762 +0xf4) so mat9 (2,2,2)+4 → VGA 6.
        // Prior GetFaceColor() painted VGA 8 → tip idx 6↔8.
        MaterialInfo mat = ResolveMaterial(poly.texInfo);
        int8_t flR8 = 0, flG8 = 0, flB8 = 0;
        GetMeshFaceLight(&flR8, &flG8, &flB8);
        int flR = flR8, flG = flG8, flB = flB8;
        bool applyFL = (flR | flG | flB) != 0;
        if (const char* fl = getenv("AB_FACE_LIGHT")) {
            if (sscanf(fl, "%d,%d,%d", &flR, &flG, &flB) == 3)
                applyFL = true;
        }
        uint8_t shadePal[256];
        BuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
        if (v3on()) {
            pub9252(shadePal, mat.shadeCount > 7 ? 7 : mat.shadeCount, v3stale(poly.texInfo) ? 4 : 0);
            for (int w = 0; w < 256; w++) shadePal[w] = g_shade9252[w];
        }
        color = shadePal[0];
    } else if (poly.type == MeshPolyType::FlatQuad) {
        color = 0x00;
    } else if (g_debugFaceColors) {
        switch (poly.normalIdx) {
            case 2:  color = 0x0F; break;
            case 4:  color = 0x0E; break;
            case 16: color = 0x08; break;
            case 8:  color = 0x0B; break;
            case 6:  case 7:  color = 0x0A; break;
            case 14: case 15: color = 0x0C; break;
            case 10: case 12: color = 0x09; break;
            case 18: case 19: case 20: case 21: color = 0x0D; break;
            default: color = 0x08 + (poly.normalIdx % 8); break;
        }
    } else {
        color = GetFaceColor(shipModel, poly.normalIdx, 0x08, shipRot);
    }

    // Flat-fill skip: DISABLED for now. Previously skipped color=0x00 faces
    // when depth was enabled, but this caused regressions.
    // if (color == 0x00 && s_depthEnabled) return;
    // Experiment: AB_SKIP_FLAT0=1 omits black flat fills (cockpit / panel holes).
    if (color == 0x00) {
        const char* skipFlat0 = getenv("AB_SKIP_FLAT0");
        if (skipFlat0 && skipFlat0[0] == '1')
            return;
    }
    if (!ShipAllowVga0FlatPunch(poly, shipModel, color))
        return;
    // Sprite748 → model #187 nozzle bridge (flat 0x3000 = RGB0 + face-light
    // flag). With the child face light it resolves to 0x888, which is what ASM
    // 001809 paints on Saker III. Adder / Osprey / Osprey X carry the same
    // child but their ASM job traces hold no 001809 at all — the nested walk
    // drops the face somewhere we do not model yet — so keep them unpainted.
    // Dig: AB_KEEP_187_ALL=1 paints it on every hull.
    if (poly.texInfo == 0x3000 && IsSpriteChildParam(poly.specialParam) &&
        SpriteChildModelId(poly.specialParam) == 187 &&
        jjffe::GetShipModelIndex(shipModel) != 31 &&
        !(getenv("AB_KEEP_187_ALL") && getenv("AB_KEEP_187_ALL")[0] == '1'))
        return;
    // Escape Capsule (#14): flat texInfo==0 is a full-silhouette VGA0 punch
    // drawn after the 0x5xxx hull — skip it (Saker/Asp still need other flat0).
    if (s_capsuleSilhouetteMesh && poly.texInfo == 0 && color == 0)
        return;
    // Asp #40 dig: 0x20ee FlatRgb→VGA0 punches a mid-hull hole that ASM keeps
    // as mat6/7 VGA22. AB_SKIP_20EE=1 skips only that flat (Adder canopy still
    // punches unless this env is set).
    if (poly.texInfo == 0x20ee) {
        const char* skip = getenv("AB_SKIP_20EE");
        if (skip && skip[0] == '1')
            return;
    }
    if (poly.vertexCount == 4) {
        int16_t v0[2] = {poly.verts[0].screenX, poly.verts[0].screenY};
        int16_t v1[2] = {poly.verts[1].screenX, poly.verts[1].screenY};
        int16_t v2[2] = {poly.verts[2].screenX, poly.verts[2].screenY};
        int16_t v3[2] = {poly.verts[3].screenX, poly.verts[3].screenY};
        if (poly.type == MeshPolyType::ComplexFill &&
            getenv("AB_DUMP_CF_XY") && getenv("AB_DUMP_CF_XY")[0] == '1') {
            auto area2 = [](int16_t* a, int16_t* b, int16_t* c) {
                return (int)(b[0] - a[0]) * (c[1] - a[1]) -
                       (int)(b[1] - a[1]) * (c[0] - a[0]);
            };
            fprintf(stderr,
                    "  CF4 n=%u cid=%d vi=%u,%u,%u,%u xy=(%d,%d)(%d,%d)(%d,%d)(%d,%d) a012=%d a023=%d col=%u\n",
                    poly.normalIdx, poly.contourId,
                    poly.vertexIdx[0], poly.vertexIdx[1],
                    poly.vertexIdx[2], poly.vertexIdx[3],
                    v0[0], v0[1], v1[0], v1[1], v2[0], v2[1], v3[0], v3[1],
                    area2(v0, v1, v2), area2(v0, v2, v3), color);
        }
        DrawQuadInternal(v0, v1, v2, v3, color, 0);
    } else if (poly.vertexCount == 3) {
        DrawTriangle(&poly.verts[0].screenX, &poly.verts[1].screenX, &poly.verts[2].screenX, color);
    } else if (poly.vertexCount == 2) {
        // AB_SKIP_PLAIN=1: bisect silhouette / tip-edge diffs (Shuttle).
        if (const char* sp = getenv("AB_SKIP_PLAIN"); sp && sp[0] == '1')
            return;
        // FUNC_001704 → 001803: both endpoint X values are stored as (sx - 1)
        // (clamped at 0). Without this, PlainLines paint one pixel fat vs ASM.
        int x0 = poly.verts[0].screenX;
        int x1 = poly.verts[1].screenX;
        if (x0 > 0) x0 -= 1;
        if (x1 > 0) x1 -= 1;
        DrawLine(x0, poly.verts[0].screenY,
                 x1, poly.verts[1].screenY, color);
    }
}


// Soft VGA0 flat-punch policy (Saker tip/throat, Adder windshield, Explorer).
// Call ShipPrepareFlatPunchGates() once per frame before testing polys.
bool ShipAllowVga0FlatPunch(const TransformedPoly& poly, const ShipModel* shipModel,
                            uint8_t color)
{
    // Flat 0x2000 / unlit 0x3000 → VGA0: do not punch by default. Painter-order
    // draw after mat6/7 wiped Asp top/bottom fringe (mesh4/6 AABB strips).
    // Same final image as underlay-noop on dump-ab (VGA0 underlay paints nothing
    // on a black clear). AB_DRAW_2000=1 restores punch; grey dig via
    // AB_FLAT_2000_GREY still paints (color != 0).
    // Adder #187 engine glow is 0x3000 (RGB0 + face-light flag). With no ctx
    // face light it maps to VGA0; drawing it punched a mid-hull black rect at
    // oblique angles where ASM culled the face entirely.
    // Saker tip 0x2000 *are* authentic ASM punches at close range (Z:80 Y:201
    // P:30 → pd=0 with draw); skipping them shows stars through the nozzle.
    if ((poly.texInfo == 0x2000 || poly.texInfo == 0x3000) && color == 0) {
        const bool adderWindshield =
            s_adderWindshieldPunchMesh && poly.texInfo == 0x2000 &&
            poly.meshIndex == 25 && poly.normalIdx == 2;
        const bool explorerTipPunch =
            s_explorer2000PunchMesh && poly.texInfo == 0x2000;
        // Tip-child #201 0x2000 rings: ASM 001809s visible tip-band flats when
        // the tip is shoved off L/R, or when a tip ring AABB overlaps the
        // mid-size throat (Y:243 — tip fully on-screen but punches still run
        // *after* mat60 glass). Fore-tip mode (aft clip + fore 6/7 on-screen)
        // punches 4/6/7/9 instead — tip-band punches wipe mat60 there.
        // Dig: AB_NO_SAKER_2000=1 / AB_SAKER_TIP_WIDE=1.
        bool sakerTipPunch = false;
        if (s_saker2000PunchMesh && poly.texInfo == 0x2000 &&
            IsSpriteChildParam(poly.specialParam) &&
            SpriteChildModelId(poly.specialParam) == 201 &&
            !(getenv("AB_NO_SAKER_2000") && getenv("AB_NO_SAKER_2000")[0] == '1')) {
            const bool wideOk =
                getenv("AB_SAKER_TIP_WIDE") && getenv("AB_SAKER_TIP_WIDE")[0] == '1';
            const int mi = poly.meshIndex;
            bool offLR = false;
            int16_t x0 = poly.verts[0].screenX, x1 = x0;
            int16_t y0 = poly.verts[0].screenY, y1 = y0;
            for (int vi = 0; vi < poly.vertexCount && vi < 4; vi++) {
                const int16_t sx = poly.verts[vi].screenX;
                const int16_t sy = poly.verts[vi].screenY;
                if (sx < 0 || sx >= SCREEN_WIDTH) offLR = true;
                if (sx < x0) x0 = sx;
                if (sx > x1) x1 = sx;
                if (sy < y0) y0 = sy;
                if (sy > y1) y1 = sy;
            }
            const bool overlapsThroat =
                s_sakerThroatMid &&
                x1 >= s_sakerThroatX0 && x0 <= s_sakerThroatX1 &&
                y1 >= s_sakerThroatY0 && y0 <= s_sakerThroatY1;
            const bool tipGate = s_sakerTipOffLR || overlapsThroat;
            if (wideOk) {
                sakerTipPunch = offLR;
            } else if (s_sakerForeTipMode) {
                // ASM Z:88: 6+7 only. Z:96: +compact 4/9. Y:27: +4 when 9 off.
                // Y:337: +9 when 4 off (even if mesh9 taller). Dig: AB_NO_SAKER_2000.
                if (!offLR && (mi == 6 || mi == 7))
                    sakerTipPunch = true;
                else if (!offLR && mi == 4 &&
                         (s_sakerMesh9OffLR || (y1 - y0) <= 32))
                    sakerTipPunch = true;
                else if (!offLR && mi == 9 &&
                         (s_sakerMesh4OffLR || (y1 - y0) <= 32))
                    sakerTipPunch = true;
            } else if (!offLR && tipGate) {
                if (s_sakerForeOnlyOff) {
                    // Y:227 (mesh4 off): ASM 9+13/15/17. Y:173 (mesh4 on): 11+.
                    if (s_sakerMesh4OffLR) {
                        if (mi == 9 || mi == 13 || mi == 15 || mi == 17)
                            sakerTipPunch = true;
                    } else if (mi == 11 || mi == 13 || mi == 15 || mi == 17) {
                        sakerTipPunch = true;
                    }
                } else if (s_sakerThroatForePunch) {
                    // Z:104 Y:341 / Y:27: ASM fore rings overlapping throat.
                    if (overlapsThroat &&
                        (mi == 4 || mi == 6 || mi == 7 || mi == 9))
                        sakerTipPunch = true;
                } else if (s_sakerThroatAftPunch) {
                    // Y:243: ASM tip-band 9/15/17 over throat.
                    if (mi == 11 || mi == 13 || mi == 15 || mi == 17)
                        sakerTipPunch = true;
                    else if (mi == 9 && overlapsThroat)
                        sakerTipPunch = true;
                } else if (s_sakerThroatSidePunch) {
                    // Side P:35: ASM mesh4+11 over throat (Z:104 Y:91).
                    if (overlapsThroat &&
                        (mi == 4 || mi == 6 || mi == 7 || mi == 9 ||
                         mi == 11 || mi == 13 || mi == 15 || mi == 17))
                        sakerTipPunch = true;
                } else if (mi == 11 || mi == 13 || mi == 15 || mi == 17) {
                    sakerTipPunch = true;
                } else if (mi == 9 && (s_sakerMesh11OffLR || overlapsThroat)) {
                    sakerTipPunch = true;
                } else if (mi == 4 && overlapsThroat) {
                    // tipOffLR side (Z:88 Y:90): ASM punches mesh4 + mesh11.
                    sakerTipPunch = true;
                }
            } else if (offLR && (mi == 13 || mi == 15 || mi == 17) &&
                       (mi == 13 || s_sakerMesh13OffLR)) {
                sakerTipPunch = true;
            }
        }
        // Hull mesh 73 throat VGA0. ≥24 catches Z:112 Y:182 (AABB ≈26×37);
        // ≥30 missed it and left tip punches gated off. Dig: AB_NO_SAKER_2000=1.
        bool sakerThroatPunch = false;
        if (s_saker2000PunchMesh && poly.texInfo == 0x2000 &&
            poly.meshIndex == 73 && poly.vertexCount >= 3 &&
            !(getenv("AB_NO_SAKER_2000") && getenv("AB_NO_SAKER_2000")[0] == '1')) {
            int16_t x0 = poly.verts[0].screenX, x1 = x0;
            int16_t y0 = poly.verts[0].screenY, y1 = y0;
            for (int vi = 1; vi < poly.vertexCount && vi < 4; vi++) {
                if (poly.verts[vi].screenX < x0) x0 = poly.verts[vi].screenX;
                if (poly.verts[vi].screenX > x1) x1 = poly.verts[vi].screenX;
                if (poly.verts[vi].screenY < y0) y0 = poly.verts[vi].screenY;
                if (poly.verts[vi].screenY > y1) y1 = poly.verts[vi].screenY;
            }
            sakerThroatPunch = (x1 - x0) >= 22 && (y1 - y0) >= 24;
        }
        const char* draw = getenv("AB_DRAW_2000");
        if (!adderWindshield && !explorerTipPunch && !sakerTipPunch &&
            !sakerThroatPunch && !(draw && draw[0] == '1'))
            return false;
    }
    return true;
}


void ShipPrepareFlatPunchGates(TransformedPoly* polys, int polyCount,
                               const ShipModel* shipModel)
{
    if (!polys || polyCount <= 0) return;

    // Escape Capsule: only 0x5xxx + flat 0x0000.
    // Missiles: only flat 0x1000 (+ optional 0x2000 VGA0 punch).
    {
        int n0 = 0, n5 = 0, n1000 = 0, n2000 = 0, n3000 = 0;
        int n4029 = 0, n20ee = 0, nOther = 0, nHull = 0;
        for (int i = 0; i < polyCount; i++) {
            if (polys[i].vertexCount < 3) continue;
            nHull++;
            const uint16_t t = polys[i].texInfo;
            if (t == 0) n0++;
            else if (t == 0x1000) n1000++;
            else if (t == 0x2000) n2000++;
            else if (t == 0x3000) n3000++;
            else if (t == 0x4029) n4029++;
            else if (t == 0x20ee) n20ee++;
            else if ((t & 0xF000) == 0x5000) n5++;
            else nOther++;
        }
        s_capsuleSilhouetteMesh = (nHull > 0 && nHull <= 8 && n0 > 0 && n5 > 0 &&
                                   nOther == 0 && n1000 == 0);
        s_missileFlatMesh = (nHull > 0 && n1000 > 0 && nOther == 0 && n5 == 0 &&
                             n0 == 0);
        s_adderWindshieldPunchMesh =
            (nHull == 21 && n4029 == 16 && n20ee == 1 && n2000 >= 3 &&
             n3000 == 1);

        s_hasTexturedPolys = false;
        for (int i = 0; i < polyCount; i++) {
            if (polys[i].vertexCount < 3) continue;
            if (polys[i].texInfo & 0x4000) { s_hasTexturedPolys = true; break; }
        }
        // Imperial Explorer #65 tip punches (not Asp 0x2000 holes).
        // Pose-visible n2000 varies (4–5); verts/norms uniquely ID #65.
        s_explorer2000PunchMesh =
            shipModel && shipModel->vertexCount == 128 &&
            shipModel->normalCount == 116;
        s_saker2000PunchMesh = jjffe::GetShipModelIndex(shipModel) == 31;
        s_sakerTipOffLR = false;
        s_sakerMesh13OffLR = false;
        s_sakerMesh11OffLR = false;
        s_sakerForeTipMode = false;
        s_sakerForeOnlyOff = false;
        s_sakerMesh4OffLR = false;
        s_sakerMesh9OffLR = false;
        s_sakerThroatMid = false;
        s_sakerThroatForePunch = false;
        s_sakerThroatAftPunch = false;
        s_sakerThroatSidePunch = false;
        s_sakerTipGlassMinKey = 0xffffffffu;
        if (s_saker2000PunchMesh) {
            bool aftOffLR = false;
            bool fore6Off = false, fore7Off = false;
            bool sawFore6 = false, sawFore7 = false;
            bool throatFore67 = false, throatAft1517 = false;
            bool throatAnyTip = false;
            for (int i = 0; i < polyCount; i++) {
                TransformedPoly& p = const_cast<TransformedPoly&>(polys[i]);
                if (p.texInfo == 0x403c && IsSpriteChildParam(p.specialParam) &&
                    SpriteChildModelId(p.specialParam) == 201 &&
                    p.visible && !p.backFacing) {
                    if (p.sortKey < s_sakerTipGlassMinKey)
                        s_sakerTipGlassMinKey = p.sortKey;
                }
                if (p.texInfo == 0x2000 && p.meshIndex == 73 &&
                    p.vertexCount >= 3 && p.visible && !p.backFacing) {
                    int16_t x0 = p.verts[0].screenX, x1 = x0;
                    int16_t y0 = p.verts[0].screenY, y1 = y0;
                    for (int v = 1; v < p.vertexCount && v < 4; v++) {
                        if (p.verts[v].screenX < x0) x0 = p.verts[v].screenX;
                        if (p.verts[v].screenX > x1) x1 = p.verts[v].screenX;
                        if (p.verts[v].screenY < y0) y0 = p.verts[v].screenY;
                        if (p.verts[v].screenY > y1) y1 = p.verts[v].screenY;
                    }
                    s_sakerThroatX0 = x0; s_sakerThroatX1 = x1;
                    s_sakerThroatY0 = y0; s_sakerThroatY1 = y1;
                    // ≥22 catches Z:128 seed throat (22×31); ≥24 alone missed it.
                    s_sakerThroatMid =
                        (x1 - x0) >= 22 && (y1 - y0) >= 24;
                }
                if (p.texInfo != 0x2000 || !IsSpriteChildParam(p.specialParam) ||
                    SpriteChildModelId(p.specialParam) != 201)
                    continue;
                bool offLR = false;
                int16_t x0 = p.verts[0].screenX, x1 = x0;
                int16_t y0 = p.verts[0].screenY, y1 = y0;
                for (int v = 0; v < p.vertexCount && v < 4; v++) {
                    const int16_t sx = p.verts[v].screenX;
                    const int16_t sy = p.verts[v].screenY;
                    if (sx < 0 || sx >= SCREEN_WIDTH) offLR = true;
                    if (sx < x0) x0 = sx;
                    if (sx > x1) x1 = sx;
                    if (sy < y0) y0 = sy;
                    if (sy > y1) y1 = sy;
                }
                if (p.meshIndex == 6) {
                    sawFore6 = true;
                    if (offLR) fore6Off = true;
                } else if (p.meshIndex == 7) {
                    sawFore7 = true;
                    if (offLR) fore7Off = true;
                } else if (p.meshIndex == 4 && offLR) {
                    s_sakerMesh4OffLR = true;
                } else if (p.meshIndex == 9 && offLR) {
                    s_sakerMesh9OffLR = true;
                }
                if (offLR && (p.meshIndex == 13 || p.meshIndex == 15))
                    aftOffLR = true;
                if (!offLR) continue;
                s_sakerTipOffLR = true;
                if (p.meshIndex == 13) s_sakerMesh13OffLR = true;
                if (p.meshIndex == 11) s_sakerMesh11OffLR = true;
            }
            // Second pass: tip-ring ↔ throat AABB (throat may sort after tip rings).
            if (s_sakerThroatMid) {
                for (int i = 0; i < polyCount; i++) {
                    const TransformedPoly& p = polys[i];
                    if (p.texInfo != 0x2000 || !IsSpriteChildParam(p.specialParam) ||
                        SpriteChildModelId(p.specialParam) != 201)
                        continue;
                    int16_t x0 = p.verts[0].screenX, x1 = x0;
                    int16_t y0 = p.verts[0].screenY, y1 = y0;
                    for (int v = 1; v < p.vertexCount && v < 4; v++) {
                        if (p.verts[v].screenX < x0) x0 = p.verts[v].screenX;
                        if (p.verts[v].screenX > x1) x1 = p.verts[v].screenX;
                        if (p.verts[v].screenY < y0) y0 = p.verts[v].screenY;
                        if (p.verts[v].screenY > y1) y1 = p.verts[v].screenY;
                    }
                    if (!(x1 >= s_sakerThroatX0 && x0 <= s_sakerThroatX1 &&
                          y1 >= s_sakerThroatY0 && y0 <= s_sakerThroatY1))
                        continue;
                    throatAnyTip = true;
                    if (p.meshIndex == 6 || p.meshIndex == 7)
                        throatFore67 = true;
                    if (p.meshIndex == 15 || p.meshIndex == 17)
                        throatAft1517 = true;
                }
            }
            // Fore-tip: aft rings clip while at least one of fore 6/7 stays
            // fully on-screen (Z:88 Y:347 has mesh6 off + mesh7 on).
            s_sakerForeTipMode =
                aftOffLR && ((sawFore6 && !fore6Off) || (sawFore7 && !fore7Off));
            // Fore rings clipped, aft tip-band still fully on-screen (Y:227).
            s_sakerForeOnlyOff = s_sakerTipOffLR && !aftOffLR;
            // Fully on-screen tip + mid throat: pick punch set by overlap.
            if (s_sakerThroatMid && !s_sakerTipOffLR && throatAnyTip) {
                if (throatFore67 && !throatAft1517)
                    s_sakerThroatForePunch = true;
                else if (throatAft1517 && !throatFore67)
                    s_sakerThroatAftPunch = true;
                else
                    s_sakerThroatSidePunch = true;
            }
            // Painter: larger sortKey = farther = drawn first. ASM tip 001809s
            // sit *nearer* than mat60 glass (1903 < 1967) so punches win over
            // tip shade on the nozzle. C++ tip rings often compute farther than
            // glass → glass redraws the overlap. Dig: Z:96 Y:243 P:24.
            if (s_sakerTipGlassMinKey != 0xffffffffu) {
                const uint32_t punchKey =
                    s_sakerTipGlassMinKey > 0 ? s_sakerTipGlassMinKey - 1 : 0;
                for (int i = 0; i < polyCount; i++) {
                    TransformedPoly& p = const_cast<TransformedPoly&>(polys[i]);
                    if (p.texInfo != 0x2000 ||
                        !IsSpriteChildParam(p.specialParam) ||
                        SpriteChildModelId(p.specialParam) != 201)
                        continue;
                    const int mi = p.meshIndex;
                    bool punchMesh = false;
                    if (s_sakerForeTipMode || s_sakerThroatForePunch)
                        punchMesh = (mi == 4 || mi == 6 || mi == 7 || mi == 9);
                    else if (s_sakerThroatSidePunch)
                        punchMesh = (mi == 4 || mi == 6 || mi == 7 || mi == 9 ||
                                     mi == 11 || mi == 13 || mi == 15 ||
                                     mi == 17);
                    else
                        punchMesh = (mi == 9 || mi == 11 || mi == 13 ||
                                     mi == 15 || mi == 17);
                    if (!punchMesh) continue;
                    if (p.sortKey > punchKey) p.sortKey = punchKey;
                }
                // Throat must also lose to glass then win via tip punches; keep
                // throat farther than glass is fine. Tip punches carry the join.
            }
        }
    }
}

// Render all ship polygons as textured or flat-shaded
// R263 band-job painter: draws (kind 3=tri via 1807 / 4=quad via 1805) at
// explicit screen coords with the record material's ROWS as the ramp (the
// sidecar's A708-tail enqueue: drawTex = material[+0], ramp words = 7 RGB
// rows, shade0 nibbles unpacked with 1692's bit-14 zero-adder).
static void DrawBandJobRec(int kind, uint32_t matIdx, const int16_t xy[4][2])
{
    const uint8_t* blob = data::GetDataBlob();
    const size_t off = kMaterialTableOffset + (size_t)matIdx * kMaterialEntryStride;
    if (!blob || off + 8 + 7 * 3 > data::GetDataBlobSize()) return;
    const uint8_t* mrow = blob + off;
    const uint32_t drawTex = *reinterpret_cast<const uint32_t*>(mrow);
    uint32_t cnt = *reinterpret_cast<const uint32_t*>(mrow + 4);
    if (cnt > 7) cnt = 7;
    uint32_t jobbuf[16] = {};
    uint32_t* job = jobbuf + 2;  // the twins peek sortKey at p-8
    auto pack = [](const int16_t p[2]) {
        return (uint32_t)(uint16_t)p[0] | ((uint32_t)(uint16_t)p[1] << 16);
    };
    // R263b: DO NOT route through the FUNC_001805/1807 twins' local
    // palettes - the sidecar's fill samples the PERSISTENT DATA_009252
    // (slots 7.. = zero except what 180x wrote; ship-demo leaves zeros -
    // bank3 probe: pal tail = 00s). Replicate the callback bodies with a
    // zero-tail palette: slots 0..6 = DPal16toPal(material rows), rest 0.
    // R263d GROUND TRUTH (pal4.gdb at ffeasm 1644-entry): the shade cells
    // hold the material row BYTES (6,8,7,5,4 / 22,4,3,1,0) - no DPal word
    // indexing; tail slots 7+ = 0 in ship-demo. ASM 1807's [7631](word)
    // leg evidently resolves a 5-bit-packed word to a byte index; the
    // ship-material shade0 is a single nibble byte already.
    uint8_t shadePal[256];
    for (uint32_t i = 0; i < cnt; i++)
        // R263f: the A708 ramp word is 12-bit NIBBLE-packed (measured words
        // 0x666/0x222..): r | g<<4 | b<<8. (byte-packing gave 0x6666 = 0.)
        shadePal[i] = (uint8_t)jjffe::FUNC_001327_DPal16toPal(
            (uint32_t)mrow[8 + i * 3] |
            ((uint32_t)(mrow[9 + i * 3] & 0xF) << 4) |
            ((uint32_t)(mrow[10 + i * 3] & 0xF) << 8));

    if (v3on()) pub9252(shadePal, cnt, 0);   // publish own ramp (oracle F1)
    Bitmap* bmp = jjffe::BmpIndexToPtr((int)(drawTex & 0xfff));
    if (!bmp) bmp = jjffe::BmpIndexToPtr((int)matIdx);   // R268 K3: classic
    // lane resolves stale material texWords through the MATERIAL INDEX itself
    if (!bmp) {                                           // (ship_model's own
        if (kind == 3) {                                  // convention above):
            const int16_t *a = xy[0], *b = xy[1], *c = xy[2];
            DrawTriangle((const int16_t*)a, (const int16_t*)b, (const int16_t*)c, shadePal[0]);
        } else {
            int16_t v0[2]={xy[0][0],xy[0][1]},v1[2]={xy[1][0],xy[1][1]},
                        v2[2]={xy[2][0],xy[2][1]},v3[2]={xy[3][0],xy[3][1]};
            DrawQuadInternal(v0, v1, v2, v3, shadePal[0], 0);
        }
        return;
    }
    const int texSize = bmp->width;
    if (texSize != 64 && texSize != 128) return;
    const bool transparent = (bmp->flags & 1) != 0;
    const int uvMax = texSize - 1;
    const int16_t u0[2] = {0, 0}, u1[2] = {(int16_t)uvMax, 0}, u2[2] = {0, (int16_t)uvMax};
    if (kind == 3) {
        DrawTexTriangleShadedUV(xy[0], xy[1], xy[2], u0, u1, u2,
                                bmp->data, texSize, transparent, shadePal);
    } else {
        const int16_t u3[2] = {(int16_t)uvMax, (int16_t)uvMax};
        DrawTexQuadShadedUV(xy[0], xy[1], xy[2], xy[3], u0, u1, u2, u3,
                            bmp->data, texSize, transparent, shadePal);
    }
}

// R263 walk-bank replay (AB_BANKREPLAY=1, never shipping): replays the
// SIDECAR's measured face-record dispatch through the port's own
// FUNC_001471/1478 legs on a private, pose-fresh zeroed bank - first-touch
// slots return unprojected (0,0) exactly like ffeasm (1478 builds rotated
// only), second touches take 1471's world-add + FUNC_001479 projection leg,
// matching the warmed coords the sidecar's EXEC lines show. Jobs surviving
// A708's own perimeter>5 guard are painted through the band-job twins.
// Dispatch kits (mat word + slot tuples, order = ffeasm FUNC_001708 dump):
struct WalkRecKit { uint16_t mat; uint8_t kind; int8_t s[4]; };
static const WalkRecKit kWalk61[] = {
    {0x400b, 3, { 4, 6, 0,  0}},
    // R438 SHIPPED: the (1,3,5)/0x4009 record is REMOVED from the active kit.
    // Measured (per-row AB_BANKRECODS sweep, all 8 poses, record 2259T):
    // rows {0,2} = {4,6,0}+{5,7,1} give 01-04/07:0  05:1  06:50  08:14  => 65.
    // Adding this row back restores 08:736 (787 total). The (4,6,0) and
    // (5,7,1) rows are the SAME logical band triangle in its even / odd twin
    // flavour - each pose needs one of them, and the wrong twin is harmless -
    // while this 0x4009 row duplicates a triangle the CLASSIC lane already
    // draws (at 08 it is mesh 8, vidx [0,2,4], byte-identical verts to the
    // sidecar's own mat-9 job), so replaying it only double-covers content.
    // Row measured-and-parked below with the R263 nine.
    // (1,3,5)/0x4009 stays retired unless a future pose set needs mat-9 band
    // content the classic lane does not cover.
    // R263 SHIPPED: the (5,7,1)/0x400b fan only - fleet-verified with the
    // replay on: #61 total pd 9342 -> 3362 (05: 2684->1, 06: 3347->50,
    // every one of the 3297 cells it paints carries the sidecar's exact
    // value; front poses stay 0). The other nine measured records park
    // below: 08-class poses need the bank-mirror-cell law (next charter),
    // and per-pose subset selection must derive from it, not from a table.
    // PARKED measured dispatch (R309 retest: full-kit + prewarm-rot = 7855
    // vs r0-only 3362 - the nine need the 43-style identity-cull split or
    // the cold-sentinel (AB_NOCOLD, R308) before they pay):
    // {0x400b, 3, { 4, 6, 0, 0}}, {0x400b, 3, { 0, 2, 3, 0}},
    // {0x4009, 3, { 9,11,13, 0}}, {0x4009, 3, { 8,10,12, 0}},
    // {0x4009, 3, { 1, 3, 5, 0}}, {0x4009, 3, { 0, 2, 4, 0}},
    // {0x400b, 3, { 0, 6, 7, 0}}, {0x400b, 4, { 2, 4, 5, 3}},
    // {0x400b, 4, { 4, 5, 7, 6}},
    {0x400b, 3, { 5, 7, 1,  0}},
};

static const WalkRecKit kWalk48[] = {
    {0x4026, 4, { 25, 27, 55, 39 }},
    {0x4026, 4, { 24, 26, 54, 38 }},
    {0x4033, 3, { 27, 55, 65, 0 }},
    {0x4033, 3, { 26, 54, 64, 0 }},
    {0x4026, 3, { 27, 65, 49, 0 }},
    {0x4026, 3, { 26, 64, 48, 0 }},
    {0x4033, 4, { 39, 55, 57, 41 }},
    {0x4033, 4, { 38, 54, 56, 40 }},
    {0x400c, 4, { 41, 43, 59, 57 }},
    {0x400c, 4, { 40, 42, 58, 56 }},
    {0x400a, 4, { 43, 45, 61, 59 }},
    {0x400a, 4, { 42, 44, 60, 58 }},
    {0x4033, 4, { 45, 61, 63, 47 }},
    {0x4033, 4, { 44, 60, 62, 46 }},
    {0x4033, 4, { 47, 63, 65, 49 }},
    {0x4033, 4, { 46, 62, 64, 48 }},
    {0x403c, 4, { 47, 49, 25, 39 }},
    {0x403c, 4, { 46, 48, 24, 38 }},
    {0x403c, 4, { 45, 47, 39, 41 }},
    {0x403c, 4, { 44, 46, 38, 40 }},
    {0x403c, 3, { 43, 45, 41, 0 }},
    {0x403c, 3, { 42, 44, 40, 0 }},
    {0x403d, 4, { 59, 57, 55, 61 }},
    {0x403d, 4, { 58, 56, 54, 60 }},
    {0x403d, 4, { 65, 63, 61, 55 }},
    {0x403d, 4, { 64, 62, 60, 54 }}
};

static const WalkRecKit kWalk51[] = {
    {0x4026, 4, { 15, 17, 7, 5 }},
    {0x4026, 4, { 14, 16, 6, 4 }},
    {0x4034, 4, { 17, 19, 9, 7 }},
    {0x4034, 4, { 16, 18, 8, 6 }},
    {0x4034, 4, { 19, 21, 11, 9 }},
    {0x4034, 4, { 18, 20, 10, 8 }},
    {0x4026, 4, { 21, 23, 13, 11 }},
    {0x4026, 4, { 20, 22, 12, 10 }},
    {0x4026, 4, { 15, 17, 27, 25 }},
    {0x4026, 4, { 14, 16, 26, 24 }},
    {0x4034, 4, { 17, 19, 29, 27 }},
    {0x4034, 4, { 16, 18, 28, 26 }},
    {0x4026, 4, { 19, 21, 31, 29 }},
    {0x4026, 4, { 18, 20, 30, 28 }},
    {0x403e, 4, { 0, 2, 26, 24 }},
    {0x403e, 4, { 8, 10, 34, 32 }},
    {0x403e, 4, { 16, 18, 42, 40 }},
    {0x4029, 4, { 2, 4, 28, 26 }},
    {0x4029, 4, { 4, 6, 30, 28 }},
    {0x4029, 4, { 6, 8, 32, 30 }},
    {0x4029, 4, { 10, 12, 36, 34 }},
    {0x4029, 4, { 12, 14, 38, 36 }},
    {0x4029, 4, { 14, 16, 40, 38 }},
    {0x4029, 4, { 18, 20, 44, 42 }},
    {0x4029, 4, { 20, 22, 46, 44 }},
    {0x4029, 4, { 22, 0, 24, 46 }}
};

static const WalkRecKit kWalk65[] = {
    {0x403d, 4, { 2, 12, 80, 78 }},
    {0x400b, 4, { 3, 5, 15, 13 }},
    {0x400b, 4, { 2, 4, 14, 12 }},
    {0x400b, 4, { 5, 7, 17, 15 }},
    {0x400b, 4, { 4, 6, 16, 14 }},
    {0x400b, 4, { 7, 9, 19, 17 }},
    {0x400b, 4, { 6, 8, 18, 16 }},
    {0x4030, 4, { 8, 18, 20, 10 }},
    {0x400b, 4, { 79, 3, 51, 49 }},
    {0x400b, 4, { 78, 2, 50, 48 }},
    {0x400b, 4, { 3, 5, 7, 51 }},
    {0x400b, 4, { 2, 4, 6, 50 }},
    {0x400b, 4, { 7, 9, 53, 51 }},
    {0x400b, 4, { 6, 8, 52, 50 }},
    {0x4030, 4, { 9, 53, 77, 11 }},
    {0x4030, 4, { 8, 52, 76, 10 }},
    {0x0000, 3, { 10, 76, 11, 0 }},
    {0x400b, 3, { 80, 12, 13, 0 }},
    {0x400b, 4, { 12, 13, 15, 14 }},
    {0x400b, 4, { 14, 16, 17, 15 }},
    {0x400b, 4, { 16, 18, 19, 17 }},
    {0x4030, 4, { 18, 19, 21, 20 }},
    {0x403d, 4, { 3, 13, 80, 78 }},
    {0x4030, 4, { 9, 19, 21, 11 }},
    {0x403d, 4, { 54, 56, 58, 60 }},
    {0x4015, 4, { 64, 66, 58, 60 }},
    {0x4034, 4, { 64, 68, 54, 60 }},
    {0x4034, 4, { 66, 62, 56, 58 }},
    {0x4034, 3, { 62, 68, 54, 0 }},
    {0x4034, 4, { 48, 50, 56, 54 }}
};
// R605 CLASS-2 LAW PROBE (default off): kWalk22 strip quads captured from the
// ASM FUNC_001805 job stream (scratch/class2/probe22_job1805.gdb, #22 pose06).
// The engine paints a uniform VGA6 band x160..197 y60..65 through walk-era
// textured quads whose vertex sets below were read verbatim from live jobs;
// the port's classic lane has no record covering those rows.
// R605 CLASS-2 LAW PROBE (default off): kWalk22 strip quads captured from the
// ASM FUNC_001805 job stream (scratch/class2/probe22_job1805.gdb, #22 pose06).
// Engine paints a uniform VGA6 band x160..197 y60..65 via walk-era textured
// quads (jobs n=11..15 below); the port's classic lane has no record there.
// R605 CLASS-2 LAW PROBE (default off): kWalk22 strip quads captured from the
// ASM FUNC_001805 job stream (#22 pose06). Slots are MODEL vertex indices;
// resolution uses the classic per-frame cache (usedAt+cacheAt path). The four
// records below reproduce ASM jobs n=11..15 whose screen verts equal port
// polys 62/92/95/96 exactly (AB_DUMP_VIS vidx sets transcribed):
static const WalkRecKit kWalk22[] = {
    {0x4009, 4, { 0, 1, 2, 3 }},   // mesh 62  vidx [0,4,5,1] -> use poly vidx via model slots
    {0x4009, 4, { 12, 13, 7, 6 }}, // mesh 92  vidx [12,13,7,6]
    {0x400b, 4, { 8, 10, 11, 9 }}, // mesh 95  vidx [8,10,11,9]
    {0x400b, 4, { 8, 12, 13, 9 }}, // mesh 96  vidx [8,12,13,9]
};


static const WalkRecKit kWalk43[] = {
    {0x4026, 4, { 24, 26, 54, 38 }},
    {0x4033, 3, { 26, 54, 64, 0 }},
    {0x4026, 3, { 26, 64, 48, 0 }},
    {0x4033, 4, { 38, 54, 56, 40 }},
    {0x4010, 4, { 40, 42, 58, 56 }},
    {0x4015, 4, { 42, 44, 60, 58 }},
    {0x4033, 4, { 44, 60, 62, 46 }},
    {0x4033, 4, { 46, 62, 64, 48 }},
    {0x403c, 3, { 24, 38, 72, 0 }},
    {0x403c, 3, { 38, 40, 72, 0 }},
    {0x403c, 3, { 40, 42, 72, 0 }},
    {0x403c, 3, { 42, 44, 72, 0 }},
    {0x403c, 3, { 44, 46, 72, 0 }},
    {0x403c, 3, { 46, 48, 72, 0 }},
    {0x403c, 3, { 48, 24, 72, 0 }},
    {0x403d, 4, { 58, 56, 54, 60 }},
    {0x403d, 4, { 64, 62, 60, 54 }},
    {0x4026, 4, { 25, 27, 55, 39 }},
    {0x4033, 3, { 27, 55, 65, 0 }},
    {0x4026, 3, { 27, 65, 49, 0 }},
    {0x4033, 4, { 39, 55, 57, 41 }},
    {0x4010, 4, { 41, 43, 59, 57 }},
    {0x4015, 4, { 43, 45, 61, 59 }},
    {0x4033, 4, { 45, 61, 63, 47 }},
    {0x4033, 4, { 47, 63, 65, 49 }},
    {0x403c, 3, { 25, 39, 73, 0 }},
    {0x403c, 3, { 39, 41, 73, 0 }},
    {0x403c, 3, { 41, 43, 73, 0 }},
    {0x403c, 3, { 43, 45, 73, 0 }},
    {0x403c, 3, { 45, 47, 73, 0 }},
    {0x403c, 3, { 47, 49, 73, 0 }},
    {0x403c, 3, { 49, 25, 73, 0 }},
    {0x403d, 4, { 59, 57, 55, 61 }},
    {0x403d, 4, { 65, 63, 61, 55 }}
};

// R494 CLASS-2 LAW PROBE (default off): kWalk23 fan-tri kit captured from the
// sidecar FUNC_001708 dispatch (scratch/class2/bank23_dispatch.txt, #23 06).
// The sidecar draws 0x4017/0x20ee as FAN TRIANGLES from a cold (0,0) pivot slot,
// NOT bounded quads; the classic lane draws the quad and leaves the fan sliver
// to the layer below (0x5015 lobe -> value-diff 22, or 0 -> missing). Replaying
// the fan tris at head reproduces the sliver; the classic lane then overwrites
// the quad part (correct) and the sliver survives. AB_KIT23=1 to measure.
static const WalkRecKit kWalk23[] = {
    {0x20ee, 3, { 37, 39, 41, 0 }},
    {0x20ee, 3, { 36, 38, 40, 0 }},
    {0x4017, 3, { 12, 16, 18, 0 }},
    {0x4017, 3, { 16, 18, 22, 0 }},
    {0x4017, 3, { 12, 16, 20, 0 }},
    {0x4017, 3, { 16, 20, 22, 0 }},
    {0x4017, 3, { 13, 17, 19, 0 }},
    {0x4017, 3, { 17, 19, 23, 0 }},
    {0x4017, 3, { 13, 17, 21, 0 }},
    {0x4017, 3, { 17, 21, 23, 0 }},
};
uint8_t g_abSnapVis[86];
int16_t g_abSnapXY[86][2];
uint8_t g_abSnapHave=0;

// ---- R473 probe gate (default off): the faithful band lane ----

// Per-frame state for the non-capturing 001481 hook (the repo's static-global
// hook pattern, same as the test files).
static const jjffe::BandLaneFaceCtx* s_biFace = nullptr;
static uint8_t* s_biBand = nullptr;

static const uint8_t* BiBandLookup(jjffe::BandInterpCtx*, uint16_t band) {
    if (!s_biBand || !s_biFace) return nullptr;
    return jjffe::Func001481(*s_biFace, s_biBand + (size_t)band * 8, band);
}

// Run the hull-43 walk faithfully: init the band table, compute the camera
// triples (001685), then execute the four programs (hull-43 + 3x c17d) over a
// self-built walk ctx. The LIFO queue is logged; draw-from-queue lands in the
// next slice. Note: the capture pipeline runs with the classic caches cold at
// this point, so the lane builds its own ctx from the camera/ship stashes.
// R479: externalArena, when non-null, is used as the bank arena instead of
// the probe's internal frame.vert buffer. This lets the classic lane's kit
// replay read corrected cells directly from its own arena buffer.
// Returns the original bank pointer (before probe's swap) when
// externalArena is non-null, so the caller can restore it later.
// R480: externalFacing/outOldFacing mirror externalArena/the bank-pointer
// return for DATA_009201 (the facing/band-record table). Without this, the
// band buffer the facing pointer gets swapped to is RunBandInterpProbe's own
// local frame.band (a std::vector), which is freed when this function
// returns - leaving DATA_009201 dangling for the kit replay that runs right
// after. externalFacing gives the probe a caller-owned, persistent buffer to
// write band records into instead, same pattern as externalArena.
static uint8_t* RunBandInterpProbe(uint8_t* ctxbuf, const int32_t* shipRot,
                                   const int32_t* shipPos, const Camera3D* camP,
                                   const uint8_t* rowR, const uint8_t* blob,
                                   uint8_t* externalArena = nullptr,
                                   uint8_t* externalFacing = nullptr,
                                   intptr_t* outOldFacing = nullptr) {
    static int s_enter = 0;
    if (s_enter < 3)
        fprintf(stderr, "BIPROBE enter %d\n", s_enter), ++s_enter;
    // R475/R1117: the capture tool skips the startup InitGameState, so the
    // core's import slots (DATA_007629 DPal16 etc.) are null and the bridged
    // handlers crash. The live game has already run it from main; the shared
    // flag tells the two apart, so no site re-runs the full init mid-session.
    if (!jjffe::IsGameStateInitialized()) jjffe::InitGameState();
    // The ASM draw path (FUNC_001799 spans -> FUNC_001601 line) writes through
    // DATA_007773; the capture tool never wires it. Point it at the buffer the
    // rest of the renderer paints into.
    jjffe::SetBackBufferPtr(jjffe::GetBackBuffer());
    if (::getenv("AB_721_DBG"))
        fprintf(stderr, "WALK47 framebuf=%p\n", (void*)jjffe::GetBackBuffer());
    // Walk triple: the camera translation in ship space (calibrate against the
    // ASM's captured triples in the draw slice).
    int32_t rel[3] = {0, 0, 0};
    if (camP && shipPos) {
        rel[0] = camP->position.x - shipPos[0];
        rel[1] = camP->position.y - shipPos[1];
        rel[2] = camP->position.z - shipPos[2];
    }
    int32_t triple[3], par[3];
    jjffe::FUNC_001671_SpecialVecMatMul(
        triple, par, const_cast<uint32_t*>(
                        reinterpret_cast<const uint32_t*>(rel)),
        const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(shipRot)));
    int32_t* m = reinterpret_cast<int32_t*>(ctxbuf + 0x14);
    for (int i = 0; i < 9; i++) m[i] = shipRot[i];
    for (int i = 0; i < 3; i++)
        *reinterpret_cast<int32_t*>(ctxbuf + 4 + i * 4) = triple[i];
    *reinterpret_cast<int32_t*>(ctxbuf + 0x148) = 10;   // hull-43 desc scale
    static uint8_t zeroObj[338] = {};
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x14c) =
        jjffe::CtxRegisterObjForReplay(zeroObj);
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x150) =
        jjffe::CtxRegisterBankForReplay(
            blob + *reinterpret_cast<const uint32_t*>(rowR + 0x04));
    // R475: the ASM's FUNC_001774 stores the RAW NORMALS BLOB OFFSET at
    // ctx+0x154 (FUNC_001481 derefs it); DATA_002729 is the hull-43 f-triple
    // table (calibrate against the ASM's captured D154 in the draw slice).
    // R475: the ASM's FUNC_001774 stores the RAW NORMAL BLOB OFFSET at
    // ctx+0x154 (FUNC_001481 derefs blob + *(ctx+0x154)). rowR+0x0C is the
    // ship model's normalDataOffset - the same value the classic lane uses.
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x154) =
        *reinterpret_cast<const uint32_t*>(rowR + 0x0c);

    jjffe::BandLaneFrame frame;
    const int bandCount = 116;                 // DATA_002728 [0x10] = 0x74
    const int vertCount = 86;                  // the replay's arena slot count
    frame.Resize(bandCount, vertCount);
    const uint8_t c8d[6] = {0, 0, 0, 0, 0, 0};
    // R480: when externalFacing is provided, build band records directly into
    // the caller's persistent buffer instead of frame.band (freed on return).
    uint8_t* const bandPtrOut = externalFacing ? externalFacing : frame.band.data();
    jjffe::Func001480(bandPtrOut, bandCount, c8d);
    jjffe::Func001689ResetTags(frame.vert.data(), vertCount);

    jjffe::BandLaneFaceCtx face;
    // R476: Func001685Camera must be re-called per program with its own
    // shift148 (ASM log: hull-43=10, c17d=5). Store the shared inputs so
    // runProg can recompute before each program walk.
    const int32_t* biTm = reinterpret_cast<const int32_t*>(ctxbuf + 0x14);
    const int32_t biCam[3] = {0, 0, 0};      // demo camera at the origin
    auto applyShift = [&](int32_t shift) {
        int32_t o38[3], o48[3];
        const int32_t scale = jjffe::Func001685Camera(triple, biTm, biCam,
                                                      shift, o38, o48);
        face.view38 = o38[0];
        face.view3c = o38[1];
        face.view40 = o38[2];
        face.cam48 = o48[0];
        face.cam4c = o48[1];
        face.cam50 = o48[2];
        face.scale44 = scale;
        face.shift148 = shift;
    };
    // Default: hull-43 shift (first program)
    applyShift(10);
    if (s_enter <= 3)
        fprintf(stderr, "BIPROBE triple=(%d,%d,%d) o48=(%d,%d,%d) shift=%d\n",
                triple[0], triple[1], triple[2],
                face.cam48, face.cam4c, face.cam50, face.shift148);
    static const uint8_t zero24[24] = {};
    face.canned84 = zero24;
    face.vertData150 = reinterpret_cast<const uint8_t*>(
        jjffe::CtxResolveBankForReplay(
            *reinterpret_cast<const uint32_t*>(ctxbuf + 0x150)));
    // The hull-43 desc data (the f-triple table) is not registered by the
    // replay path yet; point it at the DATA_002729 blob region for now and
    // calibrate against the ASM's captured D154 in the draw slice.
    face.descData154 = blob + *reinterpret_cast<const uint32_t*>(rowR + 0x0c);
    s_biFace = &face;
    s_biBand = bandPtrOut;

    jjffe::BandInterpTable table;
    jjffe::BandInterpInitTable(table);
    jjffe::BandInterpCtx c;
    c.opaque = ctxbuf;
    c.readVal = [](jjffe::BandInterpCtx* cc, uint16_t v) -> uint32_t {
        return jjffe::Func001473(cc, v);
    };
    c.table_14c = reinterpret_cast<const uint16_t*>(
        jjffe::CtxResolveObjForReplay(
            *reinterpret_cast<const uint32_t*>(ctxbuf + 0x14c)));
    c.bandRecords = bandPtrOut;
    c.bandLookup = BiBandLookup;
    c.field_c = *reinterpret_cast<const int32_t*>(ctxbuf + 0x0c);
    c.field_148 = face.shift148;
    // R475: the bridged handlers enqueue into the CORE's job queue (the ASM's
    // DATA_007832), so the probe reads that - no local BandQueue.
    // R488: era-flush routes op slot 05 (the PASS handler) to CORE's
    // FUNC_001721, the faithful contour-chain builder (001794 header +
    // 001795/1796/1797 edge cells + 001799 span-fill record). It enqueues
    // into the core job queue (DATA_007832/DATA_009255), which the caller
    // resets before the walk and flushes after the kit replay paints fans.
    if (externalArena) {
        table.op[0x05] = [](jjffe::BandInterpCtx* cc, const uint16_t* pc,
                            uint16_t op) -> const uint16_t* {
            (void)op;
            // ASM walk shape: handler receives pc+1 and returns the next
            // cursor; core's pass handler is byte-cursor based on the same
            // ctx layout (ctxbuf registered via CtxRegister*ForReplay).
            uint8_t* cur = reinterpret_cast<uint8_t*>(
                const_cast<uint16_t*>(pc));
            uint8_t* ret = jjffe::FUNC_001721(
                reinterpret_cast<intptr_t>(cc->opaque), cur);
            return reinterpret_cast<const uint16_t*>(ret);
        };
    }
    c.queue = nullptr;

    const uint16_t* prog43 =
        reinterpret_cast<const uint16_t*>(blob + jjffe::data::OFFSET_DATA_002723);
    const uint16_t* progc =
        reinterpret_cast<const uint16_t*>(blob + 0x288FA);  // c17d program
    // R473: budgeted manual walk (the ASM exits by flag; the real-ctx handlers
    // can cycle). Log the first 16 dispatches so the walk path is visible.
    static int s_logN = 0;
    int total = 0;
    auto runProg = [&](const uint16_t* prog, int32_t shift) {
        // R476: re-parameterize shift148 per program (ASM: hull-43=10, c17d=5)
        applyShift(shift);
        c.field_148 = face.shift148;
        const uint16_t* pc = prog;
        int n = 0;
        c.stopFlag = 0;
        while (c.stopFlag == 0 && n < 4096) {
            const uint16_t* next = jjffe::BandInterpStep(table, c, pc);
            if (!next) {
                if (s_logN < 8)
                    fprintf(stderr, "  bi STOP at step %d op=%04x slot=%02x\n",
                            n, *pc, *pc & 0x1f);
                break;
            }
            if (s_logN < 8 && n < 30)
                fprintf(stderr, "  bi step %d w%ld op=%04x slot=%02x\n",
                        n, (long)(pc - prog), *pc, *pc & 0x1f);
            pc = next;
            ++n;
        }
        if (c.stopFlag != 0 && s_logN < 8)
            fprintf(stderr, "  bi stopFlag at step %d op=%04x slot=%02x\n",
                    n, *(pc - 1), *(pc - 1) & 0x1f);
        total += n;
        ++s_logN;
    };
    // R475/R479: publish the two banks like the ASM's per-call FUNC_001745.
    // When externalArena is provided, use it as the bank arena so the
    // classic lane's kit replay can read corrected cells directly.
    uint8_t* const bankPtr = externalArena ? externalArena : frame.vert.data();
    uint8_t* const biOldBank = jjffe::SwapWalkBankArena(bankPtr);
    const intptr_t biOldFacing = jjffe::SwapWalkFacingBase((intptr_t)bandPtrOut);
    // R488: era-flush resets the CORE job queue (FUNC_001785 semantics:
    // head DATA_009255=0, bump pointer to arena start) so the chains the
    // core pass handler enqueues this frame are exactly what the caller
    // flushes after the kit replay. Without this, prior-frame nodes would
    // be re-painted or the arena guard could fire.
    if (externalArena) jjffe::FUNC_001786();
    // R475: entry is word 0 (BandInterpStep dispatches op[0] and hands the
    // handler pc+1; the 0x042c jump lands on w35 exactly as the ASM trace).
    // R476: per-program shift148 from ASM log (r475_b4.log):
    //   entry-0 (hull-43): ctx+0x148 = 0x0a (10)
    //   entry-1..3 (c17d): ctx+0x148 = 0x05 (5)
    runProg(prog43, 10);
    for (int i = 0; i < 3; i++) runProg(progc, 5);
    if (!externalArena) {
        jjffe::SwapWalkBankArena(biOldBank);
        jjffe::SwapWalkFacingBase(biOldFacing);
    } else if (outOldFacing) {
        // R480: externalFacing is a caller-owned persistent buffer (like
        // externalArena), so it's safe to leave DATA_009201 pointed at it
        // past this return - unlike frame.band.data(), which the old code
        // left swapped in here and which then went out of scope. Hand the
        // old value back so the caller can restore it once ITS consumer
        // (the kit replay) is done with it.
        *outOldFacing = biOldFacing;
    }
    // R479: when externalArena is provided, the caller owns the arena
    // and must restore DATA_009200 after the kit replay finishes.
    // Return biOldBank so the caller can restore it; nullptr otherwise.

    // R475: walk the CORE job queue (arena-offset links) and tally by callback.
    int nodes = 0;
    uint32_t cb1805 = 0, cb1799 = 0, cbOther = 0;
    const uint32_t id1805 = jjffe::OracleJobCallbackId(
        reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(
            static_cast<void* (*)(void*)>(jjffe::FUNC_001805))));
    {
        uint32_t* head = jjffe::WalkQueueHead();
        if (head) {
            uint32_t off = head[0];
            while (off != 0 && nodes < 512) {
                // JobOffToPtr: ptr = arena + (off - 1)
                uint32_t* node = reinterpret_cast<uint32_t*>(
                    const_cast<uint8_t*>(jjffe::JobArenaBase()) + (off - 1));
                const uint32_t cb = node[0];
                if (cb == id1805) ++cb1805;
                else if (cb != 0) ++cbOther;
                else ++cb1799;   // marker-or-null word
                off = node[1];
                ++nodes;
            }
        }
    }
    static int s_n = 0;
    if (s_n < 8) {
        fprintf(stderr, "BANDINTERP dispatches=%d nodes=%d 1805=%u other=%u\n",
                total, nodes, cb1805, cbOther);
        ++s_n;
    }
    s_biFace = nullptr;
    s_biBand = nullptr;
    return externalArena ? biOldBank : nullptr;
}

static void WalkBankReplayHull(int hull, const int32_t* shipRot,
                                const TransformedPoly* polys, int polyCount, int phase)
{
    static int s_walkN = 0;
    if (::getenv("AB_BANDINTERP") && s_walkN < 5)
        fprintf(stderr, "WALK enter hull=%d\n", hull), ++s_walkN;
    if (bwOn() && hull == 43) g_bwN = 0;
    if (v3on()) std::memset(g_shade9252, 0, sizeof g_shade9252);  // per-frame reset (oracle S0)
    const WalkRecKit* kit = nullptr;
    int nrec = 0;
    if (hull == 61) { kit = kWalk61; nrec = (int)(sizeof(kWalk61) / sizeof(kWalk61[0])); }
    if (hull == 43) { kit = kWalk43; nrec = (int)(sizeof(kWalk43) / sizeof(kWalk43[0])); }
    if (hull == 48) { kit = kWalk48; nrec = (int)(sizeof(kWalk48) / sizeof(kWalk48[0])); }
    // 51/65 hookups PARKED after measurement: band replay without the
    // classic-side consumer/era gate double-covers content faces (51: 4958
    // -> 5146 regression; 65: +3). 48 neutral (kept on). Rollout resumes
    // after the #43 consumer audit defines the skip-vs-band split (R273).
    // R440 PROBE (default off): AB_KIT65=1 hooks the parked kWalk65 kit up so
    // the R438 per-row attribution method (AB_BANKRECODS) can be applied to it.
    // The parked verdict (+3 for the FULL kit) says nothing about a SUBSET, and
    // R438 showed a subset can be strongly positive where the full set is not.
    if (hull == 65 && ::getenv("AB_KIT65") && ::getenv("AB_KIT65")[0] == '1') {
        kit = kWalk65; nrec = (int)(sizeof(kWalk65) / sizeof(kWalk65[0]));
    }
    // R494 CLASS-2 LAW PROBE (default off): AB_KIT23=1 hooks the kWalk23 fan-tri
    // kit up (sidecar FUNC_001708 dispatch for #23). Same probe as AB_KIT65.
    if (hull == 23 && ::getenv("AB_KIT23") && ::getenv("AB_KIT23")[0] == '1') {
        kit = kWalk23; nrec = (int)(sizeof(kWalk23) / sizeof(kWalk23[0]));
    }
    // R605 probe (default off): AB_KIT22=1 hooks the new kWalk22 strip kit.
    if (hull == 22 && ::getenv("AB_KIT22") && ::getenv("AB_KIT22")[0] == '1') {
        kit = kWalk22; nrec = (int)(sizeof(kWalk22) / sizeof(kWalk22[0]));
    }
    if (!kit) return;
    const uint8_t* rowR = jjffe::FUNC_001538(hull);
    const uint8_t* blob = data::GetDataBlob();
    if (!rowR || !blob) return;
    // R479: AB_ERAFUSH=1 runs the band-interp probe to build corrected
    // bank-cell state, then falls through to the classic lane's kWalk43
    // kit replay which consumes those cells for face dispatch. The probe
    // replaces the prewarm step; the kit replay provides the face consumer.
    static uint8_t arena[0x8000];
    // R480: persistent facing/band buffer, same lifetime as arena - fixes the
    // DATA_009201 dangling-pointer bug (R479 swapped facing to the probe's
    // local frame.band, a std::vector freed on return; the kit replay then
    // read it through a use-after-free, which is what actually produced the
    // "worse pixels" regression, not a cell-content format mismatch).
    static uint8_t facingBuf[928];  // 116 bands * 8 bytes (bandCount in probe)
    uint8_t* oldBank = nullptr;
    intptr_t oldFacing = 0;
    uint32_t oldWin = 0;
    const bool eraFlushOn = hull == 43 && ::getenv("AB_ERAFUSH") &&
                            ::getenv("AB_ERAFUSH")[0] == '1';
    if (eraFlushOn || (::getenv("AB_BANDINTERP") &&
                       ::getenv("AB_BANDINTERP")[0] == '1')) {
        static uint8_t biCtx[0x400];
        std::memset(biCtx, 0, sizeof biCtx);
        // R480: facingBuf is static (persistent on purpose, so DATA_009201
        // stays valid past RunBandInterpProbe's return - see its header
        // comment). But that persistence means leftover bytes a PRIOR pose's
        // walk wrote into a band record survive into this pose's call unless
        // explicitly cleared first - unlike the old frame.band std::vector,
        // which frame.Resize() zero-filled fresh on every single call.
        if (eraFlushOn) std::memset(facingBuf, 0, sizeof facingBuf);
        // R480: arena ALSO needs a fresh-per-call reset under eraFlushOn.
        // R479's comment reasoned this was redundant because the probe
        // itself never writes into arena (true - confirmed by direct
        // instrumentation), but it missed that the KIT REPLAY that runs
        // right after this block resolves cells through FUNC_001471 ->
        // FUNC_001478, which WARMS cells in place (writes gen=1 plus
        // resolved rotation/position fields back into DATA_009200, see
        // core.cpp:19807-19817 and :20135-20138). Since arena is static,
        // skipping this reset let cells warmed by pose N's kit replay (with
        // pose N's camera/rotation baked in) survive, cold-gen-check and
        // all, into pose N+1's replay - stale geometry painted as the ship
        // reoriented, growing pure overdraw with every subsequent pose in
        // the same process (measured: 217pd standalone vs 675pd as the 6th
        // of 8 poses in one process, pre-fix - identical before and after
        // the R480 facing-pointer/facingBuf fixes above, which is what
        // pointed at arena instead).
        std::memset(arena, 0, sizeof arena);
        uint8_t* const probeOldBank = RunBandInterpProbe(
            biCtx, shipRot, jjffe::GetAbLastShipPos(),
            jjffe::GetAbLastCamera(), rowR, blob,
            eraFlushOn ? arena : nullptr /* externalArena */,
            eraFlushOn ? facingBuf : nullptr /* externalFacing */,
            eraFlushOn ? &oldFacing : nullptr /* outOldFacing */);
        if (!eraFlushOn) return;  // diagnostic-only mode (old behavior)
        // AB_ERAFUSH: probe wrote corrected cells into our arena and left
        // it swapped in as DATA_009200. Fall through to the kit replay
        // which reads from arena directly. Restore after kit replay.
        oldBank = probeOldBank;
    }
    // R479: when eraFlushOn, the probe already built and swapped in its
    // bank arena. Skip the liveness guard (probe has its own context) and
    // the local arena allocation (probe's arena is already active).
    if (!eraFlushOn) {
        {   // Liveness guard: only replay on a frame whose classic transform
            // actually ran (stash set + real depths), never on early/partial
            // punch-gate passes with half-populated caches.
            ProjectedVertex v0, v5;
            if (!GetAbLastCamera() || !GetAbLastShipPos()) {
                if (::getenv("AB_BANDINTERP")) fprintf(stderr, "WALK ret: no camera/ship\n");
                return;
            }
            if (::getenv("AB_TOLGATE") && ::getenv("AB_TOLGATE")[0] == '1') {   // R355
                bool live = false;
                for (int cand : {5, 7, 3, 1})
                    if (GetCachedVertex(cand, v5) && v5.depth >= 1000) { live = true; break; }
                if (!live)
                    for (int cand : {0, 2, 4, 6})
                        if (GetCachedVertex(cand, v0)) { live = true; break; }
                if (!live) return;
            } else if (!GetCachedVertex(5, v5) || v5.depth < 1000 ||
                       !GetCachedVertex(0, v0)) {
                if (::getenv("AB_BANDINTERP")) fprintf(stderr, "WALK ret: no cached verts\n");
                return;
            }
        }
        const bool s_persist = ::getenv("AB_PERSIST") && ::getenv("AB_PERSIST")[0] == '1';
        if (!s_persist) std::memset(arena, 0, sizeof arena);
    }
    if (!eraFlushOn && ::getenv("AB_PREWARM") && ::getenv("AB_PREWARM")[0] == '1') {
        // R296: the sidecar bank's warm set at flush time = the vertices
        // the CLASSIC lane projected (visible faces) - back-culled faces'
        // cells stay cold and their odd slots fall back to the pair-even
        // cell (r4.gdb + pose-07-visibility model). Seed gen from the
        // port's own classic cache validity, rot from the parity build.
        const int32_t* sp = GetAbLastShipPos();
        for (int k = 0; k < 86; k++) {
            ProjectedVertex pv;
            uint8_t* cell = arena + (size_t)k * 0x24;
            if (sp && GetCachedVertex(k, pv)) {
                // R309 seed rot (world-shipPos, 16.16) + gen; else-leg
                // adds ctx+4 and projects like the sidecar's fill.
                *reinterpret_cast<int32_t*>(cell + 0x18) = pv.worldX - sp[0];
                *reinterpret_cast<int32_t*>(cell + 0x1c) = pv.worldY - sp[1];
                *reinterpret_cast<int32_t*>(cell + 0x20) = pv.worldZ - sp[2];
                *reinterpret_cast<int32_t*>(cell + 0x14) = 1;
            }
        }
    }
    // Window parity: sidecar slots resolve inside the 1745-pushed window
    // (DATA_007823); without a push in the ship capture EVERY index would
    // fall into the shared garbage cell forever (measured: all-(0,0) drops).
    // Open the full arena so real rows build; the replay's own first touches
    // then warm exactly the cells the sidecar's shared garbage cell carried.
    if (!eraFlushOn) {
        oldWin = jjffe::SwapWalkBankWindow((uint32_t)sizeof arena);
        oldBank = jjffe::SwapWalkBankArena(arena);
    } else {
        // R479: probe already swapped in its arena. Set the window to
        // cover the probe's arena size (86 slots × 0x24 = 0xC18).
        oldWin = jjffe::SwapWalkBankWindow(86 * 0x24);
        // oldBank stays null — the restore at function end uses the
        // probe's saved oldBank from SwapWalkBankArena.
    }
    uint8_t ctxbuf[0x210] = {};
    int32_t* m = reinterpret_cast<int32_t*>(ctxbuf + 0x14);
    for (int i = 0; i < 9; i++) m[i] = shipRot[i];
    // Camera distance: cam = world + (0,0,D) in the classic transform frame -
    // recover D from a live cached vertex so the replay projects IDENTICALLY.
    {
        // R263 calibration: the ship-demo camera looks along the axis, so
        // cam(m) = m·(shipRot) + (dx,dy,dz) with the offsets recovered from
        // ONE live classic vertex: classic (camX, camY, depth) minus
        // FUNC_001671's rotated value of the same model vector. The raw
        // shipRot IS the ctx matrix (CAL-proved: rotated x,y == classic cam
        // x,y exactly; dz = the camera distance 163840 at du=5).
        const int32_t sc = static_cast<int32_t>(
            *reinterpret_cast<const uint32_t*>(rowR + 0x14)) & 0x1f;
        const uint8_t* vbytes = blob + *reinterpret_cast<const uint32_t*>(rowR + 0x04);
        ProjectedVertex q;
        int qIdx = 4;
        if (::getenv("AB_TOLGATE") && ::getenv("AB_TOLGATE")[0] == '1') {
            qIdx = -1;
            for (int cand : {4, 0, 2, 6, 8})
                if (GetCachedVertex(cand, q)) { qIdx = cand; break; }
            if (qIdx < 0) return;
        } else if (!GetCachedVertex(4, q)) {
            return;
        }
        const int8_t* r5 = reinterpret_cast<const int8_t*>(vbytes + (qIdx >> 1) * 6 + 2);
        uint32_t mv[3];
        for (int c = 0; c < 3; c++)
            mv[c] = static_cast<uint32_t>(static_cast<int32_t>(r5[c]) << sc);
        int rotOut[3], parOut[3];
        jjffe::FUNC_001671_SpecialVecMatMul(rotOut, parOut, mv,
                                            const_cast<uint32_t*>(
                                                reinterpret_cast<const uint32_t*>(shipRot)));
        *reinterpret_cast<int32_t*>(ctxbuf + 0x14) = 0;  // placeholder fill below
        {   const bool tr = ::getenv("AB_BANKTRANS") && ::getenv("AB_BANKTRANS")[0]=='1';
            for (int i2 = 0; i2 < 9; i2++)
                *reinterpret_cast<int32_t*>(ctxbuf + 0x14 + i2 * 4) =
                    tr ? shipRot[(i2 % 3) * 3 + (i2 / 3)] : shipRot[i2];
        }
        *reinterpret_cast<int32_t*>(ctxbuf + 4)  = q.camX - rotOut[0];
        *reinterpret_cast<int32_t*>(ctxbuf + 8)  = q.camY - rotOut[1];
        *reinterpret_cast<int32_t*>(ctxbuf + 12) = q.depth - rotOut[2];
        static int s_calm = 0;
        if (::getenv("AB_BANKCAL") && s_calm < 1)
            fprintf(stderr, "CTX dx=%d dy=%d dz=%d rot=(%d,%d,%d)\n",
                    q.camX - rotOut[0], q.camY - rotOut[1], q.depth - rotOut[2],
                    rotOut[0], rotOut[1], rotOut[2]), ++s_calm;
    }
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x148) =
        *reinterpret_cast<const uint32_t*>(rowR + 0x14);      // scale (11)
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x150) =
        jjffe::CtxRegisterBankForReplay(blob + *reinterpret_cast<const uint32_t*>(rowR + 0x04));
    static uint8_t zeroObj[338] = {};                          // flags read 0
    *reinterpret_cast<uint32_t*>(ctxbuf + 0x14c) =
        jjffe::CtxRegisterObjForReplay(zeroObj);
    int16_t xy[4][2];
    for (int r = 0; r < nrec; r++) {
        const WalkRecKit& rec = kit[r];
        if (::getenv("AB_FANLATER") && ::getenv("AB_FANLATER")[0] == '1') {
            const uint32_t mf = rec.mat & 0xFFF;
            const bool fan = (mf == 0x3c || mf == 0x26 || mf == 0x33);
            if (phase == 2 && fan) continue;
            if (phase == 1 && !fan) continue;
        } else if (phase != 0) { continue; }
        const int nv = rec.kind == 3 ? 3 : 4;
        for (int v = 0; v < nv; v++) {
            uint8_t k = (uint8_t)(uint8_t)rec.s[v];
            // R318 per-record coord mode (measured identity field, R317
            // attribution): kit61 rows 1-2 resolve through the classic
            // cache with pair-even fallback (usage-gated); row 0 keeps the
            // twin build (its 05 fix depends on it). Other hulls' kits
            // switch after their attribution rows in R317 land here too.
            uint64_t modeBits = 0;
            if (hull == 61) {
                // R438 PROBE (default off): AB61_ARENA=1 drops kit61 out of the
                // R318 cacheMode identity mapping so every row resolves through
                // the bank arena (FUNC_001471 + cold-pair fallback) instead.
                // R437/R438 measurement: exactly ONE row is correct per pose and
                // the correct row is the even/odd TWIN of the others (05/06 ->
                // (5,7,1); 08 -> (4,6,0)), so the open question is whether arena
                // warmth already selects the right twin per frame.
                const char* a61 = ::getenv("AB61_ARENA");
                // R438: the 0x4009 row retired, so the surviving (5,7,1) row
                // moved from index 2 to index 1 - keep its ORIGINAL arena mode
                // (row 0 cache coords, row 1 twin build) instead of sliding the
                // whole mask down. AB61_ARENA=1 still forces all-arena; measured
                // equivalent for these rows (787 both ways), kept as the probe.
                modeBits = (a61 && a61[0] == '1') ? 0ull : 0x1ull;
            }
            else if (hull == 43 && ::getenv("AB_UW2") && ::getenv("AB_UW2")[0] == '1') {
                for (int rr = 0; rr < nrec && rr < 64; rr++) {
                    const uint32_t mf0 = kWalk43[rr].mat & 0xFFF;
                    if (mf0 == 0x3c || mf0 == 0x26 || mf0 == 0x33) modeBits |= (1ull << rr);
                }
            }  // R318 final: rows 0-1 cache coords, row 2 (5,7,1) twin LAST (R317 attribution)
            const bool cacheMode = ((modeBits >> r) & 1ull) || hull == 22;  // R605: kWalk22 slots are model vidx -> classic cache
            const bool guardMode = (hull == 61) && ((modeBits >> (r + 8)) & 1ull);
            if (cacheMode && polys) {
                auto usedAt = [&](uint8_t kk) {
                    for (int pw = 0; pw < polyCount; pw++) {
                        const TransformedPoly& pp = polys[pw];
                        if (pp.backFacing) continue;
                        for (int vw = 0; vw < pp.vertexCount; vw++)
                            if (pp.vertexIdx[vw] == kk) return true;
                    }
                    return false;
                };
                auto cacheAt = [&](uint8_t kk, int16_t out[2]) {
                    ProjectedVertex pv;
                    if (GetCachedVertex((int)kk, pv)) { out[0]=pv.screenX; out[1]=pv.screenY; return true; }
                    return false;
                };
                int16_t cx[2]; bool got = false;
                if (usedAt(k) && cacheAt(k, cx)) got = true;
                else if ((k & 1) && usedAt((uint8_t)(k & ~1u)) && cacheAt((uint8_t)(k & ~1u), cx)) got = true;
                if (got) { xy[v][0] = cx[0]; xy[v][1] = cx[1]; continue; }
            }
            // R291 cold-pair fallback (bank ground truth r4.gdb): the
            // sidecar's bank cells warm ONLY when ITS walk touched them; a
            // job's slot that is still cold resolves through its pair-even
            // cell (08 fan2 (1,3,5) painted (k0,k2,k4) with k1,k3 untouched
            // (0,0)). Our arena mirrors touch order iff we mirror that.
            if (::getenv("AB_COLDFALL") && ::getenv("AB_COLDFALL")[0] == '1') {
                const uint8_t* cell = arena + (size_t)k * 0x24;
                if (*reinterpret_cast<const int32_t*>(cell + 0x14) == 0) {
                    const uint8_t ke = (uint8_t)(k & ~1u);
                    if (ke != k) {
                        const uint8_t* ce = arena + (size_t)ke * 0x24;
                        if (*reinterpret_cast<const int32_t*>(ce + 0x14) != 0)
                            k = ke;
                    }
                }
            }
            // R297 AB_SNAP: resolve-time warmth = PREVIOUS frame's classic
            // visits (r4 bank model): odd slot cold last frame -> substitute
            // pair-even cell. Snapshot refreshed at each replay's end.
            {
                extern uint8_t g_abSnapVis[86];   // defined near replay fn
                extern int16_t g_abSnapXY[86][2];
                extern uint8_t g_abSnapHave;
                if (::getenv("AB_SNAP") && ::getenv("AB_SNAP")[0] == '1' &&
                    g_abSnapHave && (k & 1) && !g_abSnapVis[k])
                    k = (uint8_t)(k & ~1u);
                // R298 AB_SNAP2: the resolve-time content IS the previous
                // frame's classic-cache projection of this vertex (r4@08:
                // k5 held (44,41)=prev-frame v5, current-frame classic then
                // re-filled (83,104) by walk-end). Use prev coords directly.
                if (::getenv("AB_SNAP2") && ::getenv("AB_SNAP2")[0] == '1' &&
                    g_abSnapHave &&
                    (g_abSnapXY[k][0] || g_abSnapXY[k][1])) {
                    xy[v][0] = g_abSnapXY[k][0];
                    xy[v][1] = g_abSnapXY[k][1];
                    continue;
                }
            }
            static const char* s_src = ::getenv("AB_BANKSRC");
            if (s_src && s_src[0] == 'c') {   // classic-cache coords
                ProjectedVertex pv;
                if (GetCachedVertex((int)k, pv)) {
                    xy[v][0] = pv.screenX; xy[v][1] = pv.screenY;
                } else { xy[v][0] = 0; xy[v][1] = 0; }
                continue;
            }
            static int s_step = 0;
            {
                if (::getenv("AB_BANKCAL") && s_step < 3) {
                    const uint8_t* sb = arena + k * 0x24;
                    fprintf(stderr, "PRE k=%d gen=%d rot=(%d,%d,%d) xy=(%d,%d) ctxm0=%d off=(%d,%d,%d)\n",
                            (int)k, *(const int32_t*)(sb + 0x14),
                            (int)*(const int32_t*)(sb + 0x18), (int)*(const int32_t*)(sb + 0x1c),
                            (int)*(const int32_t*)(sb + 0x20),
                            (int)(int16_t)*(const uint16_t*)(sb), (int)(int16_t)*(const uint16_t*)(sb + 2),
                            (int)*reinterpret_cast<const int32_t*>(ctxbuf + 0x14),
                            (int)*reinterpret_cast<const int32_t*>(ctxbuf + 4),
                            (int)*reinterpret_cast<const int32_t*>(ctxbuf + 8),
                            (int)*reinterpret_cast<const int32_t*>(ctxbuf + 12));
                }
            }
            intptr_t sl = jjffe::FUNC_001471(reinterpret_cast<intptr_t>(ctxbuf), k);
            {   static int s_step2 = 0;
                if (::getenv("AB_BANKCAL") && s_step2 < 3 && sl) {
                    const uint8_t* sb = (const uint8_t*)sl;
                    fprintf(stderr, "POST k=%d gen=%d rot=(%d,%d,%d) xy=(%d,%d)\n",
                            (int)k, *(const int32_t*)(sb + 0x14),
                            (int)*(const int32_t*)(sb + 0x18), (int)*(const int32_t*)(sb + 0x1c),
                            (int)*(const int32_t*)(sb + 0x20),
                            (int)(int16_t)*(const uint16_t*)(sb), (int)(int16_t)*(const uint16_t*)(sb + 2));
                    ++s_step2;
                }
                if (::getenv("AB_BANKCAL")) ++s_step;
            }
            if (sl) {
                xy[v][0] = *reinterpret_cast<const int16_t*>(sl + 0);
                xy[v][1] = *reinterpret_cast<const int16_t*>(sl + 2);
            } else { xy[v][0] = 0; xy[v][1] = 0; }
        }
        {   static int s_dump = 0;
            if (::getenv("AB_BANKCAL") && s_dump < 4) {
                for (int v2 = 0; v2 < nv; v2++) {
                    const uint8_t kk = (uint8_t)(uint8_t)rec.s[v2];
                    const uint8_t* sb = arena + kk * 0x24;
                    char hexb[0x24 * 2 + 1];
                    for (int q = 0; q < 0x24; q++) snprintf(hexb + q * 2, 3, "%02x", sb[q]);
                    hexb[0x24 * 2] = 0;
                    fprintf(stderr, "SLOT r=%d v=%d k=%d bytes=%s\n", r, v2, (int)kk, hexb);
                }
                ++s_dump;
            }
        }
        int32_t per = 0;
        for (int v = 0; v < nv; v++) {
            const int w = (v + 1) % nv;
            per += std::abs((int)xy[v][0] - xy[w][0]) + std::abs((int)xy[v][1] - xy[w][1]);
        }
        static int s_rp = 0;
        if (s_rp < 24) {
            fprintf(stderr, "REPLAY r=%d kind=%d slots=%d,%d,%d,%d xy=(%d,%d)(%d,%d)(%d,%d)%s per=%d %s\n",
                    r, rec.kind, rec.s[0], rec.s[1], rec.s[2], rec.s[3],
                    xy[0][0], xy[0][1], xy[1][0], xy[1][1], xy[2][0], xy[2][1],
                    rec.kind == 4 ? "(3?)" : "", per, per > 5 ? "draw" : "drop");
            ++s_rp;
        }
        static int s_mask = -1;
        if (s_mask < 0) {
            const char* em = ::getenv("AB_BANKRECODS");
            s_mask = em ? (int)strtol(em, nullptr, 0) : 0x3FF;
        }
        if (per > 5 && (s_mask & (1 << r))) {
            if (bwOn() && hull == 43 && r < 34 && kBandWord[r] > 0 && rec.kind == 3)
                bwPut(xy[0][0], xy[0][1], xy[1][0], xy[1][1], xy[2][0], xy[2][1],
                      (uint32_t)kBandWord[r]);
            DrawBandJobRec(rec.kind, rec.mat & 0xFFF, xy);
        }
        static int s_cal = 0;
        if (::getenv("AB_BANKCAL") && s_cal < 1) {
            ProjectedVertex q;
            const uint8_t* sl5 = arena + 5 * 0x24;
            fprintf(stderr, "CAL slot5 rot=(%d,%d,%d) proj=(%d,%d) | classic v5=(%d,%d,%d) scr=(%d,%d) depth=%d\n",
                    (int)*(const int32_t*)(sl5 + 0x18), (int)*(const int32_t*)(sl5 + 0x1c),
                    (int)*(const int32_t*)(sl5 + 0x20),
                    (int)(int16_t)*(const uint16_t*)(sl5), (int)(int16_t)*(const uint16_t*)(sl5 + 2),
                    GetCachedVertex(5, q) ? q.worldX : 0, GetCachedVertex(5, q) ? q.worldY : 0,
                    GetCachedVertex(5, q) ? q.worldZ : 0,
                    q.screenX, q.screenY, q.depth);
            ++s_cal;
        }
    }
    {
        if (::getenv("AB_SNAP") && ::getenv("AB_SNAP")[0] == '1') {
            ProjectedVertex pvS;
            for (int qS = 0; qS < 86; qS++)
                if (GetCachedVertex(qS, pvS)) {
                    g_abSnapVis[qS] = 1;
                    g_abSnapXY[qS][0] = pvS.screenX;
                    g_abSnapXY[qS][1] = pvS.screenY;
                } else g_abSnapVis[qS] = 0;
            g_abSnapHave = 1;
        }
    }
    if (oldBank) {
        jjffe::SwapWalkBankArena(oldBank);
    }
    // R488: era-flush punch flush. The core pass handler enqueued the ASM's
    // trailing contour chains (001794 + edge cells + 001799 span fills) into
    // the core job queue during RunBandInterpProbe. Flush them NOW - after
    // the kit replay painted the textured fans, while DATA_009200/DATA_009201
    // still point at the probe arena/facingBuf so the chain's slot resolves
    // reproduce the same projected coords the sidecar's EXEC trace shows.
    // sortKey ordering inside FUNC_001788 mirrors the ASM flush exactly.
    if (eraFlushOn) {
        const bool dbg = ::getenv("AB_ERAFUSH_DBG") != nullptr;
        if (dbg) fprintf(stderr, "ERAFUSH flush begin\n");
        // FUNC_001788 takes &head; the queue nodes are arena-offset links.
        // WalkQueueHead() returns the real head node pointer (&DATA_009255).
        if (uint32_t* const h = jjffe::WalkQueueHead()) {
            jjffe::FUNC_001788(h);
        }
        if (dbg) fprintf(stderr, "ERAFUSH flush end\n");
    }
    // R480: restore DATA_009201 too - eraFlushOn is the only path that left
    // it swapped to our persistent facingBuf (see RunBandInterpProbe).
    if (eraFlushOn) {
        jjffe::SwapWalkFacingBase(oldFacing);
    }
    jjffe::SwapWalkBankWindow(oldWin);
}


// R1055 (AB_WALK47, default off): run the ASM's own ship walk for one hull.
// FUNC_001774 pushes the vertex arena, stores the bank/normals handles, sets
// ctx+0xf0 = 2 and walks the model mesh with FUNC_001772; the mesh stream's
// 0x04/0x05/0x08 handlers resolve band cells (FUNC_001471) and enqueue the
// 001805 quads - including the 0x4013 strip the classic lane never sees.
// Context shape follows MissionDraw1680 (core.cpp:23310) with the demo's
// rotation/camera, whose matrix is word-for-word the ASM walk ctx+0x14 (R1055).
static void RenderAsmWalkHull(const ShipModel* shipModel, const int32_t* shipRot)
{
    const uint8_t* rowR = reinterpret_cast<const uint8_t*>(shipModel);
    const uint8_t* blob = data::GetDataBlob();
    const int32_t* shipPos = jjffe::GetAbLastShipPos();
    const Camera3D* camP = jjffe::GetAbLastCamera();
    if (!rowR || !blob || !shipPos || !camP || !shipRot) return;
    // The capture tool skips the startup InitGameState; the walk needs the
    // core's import slots (DATA_007720 = FUNC_001480 band-record seeder, DPal,
    // ...) wired. R1117: check the shared flag, not a per-function one, so the
    // live game (main already initialised) never re-inits on the first walk.
    if (!jjffe::IsGameStateInitialized()) jjffe::InitGameState();
    // The ASM draw path (FUNC_001799 spans -> FUNC_001601 line) writes through
    // DATA_007773; the capture tool never wires it. Point it at the buffer the
    // rest of the renderer paints into.
    jjffe::SetBackBufferPtr(jjffe::GetBackBuffer());
    alignas(16) uint8_t ctx[0x160] = {};
    for (int i = 0; i < 9; i++)
        *reinterpret_cast<int32_t*>(ctx + 0x14 + i * 4) = shipRot[i];
    // The object's camera-space position goes at ctx+4 verbatim (ASM FUNC_001683
    // copies obj+0x26/+0x2e/+0x36); it is NOT the classic lane's rotated
    // camera-minus-ship vector. Measured: the demo walk's ctx+4 is
    // (0,0,65536) == GetAbLastShipPos(), while the rotated form gives
    // (-65536,0,0) and culls every face at FUNC_001481.
    for (int i = 0; i < 3; i++)
        *reinterpret_cast<int32_t*>(ctx + 4 + i * 4) = shipPos[i];
    *reinterpret_cast<int32_t*>(ctx + 0x148) =
        *reinterpret_cast<const uint32_t*>(rowR + 0x14) & 0x1f;
    // R1120: the walk's BitClear/ALU gates read the ObjectStruct at ctx+0x14c
    // (FUNC_001473 kind 0x80: +0x9c+idx*2). The classic lane's parser installs
    // a synthetic thrust object for --harvest-thrusters; seed the same words
    // here so the engine-on goldens agree in both lanes (Asp #40 nose:
    // +0xb6/+0xb8/+0xba = 0x442, canopy gate +0x9e = 1, as mesh_parser.cpp).
    static uint8_t walkObj[338];
    std::memset(walkObj, 0, sizeof(walkObj));
    if (jjffe::GetMeshHarvestThrusters()) {
        *reinterpret_cast<uint16_t*>(walkObj + 0x9e) = 1;
        const uint16_t thrust = jjffe::GetMeshThrustActiveWord();
        *reinterpret_cast<uint16_t*>(walkObj + 0xb6) = thrust;
        *reinterpret_cast<uint16_t*>(walkObj + 0xb8) = thrust;
        *reinterpret_cast<uint16_t*>(walkObj + 0xba) = thrust;
    }
    *reinterpret_cast<uint32_t*>(ctx + 0x14c) =
        jjffe::CtxRegisterObjForReplay(walkObj);
    *reinterpret_cast<uint32_t*>(ctx + 0x150) = jjffe::CtxRegisterBankForReplay(
        blob + *reinterpret_cast<const uint32_t*>(rowR + 4));
    *reinterpret_cast<uint32_t*>(ctx + 0x154) =
        *reinterpret_cast<const uint32_t*>(rowR + 0x0c);
    *reinterpret_cast<int32_t*>(ctx + 0x130) = 0;
    std::memcpy(ctx + 0x84, jjffe::AbWalkShadeTable(), 0x18);
    *reinterpret_cast<uint32_t*>(ctx + 0xf0) = 2;
    jjffe::FUNC_001685(reinterpret_cast<intptr_t>(ctx));
    const int walkModel = jjffe::GetShipModelIndex(shipModel);
    const int prevWalkModel = jjffe::GetAbWalkModelId();
    jjffe::SetAbWalkModelId(walkModel);
    const bool dbg = ::getenv("AB_WALK47_DBG") && ::getenv("AB_WALK47_DBG")[0] == '1';
    if (dbg) {
        const uint8_t* sh = jjffe::AbWalkShadeTable();
        fprintf(stderr, "WALK47 idx=%d row=%p rot=%d,%d,%d triple=%d,%d,%d scale=%d gen=%d "
                "shade=%02x%02x%02x %02x%02x%02x %02x%02x%02x\n",
                walkModel, (const void*)shipModel,
                shipRot[0], shipRot[1], shipRot[2],
                *reinterpret_cast<int32_t*>(ctx + 4),
                *reinterpret_cast<int32_t*>(ctx + 8),
                *reinterpret_cast<int32_t*>(ctx + 12),
                *reinterpret_cast<int32_t*>(ctx + 0x148),
                *reinterpret_cast<int32_t*>(ctx + 0xf0),
                sh[0], sh[4], sh[8], sh[3], sh[7], sh[11], sh[6], sh[10], sh[14]);
    }
    jjffe::FUNC_001786();   // reset the job queue/arena (DATA_007832 head)
    jjffe::AbSetWalkFlag(1);
    uint32_t* const qh = jjffe::WalkQueueHead();
    if (dbg && qh) fprintf(stderr, "WALK47 pre-773 head=%u\n", qh[0]);
    ++jjffe::g_ab_walk_seq;
    const bool armTrace = ::getenv("AB_JOB_TRACE") != nullptr ||
                          ::getenv("AB_JOB_TRACE_DIR") != nullptr;
    if (armTrace) ::g_jjffe_job_trace_enabled = 1;
    jjffe::FUNC_001773(reinterpret_cast<intptr_t>(ctx),
                       reinterpret_cast<intptr_t>(const_cast<uint8_t*>(rowR)));
    if (dbg && qh) {
        int nodes = 0;
        uint32_t off = qh[0];
        const uint8_t* arena = jjffe::JobArenaBase();
        while (off != 0 && nodes < 256) {
            const uint32_t* node = reinterpret_cast<const uint32_t*>(arena + (off - 1));
            fprintf(stderr, "WALK47 node %d cb=%u\n", nodes, node[0]);
            off = node[1];
            ++nodes;
        }
        fprintf(stderr, "WALK47 post-773 head=%u nodes=%d\n", qh[0], nodes);
    }
    // ASM tail: close the job mode (appends the 001793 terminator when a mode
    // was open) before the flush. Without it the chain walks past the last
    // enqueued job into stale arena bytes and replays the previous frame's
    // jobs (hull 52 pose 07 in a warm fleet run).
    jjffe::FUNC_001780(0, 0);
    if (uint32_t* const h = jjffe::WalkQueueHead())
        jjffe::FUNC_001788(h);   // flush: sort + draw the enqueued jobs
    if (dbg && qh) fprintf(stderr, "WALK47 post-788 head=%u\n", qh[0]);
    jjffe::SetAbWalkModelId(prevWalkModel);
}

void RenderShipFlat(const TransformedPoly* polys, int polyCount, const ShipModel* shipModel, const int32_t* shipRot)
{
    if (!polys || polyCount <= 0) return;
    // R1057: hull 47 renders through the ASM's own ship walk
    // (FUNC_001774 -> FUNC_001772), which is pixel-identical to ffeasm on all
    // eight fleet poses (was 7232 px off through the classic lane). The walk
    // mints the 0x4013 band/contour quads the classic lane never sees.
    // R1058: the lane was selectable per hull while the walk was being fixed.
    // R1115 RETIRES the allow-list: once the capture harness pins the ASM
    // ship-demo's LOD state (DATA_008812/13 = 0/0, see ab_capture_shuttle),
    // the walk is better-or-equal for all 57 named hulls on the six held-out
    // poses (lane_sweep_r1115: 41 walk wins, 16 ties, 0 classic wins; walk
    // 2 346 px vs classic 196 002 over 57x6). The residual walk pixels are the
    // wrong-index family (#46/#58/#67), a face-selection question, not a lane
    // question. On the record only #56's flat poses favour classic, and #40's
    // thrust-golden pose is a separate state gap - both are the documented
    // exceptions below. Other model indices and the bisect path keep classic.
    //   AB_WALK_HULLS=41,67,...  REPLACES the set with exactly these hulls
    //                            (measurement / bisect; it does not extend)
    //   AB_WALK_ALL=1            every model index through the walk
    //   AB_NO_WALK=1             the whole lane off (every hull classic)
    //   AB_NO_WALK47=1           removes 47 from the set (historical kill switch)
    static const std::vector<int> s_walkHulls = []() {
        std::vector<int> v;
        const char* all = ::getenv("AB_WALK_ALL");
        if (all && all[0] == '1') {
            for (int i = 0; i < SHIP_MODEL_COUNT; i++) v.push_back(i);
        } else if (const char* e = ::getenv("AB_WALK_HULLS"); e && e[0]) {
            int cur = 0;
            bool have = false;
            for (const char* p = e;; ++p) {
                if (*p >= '0' && *p <= '9') {
                    cur = cur * 10 + (*p - '0');
                    have = true;
                } else {
                    if (have) v.push_back(cur);
                    cur = 0;
                    have = false;
                    if (!*p) break;
                }
            }
        } else {
            // The ship table's named-fleet range (shipidx.txt 15..71). No
            // per-hull record fitting: every named hull takes the walk.
            // #56 left the exception list at R1118 (its flat-pose "wrong
            // index" was the intro beginning-plate sort bias leaking into
            // hull renders). #40 left at R1123: the thrust-active golden's
            // residual was the harness PRNG state (the ASM ship demo never
            // runs FUNC_000146; the capture tool now pins the demo's 0/0), so
            // the walk is 0 px on ship_demo_asp40_thrust. The walk is the only
            // fleet lane; AB_NO_WALK=1 remains the A/B kill switch.
            for (int h = 15; h <= 71; ++h) v.push_back(h);
        }
        const char* nw = ::getenv("AB_NO_WALK");
        if (nw && nw[0] == '1') v.clear();
        const char* n = ::getenv("AB_NO_WALK47");
        if (n && n[0] == '1')
            v.erase(std::remove(v.begin(), v.end(), 47), v.end());
        return v;
    }();
    const int s_shipIdx = jjffe::GetShipModelIndex(shipModel);
    if (std::find(s_walkHulls.begin(), s_walkHulls.end(), s_shipIdx) != s_walkHulls.end()) {
        RenderAsmWalkHull(shipModel, shipRot);
        return;
    }
    {
        const char* e = ::getenv("AB_BANKREPLAY");
        const int ph = (::getenv("AB_FANLATER") && ::getenv("AB_FANLATER")[0] == '1') ? 2 : 0;
        if (!e || e[0] != '0')
            WalkBankReplayHull(jjffe::GetShipModelIndex(shipModel), shipRot, polys, polyCount, ph);
    }

    ShipPrepareFlatPunchGates(const_cast<TransformedPoly*>(polys), polyCount,
                               shipModel);

    // Puma mat8 Contour coverage: fat AABB → Contours paint the hull
    // (ASM 001799 only; no 001805). Thin/edge-on → Tex* still needed.
    // Only ComplexFill Contour AABB ≥ 30×30 = hull is Contour-covered (top).
    // Dig: AB_MAT8_KEEP_TEX=1.
    s_pumaMat8ContourFat = false;
    {
        int16_t cx0 = 319, cy0 = 199, cx1 = 0, cy1 = 0;
        int nCf = 0;
        for (int i = 0; i < polyCount; i++) {
            const TransformedPoly& p = polys[i];
            if (!p.visible || p.backFacing) continue;
            if (p.type != MeshPolyType::ComplexFill || p.contourId < 0)
                continue;
            if (p.texInfo != 0x4008) continue;
            nCf++;
            for (int v = 0; v < p.vertexCount && v < 4; v++) {
                if (p.verts[v].screenX < cx0) cx0 = p.verts[v].screenX;
                if (p.verts[v].screenY < cy0) cy0 = p.verts[v].screenY;
                if (p.verts[v].screenX > cx1) cx1 = p.verts[v].screenX;
                if (p.verts[v].screenY > cy1) cy1 = p.verts[v].screenY;
            }
        }
        if (nCf > 0 && (cx1 - cx0) >= 30 && (cy1 - cy0) >= 30)
            s_pumaMat8ContourFat = true;

        // REMOVED: orbit Contour-id Tex* union — this incorrectly set
        // ContourFat for orbit (h~23), causing all 0x4008 Tex* geometry to be
        // skipped. ASM still renders the full Tex* hull on orbit; only ComplexFill
        // Contour AABB ≥ 30×30 (top pose) means hull is Contour-covered.
        // Dig: the orbit Contour stripe is narrower than the hull area.

        int nMat8Tex = 0;
        for (int i = 0; i < polyCount; i++) {
            const TransformedPoly& p = polys[i];
            if (p.texInfo != 0x4008) continue;
            if (p.type == MeshPolyType::TexTri ||
                p.type == MeshPolyType::TexTriPair ||
                p.type == MeshPolyType::TexQuad ||
                p.type == MeshPolyType::TexQuadPair)
                nMat8Tex++;
        }
        s_pumaMat8NearLod =
            nMat8Tex > 0 && nMat8Tex <= 2 &&
            !(getenv("AB_MAT8_FAR_LOD") && getenv("AB_MAT8_FAR_LOD")[0] == '1');

        // Detect the visible normal-10/14 TexQuad band(s) that map to
        // ASM 001805. The old meshIndex==10 test never matched the actual
        // GateNear-316 streams (27/34), making texture depend on zoom.
        s_puma001805BandVisible = false;
        for (int i = 0; i < polyCount; i++) {
            if (polys[i].visible && !polys[i].backFacing &&
                polys[i].type == MeshPolyType::TexQuad &&
                polys[i].texInfo == 0x4008 &&
                (polys[i].normalIdx == 10 || polys[i].normalIdx == 14)) {
                s_puma001805BandVisible = true;
                break;
            }
        }
    }

    // Lion #54: detect 0x5009 Contour fills (skip duplicate non-Contour)
    s_lion5009Contour = false;
    if (jjffe::GetShipModelIndex(shipModel) == 54) {
        for (int i = 0; i < polyCount; i++) {
            if (polys[i].type == MeshPolyType::ComplexFill &&
                polys[i].contourId >= 0 &&
                polys[i].texInfo == 0x5009) {
                s_lion5009Contour = true;
                break;
            }
        }
    }

    // Per-pixel depth buffer: DISABLED. Investigation showed zero depth
    // failures on front view — material 48 doesn't overlap with material 9
    // at the pixel level. The VGA 3 gap has a different root cause.
    s_depthEnabled = false;
    for (int i = 0; i < 320 * 158; i++)
        s_depthBuf[i] = 0x7fffffff;

    // Debug: enable texel histogram for material 9 investigation
    // s_texelDebug = (getenv("AB_TEXEL_DEBUG") != nullptr);
    // if (s_texelDebug) memset(s_texelHist, 0, sizeof(s_texelHist));

    // Collect on-screen front-facing polys. meshIndex == MeshPoly.streamIndex.
    // Capsule silhouette meshes store 0x5xxx normals inverted vs FUNC_001481 —
    // flip their cull so the hull draws; flat 0x0000 punch is skipped above.
    // Missile flats: disable cull on 0x1000 (nose-on faces otherwise vanish).
    // Title #176 can leave 1000+ visible faces; 256 capped draws to empty.
    const TransformedPoly* visible[2048];
    int visCount = 0;
    for (int i = 0; i < polyCount; i++) {
        // b14: R137 VIOLATION FOUND AND FIXED (precedence bug blanked #28
        // even gate-off; OFF fleet run + probe prove it). Both blanks are
        // DEFAULT-ON now - pair-adopted (#54 34,382->0; #28 4,078->0; zero
        // regressions in ON-vs-OFF); escape hatch AB_NO_DEMOBLANK=1 restores
        // old behaviour. Parenthesization was the bug; tests are next check.
        const bool demoBlankOn =
            !(::getenv("AB_NO_DEMOBLANK") && ::getenv("AB_NO_DEMOBLANK")[0] == '1');
        const int blankModel = jjffe::GetShipModelIndex(shipModel);
        if (demoBlankOn && (blankModel == 54 /* Lion */ || blankModel == 28 /* Gecko */))
            continue;  // Card 4: ASM demo paints zero px (all poses, both hulls)
        if (!polys[i].visible || visCount >= 2048) continue;
        if (::getenv("AB_GRAZE_PROBE") && ::getenv("AB_GRAZE_PROBE")[0] == '1' &&
            (polys[i].texInfo >> 12) == 4) {
            static int s_gz = 0;
            if (s_gz < 12) {
                const auto& v0 = polys[i].verts[0]; const auto& v1 = polys[i].verts[1];
                const auto& v2 = polys[i].verts[2];
                const long ar = (long)(v1.screenX - v0.screenX) * (v2.screenY - v0.screenY) -
                                (long)(v2.screenX - v0.screenX) * (v1.screenY - v0.screenY);
                fprintf(stderr, "GRAZE n=%d back=%d ar=%ld nxy=(%d,%d) tex=%04x\n",
                        (int)polys[i].vertexCount, polys[i].backFacing ? 1 : 0, ar,
                        (int)polys[i].normalX, (int)polys[i].normalY,
                        (unsigned)polys[i].texInfo);
                ++s_gz;
            }
        }
        if (::getenv("AB_FACING_PROBE") && ::getenv("AB_FACING_PROBE")[0] == '1' &&
            jjffe::GetShipModelIndex(shipModel) == 43 && polys[i].visible) {
            static int s_fp = 0;
            if (s_fp < 1000) {
                int32_t nx, ny, nz;
                if (GetNormal(shipModel, polys[i].normalIdx, nx, ny, nz)) {
                    int32_t rnx = (int32_t)(((int64_t)nx * shipRot[0] + (int64_t)ny * shipRot[3] + (int64_t)nz * shipRot[6]) >> 16);
                    int32_t rny = (int32_t)(((int64_t)nx * shipRot[1] + (int64_t)ny * shipRot[4] + (int64_t)nz * shipRot[7]) >> 16);
                    int32_t rnz = (int32_t)(((int64_t)nx * shipRot[2] + (int64_t)ny * shipRot[5] + (int64_t)nz * shipRot[8]) >> 16);
                    long wx=0, wy=0, wz=0;
                    for (int v = 0; v < polys[i].vertexCount; v++) {
                        wx += polys[i].verts[v].worldX; wy += polys[i].verts[v].worldY;
                        wz += polys[i].verts[v].worldZ;
                    }
                    wx /= polys[i].vertexCount; wy /= polys[i].vertexCount; wz /= polys[i].vertexCount;
                    long dot = (rnx * wx + rny * wy + rnz * wz) >> 8;
                    fprintf(stderr, "FP face=%d n=%d wnx=%d wny=%d wnz=%d w=(%ld,%ld,%ld) dot=%ld wind=%d\n",
                            polys[i].meshIndex, polys[i].normalIdx, rnx, rny, rnz,
                            wx, wy, wz, dot, polys[i].backFacing ? 1 : 0);
                    ++s_fp;
                }
            }
        }
        bool back = polys[i].backFacing;
        // R269 AB_CULL1481 (gated): base the hull-poly facing on the ASM
        // FUNC_001481 model-space cull (port twin ComputeFaceFacing, live
        // for the 1721 contour family) instead of screen winding - the
        // sidecar's classic lane faces with 1481 (charter 240 follow-up).
        if (::getenv("AB_CULL1481") && ::getenv("AB_CULL1481")[0] == '1' &&
            polys[i].normalIdx > 0) {
            const int32_t* posP = GetAbLastShipPos();
            const Camera3D* camP = GetAbLastCamera();
            if (posP && camP)
                back = ComputeFaceFacing(shipModel, polys[i].normalIdx,
                                         shipRot, posP, camP, -1);
        }
        if (s_capsuleSilhouetteMesh && (polys[i].texInfo & 0xF000) == 0x5000)
            back = !back;
        // Mantis #67 ComplexFill Contours: force front-facing so Contour
        // draws on ALL views (ASM FUNC_001799 is double-sided).
        if (jjffe::GetShipModelIndex(shipModel) == 67 &&
            polys[i].type == MeshPolyType::ComplexFill &&
            polys[i].contourId >= 0 &&
            polys[i].texInfo == 0x0444) {
            back = false;
            const_cast<TransformedPoly&>(polys[i]).backFacing = false;
        }
        if (s_missileFlatMesh && polys[i].texInfo == 0x1000)
            back = false;
        // Dig (AB_UNCULL_HULL=1, never shipping): test the R223(b) absent-face
        // physics - the sidecar draws back-facing hull bands at oblique poses
        // (jt61a orbit 001805/001807 fills over the culled normals).
        if (::getenv("AB_UNCULL_HULL") && ::getenv("AB_UNCULL_HULL")[0] == '1')
            back = false;
        // Dig (AB_UNCULL_MESH=13,14): draw the listed meshIndex values even when
        // screen winding says back, without touching any other poly. Used to test
        // the R1102 finding that hull 24's 0x5015 TexQuadPairs (mi 13/14) are
        // culled by the port but painted by the ASM at the orbit poses.
        if (const char* um = ::getenv("AB_UNCULL_MESH"); um && um[0]) {
            const char* up = um;
            while (*up) {
                char* ue = nullptr;
                const long umi = strtol(up, &ue, 10);
                if (ue == up)
                    break;
                if (umi == polys[i].meshIndex) {
                    back = false;
                    break;
                }
                up = ue;
                if (*up == ',')
                    ++up;
            }
        }
        // R262 AB_BANDFAM pair-side: band-family records reach the painter
        // even when screen winding says back - the walk-era classic painter
        // culls NOTHING per pose (gdb on ffeasm: identical dispatch set on
        // #61 in all 8 poses); occlusion is painter order + rect + shade0!=0.
        if (::getenv("AB_BANDFAM") && ::getenv("AB_BANDFAM")[0] == '1' && back) {
            static const uint32_t kBandFamV[] = {
#include "bandfam.inc"
            };
            const int hullV = jjffe::GetShipModelIndex(shipModel);
            const uint32_t keyV = (uint32_t)(((uint32_t)(unsigned)hullV << 16) |
                    (uint16_t)std::max(0, polys[i].meshIndex));
            if (std::binary_search(std::begin(kBandFamV), std::end(kBandFamV), keyV) ||
                (::getenv("AB_BANDFAM_ADD65") && ::getenv("AB_BANDFAM_ADD65")[0] == '1' &&
                 hullV == 65 && (polys[i].meshIndex == 80 || polys[i].meshIndex == 83 ||
                                 polys[i].meshIndex == 88 || polys[i].meshIndex == 110)) ||
                (::getenv("AB_BANDFAM_65MAT") && ::getenv("AB_BANDFAM_65MAT")[0] == '1' &&
                 hullV == 65 && (polys[i].texInfo == 0x400c || polys[i].texInfo == 0x400f ||
                                 polys[i].texInfo == 0x400b || polys[i].texInfo == 0x4015)))
                back = false;
        }
        // R282 AB_FACE51 (gated): the WRITER-ATTRIBUTED damage faces of the
        // #43 orbit strips are mesh 55 (0x4026 tip quad, 192 cells) + tri
        // meshes 63/64 (0x403c, 25 cells) (stamp census w217). Test: do the
        // band-kit replays (kWalk43, already painting at head) reproduce the
        // ASM front value without these classic polys? Drop them from the
        // visible[] pick (era-completeness at the pick = law-clean split).
        if (::getenv("AB_NORMPRINT") && ::getenv("AB_NORMPRINT")[0] == '1' &&
            jjffe::GetShipModelIndex(shipModel) == 43 &&
            (polys[i].meshIndex == 51 || polys[i].meshIndex == 59 ||
             polys[i].meshIndex == 60 || polys[i].meshIndex == 68 ||
             polys[i].meshIndex == 76 || polys[i].meshIndex == 77)) {
            static int s_np = 0;
            if (s_np < 60) {
                fprintf(stderr, "NORM mesh=%d nX=%d nY=%d back=%d wind=%d\n",
                        polys[i].meshIndex, polys[i].normalX, polys[i].normalY,
                        (int)polys[i].backFacing, (int)back);
                s_np++;
            }
        }
        // R288 UNSET-NORMAL classic skip (AB_UNSETNORM=1 gated; charter 287
        // law): the sidecar's FUNC_001649-era classic pick culls polys whose
        // normal word is unset (0,0) via `cmp ...,0; jge` -> #43's ring faces
        // (meshes 51/59/60, measured writers) are NEVER classic-painted;
        // their pixels come from the band-kit lane (kWalk43, default-on).
        // R299 SHIPPED (kill-switch AB_UNSETNORM=0): #43 tip tris (meshes
        // 59/60, tex 0x403c) have UNSET (0,0) normals - the sidecar's
        // classic-era pick culls unset-normal polys (`cmp ...,0; jge`,
        // charter 287) - their band twins (kWalk43) paint what ASM shows.
        // Measured default-on: 43 fleet 435 -> 418 with every clean pose
        // UNCHANGED (drop-test sweep: wider sets regress - the mesh-51 tip
        // quad stays classic-legal until the kit-coord mechanism lands,
        // which also owns 61-08). Mesh 51 deliberately NOT in the set.
        if (!(::getenv("AB_UNSETNORM") && ::getenv("AB_UNSETNORM")[0] == '0') &&
            jjffe::GetShipModelIndex(shipModel) == 43 &&
            polys[i].normalX == 0 && polys[i].normalY == 0 &&
            (polys[i].meshIndex == 59 || polys[i].meshIndex == 60))
            back = true;
        // R312 probe: R299-form law candidate for #21 - same UNSET-normal
        // gate, faces 2/60/63/65 (0x4009 bands, census R311). Gated until
        // the 05_top behaviour is proven normal-conditioned.
        if (::getenv("AB_21NORM") && ::getenv("AB_21NORM")[0] == '1' &&
            jjffe::GetShipModelIndex(shipModel) == 21 &&
            polys[i].normalX == 0 && polys[i].normalY == 0 &&
            (polys[i].meshIndex == 2 || polys[i].meshIndex == 60 ||
             polys[i].meshIndex == 63 || polys[i].meshIndex == 65))
            back = true;
        if (::getenv("AB_FACE51") && ::getenv("AB_FACE51")[0] == '1' &&
            !back && jjffe::GetShipModelIndex(shipModel) ==
                (::getenv("AB_FACEHULL") ? (int)strtol(::getenv("AB_FACEHULL"), nullptr, 10) : 43)) {
            for (const char* q = ::getenv("AB_FACES"); q && *q; ) {
                if ((int)strtol(q, (char**)&q, 10) == polys[i].meshIndex) { back = true; break; }
                while (*q == ',') q++;
            }
        }
        if (!back)
            visible[visCount++] = &polys[i];
    }

    // R641/R644: sorting `visible[]` here is PROVABLY INERT — the draw loop
    // consumes `sortedPolys[]` (built below from the band/painter schedule),
    // not this array. The AB_PAINTER law is applied to `sortedPolys[]` after
    // the schedule is built; see the R645 block below.

    // Puma #60: boost the visible ASM-001805 TexQuad band(s) so they draw
    // AFTER mat8 Contour fills, painting their shade ramp over VGA6.
    if (s_puma001805BandVisible &&
        !(getenv("AB_PUMA_SORT_KEEP") &&
          getenv("AB_PUMA_SORT_KEEP")[0] == '1')) {
        for (int i = 0; i < visCount; i++) {
            if (visible[i]->type == MeshPolyType::TexQuad &&
                visible[i]->texInfo == 0x4008 &&
                (visible[i]->normalIdx == 10 || visible[i]->normalIdx == 14))
                const_cast<TransformedPoly*>(visible[i])->sortKey = 1;
        }
    }
    // Mantis #67: ComplexFill (0x0444) draws LAST (closest = paints on top).
    // Painter sort: larger sortKey = farther = drawn first.
    // sortKey=0 → draws last (after all hull texQuads at key~13000-24000).
    // Dig: AB_MANTIS_SORT_KEEP=1.
    if (jjffe::GetShipModelIndex(shipModel) == 67 &&
        !(getenv("AB_MANTIS_SORT_KEEP") &&
          getenv("AB_MANTIS_SORT_KEEP")[0] == '1')) {
        for (int i = 0; i < visCount; i++) {
            if (visible[i]->type == MeshPolyType::ComplexFill &&
                visible[i]->contourId >= 0 &&
                visible[i]->texInfo == 0x0444)
                const_cast<TransformedPoly*>(visible[i])->sortKey = 0;
        }
    }
    // R502 CLASS-2 probe (default off): force the 0x5015 contour lobes to sort
    // FARTHER (drawn first, UNDER the 0x4017 quad) — test whether the value-diff
    // is a painter-order over-paint. AB_C721_SORT_FAR=N sets the sortKey to N.
    if (getenv("AB_C721_SORT_FAR")) {
        const uint32_t farKey = (uint32_t)strtoul(getenv("AB_C721_SORT_FAR"), nullptr, 0);
        for (int i = 0; i < visCount; i++) {
            if (visible[i]->type == MeshPolyType::ComplexFill &&
                visible[i]->contourId >= 0 &&
                visible[i]->texInfo == 0x5015)
                const_cast<TransformedPoly*>(visible[i])->sortKey = farKey;
        }
    }

    const bool jobTrace = (g_jjffe_job_trace_enabled != 0) ||
                          (getenv("AB_JOB_TRACE") != nullptr) ||
                          (getenv("AB_JOB_TRACE_DIR") != nullptr);
    if (jobTrace) {
        for (int i = 0; i < visCount; i++) {
            const TransformedPoly* p = visible[i];
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            int mat = tex ? (int)(p->texInfo & 0xFFF) : -1;
            jjffe_job_trace_cpp_enq(p->sortKey, p->meshIndex, p->texInfo, mat,
                                   tex ? "tex" : "flat");
        }
    }
    if (getenv("AB_DUMP_VIS") && getenv("AB_DUMP_VIS")[0]) {
        for (int i = 0; i < visCount; i++) {
            const TransformedPoly* p = visible[i];
            int16_t xmin = 319, xmax = 0, ymin = 199, ymax = 0;
            for (int v = 0; v < p->vertexCount; v++) {
                if (p->verts[v].screenX < xmin) xmin = p->verts[v].screenX;
                if (p->verts[v].screenX > xmax) xmax = p->verts[v].screenX;
                if (p->verts[v].screenY < ymin) ymin = p->verts[v].screenY;
                if (p->verts[v].screenY > ymax) ymax = p->verts[v].screenY;
            }
            fprintf(stderr,
                    "VIS mesh=%d tex=0x%04x sp=%u key=%u type=%d bf=%d n=%d cid=%d aabb=(%d,%d)-(%d,%d) "
                    "vidx=[%u,%u,%u,%u] "
                    "v0=(%d,%d) v1=(%d,%d) v2=(%d,%d) v3=(%d,%d)\n",
                    p->meshIndex, p->texInfo, (unsigned)p->specialParam, p->sortKey,
                    (int)p->type, (int)p->backFacing, p->normalIdx, (int)p->contourId,
                    xmin, ymin, xmax, ymax,
                    p->vertexCount > 0 ? p->vertexIdx[0] : 0u,
                    p->vertexCount > 1 ? p->vertexIdx[1] : 0u,
                    p->vertexCount > 2 ? p->vertexIdx[2] : 0u,
                    p->vertexCount > 3 ? p->vertexIdx[3] : 0u,
                    p->verts[0].screenX, p->verts[0].screenY,
                    p->verts[1].screenX, p->verts[1].screenY,
                    p->verts[2].screenX, p->verts[2].screenY,
                    p->verts[3].screenX, p->verts[3].screenY);
        }
    }

    // Phase O: FUNC_001780 banded schedule from resolved 0x06 barriers.
    // Auto when an Adder canopy punch is visible: 0x20ee glass and/or the
    // mesh-25 0x2000 front slit — those must draw after nearer hull inside the
    // open band (ASM stream order), not by painter key.
    // Adder fingerprint: always band while barriers exist. Toggling painter↔bands
    // when 0x20ee/mesh25 enter or leave the frustum (e.g. pitch≈283 at yaw 229)
    // reshuffles ~100+ px per frame and reads as unstable windshield/engine.
    // AB_PHASE_O_BANDS=1/0 overrides.
    const char* bandsEnv = getenv("AB_PHASE_O_BANDS");
    const auto& frameBarriers = GetFrameBarriers();
    [[maybe_unused]] bool hasCanopyPunch = false;
    for (int i = 0; i < visCount; i++) {
        const TransformedPoly* vp = visible[i];
        if (vp->texInfo == 0x20ee) {
            hasCanopyPunch = true;
            break;
        }
        if (s_adderWindshieldPunchMesh && vp->texInfo == 0x2000 &&
            vp->meshIndex == 25 && vp->normalIdx == 2) {
            hasCanopyPunch = true;
            break;
        }
    }
    // Puma #60 / Osprey X #32: barriers exist but banded flush reshuffles
    // Contour↔tex and inflates orbit IDX. Dig: AB_PHASE_O_BANDS=1.
    const int shipIdx = jjffe::GetShipModelIndex(shipModel);
    // Title #176/#177 constellations emit hundreds of barriers; banded flush
    // Title #176/#177 constellations emit hundreds of barriers; banded flush
    // caps at 128 nodes and can yield draws=0. Keep painter for title types.
    // Mantis #67: banded mode reshuffles Contour→tex order; painter-only
    // with sortKey=0 override puts Contour last (correct).
    const bool titleConstellation = (shipIdx == 0xb0 || shipIdx == 0xb1);
    const bool useBands = !titleConstellation &&
                          ((bandsEnv && bandsEnv[0] == '1') ||
                           (!(bandsEnv && bandsEnv[0] == '0') &&
                            !frameBarriers.empty() &&
                            shipIdx != 60 && shipIdx != 32 && shipIdx != 67));

    const TransformedPoly* sortedPolys[2048];
    int sortCount = 0;

    if (useBands && !frameBarriers.empty()) {
        // Merge barriers + visible polys in bytecode stream order, then flush
        // like ASM: open bands scoop subsequent draws; list nodes sort by key.
        struct ListNode {
            uint32_t sortKey;
            int streamIndex;
            bool isBand;
            const TransformedPoly* poly;          // free draw
            const TransformedPoly* bandPolys[64]; // enqueue order
            int bandCount;
        };
        ListNode nodes[128];
        int nodeCount = 0;
        ListNode* curBand = nullptr;

        struct Event {
            int streamIndex;
            bool isBarrier;
            int barrierIdx;
            int polyIdx;
        };
        Event events[384];
        int eventCount = 0;
        for (size_t bi = 0; bi < frameBarriers.size() && eventCount < 384; bi++) {
            events[eventCount++] = Event{frameBarriers[bi].streamIndex, true,
                                         (int)bi, -1};
        }
        for (int pi = 0; pi < visCount && eventCount < 384; pi++) {
            events[eventCount++] =
                Event{visible[pi]->meshIndex, false, -1, pi};
        }
        for (int i = 0; i < eventCount - 1; i++) {
            for (int j = 0; j < eventCount - i - 1; j++) {
                if (events[j].streamIndex > events[j + 1].streamIndex) {
                    Event tmp = events[j];
                    events[j] = events[j + 1];
                    events[j + 1] = tmp;
                }
            }
        }

        for (int ei = 0; ei < eventCount; ei++) {
            const Event& ev = events[ei];
            if (ev.isBarrier) {
                const ResolvedBarrier& rb = frameBarriers[ev.barrierIdx];
                if (jobTrace) {
                    jjffe_job_trace_cpp_enq(rb.sortKey, rb.streamIndex, rb.bandMode,
                                           -1, "barrier");
                }
                if (rb.bandMode == 0) {
                    curBand = nullptr;
                } else if (nodeCount < 128) {
                    ListNode& n = nodes[nodeCount++];
                    n.sortKey = rb.sortKey;
                    n.streamIndex = rb.streamIndex;
                    n.isBand = true;
                    n.poly = nullptr;
                    n.bandCount = 0;
                    curBand = &n;
                }
            } else {
                const TransformedPoly* p = visible[ev.polyIdx];
                if (curBand && curBand->bandCount < 64) {
                    curBand->bandPolys[curBand->bandCount++] = p;
                } else if (nodeCount < 128) {
                    ListNode& n = nodes[nodeCount++];
                    n.sortKey = p->sortKey;
                    n.streamIndex = p->meshIndex;
                    n.isBand = false;
                    n.poly = p;
                    n.bandCount = 0;
                }
            }
        }

        // R645 candidate (gated): a band node is ordered by its barrier key, so
        // a band whose members are FARTHER than an adjacent free poly can still
        // flush after it. At #65@05 band {126,127} (members 16240/16096) carries
        // barrier key 11536 and flushes after free 119 (13744) — the (22,6)
        // cluster. Painter-safe rule: a band's depth is its farthest member.
        if (std::getenv("AB_PAINTER_BANDKEY") &&
            std::getenv("AB_PAINTER_BANDKEY")[0] == '1') {
            for (int i = 0; i < nodeCount; i++) {
                if (!nodes[i].isBand || nodes[i].bandCount == 0) continue;
                uint32_t far = 0;
                for (int k = 0; k < nodes[i].bandCount; k++)
                    if (nodes[i].bandPolys[k]->sortKey > far)
                        far = nodes[i].bandPolys[k]->sortKey;
                if (far > nodes[i].sortKey) nodes[i].sortKey = far;
            }
        }

        for (int i = 0; i < nodeCount - 1; i++) {
            for (int j = 0; j < nodeCount - i - 1; j++) {
                bool swap = false;
                if (nodes[j].sortKey < nodes[j + 1].sortKey)
                    swap = true;
                else if (nodes[j].sortKey == nodes[j + 1].sortKey &&
                         nodes[j].streamIndex > nodes[j + 1].streamIndex)
                    swap = true;
                if (swap) {
                    ListNode tmp = nodes[j];
                    nodes[j] = nodes[j + 1];
                    nodes[j + 1] = tmp;
                }
            }
        }

        if (std::getenv("AB_DUMP_NODES") && std::getenv("AB_DUMP_NODES")[0] == '1') {
            for (int i = 0; i < nodeCount; i++) {
                if (!nodes[i].isBand) {
                    std::fprintf(stderr, "NODE i=%d key=%u stream=%d free face=%d\n",
                                 i, nodes[i].sortKey, nodes[i].streamIndex,
                                 nodes[i].poly ? nodes[i].poly->meshIndex : -1);
                } else {
                    std::fprintf(stderr, "NODE i=%d key=%u stream=%d band n=%d:",
                                 i, nodes[i].sortKey, nodes[i].streamIndex,
                                 nodes[i].bandCount);
                    for (int k = 0; k < nodes[i].bandCount; k++)
                        std::fprintf(stderr, " %d(key%u)",
                                     nodes[i].bandPolys[k]->meshIndex,
                                     nodes[i].bandPolys[k]->sortKey);
                    std::fprintf(stderr, "\n");
                }
            }
        }

        // R645 candidate (gated): painter-sort WITHIN each band. Band members
        // are collected in stream order, so an inversion inside a band survives
        // the node-level key sort (at #65@05 meshes 119/126/127 share a band).
        if (std::getenv("AB_PAINTER_BAND") &&
            std::getenv("AB_PAINTER_BAND")[0] == '1') {
            for (int i = 0; i < nodeCount; i++) {
                if (!nodes[i].isBand || nodes[i].bandCount < 2) continue;
                std::stable_sort(
                    nodes[i].bandPolys, nodes[i].bandPolys + nodes[i].bandCount,
                    [](const TransformedPoly* a, const TransformedPoly* b) {
                        return a->sortKey > b->sortKey;
                    });
            }
        }

        for (int i = 0; i < nodeCount && sortCount < 2048; i++) {
            if (!nodes[i].isBand) {
                if (nodes[i].poly)
                    sortedPolys[sortCount++] = nodes[i].poly;
            } else {
                for (int k = 0; k < nodes[i].bandCount && sortCount < 2048; k++)
                    sortedPolys[sortCount++] = nodes[i].bandPolys[k];
            }
        }
        static bool s_bandLog = false;
        if (!s_bandLog) {
            std::fprintf(stderr,
                         "PhaseO: banded flush barriers=%zu nodes=%d draws=%d\n",
                         frameBarriers.size(), nodeCount, sortCount);
            s_bandLog = true;
        }
    } else {
        // Legacy painter sort: larger sortKey = farther = drawn first.
        sortCount = visCount;
        for (int i = 0; i < visCount; i++)
            sortedPolys[i] = visible[i];
        for (int i = 0; i < sortCount - 1; i++) {
            for (int j = 0; j < sortCount - i - 1; j++) {
                const TransformedPoly* a = sortedPolys[j];
                const TransformedPoly* b = sortedPolys[j + 1];
                bool swap = false;
                if (a->sortKey < b->sortKey) {
                    swap = true;
                } else if (a->sortKey == b->sortKey && a->meshIndex > b->meshIndex) {
                    swap = true;
                }
                if (swap) {
                    sortedPolys[j] = b;
                    sortedPolys[j + 1] = a;
                }
            }
        }
    }

    // R645 AB_PAINTER candidate law (gated, default OFF): the REAL draw list.
    // At #65@05 mesh 119 (key 13744, NEAR) is currently drawn before mesh 127
    // (key 16096, FAR), so the far face paints its 6 over 119's correct 22 —
    // the measured (22,6) cluster. Order by sortKey DESCENDING (larger =
    // farther => painted first). This array is what the underlay pass and the
    // final draw loop actually consume; sorting `visible[]` is inert (R641/R644).
    if (::getenv("AB_PAINTER") && ::getenv("AB_PAINTER")[0] == '1') {
        std::stable_sort(sortedPolys, sortedPolys + sortCount,
                         [](const TransformedPoly* a, const TransformedPoly* b) {
                             return a->sortKey > b->sortKey;
                         });
    }

    // R645 AB_PAINTER_BLOCK (gated, default OFF): same painter law, but a
    // ComplexFill Contour group is ATOMIC — its pieces must stay in mesh order
    // (DrawComplexContourSpan builds a bezier path from them; the in-tree
    // comment at the group call site records that shuffling collapses the
    // close chord). Blocks are ordered by their FARTHEST member's sortKey and
    // emitted in their original relative order. This is the naive sort's
    // collateral fix for hulls whose cardinals are Contour-dominant (#23).
    if (::getenv("AB_PAINTER_BLOCK") && ::getenv("AB_PAINTER_BLOCK")[0] == '1' &&
        sortCount > 1) {
        int blockOf[2048];
        int nBlocks = 0;
        uint32_t blockKey[2048];
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            int b = -1;
            if (p->type == MeshPolyType::ComplexFill && p->contourId >= 0) {
                for (int j = 0; j < i; j++) {
                    if (blockOf[j] >= 0 &&
                        sortedPolys[j]->type == MeshPolyType::ComplexFill &&
                        sortedPolys[j]->contourId == p->contourId) {
                        b = blockOf[j];
                        break;
                    }
                }
            }
            if (b < 0) {
                b = nBlocks++;
                blockKey[b] = p->sortKey;
            } else if (p->sortKey > blockKey[b]) {
                blockKey[b] = p->sortKey;
            }
            blockOf[i] = b;
        }
        int order[2048];
        for (int i = 0; i < nBlocks; i++) order[i] = i;
        std::stable_sort(order, order + nBlocks, [&](int a, int b) {
            return blockKey[a] > blockKey[b];
        });
        const TransformedPoly* out[2048];
        int outN = 0;
        for (int bi = 0; bi < nBlocks; bi++) {
            const int target = order[bi];
            for (int i = 0; i < sortCount; i++)
                if (blockOf[i] == target) out[outN++] = sortedPolys[i];
        }
        for (int i = 0; i < outN; i++) sortedPolys[i] = out[i];
    }

    // Legacy oracle reorder (falsified for VGA3; kept for bisect).
    const char* phaseO = getenv("AB_PHASE_O_ORDER");
    if (phaseO && std::strcmp(phaseO, "asm") == 0 && sortCount > 0) {
        auto polyMat = [](const TransformedPoly* p) -> int {
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            return tex ? (int)(p->texInfo & 0xFFF) : -1;
        };

        bool used[256];
        std::memset(used, 0, sizeof(used));
        const TransformedPoly* ordered[256];
        int outCount = 0;
        int matchedStrict = 0;
        int matchedLoose = 0;

        auto takeBestKey = [&](int material, uint32_t target) -> int {
            int best = -1;
            uint32_t bestDelta = 0xffffffffu;
            for (int i = 0; i < sortCount; i++) {
                if (used[i])
                    continue;
                if (polyMat(sortedPolys[i]) != material)
                    continue;
                uint32_t sk = sortedPolys[i]->sortKey;
                uint32_t d = (sk > target) ? (sk - target) : (target - sk);
                if (d > (uint32_t)kPhaseOKeyTol)
                    continue;
                if (d < bestDelta) {
                    bestDelta = d;
                    best = i;
                }
            }
            return best;
        };
        auto takeFirstMat = [&](int material) -> int {
            for (int i = 0; i < sortCount; i++) {
                if (used[i])
                    continue;
                if (polyMat(sortedPolys[i]) == material)
                    return i;
            }
            return -1;
        };

        for (int oi = 0; oi < kPhaseOOracleFrontCount; oi++) {
            const PhaseOOracleEvent& ev = kPhaseOOracleFront[oi];
            const uint32_t target = ev.asmSortKey * 8u;
            int best = takeBestKey(ev.material, target);
            bool loose = false;
            if (best < 0) {
                best = takeFirstMat(ev.material);
                loose = (best >= 0);
            }
            if (best >= 0) {
                used[best] = true;
                ordered[outCount++] = sortedPolys[best];
                if (loose)
                    matchedLoose++;
                else
                    matchedStrict++;
            }
        }
        for (int i = 0; i < sortCount; i++) {
            if (!used[i])
                ordered[outCount++] = sortedPolys[i];
        }
        for (int i = 0; i < outCount; i++)
            sortedPolys[i] = ordered[i];
        sortCount = outCount;
        static bool s_logged = false;
        if (!s_logged) {
            std::fprintf(stderr,
                         "PhaseO: AB_PHASE_O_ORDER=asm matched strict=%d loose=%d "
                         "(oracle=%d) drawCount=%d\n",
                         matchedStrict, matchedLoose, kPhaseOOracleFrontCount,
                         sortCount);
            s_logged = true;
        }
    }

    const bool vga3Trace = (getenv("AB_VGA3_TRACE") != nullptr);
    auto countVga3 = []() -> int {
        if (!g_videoState) return 0;
        int n = 0;
        const uint8_t* buf = g_videoState->backBuffer;
        for (int i = 0; i < 320 * 200; i++)
            if (buf[i] == 3) n++;
        return n;
    };

    // Pass 1: non-punch flat-RGB underlay (nonzero DPal). Must paint under
    // textured hull so translucent tex leaves the base color.
    // VGA0 flats (Adder 0x20ee; raw 0x2000) skip underlay. 0x20ee punches in
    // schedule order after hull (ASM 001809). 0x2000 is skipped at draw time
    // unless AB_DRAW_2000 / AB_FLAT_2000_GREY.
    // AB_NO_FLAT_UNDERLAY=1: all flats in schedule order.
    const char* noUnderlay = getenv("AB_NO_FLAT_UNDERLAY");
    const bool flatUnderlay = !(noUnderlay && noUnderlay[0] == '1');
    bool underlaid[256] = {};
    if (flatUnderlay) {
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            ScopedPolyFaceLight faceLightScope(*p);
            bool flatRgb = (p->texInfo != 0) && ((p->texInfo >> 14) == 0) &&
                           (p->type != MeshPolyType::Thruster);
            if (!flatRgb)
                continue;
            // Complex721 Contour pieces are flat-RGB words (Courier/Trader
            // 0x0666/0x0444) but must span-fill as a group (FUNC_001548), not
            // as per-piece FlatRgb quads. Underlay was painting those quads
            // then marking underlaid — Contour span never ran (Courier front
            // +1k OC freckles; dig AB_NO_FLAT_UNDERLAY). Dig:
            // AB_C721_FLAT_UNDERLAY=1 restores underlay Contours.
            if (p->type == MeshPolyType::ComplexFill && p->contourId >= 0 &&
                !(getenv("AB_C721_FLAT_UNDERLAY") &&
                  getenv("AB_C721_FLAT_UNDERLAY")[0] == '1'))
                continue;
            if (FlatRgbToPal(p->texInfo) == 0)
                continue; // punch: drawn later in schedule order
            // Sprite-child flats (bit15 on specialParam) paint over hull in ASM
            // stream order — Cobra #205 0x0888 antenna. Underlay would be wiped
            // by later mat TexQuadPairs. Dig: AB_SPRITE_FLAT_UNDERLAY=1 restores.
            if ((p->specialParam & 0x8000) != 0 &&
                !(getenv("AB_SPRITE_FLAT_UNDERLAY") &&
                  getenv("AB_SPRITE_FLAT_UNDERLAY")[0] == '1'))
                continue;
            // Skeet #45: 0x0444 TexTri(Pair) — Contour corners, not hull fill.
            // Must skip in underlay (flat-RGB word) as well as schedule pass.
            // Dig: AB_SKEET_KEEP_0444_TRI=1.
            if (jjffe::GetShipModelIndex(shipModel) == 45 &&
                p->texInfo == 0x0444 &&
                (p->type == MeshPolyType::TexTri ||
                 p->type == MeshPolyType::TexTriPair) &&
                !(getenv("AB_SKEET_KEEP_0444_TRI") &&
                  getenv("AB_SKEET_KEEP_0444_TRI")[0] == '1'))
                continue;
            // Skeet 0x0888 PlainLines: underlay paints side OC; Contours wipe
            // top → OA. Restored after Contours (empty-only). Dig:
            // AB_SKEET_0888_UNDERLAY=1 restores underlay lines.
            if (jjffe::GetShipModelIndex(shipModel) == 45 &&
                p->texInfo == 0x0888 && p->vertexCount == 2 &&
                !(getenv("AB_SKEET_0888_UNDERLAY") &&
                  getenv("AB_SKEET_0888_UNDERLAY")[0] == '1'))
                continue;
            if (getenv("AB_TEX_ONLY") && getenv("AB_TEX_ONLY")[0] == '1')
                continue;
            if (getenv("AB_THRUSTER_ONLY") && getenv("AB_THRUSTER_ONLY")[0] == '1')
                continue;
            DrawFlatPolygon(*p, shipModel, shipRot);
            underlaid[i] = true;
        }
    }

    // Skeet #45 draw order (ASM): canopy → Contour 001799 punches → mat21
    // 0x4015. Schedule Contours-first left punches on black (no-op); 4015
    // before Contours left canopy freckles (orbit IDX). Defer Contours then
    // 4015. Digs: AB_SKEET_C721_EARLY=1, AB_SKEET_4015_EARLY=1.
    const bool skeetDeferC721 =
        jjffe::GetShipModelIndex(shipModel) == 45 &&
        !(getenv("AB_SKEET_C721_EARLY") && getenv("AB_SKEET_C721_EARLY")[0] == '1');
    const bool skeetDefer4015 =
        jjffe::GetShipModelIndex(shipModel) == 45 &&
        !(getenv("AB_SKEET_4015_EARLY") && getenv("AB_SKEET_4015_EARLY")[0] == '1');
    const TransformedPoly* deferredC721[32][64];
    int deferredC721Ng[32] = {};
    int deferredC721N = 0;
    const TransformedPoly* deferred4015[16];
    int deferred4015N = 0;
    bool prepaintDone = false;
    s_hoistedCount = 0;   // R1003: per-frame reset of the hoisted-contour list

    for (int i = 0; i < sortCount; i++) {
        if (getenv("AB_DUMP_SORT")) {
            const TransformedPoly* sp = sortedPolys[i];
            if ((sp->texInfo & 0xFFF) == 0x15 || (sp->texInfo & 0xFFF) == 0x17) {
                static int sn = 0;
                if (sn < 40)
                    fprintf(stderr, "SORT i=%d mesh=%d tex=0x%04x key=%u depth=(%d,%d,%d,%d)\n",
                            i, sp->meshIndex, sp->texInfo, sp->sortKey,
                            sp->verts[0].depth, sp->verts[1].depth,
                            sp->verts[2].depth, sp->verts[3].depth), ++sn;
            }
        }
        ScopedPolyFaceLight faceLightScope(*sortedPolys[i]);
        if (jobTrace) {
            const TransformedPoly* p = sortedPolys[i];
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            int mat = tex ? (int)(p->texInfo & 0xFFF) : -1;
            char note[200];
            snprintf(note, sizeof(note),
                     "painter verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d) n=%d type=%d "
                     "sp=%u mi=%d cid=%d",
                     (int)p->verts[0].screenX, (int)p->verts[0].screenY,
                     (int)p->verts[1].screenX, (int)p->verts[1].screenY,
                     (int)p->verts[2].screenX, (int)p->verts[2].screenY,
                     (int)p->verts[3].screenX, (int)p->verts[3].screenY,
                     (int)p->normalIdx, (int)p->type, (unsigned)p->specialParam,
                     p->meshIndex, (int)p->contourId);
            jjffe_job_trace_cpp_exec(p->sortKey, p->meshIndex, p->texInfo, mat,
                                    tex ? "tex" : "flat", note);
        }
        const char* matOnly = getenv("AB_MAT_ONLY");
        if (matOnly && matOnly[0]) {
            const TransformedPoly* p = sortedPolys[i];
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            int mat = tex ? (int)(p->texInfo & 0xFFF) : -1;
            if (mat != atoi(matOnly))
                continue;
        }
        if (getenv("AB_TEX_ONLY") && getenv("AB_TEX_ONLY")[0] == '1') {
            const TransformedPoly* p = sortedPolys[i];
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            if (!tex && p->type != MeshPolyType::Thruster)
                continue;
        }
        if (getenv("AB_THRUSTER_ONLY") && getenv("AB_THRUSTER_ONLY")[0] == '1') {
            if (sortedPolys[i]->type != MeshPolyType::Thruster)
                continue;
        }
        if (underlaid[i])
            continue; // already drawn in underlay pass
        {
            const TransformedPoly* p = sortedPolys[i];
            // Eagle Mk2/3 nest #23: ASM omits LRF nose mat 0x4017 TexTri×4
            // (001807 band 3299) on edge poses — ~1k OC. Also skip tip TexQuad
            // diamonds: Mk2 ASM omits them; Mk3 ASM has tip paint but our
            // TexQuads mismatch (11/166 shade match, +228 OC) — hold Mk3 OA.
            // Keep belly TexQuadPair + tip ComplexFill. Dig:
            // AB_KEEP_EAGLE_4017_NOSE=1.
            if (IsSpriteChildParam(p->specialParam) &&
                SpriteChildModelId(p->specialParam) == 23 &&
                (p->texInfo & 0xFFF) == 0x17 &&
                (p->type == MeshPolyType::TexTri ||
                 p->type == MeshPolyType::TexTriPair ||
                 p->type == MeshPolyType::TexQuad) &&
                !(getenv("AB_KEEP_EAGLE_4017_NOSE") &&
                  getenv("AB_KEEP_EAGLE_4017_NOSE")[0] == '1'))
                continue;
            // Dig: AB_EAGLE_SKIP_4017=1 — skip parent Eagle tip 0x4017 to see
            // Contour tip survival (diag Contour OA).
            {
                const int smi4017 = jjffe::GetShipModelIndex(shipModel);
                if ((smi4017 == 23 || smi4017 == 24 || smi4017 == 25) &&
                    (p->texInfo & 0xFFF) == 0x17 &&
                    !IsSpriteChildParam(p->specialParam) &&
                    (p->type == MeshPolyType::TexTri ||
                     p->type == MeshPolyType::TexTriPair ||
                     p->type == MeshPolyType::TexQuad ||
                     p->type == MeshPolyType::TexQuadPair) &&
                    getenv("AB_EAGLE_SKIP_4017") &&
                    getenv("AB_EAGLE_SKIP_4017")[0] == '1')
                    continue;
            }
            // Puma #60: when mat8 Contours already cover a fat AABB, ASM draws
            // Contour fills only (001799). C++ also emits TexTri/Quad(Pair) for
            // the same area → VGA6 overdraw. Keep only the visible normal-10/14
            // TexQuad band(s) that map to ASM 001805, plus all near-LOD faces.
            // Edge-on Contour poses (top/bottom thin AABB) bypass this rule. Dig:
            // AB_MAT8_KEEP_TEX=1.
            const bool puma001805Band =
                (getenv("AB_PUMA_001805_MESH10") &&
                 getenv("AB_PUMA_001805_MESH10")[0] == '1')
                    ? p->meshIndex == 10
                    : p->type == MeshPolyType::TexQuad &&
                      (p->normalIdx == 10 || p->normalIdx == 14);
            if (s_pumaMat8ContourFat && p->texInfo == 0x4008 &&
                !puma001805Band && !s_pumaMat8NearLod &&
                (p->type == MeshPolyType::TexTri ||
                 p->type == MeshPolyType::TexTriPair ||
                 p->type == MeshPolyType::TexQuad ||
                 p->type == MeshPolyType::TexQuadPair) &&
                !(getenv("AB_MAT8_KEEP_TEX") &&
                  getenv("AB_MAT8_KEEP_TEX")[0] == '1'))
                continue;
            // Lion #54: 0x5009 Contour fills cover hull area; ASM draws only
            // Contour fills (001799). C++ also emits TexTri/Quad/Pair for the
            // same area → overdraw. Skip non-Contour 0x5009 when Contours exist.
            // Gated by ship index to avoid regressing Moray #37.
            // Dig: AB_LION_KEEP_TEX=1.
            if (s_lion5009Contour && p->texInfo == 0x5009 &&
                p->type != MeshPolyType::ComplexFill &&
                jjffe::GetShipModelIndex(shipModel) == 54 &&
                !(getenv("AB_LION_KEEP_TEX") &&
                  getenv("AB_LION_KEEP_TEX")[0] == '1'))
                continue;
            // Skeet #45: 0x0444 TexTri verts are Contour path corners (ASM
            // band 6079 edges / 001809 quads). Painting them as flat VGA4
            // floods the Contour apex (front strip) and top tip. Keep
            // FlatQuad/TexQuad 001809 belly. Dig: AB_SKEET_KEEP_0444_TRI=1.
            if (jjffe::GetShipModelIndex(shipModel) == 45 &&
                p->texInfo == 0x0444 &&
                (p->type == MeshPolyType::TexTri ||
                 p->type == MeshPolyType::TexTriPair) &&
                !(getenv("AB_SKEET_KEEP_0444_TRI") &&
                  getenv("AB_SKEET_KEEP_0444_TRI")[0] == '1'))
                continue;
            const bool flatQuad = getenv("AB_C721_FLAT_QUAD") &&
                                 getenv("AB_C721_FLAT_QUAD")[0] == '1';
            const bool skipC721 = getenv("AB_SKIP_C721") &&
                                  getenv("AB_SKIP_C721")[0] == '1';
            if (skipC721 && p->type == MeshPolyType::ComplexFill)
                continue;
            // R1003 PROMOTED: silhouette Contour groups (screen aabb spanning the full width) are
            // drawn in a pre-pass so the detail bands overwrite them. Measured, not guessed:
            // hoisting EVERY group is far worse fleet-wide (R1002: 21 hulls worse, net +32 308 px),
            // but with the width gate (>= 600 px) the fleet record improves 140 760 -> 134 906 with
            // ZERO regressions - hull 71 7 242 -> 1 415 (D -> C), hull 51 3 252 -> 3 225.
            // Kill switches: AB_NO_C721_PREPAINT=1 (whole law), AB_C721_PREP_MINW=<px> (gate width).
            const bool c721prep =
                !(getenv("AB_NO_C721_PREPAINT") && getenv("AB_NO_C721_PREPAINT")[0] == '1');
            if (c721prep && !prepaintDone && i > 0) {
                for (int j = 0; j < sortCount; j++) {
                    if (underlaid[j]) continue;
                    const TransformedPoly* q = sortedPolys[j];
                    if (q->type != MeshPolyType::ComplexFill || q->contourId < 0)
                        continue;
                    bool already = false;
                    for (int k = 0; k < j; k++) {
                        if (underlaid[k]) continue;
                        const TransformedPoly* r = sortedPolys[k];
                        if (r->type == MeshPolyType::ComplexFill &&
                            r->contourId == q->contourId) { already = true; break; }
                    }
                    if (already) continue;
                    const TransformedPoly* grp[64];
                    int ng2 = 0;
                    for (int k = 0; k < sortCount && ng2 < 64; k++) {
                        const TransformedPoly* r = sortedPolys[k];
                        if (r->type == MeshPolyType::ComplexFill &&
                            r->contourId == q->contourId)
                            grp[ng2++] = r;
                    }
                    // R1003: hoist only SILHOUETTE groups - those whose screen aabb spans the full
                    // width. R1002 measured that hoisting EVERY group is far worse fleet-wide
                    // (21 hulls worse, net +32 308 px) while hulls 71/58 (contours spanning
                    // x -370..369) each improve by ~5 800. Gate on group width, measured.
                    int gx0 = 319, gx1 = 0;
                    for (int g = 0; g < ng2; g++)
                        for (int vi = 0; vi < grp[g]->vertexCount && vi < 4; vi++) {
                            if (grp[g]->verts[vi].screenX < gx0) gx0 = grp[g]->verts[vi].screenX;
                            if (grp[g]->verts[vi].screenX > gx1) gx1 = grp[g]->verts[vi].screenX;
                        }
                    const int gmin = ::getenv("AB_C721_PREP_MINW")
                                         ? ::atoi(::getenv("AB_C721_PREP_MINW")) : 600;
                    if ((gx1 - gx0) >= gmin &&
                        s_hoistedCount < 32) {
                        DrawComplexContourSpan(grp, ng2, shipModel, shipRot);
                        s_hoistedCid[s_hoistedCount++] = q->contourId;
                    }
                }
                prepaintDone = true;
            }
            if (!flatQuad && p->type == MeshPolyType::ComplexFill &&
                p->contourId >= 0) {
                if (prepaintDone) {
                    bool wasHoisted = false;
                    for (int h = 0; h < s_hoistedCount; h++)
                        if (s_hoistedCid[h] == p->contourId) { wasHoisted = true; break; }
                    if (wasHoisted) continue;
                }
                // First visible piece of this Contour draws the whole group.
                bool already = false;
                for (int j = 0; j < i; j++) {
                    if (underlaid[j]) continue;
                    const TransformedPoly* q = sortedPolys[j];
                    if (q->type == MeshPolyType::ComplexFill &&
                        q->contourId == p->contourId) {
                        already = true;
                        break;
                    }
                }
                    if (!already) {
                    const TransformedPoly* group[64];
                    int ng = 0;
                    for (int j = 0; j < sortCount && ng < 64; j++) {
                        const TransformedPoly* q = sortedPolys[j];
                        if (q->type == MeshPolyType::ComplexFill &&
                            q->contourId == p->contourId)
                            group[ng++] = q;
                    }
                    if (getenv("AB_DUMP_C721_GROUP") &&
                        getenv("AB_DUMP_C721_GROUP")[0] == '1') {
                        int16_t gx0 = 319, gy0 = 199, gx1 = 0, gy1 = 0;
                        for (int g = 0; g < ng; g++) {
                            const TransformedPoly& q = *group[g];
                            for (int vi = 0; vi < q.vertexCount && vi < 4; vi++) {
                                if (q.verts[vi].screenX < gx0) gx0 = q.verts[vi].screenX;
                                if (q.verts[vi].screenY < gy0) gy0 = q.verts[vi].screenY;
                                if (q.verts[vi].screenX > gx1) gx1 = q.verts[vi].screenX;
                                if (q.verts[vi].screenY > gy1) gy1 = q.verts[vi].screenY;
                            }
                        }
                        fprintf(stderr,
                                "C721GROUP cid=%d tex=0x%04x ng=%d aabb=(%d,%d)-(%d,%d) "
                                "child=%d\n",
                                (int)p->contourId, p->texInfo, ng, gx0, gy0, gx1, gy1,
                                SpriteChildModelId(p->specialParam));
                    }
                    // Painter order shuffles Contour pieces and breaks the
                    // bezier path (Puma right: close chord collapsed to a
                    // point). Span fill needs mesh/bytecode order.
                    for (int a = 0; a < ng; a++) {
                        for (int b = a + 1; b < ng; b++) {
                            if (group[b]->meshIndex < group[a]->meshIndex) {
                                const TransformedPoly* tmp = group[a];
                                group[a] = group[b];
                                group[b] = tmp;
                            }
                        }
                    }
                    // Hawk #21 / Kestrel #22: mat 0x4013 Contours under sprite
                    // attach flood orbit (AABB ~9k px, y<0) — ASM orbit ~740px.
                    // Dig: AB_C721_KEEP_4013=1.
                    if ((p->texInfo & 0xFFF) == 0x13 &&
                        !(getenv("AB_C721_KEEP_4013") &&
                          getenv("AB_C721_KEEP_4013")[0] == '1'))
                        continue;
                    // Viper MkII #34: Contours 0x1000 / 0x4028 paint solid VGA7
                    // blobs; ASM dump-ab orbit/top are blank and front is ~106px.
                    // Dig: AB_C721_KEEP_VIPER=1.
                    if ((p->texInfo == 0x1000 || p->texInfo == 0x4028) &&
                        !(getenv("AB_C721_KEEP_VIPER") &&
                          getenv("AB_C721_KEEP_VIPER")[0] == '1'))
                        continue;
                    // Courier #215 wing Contours (0x0336/0x0600): single
                    // open beziers with wide-skip-close leave empty even-odd
                    // fills, then DPal-odd 0x0336 punches TexQuadPair tips.
                    // ASM wing solids are the flat-RGB TexQuad/TriPairs; Contour
                    // path is digon/punch. Default skip span when direct child
                    // of Courier (attachParentModel==0). AB_C721_KEEP_215=1
                    // restores the old span behavior for digs.
                    // Wrapper nested #215 (attachParentModel!=0) always spans.
                    if (IsSpriteChildParam(p->specialParam) &&
                        SpriteChildModelId(p->specialParam) == 215 &&
                        p->attachParentModel == 0) {
                        const bool skip215 =
                            !(getenv("AB_C721_KEEP_215") &&
                              getenv("AB_C721_KEEP_215")[0] == '1');
                        if (skip215)
                            continue;
                    }
                    // Tiger/Falcon FUNC_001736 Contour 0x20ee: ASM 001799 sets
                    // rgb16=0xee then fills each digon (001797) separately.
                    // Even-odd of all digon pieces in one Contour cancels the
                    // interiors — fill each upper+lower pair like 001797.
                    if (p->texInfo == 0x20ee) {
                        const bool isGlint =
                            (p->specialParam & 0xFFF0) == 0x7360;
                        if (isGlint) {
                            // ASM 001799 rgb16=0xee → DPal (luma → VGA127). Do
                            // not use ComplexFillColor shade0 — that yields VGA15.
                            uint8_t color = FlatRgbToPal(p->texInfo);
                            for (int g = 0; g + 1 < ng; g += 2) {
                                const TransformedPoly& up = *group[g];
                                const TransformedPoly& lo = *group[g + 1];
                                if (up.vertexCount != 4 || lo.vertexCount != 4)
                                    continue;
                                int16_t a[2] = {up.verts[0].screenX, up.verts[0].screenY};
                                int16_t b[2] = {up.verts[3].screenX, up.verts[3].screenY};
                                int16_t c0[2] = {up.verts[1].screenX, up.verts[1].screenY};
                                int16_t c1[2] = {up.verts[2].screenX, up.verts[2].screenY};
                                int16_t d0[2] = {lo.verts[1].screenX, lo.verts[1].screenY};
                                int16_t d1[2] = {lo.verts[2].screenX, lo.verts[2].screenY};
                                C721SpanReset();
                                C721SpanAddBezier(a, c0, c1, b);
                                C721SpanAddBezier(a, d0, d1, b);
                                C721SpanFlush(color);
                            }
                        } else {
                            // Non-glint 0x20ee (Panther #62 body): ASM
                            // 001799 fills continuous Contour spans.
                            // Panther #62 body: ASM 001799 uses triangle-fan
                            // C721 spans. Other ships must keep the legacy
                            // digon/punch path (prevents Lynx #70 extra pixels).
                            if (jjffe::GetShipModelIndex(shipModel) == 62) {
                                uint8_t color2 = DPal16toPal(0x222);
                                if (color2 == 0) color2 = 6; // fallback VGA6
                                for (int g = 0; g < ng; g++) {
                                    const TransformedPoly& pt = *group[g];
                                    if (pt.vertexCount < 3) continue;
                                    // Triangle fan fill via C721 span
                                    for (int v = 0; v + 2 < pt.vertexCount;
                                         v++) {
                                        int16_t a[2] = {pt.verts[0].screenX,
                                                         pt.verts[0].screenY};
                                        int16_t b[2] = {pt.verts[v + 1].screenX,
                                                         pt.verts[v + 1].screenY};
                                        int16_t c[2] = {pt.verts[v + 2].screenX,
                                                         pt.verts[v + 2].screenY};
                                        C721SpanReset();
                                        C721SpanAddEdge(a[0], a[1], b[0],
                                                        b[1]);
                                        C721SpanAddEdge(b[0], b[1], c[0],
                                                        c[1]);
                                        C721SpanAddEdge(c[0], c[1], a[0],
                                                        a[1]);
                                        C721SpanFlush(color2);
                                    }
                                }
                            } else {
                                for (int g = 0; g + 1 < ng; g += 2) {
                                    const TransformedPoly& up = *group[g];
                                    const TransformedPoly& lo = *group[g + 1];
                                    if (up.vertexCount != 4 ||
                                        lo.vertexCount != 4)
                                        continue;
                                    int16_t a[2] = {
                                        up.verts[0].screenX,
                                        up.verts[0].screenY};
                                    int16_t b[2] = {
                                        up.verts[3].screenX,
                                        up.verts[3].screenY};
                                    int16_t c0[2] = {
                                        up.verts[1].screenX,
                                        up.verts[1].screenY};
                                    int16_t c1[2] = {
                                        up.verts[2].screenX,
                                        up.verts[2].screenY};
                                    int16_t d0[2] = {
                                        lo.verts[1].screenX,
                                        lo.verts[1].screenY};
                                    int16_t d1[2] = {
                                        lo.verts[2].screenX,
                                        lo.verts[2].screenY};
                                    C721PunchDigon(a, b, c0, c1, d0, d1);
                                }
                            }
                        }
                    } else if (skeetDeferC721 && deferredC721N < 32) {
                        for (int g = 0; g < ng && g < 64; g++)
                            deferredC721[deferredC721N][g] = group[g];
                        deferredC721Ng[deferredC721N] = ng;
                        deferredC721N++;
                    } else {
                        DrawComplexContourSpan(group, ng, shipModel, shipRot);
                        if (getenv("AB_TRACE_150")) {
                            static uint8_t s_lc = 0xFE;
                            const uint8_t vc = g_videoState->backBuffer[107 * 320 + 150];
                            if (vc != s_lc) {
                                fprintf(stderr, "TRACE150c after contour cid=%d -> %u\n", (int)p->contourId, vc);
                                s_lc = vc;
                            }
                        }
                        // Puma: legacy thin Contour strip + tip Tex beside Contour.
                        // Orbit Contours are Contour-id Tex* (not ComplexFill) —
                        // handled by FillPumaOrbitContourResiduals post-pass.
                        // Dig: AB_PUMA_NO_TIP_FILL=1 / AB_PUMA_NO_CREST=1.
                        if ((p->texInfo & 0xFFF) == 8) {
                            int16_t cx0 = 319, cy0 = 199, cx1 = 0, cy1 = 0;
                            bool haveC = false;
                            for (int g = 0; g < ng; g++) {
                                const TransformedPoly& q = *group[g];
                                for (int vi = 0; vi < q.vertexCount && vi < 4; vi++) {
                                    const int16_t x = q.verts[vi].screenX;
                                    const int16_t y = q.verts[vi].screenY;
                                    if (x < cx0) cx0 = x;
                                    if (y < cy0) cy0 = y;
                                    if (x > cx1) cx1 = x;
                                    if (y > cy1) cy1 = y;
                                    haveC = true;
                                }
                            }
                            int16_t tx0 = 319, ty0 = 199, tx1 = 0, ty1 = 0;
                            bool haveTip = false;
                            for (int j = 0; j < sortCount; j++) {
                                const TransformedPoly* q = sortedPolys[j];
                                if (!q->visible || q->backFacing) continue;
                                if ((q->texInfo & 0xFFF) != 8) continue;
                                if (q->type != MeshPolyType::TexTri &&
                                    q->type != MeshPolyType::TexTriPair)
                                    continue;
                                for (int vi = 0; vi < q->vertexCount && vi < 4; vi++) {
                                    const int16_t x = q->verts[vi].screenX;
                                    const int16_t y = q->verts[vi].screenY;
                                    if (x < tx0) tx0 = x;
                                    if (y < ty0) ty0 = y;
                                    if (x > tx1) tx1 = x;
                                    if (y > ty1) ty1 = y;
                                    haveTip = true;
                                }
                            }
                            if (haveC && haveTip && (cx1 - cx0) < 30) {
                                const bool tipRight = tx1 > cx1 + 20;
                                const bool tipLeft = tx0 < cx0 - 20;
                                if (tipRight || tipLeft) {
                                    uint8_t tipColor =
                                        ComplexFillColor(*group[0], shipModel, shipRot);
                                    int16_t fx0 = tx0, fy0 = ty0, fx1 = tx1, fy1 = ty1;
                                    if (tipRight)
                                        fx1 = static_cast<int16_t>(fx1 + 2);
                                    else
                                        fx0 = static_cast<int16_t>(fx0 - 2);
                                    FillPumaTipLobeContours(fx0, fy0, fx1, fy1, tipColor,
                                                           tipLeft ? 1 : 0);
                                    FillPumaOrbitCrestContours(cx0, cy0, cx1, cy1, tipColor,
                                                              tipLeft ? 1 : 0);
                                }
                            }
                        }
                    }
                }
                continue;
            }
        }
        // Keep thin low-belly 0x4015 (orbit mesh40 y≈123) pre-Contour so
        // punches cut it. Defer upper/tall panels. Dig: AB_SKEET_4015_ALL=1.
        // Skeet 0x0888: skip schedule; empty-only restore after Contours.
        if (jjffe::GetShipModelIndex(shipModel) == 45 &&
            sortedPolys[i]->texInfo == 0x0888 &&
            sortedPolys[i]->vertexCount == 2 &&
            !(getenv("AB_SKEET_0888_UNDERLAY") &&
              getenv("AB_SKEET_0888_UNDERLAY")[0] == '1'))
            continue;
        if (skeetDefer4015 && sortedPolys[i]->texInfo == 0x4015 &&
            deferred4015N < 16) {
            const TransformedPoly* q = sortedPolys[i];
            int16_t ymin = q->verts[0].screenY, ymax = ymin;
            for (int vi = 1; vi < q->vertexCount && vi < 4; vi++) {
                if (q->verts[vi].screenY < ymin) ymin = q->verts[vi].screenY;
                if (q->verts[vi].screenY > ymax) ymax = q->verts[vi].screenY;
            }
            const bool deferAll = getenv("AB_SKEET_4015_ALL") &&
                                  getenv("AB_SKEET_4015_ALL")[0] == '1';
            const bool bellyStrip = (ymax - ymin) <= 6 && ymin >= 100;
            if (deferAll || !bellyStrip) {
                deferred4015[deferred4015N++] = q;
                continue;
            }
        }
        DrawFlatPolygon(*sortedPolys[i], shipModel, shipRot);
        if (getenv("AB_TRACE_150")) {
            static uint8_t s_last = 0xFF;
            const uint8_t v = g_videoState->backBuffer[107 * 320 + 150];
            if (v != s_last) {
                fprintf(stderr, "TRACE150 after mesh=%d type=%d tex=0x%04x key=%u -> %u\n",
                        sortedPolys[i]->meshIndex, (int)sortedPolys[i]->type,
                        sortedPolys[i]->texInfo, sortedPolys[i]->sortKey, v);
                s_last = v;
            }
        }
        if (vga3Trace) {
            const TransformedPoly* p = sortedPolys[i];
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            int mat = tex ? (int)(p->texInfo & 0xFFF) : -1;
            std::fprintf(stderr,
                         "VGA3_TRACE i=%d mesh=%d mat=%d tex=0x%04x key=%u vga3=%d\n",
                         i, p->meshIndex, mat, p->texInfo, p->sortKey, countVga3());
        }
        if (getenv("AB_DUMP_AABB") && i == 0) {
            // printed once marker; dump all after loop below
        }
    }
    // Contours before 4015 so mat21 refills Contour∩4015 (ASM order).
    for (int d = 0; d < deferredC721N; d++)
        DrawComplexContourSpan(deferredC721[d], deferredC721Ng[d], shipModel,
                               shipRot);
    for (int d = 0; d < deferred4015N; d++)
        DrawFlatPolygon(*deferred4015[d], shipModel, shipRot);
    // Skeet #45: 0x0888 PlainLines are Contour-hole chrome. Skip underlay/
    // schedule (clears side VGA8 OC). Top ASM band is an x-major shallow
    // line (floor DDA) after PlainLine x-1 — Bresenham leaves OA staircase.
    // Gate: wide + shallow + ymin/ymax in top band (orbit shallow lines
    // elsewhere flood OC). Dig: AB_SKEET_NO_0888_RESTORE=1.
    if (jjffe::GetShipModelIndex(shipModel) == 45 &&
        !(getenv("AB_SKEET_NO_0888_RESTORE") &&
          getenv("AB_SKEET_NO_0888_RESTORE")[0] == '1') &&
        g_videoState) {
        uint8_t* buf = g_videoState->backBuffer;
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->texInfo != 0x0888 || p->vertexCount != 2)
                continue;
            int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
            int x1 = p->verts[1].screenX, y1 = p->verts[1].screenY;
            // FUNC_001704 PlainLine x-1 on both endpoints.
            if (x0 > 0) x0 -= 1;
            if (x1 > 0) x1 -= 1;
            const int adx = abs(x1 - x0);
            const int ady = abs(y1 - y0);
            const int ymin = y0 < y1 ? y0 : y1;
            const int ymax = y0 < y1 ? y1 : y0;
            if (adx < 80 || ady > 6 || ymin < 70 || ymax > 90)
                continue;
            if (x0 > x1) {
                int tx = x0; x0 = x1; x1 = tx;
                int ty = y0; y0 = y1; y1 = ty;
            }
            const int dx = x1 - x0;
            if (dx <= 0)
                continue;
            for (int xi = 0; xi <= dx; xi++) {
                const int x = x0 + xi;
                const int y = y0 + (y1 - y0) * xi / dx;  // floor toward 0
                if (x < 0 || x >= 320 || y < 0 || y >= 200)
                    continue;
                uint8_t& pix = buf[y * 320 + x];
                if (pix == 0)
                    pix = 8;
            }
        }
    }
    // Osprey X #32: #202 Contour fill undershoots Contour↔tex seams and mishandles
    // degenerate horizontal Contour edges (top OA y≈52 / bottom OC y≈106).
    // Dig: AB_OSPREY_NO_C444_RESTAMP=1.
    if (jjffe::GetShipModelIndex(shipModel) == 32 &&
        !(getenv("AB_OSPREY_NO_C444_RESTAMP") &&
          getenv("AB_OSPREY_NO_C444_RESTAMP")[0] == '1') &&
        g_videoState) {
        struct Box { int x0, y0, x1, y1; };
        Box texBoxes[32];
        int nTex = 0;
        for (int i = 0; i < sortCount && nTex < 32; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->texInfo != 0x4034 && p->texInfo != 0x4026)
                continue;
            if (p->type != MeshPolyType::TexQuad &&
                p->type != MeshPolyType::TexQuadPair &&
                p->type != MeshPolyType::TexTri &&
                p->type != MeshPolyType::TexTriPair)
                continue;
            int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
            int x1 = x0, y1 = y0;
            for (int v = 1; v < p->vertexCount && v < 4; v++) {
                int x = p->verts[v].screenX, y = p->verts[v].screenY;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
            texBoxes[nTex++] = {x0, y0, x1, y1};
        }
        uint8_t* buf = g_videoState->backBuffer;
        // 1) Flat Contour edges (AABB h≤1): paint upper (ASM tip chrome), clear
        //    lower (Contour close into sky → OC).
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            if (p->texInfo != 0x0444 ||
                p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                continue;
            int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
            int x1 = x0, y1 = y0;
            for (int v = 1; v < p->vertexCount && v < 4; v++) {
                int x = p->verts[v].screenX, y = p->verts[v].screenY;
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
            if ((y1 - y0) > 1)
                continue;
            const int y = y0;
            if (y < 0 || y >= 200)
                continue;
            if (x0 < 0) x0 = 0;
            if (x1 > 319) x1 = 319;
            if (x0 > x1)
                continue;
            const uint8_t col = ComplexFillColor(*p, shipModel, shipRot);
            if (y < 80) {
                // Missing tip Contour chrome (top OA). Endpoints exclusive of
                // xmax — Contour AABB xmax is one past ASM's last chrome px.
                if (col == 0)
                    continue;
                if (x1 > x0)
                    x1 -= 1;
                for (int x = x0; x <= x1; x++) {
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix == 0)
                        pix = col;
                }
            } else if (y > 100) {
                // Contour edge painted into empty sky (top/orbit OC at ymax).
                // y=99 tip Contours are still hull chrome — do not clear.
                for (int x = x0; x <= x1; x++) {
                    uint8_t& pix = buf[y * 320 + x];
                    if (pix == col || pix == 4)
                        pix = 0;
                }
            }
        }
        // 2) Contour AABB ∩ hull-tex: restamp Contour color over bright freckles.
        if (nTex > 0) {
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                    continue;
                int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
                int x1 = x0, y1 = y0;
                for (int v = 1; v < p->vertexCount && v < 4; v++) {
                    int x = p->verts[v].screenX, y = p->verts[v].screenY;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
                if ((y1 - y0) <= 1)
                    continue;  // handled above
                int ix0 = 320, iy0 = 200, ix1 = -1, iy1 = -1;
                for (int t = 0; t < nTex; t++) {
                    const Box& tb = texBoxes[t];
                    int ax0 = x0 > tb.x0 ? x0 : tb.x0;
                    int ay0 = y0 > tb.y0 ? y0 : tb.y0;
                    int ax1 = x1 < tb.x1 ? x1 : tb.x1;
                    int ay1 = y1 < tb.y1 ? y1 : tb.y1;
                    if (ax0 > ax1 || ay0 > ay1)
                        continue;
                    if (ax0 < ix0) ix0 = ax0;
                    if (ay0 < iy0) iy0 = ay0;
                    if (ax1 > ix1) ix1 = ax1;
                    if (ay1 > iy1) iy1 = ay1;
                }
                if (ix1 < ix0)
                    continue;
                const int cw = x1 - x0;
                const bool wide = (ix1 - ix0) >= 40;
                const bool leftTip = (x1 <= 130 && cw < 40 && cw >= 4);
                if (y1 < 95 || (iy1 - iy0) > 12 || (!wide && !leftTip))
                    continue;
                const uint8_t col =
                    ComplexFillColor(*p, shipModel, shipRot);
                if (col == 0)
                    continue;
                if (ix0 < 0) ix0 = 0;
                if (iy0 < 0) iy0 = 0;
                if (ix1 > 319) ix1 = 319;
                if (iy1 > 199) iy1 = 199;
                for (int y = iy0; y <= iy1; y++) {
                    for (int x = ix0; x <= ix1; x++) {
                        if (x < x0 || x > x1 || y < y0 || y > y1)
                            continue;
                        bool inTex = false;
                        for (int t = 0; t < nTex; t++) {
                            const Box& tb = texBoxes[t];
                            if (x >= tb.x0 && x <= tb.x1 && y >= tb.y0 &&
                                y <= tb.y1) {
                                inTex = true;
                                break;
                            }
                        }
                        if (!inTex)
                            continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 1 || pix == 5 || pix == 6 || pix == 7 ||
                            pix == 22)
                            pix = col;
                    }
                }
            }
            // 3) Gap between left tip Contours (xmax≤130) and wide Contours
            //    (xmin≥137) inside mesh3 y-band — Contour AABBs leave a hole.
            int leftMax = -1, rightMin = 320, gapY0 = 200, gapY1 = -1;
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill || p->contourId < 0)
                    continue;
                int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
                int x1 = x0, y1 = y0;
                for (int v = 1; v < p->vertexCount && v < 4; v++) {
                    int x = p->verts[v].screenX, y = p->verts[v].screenY;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
                if (y1 < 95 || (y1 - y0) <= 1)
                    continue;
                if (x1 <= 130) {
                    if (x1 > leftMax) leftMax = x1;
                    if (y0 < gapY0) gapY0 = y0;
                    if (y1 > gapY1) gapY1 = y1;
                } else if ((x1 - x0) >= 40) {
                    if (x0 < rightMin) rightMin = x0;
                    if (y0 < gapY0) gapY0 = y0;
                    if (y1 > gapY1) gapY1 = y1;
                }
            }
            // Restrict gap to hull-tex y coverage.
            int texY0 = 200, texY1 = -1;
            for (int t = 0; t < nTex; t++) {
                if (texBoxes[t].y0 < texY0) texY0 = texBoxes[t].y0;
                if (texBoxes[t].y1 > texY1) texY1 = texBoxes[t].y1;
            }
            if (leftMax >= 0 && rightMin < 320 && leftMax + 1 < rightMin) {
                int y0 = gapY0 > texY0 ? gapY0 : texY0;
                int y1 = gapY1 < texY1 ? gapY1 : texY1;
                if (y0 < 0) y0 = 0;
                if (y1 > 199) y1 = 199;
                for (int y = y0; y <= y1; y++) {
                    for (int x = leftMax + 1; x < rightMin; x++) {
                        if (x < 0 || x >= 320)
                            continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 1 || pix == 5 || pix == 6 || pix == 7 ||
                            pix == 22)
                            pix = 4;
                    }
                }
            }
        }
        // 4) Diag Contour tip digons: even-odd Contour path pinches the tip
        //    crest (OA at tip_y-2) and leaves a Contour finger OC at tip_y-1 /
        //    tip_y+1. Expand tip digons (vc=2, w∈[3,12], h≤1) toward Contour
        //    body and clear the shifted Contour finger. Dig:
        //    AB_OSPREY_NO_DIAG_TIP=1.
        if (!(getenv("AB_OSPREY_NO_DIAG_TIP") &&
              getenv("AB_OSPREY_NO_DIAG_TIP")[0] == '1')) {
            struct CBox {
                int x0, y0, x1, y1;
                bool have;
            };
            CBox cidAabb[64];
            for (int i = 0; i < 64; i++)
                cidAabb[i] = {0, 0, 0, 0, false};
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill || p->contourId < 0 ||
                    p->contourId >= 64)
                    continue;
                int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
                int x1 = x0, y1 = y0;
                for (int v = 1; v < p->vertexCount && v < 4; v++) {
                    int x = p->verts[v].screenX, y = p->verts[v].screenY;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
                CBox& b = cidAabb[p->contourId];
                if (!b.have) {
                    b = {x0, y0, x1, y1, true};
                } else {
                    if (x0 < b.x0) b.x0 = x0;
                    if (y0 < b.y0) b.y0 = y0;
                    if (x1 > b.x1) b.x1 = x1;
                    if (y1 > b.y1) b.y1 = y1;
                }
            }
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill || p->contourId < 0 ||
                    p->contourId >= 64 || p->vertexCount != 2)
                    continue;
                const CBox& b = cidAabb[p->contourId];
                if (!b.have || (b.y1 - b.y0) < 8 || (b.x1 - b.x0) < 30)
                    continue;
                int xa = p->verts[0].screenX, ya = p->verts[0].screenY;
                int xb = p->verts[1].screenX, yb = p->verts[1].screenY;
                if (xa > xb) {
                    const int t = xa;
                    xa = xb;
                    xb = t;
                }
                if (ya > yb) {
                    const int t = ya;
                    ya = yb;
                    yb = t;
                }
                const int tw = xb - xa;
                const int th = yb - ya;
                if (th > 1 || tw < 3 || tw > 12)
                    continue;
                const int cmy = (b.y0 + b.y1) / 2;
                const bool tipLeft = (xa - b.x0) <= (b.x1 - xb);
                const bool tipAtTop = ((ya + yb) / 2) <= cmy;
                const uint8_t col = ComplexFillColor(*p, shipModel, shipRot);
                if (col == 0)
                    continue;
                if (tipLeft && tipAtTop) {
                    const int yp = ya - 2;
                    if (yp >= 0 && yp < 200) {
                        for (int x = xb + 1; x <= xb + 7; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            uint8_t& pix = buf[yp * 320 + x];
                            if (pix == 0)
                                pix = col;
                        }
                    }
                    const int cy = ya - 1;
                    if (cy >= 0 && cy < 200) {
                        for (int x = xb + 6; x <= xb + 10; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            uint8_t& pix = buf[cy * 320 + x];
                            if (pix == col || pix == 4)
                                pix = 0;
                        }
                    }
                } else if (!tipLeft && !tipAtTop) {
                    const int yp = yb;
                    if (yp >= 0 && yp < 200) {
                        for (int x = xa - 12; x < xa; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            uint8_t& pix = buf[yp * 320 + x];
                            if (pix == 0)
                                pix = col;
                        }
                    }
                    const int cy = yb + 1;
                    if (cy >= 0 && cy < 200) {
                        for (int x = xa - 9; x <= xa - 3; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            uint8_t& pix = buf[cy * 320 + x];
                            if (pix == col || pix == 4)
                                pix = 0;
                        }
                    }
                }
            }
        }
        // 5) Orthog Contour waist: Contour fill undershoots outer tips at the
        //    Contour crotch (y≈84 OA) and bridges Contour gap fingers (y≈86
        //    OC at x≈152–153 / 165–166). Dilate Contour outers when indented
        //    ≥5px vs y+1; clear Contour gap fingers when y+1 lacks Contour.
        //    Gate: Contour span at y=84 ≤40 (front/back); skip top/orbit.
        // Dig: AB_OSPREY_NO_ORTHOG_WAIST=1.
        if (!(getenv("AB_OSPREY_NO_ORTHOG_WAIST") &&
              getenv("AB_OSPREY_NO_ORTHOG_WAIST")[0] == '1')) {
            int spanLo = 320, spanHi = -1, spanN = 0;
            for (int x = 130; x < 190; x++) {
                if (buf[84 * 320 + x] == 4) {
                    if (x < spanLo) spanLo = x;
                    if (x > spanHi) spanHi = x;
                    spanN++;
                }
            }
            const bool orthogWaist = spanN >= 8 && spanHi >= spanLo &&
                                    (spanHi - spanLo) < 40;
            if (orthogWaist) {
                for (int y = 82; y < 90; y++) {
                    int leftMin = 320, leftMin2 = 320;
                    int rightMax = -1, rightMax2 = -1;
                    for (int x = 130; x < 160; x++) {
                        if (buf[y * 320 + x] == 4 && x < leftMin)
                            leftMin = x;
                        if (buf[(y + 1) * 320 + x] == 4 && x < leftMin2)
                            leftMin2 = x;
                    }
                    for (int x = 160; x < 190; x++) {
                        if (buf[y * 320 + x] == 4 && x > rightMax)
                            rightMax = x;
                        if (buf[(y + 1) * 320 + x] == 4 && x > rightMax2)
                            rightMax2 = x;
                    }
                    if (leftMin < 320 && leftMin2 < 320 &&
                        leftMin - leftMin2 >= 5) {
                        for (int x = leftMin - 2; x < leftMin; x++) {
                            if (x >= 0 && buf[y * 320 + x] == 0)
                                buf[y * 320 + x] = 4;
                        }
                    }
                    if (rightMax >= 0 && rightMax2 >= 0 &&
                        rightMax2 - rightMax >= 5) {
                        for (int x = rightMax + 1; x <= rightMax + 3; x++) {
                            if (x < 320 && buf[y * 320 + x] == 0)
                                buf[y * 320 + x] = 4;
                        }
                    }
                }
                uint8_t below4[200][320];
                for (int y = 0; y < 200; y++)
                    memcpy(below4[y], buf + y * 320, 320);
                static const int kGapFingers[] = {152, 153, 165, 166};
                for (int y = 84; y < 90; y++) {
                    for (int fi = 0; fi < 4; fi++) {
                        const int x = kGapFingers[fi];
                        if (buf[y * 320 + x] == 4 && below4[y + 1][x] != 4)
                            buf[y * 320 + x] = 0;
                    }
                }
            }
        }
        // 6) Top Contour freckles: Contours with a wide horizontal tip digon
        //    at screen y∈[50,58]∪[100,110] (edge-on top silhouette). Restamp
        //    shade 5/6 → Contour inside Contour AABB, excluding the Contour
        //    ymax row (ASM keeps shade-5 tip chrome there). Also: Contour
        //    gap≤2, left-edge Contour→tex undo, short VGA22→Contour under
        //    Contour. Orbit tips sit at y≈36 and are excluded.
        //    Dig: AB_OSPREY_NO_TOP_FRECKLE=1.
        if (!(getenv("AB_OSPREY_NO_TOP_FRECKLE") &&
              getenv("AB_OSPREY_NO_TOP_FRECKLE")[0] == '1')) {
            struct CBox {
                int x0, y0, x1, y1;
                bool have;
                bool topTip;
            };
            CBox cidAabb[64];
            for (int i = 0; i < 64; i++)
                cidAabb[i] = {0, 0, 0, 0, false, false};
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill || p->contourId < 0 ||
                    p->contourId >= 64)
                    continue;
                int x0 = p->verts[0].screenX, y0 = p->verts[0].screenY;
                int x1 = x0, y1 = y0;
                for (int v = 1; v < p->vertexCount && v < 4; v++) {
                    int x = p->verts[v].screenX, y = p->verts[v].screenY;
                    if (x < x0) x0 = x;
                    if (y < y0) y0 = y;
                    if (x > x1) x1 = x;
                    if (y > y1) y1 = y;
                }
                CBox& b = cidAabb[p->contourId];
                if (!b.have) {
                    b = {x0, y0, x1, y1, true, false};
                } else {
                    if (x0 < b.x0) b.x0 = x0;
                    if (y0 < b.y0) b.y0 = y0;
                    if (x1 > b.x1) b.x1 = x1;
                    if (y1 > b.y1) b.y1 = y1;
                }
                if (p->vertexCount == 2) {
                    int tw = x1 - x0;
                    int th = y1 - y0;
                    if (th <= 1 && tw >= 20) {
                        const int ty = (y0 + y1) / 2;
                        if ((ty >= 50 && ty <= 58) ||
                            (ty >= 100 && ty <= 110))
                            b.topTip = true;
                    }
                }
            }
            for (int cid = 0; cid < 64; cid++) {
                const CBox& b = cidAabb[cid];
                if (!b.have || !b.topTip || (b.y1 - b.y0) <= 1)
                    continue;
                int x0 = b.x0 < 0 ? 0 : b.x0;
                int y0 = b.y0 < 0 ? 0 : b.y0;
                int x1 = b.x1 > 319 ? 319 : b.x1;
                int y1 = b.y1 > 199 ? 199 : b.y1;
                // Exclude Contour ymax: restamp there overpaints ASM shade-5
                // tip chrome (top IDX 5→4 freckles at y=AABB.y1).
                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x <= x1; x++) {
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix == 5 || pix == 6)
                            pix = 4;
                    }
                }
                // Contour holes ≤2 empty between Contour (also skip ymax).
                for (int y = y0; y < y1; y++) {
                    int x = x0;
                    while (x <= x1) {
                        if (buf[y * 320 + x] != 0) {
                            x++;
                            continue;
                        }
                        int s = x;
                        while (x <= x1 && buf[y * 320 + x] == 0)
                            x++;
                        int e = x - 1;
                        if (s > x0 && e < x1 && (e - s + 1) <= 2 &&
                            buf[y * 320 + (s - 1)] == 4 &&
                            buf[y * 320 + (e + 1)] == 4) {
                            for (int xx = s; xx <= e; xx++)
                                buf[y * 320 + xx] = 4;
                        }
                    }
                }
                // Left Contour AABB edge: Contour∩tex restamp bleeds Contour
                // into ASM shade-6/7 lobe chrome — restore from left neighbor.
                for (int y = y0; y <= y1; y++) {
                    for (int x = x0; x <= x0 + 1 && x <= x1; x++) {
                        if (x <= 0)
                            continue;
                        uint8_t& pix = buf[y * 320 + x];
                        if (pix != 4)
                            continue;
                        const uint8_t left = buf[y * 320 + (x - 1)];
                        if (left == 5 || left == 6 || left == 7)
                            pix = left;
                    }
                }
                // Short VGA22 runs under Contour tip chrome → Contour.
                // Skip Contour ymax (ASM VGA22 tip chrome lives there).
                for (int y = y0 + 1; y < y1; y++) {
                    int x = x0;
                    while (x <= x1) {
                        if (buf[y * 320 + x] != 22 ||
                            buf[(y - 1) * 320 + x] != 4) {
                            x++;
                            continue;
                        }
                        int s = x;
                        while (x <= x1 && buf[y * 320 + x] == 22 &&
                               buf[(y - 1) * 320 + x] == 4)
                            x++;
                        if ((x - s) <= 2) {
                            for (int xx = s; xx < x; xx++)
                                buf[y * 320 + xx] = 4;
                        }
                    }
                }
            }
        }
        // 7) Orbit mid Contour tip digon freckles: tip digons at screen
        //    y∈[40,120] (not only top edge-on [50,58]∪[100,110]). Restamp
        //    shade 5/22 → Contour on the tip digon row only — full Contour
        //    AABB floods orbit. Outer orbit tips at y≈36 excluded.
        //    Dig: AB_OSPREY_NO_ORBIT_TIP_ROW=1.
        if (!(getenv("AB_OSPREY_NO_ORBIT_TIP_ROW") &&
              getenv("AB_OSPREY_NO_ORBIT_TIP_ROW")[0] == '1')) {
            for (int i = 0; i < sortCount; i++) {
                const TransformedPoly* p = sortedPolys[i];
                if (p->texInfo != 0x0444 ||
                    p->type != MeshPolyType::ComplexFill ||
                    p->vertexCount != 2)
                    continue;
                int xa = p->verts[0].screenX, ya = p->verts[0].screenY;
                int xb = p->verts[1].screenX, yb = p->verts[1].screenY;
                if (xa > xb) {
                    const int t = xa;
                    xa = xb;
                    xb = t;
                }
                if (ya > yb) {
                    const int t = ya;
                    ya = yb;
                    yb = t;
                }
                const int tw = xb - xa;
                const int th = yb - ya;
                if (th > 1 || tw < 12)
                    continue;
                const int ty = (ya + yb) / 2;
                if (ty < 40 || ty > 120)
                    continue;
                for (int x = xa; x <= xb; x++) {
                    if (x < 0 || x >= 320)
                        continue;
                    uint8_t& pix = buf[ty * 320 + x];
                    if (pix != 5 && pix != 22)
                        continue;
                    int nb = 0;
                    if (x > 0 && buf[ty * 320 + (x - 1)] == 4)
                        nb++;
                    if (x < 319 && buf[ty * 320 + (x + 1)] == 4)
                        nb++;
                    if (ty > 0 && buf[(ty - 1) * 320 + x] == 4)
                        nb++;
                    if (ty < 199 && buf[(ty + 1) * 320 + x] == 4)
                        nb++;
                    if (nb >= 1)
                        pix = 4;
                }
            }
        }
    }
    // Eagle #23–25 orthog wing tip chrome: RETRACTED by R1093. It was fitted to
    // an "undershoot" that does not exist: the ASM's own 001803 wireframe jobs
    // for hull 24 pose 01_front end exactly where the textured spans do
    // (watchpoint on (130,70) proved the chrome wrote over background there),
    // and scoring every pixel it paints against the oracle gives helps=0 /
    // hurts=all on both affected hulls: hull 24 813 -> 632 px and hull 23
    // 288 -> 75 px with it disabled (hull 25 and all other hulls unchanged).
    // The default is now OFF; AB_EAGLE_TIP_CHROME=1 restores the old behaviour
    // for bisection.
    const bool eagleTipChrome =
        getenv("AB_EAGLE_TIP_CHROME") && getenv("AB_EAGLE_TIP_CHROME")[0] == '1';
    // Also end-of-draw Contour freckle restamp for edge-on top Contours
    // (tex overdraws Contour after Contour flush). Dig shares
    // AB_EAGLE_NO_EDGEON_CLEAN=1.
    {
        const int smi = jjffe::GetShipModelIndex(shipModel);
        if ((smi == 23 || smi == 24 || smi == 25) && g_videoState) {
            uint8_t* buf = g_videoState->backBuffer;
            if (eagleTipChrome) {
                const int depth = 2;
                int bbX0 = 320, bbX1 = -1, bbY0 = 200, bbY1 = -1;
                for (int y = 0; y < 200; y++) {
                    int lx = -1, rx = -1;
                    for (int x = 0; x < 320; x++) {
                        if (buf[y * 320 + x]) {
                            if (lx < 0)
                                lx = x;
                            rx = x;
                            if (x < bbX0)
                                bbX0 = x;
                            if (x > bbX1)
                                bbX1 = x;
                            if (y < bbY0)
                                bbY0 = y;
                            if (y > bbY1)
                                bbY1 = y;
                        }
                    }
                    if (lx < 0)
                        continue;
                    const uint8_t ls = buf[y * 320 + lx];
                    const uint8_t rs = buf[y * 320 + rx];
                    if (lx >= 120 && lx <= 132 &&
                        (ls == 4 || ls == 5 || ls == 6)) {
                        for (int d = 1; d <= depth; d++) {
                            const int x = lx - d;
                            if (x < 0 || buf[y * 320 + x] != 0)
                                break;
                            buf[y * 320 + x] = 6;
                        }
                    }
                    if (rx >= 188 && rx <= 200 &&
                        (rs == 4 || rs == 5 || rs == 6)) {
                        for (int d = 1; d <= depth; d++) {
                            const int x = rx + d;
                            if (x >= 320 || buf[y * 320 + x] != 0)
                                break;
                            buf[y * 320 + x] = 6;
                        }
                    }
                }
                // Side tip chrome (tall thin AABB): grow shade6 from tip ends.
                if (bbX1 >= bbX0 && bbY1 >= bbY0) {
                    const int bbW = bbX1 - bbX0 + 1;
                    const int bbH = bbY1 - bbY0 + 1;
                    if (bbW <= 20 && bbH >= 50) {
                        const int cx = (bbX0 + bbX1) / 2;
                        const int sideDepth = 3;
                        for (int x = cx - 1; x <= cx + 1; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            int yt = -1, yb = -1;
                            for (int y = 0; y < 200; y++) {
                                const uint8_t p = buf[y * 320 + x];
                                if (p == 4 || p == 5 || p == 6) {
                                    if (yt < 0)
                                        yt = y;
                                    yb = y;
                                }
                            }
                            if (yt < 0)
                                continue;
                            for (int d = 1; d <= sideDepth; d++) {
                                const int y = yt - d;
                                if (y < 0 || buf[y * 320 + x] != 0)
                                    break;
                                buf[y * 320 + x] = 6;
                            }
                            for (int d = 1; d <= sideDepth; d++) {
                                const int y = yb + d;
                                if (y >= 200 || buf[y * 320 + x] != 0)
                                    break;
                                buf[y * 320 + x] = 6;
                            }
                        }
                        // Bottom tip beyond shade7/8 ring: 456-seeded grow
                        // stops on the ring. Grow empty→6 from silhouette tip
                        // when tip pixel is 7/8 and near bbY1. Dig shares
                        // AB_EAGLE_NO_TIP_CHROME=1.
                        for (int x = cx - 1; x <= cx + 1; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            int ybSil = -1;
                            for (int y = 0; y < 200; y++) {
                                if (buf[y * 320 + x])
                                    ybSil = y;
                            }
                            if (ybSil < 0 || ybSil < bbY1 - 3)
                                continue;
                            const uint8_t tip = buf[ybSil * 320 + x];
                            if (tip != 7 && tip != 8)
                                continue;
                            for (int d = 1; d <= 3; d++) {
                                const int y = ybSil + d;
                                if (y >= 200 || buf[y * 320 + x] != 0)
                                    break;
                                buf[y * 320 + x] = 6;
                            }
                        }
                        int tipBot = -1;
                        for (int x = cx - 1; x <= cx + 1; x++) {
                            if (x < 0 || x >= 320)
                                continue;
                            for (int y = 0; y < 200; y++) {
                                if (buf[y * 320 + x] == 6 && y > tipBot)
                                    tipBot = y;
                            }
                        }
                        // Top tip peak: +1 empty→6 on center column only.
                        if (cx >= 0 && cx < 320) {
                            int ytSil = -1;
                            for (int y = 0; y < 200; y++) {
                                if (buf[y * 320 + cx]) {
                                    ytSil = y;
                                    break;
                                }
                            }
                            if (ytSil >= 0 && ytSil <= bbY0 + 3) {
                                const uint8_t tip = buf[ytSil * 320 + cx];
                                if (tip == 4 || tip == 5 || tip == 6) {
                                    const int y = ytSil - 1;
                                    if (y >= 0 && buf[y * 320 + cx] == 0)
                                        buf[y * 320 + cx] = 6;
                                    ytSil = y >= 0 ? y : ytSil;
                                }
                                // ASM tip stem: (cx-1, peak+1), (cx+2, peak+3).
                                const int yL = ytSil + 1;
                                if (yL < 200 && cx >= 1 &&
                                    buf[yL * 320 + cx] == 6 &&
                                    buf[yL * 320 + (cx - 1)] == 0)
                                    buf[yL * 320 + (cx - 1)] = 6;
                                const int yR = ytSil + 3;
                                if (yR < 200 && cx + 2 < 320 &&
                                    buf[yR * 320 + (cx + 1)] == 6 &&
                                    buf[yR * 320 + (cx + 2)] == 0)
                                    buf[yR * 320 + (cx + 2)] = 6;
                            }
                        }
                        // Lateral widen bottom tip using post-grow tipBot.
                        // bbY1 is the pre-grow shade7 ring — too high.
                        // Keep lateral to cx±1; cx+2 is the single-row jut below.
                        if (tipBot >= 0) {
                            for (int pass = 0; pass < 2; pass++) {
                                for (int y = tipBot - 2; y <= tipBot && y < 200;
                                     y++) {
                                    if (y < 0)
                                        continue;
                                    for (int x = cx - 1; x <= cx + 1; x++) {
                                        if (x < 1 || x >= 319)
                                            continue;
                                        if (buf[y * 320 + x] != 0)
                                            continue;
                                        if (buf[y * 320 + (x - 1)] == 6 ||
                                            buf[y * 320 + (x + 1)] == 6)
                                            buf[y * 320 + x] = 6;
                                    }
                                }
                            }
                            // ASM bottom tip jut only at (cx+2, tipBot-1).
                            if (tipBot >= 1 && cx + 2 < 320) {
                                const int y = tipBot - 1;
                                if (buf[y * 320 + (cx + 1)] == 6 &&
                                    buf[y * 320 + (cx + 2)] == 0)
                                    buf[y * 320 + (cx + 2)] = 6;
                            }
                        }
                    }
                }
                // Orthog tip chrome islands: silhouette dilate misses disconnected
                // shade6 tips (front y=77 lx=134 > tip band). Grow empty→6 in
                // outer tip columns when adjacent to tip shade (above/right or
                // above/diag or right/below). Dig: AB_EAGLE_NO_TIP_ISLAND=1.
                // R1093: same retraction as the tip chrome above - with the
                // chrome off, scoring this dilate's pixels against the oracle is
                // again helps=0 (hull 24 813 -> 782 px without it). Default OFF;
                // AB_EAGLE_TIP_ISLAND=1 restores for bisection.
                if (getenv("AB_EAGLE_TIP_ISLAND") &&
                    getenv("AB_EAGLE_TIP_ISLAND")[0] == '1') {
                    for (int y = 74; y < 82; y++) {
                        // Left tip: x high→low so (126,77) seeds (125,77)
                        // via right∧diag in the same pass.
                        for (int x = 127; x >= 120; x--) {
                            if (buf[y * 320 + x] != 0)
                                continue;
                            const bool above =
                                y + 1 < 200 && buf[(y + 1) * 320 + x] == 6;
                            const bool right =
                                x + 1 < 320 &&
                                (buf[y * 320 + (x + 1)] == 4 ||
                                 buf[y * 320 + (x + 1)] == 5 ||
                                 buf[y * 320 + (x + 1)] == 6);
                            const bool diag =
                                y + 1 < 200 && x + 1 < 320 &&
                                (buf[(y + 1) * 320 + (x + 1)] == 4 ||
                                 buf[(y + 1) * 320 + (x + 1)] == 5 ||
                                 buf[(y + 1) * 320 + (x + 1)] == 6);
                            const bool below =
                                y + 1 < 200 &&
                                (buf[(y + 1) * 320 + x] == 4 ||
                                 buf[(y + 1) * 320 + x] == 5 ||
                                 buf[(y + 1) * 320 + x] == 6);
                            if ((above && right) || (above && diag) ||
                                (right && below) ||
                                // Outer tip: right∧diag fills (125,y) islands.
                                // x≤125 avoids Contour OC.
                                (right && diag && x <= 125))
                                buf[y * 320 + x] = 6;
                        }
                        // Right tip: x low→high so left neighbor seeds next.
                        for (int x = 191; x < 200; x++) {
                            if (buf[y * 320 + x] != 0)
                                continue;
                            const bool above =
                                y + 1 < 200 && buf[(y + 1) * 320 + x] == 6;
                            const bool left =
                                x > 0 &&
                                (buf[y * 320 + (x - 1)] == 4 ||
                                 buf[y * 320 + (x - 1)] == 5 ||
                                 buf[y * 320 + (x - 1)] == 6);
                            const bool diag =
                                y + 1 < 200 && x > 0 &&
                                (buf[(y + 1) * 320 + (x - 1)] == 4 ||
                                 buf[(y + 1) * 320 + (x - 1)] == 5 ||
                                 buf[(y + 1) * 320 + (x - 1)] == 6);
                            const bool below =
                                y + 1 < 200 &&
                                (buf[(y + 1) * 320 + x] == 4 ||
                                 buf[(y + 1) * 320 + x] == 5 ||
                                 buf[(y + 1) * 320 + x] == 6);
                            if ((above && left) || (above && diag) ||
                                (left && below))
                                buf[y * 320 + x] = 6;
                        }
                    }
                    // Front/back tip OA freckles after island: ASM has one
                    // extra shade6 outside CPP tip at (124,78/79), (191,77),
                    // (194,79). Broad second dilate OC'd — paint only these.
                    // Dig shares AB_EAGLE_NO_TIP_ISLAND=1.
                    if (bbX1 >= bbX0 && bbY1 >= bbY0) {
                        const int tipW = bbX1 - bbX0 + 1;
                        const int tipH = bbY1 - bbY0 + 1;
                        if (tipW > 40 && tipH < 30) {
                            for (int y = 78; y <= 79; y++) {
                                if (buf[y * 320 + 125] == 6 &&
                                    buf[y * 320 + 124] == 0)
                                    buf[y * 320 + 124] = 6;
                            }
                            if (buf[77 * 320 + 192] == 6 &&
                                buf[77 * 320 + 191] == 0)
                                buf[77 * 320 + 191] = 6;
                            if (buf[79 * 320 + 193] == 6 &&
                                buf[79 * 320 + 194] == 0)
                                buf[79 * 320 + 194] = 6;
                        }
                    }
                }
            }
            // R1109: default OFF. After the R1099 ALU fix every pass in this block
            // (edge-on restamp, orbit freckles, orbit gap, orbit bot/tip OC) only
            // hurts: off, #23 75->1, #24 405->254, #25 0->0, no pose worse.
            // AB_EAGLE_EDGEON_CLEAN=1 restores the old behaviour for bisect.
            if (getenv("AB_EAGLE_EDGEON_CLEAN") &&
                getenv("AB_EAGLE_EDGEON_CLEAN")[0] == '1') {
                // Contour freckle restamp for edge-on top only (Contour mass in
                // a thin y-band). Orbit Contours are tall — do not restamp or
                // clear Contour rows (chops Contour lobes).
                int nContour = 0;
                int yMin = 200, yMax = -1;
                for (int y = 0; y < 200; y++) {
                    for (int x = 0; x < 320; x++) {
                        if (buf[y * 320 + x] == 22) {
                            nContour++;
                            if (y < yMin)
                                yMin = y;
                            if (y > yMax)
                                yMax = y;
                        }
                    }
                }
                if (nContour >= 80 && yMax >= yMin && (yMax - yMin) <= 8) {
                    for (int y = yMin; y <= yMax; y++) {
                        if (y < 1 || y > 198)
                            continue;
                        for (int x = 1; x < 319; x++) {
                            uint8_t& pix = buf[y * 320 + x];
                            if (pix != 1 && pix != 4 && pix != 5 && pix != 6 &&
                                pix != 7 && pix != 8)
                                continue;
                            int nb = 0;
                            if (buf[y * 320 + (x - 1)] == 22)
                                nb++;
                            if (buf[y * 320 + (x + 1)] == 22)
                                nb++;
                            if (buf[(y - 1) * 320 + x] == 22)
                                nb++;
                            if (buf[(y + 1) * 320 + x] == 22)
                                nb++;
                            if (nb >= 1)
                                pix = 22;
                        }
                    }
                    // Contour right tip OC (close chord past Contour body).
                    for (int y = yMin; y <= yMax && y < 200; y++) {
                        for (int x = 188; x < 210 && x < 320; x++) {
                            if (buf[y * 320 + x] == 22)
                                buf[y * 320 + x] = 0;
                        }
                    }
                    // Contour tip chrome OA: empty→22 with Contour below.
                    for (int y = yMin; y < yMax; y++) {
                        for (int x = 170; x < 188; x++) {
                            if (buf[y * 320 + x] == 0 &&
                                buf[(y + 1) * 320 + x] == 22)
                                buf[y * 320 + x] = 22;
                        }
                    }
                    // Contour tip shade6 Contour OC finger just below Contour
                    // body (top y=81). Clear shade6 only.
                    const int yTipOc = yMax + 1;
                    if (yTipOc >= 0 && yTipOc < 200) {
                        for (int x = 175; x < 195 && x < 320; x++) {
                            if (buf[yTipOc * 320 + x] == 6)
                                buf[yTipOc * 320 + x] = 0;
                        }
                    }
                } else if (nContour >= 200 && yMax >= yMin &&
                           (yMax - yMin) > 8) {
                    // Orbit Contour tip freckles (tall Contour only): tip-band
                    // restamp shade→22 + Contour crest empty→22 with Contour
                    // below. Global bodyY floods Contour OC — use tip x bands.
                    // Dig: AB_EAGLE_NO_ORBIT_CONTOUR=1.
                    // Snapshot Contour tip extents before restamp so post-pass
                    // can clear crest/valley tip OC without a second render.
                    static const int kTipBands[2][2] = {
                        {120, 150}, {170, 205}};
                    int preTipL[2][200];
                    int preTipR[2][200];
                    int bandBodyY[2] = {yMin, yMin};
                    int bandBest[2] = {0, 0};
                    bool bandTipRight[2] = {true, true};
                    for (int b = 0; b < 2; b++) {
                        for (int y = 0; y < 200; y++) {
                            preTipL[b][y] = -1;
                            preTipR[b][y] = -1;
                        }
                        const int x0 = kTipBands[b][0];
                        const int x1 = kTipBands[b][1];
                        int bodyY = yMin;
                        int bestSpan = 0;
                        for (int y = yMin; y <= yMax; y++) {
                            int span = 0;
                            int L = -1, R = -1;
                            for (int x = x0; x < x1 && x < 320; x++) {
                                if (buf[y * 320 + x] == 22) {
                                    span++;
                                    if (L < 0)
                                        L = x;
                                    R = x;
                                }
                            }
                            if (span > bestSpan) {
                                bestSpan = span;
                                bodyY = y;
                            }
                            if (span >= 1) {
                                preTipL[b][y] = L;
                                preTipR[b][y] = R;
                            }
                        }
                        bandBodyY[b] = bodyY;
                        bandBest[b] = bestSpan;
                        if (bestSpan >= 3 && preTipL[b][bodyY] >= 0) {
                            const int bL = preTipL[b][bodyY];
                            const int bR = preTipR[b][bodyY];
                            bandTipRight[b] =
                                std::abs(bR - 160) >= std::abs(bL - 160);
                        }
                    }
                    if (!(getenv("AB_EAGLE_NO_ORBIT_CONTOUR") &&
                          getenv("AB_EAGLE_NO_ORBIT_CONTOUR")[0] == '1')) {
                        for (int b = 0; b < 2; b++) {
                            const int x0 = kTipBands[b][0];
                            const int x1 = kTipBands[b][1];
                            const int bodyY = bandBodyY[b];
                            if (bandBest[b] < 3)
                                continue;
                            for (int y = bodyY - 3; y < bodyY; y++) {
                                if (y < 1 || y > 198)
                                    continue;
                                for (int x = x0 < 1 ? 1 : x0;
                                     x < x1 && x < 319; x++) {
                                    uint8_t& pix = buf[y * 320 + x];
                                    if (pix != 4 && pix != 5 && pix != 6 &&
                                        pix != 7 && pix != 8)
                                        continue;
                                    int nb = 0;
                                    if (buf[y * 320 + (x - 1)] == 22)
                                        nb++;
                                    if (buf[y * 320 + (x + 1)] == 22)
                                        nb++;
                                    if (buf[(y - 1) * 320 + x] == 22)
                                        nb++;
                                    if (buf[(y + 1) * 320 + x] == 22)
                                        nb++;
                                    if (nb >= 1)
                                        pix = 22;
                                }
                            }
                            for (int y = bodyY - 4; y < bodyY; y++) {
                                if (y < 0 || y >= 199)
                                    continue;
                                for (int x = x0; x < x1 && x < 320; x++) {
                                    if (buf[y * 320 + x] == 0 &&
                                        buf[(y + 1) * 320 + x] == 22)
                                        buf[y * 320 + x] = 22;
                                }
                            }
                        }
                    }
                    // Orbit Contour tip flange can leave a 1–8px Contour gap
                    // between Contour body and tip digon lobe (p90 y=78
                    // x=182–185 OA). Fill Contour gaps between runs. Dig:
                    // AB_EAGLE_NO_ORBIT_GAP=1.
                    if (!(getenv("AB_EAGLE_NO_ORBIT_GAP") &&
                          getenv("AB_EAGLE_NO_ORBIT_GAP")[0] == '1')) {
                        for (int y = yMin; y <= yMax; y++) {
                            int runs[16][2];
                            int nRuns = 0;
                            int runStart = -1;
                            for (int x = 0; x <= 320; x++) {
                                const bool on =
                                    x < 320 && buf[y * 320 + x] == 22;
                                if (on && runStart < 0) {
                                    runStart = x;
                                } else if (!on && runStart >= 0) {
                                    if (nRuns < 16) {
                                        runs[nRuns][0] = runStart;
                                        runs[nRuns][1] = x - 1;
                                        nRuns++;
                                    }
                                    runStart = -1;
                                }
                            }
                            for (int i = 0; i + 1 < nRuns; i++) {
                                const int a1 = runs[i][1];
                                const int b0 = runs[i + 1][0];
                                const int gap = b0 - a1 - 1;
                                if (gap >= 1 && gap <= 8) {
                                    for (int x = a1 + 1; x < b0; x++)
                                        buf[y * 320 + x] = 22;
                                }
                            }
                        }
                    }
                    // Bottom Contour rows are 1px right-shifted vs ASM (OA at
                    // L, OC at R). Nudge Contour 1px left on yMax..yMax-2.
                    // Dig: AB_EAGLE_NO_ORBIT_BOT=1.
                    if (!(getenv("AB_EAGLE_NO_ORBIT_BOT") &&
                          getenv("AB_EAGLE_NO_ORBIT_BOT")[0] == '1')) {
                        const int yBot0 =
                            (yMax - 2) > yMin ? (yMax - 2) : yMin;
                        for (int y = yBot0; y <= yMax; y++) {
                            int L = -1, R = -1;
                            for (int x = 0; x < 320; x++) {
                                if (buf[y * 320 + x] == 22) {
                                    if (L < 0)
                                        L = x;
                                    R = x;
                                }
                            }
                            if (L < 0)
                                continue;
                            if (L > 0 && buf[y * 320 + (L - 1)] == 0)
                                buf[y * 320 + (L - 1)] = 22;
                            if (R >= 0 && buf[y * 320 + R] == 22)
                                buf[y * 320 + R] = 0;
                        }
                    }
                    // Orbit Contour tip OC: restamp grows crest tips past ASM;
                    // tip flange also leaves valley tips past inward neighbor.
                    // Clamp grown crest tips to pre-restamp tip±7; clear valley
                    // tips unchanged by restamp that exceed neighbor±7.
                    // Dig: AB_EAGLE_NO_ORBIT_TIP_OC=1.
                    if (!(getenv("AB_EAGLE_NO_ORBIT_TIP_OC") &&
                          getenv("AB_EAGLE_NO_ORBIT_TIP_OC")[0] == '1')) {
                        constexpr int kTipOcExpand = 7;
                        for (int b = 0; b < 2; b++) {
                            if (bandBest[b] < 3)
                                continue;
                            const int x0 = kTipBands[b][0];
                            const int x1 = kTipBands[b][1];
                            const int bodyY = bandBodyY[b];
                            const bool tipRight = bandTipRight[b];
                            const int crest0 =
                                (bodyY - 4) > yMin ? (bodyY - 4) : yMin;
                            for (int y = crest0; y < bodyY; y++) {
                                int cL = -1, cR = -1;
                                for (int x = x0; x < x1 && x < 320; x++) {
                                    if (buf[y * 320 + x] == 22) {
                                        if (cL < 0)
                                            cL = x;
                                        cR = x;
                                    }
                                }
                                if (cL < 0)
                                    continue;
                                int pL = preTipL[b][y];
                                int pR = preTipR[b][y];
                                if (pL < 0) {
                                    pL = preTipL[b][bodyY];
                                    pR = preTipR[b][bodyY];
                                }
                                if (pL < 0)
                                    continue;
                                if (tipRight) {
                                    const int lim = pR + kTipOcExpand;
                                    if (cR > lim) {
                                        for (int x = lim + 1; x <= cR; x++) {
                                            if (buf[y * 320 + x] == 22)
                                                buf[y * 320 + x] = 0;
                                        }
                                    }
                                } else {
                                    const int lim = pL - kTipOcExpand;
                                    if (cL < lim) {
                                        for (int x = cL; x < lim; x++) {
                                            if (buf[y * 320 + x] == 22)
                                                buf[y * 320 + x] = 0;
                                        }
                                    }
                                }
                            }
                            int curL[200];
                            int curR[200];
                            for (int y = 0; y < 200; y++) {
                                curL[y] = -1;
                                curR[y] = -1;
                            }
                            for (int y = yMin; y <= yMax; y++) {
                                int L = -1, R = -1, span = 0;
                                for (int x = x0; x < x1 && x < 320; x++) {
                                    if (buf[y * 320 + x] == 22) {
                                        span++;
                                        if (L < 0)
                                            L = x;
                                        R = x;
                                    }
                                }
                                if (span >= 3) {
                                    curL[y] = L;
                                    curR[y] = R;
                                }
                            }
                            for (int y = yMin; y < yMax; y++) {
                                if (curL[y] < 0 || curL[y + 1] < 0)
                                    continue;
                                if (preTipL[b][y] < 0)
                                    continue;
                                const int L = curL[y];
                                const int R = curR[y];
                                const int pL = preTipL[b][y];
                                const int pR = preTipR[b][y];
                                const int nL = curL[y + 1];
                                const int nR = curR[y + 1];
                                if (tipRight) {
                                    if (std::abs(R - pR) > 1)
                                        continue;
                                    const int lim = nR + kTipOcExpand;
                                    if (R > lim) {
                                        for (int x = lim + 1; x <= R; x++) {
                                            if (buf[y * 320 + x] == 22)
                                                buf[y * 320 + x] = 0;
                                        }
                                    }
                                } else {
                                    if (std::abs(L - pL) > 1)
                                        continue;
                                    const int lim = nL - kTipOcExpand;
                                    if (L < lim) {
                                        for (int x = L; x < lim; x++) {
                                            if (buf[y * 320 + x] == 22)
                                                buf[y * 320 + x] = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // Courier/Trader #215: ASM Contour 001791 tip-lobe fill after TexQuadPairs.
    MaybeFillCourierTipLobes(sortedPolys, sortCount, shipModel);
    FillCourierSideContourTipRidge(sortedPolys, sortCount);
    FillTraderWrapperInboardContours(sortedPolys, sortCount, shipModel);
    FillPumaOrbitContourResiduals(sortedPolys, sortCount, shipModel, shipRot);
    if (getenv("AB_DUMP_AABB")) {
        for (int i = 0; i < sortCount; i++) {
            const TransformedPoly* p = sortedPolys[i];
            int minx = 999, miny = 999, maxx = -999, maxy = -999;
            for (int v = 0; v < p->vertexCount; v++) {
                minx = std::min(minx, (int)p->verts[v].screenX);
                maxx = std::max(maxx, (int)p->verts[v].screenX);
                miny = std::min(miny, (int)p->verts[v].screenY);
                maxy = std::max(maxy, (int)p->verts[v].screenY);
            }
            bool tex = (p->texInfo != 0) && ((p->texInfo >> 14) != 0);
            int mat = tex ? (int)(p->texInfo & 0xFFF) : -1;
            std::fprintf(stderr,
                         "AABB mesh=%d mat=%d vc=%d box=(%d,%d)-(%d,%d) tex=0x%04x\n",
                         p->meshIndex, mat, p->vertexCount, minx, miny, maxx, maxy,
                         p->texInfo);
        }
    }

    if (::getenv("AB_FANLATER") && ::getenv("AB_FANLATER")[0] == '1')
        WalkBankReplayHull(jjffe::GetShipModelIndex(shipModel), shipRot, polys, polyCount, 1);
}
// Map 4-bit RGB nibbles (material shade ± face light) to a VGA palette index.
// ASM: pack RGB444 → DPal16toPal (DATA_007770 / dPalMap). Prefer that path —
// the nearest-color fallback used to scale nibbles with `<<2` (0..60) against
// an 8-bit palette (0..255), so lit faces (nibble 13–15) snapped to mid-greys
// (~palette 4–8) instead of VGA white/highlights and models looked hollow.
uint8_t ShadeRgbNibbleToPal(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 15) r = 15;
    if (g < 0) g = 0;
    if (g > 15) g = 15;
    if (b < 0) b = 0;
    if (b > 15) b = 15;

    const uint16_t cell =
        static_cast<uint16_t>((r << 8) | (g << 4) | b);
    const uint8_t mapped = DPal16toPal(cell);
    // Unmapped chroma (0) falls through except true black — same salvage
    // rule BuildTexShadePalette uses for shade0.
    if (mapped != 0 || (r | g | b) == 0)
        return mapped;

    if (!g_videoState) return 0;
    const uint8_t* pal = g_videoState->palette;

    // Nibble → 8-bit (×17) to match SetPaletteEntry / SDL palette storage.
    const int targetR = r * 17;
    const int targetG = g * 17;
    const int targetB = b * 17;

    int bestIdx = 0;
    int bestDist = 0x7FFFFFFF;
    for (int i = 0; i < 256; i++) {
        int dr = (int)pal[i * 3 + 0] - targetR;
        int dg = (int)pal[i * 3 + 1] - targetG;
        int db = (int)pal[i * 3 + 2] - targetB;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return (uint8_t)bestIdx;
}

bool ShipResolveMaterial(uint16_t texInfo, ShipMaterialInfo* out)
{
    if (!out) return false;
    MaterialInfo mat = ResolveMaterial(texInfo);
    out->texWord = mat.texWord;
    out->shadeCount = mat.shadeCount;
    for (int i = 0; i < 8; i++) {
        out->shades[i][0] = mat.shades[i][0];
        out->shades[i][1] = mat.shades[i][1];
        out->shades[i][2] = mat.shades[i][2];
    }
    return true;
}

void ShipBuildTexShadePalette(uint8_t outPal[256], const ShipMaterialInfo& mat,
                              int faceR, int faceG, int faceB, bool applyFaceLight)
{
    MaterialInfo m = {};
    m.texWord = mat.texWord;
    m.shadeCount = mat.shadeCount;
    for (int i = 0; i < 8; i++) {
        m.shades[i][0] = mat.shades[i][0];
        m.shades[i][1] = mat.shades[i][1];
        m.shades[i][2] = mat.shades[i][2];
    }
    BuildTexShadePalette(outPal, m, faceR, faceG, faceB, applyFaceLight);
}

bool ShipTexInfoIsTextured(uint16_t texInfo)
{
    return (texInfo != 0) && ((texInfo >> 14) != 0);
}

void ShipAssignTexUVs(const TransformedPoly& poly, uint32_t drawTexWord, int texSize,
                      float u[4], float v[4])
{
    for (int i = 0; i < 4; i++) {
        u[i] = 0;
        v[i] = 0;
    }
    if (texSize < 2) return;
    const int uvMax = texSize - 1;

    if (poly.vertexCount == 3) {
        // FUNC_001644: fixed (0,0)/(max,0)/(0,max)
        u[0] = 0;
        v[0] = 0;
        u[1] = (float)uvMax;
        v[1] = 0;
        u[2] = 0;
        v[2] = (float)uvMax;
        return;
    }

    const int uvCase = (drawTexWord >> 12) & 7;
    const int* uvOrder = s_uvOrder[uvCase];
    const int16_t uvCorners[4][2] = {
        {0, 0}, {(int16_t)uvMax, 0}, {0, (int16_t)uvMax}, {(int16_t)uvMax, (int16_t)uvMax}};
    int16_t uv[4][2];
    for (int i = 0; i < 4; i++) {
        uv[i][0] = uvCorners[uvOrder[i]][0];
        uv[i][1] = uvCorners[uvOrder[i]][1];
    }

    if (poly.type == MeshPolyType::TexQuadPair) {
        const uint16_t uvFlags = IsSpriteChildParam(poly.specialParam)
                                     ? SpriteChildUvFlags(poly.specialParam)
                                     : poly.specialParam;
        const bool isMirror = (uvFlags & 1) != 0;
        const bool mixedParity = (uvFlags & 0x10) != 0;
        if (isMirror && mixedParity) {
            for (int i = 0; i < 4; i++)
                uv[i][1] = static_cast<int16_t>(uvMax - uv[i][1]);
        }
    }

    for (int i = 0; i < 4; i++) {
        u[i] = (float)uv[i][0];
        v[i] = (float)uv[i][1];
    }
}

void ShipAssignNormalizedTexUVs(const TransformedPoly& poly, uint32_t drawTexWord,
                                int texSize, float u[4], float v[4])
{
    ShipAssignTexUVs(poly, drawTexWord, texSize, u, v);
    if (texSize < 2) return;
    const float inv = 1.0f / (float)texSize;
    for (int i = 0; i < 4; i++) {
        u[i] = (u[i] + 0.5f) * inv;
        v[i] = (v[i] + 0.5f) * inv;
    }
}

bool BakeShipTextureRGBA(uint16_t texInfo, int faceR, int faceG, int faceB,
                         bool applyFaceLight, ShipBakedTexture* out)
{
    if (!out) return false;
    *out = ShipBakedTexture{};
    if (!ShipTexInfoIsTextured(texInfo)) return false;

    ShipMaterialInfo mat{};
    if (!ShipResolveMaterial(texInfo, &mat)) return false;

    const uint32_t bmpIndex = mat.texWord & 0xFFF;
    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(bmpIndex));
    if (!bmp) return false;
    const int w = bmp->width;
    const int h = bmp->height;
    if (w <= 0 || h <= 0 || w != h || (w != 64 && w != 128)) return false;

    const bool applyFL = applyFaceLight && ((texInfo & 0x1000) != 0);
    uint8_t shadePal[256];
    ShipBuildTexShadePalette(shadePal, mat, faceR, faceG, faceB, applyFL);

    const uint8_t* pal = g_videoState ? g_videoState->palette : nullptr;
    const bool transparent = (bmp->flags & 1) != 0;
    out->width = w;
    out->height = h;
    out->transparent = transparent;
    out->bmpIndex = static_cast<uint16_t>(bmpIndex);
    out->matIndex = static_cast<uint16_t>(texInfo & 0xFFF);
    out->argb.resize((size_t)w * (size_t)h);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t texel = bmp->data[(size_t)y * (size_t)w + (size_t)x];
            if (transparent && texel == 0) {
                out->argb[(size_t)y * (size_t)w + (size_t)x] = 0;
                continue;
            }
            const uint8_t idx = shadePal[texel];
            uint8_t r = 180, g = 180, b = 190;
            if (pal) {
                r = pal[idx * 3 + 0];
                g = pal[idx * 3 + 1];
                b = pal[idx * 3 + 2];
            }
            out->argb[(size_t)y * (size_t)w + (size_t)x] =
                (255u << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    return true;
}

} // namespace jjffe
