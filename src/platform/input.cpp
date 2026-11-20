#include "jjffe/input.h"
#include "jjffe/platform.h"
#include <cstring>
#include <algorithm>

#ifdef JJFFE_USE_SDL
#include <SDL.h>
#endif

namespace jjffe {

// Global input state
InputState g_inputState = {};

// GUI hotspot state
HotArea g_hotAreas[MAX_HOT_AREAS + 1] = {};
int g_hotAreaCount = 0;
XYAccum g_xyAccum = {};

// Keyboard functions

// FUNC_001407_KeybInit
void KeybInit()
{
    g_inputState.keyRepeat = false;
    g_inputState.lastKey = 0xFF;
    
#ifdef JJFFE_USE_SDL
    PlatformInputInit();
#endif
}

// FUNC_001408_KeybCleanup
void KeybCleanup()
{
    // Nothing to cleanup
}

// Input initialization (calls all sub-inits)
void InputInit()
{
    KeybInit();
    MouseInit();
    JoyInit();
}

// Input cleanup (calls all sub-cleanups)
void InputCleanup()
{
    KeybCleanup();
}

// FUNC_001406_KeybGetKeyState
bool KeybGetKeyState(int keyCode)
{
    if (keyCode < 0 || keyCode >= 256) {
        return false;
    }
    return g_inputState.keyStates[keyCode] == 0; // 0 = pressed
}

// Mouse functions

// FUNC_001427_MouseInit
void MouseInit()
{
    g_inputState.mouseX = 160;  // Center of screen (320/2)
    g_inputState.mouseY = 100;  // Center of screen (200/2)
}

// Joystick state
static int16_t joyCenterX = 0;
static int16_t joyCenterY = 0;
static int16_t joyMinX = 0;
static int16_t joyMinY = 0;
static int16_t joyMaxX = 0;
static int16_t joyMaxY = 0;
static bool joyCalibrated = false;

// Joystick functions
void JoyInit()
{
    g_inputState.joyX = 0;
    g_inputState.joyY = 0;
    g_inputState.joyButtons = 0;
    joyCenterX = 0;
    joyCenterY = 0;
    joyMinX = 0;
    joyMinY = 0;
    joyMaxX = 0;
    joyMaxY = 0;
    joyCalibrated = false;
}

// FUNC_001433_JoyPollAxes
void JoyPollAxes()
{
    int xPos = 0, yPos = 0;
    InputJoyReadPos(&xPos, &yPos);
    g_inputState.joyX = (int16_t)xPos;
    g_inputState.joyY = (int16_t)yPos;
}

// FUNC_001434_JoyUpdate
void JoyUpdate()
{
    JoyPollAxes();
    
    if (joyCalibrated) {
        int16_t x = g_inputState.joyX - joyCenterX;
        int16_t y = g_inputState.joyY - joyCenterY;
        
        x = JoyConvPollVal(x, joyMinX, joyMaxX);
        y = JoyConvPollVal(y, joyMinY, joyMaxY);
        
        g_inputState.joyX = x;
        g_inputState.joyY = y;
    }
    
    g_inputState.joyButtons = (uint8_t)InputJoyReadButtons();
}

// FUNC_001435_JoyConvPollVal
int16_t JoyConvPollVal(int16_t value, int16_t minVal, int16_t maxVal)
{
    int16_t range = maxVal - minVal;
    if (range <= 0) return 0;
    
    int16_t centered = value - minVal;
    int16_t normalized = (centered * 200) / range - 100;
    
    if (normalized < -100) normalized = -100;
    if (normalized > 100) normalized = 100;
    
    return normalized;
}

// FUNC_001436_JoyCalibrate
void JoyCalibrate()
{
    JoyPollAxes();
    joyCenterX = g_inputState.joyX;
    joyCenterY = g_inputState.joyY;
    
    joyMinX = joyCenterX - 100;
    joyMinY = joyCenterY - 100;
    joyMaxX = joyCenterX + 100;
    joyMaxY = joyCenterY + 100;
    
    joyCalibrated = true;
}

// FUNC_001437_JoyGetVals
void JoyGetVals(int16_t* outX, int16_t* outY, uint8_t* outButtons)
{
    JoyUpdate();
    
    if (outX) *outX = g_inputState.joyX;
    if (outY) *outY = g_inputState.joyY;
    if (outButtons) *outButtons = g_inputState.joyButtons;
}

// Input reading

// SDL input function stubs (overridden by sdl_input.cpp when SDL is enabled)
__attribute__((weak)) void PlatformInputInit()
{
}

__attribute__((weak)) void InputKeybReadStates(uint8_t* pKeys)
{
    // Match the SDL/D3D/Ogre backends' contract: all keys up (0x80 = not
    // pressed). Leaving the buffer uninitialized here made callers like
    // SampleFlightInput read garbage, flapping ShipControlsTests.
    memset(pKeys, 0x80, 256);
}

__attribute__((weak)) int InputKeybGetLastKey()
{
    return 0xFF;
}

__attribute__((weak)) void InputKeybSetLastKey(int)
{
}

__attribute__((weak)) int InputMouseReadButtons()
{
    return 0;
}

__attribute__((weak)) void InputMouseReadMickeys(int* pXMick, int* pYMick)
{
    if (pXMick) *pXMick = 0;
    if (pYMick) *pYMick = 0;
}

__attribute__((weak)) void InputMouseReadPos(int* pXPos, int* pYPos)
{
    if (pXPos) *pXPos = 160;
    if (pYPos) *pYPos = 100;
}

__attribute__((weak)) void InputJoyReadPos(int* xpos, int* ypos)
{
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}

__attribute__((weak)) int InputJoyReadButtons()
{
    return 0;
}

void InputReadAll()
{
#ifdef JJFFE_USE_SDL
    // Read keyboard state
    InputKeybReadStates(g_inputState.keyStates);
    
    // Read mouse buttons
    g_inputState.mouseButtons = (uint8_t)InputMouseReadButtons();
    
    // Read mouse position
    int mouseX = 0, mouseY = 0;
    InputMouseReadPos(&mouseX, &mouseY);
    g_inputState.mouseX = (int16_t)mouseX;
    g_inputState.mouseY = (int16_t)mouseY;
    
    // Read mouse relative movement
    int relX = 0, relY = 0;
    InputMouseReadMickeys(&relX, &relY);
    g_inputState.mouseDeltaX = (int16_t)relX;
    g_inputState.mouseDeltaY = (int16_t)relY;
    
    // Read joystick
    int joyX = 0, joyY = 0;
    InputJoyReadPos(&joyX, &joyY);
    g_inputState.joyX = (int16_t)joyX;
    g_inputState.joyY = (int16_t)joyY;
    g_inputState.joyButtons = (uint8_t)InputJoyReadButtons();
    
    // Update last key
    g_inputState.lastKey = (uint8_t)InputKeybGetLastKey();
#else
    // Stub for non-SDL builds
    (void)g_inputState;
#endif
}

// GUI input processing

// FUNC_001429_GuiReadAllInput
void GuiReadAllInput(int16_t* outMouseX, int16_t* outMouseY)
{
    // Read keyboard state
    InputReadAll();
    
    // Process keyboard input
    uint8_t currentKey = g_inputState.lastKey;
    
    if (currentKey != 0xFF) {
        if (currentKey != g_inputState.prevLastKey) {
            g_inputState.keyRepeat = true;
            g_inputState.prevLastKey = currentKey;
        }
    }
    
    // Check if key is still pressed
    if (g_inputState.keyStates[g_inputState.prevLastKey] != 0) {
        g_inputState.keyRepeat = false;
        g_inputState.prevLastKey = 0xFF;
    }
    
    // Read mouse buttons
    uint8_t mouseButtons = g_inputState.mouseButtons;
    
    g_inputState.mouseLeftPressed = (mouseButtons & MOUSE_LEFT) != 0;
    g_inputState.mouseRightPressed = (mouseButtons & MOUSE_RIGHT) != 0;
    
    // Read mouse position
    if (!g_inputState.mouseButtons) {
        // Only update position if no buttons pressed (windowed mode)
        // In exclusive mode, position is tracked via mickeys
    }
    
    // Clamp mouse position to screen bounds
    if (g_inputState.mouseX < 0) g_inputState.mouseX = 0;
    if (g_inputState.mouseX > 319) g_inputState.mouseX = 319;
    if (g_inputState.mouseY < 0) g_inputState.mouseY = 0;
    if (g_inputState.mouseY > 199) g_inputState.mouseY = 199;
    
    // Update GUI mouse position
    g_inputState.guiMouseX = g_inputState.mouseX;
    g_inputState.guiMouseY = g_inputState.mouseY;
    
    // Output mouse position
    if (outMouseX) *outMouseX = g_inputState.mouseDeltaX;
    if (outMouseY) *outMouseY = g_inputState.mouseDeltaY;
    
    // Apply joystick input to mouse deltas (for game control)
    // This allows joystick to control cursor in certain modes
    
    // Reset last key
    g_inputState.lastKey = 0xFF;
}

// g_inputState poll for the port-only GameUpdate loop (state.cpp). Not the ASM
// routine: FUNC_001432_GuiProcessInput is in core.cpp, called through DATA_007674.
uint32_t GuiProcessInput()
{
    // Read joystick buttons
    [[maybe_unused]] uint8_t joyButtons = g_inputState.joyButtons;
    
    // Read joystick position
    int16_t joyDeltaX = g_inputState.joyX;
    int16_t joyDeltaY = g_inputState.joyY;
    
    // Accumulate joystick position
    g_inputState.joyX += joyDeltaX;
    g_inputState.joyY += joyDeltaY;
    
    // Read all input
    int16_t mouseDeltaX, mouseDeltaY;
    GuiReadAllInput(&mouseDeltaX, &mouseDeltaY);
    
    // Process mouse input
    uint32_t mousePos = 0;
    
    if (!g_inputState.mouseRightPressed) {
        // Normal mouse mode
        mousePos = (g_inputState.mouseY << 16) | (g_inputState.mouseX & 0xFFFF);
        
        // Check for GUI hotspots
        uint8_t hotspotAction = 0xFF;
        
        // This would check against GUI hotspot table
        // For now, just return the position
        
        // Handle mouse button press
        if (g_inputState.mouseLeftPressed) {
            GuiSetLastAction(hotspotAction);
        }
    } else {
        // Right button pressed - disable pointer
        // Would call DisablePtr()
        mousePos = (g_inputState.mouseY << 16) | (g_inputState.mouseX & 0xFFFF);
    }
    
    return mousePos;
}

// FUNC_001410_GuiSetLastActionInternal
void GuiSetLastAction(uint8_t action)
{
    g_inputState.prevAction = g_inputState.lastAction;
    g_inputState.lastAction = action;
    g_inputState.actionHoldTimer = 1;
}

// FUNC_001414_GuiGetLastAction
uint8_t GuiGetLastAction()
{
    uint8_t action = g_inputState.lastAction;
    g_inputState.lastAction = 0;
    return action;
}

// GUI Hotspot functions (Phase 12)

// FUNC_001438_GuiClearHotAreas
void GuiClearHotAreas()
{
    g_hotAreas[0].left = -1;
    g_hotAreaCount = 0;
}

// FUNC_001441_GuiAddHotRect
void GuiAddHotRect(int16_t centerX, int16_t centerY, int16_t halfWidth, int16_t halfHeight, int16_t id)
{
    if (g_hotAreaCount >= MAX_HOT_AREAS) return;
    
    HotArea* area = &g_hotAreas[g_hotAreaCount];
    area->left = std::max<int16_t>(0, centerX - halfWidth);
    area->top = std::max<int16_t>(0, centerY - halfHeight);
    area->right = centerX + halfWidth;
    area->bottom = centerY + halfHeight;
    area->id = id;
    
    g_hotAreaCount++;
    g_hotAreas[g_hotAreaCount].left = -1;
}

// FUNC_001442_GuiAddHotSquare
void GuiAddHotSquare(int16_t centerX, int16_t centerY, int16_t halfWidth, int16_t id)
{
    GuiAddHotRect(centerX, centerY, halfWidth, halfWidth, id);
}

// FUNC_001461_GuiAddHotRect2
void GuiAddHotRect2(int16_t left, int16_t top, int16_t width, int16_t height, int16_t id)
{
    if (g_hotAreaCount >= MAX_HOT_AREAS) return;
    
    HotArea* area = &g_hotAreas[g_hotAreaCount];
    area->left = left;
    area->top = top;
    area->right = left + width;
    area->bottom = top + height;
    area->id = id;
    
    g_hotAreaCount++;
    g_hotAreas[g_hotAreaCount].left = -1;
}

// FUNC_001440_GuiAddHotAreas
void GuiAddHotAreas(const HotArea* areas)
{
    if (!areas) return;
    
    while (areas->left != -1) {
        if (g_hotAreaCount >= MAX_HOT_AREAS) break;
        
        g_hotAreas[g_hotAreaCount] = *areas;
        g_hotAreaCount++;
        areas++;
    }
    
    g_hotAreas[g_hotAreaCount].left = -1;
}

// FUNC_001443_GuiRemoveHotArea
void GuiRemoveHotArea(int16_t id)
{
    int foundIndex = -1;
    
    for (int i = 0; i < g_hotAreaCount; i++) {
        if (g_hotAreas[i].id == id) {
            foundIndex = i;
            break;
        }
    }
    
    if (foundIndex >= 0) {
        for (int i = foundIndex; i < g_hotAreaCount; i++) {
            g_hotAreas[i] = g_hotAreas[i + 1];
        }
        g_hotAreaCount--;
    }
}

// Hit test for hot areas (checks if point is inside any hot area)
int16_t GuiHitTestHotAreas(int16_t x, int16_t y)
{
    for (int i = 0; i < g_hotAreaCount; i++) {
        const HotArea& area = g_hotAreas[i];
        if (x >= area.left && x < area.right &&
            y >= area.top && y < area.bottom) {
            return area.id;
        }
    }
    return -1;
}

// FUNC_001444_GuiSetXYAccum
void GuiSetXYAccum(int16_t x, int16_t y)
{
    g_xyAccum.x = x;
    g_xyAccum.y = y;
}

// FUNC_001445_GuiGetXYAccum
XYAccum GuiGetXYAccum()
{
    return g_xyAccum;
}

// FUNC_001404_InstallIntHandler
void InstallIntHandler()
{
    // DOS-specific: installs keyboard interrupt handler
    // No-op in C++/SDL version
}

// FUNC_001655_setupkeyhandler
void setupkeyhandler()
{
    // DOS-specific: sets up key handler
    // No-op in C++/SDL version
}

} // namespace jjffe
