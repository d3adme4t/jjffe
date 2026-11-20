#include <cstdlib>
#include <cassert>
#include "jjffe/string.h"
#include "jjffe/core/utils.h"
#include "jjffe/draw.h"
#include "jjffe/video.h"
#include "jjffe/data_globals.h"
#include "jjffe/core/gameplay.h"
#include <cstring>
#include <cstdio>

namespace jjffe {

// Global text write state
TextWriteState g_textState = {};

// Global FF30 parameter array
int32_t g_ff30Params[8] = {};

uint8_t* FUNC_001377_StringConvertFraction(char* param_1, uint32_t param_2, int32_t param_3);
void FUNC_001357_StringConvertDateNum(uint32_t, uint32_t*, uint32_t*, int32_t*);
char* FUNC_001358_StringConvertDate(char*, uint32_t, char);
char* FUNC_001363_StringConvertTime(char*, uint32_t);
uint8_t* FUNC_001356_FF30WriteDateYearD8807(intptr_t);
char* FUNC_001542(char* param_1, uint32_t param_2);
intptr_t FUNC_001343_StringExpandFFCodeInternal(intptr_t param_1, uint32_t param_2);

// Month strings (ffedat DATA_007652 -> DATA_007657..7669); [0] is the
// out-of-range entry.
static const char* const MONTH_NAMES[13] = {
    "non", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

uint32_t DATA_007776[16] = {
    0xf, 0x10, 0x10, 0x5f, 0x11, 0xf, 0x12, 0xf,
    0x43, 0xf, 0xf, 0x12, 0xf, 0x43, 0x10, 0xf
};

// Character width table (simplified - in original this is DATA_007648)
static const int8_t CHAR_WIDTHS[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    4, 3, 5, 7, 6, 8, 7, 3, 4, 4, 6, 6, 3, 5, 3, 5,
    6, 4, 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 6, 6, 6, 6,
    8, 7, 6, 6, 6, 6, 6, 6, 7, 7, 6, 6, 9, 8, 7, 6,
    7, 6, 5, 6, 6, 7, 7, 8, 7, 7, 7, 4, 5, 4, 6, 6,
    4, 6, 6, 6, 6, 5, 6, 6, 3, 4, 5, 3, 9, 6, 6, 6,
    6, 6, 6, 6, 5, 6, 6, 7, 6, 6, 6, 4, 3, 4, 6, 0,
    // Rest filled with default width
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6
};

// StringExpand - FUNC_001348_StringExpand
void StringExpand(uint8_t* dst, const uint8_t* src)
{
    while (true) {
        uint8_t ch = *src++;
        *dst++ = ch;
        
        if (ch == 0) break;
        
        if (ch & 0x80) {
            if (ch == CHAR_CODE_FD) {
                // 2-byte code: replace with 0x1E and copy next byte
                dst[-1] = 0x1E;
                *dst++ = *src++;
            }
            else if (ch == CHAR_CODE_FE) {
                // 3-byte code: replace with 0x1F and copy next 2 bytes
                dst[-1] = 0x1F;
                *dst++ = *src++;
                *dst++ = *src++;
            }
            else if (ch == CHAR_CODE_FF) {
                // Complex code: call internal expander
                uint16_t code = (src[0] << 8) | src[1];
                src += 2;
                dst = StringExpandFFCodeInternal(dst - 1, code);
            }
        }
    }

}

// FF30 dispatch table — maps (code - 0x3000) to formatting function.
// Order MUST match ffedat.asm DATA_007652_PLUS_52 (the original table).
// Index 0 = code 0x3000, index 1 = 0x3001, ...
using FF30WriteFunc = void(*)(char*);

static const FF30WriteFunc g_ff30Dispatch[] = {
    /* 0x00 */ nullptr,                // 0x3000 FF30Null
    /* 0x01 */ FF30WriteIntV1,         // 0x3001
    /* 0x02 */ FF30WriteIntV2,         // 0x3002
    /* 0x03 */ FF30WriteIntV3,         // 0x3003
    /* 0x04 */ FF30Write1dpFracV1,     // 0x3004
    /* 0x05 */ FF30Write1dpFracV2,     // 0x3005
    /* 0x06 */ FF30Write1dpFracV3,     // 0x3006
    /* 0x07 */ FF30Write2dpFracV1,     // 0x3007
    /* 0x08 */ FF30Write2dpFracV2,     // 0x3008
    /* 0x09 */ FF30Write2dpFracV3,     // 0x3009
    /* 0x0A */ FF30Write3dpFracV1,     // 0x300A
    /* 0x0B */ FF30Write3dpFracV2,     // 0x300B
    /* 0x0C */ FF30Write3dpFracV3,     // 0x300C
    /* 0x0D */ FF30WriteDateYearV1,    // 0x300D
    /* 0x0E */ FF30WriteDateYearV2,    // 0x300E
    /* 0x0F */ FF30WriteDateYearV3,    // 0x300F
    /* 0x10 */ FF30WriteTimeV1,        // 0x3010
    /* 0x11 */ FF30WriteTimeV2,        // 0x3011
    /* 0x12 */ FF30WriteTimeV3,        // 0x3012
    /* 0x13 */ FF30WriteHexIntV1,      // 0x3013
    /* 0x14 */ FF30WriteHexIntV2,      // 0x3014
    /* 0x15 */ FF30WriteHexIntV3,      // 0x3015
    /* 0x16 */ FF30WriteStringV4,      // 0x3016
    /* 0x17 */ FF30WriteStringV5,      // 0x3017
    /* 0x18 */ FF30WriteFFCodeV1,      // 0x3018
    /* 0x19 */ FF30WriteFFCodeV2,      // 0x3019
    /* 0x1A */ FF30WriteFFCodeV3,      // 0x301A
    /* 0x1B */ FF30WriteFFCodeV4,      // 0x301B
    /* 0x1C */ FF30WriteFFCodeV5,      // 0x301C
    /* 0x1D */ FF30WriteIntV4,         // 0x301D
    /* 0x1E */ FF30WriteIntV5,         // 0x301E
    /* 0x1F */ FF30WriteTonsV1,        // 0x301F
    /* 0x20 */ FF30WriteTonsV2,        // 0x3020
    /* 0x21 */ FF30WriteTonsV3,        // 0x3021
    /* 0x22 */ FF30WriteWrapStringV4,  // 0x3022
    /* 0x23 */ FF30WriteSpeedV1,       // 0x3023
    /* 0x24 */ FF30WriteSpeedV2,       // 0x3024
    /* 0x25 */ FF30WriteSpeedV3,       // 0x3025
    /* 0x26 */ FF30WriteDateV1,        // 0x3026
    /* 0x27 */ FF30WriteDateV2,        // 0x3027
    /* 0x28 */ FF30WriteDateV3,        // 0x3028
    /* 0x29 */ FF30WriteDateYearD8807, // 0x3029
    // Additional slots are nullptr (no-op)
};
constexpr int FF30_DISPATCH_COUNT = sizeof(g_ff30Dispatch) / sizeof(g_ff30Dispatch[0]);
constexpr uint16_t FF30_CODE_BASE = 0x3000;

// StringExpandFFCodeInternal - FUNC_001343_StringExpandFFCodeInternal
uint8_t* StringExpandFFCodeInternal(uint8_t* dst, uint16_t code)
{
    // Three-tier dispatch matching original FUNC_001343:
    // Tier 1: 0x3000-0x3FFF → dispatch table indexed by (code - 0x3000)
    // Tier 2: 0x4000-0x7FFF → secondary handler (stub: return dst)
    // Tier 3: 0x8000+       → high-index dispatch (stub: return dst)

    if (code >= FF30_CODE_BASE) {
        int index = code - FF30_CODE_BASE;
        if (index < FF30_DISPATCH_COUNT && g_ff30Dispatch[index] != nullptr) {
            // Save current destination position (function will write to it)
            char* out = reinterpret_cast<char*>(dst);
            g_ff30Dispatch[index](out);
            // Advance dst past what the function wrote
            dst = reinterpret_cast<uint8_t*>(out + strlen(out));
        }
        // else: slot is nullptr → no-op, return dst unchanged
    }
    else if (code >= 0x4000 && code < 0x8000) {
        // Tier 2: 0x4000-0x7FFF range
        // Original calls DATA_007640 handler — stub for now (no known callers)
        // Return dst unchanged (no expansion)
    }
    else if (code >= 0x8000) {
        // Tier 3: 0x8000+ range
        // Original indexes table DATA_007649 by (code >> 9) & 0x3F
        // Stub for now — return dst unchanged
    }

    return dst;
}

// StringExpandFFCode - FUNC_001344_StringExpandFFCode
uint8_t* StringExpandFFCode(uint8_t* dst, uint16_t code)
{
    return reinterpret_cast<uint8_t*>(StringExpandFFCodeInternal(dst, code));
}

// StringExpandFF40Code - FUNC_001345_StringExpandFF40Code
uint8_t* StringExpandFF40Code(uint8_t* dst, uint8_t param)
{
    // Handle 0x40-prefixed codes
    *dst++ = '[';
    *dst++ = param + '0';
    *dst++ = ']';
    *dst = 0;
    return dst;
}

// StringExpandFF80Code - FUNC_001346_StringExpandFF80Code
uint8_t* StringExpandFF80Code(uint8_t* dst, uint8_t param)
{
    // Handle 0x80-prefixed codes
    *dst++ = '<';
    *dst++ = param + '0';
    *dst++ = '>';
    *dst = 0;
    return dst;
}

// StringExpandArrayIndex - FUNC_001347_StringExpandArrayIndex
uint8_t* StringExpandArrayIndex(uint8_t* dst, uint8_t index)
{
    // Expand array index reference
    *dst++ = '#';
    if (index >= 10) {
        *dst++ = (index / 10) + '0';
    }
    *dst++ = (index % 10) + '0';
    *dst = 0;
    return dst;
}

// StringConvertDateNum - FUNC_001357_StringConvertDateNum (ffe.asm:84342):
// 1461-day cycles from a leap year 0, month 1-based.
void StringConvertDateNum(uint32_t dateValue, uint32_t* day, uint32_t* month, int32_t* year)
{
    FUNC_001357_StringConvertDateNum(dateValue, day, month, year);
}

// StringConvertMonth - FUNC_001359_StringConvertMonth
const char* StringConvertMonth(uint32_t month)
{
    if (month < 1 || month > 12) {
        return MONTH_NAMES[0];
    }
    return MONTH_NAMES[month];
}

// StringConvertDate - FUNC_001358_StringConvertDate (ffe.asm:84443)
char* StringConvertDate(char* buffer, uint32_t dateValue, bool includeYear)
{
    FUNC_001358_StringConvertDate(buffer, dateValue, includeYear ? 1 : 0);
    return buffer + strlen(buffer);
}

// StringConvertTime - FUNC_001363_StringConvertTime (ffe.asm:84570): 0xc22e ticks a second.
char* StringConvertTime(char* buffer, uint32_t timeValue)
{
    return FUNC_001363_StringConvertTime(buffer, timeValue);
}

// StringConvertFraction - FUNC_001377_StringConvertFraction
char* StringConvertFraction(char* buffer, int32_t value, int32_t divisor, int decimalPlaces)
{
    if (divisor == 0) divisor = 1;
    
    int32_t integerPart = value / divisor;
    int32_t remainder = value % divisor;
    
    if (remainder < 0) remainder = -remainder;
    
    // Calculate decimal part
    int32_t decimalPart = 0;
    int32_t multiplier = 1;
    for (int i = 0; i < decimalPlaces; i++) {
        multiplier *= 10;
    }
    
    decimalPart = (remainder * multiplier) / divisor;
    
    if (decimalPlaces == 0) {
        sprintf(buffer, "%d", integerPart);
    } else {
        sprintf(buffer, "%d.%0*d", integerPart, decimalPlaces, decimalPart);
    }
    
    return buffer + strlen(buffer);
}

// StringConvertSpeed - FUNC_001393_StringConvertSpeed
// FUNC_001377 writes the number and returns one past its terminator; the ASM
// then StringExpands DATA_007646 -> DATA_007653 " km{" or DATA_007647 ->
// DATA_007654 " km|" over that terminator (font '{' = h^-1, '|' = s^-1).
char* StringConvertSpeed(char* buffer, int32_t speed)
{
    static const uint8_t kKmPerHour[] = " km{";
    static const uint8_t kKmPerSec[] = " km|";
    if (HudPre443()) {
        if (speed < 0x4705E)
            return StringConvertFraction(buffer, (speed * 0x156) / 100, 100, 1);
        return StringConvertFraction(buffer, speed / 0x69, 100, 2);
    }
    uint8_t* end;
    const uint8_t* unit;
    if (speed < 0x4705E) {
        const int32_t scaled = static_cast<int32_t>(static_cast<uint32_t>(speed) * 0x156u);  // imul wraps
        end = FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(scaled / 100), 1);
        unit = kKmPerHour;
    } else {
        end = FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(speed / 0x69), 2);
        unit = kKmPerSec;
    }
    StringExpand(end - 1, unit);
    return buffer + strlen(buffer);
}

// StringWrap - FUNC_001398_StringWrap
uint8_t* StringWrap(uint8_t* str, int maxWidth)
{
    // Simplified word wrapping
    int currentWidth = 0;
    uint8_t* lastSpace = nullptr;
    uint8_t* p = str;
    
    while (*p) {
        if (*p == CHAR_SPACE) {
            lastSpace = p;
        }
        
        int charWidth = CHAR_WIDTHS[*p];
        currentWidth += charWidth;
        
        if (currentWidth > maxWidth && lastSpace) {
            // Wrap at last space
            *lastSpace = CHAR_NEWLINE;
            currentWidth = 0;
            p = lastSpace + 1;
            lastSpace = nullptr;
        } else {
            p++;
        }
    }
    
    return p;
}

// StringExpandWrapFFCode - FUNC_001397_StringExpandWrapFFCode
uint8_t* StringExpandWrapFFCode(uint8_t* dst, const uint8_t* src, int maxWidth)
{
    // First expand FF codes
    StringExpand(dst, src);
    
    // Then wrap if maxWidth specified
    if (maxWidth > 0) {
        StringWrap(dst, maxWidth);
    }
    
    return dst;
}

// StringDrawWrapShadow - FUNC_001399_StringDrawWrapShadow (with line count)
int StringDrawWrapShadowCount(const uint8_t* str, int x, int y, uint8_t color, int maxWidth)
{
    static uint8_t wrapBuffer[512];
    StringExpandWrapFFCode(wrapBuffer, str, maxWidth);
    
    // Count lines
    int lineCount = 1;
    for (int i = 0; wrapBuffer[i] != 0; i++) {
        if (wrapBuffer[i] == CHAR_NEWLINE) {
            lineCount++;
        }
    }
    
    // Draw with shadow
    WriteStringShadowed(wrapBuffer, x, y, color);
    
    return lineCount;
}

// StringDrawWrapShadow - FUNC_001399_StringDrawWrapShadow
void StringDrawWrapShadow(const uint8_t* str, int x, int y, uint8_t color, int maxWidth)
{
    static uint8_t wrapBuffer[512];
    StringExpandWrapFFCode(wrapBuffer, str, maxWidth);
    WriteStringShadowed(wrapBuffer, x, y, color);
}

// StringDrawWrapNoVars - FUNC_001400_StringDrawWrapNoVars
void StringDrawWrapNoVars(const uint8_t* str, int clipTop, int x, int y, int maxWidth)
{
    static uint8_t wrapBuffer[512];
    StringExpandWrapFFCode(wrapBuffer, str, maxWidth);
    TextWriteDefaultClip((const uint8_t*)wrapBuffer, x, y, clipTop);
}

// StringDrawWrap - FUNC_001401_StringDrawWrap
void StringDrawWrap(const uint8_t* str, int paramIndex, int clipTop, int x, int y, int maxWidth)
{
    static uint8_t wrapBuffer[512];
    g_textState.startX = x;
    StringExpandWrapFFCode(wrapBuffer, str, maxWidth);
    TextWriteDefaultClip((const uint8_t*)wrapBuffer, x, y, clipTop);
}

// StringDrawWrapPersist - FUNC_001402_StringDrawWrapPersist
void StringDrawWrapPersist(const uint8_t* str, int paramIndex, int clipTop, int x, int y, int maxWidth)
{
    static uint8_t wrapBuffer[512];
    g_textState.startX = x;
    StringExpandWrapFFCode(wrapBuffer, str, maxWidth);
    PersistTextWrite((const uint8_t*)wrapBuffer, x, y, clipTop);
}

// TextWriteInternal - FUNC_001587_TextWriteInternal
// A byte below 0x20 is a control code; code = byte + 0xE0 (CTRL_*). Each
// operand is one byte.
uint8_t* TextWriteInternal(uint8_t* str, int x, int y, uint8_t color,
                           CharDrawFunc drawChar, int clipTop, int clipBottom, int lineHeight)
{
    int startX = x;
    int alignX = 0;          // x just past the last inline bitmap
    int bitmapRows = 0;      // rows of that bitmap still below the pen
    bool afterNewline = false;

    g_textState.x = 0;
    g_textState.y = 0;

    if (y >= 0xc0) {
        return str;
    }

    while (true) {
        uint8_t ch = *str++;
        if (ch == 0) break;

        const uint8_t code = static_cast<uint8_t>(ch - 0x20);

        if (code < 0xE0) {
            if (static_cast<unsigned>(x) < 0x13c) {
                x += drawChar(static_cast<char>(code), color, x, y, clipTop, clipBottom);
            }
        }
        else if (code == CTRL_NEWLINE) {
            y += lineHeight;
            afterNewline = true;
            bitmapRows -= lineHeight;
            if (y > 0xbf) {
                return str;
            }
            x = startX;
            if (g_textState.y != 0) {
                return str;
            }
        }
        else if (code == CTRL_SET_COLOR) {
            // The ASM indexes past the 16-entry table into the ffedat bytes after it.
            if (*str < 16) {
                color = static_cast<uint8_t>(DATA_007776[*str]);
            }
            str++;
        }
        else if (code == CTRL_DRAW_BITMAP) {
            const uint8_t bmpIndex = *str++;
            const Bitmap* bmp = BmpIndexToPtr(bmpIndex);
            if (g_videoState) {
                BlitClipIndexToBuf(bmpIndex, x + 5, y, 0, clipTop, -1, clipBottom);
            }
            if (bmp) {
                const int advance = bmp->width + 10;
                alignX = x + advance;
                bitmapRows = bmp->height + lineHeight;
                x += advance;
            }
            afterNewline = false;
        }
        else if (code == CTRL_ALIGN) {
            if (bitmapRows > 0 && afterNewline) {
                x = alignX;
                afterNewline = false;
            }
        }
        else if (code == CTRL_SET_X) {
            x = *str * 2;
            str++;
        }
        else if (code == CTRL_SET_XY) {
            x = *str * 2;
            y = str[1];
            str += 2;
            startX = x;
        }
    }

    return str;
}

// TextWriteDefaultClip - FUNC_001584_TextWriteDefaultClip
void TextWriteDefaultClip(const uint8_t* str, int x, int y, uint8_t color)
{
    TextWriteInternal((uint8_t*)str, x, y, color, DrawChar, 0, -1, 10);
}

// TextWriteDefaultColor - FUNC_001583_TextWriteDefaultColor
void TextWriteDefaultColor(const uint8_t* str, int x, int y, uint8_t color)
{
    TextWriteDefaultClip(str, x, y, color);
}

// TextWriteDefaultHeight - FUNC_001585_TextWriteDefaultHeight
void TextWriteDefaultHeight(const uint8_t* str, int x, int y, uint8_t color,
                            int clipTop, int clipBottom)
{
    TextWriteInternal((uint8_t*)str, x, y, color, DrawChar, clipTop, clipBottom, 10);
}

// TextWriteDetailed - FUNC_001586_TextWriteDetailed
void TextWriteDetailed(const uint8_t* str, int x, int y, uint8_t color,
                       int clipTop, int clipBottom, int lineHeight)
{
    TextWriteInternal((uint8_t*)str, x, y, color, DrawChar, clipTop, clipBottom, lineHeight);
}

// TextWriteStringOnly - FUNC_001582_TextWriteStringOnly
void TextWriteStringOnly(const uint8_t* packedStr)
{
    // Packed format: [color_index, clip_top, string...]
    uint8_t color = packedStr[0] * 2;
    uint8_t clipTop = packedStr[1];
    TextWriteDefaultColor(packedStr + 2, 0, clipTop, color);
}

// WriteStringShadowed - FUNC_001589_WriteStringShadowed
void WriteStringShadowed(const uint8_t* str, int x, int y, uint8_t color)
{
    TextWriteInternal((uint8_t*)str, x, y, color, DrawShadowedChar, 0, -1, 10);
}

// PersistTextWrite - FUNC_001581_PersistTextWrite
void PersistTextWrite(const uint8_t* str, int x, int y, uint8_t color)
{
    TextWriteDefaultClip(str, x, y, color);
    FlipScreen();
}

// PersistBlit - FUNC_001580_PersistBlit
void PersistBlit()
{
    // No-op in original
}

// Utility functions
int StringLength(const char* str)
{
    return strlen(str);
}

void StringCopy(char* dst, const char* src)
{
    strcpy(dst, src);
}

void StringCopyN(char* dst, const char* src, size_t maxLen)
{
    strncpy(dst, src, maxLen);
    dst[maxLen - 1] = 0;
}

// StringConvertInt - helper for FF30 functions
char* StringConvertInt(char* buffer, int32_t value)
{
    sprintf(buffer, "%d", value);
    return buffer + strlen(buffer);
}

// StringConvertHexInt - FUNC_001542 (ffe.asm:91649): the 8 hex digits, low nibble
// first; returns the terminating NUL.
char* StringConvertHexInt(char* buffer, uint32_t value)
{
    return FUNC_001542(buffer, value);
}

// StringConvertTons - helper for FF30 functions
char* StringConvertTons(char* buffer, int32_t tons)
{
    if (tons == 0) {
        strcpy(buffer, "-");
        return buffer + 1;
    }
    char* end = StringConvertInt(buffer, tons);
    // Append 't' after the number
    *end++ = 't';
    *end = 0;
    return end;
}

// FF30 parameter functions
void FF30SetParam(int index, int32_t value)
{
    if (index >= 0 && index < 8) {
        g_ff30Params[index] = value;
    }
}

int32_t FF30GetParam(int index)
{
    if (index >= 0 && index < 8) {
        return g_ff30Params[index];
    }
    return 0;
}

// Resolve an FF30 parameter value. Prefer the live context (DATA_009149[0],
// set before StringExpand by FUNC_001344/001347) — the ASM formatters read
// [DATA_009149][i*4] directly. Fall back to the g_ff30Params mirror for unit
// tests that use FF30SetParam without a context.
static int32_t FF30Param(int index)
{
    const intptr_t ctx = GetFF30ContextBase();
    if (ctx)
        return *reinterpret_cast<const int32_t*>(ctx + static_cast<intptr_t>(index) * 4);
    return g_ff30Params[index];
}

// FF30 Date formatting functions
void FF30WriteDateV1(char* buffer)
{
    StringConvertDate(buffer, FF30Param(0), false);
}

void FF30WriteDateV2(char* buffer)
{
    StringConvertDate(buffer, FF30Param(1), false);
}

void FF30WriteDateV3(char* buffer)
{
    StringConvertDate(buffer, FF30Param(2), false);
}

void FF30WriteDateYearV1(char* buffer)
{
    StringConvertDate(buffer, FF30Param(0), true);
}

void FF30WriteDateYearV2(char* buffer)
{
    StringConvertDate(buffer, FF30Param(1), true);
}

void FF30WriteDateYearV3(char* buffer)
{
    StringConvertDate(buffer, FF30Param(2), true);
}

// FF30 code 0x3029 — date with year from DATA_008807 (FUNC_001356)
void FF30WriteDateYearD8807(char* buffer)
{
    FUNC_001356_FF30WriteDateYearD8807(reinterpret_cast<intptr_t>(buffer));
}

// FF30 Time formatting functions
void FF30WriteTimeV1(char* buffer)
{
    StringConvertTime(buffer, FF30Param(0));
}

void FF30WriteTimeV2(char* buffer)
{
    StringConvertTime(buffer, FF30Param(1));
}

void FF30WriteTimeV3(char* buffer)
{
    StringConvertTime(buffer, FF30Param(2));
}

// FF30 Integer formatting functions
void FF30WriteIntV1(char* buffer)
{
    StringConvertInt(buffer, FF30Param(0));
}

void FF30WriteIntV2(char* buffer)
{
    StringConvertInt(buffer, FF30Param(1));
}

void FF30WriteIntV3(char* buffer)
{
    StringConvertInt(buffer, FF30Param(2));
}

void FF30WriteIntV4(char* buffer)
{
    StringConvertInt(buffer, FF30Param(3));
}

void FF30WriteIntV5(char* buffer)
{
    StringConvertInt(buffer, FF30Param(4));
}

// FF30 Hex integer formatting functions
void FF30WriteHexIntV1(char* buffer)
{
    StringConvertHexInt(buffer, FF30Param(0));
}

void FF30WriteHexIntV2(char* buffer)
{
    StringConvertHexInt(buffer, FF30Param(1));
}

void FF30WriteHexIntV3(char* buffer)
{
    StringConvertHexInt(buffer, FF30Param(2));
}

// FF30 Tons formatting functions
void FF30WriteTonsV1(char* buffer)
{
    StringConvertTons(buffer, FF30Param(0));
}

void FF30WriteTonsV2(char* buffer)
{
    StringConvertTons(buffer, FF30Param(1));
}

void FF30WriteTonsV3(char* buffer)
{
    StringConvertTons(buffer, FF30Param(2));
}

// FF30 Speed formatting functions
void FF30WriteSpeedV1(char* buffer)
{
    StringConvertSpeed(buffer, FF30Param(0));
}

void FF30WriteSpeedV2(char* buffer)
{
    StringConvertSpeed(buffer, FF30Param(1));
}

void FF30WriteSpeedV3(char* buffer)
{
    StringConvertSpeed(buffer, FF30Param(2));
}

// FF30 Fraction formatting functions: FUNC_001378..001386 (ffe.asm 84953-85085)
// are FUNC_001377(dst, [DATA_009149 + slot*4], places), which inserts the point
// into the signed decimal text, so -6 at one place is "-.6" (ffe.asm 84876-84950).
void FF30Write1dpFracV1(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(0)), 1);
}

void FF30Write1dpFracV2(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(1)), 1);
}

void FF30Write1dpFracV3(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(2)), 1);
}

void FF30Write2dpFracV1(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(0)), 2);
}

void FF30Write2dpFracV2(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(1)), 2);
}

void FF30Write2dpFracV3(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(2)), 2);
}

void FF30Write3dpFracV1(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(0)), 3);
}

void FF30Write3dpFracV2(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(1)), 3);
}

void FF30Write3dpFracV3(char* buffer)
{
    FUNC_001377_StringConvertFraction(buffer, static_cast<uint32_t>(FF30Param(2)), 3);
}

// FF30 FF code expansion functions (ASM FUNC_001391..001387). They go through
// the full FUNC_001343: the nested code is usually an 0x8000+ data code, which
// this file's StringExpandFFCodeInternal does not expand.
// Null-terminate up front: the caller (StringExpand) advances by strlen() of
// what we write. When the inner code is 0 (g_ff30Params unset) it expands to
// nothing, so without a terminator strlen() reads the stale 0xFF escape and
// runs off the end of the destination buffer (global-buffer-overflow).
void FF30WriteFFCodeV1(char* buffer)
{
    buffer[0] = '\0';
    FUNC_001343_StringExpandFFCodeInternal(reinterpret_cast<intptr_t>(buffer),
                                           static_cast<uint32_t>(FF30Param(0)));
}

void FF30WriteFFCodeV2(char* buffer)
{
    buffer[0] = '\0';
    FUNC_001343_StringExpandFFCodeInternal(reinterpret_cast<intptr_t>(buffer),
                                           static_cast<uint32_t>(FF30Param(1)));
}

void FF30WriteFFCodeV3(char* buffer)
{
    buffer[0] = '\0';
    FUNC_001343_StringExpandFFCodeInternal(reinterpret_cast<intptr_t>(buffer),
                                           static_cast<uint32_t>(FF30Param(2)));
}

void FF30WriteFFCodeV4(char* buffer)
{
    buffer[0] = '\0';
    FUNC_001343_StringExpandFFCodeInternal(reinterpret_cast<intptr_t>(buffer),
                                           static_cast<uint32_t>(FF30Param(3)));
}

void FF30WriteFFCodeV5(char* buffer)
{
    buffer[0] = '\0';
    FUNC_001343_StringExpandFFCodeInternal(reinterpret_cast<intptr_t>(buffer),
                                           static_cast<uint32_t>(FF30Param(4)));
}

// FF30 String formatting functions — ASM FUNC_001364/365 push
// [DATA_009149+0xc]/[+0x10] into DATA_007642 (StringExpand). Hull text
// (001747 / code 0x3022) seeds +0xc with obj+0x124 (20-byte name). The old
// sprintf("%d", FF30Param(3)) printed the truncated LP64 pointer as decimal
// ("55429220" from 0x034dc864) instead of expanding "01".
// ASM-layout vars blocks keep another dword right after a dword pointer (e.g.
// FUNC_000316's contract rows), so the pointer is the low dword whenever the
// image is linked below 4 GB (the lowmem logic replay). Otherwise only LP64
// writers (hull text, ProcessObject791) can have stored a usable full pointer:
// accept it if it lies in the image's or the stack's 4 GB window. A zero high
// dword is a 32-bit pointer into a MAP_32BIT arena (FUNC_000327's station
// name) when the low dword is at or above the kernel's lowest mappable address
// (vm.mmap_min_addr, 0x10000); below that it is a number.
extern "C" char __executable_start[];
extern "C" char _end[];

static bool s_ff30TrustContextPointers = false;

void FF30SetContextTrusted(bool trusted) { s_ff30TrustContextPointers = trusted; }

bool FF30HighDwordIsLocal(uint32_t hi, uintptr_t imageFirst, uintptr_t imageLast,
                          uintptr_t stackAddr)
{
    return hi == (imageFirst >> 32) || hi == (imageLast >> 32) || hi == (stackAddr >> 32);
}

static const uint8_t* FF30StringParamPtr(int index)
{
    const intptr_t ctx = GetFF30ContextBase();
    if (!ctx) return nullptr;
    const uint8_t* slot = reinterpret_cast<const uint8_t*>(ctx + static_cast<intptr_t>(index) * 4);
    uint32_t lo, hi;
    std::memcpy(&lo, slot, 4);
    if (s_ff30TrustContextPointers) {
        // The writer declared this block holds full pointers: no guessing.
        std::memcpy(&hi, slot + 4, 4);
        if (hi == 0)
            return lo >= 0x10000u ? reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(lo))
                                  : nullptr;
        return reinterpret_cast<const uint8_t*>((static_cast<uintptr_t>(hi) << 32) |
                                                static_cast<uintptr_t>(lo));
    }
    const uintptr_t imageHi = reinterpret_cast<uintptr_t>(&g_textState) >> 32;
    if (imageHi == 0)
        return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(lo));
    std::memcpy(&hi, slot + 4, 4);
    if (hi == 0)
        return lo >= 0x10000u ? reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(lo)) : nullptr;
    const bool local = FF30HighDwordIsLocal(hi, reinterpret_cast<uintptr_t>(__executable_start),
                                            reinterpret_cast<uintptr_t>(_end) - 1,
                                            reinterpret_cast<uintptr_t>(&slot));
    // A 32-bit address of a below-4G table followed by another live dword - a second
    // address (FUNC_000195's save name and extension in the span) or a number
    // (FUNC_000316's DATA_009148 then the model's name id, ffe.asm 25549-25552):
    // the next slot is not a high dword.
    if (!local && InBelow4G(lo))
        return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(lo));
    if (!local) return nullptr;
    return reinterpret_cast<const uint8_t*>((static_cast<uintptr_t>(hi) << 32) | lo);
}

void FF30WriteStringV4(char* buffer)
{
    buffer[0] = '\0';
    const uint8_t* src = FF30StringParamPtr(3);
    if (src) {
        StringExpand(reinterpret_cast<uint8_t*>(buffer), src);
        return;
    }
    // Unit-test fallback when FF30SetParam is used without a live context.
    sprintf(buffer, "%d", FF30GetParam(3));
}

void FF30WriteStringV5(char* buffer)
{
    buffer[0] = '\0';
    const uint8_t* src = FF30StringParamPtr(4);
    if (src) {
        StringExpand(reinterpret_cast<uint8_t*>(buffer), src);
        return;
    }
    sprintf(buffer, "%d", FF30GetParam(4));
}

void FF30WriteWrapStringV4(char* buffer)
{
    // ASM FUNC_001392: WriteStringV4 then StringWrap(dst, 0x23).
    FF30WriteStringV4(buffer);
    StringWrap(reinterpret_cast<uint8_t*>(buffer), 0x23);
}

// Phase 65: Missing string functions (FUNC_001300-0x1399)

// FUNC_001329 — 3D bounding-box scan over DATA_009139 (ffe.asm:4e400)
// Finds the axis-aligned bounding box of all non-zero (occupied) elements in
// a 3D volume, stores the box in param_1[0..7], and returns the occupied count.
int32_t FUNC_001329(int16_t* param_1)
{
    if (!param_1 || !DATA_009139) return 0;

    // Save original search bounds (sign-extended shorts)
    int32_t i0 = (int32_t)param_1[0];
    int32_t i1 = (int32_t)param_1[1];
    int32_t j0 = (int32_t)param_1[2];
    int32_t j1 = (int32_t)param_1[3];
    int32_t k0 = (int32_t)param_1[4];
    int32_t k1 = (int32_t)param_1[5];

    // Reset to an empty bounding box (min = 0x10, max = 0)
    param_1[0] = 0x10;
    param_1[1] = 0;
    param_1[2] = 0x10;
    param_1[3] = 0;
    param_1[4] = 0x10;
    param_1[5] = 0;

    int32_t count = 0;
    uint8_t* base = DATA_009139;

    // Strides: i = 0x100, j = 0x10, k = 1 (ASM lea [edx*8+DATA_009139])
    for (int32_t i = i0; i < i1; i++) {
        uint8_t* row = base + i * 0x100 + j0 * 0x10;
        for (int32_t j = j0; j < j1; j++) {
            uint8_t* col = row + k0;
            for (int32_t k = k0; k < k1; k++) {
                if (*col != 0) {
                    count++;
                    if (i < (int32_t)param_1[0]) param_1[0] = (int16_t)i;
                    if ((int32_t)param_1[1] <= i) param_1[1] = (int16_t)(i + 1);
                    if (j < (int32_t)param_1[2]) param_1[2] = (int16_t)j;
                    if ((int32_t)param_1[3] <= j) param_1[3] = (int16_t)(j + 1);
                    if (k < (int32_t)param_1[4]) param_1[4] = (int16_t)k;
                    if ((int32_t)param_1[5] <= k) param_1[5] = (int16_t)(k + 1);
                }
                col++;
            }
            row += 0x10;
        }
    }

    // Largest extent + which axis (0=width, 1=height, 2=depth)
    param_1[6] = param_1[1] - param_1[0];
    param_1[7] = 0;
    if ((int32_t)param_1[6] < (int32_t)param_1[3] - (int32_t)param_1[2]) {
        param_1[6] = (int16_t)((int32_t)param_1[3] - (int32_t)param_1[2]);
        param_1[7] = 1;
    }
    if ((int32_t)param_1[6] < (int32_t)param_1[5] - (int32_t)param_1[4]) {
        param_1[6] = (int16_t)((int32_t)param_1[5] - (int32_t)param_1[4]);
        param_1[7] = 2;
    }
    return count;
}

} // namespace jjffe
