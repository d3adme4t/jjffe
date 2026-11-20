#ifndef JJFFE_OGRE_BACKEND_H
#define JJFFE_OGRE_BACKEND_H

#include <cstdint>

struct SDL_Window;

namespace Ogre {
class Root;
class Window;
class SceneManager;
class Camera;
class ManualObject;
class CompositorWorkspace;
}

namespace jjffe {
namespace ogre {

bool OgreInit();
void OgreShutdown();

// Called from PlatformVideoBlit — draws current phase and presents one frame.
void OgrePresentFrame();

SDL_Window* OgreGetWindow();
Ogre::Root* OgreGetRoot();
Ogre::SceneManager* OgreGetSceneManager();
Ogre::Window* OgreGetRenderWindow();
Ogre::Camera* OgreGetCamera();
// Next has no Viewport; returns null. Kept for call-site compatibility.
void* OgreGetViewport();
void OgreSetWindowTitleTag(const char* tag); // e.g. "soft" / "hw"
int OgreGetWidth();
int OgreGetHeight();

// Swap the compositor workspace camera (HW perspective vs soft ortho).
void OgreReconnectWorkspace(Ogre::Camera* cam);

// After ManualObject::end(), mark all sections identity NDC (UI / soft overlays).
void OgreApplyIdentityProjection(Ogre::ManualObject* mo, bool identity);

// Next ManualObject2 requires indices — emit 0..N-1 then end().
void OgreEndManualTriangles(Ogre::ManualObject* mo, bool identityProjection);

// Frame helpers shared by UI / demo.
void OgreBeginFrame(float r, float g, float b);
void OgreEndFrame();

// True once a JJFFE_OGRE_SHOT capture run has written its frame and the
// process should exit (used by tools/ogre_view.py parity captures).
bool OgreScriptedCaptureDone();

// Screen-space ManualObjects (origin top-left in caller; Y flipped for Ogre).
Ogre::ManualObject* OgreUiManual();
Ogre::ManualObject* OgreHullManual();
Ogre::ManualObject* OgreContourManual();
Ogre::ManualObject* OgreGlintManual();
Ogre::ManualObject* OgrePunchManual();
Ogre::ManualObject* OgreStarsManual();
// One ManualObject per textured datablock name (created on demand).
Ogre::ManualObject* OgreTexManual(const char* materialName);

void OgreClearTexManuals();

// Painter-order poly pool: one MO per poly.
Ogre::ManualObject* OgrePainterPolyManual(int index, uint16_t priority);
void OgreClearPainterPolyManuals(int keepCount);

// HlmsUnlit datablock names created at init (vertex colour; no hardware cull).
const char* OgreUnlitMaterial();     // depth on — ships / stars
const char* OgreUnlitUiMaterial();   // depth off — HUD / menus

// Create or return an HlmsUnlit datablock bound to a TextureGpu name.
bool OgreEnsureTexturedDatablock(const char* datablockName, const char* textureName,
                                 bool depthCheck, bool depthWrite, bool alphaTest);

} // namespace ogre
} // namespace jjffe

#endif
