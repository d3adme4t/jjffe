#include "jjffe/band_lane_interp.h"
#include <cstdio>
#include <cstdlib>

// R456 / scope (f): interpreter mechanics + resolved ASM table identity.
// DATA_007816[op & 0x1f] resolved against ffeasm.map (R456, dump_tables3.gdb):
//   [00] FUNC_001754  [01] FUNC_001698  [02] FUNC_001702  [03] FUNC_001707
//   [04] FUNC_001711  [05] FUNC_001721  [06] FUNC_001696  [07] FUNC_001706
//   [08] FUNC_001710  [09] FUNC_001738  [10] FUNC_001747  [11] FUNC_001752
//   [12] FUNC_001755  [13] FUNC_001758  [14] FUNC_001749  [15] FUNC_001759
//   [16] FUNC_001815  [17] FUNC_001760  [18] FUNC_001763  [19] FUNC_001756
//   [20] FUNC_001757  [21] FUNC_001697  [22] FUNC_001737  [23] FUNC_001765
//   [24] FUNC_001766  [25] FUNC_001771  [26] FUNC_001762  [27] FUNC_001748
//   [28] FUNC_001741  [29] FUNC_001758  [30] FUNC_001767  [31] FUNC_001874
// DATA_007817 (sub-walk) leading entries:
//   [00] FUNC_001722 (terminator) [01] 001734 [02] 001724 [03] 001730
//   [04] 001733 [05] 001723 [06] 001736 [07] null [08] -1 [09] null
//   [10] null [11] -1 [12] null

namespace jjffe {

// R470: the last seven slots the two band programs dispatch are already ported
// in jjffe_core (src/core/core.cpp) as FUNC_0017xx — the same implementations
// the MeshOp table dispatches for the classic lane. They take the ctx as
// intptr_t (the port's walk-ctx-shaped struct, built by the ship replay).
int16_t* FUNC_001707(intptr_t param_1, int16_t* param_2);
int16_t* FUNC_001706(intptr_t param_1, int16_t* param_2, uint32_t param_3);
int16_t* FUNC_001741(intptr_t param_1, int16_t* param_2, unsigned int param_3);
int16_t* FUNC_001747(intptr_t param_1, int16_t* param_2);
uint8_t* FUNC_001748(intptr_t param_1, char* param_2, unsigned int param_3);
int16_t* FUNC_001752(intptr_t param_1, int16_t* param_2, uint32_t param_3);
int16_t* FUNC_001737(intptr_t param_1, int16_t* param_2);
int16_t* FUNC_001696(intptr_t param_1, int16_t* param_2, uint32_t param_3);
short* FUNC_001702(intptr_t param_1, short* param_2);
int16_t* FUNC_001874(intptr_t param_1, int16_t* param_2);
// R475: the pass handler's projection chain (core ports).
void FUNC_001691(uint16_t* param_1, uint32_t param_2, int32_t param_3);
uint32_t* FUNC_001692(uint32_t* param_1, uint32_t param_2);
void FUNC_001693(uint32_t* param_1, intptr_t param_2, uint32_t param_3,
                uint32_t param_4, int32_t param_5, int32_t param_6, int32_t param_7);
// R473: the DATA_007817 sub-walk handlers (dispatched inside the 001721 pass
// handler), also ported in jjffe_core.
uint8_t* FUNC_001734(intptr_t param_1, uint8_t* param_2);
char* FUNC_001724(intptr_t param_1, char* param_2, uint32_t param_3);
intptr_t FUNC_001730(intptr_t param_1, intptr_t param_2, uint32_t param_3);
uint8_t* FUNC_001733(intptr_t param_1, uint8_t* param_2, uint32_t param_3);
intptr_t FUNC_001723(intptr_t param_1, intptr_t param_2);
uint8_t* FUNC_001736(intptr_t param_1, uint8_t* param_2, uint32_t param_3);

namespace {

// Unported handlers stop the walk immediately (return nullptr); they exist so
// the table identity is faithful even before the bodies are ported.
const uint16_t* StubUnported(BandInterpCtx*, const uint16_t*, uint16_t) {
    return nullptr;
}

#define STUB(name) \
    const uint16_t* name(BandInterpCtx* c, const uint16_t* p, uint16_t o) { \
        (void)c; (void)p; (void)o; return nullptr; \
    }

// Named stubs, one per resolved table slot, so that when a later slice ports a
// handler it only has to replace the stub body and the name stays correct.
STUB(Func001754_Unported)  // [00]
STUB(Func001698_Unported)  // [01]
STUB(Func001702_Unported)  // [02]
STUB(Func001707_Unported)  // [03]
STUB(Func001711_Unported)  // [04] face-quad wrapper (calls 001712)
STUB(Func001721_Unported)  // [05] the PASS handler (enqueues 1794/1796/1797/1799)
STUB(Func001696_Unported)  // [06]
STUB(Func001706_Unported)  // [07]
STUB(Func001710_Unported)  // [08]
STUB(Func001738_Unported)  // [09]
STUB(Func001747_Unported)  // [10]
STUB(Func001752_Unported)  // [11]
// R457 slice 2: ported handlers below replace these stubs.
STUB(Func001755_Unported)  // [12]
STUB(Func001758_Unported)  // [13] also [29]
STUB(Func001749_Unported)  // [14]
STUB(Func001759_Unported)  // [15]
STUB(Func001815_Unported)  // [16]
STUB(Func001760_Unported)  // [17] wrapper that calls 001761
STUB(Func001763_Unported)  // [18]
STUB(Func001756_Unported)  // [19]
STUB(Func001757_Unported)  // [20]
STUB(Func001697_Unported)  // [21]
STUB(Func001737_Unported)  // [22]
STUB(Func001765_Unported)  // [23]
STUB(Func001766_Unported)  // [24]
STUB(Func001771_Unported)  // [25]
STUB(Func001762_Unported)  // [26]
STUB(Func001748_Unported)  // [27]
STUB(Func001741_Unported)  // [28]
STUB(Func001767_Unported)  // [30]
STUB(Func001874_Unported)  // [31]

STUB(Func001722_Unported)  // sub[00] terminator
STUB(Func001734_Unported)  // sub[01]
STUB(Func001724_Unported)  // sub[02]
STUB(Func001730_Unported)  // sub[03]
STUB(Func001733_Unported)  // sub[04]
STUB(Func001723_Unported)  // sub[05]
STUB(Func001736_Unported)  // sub[06]

// ---- R457 slice 2: ported handlers ----

// FUNC_001751 (ASM 0x6591f, decoded R470): band/era gate used by 001755/001752.
// v is the sign-extended int16 operand. Exact ASM:
//   if v16 < 0: rec = table[v&0x7fff] (dynamic via 001481 when bit1 set);
//               return rec[3] & 1
//   if field_c < 0: return 0
//   u = eraA; if (eraA & 1) u = eraB;  shift = field_148 + u
//   if shift <= 0: return (v < field_c) ? 1 : 0
//   v2 = v << shift; if ((v2 >> shift) != v) return 0;
//   return (v2 < field_c) ? 1 : 0
static uint8_t Func001751(BandInterpCtx* c, uint16_t v) {
    const int32_t vs = (int32_t)(int16_t)v;
    if (vs < 0) {
        const uint8_t* rec = nullptr;
        if (c->bandRecords &&
            ((c->bandRecords[3 + (v & 0x7fff) * 8] >> 1) & 1) == 0) {
            rec = c->bandRecords + (v & 0x7fff) * 8;
        } else if (c->bandLookup) {
            rec = c->bandLookup(c, v & 0x7fff);
        }
        return rec ? (rec[3] & 1) : 0;
    }
    if (c->field_c < 0) return 0;
    int32_t u = c->eraA;
    if (u & 1) u = c->eraB;
    const int32_t shift = c->field_148 + u;
    if (shift <= 0) return (vs < c->field_c) ? 1 : 0;
    const uint32_t v2 = (uint32_t)vs << (shift & 0x1f);
    if ((int32_t)(v2 >> (shift & 0x1f)) != vs) return 0;
    return ((int32_t)v2 < c->field_c) ? 1 : 0;
}

// FUNC_001753 (decompile score 60): conditional continue/jump.
// flag != 0: if (op >> 5) return pc + ((int16)op >> 5) * 2, else set stop.
// R470 fix: the ASM writes DATA_007824 = 0 on the zero-jump path, which STOPS
// the 001772 walk (the loop dispatches while the flag is non-zero); the port's
// stopFlag is inverted, so set it to 1 here.
static const uint16_t* Func001753(BandInterpCtx* c, int flag,
                                  const uint16_t* pc, uint16_t op) {
    if (flag != 0) {
        if ((op >> 5) != 0)
            return pc + ((int16_t)op >> 5);   // R470: ASM adds 2*(op>>5) BYTES
        c->stopFlag = 1;
    }
    return pc;
}

// FUNC_001755 (decompile score 30): first opcode of the hull-43 program.
// R470 fix: the ASM passes pc+1 (lea 0x2(%ebx)) to 001753, so the jump base is
// the word AFTER the operand - verified: op 0x042c jumps to word 35 (0x0b2c),
// exactly the second op of the captured 78-op trace.
const uint16_t* Func001755(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    const uint16_t operand = (pc) ? pc[0] : 0;
    const uint8_t r = Func001751(c, operand);
    return Func001753(c, r == 0 ? 1 : 0, pc + 1, op);
}

// FUNC_001756 (ASM 0x65a69, decoded R470): bit-test + conditional jump.
// v = (uint8)pc[0] via 001473; mask = (uint8)pc[1] == 0 ? 0xffffffff
// : 1 << (byte-1 & 0x1f); flag = (mask & v) == 0; then 001753 with pc + 1
// WORD (ASM lea 0x2 = 2 bytes). Slot 0x13.
const uint16_t* Func001756(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    const uint32_t v = c->readVal
        ? c->readVal(c, pc ? (uint8_t)pc[0] : 0) : (pc ? (uint8_t)pc[0] : 0);
    const uint8_t b = pc ? (uint8_t)pc[1] : 0;
    const uint32_t mask = (b == 0) ? 0xffffffffu : (1u << ((b - 1) & 0x1f));
    return Func001753(c, (mask & v) == 0 ? 1 : 0, pc + 1, op);
}

// FUNC_001757 (ASM 0x65ab5): same bit-test, inverted predicate. Slot 0x14.
const uint16_t* Func001757(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    const uint32_t v = c->readVal
        ? c->readVal(c, pc ? (uint8_t)pc[0] : 0) : (pc ? (uint8_t)pc[0] : 0);
    const uint8_t b = pc ? (uint8_t)pc[1] : 0;
    const uint32_t mask = (b == 0) ? 0xffffffffu : (1u << ((b - 1) & 0x1f));
    return Func001753(c, (mask & v) != 0 ? 1 : 0, pc + 1, op);
}

// FUNC_001758 (ASM 0x65b01, decoded R475): the program ALU. Reads two BYTE
// operands via FUNC_001473, dispatches on (op>>4)&0xf, writes
// (result & 0xffff) ZERO-EXTENDED to regs[(op>>8)&7], advances one word.
const uint16_t* Func001758(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    const uint32_t v1 = c->readVal
        ? c->readVal(c, pc ? (uint8_t)pc[0] : 0) : (pc ? (uint8_t)pc[0] : 0);
    const uint32_t v2 = c->readVal
        ? c->readVal(c, pc ? (uint8_t)pc[1] : 0) : (pc ? (uint8_t)pc[1] : 0);
    uint32_t r;
    switch ((op >> 4) & 0xf) {
        case 0x0: r = v1 + v2; break;                       // addl
        case 0x1: r = v1 - v2; break;                       // subl
        case 0x2: r = v1 * v2; break;                       // imull
        case 0x3:                                                   // idivl guard
            if (v2 == 0) r = v1;
            else r = (uint32_t)((int32_t)v1 / (int32_t)v2);
            break;
        case 0x4: r = v1 >> (v2 & 0x1f); break;              // shrl
        case 0x5: r = v1 << (v2 & 0x1f); break;              // shll
        case 0x6: r = ((int32_t)v2 < (int32_t)v1) ? v1 : v2; break;  // max (signed)
        case 0x7: r = ((int32_t)v2 <= (int32_t)v1) ? (v2 - 1) : v1; break; // min-1
        case 0x8:                                                   // 16.16 div
            if (v2 == 0) r = v1;
            else r = (uint32_t)(((int64_t)(int32_t)v1 << 16) / (int32_t)v2);
            break;
        case 0x9: r = (int32_t)v1 >> (v2 & 0x1f); break;     // sarl
        case 0xa:
            r = (c->table_14c) ? c->table_14c[0x9c / 2 + (v1 + v2)] : 0;
            break;
        case 0xb: r = ((int32_t)v1 < (int32_t)v2) ? 0 : v1; break;
        case 0xc: r = ((int32_t)v2 < (int32_t)v1) ? 0 : v1; break;
        case 0xd:
        case 0xe:
            // FUNC_001914_Sin32 + FUNC_001521 - TODO once 001521 is ported.
            r = v1;
            break;
        default: r = v1 & v2; break;                         // andl
    }
    c->regs[(op >> 8) & 7] = r & 0xffffu;   // zero-extended store (R475)
    return pc + 1;                           // ASM lea 0x2(%esi) = 1 word
}

// ---- R470: bridges to the jjffe_core FUNC_0017xx ports ----

// FUNC_001707 (slot 0x03): face-triple dispatch; always advances 3 words.
static const uint16_t* Func001707Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 3;
    int16_t* next = FUNC_001707((intptr_t)c->opaque, (int16_t*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001706 (slot 0x07): tri-face with parity toggle; advances like 001707.
static const uint16_t* Func001706Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 3;
    int16_t* next = FUNC_001706((intptr_t)c->opaque, (int16_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001696 (slot 0x06, R475): the unified-z barrier hub; next pc from the
// port.
static const uint16_t* Func001696Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001696((intptr_t)c->opaque, (int16_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001702 (slot 0x02, R475): edge/line command; advances 2 words.
static const uint16_t* Func001702Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 2;
    short* next = FUNC_001702((intptr_t)c->opaque, (short*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001874 (slot 0x1e, R475): sat-tree record; next pc from the port.
static const uint16_t* Func001874Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001874((intptr_t)c->opaque, (int16_t*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001741 (slot 0x1c): matrix set; next pc from the port.
static const uint16_t* Func001741Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001741((intptr_t)c->opaque, (int16_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001747 (slot 0x0a): text/console slot; next pc from the port.
static const uint16_t* Func001747Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001747((intptr_t)c->opaque, (int16_t*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001748 (slot 0x1b): sprite record; next pc from the port.
static const uint16_t* Func001748Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    uint8_t* next = FUNC_001748((intptr_t)c->opaque, (char*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001752 (slot 0x0b): record emitter; next pc from the port.
static const uint16_t* Func001752Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001752((intptr_t)c->opaque, (int16_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

// FUNC_001754 (slot 0x00): the program terminator - set the stop flag so the
// 001772 walk exits (ASM DATA_007824), do not advance.
static const uint16_t* Func001754Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)pc; (void)op;
    if (c) c->stopFlag = 1;
    return nullptr;
}

// FUNC_001737 (slot 0x16, R475): quad-point command builder; next pc from the
// port.
static const uint16_t* Func001737Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 1;
    int16_t* next = FUNC_001737((intptr_t)c->opaque, (int16_t*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}

// ---- R473: sub-walk bridges (DATA_007817, inside the pass handler) ----

static const uint16_t* Sub001734Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 1;
    uint8_t* next = FUNC_001734((intptr_t)c->opaque, (uint8_t*)pc);
    return reinterpret_cast<const uint16_t*>(next);
}
static const uint16_t* Sub001724Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    char* next = FUNC_001724((intptr_t)c->opaque, (char*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}
static const uint16_t* Sub001730Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    intptr_t next = FUNC_001730((intptr_t)c->opaque, (intptr_t)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}
static const uint16_t* Sub001733Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    uint8_t* next = FUNC_001733((intptr_t)c->opaque, (uint8_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}
static const uint16_t* Sub001723Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !c->opaque) return pc + 1;
    intptr_t next = FUNC_001723((intptr_t)c->opaque, (intptr_t)pc);
    return reinterpret_cast<const uint16_t*>(next);
}
static const uint16_t* Sub001736Bridge(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    if (!c || !c->opaque) return pc + 1;
    uint8_t* next = FUNC_001736((intptr_t)c->opaque, (uint8_t*)pc, op);
    return reinterpret_cast<const uint16_t*>(next);
}

} // namespace

// ---- R458 slice 3: value file + queue ----

// FUNC_001473 (ASM 0x58954, verified R471): the value decoder the bit-tests
// and ALU read through.
//   sel 0x00: v & 0x3f
//   sel 0x40: (int32)(int16)(v & 0x3f) << 10   (ASM: and ax; cwtl; shl 10)
//   sel 0x80: (u16)(*(ctx+0x14c) + 0x9c + (v & 0x3f) * 2)
//   sel 0xc0: *(u32*)(ctx + 0x54 + (v & 0x3f) * 4)
uint32_t Func001473(BandInterpCtx* c, uint32_t v) {
    const uint32_t sel = v & 0xc0;
    if (sel == 0x00) return v & 0x3f;
    if (sel == 0x40)
        return (uint32_t)((int32_t)(int16_t)(uint16_t)(v & 0x3f) << 10);
    if (sel == 0x80) {
        if (!c || !c->table_14c) return 0;
        return (uint32_t)c->table_14c[0x9c / 2 + (v & 0x3f)];
    }
    if (sel != 0xc0) return 0;
    if (!c) return 0;
    return (uint32_t)c->regs[v & 0x3f & 7];  // regs[8] mirrors the +0x54 file
}

// FUNC_001778 (score 40): arena allocator with link-at-head semantics.
uint32_t* Func001778(BandQueue& q, int words, uint32_t key, int link) {
    uint32_t* p = q.next;
    if (link != 0 || q.flag == 0) {
        p[0] = key;
        p[1] = (uint32_t)(uintptr_t)q.head;
        q.head = p;
        p += 2;
    }
    q.next = p + words;
    return p;
}

// FUNC_001777 (score 60, tail TODO): key = 1 when z < 1 else ((|x|+|y|)>>3)+z.
// The +0x130 shift-headroom tail is not ported (garbled in the decompile);
// oracle-verify before wiring.
uint32_t Func001777(BandInterpCtx* c, const int32_t* xyz) {
    (void)c;
    if (!xyz) return 1;
    if (xyz[2] < 1) return 1;
    const int32_t ax = xyz[0] < 0 ? -xyz[0] : xyz[0];
    const int32_t ay = xyz[1] < 0 ? -xyz[1] : xyz[1];
    return (uint32_t)(((ax + ay) >> 3) + xyz[2]);
}

// ---- R459 slice 4: the pass handler (structure) + face wrapper ----

// FUNC_001721 (ASM 0x63859, decoded R475): the PASS opcode handler.
//   band = (uint8)pc[1] & 0x7f; rec = table[band] (dynamic -> 001481).
//   color = (int16)pc[0]; p = pc + 2.
//   bit0 set: SKIP -> return p + (int8)p[-1]   (byte-relative jump!)
//   else: shade = 001692(color); projected = 001693(shade...); rgb = 001691.
//         field_118 = 0; field_128 = (int8)p[0] & 0x80 (sign-extended u8!).
//         slot_12c = 001778(1, key, 1); *slot_12c = 001794 marker.
//         sub-walk on CURRENT word (DATA_007817[w>>9]) until the 001722
//         terminator (break before calling it).
//         -1 -> unlink + 001813 record; else 001731/001718 then 001799 record
//         with rgb16 = DPal16(rgb).
const uint16_t* Func001721(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (!c || !pc) return nullptr;
    BandInterpTable* t = c->activeTable;
    const uint8_t band = (uint8_t)(pc[1] & 0x7f);
    const uint8_t* rec = nullptr;
    if (c->bandRecords && ((c->bandRecords[3 + band * 8] >> 1) & 1) == 0) {
        rec = c->bandRecords + band * 8;
    } else if (c->bandLookup) {
        rec = c->bandLookup(c, band);
    }
    if (getenv("AB_BANDINTERP_DEBUG"))
        fprintf(stderr, "    [721 band=%u rec=%p r3=%02x]\n", band, (void*)rec,
                rec ? rec[3] : 0xff);
    if (!rec) return pc + 3;                       // conservative advance
    const int16_t color = (int16_t)pc[0];
    const uint16_t* p = pc + 2;
    if ((rec[3] & 1) != 0) {
        // ASM skip: ebx (word ptr, already at pc+2) += (int8)[-1(%ebx)] BYTES.
        const int8_t off = (int8_t)((const uint8_t*)p)[-1];
        return reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(p) + off);
    }
    // rgb via the projection chain (001692 unpack, 001693 project, 001691
    // shade byte) - faithful when opaque is a real walk ctx.
    uint16_t local_a[4] = {};
    if (c->opaque) {
        uint32_t built[4] = {};
        uint32_t projected[4] = {};
        FUNC_001692(built, (uint32_t)color);
        FUNC_001693(projected, (intptr_t)c->opaque, built[0], built[1],
                    (int32_t)built[2], (int32_t)built[3],
                    (int32_t)*(const uint32_t*)rec);
        FUNC_001691(local_a, projected[0], (int32_t)projected[1]);
    }
    c->field_118 = 0;
    c->field_128 = (int32_t)(int8_t)((const uint8_t*)p)[0] & 0x80;
    // The 001778 record for this pass; its address lives at ctx+0x12c.
    if (c->queue) {
        uint32_t* r = Func001778(*c->queue, 1, 0, 1);
        c->slot_12c = r;
        if (r) r[0] = 0;  // FUNC_001794 marker (handler ptr oracle-filled)
    }
    // Sub-walk (ASM 0x63988): advance ebx by one word FIRST, dispatch on the
    // pre-advance word's >>9; the handler gets the advanced pc and returns the
    // next. Break when the table entry IS the 001722 terminator address.
    while (t) {
        const uint16_t w = *p;               // pre-advance word
        const uint16_t* np = p + 1;          // ASM add $0x2(%ebx)
        const uint32_t idx = w >> 9;
        if (idx >= 13 || !t->sub[idx]) { p = np; break; }   // terminator/absent
        const uint16_t* nxt = t->sub[idx](c, np, w);
        if (!nxt) { p = np; break; }
        p = nxt;
    }
    if (c->field_128 == -1) {
        if (c->unlink779 && c->slot_12c) c->unlink779(c->slot_12c);
        uint32_t* p2 = c->alloc787 ? c->alloc787(1) : nullptr;
        if (p2) p2[0] = 0;  // FUNC_001813 marker
    } else {
        if (c->field_104 != 0) {
            // FUNC_001731 walk + FUNC_001718 on 0x10c != 0 - oracle TODO
        }
        uint32_t* p2 = c->alloc787 ? c->alloc787(2) : nullptr;
        if (p2) {
            p2[0] = 0;  // FUNC_001799 marker
            p2[1] = c->dpal16 ? (uint32_t)c->dpal16(local_a[3]) & 0xffff
                              : (uint32_t)local_a[3];
        }
    }
    return p;
}

// FUNC_001711 (score 15): face-opcode wrapper - delegates to FUNC_001712 via
// the ctx hook until the real port lands.
const uint16_t* Func001711(BandInterpCtx* c, const uint16_t* pc, uint16_t op) {
    (void)op;
    if (c && c->face712) return c->face712(c, pc, *pc);
    return pc + 1;
}

// ---- R462: the face handler FUNC_001712 (gate structure) ----

// FUNC_001712 (decompile score 40): gate structure ported, heavy parts hooked.
//  1. L1 perimeter sum over the four verts; the TEXTURED 1805 path requires
//     sum > 5 AND the FUNC_001717 era test on all four verts not hitting the
//     -0x9c3c7 magic ((u|v) * 8 == -0x9c3c7 returns 1 = skip).
//  2. Otherwise, if all four verts are on-screen (x < 0x140, y < 0x9e), the
//     FLAT 1809 path is taken (with the ramp/shade block for the textured
//     variant left to the material-table hook).
//  3. Off-screen falls back to FUNC_001713 (all off) / FUNC_001714.
const uint16_t* Func001712(BandInterpCtx* c, const uint16_t* rec, uint16_t op) {
    (void)op;
    if (!c || !rec) return nullptr;
    int16_t v[4][2] = {};
    if (c->resolveQuad) c->resolveQuad(c, rec, v);
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        const int dx = (int)v[(i + 1) % 4][0] - (int)v[i][0];
        const int dy = (int)v[(i + 1) % 4][1] - (int)v[i][1];
        sum += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    }
    if (sum > 5) {
        // Textured path: era test gate (FUNC_001717), magic -0x9c3c7 = skip.
        if (c->eraTest717) {
            int acc = 0;
            for (int i = 0; i < 4; i++) acc |= c->eraTest717(c, v[i]);
            if (acc * 8 == -0x9c3c7) return rec + 1;   // skip (oracle fixes N)
        }
        // Enqueue the textured 1805 record (words=13) via the material table.
        if (c->queue && c->materialTable) {
            const uint32_t key = Func001777(c, nullptr);  // oracle fixes coords
            uint32_t* job = Func001778(*c->queue, 13, key, 1);
            if (job) job[0] = 0;  // FUNC_001805 marker (oracle fills handler)
        }
        return rec + 4;
    }
    // Flat path: all four verts on-screen -> enqueue FLAT 1809 (words=6).
    bool on = true;
    for (int i = 0; i < 4; i++)
        if (!(v[i][0] >= 0 && v[i][0] < 0x140 && v[i][1] >= 0 && v[i][1] < 0x9e))
            on = false;
    if (on && c->queue) {
        const uint32_t key = Func001777(c, nullptr);
        uint32_t* job = Func001778(*c->queue, 6, key, 1);
        if (job) job[0] = 0;  // FUNC_001809 marker (oracle fills handler)
    }
    return rec + 4;
}

void BandInterpInitTable(BandInterpTable& t) {
    using H = BandInterpTable::Handler;
    const H ops[32] = {
        Func001754Bridge,  Func001698_Unported, Func001702Bridge,   Func001707Bridge,
        Func001711,        Func001721,         Func001696Bridge,    Func001706Bridge,
        Func001710_Unported, Func001738_Unported, Func001747Bridge,  Func001752Bridge,
        Func001755,        Func001758,         Func001749_Unported, Func001759_Unported,
        Func001815_Unported, Func001760_Unported, Func001763_Unported, Func001756,
        Func001757,        Func001697_Unported, Func001737Bridge,  Func001765_Unported,
        Func001766_Unported, Func001771_Unported, Func001762_Unported, Func001748Bridge,
        Func001741Bridge,  Func001758,         Func001767_Unported, Func001874Bridge,
    };
    for (int i = 0; i < 32; i++) t.op[i] = ops[i];
    const H subs[13] = {
        Func001722_Unported, Sub001734Bridge, Sub001724Bridge, Sub001730Bridge,
        Sub001733Bridge, Sub001723Bridge, Sub001736Bridge,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    };
    for (int i = 0; i < 13; i++) t.sub[i] = subs[i];
}

const uint16_t* BandInterpStep(BandInterpTable& t, BandInterpCtx& ctx, const uint16_t* pc) {
    if (!pc) return nullptr;
    const uint16_t op = *pc;
    const uint32_t slot = op & 0x1f;
    if (slot >= 32 || !t.op[slot]) return nullptr;
    return t.op[slot](&ctx, pc + 1, op);
}

void BandInterpRun(BandInterpTable& t, BandInterpCtx& ctx, const uint16_t* pc) {
    // FUNC_001772: dispatch while DATA_007824 == 0, then set it to 1.
    while (ctx.stopFlag == 0) {
        const uint16_t* next = BandInterpStep(t, ctx, pc);
        if (!next) break;
        pc = next;
    }
    ctx.stopFlag = 1;
}

bool BandInterpHasUnported(const BandInterpTable& t) {
    // True while any slot is a stub. R457: 001758/001755 (and their helpers)
    // are ported; everything else is still a stub, so this stays true until the
    // last handler lands.
    (void)t;
    return true;
}

} // namespace jjffe
