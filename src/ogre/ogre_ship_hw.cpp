#include "jjffe/ogre/ogre_ship_hw.h"
#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ogre/ogre_texture.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/video.h"
#include "jjffe/math.h"

#include <Ogre.h>
#include <OgreManualObject2.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>
#include <OgreRoot.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace jjffe {
namespace ogre {

namespace {

bool s_ready = false;
bool s_normalDebug = false;
bool s_wireframe = false;
bool s_textures = true;
bool s_hybrid = true;
bool s_authoredCull = false;

Ogre::Camera* s_shipCam = nullptr;
Ogre::Camera* s_orthoCam = nullptr;
Ogre::SceneNode* s_shipCamNode = nullptr;
Ogre::ManualObject* s_hwHullMO = nullptr;
Ogre::ManualObject* s_hwPunchMO = nullptr;
Ogre::ManualObject* s_hwFreckleMO = nullptr; // unused single-queue slot (cleared)
std::vector<Ogre::ManualObject*> s_freckleScheduleMOs;

struct FreckleTri {
    WorldFaceVert v[3]{};
    uint32_t col[3]{};
    std::string mat;
    uint32_t sortKey = 0;
};
std::vector<FreckleTri> s_freckleTris;
Ogre::Light* s_dirLight = nullptr;

constexpr const char* kFlatLitMat = "JjffeHwFlatLit";
constexpr const char* kNormalDebugMat = "JjffeHwNormalDebug";
constexpr const char* kPunchMat = "JjffeHwPunch";
constexpr const char* kEngineCapMat = "JjffeHwEngineCap";

bool EnvTruthy(const char* name)
{
    const char* e = getenv(name);
    return e && e[0] && e[0] != '0';
}

void CreateHwMaterials()
{
    // HlmsUnlit datablocks (vertex colour). Depth variants for punches/caps.
    auto ensure = [](const char* name, bool depthCheck, bool depthWrite) {
        OgreEnsureTexturedDatablock(name, "", depthCheck, depthWrite, false);
        // Textured helper rejects empty texture — create plain Unlit via known names.
        (void)name;
        (void)depthCheck;
        (void)depthWrite;
    };
    (void)ensure;
    // Reuse shared Unlit + dedicated no-write / no-depth datablocks.
    Ogre::Root* root = OgreGetRoot();
    if (!root) return;
    auto* hlms = static_cast<Ogre::HlmsUnlit*>(
        root->getHlmsManager()->getHlms(Ogre::HLMS_UNLIT));
    if (!hlms) return;
    auto make = [&](const char* name, bool depthCheck, bool depthWrite) {
        if (hlms->getDatablock(name)) return;
        Ogre::HlmsMacroblock macro;
        macro.mDepthCheck = depthCheck;
        macro.mDepthWrite = depthWrite;
        macro.mDepthFunc = Ogre::CMPF_LESS_EQUAL;
        macro.mCullMode = Ogre::CULL_NONE;
        Ogre::HlmsBlendblock blend;
        auto* db = static_cast<Ogre::HlmsUnlitDatablock*>(
            hlms->createDatablock(name, name, macro, blend, Ogre::HlmsParamVec()));
        db->setUseColour(false);
    };
    make(kFlatLitMat, true, true);
    make(kNormalDebugMat, true, true);
    make(kPunchMat, true, false);
    make(kEngineCapMat, false, false);
}

Ogre::ManualObject* MakeWorldMO(const char* /*name*/, uint8_t queue)
{
    auto* sm = OgreGetSceneManager();
    if (!sm) return nullptr;
    auto* mo = sm->createManualObject(Ogre::SCENE_DYNAMIC);
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    mo->setRenderQueueGroup(queue);
    sm->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode()->attachObject(mo);
    return mo;
}

uint32_t FaceBaseColor(const WorldFace& f, const ShipModel* model,
                       const int32_t* shipRotFp16)
{
    if (s_normalDebug) {
        const float nx = f.verts[0].nx, ny = f.verts[0].ny, nz = f.verts[0].nz;
        auto ch = [](float n) -> uint8_t {
            int v = (int)((n * 0.5f + 0.5f) * 255.0f);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return (uint8_t)v;
        };
        return (255u << 24) | ((uint32_t)ch(nx) << 16) | ((uint32_t)ch(ny) << 8) | ch(nz);
    }
    if (!model || !shipRotFp16) return 0xFFE0E0F0u;
    uint8_t lit = GetFaceColor(model, f.normalIdx, 0x08, shipRotFp16);
    if (lit < 2) lit = 2;
    const uint16_t ti = f.texInfo;
    if (ti == 0 || ti == 0x2000 ||
        (ti == 0x20ee && model && GetShipModelIndex(model) == 29))
        return 0xFF000000u;
    uint8_t palIdx = lit;
    const bool flatRgb = (ti >> 14) == 0;
    const bool plainStroke =
        f.type == MeshPolyType::PlainLine || f.type == MeshPolyType::DirLine;
    if (flatRgb) {
        int r = (ti >> 8) & 0xF;
        int g = (ti >> 4) & 0xF;
        int b = ti & 0xF;
        if ((ti & 0x1000) != 0) {
            r += f.faceLight[0];
            g += f.faceLight[1];
            b += f.faceLight[2];
            if (r < 0) r = 0;
            if (r > 15) r = 15;
            if (g < 0) g = 0;
            if (g > 15) g = 15;
            if (b < 0) b = 0;
            if (b > 15) b = 15;
        }
        palIdx = DPal16toPal(static_cast<uint16_t>((r << 8) | (g << 4) | b));
        // 0x1000 is RGB0 + face-light flag. When the stamped face light is
        // zero (or DPal collapses to VGA0), soft still shades by the authored
        // normal — Gecko/Krait rear caps and missile hulls all need that.
        if (palIdx == 0 && ti == 0x1000)
            palIdx = lit;
    } else if (plainStroke) {
        // Soft PlainLine/DirLine: material shade0 (+ face light), not freckles.
        ShipMaterialInfo mat{};
        if (ShipResolveMaterial(ti, &mat)) {
            uint8_t shadePal[256];
            const bool applyFL =
                (f.faceLight[0] | f.faceLight[1] | f.faceLight[2]) != 0;
            ShipBuildTexShadePalette(shadePal, mat, f.faceLight[0], f.faceLight[1],
                                     f.faceLight[2], applyFL);
            palIdx = shadePal[0];
        }
    }
    uint8_t pr = 180, pg = 180, pb = 200;
    if (g_videoState) {
        const uint8_t* pal = g_videoState->palette;
        pr = pal[palIdx * 3];
        pg = pal[palIdx * 3 + 1];
        pb = pal[palIdx * 3 + 2];
    }
    // Flat RGB / line strokes already consume face light via DPal/shade0;
    // applying the HW key again would double-shade caps and rails.
    if (flatRgb || plainStroke)
        return (255u << 24) | ((uint32_t)pr << 16) |
               ((uint32_t)pg << 8) | pb;
    // CPU key light from top-right (unlit MO path — RTSS lit ManualObjects stay dark).
    // Light direction (incoming): from (+X,+Y) toward the hull.
    const float lx = 0.55f, ly = 0.75f, lz = -0.35f;
    const float llen = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float nx = f.verts[0].nx, ny = f.verts[0].ny, nz = f.verts[0].nz;
    float ndotl = (nx * lx + ny * ly + nz * lz) / llen;
    if (ndotl < 0.0f) ndotl = 0.0f;
    const float shade = 0.75f + 0.90f * ndotl; // bright ambient + top-right key
    auto apply = [&](uint8_t c) -> uint8_t {
        int v = (int)(c * shade * 1.25f + 35.0f);
        // Preserve near-black plates (windshield / nozzle) — don't lift VGA0.
        if (c < 12) return c;
        if (v < 40) v = 40;
        if (v > 255) v = 255;
        return (uint8_t)v;
    };
    return (255u << 24) | ((uint32_t)apply(pr) << 16) |
           ((uint32_t)apply(pg) << 8) | apply(pb);
}

void EmitVert(Ogre::ManualObject* mo, const WorldFaceVert& v, uint32_t col, bool withUv)
{
    const float r = ((col >> 16) & 0xFF) / 255.0f;
    const float g = ((col >> 8) & 0xFF) / 255.0f;
    const float b = (col & 0xFF) / 255.0f;
    // Ogre's camera looks down local -Z. Our camera is aimed toward world +Z,
    // which makes its screen-right axis world -X; soft projection uses +X.
    // Reflect X at upload so world HW and the ASM/soft camera have the same
    // handedness. Reflect the normal as well to preserve lighting.
    mo->position(-v.x, v.y, v.z);
    mo->normal(-v.nx, v.ny, v.nz);
    mo->colour(r, g, b, 1.0f);
    if (withUv) mo->textureCoord(v.u, v.v);
}

void ApplyPolygonMode(const std::string& /*matName*/)
{
    // Wireframe via Hlms macroblock not wired yet for Next 4; soft path covers digs.
}

} // namespace

void OgreShipHwInit()
{
    OgreShipHwShutdown();
    auto* sm = OgreGetSceneManager();
    if (!sm) return;

    s_orthoCam = OgreGetCamera();
    s_shipCam = sm->createCamera("jjffeShipCam");
    s_shipCam->setProjectionType(Ogre::PT_PERSPECTIVE);
    const float halfFovX = std::atan(160.0f / 256.0f);
    const float aspectLogical = 320.0f / 200.0f;
    const float fovY = 2.0f * std::atan(std::tan(halfFovX) / aspectLogical);
    s_shipCam->setFOVy(Ogre::Radian(fovY));
    s_shipCam->setAspectRatio((float)OgreGetWidth() / (float)OgreGetHeight());
    // ASM near = 0x40 fixed; tightened each frame via OgreShipHwSetViewDistance.
    s_shipCam->setNearClipDistance(0.05f);
    s_shipCam->setFarClipDistance(40.0f);
    // Next createCamera may already be parented; detach before reparenting.
    if (s_shipCam->isAttached())
        s_shipCam->detachFromParent();
    s_shipCamNode = sm->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode();
    s_shipCamNode->setPosition(0, 0, 0);
    s_shipCamNode->lookAt(Ogre::Vector3(0, 0, 1), Ogre::Node::TS_WORLD);
    s_shipCamNode->attachObject(s_shipCam);

    // Bright fill + key from the ship's top-right (camera looks +Z; +X right, +Y up).
    sm->setAmbientLight(Ogre::ColourValue(0.55f, 0.55f, 0.6f),
                        Ogre::ColourValue(0.25f, 0.25f, 0.3f),
                        Ogre::Vector3::UNIT_Y);
    s_dirLight = sm->createLight();
    s_dirLight->setType(Ogre::Light::LT_DIRECTIONAL);
    s_dirLight->setDiffuseColour(Ogre::ColourValue(1.35f, 1.3f, 1.2f));
    s_dirLight->setSpecularColour(Ogre::ColourValue(0.15f, 0.15f, 0.12f));
    if (s_dirLight->isAttached())
        s_dirLight->detachFromParent();
    auto* lightNode = sm->getRootSceneNode(Ogre::SCENE_DYNAMIC)->createChildSceneNode();
    lightNode->setDirection(Ogre::Vector3(-0.55f, -0.75f, 0.35f), Ogre::Node::TS_WORLD);
    lightNode->attachObject(s_dirLight);

    CreateHwMaterials();
    OgreEnsureLitTextureMaterials();

    // Queue bands: hull → freckles → punches (numeric RQ ids; Next has no RENDER_QUEUE_*).
    s_hwHullMO = MakeWorldMO("hwHullMO", 10);
    s_hwFreckleMO = MakeWorldMO("hwFreckleMO", 12);
    s_hwPunchMO = MakeWorldMO("hwPunchMO", 13);

    s_normalDebug = EnvTruthy("JJFFE_OGRE_NORMAL_DEBUG");
    s_wireframe = EnvTruthy("JJFFE_OGRE_WIREFRAME");
    const char* textures = getenv("JJFFE_OGRE_HW_TEXTURES");
    s_textures = !(textures && textures[0] == '0');
    s_hybrid = !EnvTruthy("JJFFE_OGRE_NO_HYBRID");
    // Painter-era authored facing is not a reliable GPU backface decision:
    // many canopy/connector faces intentionally use the opposite normal.
    // World HW relies on depth and keeps authored culling diagnostic-only.
    s_authoredCull = EnvTruthy("JJFFE_OGRE_AUTHORED_CULL");
    s_ready = true;
    printf("Ogre HW ship renderer ready (perspective cam, focal=256)\n");
}

void OgreShipHwShutdown()
{
    auto* sm = OgreGetSceneManager();
    if (sm) {
        if (s_hwHullMO) { sm->destroyManualObject(s_hwHullMO); s_hwHullMO = nullptr; }
        if (s_hwFreckleMO) { sm->destroyManualObject(s_hwFreckleMO); s_hwFreckleMO = nullptr; }
        if (s_hwPunchMO) { sm->destroyManualObject(s_hwPunchMO); s_hwPunchMO = nullptr; }
        for (auto* mo : s_freckleScheduleMOs)
            if (mo) sm->destroyManualObject(mo);
        s_freckleScheduleMOs.clear();
        s_freckleTris.clear();
        if (s_dirLight) { sm->destroyLight(s_dirLight); s_dirLight = nullptr; }
        if (s_shipCam) { sm->destroyCamera(s_shipCam); s_shipCam = nullptr; }
        if (s_shipCamNode) { sm->destroySceneNode(s_shipCamNode); s_shipCamNode = nullptr; }
    }
    s_orthoCam = nullptr;
    s_ready = false;
}

void OgreShipHwClear()
{
    if (s_hwHullMO) s_hwHullMO->clear();
    if (s_hwFreckleMO) s_hwFreckleMO->clear();
    if (s_hwPunchMO) s_hwPunchMO->clear();
    for (auto* mo : s_freckleScheduleMOs)
        if (mo) mo->clear();
    s_freckleTris.clear();
}

void OgreShipHwSetViewDistance(float shipDistWorld)
{
    if (!s_shipCam) return;
    if (shipDistWorld < 0.05f) shipDistWorld = 0.05f;
    s_shipCam->setCustomProjectionMatrix(false);
    s_shipCam->setFrustumOffset(0.0f, 0.0f);
    // Tight clip range around the ship — a 0.001..256 range destroys Z precision
    // so opaque blacks and freckles cannot depth-sort.
    const float nearZ = std::max(0.02f, shipDistWorld * 0.08f);
    const float farZ = std::max(nearZ + 1.0f, shipDistWorld * 12.0f);
    s_shipCam->setNearClipDistance(nearZ);
    s_shipCam->setFarClipDistance(farZ);
    // ASM projects around logical (160,79), not the 320x200 centre (160,100):
    // y_ndc = 0.21 + 2.56*y/z. Ogre's camera looks toward world +Z, so view-Z
    // is negative and the projection's Z→Y term must be -0.21.
    Ogre::Matrix4 projection = s_shipCam->getProjectionMatrix();
    projection[1][2] = -0.21f;
    s_shipCam->setCustomProjectionMatrix(true, projection);
}

void OgreShipHwSetActive(bool hardware)
{
    if (hardware && s_shipCam) {
        OgreReconnectWorkspace(s_shipCam);
        OgreSetWindowTitleTag("hw");
    } else {
        Ogre::Camera* cam = s_orthoCam ? s_orthoCam : OgreGetCamera();
        if (cam) OgreReconnectWorkspace(cam);
        OgreSetWindowTitleTag("soft");
    }
    OgreShipHwClear();
    if (auto* h = OgreHullManual()) h->clear();
    if (auto* c = OgreContourManual()) c->clear();
    if (auto* g = OgreGlintManual()) g->clear();
    if (auto* p = OgrePunchManual()) p->clear();
    OgreClearTexManuals();
    OgreClearPainterPolyManuals(0);
}

bool OgreShipHwNormalDebug() { return s_normalDebug; }
void OgreShipHwSetNormalDebug(bool on) { s_normalDebug = on; }
void OgreShipHwToggleNormalDebug() { s_normalDebug = !s_normalDebug; }
bool OgreShipHwWireframe() { return s_wireframe; }
void OgreShipHwToggleWireframe() { s_wireframe = !s_wireframe; }
bool OgreShipHwTextures() { return s_textures; }
void OgreShipHwSetTextures(bool on) { s_textures = on; }
void OgreShipHwToggleTextures() { s_textures = !s_textures; }
bool OgreShipHwHybridOverlays() { return s_hybrid; }
void OgreShipHwToggleHybridOverlays() { s_hybrid = !s_hybrid; }
bool OgreShipHwAuthoredCull() { return s_authoredCull; }
void OgreShipHwToggleAuthoredCull() { s_authoredCull = !s_authoredCull; }

OgreShipHwStats OgreShipHwDrawHull(const WorldFace* faces, int faceCount,
                                   const TransformedPoly* polys, int polyCount,
                                   const ShipModel* model,
                                   const int32_t* shipRotFp16)
{
    OgreShipHwStats stats{};
    if (!s_ready || !faces || !s_hwHullMO || !s_hwPunchMO || !s_hwFreckleMO)
        return stats;

    struct Tri {
        WorldFaceVert v[3]{};
        uint32_t col[3]{};
        uint32_t sortKey = 0;
        std::string mat;
        bool textured = false;
    };
    std::vector<Tri> hullTris;
    std::vector<Tri> decalTris;

    // The runtime T toggle supersedes the old one-shot flat-only diagnostic.
    const bool useTextures =
        s_textures && !EnvTruthy("JJFFE_OGRE_HW_FLAT_ONLY") && !s_normalDebug;

    const bool missileFlat = ShipIsMissileFlatMesh();

    for (int i = 0; i < faceCount; i++) {
        WorldFace f = faces[i];
        stats.worldFaces++;
        // PlainLine rails (Shuttle) are 2-vert world segments — expand below.
        if (!f.worldValid || f.vertexCount < 2) continue;
        if (f.kind == WorldFaceKind::ScreenHybrid) continue;
        // Soft skips backfacing polys (FUNC_001481). Missiles keep both sides
        // via the flat-mesh bypass. Without this, Sidewinder's backfacing
        // 0x4027 plates draw as a hollow interior at rear-quarter views.
        if (f.backFacing && !missileFlat)
            continue;
        if (s_authoredCull && f.backFacing && f.kind != WorldFaceKind::Punch)
            continue;

        // Sidewinder engine plate (mesh 12 / 0x403e) is authored coplanar with
        // the rear 0x4029 wall (same normal). Soft resolves by painter sortKey;
        // Vulkan still z-fights on equal depth. Bias along the view ray
        // (preserves x/z, y/z) so the nearer key clearly wins without flicker.
        if (model && GetShipModelIndex(model) == 26 &&
            f.meshIndex == 12 && f.texInfo == 0x403e) {
            uint32_t rearKey = f.sortKey;
            for (int j = 0; j < faceCount; j++) {
                if (faces[j].texInfo == 0x4029 && faces[j].worldValid &&
                    !faces[j].backFacing) {
                    rearKey = faces[j].sortKey;
                    break;
                }
            }
            // Smaller sortKey = nearer = should sit in front.
            const float scale = (f.sortKey <= rearKey) ? 0.985f : 1.015f;
            for (int v = 0; v < f.vertexCount; v++) {
                f.verts[v].x *= scale;
                f.verts[v].y *= scale;
                f.verts[v].z *= scale;
            }
        }

        // Prefer soft UV assignment when we have the source TransformedPoly.
        bool haveUv = false;
        if (polys && f.sourceIndex >= 0 && f.sourceIndex < polyCount &&
            ShipTexInfoIsTextured(f.texInfo)) {
            ShipMaterialInfo minfo{};
            if (ShipResolveMaterial(f.texInfo, &minfo)) {
                int texSize = 64;
                bool transparent = false;
                std::string probe = OgreGetShipTextureMaterial(
                    f.texInfo, f.faceLight[0], f.faceLight[1], f.faceLight[2], true,
                    &texSize, &transparent);
                if (!probe.empty() && texSize >= 2) {
                    float u[4], v[4];
                    ShipAssignNormalizedTexUVs(polys[f.sourceIndex], minfo.texWord,
                                               texSize, u, v);
                    for (int vi = 0; vi < f.vertexCount; vi++) {
                        f.verts[vi].u = u[vi];
                        f.verts[vi].v = v[vi];
                    }
                    haveUv = true;
                    (void)transparent;
                }
            }
        }

        // PlainLine (Shuttle rails): expand the 2-vert segment into a camera-
        // facing ribbon so the GPU path matches soft's 1px hull strokes.
        if (f.vertexCount == 2) {
            WorldFaceVert a = f.verts[0];
            WorldFaceVert b = f.verts[1];
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            const float midX = 0.5f * (a.x + b.x);
            const float midY = 0.5f * (a.y + b.y);
            const float midZ = 0.5f * (a.z + b.z);
            // View direction ≈ toward camera at origin.
            float vx = -midX, vy = -midY, vz = -midZ;
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
            // ~1 soft pixel at this depth (sy scale = 256/z).
            const float half = std::max(0.0015f, std::fabs(midZ) / 512.0f);
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

        const uint32_t baseCol = FaceBaseColor(f, model, shipRotFp16);
        // Will recompute after we know textured — see below.
        uint32_t col = baseCol;
        if ((f.texInfo == 0x1000 || f.texInfo == 0x20ee) &&
            EnvTruthy("JJFFE_OGRE_HW_DEBUG")) {
            static int s_flatLightLogs = 0;
            if (s_flatLightLogs++ < 24) {
                fprintf(stderr,
                        "HW_FLAT tex=%04x mesh=%d kind=%d light=(%d,%d,%d) "
                        "color=%08x vc=%d\n",
                        (unsigned)f.texInfo, f.meshIndex, (int)f.kind,
                        (int)f.faceLight[0], (int)f.faceLight[1],
                        (int)f.faceLight[2], (unsigned)col, f.vertexCount);
            }
        }
        auto pushTris = [&](std::vector<Tri>& dst, const std::string& mat,
                            bool texturedFlag) {
            // Soft ship rasterization clips at logical row 158. With
            // sy = 79 - 256*y/z, its bottom edge is y + (79/256)z = 0.
            // Clip in camera/world space so the GPU keeps perspective-correct
            // UV interpolation at the generated edge.
            WorldFaceVert clipped[8]{};
            int clippedCount = 0;
            auto distance = [](const WorldFaceVert& v) {
                return v.y + (79.0f / 256.0f) * v.z;
            };
            auto interpolate = [](const WorldFaceVert& a, const WorldFaceVert& b,
                                  float t) {
                WorldFaceVert r{};
                r.x = a.x + (b.x - a.x) * t;
                r.y = a.y + (b.y - a.y) * t;
                r.z = a.z + (b.z - a.z) * t;
                r.nx = a.nx + (b.nx - a.nx) * t;
                r.ny = a.ny + (b.ny - a.ny) * t;
                r.nz = a.nz + (b.nz - a.nz) * t;
                r.u = a.u + (b.u - a.u) * t;
                r.v = a.v + (b.v - a.v) * t;
                r.color = a.color;
                return r;
            };
            for (int cur = 0; cur < f.vertexCount; cur++) {
                const int prev = (cur + f.vertexCount - 1) % f.vertexCount;
                const WorldFaceVert& a = f.verts[prev];
                const WorldFaceVert& b = f.verts[cur];
                const float da = distance(a);
                const float db = distance(b);
                const bool aIn = da >= 0.0f;
                const bool bIn = db >= 0.0f;
                if (aIn != bIn) {
                    const float t = da / (da - db);
                    clipped[clippedCount++] = interpolate(a, b, t);
                }
                if (bIn)
                    clipped[clippedCount++] = b;
            }

            for (int v = 1; v + 1 < clippedCount; v++) {
                Tri t{};
                t.v[0] = clipped[0];
                t.v[1] = clipped[v];
                t.v[2] = clipped[v + 1];
                t.col[0] = t.col[1] = t.col[2] = col;
                t.sortKey = f.sortKey;
                // Soft keeps Gecko/Krait 0x20ee engine marks on top of the
                // coplanar 0x1000 rear plate. Pure sortKey puts mesh12 (5572)
                // before mesh9 (5555), so the grey plate wipes the pale rect —
                // force marks nearer so they draw last among decals.
                if (f.texInfo == 0x20ee && t.sortKey > 0 && model &&
                    (GetShipModelIndex(model) == 27 ||
                     GetShipModelIndex(model) == 28))
                    t.sortKey = 1;
                t.mat = mat;
                t.textured = texturedFlag;
                dst.push_back(t);
            }
        };

        std::string matName = s_normalDebug ? kNormalDebugMat : kFlatLitMat;
        bool textured = false;
        if (useTextures && haveUv && ShipTexInfoIsTextured(f.texInfo) &&
            f.kind != WorldFaceKind::Punch) {
            int texSize = 0;
            bool transparent = false;
            std::string texMat = OgreGetShipTextureMaterial(
                f.texInfo, f.faceLight[0], f.faceLight[1], f.faceLight[2], true,
                &texSize, &transparent);
            if (!texMat.empty()) {
                matName = texMat; // unlit textured — reliable RTSS path
                textured = true;
                // Modulate freckle textures with the same top-right shade (on white).
                const float lx = 0.55f, ly = 0.75f, lz = -0.35f;
                const float llen = std::sqrt(lx * lx + ly * ly + lz * lz);
                float ndotl = (f.verts[0].nx * lx + f.verts[0].ny * ly +
                               f.verts[0].nz * lz) / llen;
                if (ndotl < 0.0f) ndotl = 0.0f;
                const float shade = 0.80f + 0.85f * ndotl;
                auto ch = [&](int c) -> uint8_t {
                    int v = (int)(c * shade);
                    if (v < 140) v = 140;
                    if (v > 255) v = 255;
                    return (uint8_t)v;
                };
                col = (255u << 24) | ((uint32_t)ch(255) << 16) |
                      ((uint32_t)ch(255) << 8) | ch(245);
            }
            (void)transparent;
        }

        if (f.kind == WorldFaceKind::Punch) {
            // Use the exact soft gate: it suppresses broad wing-back VGA0 flats
            // while retaining the Saker tip/throat and other authentic punches.
            const uint8_t colorNonZero =
                ((col >> 16) & 0xFF) || ((col >> 8) & 0xFF) || (col & 0xFF);
            if (!polys || f.sourceIndex < 0 || f.sourceIndex >= polyCount)
                continue;
            const TransformedPoly& source = polys[f.sourceIndex];
            // Adder/Osprey-family child #187 is not emitted by the ASM painter;
            // Saker is the one ship whose lit nozzle bridge is authentic.
            if (f.texInfo == 0x3000 && IsSpriteChildParam(source.specialParam) &&
                SpriteChildModelId(source.specialParam) == 187 &&
                (!model || GetShipModelIndex(model) != 31))
                continue;
            if (!ShipAllowVga0FlatPunch(source, model, colorNonZero))
                continue;
            matName = kFlatLitMat;
            textured = false;
        }

        // Gecko/Krait rear engine caps are face-lit 0x1000 flats scheduled as
        // surface decals by the original painter. Missile 0x1000 hulls are the
        // entire solid body — keep them depth-tested with the hull batch.
        const bool geckoFamily =
            model && (GetShipModelIndex(model) == 27 ||
                      GetShipModelIndex(model) == 28);
        const bool geckoEngineCap =
            geckoFamily &&
            (f.texInfo == 0x1000 || f.texInfo == 0x20ee) &&
            f.vertexCount >= 3;

        const bool sakerPunch =
            f.kind == WorldFaceKind::Punch && model &&
            GetShipModelIndex(model) == 31;
        const bool surfaceDecal =
            (f.kind == WorldFaceKind::Punch && !sakerPunch) ||
            (f.texInfo == 0x1000 && !missileFlat);
        std::vector<Tri>& dst = surfaceDecal ? decalTris : hullTris;
        const std::string emitMat =
            geckoEngineCap ? kEngineCapMat
                           : (surfaceDecal ? kPunchMat : matName);
        const bool emitTextured = surfaceDecal ? false : textured;
        const int before = (int)dst.size();
        pushTris(dst, emitMat, emitTextured);
        const int added = (int)dst.size() - before;
        if (f.kind == WorldFaceKind::Punch)
            stats.punchTris += added;
        else
            stats.hullTris += added;
    }

    // Soft painter: larger sortKey = farther = drawn first. Emit far→near with
    // LEQUAL so coplanar faces (Sidewinder 0x4029 rear vs 0x403e engine) resolve
    // the same way as ASM instead of a fixed depth bias.
    auto byPainter = [](const Tri& a, const Tri& b) {
        if (a.sortKey != b.sortKey) return a.sortKey > b.sortKey;
        return a.mat < b.mat;
    };
    std::stable_sort(hullTris.begin(), hullTris.end(), byPainter);
    std::stable_sort(decalTris.begin(), decalTris.end(), byPainter);

    s_hwHullMO->clear();
    s_hwHullMO->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    s_hwFreckleMO->clear();
    s_hwFreckleMO->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    s_hwPunchMO->clear();
    s_hwPunchMO->setLocalAabb(Ogre::Aabb::BOX_INFINITE);

    auto uploadTris = [&](Ogre::ManualObject* target, const std::vector<Tri>& tris,
                          bool texturedOnly) {
        if (tris.empty()) return;
        size_t i = 0;
        while (i < tris.size()) {
            const std::string& mat = tris[i].mat;
            const bool textured = tris[i].textured;
            size_t j = i + 1;
            while (j < tris.size() && tris[j].mat == mat && tris[j].textured == textured)
                j++;
            if (texturedOnly && !textured) {
                i = j;
                continue;
            }
            ApplyPolygonMode(mat);
            target->begin(mat.c_str(), Ogre::OT_TRIANGLE_LIST);
            for (size_t t = i; t < j; t++) {
                EmitVert(target, tris[t].v[0], tris[t].col[0], textured);
                EmitVert(target, tris[t].v[1], tris[t].col[1], textured);
                EmitVert(target, tris[t].v[2], tris[t].col[2], textured);
            }
            OgreEndManualTriangles(target, false);
            stats.materials++;
            i = j;
        }
    };

    uploadTris(s_hwHullMO, hullTris, /*texturedOnly=*/false);
    // Capture textured freckles for Contour∩body restamp (soft painter order).
    s_freckleTris.clear();
    s_freckleTris.reserve(hullTris.size());
    for (const Tri& t : hullTris) {
        if (!t.textured) continue;
        FreckleTri ft{};
        ft.v[0] = t.v[0];
        ft.v[1] = t.v[1];
        ft.v[2] = t.v[2];
        ft.col[0] = t.col[0];
        ft.col[1] = t.col[1];
        ft.col[2] = t.col[2];
        ft.mat = t.mat;
        ft.sortKey = t.sortKey;
        s_freckleTris.push_back(ft);
    }
    // Approved painter-era decals intentionally overlay the finished hull.
    uploadTris(s_hwPunchMO, decalTris, /*texturedOnly=*/false);

    if (getenv("JJFFE_OGRE_HW_DEBUG") && getenv("JJFFE_OGRE_HW_DEBUG")[0]) {
        static int s_once = 0;
        if (s_once++ < 3) {
            fprintf(stderr,
                    "HW_DEBUG faces=%d hullTris=%d punchTris=%d mats=%d "
                    "hullMO=%zu punchMO=%zu\n",
                    stats.worldFaces, stats.hullTris, stats.punchTris, stats.materials,
                      static_cast<size_t>(s_hwHullMO ? s_hwHullMO->getNumSections() : 0),
                      static_cast<size_t>(s_hwPunchMO ? s_hwPunchMO->getNumSections() : 0));
            for (int i = 0; i < faceCount && i < 3; i++) {
                if (!faces[i].worldValid) continue;
                fprintf(stderr,
                        "  face%d kind=%d vc=%d p0=(%.4f,%.4f,%.4f) n=(%.2f,%.2f,%.2f)\n",
                        i, (int)faces[i].kind, faces[i].vertexCount,
                        faces[i].verts[0].x, faces[i].verts[0].y, faces[i].verts[0].z,
                        faces[i].verts[0].nx, faces[i].verts[0].ny, faces[i].verts[0].nz);
            }
        }
    }
    (void)s_hybrid;
    return stats;
}

void OgreShipHwScheduleContourFreckles(const OgreHwContourBatch* batches,
                                       int batchCount)
{
    if (!s_ready) return;
    for (auto* mo : s_freckleScheduleMOs)
        if (mo) mo->clear();
    OgreClearPainterPolyManuals(0);
    if (auto* c = OgreContourManual()) c->clear();

    struct Item {
        uint32_t sortKey = 0;
        bool contour = false;
        int index = 0; // Contour batch index or freckle tri index
    };
    std::vector<Item> items;
    items.reserve((size_t)std::max(0, batchCount) + s_freckleTris.size());
    for (int i = 0; i < batchCount; i++) {
        if (!batches || batches[i].xy.size() < 6) continue;
        items.push_back({batches[i].sortKey, true, i});
    }
    // One schedule item per freckle material run would be nicer; per-tri is
    // fine for Osprey-scale Contour ships (tens of freckle tris).
    for (int i = 0; i < (int)s_freckleTris.size(); i++)
        items.push_back({s_freckleTris[(size_t)i].sortKey, false, i});

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.sortKey != b.sortKey) return a.sortKey > b.sortKey; // far first
        // Freckles before Contours at equal key — Contours win Contour∩wing.
        // Nearer freckles (smaller sortKey) still restamp Contour∩body later.
        if (a.contour != b.contour) return !a.contour && b.contour;
        return a.index < b.index;
    });

    auto* sm = OgreGetSceneManager();
    int w = OgreGetWidth();
    int h = OgreGetHeight();
    if (w < 1) w = 640;
    if (h < 1) h = 400;

    int freckleMo = 0;
    int painterMo = 0;
    for (size_t ii = 0; ii < items.size(); ii++) {
        const Item& it = items[ii];
        const uint16_t prio = (uint16_t)std::min<size_t>(ii, 65535);
        if (it.contour) {
            const OgreHwContourBatch& b = batches[it.index];
            auto* mo = OgrePainterPolyManual(painterMo++, prio);
            if (!mo) continue;
            // identity applied after end() via OgreApplyIdentityProjection
            mo->setRenderQueueGroup(static_cast<uint8_t>(11));
            mo->clear();
            mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
            mo->begin(OgreUnlitUiMaterial(), Ogre::OT_TRIANGLE_LIST);
            const size_t nVerts = b.xy.size() / 2;
            // NDC Y flip vs soft Y-down — reverse winding (same as UploadScreenTris).
            for (size_t ti = 0; ti + 2 < nVerts; ti += 3) {
                auto emit = [&](size_t vi) {
                    const float x = b.xy[vi * 2];
                    const float y = b.xy[vi * 2 + 1];
                    const uint32_t col =
                        (vi < b.colors.size()) ? b.colors[vi] : 0xFF414141u;
                    const float r = ((col >> 16) & 0xFF) / 255.0f;
                    const float g = ((col >> 8) & 0xFF) / 255.0f;
                    const float bl = (col & 0xFF) / 255.0f;
                    const float nx = (2.0f * x / (float)w) - 1.0f;
                    const float ny = 1.0f - (2.0f * y / (float)h);
                    mo->position(nx, ny, 0.5f);
                    mo->normal(0, 0, 1);
                    mo->colour(r, g, bl, 1.0f);
                };
                emit(ti);
                emit(ti + 2);
                emit(ti + 1);
            }
            OgreEndManualTriangles(mo, true);
        } else {
            if (!sm) continue;
            while (freckleMo >= (int)s_freckleScheduleMOs.size()) {
                char name[64];
                snprintf(name, sizeof(name), "hwFreckleSched_%zu",
                         s_freckleScheduleMOs.size());
                s_freckleScheduleMOs.push_back(
                    MakeWorldMO(name, 11));
            }
            auto* mo = s_freckleScheduleMOs[(size_t)freckleMo++];
            mo->setRenderQueueGroup(static_cast<uint8_t>(11));
            mo->clear();
            mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
            const FreckleTri& t = s_freckleTris[(size_t)it.index];
            ApplyPolygonMode(t.mat);
            mo->begin(t.mat.c_str(), Ogre::OT_TRIANGLE_LIST);
            EmitVert(mo, t.v[0], t.col[0], true);
            EmitVert(mo, t.v[1], t.col[1], true);
            EmitVert(mo, t.v[2], t.col[2], true);
            OgreEndManualTriangles(mo, false);
        }
    }
}

} // namespace ogre
} // namespace jjffe
