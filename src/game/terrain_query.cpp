// FUNC_000574 terrain-height query chain (ffe.asm 36511-39135), LP64-safe.
#include "jjffe/terrain_query.h"

#include "jjffe/planet_render.h"
#include "jjffe/ship_model.h"

#include <cstring>

namespace jjffe {

uint32_t FUNC_001466(uint32_t* param_1);
uint32_t FUNC_001521(int32_t param_1, int32_t param_2);
uint32_t FUNC_001523(uint32_t param_1, uint32_t param_2);
int FUNC_001656_FindMSB(uint32_t param_1);
int FUNC_001658_Vec64Truncate(uint32_t* param_1, int param_2);
int16_t* FUNC_001822(int16_t* param_1);
int16_t* PlanetTerrainCollision876(int* heightOut, int limit, intptr_t planetObj, int16_t* prog,
                                   int* origin, int* basis, int mode);
bool PlanetMeshTablesReady();

namespace {

// DATA_008696..DATA_008707 (ffedat.asm): collision query results.
struct QueryState {
    uint8_t flags696 = 0;          // DATA_008696
    uint8_t flags697 = 0;          // DATA_008697
    uint8_t hitIndex698 = 0;       // DATA_008698
    uint8_t d700 = 0;              // DATA_008700
    uint8_t hitCmd701 = 0;         // DATA_008701
    PhysObj* hitObj702 = nullptr;  // DATA_008702
    uint32_t alt703 = 0;           // DATA_008703
    uint32_t alt704 = 0;           // DATA_008704
    uint32_t alt705 = 0;           // DATA_008705
    uint32_t alt706 = 0;           // DATA_008706
    uint8_t mode707 = 0;           // DATA_008707
};
QueryState s_q;
uint32_t s_unported = 0;

// FUNC_000579 stack block at ebp-0x1c, handed to FUNC_000581/582/583.
struct CollisionCtx {
    int32_t v[3];            // +0x00 query point, model frame
    const uint8_t* normals;  // +0x0c model +0x0c
    const uint8_t* verts;    // +0x10 model +0x04
    int32_t scale;           // +0x14 model +0x14 + model +0x18
    int32_t shift;           // +0x18
};

int64_t GetI64(const PhysObj* o, int off)
{
    int64_t v;
    std::memcpy(&v, o->raw + off, sizeof(v));
    return v;
}

int32_t GetI32(const PhysObj* o, int off)
{
    int32_t v;
    std::memcpy(&v, o->raw + off, sizeof(v));
    return v;
}

// FUNC_001341_Int64ArithShift: positive = left, negative = arithmetic right.
int64_t ArithShift64(int64_t v, int s)
{
    if (s == 0) return v;
    if (s > 0) return s >= 64 ? 0 : static_cast<int64_t>(static_cast<uint64_t>(v) << s);
    return -s >= 64 ? (v < 0 ? -1 : 0) : (v >> -s);
}

int32_t Sar(int32_t v, int s) { return v >> (s & 31); }
int32_t Shl(int32_t v, int s) { return static_cast<int32_t>(static_cast<uint32_t>(v) << (s & 31)); }

int32_t Len3(const int32_t v[3])
{
    uint32_t t[3] = {static_cast<uint32_t>(v[0]), static_cast<uint32_t>(v[1]),
                     static_cast<uint32_t>(v[2])};
    return static_cast<int32_t>(FUNC_001466(t));
}

// FUNC_001669_VecMatMul; the ASM builds the result in locals, so dst may alias src.
void VecMatMul(int32_t dst[3], const int32_t src[3], const int32_t m[9])
{
    const int32_t s[3] = {src[0], src[1], src[2]};
    for (int c = 0; c < 3; ++c) {
        uint32_t acc = FUNC_001521(s[0], m[c]);
        acc += FUNC_001521(s[1], m[3 + c]);
        acc += FUNC_001521(s[2], m[6 + c]);
        dst[c] = static_cast<int32_t>(acc);
    }
}

// FUNC_001670_VecMatTMul (alias-safe like the ASM).
void VecMatTMul(int32_t dst[3], const int32_t src[3], const int32_t m[9])
{
    const int32_t s[3] = {src[0], src[1], src[2]};
    for (int r = 0; r < 3; ++r) {
        uint32_t acc = FUNC_001521(s[0], m[r * 3]);
        acc += FUNC_001521(s[1], m[r * 3 + 1]);
        acc += FUNC_001521(s[2], m[r * 3 + 2]);
        dst[r] = static_cast<int32_t>(acc);
    }
}

const ShipModel* ModelOf(const PhysObj* o) { return GetShipModel(PhysObjGetModel(o)); }

const uint8_t* ModelPtr(uint32_t off) { return off ? GetShipModelPointer(off) : nullptr; }

// FUNC_000581 vertex fetch: record (idx>>1)*6, x/y/z = signed bytes 2..4,
// x negated for odd (mirrored) indices.
void FetchVertex(const CollisionCtx& c, int idx, int32_t out[3])
{
    const uint8_t* r = c.verts + (idx >> 1) * 6;
    const int32_t x = static_cast<int8_t>(r[2]);
    out[0] = (idx & 1) ? -x : x;
    out[1] = static_cast<int8_t>(r[3]);
    out[2] = static_cast<int8_t>(r[4]);
}

// FUNC_001523 ratio with the ASM's sign handling and 0x7fffffff saturation.
int32_t Ratio(int32_t v, int32_t len)
{
    if (v < 0) {
        int32_t r = static_cast<int32_t>(FUNC_001523(0u - static_cast<uint32_t>(v),
                                                     static_cast<uint32_t>(len)));
        if (r < 0) r = 0x7fffffff;
        return -r;
    }
    int32_t r = static_cast<int32_t>(FUNC_001523(static_cast<uint32_t>(v), static_cast<uint32_t>(len)));
    if (r < 0) r = 0x7fffffff;
    return r;
}

} // namespace

// FUNC_000581 (ffe.asm 38116-38697): terrain command. p points at the four
// vertex bytes + mode word; returns the program pointer past the sat records.
static const uint8_t* FUNC_000581(int32_t* out, const uint8_t* p, PhysObj* parent, int32_t radius,
                                  const CollisionCtx& c)
{
    const int mode = p[4] | (p[5] << 8);
    s_q.mode707 = 0xff;
    int32_t v0[3], e[9];
    FetchVertex(c, p[0], v0);
    FetchVertex(c, p[1], e);
    FetchVertex(c, p[2], e + 3);
    FetchVertex(c, p[3], e + 6);
    const int32_t sc = c.scale - c.shift;
    for (int a = 0; a < 3; ++a) v0[a] = sc < 0 ? Sar(v0[a], -sc) : Shl(v0[a], sc);
    for (int a = 0; a < 9; ++a) e[a] = sc < 0 ? Sar(e[a], -sc) : Shl(e[a], sc);
    for (int k = 0; k < 3; ++k)
        for (int a = 0; a < 3; ++a)
            e[k * 3 + a] = static_cast<int32_t>(static_cast<uint32_t>(e[k * 3 + a]) -
                                                static_cast<uint32_t>(v0[a]));
    int32_t l30[3];
    for (int a = 0; a < 3; ++a)
        l30[a] = static_cast<int32_t>(static_cast<uint32_t>(v0[a]) - static_cast<uint32_t>(c.v[a]));
    const int32_t len = Len3(l30);
    int16_t* const next = reinterpret_cast<int16_t*>(const_cast<uint8_t*>(p + 6));

    const int32_t r138 = static_cast<int32_t>(ArithShift64(GetI64(parent, 0x138), -c.shift));
    if (static_cast<int32_t>(static_cast<uint32_t>(len) - static_cast<uint32_t>(r138)) >= radius) {
        *out = -1;
        s_q.mode707 = 0;
        return reinterpret_cast<const uint8_t*>(FUNC_001822(next));
    }
    const int32_t r140 = static_cast<int32_t>(ArithShift64(GetI64(parent, 0x140), -c.shift));
    if (static_cast<int32_t>(static_cast<uint32_t>(len) - static_cast<uint32_t>(r140)) <= radius) {
        *out = 0;
        s_q.mode707 = 1;
        return reinterpret_cast<const uint8_t*>(FUNC_001822(next));
    }

    // Basis turning the query→body-origin vector onto +z (ASM 38397-38589).
    int32_t m[9] = {0x7fffffff, 0, 0, 0, 0x7fffffff, 0, 0, 0, 0x7fffffff};
    if (l30[0] != 0) {
        const int32_t t[3] = {l30[0], l30[1], 0};
        const int32_t lxy = Len3(t);
        const int32_t bx = Ratio(l30[0], lxy);
        const int32_t si = Ratio(l30[1], lxy);
        const int32_t mm[9] = {si, bx, 0, -bx, si, 0, 0, 0, 0x7fffffff};
        std::memcpy(m, mm, sizeof(m));
        l30[1] = lxy;
    }
    if (l30[1] != 0) {
        const int32_t t[3] = {0, l30[1], l30[2]};
        const int32_t lyz = Len3(t);
        const int32_t bx = Ratio(l30[1], lyz);
        const int32_t si = Ratio(l30[2], lyz);
        m[2] = static_cast<int32_t>(FUNC_001521(m[1], bx));
        m[1] = static_cast<int32_t>(FUNC_001521(m[1], si));
        m[5] = static_cast<int32_t>(FUNC_001521(m[4], bx));
        m[4] = static_cast<int32_t>(FUNC_001521(m[4], si));
        m[7] = -bx;
        m[8] = si;
    }
    l30[0] = 0;
    l30[1] = 0;
    l30[2] = len;
    VecMatMul(e, e, m);
    VecMatMul(e + 3, e + 3, m);
    VecMatMul(e + 6, e + 6, m);

    const int32_t limit = Sar(0x77359400, c.shift);
    int height = 0;
    const int16_t* after = PlanetTerrainCollision876(&height, limit, reinterpret_cast<intptr_t>(parent->raw),
                                                     next, l30, e, mode);
    if (!after) {
        s_unported |= kTqNo876;
        *out = -1;
        s_q.mode707 = 0;
        return reinterpret_cast<const uint8_t*>(FUNC_001822(next));
    }
    *out = (Sar(radius, 16) > height) ? 0 : -1;
    if (limit > height) {
        const int64_t alt = ArithShift64(static_cast<int64_t>(height), c.shift);
        s_q.alt703 = static_cast<uint32_t>(alt);
        s_q.alt704 = static_cast<uint32_t>(static_cast<uint64_t>(alt) >> 32);
    }
    s_q.alt705 = s_q.alt703;
    s_q.alt706 = s_q.alt704;
    if (*out == 0) {
        s_q.alt703 = 0;
        s_q.alt704 = 0;
        s_q.mode707 = 1;
    }
    return reinterpret_cast<const uint8_t*>(after);
}

// FUNC_000583 (ffe.asm 38980-39135): half-space test against normal `idx`.
static bool FUNC_000583(int32_t radius, uint32_t idx, const CollisionCtx& c)
{
    if (!c.normals || idx == 0) {
        // idx 0 reads the 6 bytes before the normal table in the DOS image.
        s_unported |= kTqNoNormals583;
        return true;
    }
    const uint8_t* n = c.normals + (idx >> 1) * 6 - 6;
    const uint8_t vi = n[0];
    const uint8_t* vr = c.verts + (vi >> 1) * 6;
    int8_t vx = static_cast<int8_t>(vr[2]);
    int32_t nrm[3];
    if (idx & 1) {
        nrm[0] = (int32_t)((uint32_t)(-static_cast<int32_t>(static_cast<int8_t>(n[2]))) << 8);
        if (!(vi & 1)) vx = static_cast<int8_t>(-vx);
    } else {
        nrm[0] = static_cast<int32_t>(static_cast<int8_t>(n[2])) * 256;
        if (vi & 1) vx = static_cast<int8_t>(-vx);
    }
    nrm[1] = static_cast<int32_t>(static_cast<int8_t>(n[3])) * 256;
    nrm[2] = static_cast<int32_t>(static_cast<int8_t>(n[4])) * 256;
    const int32_t sc = c.scale - c.shift;
    int32_t v[3] = {vx, static_cast<int8_t>(vr[3]), static_cast<int8_t>(vr[4])};
    for (int a = 0; a < 3; ++a) {
        v[a] = sc < 0 ? Sar(v[a], -sc) : Shl(v[a], sc);
        v[a] = static_cast<int32_t>(static_cast<uint32_t>(v[a]) - static_cast<uint32_t>(c.v[a]));
    }
    auto iabs = [](int32_t x) { return x < 0 ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x); };
    int32_t s = FUNC_001656_FindMSB(iabs(v[0]) | iabs(v[1]) | iabs(v[2])) - 0xd;
    if (s < 0) s = 0;
    uint32_t d = static_cast<uint32_t>(Sar(v[0], s)) * static_cast<uint32_t>(nrm[0]) +
                 static_cast<uint32_t>(Sar(v[1], s)) * static_cast<uint32_t>(nrm[1]) +
                 static_cast<uint32_t>(Sar(v[2], s)) * static_cast<uint32_t>(nrm[2]);
    d = 0u - d;
    d += d;
    return static_cast<int32_t>(d) >= Sar(radius, s);
}

static int8_t FUNC_000582(uint8_t* out, const CollisionCtx& c, const uint8_t* p, PhysObj* parent, int32_t radius);

// FUNC_000580 (ffe.asm 37930-38113): sub-model command. The query point moves
// to sub-model `model`'s frame (offset by vertex `vtx`, axes per `mode`) and
// that model's collision program runs with the caller's shift.
static int8_t FUNC_000580(uint8_t* out, const CollisionCtx& c, uint32_t model, uint8_t vtx, uint8_t mode,
                          int32_t radius, PhysObj* parent)
{
    const ShipModel* md = GetShipModel(model);
    if (!md) return 1;
    CollisionCtx s{};
    s.normals = ModelPtr(md->normalDataOffset);
    s.verts = ModelPtr(md->vertexDataOffset);
    s.scale = static_cast<int32_t>(md->scaleRelated + md->scaleFactor);
    s.shift = c.shift;
    // movsx byte / sar 1: indices >= 0x80 address records before the table.
    const uint8_t* r = c.verts + (static_cast<int8_t>(vtx) >> 1) * 6;
    int32_t d[3] = {static_cast<int8_t>(r[2]), static_cast<int8_t>(r[3]), static_cast<int8_t>(r[4])};
    if (vtx & 1) d[0] = -d[0];
    const int32_t sc = c.scale - c.shift;
    for (int a = 0; a < 3; ++a) {
        d[a] = sc < 0 ? Sar(d[a], -sc) : Shl(d[a], sc);
        d[a] = static_cast<int32_t>(static_cast<uint32_t>(c.v[a]) - static_cast<uint32_t>(d[a]));
    }
    if (mode & 0x20) d[0] = -d[0];
    if (mode & 0x10) d[1] = -d[1];
    if (mode & 0x08) d[2] = -d[2];
    // DATA_000028: where x, y, z land; modes 6/7 leave the ASM's stack block unset.
    static const int8_t kDst[8][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
                                      {1, 0, 2}, {0, 2, 1}, {-1, -1, -1}, {-1, -1, -1}};
    const int8_t* dst = kDst[mode & 7];
    for (int a = 0; a < 3; ++a)
        if (dst[a] >= 0) s.v[dst[a]] = d[a];
    const uint8_t* prog = ModelPtr(md->collisionDataOffset);
    if (!prog) {
        *out = 0;
        return 1;
    }
    return FUNC_000582(out, s, prog, parent, radius);
}

// FUNC_000582 (ffe.asm 38701-38977): collision bytecode walk.
static int8_t FUNC_000582(uint8_t* out, const CollisionCtx& c, const uint8_t* p, PhysObj* parent, int32_t radius)
{
    uint8_t bl = *p++;
    while (bl != 0) {
        const uint8_t flags = *p++;
        bool skip = false;
        if (bl & 0x20) {
            const uint8_t idx = *p++;
            uint16_t w;
            std::memcpy(&w, parent->raw + 0x9c + idx * 2, sizeof(w));
            const int8_t cnd = static_cast<int8_t>(*p++);
            uint32_t v = static_cast<uint32_t>(w) >> (cnd & 0x1f);
            v ^= static_cast<uint32_t>(static_cast<int32_t>(cnd) >> 7);
            skip = (v & 1) != 0;
        }
        const uint8_t type = bl & 0xc0;
        if (skip) {
            if (type == 0x00) {
                const uint8_t e = *p++;
                if (e == 0xff)
                    p = reinterpret_cast<const uint8_t*>(
                        FUNC_001822(reinterpret_cast<int16_t*>(const_cast<uint8_t*>(p + 6))));
                else if (e != 0)
                    while (*p++ != 0) {}
            } else if (type == 0x80) {
                p += 4;
            } else {
                while (*p++ != 0) {}
            }
        } else if (type == 0x00) {
            uint8_t e = *p++;
            if (e == 0xff) {
                int32_t hit = 0;
                p = FUNC_000581(&hit, p, parent, radius, c);
                if (hit == 0) {
                    s_q.flags697 |= flags;
                    *out = bl;
                    return 0;
                }
                s_q.flags697 |= flags & 0x7f;
            } else {
                while (e != 0) {
                    if (FUNC_000583(radius, e, c)) {
                        s_q.flags697 |= flags & 0x7f;
                        *out = bl;
                        return -1;
                    }
                    e = *p++;
                }
            }
        } else if (type == 0x40) {
            for (;;) {
                const uint8_t e = *p++;
                if (e == 0) {
                    s_q.flags697 |= flags & 0x7f;
                    *out = bl;
                    return -1;
                }
                if (!FUNC_000583(radius, e, c)) break;
            }
            while (*p++ != 0) {}
        } else if (type == 0x80) {
            const uint32_t model = (static_cast<uint32_t>(p[0]) << 8) | p[1];
            // Big-endian model word, then the axis mode byte, then the vertex byte.
            const int8_t r = FUNC_000580(out, c, model, p[3], p[2], radius, parent);
            p += 4;
            s_q.hitCmd701 |= *out;
            if (r < 0) {
                s_q.flags697 |= flags & 0x7f;
                *out = bl;
                return -1;
            }
            if (r == 0) {
                s_q.flags697 |= flags;
                if (flags & 0x80) {
                    *out = bl;
                    return 0;
                }
            }
        } else {
            // JUMP_002527: a terminator without flag 0x80 still reaches
            // FUNC_000583 with index 0 (JUMP_002528) before JUMP_002529.
            for (;;) {
                const uint8_t e = *p++;
                if (e == 0) {
                    s_q.flags697 |= flags;
                    if (flags & 0x80) {
                        *out = bl;
                        return 0;
                    }
                }
                if (FUNC_000583(radius, e, c)) break;
            }
            while (*p++ != 0) {}
        }
        bl = *p++;
    }
    *out = 0;
    return 1;
}

// FUNC_000579 (ffe.asm 37833-37927).
static int FUNC_000579(uint8_t* out, PhysObj* obj, PhysObj* parent, int64_t pos[3], int32_t radius)
{
    const ShipModel* md = ModelOf(parent);
    if (!md) return 1;
    CollisionCtx c{};
    c.normals = ModelPtr(md->normalDataOffset);
    c.verts = ModelPtr(md->vertexDataOffset);
    c.scale = static_cast<int32_t>(md->scaleRelated + md->scaleFactor);
    const int t = FUNC_001658_Vec64Truncate(reinterpret_cast<uint32_t*>(pos), 0x1c);
    for (int a = 0; a < 3; ++a) c.v[a] = static_cast<int32_t>(pos[a]);
    c.shift = t;
    const int d = t - (c.scale - 0x17);
    if (d < 0) {
        c.shift += -d;
        for (int a = 0; a < 3; ++a) c.v[a] = Sar(c.v[a], -d);
    }
    if (c.shift < 0)
        radius = Shl(radius, -c.shift);
    else if (c.shift > 0)
        radius = Sar(radius, c.shift);
    if (PhysObjGetU8(obj, 0x57) == 0 || PhysObjGetU8(obj, 0x56) != PhysObjGetU8(parent, 0x86)) {
        int32_t rot[9];
        std::memcpy(rot, parent->raw, sizeof(rot));
        VecMatTMul(c.v, c.v, rot);
    }
    const uint8_t* prog = ModelPtr(md->collisionDataOffset);
    if (!prog) {
        *out = 0;
        return 1;
    }
    return FUNC_000582(out, c, prog, parent, radius);
}

namespace {

// FUNC_000576 (ffe.asm 37229-37404): obj's collision points against parent.
int Func576(PhysObj* obj, PhysObj* parent, const int64_t base[3])
{
    // Task 4.167: if the parent has no model (index 0), there can be no collision.
    // The ASM's equivalent returns 0 in this case; without this guard the port's
    // ModelOf(parent) may find a spurious model at index 0, causing Func579 to
    // set flags697 |= 0x80 and ultimately making chain A's 575 return 0x02 instead
    // of the correct 0x80 (the bounce path via FUNC_000578).
    if (!parent || PhysObjGetModel(parent) == 0) return 0;
    s_q.flags697 = 0;
    const ShipModel* md = ModelOf(obj);
    if (!md) return -1;
    const int32_t shift = 0x17 - static_cast<int32_t>(md->scaleRelated);
    const uint8_t* table = ModelPtr(md->collisionDataOffset);
    int32_t radius = GetI32(obj, 0x140);
    int64_t probe[3] = {base[0], base[1], base[2]};
    int32_t vec[3] = {0, 0, 0};
    for (;;) {
        uint8_t out = 0;
        const int al = static_cast<int8_t>(FUNC_000579(&out, obj, parent, probe, radius));
        if (al > 0) s_q.flags697 |= 0x80;
        if (s_q.flags697 != 0) {
            s_q.hitIndex698 = PhysObjGetU8(parent, 0x86);
            s_q.hitObj702 = parent;
            s_q.flags696 = s_q.flags697;
            s_q.hitCmd701 = out;
        }
        if (al >= 0) return 0;
        if (static_cast<int8_t>(PhysObjGetU8(obj, 0x14c)) >= 0) return -1;
        if (!table) return -1;
        table -= 2;
        int16_t w;
        std::memcpy(&w, table, sizeof(w));
        if (w == 0) return -1;
        table -= 2;
        int16_t e16;
        std::memcpy(&e16, table, sizeof(e16));
        const int32_t e = e16;
        switch (e & 6) {
        case 0: std::memcpy(vec, obj->raw + 0x00, sizeof(vec)); break;
        case 2: std::memcpy(vec, obj->raw + 0x0c, sizeof(vec)); break;
        case 4: std::memcpy(vec, obj->raw + 0x18, sizeof(vec)); break;
        default: break;  // 6: ASM keeps the previous vector
        }
        if (e & 1)
            for (int a = 0; a < 3; ++a) vec[a] = -vec[a];
        for (int a = 0; a < 3; ++a)
            vec[a] = static_cast<int32_t>(static_cast<uint32_t>(Sar(vec[a], 16)) * static_cast<uint32_t>(w));
        radius = Shl(e & 0xfff8, 15);
        if (shift != 0) {
            for (int a = 0; a < 3; ++a) vec[a] = Sar(vec[a], shift);
            radius = Sar(radius, shift);
        }
        for (int a = 0; a < 3; ++a) probe[a] = static_cast<int64_t>(static_cast<uint64_t>(base[a]) +
                                                                   static_cast<uint64_t>(static_cast<int64_t>(vec[a])));
    }
}

// FUNC_000575 (ffe.asm 36550-37226), spawn-path branches.
uint8_t Func575(int64_t pos[3], PhysObj* obj, PhysObj* parent)
{
    const int64_t local38[3] = {pos[0], pos[1], pos[2]};
    for (int a = 0; a < 3; ++a)
        if (pos[a] < 0) pos[a] = static_cast<int64_t>(0ull - static_cast<uint64_t>(pos[a]));
    uint64_t sum = static_cast<uint64_t>(pos[0]) + static_cast<uint64_t>(pos[1]) + static_cast<uint64_t>(pos[2]);
    int64_t d = static_cast<int64_t>(sum) >> 1;
    d = static_cast<int64_t>(static_cast<uint64_t>(d) - static_cast<uint64_t>(GetI64(obj, 0x138)));
    if (d >= 0) {
        d = static_cast<int64_t>(static_cast<uint64_t>(d) - static_cast<uint64_t>(GetI64(parent, 0x138)));
        if (d >= 0) return 0;
    }
    s_q.mode707 = 0;
    if (static_cast<int8_t>(PhysObjGetU8(parent, 0x14c)) >= 0) {
        s_unported |= kTqSphere577;
        return 0;
    }
    if (Func576(obj, parent, local38) != 0) return s_q.flags696;
    if (s_q.mode707 == 0) {
        s_unported |= kTqBounce578;
        return s_q.flags696;
    }
    s_unported |= kTqResponse2410;
    return s_q.flags696;
}

void LoadState(const TerrainQueryState& io)
{
    s_q.flags696 = io.flags696;
    s_q.flags697 = io.flags697;
    s_q.hitIndex698 = io.hitIndex698;
    s_q.d700 = io.d700;
    s_q.hitCmd701 = io.hitCmd701;
    s_q.hitObj702 = io.hitObj702;
    s_q.alt703 = io.alt703;
    s_q.alt704 = io.alt704;
    s_q.alt705 = io.alt705;
    s_q.alt706 = io.alt706;
    s_q.mode707 = io.mode707;
}

void SaveState(TerrainQueryState* io)
{
    io->flags696 = s_q.flags696;
    io->flags697 = s_q.flags697;
    io->hitIndex698 = s_q.hitIndex698;
    io->d700 = s_q.d700;
    io->hitCmd701 = s_q.hitCmd701;
    io->hitObj702 = s_q.hitObj702;
    io->alt703 = s_q.alt703;
    io->alt704 = s_q.alt704;
    io->alt705 = s_q.alt705;
    io->alt706 = s_q.alt706;
    io->mode707 = s_q.mode707;
}

} // namespace

// The query runs on DATA_008696..8707 themselves: the fields FUNC_000574 does
// not clear (8702, 8704..8706) keep their last values, and the results stay
// there after it returns, as in the ASM.
int32_t FUNC_000574_TerrainHeight(int64_t pos[3], PhysObj* obj, PhysObj* parent)
{
    if (!PlanetMeshTablesReady()) InitPlanetMeshTables();
    TerrainQueryState io{};
    TerrainQueryLoadLabels(&io);
    LoadState(io);
    s_q.d700 = 0;
    s_q.flags696 = 0;
    s_q.flags697 = 0;
    s_q.hitIndex698 = 0;
    s_q.hitCmd701 = 0;
    s_q.alt703 = 0xffffffffu;
    s_q.mode707 = 0;
    (void)Func575(pos, obj, parent);
    SaveState(&io);
    TerrainQueryStoreLabels(io);
    if (s_q.alt704 != 0 || static_cast<int32_t>(s_q.alt703) < 0) return -1;
    return static_cast<int32_t>(s_q.alt703);
}

int TerrainQueryFunc576(PhysObj* obj, PhysObj* parent, const int64_t base[3], TerrainQueryState* io)
{
    if (!PlanetMeshTablesReady()) InitPlanetMeshTables();
    LoadState(*io);
    const int r = Func576(obj, parent, base);
    SaveState(io);
    return r;
}

uint32_t TerrainQueryUnported() { return s_unported; }
void TerrainQueryResetUnported() { s_unported = 0; }

} // namespace jjffe
