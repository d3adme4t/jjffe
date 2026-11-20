// Task 4.168: narrow header for the below-4G arena machinery, extracted from
// data_globals.h so that TUs needing only the span (e.g. sysgen_proc.cpp) don't
// flood their .bss with hundreds of unused static DATA globals.
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>

namespace jjffe {

// Zeroed storage for ASM label DATA_<label> at ffeasm's address for it, or null.
// Only test_logic_replay_lowmem defines it (task 4.145); in every other binary the
// weak reference is null and the arenas are mmap'd as before.
extern "C" void* JjffeAsmArenaPlace(int label, std::size_t size) __attribute__((weak));

// The Below4G arenas' address ranges: a 32-bit field holding one of these
// addresses is a whole pointer even when the next dword is not its high half.
struct Below4GSpan { uintptr_t first, end; };
inline Below4GSpan* Below4GSpans() { static Below4GSpan s[16] = {}; return s; }
inline int& Below4GSpanCount() { static int n = 0; return n; }
inline bool InBelow4G(uintptr_t a) {
    for (int i = 0; i < Below4GSpanCount(); ++i)
        if (a >= Below4GSpans()[i].first && a < Below4GSpans()[i].end) return true;
    return false;
}

// Zeroed storage below 4 GiB, one instance per (T, Label) across all TUs:
// for ASM tables that the game links with 32-bit pointers.
template <typename T, int Label>
inline T& Below4G() {
    static T* p = [] {
        void* m = JjffeAsmArenaPlace ? JjffeAsmArenaPlace(Label, sizeof(T)) : nullptr;
        if (!m)
            m = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (m == MAP_FAILED) {
            m = new T();
        } else if (Below4GSpanCount() < 16) {
            const uintptr_t a = reinterpret_cast<uintptr_t>(m);
            Below4GSpans()[Below4GSpanCount()++] = {a, a + sizeof(T)};
        }
        return reinterpret_cast<T*>(m);
    }();
    return *p;
}

// ffedat DATA_008804..DATA_009121 (resb runs, 0x556c bytes): the state span
// FUNC_000170 / FUNC_000171 save and restore as one 0x556a-byte run.
// Every label in it is a reference at its ASM offset.
using Bss8804 = uint8_t[0x556c];
static Bss8804& g_bss8804 = Below4G<Bss8804, 8804>();

// Task 4.184: ffedat places some dwords at byte offsets that are NOT 4-aligned (its labels are
// byte-aligned). Binding a plain `uint32_t&`/`int32_t&` there is UB in C++ even though x86 executes
// the unaligned access correctly, and UBSan reports both the binding and every access through it
// (measured: 3503 reports before this change). These 1-aligned alias types state what is actually
// true - the access is unaligned - so neither the optimiser nor UBSan assumes 4-byte alignment.
// A `u32_unaligned*` still converts implicitly to `uint32_t*`, so existing `&label` use sites keep
// compiling unchanged; that is what keeps this a binding-only change.
// See docs/SPAN_ALIGNMENT_PLAN.md for the measured before/after and the residual.
typedef uint32_t u32_unaligned __attribute__((aligned(1), may_alias));
typedef int32_t  i32_unaligned __attribute__((aligned(1), may_alias));

} // namespace jjffe
