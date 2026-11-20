#ifndef JJFFE_OGRE_TEXTURE_H
#define JJFFE_OGRE_TEXTURE_H

#include <cstdint>
#include <string>

namespace jjffe {
namespace ogre {

// Soft-style ship textures: indexed blob bitmap + shadePal → A8R8G8B8 GPU tex.
// Returns Ogre material name (empty if unavailable).

void OgreTextureInit();
void OgreTextureShutdown();

// applyFaceLight follows soft: gated on (texInfo & 0x1000) unless ungated.
std::string OgreGetShipTextureMaterial(uint16_t texInfo, int faceR, int faceG, int faceB,
                                       bool applyFaceLight, int* outTexSize,
                                       bool* outTransparent);

// Ensure lit / normal-aware clones of ship texture materials exist.
void OgreEnsureLitTextureMaterials();

// Return (creating if needed) a lit variant of an unlit ship texture material.
std::string OgreGetShipLitTextureMaterial(const std::string& unlitMatName);

} // namespace ogre
} // namespace jjffe

#endif
