#ifndef JJFFE_MENU_H
#define JJFFE_MENU_H

#include <cstdint>

namespace jjffe {

struct MenuItem {
    const char* label;
    int action;  // 0=stay, 1=start mission, 2=load game, 3=3D demo, 4=quit
};

extern const MenuItem g_menuItems[];
extern const int g_menuItemCount;

void DrawMenuScreen(uint32_t frameCount, uint32_t currentFps);
int  HandleMenuInput();
void MenuProcessAction(int action);
// Call when entering the menu so keys still held from the previous screen
// (Space from title, Esc from ship select) are not treated as new presses.
void MenuResetInput();
int GetMenuSelectedIndex();

} // namespace jjffe

#endif // JJFFE_MENU_H
