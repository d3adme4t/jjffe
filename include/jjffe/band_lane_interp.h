#pragma once
#include <cstdint>

// R456 / scope (f): port of the band-lane bytecode interpreter FUNC_001772
// (ffe.asm 0x1772) and its dispatch tables. Slice 1 lands the interpreter
// mechanics and the resolved ASM table identity; the handlers themselves are
// explicit stubs to be ported in later slices. Nothing in this header is wired
// into the renderer yet (gated work in progress).

namespace jjffe {

struct BandInterpTable;
struct BandQueue;

// Placeholder for the ASM's param_1 context (ship render state, band arena,
// queue). Fleshed out as the handlers that read it are ported.
struct BandInterpCtx {
    void* opaque = nullptr;
    // DATA_007824 - the walk loops while this is 0 and is set to 1 on exit.
    int32_t stopFlag = 0;
    // Register file at +0x54: eight 32-bit slots written by the ALU handler
    // FUNC_001758 as (result & 0xffff) ZERO-EXTENDED (ASM mov %eax,0x54(...)),
    // indexed by (op >> 8) & 7. R475: u32 not u16 - negative ALU results read
    // back as large positives through the value decoder.
    uint32_t regs[8] = {};
    // Fields read by FUNC_001751 (era/band gates), offsets from the ASM ctx.
    int32_t field_c = -1;        // +0x0c
    int32_t field_148 = 0;       // +0x148
    int32_t field_130 = 0;       // +0x130 (FUNC_001777 key scale)
    int32_t eraA = 0;            // DATA_008812
    int32_t eraB = 0;            // DATA_008813
    uint8_t* bandRecords = nullptr;   // DATA_009201, 8 bytes per band
    const uint16_t* table_14c = nullptr; // +0x14c table read by ALU case 10
    // FUNC_001473 value read (two operands of the ALU opcode).
    uint32_t (*readVal)(BandInterpCtx*, uint16_t) = nullptr;
    // DATA_007721 band-record lookup for negative band words.
    const uint8_t* (*bandLookup)(BandInterpCtx*, uint16_t) = nullptr;
    // +0x118 / +0x128 / +0x104 / +0x10c / +0x110 fields the pass handler reads.
    int32_t field_118 = 0;
    int32_t field_128 = 0;
    int32_t field_104 = 0;
    int32_t field_10c = 0;
    int32_t field_110 = 0;
    uint32_t* slot_12c = nullptr;  // +0x12c: the record ptr written by 001721
    BandInterpTable* activeTable = nullptr;  // so handlers reach the sub-walk
    // Hooks for the functions the pass handler calls (oracle-verified later):
    // FUNC_001692 edge projection -> rgb via 001691; FUNC_007629 DPal16;
    // FUNC_001787 record allocator; FUNC_001779 record unlink.
    uint16_t (*dpal16)(uint32_t) = nullptr;
    uint32_t* (*alloc787)(int words) = nullptr;
    void (*unlink779)(uint32_t*) = nullptr;
    // R475: the face handler's vertex resolve (ASM DATA_007727 = FUNC_001471):
    // returns the resolved vertex-record pointer for a band index.
    const uint16_t* (*resolveVertex)(BandInterpCtx*, uint8_t idx) = nullptr;
    // FUNC_001712 face handler (real port lands after the oracle slice).
    const uint16_t* (*face712)(BandInterpCtx*, const uint16_t*, uint16_t) = nullptr;
    // FUNC_001712 hooks: vertex resolution, era test (001717), enqueue.
    // resolveQuad fills 4 screen verts from the face's operand record.
    void (*resolveQuad)(BandInterpCtx*, const uint16_t* rec, int16_t out[4][2]) = nullptr;
    int (*eraTest717)(BandInterpCtx*, const int16_t v[2]) = nullptr;
    BandQueue* queue = nullptr;   // 001778 allocations go here
    const uint8_t* materialTable = nullptr;  // DATA_007811, 0x1e stride
};

struct BandInterpTable {
    // Handler shape per the ASM: (ctx, pc + 1, op) -> next pc, or nullptr to
    // stop the walk. cdecl pointers, byte* return in the original.
    using Handler = const uint16_t* (*)(BandInterpCtx*, const uint16_t*, uint16_t);
    // DATA_007816 - dispatched on (op & 0x1f); 32 entries, resolved to symbols.
    Handler op[32];
    // DATA_007817 - the sub-walk table (index = word >> 9) used inside the pass
    // handler FUNC_001721 until the FUNC_001722 terminator breaks; the resolved
    // leading 13 entries.
    Handler sub[13];
};

// Bind every slot to its resolved ASM symbol name (stub body for unported ones).
void BandInterpInitTable(BandInterpTable& t);

// One dispatch step of FUNC_001772; returns the next pc or nullptr to stop.
const uint16_t* BandInterpStep(BandInterpTable& t, BandInterpCtx& ctx, const uint16_t* pc);

// The full FUNC_001772 walk: dispatch while ctx.stopFlag == 0, then set it.
void BandInterpRun(BandInterpTable& t, BandInterpCtx& ctx, const uint16_t* pc);

// R457 slice 3: the enqueue arena (FUNC_001778). Nodes are {key, next}
// headers; the list head is DATA_007832 and the free pointer DATA_007830.
struct BandQueue {
    uint32_t arena[4096];
    uint32_t* next = arena;   // DATA_007830
    uint32_t* head = nullptr; // DATA_007832
    int flag = 0;             // DATA_007833
};

// FUNC_001473 (score 60): value decoder - literal / shifted literal / table /
// register, selected by (v & 0xc0).
uint32_t Func001473(BandInterpCtx* c, uint32_t v);

// FUNC_001778 (score 40): allocate `words` u32 of payload; when link != 0 or
// flag == 0, prepend a {key, old-head} header and move the head (LIFO queue).
uint32_t* Func001778(BandQueue& q, int words, uint32_t key, int link);

// FUNC_001721 (score 40, pointer arithmetic marked oracle-TODO): the PASS
// opcode handler. Control flow is faithful; heavy callees go through the ctx
// hooks above. Returns the next pc.
const uint16_t* Func001721(BandInterpCtx* c, const uint16_t* pc, uint16_t op);

// FUNC_001711 (score 15): face-opcode wrapper around FUNC_001712.
const uint16_t* Func001711(BandInterpCtx* c, const uint16_t* pc, uint16_t op);

// FUNC_001712 (decompile score 40, gate structure): the face handler. Flat 1809
// iff all four verts on-screen; textured 1805 iff the L1 perimeter sum > 5 and
// the FUNC_001717 era test does not hit the -0x9c3c7 magic. Heavy parts hooked.
const uint16_t* Func001712(BandInterpCtx* c, const uint16_t* rec, uint16_t op);

// FUNC_001777 (score 60, tail TODO): sort-key computation from a coordinate
// triple; 1 when z < 1, else ((|x|+|y|) >> 3) + z, then a +0x130-dependent
// shift headroom test (not yet ported - oracle-verify before wiring).
uint32_t Func001777(BandInterpCtx* c, const int32_t* xyz);

// True while any slot still points at an unported stub.
bool BandInterpHasUnported(const BandInterpTable& t);

} // namespace jjffe
