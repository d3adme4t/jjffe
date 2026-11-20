#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ogre/ogre_soft_blit.h"
#include "jjffe/video.h"

#include <Ogre.h>
#include <OgreManualObject2.h>
#include <OgreHlmsUnlit.h>
#include <OgreRoot.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreStagingTexture.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace jjffe {
namespace ogre {

namespace {

bool s_ready = false;
Ogre::TextureGpu* s_tex = nullptr;
constexpr const char* kTexName = "jjffe_soft_ship_tex";
constexpr const char* kMatName = "JjffeSoftShipBlit";

bool EnsureResources()
{
    if (s_ready && s_tex) return true;
    Ogre::Root* root = OgreGetRoot();
    if (!root) return false;
    auto* rs = root->getRenderSystem();
    if (!rs) return false;
    auto* texMgr = rs->getTextureGpuManager();

    s_tex = texMgr->createOrRetrieveTexture(
        kTexName, Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::ManualTexture | Ogre::TextureFlags::AutomaticBatching,
        Ogre::TextureTypes::Type2D);
    s_tex->setResolution(SCREEN_WIDTH, SCREEN_HEIGHT);
    s_tex->setNumMipmaps(1u);
    s_tex->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
    s_tex->scheduleTransitionTo(Ogre::GpuResidency::Resident);

    OgreEnsureTexturedDatablock(kMatName, kTexName, /*depthCheck=*/false,
                                /*depthWrite=*/false, /*alphaTest=*/true);
    s_ready = true;
    return true;
}

void UploadPixels(const uint8_t* buf, const uint8_t* pal)
{
    if (!s_tex) return;
    auto* texMgr = s_tex->getTextureManager();
    Ogre::StagingTexture* staging = texMgr->getStagingTexture(
        SCREEN_WIDTH, SCREEN_HEIGHT, 1u, 1u, Ogre::PFG_RGBA8_UNORM);
    staging->startMapRegion();
    Ogre::TextureBox box =
        staging->mapRegion(SCREEN_WIDTH, SCREEN_HEIGHT, 1u, 1u, Ogre::PFG_RGBA8_UNORM);

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        auto* row = reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(box.data) + (size_t)y * box.bytesPerRow);
        const uint8_t* src = buf + y * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const uint8_t idx = src[x];
            if (idx == 0xFF) {
                row[x] = 0u; // transparent → starfield
                continue;
            }
            uint8_t r = 0, g = 0, b = 0;
            if (idx != 0 && pal) {
                r = pal[idx * 3 + 0];
                g = pal[idx * 3 + 1];
                b = pal[idx * 3 + 2];
            }
            // RGBA8 little-endian: R,G,B,A
            row[x] = (255u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
        }
    }

    staging->stopMapRegion();
    staging->upload(box, s_tex, 0, 0);
    texMgr->removeStagingTexture(staging);
    s_tex->notifyDataIsReady();
}

} // namespace

void OgreSoftBlitShutdown()
{
    s_ready = false;
    s_tex = nullptr; // TextureGpuManager owns lifetime with Root
}

void OgreBlitSoftBackBuffer(Ogre::ManualObject* mo)
{
    if (!mo || !g_videoState) return;
    if (!EnsureResources()) return;

    UploadPixels(g_videoState->backBuffer, g_videoState->palette);

    mo->clear();
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    mo->begin(kMatName, Ogre::OT_TRIANGLE_LIST);
    auto push = [&](float x, float y, float u, float v) {
        mo->position(x, y, 0.5f);
        mo->normal(0, 0, 1);
        mo->textureCoord(u, v);
        mo->colour(1, 1, 1, 1);
    };
    push(-1, 1, 0, 0);
    push(-1, -1, 0, 1);
    push(1, -1, 1, 1);
    push(-1, 1, 0, 0);
    push(1, -1, 1, 1);
    push(1, 1, 1, 0);
    OgreEndManualTriangles(mo, true);
}

} // namespace ogre
} // namespace jjffe
