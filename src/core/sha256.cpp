#include "jjffe/sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace jjffe {

namespace {

uint32_t Rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void Transform(Sha256* ctx, const uint8_t block[64])
{
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 =
            Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 =
            Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
             d = ctx->state[3], e = ctx->state[4], f = ctx->state[5],
             g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + S1 + ch + K[i] + w[i];
        const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

}  // namespace

void Sha256Init(Sha256* ctx)
{
    if (!ctx) return;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitLen = 0;
    ctx->bufLen = 0;
}

void Sha256Update(Sha256* ctx, const void* data, size_t len)
{
    if (!ctx || (!data && len)) return;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (len > 0) {
        const size_t n = std::min(len, 64 - ctx->bufLen);
        if (n) std::memcpy(ctx->buf + ctx->bufLen, p, n);
        ctx->bufLen += n;
        p += n;
        len -= n;
        if (ctx->bufLen == 64) {
            Transform(ctx, ctx->buf);
            ctx->bitLen += 512;
            ctx->bufLen = 0;
        }
    }
}

void Sha256Final(Sha256* ctx, uint8_t out[32])
{
    if (!ctx || !out) return;
    ctx->bitLen += (uint64_t)ctx->bufLen * 8;
    ctx->buf[ctx->bufLen++] = 0x80;
    if (ctx->bufLen > 56) {
        while (ctx->bufLen < 64) ctx->buf[ctx->bufLen++] = 0;
        Transform(ctx, ctx->buf);
        ctx->bufLen = 0;
    }
    while (ctx->bufLen < 56) ctx->buf[ctx->bufLen++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buf[ctx->bufLen++] = (uint8_t)((ctx->bitLen >> (i * 8)) & 0xff);
    Transform(ctx, ctx->buf);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

std::string Sha256Hex(const uint8_t digest[32])
{
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.resize(64);
    for (int i = 0; i < 32; i++) {
        s[(size_t)i * 2] = hex[digest[i] >> 4];
        s[(size_t)i * 2 + 1] = hex[digest[i] & 0xf];
    }
    return s;
}

std::string Sha256HexPrefixed(const void* data, size_t len)
{
    Sha256 ctx;
    Sha256Init(&ctx);
    Sha256Update(&ctx, data, len);
    uint8_t dig[32];
    Sha256Final(&ctx, dig);
    return std::string("sha256:") + Sha256Hex(dig);
}

std::string Sha256FilePrefixed(const char* path)
{
    if (!path) return {};
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    Sha256 ctx;
    Sha256Init(&ctx);
    uint8_t buf[8192];
    while (true) {
        const size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n) Sha256Update(&ctx, buf, n);
        if (n < sizeof(buf)) break;
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    if (!ok) return {};
    uint8_t dig[32];
    Sha256Final(&ctx, dig);
    return std::string("sha256:") + Sha256Hex(dig);
}

}  // namespace jjffe
