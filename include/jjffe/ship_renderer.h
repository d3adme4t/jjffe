#ifndef JJFFE_SHIP_RENDERER_H
#define JJFFE_SHIP_RENDERER_H

#include <cstdint>
#include <vector>
#include "jjffe/vertex_transform.h"
#include "jjffe/ship_model.h"

namespace jjffe {

// Render a ship model as textured or flat-shaded polygons
// Draws directly to the back buffer
// polys: transformed polygons from TransformShipVertices()
// polyCount: number of polygons to draw
// shipModel: ship model for normal lookups
void RenderShipFlat(const TransformedPoly* polys, int polyCount, const ShipModel* shipModel, const int32_t* shipRot);

// Soft VGA0 flat-punch policy (Saker tip/throat, Adder windshield, Explorer).
// Prepare once per frame (also nudges tip-punch sortKeys); then filter draws.
void ShipPrepareFlatPunchGates(TransformedPoly* polys, int polyCount,
                               const ShipModel* shipModel);
bool ShipAllowVga0FlatPunch(const TransformedPoly& poly, const ShipModel* shipModel,
                            uint8_t color);
// True after ShipPrepareFlatPunchGates when the mesh is a pure 0x1000 missile hull.
bool ShipIsMissileFlatMesh();

// Get shading color for a face based on normal direction
// Returns palette index (darker = further from light)
uint8_t GetFaceColor(const ShipModel* shipModel, uint8_t normalIdx, uint8_t baseColor, const int32_t* shipRot);

// Enable/disable debug logging for texture rasterization
void SetTextureRasterDebug(bool enabled);

// Enable rainbow normalIdx face colors (off by default — production uses lighting / black windows)
void SetDebugFaceColors(bool enabled);

// Map 4-bit RGB (material shade ± face light) to a VGA palette index.
// Used by textured shade remap (FUNC_001805 / DATA_009252) and flat RGB polys.
uint8_t ShadeRgbNibbleToPal(int r, int g, int b);

// --- Shared material / shade API (soft + D3D texture upload) ---

struct ShipMaterialInfo {
    uint32_t texWord = 0;   // draw tex word from DATA_007811
    int shadeCount = 0;
    uint8_t shades[8][3] = {};
};

// FUNC_001712 material table lookup.
bool ShipResolveMaterial(uint16_t texInfo, ShipMaterialInfo* out);

// FUNC_001805 shade remap table (texel → VGA index).
void ShipBuildTexShadePalette(uint8_t outPal[256], const ShipMaterialInfo& mat,
                              int faceR, int faceG, int faceB, bool applyFaceLight);

// True when texInfo selects a textured material (bits 15:14 ≠ 0), not Contour/flat.
bool ShipTexInfoIsTextured(uint16_t texInfo);

// Soft UV corners for a poly (texel space 0..texSize-1). vertexCount 3 or 4.
// Pair mirror V-flip matches RenderTexturedPoly when specialParam mixedParity.
void ShipAssignTexUVs(const TransformedPoly& poly, uint32_t drawTexWord, int texSize,
                      float u[4], float v[4]);

// Same corners, then (u + 0.5) / texSize — the D3D / Ogre HW convention.
void ShipAssignNormalizedTexUVs(const TransformedPoly& poly, uint32_t drawTexWord,
                                int texSize, float u[4], float v[4]);

// Device-free bake: texel → shadePal → VGA palette → packed 0xAARRGGBB.
// applyFaceLight is further gated on (texInfo & 0x1000), matching Ogre/D3D.
// Requires VideoInit() (palette) and a loaded bitmap table. Square 64/128 only.
struct ShipBakedTexture {
    int width = 0;
    int height = 0;
    bool transparent = false;
    uint16_t bmpIndex = 0;
    uint16_t matIndex = 0;
    std::vector<uint32_t> argb;  // width * height, 0xAARRGGBB
};

bool BakeShipTextureRGBA(uint16_t texInfo, int faceR, int faceG, int faceB,
                         bool applyFaceLight, ShipBakedTexture* out);

// Ogre PFG_RGBA8_UNORM on LE is byte order R,G,B,A = packed 0xAABBGGRR.
inline uint32_t ShipArgbToRgba8(uint32_t argb)
{
    const uint32_t a = argb & 0xFF000000u;
    const uint32_t r = (argb >> 16) & 0xFFu;
    const uint32_t g = (argb >> 8) & 0xFFu;
    const uint32_t b = argb & 0xFFu;
    return a | (b << 16) | (g << 8) | r;
}

} // namespace jjffe

#endif // JJFFE_SHIP_RENDERER_H
