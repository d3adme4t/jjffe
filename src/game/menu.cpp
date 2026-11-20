#include "jjffe/menu.h"
#include "jjffe/game_loop.h"
#include "jjffe/video.h"
#include "jjffe/platform.h"
#include "jjffe/input.h"
#include "jjffe/draw.h"
#include "jjffe/draw_main.h"
#include "jjffe/ui.h"
#include "jjffe/briefing.h"
#include "jjffe/demo3d.h"
#include <cstring>
#include <cstdio>

namespace jjffe {

// Menu items: label + action
const MenuItem g_menuItems[] = {
    { "Start Mission", 1 },
    { "Load Game",     2 },
    { "3D Demo",       3 },
    { "Quit",          4 },
};
const int g_menuItemCount = 4;

// Internal state
static int s_selectedIndex = 0;
static bool s_keyHeld[256] = {};

void MenuResetInput()
{
    // Treat currently relevant keys as already held until released.
    s_keyHeld[KEY_ENTER] = true;
    s_keyHeld[KEY_ESCAPE] = true;
    s_keyHeld[KEY_UP] = true;
    s_keyHeld[KEY_DOWN] = true;
    s_keyHeld[KEY_W] = true;
    s_keyHeld[KEY_S] = true;
    s_keyHeld[KEY_SPACE] = true;
}

int GetMenuSelectedIndex()
{
    return s_selectedIndex;
}

void DrawMenuScreen(uint32_t frameCount, uint32_t currentFps)
{
    uint8_t* backBuffer = g_videoState->backBuffer;

    // Black background
    memset(backBuffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT);

    // Title centered at top
    DrawStringLarge(backBuffer, 100, 20, "Main Menu", 0x0F);
    DrawStringSimple(backBuffer, 20, 42, "UP/DOWN or W/S  ENTER select  ESC quit", 0x0D);

    // Menu items with highlight on selected
    int baseY = 70;
    for (int i = 0; i < g_menuItemCount; i++) {
        int y = baseY + i * 20;
        int x = 100;
        if (i == s_selectedIndex) {
            // Highlight: white rect behind text, dark text
            int rectW = MeasureStringWidth(g_menuItems[i].label) + 8;
            FillRect(backBuffer, x - 4, y - 2, rectW, 14, 0x1F);
            DrawStringSimple(backBuffer, x, y, g_menuItems[i].label, 0x00);
        } else {
            DrawStringSimple(backBuffer, x, y, g_menuItems[i].label, 0x0E);
        }
    }

    // FPS counter
    char fpsText[32];
    snprintf(fpsText, sizeof(fpsText), "FPS: %u", currentFps);
    DrawStringSimple(backBuffer, 10, 180, fpsText, 0x0D);

    // Frame counter
    char frameText[32];
    snprintf(frameText, sizeof(frameText), "Frame: %u", frameCount);
    DrawStringSimple(backBuffer, 100, 180, frameText, 0x0D);
}

int HandleMenuInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    auto edge = [&](int key) -> bool {
        if (keyStates[key] == 0) {
            if (!s_keyHeld[key]) {
                s_keyHeld[key] = true;
                return true;
            }
        } else {
            s_keyHeld[key] = false;
        }
        return false;
    };

    // Arrows (fixed scancodes) + W/S fallback
    if (edge(KEY_UP) || edge(KEY_W)) {
        s_selectedIndex--;
        if (s_selectedIndex < 0) {
            s_selectedIndex = g_menuItemCount - 1;
        }
    }
    if (edge(KEY_DOWN) || edge(KEY_S)) {
        s_selectedIndex++;
        if (s_selectedIndex >= g_menuItemCount) {
            s_selectedIndex = 0;
        }
    }

    if (edge(KEY_ENTER)) {
        return g_menuItems[s_selectedIndex].action;
    }
    if (edge(KEY_ESCAPE)) {
        return -1;
    }

    return 0;  // stay
}

void MenuProcessAction(int action)
{
    switch (action) {
    case 1:  // Start Mission → Briefing
        GameLoopTransition(GamePhase::Briefing);
        InitBriefing();
        break;
    case 2:  // Load Game → open SysMenu load dialog
        SysMenuSelectLoadDialog();
        break;
    case 3:  // 3D Demo → ship picker, then Demo3D
        InitDemoShipSelect();
        GameLoopTransition(GamePhase::DemoShipSelect);
        break;
    case 4:  // Quit
        GameLoopTransition(GamePhase::Shutdown);
        break;
    default:
        // T-59-06: Unknown action — do nothing
        fprintf(stderr, "MenuProcessAction: unknown action %d, ignoring\n", action);
        break;
    }
}

} // namespace jjffe
