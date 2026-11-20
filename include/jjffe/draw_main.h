#ifndef JJFFE_DRAW_MAIN_H
#define JJFFE_DRAW_MAIN_H

#include <cstdint>

namespace jjffe {

void DrawBitmap(uint8_t* buffer, int x, int y, const uint8_t* bmpData, bool masked = false);
void DrawCharSimple(uint8_t* buffer, int x, int y, char ch, uint8_t color);
void DrawCharLarge(uint8_t* buffer, int x, int y, char ch, uint8_t color);
void DrawStringSimple(uint8_t* buffer, int x, int y, const char* str, uint8_t color);
void DrawStringLarge(uint8_t* buffer, int x, int y, const char* str, uint8_t color);
void FillRect(uint8_t* buffer, int x, int y, int w, int h, uint8_t color);

} // namespace jjffe

#endif // JJFFE_DRAW_MAIN_H
