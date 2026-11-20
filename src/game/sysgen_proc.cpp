// Task 4.18 — procedural system generation + cold-boot spawn table (LP64-safe).
// See include/jjffe/sysgen_proc.h for the ASM chain. Every table is read from
// the data blob at its DATA_* offset; nothing is taken from the ffeasm oracle.

#include "jjffe/sysgen_proc.h"

#include "jjffe/data.h"
#include "jjffe/ship_model.h"
#include "jjffe/terrain_query.h"

#include "data_blob.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Task 4.162: the shared FUNC_000892 fixture tables and the g_bss8804 span labels the
// oracle bridge reads/writes (extern since this round; no per-TU copies of the tables).
#include "jjffe/span_arena.h"

// Task 4.168: narrow declarations for the shared tables and span refs this TU needs.
namespace jjffe {
extern uint32_t DATA_007136[4];
extern uint32_t DATA_007137[4];
extern uint32_t DATA_007138[4];
extern uint8_t  DATA_008794[0x44];
extern uint8_t  DATA_008795[0x44];
extern uint8_t  DATA_008797[0x44];
static uint32_t& DATA_008843 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x98);
static uint32_t& DATA_008846 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0xa4);
static uint8_t&  DATA_008855 = *reinterpret_cast<uint8_t*>(g_bss8804 + 0xba);
static u32_unaligned& DATA_008856 = *reinterpret_cast<u32_unaligned*>(g_bss8804 + 0xbb);
}

namespace jjffe {

void FUNC_001533(intptr_t dst, intptr_t src, int32_t param_3);
intptr_t FUNC_000927(intptr_t obj_array, intptr_t p2, uint8_t flags, uint32_t p4, int32_t* out_slot);
void FUNC_001507(intptr_t param_1, uint32_t param_2, uint32_t param_3, uint32_t param_4);
uint32_t* FUNC_001510(uint32_t* param_1, uint32_t param_2, uint32_t param_3,
                      uint32_t param_4, int32_t param_5, uint32_t param_6);
void FUNC_001674_MatBuildOdd(int* param_1, int param_2, int param_3);
void FUNC_001675_MatBuildYZT(int32_t* param_1, int32_t param_2, int32_t param_3);
char* FUNC_000830(uint32_t param_1, char* param_2, char* param_3, uint32_t param_4);
intptr_t FUNC_001347(intptr_t param_1, int32_t param_2, intptr_t param_3, intptr_t* param_4);

namespace {

using namespace data;

// DATA_008811 (DATA_008804+0xe, world-transform cache generation) while the
// start-game path spawns the system: ffe.asm 68567 sets it to 1 and the first
// per-frame increment (68686-68690) runs after FUNC_000147 (68669).
constexpr int kStartGameWorldGen = 1;

// ---------------------------------------------------------------- blob access

const uint8_t* Blob() { return GetDataBlob(); }

bool BlobHas(uint32_t off, uint32_t len)
{
    return Blob() && static_cast<size_t>(off) + len <= GetDataBlobSize();
}

uint32_t RdU32(uint32_t off)
{
    uint32_t v = 0;
    if (BlobHas(off, 4)) std::memcpy(&v, Blob() + off, 4);
    return v;
}

uint16_t RdU16(uint32_t off)
{
    uint16_t v = 0;
    if (BlobHas(off, 2)) std::memcpy(&v, Blob() + off, 2);
    return v;
}

uint8_t RdU8(uint32_t off) { return BlobHas(off, 1) ? Blob()[off] : 0; }

inline uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// --------------------------------------------------- FFP helpers (exact ASM)
// FFP value: high word = signed exponent, low word = signed mantissa.

inline int16_t Lo(uint32_t v) { return static_cast<int16_t>(v & 0xffff); }
inline int16_t Hi(uint32_t v) { return static_cast<int16_t>(v >> 16); }

inline uint32_t Pack(int32_t hi, int32_t lo)
{
    return (static_cast<uint32_t>(static_cast<uint16_t>(hi)) << 16) |
           static_cast<uint16_t>(lo);
}

inline uint32_t AddHi(uint32_t v, int32_t d) { return Pack(Hi(v) + d, Lo(v)); }

// FUNC_001482
uint32_t FpMul(uint32_t a, uint32_t b)
{
    const int32_t p = static_cast<int32_t>(Lo(a)) * static_cast<int32_t>(Lo(b));
    return Pack(Hi(a) + Hi(b), p >> 15);
}

// FUNC_001483
uint32_t FpDiv(uint32_t a, uint32_t b)
{
    if (Lo(b) == 0) return 0x007f7fffu;
    int32_t hi = Hi(a) - Hi(b);
    int16_t lo = static_cast<int16_t>((static_cast<int32_t>(Lo(a)) << 14) / Lo(b));
    const int32_t mag = lo < 0 ? -static_cast<int32_t>(lo) : lo;
    if (mag >= 0x4000)
        ++hi;
    else
        lo = static_cast<int16_t>(lo << 1);
    return Pack(hi, lo);
}

// FUNC_001486
uint32_t FpAdd(uint32_t a, uint32_t b)
{
    const int32_t ea = Hi(a), eb = Hi(b);
    int32_t e, sum;
    if (ea > eb) {
        if (ea > eb + 0x1f) return a;
        e = ea;
        sum = (static_cast<int32_t>(Lo(b)) >> (ea - eb)) + Lo(a);
    } else {
        if (eb > ea + 0x1f) return b;
        e = eb;
        sum = (static_cast<int32_t>(Lo(a)) >> (eb - ea)) + Lo(b);
    }
    const int32_t mag = sum < 0 ? -sum : sum;
    if (mag > 0x7fff) return Pack(e + 1, sum >> 1);
    int16_t lo = static_cast<int16_t>(sum);
    if (lo == 0) return Pack(e, 0);
    for (;;) {
        const int32_t m = lo < 0 ? -static_cast<int32_t>(lo) : lo;
        if (m >= 0x3fff) break;
        lo = static_cast<int16_t>(lo << 1);
        --e;
    }
    return Pack(e, lo);
}

// FUNC_001489
uint32_t IntToFp(int32_t v)
{
    if (v == 0) return 0xfff90000u;
    int32_t e = 0x20;
    uint32_t u = static_cast<uint32_t>(v);
    while (static_cast<int32_t>(u) > 0) {
        u += u;
        --e;
    }
    return Pack(e, static_cast<int32_t>(u >> 17));
}

// FUNC_001491
int32_t FpToInt(uint32_t v)
{
    if (Hi(v) > 0x1f) return Lo(v) < 0 ? static_cast<int32_t>(0x80000000u) : 0;
    const int sh = (0x1f - Hi(v)) & 0x1f;
    return (static_cast<int32_t>(Lo(v)) * 0x10000) >> sh;
}

// FUNC_001494 (signed exponent compares; SHR of the sign-extended mantissa)
uint32_t FpToUnsigned(uint32_t v)
{
    if (Hi(v) <= 0xf) {
        const int sh = (0xf - Hi(v)) & 0x1f;
        return static_cast<uint32_t>(static_cast<int32_t>(Lo(v))) >> sh;
    }
    if (Hi(v) <= 0x20) {
        const int sh = (Hi(v) - 0xf) & 0x1f;
        return static_cast<uint32_t>(static_cast<int32_t>(Lo(v))) << sh;
    }
    return 0xffffffffu;
}

// FUNC_001496 (normalise until mantissa bit 14 is set)
uint32_t FpNormalise(uint32_t v)
{
    if (Lo(v) == 0) return v;
    int32_t e = Hi(v);
    int16_t lo = Lo(v);
    for (int guard = 0; guard < 32 && !(lo & 0x4000); ++guard) {
        lo = static_cast<int16_t>(lo << 1);
        --e;
    }
    return Pack(e, lo);
}

// FUNC_001467 — integer sqrt via DATA_007977 word table.
uint32_t Isqrt1467(uint32_t x)
{
    auto t = [](uint32_t i) -> uint32_t {
        return RdU16(OFFSET_DATA_007977 + i * 2);
    };
    if (x < 0x4000000u) {
        if (x >= 0x400000u) return t(x >> 15) >> 3;
        if (x >= 0x40000u) return t(x >> 11) >> 5;
        if (x >= 0x4000u) return t(x >> 7) >> 7;
        if (x >= 0x400u) return t(x >> 3) >> 9;
        return t(x * 2) >> 11;
    }
    if (x >= 0x40000000u) return t(x >> 21);
    if (x >= 0x10000000u) return t(x >> 19) >> 1;
    return t(x >> 17) >> 2;
}

// FUNC_001463
uint32_t FpSqrt(uint32_t v)
{
    uint32_t m = static_cast<uint32_t>(static_cast<int32_t>(Lo(v))) << 16;
    int16_t e = Hi(v);
    if (e & 1) {
        e = static_cast<int16_t>((e >> 1) + 1);
    } else {
        e = static_cast<int16_t>(e >> 1);
        m += m;
    }
    return Pack(e, static_cast<int32_t>(Isqrt1467(m) >> 1));
}

// FUNC_001515
uint32_t Mul1515(uint32_t a, uint32_t b)
{
    const uint32_t lo = ((a & 0xffff) * b) >> 16;
    return lo + (a >> 16) * b;
}

int64_t Shift64(int64_t v, int s)
{
    if (s >= 0) return s >= 64 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(v) << s);
    return -s >= 64 ? (v < 0 ? -1 : 0) : (v >> -s);
}

// --------------------------------------------------------------- string table
// DATA_006272: 510 pointers to the consecutive NUL-terminated strings
// DATA_006280 + i (ffedat.asm). The blob zeroes the pointer table, so index i
// is resolved by walking i terminators from DATA_006280.

constexpr int kStringTableCount = 510;

const char* SysString(uint32_t idx)
{
    static uint32_t s_off[kStringTableCount];
    static bool s_built = false;
    if (!s_built) {
        if (!Blob()) return "";
        uint32_t off = OFFSET_DATA_006280;
        for (int i = 0; i < kStringTableCount; ++i) {
            s_off[i] = off;
            while (BlobHas(off, 1) && Blob()[off] != 0) ++off;
            ++off;
        }
        s_built = true;
    }
    if (idx >= static_cast<uint32_t>(kStringTableCount)) return "";
    return reinterpret_cast<const char*>(Blob() + s_off[idx]);
}

// FUNC_000791's vars block (FUNC_000878's, passed down). FUNC_000800 hands it to
// FUNC_001347, which makes it the FF30 context (DATA_009149); the table strings'
// 0x3016 code (FUNC_001364) expands the string at +0xc, which FUNC_000791 points at
// its FUNC_000793/794 buffer (ffe.asm 51741-51743, 51800-51802). The pointer is
// stored pointer-wide, as FF30StringParamPtr reads it. The callers keep the block
// in static storage: DATA_009149 still points at it after the generator returns,
// as the ASM's still points at FUNC_000878's frame.
static char g_v4buf[256] = {};

struct NameVars {
    uint32_t slot[6] = {};

    void PointV4AtBuf()
    {
        const intptr_t p = reinterpret_cast<intptr_t>(g_v4buf);
        std::memcpy(&slot[3], &p, sizeof(p));
    }
};

// FUNC_000800 (ffe.asm 52316-52332): FUNC_001347(dst, idx & 0x1ff, vars, DATA_006272),
// so the 0x3016 code goes through FUNC_001348 / FUNC_001343 as in the ASM.
char* ExpandTableString(uint32_t idx, NameVars* vars, char* dst)
{
    static intptr_t s_table[0x200];
    static bool s_built = false;
    if (!s_built) {
        for (int i = 0; i < kStringTableCount; ++i) s_table[i] = reinterpret_cast<intptr_t>(SysString(i));
        s_built = true;
    }
    return reinterpret_cast<char*>(FUNC_001347(reinterpret_cast<intptr_t>(dst), static_cast<int32_t>(idx & 0x1ff),
                                               reinterpret_cast<intptr_t>(vars->slot), s_table));
}

// The same expansion for SysGenGenerateProcedural, world.cpp's port-only spawn
// list, which no ASM path runs: FUNC_001348 with 0x3016 → the +0xc string, without
// FUNC_001343 or DATA_009149. Returns one past the NUL.
char* ExpandScaffold(char* dst, char* end, const char* src, const NameVars& vars, int depth = 0)
{
    auto put = [&](char c) {
        if (dst < end) *dst = c;
        ++dst;
    };
    for (;;) {
        const uint8_t c = static_cast<uint8_t>(*src++);
        if (c == 0) break;
        if (c < 0xfd) {
            put(static_cast<char>(c));
        } else if (c == 0xfd) {
            put(0x1e);
            put(*src++);
        } else if (c == 0xfe) {
            put(0x1f);
            put(*src++);
            put(*src++);
        } else {
            const uint32_t code = (static_cast<uint32_t>(static_cast<uint8_t>(src[0])) << 8) |
                                  static_cast<uint8_t>(src[1]);
            src += 2;
            if (code == 0x3016 && depth < 4) {
                dst = ExpandScaffold(dst, end, g_v4buf, vars, depth + 1) - 1;
            } else if (code != 0x3016) {
                std::fprintf(stderr, "sysgen_proc: FF code %#x not ported\n", code);
            }
        }
    }
    put(0);
    return dst;
}

// ------------------------------------------------------------------ generator

// DATA_007125 (ffedat.asm): per-mass-bracket callback, pointers zeroed in blob.
enum class BodyFn : uint8_t { kNone, k883, k884, k885, k886, k887, k888, k889, k890 };
constexpr BodyFn kBracketFn[23] = {
    BodyFn::kNone, BodyFn::k887, BodyFn::k886, BodyFn::k885, BodyFn::k884, BodyFn::k883,
    BodyFn::kNone, BodyFn::kNone, BodyFn::kNone, BodyFn::kNone, BodyFn::kNone, BodyFn::kNone,
    BodyFn::kNone, BodyFn::kNone, BodyFn::kNone, BodyFn::k888, BodyFn::k889, BodyFn::k890,
    BodyFn::k890, BodyFn::kNone, BodyFn::kNone, BodyFn::kNone, BodyFn::kNone,
};

constexpr uint32_t kStarMass = 0x1036640;
constexpr int kGenScratch = 96;   // DATA_008798 holds 60; F891/F892 may run past F881's cap

struct Gen {
    SysObj rec[kGenScratch] = {};
    int cnt = 0;

    uint32_t d8843 = 0, d8844 = 0;
    int32_t d8846 = 0;
    uint8_t d8847 = 0, d8848 = 0, d8849 = 0, d8851 = 0, d8855 = 0, d8856 = 0;
    uint32_t d8852 = 0, d8853 = 0;
    int32_t d8854 = 0;
    int32_t tier = 0;
    bool unsupported = false;
    bool scaffold = false;  // SysGenGenerateProcedural: no ASM path runs it

    const SysObj* tpl8794 = nullptr;
    const SysObj* tpl8795 = nullptr;
    const SysObj* tpl8796 = nullptr;
    const SysObj* tpl8797 = nullptr;

    SysObj& R(int i)
    {
        static SysObj s_sink;
        if (i < 0 || i >= kGenScratch) {
            unsupported = true;
            std::memset(&s_sink, 0, sizeof(s_sink));
            return s_sink;
        }
        return rec[i];
    }

    // Name bytes run from +0x26 to the record end; longer strings overflow into
    // +0x3a.. exactly like the ASM's strcpy/strcat into the 68-byte record.
    static constexpr int kNameSpan = static_cast<int>(kSysObjSize) - 0x26;
    uint8_t* NameBytes(int i) { return reinterpret_cast<uint8_t*>(&R(i)) + 0x26; }

    void GetName(int i, char* out64)
    {
        std::memset(out64, 0, 64);
        const uint8_t* p = NameBytes(i);
        for (int k = 0; k < kNameSpan && p[k]; ++k) out64[k] = static_cast<char>(p[k]);
    }

    void SetName(int i, const char* s)
    {
        uint8_t* p = NameBytes(i);
        const int n = static_cast<int>(std::strlen(s)) + 1;
        std::memcpy(p, s, n < kNameSpan ? n : kNameSpan);
    }

    // FUNC_000894
    uint32_t FUNC_000894_Rand()
    {
        const uint32_t sum = d8844 + d8843;
        const uint32_t r5 = Rol(sum, 5);
        d8843 = Rol(d8843, 3) + sum + r5;
        d8844 = r5;
        return d8843;
    }

    // FUNC_000921
    static uint32_t Pick(uint32_t n, uint32_t* seed, uint32_t* local)
    {
        *seed += *local;
        *local = Rol(*local, 5);
        *seed += *local;
        *seed = Rol(*seed, 16);
        return ((*seed & 0xffff) * n) >> 16;
    }

    // FUNC_000800
    char* StrIdx(uint32_t idx, NameVars* vars, char* dst)
    {
        if (scaffold) return ExpandScaffold(dst, dst + 0xff, SysString(idx & 0x1ff), *vars);
        return ExpandTableString(idx, vars, dst);
    }

    // FUNC_000793
    char* Name793(char* dst, NameVars* vars, uint32_t* seed, uint32_t* local)
    {
        if ((*seed & 0xff) >= 0x19) {
            const uint32_t idx = Pick(0x8a, seed, local) + 0x59;
            return StrIdx(idx, vars, dst);
        }
        const uint32_t idx = Pick(10, seed, local) + 0x4f;
        char* e = StrIdx(idx, vars, dst);
        e[-1] = 's';
        if (!((*seed >> 8) & 8)) {
            *e++ = 'o';
            *e++ = 'n';
        }
        *e++ = 0;
        return e;
    }

    // FUNC_000794
    char* Name794(char* dst, NameVars* vars, uint32_t* seed, uint32_t* local)
    {
        const uint32_t idx = Pick(0x58, seed, local) + 0x18a;
        return StrIdx(idx, vars, dst);
    }

    // FUNC_000791 (types 6..14, 0x10, 0x11 — the ones FUNC_000893 can pass)
    void Name791(uint32_t type, NameVars* vars, uint32_t seed, int recIdx)
    {
        uint32_t local = 0;
        if (type < 0x16) {
            if (type < 6) {
                local = Rol(seed, 16) + seed;
                seed = Rol(seed, 7);
            } else {
                local = Rol(seed, 16);
                seed += local;
                local = Rol(local, 7);
                seed += local;
                local = Rol(local, 4);
                seed += local;
            }
        }
        char out[256] = {};
        if (type >= 6 && type <= 14) {
            Name793(g_v4buf, vars, &seed, &local);
            vars->PointV4AtBuf();
            const uint32_t idx = Pick(RdU32(OFFSET_DATA_006275 + type * 4), &seed, &local) +
                                 RdU32(OFFSET_DATA_006274 + type * 4);
            StrIdx(idx, vars, out);
        } else if (type == 0x10 || type == 0x11) {
            Name794(g_v4buf, vars, &seed, &local);
            vars->PointV4AtBuf();
            const uint32_t cnt = RdU32(type == 0x10 ? OFFSET_DATA_006278 : OFFSET_DATA_006279);
            const uint32_t base = RdU32(type == 0x10 ? OFFSET_DATA_006276 : OFFSET_DATA_006277);
            StrIdx(Pick(cnt, &seed, &local) + base, vars, out);
        } else {
            std::fprintf(stderr, "sysgen_proc: FUNC_000791 type %#x not ported\n", type);
            unsupported = true;
            return;
        }
        SetName(recIdx, out);
    }

    // FUNC_000893
    void NameBody(int i, uint32_t type, NameVars* vars)
    {
        R(i).treeLevel = static_cast<uint8_t>(R(i).treeLevel | 0x80);
        uint32_t t = type;
        if (tier == 3) {
            if (type == 8 || type == 9) t = 0x10;
            else if (type == 6 || type == 7) t = 0x11;
        }
        Name791(t, vars, R(i).randseed, i);
    }

    // FUNC_000896
    static uint16_t FUNC_000896_Density(uint32_t p)
    {
        const uint16_t w = static_cast<uint16_t>(p);
        if (w > 0x4000) return 0xffff;
        return RdU16(OFFSET_DATA_007129 + (w >> 6) * 4);
    }

    uint32_t OrbVelAbout(uint32_t orb, const SysObj& par)
    {
        uint32_t v = FpSqrt(FpDiv(FpMul(FpMul(orb, orb), orb), static_cast<uint32_t>(par.mass)));
        const int32_t sc = (static_cast<int32_t>(Lo(v)) * 0x5edb) >> 15;
        return (v & 0xffff0000u) | static_cast<uint16_t>(sc);
    }

    // FUNC_000882
    void FUNC_000882_Orbit(int i, int parent, uint32_t p4, uint32_t p5w, uint32_t mass)
    {
        SysObj& r = R(i);
        uint32_t orb = Mul1515(p4, p5w & 0xffff);
        r.orbRadius = static_cast<int32_t>(orb);
        if (orb != 0) {
            orb = AddHi(IntToFp(static_cast<int32_t>(orb)), 0x19);
            r.orbRadius = static_cast<int32_t>(orb);
            if (parent != 0) {
                uint32_t rr = orb;
                const SysObj& par = R(parent - 1);
                if (par.type == 0 && static_cast<uint32_t>(i - 1) <= static_cast<uint32_t>(parent))
                    rr = AddHi(rr, 1);
                r.orbVel = static_cast<int32_t>(OrbVelAbout(rr, par));
            } else {
                r.orbVel = 0;
            }
            r.latitude = static_cast<int16_t>(d8843 & 0xffff);
        }
        r.randseed = d8844;
        ++d8846;
        r.parentIndex = static_cast<uint16_t>(parent);
        r.mass = static_cast<int32_t>(AddHi(IntToFp(static_cast<int32_t>(mass)), 0x3f));
        r.unk20 = 0;

        const uint16_t w = static_cast<uint16_t>(d8843 & 0xffff);
        const uint16_t r3 = static_cast<uint16_t>((w >> 3) | (w << 13));
        const int32_t sq = static_cast<int32_t>(static_cast<uint32_t>(r3) * r3);
        r.longitude = static_cast<int16_t>(sq >> 20);
        if (parent != 0)
            r.longitude = static_cast<int16_t>(r.longitude + R(parent - 1).orientation);

        const int64_t ss = static_cast<int64_t>(static_cast<int32_t>(r.randseed)) *
                           static_cast<int32_t>(r.randseed);
        r.orientation = static_cast<int16_t>(static_cast<int32_t>(static_cast<int16_t>(ss)) >> 17);
        r.rotSpeed = static_cast<uint8_t>(r.orientation & 3);
        r.treeLevel = d8849;

        if (r.temperature < 1000) {
            uint32_t acc = 0;
            int e = i;
            for (int guard = 0; R(e).parentIndex != 0 && guard < kGenScratch; ++guard) {
                const int p = R(e).parentIndex - 1;
                if (R(p).lightOutput != 0) {
                    uint32_t s = AddHi(IntToFp(static_cast<int32_t>(R(p).lightOutput)), 0x32);
                    s = FpDiv(s, static_cast<uint32_t>(R(e).orbRadius));
                    acc = FpAdd(acc, FpMul(s, s));
                }
                e = p;
            }
            r.temperature = static_cast<uint16_t>(r.temperature + FpToInt(FpSqrt(FpSqrt(acc))));
        }
    }

    static int32_t StationKind(int32_t x, int32_t hi2, int32_t hi1)
    {
        if (x >= hi2) return 2;
        return x >= hi1 ? 1 : 0;
    }

    // FUNC_000891
    void FUNC_000891_Starports(int* cnt, uint32_t type, int32_t pop, NameVars* vars)
    {
        uint32_t esi = d8843, s1 = d8844;
        const uint32_t ext = static_cast<uint32_t>(d8854);
        const int parentIdx = *cnt + 1;
        if (static_cast<int8_t>(d8855) >= 0x12) return;
        uint32_t n = ((d8844 & 0xffff) * static_cast<uint32_t>(pop)) >> 16;
        if (n > 3) n = 3;
        for (uint32_t k = 0; k <= n; ++k) {
            s1 += esi;
            esi = Rol(esi, 5) + s1;
            esi = Rol(esi, 16) + s1;
            ++*cnt;
            SysObj& r = R(*cnt);
            r = *tpl8796;
            r.mass = 0x00390000;
            r.parentIndex = static_cast<uint16_t>(parentIdx);
            r.randseed = esi;
            ++d8846;
            const uint32_t k16 = static_cast<uint32_t>(d8846) & 0xf;
            r.longitude = static_cast<int16_t>(RdU16(OFFSET_DATA_007130 + k16 * 8));
            r.latitude = static_cast<int16_t>(RdU16(OFFSET_DATA_007131 + k16 * 8));
            NameBody(*cnt, 6, vars);
            const uint32_t mi = ((ext >> 5) & 7) + type + ((R(*cnt).randseed & 0x30000) >> 16);
            R(*cnt).modelIndex = RdU16(OFFSET_DATA_007132 + mi * 4);
            ++d8855;
            if (static_cast<int8_t>(d8855) >= 0x12) break;
        }
    }

    // FUNC_000892
    void Station(int* cnt, uint32_t type, NameVars* vars, int parent)
    {
        if (static_cast<int8_t>(d8855) >= 0x12) return;
        ++d8855;
        ++d8856;
        ++*cnt;
        SysObj& r = R(*cnt);
        const uint32_t sel = (d8843 & 0x300) >> 8;
        switch (type) {
        case 0: r = *tpl8794; r.modelIndex = RdU16(OFFSET_DATA_007136 + sel * 4); break;
        case 1: r = *tpl8797; r.modelIndex = RdU16(OFFSET_DATA_007137 + sel * 4); break;
        case 2: r = *tpl8795; r.modelIndex = RdU16(OFFSET_DATA_007138 + sel * 4); break;
        case 3: r = *tpl8794; r.modelIndex = 0x45; break;
        case 4: r = *tpl8795; r.modelIndex = 0x4f; break;
        default: break;
        }
        r.mass = 0x00390000;
        r.randseed = d8843;
        r.parentIndex = static_cast<uint16_t>(parent);
        ++d8846;
        const uint32_t orb = 0x00227fffu;
        r.orbRadius = static_cast<int32_t>(orb);
        r.orbVel = static_cast<int32_t>(OrbVelAbout(orb, R(r.parentIndex - 1)));
        if (type == 3 || type == 4) {
            SetName(*cnt, reinterpret_cast<const char*>(Blob() + OFFSET_DATA_007185));
        } else {
            NameBody(*cnt, 7, vars);
        }
    }

    uint16_t Temp(int* cnt) { return R(*cnt).temperature; }
    void SetTemp(int* cnt, uint32_t t) { R(*cnt).temperature = static_cast<uint16_t>(t); }

    // FUNC_000883
    void FUNC_000883_Rocky(int* cnt, uint32_t mass, NameVars* vars)
    {
        const int saved = *cnt;
        SysObj& r = R(*cnt);
        if (r.temperature > 0x139) {
            r.modelIndex = 0x7b;
            r.type = 0xc;
            r.temperature = static_cast<uint16_t>(r.temperature + static_cast<uint16_t>(mass >> 1));
            return;
        }
        auto terraformed = [&]() {
            if (Temp(cnt) < 0x127) SetTemp(cnt, 0x127);
            R(*cnt).modelIndex = 0x7e;
            R(*cnt).type = 0xb;
            NameBody(*cnt, 8, vars);
            FUNC_000891_Starports(cnt, 0, d8854 >> 5, vars);
            if (d8854 >= 0x1e)
                Station(cnt, static_cast<uint32_t>(StationKind(d8854, 0xf2, 0x32)), vars, saved + 1);
        };
        if (r.temperature <= 0xd7) {
            if (r.temperature >= 0xaa) {
                r.temperature = static_cast<uint16_t>(r.temperature + 0x19);
                if (d8854 < 0xc8) {
                    r.modelIndex = 0x7b;
                    r.type = 6;
                    if (d8853 == 0x1aa89738u || d8853 == 0x2ae1718u || d8853 == 0xeb6973du) {
                        r.modelIndex = 0x85;
                        if (d8853 == 0x2ae1718u && R(*cnt).randseed == 0x5b5ff290u)
                            Station(cnt, 3, vars, saved + 1);
                        if (d8853 == 0xeb6973du && R(*cnt).randseed == 0x70740190u)
                            Station(cnt, 4, vars, saved + 1);
                    }
                    return;
                }
                terraformed();
                return;
            }
            if (r.temperature <= 0x7b && r.temperature >= 0x42) {
                r.temperature = static_cast<uint16_t>(r.temperature + 0x16);
                r.modelIndex = 0x7c;
                r.type = 7;
                return;
            }
            FUNC_000884_Rocky(cnt, mass, vars);
            return;
        }
        r.temperature = static_cast<uint16_t>(r.temperature + 0x23);
        FUNC_000894_Rand();
        const uint32_t edx = (d8843 & 0xff) >> 2;
        const uint32_t ecx = static_cast<uint32_t>(R(*cnt).temperature) + 0xffffff06u;
        if (edx > ecx) {
            if (d8854 < 0x78) {
                R(*cnt).modelIndex = 0x80;
                R(*cnt).type = 8;
                return;
            }
            terraformed();
            return;
        }
        if (d8854 < 4) return;
        NameBody(*cnt, 9, vars);
        if (d8854 < 0x10) return;
        if (!(d8843 & 0xf0)) R(*cnt).modelIndex = 0x84;
        if (Temp(cnt) > 0x118) {
            if (Temp(cnt) > 0x134) {
                R(*cnt).modelIndex = 0x83;
                if (Temp(cnt) > 0x152) R(*cnt).modelIndex = 0x82;
            }
            if (!(d8843 & 0xf)) {
                R(*cnt).modelIndex = 0x80;
                FUNC_000891_Starports(cnt, 0x24, d8854 >> 5, vars);
            } else {
                FUNC_000891_Starports(cnt, 0, d8854 >> 5, vars);
            }
            if (d8854 >= 0x1e)
                Station(cnt, static_cast<uint32_t>(StationKind(d8854, 0xf2, 0x32)), vars, saved + 1);
        } else {
            R(*cnt).modelIndex = 0x7f;
            FUNC_000891_Starports(cnt, 0x18, d8854 >> 6, vars);
            if (d8854 >= 0x28)
                Station(cnt, d8854 >= 0x50 ? 1u : 0u, vars, saved + 1);
        }
    }

    // FUNC_000884
    void FUNC_000884_Rocky(int* cnt, uint32_t, NameVars* vars)
    {
        const int saved = *cnt;
        SetTemp(cnt, Temp(cnt) + 0x14u);
        if (d8854 >= 0x96 && Temp(cnt) >= 0x102 && Temp(cnt) <= 0x139) {
            if (Temp(cnt) < 0x127) SetTemp(cnt, 0x127);
            R(*cnt).modelIndex = 0x7e;
            R(*cnt).type = 9;
            NameBody(*cnt, 8, vars);
            FUNC_000891_Starports(cnt, 0, d8854 >> 6, vars);
            if (d8854 >= 0x1e)
                Station(cnt, static_cast<uint32_t>(StationKind(d8854, 0xb4, 0x32)), vars, saved + 1);
            return;
        }
        R(*cnt).modelIndex = 0x79;
        R(*cnt).type = 4;
        if (Temp(cnt) > 0x212) return;
        bool go = false;
        if (d8854 >= 0xc8 && Temp(cnt) >= 0xa0) {
            go = true;
        } else if (Temp(cnt) >= 0xb4 && d8854 > 6) {
            go = true;
        } else if (d8854 >= 0xfa && Temp(cnt) > 0x87) {
            go = true;
        }
        if (!go) return;
        SysObj& r = R(*cnt);
        if (Hi(static_cast<uint32_t>(r.mass)) > 0x47)
            r.mass = static_cast<int32_t>(Pack(0x47, Lo(static_cast<uint32_t>(r.mass))));
        NameBody(*cnt, 10, vars);
        if (d8854 < 0x1e) return;
        FUNC_000891_Starports(cnt, 0xc, 0, vars);
        if (d8854 < 0x64) return;
        Station(cnt, d8854 >= 0xaa ? 1u : 0u, vars, saved + 1);
    }

    // FUNC_000885
    void FUNC_000885_Body(int* cnt, uint32_t, NameVars* vars)
    {
        const int saved = *cnt;
        const uint16_t t = Temp(cnt);
        if (t < 0x96 || t > 0x1f4) return;
        if (d8854 < 0xc8) {
            if (Temp(cnt) < 0xc8) return;
            if (d8854 <= 8) return;
        }
        NameBody(*cnt, 10, vars);
        if (d8854 < 0x2d) return;
        FUNC_000891_Starports(cnt, 0xc, 0, vars);
        if (d8854 < 0x96) return;
        Station(cnt, 0, vars, saved + 1);
    }

    // FUNC_000886
    void FUNC_000886_Body(int* cnt, uint32_t, NameVars* vars)
    {
        if (Temp(cnt) >= 0xdc && d8854 > 0x32) {
            NameBody(*cnt, 10, vars);
            if (d8854 >= 0x50) FUNC_000891_Starports(cnt, 0xc, 0, vars);
        }
    }

    // FUNC_000887
    void FUNC_000887_Body(int* cnt, uint32_t, NameVars* vars)
    {
        R(*cnt).mass = 0x00380000;
        if (Temp(cnt) >= 0xdc && d8854 > 0x32) NameBody(*cnt, 10, vars);
    }

    // FUNC_000888
    void FUNC_000888_Body(int* cnt)
    {
        if (d8846 > 2) {
            SysObj& r = R(*cnt);
            r.modelIndex = 0x94;
            r.type = 0x19;
            r.lightOutput = 0x3e2;
            r.temperature = 0x2c09;
            r.mass = static_cast<int32_t>(AddHi(static_cast<uint32_t>(r.mass), -1));
        }
    }

    // FUNC_000890 (ffe.asm:66383-66402)
    void FUNC_000890(int* cnt)
    {
        if (FUNC_000894_Rand() & 0x80000000u) FUNC_000888_Body(cnt);
    }

    // FUNC_000889 (ffe.asm:66360-66379)
    void FUNC_000889(int* cnt)
    {
        if (FUNC_000894_Rand() & 0x80000000u) FUNC_000890(cnt);
    }

    void CallFn(BodyFn fn, int* cnt, uint32_t mass, NameVars* vars)
    {
        switch (fn) {
        case BodyFn::k883: FUNC_000883_Rocky(cnt, mass, vars); break;
        case BodyFn::k884: FUNC_000884_Rocky(cnt, mass, vars); break;
        case BodyFn::k885: FUNC_000885_Body(cnt, mass, vars); break;
        case BodyFn::k886: FUNC_000886_Body(cnt, mass, vars); break;
        case BodyFn::k887: FUNC_000887_Body(cnt, mass, vars); break;
        case BodyFn::k888: FUNC_000888_Body(cnt); break;
        case BodyFn::k889: FUNC_000889(cnt); break;
        case BodyFn::k890: FUNC_000890(cnt); break;
        case BodyFn::kNone: break;
        }
    }

    // FUNC_000881
    uint16_t FUNC_000881_Body(int* cnt, uint32_t mass, int parent, uint32_t p5, uint32_t p6w,
                  const char* name, NameVars* vars, uint32_t* l1, uint32_t* l2, uint32_t* l3)
    {
        const int i = *cnt;
        if (static_cast<uint32_t>(i) >= 0x3c) return 0xffff;
        ++d8847;
        int k = 0;
        for (;;) {
            const uint32_t c = RdU32(OFFSET_DATA_007121 + k * 4);
            if (c == 0 || c > mass) break;
            ++k;
        }
        SysObj& r = R(i);
        r.lightOutput = RdU32(OFFSET_DATA_007124 + k * 4);
        r.type = RdU16(OFFSET_DATA_007119 + k * 4);
        r.temperature = RdU16(OFFSET_DATA_007123 + k * 4);
        r.modelIndex = RdU16(OFFSET_DATA_007120 + k * 4);
        const uint16_t ret = RdU16(OFFSET_DATA_007122 + k * 4);
        const BodyFn fn = (k < 23) ? kBracketFn[k] : BodyFn::kNone;

        const char* sp183 = reinterpret_cast<const char*>(Blob() + OFFSET_DATA_007183);
        const char* sp184 = reinterpret_cast<const char*>(Blob() + OFFSET_DATA_007184);
        char nm[64];
        if (mass >= kStarMass) {
            std::memset(nm, 0, sizeof(nm));
            std::strncpy(nm, name, 40);
            std::strncat(nm, sp183, 4);
            ++d8848;
            size_t len = std::strlen(nm);
            if (parent != 0) nm[len++] = static_cast<char>(d8848 + 0x40);
            nm[len] = 0;
            SetName(i, nm);
        } else if (parent != 0 && (R(parent - 1).treeLevel & 0x80)) {
            r.randseed = d8844;
            NameBody(i, 10, vars);
        } else {
            if (parent != 0)
                GetName(parent - 1, nm);
            else
                std::memset(nm, 0, sizeof(nm));
            std::strncat(nm, sp184, 4);
            size_t last = std::strlen(nm) - 1;
            if (d8849 == 0) {
                ++*l1;
                if (*l1 > 9) {
                    nm[last++] = '1';
                    nm[last++] = static_cast<char>(*l1 + 0x26);
                } else {
                    nm[last++] = static_cast<char>(*l1 + 0x30);
                }
                nm[last] = 0;
            } else if (d8849 == 1) {
                ++*l2;
                nm[last] = static_cast<char>(*l2 + 0x60);
            } else {
                ++*l3;
                nm[last] = static_cast<char>(*l3 + 0x30);
            }
            SetName(i, nm);
        }

        FUNC_000882_Orbit(i, parent, p5, p6w, mass);
        CallFn(fn, cnt, mass, vars);
        ++*cnt;
        return ret;
    }

    // FUNC_000877 (ffe.asm:63757-64013)
    uint16_t FUNC_000877_Binary(int* cnt, int parent, uint32_t mass, uint32_t p5, uint32_t p6w, uint32_t p7w,
                    const char* name, NameVars* vars)
    {
        // The ASM's three counters are uninitialised stack; they only feed planet
        // names, and every FUNC_000881 call here passes a star mass.
        uint32_t l4 = 0, l8 = 0, lc = 0;
        if (mass >= 0xffffffefu || (mass >= 170000000u && mass < 175000000u)) {
            FUNC_000881_Body(cnt, mass, parent, p5, p6w, name, vars, &l4, &l8, &lc);
            FUNC_000881_Body(cnt, mass >> 2, *cnt, p5, p7w, name, vars, &l4, &l8, &lc);
            return static_cast<uint16_t>(p7w << 3);
        }
        FUNC_000894_Rand();
        const uint32_t w = static_cast<uint16_t>(
            static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(d8843) | 0x8000)) >> 2);
        uint32_t second = w * (mass >> 16);
        if (second <= kStarMass) second = kStarMass;
        const int pair = *cnt;
        FUNC_000882_Orbit(pair, parent, p5, p6w, second + mass);
        R(pair).type = 0;
        R(pair).modelIndex = 0x17;
        SetName(pair, name);
        uint8_t* nm = NameBytes(pair);
        int len = 0;
        while (len < kNameSpan && nm[len]) ++len;
        int p;
        if (len > 0xb) {
            nm[0xb] = '.';
            p = 0xc;
        } else {
            nm[len] = ' ';
            p = len + 1;
        }
        const uint8_t tail[4] = {static_cast<uint8_t>(d8848 + 'A'), ',',
                                 static_cast<uint8_t>(d8848 + 'B'), 0};
        for (int k = 0; k < 4 && p + k < kNameSpan; ++k) nm[p + k] = tail[k];
        ++*cnt;
        FUNC_000881_Body(cnt, mass, pair + 1, p5, p7w, name, vars, &l4, &l8, &lc);
        R(*cnt - 1).latitude = 0;
        FUNC_000881_Body(cnt, second, pair + 1, p5, p7w, name, vars, &l4, &l8, &lc);
        R(*cnt - 1).latitude = 0x7fff;
        // JJ "FIX: for binary light bug" (ffe.asm:63997-64001).
        R(*cnt - 3).lightOutput = R(*cnt - 1).lightOutput + R(*cnt - 2).lightOutput;
        return static_cast<uint16_t>(p7w << 3);
    }

    // FUNC_000878's procedural branch (ffe.asm:64071-64255), entered with
    // d8843/d8844 = randseed1/randseed2 and d8851 = numstars. Returns false when
    // the ASM writes no terminator (the lone FUNC_000881 returned 0, 64104-64106).
    bool Run878(uint32_t numstars, const char* name, NameVars* vars)
    {
        uint32_t l10 = 0, l14 = 0, l18 = 0;
        const uint32_t l30 = d8852;
        if (numstars == 0) {
            const uint16_t s = FUNC_000881_Body(&cnt, l30, 0, 0, 0, name, vars, &l10, &l14, &l18);
            if (s == 0) return false;
            const uint32_t rnd = FUNC_000894_Rand();
            FUNC_000897_Orbits(&cnt, 1, rnd, s, l30, l30 >> 8, name, vars, l10, 0, 0, 0);
        } else if (numstars == 1) {
            --d8851;
            FUNC_000894_Rand();
            const uint32_t w = static_cast<uint16_t>(d8844) >> 3;
            const uint32_t l38 = (l30 & 0xe0000000u) ? 0xffffffffu : l30 << 3;
            const uint16_t s = FUNC_000877_Binary(&cnt, 0, l30, l38, 0, w, name, vars);
            const uint32_t rnd = FUNC_000894_Rand();
            FUNC_000897_Orbits(&cnt, 1, rnd, s, l38, l30 >> 6, name, vars, l10, 0, 0, 0);
        } else {
            FUNC_000894_Rand();
            const uint32_t w = static_cast<uint16_t>(d8844) >> 8;
            FUNC_000877_Binary(&cnt, 0, l30, 0xffffffffu, 0, w, name, vars);
            const uint16_t s = static_cast<uint16_t>(w << 4);
            --d8851;
            const uint32_t outer = (d8851 > 2) ? 0xffffffffu : l30 + 0x81b3200u;
            const uint32_t rnd = static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(FUNC_000894_Rand()) | 0x8000)) >> 2);
            FUNC_000897_Orbits(&cnt, 1, rnd, s, 0xffffffffu, outer, name, vars, l10, 0, 0, 0);
        }
        if (unsupported) return false;
        R(cnt).mass = 0;
        return true;
    }

    // FUNC_000897
    void FUNC_000897_Orbits(int* cnt, int parent, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7,
                const char* name, NameVars* vars, uint32_t p10, uint32_t p11, uint32_t p12,
                int depth)
    {
        if (depth > 16) {
            unsupported = true;
            return;
        }
        const uint32_t s1 = (p6 > 0x7fffffffu) ? 0xffffffffu : p6 * 2;
        uint16_t s3 = FUNC_000896_Density(p5);
        uint32_t edi = 0;
        uint16_t si = static_cast<uint16_t>(p5);
        for (;;) {
            edi += si;
            if (edi >= 0x10000) return;

            uint32_t ebx = ((FUNC_000894_Rand() & 0xffff) >> 3) + (p4 & 0xffff);
            uint32_t eax = (ebx >= 0x10000) ? si : ((static_cast<uint32_t>(si) * ebx) >> 16);
            ebx = eax;
            eax = si;
            edi = (eax >> 2) + (eax >> 3) + ebx;
            if (eax + edi >= 0x10000) return;

            uint32_t r = FUNC_000894_Rand() & 0xffff;
            r = (r * r) >> 16;
            r = (r * r) >> 16;
            const uint32_t d16 = FUNC_000896_Density(static_cast<uint16_t>(edi + si));
            ebx = (static_cast<uint32_t>(static_cast<int32_t>(d16) - static_cast<int32_t>(s3)) * r) >> 16;
            s3 = static_cast<uint16_t>(s3 + static_cast<uint16_t>(ebx));
            ebx = Mul1515(p7, ebx & 0xffff);

            if (ebx > 0x20) {
                if (ebx < 0x3a98) ebx >>= 3;
                const int s10 = *cnt;
                uint16_t sc = 0;
                const uint32_t half = static_cast<uint32_t>(static_cast<uint16_t>((edi >> 1) + si));
                if (ebx > kStarMass && d8851 > 1) {
                    // ffe.asm:67036-67072: a binary pair; FUNC_000877's return is
                    // discarded and the children orbit at density 0x3000.
                    if (ebx > (d8852 >> 1)) ebx = d8852 >> 1;
                    FUNC_000894_Rand();
                    FUNC_000877_Binary(cnt, parent, ebx, s1, half, (d8844 & 0xffff) >> 11, name, vars);
                    d8851 = static_cast<uint8_t>(d8851 - 2);
                    sc = 0x3000;
                    ebx >>= 2;
                } else {
                    if (ebx > kStarMass) {
                        if (d8851 == 0) {
                            const uint32_t a = FUNC_000894_Rand() & 0xffff;
                            ebx = a * 259;
                        } else {
                            --d8851;
                            const uint32_t cap = d8852 >> 1;
                            if (ebx > cap) {
                                ebx = cap;
                                if (ebx < kStarMass) ebx = kStarMass;
                            }
                        }
                    }
                    sc = FUNC_000881_Body(cnt, ebx, parent, s1, half, name, vars, &p10, &p11, &p12);
                }

                if (ebx <= 0x3a98) ebx <<= 3;
                const uint32_t s8 = (ebx > (s1 >> 6)) ? (s1 >> 6) : ebx;
                const uint32_t save43 = d8843, save44 = d8844;
                const uint8_t save49 = d8849;
                if (ebx > kStarMass)
                    d8849 = 0;
                else
                    ++d8849;
                char childName[64];
                GetName(s10, childName);
                const uint32_t rnd = FUNC_000894_Rand();
                FUNC_000897_Orbits(cnt, s10 + 1, rnd, sc, s8, ebx >> 7, childName, vars, p10, p11, p12,
                       depth + 1);
                d8843 = save43;
                d8844 = save44;
                d8849 = save49;
                if (unsupported) return;
            }
            si = static_cast<uint16_t>(si + edi);
        }
    }
};

// FUNC_000868: location-record index for a system code (<0 = default record).
int32_t LocationIndex(uint32_t code)
{
    int32_t i = 0;
    for (;; ++i) {
        const uint32_t c = RdU32(OFFSET_DATA_007110 + static_cast<uint32_t>(i) * 0x36);
        if (c == 0 || c == code) break;
        if (i > 4096) break;
    }
    if (RdU32(OFFSET_DATA_007110 + static_cast<uint32_t>(i) * 0x36) != 0) return i;
    const uint32_t masked = code & 0x03ffffffu;
    for (int k = 0; k < 4; ++k)
        if (RdU32(OFFSET_DATA_007116 + k * 4) == masked) return -1;
    const int32_t sub = static_cast<int32_t>((code >> 26) & 7);
    int32_t dx = static_cast<int32_t>(code & 0x1fff) - 0x1718;
    int32_t dy = static_cast<int32_t>((code >> 13) & 0x1fff) - 0x1524;
    dx = (dx > 0 ? dx : -dx) + sub;
    dy = (dy > 0 ? dy : -dy) + sub;
    const uint32_t d2 = static_cast<uint32_t>(dx * dx + dy * dy);
    if (d2 >= 0x4c90) return -8;
    if (d2 >= 0x121) return -7;
    if (d2 >= 0x64) return -6;
    if (d2 >= 0x31) return -5;
    if (d2 >= 0x19) return -4;
    if (d2 >= 9) return -3;
    return -2;
}

uint32_t LocRecord(int32_t idx)
{
    return static_cast<uint32_t>(static_cast<int64_t>(OFFSET_DATA_007110) +
                                 static_cast<int64_t>(idx) * 0x36);
}

bool EnsureHandcodedTemplates()
{
    const SysObj* h = SysObjHandcodedTable();
    if (h[16].type == static_cast<uint16_t>(SysObjType::kStarport) && h[2].mass != 0) return true;
    static SysObj s_tmp[kSysObjGeneratedCapacity];
    SysGenGenerateHandcodedSol(s_tmp, kSysObjGeneratedCapacity);
    return h[16].type == static_cast<uint16_t>(SysObjType::kStarport) && h[2].mass != 0;
}

}  // namespace

int32_t SysGenLocationIndex(uint32_t code) { return LocationIndex(code); }
uint32_t SysGenLocationRecord(int32_t index) { return LocRecord(index); }

bool SysGenStartParams(int startIndex, SysGenStartInfo* out)
{
    if (!out || !Blob() || startIndex < 0 || startIndex > 6) return false;
    std::memset(out, 0, sizeof(*out));

    // FUNC_000829: 20-byte start record → galactic x/y/z, sector byte, packed pair.
    const uint32_t rec = OFFSET_DATA_006820 + static_cast<uint32_t>(startIndex) * 20;
    const uint32_t x = RdU32(rec + 0), y = RdU32(rec + 4), z = RdU32(rec + 8);
    const uint32_t p1 = RdU32(rec + 0xc);
    const uint32_t pk = RdU32(rec + 0x10);
    const uint8_t p2 = static_cast<uint8_t>(pk & 0xff);        // → +0x26a
    const uint8_t p3 = static_cast<uint8_t>((pk >> 8) & 0xff); // → +0x269

    // FUNC_000832
    const uint32_t code = (static_cast<uint32_t>(static_cast<int32_t>(x) >> 16) & 0x1fff) |
                          ((static_cast<uint32_t>(static_cast<int32_t>(y) >> 16) & 0x1fff) << 13) |
                          ((p1 & 0x3f) << 26);

    // FUNC_000832 (ffe.asm:56815-56820): FUNC_000830(0, DATA_006824 copy, name, code).
    uint32_t nameCtx[5];
    std::memcpy(nameCtx, Blob() + OFFSET_DATA_006824, sizeof(nameCtx));
    char name[256] = {};
    FUNC_000830(0, reinterpret_cast<char*>(nameCtx), name, code);
    std::strncpy(out->params.name, name, sizeof(out->params.name) - 1);

    uint32_t rs1 = static_cast<uint32_t>(static_cast<int32_t>(y) >> 16) + (y << 16) + x - z;
    const int32_t loc = LocationIndex(code);
    if (p2 & 0x80) rs1 = 0;
    const uint32_t t = p2 & 0xf;
    const uint32_t lo = RdU32(OFFSET_DATA_006803 + t * 4);
    const uint32_t hi = RdU32(OFFSET_DATA_006804 + t * 4);
    const uint32_t rs2 = lo + ((hi - lo) >> 16) * ((static_cast<uint32_t>(p3) & 0xf8) << 8);

    out->params.randseed1 = rs1;
    out->params.randseed2 = rs2;
    out->params.numstars = p3 & 7;                         // FUNC_000855: b[D8954] & 7
    out->params.systemcode = code;
    out->params.extra = RdU8(LocRecord(loc) + 5);          // FUNC_000869 → +0x28c
    out->tier = RdU16(LocRecord(loc) + 0xa) >> 6;          // FUNC_000871 → DATA_007128
    out->locIndex = loc;
    out->timeLo = 0x80000000u;                              // ffe.asm 15105
    out->timeHi = 0x00121cf7u;                              // ffe.asm 15106
    out->galPos[0] = static_cast<int32_t>(x);
    out->galPos[1] = static_cast<int32_t>(y);
    out->galPos[2] = static_cast<int32_t>(z);
    return true;
}

int SysGenGenerateProcedural(const SysGenParams& params, int32_t tier, SysObj* out, int cap,
                             int32_t* orbitalStations)
{
    if (!out || cap <= 1 || !Blob()) return -1;
    if (params.randseed1 == 0 || params.numstars != 0) return -1;
    if (!EnsureHandcodedTemplates()) return -1;

    static Gen g;
    g = Gen{};
    g.scaffold = true;
    const SysObj* h = SysObjHandcodedTable();
    g.tpl8794 = &h[2];
    g.tpl8795 = &h[12];
    g.tpl8796 = &h[16];
    g.tpl8797 = &h[17];
    g.d8852 = params.randseed2;
    g.d8853 = params.systemcode;
    g.d8854 = static_cast<int32_t>(params.extra);
    g.tier = tier;
    g.d8843 = params.randseed1;
    g.d8844 = params.randseed2;
    g.d8851 = static_cast<uint8_t>(params.numstars);

    char name[24] = {};
    std::memcpy(name, params.name, sizeof(params.name));
    static NameVars vars;
    vars = NameVars{};
    uint32_t l1 = 0, l2 = 0, l3 = 0;
    const uint16_t s = g.FUNC_000881_Body(&g.cnt, params.randseed2, 0, 0, 0, name, &vars, &l1, &l2, &l3);
    if (s != 0) {
        const uint32_t rnd = g.FUNC_000894_Rand();
        g.FUNC_000897_Orbits(&g.cnt, 1, rnd, s, params.randseed2, params.randseed2 >> 8, name, &vars, l1, 0,
                 0, 0);
    }
    if (g.unsupported) return -1;
    g.R(g.cnt).mass = 0;

    const int n = g.cnt;
    if (n + 1 > cap) return -1;
    std::memcpy(out, g.rec, sizeof(SysObj) * static_cast<size_t>(n + 1));
    if (orbitalStations) *orbitalStations = static_cast<int8_t>(g.d8856);
    return n;
}

int SysGenRunProcedural878(const SysGenParams& params, int32_t tier, SysObj* out, int cap,
                           SysGenProcState* state)
{
    if (!state || !Blob() || params.randseed1 == 0) return -1;
    if (!EnsureHandcodedTemplates()) return -1;
    if (!out) cap = 0;
    if (cap > kGenScratch) cap = kGenScratch;

    static Gen g;
    g = Gen{};
    // The ASM generates in place: fields FUNC_000881/882 never write (+0x18,
    // +0x3a..+0x41, name tails, orbVel/latitude of zero-radius bodies) keep the
    // previous system's bytes.
    if (cap > 0) std::memcpy(g.rec, out, sizeof(SysObj) * static_cast<size_t>(cap));
    const SysObj* h = SysObjHandcodedTable();
    g.tpl8794 = &h[2];
    g.tpl8795 = &h[12];
    g.tpl8796 = &h[16];
    g.tpl8797 = &h[17];
    g.d8852 = params.randseed2;
    g.d8853 = params.systemcode;
    g.d8854 = static_cast<int32_t>(params.extra);
    g.tier = tier;
    g.d8843 = params.randseed1;
    g.d8844 = params.randseed2;
    g.d8851 = static_cast<uint8_t>(params.numstars);

    char name[24] = {};
    std::memcpy(name, params.name, sizeof(params.name));
    static NameVars vars;
    vars = NameVars{};
    const bool terminated = g.Run878(params.numstars, name, &vars);
    if (g.unsupported) return -1;

    const int written = g.cnt + (terminated ? 1 : 0);
    if (written > cap && cap > 0)
        std::fprintf(stderr, "SysGenRunProcedural878: %d records, destination holds %d\n", written,
                     cap);
    const int n = written < cap ? written : cap;
    if (n > 0) std::memcpy(out, g.rec, sizeof(SysObj) * static_cast<size_t>(n));

    state->d8843 = g.d8843;
    state->d8844 = g.d8844;
    state->d8846 = g.d8846;
    state->d8847 = g.d8847;
    state->d8848 = g.d8848;
    state->d8849 = g.d8849;
    state->d8850 = static_cast<uint8_t>(params.numstars);
    state->d8851 = g.d8851;
    state->d8855 = g.d8855;
    state->d8856 = g.d8856;
    return g.cnt;
}

namespace {

// The cold-boot spawn table: FUNC_000927 on an empty table.
struct SpawnTableSlots {
    SpawnSlotTable* t;
    PhysObj* Obj(int s) { return &t->slots[s]; }
    uint8_t& Type(int s) { return t->slotType[s]; }
    PhysObj* Alloc(intptr_t src, uint16_t model, uint32_t timeLo, uint32_t timeHi, int* slotOut)
    {
        // Highest free slot 0x72..1, list byte = 0x1d.
        int slot = kSpawnSlotCount - 1;
        while (slot > 0 && t->slotType[slot] != 0) --slot;
        if (slot < 0x14) return nullptr;
        t->slotType[slot] = 0x1d;
        PhysObj* obj = &t->slots[slot];
        FUNC_001533(reinterpret_cast<intptr_t>(obj->raw), src, model);
        PhysObjSetI32(obj, 0xa8, static_cast<int32_t>(timeLo));
        PhysObjSetI32(obj, 0xac, static_cast<int32_t>(timeHi));
        PhysObjSetModel(obj, model);
        {
            const uint8_t old86 = PhysObjGetIndex(obj);
            PhysObjSetIndex(obj, static_cast<uint8_t>(slot));
            // R1224: env-gated +0x86 trace (SpawnTableSlots mirrors FUNC_000927).
            static int s_on = -1, s_cap = 0, s_logs = 0, s_model = -1;
            if (s_on < 0) {
                const char* e = std::getenv("R1224_W86");
                s_on = (e && e[0] == '1') ? 1 : 0;
                s_cap = 4096;
                if (const char* p = std::getenv("R1224_W86_CAP"))
                    if (p[0]) s_cap = std::atoi(p);
                if (const char* p = std::getenv("R1224_W86_MODEL"))
                    if (p[0]) s_model = std::atoi(p);
            }
            if (s_on && s_logs < s_cap && old86 != static_cast<uint8_t>(slot) &&
                (s_model < 0 || static_cast<int>(model) == s_model)) {
                std::fprintf(stderr,
                             "R1224_W86 n=%d clock=0x0 site=SpawnTableSlots_SetIndex obj=%p "
                             "model=%d %u->%u\n",
                             s_logs, static_cast<void*>(obj->raw), static_cast<int>(model),
                             static_cast<unsigned>(old86), static_cast<unsigned>(slot));
                ++s_logs;
            }
        }
        *slotOut = slot;
        return obj;
    }
    void Placed(int, PhysObj*) {}
};

// An ASM-layout object pool (DATA_009133 / DATA_008801): list bytes at +0..+0x72,
// objects at +0x74 + slot*0x152, allocated by the real FUNC_000927.
struct PoolSlots {
    uint8_t* base;
    SysObj* sys;
    PhysObj* Obj(int s) { return reinterpret_cast<PhysObj*>(base + 0x74 + s * kPhysObjSize); }
    uint8_t& Type(int s) { return base[s]; }
    PhysObj* Alloc(intptr_t src, uint16_t model, uint32_t, uint32_t, int* slotOut)
    {
        int32_t slot = 0;
        FUNC_000927(reinterpret_cast<intptr_t>(base), src, 0x1d, model, &slot);
        if (slot <= 0) return nullptr;
        *slotOut = slot;
        return Obj(slot);
    }
    // ffe.asm:64811 stores the object pointer at SysObj +0x18. The field is 32-bit,
    // so on LP64 it holds the low half only; parents resolve through sysToSlot.
    void Placed(int i, PhysObj* obj)
    {
        sys[i].tempPtr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
    }
};

// FUNC_000880 (ffe.asm:64773-65062) over either slot store.
template <class Slots>
int MaterializeInto(Slots& table, const SysObj* sys, int count, uint32_t timeLo, uint32_t timeHi,
                    uint8_t worldGen, int* sysToSlot)
{
    InitShipModels();
    for (int k = 0; k < count; ++k) sysToSlot[k] = -1;

    int done = 0;
    for (int i = 0; i < count && sys[i].mass != 0; ++i) {
        const SysObj& so = sys[i];

        const int parentSlot =
            (so.parentIndex != 0 && so.parentIndex - 1 < i) ? sysToSlot[so.parentIndex - 1] : -1;
        const intptr_t src = parentSlot >= 0
            ? reinterpret_cast<intptr_t>(table.Obj(parentSlot)->raw)
            : reinterpret_cast<intptr_t>(Blob() + OFFSET_DATA_007134);
        int slot = 0;
        PhysObj* obj = table.Alloc(src, so.modelIndex, timeLo, timeHi, &slot);
        if (!obj) break;
        sysToSlot[i] = slot;
        table.Placed(i, obj);

        // FUNC_000880 body
        PhysObjSetSeed(obj, static_cast<int32_t>(so.randseed));
        PhysObjSetU8(obj, 0xc8, so.rotSpeed);
        PhysObjSetU8(obj, 0xcb, so.treeLevel);
        PhysObjSetU16(obj, 0xe0, so.temperature);
        PhysObjSetU16(obj, 0xcc, static_cast<uint16_t>(so.orientation));
        const uint32_t m = FpNormalise(FpMul(static_cast<uint32_t>(so.mass), 0xffd54082u));
        PhysObjSetI32(obj, 0xa4, static_cast<int32_t>(m));
        int32_t da = Hi(m);
        if (so.type == 0) {
            table.Type(slot) = 0x11;
            da += 2;  // ffe.asm FIX: was +0x10
        }
        if (so.parentIndex != 0 && sys[so.parentIndex - 1].type == 0 &&
            static_cast<uint32_t>(so.parentIndex) + 1 >= static_cast<uint32_t>(i)) {
            da -= 6;  // ffe.asm FIX: was -0xa
        }
        PhysObjSetU16(obj, 0xda, static_cast<uint16_t>(da));
        PhysObjSetOrbRadius(obj, so.orbRadius);
        PhysObjSetControl(obj, static_cast<uint8_t>(PhysObjControl::kOrbital));

        if (so.type >= 0x20) {
            PhysObjSetU16(obj, 0xda, static_cast<uint16_t>(PhysObjGetU16(obj, 0xda) + 4));
            PhysObjOrFlags(obj, kPhysFlagDockable);
            PhysObjSetU16(obj, 0xce, 3);
            table.Type(slot) = 0x1f;
            if (so.type == 0x22) PhysObjSetU16(obj, 0xce, 7);
            PhysObjSetU8(obj, 0xca, 0);
            PhysObjSetU8(obj, 0xc9, 0);
            PhysObjSetU16(obj, 0xd8, 0);
            PhysObjSetU16(obj, 0xdc, static_cast<uint16_t>((PhysObjGetU16(obj, 0xce) << 3) + 8));
            if (so.type == 0x23) {
                PhysObjSetParent(obj, static_cast<uint8_t>(parentSlot >= 0 ? parentSlot : 0));
                PhysObjOrFlags(obj, kPhysFlagSurfaceStarport);
                const uint16_t mdl = so.modelIndex;
                if (mdl >= 0x51 && mdl <= 0x5a) PhysObjOrFlags(obj, kPhysFlagOpenAir);
                if (mdl != 0x51 && mdl != 0x52) PhysObjSetU16(obj, 0xce, 5);
                if (mdl == 0x55 || mdl == 0x56 || mdl == 0x59) PhysObjSetU16(obj, 0xce, 1);
                PhysObjSetU16(obj, 0xdc, static_cast<uint16_t>((PhysObjGetU16(obj, 0xce) << 3) + 8));
                table.Type(slot) = 0x0f;
                PhysObjSetU8(obj, 0x58, static_cast<uint8_t>(worldGen - 1));
                PhysObjSetU8(obj, 0x57, 1);
                PhysObjSetI32(obj, 0xc2, 0);

                // FUNC_000879 (ffe.asm 64530-64769): lat/lon basis, then the
                // parent-model radius lowered by two FUNC_000574 terrain queries.
                int mat[9] = {};
                FUNC_001674_MatBuildOdd(mat, static_cast<uint16_t>(so.latitude),
                                        static_cast<uint16_t>(so.longitude));
                std::memcpy(obj->raw, mat, sizeof(mat));
                PhysObj* parent = parentSlot >= 0 ? table.Obj(parentSlot) : nullptr;
                const ShipModel* pm = parent ? GetShipModel(PhysObjGetModel(parent)) : nullptr;
                if (pm) {
                    const int32_t row[3] = {mat[3], mat[4], mat[5]};
                    int32_t esi = static_cast<int32_t>(pm->viewZOffset) - 1;
                    int32_t ebx = static_cast<int32_t>(pm->scaleRelated + pm->scaleFactor) - 8;
                    int64_t pos[3];
                    // FUNC_001338_Int64Mul32 (row sign-extended × esi unsigned)
                    // then FUNC_001341_Int64ArithShift(ebx - 0x1f).
                    auto place = [&]() {
                        for (int a = 0; a < 3; ++a)
                            pos[a] = Shift64(static_cast<int64_t>(static_cast<uint64_t>(static_cast<int64_t>(row[a])) *
                                                                  static_cast<uint32_t>(esi)),
                                             ebx - 0x1f);
                    };
                    place();
                    int32_t hgt = FUNC_000574_TerrainHeight(pos, obj, parent);
                    hgt -= hgt >> 3;  // ffe.asm FIX "Starports below ground": was >> 4
                    if (ebx < 0) {
                        esi -= static_cast<int32_t>(static_cast<uint32_t>(hgt) << (-ebx & 31));
                    } else if (ebx < 16) {
                        esi = static_cast<int32_t>(static_cast<uint32_t>(esi) << ebx) - hgt;
                        ebx = 0;
                    } else {
                        ebx -= 16;
                        esi = static_cast<int32_t>(static_cast<uint32_t>(esi) << 16) - (hgt >> (ebx & 31));
                    }
                    place();
                    hgt = FUNC_000574_TerrainHeight(pos, obj, parent);
                    if (ebx < 0)
                        esi -= static_cast<int32_t>(static_cast<uint32_t>(hgt) << (-ebx & 31));
                    else
                        esi -= hgt >> (ebx & 31);
                    place();
                    std::memcpy(obj->raw + 0x3e, pos, sizeof(pos));
                }
            }
        }
        if (so.type != 0x23) {
            if (parentSlot >= 0) PhysObjSetParent(obj, static_cast<uint8_t>(parentSlot));
            uint32_t u = FpDiv(0xffee5460u, FpNormalise(static_cast<uint32_t>(so.orbVel)));
            u = AddHi(u, 0x40);
            const uint32_t angVel = FpToUnsigned(u);
            PhysObjSetI32(obj, 0xc2, static_cast<int32_t>(angVel));
            PhysObjSetI32(obj, 0xa8, static_cast<int32_t>(timeLo));
            PhysObjSetI32(obj, 0xac, static_cast<int32_t>(timeHi));
            uint32_t ang[2] = {0, 0};
            // ASM pushes [ebp+0x14] then [ebp+0x10]: FUNC_001510 sees (timeLo, timeHi)
            // as its (new hi, new lo) pair.
            FUNC_001510(ang, angVel, 0, 0, static_cast<int32_t>(timeLo), timeHi);
            std::memcpy(obj->raw + 0xba, ang, sizeof(ang));
            PhysObjSetI32(obj, 0xbe,
                          static_cast<int32_t>(static_cast<uint32_t>(PhysObjGetI32(obj, 0xbe)) +
                                               (static_cast<uint32_t>(static_cast<uint16_t>(so.latitude)) << 16)));
            PhysObjSetU16(obj, 0xc6, static_cast<uint16_t>(so.longitude));
            FUNC_001507(reinterpret_cast<intptr_t>(obj->raw),
                        static_cast<uint32_t>(PhysObjGetOrbRadius(obj)),
                        static_cast<uint32_t>(static_cast<int32_t>(so.longitude)),
                        static_cast<uint32_t>(PhysObjGetI32(obj, 0xbe)));
            const int sh = (0x11 - static_cast<int8_t>(PhysObjGetU8(obj, 0xc8))) & 0x1f;
            int32_t mat[9];
            std::memcpy(mat, obj->raw, sizeof(mat));
            FUNC_001675_MatBuildYZT(mat, static_cast<int16_t>(PhysObjGetU16(obj, 0xcc)),
                                    static_cast<int32_t>(timeLo >> sh));
            std::memcpy(obj->raw, mat, sizeof(mat));
        }
        // ffe.asm:65025-65047: strcpy, so bytes past the NUL keep what FUNC_001533
        // copied from the parent.
        std::memcpy(obj->raw + 0x124, so.name, strnlen(so.name, sizeof(so.name) - 1) + 1);
        ++done;
    }
    return done;
}

}  // namespace

int SysGenMaterializeSlots(const SysObj* sys, int count, uint32_t timeLo, uint32_t timeHi,
                           SpawnSlotTable* table)
{
    if (!sys || !table || count <= 0 || count > kSysObjGeneratedCapacity || !Blob()) return 0;
    std::memset(table, 0, sizeof(*table));
    for (int& s : table->sysToSlot) s = -1;
    SpawnTableSlots slots{table};
    table->sysCount = MaterializeInto(slots, sys, count, timeLo, timeHi, kStartGameWorldGen,
                                      table->sysToSlot);
    return table->sysCount;
}

int SysGenMaterializePool(intptr_t pool, SysObj* sys, int count, uint32_t timeLo,
                          uint32_t timeHi, uint8_t worldGen)
{
    if (!pool || !sys || count <= 0 || count > kSysObjGeneratedCapacity || !Blob()) return 0;
    int sysToSlot[kSysObjGeneratedCapacity];
    PoolSlots slots{reinterpret_cast<uint8_t*>(pool), sys};
    return MaterializeInto(slots, sys, count, timeLo, timeHi, worldGen, sysToSlot);
}

bool SysGenBuildSpawnTable(SpawnSlotTable* table, SysObj* sysOut, int sysCap,
                           SysGenStartInfo* infoOut, int startIndex)
{
    if (!table || !sysOut || sysCap < kSysObjGeneratedCapacity) return false;
    SysGenStartInfo info;
    if (!SysGenStartParams(startIndex, &info)) return false;
    int n = 0;
    if (info.params.randseed1 != 0) {
        n = SysGenGenerateProcedural(info.params, info.tier, sysOut, sysCap, &info.orbitalStations);
    } else {
        if (!EnsureHandcodedTemplates()) return false;
        std::memset(sysOut, 0, sizeof(SysObj) * static_cast<size_t>(sysCap));
        SysGenProcState st{};
        n = SysGenRunHandcoded878(info.params.systemcode, sysOut, sysCap, &st);
        if (n + 1 > sysCap) return false;
        info.orbitalStations = st.d8856;
    }
    if (n <= 0) return false;
    if (SysGenMaterializeSlots(sysOut, n, info.timeLo, info.timeHi, table) != n) return false;
    if (infoOut) *infoOut = info;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Task 4.162 (shadow-chain consolidation, stage 2a): the ORACLE BRIDGE.
//
// FUNC_000892 has two ports - this file's live `Gen::Station()` and a dead duplicate at
// core.cpp:12845 that only the call-oracle adapter reached, so the `892_*` probes (4.155) have
// been guarding the wrong copy. The live generator cannot be probed directly because:
//   * its state (`d8843`, `d8855`, `d8856`) lives in `Gen` members, not the shared labels the
//     oracle writes with reg:S+... / lab:DATA_...;
//   * its station TEMPLATES are the handcoded table `h[]`, while the ASM's 892 copies the
//     writable `.data` templates DATA_008794/8795/8797 (the probes' fixture);
//   * it reads the model-index tables BLOB-DIRECT (`RdU16(OFFSET_DATA_007136 + sel*4)`), while
//     the oracle writes the shared statics.
// This bridge routes the fixture to wherever the live implementation reads it, using only
// writable/official storage: shared labels in, shared labels out, blob bytes restored by RAII.
// It changes NO production path - the game keeps calling Station through the sysgen entry points.
//
// Contract mirrors the ASM's FUNC_000892: records is the 0x44-byte-record destination base,
// *cntInOut the record counter (in: current count, out: count after), `vars` the NameVars the
// name generator writes through, `parent` the parent slot written at record +0x08.
int32_t SysGenStationProbe(int32_t* records, int32_t* cntInOut, uint32_t type, void* vars,
                           int32_t parent)
{
    if (!EnsureHandcodedTemplates()) return -1;
    const SysObj* h = SysObjHandcodedTable();

    static Gen g;
    g = Gen{};
    // Fixture-controllable storage: the oracle writes these (shared since 4.162).
    g.tpl8794 = reinterpret_cast<const SysObj*>(DATA_008794);
    g.tpl8795 = reinterpret_cast<const SysObj*>(DATA_008795);
    g.tpl8796 = &h[16];
    g.tpl8797 = reinterpret_cast<const SysObj*>(DATA_008797);
    g.cnt = cntInOut ? *cntInOut : 0;
    g.d8843 = DATA_008843;
    g.d8855 = DATA_008855;
    g.d8856 = DATA_008856;
    g.d8846 = DATA_008846;

    // The live Station reads the model tables from the blob; lift the fixture (the shared
    // statics) into those blob bytes for the call and restore them afterwards.
    struct BlobTableGuard {
        uint8_t* blob = nullptr;
        uint8_t saved[3][16];
        static const uint32_t* offs() {
            static const uint32_t o[3] = {OFFSET_DATA_007136, OFFSET_DATA_007137,
                                          OFFSET_DATA_007138};
            return o;
        }
        BlobTableGuard() {
            const uint8_t* b = data::GetDataBlob();
            if (!b) return;
            blob = const_cast<uint8_t*>(b);
            const uint32_t* o = offs();
            const uint8_t* src[3] = {reinterpret_cast<const uint8_t*>(DATA_007136),
                                     reinterpret_cast<const uint8_t*>(DATA_007137),
                                     reinterpret_cast<const uint8_t*>(DATA_007138)};
            for (int i = 0; i < 3; ++i) {
                std::memcpy(saved[i], blob + o[i], sizeof(saved[i]));
                std::memcpy(blob + o[i], src[i], sizeof(saved[i]));
            }
        }
        ~BlobTableGuard() {
            if (!blob) return;
            const uint32_t* o = offs();
            for (int i = 0; i < 3; ++i) std::memcpy(blob + o[i], saved[i], sizeof(saved[i]));
        }
    } guard;

    g.Station(&g.cnt, type, reinterpret_cast<NameVars*>(vars), parent);

    // Publish the state the ASM leaves in memory: the span records DATA_008843/8855/8856/8846,
    // so the probe must see the values the live path produced (its rng advances d8843).
    DATA_008843 = g.d8843;
    DATA_008855 = g.d8855;
    DATA_008856 = g.d8856;
    DATA_008846 = g.d8846;

    if (records && g.cnt >= 0) {
        const int n = (g.cnt + 1 < kGenScratch) ? g.cnt + 1 : kGenScratch;
        std::memcpy(records, g.rec, sizeof(SysObj) * static_cast<size_t>(n));
    }
    if (cntInOut) *cntInOut = g.cnt;
    return 0;
}

}  // namespace jjffe
