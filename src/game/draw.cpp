#include "jjffe/draw.h"
#include "jjffe/ab_plotstamp.h"
#include "jjffe/video.h"
#include "jjffe/data.h"
#include "data_blob.h"
#include "jjffe/planet_shade_trace.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace jjffe {

// ASM 3D viewport clip: the original fill primitives (FUNC_001604 triangle,
// FUNC_001650-1653 texture spans, FUNC_001700/001705/001712 dispatchers)
// hardcode scanline rejection at y >= 0x9E (158) — the 3D view area above
// the cockpit HUD. Full-screen UI (text, rects) is NOT clipped by these.
constexpr int k3DClipBottom = 0x9E; // 158

// DrawPixel - FUNC_001629_DrawPixelToBuf
void DrawPixel(int x, int y, uint8_t color)
{
    if (x >= 0 && x < 320 && y >= 0 && y < 200) {
        jjffe::AbPlotStamp(x, y, color, "pixel");
        g_videoState->backBuffer[y * 320 + x] = color;
    }
}

// DrawHLine - FUNC_001601_DrawHLine
void DrawHLine(int x, int y, int length, uint8_t color)
{
    if (y < 0 || y >= 200 || length <= 0) {
        return;
    }

    // Clip to the row so dword stores cannot spill into the next scanline's
    // left edge (x=318/319 with a 4-byte write → col0/col1 on y+1).
    if (x < 0) {
        length += x;
        x = 0;
    }
    if (x >= 320 || length <= 0) return;
    if (x + length > 320) length = 320 - x;
    if (length <= 0) return;
    jjffe::AbPlotStampSpan(y, x, length, color, "hline");

    uint8_t* buffer = g_videoState->backBuffer + y * 320;
    
    // Handle alignment for optimized 4-byte writes
    while ((x & 3) != 0 && length > 0) {
        buffer[x] = color;
        x++;
        length--;
    }
    
    // Optimized 4-byte writes — only while a full dword fits in-row.
    uint32_t color4 = color | (color << 8) | (color << 16) | (color << 24);
    while (length >= 4 && x <= 316) {
        *reinterpret_cast<uint32_t*>(buffer + x) = color4;
        x += 4;
        length -= 4;
    }
    
    // Remaining pixels
    while (length > 0) {
        buffer[x] = color;
        x++;
        length--;
    }
}

// DrawVLine
void DrawVLine(int x, int y, int length, uint8_t color)
{
    if (x < 0 || x >= 320 || length <= 0) {
        return;
    }
    
    uint8_t* buffer = g_videoState->backBuffer + x;
    for (int i = 0; i < length && y < 200; i++, y++) {
        if (y >= 0) {
            buffer[y * 320] = color;
        }
    }
}

// DrawLine - FUNC_001616_DrawLine (ffe.asm:96968)
//
// R1113: this is NOT a Bresenham single-pixel walk. The ASM computes one
// 16.16 x-step per scanline (dx<<16/dy, C truncation) and draws, for every
// row, either one pixel (|step| <= 1.0) or the horizontal run between the
// previous and the new x (|step| > 1.0), plus a final pixel at (x2,y2).
// The old Bresenham port matched shallow lines only by accident and drifted
// by one pixel at rounding boundaries; the hull-18 contour residue was that
// black line overlay landing one pixel off the ASM's line.
void DrawLine(int x1, int y1, int x2, int y2, uint8_t color)
{
    static const bool legacyBresenham = []() {
        const char* e = getenv("AB_LEGACY_BRESENHAM");
        return e && e[0] == '1';
    }();
    if (legacyBresenham) {
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;
        while (true) {
            DrawPixel(x1, y1, color);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x1 += sx; }
            if (e2 < dx) { err += dx; y1 += sy; }
        }
        return;
    }

    // Same-row: one inclusive horizontal run (ASM JUMP_006146 / fallthrough).
    if (y1 == y2) {
        if (x1 <= x2) DrawHLine(x1, y1, x2 - x1 + 1, color);
        else          DrawHLine(x2, y1, x1 - x2 + 1, color);
        return;
    }
    // ASM orders the endpoints by y (JUMP_006148), keeping x with its point.
    if (y1 > y2) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int32_t dy = y2 - y1;
    int32_t step;
    {
        const int32_t ddx = x2 - x1;
        if (ddx >= 0) step = static_cast<int32_t>((static_cast<uint32_t>(ddx) << 16) / dy);
        else          step = -static_cast<int32_t>((static_cast<uint32_t>(-ddx) << 16) / dy);
    }
    int32_t xfp = static_cast<int32_t>(static_cast<uint32_t>(x1) << 16);

    if (step >= 0) {
        if (step <= 0x10000) {
            // ASM JUMP_006154: one pixel per row, then the exact endpoint.
            for (int32_t y = y1; y < y2; ++y) {
                DrawHLine(xfp >> 16, y, 1, color);
                xfp += step;
            }
            DrawHLine(x2, y2, 1, color);
        } else {
            // ASM JUMP_006157: run from the previous x to the new x per row.
            for (int32_t y = y1; y < y2; ++y) {
                const int32_t xa = xfp >> 16;
                const int32_t xb = (xfp + step) >> 16;
                DrawHLine(xa, y, xb - xa, color);
                xfp += step;
            }
            const int32_t xa = xfp >> 16;
            DrawHLine(xa, y2, x2 - xa + 1, color);
        }
    } else {
        if (step >= -0x10000) {
            // ASM JUMP_006160: shallow leftward, one pixel per row.
            for (int32_t y = y1; y < y2; ++y) {
                DrawHLine(xfp >> 16, y, 1, color);
                xfp += step;
            }
            DrawHLine(x2, y2, 1, color);
        } else {
            // ASM JUMP_006163: steep leftward; first pixel, then the run up
            // to the previous x, through the last row (y <= y2).
            DrawHLine(x1, y1, 1, color);
            for (int32_t y = y1 + 1; y <= y2; ++y) {
                const int32_t xa = (xfp + step) >> 16;
                const int32_t xb = xfp >> 16;
                DrawHLine(xa, y, xb - xa, color);
                xfp += step;
            }
        }
    }
}

// DrawRect
void DrawRect(int x, int y, int width, int height, uint8_t color)
{
    for (int i = 0; i < height; i++) {
        DrawHLine(x, y + i, width, color);
    }
}

// DrawBox - FUNC_001627_DrawBoxToBuf
void DrawBox(int x1, int y1, int x2, int y2, uint8_t color)
{
    // Draw horizontal lines (add 1 to include both endpoints)
    DrawHLine(x1, y1, x2 - x1 + 1, color);
    DrawHLine(x1, y2, x2 - x1 + 1, color);
    
    // Draw vertical lines (add 1 to include both endpoints)
    DrawVLine(x1, y1, y2 - y1 + 1, color);
    DrawVLine(x2, y1, y2 - y1 + 1, color);
}

// DrawChar - FUNC_001598_DrawChar
// Renders a character glyph from the original game's font bitmap data.
// Font format: 10 bytes per character (9 rows of 8-bit bitmap + 1 byte advance width)
// INDEXING: callers pass (ASCII - 0x20). Space = 0. TextWriteInternal already
// subtracts 0x20 before calling; DrawString must do the same.
int DrawChar(char c, uint8_t color, int x, int y, int startY, int endY)
{
    if (!g_videoState) return 6;

    // endY < 0 (TextWriteInternal passes 0xffffffff = -1) means "no upper
    // clip" — clamp to the 200-row back buffer (ASM FUNC_001598 does the
    // same: ymax<0 → clip pointer = backbuffer + 201*320).
    const int ymax = (endY < 0) ? 200 : endY;
    if (y < 0 || y >= 200 || y < startY || y >= ymax) return 6;

    unsigned char idx = static_cast<unsigned char>(c);
    // Text system: codes >= 0xE0 are control ops, not glyphs.
    if (idx >= 0xE0) return 0;

    const uint8_t* blob = data::GetDataBlob();
    const int advance = blob ? blob[data::OFFSET_DATA_007750 + idx * 10 + 9] : 6;

    if (blob) {
        // Font data at DATA_007750, stride 10 per char
        const uint8_t* glyph = blob + data::OFFSET_DATA_007750 + idx * 10;

        for (int row = 0; row < 9; row++) {
            int py = y + row;
            if (py < startY || py >= ymax || py < 0 || py >= 200) continue;

            uint8_t bits = glyph[row];
            if (bits == 0) continue;

            int px = x;
            for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
                if (px >= 0 && px < 320 && (bits & mask)) {
                    g_videoState->backBuffer[py * 320 + px] = color;
                }
                px++;
            }
        }
    } else {
        // Fallback: simple 5x7 block when font data unavailable
        for (int row = 0; row < 7; row++) {
            int py = y + row;
            if (py < startY || py >= ymax || py < 0 || py >= 200) continue;
            for (int col = 0; col < 5; col++) {
                int px = x + col;
                if (px >= 0 && px < 320)
                    g_videoState->backBuffer[py * 320 + px] = color;
            }
        }
    }
    return advance;
}

// Advance for a font index (ASCII - 0x20), FUNC_001597 via DATA_007751.
static int GetCharAdvanceIndex(unsigned char idx)
{
    if (idx >= 0xE0) return 0;
    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return 6;
    return blob[data::OFFSET_DATA_007750 + idx * 10 + 9];
}

// DrawString — plain C strings (ASCII). Converts to font index like TextWrite.
void DrawString(const char* str, uint8_t color, int x, int y)
{
    while (*str) {
        unsigned char ch = static_cast<unsigned char>(*str++);
        if (ch < 0x20) continue;
        unsigned char idx = static_cast<unsigned char>(ch - 0x20);
        DrawChar(static_cast<char>(idx), color, x, y, 0, 200);
        x += GetCharAdvanceIndex(idx);
    }
}

int MeasureStringWidth(const char* str)
{
    int w = 0;
    while (str && *str) {
        unsigned char ch = static_cast<unsigned char>(*str++);
        if (ch < 0x20) continue;
        w += GetCharAdvanceIndex(static_cast<unsigned char>(ch - 0x20));
    }
    return w;
}

// FUNC_001603_CalcLineGrad — compute 16.16 fixed-point edge gradient.
// Returns (dx << 16) / dy with sign handling matching x86 IDIV.
// Use int64 for the shift: Earth-scale edges have |dx| up to ~20k; the
// old `(uint32_t)dx << 16` path is fine for int16 deltas, but callers can
// pass clipped int32 extents — keep the quotient in 64-bit then narrow.
static int32_t CalcLineGrad(int32_t dx, int32_t dy)
{
    if (dy == 0) return (dx < 0) ? -0x7fffffff : 0x7fffffff;
    // ASM shl on the raw value; shift the unsigned view (UBSan: negative dx).
    return static_cast<int32_t>(static_cast<int64_t>(static_cast<uint64_t>(dx) << 16) / dy);
}

// FUNC_001563 — move vert a onto clipY along segment a→b (C trunc toward 0).
static int16_t ClipVertXAtY(int16_t ax, int16_t ay, int16_t bx, int16_t by,
                            int32_t clipY)
{
    if (ay == by)
        return ax;
    // int64: Earth quads have |ax-bx|~10k and |ay-by|~20k; 32-bit
    // multiply overflows and clips X to garbage (empty fills).
    const int64_t num = (static_cast<int64_t>(ax) - bx) * (clipY - by);
    const int64_t den = static_cast<int64_t>(ay) - by;
    return static_cast<int16_t>(num / den + bx);
}

// DrawTriangle - FUNC_001604_DrawTriangle
// ASM algorithm: 16.16 fixed-point edge accumulation, exclusive bottom Y,
// exclusive right edge (memset span). Clips at y < 0x9E (158).
// Offscreen tops (y<0) and bottoms (y>=0x9E): each edge is Y-clipped
// independently via 001563 before accumulation (001560). Independent
// clips keep Asp mesh23 coverage (top [157,160) at y=0; lower tip→bot
// chord into mat7 when bot is at y=169).
void DrawTriangle(const int16_t* v1, const int16_t* v2, const int16_t* v3, uint8_t color)
{
    const int16_t* top = v1;
    const int16_t* mid = v2;
    const int16_t* bot = v3;
    if (mid[1] < top[1]) std::swap(top, mid);
    if (bot[1] < top[1]) std::swap(top, bot);
    if (bot[1] < mid[1]) std::swap(mid, bot);

    const int yTop = top[1], yMid = mid[1], yBot = bot[1];
    if (yTop >= yBot) return;

    uint8_t* buf = g_videoState->backBuffer;
    constexpr int kClipBot = 0x9E;

    auto fillSpan = [&](int32_t e1, int32_t e2, int y) {
        if ((unsigned)y >= (unsigned)kClipBot) return;
        int xL = e1 >> 16;
        int xR = e2 >> 16;
        if (xL < 0) xL = 0;
        if (xR > 0x140) xR = 0x140;
        if (xL < xR) {
            jjffe::AbPlotStampSpan(y, xL, xR - xL, color, "flat");
            memset(buf + y * 320 + xL, color, xR - xL);
        }
    };

    const int yStart = (yTop < 0) ? 0 : yTop;
    const int yEnd = (yBot > kClipBot) ? kClipBot : yBot;
    if (yStart >= yEnd) return;

    if (yTop < yMid) {
        // Short: top→mid→bot; long: top→bot.
        int16_t xS0 = top[0], yS0 = (int16_t)yTop;
        int16_t xL0 = top[0], yL0 = (int16_t)yTop;
        if (yTop < 0) {
            if (yMid >= 0) {
                xS0 = ClipVertXAtY(top[0], top[1], mid[0], mid[1], 0);
                yS0 = 0;
            }
            if (yBot >= 0) {
                xL0 = ClipVertXAtY(top[0], top[1], bot[0], bot[1], 0);
                yL0 = 0;
            }
        }
        int16_t xL1 = bot[0], yL1 = (int16_t)yBot;
        if (yBot > kClipBot) {
            xL1 = ClipVertXAtY(bot[0], bot[1], xL0, yL0, kClipBot);
            yL1 = (int16_t)kClipBot;
        }

        int32_t gS = CalcLineGrad(mid[0] - xS0, yMid - yS0);
        int32_t gL = CalcLineGrad(xL1 - xL0, yL1 - yL0);
        int32_t eS = (int32_t)((uint32_t)(int32_t)xS0 << 16);
        int32_t eL = (int32_t)((uint32_t)(int32_t)xL0 << 16);

        int yP1End = yMid;
        if (yP1End > yEnd) yP1End = yEnd;
        if (yStart >= yMid) {
            if (yMid > yL0)
                eL = static_cast<int32_t>(
                    eL + static_cast<int64_t>(gL) * (yMid - yL0));
            yP1End = yStart;
        }

        const bool shortLeftP1 = (gS < gL);
        for (int y = yStart; y < yP1End; y++) {
            if (shortLeftP1) fillSpan(eS, eL, y);
            else fillSpan(eL, eS, y);
            eS += gS;
            eL += gL;
        }

        // Phase 2: replace short with mid→bot (bot maybe clipped to kClipBot).
        int16_t xS1 = bot[0], yS1 = (int16_t)yBot;
        if (yBot > kClipBot) {
            xS1 = ClipVertXAtY(bot[0], bot[1], mid[0], mid[1], kClipBot);
            yS1 = (int16_t)kClipBot;
        }
        eS = (int32_t)((uint32_t)(int32_t)mid[0] << 16);
        gS = CalcLineGrad(xS1 - mid[0], yS1 - yMid);
        int y2 = (yStart > yMid) ? yStart : yMid;
        if (yP1End < y2)
            eL = static_cast<int32_t>(
                eL + static_cast<int64_t>(gL) * (y2 - yP1End));
        // Side from edge positions at mid (not phase-1 grads).
        const bool shortLeftP2 = ((int32_t)((uint32_t)(int32_t)mid[0] << 16) <= eL);
        for (int y = y2; y < yEnd; y++) {
            if (shortLeftP2) fillSpan(eS, eL, y);
            else fillSpan(eL, eS, y);
            eS += gS;
            eL += gL;
        }
    } else {
        // Flat top: yTop == yMid.
        int16_t x0 = top[0], x1 = mid[0];
        int y0 = yTop;
        if (yTop < 0 && yBot >= 0) {
            x0 = ClipVertXAtY(top[0], top[1], bot[0], bot[1], 0);
            x1 = ClipVertXAtY(mid[0], mid[1], bot[0], bot[1], 0);
            y0 = 0;
        }
        int16_t xB0 = bot[0], xB1 = bot[0];
        int yB = yBot;
        if (yBot > kClipBot) {
            xB0 = ClipVertXAtY(bot[0], bot[1], x0, (int16_t)y0, kClipBot);
            xB1 = ClipVertXAtY(bot[0], bot[1], x1, (int16_t)y0, kClipBot);
            yB = kClipBot;
        }
        int32_t g0 = CalcLineGrad(xB0 - x0, yB - y0);
        int32_t g1 = CalcLineGrad(xB1 - x1, yB - y0);
        int32_t e0 = (int32_t)((uint32_t)(int32_t)x0 << 16);
        int32_t e1 = (int32_t)((uint32_t)(int32_t)x1 << 16);
        const int y0s = (yTop < 0) ? 0 : yTop;
        if (x0 < x1) {
            for (int y = y0s; y < yEnd; y++) {
                fillSpan(e0, e1, y);
                e0 += g0;
                e1 += g1;
            }
        } else {
            for (int y = y0s; y < yEnd; y++) {
                fillSpan(e1, e0, y);
                e0 += g0;
                e1 += g1;
            }
        }
    }
}

// DrawTriangleZ - Z-buffered triangle with depth interpolation
// Vertices are [x, y, z] arrays
void DrawTriangleZ(const int16_t* v1, const int16_t* v2, const int16_t* v3, uint8_t color)
{
    static int callCount = 0;
    if (callCount++ < 3) {
        fprintf(stderr, "DrawTriangleZ called: v1=(%d,%d,%d) v2=(%d,%d,%d) v3=(%d,%d,%d) color=%d\n",
            v1[0], v1[1], v1[2], v2[0], v2[1], v2[2], v3[0], v3[1], v3[2], color);
        fprintf(stderr, "  zBuffer=%p zBufferEnabled=%d\n", 
            g_videoState ? (void*)g_videoState->zBuffer : nullptr,
            g_videoState ? g_videoState->zBufferEnabled : false);
    }
    
    // Sort vertices by Y coordinate
    const int16_t* verts[3] = {v1, v2, v3};
    
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2 - i; j++) {
            if (verts[j][1] > verts[j+1][1]) {
                std::swap(verts[j], verts[j+1]);
            }
        }
    }
    
    int y1 = verts[0][1];
    int y2 = verts[1][1];
    int y3 = verts[2][1];
    
    if (callCount <= 3) {
        fprintf(stderr, "  Sorted: y1=%d y2=%d y3=%d\n", y1, y2, y3);
    }
    
    if (y1 == y3) return;
    
    // Z values for each vertex
    int32_t z1 = verts[0][2];
    int32_t z2 = verts[1][2];
    int32_t z3 = verts[2][2];
    
    // Scanline fill with Z-interpolation
    for (int y = y1; y <= y3; y++) {
        if (y < 0 || y >= 200) continue;
        
        int x1, x2;
        int32_t zLeft, zRight;
        
        if (y < y2) {
            // Upper part: interpolate between v0-v1 and v0-v2
            if (y2 == y1) {
                x1 = verts[0][0];
                x2 = verts[1][0];
                zLeft = z1;
                zRight = z2;
            } else {
                int dy = y - y1;
                int dy12 = y2 - y1;
                int dy13 = y3 - y1;
                x1 = verts[0][0] + (verts[1][0] - verts[0][0]) * dy / dy12;
                x2 = verts[0][0] + (verts[2][0] - verts[0][0]) * dy / dy13;
                zLeft = z1 + (int64_t)(z2 - z1) * dy / dy12;
                zRight = z1 + (int64_t)(z3 - z1) * dy / dy13;
            }
        } else {
            // Lower part: interpolate between v1-v2 and v0-v2
            if (y3 == y2) {
                x1 = verts[1][0];
                x2 = verts[2][0];
                zLeft = z2;
                zRight = z3;
            } else {
                int dy = y - y2;
                int dy23 = y3 - y2;
                int dy13 = y3 - y1;
                x1 = verts[1][0] + (verts[2][0] - verts[1][0]) * dy / dy23;
                x2 = verts[0][0] + (verts[2][0] - verts[0][0]) * (y - y1) / dy13;
                zLeft = z2 + (int64_t)(z3 - z2) * dy / dy23;
                zRight = z1 + (int64_t)(z3 - z1) * (y - y1) / dy13;
            }
        }
        
        if (x1 > x2) {
            std::swap(x1, x2);
            std::swap(zLeft, zRight);
        }
        
        int length = x2 - x1 + 1;
        if (length <= 0) continue;
        
        // Z-interpolation across the scanline
        int32_t zStep = (zRight - zLeft) / length;
        int32_t z = zLeft;
        
        uint8_t* buffer = g_videoState->backBuffer;
        
        int pixelsDrawn = 0;
        for (int x = x1; x <= x2; x++) {
            if (x >= 0 && x < 320) {
                // Z-test: smaller z = closer = should be drawn
                // Use >> 16 to get integer part of fixed-point Z coordinate
                int16_t zDepth = (int16_t)(z >> 16);
                if (ZBufferTest(x, y, zDepth)) {
                    buffer[y * 320 + x] = color;
                    ZBufferWrite(x, y, zDepth);
                    pixelsDrawn++;
                }
            }
            z += zStep;
        }
        
        if (callCount <= 3 && y == y1) {
            fprintf(stderr, "  Scanline y=%d: x1=%d x2=%d zLeft=%d zRight=%d pixelsDrawn=%d\n",
                y, x1, x2, zLeft, zRight, pixelsDrawn);
        }
    }
}

// DrawQuad - FUNC_001607_DrawQuad
void DrawQuad(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4, uint8_t color)
{
    // Draw as two triangles
    DrawTriangle(v1, v2, v3, color);
    DrawTriangle(v1, v3, v4, color);
}

// DrawCircle - FUNC_001612_DrawCircle
void DrawCircle(int centerX, int16_t* centerY, int diameter, uint8_t color)
{
    int radius = diameter / 2;
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;
    
    while (y >= x) {
        // Draw horizontal lines for each scanline
        int yTop = *centerY - y;
        int yBottom = *centerY + y;
        int xLeft = centerX - x;
        int xRight = centerX + x;
        
        if (yTop >= 0 && yTop < 200) {
            DrawHLine(xLeft, yTop, xRight - xLeft + 1, color);
        }
        if (yBottom >= 0 && yBottom < 200 && y != 0) {
            DrawHLine(xLeft, yBottom, xRight - xLeft + 1, color);
        }
        
        x++;
        if (d > 0) {
            y--;
            d += 4 * (x - y) + 10;
        } else {
            d += 4 * x + 6;
        }
    }
}

// DrawSprite - FUNC_001609_DrawSprite
void DrawSprite(int x, int y, const uint8_t* spriteData, int width, int height)
{
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            uint8_t pixel = spriteData[row * width + col];
            if (pixel != 0) { // Skip transparent pixels
                DrawPixel(x + col, y + row, pixel);
            }
        }
    }
}

// DrawParticle - FUNC_001611_DrawParticle
void DrawParticle(int x, int y, uint8_t color)
{
    DrawPixel(x, y, color);
}

// DrawParticleClipped - FUNC_001610_DrawParticleClipped
void DrawParticleClipped(int x, int y, uint8_t color)
{
    if (x >= 0 && x < 320 && y >= 0 && y < 200) {
        DrawPixel(x, y, color);
    }
}

// DrawScannerStalk - FUNC_001569_DrawScannerStalk
void DrawScannerStalk(int centerX, int centerY, int radius, uint8_t color)
{
    DrawUpStalk(centerX, centerY - radius, radius, color);
    DrawDownStalk(centerX, centerY, radius, color);
}

// DrawUpStalk - FUNC_001570_DrawUpStalk
void DrawUpStalk(int x, int y, int length, uint8_t color)
{
    DrawVLine(x, y, length, color);
}

// DrawDownStalk - FUNC_001571_DrawDownStalk
void DrawDownStalk(int x, int y, int length, uint8_t color)
{
    DrawVLine(x, y, length, color);
}

// DrawShadowedChar - FUNC_001588_DrawShadowedChar
int DrawShadowedChar(char c, uint8_t color, int x, int y, int startY, int endY)
{
    // Draw shadow (black) offset by 1 pixel
    DrawChar(c, 0, x + 1, y + 1, startY, endY);
    // Draw character; return its advance width (ASM leaves FUNC_001598's
    // glyph[9] advance in eax for the caller's x += ).
    return DrawChar(c, color, x, y, startY, endY);
}

// DrawHLineClip - FUNC_001600_DrawHLineClip
void DrawHLineClip(int x, int y, int length, uint8_t color)
{
    // Clip to screen bounds
    if (y < 0 || y >= 200) return;
    if (x < 0) {
        length += x;
        x = 0;
    }
    if (x + length > 320) {
        length = 320 - x;
    }
    if (length > 0) {
        DrawHLine(x, y, length, color);
    }
}

// DrawHLineConClip - FUNC_001599_DrawHLineConClip
void DrawHLineConClip(int x, int y, int length, uint8_t color)
{
    // Same as DrawHLineClip for now
    DrawHLineClip(x, y, length, color);
}

// DrawPolySegment - FUNC_001602_DrawPolySegment
void DrawPolySegment(int x1, int y1, int x2, int y2, uint8_t color)
{
    DrawLine(x1, y1, x2, y2, color);
}

// DrawSpans - FUNC_001613_DrawSpans
void DrawSpans(int y, int x1, int x2, const uint8_t* texture, int texX1, int texX2)
{
    if (y < 0 || y >= 200) return;
    if (x1 > x2) {
        std::swap(x1, x2);
        std::swap(texX1, texX2);
    }
    
    int length = x2 - x1;
    if (length <= 0) return;
    
    uint8_t* buffer = g_videoState->backBuffer + y * 320;
    int texStep = (texX2 - texX1) / length;
    int texX = texX1;
    
    for (int i = 0; i < length; i++) {
        int screenX = x1 + i;
        if (screenX >= 0 && screenX < 320) {
            const uint8_t v = texture[texX >> 16];
            jjffe::AbPlotStamp(screenX, y, v, "span");
            buffer[screenX] = v;  // Fixed-point texture coordinate
        }
        texX += texStep;
    }
}

// DrawTexQuad — implemented via DrawTexQuadShaded below (ASM 001647/001649).
void DrawTexQuad(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                 const uint8_t* texture, int texWidth);
void DrawTexQuadDet(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                    const uint8_t* texture, int texWidth, uint8_t lightLevel);

// Buffer management utilities (Phase 19A)

// ClearBuf - FUNC_001577_ClearBuf
void ClearBuf()
{
    // Clear the entire back buffer to color 0
    memset(g_videoState->backBuffer, 0, 320 * 200);
}

// ClearUpperBuf - FUNC_001618_ClearUpperBuf
// ASM: memset(upper 50560 bytes, DATA_009222 (render-state colour), 0xc580).
void ClearUpperBuf()
{
    const uint32_t fill = GetRenderFillColor() & 0xffu;
    if (getenv("AB_DUMP_FILLCOLOR")) {
        static int nf = 0;
        if (nf < 3) { fprintf(stderr, "CLEARUPPER fill=%u\n", (unsigned)fill); ++nf; }
    }
    memset(g_videoState->backBuffer, (int)fill, 50560);
}

// ZeroUpperBuf - FUNC_001619_ZeroUpperBuf
void ZeroUpperBuf()
{
    // Same as ClearUpperBuf - zero out upper buffer
    memset(g_videoState->backBuffer, 0, 50560);
}

// Pointer/cursor utilities (Phase 19B)

// TogglePtr - FUNC_001524_TogglePtr
void TogglePtr()
{
    // Toggle pointer visibility
    // In original, this was a no-op, so we keep it simple
}

// Texture mapping (Phase 18E)

// Global texture rendering state (legacy callers / TexInitState)
TexRenderState g_texState = {};

// =============================================================================
// ASM texture pipeline — FUNC_001642 / 001649 / 001650-001653
// Edge state mirrors DATA_009236..009248. Shade remap via DATA_009252.
// =============================================================================

static int32_t s_u[2];   // DATA_009236 / 009238
static int32_t s_v[2];   // DATA_009237 / 009239
static int32_t s_du[2];  // DATA_009240 / 009242
static int32_t s_dv[2];  // DATA_009241 / 009243
static int32_t s_x[2];   // DATA_009244 / 009245
static int32_t s_dx[2];  // DATA_009246 / 009247
static int32_t s_y;      // DATA_009248

static const uint8_t* s_texPixels = nullptr;   // DATA_009235
static const uint8_t* s_shadePal = nullptr;    // DATA_009252
static void (*s_texSpanFunc)(int) = nullptr;   // DATA_009250
static int s_texCount = 0;
// R1203 dig: last drawTex / bmp index seen by shaded quad entry points.
static uint32_t s_r1203DrawTex = 0;
static int s_r1203Bmp = -1;
static int16_t s_r1203Verts[8] = {};
static uint32_t s_r1203Seq = 0;
static uint8_t s_identityPal[256];
static bool s_identityPalInit = false;

// R1205: env-gated watch-list for R1203_WR last-writer lines. Default off.
// R1203_CELLS=<path> loads x,y[,...] rows (diff_cells.txt shape). R1203_LAST=1
// alone still watches (301,71). Cap keeps a full targeting run bounded.
// Lookup is a y71..75 x-bitset so the hot path never calls getenv or scans a list.
static constexpr int kR1203WatchY0 = 71;
static constexpr int kR1203WatchY1 = 75;
static constexpr int kR1203WatchCap = 100000;
static uint32_t s_r1203WatchBits[5][10] = {}; // 5 rows × 320 bits
static bool s_r1203WatchOn = false;
static bool s_r1203WatchInit = false;
static int s_r1203WatchEmitted = 0;

static void R1203WatchMark(int x, int y)
{
    if (y < kR1203WatchY0 || y > kR1203WatchY1 || x < 0 || x >= 320)
        return;
    const int row = y - kR1203WatchY0;
    s_r1203WatchBits[row][x >> 5] |= (1u << (x & 31));
}

static bool R1203WatchHit(int x, int y)
{
    if (!s_r1203WatchOn || y < kR1203WatchY0 || y > kR1203WatchY1 || x < 0 || x >= 320)
        return false;
    const int row = y - kR1203WatchY0;
    return (s_r1203WatchBits[row][x >> 5] & (1u << (x & 31))) != 0;
}

static void R1203WatchEnsureInit()
{
    if (s_r1203WatchInit)
        return;
    s_r1203WatchInit = true;
    const char* cells = std::getenv("R1203_CELLS");
    const char* last = std::getenv("R1203_LAST");
    const bool lastOn = last && last[0] && last[0] != '0';
    if (cells && cells[0]) {
        if (FILE* f = std::fopen(cells, "r")) {
            char line[128];
            while (std::fgets(line, sizeof(line), f)) {
                int x = -1, y = -1;
                if (std::sscanf(line, "%d,%d", &x, &y) == 2)
                    R1203WatchMark(x, y);
            }
            std::fclose(f);
            s_r1203WatchOn = true;
        }
    }
    if (lastOn) {
        R1203WatchMark(301, 71);
        s_r1203WatchOn = true;
    }
}

// Caller must R1203WatchEnsureInit() once per span (or earlier) and gate on
// s_r1203WatchOn before entering — keeps the inert hot path to one branch.
static void R1203WatchMaybeLog(int sx, int sy, uint8_t before, uint8_t out,
                               uint8_t texel, int uInt, int vShifted, int texOff,
                               const uint8_t* pal, int xL, int xR)
{
    if (!R1203WatchHit(sx, sy) || s_r1203WatchEmitted >= kR1203WatchCap)
        return;
    ++s_r1203WatchEmitted;
    std::fprintf(stderr,
                 "R1203_WR (%d,%d) %#x->%u drawTex=0x%x bmp=%d "
                 "texel=%u u=%d v=%d off=%d pal=[%u %u %u %u %u %u %u] "
                 "xL=%d xR=%d verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                 sx, sy, (unsigned)before, (unsigned)out,
                 (unsigned)s_r1203DrawTex, s_r1203Bmp,
                 (unsigned)texel, uInt, vShifted, texOff,
                 pal ? (unsigned)pal[0] : 0u, pal ? (unsigned)pal[1] : 0u,
                 pal ? (unsigned)pal[2] : 0u, pal ? (unsigned)pal[3] : 0u,
                 pal ? (unsigned)pal[4] : 0u, pal ? (unsigned)pal[5] : 0u,
                 pal ? (unsigned)pal[6] : 0u,
                 xL, xR,
                 (int)s_r1203Verts[0], (int)s_r1203Verts[1],
                 (int)s_r1203Verts[2], (int)s_r1203Verts[3],
                 (int)s_r1203Verts[4], (int)s_r1203Verts[5],
                 (int)s_r1203Verts[6], (int)s_r1203Verts[7]);
}

static const uint8_t* ShadePalOrIdentity(const uint8_t* pal)
{
    if (pal) return pal;
    if (!s_identityPalInit) {
        for (int i = 0; i < 256; i++) s_identityPal[i] = static_cast<uint8_t>(i);
        s_identityPalInit = true;
    }
    return s_identityPal;
}

// Exact ASM fixed-div for GenerateTexSteps (signed 16.16 / dy).
// Multiplies in int64 — Earth intro quads span |dy|~8k with |dx|~5k; a
// 32-bit `num * 0x10000` overflows and yields NaN-like edge steps.
static int32_t AsmFixedStep(int num, int den)
{
    if (den == 0)
        return (num < 0) ? -0x7fffffff : 0x7fffffff;
    const int64_t n = num;
    const int64_t d = den;
    if (d < 0) {
        if (n < 0)
            return static_cast<int32_t>((n * -0x10000) / -d);
        return static_cast<int32_t>(-((n * 0x10000) / -d));
    }
    if (n < 0)
        return static_cast<int32_t>(-((n * -0x10000) / d));
    return static_cast<int32_t>((n * 0x10000) / d);
}

// FUNC_001642_GenerateTexSteps
void GenerateTexSteps(int edge, const int16_t* vStart, const int16_t* tStart,
                      const int16_t* vEnd, const int16_t* tEnd)
{
    if (edge < 0 || edge > 1 || !vStart || !tStart || !vEnd || !tEnd) return;
    const int dy = static_cast<int>(vEnd[1]) - static_cast<int>(vStart[1]);
    s_u[edge] = (int32_t)((uint32_t)static_cast<int32_t>(tStart[0]) << 16);
    s_v[edge] = (int32_t)((uint32_t)static_cast<int32_t>(tStart[1]) << 16);
    s_du[edge] = AsmFixedStep(static_cast<int>(tEnd[0]) - static_cast<int>(tStart[0]), dy);
    s_dv[edge] = AsmFixedStep(static_cast<int>(tEnd[1]) - static_cast<int>(tStart[1]), dy);
    s_x[edge] = (int32_t)((uint32_t)static_cast<int32_t>(vStart[0]) << 16);
    s_dx[edge] = AsmFixedStep(static_cast<int>(vEnd[0]) - static_cast<int>(vStart[0]), dy);
}

void GetTexEdgeState(int32_t out[12])
{
    const int32_t* src[6] = {s_u, s_v, s_du, s_dv, s_x, s_dx};
    for (int i = 0; i < 6; i++) {
        out[i * 2] = src[i][0];
        out[i * 2 + 1] = src[i][1];
    }
}

static void AdvanceEdges(int n)
{
    s_x[0] += n * s_dx[0];
    s_u[0] += n * s_du[0];
    s_v[0] += n * s_dv[0];
    s_x[1] += n * s_dx[1];
    s_u[1] += n * s_du[1];
    s_v[1] += n * s_dv[1];
    s_y += n;
}

// Shared scanline body for Tex64/128 non-trans / trans (FUNC_001650–001653).
// Walks left→right starting at the left edge's U/V; step = (right−left)/span.
template <int kTexBits, bool kTrans>
static void TexSpanAsm(int scanlines)
{
    // V packing: 64 → >>10 & ~0x3f; 128 → >>9 & ~0x7f (ASM leaves U unmasked)
    constexpr int kVShift = (kTexBits == 6) ? 10 : 9;
    constexpr uint32_t kVMask = (kTexBits == 6) ? 0xffffffc0u : 0xffffff80u;

    {
        static const bool s_dbg = []() {
            const char* e = getenv("AB_721_DBG");
            return e && e[0];
        }();
        static int s_n = 0;
        if (s_dbg && s_n < 60) {
            fprintf(stderr, "SPAN n=%d x0=%d x1=%d u0=%d v0=%d u1=%d v1=%d y=%d n=%d "
                    "dx0=%d dx1=%d du0=%d dv0=%d du1=%d dv1=%d\n",
                    s_n++, s_x[0], s_x[1], s_u[0], s_v[0], s_u[1], s_v[1], s_y, scanlines,
                    s_dx[0], s_dx[1], s_du[0], s_dv[0], s_du[1], s_dv[1]);
        }
    }

    if (!s_texPixels || !g_videoState) {
        AdvanceEdges(scanlines);
        return;
    }
    if (const char* sm = getenv("R1203_SEQMIN")) {
        const uint32_t lo = static_cast<uint32_t>(atoi(sm));
        if (s_r1203Seq >= lo && s_r1203Bmp == 64)
            fprintf(stderr,
                    "R1203_CALL seq=%u scan=%d sy=%d dx=(%#x,%#x) x=(%d,%d) "
                    "verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                    s_r1203Seq, scanlines, (int)s_y,
                    (unsigned)s_dx[0], (unsigned)s_dx[1],
                    (int)(s_x[0] >> 16), (int)(s_x[1] >> 16),
                    (int)s_r1203Verts[0], (int)s_r1203Verts[1],
                    (int)s_r1203Verts[2], (int)s_r1203Verts[3],
                    (int)s_r1203Verts[4], (int)s_r1203Verts[5],
                    (int)s_r1203Verts[6], (int)s_r1203Verts[7]);
    }
    const uint8_t* pal = ShadePalOrIdentity(s_shadePal);
    uint8_t* buf = g_videoState->backBuffer;
    {
        static const bool s_tdbg = []() {
            const char* e = getenv("AB_TEXEL_DBG") ? getenv("AB_TEXEL_DBG") : getenv("AB_721_DBG");
            return e && e[0];
        }();
        static int s_tn = 0;
        static const int kRow = getenv("AB_TEXEL_ROW") ? atoi(getenv("AB_TEXEL_ROW")) : 72;
        const bool rowHit = s_y == kRow;
        if ((s_tdbg || rowHit) && (rowHit ? s_tn < 60 : s_tn < 12)) {
            fprintf(stderr, "SPANTEX n=%d y=%d scan=%d x0=%x x1=%x u0=%x v0=%x u1=%x v1=%x dx0=%x dx1=%x tex=%p\n",
                    s_tn++, s_y, scanlines,
                    (unsigned)s_x[0], (unsigned)s_x[1],
                    (unsigned)s_u[0], (unsigned)s_v[0], (unsigned)s_u[1], (unsigned)s_v[1],
                    (unsigned)s_dx[0], (unsigned)s_dx[1], (const void*)s_texPixels);
            fprintf(stderr, "SPANTEXB n=%d b24=%02x %02x %02x %02x %02x %02x %02x %02x b31=%02x pal=%u,%u,%u,%u,%u,%u,%u\n",
                    s_tn, (const void*)s_texPixels,
                    s_texPixels ? s_texPixels[24] : 0, s_texPixels ? s_texPixels[25] : 0,
                    s_texPixels ? s_texPixels[26] : 0, s_texPixels ? s_texPixels[27] : 0,
                    s_texPixels ? s_texPixels[28] : 0, s_texPixels ? s_texPixels[29] : 0,
                    s_texPixels ? s_texPixels[30] : 0, s_texPixels ? s_texPixels[31] : 0,
                    s_texPixels ? s_texPixels[31] : 0,
                    (unsigned)pal[0], (unsigned)pal[1], (unsigned)pal[2],
                    (unsigned)pal[3], (unsigned)pal[4], (unsigned)pal[5], (unsigned)pal[6]);
        }
    }

    if (scanlines < 1 || s_y > (k3DClipBottom - 1) || s_y + scanlines < 1) {
        AdvanceEdges(scanlines);
        return;
    }
    if (s_y < 0) {
        // ASM: x -= dx * y; u/v += (-y) * du/dv; scanlines += y; y = 0
        // int64: |s_y| can be thousands on Earth-close quads; 32-bit
        // s_dx*s_y wraps and leaves span<=0 (no Earth body pixels).
        const int skip = -s_y;
        s_x[0] = static_cast<int32_t>(s_x[0] - static_cast<int64_t>(s_dx[0]) * s_y);
        s_u[0] = static_cast<int32_t>(s_u[0] + static_cast<int64_t>(skip) * s_du[0]);
        s_v[0] = static_cast<int32_t>(s_v[0] + static_cast<int64_t>(skip) * s_dv[0]);
        s_x[1] = static_cast<int32_t>(s_x[1] - static_cast<int64_t>(s_dx[1]) * s_y);
        s_u[1] = static_cast<int32_t>(s_u[1] + static_cast<int64_t>(skip) * s_du[1]);
        s_v[1] = static_cast<int32_t>(s_v[1] + static_cast<int64_t>(skip) * s_dv[1]);
        scanlines += s_y;
        s_y = 0;
    }

    for (; scanlines != 0 && s_y < k3DClipBottom; s_y++) {
        // Default: edge1 = left (xL), edge0 = right (xR). If edge0.x < edge1.x, swap.
        // R582 candidate law (gated AB_TEXSPAN_EXCL): ASM's span bodies end the
        // run exclusively at the right-edge accumulator; the port truncates both
        // edges and effectively paints one cell past xR. When gated, drop the
        // fractional-truncation overshoot by rounding xR up only when it is a
        // true interior point... simplest faithful form: xR -= ((s_x[0] & 0xFFFF) != 0) ? 0 : 1? No — measure pure
        // exclusive first: treat xR as exclusive always (span-1).
        if (::getenv("AB_TEXSPAN_EXCL") && ::getenv("AB_TEXSPAN_EXCL")[0] == '1') {
            // handled below via excl flag
        }
        const bool texSpanExcl = ::getenv("AB_TEXSPAN_EXCL") && ::getenv("AB_TEXSPAN_EXCL")[0] == '1';
        if (::getenv("AB_TRACE_SPAN2")) {
            static int nsp2 = 0;
            if (nsp2 < 40) { fprintf(stderr, "SPAN2 y=%d fx0=0x%04x fx1=0x%04x e0=%d e1=%d\n",
                s_y, (unsigned)(s_x[0] & 0xFFFF), (unsigned)(s_x[1] & 0xFFFF),
                (int)(s_x[0] >> 16), (int)(s_x[1] >> 16)); ++nsp2; }
        }
        // R589 candidate law (gated AB_TEXSPAN_INTX): ASM's hline length is
        // (b-a) with a/b floored, so an EXACT-integer right edge never paints
        // its own column; the port's inclusive loop does. Drop xR by one only
        // when the right accumulator has zero fraction.

        int xL = static_cast<int>(s_x[1] >> 16);
        int xR = static_cast<int>(s_x[0] >> 16);
        int32_t u = s_u[1];
        int32_t uR = s_u[0];
        int32_t v = s_v[1];
        int32_t vR = s_v[0];
        if (s_x[0] < s_x[1]) {
            xL = static_cast<int>(s_x[0] >> 16);
            xR = static_cast<int>(s_x[1] >> 16);
            u = s_u[0];
            uR = s_u[1];
            v = s_v[0];
            vR = s_v[1];
        }
            if (getenv("R1203_SPAN") && getenv("R1203_SPAN")[0] &&
            getenv("R1203_SPAN")[0] != '0' && s_r1203Bmp == 64 &&
            s_y >= 69 && s_y <= 75 && xL <= 315 && xR >= 290) {
            fprintf(stderr,
                    "R1203_SPAN seq=%u y=%d xL=%d xR=%d drawTex=0x%x "
                    "u0=%d v0=%d u1=%d v1=%d "
                    "verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                    s_r1203Seq, s_y, xL, xR, (unsigned)s_r1203DrawTex,
                    (int)(s_u[0] >> 16), (int)(s_v[0] >> 10),
                    (int)(s_u[1] >> 16), (int)(s_v[1] >> 10),
                    (int)s_r1203Verts[0], (int)s_r1203Verts[1],
                    (int)s_r1203Verts[2], (int)s_r1203Verts[3],
                    (int)s_r1203Verts[4], (int)s_r1203Verts[5],
                    (int)s_r1203Verts[6], (int)s_r1203Verts[7]);
        }
        int span = xR - xL - (texSpanExcl ? 1 : 0);
        if (getenv("AB_TRACE_SPAN") && s_y >= 100 && s_y <= 113) {
            static int ts_n = 0;
            if (ts_n < 60)
                fprintf(stderr, "SPAN y=%d xL=%d xR=%d span=%d u=%d v=%d uR=%d vR=%d edge0x=%d edge1x=%d\n",
                        s_y, xL, xR, span, u, v, uR, vR,
                        (int)(s_x[0] >> 16), (int)(s_x[1] >> 16)), ++ts_n;
        }
        if (xL < 320 && xR > 0 && span > 0) {
            // R1203 candidate law, MEASURED DEAD: the ASM's Tex64NonTrans divides
            // dU/dV by (xRight_int - xLeft_int) = painted-count + 1. Gated test
            // on frame 480: old law 68 px (unchanged), new law 6148 px — a whole
            // rasterizer regression, not a band fix. Default stays the old law;
            // AB_TEXSTEP_SPANP1=1 selects the ASM-literal one for re-checks.
            const int32_t stepDenom =
                (::getenv("AB_TEXSTEP_SPANP1") && ::getenv("AB_TEXSTEP_SPANP1")[0] == '1')
                    ? span + 1 : span;
            // R1203 paint-extent knob (default OFF = current behavior):
            // AB_TEXSPAN_P1 paints one MORE pixel (uR/vR sampled at u+span*step
            // instead of u+(span-1)*step); AB_TEXSPAN_M1 paints one fewer.
            // The dead +1-denominator law produced 6148 px, proving extreme
            // sensitivity here — an extent off-by-one is the surviving theory.
            int32_t paintN = span;
            if (::getenv("AB_TEXSPAN_P1") && ::getenv("AB_TEXSPAN_P1")[0] == '1')
                paintN = span + 1;
            else if (::getenv("AB_TEXSPAN_M1") && ::getenv("AB_TEXSPAN_M1")[0] == '1')
                paintN = span - 1;
            if (paintN < 1) paintN = 1;
            // R1206: ASM FUNC_001650 sites (ffe.asm ~99425-99458) — four idivs:
            //   dU>0 / dV>0: cdq; idiv ebx          (signed trunc toward 0)
            //   dU<=0 / dV<=0: neg; cdq; idiv; neg  (magnitude-then-sign)
            // Under toward-zero `/` those forms are identical (R1031; re-checked
            // R1206). AB_TEXSTEP_SIGNED=1 forces plain signed trunc on both axes
            // for an A/B; default keeps the ASM's neg/idiv/neg pairing.
            static const bool s_texStepSigned = []() {
                const char* e = ::getenv("AB_TEXSTEP_SIGNED");
                return e && e[0] == '1';
            }();
            int32_t dU = uR - u;
            int32_t stepU;
            if (s_texStepSigned)
                stepU = dU / stepDenom;
            else if (dU < 1)
                stepU = -((-dU) / stepDenom);
            else
                stepU = dU / stepDenom;
            int32_t dV = vR - v;
            int32_t stepV;
            if (s_texStepSigned)
                stepV = dV / stepDenom;
            else if (dV < 1)
                stepV = -((-dV) / stepDenom);
            else
                stepV = dV / stepDenom;
            // R1206 span-step dump (default off). Once-cached gate; only fires on
            // bmp64 band rows that cover the residual cells.
            static const bool s_r1206Step = []() {
                const char* e = ::getenv("R1206_STEP");
                return e && e[0] && e[0] != '0';
            }();
            // Prefer the R1205 last-writer olive triangles (shade pal starts at
            // 219/204/206 — Contour pals start ~132 and must not fill the cap).
            if (s_r1206Step && s_r1203Bmp == 64 && s_y >= 71 && s_y <= 75 &&
                xL <= 319 && xR >= 217) {
                // Exact final-writer shade from R1205 WR (pal0==219). Broader
                // olive pals fill the cap before the (404,68) triangle runs.
                const unsigned p0 = pal ? (unsigned)pal[0] : 0u;
                const bool finalWriterPal = (p0 == 219u);
                static int s_r1206StepN = 0;
                if (finalWriterPal && s_r1206StepN < 80) {
                    fprintf(stderr,
                            "R1206_STEP y=%d xL=%d xR=%d span=%d denom=%d "
                            "dU=%d dV=%d stepU=%d stepV=%d signed=%d "
                            "u=%d v=%d uR=%d vR=%d "
                            "verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                            s_y, xL, xR, span, (int)stepDenom,
                            (int)dU, (int)dV, (int)stepU, (int)stepV,
                            (int)s_texStepSigned,
                            (int)u, (int)v, (int)uR, (int)vR,
                            (int)s_r1203Verts[0], (int)s_r1203Verts[1],
                            (int)s_r1203Verts[2], (int)s_r1203Verts[3],
                            (int)s_r1203Verts[4], (int)s_r1203Verts[5],
                            (int)s_r1203Verts[6], (int)s_r1203Verts[7]);
                    ++s_r1206StepN;
                }
            }
            if (xL < 0) {
                // int64: Earth horizon quads start at xL≈-10k; 32-bit
                // xL*stepU wraps and yields empty/corrupt spans.
                u = static_cast<int32_t>(u - static_cast<int64_t>(xL) * stepU);
                v = static_cast<int32_t>(v - static_cast<int64_t>(xL) * stepV);
                span = span + xL;
                paintN = paintN + xL;
                xL = 0;
            }
            if (xR > 320)
                span = 320 - xL;
            if (paintN > span) paintN = span;   // never paint past the clip
            if (span > 0 && paintN > 0) {
                uint8_t* dst = buf + xL + s_y * 320;
                // R609: gate on span EXTENT, not start x. The strip's span
                // starts far left (xL=69) and runs past x197; earlier probes
                // filtered on start-x and so never fired here.
                static int px2y = -1, px2x0 = -1, px2x1 = -1;
                if (px2y < 0) {
                    const char* e2 = getenv("AB_TRACE_PIX2");
                    if (e2 && e2[0]) { int a_=0,b_=0,c_=0;
                        if (sscanf(e2, "%d:%d:%d", &a_, &b_, &c_) == 3) { px2y=a_; px2x0=b_; px2x1=c_; }
                        else { px2y = 62; px2x0 = 158; px2x1 = 197; } }
                    else { px2y = -2; }
                }
                if (px2y >= 0 && s_y == px2y && (xL + span) > px2x0) {
                    int u2 = u, v2 = v;
                    for (int px = 0; px < span; px++) {
                        const int sx2 = xL + px;
                        if (sx2 >= px2x0 && sx2 <= px2x1) {
                            const int off2 = (u2 >> 16) + ((v2 >> kVShift) & kVMask);
                            fprintf(stderr, "PX2 y=%d x=%d u=%d v=%d off=%d texel=%u out=%u pal6=%u\n",
                                    s_y, sx2, (int)(u2 >> 16), (int)(v2 >> kVShift), off2,
                                    (unsigned)s_texPixels[off2], (unsigned)pal[s_texPixels[off2]],
                                    (unsigned)pal[6]);
                        }
                        u2 += stepU; v2 += stepV;
                    }
                }
                if (getenv("AB_TRACE_TEX") && s_y >= 60 && s_y <= 120) {
                    const int t0 = static_cast<int>(u >> 16) +
                        static_cast<int>(static_cast<uint32_t>(v >> kVShift) & kVMask);
                    const uint8_t tl = s_texPixels[t0];
                    fprintf(stderr, "TEX y=%d xL=%d u=%d v=%d off=%d texel=%d out=%u\n",
                            s_y, xL, (int)(u >> 16), (int)(v >> kVShift), t0, (int)tl, pal[tl]);
                }
                // R1205/R1203 watch-list: init once per span; inert path is a
                // single s_r1203WatchOn branch per pixel (no helper call).
                R1203WatchEnsureInit();
                do {
                    const int texOff = static_cast<int>(u >> 16) +
                        static_cast<int>(static_cast<uint32_t>(v >> kVShift) & kVMask);
                    const uint8_t texel = s_texPixels[texOff];
                    jjffe_planet_tex_trace_note(texel);
                    if (getenv("AB_TRACE_TRI") && s_y >= 68 && s_y <= 92 &&
                        pal[0] == 1 && pal[6] == 4) {
                        static int ntr = 0;
                        if (ntr < 12) {
                            fprintf(stderr, "TRI y=%d x=%d off=%d texel=%u out=%u trans=%d\n",
                                    s_y, (int)(dst - buf - (ptrdiff_t)s_y * 320), texOff,
                                    texel, (unsigned)pal[texel], (int)kTrans);
                            ++ntr;
                        }
                    }
                    if (!kTrans || texel != 0) {
                        const uint8_t out = pal[texel];
                        jjffe_planet_fill_trace_note(out);
                        const bool skip0 =
                            out == 0 && getenv("AB_TEX_SKIP0") &&
                            getenv("AB_TEX_SKIP0")[0] == '1';
                        if (!skip0) {
                            if (getenv("AB_TRACE_PIX") && s_y == 105 && (dst - buf) % 320 == 150) {
                                fprintf(stderr, "PIX105 u=%d v=%d off=%d texel=%d out=%u\n",
                                        (int)(u >> 16), (int)(v >> kVShift), texOff, (int)texel, out);
                            }
                            if (s_r1203WatchOn) {
                                const int sx = (int)(dst - buf - (ptrdiff_t)s_y * 320);
                                R1203WatchMaybeLog(sx, (int)s_y, *dst, out, texel,
                                                   (int)(u >> 16), (int)(v >> kVShift),
                                                   texOff, pal, xL, xR);
                            }
                            jjffe::AbPlotStamp((int)(dst - buf - (ptrdiff_t)s_y * 320),
                                               s_y, out, "texasm");
                            *dst = out;
                        }
                    }
                    dst++;
                    u += stepU;
                    v += stepV;
                    span--;
                    paintN--;
                } while (span != 0 && paintN != 0);
            }
        }
        s_x[0] += s_dx[0];
        s_x[1] += s_dx[1];
        s_u[0] += s_du[0];
        s_v[0] += s_dv[0];
        s_u[1] += s_du[1];
        s_v[1] += s_dv[1];
        scanlines--;
    }
    if (scanlines > 0)
        AdvanceEdges(scanlines);
}

void Tex64NonTrans(int scanlines) { TexSpanAsm<6, false>(scanlines); }
void Tex64Trans(int scanlines) { TexSpanAsm<6, true>(scanlines); }
void Tex128NonTrans(int scanlines) { TexSpanAsm<7, false>(scanlines); }
void Tex128Trans(int scanlines) { TexSpanAsm<7, true>(scanlines); }

// DrawTexTriangleInternal — FUNC_001646 (v1,v2,v3,t1,t2,t3 as native pointers).
static void DrawTexTriangleInternalImpl(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                                        const int16_t* t1, const int16_t* t2, const int16_t* t3)
{
    if (!v1 || !v2 || !v3 || !t1 || !t2 || !t3 || !s_texSpanFunc) return;

    const int16_t* pv1 = v1;
    const int16_t* pv2 = v2;
    const int16_t* pv3 = v3;
    const int16_t* pt1 = t1;
    const int16_t* pt2 = t2;
    const int16_t* pt3 = t3;

    while (pv2[1] < pv1[1] || pv3[1] < pv1[1]) {
        const int16_t* tv = pv1;
        const int16_t* tt = pt1;
        pv1 = pv2; pt1 = pt2;
        pv2 = pv3; pt2 = pt3;
        pv3 = tv;  pt3 = tt;
    }

    GenerateTexSteps(0, pv1, pt1, pv2, pt2);
    GenerateTexSteps(1, pv1, pt1, pv3, pt3);
    s_y = pv1[1];
    s_r1203Verts[0] = pv1[0]; s_r1203Verts[1] = pv1[1];
    s_r1203Verts[2] = pv2[0]; s_r1203Verts[3] = pv2[1];
    s_r1203Verts[4] = pv3[0]; s_r1203Verts[5] = pv3[1];
    s_r1203Verts[6] = 0; s_r1203Verts[7] = -1; // mark triangle
    s_texCount++;

    auto tspan = [&](int n, const char* tag) {
        if (getenv("R1203_LAST") && getenv("R1203_LAST")[0] &&
            getenv("R1203_LAST")[0] != '0' &&
            s_y >= 69 && s_y <= 75 && n > 0) {
            const int x0 = static_cast<int>(s_x[0] >> 16);
            const int x1 = static_cast<int>(s_x[1] >> 16);
            if ((x0 >= 250 || x1 >= 250) && (x0 <= 320 || x1 <= 320))
                fprintf(stderr,
                        "R1203_TSPAN tag=%s n=%d sy=%d x=(%d,%d) "
                        "pv=(%d,%d)(%d,%d)(%d,%d) bmp=%d drawTex=0x%x pal0=%u\n",
                        tag, n, (int)s_y, x0, x1,
                        (int)pv1[0], (int)pv1[1], (int)pv2[0], (int)pv2[1],
                        (int)pv3[0], (int)pv3[1], s_r1203Bmp,
                        (unsigned)s_r1203DrawTex,
                        s_shadePal ? (unsigned)s_shadePal[0] : 0u);
        }
        s_texSpanFunc(n);
    };

    if (pv2[1] < pv3[1]) {
        tspan(static_cast<int>(pv2[1]) - static_cast<int>(pv1[1]), "A");
        GenerateTexSteps(0, pv2, pt2, pv3, pt3);
        tspan(static_cast<int>(pv3[1]) - static_cast<int>(pv2[1]), "B");
    } else {
        tspan(static_cast<int>(pv3[1]) - static_cast<int>(pv1[1]), "A");
        GenerateTexSteps(1, pv3, pt3, pv2, pt2);
        tspan(static_cast<int>(pv2[1]) - static_cast<int>(pv3[1]), "B");
    }
    s_texCount--;
}

void DrawTexTriangleShadedUV(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                             const int16_t* t1, const int16_t* t2, const int16_t* t3,
                             const uint8_t* texture, int texSize, bool transparent,
                             const uint8_t shadePal[256])
{
    if (!v1 || !v2 || !v3 || !t1 || !t2 || !t3 || !texture || !g_videoState || !shadePal) return;
    if (texSize != 64 && texSize != 128) return;

    s_texPixels = texture;
    s_shadePal = shadePal;
    if (texSize == 64)
        s_texSpanFunc = transparent ? Tex64Trans : Tex64NonTrans;
    else
        s_texSpanFunc = transparent ? Tex128Trans : Tex128NonTrans;

    DrawTexTriangleInternalImpl(v1, v2, v3, t1, t2, t3);
}

void DrawTexTriangleShaded(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                           uint32_t drawTexWord, const uint8_t shadePal[256])
{
    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(drawTexWord & 0xfff));
    if (!bmp || !shadePal) return;
    const int texSize = bmp->width;
    if (texSize != 64 && texSize != 128) return;
    const bool transparent = (bmp->flags & 1) != 0;
    const int uvMax = texSize - 1;
    const int16_t uv0[2] = {0, 0};
    const int16_t uv1[2] = {static_cast<int16_t>(uvMax), 0};
    const int16_t uv2[2] = {0, static_cast<int16_t>(uvMax)};
    DrawTexTriangleShadedUV(v1, v2, v3, uv0, uv1, uv2, bmp->data, texSize, transparent, shadePal);
}

// DrawTexTriangle - FUNC_001644_DrawTexTriangle (legacy API; prefer DrawTexTriangleShaded)
void DrawTexTriangle(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                     const uint8_t* texture, int texWidth)
{
    if (!texture) return;
    static uint8_t id[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) id[i] = static_cast<uint8_t>(i);
        init = true;
    }
    const int16_t uv0[2] = {0, 0};
    const int16_t uv1[2] = {static_cast<int16_t>(texWidth - 1), 0};
    const int16_t uv2[2] = {0, static_cast<int16_t>(texWidth - 1)};
    DrawTexTriangleShadedUV(v1, v2, v3, uv0, uv1, uv2, texture, texWidth, false, id);
}

// DrawTexTriangleDet - FUNC_001645_DrawTexTriangleDet
void DrawTexTriangleDet(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                        const uint8_t* texture, int texWidth, uint8_t lightLevel)
{
    (void)lightLevel;
    DrawTexTriangle(v1, v2, v3, texture, texWidth);
}

void TexInitState(TexRenderState* state, const uint8_t* texture, const uint8_t* palette, int texSize)
{
    if (!state) return;
    std::memset(state, 0, sizeof(*state));
    state->texture = texture;
    state->palette = palette;
    state->texSize = texSize;
}

// FUNC_001649_DrawTexQuadInternal — rotate until v1 is topmost, span via Tex*.
void DrawTexQuadInternal(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                         const int16_t* v4, const int16_t* t1, const int16_t* t2,
                         const int16_t* t3, const int16_t* t4)
{
    if (!v1 || !v2 || !v3 || !v4 || !t1 || !t2 || !t3 || !t4) return;
    if (!s_texSpanFunc) return;

    const int16_t* pv1 = v1;
    const int16_t* pv2 = v2;
    const int16_t* pv3 = v3;
    const int16_t* pv4 = v4;
    const int16_t* pt1 = t1;
    const int16_t* pt2 = t2;
    const int16_t* pt3 = t3;
    const int16_t* pt4 = t4;

    // Rotate (v,t) until pv1 has the minimum Y (ASM while-loop).
    while (pv2[1] < pv1[1] || pv3[1] < pv1[1] || pv4[1] < pv1[1]) {
        const int16_t* tv = pv1;
        const int16_t* tt = pt1;
        pv1 = pv2; pt1 = pt2;
        pv2 = pv3; pt2 = pt3;
        pv3 = pv4; pt3 = pt4;
        pv4 = tv;  pt4 = tt;
    }

    GenerateTexSteps(0, pv1, pt1, pv2, pt2);
    GenerateTexSteps(1, pv1, pt1, pv4, pt4);
    s_y = pv1[1];
    const uint32_t seq = ++s_r1203Seq;
    if (getenv("R1203_SPAN") && getenv("R1203_SPAN")[0] &&
        getenv("R1203_SPAN")[0] != '0' && s_r1203Bmp == 64 &&
        pv1[1] <= 76 && pv4[1] >= 69 &&
        (std::max(std::max(pv1[0], pv2[0]), std::max(pv3[0], pv4[0])) >= 290)) {
        fprintf(stderr,
                "R1203_QUAD seq=%u top=%d verts=(%d,%d)(%d,%d)(%d,%d)(%d,%d) "
                "sy_set=%d scan0=%d drawTex=0x%x\n",
                seq, (int)pv1[1],
                (int)pv1[0], (int)pv1[1], (int)pv2[0], (int)pv2[1],
                (int)pv3[0], (int)pv3[1], (int)pv4[0], (int)pv4[1],
                (int)s_y, (int)(pv2[1] < pv4[1] ? pv2[1] - pv1[1] : pv4[1] - pv1[1]),
                (unsigned)s_r1203DrawTex);
    }
    s_texCount++; // DATA_007786 nest/counter +1 (ASM before spans)

    auto span = [&](int n, const char* tag) {
        if (getenv("R1203_LAST") && getenv("R1203_LAST")[0] &&
            getenv("R1203_LAST")[0] != '0' && s_r1203Bmp == 64 &&
            s_y >= 69 && s_y <= 75 && n > 0) {
            const int x0 = static_cast<int>(s_x[0] >> 16);
            const int x1 = static_cast<int>(s_x[1] >> 16);
            if ((x0 >= 290 || x1 >= 290) && (x0 <= 320 || x1 <= 320))
                fprintf(stderr,
                        "R1203_QSPAN tag=%s n=%d sy=%d x=(%d,%d) "
                        "pv=(%d,%d)(%d,%d)(%d,%d)(%d,%d) drawTex=0x%x\n",
                        tag, n, (int)s_y, x0, x1,
                        (int)pv1[0], (int)pv1[1], (int)pv2[0], (int)pv2[1],
                        (int)pv3[0], (int)pv3[1], (int)pv4[0], (int)pv4[1],
                        (unsigned)s_r1203DrawTex);
        }
        s_texSpanFunc(n);
    };

    if (pv2[1] < pv4[1]) {
        span(static_cast<int>(pv2[1]) - static_cast<int>(pv1[1]), "A");
        GenerateTexSteps(0, pv2, pt2, pv3, pt3);
        if (pv3[1] < pv4[1]) {
            span(static_cast<int>(pv3[1]) - static_cast<int>(pv2[1]), "B");
            GenerateTexSteps(0, pv3, pt3, pv4, pt4);
            span(static_cast<int>(pv4[1]) - static_cast<int>(pv3[1]), "C");
        } else {
            span(static_cast<int>(pv4[1]) - static_cast<int>(pv2[1]), "B2");
            GenerateTexSteps(1, pv4, pt4, pv3, pt3);
            span(static_cast<int>(pv3[1]) - static_cast<int>(pv4[1]), "C2");
        }
    } else {
        span(static_cast<int>(pv4[1]) - static_cast<int>(pv1[1]), "A");
        GenerateTexSteps(1, pv4, pt4, pv3, pt3);
        if (pv2[1] < pv3[1]) {
            span(static_cast<int>(pv2[1]) - static_cast<int>(pv4[1]), "B");
            GenerateTexSteps(0, pv2, pt2, pv3, pt3);
            span(static_cast<int>(pv3[1]) - static_cast<int>(pv2[1]), "C");
        } else {
            span(static_cast<int>(pv3[1]) - static_cast<int>(pv4[1]), "B2");
            GenerateTexSteps(1, pv3, pt3, pv2, pt2);
            span(static_cast<int>(pv2[1]) - static_cast<int>(pv3[1]), "C2");
        }
    }
    s_texCount--; // ASM −1 on return
}

// UV corner order table — FUNC_001647 / DATA_007791
// corners: 0=(0,0) 1=(max,0) 2=(0,max) 3=(max,max)
static const int kUvOrder[8][4] = {
    {0, 1, 3, 2},
    {1, 3, 2, 0},
    {3, 2, 0, 1},
    {2, 0, 1, 3},
    {0, 2, 3, 1},
    {2, 3, 1, 0},
    {3, 1, 0, 2},
    {1, 0, 2, 3},
};

void DrawTexQuadShaded(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                       const int16_t* v4, uint32_t drawTexWord,
                       const uint8_t* texture, int texSize, bool transparent,
                       const uint8_t shadePal[256])
{
    if (!v1 || !v2 || !v3 || !v4 || !texture || !g_videoState) return;
    if (texSize != 64 && texSize != 128) return;

    s_texPixels = texture;
    s_shadePal = shadePal;
    s_r1203DrawTex = drawTexWord;
    s_r1203Bmp = static_cast<int>(drawTexWord & 0xfff);
    s_r1203Verts[0] = v1[0]; s_r1203Verts[1] = v1[1];
    s_r1203Verts[2] = v2[0]; s_r1203Verts[3] = v2[1];
    s_r1203Verts[4] = v3[0]; s_r1203Verts[5] = v3[1];
    s_r1203Verts[6] = v4[0]; s_r1203Verts[7] = v4[1];
    if (texSize == 64)
        s_texSpanFunc = transparent ? Tex64Trans : Tex64NonTrans;
    else
        s_texSpanFunc = transparent ? Tex128Trans : Tex128NonTrans;

    const int uvMax = texSize - 1;
    const int16_t corners[4][2] = {
        {0, 0},
        {static_cast<int16_t>(uvMax), 0},
        {0, static_cast<int16_t>(uvMax)},
        {static_cast<int16_t>(uvMax), static_cast<int16_t>(uvMax)},
    };
    int uvCase = static_cast<int>((drawTexWord >> 12) & 7);
    const int* order = kUvOrder[uvCase];
    int16_t uv[4][2];
    for (int i = 0; i < 4; i++) {
        uv[i][0] = corners[order[i]][0];
        uv[i][1] = corners[order[i]][1];
    }

    // FUNC_001647: pass UV corners in case order to Internal as t1..t4.
    DrawTexQuadInternal(v1, v2, v3, v4, uv[0], uv[1], uv[2], uv[3]);
}

void DrawTexQuadShadedUV(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                         const int16_t* v4, const int16_t* t1, const int16_t* t2,
                         const int16_t* t3, const int16_t* t4,
                         const uint8_t* texture, int texSize, bool transparent,
                         const uint8_t shadePal[256])
{
    if (!v1 || !v2 || !v3 || !v4 || !t1 || !t2 || !t3 || !t4 || !texture || !g_videoState)
        return;
    if (texSize != 64 && texSize != 128) return;

    s_texPixels = texture;
    s_shadePal = shadePal;
    // UV-explicit path (DrawBandJobRec): bmp index unknown; mark as 0xFFFFFFFE.
    s_r1203DrawTex = 0xfffffffeu;
    s_r1203Bmp = -2;
    s_r1203Verts[0] = v1[0]; s_r1203Verts[1] = v1[1];
    s_r1203Verts[2] = v2[0]; s_r1203Verts[3] = v2[1];
    s_r1203Verts[4] = v3[0]; s_r1203Verts[5] = v3[1];
    s_r1203Verts[6] = v4[0]; s_r1203Verts[7] = v4[1];
    if (texSize == 64)
        s_texSpanFunc = transparent ? Tex64Trans : Tex64NonTrans;
    else
        s_texSpanFunc = transparent ? Tex128Trans : Tex128NonTrans;

    DrawTexQuadInternal(v1, v2, v3, v4, t1, t2, t3, t4);
}

void SelectTexFunc(uint32_t bitmapIndex)
{
    const Bitmap* bmp = BmpIndexToPtr(static_cast<int>(bitmapIndex));
    if (!bmp) return;
    const bool transparent = (bmp->flags & 1) != 0;
    s_texPixels = bmp->data;
    s_r1203Bmp = static_cast<int>(bitmapIndex & 0xfff);
    s_r1203DrawTex = bitmapIndex;
    // R1203b A/B (AB_TEXSPAN_WRAP=1): ASM FUNC_001643_SelectTexFunc sets DATA_009250
    // to the FUNC_00165x wrappers (ffe.asm:98636-98649 — the ONLY writers of that
    // pointer), which call SetTexShadeTable(DATA_009252) on entry to every span. The
    // port assigned the raw rasterizers here, leaving the span body whatever shade
    // table the last binder left behind. Default keeps the pre-R1203b wiring.
    static const bool s_wrap = [] {
        const char* e = std::getenv("AB_TEXSPAN_WRAP");
        return e && e[0] == '1';
    }();
    if (bmp->width == 64) {
        // R1204: one-shot texel dump for the ASM-vs-port bitmap-data comparison
        // (scratch/r1203_dig/tex40_probe_asm.py is the ffeasm twin). Env-gated; inert
        // unless AB_TEXDUMP=<index-hex>. The targeting band's texture is index 0x40.
        static const char* s_texdump = getenv("AB_TEXDUMP");
        static bool s_texdumpDone = false;
        if (s_texdump && s_texdump[0] && !s_texdumpDone &&
            bitmapIndex == static_cast<uint32_t>(strtoul(s_texdump, nullptr, 16))) {
            FILE* f = fopen("scratch/r1203_dig/port_texdump.bin", "wb");
            if (f) { fwrite(bmp->data, 1, static_cast<size_t>(bmp->width) * bmp->height, f); fclose(f); }
            s_texdumpDone = true;
        }
        if (s_wrap)
            s_texSpanFunc = transparent ? &FUNC_001651_Tex64Trans : &FUNC_001650_Tex64NonTrans;
        else
            s_texSpanFunc = transparent ? &Tex64Trans : &Tex64NonTrans;
    } else {
        if (s_wrap)
            s_texSpanFunc = transparent ? &FUNC_001653_Tex128Trans : &FUNC_001652_Tex128NonTrans;
        else
            s_texSpanFunc = transparent ? &Tex128Trans : &Tex128NonTrans;
    }
}

void SetTexShadeTable(const uint8_t* shadePal) { s_shadePal = shadePal; }

// The port storage of the ASM texture mapper's labels (ffedat.asm:28986-29012), for the
// call oracle: 9235 texels (a native pointer), 9236-9248 the edge/span state.
std::uintptr_t TexStateLabelAddr(int id)
{
    switch (id) {
    case 9235: return reinterpret_cast<std::uintptr_t>(&s_texPixels);
    case 9236: return reinterpret_cast<std::uintptr_t>(&s_u[0]);
    case 9237: return reinterpret_cast<std::uintptr_t>(&s_v[0]);
    case 9238: return reinterpret_cast<std::uintptr_t>(&s_u[1]);
    case 9239: return reinterpret_cast<std::uintptr_t>(&s_v[1]);
    case 9240: return reinterpret_cast<std::uintptr_t>(&s_du[0]);
    case 9241: return reinterpret_cast<std::uintptr_t>(&s_dv[0]);
    case 9242: return reinterpret_cast<std::uintptr_t>(&s_du[1]);
    case 9243: return reinterpret_cast<std::uintptr_t>(&s_dv[1]);
    case 9244: return reinterpret_cast<std::uintptr_t>(&s_x[0]);
    case 9245: return reinterpret_cast<std::uintptr_t>(&s_x[1]);
    case 9246: return reinterpret_cast<std::uintptr_t>(&s_dx[0]);
    case 9247: return reinterpret_cast<std::uintptr_t>(&s_dx[1]);
    case 9248: return reinterpret_cast<std::uintptr_t>(&s_y);
    }
    return 0;
}

void DrawTexQuad(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                 const uint8_t* texture, int texWidth)
{
    static uint8_t id[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) id[i] = static_cast<uint8_t>(i);
        init = true;
    }
    DrawTexQuadShaded(v1, v2, v3, v4, 0, texture, texWidth, false, id);
}

void DrawTexQuadDet(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                    const uint8_t* texture, int texWidth, uint8_t lightLevel)
{
    (void)lightLevel;
    DrawTexQuad(v1, v2, v3, v4, texture, texWidth);
}

void DrawTexTriangleDetRasterize(intptr_t v1, intptr_t v2, intptr_t v3,
                                 intptr_t t1, intptr_t t2, intptr_t t3,
                                 const uint8_t shadePal[256])
{
    if (!s_texSpanFunc || !s_texPixels || !g_videoState || !shadePal)
        return;
    s_shadePal = shadePal;
    DrawTexTriangleInternalImpl(reinterpret_cast<const int16_t*>(v1),
                                reinterpret_cast<const int16_t*>(v2),
                                reinterpret_cast<const int16_t*>(v3),
                                reinterpret_cast<const int16_t*>(t1),
                                reinterpret_cast<const int16_t*>(t2),
                                reinterpret_cast<const int16_t*>(t3));
}

void DrawTexQuadDetRasterize(intptr_t v1, intptr_t v2, intptr_t v3, intptr_t v4,
                             intptr_t t1, intptr_t t2, intptr_t t3, intptr_t t4,
                             const uint8_t shadePal[256])
{
    if (!s_texSpanFunc || !s_texPixels || !g_videoState || !shadePal)
        return;
    s_shadePal = shadePal;
    const int16_t* a = reinterpret_cast<const int16_t*>(v1);
    const int16_t* b = reinterpret_cast<const int16_t*>(v2);
    const int16_t* c = reinterpret_cast<const int16_t*>(v3);
    const int16_t* d = reinterpret_cast<const int16_t*>(v4);
    s_r1203Verts[0] = a[0]; s_r1203Verts[1] = a[1];
    s_r1203Verts[2] = b[0]; s_r1203Verts[3] = b[1];
    s_r1203Verts[4] = c[0]; s_r1203Verts[5] = c[1];
    s_r1203Verts[6] = d[0]; s_r1203Verts[7] = d[1];
    DrawTexQuadInternal(a, b, c, d,
                        reinterpret_cast<const int16_t*>(t1),
                        reinterpret_cast<const int16_t*>(t2),
                        reinterpret_cast<const int16_t*>(t3),
                        reinterpret_cast<const int16_t*>(t4));
}

// Phase 74: Implemented missing named rendering functions

// FUNC_001603_CalcLineGrad (ffe.asm:94644) — write 16.16 dx/dy grad + start
// x<<16; return dy. Oracle is the ASM, not a "normalized lower-Y" rewrite:
//   JUMP_005920: dy==0 → eax=0x7fffffff; if (x1-x0)<0 → eax+=2 (= -0x7fffffff)
//   JUMP_005921: *gradOut=eax; *xFpOut = x0<<16 (always the first endpoint)
// Horizontal |grad| is intentionally 0x7fffffff (not 0x7fff). Callers pass
// dy=y1-y0 into DrawPolySegment, so dy==0 never multiplies that sentinel.
static int QuadCalcLineGrad(int32_t* gradOut, int32_t* xFpOut,
                            int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t g;
    if (y1 == y0) {
        g = 0x7fffffff;
        if (x1 - x0 < 0)
            g = -0x7fffffff;
    } else if (y1 - y0 < 0) {
        if (x1 - x0 < 0)
            g = ((x1 - x0) * -0x10000) / -(y1 - y0);
        else
            g = -(((x1 - x0) * 0x10000) / -(y1 - y0));
    } else if (x1 - x0 < 0) {
        g = -(((x1 - x0) * -0x10000) / (y1 - y0));
    } else {
        g = ((x1 - x0) * 0x10000) / (y1 - y0);
    }
    *gradOut = g;
    // ASM shl 16; unsigned view (UBSan: negative x0).
    *xFpOut = static_cast<int32_t>(static_cast<uint32_t>(x0) << 16);
    return y1 - y0;
}

// FUNC_001602_DrawPolySegment — advance two 16.16 edges for `dy` rows and
// paint exclusive-right spans via DrawHLine (ASM hline callback).
static void QuadDrawPolySegment(int32_t* xFpL, int32_t* xFpR,
                                int32_t gradL, int32_t gradR, int dy,
                                int32_t* yInOut, uint8_t color)
{
    int32_t y = *yInOut;
    if (dy < 0 || y > 0x9d || dy + y < 1) {
        *xFpL += dy * gradL;
        *xFpR += dy * gradR;
        *yInOut += dy;
        return;
    }
    int32_t xl = *xFpL;
    int32_t xr = *xFpR;
    if (y < 0) {
        xl -= y * gradL;
        xr -= y * gradR;
        dy += y;
        y = 0;
    }
    for (; dy != 0 && y < 0x9e; y++) {
        const int32_t a = xl >> 0x10;
        const int32_t b = xr >> 0x10;
        if (b < a)
            DrawHLine(b, y, a - b, color);
        else
            DrawHLine(a, y, b - a, color);
        xl += gradL;
        xr += gradR;
        dy--;
    }
    if (dy != 0) {
        xl += gradL * dy;
        xr += gradR * dy;
        y += dy;
    }
    *xFpL = xl;
    *xFpR = xr;
    *yInOut = y;
}

// DrawQuadInternal — FUNC_001608 (ffe.asm:95707)
// Min-Y vertex rotation + CalcLineGrad edge DDAs + DrawPolySegment span fill.
// Not a triangle-pair split and not a per-scanline integer edge walk.
void DrawQuadInternal(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                      const int16_t* v4, uint8_t color, uint32_t flags)
{
    (void)flags;
    if (getenv("AB_DUMP_QUAD")) {
        static int nq = 0;
        if (nq < 30)
            fprintf(stderr, "QUAD color=%u v=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
                    color, v1[0],v1[1], v2[0],v2[1], v3[0],v3[1], v4[0],v4[1]), ++nq;
    }
    if (!v1 || !v2 || !v3 || !v4 || !g_videoState) return;

    const int16_t* p1 = v1;
    const int16_t* p2 = v2;
    const int16_t* p3 = v3;
    const int16_t* p4 = v4;

    int16_t minY = p1[1];
    if (p2[1] < minY) minY = p2[1];
    if (p3[1] < minY) minY = p3[1];
    if (p4[1] < minY) minY = p4[1];

    // JUMP_006063: rotate until p4 is a topmost vertex.
    do {
        const int16_t* t = p1;
        p1 = p2;
        p2 = p3;
        p3 = p4;
        p4 = t;
    } while (minY < p4[1]);

    const int16_t* top = p4;
    const int16_t* n1 = p1;
    const int16_t* n2 = p2;
    const int16_t* n3 = p3;

    int32_t y = static_cast<int32_t>(top[1]);
    int32_t gradL = 0, xFpL = 0, gradR = 0, xFpR = 0;
    int dyL = QuadCalcLineGrad(&gradL, &xFpL, (int)*top, (int)top[1], (int)*n1, (int)n1[1]);
    int dyR = QuadCalcLineGrad(&gradR, &xFpR, (int)*top, (int)top[1], (int)*n3, (int)n3[1]);

    if (n1[1] < n3[1]) {
        QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyL, &y, color);
        dyR -= dyL;
        dyL = QuadCalcLineGrad(&gradL, &xFpL, (int)*n1, (int)n1[1], (int)*n2, (int)n2[1]);
        if (n2[1] < n3[1]) {
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyL, &y, color);
            dyL = QuadCalcLineGrad(&gradL, &xFpL, (int)*n2, (int)n2[1], (int)*n3, (int)n3[1]);
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyL, &y, color);
        } else {
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyR, &y, color);
            dyR = QuadCalcLineGrad(&gradR, &xFpR, (int)*n3, (int)n3[1], (int)*n2, (int)n2[1]);
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyR, &y, color);
        }
    } else {
        QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyR, &y, color);
        dyL -= dyR;
        dyR = QuadCalcLineGrad(&gradR, &xFpR, (int)*n3, (int)n3[1], (int)*n2, (int)n2[1]);
        if (n1[1] < n2[1]) {
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyL, &y, color);
            dyL = QuadCalcLineGrad(&gradL, &xFpL, (int)*n1, (int)n1[1], (int)*n2, (int)n2[1]);
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyL, &y, color);
        } else {
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyR, &y, color);
            dyR = QuadCalcLineGrad(&gradR, &xFpR, (int)*n2, (int)n2[1], (int)*n1, (int)n1[1]);
            QuadDrawPolySegment(&xFpL, &xFpR, gradL, gradR, dyR, &y, color);
        }
    }
}

// DrawQuadVClip — Vertically-clipped quad drawing (FUNC_001606)
void DrawQuadVClip(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                   const int16_t* v4, uint8_t color, uint32_t flags)
{
    DrawQuadInternal(v1, v2, v3, v4, color, flags);
}

// DrawPixelToBuf — Write pixel to specified buffer (FUNC_001629)
void DrawPixelToBuf(uint8_t* buffer, int x, int y, uint8_t color)
{
    if (x >= 0 && x < 320 && y >= 0 && y < 200) {
        buffer[y * 320 + x] = color;
    }
}

// DrawBoxToBufMasked — Draw box to buffer with mask (FUNC_001628)
void DrawBoxToBufMasked(uint8_t* buffer, int x, int y, int w, int h, uint8_t color, uint8_t mask)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < 320 && py >= 0 && py < 200) {
                uint8_t existing = buffer[py * 320 + px];
                buffer[py * 320 + px] = (existing & ~mask) | (color & mask);
            }
        }
    }
}

// CharAdvanceWidth — Font character advance width lookup (FUNC_001597)
int32_t CharAdvanceWidth(char ch)
{
    unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || (uint32_t)(c - 0x20) >= 0xE0) return 0;
    const uint8_t* blob = data::GetDataBlob();
    if (!blob) return 8;
    return blob[data::OFFSET_DATA_007750 + (c - 0x20) * 10 + 9];
}

// FF80Null — String init for bytecode system (FUNC_001342)
char* FF80Null(char* buffer)
{
    strcpy(buffer, "MODULE STRING");
    return buffer + strlen("MODULE STRING") + 1;
}

} // namespace jjffe
