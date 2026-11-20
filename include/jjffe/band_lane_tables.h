#pragma once
#include <cstdint>
#include <vector>

// R468 / Round-2 code phase: the producer side of the band-lane subsystem.
// Ports (verbatim from the ASM, oracle-verified against pose 01/06 captures):
//   FUNC_001480 (0x59a93) - band-record initializer
//   FUNC_001481 (0x59b09) - per-face facing resolver + record writer
//   FUNC_001689/001690 (0x616da) - vertex-cache tag reset
// The ASM arena mechanics (FUNC_001745/001746) are represented by caller-owned
// buffers; only the record semantics are ported, which is what the interpreter
// and the face gate consume.
//
// Captured oracle (scratch/r1/oc_01.out, oc_06.out, entries 1-3, ctx fffeca1c,
// c17d program, a6=0x04):
//   descData (ctx+0x154) +6:  00 00 00 81 00 00   (f: cl=0, fc=-127<<8, f8=f4=0)
//   vertData (ctx+0x150) +0:  01 00 1b 00 b2 00   (e: e8=27<<8, ec=0, f0=-78<<8)
//   ctx44=3, ctx148=5, ctx38=ctx3c=ctx40=0, ctx84..=0
//   pose 01: ctx48=0, ctx4c=0x7e, ctx50=0xffffc801 -> dot2=+4,096,512 -> SKIP
//   pose 06: ctx48=0xffffd66e, ctx4c=0xffffe880, ctx50=0xfffff800
//            -> dot2=-195,592,192 -> DRAW

namespace jjffe {

// The per-call ctx fields FUNC_001481 reads, lifted out of BandInterpCtx so the
// producer can be tested without the interpreter.
struct BandLaneFaceCtx {
    int32_t view38 = 0;    // ctx+0x38 (view basis, dot1 x)
    int32_t view3c = 0;    // ctx+0x3c
    int32_t view40 = 0;    // ctx+0x40
    int32_t scale44 = 0;   // ctx+0x44
    int32_t cam48 = 0;     // ctx+0x48 (camera x, dot2 offset)
    int32_t cam4c = 0;     // ctx+0x4c
    int32_t cam50 = 0;     // ctx+0x50
    int32_t shift148 = 0;  // ctx+0x148 (dot2 operand scaling)
    const uint8_t* canned84 = nullptr;  // ctx+0x84: 8 entries x 3 bytes
    const uint8_t* vertData150 = nullptr;  // ctx+0x150: 6-byte entries
    const uint8_t* descData154 = nullptr;  // ctx+0x154: 6-byte entries
};

// FUNC_001480: initialize the band-record table.
//   bandTable: 8 bytes per record, at least count records.
//   ctx_8d: pointer to 6 bytes at ctx+0x8d..0x92; rec0 gets [0x8d,0x8e,0x8f]
//   and rec1 gets [0x90,0x91,0x92] (ASM: movw u16(0x8d) then u8(0x8f)).
//   count: number of records (desc[0x10]); records 2..count-1 get bits 0|1
//   (bit0 = skip, bit1 = dynamic) set; records 0 and 1 get them cleared.
void Func001480(uint8_t* bandTable, int count, const uint8_t* ctx_8d);

// FUNC_001481: resolve one band record (a6) and write it back.
// Returns the record pointer. bit0 of rec[3] = skip (dot2 >= 0), bit1 is
// cleared in both branches (dynamic -> static after resolution). rec[0..2] are
// zeroed when dot1 >= 0, else taken from the ctx+0x84 canned table.
uint8_t* Func001481(const BandLaneFaceCtx& f, uint8_t* rec, uint16_t a6);

// FUNC_001689/001690: zero the +0x14 tag word of the first `count` vertex
// records (36-byte stride) so every resolve re-runs.
void Func001689ResetTags(uint8_t* vertexTable, int count);

// FUNC_001475 (0x58a11): normalize a triple. e = |x| | |y| | |z|; while
// e >= 0x4000: e >>= 1, scale++; out[i] = in[i] >> scale (arithmetic).
// Returns the scale.
int32_t Func001475Normalize(const int32_t in[3], int32_t out[3]);

// FUNC_001685 (0x615d5): the walk camera transform, oracle-verified (R469):
//   scale = Func001475Normalize(triple, tmp); ctx44 = scale
//   e = scale + 7 - shift148; if (e < 0) { ctx44 += -e; tmp[i] >>= (-e) }
//   tmp = -tmp
//   out48 = M . tmp   (Q31 FixedMulHigh)
//   out38 = M . camPos
// Returns ctx44. Matches the captured oc2_01/oc2_06 outputs exactly.
int32_t Func001685Camera(const int32_t triple[3], const int32_t mat[9],
                         const int32_t camPos[3], int32_t shift148,
                         int32_t out38[3], int32_t out48[3]);

// Convenience: a per-call frame with the two tables the ASM arena publishes.
struct BandLaneFrame {
    std::vector<uint8_t> band;     // DATA_009201: 8-byte records
    std::vector<uint8_t> vert;     // DATA_009200: 36-byte records
    int bandCount = 0;
    int vertCount = 0;

    void Resize(int nband, int nvert) {
        band.assign(static_cast<size_t>(nband) * 8, 0);
        vert.assign(static_cast<size_t>(nvert) * 36, 0);
        bandCount = nband;
        vertCount = nvert;
    }
};

} // namespace jjffe
