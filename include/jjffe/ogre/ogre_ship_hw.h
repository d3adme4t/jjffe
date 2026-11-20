#ifndef JJFFE_OGRE_SHIP_HW_H
#define JJFFE_OGRE_SHIP_HW_H

#include "jjffe/vertex_transform.h"
#include "jjffe/ship_model.h"

#include <cstdint>
#include <vector>

namespace jjffe {
namespace ogre {

struct OgreShipHwStats {
    int hullTris = 0;
    int punchTris = 0;
    int contourTris = 0;
    int glintTris = 0;
    int materials = 0;
    int worldFaces = 0;
};

// Soft Contour fill is Z-less; freckles later in the painter overwrite
// Contour∩body. Hybrid Contours carry the Contour group's sortKey so they can
// be interleaved with a textured freckle restamp.
struct OgreHwContourBatch {
    uint32_t sortKey = 0;
    std::vector<float> xy;   // packed x,y pairs (screen pixels), 3 verts/tri
    std::vector<uint32_t> colors;
};

void OgreShipHwInit();
void OgreShipHwShutdown();
void OgreShipHwClear();

// Switch viewport between ortho (SOFT UI-friendly) and perspective (HW hull).
void OgreShipHwSetActive(bool hardware);

// Update perspective near/far from current ship distance (fixed>>16 world units).
void OgreShipHwSetViewDistance(float shipDistWorld);

// Diagnostic toggles (also env: JJFFE_OGRE_NORMAL_DEBUG / WIREFRAME / NO_HYBRID).
bool OgreShipHwNormalDebug();
void OgreShipHwSetNormalDebug(bool on);
void OgreShipHwToggleNormalDebug();

bool OgreShipHwWireframe();
void OgreShipHwToggleWireframe();

// World-hull texture switch. Disabled mode keeps world geometry/depth but
// renders every face with the flat material colour.
bool OgreShipHwTextures();
void OgreShipHwSetTextures(bool on);
void OgreShipHwToggleTextures();

bool OgreShipHwHybridOverlays();
void OgreShipHwToggleHybridOverlays();

bool OgreShipHwAuthoredCull();
void OgreShipHwToggleAuthoredCull();

// Build and upload world-space hull + local punches. `polys` supplies UVs via
// ShipAssignTexUVs (matched by WorldFace.sourceIndex).
OgreShipHwStats OgreShipHwDrawHull(const WorldFace* faces, int faceCount,
                                   const TransformedPoly* polys, int polyCount,
                                   const ShipModel* model,
                                   const int32_t* shipRotFp16);

// Contours + freckle restamp in soft painter order (far → near by sortKey).
// Contours are identity-projected overlays; freckles are world textured tris
// captured during the preceding OgreShipHwDrawHull call.
void OgreShipHwScheduleContourFreckles(const OgreHwContourBatch* batches,
                                       int batchCount);

} // namespace ogre
} // namespace jjffe

#endif
