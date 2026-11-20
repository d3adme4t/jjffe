#ifndef JJFFE_DRAW_H
#define JJFFE_DRAW_H

#include <cstdint>

namespace jjffe {

// Forward declarations
struct VideoState;

// Get back buffer pointer
uint8_t* GetBackBuffer();

// Basic primitives
void DrawPixel(int x, int y, uint8_t color);
void DrawHLine(int x, int y, int length, uint8_t color);
void DrawVLine(int x, int y, int length, uint8_t color);
void DrawLine(int x1, int y1, int x2, int y2, uint8_t color);

// Rectangle drawing
void DrawRect(int x, int y, int width, int height, uint8_t color);
void DrawBox(int x1, int y1, int x2, int y2, uint8_t color);

// Character rendering (returns glyph advance width, ASM FUNC_001598 glyph[9])
int DrawChar(char c, uint8_t color, int x, int y, int startY, int endY);
void DrawString(const char* str, uint8_t color, int x, int y);
int MeasureStringWidth(const char* str);

// Polygon rendering
void DrawTriangle(const int16_t* v1, const int16_t* v2, const int16_t* v3, uint8_t color);
void DrawQuad(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4, uint8_t color);

// Z-buffered triangle rendering (Phase 40B)
// v1, v2, v3 are arrays of [x, y, z] where z is depth
void DrawTriangleZ(const int16_t* v1, const int16_t* v2, const int16_t* v3, uint8_t color);

// Circle rendering
void DrawCircle(int centerX, int16_t* centerY, int diameter, uint8_t color);

// Sprite rendering
void DrawSprite(int x, int y, const uint8_t* spriteData, int width, int height);
void DrawParticle(int x, int y, uint8_t color);
void DrawParticleClipped(int x, int y, uint8_t color);

// Scanner/radar drawing
void DrawScannerStalk(int centerX, int centerY, int radius, uint8_t color);
void DrawUpStalk(int x, int y, int length, uint8_t color);
void DrawDownStalk(int x, int y, int length, uint8_t color);

// Shadowed text (returns glyph advance width)
int DrawShadowedChar(char c, uint8_t color, int x, int y, int startY, int endY);

// Clipping helpers
void DrawHLineClip(int x, int y, int length, uint8_t color);
void DrawHLineConClip(int x, int y, int length, uint8_t color);

// Polygon segment drawing
void DrawPolySegment(int x1, int y1, int x2, int y2, uint8_t color);

// Spans drawing (for textured polygons)
void DrawSpans(int y, int x1, int x2, const uint8_t* texture, int texX1, int texX2);

// Textured triangle/quad
void DrawTexTriangleShaded(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                           uint32_t drawTexWord, const uint8_t shadePal[256]);
void DrawTexTriangleShadedUV(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                             const int16_t* t1, const int16_t* t2, const int16_t* t3,
                             const uint8_t* texture, int texSize, bool transparent,
                             const uint8_t shadePal[256]);
void DrawTexTriangle(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                     const uint8_t* texture, int texWidth);
void DrawTexTriangleDet(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                        const uint8_t* texture, int texWidth, uint8_t lightLevel);
void DrawTexQuad(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                 const uint8_t* texture, int texWidth);
void DrawTexQuadDet(const int16_t* v1, const int16_t* v2, const int16_t* v3, const int16_t* v4,
                    const uint8_t* texture, int texWidth, uint8_t lightLevel);

// Buffer management utilities (Phase 19A)
void ClearBuf();
void ClearUpperBuf();
void ZeroUpperBuf();

// Render-state background colour (DATA_009222, set by FUNC_001579) — the ASM
// FUNC_001618_ClearUpperBuf memsets the upper 50560 bytes with this value.
uint32_t GetRenderFillColor();
// FUNC_001330 tail: re-resolve the fill byte through the rebuilt DPal tables.
void SetRenderFillColor(uint32_t v);
// FUNC_001330 entry: the record's rgb16 (FUNC_001579 formula) + cell mark.
uint32_t ComputeRenderFillRgb16();
// Game title clock (DATA_008804 low dword) — read-only, for A/B diagnostics.
uint32_t GetGameClock();
// AB_1F_DPAL_MARKS=1: dump the per-frame DPal mark grid + attribution log
// (mirrors tools/ffe_dpal_marks.gdb.tpl on the ASM side).
void DPalMarksDumpIfTarget();

// Pointer/cursor utilities (Phase 19B)
void TogglePtr();

// Texture mapping (Phase 18E)
// Texture rendering state for scanline texture mapping
struct TexRenderState {
    int32_t u0, v0;          // Start texture coordinates (16.16 fixed-point)
    int32_t u1, v1;          // End texture coordinates (16.16 fixed-point)
    int32_t du, dv;          // Texture coordinate deltas per scanline
    int32_t x0, x1;          // Start/end X positions
    int32_t y;               // Current scanline Y position
    const uint8_t* texture;  // Texture data pointer
    const uint8_t* palette;  // Palette for texture lookup
    int texSize;             // Texture size (64 or 128)
};

// Initialize texture rendering state
void TexInitState(TexRenderState* state, const uint8_t* texture, const uint8_t* palette, int texSize);

// Texture mapping functions (scanline-based)
void Tex64NonTrans(int scanlines);  // 64x64 non-transparent texture
void Tex64Trans(int scanlines);     // 64x64 transparent texture (skips color 0)
void Tex128NonTrans(int scanlines); // 128x128 non-transparent texture
void Tex128Trans(int scanlines);    // 128x128 transparent texture (skips color 0)

// FUNC_001650..001653 (ASM span wrappers, ffe.asm:98636-98649): the span pointer
// DATA_009250 is set to THESE in the ASM's FUNC_001643_SelectTexFunc, so every span
// re-binds the DATA_009252 shade table on entry. Defined in src/core/core.cpp.
void FUNC_001650_Tex64NonTrans(int n);
void FUNC_001651_Tex64Trans(int n);
void FUNC_001652_Tex128NonTrans(int n);
void FUNC_001653_Tex128Trans(int n);

// Texture pipeline functions (Phase 74)
void SelectTexFunc(uint32_t bitmapIndex);
void SetTexShadeTable(const uint8_t* shadePal);
std::uintptr_t TexStateLabelAddr(int id);
// Edge state GenerateTexSteps writes (DATA_009236..9247): u, v, du, dv, x, dx,
// two edges each (test support).
void GetTexEdgeState(int32_t out[12]);
void GenerateTexSteps(int edge, const int16_t* vStart, const int16_t* tStart,
                      const int16_t* vEnd, const int16_t* tEnd);
// FUNC_001646 — Det rasterizer (SelectTexFunc must run first).
void DrawTexTriangleDetRasterize(intptr_t v1, intptr_t v2, intptr_t v3,
                                 intptr_t t1, intptr_t t2, intptr_t t3,
                                 const uint8_t shadePal[256]);
// FUNC_001648/001649 — Det quad rasterizer (SelectTexFunc must run first).
void DrawTexQuadDetRasterize(intptr_t v1, intptr_t v2, intptr_t v3, intptr_t v4,
                             intptr_t t1, intptr_t t2, intptr_t t3, intptr_t t4,
                             const uint8_t shadePal[256]);
void DrawTexQuadInternal(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                         const int16_t* v4, const int16_t* t1, const int16_t* t2,
                         const int16_t* t3, const int16_t* t4);

// FUNC_001647 + 001649 + Tex64/128 with shade palette (DATA_009252).
// v1..v4 are screen [x,y]; drawTexWord supplies UV case ((word>>12)&7) and
// is otherwise unused. texture is a power-of-two shade-index map.
void DrawTexQuadShaded(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                       const int16_t* v4, uint32_t drawTexWord,
                       const uint8_t* texture, int texSize, bool transparent,
                       const uint8_t shadePal[256]);
// Same as DrawTexQuadShaded but with caller-supplied UV corners (pair flips,
// Asp n8 case override, dig knobs). Prefer this from ship_renderer.
void DrawTexQuadShadedUV(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                         const int16_t* v4, const int16_t* t1, const int16_t* t2,
                         const int16_t* t3, const int16_t* t4,
                         const uint8_t* texture, int texSize, bool transparent,
                         const uint8_t shadePal[256]);
void DrawQuadInternal(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                      const int16_t* v4, uint8_t color, uint32_t flags);
void DrawQuadVClip(const int16_t* v1, const int16_t* v2, const int16_t* v3,
                   const int16_t* v4, uint8_t color, uint32_t flags);
void DrawPixelToBuf(uint8_t* buffer, int x, int y, uint8_t color);
void DrawBoxToBufMasked(uint8_t* buffer, int x, int y, int w, int h, uint8_t color, uint8_t mask);
int32_t CharAdvanceWidth(char ch);
char* FF80Null(char* buffer);

// Global texture rendering state (used by Tex functions)
extern TexRenderState g_texState;

} // namespace jjffe

#endif // JJFFE_DRAW_H
