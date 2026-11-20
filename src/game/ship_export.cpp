#include "jjffe/ship_export.h"

#include "jjffe/ab_auto_zoom.h"
#include "jjffe/contour_tessellate.h"
#include "jjffe/export_approval.h"
#include "jjffe/math.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/ship_face_semantic.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"
#include "jjffe/video.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jjffe {

namespace {

// Nested playable ships (15–71) stay inlined in the parent mesh; accessories
// (cockpits, pods, mines, …) go to children.json. Matches D3dLoadShipAsset.
bool ExportModularSpriteChild(int modelId)
{
    return modelId >= 0 && !(modelId >= 15 && modelId <= 71);
}

} // namespace

int32_t ShipExportCanonicalDist(const ShipModel* model)
{
    // Fit framing distance, clamped to the FUNC_001751 working band.
    // Default export uses the FAR end of that band for many ships (fitUnits≥1
    // → FP_ONE/2). Near (FP_ONE/8) is available via ShipExportOptions::captureDist
    // — do not assume near ⇒ more hull; some ships drop hull for Contours.
    if (!model) return FP_ONE / 2;
    const int fitUnits = AbAutoFitDistUnits(model->radius, model->scaleRelated);
    int32_t shipDist = fitUnits * (FP_ONE / 2);
    if (shipDist > FP_ONE / 2) shipDist = FP_ONE / 2;
    if (shipDist < FP_ONE / 8) shipDist = FP_ONE / 8;
    return shipDist;
}

const char* ShipExportName(int shipIndex)
{
    // Slugs for FFE models 0–71 (playable 15–71). See reference/ffed3d/Info/FFE Models.txt.
    static const char* const kNames[] = {
        "vector_font",           // 0
        "graphic_font",          // 1
        "times_font",            // 2
        "generic_missile",       // 3
        "proximity_mine",        // 4
        "proximity_mine_xb74",   // 5
        "homing_missile",        // 6
        "smart_missile",         // 7
        "naval_missile",         // 8
        "assault_missile",       // 9
        "assault_missile_mv2",   // 10
        "thargoid_missile",      // 11
        "mycoid_missile",        // 12
        "nuclear_missile",       // 13
        "escape_capsule",        // 14
        "shuttle",               // 15
        "stowmaster",            // 16
        "thargoid_fighter",      // 17
        "lifter",                // 18
        "osprey",                // 19
        "falcon",                // 20
        "hawk",                  // 21
        "kestrel",               // 22
        "eagle",                 // 23
        "eagle_mk2",             // 24
        "eagle_mk3",             // 25
        "sidewinder",            // 26
        "krait",                 // 27
        "gecko",                 // 28
        "adder",                 // 29
        "viper",                 // 30
        "saker_iii",             // 31
        "osprey_x",              // 32
        "merlin",                // 33
        "viper_mk2",             // 34
        "gyr",                   // 35
        "cobra_mk1",             // 36
        "moray",                 // 37
        "cobra_mk3",             // 38
        "constrictor",           // 39
        "asp_explorer",          // 40
        "lanner",                // 41
        "harris",                // 42
        "spar",                  // 43
        "wyvern",                // 44
        "skeet",                 // 45
        "turner",                // 46
        "lanner_ii",             // 47
        "harrier",               // 48
        "thargoid_piston",       // 49
        "thargoid_dual",         // 50
        "thargoid_saw",          // 51
        "thargoid_warship",      // 52
        "transporter",           // 53
        "lion_transport",        // 54
        "tiger_trader",          // 55
        "imperial_courier",      // 56
        "python",                // 57
        "imperial_trader",       // 58
        "anaconda",              // 59
        "puma_clipper",          // 60
        "boa",                   // 61
        "panther_clipper",       // 62
        "thargoid_flat",         // 63
        "tiercel",               // 64
        "imperial_explorer",     // 65
        "imra_command",          // 66
        "mantis",                // 67
        "griffin",               // 68
        "thargoid_transport",    // 69
        "lynx",                  // 70
        "long_range_cruiser",    // 71
    };
    const int n = (int)(sizeof(kNames) / sizeof(kNames[0]));
    if (shipIndex >= 0 && shipIndex < n) return kNames[shipIndex];
    return "ship";
}

namespace {

bool MkDir(const char* path)
{
    if (!path || !*path) return false;
    struct stat st{};
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

bool EnsureDir(const char* path)
{
    if (!path || !*path) return false;
    char tmp[1024];
    std::snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (!MkDir(tmp)) return false;
        *p = '/';
    }
    return MkDir(tmp);
}

void Be32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
}

uint32_t PalToArgb(uint8_t idx)
{
    if (!g_videoState)
        return 0xFFC0C0D0u;
    const uint8_t* p = g_videoState->palette;
    return 0xFF000000u | ((uint32_t)p[idx * 3 + 0] << 16) |
           ((uint32_t)p[idx * 3 + 1] << 8) | p[idx * 3 + 2];
}

uint32_t FaceFlatArgb(const TransformedPoly& p, const ShipModel* model,
                      const int32_t* shipRotLit)
{
    const uint16_t ti = p.texInfo;
    uint8_t lit = GetFaceColor(model, p.normalIdx, 0x08, shipRotLit);
    if (lit < 2) lit = 2;
    if (ti == 0 || ti == 0x2000) return 0xFF000000u;
    if (ti == 0x3000)
        return PalToArgb(ShadeRgbNibbleToPal(p.faceLight[0], p.faceLight[1],
                                             p.faceLight[2]));
    // Lifter #18 pad decks (and any 0x4016 Contour fill): ASM 001721 shade0
    // is VGA22 on both skins. GetFaceColor lights the lower deck to near-white.
    if (ti == 0x4016) return PalToArgb(22);
    if ((ti >> 14) == 0) {
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
        if (pal == 0 && ti == 0x1000) return PalToArgb(lit);
        if (p.afterSpecialPoly && pal == 0) return 0xFF000000u;
        return PalToArgb(pal);
    }
    return PalToArgb(lit);
}

// True when the triangle faces the origin (Blender would show the inside).
// Does not move verts — emitFan flips the index order.
bool FaceWindingInward(const ShipExportVert* tv, int n)
{
    if (!tv || n < 3) return false;
    const float nx = (tv[1].y - tv[0].y) * (tv[2].z - tv[0].z) -
                     (tv[1].z - tv[0].z) * (tv[2].y - tv[0].y);
    const float ny = (tv[1].z - tv[0].z) * (tv[2].x - tv[0].x) -
                     (tv[1].x - tv[0].x) * (tv[2].z - tv[0].z);
    const float nz = (tv[1].x - tv[0].x) * (tv[2].y - tv[0].y) -
                     (tv[1].y - tv[0].y) * (tv[2].x - tv[0].x);
    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < n; i++) {
        cx += tv[i].x;
        cy += tv[i].y;
        cz += tv[i].z;
    }
    const float inv = 1.f / (float)n;
    return (nx * cx + ny * cy + nz * cz) * inv < 0.f;
}

bool DegenerateTri(const ShipExportVert& a, const ShipExportVert& b,
                   const ShipExportVert& c)
{
    return FaceExportTriDegenerate(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
}

bool SliverTri(const ShipExportVert& a, const ShipExportVert& b,
               const ShipExportVert& c)
{
    return FaceExportTriSliver(a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
}

struct WeldKey {
    int spriteChild = -1;
    int vertIdx = -1;
    int mat = 0;
    int side = 0;  // 0 front, 1 back (double-sided)
    uint8_t normalIdx = 0;
    // Quantised vn — Pair-mirror export may negate authored normals per face
    // while sharing normalIdx; without this, weld steals the first face's vn.
    int8_t nxq = 0, nyq = 0, nzq = 0;
    int16_t uq = 0, vq = 0;
    int32_t xq = 0, yq = 0, zq = 0;

    bool operator<(const WeldKey& o) const
    {
        if (spriteChild != o.spriteChild) return spriteChild < o.spriteChild;
        if (vertIdx != o.vertIdx) return vertIdx < o.vertIdx;
        if (mat != o.mat) return mat < o.mat;
        if (side != o.side) return side < o.side;
        if (normalIdx != o.normalIdx) return normalIdx < o.normalIdx;
        if (nxq != o.nxq) return nxq < o.nxq;
        if (nyq != o.nyq) return nyq < o.nyq;
        if (nzq != o.nzq) return nzq < o.nzq;
        if (uq != o.uq) return uq < o.uq;
        if (vq != o.vq) return vq < o.vq;
        if (xq != o.xq) return xq < o.xq;
        if (yq != o.yq) return yq < o.yq;
        return zq < o.zq;
    }
};

enum class TriBucket { Hull, Punch, Decal, Contour, Line };

bool ExpandLineRibbon(WorldFace& f)
{
    if (f.vertexCount != 2) return false;
    const WorldFaceVert a = f.verts[0];
    const WorldFaceVert b = f.verts[1];
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float edgeLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Stable export frame (Y-up, Z-forward). Camera-facing cards from the
    // D3D/Ogre viewers freeze as vertical fins once the mesh leaves that camera.
    float cx = dy * 0.0f - dz * 1.0f;  // cross(edge, +Y)
    float cy = dz * 0.0f - dx * 0.0f;
    float cz = dx * 1.0f - dy * 0.0f;
    float clen = std::sqrt(cx * cx + cy * cy + cz * cz);
    if (clen < 1e-6f) {
        cx = dy * 1.0f - dz * 0.0f;  // cross(edge, +Z)
        cy = dz * 0.0f - dx * 1.0f;
        cz = dx * 0.0f - dy * 0.0f;
        clen = std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    if (clen < 1e-6f) return false;
    const float half = std::min(0.010f, std::max(0.002f, edgeLen * 0.02f));
    cx = cx / clen * half;
    cy = cy / clen * half;
    cz = cz / clen * half;
    f.verts[0] = a;
    f.verts[0].x += cx;
    f.verts[0].y += cy;
    f.verts[0].z += cz;
    f.verts[1] = b;
    f.verts[1].x += cx;
    f.verts[1].y += cy;
    f.verts[1].z += cz;
    f.verts[2] = b;
    f.verts[2].x -= cx;
    f.verts[2].y -= cy;
    f.verts[2].z -= cz;
    f.verts[3] = a;
    f.verts[3].x -= cx;
    f.verts[3].y -= cy;
    f.verts[3].z -= cz;
    const float nx = cx / half, ny = cy / half, nz = cz / half;
    for (int i = 0; i < 4; i++) {
        f.verts[i].nx = nx;
        f.verts[i].ny = ny;
        f.verts[i].nz = nz;
    }
    f.vertexCount = 4;
    return true;
}

struct MatKey {
    bool textured = false;
    int layer = 0;  // 0 hull, 1 punch, 2 contour, 3 line, 4 decal
    uint16_t bmpIndex = 0;
    uint16_t matIndex = 0;
    int8_t flR = 0, flG = 0, flB = 0;
    uint8_t applyFL = 0;
    uint32_t argb = 0;

    bool operator<(const MatKey& o) const
    {
        if (textured != o.textured) return textured < o.textured;
        if (layer != o.layer) return layer < o.layer;
        if (bmpIndex != o.bmpIndex) return bmpIndex < o.bmpIndex;
        if (matIndex != o.matIndex) return matIndex < o.matIndex;
        if (flR != o.flR) return flR < o.flR;
        if (flG != o.flG) return flG < o.flG;
        if (flB != o.flB) return flB < o.flB;
        if (applyFL != o.applyFL) return applyFL < o.applyFL;
        return argb < o.argb;
    }
};

const char* LayerPrefix(int layer)
{
    if (layer == 1) return "punch_";
    if (layer == 2) return "contour_";
    if (layer == 3) return "line_";
    if (layer == 4) return "decal_";
    return "";
}

[[maybe_unused]] void WriteChunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
{
    uint8_t be[4];
    Be32(be, len);
    fwrite(be, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len && data) fwrite(data, 1, len, f);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    if (len && data) crc = crc32(crc, data, len);
    Be32(be, (uint32_t)crc);
    fwrite(be, 1, 4, f);
}

void WriteChunkMem(std::vector<uint8_t>& out, const char* type, const uint8_t* data,
                   uint32_t len)
{
    uint8_t be[4];
    Be32(be, len);
    out.insert(out.end(), be, be + 4);
    out.insert(out.end(), type, type + 4);
    if (len && data) out.insert(out.end(), data, data + len);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    if (len && data) crc = crc32(crc, data, len);
    Be32(be, (uint32_t)crc);
    out.insert(out.end(), be, be + 4);
}

} // namespace

bool EncodeRgbaPng(int width, int height, const uint32_t* argb,
                   std::vector<uint8_t>* out)
{
    if (!out || !argb || width <= 0 || height <= 0) return false;
    out->clear();
    const size_t rowBytes = (size_t)width * 4 + 1;
    std::vector<uint8_t> raw(rowBytes * (size_t)height);
    for (int y = 0; y < height; y++) {
        raw[(size_t)y * rowBytes] = 0;
        for (int x = 0; x < width; x++) {
            const uint32_t px = argb[(size_t)y * (size_t)width + (size_t)x];
            uint8_t* d = &raw[(size_t)y * rowBytes + 1 + (size_t)x * 4];
            d[0] = (uint8_t)(px >> 16);
            d[1] = (uint8_t)(px >> 8);
            d[2] = (uint8_t)px;
            d[3] = (uint8_t)(px >> 24);
        }
    }
    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<uint8_t> zbuf(zlen);
    if (compress(zbuf.data(), &zlen, raw.data(), (uLong)raw.size()) != Z_OK)
        return false;

    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out->insert(out->end(), sig, sig + 8);
    uint8_t ihdr[13];
    Be32(ihdr + 0, (uint32_t)width);
    Be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    WriteChunkMem(*out, "IHDR", ihdr, 13);
    WriteChunkMem(*out, "IDAT", zbuf.data(), (uint32_t)zlen);
    WriteChunkMem(*out, "IEND", nullptr, 0);
    return true;
}

bool WriteRgbaPng(const char* path, int width, int height, const uint32_t* argb)
{
    std::vector<uint8_t> png;
    if (!EncodeRgbaPng(width, height, argb, &png)) return false;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

bool ShouldHarvestContourCurve(const TransformedPoly& p)
{
    if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) return false;
    if (IsContourGlintPoly(p)) return false;
    if (p.texInfo == 0x1000 || p.texInfo == 0x4028) return false;
    return p.vertexCount == 4 || p.vertexCount == 2;
}

ShipExportVert WorldFixedToExport(int32_t x, int32_t y, int32_t z, float originZ)
{
    ShipExportVert v;
    v.x = (float)x / (float)FP_ONE;
    v.y = (float)y / (float)FP_ONE;
    v.z = (float)z / (float)FP_ONE - originZ;
    return v;
}

bool CurveCollapsed(const ShipExportPolyline& pl)
{
    if (pl.pts.size() < 2) return true;
    float mx = 0.f;
    for (size_t i = 1; i < pl.pts.size(); i++) {
        const float dx = pl.pts[i].x - pl.pts[0].x;
        const float dy = pl.pts[i].y - pl.pts[0].y;
        const float dz = pl.pts[i].z - pl.pts[0].z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > mx) mx = d;
    }
    return mx < 1e-5f;
}

// Cubic through the four world control points. Do not use
// ContourTessellator::EvaluateBezier3D here — that DDA snaps handles
// with >>16, so a 0.22-unit wing tip becomes a 1.0-unit lattice point
// and the OBJ grows a ±1 world-unit wire cage.
void SampleCubicBezierWorld(const ShipExportVert& p0, const ShipExportVert& p1,
                            const ShipExportVert& p2, const ShipExportVert& p3,
                            int nSeg, ShipExportPolyline* pl)
{
    if (nSeg < 2) nSeg = 2;
    pl->pts.reserve((size_t)nSeg + 1);
    for (int i = 0; i <= nSeg; i++) {
        const float t = (float)i / (float)nSeg;
        const float u = 1.f - t;
        const float b0 = u * u * u;
        const float b1 = 3.f * u * u * t;
        const float b2 = 3.f * u * t * t;
        const float b3 = t * t * t;
        ShipExportVert v;
        v.x = b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x;
        v.y = b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y;
        v.z = b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z;
        pl->pts.push_back(v);
    }
}

void HarvestContourPolylines(const TransformedPoly* polys, int nPoly,
                             float originZ, ShipExportMesh* out)
{
    if (!polys || !out) return;
    for (int i = 0; i < nPoly; i++) {
        const TransformedPoly& p = polys[i];
        if (!ShouldHarvestContourCurve(p)) continue;
        ShipExportPolyline pl;
        pl.texInfo = p.texInfo;
        pl.contourId = p.contourId;
        if (p.vertexCount == 4) {
            SampleCubicBezierWorld(
                WorldFixedToExport(p.verts[0].worldX, p.verts[0].worldY,
                                   p.verts[0].worldZ, originZ),
                WorldFixedToExport(p.verts[1].worldX, p.verts[1].worldY,
                                   p.verts[1].worldZ, originZ),
                WorldFixedToExport(p.verts[2].worldX, p.verts[2].worldY,
                                   p.verts[2].worldZ, originZ),
                WorldFixedToExport(p.verts[3].worldX, p.verts[3].worldY,
                                   p.verts[3].worldZ, originZ),
                16, &pl);
        } else {
            pl.pts.push_back(WorldFixedToExport(p.verts[0].worldX, p.verts[0].worldY,
                                                p.verts[0].worldZ, originZ));
            pl.pts.push_back(WorldFixedToExport(p.verts[1].worldX, p.verts[1].worldY,
                                                p.verts[1].worldZ, originZ));
        }
        // Dense bezier samples collapse when control points stack.
        {
            std::vector<ShipExportVert> dedup;
            dedup.reserve(pl.pts.size());
            for (const ShipExportVert& v : pl.pts) {
                if (!dedup.empty()) {
                    const float dx = v.x - dedup.back().x;
                    const float dy = v.y - dedup.back().y;
                    const float dz = v.z - dedup.back().z;
                    if (dx * dx + dy * dy + dz * dz < 1e-12f) continue;
                }
                dedup.push_back(v);
            }
            pl.pts.swap(dedup);
        }
        if (CurveCollapsed(pl)) continue;
        out->contourPtCount += (int)pl.pts.size();
        out->curves.push_back(std::move(pl));
    }
    out->contourCurveCount = (int)out->curves.size();
}

void HarvestSpriteChildren(const ShipModel* model, const MeshData& mesh,
                           const int32_t* shipRot, const int32_t* shipPos,
                           const Camera3D* camera, float originZ,
                           ShipExportMesh* out)
{
    if (!model || !out) return;
    SpriteChildInstance kids[64];
    const int nKids = CollectSpriteChildInstances(
        model, mesh, shipRot, shipPos, camera, (int)model->scaleRelated, kids, 64);
    out->sprites.reserve((size_t)nKids);
    for (int i = 0; i < nKids; i++) {
        if (!ExportModularSpriteChild(kids[i].modelId)) continue;
        ShipExportSpriteChild c;
        c.modelId = kids[i].modelId;
        c.pos[0] = (float)kids[i].pos[0] / (float)FP_ONE;
        c.pos[1] = (float)kids[i].pos[1] / (float)FP_ONE;
        c.pos[2] = (float)kids[i].pos[2] / (float)FP_ONE - originZ;
        // Attach frames are Sin32/Q31 (~±2^31 = ±1), same as MatBuildYZT.
        for (int r = 0; r < 9; r++)
            c.rot[r] = (float)kids[i].rot[r] / 2147483648.0f;
        c.scaleRelated = kids[i].scaleRelated;
        out->sprites.push_back(c);
    }
    out->spriteChildCount = (int)out->sprites.size();
    if (out->spriteChildCount > 0) out->hasSpriteChild = true;
}

// Identity-pose model vert → export space (same frame as hull: fixed>>16).
bool ModelVertToExport(const ShipModel* model, int vertIdx, float* xyz)
{
    if (!model || !xyz) return false;
    ShipVertex sv{};
    if (!LookupShipVertex(model, vertIdx, sv)) return false;
    int shift = (int)model->scaleRelated;
    if (shift < 0) shift = 0;
    if (shift > 24) shift = 24;
    // ASM shl on the raw word; shift the unsigned view so a negative component
    // is not UB (UBSan -18).
    xyz[0] = (float)static_cast<int32_t>(static_cast<uint32_t>(sv.x) << shift) / (float)FP_ONE;
    xyz[1] = (float)static_cast<int32_t>(static_cast<uint32_t>(sv.y) << shift) / (float)FP_ONE;
    xyz[2] = (float)static_cast<int32_t>(static_cast<uint32_t>(sv.z) << shift) / (float)FP_ONE;
    return true;
}

void EmitterColorFromTex(uint16_t texInfo, float rgb[3])
{
    // FUNC_001692 flat-RGB family (0x2xxx): thruster glow uses the nibble
    // colour directly (0x20ee → cyan). Palette lookup maps that word to a
    // near-black green and makes D3D emitters look like debug slabs.
    if ((texInfo & 0xF000u) == 0x2000u) {
        rgb[0] = ((texInfo >> 8) & 0xF) / 15.0f;
        rgb[1] = ((texInfo >> 4) & 0xF) / 15.0f;
        rgb[2] = (texInfo & 0xF) / 15.0f;
        return;
    }
    rgb[0] = rgb[1] = rgb[2] = 0.85f;
    ShipMaterialInfo mat{};
    if (!ShipResolveMaterial(texInfo, &mat) || mat.shadeCount <= 0) return;
    const uint8_t palIdx =
        ShadeRgbNibbleToPal(mat.shades[0][0], mat.shades[0][1], mat.shades[0][2]);
    if (g_videoState) {
        const uint8_t* pal = g_videoState->palette;
        rgb[0] = pal[palIdx * 3 + 0] / 255.0f;
        rgb[1] = pal[palIdx * 3 + 1] / 255.0f;
        rgb[2] = pal[palIdx * 3 + 2] / 255.0f;
    }
}

void HarvestEmitters(const ShipModel* model, const MeshData& mesh, ShipExportMesh* out)
{
    if (!model || !out) return;
    const uint8_t* glowStore = GetGlowVertStore();
    for (const MeshPoly& poly : mesh.polygons) {
        if (poly.type == MeshPolyType::NavLight) {
            float xyz[3];
            if (!ModelVertToExport(model, poly.vertexIdx[0], xyz)) continue;
            ShipExportEmitter e;
            e.kind = ShipExportEmitterKind::NavLight;
            e.x = xyz[0];
            e.y = xyz[1];
            e.z = xyz[2];
            e.vertexIdx = poly.vertexIdx[0];
            e.blink = (poly.specialParam & 0x80) != 0;
            e.texInfo = poly.texInfo;
            EmitterColorFromTex(poly.texInfo, e.rgb);
            out->emitters.push_back(e);
        } else if (poly.type == MeshPolyType::Thruster && poly.vertexCount >= 2) {
            float a[3], b[3];
            if (!ModelVertToExport(model, poly.vertexIdx[0], a)) continue;
            if (!ModelVertToExport(model, poly.vertexIdx[1], b)) continue;
            ShipExportEmitter e;
            e.kind = ShipExportEmitterKind::Thruster;
            e.x = a[0];
            e.y = a[1];
            e.z = a[2];
            e.x1 = b[0];
            e.y1 = b[1];
            e.z1 = b[2];
            e.vertexIdx = poly.vertexIdx[0];
            e.vertexIdx1 = poly.vertexIdx[1];
            e.size = (float)poly.specialParam;
            e.texInfo = poly.texInfo;
            EmitterColorFromTex(poly.texInfo, e.rgb);
            out->emitters.push_back(e);
        } else if (poly.type == MeshPolyType::GlowPoints) {
            auto pushGlow = [&](int vidx) {
                float xyz[3];
                if (!ModelVertToExport(model, vidx, xyz)) return;
                ShipExportEmitter e;
                e.kind = ShipExportEmitterKind::Glow;
                e.x = xyz[0];
                e.y = xyz[1];
                e.z = xyz[2];
                e.vertexIdx = vidx;
                e.texInfo = poly.texInfo;
                e.size = (float)poly.specialParam;
                EmitterColorFromTex(poly.texInfo, e.rgb);
                out->emitters.push_back(e);
            };
            pushGlow(poly.vertexIdx[0]);
            if (!glowStore) continue;
            for (int i = 0; i < poly.glowVertCount; i++) {
                const int8_t vidx =
                    static_cast<int8_t>(glowStore[poly.glowVertOffset + i]);
                if (vidx < 0) continue;
                pushGlow(static_cast<uint8_t>(vidx));
            }
        }
    }
    out->emitterCount = (int)out->emitters.size();
}

bool BuildExportMesh(int shipIndex, ShipExportMesh* out)
{
    return BuildExportMesh(shipIndex, out, ShipExportOptions{});
}

bool BuildExportMesh(int shipIndex, ShipExportMesh* out,
                     const ShipExportOptions& opt)
{
    if (!out) return false;
    *out = ShipExportMesh{};
    out->shipIndex = shipIndex;
    out->name = ShipExportName(shipIndex);

    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) {
        out->error = "missing model";
        return false;
    }
    const uint8_t* meshPtr = GetShipModelPointer(model->meshDataOffset);
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;
    if (!meshPtr) {
        out->error = "missing mesh pointer";
        return false;
    }

    const int32_t shipDist =
        (opt.captureDist != 0) ? opt.captureDist : ShipExportCanonicalDist(model);
    out->shipDist = shipDist;
    int32_t shipPos[3] = {0, 0, shipDist};

    Mat33 rotMat{};
    MatBuildYZT(&rotMat, 0, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];

    Camera3D camera;
    CameraInit(&camera);

    const bool prevHarvest = GetMeshHarvestThrusters();
    const int32_t prevDist = GetMeshParseDistance();
    const int32_t prevScale = GetMeshParseScaleRelated();
    SetMeshHarvestThrusters(true);
    SetMeshHarvestThrustersBlind(true); // all 0x0009 emitters
    SetMeshParseScaleRelated((int32_t)model->scaleRelated);
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    // Soft-filtered Sprite749 children (e.g. Stowmaster #180 textured pods) stay
    // out of the mesh — matches painter/ASM. Soft-kept accessories (Eagle #184)
    // remain tagged and go to children.json via HarvestSpriteChildren.
    SetVertexCacheBankBase(0);
    SetVertexBankVerts(0);
    const int prevParseModel = GetMeshParseModelId();
    SetMeshParseModelId(shipIndex);

    MeshData mesh = ParseMeshBytecode(meshPtr, meshSize);

    SetMeshParseModelId(prevParseModel);
    SetMeshHarvestThrustersBlind(false);
    SetMeshHarvestThrusters(prevHarvest);
    SetMeshParseDistance(prevDist);
    SetMeshParseScaleRelated(prevScale);

    if (!mesh.valid) {
        out->error = "parse failed";
        return false;
    }

    std::vector<TransformedPoly> polys(8192);
    const int nPoly = TransformShipVertices(model, mesh, shipRot, shipPos, &camera,
                                            polys.data(), (int)polys.size());
    if (nPoly >= (int)polys.size()) out->truncated = true;

    std::vector<WorldFace> faces(32768);
    const int nHullFace = BuildWorldShipFaces(polys.data(), nPoly, model, shipRotLit,
                                              faces.data(), (int)faces.size(),
                                              /*includeHidden=*/true);
    const int nFace = nHullFace;
    if (nHullFace >= (int)faces.size()) out->truncated = true;
    // ContourHull tessellation stays paused (O0 needles / O1 sails). Curves
    // harvested below. Contour ComplexFill ≥3 are still emitted as hull tris
    // later (soft C721 fill — e.g. Viper #30 rear plane).
    out->contourPlanarity = 0.f;
    out->contourTriCount = 0;

    const float originZ = (float)shipDist / (float)FP_ONE;
    HarvestContourPolylines(polys.data(), nPoly, originZ, out);
    HarvestSpriteChildren(model, mesh, shipRot, shipPos, &camera, originZ, out);
    HarvestEmitters(model, mesh, out);
    const bool missileFlat = FaceSemanticMissileFlat(polys.data(), nPoly);
    std::map<MatKey, int> matMap;
    std::map<WeldKey, int> weld;

    auto addMat = [&](const MatKey& key, uint16_t texInfo) -> int {
        auto it = matMap.find(key);
        if (it != matMap.end()) return it->second;
        ShipExportMaterial m;
        m.texInfo = texInfo;
        m.punch = (key.layer == 1);
        m.decal = (key.layer == 4);
        m.textured = key.textured;
        m.bmpIndex = key.bmpIndex;
        m.matIndex = key.matIndex;
        m.kdArgb = key.argb ? key.argb : 0xFFC0C0D0u;
        const char* pre = LayerPrefix(key.layer);
        if (key.textured) {
            char png[80];
            std::snprintf(png, sizeof(png), "mat_%u_%u_%d_%d_%d_%u.png",
                          key.bmpIndex, key.matIndex, (int)key.flR, (int)key.flG,
                          (int)key.flB, (unsigned)key.applyFL);
            m.pngName = png;
            m.name = std::string(pre) + (key.layer == 0 ? "tex_" : "") + png;
            m.name.resize(m.name.size() - 4); // drop .png
        } else {
            char nm[64];
            std::snprintf(nm, sizeof(nm), "%sflat_%04x_%08x", pre, texInfo, key.argb);
            m.name = nm;
        }
        const int idx = (int)out->materials.size();
        out->materials.push_back(std::move(m));
        matMap.emplace(key, idx);
        return idx;
    };

    auto weldVert = [&](const ShipExportVert& vert, const WeldKey& key) -> uint32_t {
        auto it = weld.find(key);
        if (it != weld.end()) return (uint32_t)it->second;
        const uint32_t id = (uint32_t)out->verts.size();
        out->verts.push_back(vert);
        weld.emplace(key, (int)id);
        return id;
    };

    auto emitTri = [&](const ShipExportVert v[3], const WeldKey keys[3], int mat,
                       TriBucket bucket, bool count, int sourceIndex,
                       int fanOrdinal, bool reverseSide) {
        if (DegenerateTri(v[0], v[1], v[2])) return;
        if (bucket == TriBucket::Contour && SliverTri(v[0], v[1], v[2])) return;
        out->indices.push_back(weldVert(v[0], keys[0]));
        out->indices.push_back(weldVert(v[1], keys[1]));
        out->indices.push_back(weldVert(v[2], keys[2]));
        out->triMaterial.push_back(mat);
        ShipExportTriMeta meta;
        meta.sourceOrder = sourceIndex;
        meta.fanOrdinal = (uint8_t)(fanOrdinal & 0xff);
        meta.reverseSide = reverseSide;
        if (sourceIndex >= 0) {
            meta.primitiveId =
                ShipExportMakePrimitiveId(sourceIndex, fanOrdinal, reverseSide);
        } else {
            const uint32_t seq = (uint32_t)out->triMeta.size();
            meta.primitiveId = kShipExportPrimGeneratedBase | (seq & 0x7fffffffu);
        }
        out->triMeta.push_back(meta);
        if (!count) return;
        switch (bucket) {
        case TriBucket::Punch: out->punchTriCount++; break;
        case TriBucket::Decal: out->decalTriCount++; break;
        case TriBucket::Contour: out->contourTriCount++; break;
        case TriBucket::Line: out->lineTriCount++; break;
        default: out->hullTriCount++; break;
        }
    };

    auto makeKeys = [&](const ShipExportVert tri[3], const WorldFace& f,
                        const TransformedPoly* src, int mat, int side,
                        const int fan[3], WeldKey keys[3]) {
        for (int k = 0; k < 3; k++) {
            keys[k].spriteChild = f.spriteChildModel;
            keys[k].mat = mat;
            keys[k].side = side;
            keys[k].normalIdx = f.normalIdx;
            keys[k].nxq = (int8_t)std::lround(std::max(-1.f, std::min(1.f, tri[k].nx)) * 127.f);
            keys[k].nyq = (int8_t)std::lround(std::max(-1.f, std::min(1.f, tri[k].ny)) * 127.f);
            keys[k].nzq = (int8_t)std::lround(std::max(-1.f, std::min(1.f, tri[k].nz)) * 127.f);
            keys[k].uq = (int16_t)std::lround(tri[k].u * 4096.0f);
            keys[k].vq = (int16_t)std::lround(tri[k].v * 4096.0f);
            const int srcVi = fan[k];
            if (src && srcVi < src->vertexCount)
                keys[k].vertIdx = src->vertexIdx[srcVi];
            else
                keys[k].vertIdx = -1;
            if (keys[k].vertIdx < 0 || side != 0) {
                keys[k].xq = (int32_t)std::lround(tri[k].x * 1024.0f);
                keys[k].yq = (int32_t)std::lround(tri[k].y * 1024.0f);
                keys[k].zq = (int32_t)std::lround(tri[k].z * 1024.0f);
                if (side != 0) keys[k].vertIdx = -1;
            }
        }
    };

    auto emitFan = [&](const ShipExportVert* tv, int vertexCount, const WorldFace& f,
                       const TransformedPoly* src, int mat, TriBucket bucket,
                       bool doubleSided, bool flipWinding) {
        const int nTri = vertexCount == 4 ? 2 : 1;
        const int fanFwd[2][3] = {{0, 1, 2}, {0, 2, 3}};
        const int fanRev[2][3] = {{0, 2, 1}, {0, 3, 2}};
        const int (*fan)[3] = flipWinding ? fanRev : fanFwd;
        const int sourceIndex = f.sourceIndex;
        for (int t = 0; t < nTri; t++) {
            ShipExportVert tri[3] = {tv[fan[t][0]], tv[fan[t][1]], tv[fan[t][2]]};
            WeldKey keys[3];
            makeKeys(tri, f, src, mat, 0, fan[t], keys);
            emitTri(tri, keys, mat, bucket, /*count=*/true, sourceIndex, t,
                    /*reverseSide=*/false);
            if (doubleSided) {
                ShipExportVert rev[3] = {tri[0], tri[2], tri[1]};
                for (int k = 0; k < 3; k++) {
                    rev[k].nx = -rev[k].nx;
                    rev[k].ny = -rev[k].ny;
                    rev[k].nz = -rev[k].nz;
                }
                const int rfan[3] = {fan[t][0], fan[t][2], fan[t][1]};
                WeldKey rkeys[3];
                makeKeys(rev, f, src, mat, 1, rfan, rkeys);
                emitTri(rev, rkeys, mat, bucket, /*count=*/false, sourceIndex, t,
                        /*reverseSide=*/true);
            }
        }
    };

    // Independent T7 count: one-sided hull+punch + Contour ComplexFill pieces
    // (exported as hull; ContourHull tessellation remains paused).
    int expected = 0;
    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        // Modular Sprite749 accessories → children.json (not parent hull).
        if (ExportModularSpriteChild(f.spriteChildModel)) continue;
        const TransformedPoly* src =
            (f.sourceIndex >= 0 && f.sourceIndex < nPoly) ? &polys[f.sourceIndex]
                                                          : nullptr;
        bool contourFill = false;
        if (f.kind == WorldFaceKind::ScreenHybrid &&
            f.type == MeshPolyType::ComplexFill && f.contourId >= 0 &&
            f.vertexCount >= 3 && src) {
            for (int v = 0; v < src->vertexCount && v < 4; v++) {
                if (src->verts[v].depth > 0x40 || src->verts[v].worldZ > 0x40) {
                    contourFill = true;
                    break;
                }
            }
        }
        if (!contourFill) {
            if (f.kind != WorldFaceKind::Hull && f.kind != WorldFaceKind::Punch)
                continue;
        }
        if (f.vertexCount < 3) continue;
        ShipExportVert tv[4];
        for (int v = 0; v < f.vertexCount && v < 4; v++) {
            tv[v].x = f.verts[v].x;
            tv[v].y = f.verts[v].y;
            tv[v].z = f.verts[v].z - originZ;
        }
        const int nTri = f.vertexCount == 4 ? 2 : 1;
        const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int t = 0; t < nTri; t++) {
            if (!DegenerateTri(tv[fan[t][0]], tv[fan[t][1]], tv[fan[t][2]]))
                expected++;
        }
    }
    out->expectedTriCount = expected;

    auto contourFillHasWorld = [](const TransformedPoly& p) -> bool {
        // Mirror PolyWorldValid: keep if any vert is past ASM near plane.
        for (int v = 0; v < p.vertexCount && v < 4; v++) {
            if (p.verts[v].depth > 0x40 || p.verts[v].worldZ > 0x40)
                return true;
        }
        return false;
    };

    for (int i = 0; i < nFace; i++) {
        WorldFace f = faces[i];
        if (ExportModularSpriteChild(f.spriteChildModel)) continue;
        const TransformedPoly* src =
            (f.sourceIndex >= 0 && f.sourceIndex < nPoly) ? &polys[f.sourceIndex]
                                                          : nullptr;

        // Soft Contour fill (ComplexFill, contourId≥0) is ScreenHybrid here —
        // ContourHull tessellation remains paused. Export those authored tris
        // as hull so rear/body Contour planes (Viper #30, …) are not holes.
        const bool contourFillPiece =
            f.kind == WorldFaceKind::ScreenHybrid &&
            f.type == MeshPolyType::ComplexFill && f.contourId >= 0 &&
            f.vertexCount >= 3 && src && contourFillHasWorld(*src);

        if (!contourFillPiece) {
            if (f.kind == WorldFaceKind::ScreenHybrid) continue;
            if (f.kind == WorldFaceKind::ContourHull) continue;
            // Canon-pose PolyWorldValid drops back-facing rear plates (#39).
            if (f.kind != WorldFaceKind::Hull && f.kind != WorldFaceKind::Punch)
                continue;
        }

        const bool isPunch = !contourFillPiece && (f.kind == WorldFaceKind::Punch);
        const bool isHull = contourFillPiece || (f.kind == WorldFaceKind::Hull);
        if (!isPunch && !isHull) continue;

        const FaceSemantic sem = contourFillPiece
                                     ? FaceSemantic::Hull
                                     : ClassifyFaceSemantic(f, missileFlat);
        bool isLine = (sem == FaceSemantic::Line);
        if (isLine) {
            if (!ExpandLineRibbon(f)) continue;
        }
        if (f.vertexCount < 3) continue;

        out->worldFaceCount++;
        if (f.spriteChildModel >= 0) out->hasSpriteChild = true;

        const bool isDecal = (sem == FaceSemantic::Overlay);
        TriBucket bucket = TriBucket::Hull;
        int layer = 0;
        if (isPunch) {
            bucket = TriBucket::Punch;
            layer = 1;
        } else if (isDecal) {
            bucket = TriBucket::Decal;
            layer = 4;
        } else if (isLine) {
            bucket = TriBucket::Line;
            layer = 3;
        }

        float u[4] = {}, v[4] = {};
        // ComplexFill is ASM 001721 shade, not FUNC_001644 UV-mapped hull skin
        // (Falcon #20 rear hex was a texture starburst).
        bool textured =
            !isLine && !contourFillPiece && ShipTexInfoIsTextured(f.texInfo);
        ShipMaterialInfo minfo{};
        ShipBakedTexture baked;
        bool haveBake = false;
        if (textured && ShipResolveMaterial(f.texInfo, &minfo)) {
            const int flR = src ? src->faceLight[0] : f.faceLight[0];
            const int flG = src ? src->faceLight[1] : f.faceLight[1];
            const int flB = src ? src->faceLight[2] : f.faceLight[2];
            if (BakeShipTextureRGBA(f.texInfo, flR, flG, flB, true, &baked) &&
                baked.width >= 2) {
                haveBake = true;
                if (src)
                    ShipAssignNormalizedTexUVs(*src, minfo.texWord, baked.width, u, v);
                else {
                    u[0] = 0;
                    v[0] = 0;
                    u[1] = 1;
                    v[1] = 0;
                    u[2] = 1;
                    v[2] = 1;
                    u[3] = 0;
                    v[3] = 1;
                }
            } else {
                textured = false;
            }
        } else {
            textured = false;
        }

        MatKey mk;
        mk.layer = layer;
        mk.textured = textured;
        if (textured) {
            const bool applyFL = (f.texInfo & 0x1000) != 0;
            mk.bmpIndex = baked.bmpIndex;
            mk.matIndex = baked.matIndex;
            mk.flR = applyFL ? (src ? src->faceLight[0] : f.faceLight[0]) : 0;
            mk.flG = applyFL ? (src ? src->faceLight[1] : f.faceLight[1]) : 0;
            mk.flB = applyFL ? (src ? src->faceLight[2] : f.faceLight[2]) : 0;
            mk.applyFL = applyFL ? 1 : 0;
            mk.argb = 0xFFFFFFFFu;
        } else {
            mk.argb = src ? FaceFlatArgb(*src, model, shipRotLit) : 0xFFC0C0D0u;
            if (isPunch && mk.argb == 0xFFC0C0D0u) mk.argb = 0xFF101010u;
            if (isLine && mk.argb == 0xFFC0C0D0u) mk.argb = 0xFFB0B0B8u;
        }
        const int mat = addMat(mk, f.texInfo);
        if (haveBake && out->materials[mat].baked.argb.empty()) {
            out->materials[mat].baked = std::move(baked);
            out->materials[mat].transparent = out->materials[mat].baked.transparent;
        }

        ShipExportVert tv[4];
        for (int vi = 0; vi < f.vertexCount && vi < 4; vi++) {
            tv[vi].x = f.verts[vi].x;
            tv[vi].y = f.verts[vi].y;
            tv[vi].z = f.verts[vi].z - originZ;
            tv[vi].nx = f.verts[vi].nx;
            tv[vi].ny = f.verts[vi].ny;
            tv[vi].nz = f.verts[vi].nz;
            tv[vi].u = textured ? u[vi] : 0.f;
            tv[vi].v = textured ? v[vi] : 0.f;
        }
        // Structural Pair handedness (export-only — ASM keeps same-order ^1).
        // When the mirror half's winding is antiparallel to authored GetNormal,
        // swap last two verts. Conditional: bytecode winding is unconstrained
        // under FUNC_001481, so some primaries already oppose — their ^1 twins
        // then *agree*, and an unconditional swap would invent false residuals.
        const bool pairCandidate =
            !isLine && (f.normalIdx & 1) &&
            (f.type == MeshPolyType::TexTriPair ||
             f.type == MeshPolyType::TexQuadPair) &&
            f.vertexCount >= 3;

        auto windingAuthoredCos = [&](float* outCos) -> bool {
            const float ux = tv[1].x - tv[0].x, uy = tv[1].y - tv[0].y,
                        uz = tv[1].z - tv[0].z;
            const float vx = tv[2].x - tv[0].x, vy = tv[2].y - tv[0].y,
                        vz = tv[2].z - tv[0].z;
            const float wx = uy * vz - uz * vy;
            const float wy = uz * vx - ux * vz;
            const float wz = ux * vy - uy * vx;
            const float w2 = wx * wx + wy * wy + wz * wz;
            const float ax = tv[0].nx, ay = tv[0].ny, az = tv[0].nz;
            const float n2 = ax * ax + ay * ay + az * az;
            if (!f.authoredNormal || w2 <= 1e-12f || n2 <= 1e-12f) return false;
            *outCos = (wx * ax + wy * ay + wz * az) / std::sqrt(w2 * n2);
            return true;
        };

        bool pairMirror = false;
        if (pairCandidate) {
            float cos0 = 0.f;
            if (windingAuthoredCos(&cos0) && cos0 < 0.f) {
                if (f.vertexCount == 3) {
                    std::swap(tv[1], tv[2]);
                } else if (f.vertexCount == 4) {
                    std::swap(tv[1], tv[3]);
                }
                pairMirror = true;
                out->pairHandednessFixCount++;
            }
        }

        // Align export winding to authored vn (glTF CCW). Residual opposition
        // after Pair fix is a source fingerprint — bytecode winding is
        // unconstrained under FUNC_001481. Flip uses authored-sign only (no
        // |cos| threshold): the fleet has faces in 0.15–0.25, so a
        // FaceWindingInward/sign split would churn method at the boundary.
        bool flipWinding = false;
        float decisionCos = 0.f;
        bool haveDecisionCos = false;
        {
            float cos = 0.f;
            if (windingAuthoredCos(&cos)) {
                haveDecisionCos = true;
                decisionCos = cos;
                const float ac = std::fabs(cos);
                if (ac < out->minAbsWindingCos) out->minAbsWindingCos = ac;
                if (ac >= 0.15f && ac <= 0.25f) out->ambiguousCosBandCount++;
                if (cos < -0.5f) out->authoredOpposedCount++;
                if (cos < 0.f) flipWinding = true;
            } else if (!isLine && f.vertexCount >= 3) {
                flipWinding = FaceWindingInward(tv, f.vertexCount);
            }
        }
        if (flipWinding && !isLine) out->windingFlipCount++;

        // Non-planar quads: FFE never triangulates; export invents diagonal 0–2.
        if (!isLine && f.vertexCount == 4) {
            const float ux = tv[1].x - tv[0].x, uy = tv[1].y - tv[0].y,
                        uz = tv[1].z - tv[0].z;
            const float vx = tv[2].x - tv[0].x, vy = tv[2].y - tv[0].y,
                        vz = tv[2].z - tv[0].z;
            const float nx = uy * vz - uz * vy;
            const float ny = uz * vx - ux * vz;
            const float nz = ux * vy - uy * vx;
            const float n2 = nx * nx + ny * ny + nz * nz;
            float dev = 0.f;
            bool nonPlanar = false;
            if (n2 > 1e-20f) {
                const float inv = 1.f / std::sqrt(n2);
                const float dx = tv[3].x - tv[0].x;
                const float dy = tv[3].y - tv[0].y;
                const float dz = tv[3].z - tv[0].z;
                dev = std::fabs((nx * dx + ny * dy + nz * dz) * inv);
                if (dev > out->maxNonPlanarDev) out->maxNonPlanarDev = dev;
                if (dev > 1e-3f) {
                    out->nonPlanarQuadCount++;
                    nonPlanar = true;
                }
            }
            out->quadDiag02Count++; // emitFan {{0,1,2},{0,2,3}}

            // Per-half authored cos on the chosen (0–2) split. Face-level flip
            // applies to both tris; opposite signs ⇒ one half always opposed.
            auto triAuthCos = [&](int ia, int ib, int ic, float* outCos) -> bool {
                const float ax = tv[ib].x - tv[ia].x, ay = tv[ib].y - tv[ia].y,
                            az = tv[ib].z - tv[ia].z;
                const float bx = tv[ic].x - tv[ia].x, by = tv[ic].y - tv[ia].y,
                            bz = tv[ic].z - tv[ia].z;
                const float wx = ay * bz - az * by;
                const float wy = az * bx - ax * bz;
                const float wz = ax * by - ay * bx;
                const float w2 = wx * wx + wy * wy + wz * wz;
                const float anx = tv[0].nx, any = tv[0].ny, anz = tv[0].nz;
                const float an2 = anx * anx + any * any + anz * anz;
                if (!f.authoredNormal || w2 <= 1e-12f || an2 <= 1e-12f) return false;
                *outCos = (wx * anx + wy * any + wz * anz) / std::sqrt(w2 * an2);
                return true;
            };
            float c02a = 0.f, c02b = 0.f;
            bool haveHalves = triAuthCos(0, 1, 2, &c02a) && triAuthCos(0, 2, 3, &c02b);
            bool flipDisagree = false;
            if (nonPlanar && haveHalves && (c02a * c02b) < 0.f) {
                out->nonPlanarFlipDisagreeCount++;
                flipDisagree = true;
            }
            const char* npDump = std::getenv("EXPORT_NONPLANAR_DUMP");
            if (npDump && npDump[0] == '1' && (nonPlanar || dev > 1e-4f)) {
                static bool s_npHdr = false;
                if (!s_npHdr) {
                    std::fprintf(stderr,
                        "NONPLANAR ship face tex nIdx dev cos02a cos02b "
                        "flipDisagree diag\n");
                    s_npHdr = true;
                }
                std::fprintf(stderr,
                    "NONPLANAR %d %d 0x%04x %u %.4f %.4f %.4f %s 02\n",
                    out->shipIndex, i, (unsigned)f.texInfo, (unsigned)f.normalIdx,
                    dev, haveHalves ? c02a : 0.f, haveHalves ? c02b : 0.f,
                    flipDisagree ? "yes" : "no");
            }
        }

        {
            const char* dumpAll = std::getenv("EXPORT_COS_DUMP");
            const char* dumpRes = std::getenv("EXPORT_RESIDUAL_DUMP");
            const bool wantAll = dumpAll && dumpAll[0] == '1';
            const bool wantRes = dumpRes && dumpRes[0] == '1' && haveDecisionCos &&
                                 decisionCos < -0.5f;
            if ((wantAll || wantRes) && haveDecisionCos && !isLine) {
                static bool s_dumpHdr = false;
                if (!s_dumpHdr) {
                    std::fprintf(stderr,
                        "COS ship face type kind semantic tex nIdx vc "
                        "pairFix cos centreline\n");
                    s_dumpHdr = true;
                }
                float xmin = tv[0].x, xmax = tv[0].x;
                for (int vi = 1; vi < f.vertexCount && vi < 4; vi++) {
                    if (tv[vi].x < xmin) xmin = tv[vi].x;
                    if (tv[vi].x > xmax) xmax = tv[vi].x;
                }
                const bool centreline = (xmin < -1e-3f && xmax > 1e-3f);
                std::fprintf(stderr,
                    "COS %d %d %s %s %s 0x%04x %u %u %d %.4f %s\n",
                    out->shipIndex, i, MeshPolyTypeName(f.type),
                    isPunch ? "Punch" : (isHull ? "Hull" : "?"),
                    FaceSemanticName(sem), (unsigned)f.texInfo,
                    (unsigned)f.normalIdx, (unsigned)f.vertexCount,
                    pairMirror ? 1 : 0, decisionCos,
                    centreline ? "yes" : "no");
            }
        }

        // Overlay / hull / punch all keep GetNormal authored vn (policy D).
        // Geometric overwrite was hiding the 7° authored tilt on flat plates
        // (Krait wingtip: (16,-125,0) → cos≈0.992 vs winding, not 1.000).
        if (isDecal && opt.decalEpsilon != 0.f && f.vertexCount >= 3) {
            float nx = tv[0].nx, ny = tv[0].ny, nz = tv[0].nz;
            const float n2 = nx * nx + ny * ny + nz * nz;
            if (n2 > 1e-20f) {
                const float inv = 1.f / std::sqrt(n2);
                nx *= inv;
                ny *= inv;
                nz *= inv;
                for (int vi = 0; vi < f.vertexCount && vi < 4; vi++) {
                    tv[vi].x += nx * opt.decalEpsilon;
                    tv[vi].y += ny * opt.decalEpsilon;
                    tv[vi].z += nz * opt.decalEpsilon;
                }
            }
        }

        emitFan(tv, f.vertexCount, f, isLine ? nullptr : src, mat, bucket,
                /*doubleSided=*/isLine, flipWinding);
    }

    // Post-condition repair: if a tri still opposes (rare weld/quad case),
    // reverse winding indices — keep authored vn (policy D).
    {
        const size_t nOutTri = out->triMaterial.size();
        for (size_t t = 0; t < nOutTri; t++) {
            const int mi = out->triMaterial[t];
            if (mi >= 0 && mi < (int)out->materials.size() &&
                out->materials[mi].name.rfind("line_", 0) == 0)
                continue;
            uint32_t* idx = &out->indices[t * 3];
            const ShipExportVert& a = out->verts[idx[0]];
            const ShipExportVert& b = out->verts[idx[1]];
            const ShipExportVert& c = out->verts[idx[2]];
            const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const float wx = uy * vz - uz * vy;
            const float wy = uz * vx - ux * vz;
            const float wz = ux * vy - uy * vx;
            const float nx = (a.nx + b.nx + c.nx) * (1.f / 3.f);
            const float ny = (a.ny + b.ny + c.ny) * (1.f / 3.f);
            const float nz = (a.nz + b.nz + c.nz) * (1.f / 3.f);
            const float w2 = wx * wx + wy * wy + wz * wz;
            const float n2 = nx * nx + ny * ny + nz * nz;
            if (w2 < 1e-12f || n2 < 1e-12f) continue;
            if (wx * nx + wy * ny + wz * nz >= 0.f) continue;
            std::swap(idx[1], idx[2]);
            out->vnSplitCount++; // name kept: winding repairs after emit
        }
    }

    // T14 census: X-mirror winding (allows two-sided coplanar plates).
    {
        const int nOutTri = (int)out->triMaterial.size();
        auto windN = [&](int t, float& nx, float& ny, float& nz) {
            const ShipExportVert& a = out->verts[out->indices[(size_t)t * 3 + 0]];
            const ShipExportVert& b = out->verts[out->indices[(size_t)t * 3 + 1]];
            const ShipExportVert& c = out->verts[out->indices[(size_t)t * 3 + 2]];
            const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            nx = uy * vz - uz * vy;
            ny = uz * vx - ux * vz;
            nz = ux * vy - uy * vx;
            const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (L > 1e-12f) {
                nx /= L;
                ny /= L;
                nz /= L;
            }
        };
        auto posKey = [&](int t, bool negX) -> uint64_t {
            std::array<std::array<int32_t, 3>, 3> p{};
            for (int k = 0; k < 3; k++) {
                const ShipExportVert& v =
                    out->verts[out->indices[(size_t)t * 3 + k]];
                p[k] = {(int32_t)std::lround((negX ? -v.x : v.x) * 256.0),
                        (int32_t)std::lround(v.y * 256.0),
                        (int32_t)std::lround(v.z * 256.0)};
            }
            std::sort(p.begin(), p.end());
            uint64_t h = 1469598103934665603ull;
            for (const auto& c : p) {
                for (int32_t x : c) {
                    h ^= (uint64_t)(uint32_t)x;
                    h *= 1099511628211ull;
                }
            }
            return h;
        };
        std::unordered_map<uint64_t, std::vector<int>> byPos;
        for (int t = 0; t < nOutTri; t++) {
            const int mi = out->triMaterial[t];
            if (mi >= 0 && mi < (int)out->materials.size() &&
                out->materials[mi].name.rfind("line_", 0) == 0)
                continue;
            byPos[posKey(t, false)].push_back(t);
        }
        out->mirrorWindingFailCount = 0;
        for (int t = 0; t < nOutTri; t++) {
            const int mi = out->triMaterial[t];
            if (mi >= 0 && mi < (int)out->materials.size() &&
                out->materials[mi].name.rfind("line_", 0) == 0)
                continue;
            auto it = byPos.find(posKey(t, true));
            if (it == byPos.end() || it->second.empty()) continue;
            float ax, ay, az;
            windN(t, ax, ay, az);
            ax = -ax;
            bool found = false;
            for (int p : it->second) {
                if (p == t) continue;
                float bx, by, bz;
                windN(p, bx, by, bz);
                if (ax * bx + ay * by + az * bz >= 0.5f) {
                    found = true;
                    break;
                }
            }
            if (!found) out->mirrorWindingFailCount++;
        }
    }

    // Sub-lattice inset fingerprint: overlay/punch verts vs nearest hull plane.
    {
        struct Plane {
            float nx, ny, nz, px, py, pz;
        };
        std::vector<Plane> hullPlanes;
        std::vector<int> overlayVerts, punchVerts;
        const int nTri = (int)out->triMaterial.size();
        for (int t = 0; t < nTri; t++) {
            const int mi = out->triMaterial[(size_t)t];
            if (mi < 0 || mi >= (int)out->materials.size()) continue;
            const ShipExportMaterial& mat = out->materials[(size_t)mi];
            const uint32_t ia = out->indices[(size_t)t * 3 + 0];
            const uint32_t ib = out->indices[(size_t)t * 3 + 1];
            const uint32_t ic = out->indices[(size_t)t * 3 + 2];
            if (ia >= out->verts.size() || ib >= out->verts.size() ||
                ic >= out->verts.size())
                continue;
            if (mat.decal) {
                overlayVerts.push_back((int)ia);
                overlayVerts.push_back((int)ib);
                overlayVerts.push_back((int)ic);
                continue;
            }
            if (mat.punch) {
                punchVerts.push_back((int)ia);
                punchVerts.push_back((int)ib);
                punchVerts.push_back((int)ic);
                continue;
            }
            // Hull (and line ribbons): contribute planes for nearest-plane search.
            if (mat.name.rfind("line_", 0) == 0) continue;
            const ShipExportVert& a = out->verts[ia];
            const ShipExportVert& b = out->verts[ib];
            const ShipExportVert& c = out->verts[ic];
            const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
            const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            const float L = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (L < 1e-12f) continue;
            nx /= L;
            ny /= L;
            nz /= L;
            hullPlanes.push_back({nx, ny, nz, a.x, a.y, a.z});
        }
        auto uniq = [](std::vector<int>& v) {
            std::sort(v.begin(), v.end());
            v.erase(std::unique(v.begin(), v.end()), v.end());
        };
        uniq(overlayVerts);
        uniq(punchVerts);
        auto measure = [&](const std::vector<int>& vids, float* maxOut,
                           int* countOut) {
            for (int vi : vids) {
                if (vi < 0 || vi >= (int)out->verts.size()) continue;
                const ShipExportVert& p = out->verts[(size_t)vi];
                float best = 1e30f;
                for (const Plane& pl : hullPlanes) {
                    const float d = std::fabs((p.x - pl.px) * pl.nx +
                                              (p.y - pl.py) * pl.ny +
                                              (p.z - pl.pz) * pl.nz);
                    if (d < best) best = d;
                }
                if (best > 1e-6f && best < kShipExportLatticeUnit) {
                    (*countOut)++;
                    if (best > *maxOut) *maxOut = best;
                }
            }
        };
        measure(overlayVerts, &out->maxOverlaySubLatticeInset,
                &out->overlaySubLatticeVertCount);
        measure(punchVerts, &out->maxPunchSubLatticeInset,
                &out->punchSubLatticeVertCount);
    }

    out->missileFlat = missileFlat;
    out->valid = true;
    return true;
}

bool WriteExportObjMtl(const ShipExportMesh& mesh, const char* dir)
{
    if (!mesh.valid || !dir) return false;
    if (!EnsureDir(dir)) return false;

    for (const ShipExportMaterial& m : mesh.materials) {
        if (!m.textured || m.baked.argb.empty()) continue;
        char path[1200];
        std::snprintf(path, sizeof(path), "%s/%s", dir, m.pngName.c_str());
        if (!WriteRgbaPng(path, m.baked.width, m.baked.height, m.baked.argb.data()))
            return false;
    }

    char mtlPath[1200];
    std::snprintf(mtlPath, sizeof(mtlPath), "%s/model.mtl", dir);
    FILE* mtl = std::fopen(mtlPath, "w");
    if (!mtl) return false;
    fprintf(mtl, "# JJFFE ship %d %s\n", mesh.shipIndex, mesh.name.c_str());
    for (const ShipExportMaterial& m : mesh.materials) {
        fprintf(mtl, "newmtl %s\n", m.name.c_str());
        const float r = ((m.kdArgb >> 16) & 0xFF) / 255.0f;
        const float g = ((m.kdArgb >> 8) & 0xFF) / 255.0f;
        const float b = (m.kdArgb & 0xFF) / 255.0f;
        fprintf(mtl, "Kd %.4f %.4f %.4f\n", r, g, b);
        fprintf(mtl, "Ka 0 0 0\nKs 0 0 0\n");
        if (m.punch) {
            fprintf(mtl, "illum 0\n# punch 1\n");
            fprintf(mtl, "# painter_order punch\n# draw_after_hull 1\n");
            fprintf(mtl, "# depth_write 0\n# depth_bias %.6g\n"
                    "# slope_scale_depth_bias %.6g\n",
                    kShipExportPainterDepthBias, kShipExportPainterSlopeScale);
            fprintf(mtl, "# blender: run tools/blender_decal_setup.py\n");
        } else if (m.decal) {
            fprintf(mtl, "illum 1\n# decal 1\n");
            fprintf(mtl, "# painter_order overlay\n# draw_after_hull 1\n");
            fprintf(mtl, "# depth_write 0\n# depth_bias %.6g\n"
                    "# slope_scale_depth_bias %.6g\n",
                    kShipExportPainterDepthBias, kShipExportPainterSlopeScale);
            fprintf(mtl, "# blender: run tools/blender_decal_setup.py\n");
        } else {
            fprintf(mtl, "illum 1\n# painter_order hull\n");
        }
        if (m.name.rfind("line_", 0) == 0)
            fprintf(mtl, "# double_sided 1\n");
        if (m.textured && !m.pngName.empty())
            fprintf(mtl, "map_Kd %s\n", m.pngName.c_str());
        if (m.transparent)
            fprintf(mtl, "map_d %s\n", m.pngName.c_str());
        fprintf(mtl, "\n");
    }
    if (!mesh.curves.empty()) {
        fprintf(mtl, "newmtl curves\nKd 0.85 0.75 0.35\nKa 0 0 0\nKs 0 0 0\n");
        fprintf(mtl, "illum 0\n# contour_polyline 1\n\n");
    }
    fclose(mtl);

    char objPath[1200];
    std::snprintf(objPath, sizeof(objPath), "%s/model.obj", dir);
    FILE* obj = std::fopen(objPath, "w");
    if (!obj) return false;
    const float s = kShipExportObjScale;
    fprintf(obj, "# JJFFE export ship %d (%s) dist=%d faces=%d "
            "hull=%d punch=%d decal=%d contour_tri=%d curves=%d line=%d sprites=%d\n",
            mesh.shipIndex, mesh.name.c_str(), (int)mesh.shipDist,
            mesh.worldFaceCount, mesh.hullTriCount, mesh.punchTriCount,
            mesh.decalTriCount, mesh.contourTriCount, mesh.contourCurveCount,
            mesh.lineTriCount, mesh.spriteChildCount);
    fprintf(obj, "# capture: dist=%d authoredOpposed=%d pairHandednessFix=%d "
            "windingFlips=%d minAbsCos=%.4f cosBand=%d nonPlanarQuads=%d "
            "maxNonPlanarDev=%.4f flipDisagree=%d diag02=%d diag13=%d "
            "maxOverlayInset=%.4f maxPunchInset=%.4f "
            "windingRepairs=%d mirrorWindingFail=%d\n",
            (int)mesh.shipDist, mesh.authoredOpposedCount,
            mesh.pairHandednessFixCount, mesh.windingFlipCount,
            mesh.minAbsWindingCos, mesh.ambiguousCosBandCount,
            mesh.nonPlanarQuadCount, mesh.maxNonPlanarDev,
            mesh.nonPlanarFlipDisagreeCount,
            mesh.quadDiag02Count, mesh.quadDiag13Count,
            mesh.maxOverlaySubLatticeInset, mesh.maxPunchSubLatticeInset,
            mesh.vnSplitCount, mesh.mirrorWindingFailCount);
    fprintf(obj, "# units: world (fixed>>16) * %.0f, origin at model, +Z forward\n",
            s);
    fprintf(obj, "mtllib model.mtl\n");
    fprintf(obj, "o %03d_%s\n", mesh.shipIndex, mesh.name.c_str());
    for (const ShipExportVert& v : mesh.verts)
        fprintf(obj, "v %.6f %.6f %.6f\n", v.x * s, v.y * s, v.z * s);
    for (const ShipExportVert& v : mesh.verts)
        fprintf(obj, "vt %.6f %.6f\n", v.u, v.v);
    for (const ShipExportVert& v : mesh.verts)
        fprintf(obj, "vn %.6f %.6f %.6f\n", v.nx, v.ny, v.nz);

    const int nTri = (int)mesh.indices.size() / 3;
    enum class ObjLayer { Hull, Punch, Decal };
    auto writeFaces = [&](ObjLayer layer) {
        int lastMat = -1;
        for (int t = 0; t < nTri; t++) {
            const int mi =
                (t < (int)mesh.triMaterial.size()) ? mesh.triMaterial[t] : -1;
            ObjLayer tLayer = ObjLayer::Hull;
            if (mi >= 0 && mi < (int)mesh.materials.size()) {
                if (mesh.materials[mi].punch) tLayer = ObjLayer::Punch;
                else if (mesh.materials[mi].decal) tLayer = ObjLayer::Decal;
            }
            if (tLayer != layer) continue;
            if (mi != lastMat) {
                lastMat = mi;
                if (lastMat >= 0 && lastMat < (int)mesh.materials.size())
                    fprintf(obj, "usemtl %s\n",
                            mesh.materials[lastMat].name.c_str());
            }
            if (t < (int)mesh.triMeta.size()) {
                const ShipExportTriMeta& tm = mesh.triMeta[(size_t)t];
                fprintf(obj, "# prim %u order %d fan %u%s\n", tm.primitiveId,
                        tm.sourceOrder, (unsigned)tm.fanOrdinal,
                        tm.reverseSide ? " rev" : "");
            }
            const uint32_t a = mesh.indices[t * 3 + 0] + 1;
            const uint32_t b = mesh.indices[t * 3 + 1] + 1;
            const uint32_t c = mesh.indices[t * 3 + 2] + 1;
            fprintf(obj, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                    a, a, a, b, b, b, c, c, c);
        }
    };
    writeFaces(ObjLayer::Hull);
    if (mesh.punchTriCount > 0) {
        fprintf(obj, "o %03d_%s_punch\n", mesh.shipIndex, mesh.name.c_str());
        writeFaces(ObjLayer::Punch);
    }
    if (mesh.decalTriCount > 0) {
        fprintf(obj, "o %03d_%s_decal\n", mesh.shipIndex, mesh.name.c_str());
        writeFaces(ObjLayer::Decal);
    }

    if (!mesh.curves.empty()) {
        fprintf(obj, "o %03d_%s_curves\n", mesh.shipIndex, mesh.name.c_str());
        fprintf(obj, "usemtl curves\n");
        int vi = (int)mesh.verts.size();
        for (const ShipExportPolyline& pl : mesh.curves) {
            for (const ShipExportVert& p : pl.pts)
                fprintf(obj, "v %.6f %.6f %.6f\n", p.x * s, p.y * s, p.z * s);
            fprintf(obj, "l");
            for (size_t i = 0; i < pl.pts.size(); i++)
                fprintf(obj, " %d", vi + (int)i + 1);
            fprintf(obj, "\n");
            vi += (int)pl.pts.size();
        }
    }
    fclose(obj);

    char jsonPath[1200];
    std::snprintf(jsonPath, sizeof(jsonPath), "%s/children.json", dir);
    FILE* js = std::fopen(jsonPath, "w");
    if (!js) return false;
    fprintf(js, "{\n  \"parent\": %d,\n  \"name\": \"%s\",\n  \"scale\": %.1f,\n"
            "  \"children\": [\n",
            mesh.shipIndex, mesh.name.c_str(), s);
    for (size_t i = 0; i < mesh.sprites.size(); i++) {
        const ShipExportSpriteChild& c = mesh.sprites[i];
        fprintf(js,
                "    {\"modelId\": %d, \"pos\": [%.6f, %.6f, %.6f], "
                "\"rot\": [%.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f], "
                "\"scaleRelated\": %d}%s\n",
                c.modelId, c.pos[0] * s, c.pos[1] * s, c.pos[2] * s,
                c.rot[0], c.rot[1], c.rot[2], c.rot[3], c.rot[4], c.rot[5],
                c.rot[6], c.rot[7], c.rot[8], c.scaleRelated,
                (i + 1 < mesh.sprites.size()) ? "," : "");
    }
    fprintf(js, "  ]\n}\n");
    fclose(js);

    char emPath[1200];
    std::snprintf(emPath, sizeof(emPath), "%s/emitters.json", dir);
    FILE* em = std::fopen(emPath, "w");
    if (!em) return false;
    fprintf(em,
            "{\n  \"parent\": %d,\n  \"name\": \"%s\",\n  \"scale\": %.1f,\n"
            "  \"emitters\": [\n",
            mesh.shipIndex, mesh.name.c_str(), s);
    for (size_t i = 0; i < mesh.emitters.size(); i++) {
        const ShipExportEmitter& e = mesh.emitters[i];
        const char* kind = "navlight";
        if (e.kind == ShipExportEmitterKind::Thruster) kind = "thruster";
        else if (e.kind == ShipExportEmitterKind::Glow) kind = "glow";
        fprintf(em,
                "    {\"kind\": \"%s\", \"pos\": [%.6f, %.6f, %.6f]",
                kind, e.x * s, e.y * s, e.z * s);
        if (e.kind == ShipExportEmitterKind::Thruster) {
            fprintf(em, ", \"pos1\": [%.6f, %.6f, %.6f], \"size\": %.3f",
                    e.x1 * s, e.y1 * s, e.z1 * s, e.size);
        }
        fprintf(em,
                ", \"color\": [%.4f, %.4f, %.4f], \"vertexIdx\": %d",
                e.rgb[0], e.rgb[1], e.rgb[2], e.vertexIdx);
        if (e.kind == ShipExportEmitterKind::Thruster)
            fprintf(em, ", \"vertexIdx1\": %d", e.vertexIdx1);
        if (e.kind == ShipExportEmitterKind::NavLight)
            fprintf(em, ", \"blink\": %s", e.blink ? "true" : "false");
        fprintf(em, ", \"texInfo\": %u}%s\n", (unsigned)e.texInfo,
                (i + 1 < mesh.emitters.size()) ? "," : "");
    }
    fprintf(em, "  ]\n}\n");
    fclose(em);
    if (!WriteExportPrimitivesJson(mesh, dir)) return false;
    // W4.3 — per-ship manifest + materials (export/ geometry still immutable).
    const std::string hash = ComputeShipExportAssetHash(mesh);
    if (!WriteExportShipManifest(mesh, dir, hash.c_str())) return false;
    return WriteExportMaterialsJson(mesh, dir);
}

bool WriteExportPrimitivesJson(const ShipExportMesh& mesh, const char* dir)
{
    if (!mesh.valid || !dir) return false;
    if (!EnsureDir(dir)) return false;
    char path[1200];
    std::snprintf(path, sizeof(path), "%s/primitives.json", dir);
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    const int nTri = (int)mesh.indices.size() / 3;
    fprintf(f,
            "{\n  \"parent\": %d,\n  \"name\": \"%s\",\n  \"policyVersion\": 1,\n"
            "  \"triangleCount\": %d,\n  \"primitives\": [\n",
            mesh.shipIndex, mesh.name.c_str(), nTri);
    for (int t = 0; t < nTri; t++) {
        const int mi =
            (t < (int)mesh.triMaterial.size()) ? mesh.triMaterial[(size_t)t] : -1;
        const char* pass = "hull";
        if (mi >= 0 && mi < (int)mesh.materials.size()) {
            if (mesh.materials[(size_t)mi].punch) pass = "punch";
            else if (mesh.materials[(size_t)mi].decal) pass = "overlay";
            else if (mesh.materials[(size_t)mi].name.find("line") !=
                     std::string::npos)
                pass = "line";
        }
        uint32_t pid = 0;
        int order = -1;
        unsigned fan = 0;
        int rev = 0;
        if (t < (int)mesh.triMeta.size()) {
            const ShipExportTriMeta& tm = mesh.triMeta[(size_t)t];
            pid = tm.primitiveId;
            order = tm.sourceOrder;
            fan = tm.fanOrdinal;
            rev = tm.reverseSide ? 1 : 0;
        }
        fprintf(f,
                "    {\"id\": %u, \"sourceOrder\": %d, \"fan\": %u, "
                "\"reverse\": %s, \"tri\": %d, \"material\": %d, "
                "\"pass\": \"%s\"}%s\n",
                pid, order, fan, rev ? "true" : "false", t, mi, pass,
                (t + 1 < nTri) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

namespace {

void Align4(std::vector<uint8_t>& b)
{
    while (b.size() % 4) b.push_back(0);
}

void AppendBytes(std::vector<uint8_t>& b, const void* data, size_t n)
{
    const auto* p = static_cast<const uint8_t*>(data);
    b.insert(b.end(), p, p + n);
}

void AppendF32(std::vector<uint8_t>& b, float v)
{
    AppendBytes(b, &v, sizeof(v));
}

void AppendU32(std::vector<uint8_t>& b, uint32_t v)
{
    AppendBytes(b, &v, sizeof(v));
}

std::string JsonEscapeStr(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

} // namespace

bool WriteExportGlb(const ShipExportMesh& mesh, const char* dir)
{
    if (!mesh.valid || !dir) return false;
    if (!EnsureDir(dir)) return false;

    const float s = kShipExportObjScale;
    const int nVert = (int)mesh.verts.size();
    const int nTri = (int)mesh.indices.size() / 3;
    if (nVert <= 0 || nTri < 0) return false;

    std::vector<uint8_t> bin;
    bin.reserve((size_t)nVert * 32 + mesh.indices.size() * 4 + 65536);

    struct TexSlot {
        int imageIndex = -1;
        int bufferView = -1;
        size_t byteOffset = 0;
        size_t byteLength = 0;
    };
    std::vector<TexSlot> texSlots(mesh.materials.size());
    std::vector<std::vector<uint8_t>> pngBytes(mesh.materials.size());

    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        const ShipExportMaterial& m = mesh.materials[mi];
        if (!m.textured || m.baked.argb.empty()) continue;
        if (!EncodeRgbaPng(m.baked.width, m.baked.height, m.baked.argb.data(),
                           &pngBytes[mi]))
            return false;
        Align4(bin);
        texSlots[mi].byteOffset = bin.size();
        texSlots[mi].byteLength = pngBytes[mi].size();
        AppendBytes(bin, pngBytes[mi].data(), pngBytes[mi].size());
        Align4(bin);
    }

    // Shared vertex attributes (scaled positions).
    Align4(bin);
    const size_t posOff = bin.size();
    float posMin[3] = {1e30f, 1e30f, 1e30f};
    float posMax[3] = {-1e30f, -1e30f, -1e30f};
    for (const ShipExportVert& v : mesh.verts) {
        const float x = v.x * s, y = v.y * s, z = v.z * s;
        AppendF32(bin, x);
        AppendF32(bin, y);
        AppendF32(bin, z);
        if (x < posMin[0]) posMin[0] = x;
        if (y < posMin[1]) posMin[1] = y;
        if (z < posMin[2]) posMin[2] = z;
        if (x > posMax[0]) posMax[0] = x;
        if (y > posMax[1]) posMax[1] = y;
        if (z > posMax[2]) posMax[2] = z;
    }
    Align4(bin);
    const size_t nrmOff = bin.size();
    for (const ShipExportVert& v : mesh.verts) {
        AppendF32(bin, v.nx);
        AppendF32(bin, v.ny);
        AppendF32(bin, v.nz);
    }
    Align4(bin);
    const size_t uvOff = bin.size();
    for (const ShipExportVert& v : mesh.verts) {
        AppendF32(bin, v.u);
        AppendF32(bin, v.v);
    }

    // Per-material index ranges.
    std::vector<std::vector<uint32_t>> matIdx(mesh.materials.size());
    for (int t = 0; t < nTri; t++) {
        const int mi = mesh.triMaterial[t];
        if (mi < 0 || mi >= (int)mesh.materials.size()) continue;
        matIdx[mi].push_back(mesh.indices[(size_t)t * 3 + 0]);
        matIdx[mi].push_back(mesh.indices[(size_t)t * 3 + 1]);
        matIdx[mi].push_back(mesh.indices[(size_t)t * 3 + 2]);
    }

    struct IdxSlot {
        size_t off = 0;
        size_t count = 0;  // index count
        size_t bytes = 0;
    };
    std::vector<IdxSlot> idxSlots(mesh.materials.size());
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (matIdx[mi].empty()) continue;
        Align4(bin);
        idxSlots[mi].off = bin.size();
        idxSlots[mi].count = matIdx[mi].size();
        idxSlots[mi].bytes = matIdx[mi].size() * sizeof(uint32_t);
        for (uint32_t ix : matIdx[mi]) AppendU32(bin, ix);
    }

    // Contour polylines as LINES (pairs).
    size_t curvePosOff = 0, curveIdxOff = 0;
    size_t curveVertCount = 0, curveIdxCount = 0;
    float curveMin[3] = {1e30f, 1e30f, 1e30f};
    float curveMax[3] = {-1e30f, -1e30f, -1e30f};
    if (!mesh.curves.empty()) {
        std::vector<float> cpos;
        std::vector<uint32_t> cidx;
        for (const ShipExportPolyline& pl : mesh.curves) {
            if (pl.pts.size() < 2) continue;
            const uint32_t base = (uint32_t)(cpos.size() / 3);
            for (const ShipExportVert& p : pl.pts) {
                const float x = p.x * s, y = p.y * s, z = p.z * s;
                cpos.push_back(x);
                cpos.push_back(y);
                cpos.push_back(z);
                if (x < curveMin[0]) curveMin[0] = x;
                if (y < curveMin[1]) curveMin[1] = y;
                if (z < curveMin[2]) curveMin[2] = z;
                if (x > curveMax[0]) curveMax[0] = x;
                if (y > curveMax[1]) curveMax[1] = y;
                if (z > curveMax[2]) curveMax[2] = z;
            }
            for (size_t i = 0; i + 1 < pl.pts.size(); i++) {
                cidx.push_back(base + (uint32_t)i);
                cidx.push_back(base + (uint32_t)i + 1);
            }
        }
        if (!cpos.empty() && !cidx.empty()) {
            Align4(bin);
            curvePosOff = bin.size();
            AppendBytes(bin, cpos.data(), cpos.size() * sizeof(float));
            curveVertCount = cpos.size() / 3;
            Align4(bin);
            curveIdxOff = bin.size();
            AppendBytes(bin, cidx.data(), cidx.size() * sizeof(uint32_t));
            curveIdxCount = cidx.size();
        }
    }
    Align4(bin);

    // ---- bufferViews / accessors / images / materials / meshes ----
    std::ostringstream json;
    json << std::fixed;
    json << "{"
         << "\"asset\":{\"version\":\"2.0\",\"generator\":\"jjffe-export\"},"
         << "\"scene\":0,"
         << "\"scenes\":[{\"nodes\":[0]}],"
         << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],";

    // bufferViews
    std::vector<std::string> bviews;
    auto addView = [&](size_t off, size_t len, int target) -> int {
        const int id = (int)bviews.size();
        std::ostringstream v;
        v << "{\"buffer\":0,\"byteOffset\":" << off << ",\"byteLength\":" << len;
        if (target) v << ",\"target\":" << target;
        v << "}";
        bviews.push_back(v.str());
        return id;
    };
    const int bvPos = addView(posOff, (size_t)nVert * 12, 34962);
    const int bvNrm = addView(nrmOff, (size_t)nVert * 12, 34962);
    const int bvUv = addView(uvOff, (size_t)nVert * 8, 34962);
    std::vector<int> bvIdx(mesh.materials.size(), -1);
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (idxSlots[mi].count == 0) continue;
        bvIdx[mi] = addView(idxSlots[mi].off, idxSlots[mi].bytes, 34963);
    }
    int bvCurvePos = -1, bvCurveIdx = -1;
    if (curveVertCount > 0) {
        bvCurvePos = addView(curvePosOff, curveVertCount * 12, 34962);
        bvCurveIdx = addView(curveIdxOff, curveIdxCount * 4, 34963);
    }
    std::vector<int> bvImg(mesh.materials.size(), -1);
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (texSlots[mi].byteLength == 0) continue;
        bvImg[mi] = addView(texSlots[mi].byteOffset, texSlots[mi].byteLength, 0);
    }

    json << "\"bufferViews\":[";
    for (size_t i = 0; i < bviews.size(); i++) {
        if (i) json << ",";
        json << bviews[i];
    }
    json << "],";

    // accessors
    std::vector<std::string> accessors;
    auto addAcc = [&](int bv, int count, const char* type, int comp,
                      const float* mn, const float* mx) -> int {
        const int id = (int)accessors.size();
        std::ostringstream a;
        a << "{\"bufferView\":" << bv << ",\"componentType\":" << comp
          << ",\"count\":" << count << ",\"type\":\"" << type << "\"";
        if (mn && mx) {
            a << ",\"min\":[" << mn[0] << "," << mn[1] << "," << mn[2] << "],"
              << "\"max\":[" << mx[0] << "," << mx[1] << "," << mx[2] << "]";
        }
        a << "}";
        accessors.push_back(a.str());
        return id;
    };
    const int accPos = addAcc(bvPos, nVert, "VEC3", 5126, posMin, posMax);
    const int accNrm = addAcc(bvNrm, nVert, "VEC3", 5126, nullptr, nullptr);
    const int accUv = addAcc(bvUv, nVert, "VEC2", 5126, nullptr, nullptr);
    std::vector<int> accIdx(mesh.materials.size(), -1);
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (idxSlots[mi].count == 0) continue;
        accIdx[mi] = addAcc(bvIdx[mi], (int)idxSlots[mi].count, "SCALAR", 5125,
                            nullptr, nullptr);
    }
    int accCurvePos = -1, accCurveIdx = -1;
    if (curveVertCount > 0) {
        accCurvePos = addAcc(bvCurvePos, (int)curveVertCount, "VEC3", 5126,
                             curveMin, curveMax);
        accCurveIdx = addAcc(bvCurveIdx, (int)curveIdxCount, "SCALAR", 5125,
                             nullptr, nullptr);
    }

    json << "\"accessors\":[";
    for (size_t i = 0; i < accessors.size(); i++) {
        if (i) json << ",";
        json << accessors[i];
    }
    json << "],";

    // images / textures / samplers
    std::vector<int> matTexIndex(mesh.materials.size(), -1);
    {
        std::vector<std::string> images, textures;
        bool anyTex = false;
        for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
            if (bvImg[mi] < 0) continue;
            anyTex = true;
            const int imgId = (int)images.size();
            {
                std::ostringstream im;
                im << "{\"bufferView\":" << bvImg[mi]
                   << ",\"mimeType\":\"image/png\"}";
                images.push_back(im.str());
            }
            const int texId = (int)textures.size();
            {
                std::ostringstream tx;
                tx << "{\"sampler\":0,\"source\":" << imgId << "}";
                textures.push_back(tx.str());
            }
            matTexIndex[mi] = texId;
        }
        if (anyTex) {
            json << "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728,"
                    "\"wrapS\":33071,\"wrapT\":33071}],";
            json << "\"images\":[";
            for (size_t i = 0; i < images.size(); i++) {
                if (i) json << ",";
                json << images[i];
            }
            json << "],\"textures\":[";
            for (size_t i = 0; i < textures.size(); i++) {
                if (i) json << ",";
                json << textures[i];
            }
            json << "],";
        }
    }

    // materials
    json << "\"materials\":[";
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (mi) json << ",";
        const ShipExportMaterial& m = mesh.materials[mi];
        const float r = ((m.kdArgb >> 16) & 0xFF) / 255.0f;
        const float g = ((m.kdArgb >> 8) & 0xFF) / 255.0f;
        const float b = (m.kdArgb & 0xFF) / 255.0f;
        const bool lineMat = m.name.rfind("line_", 0) == 0;
        json << "{\"name\":\"" << JsonEscapeStr(m.name) << "\","
             << "\"pbrMetallicRoughness\":{";
        if (matTexIndex[mi] >= 0) {
            json << "\"baseColorTexture\":{\"index\":" << matTexIndex[mi] << "},"
                 << "\"baseColorFactor\":[1,1,1,1]";
        } else {
            json << "\"baseColorFactor\":[" << r << "," << g << "," << b << ",1]";
        }
        json << ",\"metallicFactor\":0,\"roughnessFactor\":1}";
        if (lineMat) json << ",\"doubleSided\":true";
        else json << ",\"doubleSided\":false";  // coincident front+back need cull
        if (m.transparent) json << ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.1";
        json << ",\"extras\":{"
             << "\"punch\":" << (m.punch ? "true" : "false") << ","
             << "\"decal\":" << (m.decal ? "true" : "false") << ","
             << "\"texInfo\":" << (int)m.texInfo << ","
             << "\"painterOrder\":\""
             << (m.punch ? "punch" : m.decal ? "overlay" : "hull") << "\","
             << "\"drawAfterHull\":" << ((m.punch || m.decal) ? "true" : "false")
             << ",\"depthWrite\":" << ((m.punch || m.decal) ? "false" : "true");
        if (m.punch || m.decal) {
            json << ",\"depthBias\":" << kShipExportPainterDepthBias
                 << ",\"slopeScaleDepthBias\":" << kShipExportPainterSlopeScale;
        }
        json << "}";
        json << "}";
    }
    // Curve material (always last if curves exist)
    int curveMatIndex = -1;
    if (curveVertCount > 0) {
        curveMatIndex = (int)mesh.materials.size();
        if (!mesh.materials.empty()) json << ",";
        json << "{\"name\":\"curves\",\"doubleSided\":true,"
                "\"pbrMetallicRoughness\":{"
                "\"baseColorFactor\":[0.85,0.75,0.35,1],"
                "\"metallicFactor\":0,\"roughnessFactor\":1},"
                "\"extras\":{\"contour_polyline\":true}}";
    }
    json << "],";

    // meshes — hull / punch / overlay split so DCC can depth-bias independently
    bool hasHullPrim = false, hasPunchPrim = false, hasDecalPrim = false;
    for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
        if (accIdx[mi] < 0) continue;
        if (mesh.materials[mi].punch) hasPunchPrim = true;
        else if (mesh.materials[mi].decal) hasDecalPrim = true;
        else hasHullPrim = true;
    }

    auto emitPrims = [&](bool wantPunch, bool wantDecal, bool wantHull) {
        bool firstPrim = true;
        for (size_t mi = 0; mi < mesh.materials.size(); mi++) {
            if (accIdx[mi] < 0) continue;
            const ShipExportMaterial& m = mesh.materials[mi];
            const bool isP = m.punch;
            const bool isD = m.decal;
            const bool isH = !isP && !isD;
            if ((isP && !wantPunch) || (isD && !wantDecal) || (isH && !wantHull))
                continue;
            if (!firstPrim) json << ",";
            firstPrim = false;
            json << "{\"attributes\":{\"POSITION\":" << accPos << ",\"NORMAL\":"
                 << accNrm << ",\"TEXCOORD_0\":" << accUv << "},"
                 << "\"indices\":" << accIdx[mi] << ",\"material\":" << mi;
            if (isP || isD) {
                json << ",\"extras\":{\"painterOrder\":\""
                     << (isP ? "punch" : "overlay") << "\"}";
            }
            json << "}";
        }
    };

    json << "\"meshes\":[";
    int meshHull = -1, meshPunch = -1, meshDecal = -1, meshCurves = -1;
    int nextMesh = 0;
    bool firstMesh = true;
    auto emitNamedMesh = [&](const char* name, bool p, bool d, bool h) -> int {
        if (firstMesh) firstMesh = false;
        else json << ",";
        json << "{\"name\":\"" << JsonEscapeStr(name) << "\",\"primitives\":[";
        emitPrims(p, d, h);
        json << "]}";
        return nextMesh++;
    };
    if (hasHullPrim) meshHull = emitNamedMesh(mesh.name.c_str(), false, false, true);
    if (hasPunchPrim) meshPunch = emitNamedMesh("punch", true, false, false);
    if (hasDecalPrim) meshDecal = emitNamedMesh("decal", false, true, false);
    if (curveVertCount > 0 && curveMatIndex >= 0) {
        if (firstMesh) firstMesh = false;
        else json << ",";
        meshCurves = nextMesh++;
        json << "{\"name\":\"curves\",\"primitives\":[{"
             << "\"attributes\":{\"POSITION\":" << accCurvePos << "},"
             << "\"indices\":" << accCurveIdx << ",\"mode\":1,"
             << "\"material\":" << curveMatIndex << "}]}";
    }
    json << "],";

    // nodes
    json << "\"nodes\":[";
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%03d_%s", mesh.shipIndex, mesh.name.c_str());
        json << "{\"name\":\"" << JsonEscapeStr(buf) << "\"";
        if (meshHull >= 0) json << ",\"mesh\":" << meshHull;
        std::vector<int> kids;
        int nextNode = 1;
        const int nodePunch = hasPunchPrim ? nextNode++ : -1;
        const int nodeDecal = hasDecalPrim ? nextNode++ : -1;
        const int nodeCurves = (meshCurves >= 0) ? nextNode++ : -1;
        if (nodePunch >= 0) kids.push_back(nodePunch);
        if (nodeDecal >= 0) kids.push_back(nodeDecal);
        if (nodeCurves >= 0) kids.push_back(nodeCurves);
        const int spriteBase = nextNode;
        nextNode += (int)mesh.sprites.size();
        for (size_t i = 0; i < mesh.sprites.size(); i++)
            kids.push_back(spriteBase + (int)i);
        const int emitterBase = nextNode;
        for (size_t i = 0; i < mesh.emitters.size(); i++)
            kids.push_back(emitterBase + (int)i);
        if (!kids.empty()) {
            json << ",\"children\":[";
            for (size_t i = 0; i < kids.size(); i++) {
                if (i) json << ",";
                json << kids[i];
            }
            json << "]";
        }
        json << ",\"extras\":{\"shipIndex\":" << mesh.shipIndex
             << ",\"scale\":" << s
             << ",\"captureDist\":" << (int)mesh.shipDist
             << ",\"hullTri\":" << mesh.hullTriCount
             << ",\"punchTri\":" << mesh.punchTriCount
             << ",\"decalTri\":" << mesh.decalTriCount
             << ",\"curves\":" << mesh.contourCurveCount
             << ",\"emitters\":" << mesh.emitterCount
             << ",\"trianglePrimitiveIds\":[";
        for (size_t ti = 0; ti < mesh.triMeta.size(); ti++) {
            if (ti) json << ",";
            json << mesh.triMeta[ti].primitiveId;
        }
        json << "]"
             << ",\"authoredOpposedCount\":" << mesh.authoredOpposedCount
             << ",\"pairHandednessFixCount\":" << mesh.pairHandednessFixCount
             << ",\"windingFlipCount\":" << mesh.windingFlipCount
             << ",\"minAbsWindingCos\":" << mesh.minAbsWindingCos
             << ",\"ambiguousCosBandCount\":" << mesh.ambiguousCosBandCount
             << ",\"nonPlanarQuadCount\":" << mesh.nonPlanarQuadCount
             << ",\"maxNonPlanarDev\":" << mesh.maxNonPlanarDev
             << ",\"nonPlanarFlipDisagreeCount\":" << mesh.nonPlanarFlipDisagreeCount
             << ",\"quadDiag02Count\":" << mesh.quadDiag02Count
             << ",\"quadDiag13Count\":" << mesh.quadDiag13Count
             << ",\"maxOverlaySubLatticeInset\":" << mesh.maxOverlaySubLatticeInset
             << ",\"maxPunchSubLatticeInset\":" << mesh.maxPunchSubLatticeInset
             << ",\"overlayNormalBias\":" << kShipExportOverlayNormalBias
             << ",\"latticeUnit\":" << kShipExportLatticeUnit
             << ",\"windingRepairCount\":" << mesh.vnSplitCount
             << ",\"mirrorWindingFailCount\":" << mesh.mirrorWindingFailCount
             << ",\"painterOrder\":\"hull\"}}";
        if (hasPunchPrim) {
            char pn[160];
            std::snprintf(pn, sizeof(pn), "%03d_%s_punch", mesh.shipIndex,
                          mesh.name.c_str());
            json << ",{\"name\":\"" << JsonEscapeStr(pn) << "\",\"mesh\":"
                 << meshPunch << ",\"extras\":{\"painterOrder\":\"punch\","
                 << "\"drawAfterHull\":true,\"depthWrite\":false,"
                 << "\"depthBias\":" << kShipExportPainterDepthBias
                 << ",\"slopeScaleDepthBias\":" << kShipExportPainterSlopeScale
                 << "}}";
        }
        if (hasDecalPrim) {
            char dn[160];
            std::snprintf(dn, sizeof(dn), "%03d_%s_decal", mesh.shipIndex,
                          mesh.name.c_str());
            json << ",{\"name\":\"" << JsonEscapeStr(dn) << "\",\"mesh\":"
                 << meshDecal << ",\"extras\":{\"painterOrder\":\"overlay\","
                 << "\"drawAfterHull\":true,\"depthWrite\":false,"
                 << "\"depthBias\":" << kShipExportPainterDepthBias
                 << ",\"slopeScaleDepthBias\":" << kShipExportPainterSlopeScale
                 << "}}";
        }
        if (meshCurves >= 0) {
            json << ",{\"name\":\"curves\",\"mesh\":" << meshCurves << "}";
        }
    }
    for (size_t i = 0; i < mesh.sprites.size(); i++) {
        const ShipExportSpriteChild& c = mesh.sprites[i];
        char nm[64];
        std::snprintf(nm, sizeof(nm), "sprite_%d", c.modelId);
        // Column-major 4x4 from 3x3 rot + translation (export scale).
        const float m[16] = {
            c.rot[0], c.rot[3], c.rot[6], 0,
            c.rot[1], c.rot[4], c.rot[7], 0,
            c.rot[2], c.rot[5], c.rot[8], 0,
            c.pos[0] * s, c.pos[1] * s, c.pos[2] * s, 1};
        json << ",{\"name\":\"" << nm << "\",\"matrix\":[";
        for (int k = 0; k < 16; k++) {
            if (k) json << ",";
            json << m[k];
        }
        json << "],\"extras\":{\"kind\":\"spriteChild\",\"modelId\":"
             << c.modelId << ",\"scaleRelated\":" << c.scaleRelated << "}}";
    }
    for (size_t i = 0; i < mesh.emitters.size(); i++) {
        const ShipExportEmitter& e = mesh.emitters[i];
        const char* kind = "navlight";
        if (e.kind == ShipExportEmitterKind::Thruster) kind = "thruster";
        else if (e.kind == ShipExportEmitterKind::Glow) kind = "glow";
        char nm[64];
        std::snprintf(nm, sizeof(nm), "%s_%zu", kind, i);
        json << ",{\"name\":\"" << nm << "\",\"translation\":["
             << (e.x * s) << "," << (e.y * s) << "," << (e.z * s) << "],"
             << "\"extras\":{\"kind\":\"" << kind << "\","
             << "\"color\":[" << e.rgb[0] << "," << e.rgb[1] << "," << e.rgb[2]
             << "],\"size\":" << e.size
             << ",\"vertexIdx\":" << e.vertexIdx
             << ",\"blink\":" << (e.blink ? "true" : "false")
             << ",\"texInfo\":" << (int)e.texInfo;
        if (e.kind == ShipExportEmitterKind::Thruster) {
            json << ",\"pos1\":[" << (e.x1 * s) << "," << (e.y1 * s) << ","
                 << (e.z1 * s) << "],\"vertexIdx1\":" << e.vertexIdx1;
        }
        json << "}}";
    }
    json << "]";
    json << "}";

    std::string jsonStr = json.str();
    while (jsonStr.size() % 4) jsonStr.push_back(' ');

    const uint32_t jsonLen = (uint32_t)jsonStr.size();
    const uint32_t binLen = (uint32_t)bin.size();
    const uint32_t total =
        12 + 8 + jsonLen + 8 + binLen;

    char path[1200];
    std::snprintf(path, sizeof(path), "%s/model.glb", dir);
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    auto wrU32 = [&](uint32_t v) { fwrite(&v, 1, 4, f); };
    fwrite("glTF", 1, 4, f);
    wrU32(2);
    wrU32(total);
    wrU32(jsonLen);
    wrU32(0x4E4F534Au);  // JSON
    fwrite(jsonStr.data(), 1, jsonStr.size(), f);
    wrU32(binLen);
    wrU32(0x004E4942u);  // BIN
    if (binLen) fwrite(bin.data(), 1, bin.size(), f);
    fclose(f);
    return true;
}

int ShipExportCheckInvariants(const ShipExportMesh& mesh, const ShipModel* model)
{
    if (!mesh.valid) return 1;
    const int nTri = (int)mesh.indices.size() / 3;
    if ((int)mesh.indices.size() % 3 != 0) return 2;
    if (mesh.contourTriCount != 0) return 11; // filled ContourHull is paused
    if (mesh.contourCurveCount != (int)mesh.curves.size()) return 12;
    const int front = mesh.hullTriCount + mesh.punchTriCount +
                      mesh.decalTriCount + mesh.lineTriCount;
    const int rev = nTri - front;
    // Lines are double-sided; reverse may drop if a back-face degenerates.
    if (rev < 0 || rev > mesh.lineTriCount) return 3;
    if (mesh.hullTriCount + mesh.punchTriCount + mesh.decalTriCount !=
        mesh.expectedTriCount)
        return 4; // T7: hull+punch+decal still matches independent count
    if ((int)mesh.triMaterial.size() != nTri) return 5;
    if ((int)mesh.triMeta.size() != nTri) return 14; // W1 parallel meta

    float rMax = 0.f;
    for (const ShipExportVert& v : mesh.verts) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            !std::isfinite(v.nx) || !std::isfinite(v.ny) || !std::isfinite(v.nz))
            return 6; // T1
        const float d = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (d > rMax) rMax = d;
    }
    // Parent radius does not bound sprite-child geometry (title #346 etc.).
    if (model && !mesh.hasSpriteChild) {
        const int shift = (int)(model->scaleRelated & 31);
        const float worldR =
            ((float)((uint64_t)model->radius << shift)) / (float)FP_ONE;
        if (worldR > 0.f && rMax > worldR * 32.f + 4.f) return 7;
    }

    for (uint32_t ix : mesh.indices) {
        if (ix >= (uint32_t)mesh.verts.size()) return 8; // T2
    }

    for (int t = 0; t < nTri; t++) {
        const int mi = mesh.triMaterial[t];
        if (mi < 0 || mi >= (int)mesh.materials.size()) return 9; // T4
        const ShipExportMaterial& m = mesh.materials[mi];
        if (m.textured && m.baked.argb.empty()) return 10; // T4
    }

    // T11 post-condition — after winding-align, vn must agree with winding.
    // Assertion that the pass worked (vacuous as a root-cause detector).
    // Real diagnostic: mesh.authoredOpposedCount (pre-fix, in OBJ/glTF extras).
    // T14 (mirror winding) is recorded on the mesh during BuildExportMesh as
    // mirrorWindingFailCount — census only until fleet failures are classified.
    for (int t = 0; t < nTri; t++) {
        const int mi = mesh.triMaterial[t];
        if (mi >= 0 && mi < (int)mesh.materials.size() &&
            mesh.materials[mi].name.rfind("line_", 0) == 0)
            continue;
        const ShipExportVert& a = mesh.verts[mesh.indices[(size_t)t * 3 + 0]];
        const ShipExportVert& b = mesh.verts[mesh.indices[(size_t)t * 3 + 1]];
        const ShipExportVert& c = mesh.verts[mesh.indices[(size_t)t * 3 + 2]];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float wx = uy * vz - uz * vy;
        const float wy = uz * vx - ux * vz;
        const float wz = ux * vy - uy * vx;
        const float wLen = std::sqrt(wx * wx + wy * wy + wz * wz);
        const float nx = (a.nx + b.nx + c.nx) * (1.f / 3.f);
        const float ny = (a.ny + b.ny + c.ny) * (1.f / 3.f);
        const float nz = (a.nz + b.nz + c.nz) * (1.f / 3.f);
        const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (wLen < 1e-5f || nLen < 1e-5f) continue;
        const float cos = (wx * nx + wy * ny + wz * nz) / (wLen * nLen);
        if (cos < -0.5f) return 13; // T11 post-condition failed
    }

    return 0;
}

int ShipExportCheckPrimitiveIds(const ShipExportMesh& mesh)
{
    if (!mesh.valid) return 1;
    const int nTri = (int)mesh.indices.size() / 3;
    if ((int)mesh.triMeta.size() != nTri) return 2;
    if ((int)mesh.triMaterial.size() != nTri) return 3;
    std::unordered_set<uint32_t> seen;
    seen.reserve((size_t)nTri * 2);
    for (int t = 0; t < nTri; t++) {
        const ShipExportTriMeta& tm = mesh.triMeta[(size_t)t];
        // id 0 is valid: sourceOrder=0, fan=0, front face.
        if (!seen.insert(tm.primitiveId).second) return 5;  // duplicate
        if (tm.sourceOrder >= 0) {
            const uint32_t expect =
                ShipExportMakePrimitiveId(tm.sourceOrder, tm.fanOrdinal,
                                          tm.reverseSide);
            if (tm.primitiveId != expect) return 7;
        } else if ((tm.primitiveId & kShipExportPrimGeneratedBase) == 0) {
            return 8;  // generated must use high bit
        }
    }
    return 0;
}

} // namespace jjffe
