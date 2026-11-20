#include "jjffe/video.h"
#include "jjffe/data_globals.h"
#include "jjffe/dpal_build.h"
#include "jjffe/dpal_luma.h"
#include "jjffe/platform.h"
#include "jjffe/data.h"
#include "data_blob.h"
#include "jjffe/bitmap_table.h"
#include "dpal_static_map_data.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#if defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace jjffe {

// Global video state (pointer to heap-allocated state)
VideoState* g_videoState = nullptr;

// Get pointer to the back buffer
uint8_t* GetBackBuffer()
{
    return g_videoState->backBuffer;
}

// BufferBlit: Copy source to back buffer (opaque)
// FUNC_001626_BlitInternal / BufferBlit
void BufferBlit(const uint8_t* src, int x, int y, int width, int height, int srcJump)
{
    uint8_t* dst = g_videoState->backBuffer + x + y * SCREEN_WIDTH;
    int dstJump = SCREEN_WIDTH - width;
    
    // Optimized path: 4-byte aligned
    if (((uintptr_t)dst & 3) == 0 && ((uintptr_t)src & 3) == 0 && (width & 3) == 0) {
        uint32_t* dst32 = (uint32_t*)dst;
        const uint32_t* src32 = (const uint32_t*)src;
        int width32 = width >> 2;
        
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width32; ++col) {
                dst32[col] = src32[col];
            }
            src32 = (const uint32_t*)((const uint8_t*)src32 + width + srcJump);
            dst32 = (uint32_t*)((uint8_t*)dst32 + width + dstJump);
        }
    } else {
        // Byte-by-byte path
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                dst[col] = src[col];
            }
            src += width + srcJump;
            dst += width + dstJump;
        }
    }
}

// BufferMaskedBlit: Copy source to back buffer with transparency (0 = skip pixel)
// FUNC_001628 / BufferMaskedBlit
void BufferMaskedBlit(const uint8_t* src, int x, int y, int width, int height, int srcJump)
{
    uint8_t* dst = g_videoState->backBuffer + x + y * SCREEN_WIDTH;
    int dstJump = SCREEN_WIDTH - width;
    
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            if (src[col] != 0) {
                dst[col] = src[col];
            }
        }
        src += width + srcJump;
        dst += width + dstJump;
    }
}

// BufferReverseBlit: Copy back buffer to destination (save background)
// FUNC_001629 / BufferReverseBlit
void BufferReverseBlit(uint8_t* dst, int x, int y, int width, int height, int dstJump)
{
    const uint8_t* src = g_videoState->backBuffer + x + y * SCREEN_WIDTH;
    int srcJump = SCREEN_WIDTH - width;
    
    // Optimized path: 4-byte aligned
    if (((uintptr_t)dst & 3) == 0 && ((uintptr_t)src & 3) == 0 && (width & 3) == 0) {
        uint32_t* dst32 = (uint32_t*)dst;
        const uint32_t* src32 = (const uint32_t*)src;
        int width32 = width >> 2;
        
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width32; ++col) {
                dst32[col] = src32[col];
            }
            src32 = (const uint32_t*)((const uint8_t*)src32 + width + srcJump);
            dst32 = (uint32_t*)((uint8_t*)dst32 + width + dstJump);
        }
    } else {
        // Byte-by-byte path
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                dst[col] = src[col];
            }
            src += width + srcJump;
            dst += width + dstJump;
        }
    }
}

// FlipScreen: Copy back buffer to display and handle cursor
// FUNC_001637_FlipScreen — the ASM tail calls FUNC_001331_DPalSet every flip
// (ffe.asm 98006/98022): installs the changed entries + XORs the active
// palette half (DATA_007634 ^= 0x40), so the fill alternates 0xE8/0xA8.
// ASM order (ffe.asm:98022-98023): DPalSet THEN VideoBlitBuffer. The port used
// to blit first, so SDL's index→RGB present (g_palette) showed frame N with
// palette N-1. R1208 restores the ASM order. AB_DPAL_PHASE_FIX=0 reverts to
// the old blit-then-DPalSet path for A/B; unset/other = ASM order.
void FlipScreen()
{
    g_videoState->flipCount++;

    // R1208: dump live VGA slots at FlipScreen *entry* (pre-DPalSet), matching
    // the ASM pPalette probe. Env: AB_DPAL_PHASE_PAL=1. Keyed on flipCount
    // (platform layer — no GameLoop dependency). Wide flip window covers the
    // targeting residual (mission ~480 ≈ flip ~661 measured R1208).
    static int s_palDumpEnv = -1;
    if (s_palDumpEnv < 0) {
        const char* palDump = getenv("AB_DPAL_PHASE_PAL");
        s_palDumpEnv = (palDump && palDump[0] == '1') ? 1 : 0;
    }
    if (s_palDumpEnv && g_videoState) {
        const uint32_t flip = g_videoState->flipCount;
        if (flip >= 620 && flip <= 700) {
            fprintf(stderr,
                    "AB_DPAL_PHASE_PAL flip=%u base_at_entry=%#x slots140-222:",
                    flip, g_videoState->dPalBaseIndex);
            for (int i = 140; i <= 222; ++i) {
                uint8_t r = 0, g = 0, b = 0;
                PlatformGetPaletteEntry(i, &r, &g, &b);
                fprintf(stderr, " %d=%02x%02x%02x", i, r, g, b);
            }
            fprintf(stderr, "\n");
        }
    }

    // Default = ASM order. AB_DPAL_PHASE_FIX=0 keeps the pre-R1208 blit-first
    // path for measurement; any other value (including unset) uses DPalSet first.
    static int s_oldOrder = -1;
    if (s_oldOrder < 0) {
        const char* phaseFix = getenv("AB_DPAL_PHASE_FIX");
        s_oldOrder = (phaseFix && phaseFix[0] == '0') ? 1 : 0;
    }
    // R1216: when AB_DPAL_PANEL_INSTALL restored DPalSet inside FUNC_001637,
    // that call already installed+XORed — skip the duplicate outer DPalSet.
    const bool inlineDid = DPalConsumeInlineSet();
    if (s_oldOrder) {
        PlatformVideoBlit();
        if (!inlineDid) DPalSet();
    } else {
        if (!inlineDid) DPalSet();
        PlatformVideoBlit();
    }
}

// Platform video blit stub (overridden by SDL / D3D backends)
__attribute__((weak)) void PlatformVideoBlit()
{
}

__attribute__((weak)) bool PlatformIsGpuDraw()
{
    return false;
}

__attribute__((weak)) bool PlatformIsD3D()
{
    return PlatformIsGpuDraw();
}

__attribute__((weak)) uint32_t PlatformGetTicks()
{
    return 0;
}

__attribute__((weak)) void PlatformDelay(uint32_t /*ms*/)
{
}

__attribute__((weak)) void PlatformSetControlStripHeight(int /*height*/)
{
}

__attribute__((weak)) int PlatformGetControlStripHeight()
{
    return 0;
}

__attribute__((weak)) void PlatformSetControlStripPainter(PlatformStripPainterFn /*fn*/)
{
}

__attribute__((weak)) void PlatformGetWindowMousePos(int* x, int* y)
{
    if (x) *x = 0;
    if (y) *y = 0;
}

// Video initialization
// FUNC_001638_VideoInit
// Load original game palette from data blob (DATA_007768 + 0x18)
void LoadGamePalette()
{
    const uint8_t* blob = data::GetDataBlob();
    if (blob && data::GetDataBlobSize() > data::OFFSET_DATA_007769 + 384) {
        const uint8_t* paletteData = blob + data::OFFSET_DATA_007769;  // DATA_007768 + 0x18
        for (int i = 0; i < 128; i++) {
            uint8_t r6 = paletteData[i * 3 + 0];
            uint8_t g6 = paletteData[i * 3 + 1];
            uint8_t b6 = paletteData[i * 3 + 2];
            // Scale from 6-bit VGA (0-63) to 8-bit — match ASM sdlvideo
            // `pPalTable[v] << 2` (not `(v<<2)|(v>>4)`).
            SetPaletteEntry(i, static_cast<uint8_t>(r6 << 2),
                            static_cast<uint8_t>(g6 << 2),
                            static_cast<uint8_t>(b6 << 2));
            // Duplicate to upper half (indices 128-255)
            SetPaletteEntry(i + 128, static_cast<uint8_t>(r6 << 2),
                            static_cast<uint8_t>(g6 << 2),
                            static_cast<uint8_t>(b6 << 2));
            // Seed ASM DATA_009144 (dPalRGB) once with **6-bit** DAC values —
            // FUNC_001330 persists this store and Pass1 indexes via >>2.
            // R1210: AB_DPAL_REV_FIX=1 skips this seed — ASM DATA_009144 is
            // BSS-zero until Pass2 centroid writes (first high-bank build then
            // Pass1-misses all 64 → free=0..63 LIFO). Default keeps historical
            // seed (star-map / water digs depended on it).
            // R1211: AB_DPAL_BOOT_LAW=1 also skips the seed (BSS-zero like ASM).
            static int s_revFix = -1;
            if (s_revFix < 0) {
                const char* e = getenv("AB_DPAL_REV_FIX");
                const char* b = getenv("AB_DPAL_BOOT_LAW");
                s_revFix = ((e && e[0] == '1') || (b && b[0] == '1')) ? 1 : 0;
            }
            if (g_videoState && !s_revFix) {
                g_videoState->dPalRGB[i * 3 + 0] = r6;
                g_videoState->dPalRGB[i * 3 + 1] = g6;
                g_videoState->dPalRGB[i * 3 + 2] = b6;
            }
        }
    } else if (g_videoState) {
        // Fallback: grayscale palette
        for (int i = 0; i < 256; i++) {
            uint8_t v = (uint8_t)i;
            SetPaletteEntry(i, v, v, v);
        }
        static int s_revFixFb = -1;
        if (s_revFixFb < 0) {
            const char* e = getenv("AB_DPAL_REV_FIX");
            const char* b = getenv("AB_DPAL_BOOT_LAW");
            s_revFixFb = ((e && e[0] == '1') || (b && b[0] == '1')) ? 1 : 0;
        }
        if (!s_revFixFb) {
            for (int i = 0; i < 128; i++) {
                uint8_t v = (uint8_t)i;
                g_videoState->dPalRGB[i * 3 + 0] = v;
                g_videoState->dPalRGB[i * 3 + 1] = v;
                g_videoState->dPalRGB[i * 3 + 2] = v;
            }
        }
    }
}

static void BindDpalTableGlobals()
{
    if (g_videoState) {
        DATA_007770 = g_videoState->dPalMap;
        DATA_009139 = g_videoState->dPalSecondary;
        DATA_009140 = g_videoState->dPalReverse;
    } else {
        DATA_007770 = nullptr;
        DATA_009139 = nullptr;
        DATA_009140 = nullptr;
    }
}

void VideoInit()
{
    // Allocate video state on heap to avoid large BSS section
    if (!g_videoState) {
        g_videoState = new VideoState();
    }
    // Free any prior z-buffer before memset orphans the pointer (VideoInit is
    // called twice at startup: main → VideoInit, then GameInit → SysInit → VideoInit).
    if (g_videoState) ZBufferCleanup();

    // Initialize video state
    std::memset(g_videoState, 0, sizeof(VideoState));
    g_videoState->ptrIndex = -1;
    // ASM DATA_007770 = a static 4096-B seed table (126 direct-hit entries,
    // map[0]=1), never written at runtime. Load it once; DPalBuild/DPalClear
    // must NOT clear it (the seeds gate DPal32to16's marks).
    static_assert(sizeof(kDpalStaticMap) == 4096, "static DPal map size");
    std::memcpy(g_videoState->dPalMap, kDpalStaticMap, 4096);
    BindDpalTableGlobals();

    // Initialize Z-buffer
    ZBufferInit();
    
    LoadGamePalette();
    
    // In the original, this calls:
    // - Platform VideoInit()
    // - SetWholePalette()
    // - SetPtrImage(0)
    // - DisablePtr()
    // - DrawBoxToBuf() - clear screen
    // - FlipScreen()
    // R1211: ASM FUNC_001638 ends with FUNC_001637_FlipScreen (ffe.asm:98326),
    // whose DPalSet XORs DATA_007634 0→0x40. Port historically skipped that
    // flip, so the first marking DPalBuild wrote the low bank while ASM's
    // first fill (frame 1) wrote the high bank — permanent dPalRGB history
    // drift. VideoInit runs twice (memset zeroes base each time), so under
    // AB_DPAL_BOOT_LAW=1 re-apply the post-FlipScreen bank phase after each
    // LoadGamePalette rather than calling FlipScreen (avoids double-blit /
    // flipCount side effects). Also pairs with the seed-skip above.
    {
        static int s_bootLaw = -1;
        if (s_bootLaw < 0) {
            const char* e = getenv("AB_DPAL_BOOT_LAW");
            s_bootLaw = (e && e[0] == '1') ? 1 : 0;
        }
        if (s_bootLaw && g_videoState)
            g_videoState->dPalBaseIndex = 0x40;
    }
}

void VideoCleanup()
{
    ZBufferCleanup();
    DATA_007770 = nullptr;
    DATA_009139 = nullptr;
    DATA_009140 = nullptr;
    if (g_videoState) {
        delete g_videoState;
        g_videoState = nullptr;
    }
}

// FUNC_001881_VideoBlitBuffer
void VideoBlitBuffer()
{
    VideoBlit(GetBackBuffer(), 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
}

// FUNC_001882_FlipScreenLow: shows the console band (rows 158..199) only.
void FlipScreenLow()
{
    if (!g_videoState) return;
    g_videoState->flipCount++;

    constexpr int kBandY = 158;
    const uint8_t* band = GetBackBuffer() + kBandY * SCREEN_WIDTH;
    if (g_videoState->ptrEnabled && g_videoState->ptrExclusive &&
        g_videoState->ptrSavedY >= kBandY - g_videoState->ptrHeight) {
        BufferReverseBlit(g_videoState->ptrSavedData,
                         g_videoState->ptrSavedX, g_videoState->ptrSavedY,
                         g_videoState->ptrWidth, g_videoState->ptrHeight, 0);
        BufferMaskedBlit(g_videoState->ptrData,
                        g_videoState->ptrSavedX, g_videoState->ptrSavedY,
                        g_videoState->ptrWidth, g_videoState->ptrHeight, 0);
        VideoBlit(band, 0, kBandY, SCREEN_WIDTH, SCREEN_HEIGHT - kBandY, 0);
        BufferBlit(g_videoState->ptrSavedData,
                  g_videoState->ptrSavedX, g_videoState->ptrSavedY,
                  g_videoState->ptrWidth, g_videoState->ptrHeight, 0);
        return;
    }

    VideoBlit(band, 0, kBandY, SCREEN_WIDTH, SCREEN_HEIGHT - kBandY, 0);
}

// Palette management
// FUNC_001641_SetWholePalette
void SetWholePalette()
{
    // In the original, this reads from a global palette buffer
    // and calls VideoSetPalValue for each of the 128 entries
}

void SetPaletteEntry(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < 0 || index >= 256) return;
    
    if (index < PALETTE_ENTRIES) {
        g_videoState->palette[index * 3 + 0] = r;
        g_videoState->palette[index * 3 + 1] = g;
        g_videoState->palette[index * 3 + 2] = b;
    }
    PlatformSetPaletteEntry(index, r, g, b);
}

// Pointer/cursor system: FUNC_001630/1631/1632/1635/1636 in src/core/core.cpp.

// Bitmap/Blit functions (Phase 16)

// Set bitmap table (legacy - now uses data blob)
void SetBitmapTable(Bitmap** table, int count)
{
    if (count > MAX_BITMAPS) count = MAX_BITMAPS;
    for (int i = 0; i < count; i++) {
        g_videoState->bitmapTable[i] = table[i];
    }
    g_videoState->bitmapCount = count;
}

// FUNC_001620_BmpIndexToPtr
// Returns pointer to bitmap data from the data blob
Bitmap* BmpIndexToPtr(int index)
{
    const uint8_t* blob = data::GetDataBlob();
    if (!blob) {
        // Fallback to legacy table
        if (index < 0 || index >= g_videoState->bitmapCount) {
            return nullptr;
        }
        return g_videoState->bitmapTable[index];
    }
    
    // Use bitmap table 0 (DATA_008240) for indices 0-95
    if (index >= 0 && index < (int)data::BITMAP_TABLE_0_COUNT) {
        uint32_t offset = data::BITMAP_TABLE_0_OFFSETS[index];
        if (offset > 0 && offset < data::GetDataBlobSize()) {
            return (Bitmap*)(blob + offset);
        }
    }
    
    // Use bitmap table 1 (DATA_008241) for indices 96-346
    int idx1 = index - data::BITMAP_TABLE_0_COUNT;
    if (idx1 >= 0 && idx1 < (int)data::BITMAP_TABLE_1_COUNT) {
        uint32_t offset = data::BITMAP_TABLE_1_OFFSETS[idx1];
        if (offset > 0 && offset < data::GetDataBlobSize()) {
            return (Bitmap*)(blob + offset);
        }
    }
    
    return nullptr;
}

// FUNC_001626_BlitInternal
void BlitInternal(uint8_t* dst, const uint8_t* src, int width, int height,
                  int dstJump, int srcJump, const Bitmap* bmp)
{
    bool masked = (bmp && (bmp->flags & 1));
    
    if (!masked) {
        // Opaque blit - use memcpy for speed
        for (int y = 0; y < height; y++) {
            memcpy(dst, src, width);
            dst += dstJump + width;
            src += srcJump + width;
        }
    } else {
        // Masked blit - skip transparent pixels (value 0)
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (*src != 0) {
                    *dst = *src;
                }
                dst++;
                src++;
            }
            dst += dstJump;
            src += srcJump;
        }
    }
}

// FUNC_001625_BlitClipInternal
void BlitClipInternal(uint8_t* dst, int x, int y, const uint8_t* src,
                      int clipLeft, int clipTop, int clipRight, int clipBottom)
{
    const Bitmap* bmp = (const Bitmap*)(src - 8);  // Bitmap header is before data
    
    int bmpHeight = bmp->height;
    int bmpWidth = bmp->width;
    int srcPitch = bmpWidth;
    
    // Default clip bounds to screen
    if (clipRight == -1) clipRight = SCREEN_WIDTH;
    if (clipBottom == -1) clipBottom = SCREEN_HEIGHT;
    
    // Calculate visible region
    int startY = y + bmp->yOff;
    int visibleHeight = bmpHeight;
    
    // Clip top
    if (startY < clipTop) {
        visibleHeight -= (clipTop - startY);
        if (visibleHeight < 1) return;
        src += srcPitch * (clipTop - startY);
        startY = clipTop;
    }
    
    // Clip bottom
    if (clipBottom < startY + visibleHeight) {
        visibleHeight = clipBottom - startY;
    }
    
    if (visibleHeight <= 0) return;
    
    int startX = x + bmp->xOff;
    int visibleWidth = bmpWidth;
    int srcOffset = 0;
    
    // Clip left
    if (startX < clipLeft) {
        visibleWidth -= (clipLeft - startX);
        if (visibleWidth < 1) return;
        srcOffset = clipLeft - startX;
        startX = clipLeft;
    }
    
    // Clip right
    if (startX + visibleWidth > clipRight) {
        visibleWidth = clipRight - startX;
    }
    
    if (visibleWidth <= 0) return;
    
    // Calculate destination pointer
    uint8_t* dstPtr = g_videoState->backBuffer + startY * SCREEN_WIDTH + startX;
    const uint8_t* srcPtr = src + srcOffset;
    
    // Calculate jumps
    int dstJump = SCREEN_WIDTH - visibleWidth;
    int srcJump = srcPitch - visibleWidth;
    
    BlitInternal(dstPtr, srcPtr, visibleWidth, visibleHeight, dstJump, srcJump, bmp);
}

// FUNC_001623_BlitIndex
void* BlitIndex(uint8_t* dst, int bmpIndex, int x, int y)
{
    g_videoState->blitDepth++;
    
    Bitmap* bmp = BmpIndexToPtr(bmpIndex);
    if (dst && bmp) {
        BlitClipInternal(dst, x, y, bmp->data, 0, 0, -1, -1);
    }
    
    g_videoState->blitDepth--;
    return bmp;
}

// FUNC_001624_BlitClipIndex
void* BlitClipIndex(uint8_t* dst, int bmpIndex, int x, int y,
                    int clipLeft, int clipTop, int clipRight, int clipBottom)
{
    g_videoState->blitDepth++;
    
    Bitmap* bmp = BmpIndexToPtr(bmpIndex);
    if (dst && bmp) {
        BlitClipInternal(dst, x, y, bmp->data, clipLeft, clipTop, clipRight, clipBottom);
    }
    
    g_videoState->blitDepth--;
    return bmp;
}

// FUNC_001621_BlitIndexToBuf
void BlitIndexToBuf(int bmpIndex, int x, int y)
{
    BlitIndex(g_videoState->backBuffer, bmpIndex, x, y);
}

// FUNC_001622_BlitClipIndexToBuf
void BlitClipIndexToBuf(int bmpIndex, int x, int y,
                        int clipLeft, int clipTop, int clipRight, int clipBottom)
{
    BlitClipIndex(g_videoState->backBuffer, bmpIndex, x, y, clipLeft, clipTop, clipRight, clipBottom);
}

// FUNC_001617_BlitVidToBuf
void BlitVidToBuf()
{
    // Restore background (reverse blit)
    BufferReverseBlit(g_videoState->backBuffer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    
    // Restore pointer if in exclusive mode
    if (g_videoState->ptrExclusive) {
        // Would restore saved pointer background
        // BlitClipWrapper(&ptrSavedData, ptrSavedX, ptrSavedY, ptrWidth, ptrHeight, 0, BufferBlit);
    }
}

// Dynamic palette functions (Phase 18A)

// FUNC_001328_DPalClear — ASM memsets ONLY DATA_009139[0x1000]. The reverse
// table window (DATA_009140/009141) PERSISTS across clears: DPalBuild only
// updates slots whose byte is still 0.
void DPalClear()
{
    memset(g_videoState->dPalSecondary, 0, DPAL_MAP_SIZE);
}

// FUNC_001326_DPal32to16 — ASM packs the pushed dword's LOW byte at <<8,
// the mid byte at <<4, the high byte at the low nibble (byte0-major RGB444).
// The low byte is the record colour's R component (the same axis the static
// seed map keys on — see DPalBuildMedianCut's seed note).
void DPal32to16(uint32_t rgb)
{
    // ASM (ffe.asm 4e384): al = byte0 -> <<8, dl = byte1 -> <<4, then byte2,
    // with no nibble shift: the render path only feeds 4-bit channels, so the
    // sum stays inside the 4096-byte table; larger bytes would overrun it in
    // the ASM, hence the 12-bit clamp (mirrors FUNC_001326 in core.cpp).
    uint8_t b0 = rgb & 0xFF;
    uint8_t b1 = (rgb >> 8) & 0xFF;
    uint8_t b2 = (rgb >> 16) & 0xFF;

    uint16_t index =
        (uint16_t)(((uint16_t)b0 << 8) + ((uint16_t)b1 << 4) + (uint16_t)b2) & 0xFFF;

    // R1212: same first-hit mark log as FUNC_001326 (AB_DPAL_OCC_MARK).
    {
        static int s_omEnv = -1;
        static int s_omCap = 200;
        static int s_omLogs = 0;
        if (s_omEnv < 0) {
            const char* e = getenv("AB_DPAL_OCC_MARK");
            s_omEnv = (e && e[0] == '1') ? 1 : 0;
            if (const char* c = getenv("AB_DPAL_OCC_MARK_N")) {
                if (c[0]) {
                    char* end = nullptr;
                    long v = strtol(c, &end, 0);
                    if (end != c && v > 0 && v < 10000)
                        s_omCap = static_cast<int>(v);
                }
            }
        }
        if (s_omEnv && s_omLogs < s_omCap && g_videoState &&
            g_videoState->dPalMap[index] == 0 &&
            g_videoState->dPalSecondary[index] == 0) {
            fprintf(stderr,
                    "AB_DPAL_OCC_MARK idx=%u r=%u g=%u b=%u in=%06x via=DPal32to16\n",
                    (unsigned)index, (unsigned)((index >> 8) & 0xf),
                    (unsigned)((index >> 4) & 0xf), (unsigned)(index & 0xf),
                    (unsigned)(rgb & 0xffffffu));
            ++s_omLogs;
        }
    }

    if (g_videoState->dPalMap[index] == 0) {
        g_videoState->dPalSecondary[index] = 1;
    }
}

static uint32_t* s_dpalLookupTap = nullptr;

void SetDpalLookupTap(uint32_t* hist4096)
{
    s_dpalLookupTap = hist4096;
}

void DPalLookupTapNote(uint16_t index)
{
    if (s_dpalLookupTap)
        ++s_dpalLookupTap[index & 0xFFF];
}

// FUNC_001327_DPal16toPal — ASM returns the map hit raw (val−1), no promote.
uint8_t DPal16toPal(uint16_t index)
{
    // R594 candidate law (gated AB_DPAL_OCT): ASM's FUNC_001330 quantizer maps
    // EVERY RGB444 word to some palette entry (octree nearest-cell), so the
    // live map never answers 0 for a chromatic shade word. The port's map misses
    // lit chromatics (#22 greens (2,6,6)-style, #65 reds, #23 lens) -> zero
    // palettes -> unpainted coverage on three hulls. Fallback: nearest mapped
    // entry by squared RGB distance over the 4096-entry map's hits.

    // DPAL_MAP_SIZE = 4096 (12-bit RGB444). Callers may pass an 8-bit-material
    // shade unmasked, producing a 16-bit index that overruns dPalMap. Clamp to
    // 12 bits to match the ASM's effective RGB444 lookup (DATA_007770 is 4096 B).
    index &= 0xFFF;
    DPalLookupTapNote(index);
    uint8_t mapping = g_videoState->dPalMap[index];

    if (mapping != 0) {
        return static_cast<uint8_t>(mapping - 1);
    }

    if (::getenv("AB_DPAL_OCT") && ::getenv("AB_DPAL_OCT")[0] == '1') {
        // Nearest-MAPPED-entry search over the live RGB444 map (R594).
        const int r = (index >> 8) & 0xF, g = (index >> 4) & 0xF, b = index & 0xF;
        int best = 0x7fffffff; uint8_t bestPal = 0;
        for (int ci = 0; ci < DPAL_MAP_SIZE; ci++) {
            const uint8_t m = g_videoState->dPalMap[ci];
            if (m == 0) continue;
            const int dr = r - ((ci >> 8) & 0xF), dg = g - ((ci >> 4) & 0xF), db = b - (ci & 0xF);
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < best) { best = dist; bestPal = static_cast<uint8_t>(m - 1); }
        }
        if (bestPal != 0) return bestPal;
    }

    // R632 candidate law (gated AB_DPAL_RAW): the function's own contract line
    // above says ASM "returns the map hit raw (val-1), no promote" — i.e. a MISS
    // yields 0 (nothing painted). The port instead fabricates a colour through
    // dPalReverse[dPalSecondary[index]], which makes faces visible that ASM
    // leaves empty (measured: #21 band y71 x68.. texel3 -> pal 1 while ASM's
    // table is 0). Test the faithful behaviour: return 0 on miss.
    if (::getenv("AB_DPAL_RAW") && ::getenv("AB_DPAL_RAW")[0] == '1')
        return 0;
    return g_videoState->dPalReverse[g_videoState->dPalSecondary[index]];
}

// Legacy oracle + hole-fill + GREY_SNAP luminance path (pre-FUNC_001330 port).
// Opt in with AB_DPAL_LEGACY=1 for A/B flips against dpal_asm.bin.
static void DPalBuildLegacyOracle(const uint8_t* palette, int baseIndex)
{
    g_videoState->dPalBaseIndex = baseIndex;
    g_videoState->dPalChangedCount = 0;
    g_videoState->dPalAllocCount = DPAL_ENTRIES;

    memset(g_videoState->dPalMap, 0, DPAL_MAP_SIZE);
    memset(g_videoState->dPalSecondary, 0, DPAL_MAP_SIZE);
    memset(g_videoState->dPalReverse, 0, sizeof(g_videoState->dPalReverse));

    // ASM reverse bank: VGA byte = slot + baseIndex - 0x80 (→ 0x80..0xBF when
    // baseIndex==0). DPal16toPal's miss path returns reverse[secondary].
    //
    // Only FUNC_001330 DPalBuild populates this bank, so a scenario that never
    // builds leaves it ALL ZERO — and then every unmapped cell resolves to
    // palette 0 (black) exactly like ffeasm. Dumped live for ship 15:
    // reverse[0..6] = 0, marked cells 0x655/0x766 → both 0, and the oracle blob
    // dpal_asm.bin's reverse half is 64 zero bytes too. Pre-filling the bank
    // here emulates a *built* reverse and made every miss return 0x80+ instead
    // of 0 (the last index-only diff on the golden ship demos), so keep it
    // opt-in for A/B runs that compare against tables dumped from a scenario
    // that does build.
    const char* bank = getenv("AB_DPAL_REVERSE_BANK");
    if (bank && bank[0] == '1') {
        for (int i = 1; i < DPAL_ENTRIES; i++) {
            g_videoState->dPalReverse[i] =
                static_cast<uint8_t>(i + baseIndex - 0x80);
        }
        g_videoState->dPalReverse[0] = 0;
    }

    bool loaded = false;
    static const char* kDpalPaths[] = {
        "data_blob/dpal_asm.bin",
        "../data_blob/dpal_asm.bin",
        "dpal_asm.bin",
        nullptr
    };
    FILE* f = nullptr;
    for (int pi = 0; kDpalPaths[pi]; pi++) {
        f = fopen(kDpalPaths[pi], "rb");
        if (f) break;
    }
#if defined(__linux__)
    char exeDpal[PATH_MAX + 64];
    if (!f) {
        char exe[PATH_MAX];
        const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            char* slash = strrchr(exe, '/');
            if (slash) {
                *slash = '\0';
                snprintf(exeDpal, sizeof(exeDpal), "%s/data_blob/dpal_asm.bin", exe);
                f = fopen(exeDpal, "rb");
                if (!f) {
                    snprintf(exeDpal, sizeof(exeDpal), "%s/dpal_asm.bin", exe);
                    f = fopen(exeDpal, "rb");
                }
            }
        }
    }
#endif
    if (f) {
        uint8_t oracle[4096 + 64];
        size_t nread = fread(oracle, 1, sizeof(oracle), f);
        fclose(f);
        if (nread >= 4096) {
            memcpy(g_videoState->dPalMap, oracle, 4096);
            if (nread >= 4096 + 64) {
                memcpy(g_videoState->dPalReverse, oracle + 4096, 64);
                // Older/partial captures write 64 zero bytes: that is real
                // state (see above), so only substitute the bank when the A/B
                // run explicitly asks for it.
                bool allZero = true;
                for (int i = 0; i < DPAL_ENTRIES; i++) {
                    if (g_videoState->dPalReverse[i] != 0) {
                        allZero = false;
                        break;
                    }
                }
                if (allZero && bank && bank[0] == '1') {
                    // Match ASM bank encoding: reverse[i] = i + base - 0x80
                    // (baseIndex 0 → VGA 0x80..0xBF). Identity 0..63 made
                    // DPal16toPal miss-path return low indices while
                    // SetPaletteEntry installed RGB at slot+0x80 — Earth
                    // 001810 flats then painted near-black (pal 6 vs 168).
                    for (int i = 0; i < DPAL_ENTRIES; i++)
                        g_videoState->dPalReverse[i] =
                            static_cast<uint8_t>(i + baseIndex - 0x80);
                    g_videoState->dPalReverse[0] = 0;
                }
            }
            loaded = true;
        }
    }

    // AB_DPAL_NO_FILL=1 stops here: the loaded oracle map (126 static entries)
    // and the loaded reverse are used as-is, so an unmapped cell resolves to
    // reverse[secondary] = 0 = palette 0, which is what ffeasm does in any
    // scenario that never runs FUNC_001330. Dumped live for the ship demo
    // (ship 15): 126 mapped entries, secondary marked 0x655/0x766, reverse all
    // zero, both cells → palette 0. The A/B capture tools run that same
    // scenario, so they set this flag; the fill below emulates a *built*
    // reverse and turned every unmapped hull cell into a shaded slot instead
    // (128-index noise + hull pixels the ASM leaves black).
    if (const char* noFill = getenv("AB_DPAL_NO_FILL")) {
        if (noFill[0] == '1') {
            return;
        }
    }

    // Fill unmapped dPalMap entries using median-cut octree
    // (port of FUNC_001330+001329 DPalBuild octree quantizer).
    // The oracle has 126 of 4096 entries mapped. The ASM octree maps ALL
    // 4096 entries to one of 64 VGA palette slots via box partitioning.
    // Required for A/B LEGACY/STRICT: without this fill, Skeet #45 Contour/
    // tex paths paint ~72k only_cpp (was grade A / pd=20). Restored after
    // 8f10155 dropped it when splitting MedianCut vs LegacyOracle.
    {
        // Step 1: Build 16x16x16 histogram from existing dPalMap entries
        uint8_t hist[4096];
        memset(hist, 0, sizeof(hist));
        int nSeeds = 0;
        for (int i = 0; i < 4096; i++) {
            if (g_videoState->dPalMap[i] != 0) {
                hist[i] = 1;
                nSeeds++;
            }
        }
        if (nSeeds > 64) {
            struct Box {
                int rMin, rMax, gMin, gMax, bMin, bMax;
                int maxSpan, longestAxis;
                int count;
                int vgaSlot; // assigned VGA palette entry (-1 = unassigned)
            };
            Box boxes[64];
            int nBoxes = 0;

            // Compute tight bounding box for a region [r0,r1)×[g0,g1)×[b0,b1)
            auto computeBoxRegion = [&](Box& box, int r0, int r1, int g0, int g1, int b0, int b1) {
                box.count = 0;
                box.rMin = 16; box.rMax = 0;
                box.gMin = 16; box.gMax = 0;
                box.bMin = 16; box.bMax = 0;
                for (int ri = r0; ri < r1; ri++) {
                    for (int gi = g0; gi < g1; gi++) {
                        for (int bi = b0; bi < b1; bi++) {
                            int idx = (ri << 8) | (gi << 4) | bi;
                            if (g_videoState->dPalMap[idx] != 0) {
                                box.count++;
                                if (ri < box.rMin) box.rMin = ri;
                                if (ri + 1 > box.rMax) box.rMax = ri + 1;
                                if (gi < box.gMin) box.gMin = gi;
                                if (gi + 1 > box.gMax) box.gMax = gi + 1;
                                if (bi < box.bMin) box.bMin = bi;
                                if (bi + 1 > box.bMax) box.bMax = bi + 1;
                            }
                        }
                    }
                }
                int spanR = box.rMax - box.rMin;
                int spanG = box.gMax - box.gMin;
                int spanB = box.bMax - box.bMin;
                box.maxSpan = spanR;
                box.longestAxis = 0;
                if (spanG > box.maxSpan) { box.maxSpan = spanG; box.longestAxis = 1; }
                if (spanB > box.maxSpan) { box.maxSpan = spanB; box.longestAxis = 2; }
            };

            // Initialize with one box covering all mapped entries
            boxes[0].vgaSlot = -1;
            computeBoxRegion(boxes[0], 0, 16, 0, 16, 0, 16);
            nBoxes = 1;

            // Median-cut: split boxes until we have 64 or can't split further
            while (nBoxes < 64) {
                // Find box with largest span that has count > 1
                int bestIdx = -1;
                int bestSpan = -1;
                for (int i = 0; i < nBoxes; i++) {
                    if (boxes[i].count > 1 && boxes[i].maxSpan > bestSpan) {
                        bestSpan = boxes[i].maxSpan;
                        bestIdx = i;
                    }
                }
                if (bestIdx < 0) break;

                Box& p = boxes[bestIdx];
                int axis = p.longestAxis;

                // Split at midpoint of the range (NOT midpoint of entries!)
                if (axis == 0) {
                    int mid = (p.rMin + p.rMax) / 2;
                    computeBoxRegion(boxes[bestIdx], p.rMin, mid, p.gMin, p.gMax, p.bMin, p.bMax);
                    if (boxes[bestIdx].count > 0) {
                        boxes[bestIdx].vgaSlot = -1;
                    }
                    if (nBoxes < 64) {
                        computeBoxRegion(boxes[nBoxes], mid, p.rMax, p.gMin, p.gMax, p.bMin, p.bMax);
                        if (boxes[nBoxes].count > 0) {
                            boxes[nBoxes].vgaSlot = -1;
                            nBoxes++;
                        }
                    }
                } else if (axis == 1) {
                    int mid = (p.gMin + p.gMax) / 2;
                    computeBoxRegion(boxes[bestIdx], p.rMin, p.rMax, p.gMin, mid, p.bMin, p.bMax);
                    if (boxes[bestIdx].count > 0) {
                        boxes[bestIdx].vgaSlot = -1;
                    }
                    if (nBoxes < 64) {
                        computeBoxRegion(boxes[nBoxes], p.rMin, p.rMax, mid, p.gMax, p.bMin, p.bMax);
                        if (boxes[nBoxes].count > 0) {
                            boxes[nBoxes].vgaSlot = -1;
                            nBoxes++;
                        }
                    }
                } else {
                    int mid = (p.bMin + p.bMax) / 2;
                    computeBoxRegion(boxes[bestIdx], p.rMin, p.rMax, p.gMin, p.gMax, p.bMin, mid);
                    if (boxes[bestIdx].count > 0) {
                        boxes[bestIdx].vgaSlot = -1;
                    }
                    if (nBoxes < 64) {
                        computeBoxRegion(boxes[nBoxes], p.rMin, p.rMax, p.gMin, p.gMax, mid, p.bMax);
                        if (boxes[nBoxes].count > 0) {
                            boxes[nBoxes].vgaSlot = -1;
                            nBoxes++;
                        }
                    }
                }
            }

            // Step 2: Assign VGA palette entries to boxes.
            // Each VGA palette entry (0-63) maps to an RGB444 value.
            // Find which box that RGB444 falls in, assign the box.
            bool boxAssigned[64] = {};
            for (int p = 0; p < 64; p++) {
                int r4 = palette[p * 3 + 0] >> 4;
                int g4 = palette[p * 3 + 1] >> 4;
                int b4 = palette[p * 3 + 2] >> 4;
                for (int bi = 0; bi < nBoxes; bi++) {
                    if (r4 >= boxes[bi].rMin && r4 < boxes[bi].rMax &&
                        g4 >= boxes[bi].gMin && g4 < boxes[bi].gMax &&
                        b4 >= boxes[bi].bMin && b4 < boxes[bi].bMax) {
                        if (!boxAssigned[bi]) {
                            boxes[bi].vgaSlot = p;
                            boxAssigned[bi] = true;
                        }
                        break;
                    }
                }
            }
            // Assign remaining boxes to nearest palette entry
            for (int bi = 0; bi < nBoxes; bi++) {
                if (boxAssigned[bi]) continue;
                int cr = (boxes[bi].rMin + boxes[bi].rMax) / 2;
                int cg = (boxes[bi].gMin + boxes[bi].gMax) / 2;
                int cb = (boxes[bi].bMin + boxes[bi].bMax) / 2;
                int bestP = 0, bestDist = 0x7fffffff;
                for (int p = 0; p < 64; p++) {
                    int dr = cr - (int)(palette[p * 3 + 0] >> 4);
                    int dg = cg - (int)(palette[p * 3 + 1] >> 4);
                    int db = cb - (int)(palette[p * 3 + 2] >> 4);
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist) { bestDist = dist; bestP = p; }
                }
                boxes[bi].vgaSlot = bestP;
                boxAssigned[bi] = true;
            }

            // Step 3: Fill dPalMap entries within each box's range.
            // Store (absolute VGA + 1) so DPal16toPal returns the same byte
            // SetPaletteEntry installs at (slot + baseIndex - 0x80).
            for (int bi = 0; bi < nBoxes; bi++) {
                if (boxes[bi].vgaSlot < 0) continue;
                const uint8_t vga = static_cast<uint8_t>(
                    boxes[bi].vgaSlot + baseIndex - 0x80);
                uint8_t val = static_cast<uint8_t>(vga + 1);
                for (int r = boxes[bi].rMin; r < boxes[bi].rMax; r++) {
                    for (int g = boxes[bi].gMin; g < boxes[bi].gMax; g++) {
                        for (int b = boxes[bi].bMin; b < boxes[bi].bMax; b++) {
                            int idx = (r << 8) | (g << 4) | b;
                            if (g_videoState->dPalMap[idx] == 0) {
                                g_videoState->dPalMap[idx] = val;
                            }
                        }
                    }
                }
            }

            // Step 4: Fill entries outside all boxes with the nearest box
            for (int idx = 0; idx < DPAL_MAP_SIZE; idx++) {
                if (g_videoState->dPalMap[idx] != 0) continue;
                int r4 = (idx >> 8) & 0xF;
                int g4 = (idx >> 4) & 0xF;
                int b4 = idx & 0xF;
                int bestBi = -1, bestDist = 0x7fffffff;
                for (int bi = 0; bi < nBoxes; bi++) {
                    if (r4 >= boxes[bi].rMin && r4 < boxes[bi].rMax &&
                        g4 >= boxes[bi].gMin && g4 < boxes[bi].gMax &&
                        b4 >= boxes[bi].bMin && b4 < boxes[bi].bMax) {
                        bestBi = bi; break;
                    }
                    int cr = (boxes[bi].rMin + boxes[bi].rMax) / 2;
                    int cg = (boxes[bi].gMin + boxes[bi].gMax) / 2;
                    int cb = (boxes[bi].bMin + boxes[bi].bMax) / 2;
                    int dr = r4 - cr, dg = g4 - cg, db = b4 - cb;
                    int dist = dr * dr + dg * dg + db * db;
                    if (dist < bestDist) { bestDist = dist; bestBi = bi; }
                }
                if (bestBi >= 0 && boxes[bestBi].vgaSlot >= 0) {
                    const uint8_t vga = static_cast<uint8_t>(
                        boxes[bestBi].vgaSlot + baseIndex - 0x80);
                    g_videoState->dPalMap[idx] = static_cast<uint8_t>(vga + 1);
                }
            }
        }
    }

    if (!loaded) {
        fprintf(stderr, "DPalBuild: oracle not found; using 8-bit nearest-color fallback\n");
        for (int idx = 0; idx < DPAL_MAP_SIZE; idx++) {
            int r4 = (idx >> 8) & 0xF;
            int g4 = (idx >> 4) & 0xF;
            int b4 = idx & 0xF;
            int r6 = r4 << 2;
            int g6 = g4 << 2;
            int b6 = b4 << 2;
            int r8 = (r6 << 2) | (r6 >> 4);
            int g8 = (g6 << 2) | (g6 >> 4);
            int b8 = (b6 << 2) | (b6 >> 4);

            int bestIdx = -1;
            int bestDist = 0x7fffffff;
            for (int p = 0; p < 64; p++) {
                int pr = palette[p * 3 + 0];
                int pg = palette[p * 3 + 1];
                int pb = palette[p * 3 + 2];
                int dr = r8 - pr, dg = g8 - pg, db = b8 - pb;
                int dist = dr * dr + dg * dg + db * db;
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = p;
                }
            }
            const uint8_t vga =
                static_cast<uint8_t>(bestIdx + baseIndex - 0x80);
            g_videoState->dPalMap[idx] = static_cast<uint8_t>(vga + 1);
        }
    }

    jjffe_dpal_apply_luminance_fallback(g_videoState->dPalMap);

    g_videoState->dPalChangedCount = 0;
    for (int i = 0; i < DPAL_ENTRIES; i++) {
        uint8_t r = palette[i * 3 + 0];
        uint8_t g = palette[i * 3 + 1];
        uint8_t b = palette[i * 3 + 2];

        if (g_videoState->dPalRGB[i * 3 + 0] != r ||
            g_videoState->dPalRGB[i * 3 + 1] != g ||
            g_videoState->dPalRGB[i * 3 + 2] != b) {
            g_videoState->dPalChanged[g_videoState->dPalChangedCount++] = (uint8_t)i;
            g_videoState->dPalRGB[i * 3 + 0] = r;
            g_videoState->dPalRGB[i * 3 + 1] = g;
            g_videoState->dPalRGB[i * 3 + 2] = b;
        }
    }
}

// FUNC_001330_DPalBuild — default: ASM median-cut (dpal_build.cpp).
// AB_DPAL_LEGACY=1 keeps the oracle blob + luminance fallback for A/B.
// AB_DPAL_STRICT=1 loads oracle blob without luma fill (matches ffeasm --dump-ab).
void DPalBuild(const uint8_t* palette, int baseIndex)
{
    const char* strict = getenv("AB_DPAL_STRICT");
    if (strict && strict[0] == '1') {
        DPalBuildLegacyOracle(palette, baseIndex);
        return;
    }
    const char* legacy = getenv("AB_DPAL_LEGACY");
    if (legacy && legacy[0] == '1') {
        DPalBuildLegacyOracle(palette, baseIndex);
        return;
    }
    DPalBuildMedianCut(palette, baseIndex);
}

// R1215: armed by MissionRenderFrame on rs809>=0 fall-through frames when
// AB_DPAL_SHIFT_FIX=1. Consumed (and cleared) by DPalSet on the next call.
static bool s_dpalPanelIdleSkip = false;
void DPalNotifyPanelIdleSkip(bool skip) { s_dpalPanelIdleSkip = skip; }

// R1216: set when FUNC_001637's PANEL_INSTALL path actually ran install+XOR.
static bool s_dpalInlineDidSet = false;
void DPalMarkInlineSet(bool ran) { s_dpalInlineDidSet = ran; }
bool DPalConsumeInlineSet()
{
    const bool v = s_dpalInlineDidSet;
    s_dpalInlineDidSet = false;
    return v;
}

// FUNC_001331_DPalSet — returns true when install+XOR ran.
bool DPalSet()
{
    // R1215/R1216: AB_DPAL_SHIFT_FIX=1 — ASM JUMP_003849 fall-through (rs809>=0)
    // never calls FUNC_001637 / DPalSet (ffe.asm:68802-68806); only occasional
    // panel-module FlipScreens do (10 XORs, even). Port GameLoop FlipScreen still
    // runs every mission frame — arm skip for those synthetic idle flips.
    // R1216 AB_DPAL_PANEL_INSTALL restores DPalSet inside FUNC_001637 so module
    // flips still install+XOR; SHIFT_FIX only suppresses game-loop idle flips
    // (arm before MissionRenderFrame FlipScreen, clear across ProcessActiveObjects,
    // re-arm for the outer FlipScreen). SHIFT wins on the armed skip; PANEL_INSTALL
    // wins on unarmed FUNC_001637 calls. Default both off.
    static int s_shiftFix = -1;
    if (s_shiftFix < 0) {
        const char* e = getenv("AB_DPAL_SHIFT_FIX");
        s_shiftFix = (e && e[0] == '1') ? 1 : 0;
    }
    if (s_shiftFix && s_dpalPanelIdleSkip) {
        s_dpalPanelIdleSkip = false;
        static int s_xorSkipLogs = 0;
        static int s_xorEnv = -1;
        if (s_xorEnv < 0) {
            const char* x = getenv("AB_DPAL_XOR_COUNT");
            s_xorEnv = (x && x[0] == '1') ? 1 : 0;
        }
        if (s_xorEnv && s_xorSkipLogs < 40) {
            fprintf(stderr,
                    "AB_DPAL_XOR_COUNT SKIP n=%d flip=%u base=%#x (panel idle)\n",
                    s_xorSkipLogs, g_videoState ? g_videoState->flipCount : 0u,
                    g_videoState ? g_videoState->dPalBaseIndex : 0);
            ++s_xorSkipLogs;
        }
        return false;
    }
    s_dpalPanelIdleSkip = false;

    // R1216: count real DPalSet executions (not skips). Env-gated, capped.
    static int s_setEnv = -1;
    static int s_setLogs = 0;
    if (s_setEnv < 0) {
        const char* e = getenv("AB_DPAL_SET_COUNT");
        s_setEnv = (e && e[0] == '1') ? 1 : 0;
    }
    if (s_setEnv && s_setLogs < 200) {
        fprintf(stderr,
                "AB_DPAL_SET_COUNT EXEC n=%d flip=%u base=%#x chg=%d\n",
                s_setLogs, g_videoState ? g_videoState->flipCount : 0u,
                g_videoState ? g_videoState->dPalBaseIndex : 0,
                g_videoState ? g_videoState->dPalChangedCount : 0);
        ++s_setLogs;
    }

    for (int i = 0; i < g_videoState->dPalChangedCount; i++) {
        uint8_t slot = g_videoState->dPalChanged[i];
        // DATA_009144 holds 6-bit DAC; ASM VideoSetPalValue does <<2.
        const uint8_t r6 = g_videoState->dPalRGB[slot * 3 + 0];
        const uint8_t g6 = g_videoState->dPalRGB[slot * 3 + 1];
        const uint8_t b6 = g_videoState->dPalRGB[slot * 3 + 2];
        SetPaletteEntry(slot + 0x80,
                        static_cast<uint8_t>(r6 << 2),
                        static_cast<uint8_t>(g6 << 2),
                        static_cast<uint8_t>(b6 << 2));
    }

    // R1208: env-gated bank-phase probe (inert by default; capped).
    // Logs flipCount only (platform layer — no GameLoop dependency).
    static int s_phaseLogs = 0;
    static int s_phaseEnv = -1;
    if (s_phaseEnv < 0) {
        const char* phase = getenv("AB_DPAL_PHASE");
        s_phaseEnv = (phase && phase[0] == '1') ? 1 : 0;
    }
    const bool logPhase = s_phaseEnv && s_phaseLogs < 600;
    const int pre = g_videoState->dPalBaseIndex;

    g_videoState->dPalBaseIndex ^= 0x40;
    if (logPhase) {
        fprintf(stderr,
                "AB_DPAL_PHASE SET n=%d flip=%u pre=%#x post=%#x chg=%d\n",
                s_phaseLogs, g_videoState->flipCount, pre,
                g_videoState->dPalBaseIndex, g_videoState->dPalChangedCount);
        ++s_phaseLogs;
    }
    static int s_xorLogs = 0;
    static int s_xorEnv2 = -1;
    if (s_xorEnv2 < 0) {
        const char* x = getenv("AB_DPAL_XOR_COUNT");
        s_xorEnv2 = (x && x[0] == '1') ? 1 : 0;
    }
    if (s_xorEnv2 && s_xorLogs < 80) {
        fprintf(stderr,
                "AB_DPAL_XOR_COUNT XOR n=%d flip=%u pre=%#x post=%#x\n",
                s_xorLogs, g_videoState->flipCount, pre,
                g_videoState->dPalBaseIndex);
        ++s_xorLogs;
    }
    return true;
}

int DPalGetChangedCount()
{
    return g_videoState->dPalChangedCount;
}

const uint8_t* DPalGetChangedEntries()
{
    return g_videoState->dPalChanged;
}

// Screenshot functions (Phase 18C)

// Forward declarations
void ScrnshotWriteHeader(FILE* file);
void ScrnShotWritePalette(FILE* file);
void ScrnshotWriteData(FILE* file);

// BMP file header (54 bytes)
static const uint8_t BMP_HEADER[54] = {
    0x42, 0x4D,  // "BM" signature
    0x36, 0x04, 0x00, 0x00,  // File size: 64000 + 54 + 1024 = 65078 (0xFE36)
    0x00, 0x00,  // Reserved
    0x00, 0x00,  // Reserved
    0x36, 0x04, 0x00, 0x00,  // Pixel data offset: 54 + 1024 = 1078 (0x0436)
    0x28, 0x00, 0x00, 0x00,  // DIB header size: 40 bytes
    0x40, 0x01, 0x00, 0x00,  // Width: 320 (0x0140)
    0xC8, 0x00, 0x00, 0x00,  // Height: 200 (0x00C8)
    0x01, 0x00,              // Color planes: 1
    0x08, 0x00,              // Bits per pixel: 8
    0x00, 0x00, 0x00, 0x00,  // Compression: 0 (none)
    0x00, 0x00, 0x00, 0x00,  // Image size: 0 (uncompressed)
    0x00, 0x00, 0x00, 0x00,  // X pixels per meter
    0x00, 0x00, 0x00, 0x00,  // Y pixels per meter
    0x00, 0x00, 0x00, 0x00,  // Colors in color table
    0x00, 0x00, 0x00, 0x00   // Important color count
};

// FUNC_001446_ScrnShotSave
void ScrnShotSave()
{
    char filename[32];
    FILE* file = nullptr;
    int index = 0;
    
    // Find next available filename
    do {
        sprintf(filename, "scrnshot%03d.bmp", index);
        file = fopen(filename, "rb");
        if (file == nullptr) break;
        fclose(file);
        index++;
    } while (index < 1000);
    
    ScrnShotSaveTo(filename);
}

bool ScrnShotSaveTo(const char* path)
{
    if (!path || !g_videoState) return false;
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    ScrnshotWriteHeader(file);
    ScrnShotWritePalette(file);
    ScrnshotWriteData(file);
    fclose(file);
    return true;
}

// FUNC_001447_ScrnshotWriteHeader
void ScrnshotWriteHeader(FILE* file)
{
    fwrite(BMP_HEADER, 1, 54, file);
}

// FUNC_001448_ScrnShotWritePalette
void ScrnShotWritePalette(FILE* file)
{
    for (int i = 0; i < 256; i++) {
        uint8_t r, g, b;
        // Get palette entry - use simple mapping for now
        if (i < PALETTE_ENTRIES) {
            r = g_videoState->palette[i * 3 + 0];
            g = g_videoState->palette[i * 3 + 1];
            b = g_videoState->palette[i * 3 + 2];
        } else {
            r = g = b = 0;
        }
        
        // BMP palette format: BGRA (4 bytes per entry)
        fputc(b, file);
        fputc(g, file);
        fputc(r, file);
        fputc(0, file);  // Reserved
    }
}

// FUNC_001449_ScrnshotWriteData
void ScrnshotWriteData(FILE* file)
{
    // BMP stores image data bottom-to-top
    // Allocate buffer for the entire screen
    uint8_t* buffer = new uint8_t[SCREEN_SIZE];
    
    // Copy back buffer to buffer
    memcpy(buffer, g_videoState->backBuffer, SCREEN_SIZE);
    
    // Write rows from bottom to top
    for (int y = SCREEN_HEIGHT - 1; y >= 0; y--) {
        fwrite(buffer + y * SCREEN_WIDTH, 1, SCREEN_WIDTH, file);
    }
    
    delete[] buffer;
}

// Stub VideoBlit for tests (real implementation in sdlvideo.cpp)
void VideoBlit(const uint8_t* data, int x, int y, int w, int h, int jump)
{
    // Stub - real implementation in SDL backend
    (void)data; (void)x; (void)y; (void)w; (void)h; (void)jump;
}

// Weak stub for PlatformSetPaletteEntry (real implementation in sdl_backend.cpp)
__attribute__((weak)) void PlatformSetPaletteEntry(int index, uint8_t r, uint8_t g, uint8_t b)
{
    (void)index; (void)r; (void)g; (void)b;
}

__attribute__((weak)) bool PlatformGetPaletteEntry(int index, uint8_t* r, uint8_t* g, uint8_t* b)
{
    (void)index;
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    return false;
}

// Weak stub for PlatformDumpPalette (real implementation in sdl_backend.cpp).
// GPU backends (ogre/d3d9) keep their live palette outside this layer: fall
// back to the static half of g_videoState->palette so the .pal file still
// exists (entries >= 0x80 stay black — they are per-frame DPal allocations).
__attribute__((weak)) void PlatformDumpPalette(const char* path)
{
    if (!path || !path[0]) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < 256; i++) {
        uint8_t rgba[4] = {0, 0, 0, 0};
        if (g_videoState && i < PALETTE_ENTRIES) {
            rgba[0] = g_videoState->palette[i * 3 + 0];
            rgba[1] = g_videoState->palette[i * 3 + 1];
            rgba[2] = g_videoState->palette[i * 3 + 2];
        }
        fwrite(rgba, 1, 4, f);
    }
    fclose(f);
}

// ============================================================
// Z-Buffer Functions (Phase 40B)
// ============================================================

void ZBufferInit()
{
    if (!g_videoState) return;
    if (!g_videoState->zBuffer) {
        g_videoState->zBuffer = new int16_t[SCREEN_SIZE];
    }
    g_videoState->zBufferEnabled = false;
    ZBufferClear(0x7FFF);  // Max depth (farthest)
}

void ZBufferCleanup()
{
    if (g_videoState && g_videoState->zBuffer) {
        delete[] g_videoState->zBuffer;
        g_videoState->zBuffer = nullptr;
    }
}

void ZBufferClear(int16_t depth)
{
    if (!g_videoState || !g_videoState->zBuffer) return;
    for (int i = 0; i < SCREEN_SIZE; i++) {
        g_videoState->zBuffer[i] = depth;
    }
}

void ZBufferEnable(bool enable)
{
    if (g_videoState) {
        g_videoState->zBufferEnabled = enable;
    }
}

bool ZBufferTest(int x, int y, int16_t z)
{
    if (!g_videoState || !g_videoState->zBuffer || !g_videoState->zBufferEnabled) return true;
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return false;
    return z < g_videoState->zBuffer[y * SCREEN_WIDTH + x];
}

void ZBufferWrite(int x, int y, int16_t z)
{
    if (!g_videoState || !g_videoState->zBuffer || !g_videoState->zBufferEnabled) return;
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    g_videoState->zBuffer[y * SCREEN_WIDTH + x] = z;
}

} // namespace jjffe
