#ifndef JJFFE_PLATFORM_H
#define JJFFE_PLATFORM_H

#include <cstdint>

namespace jjffe {

void VideoInit();
void VideoCleanup();
void VideoBlit(const uint8_t* data, int x, int y, int w, int h, int jump);
void FlipScreen();

void SoundInit();
void SoundCleanup();

void InputInit();
void InputCleanup();

void TimerInit();
void TimerCleanup();
void TimerAdvance();

// SDL platform functions
void PlatformVideoInit();
void PlatformVideoCleanup();
void PlatformVideoBlit();
// Extra pixels below the 640x400 game blit (INTRO_PLAYER strip). 0 = default.
void PlatformSetControlStripHeight(int height);
int PlatformGetControlStripHeight();
// Called after the 2x game blit with the screen locked; stripY is the first
// row of the control strip (normally 400). pixels are 32-bit ARGB/XRGB.
using PlatformStripPainterFn =
    void (*)(uint32_t* pixels, int pitchWords, int stripY, int stripH, int width);
void PlatformSetControlStripPainter(PlatformStripPainterFn fn);
// Raw SDL window mouse position (not scaled to 320x200).
void PlatformGetWindowMousePos(int* x, int* y);
void PlatformSetPaletteEntry(int index, uint8_t r, uint8_t g, uint8_t b);
// Read back one live display-palette entry (8-bit RGB). Dynamic bank (>=0x80)
// lives only here — g_videoState->palette holds the static half.
bool PlatformGetPaletteEntry(int index, uint8_t* r, uint8_t* g, uint8_t* b);
// A/B diagnostics: write the 256 live display palette entries as {r,g,b,0}
// (the ASM's sdlvideo pPalette[256] SDL_Color layout) so a dumped frame can
// be compared in colour space — the dynamic bank is rebuilt every frame.
void PlatformDumpPalette(const char* path);
bool PlatformProcessEvents();
// True when a GPU backend owns drawing (ffelnxogre / ffelnxd3d). Soft SDL returns false.
bool PlatformIsGpuDraw();
// Deprecated alias — prefer PlatformIsGpuDraw(). True for Ogre and D3D9 backends.
bool PlatformIsD3D();
uint32_t PlatformGetTicks();
void PlatformDelay(uint32_t ms);

// SDL input functions
void PlatformInputInit();
void PlatformInputCleanup();
void InputKeybReadStates(uint8_t* pKeys);

// 4.458: the bulletin board's advert copy, from the extracted services tables
// (services_tables.inc DATA_002199/002200/002206). NULL when index is out of range.
const char* BulletinAdvertLine(int index);

// 4.461: how many live adverts this station currently has - the mission records' advert slots
// (DATA_008999 + i*0x322, i < DATA_008997; advert kind at record + idx*0x18 + 0x132).
int BulletinAdvertCount();
int InputKeybGetLastKey();
void InputKeybSetLastKey(int key);
int InputMouseReadButtons();
void InputMouseReadMickeys(int* pXMick, int* pYMick);
void InputMouseReadPos(int* pXPos, int* pYPos);
void InputJoyReadPos(int* xpos, int* ypos);
int InputJoyReadButtons();

// SDL sound functions
void PlatformSoundInit();
void PlatformSoundCleanup();

// Phase 56: SDL_mixer music functions
void PlatformMusicInit();
void PlatformMusicCleanup();
void PlatformMusicPlaySong(int songId);
// The game's song table: id -> name (see sdl_sound.cpp). Null when out of range.
const char* PlatformMusicSongName(int songId);
int PlatformMusicSongCount();
void PlatformMusicStopSong();
bool PlatformMusicSongDone();
void PlatformMusicSetVolume(int volume);

// SDL timer functions
void PlatformTimerInit();
void PlatformTimerCleanup();

} // namespace jjffe

#endif
