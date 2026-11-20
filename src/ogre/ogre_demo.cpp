#include "jjffe/ogre/ogre_demo.h"
#include "jjffe/ogre/ogre_texture.h"
#include "jjffe/ogre/ogre_ui.h"
#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ogre/ogre_soft_blit.h"
#include "jjffe/ogre/ogre_ship_hw.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/demo3d.h"
#include "jjffe/input.h"
#include "jjffe/platform.h"
#include "jjffe/math.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/video.h"
#include <SDL.h>
#include <Ogre.h>
#include <OgreManualObject2.h>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace jjffe {
namespace ogre {

namespace {

bool s_ready = false;
int s_width = 640;
int s_height = 400;

Camera3D s_camera{};
int32_t s_yaw = 0;
int32_t s_pitch = 0;
int32_t s_dist = FP_ONE / 2;
int s_fitDistUnits = 1;
int s_lastShipIdx = -1;
bool s_paused = true;
bool s_keyPWasDown = false;
bool s_keyHWasDown = false;
bool s_keyNWasDown = false;
bool s_keyTWasDown = false;
bool s_initialized = false;

} // namespace

static ShipRenderMode s_shipMode = ShipRenderMode::Hardware;

namespace {

constexpr int32_t kYawStep = 0x80;
constexpr int32_t kPitchStep = 0x60;
constexpr int32_t kZoomStep = FP_ONE / 32;
constexpr int32_t kSpinStep = 0x40;
constexpr int32_t kMouseYawPerPix = 0x80;
constexpr int32_t kMousePitchPerPix = 0x60;
constexpr Uint32 kTargetFrameMs = 33;

struct ColoredVert {
    float x, y, z;
    uint32_t color;
};
struct ScreenVert {
    float x, y, z, rhw;
    uint32_t color;
};
struct TexVert {
    float x, y, z, rhw;
    uint32_t color;
    float u, v;
};
int32_t DegToFp(int deg)
{
    return (int32_t)((deg * 65536LL) / 360);
}

uint32_t PalIdxToArgb(uint8_t idx)
{
    if (!g_videoState) return (uint32_t)(((255) << 24) | (((uint32_t)(180) & 0xFF) << 16) | (((uint32_t)(180) & 0xFF) << 8) | ((uint32_t)(200) & 0xFF));
    const uint8_t* pal = g_videoState->palette;
    // Palette bytes are already roughly 0..255 (6-bit DAC stored <<2).
    uint8_t r = pal[idx * 3 + 0];
    uint8_t g = pal[idx * 3 + 1];
    uint8_t b = pal[idx * 3 + 2];
    return (uint32_t)(((255) << 24) | (((uint32_t)(r) & 0xFF) << 16) | (((uint32_t)(g) & 0xFF) << 8) | ((uint32_t)(b) & 0xFF));
}

// Lift near-black results so Contour hulls read on a starfield.
// Never boost VGA0 — soft FlatRgb 0x20ee/0x2000 punches stay black (Falcon
// engine Contours became blue slabs when shade0→0 was silverized).
uint32_t BoostIfDim(uint32_t argb, uint8_t litRamp)
{
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >> 8) & 0xFF;
    uint8_t b = argb & 0xFF;
    if ((int)r + g + b == 0)
        return argb;
    if ((int)r + g + b >= 48)
        return argb;
    // Silver/blue hull ramp from soft lighting index (typically 2..15).
    uint8_t v = static_cast<uint8_t>(48 + litRamp * 10);
    if (v > 220) v = 220;
    return (uint32_t)(((255) << 24) | (((uint32_t)(v) & 0xFF) << 16) | (((uint32_t)(v) & 0xFF) << 8) | ((uint32_t)(static_cast<uint8_t>(v + 12 > 255 ? 255 : v + 12) & 0xFF)));
}

uint32_t PolyColor(const TransformedPoly& p, const ShipModel* model, const int32_t* shipRotFp16)
{
    uint16_t ti = p.texInfo;
    uint8_t lit = GetFaceColor(model, p.normalIdx, 0x08, shipRotFp16);
    if (lit < 2) lit = 2;

    // Soft FlatRgb punches → VGA0 (nozzle spacer, canopy). FUNC_001736 glint
    // Contours (specialParam 0x736*) keep Contour color (0x20ee → DPal/luma).
    if (ti == 0 || ti == 0x2000)
        return (uint32_t)(((255) << 24) | (((uint32_t)(0) & 0xFF) << 16) | (((uint32_t)(0) & 0xFF) << 8) | ((uint32_t)(0) & 0xFF));
    if (ti == 0x20ee && (p.specialParam & 0xFFF0) != 0x7360)
        return (uint32_t)(((255) << 24) | (((uint32_t)(0) & 0xFF) << 16) | (((uint32_t)(0) & 0xFF) << 8) | ((uint32_t)(0) & 0xFF));
    // 0x3000 = RGB0 + face-light flag (Saker Sprite748 → #187 nozzle bridge).
    if (ti == 0x3000) {
        int r = 0, g = 0, b = 0;
        r += (int)p.faceLight[0];
        g += (int)p.faceLight[1];
        b += (int)p.faceLight[2];
        if (r < 0) r = 0;
        if (r > 15) r = 15;
        if (g < 0) g = 0;
        if (g > 15) g = 15;
        if (b < 0) b = 0;
        if (b > 15) b = 15;
        return PalIdxToArgb(ShadeRgbNibbleToPal(r, g, b)) | 0xFF000000u;
    }

    // DirLine ribbons (Shuttle landing rails, Puma struts): soft uses material
    // shade0 + face light (never textured freckles).
    if (p.type == MeshPolyType::DirLine) {
        ShipMaterialInfo mat{};
        if (ShipResolveMaterial(ti, &mat)) {
            int8_t flR = 0, flG = 0, flB = 0;
            GetMeshFaceLight(&flR, &flG, &flB);
            const bool applyFL = (flR | flG | flB) != 0;
            uint8_t shadePal[256];
            ShipBuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
            return BoostIfDim(PalIdxToArgb(shadePal[0]), lit);
        }
    }

    const uint16_t kind = static_cast<uint16_t>(ti >> 14);
    uint8_t palIdx = 0;
    if (kind == 0) {
        // Flat RGB material (FUNC_001692 nibbles → DPal), matching soft FlatRgbToPal.
        int r = (ti >> 8) & 0xF;
        int g = (ti >> 4) & 0xF;
        int b = ti & 0xF;
        if ((ti & 0x1000) != 0) {
            int8_t flR = 0, flG = 0, flB = 0;
            GetMeshFaceLight(&flR, &flG, &flB);
            r += flR; g += flG; b += flB;
            if (r < 0) r = 0;
            if (r > 15) r = 15;
            if (g < 0) g = 0;
            if (g > 15) g = 15;
            if (b < 0) b = 0;
            if (b > 15) b = 15;
        }
        palIdx = ShadeRgbNibbleToPal(r, g, b);
    } else {
        // Textured / Contour material index — use lit VGA ramp for M1 visibility.
        palIdx = lit;
    }

    // afterSpecial attach flats that resolved to near-black stay black (no blue tint).
    if (p.afterSpecialPoly && palIdx == 0)
        return (uint32_t)(((255) << 24) | (((uint32_t)(0) & 0xFF) << 16) | (((uint32_t)(0) & 0xFF) << 8) | ((uint32_t)(0) & 0xFF));

    return BoostIfDim(PalIdxToArgb(palIdx), lit);
}

void RefreshFitForShip(const ShipModel* model)
{
    if (!model) {
        s_fitDistUnits = 1;
    } else {
        // Same framing as soft Demo3D / A/B (target 250px).
        s_fitDistUnits = AbAutoFitDistUnits(model->radius, model->scaleRelated);
    }
    s_dist = s_fitDistUnits * (FP_ONE / 2);
}

// Pin the orbit to an exact pose so `ffelnxogre` can be captured at the same
// yaw / pitch / camera Z as `ab_capture_shuttle` and `ffeasm --ship-demo`.
void ApplyScriptedPose()
{
    const char* yaw = getenv("JJFFE_OGRE_YAW");
    const char* pitch = getenv("JJFFE_OGRE_PITCH");
    const char* z = getenv("JJFFE_OGRE_Z");
    if (!yaw && !pitch && !z)
        return;
    if (yaw && yaw[0]) s_yaw = DegToFp(atoi(yaw));
    if (pitch && pitch[0]) s_pitch = DegToFp(atoi(pitch));
    if (z && z[0]) s_dist = (int32_t)strtol(z, nullptr, 0);
    s_paused = true;
}

// Ship-viewer LOD: always parse near enough for GateNear bodies. distUnits=1
// (0x8000) still skips Shuttle DirLine landing rails (need ≤~0x3800). Contour /
// FUNC_001751 far gates strip Puma/Asp detail when Z is large; locking near
// keeps high-poly Contour + ribbons while W/S only changes display scale.
[[maybe_unused]] int32_t LodParseViewZ()
{
    return 0x2000;
}

void UploadScreenTris(Ogre::ManualObject* mo, const std::vector<ScreenVert>& verts,
                      const char* matName = nullptr);
void UploadTexTris(Ogre::ManualObject* mo, const std::string& matName,
                   const std::vector<TexVert>& verts);

void DrawStars()
{
    std::vector<ScreenVert> pts;
    pts.reserve(96 * 6);
    for (int n = 0; n < 96; n++) {
        uint32_t h = 2654435761u * (uint32_t)(n + 1);
        float x = (float)((h >> 3) % (uint32_t)s_width);
        float y = (float)((h >> 13) % (uint32_t)s_height);
        float s = (h & 7u) == 0 ? 2.0f : 1.0f;
        // Brighter on navy clear — OG uses white / warm star pixels.
        uint32_t c = (h & 1u) ? 0xFFE8E8E8u : 0xFFA8A8C0u;
        if ((h & 7u) == 0)
            c = 0xFFFFF0A0u; // occasional warm “+” spark
        ScreenVert q[6] = {
            {x, y, 0.9f, 1.0f, c}, {x + s, y, 0.9f, 1.0f, c}, {x + s, y + s, 0.9f, 1.0f, c},
            {x, y, 0.9f, 1.0f, c}, {x + s, y + s, 0.9f, 1.0f, c}, {x, y + s, 0.9f, 1.0f, c},
        };
        pts.insert(pts.end(), q, q + 6);
    }
    // Depth off — stars must not write Z (D3D toggled ZENABLE around stars).
    UploadScreenTris(OgreStarsManual(), pts, OgreUnlitUiMaterial());
}

// Soft Contour pieces are bezier chords (FUNC_001548), not flat quads. Drawing
// them as world tris makes Puma "missing curves" / needles. Approximate Contour
// group fill in screen space (XYZRHW), matching soft's group+meshIndex order.
void SampleBezier(float x0, float y0, float x1, float y1, float x2, float y2,
                  float x3, float y3, int steps, std::vector<float>& xs, std::vector<float>& ys)
{
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float u = 1.0f - t;
        float uu = u * u, tt = t * t;
        float uuu = uu * u, ttt = tt * t;
        float x = uuu * x0 + 3.0f * uu * t * x1 + 3.0f * u * tt * x2 + ttt * x3;
        float y = uuu * y0 + 3.0f * uu * t * y1 + 3.0f * u * tt * y2 + ttt * y3;
        if (!xs.empty()) {
            float dx = x - xs.back(), dy = y - ys.back();
            if (dx * dx + dy * dy < 0.25f)
                continue;
        }
        xs.push_back(x);
        ys.push_back(y);
    }
}

// Emit a screen-space triangle with winding facing the camera (outward).
// Soft screen Y grows downward; positive cross ⇒ CW ⇒ toward viewer.
void EmitScreenTriFacingOut(std::vector<ScreenVert>& out,
                            float x0, float y0, float z0,
                            float x1, float y1, float z1,
                            float x2, float y2, float z2,
                            uint32_t col)
{
    const float cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (cross < 0.0f) {
        // Flip winding so the geometric normal points toward the camera.
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(z1, z2);
    }
    ScreenVert v[3] = {
        {x0, y0, z0, 1.0f, col},
        {x1, y1, z1, 1.0f, col},
        {x2, y2, z2, 1.0f, col},
    };
    out.insert(out.end(), v, v + 3);
}

void EmitBezierDigon(float ax, float ay, float bx, float by,
                     float c0x, float c0y, float c1x, float c1y,
                     float d0x, float d0y, float d1x, float d1y,
                     float z, uint32_t col, std::vector<ScreenVert>& out)
{
    std::vector<float> upperX, upperY, lowerX, lowerY;
    SampleBezier(ax, ay, c0x, c0y, c1x, c1y, bx, by, 12, upperX, upperY);
    SampleBezier(ax, ay, d0x, d0y, d1x, d1y, bx, by, 12, lowerX, lowerY);

    std::vector<float> outlineX = upperX;
    std::vector<float> outlineY = upperY;
    for (size_t i = lowerX.size(); i-- > 1;) {
        if (i + 1 == lowerX.size()) continue;
        outlineX.push_back(lowerX[i]);
        outlineY.push_back(lowerY[i]);
    }
    if (outlineX.size() < 3) return;

    float cx = 0.0f, cy = 0.0f;
    for (size_t i = 0; i < outlineX.size(); i++) {
        cx += outlineX[i];
        cy += outlineY[i];
    }
    cx /= (float)outlineX.size();
    cy /= (float)outlineY.size();
    for (size_t i = 0; i < outlineX.size(); i++) {
        const size_t j = (i + 1) % outlineX.size();
        EmitScreenTriFacingOut(out, cx, cy, z,
                               outlineX[i], outlineY[i], z,
                               outlineX[j], outlineY[j], z, col);
    }
}

// Soft Contour fill is per-edge even-odd (C721Span*), not a single polygon fan.
// Rasterize Contour edges with scanline even-odd so lobes stay solid (no ring holes).
void FillContourEdgesEvenOdd(const std::vector<float>& ex0, const std::vector<float>& ey0,
                             const std::vector<float>& ex1, const std::vector<float>& ey1,
                             float z, float camZFixed, uint32_t col,
                             std::vector<ScreenVert>& out)
{
    const int ne = (int)ex0.size();
    if (ne <= 0) return;

    float minX = ex0[0], maxX = ex0[0], minY = ey0[0], maxY = ey0[0];
    auto grow = [&](float x, float y) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    };
    for (int i = 0; i < ne; i++) {
        grow(ex0[i], ey0[i]);
        grow(ex1[i], ey1[i]);
    }
    const int y0 = (int)std::floor(minY);
    const int y1 = (int)std::ceil(maxY);
    if (y1 < y0) return;

    std::vector<std::vector<float>> hits((size_t)(y1 - y0 + 1));
    for (int i = 0; i < ne; i++) {
        float xA = ex0[i], yA = ey0[i], xB = ex1[i], yB = ey1[i];
        if (yA == yB) continue; // horizontal edges don't affect even-odd rows
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
        // Adjacent one-pixel strips can expose a raster-rule crack on Vulkan
        // after NDC conversion. Half-pixel overlap preserves soft's inclusive
        // scanline coverage without changing horizontal span endpoints.
        const float fy0 = (float)y - 0.5f;
        const float fy1 = (float)y + 1.5f;
        for (size_t i = 0; i + 1 < row.size(); i += 2) {
            const float xL = row[i];
            const float xR = row[i + 1];
            if (xR - xL < 0.5f) continue;
            // z = identity/D3D depth; rhw = raw camera Z for world-HW unproject.
            ScreenVert v[6] = {
                {xL, fy0, z, camZFixed, col}, {xR, fy0, z, camZFixed, col},
                {xL, fy1, z, camZFixed, col},
                {xR, fy0, z, camZFixed, col}, {xR, fy1, z, camZFixed, col},
                {xL, fy1, z, camZFixed, col},
            };
            out.insert(out.end(), v, v + 6);
        }
    }
}

void SoftToScreen(float sx, float sy, float& ox, float& oy)
{
    ox = sx * ((float)s_width / 320.0f);
    oy = sy * ((float)s_height / 200.0f);
}

float DepthToZ(int32_t depth);
float DepthToWorldZ(float d3dZ);

void UploadScreenTris(Ogre::ManualObject* mo, const std::vector<ScreenVert>& verts,
                      const char* matName)
{
    if (!mo) return;
    mo->clear();
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    if (verts.empty()) return;
    mo->begin(matName ? matName : OgreUnlitMaterial(),
              Ogre::OT_TRIANGLE_LIST);
    // D3D XYZRHW: pixel x/y + Z in 0..1. Identity proj expects NDC.
    auto emit = [&](const ScreenVert& v) {
        const float r = ((v.color >> 16) & 0xFF) / 255.0f;
        const float g = ((v.color >> 8) & 0xFF) / 255.0f;
        const float b = (v.color & 0xFF) / 255.0f;
        const float nx = (2.0f * v.x / (float)s_width) - 1.0f;
        const float ny = 1.0f - (2.0f * v.y / (float)s_height);
        mo->position(nx, ny, v.z);
        mo->normal(0, 0, 1);
        mo->colour(r, g, b, 1.0f);
    };
    // NDC Y flip vs soft Y-down — reverse winding.
    for (size_t i = 0; i + 2 < verts.size(); i += 3) {
        emit(verts[i]);
        emit(verts[i + 2]);
        emit(verts[i + 1]);
    }
    OgreEndManualTriangles(mo, true);
}

void UploadTexTris(Ogre::ManualObject* mo, const std::string& matName,
                   const std::vector<TexVert>& verts)
{
    if (!mo || matName.empty()) return;
    mo->clear();
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    if (verts.empty()) return;
    mo->begin(matName, Ogre::OT_TRIANGLE_LIST);
    auto emit = [&](const TexVert& v) {
        const float nx = (2.0f * v.x / (float)s_width) - 1.0f;
        const float ny = 1.0f - (2.0f * v.y / (float)s_height);
        mo->position(nx, ny, v.z);
        mo->normal(0, 0, 1);
        mo->textureCoord(v.u, v.v);
        mo->colour(1, 1, 1, 1);
    };
    for (size_t i = 0; i + 2 < verts.size(); i += 3) {
        emit(verts[i]);
        emit(verts[i + 2]);
        emit(verts[i + 1]);
    }
    OgreEndManualTriangles(mo, true);
}


uint32_t ContourColor(const TransformedPoly& head, const ShipModel* model,
                   const int32_t* shipRotLit)
{
    // Match soft ComplexFillColor / FlatRgbToPal (DPal16toPal + luma).
    // Lifter #18 top/bottom Contours are mat 0x4016 → shade0 (VGA22).
    if ((head.texInfo >> 14) == 0) {
        if (head.texInfo == 0)
            return PalIdxToArgb(DPal16toPal(0));
        int r = (head.texInfo >> 8) & 0xF;
        int g = (head.texInfo >> 4) & 0xF;
        int b = head.texInfo & 0xF;
        if ((head.texInfo & 0x1000) != 0) {
            int8_t flR = 0, flG = 0, flB = 0;
            GetMeshFaceLight(&flR, &flG, &flB);
            r += flR; g += flG; b += flB;
            if (r < 0) r = 0;
            if (r > 15) r = 15;
            if (g < 0) g = 0;
            if (g > 15) g = 15;
            if (b < 0) b = 0;
            if (b > 15) b = 15;
        }
        uint8_t pal = DPal16toPal(static_cast<uint16_t>((r << 8) | (g << 4) | b));
        uint8_t lit = GetFaceColor(model, head.normalIdx, 0x08, shipRotLit);
        if (lit < 4) lit = 4;
        return BoostIfDim(PalIdxToArgb(pal), lit);
    }

    if (head.texInfo == 0x5015 || head.texInfo == 0x5009) {
        ShipMaterialInfo mat{};
        if (ShipResolveMaterial(head.texInfo, &mat) && mat.shadeCount > 0) {
            uint16_t idx = static_cast<uint16_t>(
                (mat.shades[0][0] << 8) | (mat.shades[0][1] << 4) | mat.shades[0][2]);
            return PalIdxToArgb(DPal16toPal(idx));
        }
    }
    if ((head.texInfo & 0xF000) == 0x5000) {
        uint8_t lit = GetFaceColor(model, head.normalIdx, 0x08, shipRotLit);
        if (lit < 4) lit = 4;
        return BoostIfDim(PalIdxToArgb(lit), lit);
    }

    ShipMaterialInfo mat{};
    if (ShipResolveMaterial(head.texInfo, &mat)) {
        int8_t flR = 0, flG = 0, flB = 0;
        GetMeshFaceLight(&flR, &flG, &flB);
        const bool applyFL =
            (head.texInfo & 0x1000) != 0 && (flR | flG | flB) != 0;
        uint8_t shadePal[256];
        ShipBuildTexShadePalette(shadePal, mat, flR, flG, flB, applyFL);
        // Keep shade0 as-is (no BoostIfDim) — intentional dark hull Contour.
        return PalIdxToArgb(shadePal[0]);
    }

    uint8_t lit = GetFaceColor(model, head.normalIdx, 0x08, shipRotLit);
    if (lit < 4) lit = 4;
    return BoostIfDim(PalIdxToArgb(lit), lit);
}

float DepthToZ(int32_t depth)
{
    // Map soft/ASM depth into a D3D-style 0..1 window (smaller = closer).
    if (depth <= 0) return 0.05f;
    return 0.05f + 0.90f * ((float)depth / ((float)depth + 65536.0f));
}

// Ortho camera sits at +Z looking toward origin. Spread D3D Z across several
// world units so Vulkan's depth buffer can separate freckles vs punches
// (the old -d3dZ in ~1 unit caused windshield / gasket Z-fighting).
[[maybe_unused]] float DepthToWorldZ(float d3dZ)
{
    return -(0.25f + d3dZ * 24.0f); // ~0.05→-1.45 … 0.95→-23.05
}

int DrawContourGroups(const TransformedPoly* polys, int visibleCount,
                      const ShipModel* model, const int32_t* shipRotLit,
                      const int32_t* shipRot, const Camera3D* camera,
                      std::vector<ScreenVert>& out, bool glintPass = false,
                      std::vector<OgreHwContourBatch>* batchesOut = nullptr)
{
    auto commitBatch = [&](uint32_t sortKey, size_t before) {
        if (!batchesOut || out.size() <= before) return;
        OgreHwContourBatch b;
        b.sortKey = sortKey;
        b.xy.reserve((out.size() - before) * 2);
        b.colors.reserve(out.size() - before);
        for (size_t i = before; i < out.size(); i++) {
            b.xy.push_back(out[i].x);
            b.xy.push_back(out[i].y);
            b.colors.push_back(out[i].color);
        }
        batchesOut->push_back(std::move(b));
    };
    bool done[256] = {};
    int groups = 0;
    for (int i = 0; i < visibleCount; i++) {
        const TransformedPoly& p = polys[i];
        // Soft draw list: front-facing Contour pieces only. Mixing back faces
        // into even-odd punches hollow rings (looks transparent).
        if (!p.visible || p.backFacing) continue;
        if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
        if (done[i]) continue;
        // FUNC_001736 glints: soft paints after rear hull. Defer to a post-
        // texture pass so Z-tested hull freckles don't bury the nozzles.
        const bool pIsGlint =
            p.texInfo == 0x20ee && (p.specialParam & 0xFFF0) == 0x7360;
        if (pIsGlint != glintPass)
            continue;

        const TransformedPoly* group[64];
        int ng = 0;
        for (int j = 0; j < visibleCount && ng < 64; j++) {
            if (!polys[j].visible || polys[j].backFacing) continue;
            if (polys[j].type != MeshPolyType::ComplexFill) continue;
            if (polys[j].contourId != p.contourId) continue;
            group[ng++] = &polys[j];
            done[j] = true;
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

        // Falcon #20: soft Contour span for non-glint Contours is a no-op
        // (even-odd cancels / matches freckles). D3D even-odd fill of Contour
        // cid=0 paints a large ContourColor slab over the engine bay that
        // reads as bright nozzle "rectangles". Only keep FUNC_001736 glints.
        if (model && GetShipModelIndex(model) == 20 &&
            group[0]->texInfo != 0x20ee) {
            groups++;
            continue;
        }

        // Tiger/Falcon FUNC_001736 Contour 0x20ee glints: soft 001797 fills a
        // cubic-bezier digon (lens/oval). Solid half-quads read as rectangles;
        // even-odd of both arcs left a mid stripe. Tessellate the digon outline
        // as a triangle fan (same control points as soft C721SpanAddBezier).
        if (group[0]->texInfo == 0x20ee) {
            const bool isGlint =
                (group[0]->specialParam & 0xFFF0) == 0x7360;
            if (isGlint) {
                int r = (0x20ee >> 8) & 0xF;
                int g = (0x20ee >> 4) & 0xF;
                int b = 0x20ee & 0xF;
                uint8_t pal = DPal16toPal(static_cast<uint16_t>((r << 8) | (g << 4) | b));
                uint32_t col = PalIdxToArgb(pal);
                for (int gi = 0; gi + 1 < ng; gi += 2) {
                    const TransformedPoly& up = *group[gi];
                    const TransformedPoly& lo = *group[gi + 1];
                    if (up.vertexCount != 4 || lo.vertexCount != 4)
                        continue;
                    float ax, ay, bx, by, c0x, c0y, c1x, c1y, d0x, d0y, d1x, d1y;
                    SoftToScreen(up.verts[0].screenX, up.verts[0].screenY, ax, ay);
                    SoftToScreen(up.verts[3].screenX, up.verts[3].screenY, bx, by);
                    SoftToScreen(up.verts[1].screenX, up.verts[1].screenY, c0x, c0y);
                    SoftToScreen(up.verts[2].screenX, up.verts[2].screenY, c1x, c1y);
                    SoftToScreen(lo.verts[1].screenX, lo.verts[1].screenY, d0x, d0y);
                    SoftToScreen(lo.verts[2].screenX, lo.verts[2].screenY, d1x, d1y);

                    std::vector<float> ux, uy, lx, ly;
                    SampleBezier(ax, ay, c0x, c0y, c1x, c1y, bx, by, 12, ux, uy);
                    SampleBezier(ax, ay, d0x, d0y, d1x, d1y, bx, by, 12, lx, ly);
                    // Closed outline: upper a→b, then lower b→a (reverse, skip ends).
                    std::vector<float> ox, oy;
                    ox.reserve(ux.size() + ly.size());
                    oy.reserve(ox.capacity());
                    for (size_t i = 0; i < ux.size(); i++) {
                        ox.push_back(ux[i]);
                        oy.push_back(uy[i]);
                    }
                    for (size_t i = ly.size(); i-- > 1; ) { // skip lx[0]==a
                        if (i + 1 == ly.size())
                            continue; // skip b duplicate
                        ox.push_back(lx[i]);
                        oy.push_back(ly[i]);
                    }
                    if (ox.size() < 3)
                        continue;

                    float cx = 0, cy = 0;
                    for (size_t i = 0; i < ox.size(); i++) {
                        cx += ox[i];
                        cy += oy[i];
                    }
                    cx /= (float)ox.size();
                    cy /= (float)oy.size();
                    float z = DepthToZ(up.verts[0].depth);
                    if (z > 0.03f)
                        z -= 0.012f;

                    const size_t beforeGlint = out.size();
                    for (size_t i = 0; i < ox.size(); i++) {
                        const size_t j = (i + 1) % ox.size();
                        EmitScreenTriFacingOut(out, cx, cy, z, ox[i], oy[i], z,
                                               ox[j], oy[j], z, col);
                    }
                    commitBatch(group[0]->sortKey, beforeGlint);
                }
            } else {
                const uint32_t punchCol = (uint32_t)(((255) << 24) | (((uint32_t)(0) & 0xFF) << 16) | (((uint32_t)(0) & 0xFF) << 8) | ((uint32_t)(0) & 0xFF));
                const size_t beforePunch = out.size();
                for (int g = 0; g < ng; g++) {
                    const TransformedPoly& q = *group[g];
                    if (q.vertexCount != 4) continue;
                    float x0, y0, x1, y1, x2, y2, x3, y3;
                    SoftToScreen(q.verts[0].screenX, q.verts[0].screenY, x0, y0);
                    SoftToScreen(q.verts[1].screenX, q.verts[1].screenY, x1, y1);
                    SoftToScreen(q.verts[2].screenX, q.verts[2].screenY, x2, y2);
                    SoftToScreen(q.verts[3].screenX, q.verts[3].screenY, x3, y3);
                    float z = DepthToZ(q.verts[0].depth);
                    if (z > 0.03f) z -= 0.008f;
                    EmitScreenTriFacingOut(out, x0, y0, z, x1, y1, z, x2, y2, z,
                                           punchCol);
                    EmitScreenTriFacingOut(out, x0, y0, z, x2, y2, z, x3, y3, z,
                                           punchCol);
                }
                commitBatch(group[0]->sortKey, beforePunch);
            }
            groups++;
            continue;
        }

        // Soft fan-only Contours (Lifter plates): DrawTriangle each piece.
        // Even-odd of fan spokes cancels interiors → "transparent" grey panels.
        bool anyQuad = false, anyDigon = false, anyTri = false;
        for (int g = 0; g < ng; g++) {
            const int vc = group[g]->vertexCount;
            if (vc == 4) anyQuad = true;
            else if (vc == 2) anyDigon = true;
            else if (vc == 3) anyTri = true;
        }
        if (!anyQuad && !anyDigon && anyTri) {
            uint32_t col = ContourColor(*group[0], model, shipRotLit);
            const size_t beforeFan = out.size();
            for (int g = 0; g < ng; g++) {
                const TransformedPoly& q = *group[g];
                if (q.vertexCount != 3) continue;
                float x0, y0, x1, y1, x2, y2;
                SoftToScreen(q.verts[0].screenX, q.verts[0].screenY, x0, y0);
                SoftToScreen(q.verts[1].screenX, q.verts[1].screenY, x1, y1);
                SoftToScreen(q.verts[2].screenX, q.verts[2].screenY, x2, y2);
                EmitScreenTriFacingOut(out, x0, y0, DepthToZ(q.verts[0].depth),
                                       x1, y1, DepthToZ(q.verts[1].depth),
                                       x2, y2, DepthToZ(q.verts[2].depth), col);
            }
            commitBatch(group[0]->sortKey, beforeFan);
            groups++;
            continue;
        }

        // Contour edge list (soft C721SpanAddBezier / AddEdge) — not one fan.
        std::vector<float> e0x, e0y, e1x, e1y;
        e0x.reserve((size_t)ng * 12);
        float depthSum = 0;
        int depthN = 0;
        int nQuads = 0;
        float aabbX0 = 1e9f, aabbY0 = 1e9f, aabbX1 = -1e9f, aabbY1 = -1e9f;
        float firstX = 0, firstY = 0, lastX = 0, lastY = 0;
        bool haveFirst = false;
        bool hasHBridge = false, hasVBridge = false, hasDiagBridge = false;
        auto classifyBridge = [&](float dx, float dy) {
            dx = std::fabs(dx);
            dy = std::fabs(dy);
            if (dx > 0.0f && dy * 2.0f <= dx)
                hasHBridge = true;
            else if (dy > 0.0f && dx * 2.0f <= dy)
                hasVBridge = true;
            else if (dx > 0.0f && dy > 0.0f &&
                     dy * 2.0f > dx && dx * 2.0f > dy)
                hasDiagBridge = true;
        };

        auto addEdge = [&](float x0, float y0, float x1, float y1) {
            e0x.push_back(x0);
            e0y.push_back(y0);
            e1x.push_back(x1);
            e1y.push_back(y1);
            auto grow = [&](float x, float y) {
                if (x < aabbX0) aabbX0 = x;
                if (y < aabbY0) aabbY0 = y;
                if (x > aabbX1) aabbX1 = x;
                if (y > aabbY1) aabbY1 = y;
            };
            grow(x0, y0);
            grow(x1, y1);
        };

        for (int g = 0; g < ng; g++) {
            const TransformedPoly& q = *group[g];
            if (q.vertexCount == 4) {
                nQuads++;
                float x0, y0, x1, y1, x2, y2, x3, y3;
                SoftToScreen(q.verts[0].screenX, q.verts[0].screenY, x0, y0);
                SoftToScreen(q.verts[1].screenX, q.verts[1].screenY, x1, y1);
                SoftToScreen(q.verts[2].screenX, q.verts[2].screenY, x2, y2);
                SoftToScreen(q.verts[3].screenX, q.verts[3].screenY, x3, y3);
                std::vector<float> bx, by;
                SampleBezier(x0, y0, x1, y1, x2, y2, x3, y3, 12, bx, by);
                for (size_t k = 1; k < bx.size(); k++)
                    addEdge(bx[k - 1], by[k - 1], bx[k], by[k]);
                if (!haveFirst) {
                    firstX = x0;
                    firstY = y0;
                    haveFirst = true;
                }
                lastX = x3;
                lastY = y3;
                depthSum += (float)q.verts[0].depth + (float)q.verts[3].depth;
                depthN += 2;
            } else if (q.vertexCount == 2) {
                float x0, y0, x1, y1;
                SoftToScreen(q.verts[0].screenX, q.verts[0].screenY, x0, y0);
                SoftToScreen(q.verts[1].screenX, q.verts[1].screenY, x1, y1);
                addEdge(x0, y0, x1, y1);
                classifyBridge(x1 - x0, y1 - y0);
                if (!haveFirst) {
                    firstX = x0;
                    firstY = y0;
                    haveFirst = true;
                }
                lastX = x1;
                lastY = y1;
                depthSum += (float)q.verts[0].depth + (float)q.verts[1].depth;
                depthN += 2;
            } else if (q.vertexCount == 3) {
                float x0, y0, x1, y1, x2, y2;
                SoftToScreen(q.verts[0].screenX, q.verts[0].screenY, x0, y0);
                SoftToScreen(q.verts[1].screenX, q.verts[1].screenY, x1, y1);
                SoftToScreen(q.verts[2].screenX, q.verts[2].screenY, x2, y2);
                addEdge(x0, y0, x1, y1);
                addEdge(x1, y1, x2, y2);
                addEdge(x2, y2, x0, y0);
                depthSum += (float)q.verts[0].depth;
                depthN++;
            }
        }
        if (e0x.empty()) continue;
        if (haveFirst && !hasHBridge && !hasVBridge && !hasDiagBridge)
            classifyBridge(lastX - firstX, lastY - firstY);

        // Soft Contour close (001796): skip on wide multi-quad Contours (Puma
        // butterfly) — forced close punches even-odd holes / opposite tip.
        const float spanW = aabbX1 - aabbX0;
        const float spanH = aabbY1 - aabbY0;
        const bool wideContour = spanW * 5.0f >= spanH * 9.0f;
        const bool singleWideClose =
            nQuads == 1 && group[0]->texInfo == 0x4008;
        const bool digonWideClose = (nQuads == 2 || singleWideClose) && wideContour;
        if (haveFirst && (!wideContour || digonWideClose)) {
            const float dx = lastX - firstX, dy = lastY - firstY;
            if (dx * dx + dy * dy > 1.0f)
                addEdge(lastX, lastY, firstX, firstY);
        }

        uint32_t col = ContourColor(*group[0], model, shipRotLit);
        // Same depth mapping as hull/textures — old inverted formula let
        // freckled neck tris Z-win through Contour panels.
        float camZFixed = (float)FP_ONE;
        if (depthN > 0) {
            // Farthest Contour control depth — average pulled Contours in front
            // of the fuselage at oblique Osprey views.
            float zMax = 0.f;
            for (int gi = 0; gi < ng; gi++) {
                const TransformedPoly& q = *group[gi];
                for (int vi = 0; vi < q.vertexCount && vi < 4; vi++) {
                    const float d = (float)q.verts[vi].depth;
                    if (d > zMax) zMax = d;
                }
            }
            camZFixed = (zMax > 0.f) ? zMax : (depthSum / (float)depthN);
        }
        const float z = DepthToZ((int32_t)camZFixed);
        const size_t beforeFill = out.size();
        FillContourEdgesEvenOdd(e0x, e0y, e1x, e1y, z, camZFixed, col, out);
        commitBatch(group[0]->sortKey, beforeFill);

        // Puma's four engine/wing openings are separate black cubic-bezier
        // digons in the soft Contour schedule. The world hull must remain
        // depth-tested, so reproduce only these subtractive screen overlays.
        if (model && GetShipModelIndex(model) == 60 && nQuads == 4 && haveFirst) {
            const float bw = aabbX1 - aabbX0;
            const float bh = aabbY1 - aabbY0;
            const bool wideOk = hasHBridge && bw * 5.0f >= bh * 9.0f;
            const bool tallOk = hasVBridge && bh * 5.0f >= bw * 9.0f;
            const bool diagOk =
                hasDiagBridge && bw * 2.0f >= bh && bh * 2.0f >= bw;
            const uint32_t black = 0xFF000000u;

            auto at = [&](float fx, float fy, float& x, float& y) {
                x = aabbX0 + fx * bw;
                y = aabbY0 + fy * bh;
            };
            auto rotate = [](float fx, float fy, int turns, float& x, float& y) {
                x = fx;
                y = fy;
                for (int r = 0; r < turns; r++) {
                    const float nx = 0.5f - (y - 0.5f);
                    const float ny = 0.5f + (x - 0.5f);
                    x = nx;
                    y = ny;
                }
            };
            auto emitTemplate = [&](const float base[6][2], int turns,
                                    bool mirrorX, bool mirrorY) {
                float px[6], py[6];
                for (int pidx = 0; pidx < 6; pidx++) {
                    float fx, fy;
                    rotate(base[pidx][0], base[pidx][1], turns, fx, fy);
                    if (mirrorX) fx = 1.0f - fx;
                    if (mirrorY) fy = 1.0f - fy;
                    at(fx, fy, px[pidx], py[pidx]);
                }
                EmitBezierDigon(px[0], py[0], px[1], py[1],
                                px[2], py[2], px[3], py[3],
                                px[4], py[4], px[5], py[5],
                                z, black, out);
            };

            if (wideOk || tallOk) {
                static constexpr float gap[6][2] = {
                    {0.447f, 0.816f}, {0.342f, 0.605f},
                    {0.500f, 0.684f}, {0.408f, 0.500f},
                    {0.382f, 0.947f}, {0.276f, 0.737f},
                };
                const int turns = tallOk ? 1 : 0;
                for (int mx = 0; mx < 2; mx++)
                    for (int my = 0; my < 2; my++)
                        emitTemplate(gap, turns, mx != 0, my != 0);
            } else if (diagOk) {
                static constexpr float gap[6][2] = {
                    {0.424f, 0.333f}, {0.576f, 0.333f},
                    {0.424f, 0.439f}, {0.576f, 0.439f},
                    {0.424f, 0.212f}, {0.576f, 0.212f},
                };
                for (int turns = 0; turns < 4; turns++)
                    emitTemplate(gap, turns, false, false);
            }
        }
        groups++;
    }
    return groups;
}

} // namespace

ShipRenderMode OgreGetShipRenderMode() { return s_shipMode; }

void OgreSetShipRenderMode(ShipRenderMode mode)
{
    if (s_shipMode == mode) return;
    s_shipMode = mode;
    OgreShipHwSetActive(mode == ShipRenderMode::Hardware);
    printf("Ogre ship mode → %s\n",
           mode == ShipRenderMode::Hardware ? "HW" : "SOFT");
}

void OgreToggleShipRenderMode()
{
    OgreSetShipRenderMode(s_shipMode == ShipRenderMode::Hardware
                              ? ShipRenderMode::Software
                              : ShipRenderMode::Hardware);
}

static ShipRenderMode ParseShipModeEnv()
{
    // Default HW. Software: JJFFE_OGRE_SHIP_MODE=sw (aliases: soft, software, 0).
    const char* e = getenv("JJFFE_OGRE_SHIP_MODE");
    if (!e || !e[0]) return ShipRenderMode::Hardware;
    if (e[0] == 'h' || e[0] == 'H' || e[0] == '1')
        return ShipRenderMode::Hardware;
    if (e[0] == 's' || e[0] == 'S' || e[0] == '0')
        return ShipRenderMode::Software;
    return ShipRenderMode::Hardware;
}

static bool UseWorldHwPath()
{
    const char* path = getenv("JJFFE_OGRE_HW_PATH");
    return !(path && (std::strcmp(path, "painter") == 0 ||
                     std::strcmp(path, "2d") == 0));
}

bool OgreDemoInit(int width, int height)
{
    s_width = width;
    s_height = height;
    CameraInit(&s_camera);
    CameraSetPosition(&s_camera, 0, 0, 0);
    s_yaw = DegToFp(180);
    s_pitch = 0;
    s_dist = FP_ONE / 2;
    s_paused = true;
    s_initialized = true;
    s_ready = true;
    s_shipMode = ParseShipModeEnv();
    InitMeshOpcodeHandlers();
    if (g_videoState) {
        if (!getenv("AB_DPAL_LEGACY"))
            setenv("AB_DPAL_LEGACY", "1", 0);
        DPalClear();
        DPalBuild(g_videoState->palette, 0);
    }
    OgreTextureInit();
    return true;
}

void OgreDemoShutdown()
{
    OgreSoftBlitShutdown();
    OgreTextureShutdown();
    s_ready = false;
    s_initialized = false;
}

void OgreDrawDemo()
{
    if (!s_ready || !s_initialized) return;
    if (IsDemoExportAsset()) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            std::fprintf(stderr,
                         "Ogre: --export-ship GPU upload not wired yet; "
                         "use ffelnxd3d --export-ship N for B5 orbit.\n");
        }
    }
    const Uint32 frameStart = SDL_GetTicks();

    // OG flight/space clear is navy, not pure black (screenshot ~RGB 16,11,67).
    OgreClear(16.0f / 255.0f, 11.0f / 255.0f, 67.0f / 255.0f);
    DrawStars();

    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    if (keyStates[KEY_A] == 0) s_yaw -= kYawStep;
    if (keyStates[KEY_D] == 0) s_yaw += kYawStep;
    if (keyStates[KEY_W] == 0) { s_dist -= kZoomStep; if (s_dist < FP_ONE / 8) s_dist = FP_ONE / 8; }
    if (keyStates[KEY_S] == 0) { s_dist += kZoomStep; if (s_dist > 20 * FP_ONE) s_dist = 20 * FP_ONE; }
    if (keyStates[KEY_Q] == 0) s_pitch -= kPitchStep;
    if (keyStates[KEY_E] == 0) s_pitch += kPitchStep;

    {
        int mx = 0, my = 0;
        InputMouseReadMickeys(&mx, &my);
        if ((InputMouseReadButtons() & 1) != 0 && (mx != 0 || my != 0)) {
            s_yaw += static_cast<int32_t>(mx) * kMouseYawPerPix;
            s_pitch += static_cast<int32_t>(my) * kMousePitchPerPix;
            s_paused = true;
        }
    }

    if (keyStates[KEY_P] == 0) {
        if (!s_keyPWasDown) {
            s_paused = !s_paused;
            s_keyPWasDown = true;
        }
    } else {
        s_keyPWasDown = false;
    }

    if (keyStates[KEY_H] == 0) {
        if (!s_keyHWasDown) {
            OgreToggleShipRenderMode();
            s_keyHWasDown = true;
        }
    } else {
        s_keyHWasDown = false;
    }

    if (keyStates[KEY_N] == 0) {
        if (!s_keyNWasDown) {
            OgreShipHwToggleNormalDebug();
            s_keyNWasDown = true;
        }
    } else {
        s_keyNWasDown = false;
    }

    if (keyStates[KEY_T] == 0) {
        if (!s_keyTWasDown) {
            OgreShipHwToggleTextures();
            printf("Ogre HW textures → %s\n",
                   OgreShipHwTextures() ? "ON" : "OFF");
            s_keyTWasDown = true;
        }
    } else {
        s_keyTWasDown = false;
    }

    if (!s_paused) s_yaw += kSpinStep;

    int shipIdx = GetDemoShipIndex();
    const ShipModel* model = GetShipModel(shipIdx);
    if (!model || !model->meshDataOffset) {
        OgreClear(16.0f / 255.0f, 11.0f / 255.0f, 67.0f / 255.0f);
        OgreDrawText(20, 20, "No ship model", 0xFFFFFFFF, 2.0f);
        OgreEndFrame();
        return;
    }

    if (shipIdx != s_lastShipIdx) {
        RefreshFitForShip(model);
        s_yaw = DegToFp(180);
        s_pitch = 0;
        s_paused = true;
        s_lastShipIdx = shipIdx;
        printf("Ogre demo ship #%d fitDistUnits=%d\n", shipIdx, s_fitDistUnits);
    }
    ApplyScriptedPose();

    Mat33 rotMat;
    MatBuildYZT(&rotMat, s_yaw, s_pitch);
    Mat33 rotLit = rotMat;
    MatScaleSin32ToFp16(&rotLit);

    for (int i = 0; i < 9; i++) s_camera.rotation.m[i] = 0;
    s_camera.rotation.m[0] = FP_ONE;
    s_camera.rotation.m[4] = FP_ONE;
    s_camera.rotation.m[8] = FP_ONE;

    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = rotLit.m[i];
    int32_t shipPos[3] = {0, 0, s_dist};

    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) {
        OgreDrawText(20, 20, "No mesh data", 0xFFFF4444, 2.0f);
        OgreEndFrame();
        return;
    }
    size_t meshSize = MeshBytecodeSize(shipIdx);
    if (meshSize == 0) meshSize = 1024;

    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(s_dist));
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    if (!mesh.valid || mesh.polygons.empty()) {
        OgreDrawText(20, 20, "Mesh parse failed", 0xFFFF4444, 2.0f);
        OgreEndFrame();
        return;
    }

    TransformedPoly polys[256];
    int visibleCount = TransformShipVertices(model, mesh, shipRot, shipPos, &s_camera, polys, 256);

    OgreClearPainterPolyManuals(0);
    OgreClearTexManuals();
    if (auto* c = OgreContourManual()) c->clear();
    if (auto* g = OgreGlintManual()) g->clear();
    if (auto* p = OgrePunchManual()) p->clear();
    OgreShipHwClear();

    OgreShipHwStats hwStats{};
    int contourGroups = 0;
    int glintGroups = 0;

    if (s_shipMode == ShipRenderMode::Hardware) {
        // Same tip/throat punch gates + sortKey nudges as soft RenderShipFlat.
        ShipPrepareFlatPunchGates(polys, visibleCount, model);

        WorldFace faces[256];
        const int faceCount =
            BuildWorldShipFaces(polys, visibleCount, model, shipRotLit, faces, 256);
        OgreShipHwSetViewDistance((float)s_dist / (float)FP_ONE);

        const bool worldHw = UseWorldHwPath();
        if (worldHw) {
            // True HW path: authored world coordinates, perspective camera and
            // GPU depth. Contours/glints remain identity-projected overlays.
            OgreClearPainterPolyManuals(0);
            hwStats = OgreShipHwDrawHull(faces, faceCount, polys, visibleCount,
                                         model, shipRotLit);
        } else {
        // Diagnostic fallback: screen-space GPU path with soft painter order.
        struct HwItem {
            int faceIdx;
            bool textured = false;
            std::string mat;
            float u[4]{}, v[4]{};
            uint32_t col = 0xFF0C0C14u;
        };
        std::vector<HwItem> items;
        items.reserve((size_t)faceCount);
        for (int i = 0; i < faceCount; i++) {
            const WorldFace& f = faces[i];
            if (!f.worldValid || f.vertexCount < 3) continue;
            if (f.kind == WorldFaceKind::ScreenHybrid) continue;
            if (f.sourceIndex < 0 || f.sourceIndex >= visibleCount) continue;
            const TransformedPoly& p = polys[f.sourceIndex];
            if (!p.visible) continue;

            HwItem it{};
            it.faceIdx = i;
            if (f.kind != WorldFaceKind::Punch && ShipTexInfoIsTextured(f.texInfo)) {
                int texSize = 0;
                bool transparent = false;
                std::string mat = OgreGetShipTextureMaterial(
                    f.texInfo, f.faceLight[0], f.faceLight[1], f.faceLight[2], true,
                    &texSize, &transparent);
                if (!mat.empty() && texSize >= 2) {
                    ShipMaterialInfo minfo{};
                    ShipResolveMaterial(f.texInfo, &minfo);
                    ShipAssignNormalizedTexUVs(p, minfo.texWord, texSize, it.u, it.v);
                    it.textured = true;
                    it.mat = mat;
                    (void)transparent;
                }
            }
            if (!it.textured) {
                // Match soft colours (PolyColor already maps 0x2000 → VGA0;
                // 0x3000 + face light → grey nozzle bridge).
                it.col = PolyColor(p, model, shipRotLit) | 0xFF000000u;
                // Soft VGA0 gate uses palette index; approximate from ARGB.
                const uint8_t punchPal =
                    ((it.col >> 16) & 0xFF) | ((it.col >> 8) & 0xFF) | (it.col & 0xFF)
                        ? 1
                        : 0;
                // Skip gated 0x2000/0x3000 (wing blacks); keep Saker tip/throat.
                if (f.kind == WorldFaceKind::Punch &&
                    !ShipAllowVga0FlatPunch(p, model, punchPal))
                    continue;
                if (getenv("JJFFE_OGRE_HW_PUNCH_DBG") && f.kind == WorldFaceKind::Punch)
                    it.col = 0xFFFF00FFu;
            }
            items.push_back(it);
        }
        std::sort(items.begin(), items.end(), [&](const HwItem& a, const HwItem& b) {
            const WorldFace& fa = faces[a.faceIdx];
            const WorldFace& fb = faces[b.faceIdx];
            if (fa.sortKey != fb.sortKey) return fa.sortKey > fb.sortKey; // far first
            return fa.meshIndex < fb.meshIndex;
        });

        // Soft is a painter (no Z-buffer): far→near overwrite. GPU depth made
        // nozzle/wing blacks float over freckles; depth-off restores soft order.
        OgreClearPainterPolyManuals(0);
        int painterIdx = 0;
        for (const HwItem& it : items) {
            const WorldFace& f = faces[it.faceIdx];
            const TransformedPoly& p = polys[f.sourceIndex];
            auto* mo = OgrePainterPolyManual(painterIdx, (uint16_t)painterIdx);
            if (!mo) continue;
            // identity applied after end() via OgreApplyIdentityProjection
            if (it.textured) {
                // Depth off so painter order wins; alpha-reject still reveals
                // earlier underlay (gasket holes) like soft.
                // Depth-off painter order is encoded in RQ priority (Next Hlms).
                std::vector<TexVert> tv;
                tv.reserve(6);
                auto corner = [&](int a, int b, int c) {
                    float x0, y0, x1, y1, x2, y2;
                    SoftToScreen(p.verts[a].screenX, p.verts[a].screenY, x0, y0);
                    SoftToScreen(p.verts[b].screenX, p.verts[b].screenY, x1, y1);
                    SoftToScreen(p.verts[c].screenX, p.verts[c].screenY, x2, y2);
                    // Constant Z — unused when depth is off.
                    float z0 = 0.5f, z1 = 0.5f, z2 = 0.5f;
                    float cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
                    int ia = a, ib = b, ic = c;
                    if (cross < 0.0f) {
                        std::swap(x1, x2); std::swap(y1, y2); std::swap(z1, z2);
                        ib = c; ic = b;
                    }
                    TexVert t[3] = {
                        {x0, y0, z0, 1.0f, 0xFFFFFFFFu, it.u[ia], it.v[ia]},
                        {x1, y1, z1, 1.0f, 0xFFFFFFFFu, it.u[ib], it.v[ib]},
                        {x2, y2, z2, 1.0f, 0xFFFFFFFFu, it.u[ic], it.v[ic]},
                    };
                    tv.insert(tv.end(), t, t + 3);
                };
                corner(0, 1, 2);
                if (p.vertexCount >= 4) corner(0, 2, 3);
                UploadTexTris(mo, it.mat, tv);
                hwStats.hullTris += (int)tv.size() / 3;
            } else {
                std::vector<ScreenVert> sv;
                sv.reserve(6);
                float x[4], y[4], z[4];
                for (int v = 0; v < p.vertexCount && v < 4; v++) {
                    SoftToScreen((float)p.verts[v].screenX, (float)p.verts[v].screenY,
                                 x[v], y[v]);
                    z[v] = 0.5f;
                }
                auto tri = [&](int a, int b, int c) {
                    EmitScreenTriFacingOut(sv, x[a], y[a], z[a], x[b], y[b], z[b],
                                           x[c], y[c], z[c], it.col);
                };
                tri(0, 1, 2);
                if (p.vertexCount >= 4) tri(0, 2, 3);
                // Depth-off UI material — pure painter overwrite like soft.
                UploadScreenTris(mo, sv, OgreUnlitUiMaterial());
                if (f.kind == WorldFaceKind::Punch)
                    hwStats.punchTris += (int)sv.size() / 3;
                else
                    hwStats.hullTris += (int)sv.size() / 3;
            }
            painterIdx++;
        }
        OgreClearPainterPolyManuals(painterIdx);
        hwStats.worldFaces = faceCount;
        hwStats.materials = painterIdx;
        }

        if (OgreShipHwHybridOverlays()) {
            int nCf = 0, nC721 = 0, nC721Front = 0;
            for (int i = 0; i < visibleCount; i++) {
                const TransformedPoly& p = polys[i];
                if (p.type != MeshPolyType::ComplexFill) continue;
                nCf++;
                if (p.contourId < 0) continue;
                nC721++;
                if (p.visible && !p.backFacing) nC721Front++;
            }
            if (getenv("JJFFE_OGRE_HW_DEBUG") && getenv("JJFFE_OGRE_HW_DEBUG")[0])
                fprintf(stderr, "HW_C721 ComplexFill=%d contour=%d front=%d\n",
                        nCf, nC721, nC721Front);
            {
                int kindH = 0, kindP = 0, kindS = 0, kindInv = 0;
                for (int i = 0; i < faceCount; i++) {
                    if (!faces[i].worldValid) { kindInv++; continue; }
                    if (faces[i].kind == WorldFaceKind::Hull) kindH++;
                    else if (faces[i].kind == WorldFaceKind::Punch) kindP++;
                    else kindS++;
                }
                if (getenv("JJFFE_OGRE_HW_DEBUG") && getenv("JJFFE_OGRE_HW_DEBUG")[0])
                    fprintf(stderr, "HW_FACE hull=%d punch=%d hybrid=%d invalid=%d\n",
                            kindH, kindP, kindS, kindInv);
            }
            std::vector<ScreenVert> contourTris;
            std::vector<ScreenVert> glintTris;
            std::vector<OgreHwContourBatch> contourBatches;
            contourGroups = DrawContourGroups(polys, visibleCount, model, shipRotLit,
                                              shipRot, &s_camera, contourTris,
                                              /*glintPass=*/false, &contourBatches);
            glintGroups = DrawContourGroups(polys, visibleCount, model, shipRotLit,
                                            shipRot, &s_camera, glintTris,
                                            /*glintPass=*/true, nullptr);
            if (!contourBatches.empty() && UseWorldHwPath()) {
                // Soft painter: Contours and freckles interleave by sortKey so
                // wing Contours stay late while Contour∩body freckles restamp.
                OgreShipHwScheduleContourFreckles(contourBatches.data(),
                                                  (int)contourBatches.size());
            } else if (!contourTris.empty()) {
                if (auto* c = OgreContourManual()) {
                    c->setRenderQueueGroup(11);
                }
                UploadScreenTris(OgreContourManual(), contourTris,
                                 OgreUnlitUiMaterial());
            }
            if (!glintTris.empty())
                UploadScreenTris(OgreGlintManual(), glintTris, OgreUnlitUiMaterial());
            hwStats.contourTris = (int)(contourTris.size() / 3);
            hwStats.glintTris = (int)(glintTris.size() / 3);
        }
        // Soft blit MO unused in HW — keep cleared so it does not cover the hull.
        if (auto* hull = OgreHullManual()) hull->clear();
    } else {
        // Soft reference path: CPU RenderShipFlat → texture blit.
        if (g_videoState) {
            std::memset(g_videoState->backBuffer, 0xFF, SCREEN_SIZE);
            RenderShipFlat(polys, visibleCount, model, shipRot);
        }
        OgreBlitSoftBackBuffer(OgreHullManual());
        if (auto* hull = OgreHullManual())
            hull->setRenderQueueGroup(10);
    }

    char txt[128];
    char nameBuf[48];
    int yawDeg = (int)(((s_yaw & 0xFFFF) * 360LL) >> 16);
    int pitchDeg = (int)(((s_pitch & 0xFFFF) * 360LL) >> 16);
    if (s_shipMode == ShipRenderMode::Hardware) {
        snprintf(txt, sizeof(txt),
                 "HW-%s %s Z:%d Y:%d P:%d POLY:%d tris:%d/%d c721:%d%s%s %s",
                 UseWorldHwPath() ? "3D" : "2D",
                 OgreShipHwTextures() ? "TEX" : "FLAT",
                 (int)(s_dist / 256), yawDeg, pitchDeg, visibleCount,
                 hwStats.hullTris, hwStats.punchTris,
                 contourGroups + glintGroups,
                 OgreShipHwNormalDebug() ? " NDBG" : "",
                 OgreShipHwWireframe() ? " WIRE" : "",
                 s_paused ? "[P]" : "");
    } else {
        snprintf(txt, sizeof(txt), "SOFT Z:%d Y:%d P:%d POLY:%d %s",
                 (int)(s_dist / 256), yawDeg, pitchDeg, visibleCount,
                 s_paused ? "[P]" : "");
    }
    OgreDrawText(10, 10, txt, 0xFFFFFFFF, 1.5f);

    const char* name = GetDemoShipDisplayName(shipIdx, nameBuf, sizeof(nameBuf));
    snprintf(txt, sizeof(txt), "#%d %s  H=mode T=texture N=ndbg ESC=ships",
             shipIdx, name);
    OgreDrawText(10, 36, txt, 0xFFAAAAAA, 1.5f);

    OgreEndFrame();

    const Uint32 elapsed = SDL_GetTicks() - frameStart;
    if (elapsed < kTargetFrameMs)
        SDL_Delay(kTargetFrameMs - elapsed);
}

} // namespace ogre
} // namespace jjffe
