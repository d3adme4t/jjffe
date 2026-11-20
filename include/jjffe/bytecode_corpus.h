/*
 * Shared v1 bytecode smoke corpus for 62-04c1b.
 * Programs match test/test_bytecode.cpp BuildProgram() for interpreter ops.
 * Dump values only (slot[9], ip_off, prng) — never raw pointers.
 */
#ifndef JJFFE_BYTECODE_CORPUS_H
#define JJFFE_BYTECODE_CORPUS_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace jjffe {
namespace bytecode_corpus {

// v1 golden corpus: all interpreter ops 0x00–0x3C that ASM --bytecode-trace
// reports ok=true under the shared object/string harness.
inline constexpr uint8_t kV1Opcodes[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
};
inline constexpr size_t kV1OpcodeCount =
    sizeof(kV1Opcodes) / sizeof(kV1Opcodes[0]);

// Boot PRNG seeds (match InitGameState / ASM SysInit).
inline constexpr uint32_t kPrngSeed818 = 0x12345678u;
inline constexpr uint32_t kPrngSeed820 = 0x456789abu;

inline void PutI32(std::vector<uint8_t>& b, int32_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

inline void PutLit(std::vector<uint8_t>& b, int32_t v)
{
    b.push_back(0x02);
    PutI32(b, v);
}

inline void PutNLits(std::vector<uint8_t>& b, int n, int32_t v = 0)
{
    for (int i = 0; i < n; ++i)
        PutLit(b, v);
}

inline void PutPushNop(std::vector<uint8_t>& b) { b.push_back(0xFF); }
inline void PutExit(std::vector<uint8_t>& b) { b.push_back(0x00); }

inline std::vector<uint8_t> BuildProgram(uint8_t opcode)
{
    std::vector<uint8_t> b;
    b.reserve(128);
    b.push_back(opcode);

    switch (opcode) {
    case 0x00:
        break;
    case 0x01:
        PutI32(b, 0);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x02:
        PutLit(b, 0);
        PutI32(b, 4);
        PutExit(b);
        break;
    case 0x03:
        PutI32(b, 4);
        PutExit(b);
        break;
    case 0x04:
        PutNLits(b, 6);
        PutExit(b);
        break;
    case 0x05:
        PutExit(b);
        break;
    case 0x06: // FUNC_000326 — harness plants player stub (see InstallVmPlayerStubForOp06)
        PutExit(b);
        break;
    case 0x07:
        PutLit(b, 0);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x08:
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x09:
        PutI32(b, 0);
        break;
    case 0x0a: // text + 4 exprs → FUNC_000101 + push
        b.push_back(0); // empty C string
        PutNLits(b, 4);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x12:
    case 0x14:
    case 0x26:
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x0e:
        PutNLits(b, 3);
        PutExit(b);
        break;
    case 0x0f:
    case 0x11:
    case 0x21:
    case 0x29:
    case 0x2a:
    case 0x2b:
        PutNLits(b, 2);
        if (opcode == 0x29 || opcode == 0x2a || opcode == 0x2b)
            PutPushNop(b);
        PutExit(b);
        break;
    // 0x33/0x34: object index 1 (fixture slot 0 can be a soft path; lit-0
    // warm-SEGVs under DATA_007754→942/926). Same pattern as 0x28 idx=1.
    case 0x33:
        PutLit(b, 1); /* object index */
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x34:
        PutLit(b, 0);
        PutLit(b, 1); /* object index (2nd expr) */
        PutPushNop(b);
        PutExit(b);
        break;
    // 0x28 UpdateShipStats: record[1].flags=0x02 (&1 clear). Index 0 has
    // flags=0x03 → ASM/C++ both null-deref DATA_009092 on the &1 path.
    case 0x28:
        PutLit(b, 0);
        PutLit(b, 1);
        PutExit(b);
        break;
    case 0x10:
    case 0x15:
    case 0x1a:
    case 0x1f:
    case 0x2f:
    case 0x36:
    case 0x37:
        PutNLits(b, 3);
        PutExit(b);
        break;
    case 0x2c:
        PutNLits(b, 2);
        PutExit(b);
        break;
    case 0x13:
        PutLit(b, 0);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1d:
    case 0x20:
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x1b:
    case 0x1c:
    case 0x1e:
    case 0x2e:
        PutNLits(b, 2);
        PutExit(b);
        break;
    case 0x22:
        PutExit(b);
        break;
    case 0x23:
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x24:
        PutNLits(b, 2);
        PutExit(b);
        break;
    case 0x25:
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x27:
        /* Model must be ≥4: ASM FUNC_000773 derefs ship specs* without a
         * null check; models 0–3 leave +0x38 null → SEGV under ffeasm.
         * C++ 773 early-returns on null specs (parity softens that path). */
        PutLit(b, 15); /* Shuttle */
        PutLit(b, 0);  /* name seed */
        PutLit(b, 0);  /* target index */
        PutLit(b, 0);  /* time → 024/023 */
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x2d:
        PutNLits(b, 3);
        PutExit(b);
        break;
    case 0x30:
        PutNLits(b, 5);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x31:
        PutNLits(b, 5);
        PutExit(b);
        break;
    case 0x32:
        PutNLits(b, 9);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x35:
        PutNLits(b, 3);
        PutExit(b);
        break;
    case 0x38:
        PutNLits(b, 4);
        PutExit(b);
        break;
    case 0x39:
        PutLit(b, 0);
        PutExit(b);
        break;
    case 0x3a:
        PutNLits(b, 2);
        PutPushNop(b);
        PutExit(b);
        break;
    case 0x3b:
        PutNLits(b, 5);
        PutExit(b);
        break;
    case 0x3c:
        PutNLits(b, 3);
        PutExit(b);
        break;
    default:
        PutExit(b);
        break;
    }
    while (b.size() < 8)
        b.push_back(0x00);
    return b;
}

struct TraceRecord {
    uint8_t opcode = 0;
    int32_t slot[9] = {};
    int32_t ip_off = 0; // byte offset from program base after run
    uint32_t prng818 = 0;
    uint32_t prng820 = 0;
    bool ok = true;
    const char* note = "";
};

} // namespace bytecode_corpus
} // namespace jjffe

#endif
