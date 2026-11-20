#ifndef JJFFE_D3D_MESH_H
#define JJFFE_D3D_MESH_H

#include "jjffe/ship_model_asset.h"

#include <cstdint>
#include <cstddef>
#include <vector>

#include <d3d9.h>

namespace jjffe {

namespace d3d {

struct MeshGpu {
    void* vb = nullptr; // IDirect3DVertexBuffer9*
    void* ib = nullptr; // IDirect3DIndexBuffer9*
    void* tex = nullptr; // IDirect3DTexture9*
    void* coronaTex = nullptr; // IDirect3DTexture9* — S6 star halo
    void* coronaVb = nullptr;  // unit XY quad, camera-facing at draw
    void* coronaIb = nullptr;
    int coronaVertexCount = 0;
    int coronaIndexCount = 0;
    float coronaScale = 0.f;   // cfg units; 12 → 3× core, 8 → 2×
    void* atmoTex = nullptr;   // IDirect3DTexture9* — S8 atmosphere LUT
    void* atmoVb = nullptr;    // unit sphere, scaled at draw by atmoRatio
    float* atmoPos = nullptr;  // xyz * atmoVertexCount (CPU, for N·V)
    int atmoVertexCount = 0;
    float atmoRatio = 0.f;     // Earth 1.04; 0 = no shell
    int vertexCount = 0;
    int indexCount = 0;
    float radius = 1.0f;
};

// Resolve MODELS/<id>/model.x (case variants) under the D3D data root.
bool D3dFindModelPath(int modelId, char* out, int outSize);

// Load DirectX .x (or other Assimp formats) + optional skin.png beside it.
bool D3dLoadMeshFile(IDirect3DDevice9* dev, const char* path, MeshGpu* out);
bool D3dLoadModelId(IDirect3DDevice9* dev, int modelId, MeshGpu* out);

// Full procedural UV sphere. Sphere.X in FFED3D is not a full planet mesh
// (cockpit/ice fragment) — use this for --planet / planetary model ids.
bool D3dCreateSphereMesh(IDirect3DDevice9* dev, int segments, int rings, MeshGpu* out);
// Same, with lat-band colours / WRAP / known bake / EMISSIVE 701 keyed off
// ObjectStruct model id (117–148; D3dIsPlanetModel also keeps 113–116).
bool D3dCreatePlanetMesh(IDirect3DDevice9* dev, int modelId, MeshGpu* out,
                         const char* bodyName = nullptr);
// Camera-facing additive corona (tex 760). Depth-write off, not a 2nd sphere.
void D3dDrawCoronaBillboard(IDirect3DDevice9* dev, const MeshGpu& mesh,
                            float extentMul = 1.f, bool zTest = false);
// Thin atmo shell: gaussian around the ground contact (light inside, dark
// toward space). Soft width hides 96-seg aliasing. Additive SRCALPHA+ONE.
void D3dDrawAtmoShell(IDirect3DDevice9* dev, const MeshGpu& mesh);

// Upload a ShipAssetGroup (triangle list). Positions are ShipModelAsset world
// (fixed>>16); multiplied by kShipExportObjScale so bias/lattice units match
// OBJ. Converts RH/+Z-forward export → D3D LH (negate Z + flip winding), same
// as Assimp ConvertToLeftHanded on .x. normalBias (OBJ units) is applied in
// export space before the LH convert. solidColor is ARGB fallback when the
// group has no per-tri Kd. Optional mat supplies baked texture (glTF parity).
bool D3dUploadAssetGroup(IDirect3DDevice9* dev, const ShipAssetGroup* group,
                         float normalBias, uint32_t solidColor, MeshGpu* out,
                         const ShipExportMaterial* mat = nullptr);

void D3dDestroyMesh(MeshGpu* mesh);

// Draw with current D3D9 transforms / texture stage state.
// ffLighting: ignore baked vertex shade; use D3D lights + material (Sol-relative).
void D3dDrawMesh(IDirect3DDevice9* dev, const MeshGpu& mesh, bool ffLighting = false);

// Same as D3dDrawMesh but only [startIndex, startIndex+indexCount) of the IB.
void D3dDrawMeshRange(IDirect3DDevice9* dev, const MeshGpu& mesh, int startIndex,
                      int indexCount, bool ffLighting = false);

bool D3dIsPlanetModel(int modelId);

// --- Exported ShipModelAsset → GPU painter pack (Phase B / B5) -------------

struct D3dPassDrawRange {
    int startIndex = 0;
    int indexCount = 0;
    ShipAssetPassState state{};
};

struct D3dShipAssetChildGpu {
    int modelId = -1;
    MeshGpu hull{};
    MeshGpu hullFlat{};
    std::vector<MeshGpu> hullMore;  // extra hull textures (ASM per-face maps)
    MeshGpu punch{};
    MeshGpu overlay{};
    MeshGpu line{};
    std::vector<D3dPassDrawRange> punchRanges;
    std::vector<D3dPassDrawRange> overlayRanges;
    D3DMATRIX local{};  // parent-model → GPU (OBJ-scale, LH)
};

// View-aligned additive emitters (thruster / nav / glow) from emitters.json.
// Positions are already in GPU LH OBJ-scale space (same as MeshGpu verts).
struct D3dEmitterGpu {
    float x = 0, y = 0, z = 0;
    float halfExtent = 0.5f;  // half-edge of the billboard quad
    uint32_t color = 0xFFFFFFFFu;  // ARGB
    uint8_t kind = 0;  // ShipExportEmitterKind
};

struct D3dShipAssetGpu {
    int shipIndex = -1;
    float bias = 0.28f;
    float radius = 1.f;
    size_t triCount = 0;
    bool auditTints = false;
    bool hasRenderPolicy = false;
    int policyClassCount = 0;
    MeshGpu hull{};
    MeshGpu hullFlat{};
    std::vector<MeshGpu> hullMore;  // extra hull textures (ASM per-face maps)
    MeshGpu punch{};
    MeshGpu overlay{};
    MeshGpu line{};
    std::vector<D3dPassDrawRange> punchRanges;
    std::vector<D3dPassDrawRange> overlayRanges;
    std::vector<D3dShipAssetChildGpu> children;
    std::vector<D3dEmitterGpu> emitters;
    char label[96]{};
};

void D3dDestroyShipAsset(D3dShipAssetGpu* asset);

// Load export/<idx>_name/ (or bytecode rebuild). auditTints: red punch / cyan
// overlay for --export-ship; production: neutral metal + dark punch/line.
bool D3dLoadShipAsset(IDirect3DDevice9* dev, int shipIdx, const char* exportRoot,
                      bool auditTints, D3dShipAssetGpu* out);

// Painter draw: hull → punch → overlay → line, accessory children, then
// additive view-aligned emitters (depth test on, depth write off).
// parentWorld is the orbit/ship transform; children multiply local * parentWorld.
void D3dDrawShipAsset(IDirect3DDevice9* dev, const D3dShipAssetGpu& asset,
                      const D3DMATRIX& parentWorld);

void D3dApplyShipAssetPassState(IDirect3DDevice9* dev,
                                const ShipAssetPassState& st);

} // namespace d3d
} // namespace jjffe

#endif
