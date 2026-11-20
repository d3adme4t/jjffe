#include "jjffe/d3d/d3d_mesh.h"
#include "jjffe/d3d/d3d_backend.h"
#include "jjffe/body_paint.h"
#include "jjffe/ship_model_asset.h"
#include "jjffe/ship_export.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#include "stb_image.h"

namespace jjffe {
namespace d3d {

namespace {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    DWORD color;
    float u, v;
};

constexpr DWORD kFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;

bool FileExists(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

IDirect3DTexture9* LoadTextureFile(IDirect3DDevice9* dev, const char* path)
{
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return nullptr;
    }

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < h; y++) {
            auto* dst = reinterpret_cast<DWORD*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
            const unsigned char* src = pixels + y * w * 4;
            for (int x = 0; x < w; x++) {
                const unsigned char r = src[x * 4 + 0];
                const unsigned char g = src[x * 4 + 1];
                const unsigned char b = src[x * 4 + 2];
                const unsigned char a = src[x * 4 + 3];
                dst[x] = (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | DWORD(b);
            }
        }
        tex->UnlockRect(0);
    }
    stbi_image_free(pixels);
    return tex;
}

// Glow maps are often RGB-on-black with A=0. Fold luminance into alpha so
// additive / SRCALPHA blends actually show.
[[maybe_unused]] IDirect3DTexture9* LoadGlowTextureFile(IDirect3DDevice9* dev, const char* path)
{
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load(path, &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return nullptr;
    }

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr))) {
        stbi_image_free(pixels);
        return nullptr;
    }

    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        for (int y = 0; y < h; y++) {
            auto* dst = reinterpret_cast<DWORD*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
            const unsigned char* src = pixels + y * w * 4;
            for (int x = 0; x < w; x++) {
                const unsigned char r = src[x * 4 + 0];
                const unsigned char g = src[x * 4 + 1];
                const unsigned char b = src[x * 4 + 2];
                unsigned char a = src[x * 4 + 3];
                const unsigned char lum = (unsigned char)((int)r > (int)g ? ((int)r > (int)b ? r : b)
                                                                         : ((int)g > (int)b ? g : b));
                if (lum > a) a = lum;
                dst[x] = (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | DWORD(b);
            }
        }
        tex->UnlockRect(0);
    } else {
        tex->Release();
        stbi_image_free(pixels);
        return nullptr;
    }
    stbi_image_free(pixels);
    return tex;
}

IDirect3DTexture9* TryLoadSkinBeside(IDirect3DDevice9* dev, const char* modelPath)
{
    std::string dir(modelPath);
    const size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos) dir.clear();
    else dir.resize(slash + 1);

    static const char* kNames[] = {
        "skin.png", "skin.PNG", "skin1.png", "skin2.png", "Skin.png", "texture.png"
    };
    for (const char* name : kNames) {
        const std::string path = dir + name;
        if (!FileExists(path.c_str())) continue;
        IDirect3DTexture9* tex = LoadTextureFile(dev, path.c_str());
        if (tex) {
            printf("D3D: loaded texture %s\n", path.c_str());
            return tex;
        }
    }
    return nullptr;
}

bool UploadMesh(IDirect3DDevice9* dev, const std::vector<Vertex>& verts,
                const std::vector<uint16_t>& indices, MeshGpu* out)
{
    if (!dev || !out || verts.empty() || indices.empty()) return false;

    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    const UINT vBytes = (UINT)(verts.size() * sizeof(Vertex));
    const UINT iBytes = (UINT)(indices.size() * sizeof(uint16_t));

    if (FAILED(dev->CreateVertexBuffer(vBytes, 0, kFVF, D3DPOOL_MANAGED, &vb, nullptr)))
        return false;
    if (FAILED(dev->CreateIndexBuffer(iBytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr))) {
        vb->Release();
        return false;
    }

    void* p = nullptr;
    if (SUCCEEDED(vb->Lock(0, 0, &p, 0))) {
        std::memcpy(p, verts.data(), vBytes);
        vb->Unlock();
    }
    if (SUCCEEDED(ib->Lock(0, 0, &p, 0))) {
        std::memcpy(p, indices.data(), iBytes);
        ib->Unlock();
    }

    float r2 = 0.0f;
    for (const Vertex& v : verts) {
        const float d = v.x * v.x + v.y * v.y + v.z * v.z;
        if (d > r2) r2 = d;
    }

    out->vb = vb;
    out->ib = ib;
    out->vertexCount = (int)verts.size();
    out->indexCount = (int)indices.size();
    out->radius = std::sqrt(r2);
    if (out->radius < 1e-3f) out->radius = 1.0f;
    return true;
}

} // namespace

bool D3dIsPlanetModel(int modelId)
{
    // 113–116: asteroid props (demo still allowed). 117–148: G3 sphere set
    // including stars 137–148 (Sol 141). Keep the handful of extra demo ids.
    return (modelId >= 113 && modelId <= 148) || modelId == 445 || modelId == 447 ||
           modelId == 449;
}

bool D3dFindModelPath(int modelId, char* out, int outSize)
{
    if (!out || outSize < 16) return false;
    const char* root = D3dGetDataRoot();
    if (!root || !root[0]) return false;

    char path[1024];
    static const char* kPatterns[] = {
        "%s/MODELS/%d/model.X",
        "%s/MODELS/%d/model.x",
        "%s/models/%d/model.X",
        "%s/models/%d/model.x",
        "%s/MODELS/%d/Model.X",
    };
    for (const char* pat : kPatterns) {
        std::snprintf(path, sizeof(path), pat, root, modelId);
        if (FileExists(path)) {
            std::snprintf(out, outSize, "%s", path);
            return true;
        }
    }
    return false;
}

bool D3dLoadMeshFile(IDirect3DDevice9* dev, const char* path, MeshGpu* out)
{
    if (!dev || !path || !out) return false;
    *out = {};

    Assimp::Importer importer;
    // Keep authoring normals — GenSmoothNormals was regenerating them and inverting
    // lighting on some FFED3D .x files (e.g. Cobra 38). ConvertToLeftHanded for D3D
    // UVs/winding; FixInfacingNormals repairs meshes that still face inward.
    const unsigned flags =
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType | aiProcess_ConvertToLeftHanded | aiProcess_FixInfacingNormals;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->HasMeshes()) {
        fprintf(stderr, "D3D: Assimp failed to load %s: %s\n", path, importer.GetErrorString());
        return false;
    }

    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    verts.reserve(4096);
    indices.reserve(8192);

    // Match d3d_backend key light — used to bake |N·L| so Cull-NONE backfaces
    // (Viper wing underside) don't shade a darker blue than the other wing.
    const float lightX = 0.35f, lightY = -0.55f, lightZ = 0.75f;
    const float lightLen = std::sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);

    auto repairSubmeshNormals = [](std::vector<Vertex>& sv, const std::vector<uint16_t>& si) {
        if (sv.empty() || si.size() < 3) return false;
        float cx = 0, cy = 0, cz = 0;
        for (const Vertex& v : sv) {
            cx += v.x;
            cy += v.y;
            cz += v.z;
        }
        const float inv = 1.0f / (float)sv.size();
        cx *= inv;
        cy *= inv;
        cz *= inv;
        int faceInward = 0, faceOutward = 0;
        int normDisagree = 0, normAgree = 0;
        for (size_t i = 0; i + 2 < si.size(); i += 3) {
            const Vertex& a = sv[si[i]];
            const Vertex& b = sv[si[i + 1]];
            const Vertex& c = sv[si[i + 2]];
            const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
            const float e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
            const float gx = e1y * e2z - e1z * e2y;
            const float gy = e1z * e2x - e1x * e2z;
            const float gz = e1x * e2y - e1y * e2x;
            const float fx = (a.x + b.x + c.x) * (1.0f / 3.0f) - cx;
            const float fy = (a.y + b.y + c.y) * (1.0f / 3.0f) - cy;
            const float fz = (a.z + b.z + c.z) * (1.0f / 3.0f) - cz;
            if (gx * fx + gy * fy + gz * fz < 0.0f) faceInward++;
            else faceOutward++;
            const float nx = (a.nx + b.nx + c.nx) * (1.0f / 3.0f);
            const float ny = (a.ny + b.ny + c.ny) * (1.0f / 3.0f);
            const float nz = (a.nz + b.nz + c.nz) * (1.0f / 3.0f);
            if (gx * nx + gy * ny + gz * nz < 0.0f) normDisagree++;
            else normAgree++;
        }
        if ((faceInward > faceOutward && faceInward > 0) ||
            (normDisagree > normAgree && normDisagree > 0)) {
            for (Vertex& v : sv) {
                v.nx = -v.nx;
                v.ny = -v.ny;
                v.nz = -v.nz;
            }
            return true;
        }
        return false;
    };

    int flippedSubs = 0;
    for (unsigned mi = 0; mi < scene->mNumMeshes; mi++) {
        const aiMesh* mesh = scene->mMeshes[mi];
        // Skip line/point junk (Cobra embeds line primitives alongside tris).
        if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) continue;
        if (!mesh->HasFaces() || mesh->mNumVertices == 0) continue;

        std::vector<Vertex> subVerts;
        std::vector<uint16_t> subIdx;
        subVerts.reserve(mesh->mNumVertices);
        subIdx.reserve(mesh->mNumFaces * 3);

        for (unsigned vi = 0; vi < mesh->mNumVertices; vi++) {
            Vertex v{};
            v.x = mesh->mVertices[vi].x;
            v.y = mesh->mVertices[vi].y;
            v.z = mesh->mVertices[vi].z;
            if (mesh->HasNormals()) {
                v.nx = mesh->mNormals[vi].x;
                v.ny = mesh->mNormals[vi].y;
                v.nz = mesh->mNormals[vi].z;
            } else {
                v.nx = 0;
                v.ny = 1;
                v.nz = 0;
            }
            v.color = 0xFFFFFFFFu;
            if (mesh->HasTextureCoords(0)) {
                v.u = mesh->mTextureCoords[0][vi].x;
                v.v = mesh->mTextureCoords[0][vi].y;
            } else {
                v.u = v.v = 0.0f;
            }
            subVerts.push_back(v);
        }
        for (unsigned fi = 0; fi < mesh->mNumFaces; fi++) {
            const aiFace& f = mesh->mFaces[fi];
            if (f.mNumIndices != 3) continue;
            subIdx.push_back((uint16_t)f.mIndices[0]);
            subIdx.push_back((uint16_t)f.mIndices[1]);
            subIdx.push_back((uint16_t)f.mIndices[2]);
        }
        if (subIdx.empty()) continue;

        if (repairSubmeshNormals(subVerts, subIdx)) flippedSubs++;

        // Bake two-sided shade into diffuse: abs(N·L) so mirrored wings match.
        for (Vertex& v : subVerts) {
            float ndl = (v.nx * lightX + v.ny * lightY + v.nz * lightZ) / lightLen;
            if (ndl < 0.0f) ndl = -ndl;
            const float s = 0.55f + 0.45f * ndl;
            const DWORD c = (DWORD)(s * 255.0f + 0.5f);
            v.color = 0xFF000000u | (c << 16) | (c << 8) | c;
        }

        if (verts.size() + subVerts.size() > 0xFFFFu) {
            fprintf(stderr, "D3D: mesh %s too large for 16-bit indices\n", path);
            return false;
        }
        const unsigned base = (unsigned)verts.size();
        verts.insert(verts.end(), subVerts.begin(), subVerts.end());
        for (uint16_t idx : subIdx)
            indices.push_back((uint16_t)(base + idx));
    }

    if (verts.empty() || indices.empty()) {
        fprintf(stderr, "D3D: no triangle mesh data in %s\n", path);
        return false;
    }
    if (flippedSubs > 0)
        printf("D3D: flipped normals on %d submesh(es) in %s\n", flippedSubs, path);

    if (!UploadMesh(dev, verts, indices, out)) return false;
    out->tex = TryLoadSkinBeside(dev, path);
    printf("D3D: loaded mesh %s (%d verts, %d indices, r=%.2f)\n", path, out->vertexCount,
           out->indexCount, out->radius);
    return true;
}

bool D3dLoadModelId(IDirect3DDevice9* dev, int modelId, MeshGpu* out)
{
    char path[1024];
    if (!D3dFindModelPath(modelId, path, (int)sizeof(path))) {
        fprintf(stderr, "D3D: no model.x for id %d under %s\n", modelId,
                D3dGetDataRoot() ? D3dGetDataRoot() : "(null)");
        return false;
    }
    return D3dLoadMeshFile(dev, path, out);
}

namespace {

void Clamp01(float& r, float& g, float& b)
{
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 1) r = 1;
    if (g > 1) g = 1;
    if (b > 1) b = 1;
}

DWORD PackRgb(float r, float g, float b)
{
    Clamp01(r, g, b);
    return 0xFF000000u | ((DWORD)(r * 255.0f) << 16) | ((DWORD)(g * 255.0f) << 8) |
           (DWORD)(b * 255.0f);
}

DWORD PackRgba(float r, float g, float b, float a)
{
    Clamp01(r, g, b);
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return ((DWORD)(a * 255.0f) << 24) | ((DWORD)(r * 255.0f) << 16) |
           ((DWORD)(g * 255.0f) << 8) | (DWORD)(b * 255.0f);
}

float Smoothstep(float edge0, float edge1, float x)
{
    if (edge1 == edge0) return x >= edge1 ? 1.f : 0.f;
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return t * t * (3.f - 2.f * t);
}

void Mul44(D3DMATRIX* o, const D3DMATRIX& a, const D3DMATRIX& b)
{
    D3DMATRIX r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                        a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    *o = r;
}

// Billboard half-extent / core radius. Linear: 12 → 3×, 8 → 2×.
float CoronaExtent(float coronaScale)
{
    const float e = coronaScale * 0.25f;
    return (e < 1.2f) ? 1.2f : e;
}

// Glow fills limb→quad edge so corona_scale is the visible diameter.
// Corners RGB=0 so additive is not a square.
void BakeCoronaDisc(uint32_t* pix, int n, float limb)
{
    if (limb < 0.15f) limb = 0.15f;
    if (limb > 0.80f) limb = 0.80f;
    const float mid = (static_cast<float>(n) - 1.f) * 0.5f;
    const float invR = 1.f / (static_cast<float>(n) * 0.5f);
    const float inner = limb * 0.92f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float u = (static_cast<float>(x) - mid) * invR;
            const float v = (static_cast<float>(y) - mid) * invR;
            const float r = std::sqrt(u * u + v * v);
            float t = 0.f;
            if (r < 1.f && r > inner) {
                if (r <= limb) {
                    t = (r - inner) / (limb - inner);
                } else {
                    const float uo = (r - limb) / (1.f - limb);
                    const float m = 1.f - uo;
                    t = m * m;
                }
            }
            const uint32_t a = static_cast<uint32_t>(t * 220.f + 0.5f);
            const uint32_t R = static_cast<uint32_t>(t * 255.f + 0.5f);
            const uint32_t G = static_cast<uint32_t>(t * 190.f + 0.5f);
            const uint32_t B = static_cast<uint32_t>(t * 70.f + 0.5f);
            pix[y * n + x] = (a << 24) | (R << 16) | (G << 8) | B;
        }
    }
}

} // namespace

bool D3dCreateSphereMesh(IDirect3DDevice9* dev, int segments, int rings, MeshGpu* out)
{
    if (segments < 3 || rings < 2) {
        BodySphereLodParams(BodySphereLod::High, &segments, &rings);
    }
    if (!dev || !out) return false;
    BodySphereMesh gen;
    if (!BodySphereGenerate(segments, rings, &gen)) return false;
    std::vector<Vertex> verts;
    verts.resize(static_cast<size_t>(gen.vertCount));
    for (int i = 0; i < gen.vertCount; ++i) {
        const BodySphereVert& s = gen.verts[i];
        Vertex& v = verts[static_cast<size_t>(i)];
        v.x = s.x; v.y = s.y; v.z = s.z;
        v.nx = s.nx; v.ny = s.ny; v.nz = s.nz;
        v.u = s.u; v.v = s.v;
        v.color = 0xFFFFFFFFu;
    }
    std::vector<uint16_t> indices(gen.indices, gen.indices + gen.indexCount);
    *out = {};
    if (!UploadMesh(dev, verts, indices, out)) return false;
    out->radius = 1.0f;
    return true;
}

IDirect3DTexture9* CreateTextureArgb(IDirect3DDevice9* dev, int w, int h,
                                     const uint32_t* argb);

bool D3dCreatePlanetMesh(IDirect3DDevice9* dev, int modelId, MeshGpu* out,
                         const char* bodyName)
{
    if (!dev || !out) return false;
    int segments = 0, rings = 0;
    BodySphereLodParams(BodySphereLod::High, &segments, &rings);
    BodySphereMesh gen;
    if (!BodySphereGenerate(segments, rings, &gen)) return false;

    static TexturesCfg s_cfg{};
    static bool s_cfgTried = false;
    static bool s_cfgOk = false;
    if (!s_cfgTried) {
        s_cfgTried = true;
        char cfgPath[1024] = {};
        if (BodyPaintResolveCfgFile(cfgPath, (int)sizeof(cfgPath), D3dGetDataRoot())) {
            s_cfgOk = TexturesCfgLoad(&s_cfg, cfgPath);
            if (s_cfgOk) {
                printf("D3D: textures.cfg %s\n", cfgPath);
            }
        }
    }

    const TexturesCfgEntry* ent = s_cfgOk ? TexturesCfgLookup(&s_cfg, modelId) : nullptr;
    BodyMaterial mat = BodyPaintPick(modelId, 0, ent, bodyName);
    BodyPaintFinalizeWrap(&mat, D3dGetDataRoot());

    char wrapPath[1024] = {};
    const bool known = BodyPaintIsKnownPlanet(modelId);
    const bool emissive = mat.mode == BodyPaintMode::Emissive;
    char corePath[1024] = {};
    const bool coreWrap = emissive &&
                          BodyPaintResolveTexFile(mat.wrapTex, corePath, (int)sizeof(corePath),
                                                  D3dGetDataRoot());
    const bool wrap = !known && !emissive && mat.mode == BodyPaintMode::Wrap &&
                      BodyPaintResolveTexFile(mat.wrapTex, wrapPath, (int)sizeof(wrapPath),
                                              D3dGetDataRoot());
    const char* litWrapPath = wrap ? wrapPath : (coreWrap ? corePath : nullptr);

    const bool procedural = !known && !emissive && !wrap;
    const BodyProcClass procClass = BodyPaintProcClass(modelId);
    const float lightX = 0.35f, lightY = -0.55f, lightZ = 0.75f;
    const float lightLen = std::sqrt(lightX * lightX + lightY * lightY + lightZ * lightZ);

    std::vector<Vertex> verts;
    verts.resize(static_cast<size_t>(gen.vertCount));
    for (int i = 0; i < gen.vertCount; ++i) {
        const BodySphereVert& s = gen.verts[i];
        Vertex& v = verts[static_cast<size_t>(i)];
        v.x = s.x; v.y = s.y; v.z = s.z;
        v.nx = s.nx; v.ny = s.ny; v.nz = s.nz;
        v.u = s.u; v.v = s.v;
        if (emissive) {
            // Flat unlit disc — N·L would make a sun look like a planet.
            v.color = PackRgb(1.0f, 0.95f, 0.82f);
        } else {
            float ndl = (v.nx * lightX + v.ny * lightY + v.nz * lightZ) / lightLen;
            if (ndl < 0.0f) ndl = -ndl;
            const float shade = 0.40f + 0.60f * ndl;
            v.color = PackRgb(shade, shade, shade);
        }
    }
    std::vector<uint16_t> indices(gen.indices, gen.indices + gen.indexCount);

    *out = {};
    if (!UploadMesh(dev, verts, indices, out)) return false;
    out->radius = 1.0f;
    if (known) {
        const int bw = 1024, bh = 512;
        std::vector<uint32_t> argb(static_cast<size_t>(bw * bh));
        if (BodyPaintBakeKnownEquirect(modelId, argb.data(), bw, bh)) {
            out->tex = CreateTextureArgb(dev, bw, bh, argb.data());
        }
        if (!out->tex) {
            std::fprintf(stderr, "D3D: known-planet bake failed id=%d → vertex color\n",
                         modelId);
        } else {
            printf("D3D: KNOWN bake planet id=%d %dx%d (%d verts)\n", modelId, bw, bh,
                   out->vertexCount);
        }
    } else if (litWrapPath) {
        out->tex = LoadTextureFile(dev, litWrapPath);
        if (!out->tex) {
            std::fprintf(stderr, "D3D: wrap tex load failed %s → vertex color\n", litWrapPath);
        } else {
            printf("D3D: %s planet id=%d tex=%d %s (%d verts)\n",
                   emissive ? "EMISSIVE core" : "WRAP", modelId,
                   mat.wrapTex, litWrapPath, out->vertexCount);
        }
    } else if (emissive) {
        printf("D3D: EMISSIVE planet id=%d unlit verts (core tex %d missing)\n",
               modelId, mat.wrapTex);
    } else if (procedural) {
        const int bw = 1024, bh = 512;
        std::vector<uint32_t> argb(static_cast<size_t>(bw * bh));
        if (BodyPaintBakeProceduralEquirect(modelId, mat.seed, argb.data(), bw, bh))
            out->tex = CreateTextureArgb(dev, bw, bh, argb.data());
        static const char* kNames[] = {"asteroid", "rocky", "ocean", "gas", "star"};
        printf("D3D: procedural planet id=%d class=%s %s (%d verts)\n", modelId,
               kNames[static_cast<int>(procClass)],
               out->tex ? "equirect" : "vertex-color", out->vertexCount);
    }

    if (emissive && BodyPaintWantCorona()) {
        const int scale = BodyPaintCoronaScale(mat.coronaScale);
        constexpr int cn = 256;
        std::vector<uint32_t> cpix(static_cast<size_t>(cn * cn));
        BakeCoronaDisc(cpix.data(), cn, 1.f / CoronaExtent(static_cast<float>(scale)));
        out->coronaTex = CreateTextureArgb(dev, cn, cn, cpix.data());
        out->coronaScale = static_cast<float>(scale);
        if (out->coronaTex) {
            printf("D3D: corona id=%d circular bake scale=%.0f extent=%.2f (cfg tex %d)\n",
                   modelId, out->coronaScale, CoronaExtent(out->coronaScale), mat.coronaTex);
            Vertex qv[4] = {
                {-1.f, -1.f, 0.f, 0, 0, 1, 0xFFFFFFFFu, 0.f, 1.f},
                { 1.f, -1.f, 0.f, 0, 0, 1, 0xFFFFFFFFu, 1.f, 1.f},
                { 1.f,  1.f, 0.f, 0, 0, 1, 0xFFFFFFFFu, 1.f, 0.f},
                {-1.f,  1.f, 0.f, 0, 0, 1, 0xFFFFFFFFu, 0.f, 0.f},
            };
            uint16_t qi[6] = {0, 1, 2, 0, 2, 3};
            MeshGpu quad{};
            std::vector<Vertex> qvv(qv, qv + 4);
            std::vector<uint16_t> qii(qi, qi + 6);
            if (UploadMesh(dev, qvv, qii, &quad)) {
                out->coronaVb = quad.vb;
                out->coronaIb = quad.ib;
                out->coronaVertexCount = quad.vertexCount;
                out->coronaIndexCount = quad.indexCount;
                quad.vb = nullptr;
                quad.ib = nullptr;
            }
        } else {
            std::fprintf(stderr, "D3D: corona bake failed for model %d\n", modelId);
        }
    }

    if (!emissive && mat.overlayAtmo && BodyPaintWantAtmo()) {
        const float ratio = BodyPaintAtmoRatio(mat.atmoRatio);
        char atmoPath[1024] = {};
        if (mat.atmoTex > 0 &&
            BodyPaintResolveAtmoFile(mat.atmoTex, atmoPath, (int)sizeof(atmoPath),
                                     D3dGetDataRoot())) {
            out->atmoTex = LoadTextureFile(dev, atmoPath);
            if (!out->atmoTex)
                std::fprintf(stderr, "D3D: atmo tex load failed %s\n", atmoPath);
        }
        const size_t nv = verts.size();
        std::vector<Vertex> av(nv);
        out->atmoPos = new float[nv * 3];
        for (size_t i = 0; i < nv; ++i) {
            av[i] = verts[i];
            av[i].color = 0xFFFFFFFFu;
            out->atmoPos[i * 3 + 0] = verts[i].x;
            out->atmoPos[i * 3 + 1] = verts[i].y;
            out->atmoPos[i * 3 + 2] = verts[i].z;
        }
        MeshGpu tmp{};
        if (UploadMesh(dev, av, indices, &tmp)) {
            out->atmoVb = tmp.vb;
            out->atmoVertexCount = tmp.vertexCount;
            tmp.vb = nullptr;
            if (tmp.ib) {
                static_cast<IDirect3DIndexBuffer9*>(tmp.ib)->Release();
                tmp.ib = nullptr;
            }
            out->atmoRatio = ratio;
            printf("D3D: ATMO_SHELL id=%d ratio=%.3f tex=%d %s\n", modelId, ratio, mat.atmoTex,
                   atmoPath[0] ? atmoPath : "(vertex fresnel)");
        } else {
            delete[] out->atmoPos;
            out->atmoPos = nullptr;
            if (out->atmoTex) {
                static_cast<IDirect3DTexture9*>(out->atmoTex)->Release();
                out->atmoTex = nullptr;
            }
            std::fprintf(stderr, "D3D: atmo shell upload failed for model %d\n", modelId);
        }
    }
    return true;
}

void D3dDestroyMesh(MeshGpu* mesh)
{
    if (!mesh) return;
    if (mesh->vb) {
        static_cast<IDirect3DVertexBuffer9*>(mesh->vb)->Release();
        mesh->vb = nullptr;
    }
    if (mesh->ib) {
        static_cast<IDirect3DIndexBuffer9*>(mesh->ib)->Release();
        mesh->ib = nullptr;
    }
    if (mesh->tex) {
        static_cast<IDirect3DTexture9*>(mesh->tex)->Release();
        mesh->tex = nullptr;
    }
    if (mesh->coronaTex) {
        static_cast<IDirect3DTexture9*>(mesh->coronaTex)->Release();
        mesh->coronaTex = nullptr;
    }
    if (mesh->coronaVb) {
        static_cast<IDirect3DVertexBuffer9*>(mesh->coronaVb)->Release();
        mesh->coronaVb = nullptr;
    }
    if (mesh->coronaIb) {
        static_cast<IDirect3DIndexBuffer9*>(mesh->coronaIb)->Release();
        mesh->coronaIb = nullptr;
    }
    mesh->coronaVertexCount = 0;
    mesh->coronaIndexCount = 0;
    mesh->coronaScale = 0.f;
    if (mesh->atmoTex) {
        static_cast<IDirect3DTexture9*>(mesh->atmoTex)->Release();
        mesh->atmoTex = nullptr;
    }
    if (mesh->atmoVb) {
        static_cast<IDirect3DVertexBuffer9*>(mesh->atmoVb)->Release();
        mesh->atmoVb = nullptr;
    }
    delete[] mesh->atmoPos;
    mesh->atmoPos = nullptr;
    mesh->atmoVertexCount = 0;
    mesh->atmoRatio = 0.f;
    mesh->vertexCount = 0;
    mesh->indexCount = 0;
}

IDirect3DTexture9* CreateTextureArgb(IDirect3DDevice9* dev, int w, int h,
                                     const uint32_t* argb)
{
    if (!dev || !argb || w < 1 || h < 1) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                  &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    for (int y = 0; y < h; y++) {
        auto* dst = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
        const uint32_t* src = argb + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            // ShipBakedTexture is ARGB; D3D A8R8G8B8 expects same byte order
            // on LE (0xAARRGGBB).
            dst[x] = src[x];
        }
    }
    tex->UnlockRect(0);
    return tex;
}

bool D3dUploadAssetGroup(IDirect3DDevice9* dev, const ShipAssetGroup* group,
                         float normalBias, uint32_t solidColor, MeshGpu* out,
                         const ShipExportMaterial* mat)
{
    if (!dev || !group || !out) return false;
    D3dDestroyMesh(out);
    *out = {};
    if (group->verts.empty() || group->indices.size() < 3) return false;
    if (group->verts.size() > 65535) {
        std::fprintf(stderr,
                     "D3D: asset group too large for INDEX16 (%zu verts)\n",
                     group->verts.size());
        return false;
    }

    // Match d3d_backend key light — bake |N·L| into diffuse (Cull path safe).
    // Light is in D3D LH space (after Z flip below).
    const float lx = 0.35f, ly = -0.55f, lz = 0.75f;
    const float lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float lnx = lx / lLen, lny = ly / lLen, lnz = lz / lLen;

    const int nTri = (int)group->indices.size() / 3;
    const bool perTriKd =
        (int)group->triKdArgb.size() == nTri && nTri > 0;
    const bool useTex = mat && mat->textured && !mat->baked.argb.empty() &&
                        mat->baked.width > 0 && mat->baked.height > 0;

    const float scale = kShipExportObjScale;
    std::vector<Vertex> verts;
    verts.reserve(group->verts.size());
    for (size_t vi = 0; vi < group->verts.size(); vi++) {
        const ShipExportVert& src = group->verts[vi];
        float nx = src.nx, ny = src.ny, nz = src.nz;
        const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nLen > 1e-8f) {
            nx /= nLen;
            ny /= nLen;
            nz /= nLen;
        } else {
            nx = 0.f;
            ny = 0.f;
            nz = 1.f;
        }
        // Bias in export RH space (along outward authored normals).
        float x = src.x * scale;
        float y = src.y * scale;
        float z = src.z * scale;
        if (normalBias != 0.f) {
            x += nx * normalBias;
            y += ny * normalBias;
            z += nz * normalBias;
        }
        // Assimp ConvertToLeftHanded: MakeLeftHanded (negate Z) for D3D LH
        // LookAt/Perspective. Nose (+Z export) then faces the default camera.
        z = -z;
        nz = -nz;

        uint32_t solid = solidColor;
        if (perTriKd) {
            const int tri = (int)(vi / 3);  // AppendTri pushes 3 verts per tri
            if (tri >= 0 && tri < nTri) solid = group->triKdArgb[(size_t)tri];
        }
        // Textured groups: vertex diffuse is white*shade; sample map_Kd.
        if (useTex) solid = 0xFFFFFFFFu;

        const float br = ((solid >> 16) & 0xFF) / 255.f;
        const float bg = ((solid >> 8) & 0xFF) / 255.f;
        const float bb = (solid & 0xFF) / 255.f;
        const float ba = ((solid >> 24) & 0xFF) / 255.f;

        // Already-lit export Kd (0x1000 flats): extra N·L turns 0x202020 into
        // a black hole. Textured groups still get abs(N·L) on white.
        const float ndl =
            std::fabs(nx * lnx + ny * lny + nz * lnz);
        const float shade =
            (perTriKd && !useTex) ? 1.f : (0.35f + 0.65f * ndl);
        const DWORD a = (DWORD)std::min(255, (int)(ba * 255.f + 0.5f));
        const DWORD r = (DWORD)std::min(255, (int)(br * shade * 255.f + 0.5f));
        const DWORD g = (DWORD)std::min(255, (int)(bg * shade * 255.f + 0.5f));
        const DWORD b = (DWORD)std::min(255, (int)(bb * shade * 255.f + 0.5f));
        // Flip V with Assimp FlipUVs (texture path later).
        verts.push_back({x, y, z, nx, ny, nz,
                         (a << 24) | (r << 16) | (g << 8) | b, src.u,
                         1.f - src.v});
    }

    // FlipWindingOrder: export CCW → D3D CW fronts (D3DCULL_CCW keeps them).
    std::vector<uint16_t> indices;
    indices.reserve(group->indices.size());
    for (size_t i = 0; i + 2 < group->indices.size(); i += 3) {
        const uint32_t i0 = group->indices[i];
        const uint32_t i1 = group->indices[i + 1];
        const uint32_t i2 = group->indices[i + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
            return false;
        indices.push_back((uint16_t)i0);
        indices.push_back((uint16_t)i2);
        indices.push_back((uint16_t)i1);
    }
    if (!UploadMesh(dev, verts, indices, out)) return false;
    if (useTex) {
        out->tex = CreateTextureArgb(dev, mat->baked.width, mat->baked.height,
                                     mat->baked.argb.data());
    }
    return true;
}

void D3dDrawMesh(IDirect3DDevice9* dev, const MeshGpu& mesh, bool ffLighting)
{
    if (!dev || !mesh.vb || !mesh.ib || mesh.indexCount < 3) return;
    D3dDrawMeshRange(dev, mesh, 0, mesh.indexCount, ffLighting);
}

void D3dDrawMeshRange(IDirect3DDevice9* dev, const MeshGpu& mesh, int startIndex,
                      int indexCount, bool ffLighting)
{
    if (!dev || !mesh.vb || !mesh.ib || indexCount < 3) return;
    if (startIndex < 0 || startIndex + indexCount > mesh.indexCount) return;
    auto* vb = static_cast<IDirect3DVertexBuffer9*>(mesh.vb);
    auto* ib = static_cast<IDirect3DIndexBuffer9*>(mesh.ib);
    auto* tex = static_cast<IDirect3DTexture9*>(mesh.tex);

    DWORD oldColorVertex = TRUE, oldNorm = FALSE;
    if (ffLighting) {
        dev->GetRenderState(D3DRS_COLORVERTEX, &oldColorVertex);
        dev->GetRenderState(D3DRS_NORMALIZENORMALS, &oldNorm);
        dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        // Material + Sol light; drop object-space baked N·L in vertex diffuse.
        dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    } else {
        // Vertex diffuse already has abs(N·L) baked — turn off FF lighting so Cull-NONE
        // backfaces don't get a second, darker directional shade (Viper wing).
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    }
    // Default ALPHAARG1 is TEXTURE. After a textured hull pass the next
    // untextured overlay (Viper 0x1000 flats) samples A=0 and vanishes.
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    dev->SetFVF(kFVF);
    dev->SetStreamSource(0, vb, 0, sizeof(Vertex));
    dev->SetIndices(ib);
    if (tex) {
        dev->SetTexture(0, tex);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2,
                                  ffLighting ? D3DTA_CURRENT : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    } else {
        dev->SetTexture(0, nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1,
                                  ffLighting ? D3DTA_CURRENT : D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    }
    const UINT prims = (UINT)(indexCount / 3);
    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (UINT)mesh.vertexCount,
                              (UINT)startIndex, prims);

    if (ffLighting) {
        dev->SetRenderState(D3DRS_COLORVERTEX, oldColorVertex);
        dev->SetRenderState(D3DRS_NORMALIZENORMALS, oldNorm);
    }
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
}

void D3dDrawCoronaBillboard(IDirect3DDevice9* dev, const MeshGpu& mesh, float extentMul,
                            bool zTest)
{
    if (!dev || !mesh.coronaTex || !mesh.coronaVb || !mesh.coronaIb ||
        mesh.coronaIndexCount < 3 || mesh.coronaScale <= 0.f)
        return;
    if (extentMul < 0.05f) extentMul = 0.05f;
    auto* tex = static_cast<IDirect3DTexture9*>(mesh.coronaTex);
    auto* vb = static_cast<IDirect3DVertexBuffer9*>(mesh.coronaVb);
    auto* ib = static_cast<IDirect3DIndexBuffer9*>(mesh.coronaIb);

    D3DMATRIX view{};
    D3DMATRIX planetWorld{};
    dev->GetTransform(D3DTS_VIEW, &view);
    dev->GetTransform(D3DTS_WORLD, &planetWorld);
    const float rx = view._11, ry = view._21, rz = view._31;
    const float ux = view._12, uy = view._22, uz = view._32;
    const float parentScale = std::sqrt(planetWorld._11 * planetWorld._11 +
                                        planetWorld._12 * planetWorld._12 +
                                        planetWorld._13 * planetWorld._13);
    const float scale = (parentScale > 1e-8f) ? parentScale : 1.f;
    const float h = mesh.radius * CoronaExtent(mesh.coronaScale) * scale * extentMul;
    const float cx = planetWorld._41, cy = planetWorld._42, cz = planetWorld._43;

    D3DMATRIX bb{};
    bb._11 = rx * h; bb._12 = ry * h; bb._13 = rz * h; bb._14 = 0.f;
    bb._21 = ux * h; bb._22 = uy * h; bb._23 = uz * h; bb._24 = 0.f;
    bb._31 = 0.f;    bb._32 = 0.f;    bb._33 = 1.f;    bb._34 = 0.f;
    bb._41 = cx;     bb._42 = cy;     bb._43 = cz;     bb._44 = 1.f;

    DWORD oldZWrite = TRUE, oldZEnable = TRUE, oldCull = D3DCULL_NONE, oldAlpha = FALSE;
    DWORD oldSrc = D3DBLEND_ONE, oldDst = D3DBLEND_ZERO, oldLight = TRUE;
    DWORD oldATest = FALSE;
    dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    dev->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
    dev->GetRenderState(D3DRS_CULLMODE, &oldCull);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
    dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev->GetRenderState(D3DRS_LIGHTING, &oldLight);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldATest);

    dev->SetTransform(D3DTS_WORLD, &bb);
    // Default Z off: the photosphere is drawn after and covers the inner punch.
    // zTest is for callers that already wrote closer occluders.
    dev->SetRenderState(D3DRS_ZENABLE, zTest ? TRUE : FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    // RGB-only add; bake zeros the square corners. SRCALPHA+A-test ate the halo.
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetFVF(kFVF);
    dev->SetStreamSource(0, vb, 0, sizeof(Vertex));
    dev->SetIndices(ib);
    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    const UINT prims = (UINT)(mesh.coronaIndexCount / 3);
    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (UINT)mesh.coronaVertexCount, 0, prims);

    dev->SetTransform(D3DTS_WORLD, &planetWorld);
    dev->SetTexture(0, nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, oldZEnable);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    dev->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
    dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev->SetRenderState(D3DRS_LIGHTING, oldLight);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldATest);
}

void D3dDrawAtmoShell(IDirect3DDevice9* dev, const MeshGpu& mesh)
{
    if (!dev || !mesh.atmoVb || !mesh.ib || !mesh.atmoPos || mesh.atmoVertexCount < 3 ||
        mesh.indexCount < 3 || mesh.atmoRatio <= 1.f)
        return;
    if (!BodyPaintWantAtmo()) return;

    auto* vb = static_cast<IDirect3DVertexBuffer9*>(mesh.atmoVb);
    auto* ib = static_cast<IDirect3DIndexBuffer9*>(mesh.ib);
    auto* tex = static_cast<IDirect3DTexture9*>(mesh.atmoTex);

    D3DMATRIX world{}, view{}, wv{}, aw{};
    dev->GetTransform(D3DTS_WORLD, &world);
    dev->GetTransform(D3DTS_VIEW, &view);
    aw = world;
    const float s = mesh.atmoRatio;
    aw._11 *= s; aw._12 *= s; aw._13 *= s;
    aw._21 *= s; aw._22 *= s; aw._23 *= s;
    aw._31 *= s; aw._32 *= s; aw._33 *= s;
    Mul44(&wv, aw, view);

    // Sun in WORLD space (Sol at origin). Do not mix this with view-space N.
    float lx = 0.35f, ly = -0.55f, lz = 0.75f;
    const float cx = world._41, cy = world._42, cz = world._43;
    const float clen = std::sqrt(cx * cx + cy * cy + cz * cz);
    if (clen > 0.2f) {
        lx = -cx / clen;
        ly = -cy / clen;
        lz = -cz / clen;
    } else {
        const float llen = std::sqrt(lx * lx + ly * ly + lz * lz);
        lx /= llen;
        ly /= llen;
        lz /= llen;
    }
    float lvx = lx * view._11 + ly * view._21 + lz * view._31;
    float lvy = lx * view._12 + ly * view._22 + lz * view._32;
    float lvz = lx * view._13 + ly * view._23 + lz * view._33;
    const float llenV = std::sqrt(lvx * lvx + lvy * lvy + lvz * lvz);
    if (llenV > 1e-8f) {
        lvx /= llenV;
        lvy /= llenV;
        lvz /= llenV;
    }

    void* bits = nullptr;
    if (FAILED(vb->Lock(0, 0, &bits, 0)) || !bits) return;
    auto* dst = static_cast<Vertex*>(bits);
    const int n = mesh.atmoVertexCount;
    for (int i = 0; i < n; ++i) {
        const float px = mesh.atmoPos[i * 3 + 0];
        const float py = mesh.atmoPos[i * 3 + 1];
        const float pz = mesh.atmoPos[i * 3 + 2];

        // World-space geometric N (unscaled planet world) · world L.
        float nwx = px * world._11 + py * world._21 + pz * world._31;
        float nwy = px * world._12 + py * world._22 + pz * world._32;
        float nwz = px * world._13 + py * world._23 + pz * world._33;
        const float nwlen = std::sqrt(nwx * nwx + nwy * nwy + nwz * nwz);
        float nDotL = 0.f;
        if (nwlen > 1e-8f) {
            const float inv = 1.f / nwlen;
            nDotL = (nwx * inv) * lx + (nwy * inv) * ly + (nwz * inv) * lz;
        }

        const float vx = px * wv._11 + py * wv._21 + pz * wv._31 + wv._41;
        const float vy = px * wv._12 + py * wv._22 + pz * wv._32 + wv._42;
        const float vz = px * wv._13 + py * wv._23 + pz * wv._33 + wv._43;
        const float nx = px * wv._11 + py * wv._21 + pz * wv._31;
        const float ny = px * wv._12 + py * wv._22 + pz * wv._32;
        const float nz = px * wv._13 + py * wv._23 + pz * wv._33;
        const float vlen = std::sqrt(vx * vx + vy * vy + vz * vz);
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        float nDotV = 0.f, vDotL = 0.f;
        if (vlen > 1e-8f && nlen > 1e-8f) {
            const float invN = 1.f / nlen;
            const float invP = 1.f / vlen;
            const float nnx = nx * invN, nny = ny * invN, nnz = nz * invN;
            const float rx = vx * invP, ry = vy * invP, rz = vz * invP;
            nDotV = nnx * (-rx) + nny * (-ry) + nnz * (-rz);
            vDotL = rx * lvx + ry * lvy + rz * lvz;
        }

        float uLut = nDotL * 0.5f + 0.5f;
        if (uLut < 0.f) uLut = 0.f;
        if (uLut > 1.f) uLut = 1.f;

        // LOCKED atmo shell (Sol dispatcher): do not retune without a new visual
        // brief. Gaussian around ground contact; V=1 light / near surface, V=0
        // dark / space; world-space N·L terminator; additive SRCALPHA+ONE.
        float vLut = 1.f;
        float alpha = 0.f;
        if (nDotV > 0.f) {
            float mu = nDotV;
            if (mu > 1.f) mu = 1.f;
            const float limb = 1.f - mu;
            const float kInner = 1.f / s;
            float muSurf = 1.f - kInner * kInner;
            muSurf = (muSurf > 0.f) ? std::sqrt(muSurf) : 0.f;
            const float limbSurf = 1.f - muSurf;
            const float width = 0.16f + 0.45f * (s - 1.f);
            const float x = (limb - limbSurf) / (width > 0.08f ? width : 0.08f);
            const float bell = std::exp(-x * x);
            const float day = Smoothstep(-0.35f, 0.3f, nDotL);
            // Light (V=1) on the inside of the contact, dark (V=0) toward space.
            float v = 1.f - Smoothstep(limbSurf - width, limbSurf + width * 1.2f, limb);
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            vLut = v;
            float mie = vDotL > 0.f ? vDotL : 0.f;
            mie = std::pow(mie, 8.f) * bell * day;
            alpha = bell * day * (0.55f + 0.25f * mie);
            if (alpha > 0.85f) alpha = 0.85f;
        }

        dst[i].x = px;
        dst[i].y = py;
        dst[i].z = pz;
        dst[i].nx = px;
        dst[i].ny = py;
        dst[i].nz = pz;
        dst[i].u = uLut;
        dst[i].v = vLut;
        if (tex)
            dst[i].color = PackRgba(1.f, 1.f, 1.f, alpha);
        else
            dst[i].color = PackRgba(0.30f, 0.62f, 1.0f, alpha);
    }
    vb->Unlock();

    DWORD oldZWrite = TRUE, oldZEnable = TRUE, oldCull = D3DCULL_NONE, oldAlpha = FALSE;
    DWORD oldSrc = D3DBLEND_ONE, oldDst = D3DBLEND_ZERO, oldLight = TRUE;
    DWORD oldColorOp = D3DTOP_MODULATE, oldAlphaOp = D3DTOP_SELECTARG1;
    DWORD oldColorArg1 = D3DTA_TEXTURE, oldColorArg2 = D3DTA_CURRENT;
    DWORD oldAlphaArg1 = D3DTA_TEXTURE, oldAlphaArg2 = D3DTA_DIFFUSE;
    DWORD oldColorVertex = TRUE;
    dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    dev->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
    dev->GetRenderState(D3DRS_CULLMODE, &oldCull);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
    dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev->GetRenderState(D3DRS_LIGHTING, &oldLight);
    dev->GetRenderState(D3DRS_COLORVERTEX, &oldColorVertex);
    dev->GetTextureStageState(0, D3DTSS_COLOROP, &oldColorOp);
    dev->GetTextureStageState(0, D3DTSS_COLORARG1, &oldColorArg1);
    dev->GetTextureStageState(0, D3DTSS_COLORARG2, &oldColorArg2);
    dev->GetTextureStageState(0, D3DTSS_ALPHAOP, &oldAlphaOp);
    dev->GetTextureStageState(0, D3DTSS_ALPHAARG1, &oldAlphaArg1);
    dev->GetTextureStageState(0, D3DTSS_ALPHAARG2, &oldAlphaArg2);

    dev->SetTransform(D3DTS_WORLD, &aw);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    // Additive scatter: never darken the disc (INVSRCALPHA is the sunglasses bug).
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetFVF(kFVF);
    dev->SetStreamSource(0, vb, 0, sizeof(Vertex));
    dev->SetIndices(ib);
    if (tex) {
        dev->SetTexture(0, tex);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    } else {
        dev->SetTexture(0, nullptr);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    }
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

    const UINT prims = (UINT)(mesh.indexCount / 3);
    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, (UINT)mesh.atmoVertexCount, 0, prims);

    dev->SetTransform(D3DTS_WORLD, &world);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, oldColorOp);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, oldColorArg1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, oldColorArg2);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, oldAlphaOp);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, oldAlphaArg1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, oldAlphaArg2);
    dev->SetRenderState(D3DRS_ZENABLE, oldZEnable);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    dev->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
    dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev->SetRenderState(D3DRS_LIGHTING, oldLight);
    dev->SetRenderState(D3DRS_COLORVERTEX, oldColorVertex);
}

} // namespace d3d
} // namespace jjffe
