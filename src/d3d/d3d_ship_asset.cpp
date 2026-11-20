#include "jjffe/d3d/d3d_mesh.h"
#include "jjffe/d3d/d3d_math.h"
#include "jjffe/export_approval.h"
#include "jjffe/ship_export.h"
#include "jjffe/ship_model_asset.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_set>
#include <vector>

namespace jjffe {
namespace d3d {
namespace {

constexpr uint32_t kAuditHull = 0xFFB0B8C8u;
constexpr uint32_t kAuditPunch = 0xFFE06060u;
constexpr uint32_t kAuditOverlay = 0xFF60E0E0u;
constexpr uint32_t kAuditLine = 0xFF202020u;

constexpr uint32_t kProdHull = 0xFFB0B8C8u;
constexpr uint32_t kProdPunch = 0xFF181818u;
constexpr uint32_t kProdOverlay = 0xFFB0B8C8u;
constexpr uint32_t kProdLine = 0xFF202020u;

bool IsExportPlayableShipId(int modelId)
{
    return modelId >= 15 && modelId <= 71;
}

D3DMATRIX SpriteLocalToGpuLh(const ShipExportSpriteChild& sp)
{
    const float s = kShipExportObjScale;
    const float* r = sp.rot;
    D3DMATRIX m{};
    m._11 = r[0];
    m._12 = r[1];
    m._13 = -r[2];
    m._14 = 0;
    m._21 = r[3];
    m._22 = r[4];
    m._23 = -r[5];
    m._24 = 0;
    m._31 = -r[6];
    m._32 = -r[7];
    m._33 = r[8];
    m._34 = 0;
    m._41 = sp.pos[0] * s;
    m._42 = sp.pos[1] * s;
    m._43 = -sp.pos[2] * s;
    m._44 = 1.f;
    return m;
}

size_t CountAssetGpuTris(const MeshGpu& hull, const MeshGpu& punch,
                         const MeshGpu& overlay, const MeshGpu& line)
{
    size_t n = 0;
    if (hull.vb) n += (size_t)hull.indexCount / 3;
    if (punch.vb) n += (size_t)punch.indexCount / 3;
    if (overlay.vb) n += (size_t)overlay.indexCount / 3;
    if (line.vb) n += (size_t)line.indexCount / 3;
    return n;
}

static void CopyGroupTri(const ShipAssetGroup& src, size_t t, ShipAssetGroup* dst)
{
    const uint32_t ia = src.indices[t * 3 + 0];
    const uint32_t ib = src.indices[t * 3 + 1];
    const uint32_t ic = src.indices[t * 3 + 2];
    const uint32_t base = (uint32_t)dst->verts.size();
    dst->verts.push_back(src.verts[ia]);
    dst->verts.push_back(src.verts[ib]);
    dst->verts.push_back(src.verts[ic]);
    dst->indices.push_back(base + 0);
    dst->indices.push_back(base + 1);
    dst->indices.push_back(base + 2);
    const size_t nSrc = src.indices.size() / 3;
    if (src.triMaterials.size() == nSrc)
        dst->triMaterials.push_back(src.triMaterials[t]);
    if (src.primitiveIds.size() == nSrc)
        dst->primitiveIds.push_back(src.primitiveIds[t]);
    if (src.sourceOrders.size() == nSrc)
        dst->sourceOrders.push_back(src.sourceOrders[t]);
    if (src.triKdArgb.size() == nSrc)
        dst->triKdArgb.push_back(src.triKdArgb[t]);
}

static int GroupTriMaterial(const ShipAssetGroup& g, size_t t)
{
    if (g.triMaterials.size() == g.indices.size() / 3)
        return g.triMaterials[t];
    return g.materialHint;
}

static const ShipExportMaterial* MaterialOrNull(const ShipModelAsset& asset,
                                                int mi)
{
    if (mi < 0 || mi >= (int)asset.materials.size()) return nullptr;
    return &asset.materials[(size_t)mi];
}

[[maybe_unused]] static bool HullTriTextured(const ShipModelAsset& asset, const ShipAssetGroup& g,
                            size_t t)
{
    const ShipExportMaterial* m = MaterialOrNull(asset, GroupTriMaterial(g, t));
    return m && m->textured && !m->baked.argb.empty();
}

static bool HullTriPunchPlane(const ShipModelAsset& asset, const ShipAssetGroup& g,
                              size_t t)
{
    const ShipExportMaterial* m = MaterialOrNull(asset, GroupTriMaterial(g, t));
    return m && m->name == "hull_punch_plane";
}

float BumpRadius(float r, const MeshGpu& m)
{
    if (m.vb && m.radius > r) return m.radius;
    return r;
}

// Match tools/jjffe_fix_preview.py: size * (kShipExportObjScale / 65536).
constexpr float kEmitterSizeToHalf = kShipExportObjScale / 65536.0f;
// Authored thruster palette is very dark green; Blender preview uses ×8.
constexpr float kEmitterColorBoost = 8.0f;

uint32_t EmitterArgb(const float rgb[3], float alpha)
{
    auto ch = [](float c) -> uint32_t {
        const float v = std::min(1.f, std::max(0.f, c * kEmitterColorBoost));
        return (uint32_t)std::min(255, (int)(v * 255.f + 0.5f));
    };
    float r = rgb[0], g = rgb[1], b = rgb[2];
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    if (lum < 0.05f) {
        const float lift = 0.05f - lum;
        r = std::min(1.f, r + lift);
        g = std::min(1.f, g + lift * 1.2f);
        b = std::min(1.f, b + lift);
    }
    const uint32_t a = (uint32_t)std::min(255, (int)(alpha * 255.f + 0.5f));
    return (a << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

float EmitterHalfExtent(const ShipExportEmitter& e)
{
    // Blender: plane size = radius*2 with radius = size * FIXED_TO_WORLD.
    float half = e.size * kEmitterSizeToHalf;
    if (half < 1e-4f) {
        // Nav / glow often omit size — keep a visible bead.
        half = (e.kind == ShipExportEmitterKind::Thruster) ? 0.8f : 0.25f;
    }
    if (e.kind == ShipExportEmitterKind::NavLight) half *= 0.35f;
    if (e.kind == ShipExportEmitterKind::Glow) half *= 0.6f;
    return half;
}

void PackEmitters(const std::vector<ShipExportEmitter>& src,
                  std::vector<D3dEmitterGpu>* out)
{
    out->clear();
    out->reserve(src.size());
    const float scale = kShipExportObjScale;
    for (const ShipExportEmitter& e : src) {
        D3dEmitterGpu g;
        // Export RH/+Z → D3D LH (negate Z), same as D3dUploadAssetGroup.
        g.x = e.x * scale;
        g.y = e.y * scale;
        g.z = -e.z * scale;
        g.halfExtent = EmitterHalfExtent(e);
        const float alpha =
            (e.kind == ShipExportEmitterKind::Thruster) ? 0.85f : 0.7f;
        g.color = EmitterArgb(e.rgb, alpha);
        g.kind = (uint8_t)e.kind;
        out->push_back(g);
    }
}

void DrawEmitters(IDirect3DDevice9* dev, const std::vector<D3dEmitterGpu>& emitters,
                  const D3DMATRIX& parentWorld)
{
    if (!dev || emitters.empty()) return;

    // Thruster billboards off by default (green dots); purple 0x20ee punches
    // are the authored engine marks. JJFFE_DRAW_THRUSTERS=1 to show billboards.
    const char* drawThrust = std::getenv("JJFFE_DRAW_THRUSTERS");
    const bool allowThrust =
        drawThrust && drawThrust[0] == '1';

    D3DMATRIX view{};
    dev->GetTransform(D3DTS_VIEW, &view);
    // Camera axes in world (MatLookAtLH columns / row-vector D3D convention).
    const float rx = view._11, ry = view._21, rz = view._31;
    const float ux = view._12, uy = view._22, uz = view._32;

    // Emitter halfExtent is authored in the same OBJ-scale space as e.x/y/z.
    // parentWorld carries the demo orbit scale (1.6/radius) — corners must use
    // the same scale or billboards dwarf the hull (Krait green wall).
    const float parentScale = std::sqrt(parentWorld._11 * parentWorld._11 +
                                        parentWorld._12 * parentWorld._12 +
                                        parentWorld._13 * parentWorld._13);
    const float scale = (parentScale > 1e-8f) ? parentScale : 1.f;

    struct BillboardVert {
        float x, y, z;
        DWORD color;
    };
    constexpr DWORD kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE;

    // Save state we touch.
    DWORD oldZWrite = TRUE, oldZFunc = D3DCMP_LESSEQUAL, oldCull = D3DCULL_NONE;
    DWORD oldAlpha = FALSE, oldSrc = D3DBLEND_ONE, oldDst = D3DBLEND_ZERO;
    DWORD oldLight = TRUE, oldFog = FALSE;
    dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    dev->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
    dev->GetRenderState(D3DRS_CULLMODE, &oldCull);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
    dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev->GetRenderState(D3DRS_LIGHTING, &oldLight);
    dev->GetRenderState(D3DRS_FOGENABLE, &oldFog);

    D3DMATRIX oldWorld{};
    dev->GetTransform(D3DTS_WORLD, &oldWorld);
    D3DMATRIX ident{};
    MatIdentity(&ident);
    dev->SetTransform(D3DTS_WORLD, &ident);

    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    // Premultiplied-style soft add: keep exhaust readable without solid slabs.
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    // Hull pass leaves COLOROP=MODULATE(TEXTURE,DIFFUSE). With a null texture
    // that samples black → invisible emitters despite E8 in the HUD.
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetFVF(kFVF);

    std::vector<BillboardVert> verts;
    verts.reserve(emitters.size() * 6);

    auto xform = [&](float x, float y, float z, float* ox, float* oy, float* oz) {
        // Row-vector: out = v * parentWorld.
        *ox = x * parentWorld._11 + y * parentWorld._21 + z * parentWorld._31 +
              parentWorld._41;
        *oy = x * parentWorld._12 + y * parentWorld._22 + z * parentWorld._32 +
              parentWorld._42;
        *oz = x * parentWorld._13 + y * parentWorld._23 + z * parentWorld._33 +
              parentWorld._43;
    };

    int skippedThrust = 0;
    for (const D3dEmitterGpu& e : emitters) {
        if (e.kind == (uint8_t)ShipExportEmitterKind::Thruster && !allowThrust) {
            skippedThrust++;
            continue;
        }
        float cx, cy, cz;
        xform(e.x, e.y, e.z, &cx, &cy, &cz);
        const float h = e.halfExtent * scale;
        const float dx0 = (-rx - ux) * h, dy0 = (-ry - uy) * h, dz0 = (-rz - uz) * h;
        const float dx1 = (rx - ux) * h, dy1 = (ry - uy) * h, dz1 = (rz - uz) * h;
        const float dx2 = (rx + ux) * h, dy2 = (ry + uy) * h, dz2 = (rz + uz) * h;
        const float dx3 = (-rx + ux) * h, dy3 = (-ry + uy) * h, dz3 = (-rz + uz) * h;
        const BillboardVert corners[4] = {
            {cx + dx0, cy + dy0, cz + dz0, e.color},
            {cx + dx1, cy + dy1, cz + dz1, e.color},
            {cx + dx2, cy + dy2, cz + dz2, e.color},
            {cx + dx3, cy + dy3, cz + dz3, e.color},
        };
        // Two tris, CCW when viewed along -camera-forward.
        verts.push_back(corners[0]);
        verts.push_back(corners[1]);
        verts.push_back(corners[2]);
        verts.push_back(corners[0]);
        verts.push_back(corners[2]);
        verts.push_back(corners[3]);
    }

    static bool s_loggedSuppress = false;
    if (!s_loggedSuppress && skippedThrust > 0) {
        s_loggedSuppress = true;
        std::fprintf(stderr,
                     "D3D emitters: suppressed %d thruster billboards "
                     "(default off; JJFFE_DRAW_THRUSTERS=1 to show)\n",
                     skippedThrust);
    }

    if (!verts.empty()) {
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(verts.size() / 3),
                             verts.data(), sizeof(BillboardVert));
    }

    dev->SetTransform(D3DTS_WORLD, &oldWorld);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    dev->SetRenderState(D3DRS_ZFUNC, oldZFunc);
    dev->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
    dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev->SetRenderState(D3DRS_LIGHTING, oldLight);
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1);
}

bool UploadAssetPasses(IDirect3DDevice9* dev, const ShipModelAsset& asset,
                       float bias, bool auditTints, MeshGpu* hull,
                       MeshGpu* hullFlat, std::vector<MeshGpu>* hullMore,
                       MeshGpu* punch, MeshGpu* overlay, MeshGpu* line)
{
    const ShipAssetPassState hullSt =
        ShipAssetPassFor(ShipAssetPass::Hull, bias);
    const ShipAssetPassState overlaySt =
        ShipAssetPassFor(ShipAssetPass::Overlay, bias);
    const ShipAssetPassState lineSt =
        ShipAssetPassFor(ShipAssetPass::Line, bias);

    const uint32_t cHull = auditTints ? kAuditHull : kProdHull;
    const uint32_t cPunch = auditTints ? kAuditPunch : kProdPunch;
    const uint32_t cOverlay = auditTints ? kAuditOverlay : kProdOverlay;
    const uint32_t cLine = auditTints ? kAuditLine : kProdLine;

    auto matFor = [&](const ShipAssetGroup& g) -> const ShipExportMaterial* {
        if (g.materialHint < 0 ||
            g.materialHint >= (int)asset.materials.size())
            return nullptr;
        return &asset.materials[(size_t)g.materialHint];
    };

    // Prefer export Kd / baked map_Kd (glTF parity). Audit tints still win
    // when --export-ship wants red/cyan semantic colors.
    const ShipExportMaterial* punchMat =
        auditTints ? nullptr : matFor(asset.punch);
    const ShipExportMaterial* overlayMat =
        auditTints ? nullptr : matFor(asset.overlay);
    const ShipExportMaterial* lineMat = auditTints ? nullptr : matFor(asset.line);

    // Viewer-only: Krait/Viper 0x20ee engine punches often author with the
    // geometric normal pointing into the hull (+Z on the aft plate). With
    // backface cull that hides the purple nozzle marks from outside — they
    // only show through the open rear seam. Flip inward faces so bias + cull
    // keep them visible on the exterior. Punch depth-write (ShipAssetPassFor)
    // then stops later overlay decals from burying them at the authored pose.
    ShipAssetGroup punchOut = asset.punch;
    int flipped = 0;
    for (size_t t = 0; t + 2 < punchOut.indices.size(); t += 3) {
        const uint32_t i0 = punchOut.indices[t];
        const uint32_t i1 = punchOut.indices[t + 1];
        const uint32_t i2 = punchOut.indices[t + 2];
        if (i0 >= punchOut.verts.size() || i1 >= punchOut.verts.size() ||
            i2 >= punchOut.verts.size())
            continue;
        ShipExportVert& A = punchOut.verts[i0];
        ShipExportVert& B = punchOut.verts[i1];
        ShipExportVert& C = punchOut.verts[i2];
        float nx = (B.y - A.y) * (C.z - A.z) - (B.z - A.z) * (C.y - A.y);
        float ny = (B.z - A.z) * (C.x - A.x) - (B.x - A.x) * (C.z - A.z);
        float nz = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        const float cx = (A.x + B.x + C.x) * (1.f / 3.f);
        const float cy = (A.y + B.y + C.y) * (1.f / 3.f);
        const float cz = (A.z + B.z + C.z) * (1.f / 3.f);
        // Inward if geometric n points toward the origin.
        if (nx * cx + ny * cy + nz * cz >= 0.f) continue;
        std::swap(punchOut.indices[t + 1], punchOut.indices[t + 2]);
        A.nx = -A.nx;
        A.ny = -A.ny;
        A.nz = -A.nz;
        B.nx = -B.nx;
        B.ny = -B.ny;
        B.nz = -B.nz;
        C.nx = -C.nx;
        C.ny = -C.ny;
        C.nz = -C.nz;
        flipped++;
    }
    if (flipped > 0) {
        std::fprintf(stderr,
                     "D3D ship-asset: flipped %d inward punch face(s) outward "
                     "(purple 0x20ee nozzles)\n",
                     flipped);
    }
    // Viewer-only aft nudge from ShipModelAsset.punchAftNudgeObj (analytic
    // fleet solver, or non-zero render_policy override). Export verts untouched.
    if (!punchOut.verts.empty() && asset.punchAftNudgeObj != 0.f) {
        const float dz = -asset.punchAftNudgeObj / kShipExportObjScale;
        for (ShipExportVert& v : punchOut.verts) v.z += dz;
        std::fprintf(stderr,
                     "D3D ship-asset: punch aft nudge %.4f OBJ from asset "
                     "(computed=%.4f gap=%.4f policy=%d dz=%.4f world)\n",
                     asset.punchAftNudgeObj, asset.punchClearanceComputedObj,
                     asset.punchMaxGapObj,
                             asset.punchClearancePolicyOverride ? 1 : 0, dz);
    }
    // Nose windshields onto overlapping hull + tiny slack; aft 0x20ee keeps 0.28.
    ShipAssetParkPunchWindshields(&punchOut, asset.hull,
                                  kShipExportOverlayClearanceMinObj);
    ShipAssetOffsetPunchVerts(&punchOut, 0.f, kShipExportOverlayNormalBias);

    // ASM binds DATA_007811 per face. One MeshGpu holds one texture, so
    // split hull by material (and keep untextured punch-planes separate).
    std::map<int, ShipAssetGroup> hullByMat;
    ShipAssetGroup hullUnt;
    hullUnt.pass = ShipAssetPass::Hull;
    const size_t nHullTri = asset.hull.indices.size() / 3;
    if (nHullTri > 0 && asset.hull.triMaterials.size() == nHullTri) {
        for (size_t t = 0; t < nHullTri; t++) {
            if (HullTriPunchPlane(asset, asset.hull, t)) {
                CopyGroupTri(asset.hull, t, &hullUnt);
                continue;
            }
            const int mi = GroupTriMaterial(asset.hull, t);
            ShipAssetGroup& g = hullByMat[mi];
            g.pass = ShipAssetPass::Hull;
            g.materialHint = mi;
            CopyGroupTri(asset.hull, t, &g);
        }
        if (!hullUnt.triMaterials.empty())
            hullUnt.materialHint = hullUnt.triMaterials[0];
        hullUnt.triKdArgb.clear();
    } else if (nHullTri > 0) {
        hullByMat[asset.hull.materialHint] = asset.hull;
    }
    const ShipExportMaterial* hullFlatMat =
        auditTints ? nullptr : MaterialOrNull(asset, hullUnt.materialHint);
    const uint32_t cHullFlat =
        (hullFlatMat && hullFlatMat->kdArgb) ? hullFlatMat->kdArgb : 0xFF555E6Au;

    bool hullOk = true;
    bool firstHull = true;
    for (auto& kv : hullByMat) {
        const ShipExportMaterial* m =
            auditTints ? nullptr : MaterialOrNull(asset, kv.first);
        MeshGpu* dst = nullptr;
        if (firstHull) {
            dst = hull;
            firstHull = false;
        } else if (hullMore) {
            hullMore->emplace_back();
            dst = &hullMore->back();
        }
        if (!dst) continue;
        if (!D3dUploadAssetGroup(dev, &kv.second, hullSt.normalBias, cHull, dst,
                                 m))
            hullOk = false;
    }
    const bool hullFlatOk =
        !hullFlat || hullUnt.indices.empty() ||
        D3dUploadAssetGroup(dev, &hullUnt, hullSt.normalBias, cHullFlat,
                            hullFlat, hullFlatMat);
    if (hullMore && !hullMore->empty()) {
        std::fprintf(stderr,
                     "D3D ship-asset: hull %zu texture batches (ASM per-face maps)\n",
                     hullMore->size() + (hull && hull->vb ? 1u : 0u));
    }
    const bool punchOk =
        punchOut.indices.empty() ||
        D3dUploadAssetGroup(dev, &punchOut, 0.f, cPunch, punch,
                            punchMat);
    const bool overlayOk =
        asset.overlay.indices.empty() ||
        D3dUploadAssetGroup(dev, &asset.overlay, overlaySt.normalBias, cOverlay,
                            overlay, overlayMat);
    const bool lineOk =
        asset.line.indices.empty() ||
        D3dUploadAssetGroup(dev, &asset.line, lineSt.normalBias, cLine, line,
                            lineMat);
    return hullOk && hullFlatOk && punchOk && overlayOk && lineOk;
}

bool LoadAccessoryChild(IDirect3DDevice9* dev, const char* root,
                        const ShipExportSpriteChild& sp, float bias,
                        bool auditTints, D3dShipAssetGpu* parent)
{
    if (!dev || !parent || sp.modelId < 0 || IsExportPlayableShipId(sp.modelId))
        return true;

    ShipModelAsset child;
    ShipExportMesh mesh;
    if (!(BuildExportMesh(sp.modelId, &mesh) && mesh.valid &&
          BuildShipModelAsset(mesh, &child) && child.valid)) {
        char dir[768];
        const char* name = ShipExportName(sp.modelId);
        std::snprintf(dir, sizeof(dir), "%s/%03d_%s", root ? root : "export",
                      sp.modelId, name ? name : "ship");
        if (!LoadShipModelAssetDir(dir, &child) || !child.valid) {
            std::fprintf(stderr, "D3D ship-asset: skip child #%d: %s\n",
                         sp.modelId, child.error.c_str());
            return true;
        }
    }

    D3dShipAssetChildGpu gpu;
    gpu.modelId = sp.modelId;
    gpu.local = SpriteLocalToGpuLh(sp);
    if (!UploadAssetPasses(dev, child, bias, auditTints, &gpu.hull,
                           &gpu.hullFlat, &gpu.hullMore, &gpu.punch,
                           &gpu.overlay, &gpu.line)) {
        D3dDestroyMesh(&gpu.hull);
        D3dDestroyMesh(&gpu.hullFlat);
        for (auto& m : gpu.hullMore) D3dDestroyMesh(&m);
        D3dDestroyMesh(&gpu.punch);
        D3dDestroyMesh(&gpu.overlay);
        D3dDestroyMesh(&gpu.line);
        return false;
    }
    size_t extra = 0;
    for (const auto& m : gpu.hullMore)
        if (m.vb) extra += (size_t)m.indexCount / 3;
    if (CountAssetGpuTris(gpu.hull, gpu.punch, gpu.overlay, gpu.line) == 0 &&
        !(gpu.hullFlat.vb && gpu.hullFlat.indexCount >= 3) && extra == 0) {
        D3dDestroyMesh(&gpu.hull);
        D3dDestroyMesh(&gpu.hullFlat);
        for (auto& m : gpu.hullMore) D3dDestroyMesh(&m);
        D3dDestroyMesh(&gpu.punch);
        D3dDestroyMesh(&gpu.overlay);
        D3dDestroyMesh(&gpu.line);
        return true;
    }
    parent->children.push_back(std::move(gpu));
    return true;
}

void DrawPassRanges(IDirect3DDevice9* dev, const MeshGpu& mesh,
                    const std::vector<D3dPassDrawRange>& ranges,
                    ShipAssetPass pass, float bias)
{
    if (!mesh.vb) return;
    if (ranges.empty()) {
        D3dApplyShipAssetPassState(dev, ShipAssetPassFor(pass, bias));
        D3dDrawMesh(dev, mesh);
        return;
    }
    for (const D3dPassDrawRange& r : ranges) {
        D3dApplyShipAssetPassState(dev, r.state);
        D3dDrawMeshRange(dev, mesh, r.startIndex, r.indexCount);
    }
}

void DrawPasses(IDirect3DDevice9* dev, float bias, const MeshGpu& hull,
                const std::vector<MeshGpu>& hullMore, const MeshGpu& hullFlat,
                const MeshGpu& punch, const MeshGpu& overlay,
                const MeshGpu& line,
                const std::vector<D3dPassDrawRange>& punchRanges,
                const std::vector<D3dPassDrawRange>& overlayRanges)
{
    auto drawHull = [&](const MeshGpu& m) {
        if (!m.vb) return;
        D3dApplyShipAssetPassState(dev,
                                   ShipAssetPassFor(ShipAssetPass::Hull, bias));
        D3dDrawMesh(dev, m);
    };
    drawHull(hull);
    for (const MeshGpu& m : hullMore) drawHull(m);
    drawHull(hullFlat);
    DrawPassRanges(dev, punch, punchRanges, ShipAssetPass::Punch, bias);
    DrawPassRanges(dev, overlay, overlayRanges, ShipAssetPass::Overlay, bias);
    if (line.vb) {
        D3dApplyShipAssetPassState(dev,
                                   ShipAssetPassFor(ShipAssetPass::Line, bias));
        D3dDrawMesh(dev, line);
    }
}

std::vector<D3dPassDrawRange> ToGpuRanges(
    const std::vector<ShipAssetDrawRange>& src)
{
    std::vector<D3dPassDrawRange> out;
    out.reserve(src.size());
    for (const ShipAssetDrawRange& r : src) {
        D3dPassDrawRange g;
        g.startIndex = r.startIndex;
        g.indexCount = r.indexCount;
        g.state = r.state;
        out.push_back(g);
    }
    return out;
}

void AttachPolicyForAsset(ShipModelAsset* asset, const ShipExportMesh* mesh)
{
    if (!asset || !asset->valid) return;
    std::string hash;
    std::unordered_set<uint32_t> prims;
    if (mesh && mesh->valid) {
        hash = ComputeShipExportAssetHash(*mesh);
        CollectExportPrimitiveIds(*mesh, &prims);
    } else {
        // Fall back to ids already on groups (BuildShipModelAsset path).
        for (const ShipAssetPass pass : kShipAssetDrawOrder) {
            const ShipAssetGroup* g = ShipAssetGroupFor(*asset, pass);
            if (!g) continue;
            for (uint32_t id : g->primitiveIds) prims.insert(id);
        }
        hash = asset->sourceAssetHash;
    }
    if (hash.empty() && mesh && mesh->valid)
        hash = ComputeShipExportAssetHash(*mesh);
    AttachShipRenderPolicy(asset, "approved", hash, prims);
}

}  // namespace

void D3dDestroyShipAsset(D3dShipAssetGpu* asset)
{
    if (!asset) return;
    D3dDestroyMesh(&asset->hull);
    D3dDestroyMesh(&asset->hullFlat);
    for (auto& m : asset->hullMore) D3dDestroyMesh(&m);
    asset->hullMore.clear();
    D3dDestroyMesh(&asset->punch);
    D3dDestroyMesh(&asset->overlay);
    D3dDestroyMesh(&asset->line);
    for (auto& ch : asset->children) {
        D3dDestroyMesh(&ch.hull);
        D3dDestroyMesh(&ch.hullFlat);
        for (auto& m : ch.hullMore) D3dDestroyMesh(&m);
        ch.hullMore.clear();
        D3dDestroyMesh(&ch.punch);
        D3dDestroyMesh(&ch.overlay);
        D3dDestroyMesh(&ch.line);
        ch.punchRanges.clear();
        ch.overlayRanges.clear();
    }
    asset->children.clear();
    asset->emitters.clear();
    asset->punchRanges.clear();
    asset->overlayRanges.clear();
    asset->shipIndex = -1;
    asset->radius = 1.f;
    asset->triCount = 0;
    asset->bias = kShipExportOverlayNormalBias;
    asset->auditTints = false;
    asset->hasRenderPolicy = false;
    asset->policyClassCount = 0;
    asset->label[0] = 0;
}

void D3dApplyShipAssetPassState(IDirect3DDevice9* dev,
                                const ShipAssetPassState& st)
{
    if (!dev) return;
    auto f2dw = [](float f) {
        DWORD d;
        std::memcpy(&d, &f, sizeof(d));
        return d;
    };
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, st.depthWrite ? TRUE : FALSE);
    dev->SetRenderState(
        D3DRS_ZFUNC,
        st.depthFunc == ShipAssetDepthFunc::LEqual ? D3DCMP_LESSEQUAL
                                                   : D3DCMP_LESS);
    const bool painterBias = st.depthFunc == ShipAssetDepthFunc::LEqual;
    if (painterBias) {
        float slope = 0.f;
        float constant = 0.f;
        if (st.customDepthBias) {
            slope = st.depthBiasSlope;
            constant = st.depthBiasConstant;
        } else if (!st.depthWrite) {
            slope = kShipExportPainterSlopeScale;
            constant = kShipExportPainterDepthBias;
        } else {
            // Punch: constant only. Slope-scale is pixels of dz/dx — it changes
            // with zoom and made #15 glass appear/disappear between Z:4 and Z:3.
            slope = kShipExportPunchSlopeScale;
            constant = kShipExportPunchDepthBias;
        }
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(slope));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(constant));
    } else {
        dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, f2dw(0.0f));
        dev->SetRenderState(D3DRS_DEPTHBIAS, f2dw(0.0f));
    }
    if (!st.cullBack) {
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    } else {
        (void)st.frontCCW;
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    }
}

bool D3dLoadShipAsset(IDirect3DDevice9* dev, int shipIdx, const char* exportRoot,
                      bool auditTints, D3dShipAssetGpu* out)
{
    if (!out || !dev || shipIdx < 0) return false;
    D3dDestroyShipAsset(out);

    const char* root = (exportRoot && exportRoot[0]) ? exportRoot : "export";
    ShipModelAsset asset;
    ShipExportMesh mesh;
    bool haveMesh = false;
    char dir[768];
    const char* name = ShipExportName(shipIdx);
    std::snprintf(dir, sizeof(dir), "%s/%03d_%s", root, shipIdx,
                  name ? name : "ship");

    // Prefer bytecode→asset when we need W1 prim ids for W5c policy. Disk OBJ
    // is still used when BuildExportMesh fails.
    if (BuildExportMesh(shipIdx, &mesh) && mesh.valid &&
        BuildShipModelAsset(mesh, &asset)) {
        haveMesh = true;
        std::fprintf(stderr,
                     "D3D ship-asset: built #%d from bytecode "
                     "(hull=%zu punch=%zu overlay=%zu line=%zu kids=%zu "
                     "emitters=%zu bias=%.3f punchBias=%.3f audit=%d)\n",
                     shipIdx, asset.hull.indices.size() / 3,
                     asset.punch.indices.size() / 3,
                     asset.overlay.indices.size() / 3,
                     asset.line.indices.size() / 3, asset.sprites.size(),
                     asset.emitters.size(), asset.overlayNormalBias,
                     asset.punchNormalBias,
                     auditTints ? 1 : 0);
    } else if (!LoadShipModelAssetDir(dir, &asset) || !asset.valid) {
        std::fprintf(stderr,
                     "D3D ship-asset: failed bytecode and dir %s (%s)\n", dir,
                     asset.error.c_str());
        return false;
    } else {
        std::fprintf(stderr,
                     "D3D ship-asset: loaded %s "
                     "(hull=%zu punch=%zu overlay=%zu line=%zu kids=%zu "
                     "emitters=%zu bias=%.3f punchBias=%.3f audit=%d)\n",
                     dir, asset.hull.indices.size() / 3,
                     asset.punch.indices.size() / 3,
                     asset.overlay.indices.size() / 3,
                     asset.line.indices.size() / 3, asset.sprites.size(),
                     asset.emitters.size(), asset.overlayNormalBias,
                     asset.punchNormalBias,
                     auditTints ? 1 : 0);
    }

    AttachPolicyForAsset(&asset, haveMesh ? &mesh : nullptr);
    if (asset.hasRenderPolicy) {
        std::vector<ShipAssetDrawRange> punchR, overlayR;
        ShipAssetPlanPassRanges(&asset, ShipAssetPass::Punch, &punchR);
        ShipAssetPlanPassRanges(&asset, ShipAssetPass::Overlay, &overlayR);
        out->punchRanges = ToGpuRanges(punchR);
        out->overlayRanges = ToGpuRanges(overlayR);
        out->hasRenderPolicy = true;
        out->policyClassCount = asset.policyClassCount;
        std::fprintf(stderr, "D3D ship-asset: policy %s\n",
                     asset.renderPolicyNote.c_str());
    } else if (!asset.renderPolicyNote.empty() &&
               asset.renderPolicyNote.rfind("none", 0) != 0) {
        std::fprintf(stderr, "D3D ship-asset: policy skipped — %s\n",
                     asset.renderPolicyNote.c_str());
    }

    out->bias = asset.overlayNormalBias;
    out->auditTints = auditTints;
    if (!UploadAssetPasses(dev, asset, out->bias, auditTints, &out->hull,
                           &out->hullFlat, &out->hullMore, &out->punch,
                           &out->overlay, &out->line)) {
        D3dDestroyShipAsset(out);
        return false;
    }

    for (const ShipExportSpriteChild& sp : asset.sprites) {
        if (!LoadAccessoryChild(dev, root, sp, out->bias, auditTints, out)) {
            D3dDestroyShipAsset(out);
            return false;
        }
    }

    PackEmitters(asset.emitters, &out->emitters);
    if (!out->emitters.empty()) {
        const D3dEmitterGpu& e0 = out->emitters[0];
        std::fprintf(stderr,
                     "D3D emitters: %zu packed (e0 gpu=%.2f,%.2f,%.2f half=%.2f "
                     "kind=%u LH-Z) build=%s\n",
                     out->emitters.size(), e0.x, e0.y, e0.z, e0.halfExtent,
                     (unsigned)e0.kind, __DATE__ " " __TIME__);
    }

    out->radius = 0.f;
    out->radius = BumpRadius(out->radius, out->hull);
    out->radius = BumpRadius(out->radius, out->hullFlat);
    for (const auto& m : out->hullMore) out->radius = BumpRadius(out->radius, m);
    out->radius = BumpRadius(out->radius, out->punch);
    out->radius = BumpRadius(out->radius, out->overlay);
    out->radius = BumpRadius(out->radius, out->line);
    for (const auto& ch : out->children) {
        const float dx = ch.local._41;
        const float dy = ch.local._42;
        const float dz = ch.local._43;
        float rr = std::sqrt(dx * dx + dy * dy + dz * dz);
        rr += BumpRadius(0.f, ch.hull);
        rr = BumpRadius(rr, ch.hullFlat);
        for (const auto& m : ch.hullMore) rr = BumpRadius(rr, m);
        rr = BumpRadius(rr, ch.punch);
        rr = BumpRadius(rr, ch.overlay);
        rr = BumpRadius(rr, ch.line);
        if (rr > out->radius) out->radius = rr;
    }
    for (const D3dEmitterGpu& e : out->emitters) {
        const float rr =
            std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z) + e.halfExtent;
        if (rr > out->radius) out->radius = rr;
    }
    if (out->radius < 1e-3f) out->radius = 1.f;

    out->triCount =
        CountAssetGpuTris(out->hull, out->punch, out->overlay, out->line);
    if (out->hullFlat.vb) out->triCount += (size_t)out->hullFlat.indexCount / 3;
    for (const auto& m : out->hullMore)
        if (m.vb) out->triCount += (size_t)m.indexCount / 3;
    for (const auto& ch : out->children) {
        out->triCount +=
            CountAssetGpuTris(ch.hull, ch.punch, ch.overlay, ch.line);
        if (ch.hullFlat.vb) out->triCount += (size_t)ch.hullFlat.indexCount / 3;
        for (const auto& m : ch.hullMore)
            if (m.vb) out->triCount += (size_t)m.indexCount / 3;
    }

    int hullHud = (out->hull.vb ? out->hull.indexCount / 3 : 0) +
                  (out->hullFlat.vb ? out->hullFlat.indexCount / 3 : 0);
    for (const auto& m : out->hullMore)
        if (m.vb) hullHud += m.indexCount / 3;
    std::snprintf(out->label, sizeof(out->label),
                  "%s bias=%.2f H%d/P%d/O%d/L%d E%zu +%zu%s",
                  auditTints ? "EXPORT" : "ASSET", out->bias, hullHud,
                  out->punch.vb ? out->punch.indexCount / 3 : 0,
                  out->overlay.vb ? out->overlay.indexCount / 3 : 0,
                  out->line.vb ? out->line.indexCount / 3 : 0,
                  out->emitters.size(), out->children.size(),
                  out->hasRenderPolicy ? " POL" : "");
    out->shipIndex = shipIdx;
    return out->triCount > 0 || !out->emitters.empty();
}

void D3dDrawShipAsset(IDirect3DDevice9* dev, const D3dShipAssetGpu& asset,
                      const D3DMATRIX& parentWorld)
{
    if (!dev || asset.shipIndex < 0) return;

    D3DMATERIAL9 mat{};
    mat.Diffuse = {1, 1, 1, 1};
    mat.Ambient = {0.4f, 0.4f, 0.45f, 1};
    dev->SetMaterial(&mat);

    dev->SetTransform(D3DTS_WORLD, &parentWorld);
    DrawPasses(dev, asset.bias, asset.hull, asset.hullMore, asset.hullFlat,
               asset.punch, asset.overlay, asset.line, asset.punchRanges,
               asset.overlayRanges);

    for (const auto& ch : asset.children) {
        D3DMATRIX world;
        MatMultiply(&world, ch.local, parentWorld);
        dev->SetTransform(D3DTS_WORLD, &world);
        DrawPasses(dev, asset.bias, ch.hull, ch.hullMore, ch.hullFlat, ch.punch,
                   ch.overlay, ch.line, ch.punchRanges, ch.overlayRanges);
    }

    DrawEmitters(dev, asset.emitters, parentWorld);

    DWORD z0;
    float zero = 0.f;
    std::memcpy(&z0, &zero, sizeof(z0));
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, z0);
    dev->SetRenderState(D3DRS_DEPTHBIAS, z0);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
}

}  // namespace d3d
}  // namespace jjffe
