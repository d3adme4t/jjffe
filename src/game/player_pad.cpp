// Task 4.20 — the landed player on the starport pad (ffe.asm FUNC_000998 default
// start → FUNC_000996 / 000994 / 000650 / 000997, and FUNC_001677).
#include "jjffe/player_pad.h"

#include "data_blob.h"
#include "jjffe/data.h"
#include "jjffe/data_loader.h"

#include <cstring>
#include <initializer_list>

namespace jjffe {

void FUNC_001533(intptr_t dst, intptr_t src, int32_t param_3);
int FUNC_001658_Vec64Truncate(uint32_t* param_1, int param_2);
void FUNC_001517(uint32_t* param_1, int32_t* param_2, int32_t param_3);

namespace {

const uint8_t* Blob() { return data::GetLoadedBlob(); }

int32_t RdI32(uint32_t off)
{
    int32_t v;
    std::memcpy(&v, Blob() + off, sizeof(v));
    return v;
}

int32_t I32(const PhysObj* o, int off)
{
    int32_t v;
    std::memcpy(&v, o->raw + off, sizeof(v));
    return v;
}

// FUNC_001521: high dword of 2·a·b.
int32_t Mul1521(int32_t a, int32_t b)
{
    return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 31);
}

// FUNC_001669_VecMatMul: out = Mᵀ·v (alias-safe, as the ASM builds locals).
void VecMatMul(int32_t out[3], const int32_t v[3], const int32_t m[9])
{
    int32_t r[3];
    for (int c = 0; c < 3; ++c)
        r[c] = static_cast<int32_t>(static_cast<uint32_t>(Mul1521(v[0], m[c])) +
                                    static_cast<uint32_t>(Mul1521(v[1], m[3 + c])) +
                                    static_cast<uint32_t>(Mul1521(v[2], m[6 + c])));
    std::memcpy(out, r, sizeof(r));
}

void Matrix(const PhysObj* o, int32_t m[9]) { std::memcpy(m, o->raw, 9 * sizeof(int32_t)); }

// FUNC_000650's open-air pad tables (ffe.asm 2c884-2c8c5): model − 0x51.
uint32_t PadTable(int32_t model)
{
    const uint32_t d = static_cast<uint32_t>(model - 0x51);
    if (d < 2) return data::OFFSET_DATA_006178;
    if (d == 4) return data::OFFSET_DATA_006177;
    if (d == 5) return data::OFFSET_DATA_006175;
    if (d == 8) return data::OFFSET_DATA_006176;
    return data::OFFSET_DATA_006179;
}

}  // namespace

int PlayerPadClaim(PhysObj* starport, int shipIndex, int playerIndex, int64_t offset[3],
                   int* padOut)
{
    // Player landing: FUNC_000034(0x17, starport) broadcasts through
    // FUNC_000048 and FUNC_000035(0x17) reads back DATA_008548[0x17]; a module
    // refuses by zeroing it. No module handler is ported, so the value read
    // back is the starport index (non-zero), as on the ffeasm cold boot.
    if (shipIndex == playerIndex && PhysObjGetIndex(starport) == 0) return -1;

    int edx = static_cast<int16_t>(PhysObjGetU16(starport, 0xce));
    while (edx >= 0) {
        uint8_t* cell = starport->raw + 0xd0 + edx;
        const int c = static_cast<int8_t>(*cell);
        if (c == shipIndex) break;
        if (c == 0) {
            *cell = static_cast<uint8_t>(shipIndex);
            break;
        }
        --edx;
    }
    if (edx < 0) return -1;
    if (padOut) *padOut = edx;

    int32_t v[3] = {0, 0, -1};
    if (PhysObjGetFlags(starport) & kPhysFlagOpenAir) {
        const uint32_t t = PadTable(PhysObjGetModel(starport)) + static_cast<uint32_t>(edx) * 12;
        const int32_t pad[3] = {RdI32(t), RdI32(t + 4), RdI32(t + 8)};
        int32_t m[9];
        Matrix(starport, m);
        VecMatMul(v, pad, m);
        for (int32_t& c : v) c = static_cast<int32_t>(static_cast<uint32_t>(c) << 10);
    }
    for (int a = 0; a < 3; ++a) offset[a] = v[a];
    return 0;
}

bool PlayerPadSpawnDefault(SpawnSlotTable* table, PlayerPadResult* out)
{
    if (!table || !Blob()) return false;

    // FUNC_000996 template (ffe.asm 73953-73990): DATA_007265 matrix, model,
    // +0x9e/+0x98/+0x9a/+0xa0 and the DATA_007337 name.
    PhysObj tmpl;
    std::memset(tmpl.raw, 0, sizeof(tmpl.raw));
    std::memcpy(tmpl.raw, Blob() + data::OFFSET_DATA_007265, 9 * sizeof(int32_t));
    PhysObjSetModel(&tmpl, kStartPlayerModel);
    PhysObjSetU16(&tmpl, 0x9e, 0xffff);
    PhysObjSetU16(&tmpl, 0x98, 0x8000);
    PhysObjSetU16(&tmpl, 0x9a, 0x8000);
    PhysObjSetI32(&tmpl, 0xa0, 0x789a3456);
    std::memcpy(tmpl.raw + 0x124, Blob() + data::OFFSET_DATA_007337, 9);

    // FUNC_000928 (list byte 0x4f): highest free slot 0x72..1, then FUNC_001533.
    int slot = kSpawnSlotCount - 1;
    while (slot > 0 && table->slotType[slot] != 0) --slot;
    if (slot <= 0) return false;
    table->slotType[slot] = 0x4f;
    PhysObj* pl = &table->slots[slot];
    FUNC_001533(reinterpret_cast<intptr_t>(pl->raw), reinterpret_cast<intptr_t>(tmpl.raw),
                kStartPlayerModel);
    PhysObjSetIndex(pl, static_cast<uint8_t>(slot));
    const int playerIndex = slot;  // DATA_008857

    // FUNC_000996 tail (74089-74160).
    PhysObjSetU8(pl, 0x118, 0xff);
    PhysObjSetU8(pl, 0x25, 0);
    PhysObjSetU8(pl, 0x88, 0);
    PhysObjSetU16(pl, 0x116, 0);
    PhysObjSetControl(pl, static_cast<uint8_t>(PhysObjControl::kPlayer));
    PhysObjSetSeed(pl, static_cast<int32_t>(kStartPlayerSeed * 0x4a0du));

    // JUMP_004144 (74412-74447).
    PhysObjSetU8(pl, 0xd2, 0x88);
    PhysObjSetU8(pl, 0xd0, 0x2);
    PhysObjSetI32(pl, 0xc8, 0x40240020);
    PhysObjSetU8(pl, 0xd6, 0x82);
    PhysObjSetU8(pl, 0xd7, 0x82);
    PhysObjSetI32(pl, 0x11e, 0x20000000);
    PhysObjSetParent(pl, kStartPlayerParentSlot);

    // FUNC_000994(player, 0x6a) (73770-73880).
    PhysObj* sp = &table->slots[kStartStarportSlot];
    std::memcpy(pl->raw, sp->raw, 9 * sizeof(int32_t));
    std::memcpy(pl->raw + 0x3e, sp->raw + 0x3e, 24);
    PhysObjSetU8(pl, 0x57, 1);
    PhysObjSetU8(sp, 0xd1, 0xff);
    int64_t off[3] = {};
    int pad = -1;
    const int claim = PlayerPadClaim(sp, PhysObjGetIndex(pl), playerIndex, off, &pad);
    PhysObjSetU8(sp, 0xd1, 0);
    if (claim != 0) return false;
    int64_t pos[3];
    std::memcpy(pos, pl->raw + 0x3e, sizeof(pos));
    const int32_t clearance = I32(pl, 0x140);
    for (int a = 0; a < 3; ++a) {
        pos[a] += off[a];
        pos[a] += Mul1521(I32(pl, 0xc + a * 4), clearance);  // FUNC_001334_Int64Add32
    }
    std::memcpy(pl->raw + 0x3e, pos, sizeof(pos));

    // FUNC_000997: negate rows 0 and 2.
    int32_t m[9];
    Matrix(pl, m);
    for (int i : {0, 1, 2, 6, 7, 8}) m[i] = static_cast<int32_t>(0u - static_cast<uint32_t>(m[i]));
    std::memcpy(pl->raw, m, sizeof(m));

    if (out) {
        out->slot = slot;
        out->starportSlot = kStartStarportSlot;
        out->pad = pad;
        std::memcpy(out->padOffset, off, sizeof(off));
    }
    return true;
}

void PhysObjGlobalPos(const SpawnSlotTable& table, int slot, int64_t out[3])
{
    const PhysObj* o = &table.slots[slot];
    const int parent = PhysObjGetParent(o);
    int64_t rel[3];
    std::memcpy(rel, o->raw + 0x3e, sizeof(rel));
    if (parent == 0) {
        std::memcpy(out, rel, sizeof(rel));
        return;
    }
    PhysObjGlobalPos(table, parent, out);
    if (PhysObjGetU8(o, 0x57) == 0) {
        for (int a = 0; a < 3; ++a) out[a] += rel[a];
        return;
    }
    // Truncate to 28 bits, rotate by the parent matrix, expand back.
    uint32_t t[6];
    std::memcpy(t, rel, sizeof(t));
    const int sh = FUNC_001658_Vec64Truncate(t, 0x1c);
    const int32_t v[3] = {static_cast<int32_t>(t[0]), static_cast<int32_t>(t[2]),
                          static_cast<int32_t>(t[4])};
    int32_t m[9];
    Matrix(&table.slots[parent], m);
    int32_t r[3];
    VecMatMul(r, v, m);
    uint32_t e[6];
    FUNC_001517(e, r, sh);
    int64_t d[3];
    std::memcpy(d, e, sizeof(d));
    for (int a = 0; a < 3; ++a) out[a] += d[a];
}

}  // namespace jjffe
