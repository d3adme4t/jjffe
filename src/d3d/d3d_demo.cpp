#include "jjffe/d3d/d3d_demo.h"
#include "jjffe/d3d/d3d_backend.h"
#include "jjffe/d3d/d3d_mesh.h"
#include "jjffe/d3d/d3d_math.h"
#include "jjffe/demo3d.h"
#include "jjffe/intro_cast.h"
#include "jjffe/menu.h"
#include "jjffe/input.h"
#include "jjffe/platform.h"
#include "jjffe/math.h"
#include "jjffe/video.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_export.h"
#include "jjffe/ship_model_asset.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/contour_tessellate.h"
#include "jjffe/draw_main.h"
#include "jjffe/body_paint.h"
#include "jjffe/world.h"

#include <SDL.h>
#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace jjffe {
namespace d3d {
namespace {

bool s_ready = false;
int s_width = 640;
int s_height = 400;
int s_planetOverride = -1;
bool s_solMode = false;
std::unordered_map<uint64_t, MeshGpu> s_solMeshCache;
BodyPaintInstance s_solNav[64];
int s_solNavN = 0;
int s_solFocus = 0;
int s_solFocusFramed = -1;
bool s_solLeftWasDown = false;
bool s_solRightWasDown = false;
float s_solLookX = 0.f, s_solLookY = 0.f, s_solLookZ = 0.f;

int32_t s_yaw = 0;
int32_t s_pitch = 0;
float s_dist = 4.0f; // .x viewer default eye distance
int s_lastShipIdx = -1;
bool s_paused = true;
bool s_keyPWasDown = false;
bool s_legacyYawInited = false;
bool s_legacyDistInited = false;

// Set when --export-ship GPU groups are loaded (HUD third line).
char s_assetLabel[96] = {};

float FpAngleToRad(int32_t fp);
void ApplyCamera(IDirect3DDevice9* dev, float eyeDist);
void ApplyCameraSoftMatched(IDirect3DDevice9* dev, float shipDistWorld);
void BlitSoftUiAlpha(IDirect3DDevice9* dev);

// Soft-font HUD over the D3D scene (same CAM/YAW/PITCH line soft demo uses).
// Use DrawCharSimple (embedded 8×8 font) — DrawStringSimple→DrawString needs the
 // ASM font blob and can draw nothing on a cold D3D path.
static void HudDrawString(uint8_t* buf, int x, int y, const char* str, uint8_t color)
{
    int xPos = x;
    for (const char* p = str; p && *p; ++p) {
        DrawCharSimple(buf, xPos, y, *p, color);
        xPos += 8;
    }
}

void DrawDemoOrbitHud(IDirect3DDevice9* dev, int shipIdx, int camZUnits,
                      int polyCount, int fitUnits, size_t totalTris,
                      int contourGroups)
{
    if (!dev || !g_videoState) return;
    uint8_t* buf = g_videoState->backBuffer;
    memset(buf, 0xFF, SCREEN_WIDTH * SCREEN_HEIGHT);

    char txt[96];
    char nameBuf[48];
    const int yawDeg = (int)(((s_yaw & 0xFFFF) * 360LL) >> 16);
    const int pitchDeg = (int)(((s_pitch & 0xFFFF) * 360LL) >> 16);

    snprintf(txt, sizeof(txt),
             "CAM Z:%d EYE:%.1f YAW:%d PITCH:%d POLY:%d fit:%d c721:%d %s",
             camZUnits, s_dist, yawDeg, pitchDeg, polyCount, fitUnits,
             contourGroups, s_paused ? "[PAUSED]" : "");
    HudDrawString(buf, 10, 10, txt, 0x0F);

    const char* name = GetDemoShipDisplayName(shipIdx, nameBuf, sizeof(nameBuf));
    if (s_solMode) {
        const char* focusName = (s_solFocus >= 0 && s_solFocus < s_solNavN)
                                    ? s_solNav[s_solFocus].name
                                    : "Sol";
        const float rEarth = (s_solFocus >= 0 && s_solFocus < s_solNavN)
                                 ? (s_solNav[s_solFocus].scale / kSolRadScale)
                                 : 0.f;
        snprintf(txt, sizeof(txt),
                 "SOL  %d/%d %s  R=%.2f Re  LEFT/RIGHT",
                 s_solNavN > 0 ? s_solFocus + 1 : 0, s_solNavN, focusName,
                 rEarth);
    } else {
        snprintf(txt, sizeof(txt), "#%d %s  tris:%zu  ESC=ships", shipIdx, name,
                 totalTris);
    }
    HudDrawString(buf, 10, 20, txt, 0x0F);
    if (s_assetLabel[0])
        HudDrawString(buf, 10, 30, s_assetLabel, 0x0E);

    BlitSoftUiAlpha(dev);
}

// Soft→ARGB for D3D DIFFUSE (palette after DPalBuild in InitDemo3D).
DWORD SoftPalToD3d(uint8_t palIdx)
{
    if (!g_videoState) return D3DCOLOR_XRGB(180, 180, 190);
    const uint8_t* p = g_videoState->palette;
    return D3DCOLOR_XRGB(p[palIdx * 3 + 0], p[palIdx * 3 + 1], p[palIdx * 3 + 2]);
}

// ---------------------------------------------------------------------------
// Screen-space Contour fill (soft C721Span / Ogre DrawContourGroups).
// Plane-UV ContourHull invents needles on non-planar bezier Contours (Puma).
// Dig: JJFFE_CONTOUR_HULL=1 uses plane ContourHull (Clipper2). Default is
// soft-screen Contour overlay (C721Span / Ogre DrawContourGroups).
// ---------------------------------------------------------------------------
DWORD ContourFaceColor(const TransformedPoly& p, const ShipModel* model,
                       const int32_t* shipRotLit);

struct ContourRhwVert {
    float x, y, z, rhw;
    DWORD color;
};

void MulPointWvp(const D3DMATRIX& m, float x, float y, float z, float& X,
                 float& Y, float& Z, float& W)
{
    // D3D row-vector: out = v * M
    X = x * m._11 + y * m._21 + z * m._31 + m._41;
    Y = x * m._12 + y * m._22 + z * m._32 + m._42;
    Z = x * m._13 + y * m._23 + z * m._33 + m._43;
    W = x * m._14 + y * m._24 + z * m._34 + m._44;
}

bool ProjectWorldToScreen(const D3DMATRIX& wvp, float wx, float wy, float wz,
                          float& sx, float& sy, float& sz)
{
    float X, Y, Z, W;
    MulPointWvp(wvp, wx, wy, wz, X, Y, Z, W);
    // Tiny W → explosions (shards across the viewport).
    if (std::fabs(W) < 1e-3f) return false;
    const float iw = 1.0f / W;
    const float ndcX = X * iw;
    const float ndcY = Y * iw;
    sz = Z * iw;
    if (sz < -0.5f || sz > 1.5f) return false;
    sx = (ndcX * 0.5f + 0.5f) * (float)s_width;
    sy = (0.5f - ndcY * 0.5f) * (float)s_height;
    if (sx < -(float)s_width || sx > 2.0f * (float)s_width ||
        sy < -(float)s_height || sy > 2.0f * (float)s_height)
        return false;
    return true;
}

void SoftToScreenPx(float softX, float softY, float& ox, float& oy)
{
    // With ApplyCameraSoftMatched ASM-exact proj, Contours need no Ybias
    // (principal point is in the hull matrix). Dig: AB_D3D_YBIAS=<soft px>
    // (old +21 when AB_D3D_OLD_FOV=1). AB_D3D_OLD_FOV alone restores +21.
    static const float s_yBiasSoft = []() {
        float b = 0.0f;
        const bool oldFov =
            (std::getenv("AB_D3D_OLD_FOV") &&
             std::getenv("AB_D3D_OLD_FOV")[0] == '1') ||
            (std::getenv("AB_D3D_FOV50") &&
             std::getenv("AB_D3D_FOV50")[0] == '1');
        if (oldFov) b = 21.0f; // 100-79 geometric gap for non-ASM-exact FOV path
        if (const char* e = std::getenv("AB_D3D_YBIAS")) {
            if (e[0]) b = (float)std::atof(e);
            std::fprintf(stderr, "AB_D3D_YBIAS softY+=%.1f (Contour SoftToScreen)\n",
                         b);
        } else if (oldFov) {
            std::fprintf(stderr, "AB_D3D_OLD_FOV/FOV50: Contour softY+=21\n");
        }
        return b;
    }();
    ox = softX * ((float)s_width / 320.0f);
    oy = (softY + s_yBiasSoft) * ((float)s_height / 200.0f);
}

void SampleBezierScreen(float x0, float y0, float x1, float y1, float x2,
                        float y2, float x3, float y3, int steps,
                        std::vector<float>& xs, std::vector<float>& ys)
{
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / (float)steps;
        const float u = 1.0f - t;
        const float uu = u * u, tt = t * t;
        const float uuu = uu * u, ttt = tt * t;
        const float x =
            uuu * x0 + 3.0f * uu * t * x1 + 3.0f * u * tt * x2 + ttt * x3;
        const float y =
            uuu * y0 + 3.0f * uu * t * y1 + 3.0f * u * tt * y2 + ttt * y3;
        if (!xs.empty()) {
            const float dx = x - xs.back(), dy = y - ys.back();
            if (dx * dx + dy * dy < 0.25f) continue;
        }
        xs.push_back(x);
        ys.push_back(y);
    }
}

void FillContourEdgesEvenOddRhw(const std::vector<float>& ex0,
                                const std::vector<float>& ey0,
                                const std::vector<float>& ex1,
                                const std::vector<float>& ey1, float z,
                                DWORD col, std::vector<ContourRhwVert>& out)
{
    const int ne = (int)ex0.size();
    if (ne <= 0) return;
    float minY = ey0[0], maxY = ey0[0];
    for (int i = 0; i < ne; i++) {
        minY = std::min(minY, std::min(ey0[i], ey1[i]));
        maxY = std::max(maxY, std::max(ey0[i], ey1[i]));
    }
    const int y0 = (int)std::floor(minY);
    const int y1 = (int)std::ceil(maxY);
    if (y1 < y0) return;

    std::vector<std::vector<float>> hits((size_t)(y1 - y0 + 1));
    for (int i = 0; i < ne; i++) {
        float xA = ex0[i], yA = ey0[i], xB = ex1[i], yB = ey1[i];
        if (yA == yB) continue;
        if (yA > yB) {
            std::swap(xA, xB);
            std::swap(yA, yB);
        }
        const int ya = (int)std::ceil(yA);
        const int yb = (int)std::floor(yB);
        for (int y = ya; y <= yb; y++) {
            if (y < y0 || y > y1) continue;
            const float t = ((float)y - yA) / (yB - yA);
            hits[(size_t)(y - y0)].push_back(xA + t * (xB - xA));
        }
    }

    for (int y = y0; y <= y1; y++) {
        auto& row = hits[(size_t)(y - y0)];
        if (row.size() < 2) continue;
        std::sort(row.begin(), row.end());
        const float fy0 = (float)y - 0.5f;
        const float fy1 = (float)y + 0.5f;
        for (size_t i = 0; i + 1 < row.size(); i += 2) {
            const float xL = row[i], xR = row[i + 1];
            if (xR - xL < 0.5f) continue;
            ContourRhwVert v[6] = {
                {xL, fy0, z, 1.0f, col}, {xR, fy0, z, 1.0f, col},
                {xL, fy1, z, 1.0f, col}, {xR, fy0, z, 1.0f, col},
                {xR, fy1, z, 1.0f, col}, {xL, fy1, z, 1.0f, col},
            };
            out.insert(out.end(), v, v + 6);
        }
    }
}

float SoftDepthToZ(int32_t depth)
{
    if (depth <= 0) return 0.05f;
    return 0.05f + 0.90f * ((float)depth / ((float)depth + 65536.0f));
}

// Soft ProjectPoint (320×200) vs live WVP screen — proves focal/pp mismatch.
// Dig: AB_PROJ_DELTA=1 or AB_D3D_ALIGN_DUMP=1 → stderr every ~60 frames.
void DumpSoftVsWvpDelta(const TransformedPoly* polys, int visibleCount,
                        const D3DMATRIX& wvp)
{
    const bool dump =
        (std::getenv("AB_PROJ_DELTA") &&
         std::getenv("AB_PROJ_DELTA")[0] == '1') ||
        (std::getenv("AB_D3D_ALIGN_DUMP") &&
         std::getenv("AB_D3D_ALIGN_DUMP")[0] == '1');
    if (!dump) return;
    static int s_frames = 0;
    if ((s_frames++ % 60) != 0) return;

    const float sxScale = (float)s_width / 320.0f;
    const float syScale = (float)s_height / 200.0f;
    const float cxPx = 160.0f * sxScale;
    double sumAbsDx = 0, sumAbsDy = 0;
    double sumSxOff = 0, sumDxSxOff = 0, sumSxOff2 = 0;
    int n = 0, nPrint = 0;
    for (int i = 0; i < visibleCount && n < 256; i++) {
        const TransformedPoly& p = polys[i];
        if (!p.visible || p.backFacing) continue;
        for (int vi = 0; vi < p.vertexCount && vi < 4; vi++) {
            const float ssx = (float)p.verts[vi].screenX * sxScale;
            const float ssy = (float)p.verts[vi].screenY * syScale;
            float dsx, dsy, dsz;
            if (!ProjectWorldToScreen(
                    wvp, (float)p.verts[vi].worldX / (float)FP_ONE,
                    (float)p.verts[vi].worldY / (float)FP_ONE,
                    (float)p.verts[vi].worldZ / (float)FP_ONE, dsx, dsy, dsz))
                continue;
            const float dSx = dsx - ssx;
            const float dSy = dsy - ssy;
            const float sxOff = ssx - cxPx;
            sumAbsDx += std::fabs(dSx);
            sumAbsDy += std::fabs(dSy);
            sumSxOff += sxOff;
            sumDxSxOff += dSx * sxOff;
            sumSxOff2 += sxOff * sxOff;
            if (nPrint < 8) {
                std::fprintf(stderr,
                             "ABPROJ soft=(%.1f,%.1f) d3d=(%.1f,%.1f) "
                             "d=(%.1f,%.1f) worldZ=%.3f\n",
                             ssx, ssy, dsx, dsy, dSx, dSy,
                             (float)p.verts[vi].worldZ / (float)FP_ONE);
                nPrint++;
            }
            n++;
        }
    }
    float slope = 0.f;
    if (sumSxOff2 > 1.0) slope = (float)(sumDxSxOff / sumSxOff2);
    std::fprintf(stderr,
                 "ABPROJ n=%d mean|dSx|=%.2f mean|dSy|=%.2f "
                 "dSx≈%.4f*(sx-cx)  (expect ~0 if ASM proj matches soft)\n",
                 n, n ? sumAbsDx / n : 0.0, n ? sumAbsDy / n : 0.0, slope);
}

// Soft Contour curves (screenX/Y like SDL/Ogre) → XYZRHW overlay.
// Default: Ogre-compatible SoftToScreen only (no soft→WVP similarity fit).
// Parked dig: AB_CONTOUR_FIT=1 re-enables the known-bad global mapSoft fit.
// Dig: AB_CONTOUR_FIT_DUMP=1 → stderr fit params + per-group AABB before/after.
// Legacy: AB_CONTOUR_NO_FIT=1 still forces no-fit if FIT is also set.
int BuildScreenContourRhw(const TransformedPoly* polys, int visibleCount,
                          const ShipModel* model, const int32_t* shipRotLit,
                          const D3DMATRIX& wvp,
                          std::vector<ContourRhwVert>& out)
{
    std::vector<char> done((size_t)visibleCount, 0);
    int groups = 0;

    const bool forceNoFit =
        std::getenv("AB_CONTOUR_NO_FIT") &&
        std::getenv("AB_CONTOUR_NO_FIT")[0] == '1';
    const bool useFit =
        !forceNoFit && std::getenv("AB_CONTOUR_FIT") &&
        std::getenv("AB_CONTOUR_FIT")[0] == '1';
    const bool noFit = !useFit;
    const bool fitDump =
        std::getenv("AB_CONTOUR_FIT_DUMP") &&
        std::getenv("AB_CONTOUR_FIT_DUMP")[0] == '1';

    struct Pair {
        float sx, sy, dx, dy;
    };
    std::vector<Pair> pairs;
    pairs.reserve(128);
    double sumSx = 0, sumSy = 0, sumDx = 0, sumDy = 0;
    int nPair = 0;
    // Skip pair gather on default Ogre path; still gather if dump or fit dig.
    if (useFit || fitDump) {
        for (int i = 0; i < visibleCount; i++) {
            const TransformedPoly& p = polys[i];
            if (!p.visible || p.backFacing) continue;
            if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
            if (IsContourGlintPoly(p)) continue;
            for (int vi = 0; vi < p.vertexCount && vi < 4; vi++) {
                float sx, sy, dx, dy, dz;
                SoftToScreenPx((float)p.verts[vi].screenX,
                               (float)p.verts[vi].screenY, sx, sy);
                if (!ProjectWorldToScreen(
                        wvp, (float)p.verts[vi].worldX / (float)FP_ONE,
                        (float)p.verts[vi].worldY / (float)FP_ONE,
                        (float)p.verts[vi].worldZ / (float)FP_ONE, dx, dy, dz))
                    continue;
                pairs.push_back({sx, sy, dx, dy});
                sumSx += sx;
                sumSy += sy;
                sumDx += dx;
                sumDy += dy;
                nPair++;
            }
        }
    }
    float mapScale = 1.0f, mapOx = 0.0f, mapOy = 0.0f;
    if (useFit && nPair >= 2) {
        const double csx = sumSx / nPair, csy = sumSy / nPair;
        const double cdx = sumDx / nPair, cdy = sumDy / nPair;
        double num = 0, den = 0;
        for (const Pair& p : pairs) {
            const double ssx = p.sx - csx, ssy = p.sy - csy;
            const double ddx = p.dx - cdx, ddy = p.dy - cdy;
            num += ssx * ddx + ssy * ddy;
            den += ssx * ssx + ssy * ssy;
        }
        if (den > 1e-6) {
            mapScale = (float)(num / den);
            if (mapScale < 0.05f) mapScale = 0.05f;
            if (mapScale > 20.0f) mapScale = 20.0f;
            mapOx = (float)(cdx - mapScale * csx);
            mapOy = (float)(cdy - mapScale * csy);
        }
    }
    int nContourPoly = 0, nProjFail = 0, nProjOk = 0;
    for (int i = 0; i < visibleCount; i++) {
        const TransformedPoly& p = polys[i];
        if (!p.visible || p.backFacing) continue;
        if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
        if (IsContourGlintPoly(p)) continue;
        nContourPoly++;
    }
    // Recount proj ok/fail for dump (independent of noFit skip).
    if (fitDump) {
        for (int i = 0; i < visibleCount; i++) {
            const TransformedPoly& p = polys[i];
            if (!p.visible || p.backFacing) continue;
            if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
            if (IsContourGlintPoly(p)) continue;
            for (int vi = 0; vi < p.vertexCount && vi < 4; vi++) {
                float dx, dy, dz;
                if (ProjectWorldToScreen(
                        wvp, (float)p.verts[vi].worldX / (float)FP_ONE,
                        (float)p.verts[vi].worldY / (float)FP_ONE,
                        (float)p.verts[vi].worldZ / (float)FP_ONE, dx, dy,
                        dz))
                    nProjOk++;
                else
                    nProjFail++;
            }
        }
    }
    if (fitDump) {
        static int s_fitDumpFrames = 0;
        if ((s_fitDumpFrames++ % 30) == 0) {
            const int ship =
                model ? GetShipModelIndex(model) : -1;
            std::fprintf(stderr,
                         "ABFIT ship=%d eye=%.3f nPair=%d scale=%.6f ox=%.3f "
                         "oy=%.3f noFit=%d nContourPoly=%d nProjOk=%d "
                         "nProjFail=%d\n",
                         ship, s_dist, nPair, mapScale, mapOx, mapOy,
                         noFit ? 1 : 0, nContourPoly, nProjOk, nProjFail);
        }
    }
    auto mapSoft = [&](float sx, float sy, float& ox, float& oy) {
        if (noFit) {
            ox = sx;
            oy = sy;
            return;
        }
        ox = mapOx + mapScale * sx;
        oy = mapOy + mapScale * sy;
    };
    auto softVert = [&](const ProjectedVertex& v, float& sx, float& sy) {
        float rawX, rawY;
        SoftToScreenPx((float)v.screenX, (float)v.screenY, rawX, rawY);
        mapSoft(rawX, rawY, sx, sy);
    };

    for (int i = 0; i < visibleCount; i++) {
        if (done[i]) continue;
        const TransformedPoly& p = polys[i];
        if (!p.visible || p.backFacing) continue;
        if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
        if (IsContourGlintPoly(p)) continue;
        if ((p.texInfo == 0x1000 || p.texInfo == 0x4028) &&
            !(std::getenv("AB_C721_KEEP_VIPER") &&
              std::getenv("AB_C721_KEEP_VIPER")[0] == '1')) {
            for (int j = 0; j < visibleCount; j++) {
                if (polys[j].type == MeshPolyType::ComplexFill &&
                    polys[j].contourId == p.contourId)
                    done[j] = 1;
            }
            continue;
        }

        const TransformedPoly* group[64];
        int ng = 0;
        for (int j = 0; j < visibleCount && ng < 64; j++) {
            if (!polys[j].visible || polys[j].backFacing) continue;
            if (polys[j].type != MeshPolyType::ComplexFill) continue;
            if (polys[j].contourId != p.contourId) continue;
            if (IsContourGlintPoly(polys[j])) continue;
            group[ng++] = &polys[j];
            done[j] = 1;
        }
        if (ng == 0) continue;

        for (int a = 0; a < ng; a++) {
            for (int b = a + 1; b < ng; b++) {
                if (group[b]->meshIndex < group[a]->meshIndex) {
                    const TransformedPoly* tmp = group[a];
                    group[a] = group[b];
                    group[b] = tmp;
                }
            }
        }

        if (model && GetShipModelIndex(model) == 20 &&
            group[0]->texInfo != 0x20ee) {
            groups++;
            continue;
        }

        bool anyQuad = false, anyEdge = false, anyTri = false;
        for (int g = 0; g < ng; g++) {
            const int vc = group[g]->vertexCount;
            if (vc == 4) anyQuad = true;
            else if (vc == 2) anyEdge = true;
            else if (vc == 3) anyTri = true;
        }
        if (!anyQuad && !anyEdge && anyTri) {
            const DWORD col = ContourFaceColor(*group[0], model, shipRotLit);
            for (int g = 0; g < ng; g++) {
                const TransformedPoly& q = *group[g];
                if (q.vertexCount != 3) continue;
                float sx[3], sy[3];
                for (int vi = 0; vi < 3; vi++)
                    softVert(q.verts[vi], sx[vi], sy[vi]);
                const float z = SoftDepthToZ(q.verts[0].depth);
                ContourRhwVert t[3] = {{sx[0], sy[0], z, 1.0f, col},
                                      {sx[1], sy[1], z, 1.0f, col},
                                      {sx[2], sy[2], z, 1.0f, col}};
                out.insert(out.end(), t, t + 3);
            }
            groups++;
            continue;
        }

        std::vector<float> e0x, e0y, e1x, e1y;
        float firstX = 0, firstY = 0, lastX = 0, lastY = 0;
        bool haveFirst = false;
        int nQuads = 0;
        float aabbX0 = 1e9f, aabbY0 = 1e9f, aabbX1 = -1e9f, aabbY1 = -1e9f;
        float rawX0 = 1e9f, rawY0 = 1e9f, rawX1 = -1e9f, rawY1 = -1e9f;
        float zMax = 0.f;

        auto noteRaw = [&](float x, float y) {
            rawX0 = std::min(rawX0, x);
            rawY0 = std::min(rawY0, y);
            rawX1 = std::max(rawX1, x);
            rawY1 = std::max(rawY1, y);
        };
        auto addEdge = [&](float x0, float y0, float x1, float y1) {
            e0x.push_back(x0);
            e0y.push_back(y0);
            e1x.push_back(x1);
            e1y.push_back(y1);
            aabbX0 = std::min(aabbX0, std::min(x0, x1));
            aabbY0 = std::min(aabbY0, std::min(y0, y1));
            aabbX1 = std::max(aabbX1, std::max(x0, x1));
            aabbY1 = std::max(aabbY1, std::max(y0, y1));
        };

        for (int g = 0; g < ng; g++) {
            const TransformedPoly& q = *group[g];
            for (int vi = 0; vi < q.vertexCount && vi < 4; vi++)
                zMax = std::max(zMax, (float)q.verts[vi].depth);

            if (q.vertexCount == 4) {
                nQuads++;
                float sx[4], sy[4];
                for (int vi = 0; vi < 4; vi++) {
                    SoftToScreenPx((float)q.verts[vi].screenX,
                                   (float)q.verts[vi].screenY, sx[vi], sy[vi]);
                    noteRaw(sx[vi], sy[vi]);
                }
                std::vector<float> bx, by;
                SampleBezierScreen(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2],
                                   sx[3], sy[3], 16, bx, by);
                for (size_t k = 1; k < bx.size(); k++) {
                    noteRaw(bx[k], by[k]);
                    float x0, y0, x1, y1;
                    mapSoft(bx[k - 1], by[k - 1], x0, y0);
                    mapSoft(bx[k], by[k], x1, y1);
                    addEdge(x0, y0, x1, y1);
                }
                float fx, fy, lx, ly;
                mapSoft(sx[0], sy[0], fx, fy);
                mapSoft(sx[3], sy[3], lx, ly);
                if (!haveFirst) {
                    firstX = fx;
                    firstY = fy;
                    haveFirst = true;
                }
                lastX = lx;
                lastY = ly;
            } else if (q.vertexCount == 2) {
                float rx0, ry0, rx1, ry1;
                SoftToScreenPx((float)q.verts[0].screenX,
                               (float)q.verts[0].screenY, rx0, ry0);
                SoftToScreenPx((float)q.verts[1].screenX,
                               (float)q.verts[1].screenY, rx1, ry1);
                noteRaw(rx0, ry0);
                noteRaw(rx1, ry1);
                float x0, y0, x1, y1;
                mapSoft(rx0, ry0, x0, y0);
                mapSoft(rx1, ry1, x1, y1);
                addEdge(x0, y0, x1, y1);
                if (!haveFirst) {
                    firstX = x0;
                    firstY = y0;
                    haveFirst = true;
                }
                lastX = x1;
                lastY = y1;
            } else if (q.vertexCount == 3) {
                float rx[3], ry[3], x[3], y[3];
                for (int vi = 0; vi < 3; vi++) {
                    SoftToScreenPx((float)q.verts[vi].screenX,
                                   (float)q.verts[vi].screenY, rx[vi], ry[vi]);
                    noteRaw(rx[vi], ry[vi]);
                    mapSoft(rx[vi], ry[vi], x[vi], y[vi]);
                }
                addEdge(x[0], y[0], x[1], y[1]);
                addEdge(x[1], y[1], x[2], y[2]);
                addEdge(x[2], y[2], x[0], y[0]);
            }
        }
        if (e0x.empty()) continue;

        const float spanW = aabbX1 - aabbX0;
        const float spanH = aabbY1 - aabbY0;
        const bool wideContour = spanW * 5.0f >= spanH * 9.0f;
        const bool digonWideClose =
            (nQuads == 2 || (nQuads == 1 && group[0]->texInfo == 0x4008)) &&
            wideContour;
        if (haveFirst && (!wideContour || digonWideClose)) {
            const float dx = lastX - firstX, dy = lastY - firstY;
            if (dx * dx + dy * dy > 1.0f)
                addEdge(lastX, lastY, firstX, firstY);
        }

        if (fitDump && rawX1 >= rawX0 && aabbX1 >= aabbX0) {
            static int s_aabbDump = 0;
            if (s_aabbDump < 24) {
                const float rawW = rawX1 - rawX0;
                const float rawH = rawY1 - rawY0;
                const float fitW = aabbX1 - aabbX0;
                const float fitH = aabbY1 - aabbY0;
                const float cxRaw = 0.5f * (rawX0 + rawX1);
                const float cyRaw = 0.5f * (rawY0 + rawY1);
                const float cxFit = 0.5f * (aabbX0 + aabbX1);
                const float cyFit = 0.5f * (aabbY0 + aabbY1);
                std::fprintf(
                    stderr,
                    "ABFIT_AABB cid=%d mesh=%d n=%d raw=[%.1f,%.1f]-[%.1f,%.1f] "
                    "fit=[%.1f,%.1f]-[%.1f,%.1f] ratioX=%.3f ratioY=%.3f "
                    "dCx=%.1f dCy=%.1f\n",
                    group[0]->contourId, group[0]->meshIndex, ng, rawX0, rawY0,
                    rawX1, rawY1, aabbX0, aabbY0, aabbX1, aabbY1,
                    (rawW > 1e-3f) ? fitW / rawW : 0.f,
                    (rawH > 1e-3f) ? fitH / rawH : 0.f, cxFit - cxRaw,
                    cyFit - cyRaw);
                s_aabbDump++;
            }
        }

        const DWORD col = ContourFaceColor(*group[0], model, shipRotLit);
        const float z = SoftDepthToZ((int32_t)zMax);
        FillContourEdgesEvenOddRhw(e0x, e0y, e1x, e1y, z, col, out);
        groups++;
    }
    if (fitDump) {
        static int s_outDump = 0;
        if ((s_outDump++ % 30) == 0) {
            std::fprintf(stderr,
                         "ABFIT_OUT groups=%d rhwVerts=%d nContourPoly=%d\n",
                         groups, (int)out.size(), nContourPoly);
        }
    }
    return groups;
}

// Match Ogre ContourColor / soft ComplexFillColor (material shade0).
DWORD ContourFaceColor(const TransformedPoly& p, const ShipModel* model,
                       const int32_t* shipRotLit)
{
    const uint16_t ti = p.texInfo;
    if ((ti >> 14) == 0) {
        if (ti == 0) return SoftPalToD3d(0);
        int r = (ti >> 8) & 0xF;
        int g = (ti >> 4) & 0xF;
        int b = ti & 0xF;
        if ((ti & 0x1000) != 0) {
            r += p.faceLight[0];
            g += p.faceLight[1];
            b += p.faceLight[2];
            if (r < 0) r = 0;
            if (r > 15) r = 15;
            if (g < 0) g = 0;
            if (g > 15) g = 15;
            if (b < 0) b = 0;
            if (b > 15) b = 15;
        }
        return SoftPalToD3d(ShadeRgbNibbleToPal(r, g, b));
    }
    if (ti == 0x5015 || ti == 0x5009) {
        ShipMaterialInfo mat{};
        if (ShipResolveMaterial(ti, &mat) && mat.shadeCount > 0) {
            return SoftPalToD3d(ShadeRgbNibbleToPal(mat.shades[0][0],
                                                   mat.shades[0][1],
                                                   mat.shades[0][2]));
        }
    }
    if ((ti & 0xF000) == 0x5000) {
        uint8_t lit = GetFaceColor(model, p.normalIdx, 0x08, shipRotLit);
        if (lit < 4) lit = 4;
        return SoftPalToD3d(lit);
    }
    ShipMaterialInfo mat{};
    if (ShipResolveMaterial(ti, &mat)) {
        const bool applyFL =
            (ti & 0x1000) != 0 &&
            (p.faceLight[0] | p.faceLight[1] | p.faceLight[2]) != 0;
        uint8_t shadePal[256];
        ShipBuildTexShadePalette(shadePal, mat, p.faceLight[0], p.faceLight[1],
                                 p.faceLight[2], applyFL);
        return SoftPalToD3d(shadePal[0]);
    }
    uint8_t lit = GetFaceColor(model, p.normalIdx, 0x08, shipRotLit);
    if (lit < 4) lit = 4;
    return SoftPalToD3d(lit);
}

// Match Ogre PolyColor / soft FlatRgb punches (canopy, nozzles).
DWORD LegacyFaceColor(const TransformedPoly& p, const ShipModel* model,
                      const int32_t* shipRotLit)
{
    const uint16_t ti = p.texInfo;
    uint8_t lit = GetFaceColor(model, p.normalIdx, 0x08, shipRotLit);
    if (lit < 2) lit = 2;

    if (ti == 0 || ti == 0x2000)
        return D3DCOLOR_XRGB(0, 0, 0);
    // Match Ogre FaceBaseColor: only Adder #29 forces canopy 0x20ee→black.
    // Krait/Gecko/Viper engine marks use FlatRgb→DPal (often VGA0 dark rects).
    if (ti == 0x20ee && (p.specialParam & 0xFFF0) != 0x7360 && model &&
        GetShipModelIndex(model) == 29)
        return D3DCOLOR_XRGB(0, 0, 0);
    if (ti == 0x3000) {
        return SoftPalToD3d(ShadeRgbNibbleToPal(
            p.faceLight[0], p.faceLight[1], p.faceLight[2]));
    }

    const uint16_t kind = static_cast<uint16_t>(ti >> 14);
    if (kind == 0) {
        int r = (ti >> 8) & 0xF;
        int g = (ti >> 4) & 0xF;
        int b = ti & 0xF;
        if ((ti & 0x1000) != 0) {
            r += p.faceLight[0];
            g += p.faceLight[1];
            b += p.faceLight[2];
            if (r < 0) r = 0;
            if (r > 15) r = 15;
            if (g < 0) g = 0;
            if (g > 15) g = 15;
            if (b < 0) b = 0;
            if (b > 15) b = 15;
        }
        const uint8_t pal = ShadeRgbNibbleToPal(r, g, b);
        // 0x1000 RGB0+face-light: when DPal collapses to VGA0, soft still
        // shades by authored normal (Gecko/Krait rear caps).
        if (pal == 0 && ti == 0x1000)
            return SoftPalToD3d(lit);
        if (p.afterSpecialPoly && pal == 0)
            return D3DCOLOR_XRGB(0, 0, 0);
        return SoftPalToD3d(pal);
    }

    // Textured materials: vertex colour modulates the bitmap.
    return SoftPalToD3d(lit);
}

struct LegacyTexKey {
    uint16_t bmpIndex = 0;
    uint16_t matIndex = 0;
    int8_t flR = 0, flG = 0, flB = 0;
    uint8_t applyFL = 0;
    bool operator==(const LegacyTexKey& o) const
    {
        return bmpIndex == o.bmpIndex && matIndex == o.matIndex && flR == o.flR &&
               flG == o.flG && flB == o.flB && applyFL == o.applyFL;
    }
};
struct LegacyTexKeyHash {
    size_t operator()(const LegacyTexKey& k) const
    {
        size_t h = k.bmpIndex;
        h = h * 131u + k.matIndex;
        h = h * 131u + (uint8_t)k.flR;
        h = h * 131u + (uint8_t)k.flG;
        h = h * 131u + (uint8_t)k.flB;
        h = h * 131u + k.applyFL;
        return h;
    }
};
struct LegacyTexEntry {
    IDirect3DTexture9* tex = nullptr;
    int texSize = 0;
    bool transparent = false;
};
std::unordered_map<LegacyTexKey, LegacyTexEntry, LegacyTexKeyHash> s_legacyTexCache;

IDirect3DTexture9* GetLegacyShipTexture(IDirect3DDevice9* dev, uint16_t texInfo,
                                        int faceR, int faceG, int faceB,
                                        int* outTexSize, bool* outTransparent)
{
    if (outTexSize) *outTexSize = 0;
    if (outTransparent) *outTransparent = false;
    if (!ShipTexInfoIsTextured(texInfo)) return nullptr;

    ShipMaterialInfo mat{};
    if (!ShipResolveMaterial(texInfo, &mat)) return nullptr;
    const uint32_t bmpIndex = mat.texWord & 0xFFF;
    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(bmpIndex));
    if (!bmp) return nullptr;
    const int w = bmp->width;
    const int h = bmp->height;
    if (w <= 0 || h <= 0 || w != h || (w != 64 && w != 128)) return nullptr;

    bool applyFL = (texInfo & 0x1000) != 0;
    LegacyTexKey key{};
    key.bmpIndex = static_cast<uint16_t>(bmpIndex);
    key.matIndex = texInfo & 0xFFF;
    key.flR = applyFL ? static_cast<int8_t>(faceR) : 0;
    key.flG = applyFL ? static_cast<int8_t>(faceG) : 0;
    key.flB = applyFL ? static_cast<int8_t>(faceB) : 0;
    key.applyFL = applyFL ? 1 : 0;

    auto it = s_legacyTexCache.find(key);
    if (it != s_legacyTexCache.end()) {
        if (outTexSize) *outTexSize = it->second.texSize;
        if (outTransparent) *outTransparent = it->second.transparent;
        return it->second.tex;
    }

    ShipBakedTexture baked;
    if (!BakeShipTextureRGBA(texInfo, faceR, faceG, faceB, /*applyFaceLight=*/true,
                             &baked))
        return nullptr;

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(baked.width, baked.height, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;

    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    for (int y = 0; y < baked.height; y++) {
        auto* dst = reinterpret_cast<DWORD*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
        const uint32_t* src =
            baked.argb.data() + (size_t)y * (size_t)baked.width;
        memcpy(dst, src, (size_t)baked.width * sizeof(uint32_t));
    }
    tex->UnlockRect(0);

    LegacyTexEntry e{};
    e.tex = tex;
    e.texSize = baked.width;
    e.transparent = baked.transparent;
    s_legacyTexCache.emplace(key, e);
    if (outTexSize) *outTexSize = e.texSize;
    if (outTransparent) *outTransparent = e.transparent;
    return tex;
}

void ClearLegacyTexCache()
{
    for (auto& kv : s_legacyTexCache) {
        if (kv.second.tex) kv.second.tex->Release();
    }
    s_legacyTexCache.clear();
}

// FFED3D-style: mesh bytecode → BuildWorldShipFaces → D3D triangles
void DrawLegacyMeshD3d(IDirect3DDevice9* dev, int shipIdx)
{
    const ShipModel* model = GetShipModel(shipIdx);
    if (!model || !model->meshDataOffset) return;

    if (!s_legacyYawInited || shipIdx != s_lastShipIdx) {
        s_yaw = (int32_t)((180 * 65536LL) / 360);
        s_pitch = 0;
        s_legacyYawInited = true;
        s_legacyDistInited = false;
        s_dist = 1.5f;
    }

    Mat33 rotMat;
    MatBuildYZT(&rotMat, s_yaw, s_pitch);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];

    Camera3D camera;
    CameraInit(&camera);
    CameraSetPosition(&camera, 0, 0, 0);
    for (int i = 0; i < 9; i++) camera.rotation.m[i] = 0;
    camera.rotation.m[0] = FP_ONE;
    camera.rotation.m[4] = FP_ONE;
    camera.rotation.m[8] = FP_ONE;

    // Soft Contour screenX/Y and mesh world Z must track EYE (Ogre pattern:
    // one s_dist drives TransformShipVertices + HW view). A fixed autofit
    // shipDist left Contours screen-flat while ApplyCamera(s_dist) zoomed the
    // hull — the parked similarity fit was papering over that split.
    const int fitUnits =
        AbAutoFitDistUnits(model->radius, model->scaleRelated);
    int32_t shipDist =
        (int32_t)std::llround((double)s_dist * (double)FP_ONE);
    if (shipDist < FP_ONE / 8) shipDist = FP_ONE / 8;
    if (shipDist > 40 * FP_ONE) shipDist = 40 * FP_ONE;
    int32_t shipPos[3] = {0, 0, shipDist};

    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) return;
    size_t meshSize = MeshBytecodeSize(shipIdx);
    if (meshSize == 0) meshSize = 1024;

    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    if (!mesh.valid || mesh.polygons.empty()) return;

    TransformedPoly polys[512];
    int visibleCount =
        TransformShipVertices(model, mesh, shipRot, shipPos, &camera, polys, 512);
    if (visibleCount <= 0) {
        DrawDemoOrbitHud(dev, shipIdx, (int)(shipDist / 256), 0, fitUnits, 0, 0);
        return;
    }

    ShipPrepareFlatPunchGates(polys, visibleCount, model);
    WorldFace faces[512];
    const int faceCount =
        BuildWorldShipFaces(polys, visibleCount, model, shipRotLit, faces, 512,
                            /*includeHidden=*/true);
    WorldFace contourFaces[8192];
    const bool useContourHull =
        std::getenv("JJFFE_CONTOUR_HULL") &&
        std::getenv("JJFFE_CONTOUR_HULL")[0] == '1';
    // Default: soft-screen Contour overlay (matches SDL C721Span outlines).
    // Dig: JJFFE_CONTOUR_HULL=1 for plane Clipper2 ContourHull (still messy).
    const int contourCount =
        useContourHull
            ? BuildContourWorldFaces(polys, visibleCount, model, shipRotLit,
                                     contourFaces, 8192)
            : 0;

    int contourGroups = 0;
    for (int i = 0; i < visibleCount; i++) {
        if (polys[i].contourId >= 0) contourGroups++;
    }

    const float zd = (float)shipDist / (float)FP_ONE;
    D3DMATRIX world;
    MatIdentity(&world);
    // Keep soft world Z (no -zd recenter). Soft Contours are SoftToScreen of
    // ProjectPoint at this Z; Ogre HW does the same. Dig: AB_D3D_CENTER_XY=1
    // restores the old bbox XY recenter (detaches Contours).
    ApplyCameraSoftMatched(dev, zd);
    dev->SetTransform(D3DTS_WORLD, &world);

    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    struct VN {
        float x, y, z;
        float nx, ny, nz;
        DWORD color;
        float u, v;
    };
    struct TexBatch {
        IDirect3DTexture9* tex = nullptr;
        bool transparent = false;
        std::vector<VN> tris;
    };
    std::vector<VN> flatTris;
    std::vector<VN> glassTris; // windshields — drawn last with depth bias
    std::vector<VN> contourTris; // ContourHull — clip-space depth bias pass
    std::vector<TexBatch> texBatches;
    flatTris.reserve((size_t)faceCount * 6);
    glassTris.reserve(64);
    contourTris.reserve((size_t)contourCount * 3);

    auto findBatch = [&](IDirect3DTexture9* tex, bool transparent) -> TexBatch& {
        for (auto& b : texBatches) {
            if (b.tex == tex && b.transparent == transparent) return b;
        }
        texBatches.push_back({});
        texBatches.back().tex = tex;
        texBatches.back().transparent = transparent;
        return texBatches.back();
    };

    auto emitTri = [&](std::vector<VN>& dst, const WorldFaceVert& a,
                       const WorldFaceVert& b, const WorldFaceVert& c, DWORD col,
                       float ua, float va, float ub, float vb, float uc, float vc) {
        dst.push_back({a.x, a.y, a.z, a.nx, a.ny, a.nz, col, ua, va});
        dst.push_back({b.x, b.y, b.z, b.nx, b.ny, b.nz, col, ub, vb});
        dst.push_back({c.x, c.y, c.z, c.nx, c.ny, c.nz, col, uc, vc});
    };

    float bmin[3] = {1e9f, 1e9f, 1e9f}, bmax[3] = {-1e9f, -1e9f, -1e9f};
    auto grow = [&](const WorldFaceVert& v) {
        bmin[0] = std::min(bmin[0], v.x);
        bmin[1] = std::min(bmin[1], v.y);
        bmin[2] = std::min(bmin[2], v.z);
        bmax[0] = std::max(bmax[0], v.x);
        bmax[1] = std::max(bmax[1], v.y);
        bmax[2] = std::max(bmax[2], v.z);
    };

    for (int i = 0; i < faceCount; i++) {
        WorldFace f = faces[i]; // mutable copy for ribbon expand
        // Soft PolyWorldValid can reject faces that are still fine for D3D once
        // the orbit camera moves — keep any tri with authored world verts.
        if (f.vertexCount < 2) continue;

        // PlainLine / DirLine (Shuttle landing rails): expand 2-vert segments
        // into camera-facing ribbons (same as OgreShipHwDrawHull).
        if (f.vertexCount == 2) {
            WorldFaceVert a = f.verts[0];
            WorldFaceVert b = f.verts[1];
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            const float midX = 0.5f * (a.x + b.x);
            const float midY = 0.5f * (a.y + b.y);
            const float midZ = 0.5f * (a.z + b.z);
            float vx = -midX, vy = -midY + 0.35f * s_dist, vz = -midZ + s_dist;
            float cx = dy * vz - dz * vy;
            float cy = dz * vx - dx * vz;
            float cz = dx * vy - dy * vx;
            float clen = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (clen < 1e-6f) {
                cx = -dy;
                cy = dx;
                cz = 0.0f;
                clen = std::sqrt(cx * cx + cy * cy + cz * cz);
            }
            if (clen < 1e-6f) continue;
            // Slightly thicker than soft 1px so rails read at 640x400.
            const float half = std::max(0.003f, std::fabs(midZ) / 256.0f);
            cx = cx / clen * half;
            cy = cy / clen * half;
            cz = cz / clen * half;
            f.verts[0] = a; f.verts[0].x += cx; f.verts[0].y += cy; f.verts[0].z += cz;
            f.verts[1] = b; f.verts[1].x += cx; f.verts[1].y += cy; f.verts[1].z += cz;
            f.verts[2] = b; f.verts[2].x -= cx; f.verts[2].y -= cy; f.verts[2].z -= cz;
            f.verts[3] = a; f.verts[3].x -= cx; f.verts[3].y -= cy; f.verts[3].z -= cz;
            f.vertexCount = 4;
        }

        if (f.vertexCount < 3) continue;
        // Contour fill geometry comes from BuildContourWorldFaces (ContourHull).
        // Keep ScreenHybrid Contours out of the hull stub path (glints stay out).
        if (f.kind == WorldFaceKind::ScreenHybrid &&
            f.type == MeshPolyType::ComplexFill && f.contourId >= 0)
            continue;
        if (f.kind == WorldFaceKind::ScreenHybrid) continue;
        // Do NOT use soft backFacing: soft cam ≠ D3D orbit cam.
        if (f.sourceIndex < 0 || f.sourceIndex >= visibleCount) continue;
        const TransformedPoly& p = polys[f.sourceIndex];

        // Soft ShipAllowVga0FlatPunch skips most 0x2000 (painter underlay).
        // D3D needs those panes as real black geometry (windshields / glass).
        // Still skip gated tip-child overpunches that only exist to wipe texture.
        if (f.kind == WorldFaceKind::Punch &&
            (p.texInfo == 0x2000 || p.texInfo == 0 ||
             (p.texInfo == 0x20ee && (p.specialParam & 0xFFF0) != 0x7360))) {
            const bool tipChild2000 =
                p.texInfo == 0x2000 && IsSpriteChildParam(p.specialParam) &&
                SpriteChildModelId(p.specialParam) == 201;
            if (tipChild2000 && !ShipAllowVga0FlatPunch(p, model, 0))
                continue;
        }

        DWORD col = LegacyFaceColor(p, model, shipRotLit);

        // Shuttle cockpit: FlatQuad texInfo=0 after 0xAB gate. Soft often marks
        // it backFacing (FUNC_001481) while still counting it as a window; on
        // D3D it loses the Z fight to surrounding 0x5009 hull unless drawn last.
        // 0x2000 / Flat0: force black glass. 0x20ee engine marks keep FlatRgb
        // colour (Krait/Viper); Adder canopy still black via LegacyFaceColor.
        const bool shuttleGlass =
            (p.type == MeshPolyType::FlatQuad && p.afterSpecialPoly) ||
            (p.type == MeshPolyType::FlatQuad && p.texInfo == 0) ||
            (f.kind == WorldFaceKind::Punch &&
             (p.texInfo == 0 || p.texInfo == 0x20ee || p.texInfo == 0x2000));
        if (shuttleGlass && p.texInfo != 0x20ee)
            col = D3DCOLOR_XRGB(0, 0, 0);
        else if (shuttleGlass && p.texInfo == 0x20ee && model &&
                 GetShipModelIndex(model) == 29)
            col = D3DCOLOR_XRGB(0, 0, 0);

        int texSize = 0;
        bool transparent = false;
        IDirect3DTexture9* tex = nullptr;
        float u[4] = {}, v[4] = {};
        if (!shuttleGlass && f.kind != WorldFaceKind::Punch &&
            ShipTexInfoIsTextured(p.texInfo)) {
            tex = GetLegacyShipTexture(dev, p.texInfo, p.faceLight[0], p.faceLight[1],
                                       p.faceLight[2], &texSize, &transparent);
            if (tex && texSize >= 2) {
                ShipMaterialInfo minfo{};
                if (ShipResolveMaterial(p.texInfo, &minfo)) {
                    ShipAssignNormalizedTexUVs(p, minfo.texWord, texSize, u, v);
                } else {
                    tex = nullptr;
                }
            } else {
                tex = nullptr;
            }
        }

        for (int vi = 0; vi < f.vertexCount; vi++) grow(f.verts[vi]);

        auto& dst = tex ? findBatch(tex, transparent).tris
                        : shuttleGlass ? glassTris : flatTris;
        const DWORD drawCol = tex ? D3DCOLOR_XRGB(255, 255, 255) : col;

        // Soft backFacing glass: reverse winding so the pane faces the orbit cam.
        if (shuttleGlass && p.backFacing) {
            emitTri(dst, f.verts[0], f.verts[2], f.verts[1], drawCol,
                    u[0], v[0], u[2], v[2], u[1], v[1]);
            if (f.vertexCount >= 4)
                emitTri(dst, f.verts[0], f.verts[3], f.verts[2], drawCol,
                        u[0], v[0], u[3], v[3], u[2], v[2]);
        } else {
            emitTri(dst, f.verts[0], f.verts[1], f.verts[2], drawCol,
                    u[0], v[0], u[1], v[1], u[2], v[2]);
            if (f.vertexCount >= 4)
                emitTri(dst, f.verts[0], f.verts[2], f.verts[3], drawCol,
                        u[0], v[0], u[2], v[2], u[3], v[3]);
        }
    }

    // ContourHull tris (tessellated Complex721) — material color, depth-biased.
    for (int i = 0; i < contourCount; i++) {
        const WorldFace& f = contourFaces[i];
        if (!f.worldValid || f.vertexCount < 3) continue;
        if (f.sourceIndex < 0 || f.sourceIndex >= visibleCount) continue;
        const TransformedPoly& p = polys[f.sourceIndex];
        DWORD col = ContourFaceColor(p, model, shipRotLit);
        for (int vi = 0; vi < f.vertexCount; vi++) grow(f.verts[vi]);
        emitTri(contourTris, f.verts[0], f.verts[1], f.verts[2], col,
                0, 0, 0, 0, 0, 0);
    }

    if (flatTris.empty() && glassTris.empty() && texBatches.empty() &&
        contourTris.empty() && contourGroups == 0) {
        DrawDemoOrbitHud(dev, shipIdx, (int)(shipDist / 256), visibleCount,
                         fitUnits, 0, contourGroups);
        return;
    }

    if (!s_legacyDistInited) {
        const float ext = 0.5f * std::max(bmax[0] - bmin[0],
                               std::max(bmax[1] - bmin[1], bmax[2] - bmin[2]));
        s_dist = std::max(0.75f, ext * 3.5f);
        s_legacyDistInited = true;
        const float zInit = (float)shipDist / (float)FP_ONE;
        ApplyCameraSoftMatched(dev, std::max(zInit, s_dist));
    }
    // Do not XY-center: soft Contour screenX/Y assume uncentered soft world.
    if (std::getenv("AB_D3D_CENTER_XY") &&
        std::getenv("AB_D3D_CENTER_XY")[0] == '1') {
        world._41 = -0.5f * (bmin[0] + bmax[0]);
        world._42 = -0.5f * (bmin[1] + bmax[1]);
    }
    (void)zd;
    // Re-apply after autofit may have changed s_dist / shipDist coupling.
    {
        const int32_t sd =
            (int32_t)std::llround((double)s_dist * (double)FP_ONE);
        const float zCam = (float)std::max(sd, FP_ONE / 8) / (float)FP_ONE;
        ApplyCameraSoftMatched(dev, zCam);
    }
    dev->SetTransform(D3DTS_WORLD, &world);

    // Screen Contours (XYZRHW). Default = Ogre SoftToScreen (no fit).
    // Dig: AB_CONTOUR_FIT=1 / AB_CONTOUR_FIT_DUMP=1 / JJFFE_CONTOUR_HULL=1.
    std::vector<ContourRhwVert> screenContours;
    if (!useContourHull) {
        D3DMATRIX view{}, proj{}, wv{}, wvp{};
        dev->GetTransform(D3DTS_VIEW, &view);
        dev->GetTransform(D3DTS_PROJECTION, &proj);
        MatMultiply(&wv, world, view);
        MatMultiply(&wvp, wv, proj);
        DumpSoftVsWvpDelta(polys, visibleCount, wvp);
        BuildScreenContourRhw(polys, visibleCount, model, shipRotLit, wvp,
                              screenContours);
    }

    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetIndices(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);

    auto drawList = [&](const std::vector<VN>& tris, IDirect3DTexture9* tex,
                        bool transparent) {
        if (tris.empty()) return;
        if (tex) {
            dev->SetTexture(0, tex);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            if (transparent) {
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
                dev->SetRenderState(D3DRS_ALPHAREF, 0x08);
                dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            }
        } else {
            dev->SetTexture(0, nullptr);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        }
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, tex ? D3DTA_TEXTURE : D3DTA_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(tris.size() / 3),
                             tris.data(), sizeof(VN));
        if (transparent) dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    };

    drawList(flatTris, nullptr, false);
    for (const auto& b : texBatches) drawList(b.tris, b.tex, b.transparent);

    auto f2dw = [](float f) {
        DWORD d;
        std::memcpy(&d, &f, sizeof(d));
        return d;
    };

    // Contour decals: clip-space depth bias (not geometric normal offset).
    // Keep Contour bias *weaker* than glass — ContourHull used to bury
    // Viper MkII 0x2000 windshields and Krait/Viper 0x20ee engine punches.
    if (!contourTris.empty()) {
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(-1.0f));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(-1.0e-4f));
        drawList(contourTris, nullptr, false);
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(0.0f));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(0.0f));
    }

    // Soft-analogue screen Contours. Soft paints Contours in the 2D span
    // buffer with no Z fight against hull freckles — keep Z off so the whole
    // Contour set stays visible (Z-test was dropping most curves by angle).
    if (!screenContours.empty()) {
        dev->SetTexture(0, nullptr);
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
                             (UINT)(screenContours.size() / 3),
                             screenContours.data(), sizeof(ContourRhwVert));
        dev->SetRenderState(D3DRS_ZENABLE, TRUE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        // Restore FVF for subsequent glass / HUD paths that expect lit verts.
        dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    }

    // Windshields / engine punches last. Stronger bias than ContourHull; no Z
    // write so coplanar 0x1000 rear plates (Krait) cannot bury 0x20ee marks
    // (matches Ogre kPunchMat / kEngineCapMat depthWrite=false).
    if (!glassTris.empty()) {
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(-1.0f));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(-4.0e-4f));
        drawList(glassTris, nullptr, false);
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(0.0f));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(0.0f));
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    dev->SetTexture(0, nullptr);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);

    size_t totalTris = flatTris.size() / 3 + glassTris.size() / 3 +
                       contourTris.size() / 3 + screenContours.size() / 3;
    for (const auto& b : texBatches) totalTris += b.tris.size() / 3;
    DrawDemoOrbitHud(dev, shipIdx, (int)(shipDist / 256), visibleCount, fitUnits,
                     totalTris, contourGroups);
}

MeshGpu s_ship{};
MeshGpu s_sphereFallback{};
bool s_shipLoaded = false;
bool s_haveSphere = false;
int s_sphereModelId = -1;

// Export / --use-export GPU pack (shared D3dShipAssetGpu API).
D3dShipAssetGpu s_shipAsset{};
bool s_shipAssetLoaded = false;
// s_assetLabel declared near top (HUD).

IDirect3DTexture9* s_softTex = nullptr;

std::unordered_map<int, MeshGpu> s_introMeshCache;
std::unordered_map<int, int> s_standInLogged;

// Known-good FFED3D model.x packs used when cast id has no asset.
constexpr int kStandInPool[] = {31, 30, 34, 45, 3, 6, 40, 38, 43, 441, 455};

constexpr int32_t kYawStep = 0x80;
constexpr int32_t kPitchStep = 0x60;
constexpr float kZoomStep = 0.15f;
constexpr int32_t kSpinStep = 0x40;
constexpr int32_t kMouseYawPerPix = 0x80;
constexpr int32_t kMousePitchPerPix = 0x60;

float FpAngleToRad(int32_t fp)
{
    return (float)((double)fp * (2.0 * 3.141592653589793 / 65536.0));
}

void ApplyCamera(IDirect3DDevice9* dev, float eyeDist)
{
    if (eyeDist < 0.2f) eyeDist = 0.2f;
    D3DMATRIX view, proj;
    float lx = 0.f, ly = 0.f, lz = 0.f;
    if (s_solMode) {
        lx = s_solLookX;
        ly = s_solLookY;
        lz = s_solLookZ;
    }
    MatLookAtLH(&view, lx, ly + 0.35f * eyeDist, lz - eyeDist, lx, ly, lz, 0, 1, 0);
    // Ship viewer uses a tight near clip. --sol zooms in on spheres that fill
    // the view — 8% near slice looks like a circular "shadow".
    const float nearZ = s_solMode ? std::max(0.05f, eyeDist * 0.02f)
                                  : std::max(0.02f, eyeDist * 0.08f);
    float farZ = s_solMode ? std::max(eyeDist * 80.0f, 400.0f)
                           : std::max(nearZ + 1.0f, eyeDist * 20.0f);
    if (s_solMode) {
        // Sol stays at world origin; outer moons (Mimas+) sit hundreds–thousands
        // of units away. A fixed far=400 clipped the star when framing them.
        const float focusR = std::sqrt(lx * lx + ly * ly + lz * lz);
        const float sunMargin = 160.f; // photosphere + corona extent
        farZ = std::max(farZ, focusR + eyeDist + sunMargin);
        farZ = std::max(farZ, nearZ + 1.0f);
    }
    MatPerspectiveFovLH(&proj, 50.0f * 3.14159265f / 180.0f,
                        (float)s_width / (float)s_height, nearZ, farZ);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
}

// Soft Contour overlay uses ASM ProjectPoint. Hull must share that film:
// focal 256, principal point (160,79) — not a symmetric MatPerspectiveFovLH.
// Dig: AB_D3D_OLD_FOV=1 → previous FOV-from-atan + Contour Ybias=+21 path.
void ApplyCameraSoftMatched(IDirect3DDevice9* dev, float shipDistWorld)
{
    if (shipDistWorld < 0.2f) shipDistWorld = 0.2f;
    D3DMATRIX view, proj;
    // Soft/Ogre: eye at origin looking +Z; ship stays at world +Z.
    MatLookAtLH(&view, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    const float nearZ = std::max(0.02f, shipDistWorld * 0.08f);
    const float farZ = std::max(nearZ + 1.0f, shipDistWorld * 20.0f);
    const bool oldFov =
        (std::getenv("AB_D3D_OLD_FOV") &&
         std::getenv("AB_D3D_OLD_FOV")[0] == '1') ||
        (std::getenv("AB_D3D_FOV50") &&
         std::getenv("AB_D3D_FOV50")[0] == '1');
    if (oldFov) {
        // Prior SoftMatched path: focal-derived symmetric FOV (not tilted
        // ApplyCamera 50° LookAt). Contour SoftToScreen Ybias=+21 pairs with it.
        const float halfFovX = std::atan(160.0f / 256.0f);
        const float aspectLogical = 320.0f / 200.0f;
        const float fovY =
            2.0f * std::atan(std::tan(halfFovX) / aspectLogical);
        MatPerspectiveFovLH(&proj, fovY, (float)s_width / (float)s_height,
                            nearZ, farZ);
    } else {
        MatPerspectiveAsmExact(&proj, nearZ, farZ);
        // Dig sign of principal-point Y term (default = 1-2*cy/H = +0.21).
        if (const char* e = std::getenv("AB_D3D_ASM_YOFF")) {
            if (e[0]) proj._32 = (float)std::atof(e);
        }
        static bool s_loggedAsm = false;
        if (!s_loggedAsm) {
            s_loggedAsm = true;
            std::fprintf(stderr,
                         "ApplyCameraSoftMatched: ASM-exact proj "
                         "f=256 pp=(160,79) _32=%.3f\n",
                         proj._32);
        }
    }
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
}

// Frame the fleet cluster (soft parent Z≈1.28; flybys out to ~±40).
[[maybe_unused]] void ApplyTitleCamera(IDirect3DDevice9* dev)
{
    D3DMATRIX view, proj;
    MatLookAtLH(&view, 0, 3.0f, -28.0f, 0, 0, 12.0f, 0, 1, 0);
    MatPerspectiveFovLH(&proj, 40.0f * 3.14159265f / 180.0f,
                        (float)s_width / (float)s_height, 0.5f, 250.0f);
    dev->SetTransform(D3DTS_VIEW, &view);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
}

// Soft attach can emit wild verts (thousands of units) — drop those for HW.
[[maybe_unused]] bool InstanceInFleetVolume(const IntroCastHwInstance& inst)
{
    const float x = (float)inst.pos[0] / (float)FP_ONE;
    const float y = (float)inst.pos[1] / (float)FP_ONE;
    const float z = (float)inst.pos[2] / (float)FP_ONE;
    constexpr float kMax = 70.0f;
    return std::fabs(x) <= kMax && std::fabs(y) <= kMax && std::fabs(z) <= kMax &&
           z > -5.0f;
}

void ApplyShipWorld(IDirect3DDevice9* dev, float scale)
{
    D3DMATRIX rx, ry, sc, world, tmp;
    MatRotationY(&ry, FpAngleToRad(s_yaw));
    MatRotationX(&rx, FpAngleToRad(s_pitch));
    MatScaling(&sc, scale, scale, scale);
    MatMultiply(&tmp, sc, rx);
    MatMultiply(&world, tmp, ry);
    dev->SetTransform(D3DTS_WORLD, &world);
}

D3DMATRIX MakeShipWorld(float scale)
{
    D3DMATRIX rx, ry, sc, world, tmp;
    MatRotationY(&ry, FpAngleToRad(s_yaw));
    MatRotationX(&rx, FpAngleToRad(s_pitch));
    MatScaling(&sc, scale, scale, scale);
    MatMultiply(&tmp, sc, rx);
    MatMultiply(&world, tmp, ry);
    return world;
}

bool EnsurePlanetSphere(IDirect3DDevice9* dev, int modelId)
{
    if (s_haveSphere && s_sphereModelId == modelId) return true;
    D3dDestroyMesh(&s_sphereFallback);
    s_haveSphere = false;
    s_sphereModelId = -1;
    if (!D3dCreatePlanetMesh(dev, modelId, &s_sphereFallback)) return false;
    s_haveSphere = true;
    s_sphereModelId = modelId;
    return true;
}

MeshGpu* EnsureSolBodyMesh(IDirect3DDevice9* dev, const BodyPaintInstance& inst)
{
    const uint32_t atmoKey = inst.overlayAtmo ? (uint32_t)inst.atmoTex : 0u;
    const uint64_t key = (uint64_t)(uint32_t)inst.modelId | ((uint64_t)atmoKey << 32);
    auto it = s_solMeshCache.find(key);
    if (it != s_solMeshCache.end()) return &it->second;
    MeshGpu mesh{};
    if (!D3dCreatePlanetMesh(dev, inst.modelId, &mesh, inst.name)) {
        std::fprintf(stderr, "D3D: Sol instance mesh failed model %d (%s)\n",
                     inst.modelId, inst.name);
        return nullptr;
    }
    auto ins = s_solMeshCache.emplace(key, mesh);
    return &ins.first->second;
}

void SolInstanceWorldPos(const BodyPaintInstance& inst, float* x, float* y, float* z)
{
    const D3DMATRIX rot = MakeShipWorld(1.f);
    const float wx = inst.x * rot._11 + inst.y * rot._21 + inst.z * rot._31;
    const float wy = inst.x * rot._12 + inst.y * rot._22 + inst.z * rot._32;
    const float wz = inst.x * rot._13 + inst.y * rot._23 + inst.z * rot._33;
    if (x) *x = wx;
    if (y) *y = wy;
    if (z) *z = wz;
}

void ApplySolInstanceWorld(IDirect3DDevice9* dev, const BodyPaintInstance& inst)
{
    D3DMATRIX world = MakeShipWorld(inst.scale);
    SolInstanceWorldPos(inst, &world._41, &world._42, &world._43);
    dev->SetTransform(D3DTS_WORLD, &world);
}

void EnsureSolFocusList()
{
    if (s_solNavN > 0) return;
    if (WorldBodyCount() <= 0 && !WorldLoadSol()) return;
    s_solNavN = BodyPaintCollectSphereInstances(nullptr, s_solNav, 64);
    if (s_solFocus < 0 || s_solFocus >= s_solNavN) s_solFocus = 0;
}

float SolFocusEyeDist(const BodyPaintInstance& inst)
{
    float d = inst.scale * 6.f;
    if (d < 2.f) d = 2.f;
    if (d > 400.f) d = 400.f;
    return d;
}

void UpdateSolLookAt()
{
    s_solLookX = s_solLookY = s_solLookZ = 0.f;
    if (s_solFocus < 0 || s_solFocus >= s_solNavN) return;
    SolInstanceWorldPos(s_solNav[s_solFocus], &s_solLookX, &s_solLookY, &s_solLookZ);
}

size_t DrawSolSystem(IDirect3DDevice9* dev)
{
    if (!dev) return 0;
    if (WorldBodyCount() <= 0 && !WorldLoadSol()) {
        std::fprintf(stderr, "D3D: WorldLoadSol failed (--sol)\n");
        return 0;
    }

    static TexturesCfg s_cfg{};
    static bool s_cfgTried = false;
    static bool s_cfgOk = false;
    if (!s_cfgTried) {
        s_cfgTried = true;
        char cfgPath[1024] = {};
        if (BodyPaintResolveCfgFile(cfgPath, (int)sizeof(cfgPath), D3dGetDataRoot()))
            s_cfgOk = TexturesCfgLoad(&s_cfg, cfgPath);
        if (!s_cfgOk)
            s_cfgOk = TexturesCfgLoad(&s_cfg, TexturesCfgDefaultPath());
        printf("D3D: Sol system %d bodies (Earth R=%.2f, 1 AU=%.0f compressed)\n",
               WorldBodyCount(), kSolRadScale, kSolDistScale);
    }

    BodyPaintInstance insts[64];
    const int n = BodyPaintCollectSphereInstances(s_cfgOk ? &s_cfg : nullptr, insts, 64);
    D3DMATERIAL9 pmat{};
    pmat.Diffuse = {1, 1, 1, 1};
    pmat.Ambient = {0.22f, 0.22f, 0.26f, 1};
    pmat.Emissive = {0, 0, 0, 1};
    pmat.Power = 0;
    dev->SetMaterial(&pmat);
    dev->SetRenderState(D3DRS_AMBIENT, 0x00202830);

    D3DLIGHT9 sun{};
    sun.Type = D3DLIGHT_POINT;
    sun.Diffuse = {1.0f, 0.96f, 0.88f, 1.0f};
    sun.Ambient = {0.04f, 0.04f, 0.05f, 1.0f};
    sun.Position = {0.f, 0.f, 0.f}; // Sol at origin
    sun.Range = kSolDistScale * 50.f;
    sun.Attenuation0 = 1.0f;
    sun.Attenuation1 = 0.0f;
    sun.Attenuation2 = 0.0f;
    DWORD oldLight0 = FALSE;
    dev->GetLightEnable(0, (BOOL*)&oldLight0);
    dev->SetLight(0, &sun);
    dev->LightEnable(0, TRUE);

    MeshGpu* meshes[64] = {};
    for (int i = 0; i < n; ++i)
        meshes[i] = EnsureSolBodyMesh(dev, insts[i]);

    // S6 order for the star: corona then photosphere. Drawing the 3× crown
    // last left its baked inner punch as a black disc in Sol, and additive
    // over neighbors looked like a cookie-cutter. Planets drawn after eclipse
    // the halo with their own silhouette.
    for (int i = 0; i < n; ++i) {
        if (!meshes[i] || insts[i].mode != BodyPaintMode::Emissive) continue;
        ApplySolInstanceWorld(dev, insts[i]);
        D3dDrawCoronaBillboard(dev, *meshes[i]);
        D3dDrawMesh(dev, *meshes[i], false);
    }
    for (int i = 0; i < n; ++i) {
        if (!meshes[i] || insts[i].mode == BodyPaintMode::Emissive) continue;
        ApplySolInstanceWorld(dev, insts[i]);
        D3dDrawMesh(dev, *meshes[i], true);
    }
    dev->LightEnable(0, oldLight0);
    for (int i = 0; i < n; ++i) {
        if (!meshes[i]) continue;
        ApplySolInstanceWorld(dev, insts[i]);
        D3dDrawAtmoShell(dev, *meshes[i]);
    }
    return (size_t)n;
}

bool LoadShip(int shipIdx)
{
    IDirect3DDevice9* dev = D3dGetDevice();
    if (!dev) return false;
    D3dDestroyMesh(&s_ship);
    D3dDestroyShipAsset(&s_shipAsset);
    s_shipLoaded = false;
    s_shipAssetLoaded = false;
    s_assetLabel[0] = 0;
    s_lastShipIdx = shipIdx;

    if (D3dIsPlanetModel(shipIdx) || (s_planetOverride >= 0 && shipIdx == s_planetOverride)) {
        s_shipLoaded = EnsurePlanetSphere(dev, shipIdx);
        return s_shipLoaded;
    }

    // Phase B B2: prefer export/ when --use-export / JJFFE_ASSET_SHIPS.
    if (DemoShipUsesExportAsset(shipIdx) && !IsDemoExportAsset()) {
        const char* root = GetDemoExportAssetRoot();
        if (D3dLoadShipAsset(dev, shipIdx, root, /*auditTints=*/false,
                             &s_shipAsset)) {
            s_shipAssetLoaded = true;
            std::snprintf(s_assetLabel, sizeof(s_assetLabel), "%s",
                          s_shipAsset.label);
            return true;
        }
        std::fprintf(stderr,
                     "D3D: export asset load failed for #%d; falling back to "
                     ".x\n",
                     shipIdx);
    }

    if (D3dLoadModelId(dev, shipIdx, &s_ship)) {
        s_shipLoaded = true;
        return true;
    }
    return false;
}

void HandleDemoInput()
{
    uint8_t keys[256];
    InputKeybReadStates(keys);
    auto down = [&](int dos) { return keys[dos] == 0x00; };

    if (down(0x1E)) s_yaw -= kYawStep;
    if (down(0x20)) s_yaw += kYawStep;
    if (down(0x10)) s_pitch -= kPitchStep;
    if (down(0x12)) s_pitch += kPitchStep;
    const float zoomStep = s_solMode ? std::max(0.15f, s_dist * 0.04f) : kZoomStep;
    if (down(0x11)) s_dist -= zoomStep;
    if (down(0x1F)) s_dist += zoomStep;

    const bool pDown = down(0x19);
    if (pDown && !s_keyPWasDown) s_paused = !s_paused;
    s_keyPWasDown = pDown;
    if (!s_paused) s_yaw += kSpinStep;

    if (s_solMode) {
        EnsureSolFocusList();
        // SDL2 arrow keysyms are >512 and miss the DOS scancode table.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        const bool left = ks && ks[SDL_SCANCODE_LEFT];
        const bool right = ks && ks[SDL_SCANCODE_RIGHT];
        if (s_solNavN > 0) {
            if (left && !s_solLeftWasDown)
                s_solFocus = (s_solFocus + s_solNavN - 1) % s_solNavN;
            if (right && !s_solRightWasDown)
                s_solFocus = (s_solFocus + 1) % s_solNavN;
        }
        s_solLeftWasDown = left;
        s_solRightWasDown = right;
        if (s_solNavN > 0 && s_solFocus != s_solFocusFramed) {
            s_dist = SolFocusEyeDist(s_solNav[s_solFocus]);
            s_solFocusFramed = s_solFocus;
        }
        UpdateSolLookAt();
    }

    float minDist = 0.5f;
    if (s_solMode && s_solFocus >= 0 && s_solFocus < s_solNavN)
        minDist = std::max(0.35f, s_solNav[s_solFocus].scale * 1.35f);
    if (s_dist < minDist) s_dist = minDist;
    if (s_dist > (s_solMode ? 8000.0f : 40.0f))
        s_dist = s_solMode ? 8000.0f : 40.0f;

    int mx = 0, my = 0;
    InputMouseReadMickeys(&mx, &my);
    if (InputMouseReadButtons() & 1) {
        s_yaw += mx * kMouseYawPerPix / 4;
        s_pitch += my * kMousePitchPerPix / 4;
    }
}

bool EnsureSoftTexture(IDirect3DDevice9* dev)
{
    if (s_softTex) return true;
    HRESULT hr = dev->CreateTexture(SCREEN_WIDTH, SCREEN_HEIGHT, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &s_softTex, nullptr);
    if (FAILED(hr)) {
        fprintf(stderr, "D3D: CreateTexture soft overlay failed hr=0x%08lx\n",
                (unsigned long)hr);
        s_softTex = nullptr;
        return false;
    }
    return true;
}

// Soft UI overlay: 0xFF transparent → D3D scene; opaque logo/credits/stars.
void BlitSoftUiAlpha(IDirect3DDevice9* dev)
{
    if (!g_videoState || !EnsureSoftTexture(dev)) return;
    const uint8_t* buf = g_videoState->backBuffer;
    const uint8_t* pal = g_videoState->palette;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_softTex->LockRect(0, &lr, nullptr, 0))) return;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        auto* dst = reinterpret_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
        const uint8_t* src = buf + y * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const uint8_t idx = src[x];
            uint8_t r = 0, g = 0, b = 0, a = 0;
            if (idx != 0xFF) {
                a = 255;
                if (idx != 0 && pal) {
                    r = pal[idx * 3 + 0];
                    g = pal[idx * 3 + 1];
                    b = pal[idx * 3 + 2];
                }
                // Demo HUD glyphs must stay readable even if VGA 0x0E/0x0F are dim.
                if ((int)r + (int)g + (int)b < 48) {
                    r = g = b = 240;
                }
            }
            dst[x * 4 + 0] = b;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = r;
            dst[x * 4 + 3] = a;
        }
    }
    s_softTex->UnlockRect(0);

    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);

    dev->SetTexture(0, s_softTex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    struct V {
        float x, y, z, rhw;
        float u, v;
    };
    const float W = (float)s_width;
    const float H = (float)s_height;
    V tri[6] = {
        {-0.5f, -0.5f, 0, 1, 0, 0},
        {W - 0.5f, -0.5f, 0, 1, 1, 0},
        {W - 0.5f, H - 0.5f, 0, 1, 1, 1},
        {-0.5f, -0.5f, 0, 1, 0, 0},
        {W - 0.5f, H - 0.5f, 0, 1, 1, 1},
        {-0.5f, H - 0.5f, 0, 1, 0, 1},
    };
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, tri, sizeof(V));

    dev->SetTexture(0, nullptr);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
}

// Full soft cast (legacy ships + UI): bake 0xFF → navy, draw opaque.
void BlitSoftLegacyCast(IDirect3DDevice9* dev)
{
    if (!g_videoState || !EnsureSoftTexture(dev)) return;
    const uint8_t* buf = g_videoState->backBuffer;
    const uint8_t* pal = g_videoState->palette;
    constexpr uint8_t kNavyR = 16, kNavyG = 11, kNavyB = 67;

    D3DLOCKED_RECT lr{};
    if (FAILED(s_softTex->LockRect(0, &lr, nullptr, 0))) return;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        auto* dst = reinterpret_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
        const uint8_t* src = buf + y * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const uint8_t idx = src[x];
            uint8_t r = kNavyR, g = kNavyG, b = kNavyB;
            if (idx != 0xFF) {
                r = g = b = 0;
                if (idx != 0 && pal) {
                    r = pal[idx * 3 + 0];
                    g = pal[idx * 3 + 1];
                    b = pal[idx * 3 + 2];
                }
            }
            dst[x * 4 + 0] = b;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = r;
            dst[x * 4 + 3] = 255;
        }
    }
    s_softTex->UnlockRect(0);

    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
    dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);

    dev->SetTexture(0, s_softTex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    struct V {
        float x, y, z, rhw;
        float u, v;
    };
    const float W = (float)s_width;
    const float H = (float)s_height;
    V tri[6] = {
        {-0.5f, -0.5f, 0, 1, 0, 0},
        {W - 0.5f, -0.5f, 0, 1, 1, 0},
        {W - 0.5f, H - 0.5f, 0, 1, 1, 1},
        {-0.5f, -0.5f, 0, 1, 0, 0},
        {W - 0.5f, H - 0.5f, 0, 1, 1, 1},
        {-0.5f, H - 0.5f, 0, 1, 0, 1},
    };
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, tri, sizeof(V));

    dev->SetTexture(0, nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
}

int ResolveStandIn(int castId)
{
    char path[512];
    if (D3dFindModelPath(castId, path, sizeof(path)))
        return castId;

    constexpr int n = (int)(sizeof(kStandInPool) / sizeof(kStandInPool[0]));
    const int pick = kStandInPool[(castId % n + n) % n];
    if (s_standInLogged.find(castId) == s_standInLogged.end()) {
        s_standInLogged[castId] = pick;
        fprintf(stderr, "D3D intro: cast #%d → stand-in .x #%d\n", castId, pick);
    }
    return pick;
}

[[maybe_unused]] MeshGpu* GetOrLoadIntroMesh(IDirect3DDevice9* dev, int castId)
{
    const int loadId = ResolveStandIn(castId);
    auto it = s_introMeshCache.find(loadId);
    if (it != s_introMeshCache.end() && it->second.vb)
        return &it->second;

    MeshGpu mesh{};
    if (!D3dLoadModelId(dev, loadId, &mesh)) {
        s_introMeshCache[loadId] = mesh; // empty marker
        return nullptr;
    }
    s_introMeshCache[loadId] = mesh;
    return &s_introMeshCache[loadId];
}

// Sin32 / FixedMulHigh basis → float orthonormal (≈ / 2^30).
constexpr float kSin32One = 1073741824.0f; // 0x40000000

[[maybe_unused]] void ApplyInstanceWorld(IDirect3DDevice9* dev, const IntroCastHwInstance& inst,
                        float meshRadius)
{
    D3DMATRIX world{};
    float basis[9];
    for (int i = 0; i < 9; i++)
        basis[i] = (float)inst.rot[i] / kSin32One;

    float rad = meshRadius;
    if (rad < 0.5f) rad = 0.5f;
    if (rad > 12.0f) rad = 12.0f;
    const float scale = 2.0f / rad;
    (void)inst.scaleRelated;

    // Soft world is compact (~parent Z=1.28). Spread XY a bit so stand-ins
    // at near-COG attaches still separate; keep Z from cast.
    const float px = (float)inst.pos[0] / (float)FP_ONE;
    const float py = (float)inst.pos[1] / (float)FP_ONE;
    const float pz = (float)inst.pos[2] / (float)FP_ONE;

    // Soft +Z forward; .x already LH-converted — keep +Z.
    world._11 = basis[0] * scale;
    world._12 = basis[1] * scale;
    world._13 = basis[2] * scale;
    world._14 = 0;
    world._21 = basis[3] * scale;
    world._22 = basis[4] * scale;
    world._23 = basis[5] * scale;
    world._24 = 0;
    world._31 = basis[6] * scale;
    world._32 = basis[7] * scale;
    world._33 = basis[8] * scale;
    world._34 = 0;
    world._41 = px;
    world._42 = py;
    world._43 = pz;
    world._44 = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &world);
}

} // namespace

void D3dSetPlanetOverride(int modelId) { s_planetOverride = modelId; }
int D3dGetPlanetOverride() { return s_planetOverride; }
void D3dSetSolSystem(bool on) { s_solMode = on; }
bool D3dSolSystem() { return s_solMode; }

bool D3dDemoInit(int width, int height)
{
    s_width = width;
    s_height = height;
    s_ready = true;
    return true;
}

void D3dDemoShutdown()
{
    D3dDestroyMesh(&s_ship);
    D3dDestroyMesh(&s_sphereFallback);
    D3dDestroyShipAsset(&s_shipAsset);
    s_shipLoaded = false;
    s_shipAssetLoaded = false;
    s_assetLabel[0] = 0;
    s_haveSphere = false;
    s_sphereModelId = -1;
    for (auto& kv : s_introMeshCache)
        D3dDestroyMesh(&kv.second);
    s_introMeshCache.clear();
    for (auto& kv : s_solMeshCache)
        D3dDestroyMesh(&kv.second);
    s_solMeshCache.clear();
    s_solNavN = 0;
    s_solFocus = 0;
    s_solFocusFramed = -1;
    s_solLookX = s_solLookY = s_solLookZ = 0.f;
    s_standInLogged.clear();
    if (s_softTex) {
        s_softTex->Release();
        s_softTex = nullptr;
    }
    ClearLegacyTexCache();
    s_ready = false;
}

void ClearExportAssetGpu()
{
    D3dDestroyShipAsset(&s_shipAsset);
    s_shipAssetLoaded = false;
    s_assetLabel[0] = 0;
}

bool LoadExportAsset(IDirect3DDevice9* dev, int shipIdx)
{
    ClearExportAssetGpu();
    if (!dev || shipIdx < 0) return false;
    const char* root = GetDemoExportAssetRoot();
    if (!D3dLoadShipAsset(dev, shipIdx, root, /*auditTints=*/true, &s_shipAsset))
        return false;
    s_shipAssetLoaded = true;
    std::snprintf(s_assetLabel, sizeof(s_assetLabel), "%s", s_shipAsset.label);
    return true;
}

void DrawExportAsset(IDirect3DDevice9* dev, int shipIdx)
{
    if (!dev) return;
    if (!s_shipAssetLoaded || s_shipAsset.shipIndex != shipIdx) {
        if (!LoadExportAsset(dev, shipIdx)) {
            std::fprintf(stderr,
                         "D3D export-asset: nothing to draw for ship %d\n",
                         shipIdx);
            return;
        }
    }

    ApplyCamera(dev, s_dist);
    const float orbitScale = 1.6f / s_shipAsset.radius;
    const D3DMATRIX shipWorld = MakeShipWorld(orbitScale);
    D3dDrawShipAsset(dev, s_shipAsset, shipWorld);
    DrawDemoOrbitHud(dev, shipIdx, (int)(s_dist + 0.5f), 0, 0,
                     s_shipAsset.triCount, 0);
}

void D3dDrawDemo()
{
    IDirect3DDevice9* dev = D3dGetDevice();
    if (!dev || !s_ready) return;

    HandleDemoInput();

    int shipIdx = GetDemoShipIndex();
    if (s_planetOverride >= 0) shipIdx = s_planetOverride;

    // --legacy-ship: FFED3D DrawReal* role — internal mesh → D3D prims.
    if (IsDemoLegacySoft()) {
        constexpr float kNavyR = 16.0f / 255.0f;
        constexpr float kNavyG = 11.0f / 255.0f;
        constexpr float kNavyB = 67.0f / 255.0f;
        D3dBeginFrame(kNavyR, kNavyG, kNavyB);
        DrawLegacyMeshD3d(dev, shipIdx);
        s_lastShipIdx = shipIdx;
        D3dEndFrame();
        return;
    }

    // --export-ship: OBJ/asset groups with hull→punch→overlay painter.
    if (IsDemoExportAsset()) {
        constexpr float kNavyR = 16.0f / 255.0f;
        constexpr float kNavyG = 11.0f / 255.0f;
        constexpr float kNavyB = 67.0f / 255.0f;
        D3dBeginFrame(kNavyR, kNavyG, kNavyB);
        if (shipIdx != s_lastShipIdx) {
            // Force reload on ship change (ESC ship select).
            ClearExportAssetGpu();
            s_lastShipIdx = shipIdx;
        }
        DrawExportAsset(dev, shipIdx);
        D3dEndFrame();
        return;
    }

    if (shipIdx != s_lastShipIdx && !s_solMode) LoadShip(shipIdx);

    D3dBeginFrame(0.02f, 0.04f, 0.10f);
    ApplyCamera(dev, s_dist);

    D3DMATERIAL9 mat{};
    mat.Diffuse = {1, 1, 1, 1};
    mat.Ambient = {0.4f, 0.4f, 0.45f, 1};
    dev->SetMaterial(&mat);

    size_t hudTris = 0;
    if (s_solMode) {
        hudTris = DrawSolSystem(dev);
    } else {
    const bool planet = D3dIsPlanetModel(shipIdx) ||
                       (s_planetOverride >= 0 && shipIdx == s_planetOverride);
    if (planet) {
        if (EnsurePlanetSphere(dev, shipIdx)) {
            D3DMATERIAL9 pmat{};
            pmat.Diffuse = {1, 1, 1, 1};
            pmat.Ambient = {0.55f, 0.55f, 0.55f, 1};
            dev->SetMaterial(&pmat);
            ApplyShipWorld(dev, 1.8f);
            D3dDrawCoronaBillboard(dev, s_sphereFallback);
            D3dDrawMesh(dev, s_sphereFallback);
            D3dDrawAtmoShell(dev, s_sphereFallback);
            hudTris = (size_t)(s_sphereFallback.indexCount / 3);
        }
    } else if (s_shipAssetLoaded) {
        const D3DMATRIX shipWorld = MakeShipWorld(1.6f / s_shipAsset.radius);
        D3dDrawShipAsset(dev, s_shipAsset, shipWorld);
        hudTris = s_shipAsset.triCount;
    } else if (s_shipLoaded) {
        ApplyShipWorld(dev, 1.6f / s_ship.radius);
        D3dDrawMesh(dev, s_ship);
        hudTris = (size_t)(s_ship.indexCount > 0 ? s_ship.indexCount / 3 : 0);
    }
    }

    DrawDemoOrbitHud(dev, shipIdx, (int)(s_dist + 0.5f), 0, 0, hudTris, 0);

    D3dEndFrame();
}

void D3dDrawIntroCast()
{
    IDirect3DDevice9* dev = D3dGetDevice();
    if (!dev || !s_ready) return;

    // Phase 1: legacy soft cast only (internal mesh models + credits).
    // .x hybrid comes later — same policy as original FFED3D once stable.
    constexpr float kNavyR = 16.0f / 255.0f;
    constexpr float kNavyG = 11.0f / 255.0f;
    constexpr float kNavyB = 67.0f / 255.0f;
    D3dBeginFrame(kNavyR, kNavyG, kNavyB);
    IntroCastSetShipAnim(true);
    IntroCastDraw();
    BlitSoftLegacyCast(dev);
    D3dEndFrame();
}

void D3dDrawShipSelect()
{
    IDirect3DDevice9* dev = D3dGetDevice();
    if (!dev || !s_ready) return;
    D3dBeginFrame(0.0f, 0.0f, 0.05f);
    DrawDemoShipSelect();
    BlitSoftLegacyCast(dev);
    D3dEndFrame();
}

void D3dDrawMenu(uint32_t frameCount, uint32_t currentFps)
{
    IDirect3DDevice9* dev = D3dGetDevice();
    if (!dev || !s_ready) return;
    D3dBeginFrame(0.02f, 0.04f, 0.10f);
    DrawMenuScreen(frameCount, currentFps);
    BlitSoftLegacyCast(dev);
    D3dEndFrame();
}

} // namespace d3d
} // namespace jjffe
