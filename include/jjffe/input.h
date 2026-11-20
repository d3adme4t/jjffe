#ifndef JJFFE_INPUT_H
#define JJFFE_INPUT_H

#include <cstdint>

namespace jjffe {

static constexpr int MAX_HOT_AREAS = 64;

struct HotArea {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
    int16_t id;
};

struct XYAccum {
    int16_t x;
    int16_t y;
};

// Key codes (subset - extend as needed)
enum KeyCode : uint8_t {
    KEY_ESCAPE = 0x01,
    KEY_1 = 0x02,
    KEY_2 = 0x03,
    KEY_3 = 0x04,
    KEY_4 = 0x05,
    KEY_5 = 0x06,
    KEY_6 = 0x07,
    KEY_7 = 0x08,
    KEY_8 = 0x09,
    KEY_9 = 0x0A,
    KEY_0 = 0x0B,
    KEY_Q = 0x10,
    KEY_W = 0x11,
    KEY_E = 0x12,
    KEY_R = 0x13,
    KEY_T = 0x14,
    KEY_Y = 0x15,
    KEY_U = 0x16,
    KEY_I = 0x17,
    KEY_O = 0x18,
    KEY_P = 0x19,
    KEY_LEFTBRACKET = 0x1A,
    KEY_RIGHTBRACKET = 0x1B,
    KEY_A = 0x1E,
    KEY_S = 0x1F,
    KEY_D = 0x20,
    KEY_F = 0x21,
    KEY_G = 0x22,
    KEY_H = 0x23,
    KEY_J = 0x24,
    KEY_K = 0x25,
    KEY_L = 0x26,
    KEY_Z = 0x2C,
    KEY_X = 0x2D,
    KEY_C = 0x2E,
    KEY_V = 0x2F,
    KEY_B = 0x30,
    KEY_N = 0x31,
    KEY_M = 0x32,
    KEY_COMMA = 0x33,
    KEY_PERIOD = 0x34,
    KEY_F1 = 0x3B,
    KEY_F2 = 0x3C,
    KEY_F3 = 0x3D,
    KEY_F4 = 0x3E,
    KEY_F5 = 0x3F,
    KEY_F6 = 0x40,
    KEY_F7 = 0x41,
    KEY_F8 = 0x42,
    KEY_F9 = 0x43,
    KEY_F10 = 0x44,
    KEY_F11 = 0x57,
    KEY_F12 = 0x58,
    // Arrow keys: extended (E0) scancodes — matches pKeybConvTable / SDLK_UP etc.
    // (0x48/0x50/0x4B/0x4D are numpad 8/2/4/6, not the grey arrow keys.)
    KEY_UP = 0xC8,
    KEY_PGUP = 0xC9,
    KEY_LEFT = 0xCB,
    KEY_RIGHT = 0xCD,
    KEY_DOWN = 0xD0,
    KEY_PGDN = 0xD1,
    KEY_SPACE = 0x39,
    KEY_ENTER = 0x1C,
    KEY_TAB = 0x0F,
    KEY_BACKSPACE = 0x0E,
    KEY_MINUS = 0x0C,
    KEY_EQUALS = 0x0D,
    KEY_PLUS = 0x4E, // numpad +
    // Numpad digits (scancode set 1; distinct from the grey arrow keys above).
    KEY_KP7 = 0x47,
    KEY_KP8 = 0x48,
    KEY_KP9 = 0x49,
    KEY_KP4 = 0x4B,
    KEY_KP5 = 0x4C,
    KEY_KP6 = 0x4D,
    KEY_KP1 = 0x4F,
    KEY_KP2 = 0x50,
    KEY_KP3 = 0x51,
    KEY_LSHIFT = 0x2A,
    KEY_RSHIFT = 0x36,
    KEY_LCTRL = 0x1D,
    KEY_RCTRL = 0x9D,
    KEY_LALT = 0x38,
    KEY_RALT = 0xB8,
};

// Mouse button flags
enum MouseButton : uint8_t {
    MOUSE_LEFT = 0x01,
    MOUSE_RIGHT = 0x02,
    MOUSE_MIDDLE = 0x04,
};

// Input state structure
struct InputState {
    // Keyboard
    uint8_t keyStates[256];     // Current key states (0 = pressed)
    uint8_t lastKey;            // Last key pressed
    uint8_t prevLastKey;        // Previous last key
    bool keyRepeat;             // Key repeat active
    
    // Mouse
    int16_t mouseX;             // Current mouse X position
    int16_t mouseY;             // Current mouse Y position
    int16_t mouseDeltaX;        // Mouse movement delta X
    int16_t mouseDeltaY;        // Mouse movement delta Y
    uint8_t mouseButtons;       // Current mouse button state
    uint8_t prevMouseButtons;   // Previous mouse button state
    bool mouseLeftPressed;      // Left button just pressed
    bool mouseRightPressed;     // Right button just pressed
    
    // Joystick
    int16_t joyX;               // Joystick X position
    int16_t joyY;               // Joystick Y position
    uint8_t joyButtons;         // Joystick button state
    
    // GUI state
    int16_t guiMouseX;          // GUI mouse X (clamped to screen)
    int16_t guiMouseY;          // GUI mouse Y (clamped to screen)
    uint8_t lastAction;         // Last GUI action
    uint8_t prevAction;         // Previous GUI action
    uint8_t actionHoldTimer;    // Timer for action repeat
};

// Global input state
extern InputState g_inputState;

// GUI hotspot state
extern HotArea g_hotAreas[MAX_HOT_AREAS + 1];
extern int g_hotAreaCount;
extern XYAccum g_xyAccum;

// Keyboard functions
void KeybInit();
void KeybCleanup();
bool KeybGetKeyState(int keyCode);

// Mouse functions
void MouseInit();

// Joystick functions
void JoyInit();
void JoyPollAxes();
void JoyUpdate();
int16_t JoyConvPollVal(int16_t value, int16_t minVal, int16_t maxVal);
void JoyCalibrate();
void JoyGetVals(int16_t* outX, int16_t* outY, uint8_t* outButtons);

// Input initialization/cleanup
void InputInit();
void InputCleanup();

// Input reading (calls platform layer)
void InputReadAll();

// GUI input processing
void GuiReadAllInput(int16_t* outMouseX, int16_t* outMouseY);
uint32_t GuiProcessInput();

// Utility functions
void GuiSetLastAction(uint8_t action);
uint8_t GuiGetLastAction();

// GUI Hotspot functions (Phase 12)
void GuiClearHotAreas();
void GuiAddHotRect(int16_t centerX, int16_t centerY, int16_t halfWidth, int16_t halfHeight, int16_t id);
void GuiAddHotSquare(int16_t centerX, int16_t centerY, int16_t halfWidth, int16_t id);
void GuiAddHotRect2(int16_t left, int16_t top, int16_t width, int16_t height, int16_t id);
void GuiAddHotAreas(const HotArea* areas);
void GuiRemoveHotArea(int16_t id);
int16_t GuiHitTestHotAreas(int16_t x, int16_t y);
void GuiSetXYAccum(int16_t x, int16_t y);
XYAccum GuiGetXYAccum();

// DOS-specific functions (no-op in C++/SDL)
void InstallIntHandler();
void setupkeyhandler();

// SDL-specific: check if quit was requested
bool InputIsQuitRequested();

// Internal: process SDL events (called by PlatformProcessEvents)
void InputProcessEvents();

} // namespace jjffe

#endif // JJFFE_INPUT_H
