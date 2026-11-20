#include "jjffe/bitmap_parser.h"
#include <cstring>

namespace jjffe {
namespace data {

// Bitmap header: flags (1 byte), height (1 byte), width (2 bytes LE), xOff (2 bytes LE), yOff (2 bytes LE)
// Total header size: 8 bytes

bool ParseBitmap(const uint8_t* data, size_t dataSize, BitmapInfo* outInfo)
{
    if (!data || !outInfo || dataSize < 8) {
        return false;
    }
    
    outInfo->flags = data[0];
    outInfo->height = data[1];
    outInfo->width = (int16_t)(data[2] | (data[3] << 8));
    outInfo->xOff = (int16_t)(data[4] | (data[5] << 8));
    outInfo->yOff = (int16_t)(data[6] | (data[7] << 8));
    outInfo->pixelData = data + 8;
    outInfo->pixelDataSize = (size_t)outInfo->width * outInfo->height;
    outInfo->totalSize = 8 + outInfo->pixelDataSize;
    
    // Sanity checks
    if (outInfo->width <= 0 || outInfo->height <= 0 || outInfo->width > 2048 || outInfo->height > 512) {
        return false;
    }
    
    if (outInfo->totalSize > dataSize) {
        return false;
    }
    
    return true;
}

const uint8_t* GetBitmapPixelData(const uint8_t* data, size_t dataSize, int16_t* outWidth, int16_t* outHeight)
{
    BitmapInfo info;
    if (ParseBitmap(data, dataSize, &info)) {
        if (outWidth) *outWidth = info.width;
        if (outHeight) *outHeight = info.height;
        return info.pixelData;
    }
    return nullptr;
}

size_t GetBitmapTotalSize(const uint8_t* data, size_t dataSize)
{
    BitmapInfo info;
    if (ParseBitmap(data, dataSize, &info)) {
        return info.totalSize;
    }
    return 0;
}

bool IsMaskedBitmap(const uint8_t* data, size_t dataSize)
{
    if (!data || dataSize < 1) return false;
    return (data[0] & 1) != 0;  // Bit 0 = masked flag
}

} // namespace data
} // namespace jjffe
