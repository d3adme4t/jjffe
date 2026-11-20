/*
 * Batch 5a — named, address-independent bytecode VM state digests.
 *
 * Hashes fixed-size region *contents* (FNV-1a). Pointer-valued dwords inside
 * DATA_008569 are replaced with a null/non-null surrogate before hashing so
 * ASM vs C++ addresses never leak into the digest.
 *
 * Object-setup scaffolding (260908): object-list digest is separate (see
 * bytecode_object_fixture.h + HashVmObjectListDigest) so the 6 VM regions /
 * golden-39 corpus stay untouched.
 *
 * Both harnesses (ffeasm --bytecode-trace and GoldenRecordBytecode) must:
 *   1. ResetDigestRegions(...) before FUNC_000041
 *   2. HashDigestRegions(...) after
 */
#ifndef JJFFE_BYTECODE_DIGEST_H
#define JJFFE_BYTECODE_DIGEST_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace jjffe {
namespace bytecode_digest {

inline constexpr size_t kRegionCount = 6;

inline constexpr const char* kRegionNames[kRegionCount] = {
    "DATA_009095", "DATA_008908", "DATA_009093",
    "DATA_008569", "DATA_009085", "DATA_009148",
};

/* Canonical byte counts (ASM extents / agreed windows). */
inline constexpr size_t kRegionSizes[kRegionCount] = {
    24,  /* DATA_009095 — 6 dwords ship-stat window */
    24,  /* DATA_008908 */
    14,  /* DATA_009093 — ffedat resb 14 */
    280, /* DATA_008569 — 5 * 0x38 */
    312, /* DATA_009085 — 6 * 0x34 */
    612, /* DATA_009148 — ffedat resb 612 */
};

struct RegionPtrs {
    uint8_t* p009095;
    uint8_t* p008908;
    uint8_t* p009093;
    uint8_t* p008569;
    uint8_t* p009085;
    uint8_t* p009148;
};

inline uint8_t* RegionAt(const RegionPtrs& r, size_t i)
{
    switch (i) {
    case 0: return r.p009095;
    case 1: return r.p008908;
    case 2: return r.p009093;
    case 3: return r.p008569;
    case 4: return r.p009085;
    case 5: return r.p009148;
    default: return nullptr;
    }
}

inline void ResetDigestRegions(const RegionPtrs& r)
{
    for (size_t i = 0; i < kRegionCount; ++i) {
        uint8_t* p = RegionAt(r, i);
        if (p)
            std::memset(p, 0, kRegionSizes[i]);
    }
}

inline uint32_t Fnv1a32(const uint8_t* data, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* Scrub mission-cluster list heads (+0x30, +0x34 per 0x38 record) to
 * null/non-null surrogate (0 or 1) — never raw pointer bits. */
inline void ScrubMissionCluster(uint8_t* dst, const uint8_t* src)
{
    std::memcpy(dst, src, 280);
    for (int rec = 0; rec < 5; ++rec) {
        uint8_t* base = dst + rec * 0x38;
        for (int off : {0x30, 0x34}) {
            uint32_t head = 0;
            std::memcpy(&head, base + off, 4);
            uint32_t sur = head ? 1u : 0u;
            std::memcpy(base + off, &sur, 4);
        }
    }
}

inline void HashDigestRegions(const RegionPtrs& r, uint32_t out[kRegionCount])
{
    uint8_t missionBuf[280];
    for (size_t i = 0; i < kRegionCount; ++i) {
        const uint8_t* p = RegionAt(r, i);
        if (!p) {
            out[i] = 0;
            continue;
        }
        if (i == 3) {
            ScrubMissionCluster(missionBuf, p);
            out[i] = Fnv1a32(missionBuf, kRegionSizes[i]);
        } else {
            out[i] = Fnv1a32(p, kRegionSizes[i]);
        }
    }
}

/*
 * C++ port: data_globals.h uses `static` per TU, so the golden test MUST NOT
 * hash its own copies. These entry points live in core.cpp (same TU as
 * FUNC_000041) and see the real VM globals. ASM harness uses RegionPtrs on
 * ffedat BSS directly.
 *
 * DATA_009095 (Batch 5b): ASM ship-stats live at DATA_008804+0x50f8.
 * C++ HashVmDigestRegions hashes g_bss8804+0x50f8 — same window
 * FUNC_000236 writes via the DATA_009095 alias.
 */
void ResetVmDigestRegions();
void HashVmDigestRegions(uint32_t out[kRegionCount]);

/*
 * Object-setup scaffolding — install fixed raw-338 PhysObj list into
 * DATA_009133 / DATA_007758, hash with owner/target pointer scrub.
 * Implemented in core.cpp (same TU as DATA_009133).
 */
bool InstallVmObjectFixture();
uint8_t* VmObjectFixtureBase();
uint32_t HashVmObjectListDigest();

/*
 * Op 0x06 (FUNC_000326): set DATA_009054=2 (case → FUNC_000453) and
 * DATA_008996=4 (453 early-out). Avoids default 335→FlipScreen hang after
 * SysInit fork and null-DATA_008862 SEGV in 564.
 * Call ClearVmPlayerStubForOp06() on every other opcode — 8996 leaks into
 * TableLookup635 (type 0/1) and poisons op 0x13 model picks in-process.
 */
bool InstallVmPlayerStubForOp06();
void ClearVmPlayerStubForOp06();

/*
 * Ops 0x23/0x24/0x2D: DATA_007639 string-expand needs a writable cursor at
 * DATA_008558. Lit-0 SysInit leaves it null → ASM ExpandBracketCodes SEGV.
 */
bool InstallVmStringCursorForExpandOps();

} // namespace bytecode_digest
} // namespace jjffe

#endif
