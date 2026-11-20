#include "jjffe/briefing.h"
#include "jjffe/video.h"
#include "jjffe/platform.h"
#include "jjffe/input.h"
#include "jjffe/draw_main.h"
#include "jjffe/mission_data.h"
#include <cstring>
#include <cstdio>

namespace jjffe {

static int s_missionIndex = 0;
static bool s_keyHeld[256] = {};

bool InitBriefing()
{
    s_missionIndex = 0;
    data::LoadMissionData();
    std::memset(s_keyHeld, 0, sizeof(s_keyHeld));
    // ENTER is still down from Main Menu → Start Mission; ignore until release
    // or briefing is skipped on the same press (same pattern as InitDemoShipSelect).
    s_keyHeld[KEY_ENTER] = true;
    s_keyHeld[KEY_ESCAPE] = true;
    s_keyHeld[KEY_UP] = true;
    s_keyHeld[KEY_DOWN] = true;
    return true;
}

void DrawBriefing()
{
    uint8_t* backBuffer = g_videoState->backBuffer;

    // Black background
    std::memset(backBuffer, 0x00, SCREEN_WIDTH * SCREEN_HEIGHT);

    // Title
    DrawStringLarge(backBuffer, 70, 10, "MISSION BRIEFING", 0x0F);

    int missionCount = data::GetMissionStructCount();

    if (missionCount > 0 && s_missionIndex < missionCount) {
        int y = 40;

        // Mission header
        char headerText[64];
        snprintf(headerText, sizeof(headerText), "Mission %d of %d",
                 s_missionIndex + 1, missionCount);
        DrawStringSimple(backBuffer, 10, y, headerText, 0x1F);
        y += 16;

        // Objectives section
        DrawStringSimple(backBuffer, 10, y, "Objectives:", 0x1F);
        y += 14;
        DrawStringSimple(backBuffer, 10, y, "Objective data pending conversion", 0x0E);
        y += 20;

        // Ship roster
        DrawStringSimple(backBuffer, 10, y, "Available Ships:", 0x1F);
        y += 14;

        int shipCount = data::GetShipObjectCount();
        int displayCount = (shipCount < 6) ? shipCount : 6;
        for (int i = 0; i < displayCount; i++) {
            const uint8_t* shipData = data::GetShipObject(i);
            if (shipData) {
                char shipName[64];
                const char* name = data::GetShipName(shipData, shipName, sizeof(shipName));
                char line[80];
                if (name && name[0] != '\0') {
                    snprintf(line, sizeof(line), "  - %s", name);
                } else {
                    snprintf(line, sizeof(line), "  - Ship %d", i);
                }
                DrawStringSimple(backBuffer, 10, y, line, 0x0E);
                y += 14;
            }
        }

        if (shipCount > 6) {
            char moreText[48];
            snprintf(moreText, sizeof(moreText), "  ... and %d more ships", shipCount - 6);
            DrawStringSimple(backBuffer, 10, y, moreText, 0x0D);
        }
    } else {
        // No mission data
        DrawStringSimple(backBuffer, 80, 90, "No mission data loaded", 0x0E);
    }

    // Instructions at bottom
    DrawStringSimple(backBuffer, 10, 180, "ENTER to launch | ESC to return | UP/DOWN browse", 0x0D);
}

int HandleBriefingInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    int missionCount = data::GetMissionStructCount();

    // UP key — browse to previous mission
    if (keyStates[KEY_UP] == 0) {
        if (!s_keyHeld[KEY_UP]) {
            s_keyHeld[KEY_UP] = true;
            if (s_missionIndex > 0) {
                s_missionIndex--;
            }
        }
    } else {
        s_keyHeld[KEY_UP] = false;
    }

    // DOWN key — browse to next mission
    if (keyStates[KEY_DOWN] == 0) {
        if (!s_keyHeld[KEY_DOWN]) {
            s_keyHeld[KEY_DOWN] = true;
            if (missionCount > 0 && s_missionIndex < missionCount - 1) {
                s_missionIndex++;
            }
        }
    } else {
        s_keyHeld[KEY_DOWN] = false;
    }

    // ENTER — start mission
    if (keyStates[KEY_ENTER] == 0) {
        if (!s_keyHeld[KEY_ENTER]) {
            s_keyHeld[KEY_ENTER] = true;
            return 1;
        }
    } else {
        s_keyHeld[KEY_ENTER] = false;
    }

    // ESC — back to menu
    if (keyStates[KEY_ESCAPE] == 0) {
        if (!s_keyHeld[KEY_ESCAPE]) {
            s_keyHeld[KEY_ESCAPE] = true;
            return -1;
        }
    } else {
        s_keyHeld[KEY_ESCAPE] = false;
    }

    return 0;  // stay
}

} // namespace jjffe
