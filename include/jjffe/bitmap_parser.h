#ifndef JJFFE_BITMAP_PARSER_H
#define JJFFE_BITMAP_PARSER_H

#include <cstdint>
#include <cstddef>

namespace jjffe {
namespace data {

struct BitmapInfo {
    uint8_t flags;       // Bit 0 = masked blit
    int16_t height;      // Height in pixels
    int16_t width;       // Width in pixels
    int16_t xOff;        // X offset (hotspot)
    int16_t yOff;        // Y offset (hotspot)
    const uint8_t* pixelData;
    size_t pixelDataSize;
    size_t totalSize;    // Header + pixels
};

// Parse bitmap header from data blob
bool ParseBitmap(const uint8_t* data, size_t dataSize, BitmapInfo* outInfo);

// Get pixel data pointer (skips header)
const uint8_t* GetBitmapPixelData(const uint8_t* data, size_t dataSize, 
                                   int16_t* outWidth = nullptr, int16_t* outHeight = nullptr);

// Get total size of bitmap (header + pixels)
size_t GetBitmapTotalSize(const uint8_t* data, size_t dataSize);

// Check if bitmap uses transparency (masked blit)
bool IsMaskedBitmap(const uint8_t* data, size_t dataSize);

} // namespace data
} // namespace jjffe

#endif // JJFFE_BITMAP_PARSER_H
