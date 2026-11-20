#include "jjffe/ogre/ogre_texture.h"
#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/video.h"

#include <Ogre.h>
#include <OgreManualObject2.h>
#include <OgreRoot.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreStagingTexture.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace jjffe {
namespace ogre {

namespace {

bool s_ready = false;

struct CacheKey {
    uint16_t bmpIndex = 0;
    uint16_t matIndex = 0;
    int8_t flR = 0, flG = 0, flB = 0;
    uint8_t applyFL = 0;

    bool operator==(const CacheKey& o) const
    {
        return bmpIndex == o.bmpIndex && matIndex == o.matIndex && flR == o.flR &&
               flG == o.flG && flB == o.flB && applyFL == o.applyFL;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const
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

struct CacheEntry {
    std::string materialName;
    std::string textureName;
    int texSize = 0;
    bool transparent = false;
};

std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> s_cache;
int s_texSerial = 0;

std::string UploadBakedTexture(const ShipBakedTexture& baked, std::string* outTexName)
{
    if (baked.argb.empty() || baked.width <= 0 || baked.height <= 0) return {};
    const int w = baked.width;
    const int h = baked.height;

    Ogre::Root* root = OgreGetRoot();
    if (!root) return {};
    auto* texMgr = root->getRenderSystem()->getTextureGpuManager();

    char texName[64];
    char matName[64];
    snprintf(texName, sizeof(texName), "jjffe_ship_tex_%d", s_texSerial);
    snprintf(matName, sizeof(matName), "jjffe_ship_mat_%d", s_texSerial);
    s_texSerial++;
    if (outTexName) *outTexName = texName;

    Ogre::TextureGpu* tex = texMgr->createOrRetrieveTexture(
        texName, Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::ManualTexture | Ogre::TextureFlags::AutomaticBatching,
        Ogre::TextureTypes::Type2D);
    tex->setResolution((Ogre::uint32)w, (Ogre::uint32)h);
    tex->setNumMipmaps(1u);
    tex->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
    tex->scheduleTransitionTo(Ogre::GpuResidency::Resident);

    Ogre::StagingTexture* staging =
        texMgr->getStagingTexture((Ogre::uint32)w, (Ogre::uint32)h, 1u, 1u,
                                  Ogre::PFG_RGBA8_UNORM);
    staging->startMapRegion();
    Ogre::TextureBox box =
        staging->mapRegion((Ogre::uint32)w, (Ogre::uint32)h, 1u, 1u, Ogre::PFG_RGBA8_UNORM);

    for (int y = 0; y < h; y++) {
        auto* row = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(box.data) + (size_t)y * box.bytesPerRow);
        const uint32_t* src = baked.argb.data() + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            row[x] = ShipArgbToRgba8(src[x]);
    }
    staging->stopMapRegion();
    staging->upload(box, tex, 0, 0);
    texMgr->removeStagingTexture(staging);
    tex->notifyDataIsReady();

    OgreEnsureTexturedDatablock(matName, texName, /*depthCheck=*/true, /*depthWrite=*/true,
                                baked.transparent);
    return matName;
}

} // namespace

void OgreTextureInit()
{
    OgreTextureShutdown();
    s_ready = true;
}

void OgreTextureShutdown()
{
    s_cache.clear();
    s_ready = false;
}

std::string OgreGetShipTextureMaterial(uint16_t texInfo, int faceR, int faceG, int faceB,
                                       bool applyFaceLight, int* outTexSize,
                                       bool* outTransparent)
{
    if (outTexSize) *outTexSize = 0;
    if (outTransparent) *outTransparent = false;
    if (!s_ready || !ShipTexInfoIsTextured(texInfo)) return {};

    ShipMaterialInfo mat{};
    if (!ShipResolveMaterial(texInfo, &mat)) return {};

    const uint32_t bmpIndex = mat.texWord & 0xFFF;
    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(bmpIndex));
    if (!bmp || (bmp->width != 64 && bmp->width != 128) || bmp->height != bmp->width)
        return {};

    bool applyFL = applyFaceLight;
    if (applyFL && (texInfo & 0x1000) == 0) applyFL = false;

    CacheKey key{};
    key.bmpIndex = static_cast<uint16_t>(bmpIndex);
    key.matIndex = texInfo & 0xFFF;
    key.flR = applyFL ? static_cast<int8_t>(faceR) : 0;
    key.flG = applyFL ? static_cast<int8_t>(faceG) : 0;
    key.flB = applyFL ? static_cast<int8_t>(faceB) : 0;
    key.applyFL = applyFL ? 1 : 0;

    auto it = s_cache.find(key);
    if (it != s_cache.end()) {
        if (outTexSize) *outTexSize = it->second.texSize;
        if (outTransparent) *outTransparent = it->second.transparent;
        return it->second.materialName;
    }

    ShipBakedTexture baked;
    if (!BakeShipTextureRGBA(texInfo, faceR, faceG, faceB, applyFL, &baked))
        return {};

    std::string texName;
    std::string matName = UploadBakedTexture(baked, &texName);
    if (matName.empty()) return {};

    CacheEntry e{};
    e.materialName = matName;
    e.textureName = texName;
    e.texSize = baked.width;
    e.transparent = baked.transparent;
    s_cache.emplace(key, e);

    if (outTexSize) *outTexSize = e.texSize;
    if (outTransparent) *outTransparent = e.transparent;
    return matName;
}

void OgreEnsureLitTextureMaterials()
{
}

std::string OgreGetShipLitTextureMaterial(const std::string& unlitMatName)
{
    // Next Unlit path uses vertex colour for lighting; no separate lit clone.
    return unlitMatName;
}

} // namespace ogre
} // namespace jjffe
