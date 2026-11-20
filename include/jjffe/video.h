#ifndef JJFFE_VIDEO_H
#define JJFFE_VIDEO_H

#include <cstdint>
#include <cstdio>

namespace jjffe {

// Screen dimensions
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 200;
constexpr int SCREEN_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;

// Palette. The game's palette block is 0x300 bytes = 256 VGA entries
// (core.cpp:5814 "Palette (768 bytes)"), and the setters already guard with
// `index < 256`; a 128-entry cache silently dropped the upper half and let
// readers index past the 384-byte array (UBSan: index 384 out of bounds).
constexpr int PALETTE_ENTRIES = 256;

// Bitmap structure (matches original format)
struct Bitmap {
    uint8_t flags;        // Bit 0 = masked blit
    uint8_t height;       // Height in pixels
    int16_t width;        // Width in pixels
    int16_t xOff;         // X offset
    int16_t yOff;         // Y offset
    uint8_t data[];       // Pixel data (variable length)
};

// Maximum bitmap table entries
constexpr int MAX_BITMAPS = 256;

// Blitting functions - operate on the 320x200 back buffer
// These copy rectangular regions with different behaviors

// BufferBlit: Copy source to back buffer (opaque)
void BufferBlit(const uint8_t* src, int x, int y, int width, int height, int srcJump);

// BufferMaskedBlit: Copy source to back buffer with transparency (0 = skip pixel)
void BufferMaskedBlit(const uint8_t* src, int x, int y, int width, int height, int srcJump);

// BufferReverseBlit: Copy back buffer to destination (save background)
void BufferReverseBlit(uint8_t* dst, int x, int y, int width, int height, int dstJump);

// Get pointer to the back buffer
uint8_t* GetBackBuffer();

// Flip screen - copy back buffer to display and handle cursor
void FlipScreen();
void FlipScreenLow();
void VideoBlitBuffer();

// Video initialization
void VideoInit();
void VideoCleanup();

// Palette management
void SetWholePalette();
void SetPaletteEntry(int index, uint8_t r, uint8_t g, uint8_t b);
// Reload VGA palette from data blob (DATA_007768+0x18). Call on mission entry
// so flight view is not stuck on a title/scene-specific palette.
void LoadGamePalette();

// Pointer/cursor system: FUNC_001630/1631/1632/1635/1636 in src/core/core.cpp.

// Bitmap/Blit functions (Phase 16)
// Bitmap table management
void SetBitmapTable(Bitmap** table, int count);
Bitmap* BmpIndexToPtr(int index);

// Blit by index (bitmap table lookup)
void* BlitIndex(uint8_t* dst, int bmpIndex, int x, int y);
void* BlitClipIndex(uint8_t* dst, int bmpIndex, int x, int y, 
                    int clipLeft, int clipTop, int clipRight, int clipBottom);

// Blit to back buffer
void BlitIndexToBuf(int bmpIndex, int x, int y);
void BlitClipIndexToBuf(int bmpIndex, int x, int y, 
                        int clipLeft, int clipTop, int clipRight, int clipBottom);

// Video to buffer (restore background + pointer)
void BlitVidToBuf();

// Internal blit functions
void BlitClipInternal(uint8_t* dst, int x, int y, const uint8_t* src,
                      int clipLeft, int clipTop, int clipRight, int clipBottom);
void BlitInternal(uint8_t* dst, const uint8_t* src, int width, int height,
                  int dstJump, int srcJump, const Bitmap* bmp);

// Dynamic palette functions (Phase 18A)
constexpr int DPAL_MAP_SIZE = 4096;
constexpr int DPAL_ENTRIES = 64;
// ASM DATA_009140 is indexed by 1-based cluster id (stamp writes box+1).
constexpr int DPAL_REVERSE_SIZE = DPAL_ENTRIES + 1;

void DPalClear();
void DPal32to16(uint32_t rgb);
uint8_t DPal16toPal(uint16_t index);
// Diagnostic: histogram of the 12-bit cells DPal16toPal / FUNC_001327 resolve
// (nullptr = off). AB_DPAL_DUMP_DIR arms it from DPalBuild to the frame dump.
void SetDpalLookupTap(uint32_t* hist4096);
void DPalLookupTapNote(uint16_t index);
void DPalBuild(const uint8_t* palette, int baseIndex);

// 4.408: the game loop owns g_bss8804 and already uses bit 0 of word 0x0c to
// choose DrawMission over the DPalBuild chain. Reading that header-defined
// static reference from the platform module proved unreliable (separate object
// per translation unit, 4.407), so the caller passes the decision in.
void DPalSetSceneBuilds(bool buildsHere);

// R1215: game loop arms this on JUMP_003849 fall-through frames (rs809>=0)
// under AB_DPAL_SHIFT_FIX so DPalSet skips install+XOR — ASM's fall-through
// never reaches FUNC_001637/DPalSet; the port's GameLoop FlipScreen still does.
void DPalNotifyPanelIdleSkip(bool skip);

// R1216: AB_DPAL_PANEL_INSTALL — FUNC_001637 marks a successful inline
// install+XOR; outer GameLoop FlipScreen consumes it to avoid a second XOR.
void DPalMarkInlineSet(bool ran);
bool DPalConsumeInlineSet();

// 4.409: scenes that do not rebuild the palette (the DrawMission branch never
// calls DPalBuild, 4.402) must start from an empty reverse map, otherwise the
// stale tail persists and degrades the water oracle (nearest 0.52 -> 3.93).
void DPalClearReverse();
// Returns true when install+XOR ran (false on SHIFT_FIX panel-idle skip).
bool DPalSet();
int DPalGetChangedCount();
const uint8_t* DPalGetChangedEntries();

// Screenshot functions (Phase 18C)
void ScrnShotSave();
// Save current back buffer to an explicit path (for A/B capture harness)
bool ScrnShotSaveTo(const char* path);
void ScrnshotWriteHeader(FILE* file);
void ScrnShotWritePalette(FILE* file);
void ScrnshotWriteData(FILE* file);

// Video state structure
struct VideoState {
    uint8_t backBuffer[SCREEN_SIZE];
    
    // Z-buffer (heap-allocated, 320x200 int16_t = 128KB)
    int16_t* zBuffer;
    bool zBufferEnabled;
    
    // Pointer/cursor state
    int ptrIndex;
    int32_t ptrEnabled;      // ASM ptrEnabled / ptrExclusive are dwords
    int32_t ptrExclusive;
    int ptrSavedX, ptrSavedY; // Saved position for background restore
    int ptrWidth, ptrHeight;
    int ptrXOff, ptrYOff;
    const uint8_t* ptrData;
    uint8_t ptrSavedData[4096]; // Saved background under cursor
    
    // Palette
    uint8_t palette[PALETTE_ENTRIES * 3]; // RGB triples
    
    // Frame counter
    uint32_t flipCount;
    
    // Bitmap table
    Bitmap* bitmapTable[MAX_BITMAPS];
    int bitmapCount;
    
    // Blit nesting counter
    int blitDepth;
    
    // Dynamic palette state
    uint8_t dPalMap[DPAL_MAP_SIZE];       // DATA_007770 direct hit (N+1 = VGA N)
    uint8_t dPalSecondary[DPAL_MAP_SIZE]; // DATA_009139 occupancy → cluster id
    uint8_t dPalReverse[DPAL_REVERSE_SIZE]; // DATA_009140[cluster] → VGA index
    uint8_t dPalRGB[128 * 3];             // DATA_009144 working RGB — 128 slots;
                                          // ASM indexes by (VGA-0x80) ∈ 0..127
    uint8_t dPalChanged[DPAL_ENTRIES];    // DATA_009142 changed relative slots
    int dPalChangedCount;
    int dPalAllocCount;
    int dPalBaseIndex;
};

// Global video state
extern VideoState* g_videoState;

// Z-buffer functions (Phase 40B)
void ZBufferInit();
void ZBufferCleanup();
void ZBufferClear(int16_t depth);
void ZBufferEnable(bool enable);
bool ZBufferTest(int x, int y, int16_t z);
void ZBufferWrite(int x, int y, int16_t z);

} // namespace jjffe

#endif // JJFFE_VIDEO_H
