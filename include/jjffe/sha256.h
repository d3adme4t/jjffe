#ifndef JJFFE_SHA256_H
#define JJFFE_SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jjffe {

struct Sha256 {
    uint32_t state[8]{};
    uint64_t bitLen = 0;
    uint8_t buf[64]{};
    size_t bufLen = 0;
};

void Sha256Init(Sha256* ctx);
void Sha256Update(Sha256* ctx, const void* data, size_t len);
void Sha256Final(Sha256* ctx, uint8_t out[32]);

// Lowercase hex of 32 bytes (no prefix).
std::string Sha256Hex(const uint8_t digest[32]);

// One-shot: "sha256:" + hex.
std::string Sha256HexPrefixed(const void* data, size_t len);

// Hash file contents; empty string on I/O failure.
std::string Sha256FilePrefixed(const char* path);

}  // namespace jjffe

#endif
