#include "jjffe/band_lane_tables.h"

// R468 / Round-2 code phase: producer ports.
//
// FUNC_001480 (ASM 0x59a93):
//   rec0: [0..1] = u16(ctx+0x8d), [2] = u8(ctx+0x8f), rec0[3] &= ~3
//   rec1 (offset 8): [0..1] = u16(ctx+0x90), [2] = u8(ctx+0x92), rec1[3] &= ~3
//   for a6 in 2..count-1: rec[a6][3] |= 0x03
//
// FUNC_001481 (ASM 0x59b09), decoded in R467:
//   f-src = descData154 + (a6>>1)*6 - 6
//     cl = f-src[0] (u8); f8 = (int8)f-src[2]<<8; fc = (int8)f-src[3]<<8;
//     f4 = (int8)f-src[4]<<8; if (a6 & 1) f8 = -f8
//   e-src = vertData150 + (cl>>1)*6
//     e8 = (int8)e-src[2]<<8; ec = (int8)e-src[3]<<8; f0 = (int8)e-src[4]<<8;
//     if ((a6 ^ cl) & 1) e8 = -e8
//   shift = shift148 - scale44; apply sar(-shift)/shl(shift) to e8,ec,f0
//   e8 -= cam48; ec -= cam4c; f0 -= cam50
//   dot1 = view38*f4 + view3c*f8 + view40*fc
//     dot1 >= 0 -> rec[0..2] = 0
//     dot1 <  0 -> b = (dot1>>27)&7; rec[0..1] = u16(canned84+3b);
//                  rec[2] = canned84[3b+2]
//   dot2 = e8*f4 + ec*f8 + f0*fc
//     dot2 >= 0 -> rec[3] = (rec[3] & ~2) | 1    (SKIP)
//     dot2 <  0 -> rec[3] = (rec[3] & ~2) & ~1   (DRAW)
//
// FUNC_001689/001690 (ASM 0x616da): zero +0x14 tag of each vertex record.

namespace jjffe {

namespace {

// 32-bit product truncation exactly like the ASM imul.
int32_t Imul32(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<int64_t>(a) * static_cast<int64_t>(b));
}

} // namespace

void Func001480(uint8_t* bandTable, int count, const uint8_t* ctx_8d) {
    uint8_t* rec0 = bandTable;
    rec0[0] = ctx_8d[0];
    rec0[1] = ctx_8d[1];   // ASM: movw u16(ctx+0x8d) -> [0..1] = 0x8d,0x8e
    rec0[2] = ctx_8d[2];   // ASM: movb u8(ctx+0x8f) -> [2]
    rec0[3] &= ~3u;

    uint8_t* rec1 = bandTable + 8;
    rec1[0] = ctx_8d[3];   // 0x90
    rec1[1] = ctx_8d[4];   // 0x91
    rec1[2] = ctx_8d[5];   // 0x92
    rec1[3] &= ~3u;

    for (int a6 = 2; a6 < count; ++a6) {
        bandTable[static_cast<size_t>(a6) * 8 + 3] |= 0x03;
    }
}

uint8_t* Func001481(const BandLaneFaceCtx& f, uint8_t* rec, uint16_t a6) {
    const uint8_t* fs = f.descData154 + static_cast<int>((a6 >> 1) * 6) - 6;
    uint32_t cl = fs[0];
    // ASM shl 8 on the raw byte; shift the unsigned view (UBSan: negative bytes).
    int32_t f8 = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(fs[2]))) << 8);
    int32_t fc = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(fs[3]))) << 8);
    int32_t f4 = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(fs[4]))) << 8);
    if (a6 & 1) f8 = -f8;

    const uint8_t* es = f.vertData150 + static_cast<int>((cl >> 1) * 6);
    int32_t e8 = static_cast<int32_t>(static_cast<int8_t>(es[2])) << 8;
    int32_t ec = static_cast<int32_t>(static_cast<int8_t>(es[3])) << 8;
    int32_t f0 = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(es[4]))) << 8);
    if ((a6 ^ cl) & 1) e8 = -e8;

    int32_t shift = f.shift148 - f.scale44;
    if (shift < 0) {
        int c = (-shift) & 0x1f;
        e8 >>= c; ec >>= c; f0 >>= c;   // ASM sarl (arithmetic)
    } else if (shift > 0) {
        int c = shift & 0x1f;
        // ASM shl by a variable count; unsigned view (UBSan: negative values).
        e8 = static_cast<int32_t>(static_cast<uint32_t>(e8) << c);
        ec = static_cast<int32_t>(static_cast<uint32_t>(ec) << c);
        f0 = static_cast<int32_t>(static_cast<uint32_t>(f0) << c);
    }
    e8 -= f.cam48;
    ec -= f.cam4c;
    f0 -= f.cam50;

    int32_t dot1 = Imul32(f.view38, f4) + Imul32(f.view3c, f8) + Imul32(f.view40, fc);
    if (dot1 >= 0) {
        rec[0] = 0; rec[1] = 0; rec[2] = 0;
    } else {
        int b = (dot1 >> 27) & 7;
        rec[0] = f.canned84[b * 3];
        rec[1] = f.canned84[b * 3 + 1];
        rec[2] = f.canned84[b * 3 + 2];
    }

    int32_t dot2 = Imul32(e8, f4) + Imul32(ec, f8) + Imul32(f0, fc);
    if (dot2 >= 0) {
        rec[3] = static_cast<uint8_t>((rec[3] & ~2u) | 1u);   // SKIP
    } else {
        rec[3] = static_cast<uint8_t>((rec[3] & ~2u) & ~1u);  // DRAW
    }
    return rec;
}

void Func001689ResetTags(uint8_t* vertexTable, int count) {
    for (int i = 0; i < count; ++i) {
        uint32_t* tag = reinterpret_cast<uint32_t*>(vertexTable + static_cast<size_t>(i) * 36 + 0x14);
        *tag = 0;
    }
}

namespace {
// FUNC_001521 FixedMulHigh: (int64)a*b >> 31 (matches core/math.cpp:572).
int32_t FixedMulHigh(int32_t a, int32_t b) {
    return static_cast<int32_t>((static_cast<int64_t>(a) * static_cast<int64_t>(b)) >> 31);
}
} // namespace

int32_t Func001475Normalize(const int32_t in[3], int32_t out[3]) {
    int32_t e = 0;
    for (int i = 0; i < 3; ++i) {
        int32_t v = in[i];
        if (v < 0) v = -v;
        e |= v;
    }
    int32_t scale = 0;
    while (e >= 0x4000) {          // ASM: cmp $0x4000 / jae loop
        e >>= 1;
        ++scale;
    }
    for (int i = 0; i < 3; ++i) out[i] = in[i] >> scale;
    return scale;
}

int32_t Func001685Camera(const int32_t triple[3], const int32_t mat[9],
                         const int32_t camPos[3], int32_t shift148,
                         int32_t out38[3], int32_t out48[3]) {
    int32_t tmp[3];
    int32_t scale = Func001475Normalize(triple, tmp);
    int32_t e = scale + 7 - shift148;
    if (e < 0) {                   // ASM 61600: test/jge skip; neg; add to 0x44
        int32_t c = -e;
        scale += c;                // 0x44 += c
        for (int i = 0; i < 3; ++i) tmp[i] >>= c;   // sarl
    }
    for (int i = 0; i < 3; ++i) tmp[i] = -tmp[i];
    for (int r = 0; r < 3; ++r) {
        int32_t a = FixedMulHigh(mat[r * 3 + 0], tmp[0]);
        int32_t b = FixedMulHigh(mat[r * 3 + 1], tmp[1]);
        int32_t c = FixedMulHigh(mat[r * 3 + 2], tmp[2]);
        out48[r] = a + b + c;
    }
    for (int r = 0; r < 3; ++r) {
        int32_t a = FixedMulHigh(mat[r * 3 + 0], camPos[0]);
        int32_t b = FixedMulHigh(mat[r * 3 + 1], camPos[1]);
        int32_t c = FixedMulHigh(mat[r * 3 + 2], camPos[2]);
        out38[r] = a + b + c;
    }
    return scale;
}

} // namespace jjffe
