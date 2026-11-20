#ifndef JJFFE_STRING_H
#define JJFFE_STRING_H

#include <cstdint>
#include <cstddef>

namespace jjffe {

// Forward declarations
struct TextWriteState;

// Text write state for tracking position during text rendering
struct TextWriteState {
    int x;
    int y;
    int startX;
    int lineHeight;
    int clipTop;
    int clipBottom;
    bool lineBreak;
    int lastLineHeight;
};

// Global text write state
extern TextWriteState g_textState;

// String expansion functions
void StringExpand(uint8_t* dst, const uint8_t* src);
uint8_t* StringExpandFFCodeInternal(uint8_t* dst, uint16_t code);
uint8_t* StringExpandFFCode(uint8_t* dst, uint16_t code);
uint8_t* StringExpandFF40Code(uint8_t* dst, uint8_t param);
uint8_t* StringExpandFF80Code(uint8_t* dst, uint8_t param);
uint8_t* StringExpandArrayIndex(uint8_t* dst, uint8_t index);

// String wrapping
uint8_t* StringWrap(uint8_t* str, int maxWidth);
uint8_t* StringExpandWrapFFCode(uint8_t* dst, const uint8_t* src, int maxWidth);
void StringDrawWrapShadow(const uint8_t* str, int x, int y, uint8_t color, int maxWidth);

// Date/time conversion
void StringConvertDateNum(uint32_t dateValue, uint32_t* day, uint32_t* month, int32_t* year);
char* StringConvertDate(char* buffer, uint32_t dateValue, bool includeYear);
// month is 1-based (1 = "Jan"); anything outside 1..12 gives "non".
const char* StringConvertMonth(uint32_t month);
char* StringConvertTime(char* buffer, uint32_t timeValue);

// Number formatting
char* StringConvertFraction(char* buffer, int32_t value, int32_t divisor, int decimalPlaces);
char* StringConvertSpeed(char* buffer, int32_t speed);
char* StringConvertInt(char* buffer, int32_t value);
char* StringConvertHexInt(char* buffer, uint32_t value);
char* StringConvertTons(char* buffer, int32_t tons);

// Text display functions (Phase 13)
// Returns glyph advance width (ASM FUNC_001588/001598 return glyph[9]).
using CharDrawFunc = int(*)(char c, uint8_t color, int x, int y, int clipTop, int clipBottom);

// Text colour array (ffedat DATA_007776): the text code 0x01 N sets the colour
// to DATA_007776[N]. The ASM's labels DATA_007777..7783 are slots 1, 2, 3, 4,
// 6, 8 and 14 of it (DATA_007780..7782 are two dwords each).
extern uint32_t DATA_007776[16];

uint8_t* TextWriteInternal(uint8_t* str, int x, int y, uint8_t color,
                           CharDrawFunc drawChar, int clipTop, int clipBottom, int lineHeight);
void TextWriteDefaultClip(const uint8_t* str, int x, int y, uint8_t color);
void TextWriteDefaultColor(const uint8_t* str, int x, int y, uint8_t color);
void TextWriteDefaultHeight(const uint8_t* str, int x, int y, uint8_t color, 
                            int clipTop, int clipBottom);
void TextWriteDetailed(const uint8_t* str, int x, int y, uint8_t color,
                       int clipTop, int clipBottom, int lineHeight);
void TextWriteStringOnly(const uint8_t* packedStr);
void WriteStringShadowed(const uint8_t* str, int x, int y, uint8_t color);
void PersistTextWrite(const uint8_t* str, int x, int y, uint8_t color);
void PersistBlit();

// String draw wrap functions
int StringDrawWrapShadowCount(const uint8_t* str, int x, int y, uint8_t color, int maxWidth);
void StringDrawWrapNoVars(const uint8_t* str, int clipTop, int x, int y, int maxWidth);
void StringDrawWrap(const uint8_t* str, int paramIndex, int clipTop, int x, int y, int maxWidth);
void StringDrawWrapPersist(const uint8_t* str, int paramIndex, int clipTop, int x, int y, int maxWidth);

// FF30 formatting functions (Phase 14)
// These use the global parameter array for values

// Parameter array for FF30 functions (up to 8 parameters)
extern int32_t g_ff30Params[8];

// Set parameter array
void FF30SetParam(int index, int32_t value);

// Port-only: some port writers store a genuine 64-bit pointer in a context
// slot (hull text FUNC_001747 puts obj+0x124 in slot 3). FF30StringParamPtr
// otherwise *guesses* pointer-vs-number from the slot's high dword against the
// image/stack ranges, which misfires for heap, arena and sanitizer fake-stack
// addresses and silently truncates the pointer to 32 bits. A writer that knows
// the slot is a full pointer brackets the expansion with this switch instead of
// letting the accessor guess.
void FF30SetContextTrusted(bool trusted);
int32_t FF30GetParam(int index);

// FF30 context base pointer (DATA_009149[0]) — defined in core.cpp.
intptr_t GetFF30ContextBase(void);

// Whether a slot's high dword names the image or the stack window. The image
// can straddle a 4 GB boundary, so the windows of both ends count.
bool FF30HighDwordIsLocal(uint32_t hi, uintptr_t imageFirst, uintptr_t imageLast,
                          uintptr_t stackAddr);

// Date formatting
void FF30WriteDateV1(char* buffer);
void FF30WriteDateV2(char* buffer);
void FF30WriteDateV3(char* buffer);
void FF30WriteDateYearV1(char* buffer);
void FF30WriteDateYearV2(char* buffer);
void FF30WriteDateYearV3(char* buffer);
void FF30WriteDateYearD8807(char* buffer);

// Time formatting
void FF30WriteTimeV1(char* buffer);
void FF30WriteTimeV2(char* buffer);
void FF30WriteTimeV3(char* buffer);

// Integer formatting
void FF30WriteIntV1(char* buffer);
void FF30WriteIntV2(char* buffer);
void FF30WriteIntV3(char* buffer);
void FF30WriteIntV4(char* buffer);
void FF30WriteIntV5(char* buffer);

// Hex integer formatting
void FF30WriteHexIntV1(char* buffer);
void FF30WriteHexIntV2(char* buffer);
void FF30WriteHexIntV3(char* buffer);

// Tons formatting
void FF30WriteTonsV1(char* buffer);
void FF30WriteTonsV2(char* buffer);
void FF30WriteTonsV3(char* buffer);

// Speed formatting
void FF30WriteSpeedV1(char* buffer);
void FF30WriteSpeedV2(char* buffer);
void FF30WriteSpeedV3(char* buffer);

// Fraction formatting
void FF30Write1dpFracV1(char* buffer);
void FF30Write1dpFracV2(char* buffer);
void FF30Write1dpFracV3(char* buffer);
void FF30Write2dpFracV1(char* buffer);
void FF30Write2dpFracV2(char* buffer);
void FF30Write2dpFracV3(char* buffer);
void FF30Write3dpFracV1(char* buffer);
void FF30Write3dpFracV2(char* buffer);
void FF30Write3dpFracV3(char* buffer);

// FF code expansion
void FF30WriteFFCodeV1(char* buffer);
void FF30WriteFFCodeV2(char* buffer);
void FF30WriteFFCodeV3(char* buffer);
void FF30WriteFFCodeV4(char* buffer);
void FF30WriteFFCodeV5(char* buffer);

// String formatting
void FF30WriteStringV4(char* buffer);
void FF30WriteStringV5(char* buffer);
void FF30WriteWrapStringV4(char* buffer);

// Utility
int StringLength(const char* str);
void StringCopy(char* dst, const char* src);
void StringCopyN(char* dst, const char* src, size_t maxLen);

// Special character codes
constexpr uint8_t CHAR_NEWLINE = 0x0D;
constexpr uint8_t CHAR_SPACE = 0x20;
constexpr uint8_t CHAR_CODE_FD = 0xFD;  // 2-byte code
constexpr uint8_t CHAR_CODE_FE = 0xFE;  // 3-byte code
constexpr uint8_t CHAR_CODE_FF = 0xFF;  // Complex code

// Text control codes as TextWriteInternal sees them: (byte - 0x20) & 0xff,
// so string byte 0x0D arrives as 0xED.
constexpr uint8_t CTRL_NEWLINE = 0xED;      // byte 0x0D: newline
constexpr uint8_t CTRL_SET_COLOR = 0xE1;    // byte 0x01 N: colour DATA_007776[N]
constexpr uint8_t CTRL_DRAW_BITMAP = 0xE2;  // byte 0x02 N: inline bitmap N
constexpr uint8_t CTRL_ALIGN = 0xE3;        // byte 0x03: align to the last bitmap
constexpr uint8_t CTRL_SET_X = 0xFE;        // byte 0x1E X: x = X*2
constexpr uint8_t CTRL_SET_XY = 0xFF;       // byte 0x1F X Y: x = X*2, y = Y

} // namespace jjffe

#endif // JJFFE_STRING_H
