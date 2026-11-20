#ifndef JJFFE_OGRE_SOFT_BLIT_H
#define JJFFE_OGRE_SOFT_BLIT_H

namespace Ogre {
class ManualObject;
}

namespace jjffe {
namespace ogre {

// Upload g_videoState->backBuffer (after RenderShipFlat) and draw as a
// fullscreen NDC textured quad — same pixels as soft/D3D demo.
void OgreBlitSoftBackBuffer(Ogre::ManualObject* mo);
void OgreSoftBlitShutdown();

} // namespace ogre
} // namespace jjffe

#endif
