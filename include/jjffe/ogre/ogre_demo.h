#ifndef JJFFE_OGRE_DEMO_H
#define JJFFE_OGRE_DEMO_H

namespace jjffe {
namespace ogre {

enum class ShipRenderMode {
    Software = 0, // RenderShipFlat → soft blit (default / reference)
    Hardware = 1, // world-space Vulkan hull + hybrid overlays
};

bool OgreDemoInit(int width, int height);
void OgreDemoShutdown();

ShipRenderMode OgreGetShipRenderMode();
void OgreSetShipRenderMode(ShipRenderMode mode);
void OgreToggleShipRenderMode();

// Updates camera from input and draws the current demo ship via Ogre.
void OgreDrawDemo();

} // namespace ogre
} // namespace jjffe

#endif
