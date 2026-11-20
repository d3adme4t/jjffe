#ifndef JJFFE_OGRE_UI_H
#define JJFFE_OGRE_UI_H

#include <cstdint>

namespace jjffe {
namespace ogre {

bool OgreUiInit(int width, int height);
void OgreUiShutdown();

void OgreDrawMenu(uint32_t frameCount, uint32_t currentFps);
void OgreDrawShipSelect();
void OgreDrawBriefingStub();
void OgreDrawIntroCast();

// Immediate-mode 2D helpers (screen space, origin top-left).
void OgreClear(float r, float g, float b);
void OgreFillRect(float x, float y, float w, float h, uint32_t argb);
void OgreDrawText(float x, float y, const char* text, uint32_t argb, float scale = 2.0f);

// Flush accumulated UI quads into the UI ManualObject (call before EndFrame).
void OgreUiFlush();
void OgreUiBegin();

} // namespace ogre
} // namespace jjffe

#endif
