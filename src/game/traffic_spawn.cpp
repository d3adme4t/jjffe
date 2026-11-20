// Task 4.27 — new-game station markets and traffic ships (include/jjffe/traffic_spawn.h).
#include "jjffe/traffic_spawn.h"

#include "data_blob.h"
#include "jjffe/data.h"
#include "jjffe/data_loader.h"
#include "jjffe/phys_obj.h"
#include "jjffe/player_pad.h"
#include "jjffe/ship_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace jjffe {

void FUNC_001533(intptr_t dst, intptr_t src, int32_t param_3);
void FUNC_001507(intptr_t param_1, uint32_t param_2, uint32_t param_3, uint32_t param_4);

namespace {

using namespace data;

// FUNC_000147 (ffe.asm 15098-15101): new-game seed of DATA_008818/8820.
constexpr uint32_t kSeed818 = 0x12345678u;
constexpr uint32_t kSeed820 = 0x456789abu;

// FUNC_000702's packed FUNC_001507 radii (mantissa word, exponent word).
constexpr uint32_t kArrivalRadius = 0x00335000u;   // FUNC_000689/690/694-696
constexpr uint32_t kStationRadius = 0x00185000u;   // FUNC_000699

// DATA_006200: the ship-type table's pointer column (symbolic in ffedat.asm,
// zero in the blob), in DATA_006199 entry order.
constexpr uint32_t kShipTypeTable[19] = {
    OFFSET_DATA_006184, OFFSET_DATA_006185, OFFSET_DATA_006182, OFFSET_DATA_006183,
    OFFSET_DATA_006180, OFFSET_DATA_006181, OFFSET_DATA_006186, OFFSET_DATA_006187,
    OFFSET_DATA_006188, OFFSET_DATA_006189, OFFSET_DATA_006190, OFFSET_DATA_006191,
    OFFSET_DATA_006192, OFFSET_DATA_006193, OFFSET_DATA_006194, OFFSET_DATA_006195,
    OFFSET_DATA_006196, OFFSET_DATA_006197, OFFSET_DATA_006198,
};

// DATA_006170 (FUNC_000621's advert handlers) and DATA_006201 (FUNC_000623's
// news handlers), by FUNC_ number.
constexpr uint16_t kAdvertFn[32] = {
    639, 639, 639, 639, 639, 640, 642, 642, 642, 642, 636, 637, 637, 638, 638, 638,
    638, 638, 638, 638, 638, 638, 638, 638, 641, 641, 641, 645, 643, 643, 644, 644,
};
constexpr uint16_t kNewsFn[16] = {
    629, 629, 629, 629, 629, 629, 629, 629, 630, 630, 631, 631, 626, 626, 627, 628,
};

const uint8_t* Blob() { return GetLoadedBlob(); }

int32_t RdI32(uint32_t off)
{
    int32_t v;
    std::memcpy(&v, Blob() + off, sizeof(v));
    return v;
}
uint32_t RdU32(uint32_t off) { return static_cast<uint32_t>(RdI32(off)); }
int16_t RdI16(uint32_t off)
{
    int16_t v;
    std::memcpy(&v, Blob() + off, sizeof(v));
    return v;
}
uint16_t RdU16(uint32_t off) { return static_cast<uint16_t>(RdI16(off)); }
uint8_t RdU8(uint32_t off) { return Blob()[off]; }

uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
int32_t Mul32(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
// FUNC_001521: high dword of 2·a·b.
int32_t Mul1521(int32_t a, int32_t b)
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 31);
}

int64_t I64(const PhysObj* o, int off)
{
    int64_t v;
    std::memcpy(&v, o->raw + off, sizeof(v));
    return v;
}
void SetI64(PhysObj* o, int off, int64_t v) { std::memcpy(o->raw + off, &v, sizeof(v)); }

uint32_t SpecsOffset(int32_t model)
{
    const ShipModel* sm = GetShipModel(model);
    return sm ? sm->specsDataOffset : 0;
}

// FUNC_000869 outputs, in its argument order.
struct Loc869 {
    int32_t d2e, d32;
    uint32_t b5, b6, b7, b8, b9, wA;
};
// FUNC_000870 outputs (DATA_007111 / 7113 / 7112) and the DATA_008790 copy.
struct Loc870 {
    uint32_t b4, bC, wA;
};

struct Ctx {
    SpawnSlotTable* t = nullptr;
    TrafficSpawnResult* res = nullptr;

    uint32_t s818 = kSeed818, s820 = kSeed820;  // DATA_008818 / DATA_008820
    uint32_t timeLo = 0, timeHi = 0;            // DATA_008804 / DATA_008807
    uint32_t sysCode = 0;                       // DATA_008885
    int32_t galPos[3] = {};                     // DATA_008950..52
    int playerSlot = 0;                         // DATA_008857
    int32_t d9011 = 0;                          // orbital starports
    int32_t d8548_16 = 0;  // DATA_008548[0x16]: bss, no FUNC_000034(0x16, …) before 688

    uint16_t d8991 = 0, d8993 = 0, d8994 = 0, d8995 = 0, d8996 = 0;
    int16_t d8992 = 0;
    int32_t d9010 = 0;
    uint8_t d8790[0x26] = {};  // FUNC_000870's scratch copy
    uint8_t d8989[0x26] = {};  // FUNC_000622's copy of it
    int32_t d9001 = 0;
    // FUNC_000998 zeroes DATA_008912/8913, which FUNC_000871 copies here.
    int32_t d9002 = 0, d9003 = 0;
    int32_t d9006 = 0, d9007 = 0, d9008 = 0, d9009 = 0;
    int32_t news[26][6] = {};  // DATA_009004 (FUNC_000624)
    uint32_t d8947 = 0, d8948 = 0;

    bool trace = false;
    int draws = 0;

    StationRecord* St(int i) { return &res->stations[i]; }
    PhysObj* Slot(int i) { return &t->slots[i]; }

    // ------------------------------------------------------------------ RNG
    // FUNC_001530
    uint32_t R1530()
    {
        if (trace) std::printf("D1530 %d %08x %08x\n", ++draws, s818, s820);
        const uint32_t edx = s818;
        s818 += Rol(s820 ^ 0x1234567fu, 25);
        s820 = Rol(edx, 13);
        return s818 & 0xffff;
    }
    // FUNC_000922
    uint32_t R922(uint32_t n)
    {
        if (trace)
            std::printf("D922 %d %08x %08x arg=%d\n", ++draws, s818, s820, static_cast<int32_t>(n));
        const uint32_t edx = Rol(s820, 5);
        const uint32_t eax = Rol(s818 + s820 + edx, 16);
        s818 = eax;
        s820 = edx;
        return ((eax & 0xffff) * n) >> 16;
    }

    // --------------------------------------------------------- location data
    Loc869 F869(uint32_t code)
    {
        const uint32_t r = SysGenLocationRecord(SysGenLocationIndex(code));
        return {RdI32(r + 0x2e), RdI32(r + 0x32), RdU8(r + 5), RdU8(r + 6),
                RdU8(r + 7),     RdU8(r + 8),     RdU8(r + 9), RdU16(r + 0xa)};
    }
    Loc870 F870(uint32_t code)
    {
        const uint32_t r = SysGenLocationRecord(SysGenLocationIndex(code));
        std::memset(d8790, 0, sizeof(d8790));
        std::memcpy(d8790, Blob() + r + 0xd, 0x1f);
        d8790[0x20] = 0xd;  // DATA_008791
        return {RdU8(r + 4), RdU8(r + 0xc), RdU16(r + 0xa)};
    }
    // FUNC_000871 → DATA_009001's multiplier (DATA_009002/9003 stay 0).
    int32_t F871(uint32_t code)
    {
        const uint32_t r = SysGenLocationRecord(SysGenLocationIndex(code));
        const int32_t hi = RdU8(r + 0x2c) >> 4, lo = RdU8(r + 0x2c) & 0xf;
        const int32_t w = RdU16(r + 0xa) >> 6;
        return (hi > lo || (hi == lo && w == 1)) ? hi : lo;
    }

    // ------------------------------------------------------- galaxy helpers
    // FUNC_000853: star density at sector (x, y).
    static int32_t F853(uint32_t xArg, uint32_t yArg, int32_t mode)
    {
        const uint16_t dx = static_cast<uint16_t>(xArg), cx = static_cast<uint16_t>(yArg);
        if (dx >= 0x2000 || cx >= 0x2000) return 0;
        if (mode == 0) {
            const uint32_t key = dx | (static_cast<uint32_t>(cx) << 16);
            for (int i = 0; i < 0x31; ++i)
                if (RdU32(OFFSET_DATA_006799 + i * 4) == key)
                    return (RdI32(OFFSET_DATA_006801 + i * 4) - RdI32(OFFSET_DATA_006800 + i * 4)) << 10;
        }
        int32_t eax = static_cast<int32_t>(dx) << 3;
        int32_t edx = static_cast<int32_t>(cx) << 3;
        const int32_t cell = ((edx & 0xfe00) + (eax >> 7)) >> 2;
        const int32_t c0 = RdU8(OFFSET_DATA_006806 + cell);
        const int32_t c128 = RdU8(OFFSET_DATA_006808 + cell);
        const int32_t c129 = RdU8(OFFSET_DATA_006809 + cell);
        const int32_t c1 = RdU8(OFFSET_DATA_006807 + cell);
        eax = (eax & 0x1ff) << 6;
        edx = (edx & 0x1ff) << 6;
        int32_t acc = Mul32(c1 - c0, eax) + Mul32(c128 - c0, edx);
        acc += Mul32(Mul32(eax, edx) >> 15, c129 - c128 - c1 + c0);
        acc = (Mul32(acc, 2) + (c0 << 16)) >> 8;
        if (mode >= 16) return acc;
        const int32_t k = (((eax + acc) ^ (Mul32(eax, edx) >> 15)) >> 5) & 0x7f;
        const int32_t v = RdI32(OFFSET_DATA_006805 + k * 4);
        if (mode == 0) return static_cast<int32_t>(static_cast<uint32_t>(Mul32(acc, v)) >> 16);
        const int32_t e = 0xffff - (Mul32(16 - mode, v) >> 5);
        return static_cast<int32_t>(static_cast<uint32_t>(Mul32(acc, e)) >> 16);
    }

    // FUNC_000857: a neighbouring system code (0 = none). Mode 0 keeps the
    // sector, 1/2 step by the DATA_006816/6817 or 6818/6819 offsets.
    uint32_t F857(uint32_t code, uint32_t mode)
    {
        uint32_t v = code;
        if (mode == 0) {
            v &= 0x03ffffffu;
        } else if (mode == 1 || mode == 2) {
            const uint32_t tx = mode == 1 ? OFFSET_DATA_006816 : OFFSET_DATA_006818;
            const uint32_t ty = mode == 1 ? OFFSET_DATA_006817 : OFFSET_DATA_006819;
            int32_t d = RdI16(tx + R922(12) * 4);
            const uint16_t lo = static_cast<uint16_t>(v);
            const uint16_t nx = static_cast<uint16_t>(((lo + d) & 0x1fff) | (lo & 0xe000));
            v = (v & 0xffff0000u) | nx;
            d = RdI16(ty + R922(12) * 4);
            const uint32_t ny = (static_cast<uint32_t>(static_cast<int32_t>(v >> 13) + d)) & 0x1fff;
            v = (ny << 13) | (v & 0xfc001fffu);
        } else {
            return 0;
        }
        const int32_t n = F853(v & 0x1fff, (v >> 13) & 0x1fff, 0) >> 10;
        if (n == 0) return 0;
        const uint32_t r = R922(static_cast<uint32_t>(n));
        const uint32_t b3 = (((r & 0x3f) << 2) | ((v >> 24) & 3)) & 0xff;
        v = (v & 0x00ffffffu) | (b3 << 24);
        return v == code ? 0 : v;
    }

    // FUNC_000861
    void F861()
    {
        const uint32_t a = d8947, s = d8947 + d8948;
        d8947 = Rol(a, 3) + s;
        d8948 = Rol(s, 5);
    }

    // FUNC_000841 (no callbacks): the sector's star records (6 bytes each).
    int F841(uint8_t buf[64][6], int32_t xs, int32_t ys)
    {
        const int32_t x = xs >> 16, y = ys >> 16;
        const uint32_t key = static_cast<uint16_t>(x) | (static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16);
        for (int i = 0; i < 0x31; ++i) {
            if (RdU32(OFFSET_DATA_006799 + i * 4) != key) continue;
            const int32_t first = RdI32(OFFSET_DATA_006800 + i * 4);
            const int32_t n = RdI32(OFFSET_DATA_006801 + i * 4) - first;
            for (int k = 0; k < n && k < 64; ++k)
                std::memcpy(buf[k], Blob() + OFFSET_DATA_006793 + static_cast<uint32_t>(first + k) * 6, 6);
            return n;
        }
        int32_t n = F853(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0) >> 10;
        if (n >= 0x3f) n = 0x3e;
        if (n == 0) return 0;
        d8947 = (key >> 16) | (key << 16);
        d8948 = key;
        F861();
        F861();
        F861();
        for (int k = 0; k < n; ++k) {
            F861();
            buf[k][0] = 9;
            buf[k][2] = static_cast<uint8_t>((d8947 & 0xff0000u) >> 16);
            buf[k][3] = static_cast<uint8_t>(static_cast<int16_t>(d8947 & 0xff00u) >> 9);
            buf[k][4] = static_cast<uint8_t>(static_cast<int8_t>((d8947 & 0x1feu) >> 1) >> 1);
        }
        return n;
    }

    // FUNC_000856: galactic position of a star record.
    static void F856(int32_t xs, int32_t ys, const uint8_t* rec, int32_t out[3])
    {
        auto frac = [](uint8_t b) {
            return static_cast<uint32_t>(((uint32_t)static_cast<int32_t>(static_cast<int8_t>(b)) << 9) + 0x8000u) & 0xffffu;
        };
        out[0] = static_cast<int32_t>((static_cast<uint32_t>(xs) & 0xffff0000u) + frac(rec[4]));
        out[1] = static_cast<int32_t>((static_cast<uint32_t>(ys) & 0xffff0000u) + frac(rec[3]));
        const int32_t z = static_cast<int8_t>(rec[2]);
        uint32_t ecx = z > 0 ? 0xffffu : 0u;
        ecx |= static_cast<uint32_t>(-z) << 16;
        out[2] = -(static_cast<int32_t>(ecx) >> 7);
    }

    // FUNC_000833: distance between two galactic positions.
    static int32_t F833(const int32_t a[3], const int32_t b[3])
    {
        auto d = [](int32_t p, int32_t q) {
            const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(q) - static_cast<uint32_t>(p)) >> 8;
            return v < 0 ? -v : v;
        };
        int32_t x = d(a[0], b[0]), y = d(a[1], b[1]), z = d(a[2], b[2]);
        int32_t sh = 14;
        do {
            if (x > 0x7fff) { x >>= 1; y >>= 1; z >>= 1; --sh; }
            if (y > 0x7fff) { x >>= 1; y >>= 1; z >>= 1; --sh; }
            if (z > 0x7fff) { x >>= 1; y >>= 1; z >>= 1; --sh; }
        } while (x > 0x7fff || y > 0x7fff || z > 0x7fff);
        const int32_t sum = Mul32(x, x) + Mul32(y, y) + Mul32(z, z);
        // _sqrt + FUNC_002095_fstore (truncating fistp); a negative sum is NaN,
        // whose integer-indefinite low dword is 0.
        const int32_t r = sum < 0 ? 0 : static_cast<int32_t>(std::sqrt(static_cast<double>(sum)));
        return Mul32(r, 0xc8b4) >> (sh & 31);
    }

    // FUNC_000852 (via FUNC_000859): distance to the star `code`, or -1.
    int32_t F852(uint32_t code)
    {
        const int32_t xs = static_cast<int32_t>((code & 0x1fff) << 16);
        const int32_t ys = static_cast<int32_t>(((code >> 13) & 0x1fff) << 16);
        uint8_t buf[64][6] = {};
        const int n = F841(buf, xs, ys);
        const uint32_t idx = (code >> 26) & 0x3f;
        if (!(idx < static_cast<uint32_t>(n))) return -1;
        int32_t pos[3];
        F856(xs, ys, buf[idx], pos);
        return F833(pos, galPos);
    }

    // ------------------------------------------------------ FUNC_000622 chain
    // FUNC_000619: price of good `ent` (DATA_006169 entry).
    int32_t F619(uint32_t ent, int32_t e)
    {
        int32_t ebx = Mul32(RdI32(OFFSET_DATA_006165 + d8991 * 4), RdI32(ent + 4)) >> 15;
        ebx = Mul32(RdI32(OFFSET_DATA_006164 + (e & 7) * 4), ebx) >> 15;
        if (e & 8) ebx = -ebx;
        ebx += RdI32(ent);
        if (e & 0x80) ebx = Mul32(ebx, 2);
        ebx = Mul32(RdI32(OFFSET_DATA_006166 + d8992 * 4), ebx) >> 15;
        ebx = ebx > 0x7fff ? 0x7fff : Mul32(ebx, 2);
        ebx = Mul32(static_cast<int32_t>((R1530() & 0x3ff) + 0x4000 - 0x200), ebx);
        if (ebx <= 0x1fffffff) ebx = static_cast<int32_t>(static_cast<uint32_t>(ebx) << 2);
        else if (ebx <= 0x3fffffff) ebx *= 2;
        else ebx = 0x7fffffff;
        return ebx >> 16;
    }

    // FUNC_000620: stock of good `ent`.
    int32_t F620(uint32_t ent, int32_t e)
    {
        int32_t ebx = Mul32(RdI32(OFFSET_DATA_006164 + (e & 7) * 4), RdI32(ent + 0xc)) >> 15;
        if (!(e & 8)) ebx = -ebx;
        ebx += RdI32(ent + 8);
        ebx = Mul32(RdI32(OFFSET_DATA_006167 + d8991 * 4), ebx) >> 15;
        if (e & 0x80) ebx >>= 3;
        ebx = Mul32(RdI32(OFFSET_DATA_006168 + d8992 * 4), ebx) >> 15;
        const int32_t t0 = Mul32(static_cast<int32_t>((R1530() & 0x3fff) + 0x4000), ebx) >> 15;
        const int32_t t1 = Mul32(static_cast<int32_t>((R1530() & 0x3fff) + 0x4000), ebx) >> 15;
        ebx += t0 < 0 ? -t1 : t1;
        return ebx < 0 ? 0 : ebx;
    }

    // FUNC_000646
    void F646(StationRecord* r, int idx, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
              int32_t f)
    {
        if (r->advertCount >= 0x12) return;
        ++r->advertCount;
        const int32_t v[6] = {a, b, c, d, e, f};
        std::memcpy(r->advert[idx], v, sizeof(v));
    }

    // Highest i in [0, 0x20] with DATA_008989[i] & 0x80 (FUNC_000639/640), or -1.
    int IllegalGood() const
    {
        for (int i = 0x20; i >= 0; --i)
            if (d8989[i] & 0x80) return i;
        return -1;
    }

    int F636(StationRecord* r, int idx)
    {
        int32_t esi = 2;
        int32_t ebx = d9009;
        d9009 = 0;
        if (!ebx) { ebx = d9008; d9008 = 0; }
        if (!ebx) { ebx = d9007; d9007 = 0; }
        if (!ebx) {
            ebx = d9006;
            d9006 = 0;
            if (!ebx) return 0;
            esi = 1;
        }
        const uint32_t code = F857(sysCode, 1);
        if (!code) return 0;
        F646(r, idx, ebx < 0 ? 0x981e : 0x981d, 1, static_cast<int32_t>(code), esi,
             (ebx & 3) * 40 + 0x50, ebx);
        return 1;
    }

    int F637(StationRecord* r, int idx)
    {
        R1530();
        const int32_t ebx = static_cast<int32_t>(s820);
        const uint32_t code = F857(sysCode, 1);
        if (!code) return 0;
        F646(r, idx, ebx < 0 ? 0x981e : 0x981d, 1, static_cast<int32_t>(code), 0,
             (ebx & 3) * 40 + 0x50, ebx);
        return 1;
    }

    int F638(StationRecord* r, int idx)
    {
        for (int tries = 0; tries < 4; ++tries) {
            const uint32_t mode = R922(3);
            const uint32_t code = F857(sysCode, mode);
            if (!code || code == sysCode) continue;
            if (static_cast<int32_t>(F870(code).b4) < 3) continue;
            R1530();
            const uint32_t ecx = s820, eax0 = s818;
            uint32_t edx = eax0 & 7;
            const int32_t l14 = RdI32(OFFSET_DATA_006171 + edx * 8);
            int32_t esi = Mul32(static_cast<int32_t>(mode + 3), RdI32(OFFSET_DATA_006172 + edx * 8));
            if (edx == 7) edx |= 0x2000;
            uint32_t sq = static_cast<uint32_t>(Mul32(static_cast<int32_t>(eax0), static_cast<int32_t>(eax0))) >> 16;
            int32_t e = Mul32(static_cast<int32_t>(sq), static_cast<int32_t>(sq)) >> 0x13;
            e &= 0x1e00;
            edx += static_cast<uint32_t>(e);
            e = (e >> 9) & 0xf;
            esi += RdI32(OFFSET_DATA_006173 + e * 4);
            if (e >= 0xf && !(edx & 0x2000)) d9006 = static_cast<int32_t>(ecx);
            esi = Mul32(RdI32(OFFSET_DATA_006174 + ((ecx & 7) + d8992) * 4), esi);
            F646(r, idx, (ecx & 0x1000) ? 0x981b : 0x981a, l14, static_cast<int32_t>(code),
                 static_cast<int32_t>(edx), esi, static_cast<int32_t>(ecx));
            return 1;
        }
        return 0;
    }

    int F639(StationRecord* r, int idx)
    {
        if (r->flags & 1) return 0;
        r->flags |= 1;
        if (IllegalGood() < 0) return 0;
        const int32_t v = static_cast<int32_t>(Rol(static_cast<uint32_t>(PhysObjGetSeed(Slot(r->slot))), 17));
        F646(r, idx, 0x9829, 0, 0, 0, v, v);
        return 1;
    }

    int F640(StationRecord* r, int idx)
    {
        if (r->flags & 2) return 0;
        R1530();
        if (IllegalGood() < 0) return 0;
        r->flags |= 2;
        F646(r, idx, 0x9829, -1, 0, 0, static_cast<int32_t>(s818), static_cast<int32_t>(s818));
        return 1;
    }

    int F641(StationRecord* r, int idx)
    {
        int32_t ebx = static_cast<int32_t>(R922(0xc));
        for (int g = 0x20; g >= 0; --g) {
            if (r->market[g][0] != 0) continue;
            if (--ebx >= 0) continue;
            const int32_t price2 = Mul32(r->market[g][1], 2);
            if ((r->market[g][1] ^ price2) < 0) continue;
            F646(r, idx, 0x982a, g + 0x8e00, price2, 0, 0, static_cast<int32_t>(s818));
            return 1;
        }
        return 0;
    }

    int F642(StationRecord* r, int idx)
    {
        if (d8996 == 1) {
            if (r->flags & 8) return 0;
            r->flags |= 8;
            F646(r, idx, 0x9828, 0, 0, 0, 0, 0);
            return 1;
        }
        if (d8996 == 2) {
            if (r->flags & 4) return 0;
            r->flags |= 4;
            F646(r, idx, 0x9827, 0, 0, 0, 0, 0);
            return 1;
        }
        return 0;
    }

    int F643(StationRecord* r, int idx)
    {
        for (int tries = 0; tries < 4; ++tries) {
            const uint32_t mode = R922(3);
            const uint32_t code = F857(sysCode, mode);
            if (!code) continue;
            if (static_cast<int32_t>(F870(code).b4) < 3) continue;
            R1530();
            const int32_t edi = static_cast<int32_t>(s818);
            const uint32_t k = s820 & 0xf;
            R1530();
            const int32_t edx = static_cast<int32_t>(timeHi - 0x121cf7u + (s820 & 0x3f) + 7);
            const int32_t eax = Mul32(static_cast<int32_t>(mode), 1000) + RdI32(OFFSET_DATA_006214 + k * 8);
            const int32_t ecx = RdI32(OFFSET_DATA_006215 + k * 8) + 0x99b4;
            const int32_t kind = static_cast<int32_t>(((s818 >> 16) & 3) + 0x981f);
            F646(r, idx, kind, edx, static_cast<int32_t>(code), ecx, eax, edi);
            return 1;
        }
        return 0;
    }

    int F644(StationRecord* r, int idx)
    {
        R1530();
        F646(r, idx, static_cast<int32_t>(((s820 >> 16) & 3) + 0x9823), 0, 0, 0, 0,
             static_cast<int32_t>(s820));
        return 1;
    }

    int F645(StationRecord* r, int idx)
    {
        R1530();
        F646(r, idx, 0x982c, static_cast<int32_t>((s818 & 7) + 0x99d7), 0, 0,
             static_cast<int32_t>(s820), static_cast<int32_t>(s818));
        return 1;
    }

    // FUNC_000621
    void F621(StationRecord* r, int idx)
    {
        switch (kAdvertFn[R922(0x20)]) {
        case 636: F636(r, idx); break;
        case 637: F637(r, idx); break;
        case 638: F638(r, idx); break;
        case 639: F639(r, idx); break;
        case 640: F640(r, idx); break;
        case 641: F641(r, idx); break;
        case 642: F642(r, idx); break;
        case 643: F643(r, idx); break;
        case 644: F644(r, idx); break;
        case 645: F645(r, idx); break;
        default: break;
        }
    }

    // FUNC_000635: a ship model of type `type`; `w` keeps the seed word.
    int32_t F635(int32_t type, uint16_t* w)
    {
        int32_t e = type;
        if (type == 1 || type == 0) e += 2 * d8996;
        if (e < 0 || e > 0x12) return 0;
        uint32_t n = RdU32(OFFSET_DATA_006199 + static_cast<uint32_t>(e) * 8);
        if (type == 1 && d8991 < 4) n = (static_cast<uint32_t>(d8991) * n) >> 2;
        int32_t idx;
        if (type != 0x10) {
            idx = static_cast<int32_t>(R922(n));
            *w = static_cast<uint16_t>(s820);
        } else {
            idx = Mul32(*w, static_cast<int32_t>(n)) >> 16;
        }
        return RdI32(kShipTypeTable[e] + static_cast<uint32_t>(idx) * 4);
    }

    // FUNC_000634
    void F634(int32_t type, StationRecord* r)
    {
        R1530();
        if ((s820 & 8) || r->missionCount >= 0xd) return;
        uint16_t w = 0;
        const int32_t m = F635(type, &w);
        r->mission[r->missionCount][0] = static_cast<uint16_t>(m);
        r->mission[r->missionCount][1] = w;
        ++r->missionCount;
    }

    // FUNC_000624
    void F624(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
    {
        for (int i = 0; i < 25; ++i) {
            if (news[i][0] != 0) continue;
            news[i][0] = a;
            news[i][1] = b;
            news[i][2] = c;
            news[i][4] = d;
            news[i][3] = e;
            news[i][5] = f;
            news[i + 1][0] = 0;
            return;
        }
    }

    // FUNC_000625
    int32_t F625(uint32_t* code)
    {
        const uint32_t mode = R922(3);
        *code = F857(RdU32(OFFSET_DATA_006210 + d8996 * 4), mode);
        return *code ? static_cast<int32_t>(F870(*code).b4) : 0;
    }

    // FUNC_000626/627/628: distant-system news above an age threshold.
    void FNewsFar(int32_t arg, int32_t threshold, int32_t minPop, uint32_t table, int32_t base,
                  int32_t kind)
    {
        if (arg <= threshold) return;
        uint32_t code = 0;
        if (F625(&code) < minPop) return;
        const uint32_t b = R1530() & 7;
        const int32_t esi = static_cast<int32_t>(s820);
        const int32_t edx = static_cast<int32_t>((R1530() & 0x3f) + timeHi + 0xe);
        F624(kind, static_cast<int32_t>(code), edx, RdI32(table + b * 4), static_cast<int32_t>(b) + base,
             esi);
    }

    // FUNC_000632
    void F632(uint32_t n, int32_t base)
    {
        const uint32_t lc = R922(n);
        const uint32_t mode = R922(3);
        const uint32_t code = F857(sysCode, mode);
        if (!code) return;
        if (static_cast<int32_t>(F870(code).b4) < 3) return;
        R1530();
        const int32_t edi = static_cast<int32_t>(s820);
        const int32_t edx = static_cast<int32_t>(R922(0xe) + 0xa + timeHi);
        const int32_t ebx = base + static_cast<int32_t>(lc);
        const int32_t eax = Mul32(ebx + static_cast<int32_t>(mode) * 2, 50) + static_cast<int32_t>(0xffe2091au);
        F624(0x9854, static_cast<int32_t>(code), edx, eax, ebx, edi);
    }

    // FUNC_000623
    void F623()
    {
        const uint32_t k = R1530() & 0xf;
        const int32_t arg = d9002 >= d9003 ? d9002 - d9003 : 0;
        switch (kNewsFn[k]) {
        case 629: F632(8, 0x996e); break;
        case 630: if (arg > 0x10) F632(4, 0x9976); break;
        case 631: if (arg > 0x51) F632(4, 0x997a); break;
        case 626: FNewsFar(arg, 0x100, 3, OFFSET_DATA_006211, 0x99bc, 0x9857); break;
        case 627: FNewsFar(arg, 0x271, 3, OFFSET_DATA_006212, 0x9966, 0x9855); break;
        case 628: FNewsFar(arg, 0x510, 1, OFFSET_DATA_006213, 0x9966, 0x9856); break;
        default: break;
        }
    }

    // FUNC_000617/618: starport records, slots 0x72 → 1 (FUNC_000145).
    void F617()
    {
        res->stationCount = 0;
        for (int s = 0x72; s > 0; --s) {
            if (t->slotType[s] == 0) continue;
            const PhysObj* o = Slot(s);
            if (!(PhysObjGetFlags(o) & kPhysFlagDockable)) continue;
            if (res->stationCount >= kStationRecordMax) continue;
            St(res->stationCount)->slot = PhysObjGetIndex(o);
            ++res->stationCount;
        }
    }

    // FUNC_000622
    void F622(uint32_t code)
    {
        const Loc869 a = F869(code);
        d8995 = static_cast<uint16_t>(a.b5);
        d8993 = static_cast<uint16_t>(a.b6);
        d9010 = static_cast<int32_t>(a.b7);
        d8994 = static_cast<uint16_t>(a.b8);
        d8996 = static_cast<uint16_t>(a.wA >> 6);
        const Loc870 b = F870(code);
        d8991 = static_cast<uint16_t>(b.b4);
        d8992 = static_cast<int16_t>(b.bC);
        std::memcpy(d8989, d8790, sizeof(d8989));

        const int count = res->stationCount;
        for (int i = 0; i < count; ++i) {
            StationRecord* r = St(i);
            const PhysObj* sp = Slot(r->slot);
            for (int g = 0; g < 33; ++g) {
                const int32_t e = static_cast<int8_t>(d8989[g]);
                const uint32_t ent = OFFSET_DATA_006169 + static_cast<uint32_t>(g) * 16;
                r->market[g][1] = F619(ent, e);
                r->market[g][0] = F620(ent, e);
                // FUNC_000048(0x18, seed, g + 0x3e8): no listener is registered at new game.
            }
            r->advertCount = 0;
            r->flags = 0;
            for (int k = 0; k <= d8991 * 4; ++k) F621(r, r->advertCount);
            if (!(PhysObjGetFlags(sp) & kPhysFlagSurfaceStarport)) r->flags |= 0x10;
            r->missionCount = 0;
            for (int k = (res->stationCount - 1) / 4; k >= 0; --k) F634(0xd, r);
            for (int k = d8993 * 3; k >= 0; --k) F634(0, r);
            for (int k = d8994 * 3; k >= 0; --k) F634(1, r);
            // FUNC_000034(0x10, slot) / (0x10, 0): DATA_008548[0x10] ends 0; no listener.
        }
        news[0][0] = 0;
        d9001 = F871(code) * 8 + 8;
        for (int k = 0; k < d9001; ++k) F623();
    }

    // FUNC_000616 (phase 1)
    void F616()
    {
        d9006 = d9007 = d9008 = d9009 = 0;
        F617();
        F622(sysCode);
    }

    // --------------------------------------------------------- ship helpers
    // FUNC_000927 with list byte 0x4f; the new slot's PhysObj, or null.
    PhysObj* F927(const uint8_t* tmpl, int32_t model)
    {
        int s = 0x72;
        while (s > 0 && t->slotType[s] != 0) --s;
        if (s < 0x14 || s <= 0) {
            // Evict the highest list byte with bit 0x20 (none at new game).
            for (s = 0x72; s > 0; --s) {
                if (!(t->slotType[s] & 0x20)) continue;
                if (s < 0x14) return nullptr;
                break;
            }
            if (s <= 0) return nullptr;
        }
        t->slotType[s] = 0x4f;
        PhysObj* o = Slot(s);
        FUNC_001533(reinterpret_cast<intptr_t>(o->raw), reinterpret_cast<intptr_t>(tmpl), model);
        // FUNC_001533 stamps [DATA_008804]/[DATA_008807], the new-game clock.
        PhysObjSetI32(o, 0xa8, static_cast<int32_t>(timeLo));
        PhysObjSetI32(o, 0xac, static_cast<int32_t>(timeHi));
        PhysObjSetModel(o, model);
        PhysObjSetIndex(o, static_cast<uint8_t>(s));
        ++res->allocated;
        return o;
    }

    // FUNC_000940
    void F940(PhysObj* o)
    {
        t->slotType[PhysObjGetIndex(o)] = 0;
        ++res->freed;
    }

    // FUNC_000791 type 0x12 (FUNC_000795): registration "XX-999".
    static void Registration(uint32_t seed, char* out)
    {
        uint32_t local = Rol(seed, 16);
        seed += local;
        local = Rol(local, 7);
        seed += local;
        local = Rol(local, 4);
        seed += local;
        auto pick = [&](uint32_t n) {  // FUNC_000921
            seed += local;
            local = Rol(local, 5);
            seed += local;
            seed = Rol(seed, 16);
            return ((seed & 0xffff) * n) >> 16;
        };
        out[0] = static_cast<char>(pick(0x1a) + 'A');
        out[1] = static_cast<char>(pick(0x1a) + 'A');
        out[2] = '-';
        out[3] = static_cast<char>(pick(10) + '0');
        out[4] = static_cast<char>(pick(10) + '0');
        out[5] = static_cast<char>(pick(10) + '0');
        out[6] = 0;
    }

    // FUNC_000703
    void F703(PhysObj* o, int32_t nameType)
    {
        R1530();
        PhysObjSetSeed(o, static_cast<int32_t>(s818));
        if (nameType == 0x12) {
            Registration(s818, reinterpret_cast<char*>(o->raw + 0x124));
        } else {
            std::fprintf(stderr, "traffic_spawn: FUNC_000791 name type %#x not ported\n", nameType);
        }
    }

    // FUNC_000774: the ship's drive class; takes its mass from *l8.
    uint8_t F774(const PhysObj* o, int32_t* l8)
    {
        const uint32_t r = R922(0x33);
        const int32_t type = static_cast<int8_t>(o->raw[0x118]);
        const uint32_t edx = static_cast<uint32_t>(
            Mul32(static_cast<int32_t>(r + RdU32(OFFSET_DATA_006264 + type * 4)), *l8)) >> 8;
        int i = 1;
        while (i < 9 && !(edx < RdU32(OFFSET_DATA_006265 + i * 8 + 4))) ++i;
        --i;
        *l8 -= RdI32(OFFSET_DATA_006265 + i * 8 + 4);
        return RdU8(OFFSET_DATA_006265 + i * 8);
    }

    // FUNC_000773: new ship of `type` from template `tmpl`.
    PhysObj* F773(const uint8_t* tmpl, int32_t type, int32_t model)
    {
        PhysObj* o = F927(tmpl, model);
        if (!o) return nullptr;
        o->raw[0x87] = 0xb;
        o->raw[0x118] = static_cast<uint8_t>(type);
        F703(o, static_cast<int8_t>(RdU8(OFFSET_DATA_006267 + static_cast<uint32_t>(type))));
        const uint32_t specs = SpecsOffset(PhysObjGetModel(o));
        uint16_t la = static_cast<uint16_t>(R1530());
        if (o->raw[0x118] == 0xe) {
            PhysObjSetSeed(o, 0);
            la = static_cast<uint16_t>(d8995 << 8);
        }
        if (o->raw[0x118] == 0x10) la = 0xffff;
        int32_t l8 = PhysObjGetU16(o, 0x116);
        uint32_t edi = 0;
        uint32_t edx = R1530();
        int32_t eax = RdI16(specs + 0x14);
        if (o->raw[0x118] == 0xc) edx = 0xffff;
        if (edx > 0xf000 && o->raw[0x118] != 1) {
            --l8;
            edi |= 0x100;
        }
        if (eax >= 0) {
            if (edx > 0xc000) eax >>= 8;
            eax &= 0xf;
            o->raw[0xd0] = static_cast<uint8_t>(eax);
            l8 -= RdI32(OFFSET_DATA_006261 + static_cast<uint32_t>(eax) * 4);
        }
        const int32_t drive = static_cast<int8_t>(o->raw[0xd0]);
        PhysObjSetI32(o, 0x11e, static_cast<int32_t>(RdU32(OFFSET_DATA_006266 + drive * 4) << 24));
        uint32_t ebx = RdU32(OFFSET_DATA_006262 + drive * 4);
        if (!(ebx < static_cast<uint32_t>(l8))) ebx = static_cast<uint32_t>(l8);
        l8 -= static_cast<int32_t>(ebx);
        o->raw[0x119] = static_cast<uint8_t>(ebx);
        o->raw[0xd2] = F774(o, &l8);
        ebx = static_cast<uint32_t>(l8);
        if (ebx > 0x12c) ebx = 0x12c;
        if (o->raw[0x118] == 1) ebx /= 4;
        ebx /= 4;
        ebx = static_cast<uint32_t>(Mul32(la, static_cast<int32_t>(ebx))) / 0xffffu;
        ebx = R922(ebx);
        ebx = R922(ebx) * 2;
        l8 -= static_cast<int32_t>(ebx);
        PhysObjSetU16(o, 0xe2, static_cast<uint16_t>(ebx << 4));
        if (l8 >= 2) {
            const uint32_t r = R922(la);
            if (r >= 0xc000) {
                edi |= 0x80;
                l8 -= 2;
            } else if (r >= 0x4000) {
                edi |= 8;
                l8 -= 2;
            }
        }
        if (l8 > 0) {
            int32_t l10 = static_cast<int32_t>(R922(static_cast<uint32_t>(RdI16(specs + 0x12) + 1)));
            if (l10 >= 1) {
                const uint8_t bl = RdU8(OFFSET_DATA_006260 + (la >> 12));
                while (l8 > 0 && l10 != 0) {
                    o->raw[0xd6 + l10] = bl;
                    --l8;
                    --l10;
                }
            }
        }
        if (l8 > 0) {
            if (R922(la) >= 0x800) {
                --l8;
                edi |= 4;
            }
            if (R922(la) >= 0xfc00 && l8 >= 4 && o->raw[0x118] != 0xe) {
                l8 -= 4;
                edi |= 0x400;
            }
        }
        edi |= 0x4000;
        ebx = static_cast<uint32_t>(
            Mul32(RdI32(OFFSET_DATA_006263 + static_cast<int8_t>(o->raw[0x118]) * 4), l8));
        if (ebx > 0xaf) ebx = 0xaf;
        l8 -= static_cast<int32_t>(ebx);
        o->raw[0x119] = static_cast<uint8_t>(o->raw[0x119] + ebx);
        PhysObjSetU16(o, 0x116, static_cast<uint16_t>(l8));
        PhysObjSetI32(o, 0xc8,
                      static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(edi) >> 16) | (edi << 16)));
        return o;
    }

    // FUNC_000772 (FUNC_000658 → 635 picks the model)
    PhysObj* F772(int32_t type)
    {
        uint16_t w = 0;
        const int32_t model = F635(type, &w);
        return F773(Blob() + OFFSET_DATA_006259, type, model);
    }

    // FUNC_000697
    void F697(PhysObj* o, int32_t arg)
    {
        const uint32_t k = (R1530() & 0xf) + static_cast<uint32_t>(arg);
        PhysObjSetI32(o, 0x11a, RdI32(OFFSET_DATA_006268 + k * 4));
        o->raw[0x118] = 0xfb;
    }

    // FUNC_000702
    void F702(PhysObj* o, uint32_t radius)
    {
        R1530();
        FUNC_001507(reinterpret_cast<intptr_t>(o->raw), radius, s818, s820);
        PhysObjSetU16(o, 0xb4, 0);
        PhysObjSetU16(o, 0xb6, 0);
        PhysObjSetU16(o, 0xb8, 0);
    }

    // FUNC_000700: a random starport; its record in *rec.
    PhysObj* F700(StationRecord** rec)
    {
        if (res->stationCount == 0) return nullptr;
        *rec = St(static_cast<int>(R922(static_cast<uint32_t>(res->stationCount))));
        return Slot((*rec)->slot);
    }

    // FUNC_000701: a random orbital starport record.
    bool F701(StationRecord** rec)
    {
        if (d9011 == 0 || res->stationCount == 0) return false;
        int32_t edx = static_cast<int32_t>(R922(static_cast<uint32_t>(d9011)));
        int i = 0;
        for (; i < res->stationCount; ++i) {
            if (!(St(i)->flags & 0x10)) continue;
            if (edx-- <= 0) break;
        }
        if (edx >= 0 || !(St(i)->flags & 0x10)) return false;
        *rec = St(i);
        return true;
    }

    // FUNC_000903 mode 2 for a non-player ship: fuel for `dist`, or -1.
    int32_t F903(PhysObj* o, int32_t dist, int32_t* range, int32_t* travel)
    {
        const int32_t mass = RdI16(SpecsOffset(PhysObjGetModel(o)) + 6);
        const uint32_t drv = OFFSET_DATA_007187 + static_cast<uint32_t>(static_cast<int8_t>(o->raw[0xd0]) * 16);
        if (mass == 0) return -1;
        *range = RdI32(drv) / mass;
        if (*range < dist || dist == 0) return -1;
        int32_t fuel = Mul32(dist, mass) / 0x7531 + 1;
        fuel -= Mul1521(fuel, static_cast<int32_t>(RdU32(drv + 4) << 15));
        if (fuel < 1) fuel = 1;
        *travel = static_cast<int32_t>(((static_cast<uint32_t>(dist) << 16) - 1) /
                                       static_cast<uint32_t>(*range) * 7);
        if (fuel > static_cast<int32_t>(o->raw[0x119])) return -1;
        o->raw[0x119] = static_cast<uint8_t>(o->raw[0x119] - fuel);
        return dist;
    }

    // FUNC_000776: origin system for an inbound ship (0 = none).
    uint32_t F776(PhysObj* o, uint32_t mode)
    {
        const uint32_t code = F857(sysCode, mode);
        if (!code) return 0;
        PhysObjSetI32(o, 0xf2, static_cast<int32_t>(code));
        if (static_cast<int32_t>(F870(code).b4) <= 3) return 0;
        const int32_t dist = F852(code);
        int32_t range = 0, travel = 0;
        if (F903(o, dist, &range, &travel) < 0) return 0;
        PhysObjSetU16(o, 0xf6, static_cast<uint16_t>(range));
        PhysObjSetI32(o, 0xfa, travel);
        return code;
    }

    // FUNC_000775
    uint32_t F775(PhysObj* o)
    {
        for (uint32_t mode : {1u, 1u, 0u, 0u})
            if (const uint32_t c = F776(o, mode)) return c;
        return 0;
    }

    // FUNC_000689 (type 1) / FUNC_000690 (type 0x11): inbound from hyperspace.
    void F689(int32_t type)
    {
        PhysObj* o = F772(type);
        if (!o) return;
        F702(o, kArrivalRadius);
        o->raw[0xff] = 0x11;
        PhysObjSetU16(o, 0xf8, 8);
        StationRecord* rec = nullptr;
        if (!F700(&rec)) {
            F940(o);
            return;
        }
        o->raw[0x100] = rec->slot;
        R1530();
        uint32_t lo = s818 << 2, hi = s818 >> 30;
        if ((timeLo & 0x80000000u) && (lo & 0x80000000u)) ++hi;
        PhysObjSetI32(o, 0xa8, static_cast<int32_t>(lo + timeLo));
        PhysObjSetI32(o, 0xac, static_cast<int32_t>(hi + timeHi));
        if (!F775(o)) {
            F940(o);
            return;
        }
        t->slotType[PhysObjGetIndex(o)] = 2;
    }

    // FUNC_000691: docked at a random starport.
    void F691()
    {
        PhysObj* o = F772(1);
        if (!o) return;
        StationRecord* rec = nullptr;
        PhysObj* sp = F700(&rec);
        if (!sp) {
            F940(o);
            return;
        }
        if (PhysObjGetFlags(sp) & kPhysFlagSurfaceStarport) {
            std::memcpy(o->raw + 0x3e, sp->raw + 0x3e, 24);
            std::memcpy(o->raw, sp->raw, 36);
            PhysObjSetParent(o, PhysObjGetParent(sp));
        } else {
            PhysObjSetParent(o, PhysObjGetIndex(sp));
        }
        o->raw[0x57] = 1;
        int64_t off[3] = {};
        int pad = -1;
        if (PlayerPadClaim(sp, static_cast<int8_t>(PhysObjGetIndex(o)), playerSlot, off, &pad) != 0) {
            F940(o);
            return;
        }
        for (int a = 0; a < 3; ++a) SetI64(o, 0x3e + a * 8, I64(o, 0x3e + a * 8) + off[a]);
        o->raw[0xff] = 0x24;
        o->raw[0x100] = 0;
        o->raw[0xfe] = PhysObjGetIndex(sp);
        R1530();
        uint32_t lo = s818 << 4, hi = s818 >> 28;
        if ((timeLo & 0x80000000u) && (lo & 0x80000000u)) ++hi;
        PhysObjSetI32(o, 0xa8, static_cast<int32_t>(lo + timeLo));
        PhysObjSetI32(o, 0xac, static_cast<int32_t>(hi + timeHi));
        // FUNC_000671: lift by the up row × the pad clearance (+0x140).
        const int32_t clear = PhysObjGetI32(o, 0x140);
        for (int a = 0; a < 3; ++a)
            SetI64(o, 0x3e + a * 8, I64(o, 0x3e + a * 8) + Mul1521(PhysObjGetI32(o, 0xc + a * 4), clear));
        t->slotType[PhysObjGetIndex(o)] = (PhysObjGetFlags(sp) & kPhysFlagOpenAir) ? 0x4e : 2;
    }

    // FUNC_000699: near a random orbital starport.
    void F699()
    {
        StationRecord* rec = nullptr;
        if (!F701(&rec)) return;
        PhysObj* o = F772(0xf);
        if (!o) return;
        o->raw[0x101] = 0xff;
        F702(o, kStationRadius);
        o->raw[0xff] = 0xd;
        PhysObjSetParent(o, rec->slot);
    }

    // FUNC_000694: a lone pirate after `target`.
    void F694(uint8_t target)
    {
        PhysObj* o = F772(0xa);
        if (!o) return;
        o->raw[0xfe] = target;
        F702(o, kArrivalRadius);
        o->raw[0xff] = 5;
        F697(o, 4);
    }

    // FUNC_000695 / 000696: a pirate leader and up to three wingmen at
    // DATA_006270 / 6271 offsets; 695's wingmen fly the leader's model, 696's
    // pick theirs (FUNC_000658).
    void FPirateGroup(uint8_t target, uint32_t offsetTable, bool sameModel)
    {
        int64_t vec[3];
        std::memcpy(vec, Blob() + offsetTable, sizeof(vec));
        PhysObj* lead = F772(sameModel ? 0xb : 0xa);
        if (!lead) return;
        lead->raw[0xfe] = target;
        F702(lead, kArrivalRadius);
        lead->raw[0xff] = 5;
        F697(lead, sameModel ? 2 : 4);
        struct Wing {
            uint16_t w102, w106;
            int steps;  // +1: add vec once; -n: subtract vec n times
        };
        static constexpr Wing kWing[3] = {{0x1f4, 0xfe0c, 1}, {0xfe0c, 0xfe0c, -2}, {0, 0xfc18, -3}};
        for (int k = 0; k < 3; ++k) {
            if (k > 0 && (R1530() & 0x4000)) return;
            int32_t model = PhysObjGetModel(lead);
            if (!sameModel) {
                uint16_t w = 0;
                model = F635(0xb, &w);
            }
            PhysObj* o = F773(lead->raw, 0xb, model);
            if (!o) return;
            o->raw[0xff] = 0xb;
            o->raw[0xfe] = PhysObjGetIndex(lead);
            o->raw[0x100] = target;
            ++o->raw[0x101];
            PhysObjSetU16(o, 0x102, kWing[k].w102);
            PhysObjSetU16(o, 0x106, kWing[k].w106);
            PhysObjSetU16(o, 0x104, 0);
            const int n = kWing[k].steps < 0 ? -kWing[k].steps : kWing[k].steps;
            const int64_t sign = kWing[k].steps < 0 ? -1 : 1;
            for (int s = 0; s < n; ++s)
                for (int a = 0; a < 3; ++a)
                    SetI64(o, 0x3e + a * 8,
                           static_cast<int64_t>(static_cast<uint64_t>(I64(o, 0x3e + a * 8)) +
                                                static_cast<uint64_t>(sign * vec[a])));
            F697(o, 0);
        }
    }

    // FUNC_000698
    void F698()
    {
        const uint8_t target = static_cast<uint8_t>(playerSlot);
        switch (R922(4)) {
        case 0: F694(target); break;
        case 1: FPirateGroup(target, OFFSET_DATA_006270, true); break;
        case 2: FPirateGroup(target, OFFSET_DATA_006271, false); break;
        default: break;  // FUNC_000693: empty
        }
    }

    // FUNC_000688 (FUNC_000687 has just cleared DATA_009046 bit 0).
    void F688()
    {
        const Loc869 l = F869(sysCode);
        for (uint32_t n = R922(l.b8); n > 0; --n) F689(1);
        for (uint32_t n = R922(static_cast<uint32_t>(d8548_16)); n > 0; --n) F689(0x11);
        bool noDocked = false;
        if (res->stationCount != 0) {
            uint32_t k = R922(static_cast<uint32_t>(res->stationCount) * 4);
            if (k > 0x19) k = 0x19;
            if (k == 0) noDocked = true;
            for (; k > 0; --k) F691();
        }
        if (!noDocked)
            for (uint32_t n = R922(l.b8); n > 0; --n) F699();
        for (uint32_t n = R922(l.b6 << 2); n > 0; --n) F698();
        R922(l.b6);  // FUNC_000692 × n: a bare `ret`
    }
};

}  // namespace

bool TrafficSpawnNewGame(SpawnSlotTable* table, const SysGenStartInfo& info, int playerSlot,
                         TrafficSpawnResult* out)
{
    if (!table || !out || !Blob() || playerSlot <= 0 || playerSlot >= kSpawnSlotCount) return false;
    *out = TrafficSpawnResult{};
    static Ctx c;
    c = Ctx{};
    c.t = table;
    c.res = out;
    c.timeLo = info.timeLo;
    c.timeHi = info.timeHi;
    c.sysCode = info.params.systemcode;
    std::memcpy(c.galPos, info.galPos, sizeof(c.galPos));
    c.playerSlot = playerSlot;
    c.d9011 = info.orbitalStations;
    const char* tr = std::getenv("JJFFE_TRAFFIC_TRACE");
    c.trace = tr && tr[0] == '1';

    c.F616();
    out->rngAt688[0] = c.s818;
    out->rngAt688[1] = c.s820;
    c.F688();
    out->rngAfter[0] = c.s818;
    out->rngAfter[1] = c.s820;
    return true;
}

}  // namespace jjffe
