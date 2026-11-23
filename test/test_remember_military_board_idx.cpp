// Standalone test for AndyJ RememberMilitaryBoardIdx, quick task 261122-kyo.
//
// AJ changelog L732-735, cfg case 6 (cfg byte 0x7a39d5, gate 0x7a5cfe). Default
// OFF here. The sole reader of the gate is AJ 0x41b442 in AJ fn 0x41b3b2, which
// is FUNC_000493 (the military board list builder shared by FUNC_000490 /
// FUNC_000491, also reached by FUNC_000515 "back" and FUNC_000326 case 7).
//
// FUNC_000493 starts with FUNC_000342, which zeroes DATA_009050 (the list's
// top-row scroll index): that is the NASM "reset". With the gate on, after the
// list is rebuilt AJ sets DATA_009050 from a new AJ bss dword 0x899f6d. The port
// keeps it in the core.cpp static s_aj899f6d, exposed to tests through the
// OracleLabelStorageTable label "AJ_899f6d" (4 bytes).
//
// Writes to the static are AJ's ungated stores: FUNC_000355 on every successful
// scroll, zero in FUNC_000320 (param < 2), FUNC_000323, FUNC_000324, FUNC_000490,
// FUNC_000491, FUNC_000998, and in FUNC_000493 on an empty list. Only the restore
// is gated. The clamp is AJ's bytes (0x41b450..0x41b472), not "0..count":
//     idx > count -> count (static := count)
//     idx < 0     -> 1     (static := 1)
//     else idx as-is (idx == count kept)
// where count is DATA_009051 before the 0x983f trailer row is appended.
//
// Why only DATA_007639 is stubbed: the flag-off chain from FUNC_000490 /
// FUNC_000491 / FUNC_000515 (RESEARCH section 4) reaches no other non-null
// function pointer in a test process, but FUNC_000352 -> FUNC_000350 calls the
// real string expander, and FUNC_000271 then strnlen's its uninitialised 2 KiB
// buffer. The stub writes a NUL and records the format id, so the title choice
// (0x990a non-empty / 0x990b empty) is visible.
//
// Why FUNC_000323 / FUNC_000324 / FUNC_000998 are not called: they play sound,
// go through FUNC_000349 -> DATA_004920 or spawn objects. FUNC_000320(2)
// allocates a BST node. Their s_aj899f6d writes are checked structurally.
//
// Why groups 1-3 come before any label lookup: before the feature exists the
// AJ_899f6d label is missing, so the RED run must fail on behaviour (the first
// flag-on assert in group 2), not on a missing label.
//
// DATA_008692 (visible rows) is a per-TU header static; core.cpp's copy is
// written through FindLabel. FUNC_000494 -> FUNC_000327 rewrites it to
// count - DATA_009050 after every list build, so ScrollTo() sets it to 1 first.

#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sys/mman.h>

#include "jjffe/core/patch_gates.h"
#include "jjffe/data_globals.h"
#include "jjffe/asm_label_table.h"

namespace jjffe {
uint32_t FUNC_000355(int);
void FUNC_000490(intptr_t);
void FUNC_000491(intptr_t);
void FUNC_000493(int32_t*);
void FUNC_000515(int32_t*);
void FUNC_000326(void);
void FUNC_000320(uint8_t);
}

namespace {

using jjffe::g_bss8804;
using jjffe::g_patchGates;
using jjffe::PatchGates;

intptr_t* FindLabel(const char* name)
{
    for (auto get : {&jjffe::LogicLabelStorageTable, &jjffe::AsmLabelStorageTable,
                     &jjffe::OracleLabelStorageTable}) {
        std::size_t n = 0;
        const jjffe::AsmLabelStorage* t = get(&n);
        for (std::size_t i = 0; i < n; ++i)
            if (t[i].addr && t[i].label && std::strcmp(t[i].label, name) == 0 &&
                (!t[i].object || std::strcmp(t[i].object, "@blob") != 0))
                return reinterpret_cast<intptr_t*>(t[i].addr);
    }
    return nullptr;
}

intptr_t* Slot(const char* name)
{
    intptr_t* p = FindLabel(name);
    if (!p) std::printf("missing label %s\n", name);
    assert(p);
    return p;
}

// Saves a slot and restores it on scope exit.
struct SlotGuard {
    intptr_t* slot;
    intptr_t old;
    SlotGuard(const char* name, intptr_t value) : slot(Slot(name)), old(*slot) { *slot = value; }
    ~SlotGuard() { *slot = old; }
};

template <typename T> T Get(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
template <typename T> void Put(uint8_t* p, T v) { std::memcpy(p, &v, sizeof v); }

// g_bss8804 offsets (ffedat labels, logic_label_table.inc).
constexpr std::size_t kSpan = 0x556c;
constexpr std::size_t kD8862 = 0xcc;     // i32 fake ship pointer
constexpr std::size_t kD9004 = 0x3cba;   // 0x18-byte entries, zero-terminated (529 bytes)
constexpr std::size_t kD9050 = 0x48d6;   // i32 top-row scroll index
constexpr std::size_t kD9051 = 0x48da;   // i32 list entry count
constexpr std::size_t kD9054 = 0x48e8;   // u32 console mode
constexpr std::size_t kD9004Size = 529;

uint8_t g_bssStart[kSpan];
uint8_t* g_map = nullptr;
int32_t* g_ctx = nullptr;
uint8_t* g_8692 = nullptr;
int32_t* g_aj = nullptr;

// Stub expander state.
int g_calls = 0;
uint32_t g_fmts[16] = {};

int32_t StubExpand(intptr_t buf, uint32_t fmt, intptr_t /*args*/)
{
    *reinterpret_cast<char*>(buf) = 0;
    g_fmts[g_calls % 16] = fmt;
    ++g_calls;
    return 0;
}

int32_t Get9050() { return Get<int32_t>(g_bss8804 + kD9050); }
int32_t Get9051() { return Get<int32_t>(g_bss8804 + kD9051); }
void Put9054(uint32_t v) { Put<uint32_t>(g_bss8804 + kD9054, v); }
void Set8692(uint32_t v) { std::memcpy(g_8692, &v, 4); }

void SetEntries(int n)
{
    assert(n >= 0 && n <= 21);
    uint8_t* base = g_bss8804 + kD9004;
    std::memset(base, 0, kD9004Size);
    for (int i = 0; i < n; ++i)
        for (int f = 0; f < 6; ++f)
            Put<int32_t>(base + i * 0x18 + f * 4, f == 0 ? 0x9827 + i : 0x100 * (i + 1) + f);
    Put<int32_t>(base + n * 0x18, 0);
}

// AJ_899f6d label, looked up lazily (group 4 onwards only).
int32_t* AjSlot()
{
    if (!g_aj) {
        g_aj = reinterpret_cast<int32_t*>(FindLabel("AJ_899f6d"));
        if (!g_aj) std::printf("missing label AJ_899f6d\n");
        assert(g_aj);
    }
    return g_aj;
}
int32_t GetAj() { int32_t v; std::memcpy(&v, AjSlot(), 4); return v; }
void PutAj(int32_t v) { std::memcpy(AjSlot(), &v, 4); }

void ZeroCtx() { std::memset(g_ctx, 0, 12 * sizeof(int32_t)); }

// Runs fn and returns the first expander format recorded during it (the title).
uint32_t Title(void (*fn)())
{
    int start = g_calls;
    fn();
    assert(g_calls > start);
    return g_fmts[start % 16];
}

void Call490() { ZeroCtx(); jjffe::FUNC_000490(reinterpret_cast<intptr_t>(g_ctx)); }
void Call491() { ZeroCtx(); jjffe::FUNC_000491(reinterpret_cast<intptr_t>(g_ctx)); }
void Call515() { ZeroCtx(); jjffe::FUNC_000515(g_ctx); }
void Call326() { Put9054(7); jjffe::FUNC_000326(); }

void Show(const char* what, int32_t want9050, int32_t want9051, uint32_t title, uint32_t wantTitle)
{
    std::printf("  %s: 9050 got %d want %d, 9051 got %d want %d, title got 0x%x want 0x%x\n",
                what, Get9050(), want9050, Get9051(), want9051, title, wantTitle);
}

void ShowAj(const char* what, int32_t want9050, int32_t wantAj, uint32_t title, uint32_t wantTitle)
{
    std::printf("  %s: 9050 got %d want %d, static got %d want %d, title got 0x%x want 0x%x\n",
                what, Get9050(), want9050, GetAj(), wantAj, title, wantTitle);
}

// Scroll the current list so that DATA_009050 == k (FUNC_000355 success path,
// which also stores the index into the static; d == 0 still stores).
void ScrollTo(int32_t k)
{
    Set8692(1);
    int d = k - Get9050();
    uint32_t r = jjffe::FUNC_000355(d);
    std::printf("  ScrollTo(%d): FUNC_000355(%d) got %u want 0, 9050 got %d\n", k, d, r, Get9050());
    assert(r == 0);
    assert(Get9050() == k);
}

void SetFlag(bool on) { g_patchGates.RememberMilitaryBoardIdx = on; }

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::memcpy(g_bssStart, g_bss8804, kSpan);
    g_8692 = reinterpret_cast<uint8_t*>(FindLabel("DATA_008692"));
    if (!g_8692) std::printf("missing label DATA_008692\n");
    assert(g_8692);
    uint8_t old8692[4];
    std::memcpy(old8692, g_8692, 4);

    void* m = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(m != MAP_FAILED);
    g_map = static_cast<uint8_t*>(m);
    assert(reinterpret_cast<uintptr_t>(g_map) < 0x80000000u);
    std::memset(g_map, 0, 0x1000);
    g_ctx = reinterpret_cast<int32_t*>(g_map);
    uint8_t* ship = g_map + 0x100;
    Put<uint32_t>(ship + 0xa0, 0x12345678u);
    Put<int32_t>(g_bss8804 + kD8862, static_cast<int32_t>(reinterpret_cast<intptr_t>(ship)));

    {
        SlotGuard s7639("DATA_007639", reinterpret_cast<intptr_t>(&StubExpand));
        uint32_t t;

        // 1. Flag off: FUNC_000515 / FUNC_000326 reopen at the top (FUNC_000342 reset).
        g_patchGates = PatchGates{};
        SetEntries(5);
        t = Title(Call490); Show("1 FUNC_000490", 0, 6, t, 0x990a);
        assert(Get9051() == 6 && Get9050() == 0 && t == 0x990a);
        ScrollTo(2);
        t = Title(Call515); Show("1 FUNC_000515", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        assert(Get9051() == 6 && t == 0x990a);
        ScrollTo(2);
        t = Title(Call326); Show("1 FUNC_000326", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        std::printf("ok: 1 flag off: FUNC_000515 and FUNC_000326 reopen the list at row 0\n");

        // 2. Flag on: reuse the last scroll index.
        SetFlag(true);
        SetEntries(5);
        t = Title(Call490); Show("2 FUNC_000490", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        ScrollTo(2);
        t = Title(Call515); Show("2 FUNC_000515", 2, 6, t, 0x990a);
        assert(Get9050() == 2);
        assert(Get9051() == 6 && t == 0x990a);
        ScrollTo(2);
        t = Title(Call326); Show("2 FUNC_000326", 2, 6, t, 0x990a);
        assert(Get9050() == 2);
        std::printf("ok: 2 flag on: FUNC_000515 and FUNC_000326 reopen the list at the scrolled row 2\n");

        // 3. Flag on: entry points reset the index.
        ScrollTo(2);
        t = Title(Call490); Show("3 FUNC_000490", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        t = Title(Call515); Show("3 FUNC_000515 after 490", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        ScrollTo(2);
        t = Title(Call491); Show("3 FUNC_000491", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        t = Title(Call515); Show("3 FUNC_000515 after 491", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        ScrollTo(2);
        jjffe::FUNC_000320(0);
        SetEntries(5);
        t = Title(Call515); Show("3 FUNC_000515 after FUNC_000320(0)", 0, 6, t, 0x990a);
        assert(Get9050() == 0);
        std::printf("ok: 3 flag on: FUNC_000490, FUNC_000491 and FUNC_000320(0) reset the index to 0\n");

        // 4. Flag on: the clamp, as AJ's bytes (label AJ_899f6d from here on).
        SetFlag(true);
        SetEntries(3);
        PutAj(2);
        t = Title(Call515); ShowAj("4 static 2", 2, 2, t, 0x990a);
        assert(Get9050() == 2 && GetAj() == 2 && t == 0x990a);
        PutAj(3);
        t = Title(Call515); ShowAj("4 static 3 (== count)", 3, 3, t, 0x990a);
        assert(Get9050() == 3 && GetAj() == 3);
        PutAj(7);
        t = Title(Call515); ShowAj("4 static 7", 3, 3, t, 0x990a);
        assert(Get9050() == 3 && GetAj() == 3);
        PutAj(-4);
        t = Title(Call515); ShowAj("4 static -4", 1, 1, t, 0x990a);
        assert(Get9050() == 1 && GetAj() == 1);
        SetEntries(0);
        PutAj(5);
        t = Title(Call515); ShowAj("4 empty, static 5", 0, 0, t, 0x990b);
        assert(Get9050() == 0 && GetAj() == 0 && t == 0x990b);
        std::printf("ok: 4 flag on clamp: 2 -> 2, 3 (== count) -> 3, 7 -> 3, -4 -> 1, empty -> 0 / title 0x990b\n");

        // 5. Flag off: the static is not read; the empty-list zeroing is ungated.
        g_patchGates = PatchGates{};
        SetEntries(3);
        PutAj(7);
        t = Title(Call515); ShowAj("5 static 7", 0, 7, t, 0x990a);
        assert(Get9050() == 0 && GetAj() == 7);
        SetEntries(0);
        PutAj(7);
        t = Title(Call515); ShowAj("5 empty, static 7", 0, 0, t, 0x990b);
        assert(GetAj() == 0 && t == 0x990b);
        std::printf("ok: 5 flag off: index stays 0, static untouched unless the list is empty\n");

        // 6. Flag off: FUNC_000355 stores only on success (ungated write).
        SetEntries(5);
        Call490();
        std::printf("  6 after FUNC_000490: static got %d want 0\n", GetAj());
        assert(GetAj() == 0);
        PutAj(2);
        Set8692(1);
        uint32_t r = jjffe::FUNC_000355(9);
        std::printf("  6 FUNC_000355(9): got %u want 1, static got %d want 2\n", r, GetAj());
        assert(r == 1 && GetAj() == 2);
        r = jjffe::FUNC_000355(1);
        std::printf("  6 FUNC_000355(1): got %u want 0, 9050 got %d, static got %d want 1\n",
                    r, Get9050(), GetAj());
        assert(r == 0 && Get9050() == 1 && GetAj() == 1);
        std::printf("ok: 6 flag off: failed scroll leaves the static, successful scroll stores it\n");

        // 7. The gate is read at call time.
        SetEntries(5);
        PutAj(2);
        SetFlag(true);
        t = Title(Call515); ShowAj("7 on", 2, 2, t, 0x990a);
        assert(Get9050() == 2);
        g_patchGates = PatchGates{};
        t = Title(Call515); ShowAj("7 off", 0, 2, t, 0x990a);
        assert(Get9050() == 0);
        SetFlag(true);
        PutAj(2);
        t = Title(Call515); ShowAj("7 on again", 2, 2, t, 0x990a);
        assert(Get9050() == 2);
        std::printf("ok: 7 flag off/on gate read at call time\n");

        PutAj(0);
    }

    g_patchGates = PatchGates{};
    std::memcpy(g_8692, old8692, 4);
    std::memcpy(g_bss8804, g_bssStart, kSpan);
    munmap(m, 0x1000);
    std::printf("RememberMilitaryBoardIdx: PASS\n");
    return 0;
}
