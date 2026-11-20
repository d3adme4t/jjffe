#include "jjffe/ogre/ogre_ui.h"
#include "jjffe/ogre/ogre_backend.h"
#include "jjffe/ogre/ogre_soft_blit.h"
#include "jjffe/intro_cast.h"
#include "jjffe/menu.h"
#include "jjffe/demo3d.h"

#include <Ogre.h>
#include <OgreManualObject2.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace jjffe {
namespace ogre {

extern const uint8_t* FontGlyph(char c);

namespace {

int s_width = 640;
int s_height = 400;

struct UiVert {
    float x, y, z;
    uint32_t color;
};

std::vector<UiVert> s_uiVerts;

void PushQuad(float x0, float y0, float x1, float y1, uint32_t color)
{
    UiVert v[6] = {
        {x0, y0, 0.0f, color},
        {x1, y0, 0.0f, color},
        {x1, y1, 0.0f, color},
        {x0, y0, 0.0f, color},
        {x1, y1, 0.0f, color},
        {x0, y1, 0.0f, color},
    };
    s_uiVerts.insert(s_uiVerts.end(), v, v + 6);
}

void EmitToManual(Ogre::ManualObject* mo, const std::vector<UiVert>& verts)
{
    if (!mo) return;
    mo->clear();
    mo->setLocalAabb(Ogre::Aabb::BOX_INFINITE);
    if (verts.empty()) return;
    mo->begin(OgreUnlitUiMaterial(), Ogre::OT_TRIANGLE_LIST);
    auto emit = [&](const UiVert& v) {
        const float r = ((v.color >> 16) & 0xFF) / 255.0f;
        const float g = ((v.color >> 8) & 0xFF) / 255.0f;
        const float b = (v.color & 0xFF) / 255.0f;
        // Identity projection: convert pixel (top-left origin) → NDC.
        const float nx = (2.0f * v.x / (float)s_width) - 1.0f;
        const float ny = 1.0f - (2.0f * v.y / (float)s_height);
        const float nz = -0.05f - v.z * 0.001f;
        mo->position(nx, ny, nz);
        mo->normal(0, 0, 1);
        mo->colour(r, g, b, 1.0f);
    };
    for (size_t i = 0; i + 2 < verts.size(); i += 3) {
        emit(verts[i]);
        emit(verts[i + 1]);
        emit(verts[i + 2]);
    }
    OgreEndManualTriangles(mo, true);
}

} // namespace

bool OgreUiInit(int width, int height)
{
    s_width = width;
    s_height = height;
    s_uiVerts.clear();
    return true;
}

void OgreUiShutdown()
{
    s_uiVerts.clear();
}

void OgreUiBegin()
{
    s_uiVerts.clear();
}

void OgreUiFlush()
{
    if (getenv("JJFFE_OGRE_DEBUG_UI") && !s_uiVerts.empty())
        fprintf(stderr, "Ogre UI flush: %zu verts\n", s_uiVerts.size());
    EmitToManual(OgreUiManual(), s_uiVerts);
    s_uiVerts.clear();
}

void OgreClear(float r, float g, float b)
{
    OgreBeginFrame(r, g, b);
}

void OgreFillRect(float x, float y, float w, float h, uint32_t argb)
{
    PushQuad(x, y, x + w, y + h, argb);
}

void OgreDrawText(float x, float y, const char* text, uint32_t argb, float scale)
{
    if (!text) return;
    float cx = x;
    const float px = scale;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            cx = x;
            y += 8.0f * scale;
            continue;
        }
        const uint8_t* g = FontGlyph(*p);
        for (int row = 0; row < 8; ++row) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1 << col)) {
                    float x0 = cx + col * px;
                    float y0 = y + row * px;
                    PushQuad(x0, y0, x0 + px, y0 + px, argb);
                }
            }
        }
        cx += 8.0f * scale;
    }
}

void OgreDrawMenu(uint32_t frameCount, uint32_t currentFps)
{
    OgreClear(0.0f, 0.0f, 0.0f);
    OgreDrawText(160, 40, "Main Menu", 0xFFFFFFFF, 3.0f);
    OgreDrawText(40, 90, "UP/DOWN or W/S   ENTER select   ESC quit", 0xFFAAAAAA, 1.5f);

    int selected = GetMenuSelectedIndex();
    float baseY = 140.0f;
    for (int i = 0; i < g_menuItemCount; i++) {
        float y = baseY + i * 40.0f;
        if (i == selected) {
            OgreFillRect(180, y - 4, 280, 28, 0xFF3355AA);
            OgreDrawText(200, y, g_menuItems[i].label, 0xFFFFFFFF, 2.0f);
        } else {
            OgreFillRect(180, y - 4, 280, 28, 0xFF222244);
            OgreDrawText(200, y, g_menuItems[i].label, 0xFFEEEE66, 2.0f);
        }
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "FPS: %u   Frame: %u", currentFps, frameCount);
    OgreDrawText(20, 360, buf, 0xFF888888, 1.5f);
    OgreEndFrame();
}

void OgreDrawShipSelect()
{
    OgreClear(0.0f, 0.0f, 0.05f);
    OgreDrawText(120, 16, "Select Ship", 0xFFFFFFFF, 3.0f);
    OgreDrawText(20, 56, "W/S move  ENTER=demo  ESC=menu", 0xFFAAAAAA, 1.5f);

    const int count = GetDemoShipSelectCount();
    const int cur = GetDemoShipSelectCursor();
    const int scroll = GetDemoShipSelectScroll();
    constexpr int kVisible = 12;

    char nameBuf[48];
    char line[80];
    float baseY = 90.0f;
    int end = scroll + kVisible;
    if (end > count) end = count;
    for (int row = scroll; row < end; row++) {
        int shipIdx = GetDemoShipSelectAt(row);
        const char* name = GetDemoShipDisplayName(shipIdx, nameBuf, sizeof(nameBuf));
        snprintf(line, sizeof(line), "%3d  %s", shipIdx, name);
        float y = baseY + (row - scroll) * 22.0f;
        if (row == cur) {
            OgreFillRect(30, y - 2, 500, 20, 0xFF3355AA);
            OgreDrawText(40, y, line, 0xFFFFFFFF, 1.5f);
        } else {
            OgreDrawText(40, y, line, 0xFFCCCC66, 1.5f);
        }
    }

    snprintf(line, sizeof(line), "%d / %d", count ? cur + 1 : 0, count);
    OgreDrawText(480, 360, line, 0xFF888888, 1.5f);
    OgreEndFrame();
}

void OgreDrawBriefingStub()
{
    OgreClear(0.05f, 0.05f, 0.0f);
    OgreDrawText(80, 160, "Briefing (Ogre stub)", 0xFFFFFFFF, 3.0f);
    OgreDrawText(80, 220, "ESC = Menu", 0xFFAAAAAA, 2.0f);
    OgreEndFrame();
}

void OgreDrawIntroCast()
{
    // Same navy as HW ship demo — soft 0xFF pixels stay transparent.
    OgreClear(16.0f / 255.0f, 11.0f / 255.0f, 67.0f / 255.0f);
    IntroCastDraw();
    if (auto* hull = OgreHullManual()) {
        OgreBlitSoftBackBuffer(hull);
        hull->setRenderQueueGroup(10);
    }
    OgreEndFrame();
}

} // namespace ogre
} // namespace jjffe
