// Planet 0x1F icosahedron / dither fill chain (FUNC_001816–876).
#include "jjffe/data_globals.h"
#include "jjffe/data_loader.h"
#include "jjffe/planet_render.h"
#include "jjffe/planet_ab.h"
#include "jjffe/planet_subdiv_trace.h"
#include "jjffe/planet_sat_trace.h"
#include "jjffe/planet_root_seed.h"
#include "jjffe/planet_midpoint_trace.h"
#include "data_blob.h"
#include <cstdint>
#include <cstring>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>

namespace jjffe {

// Forward declarations from core.cpp
uint32_t FUNC_001467(uint32_t);
uint32_t FUNC_001479(uint16_t*, int32_t*);
int32_t FUNC_001521(int32_t, int32_t);
int32_t FUNC_001656_FindMSB(uint32_t);
int32_t FUNC_001825(uint32_t*);
int32_t FUNC_001824(uint32_t);
void FUNC_001816(void);
void FUNC_001817(void);
uint32_t* FUNC_001818(void);
void FUNC_001819(uint32_t*);
uint32_t* FUNC_001820(void);
void FUNC_001821(uint32_t*);
int16_t* FUNC_001823(uintptr_t*, int16_t*);
uint32_t FUNC_001833(intptr_t, uint32_t, int, int);
void FUNC_001835(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
                 uint32_t, intptr_t, uint32_t, uint32_t, int);
void FUNC_001691(uint16_t*, uint32_t, int32_t);
uint32_t* FUNC_001692(uint32_t*, uint32_t);
void FUNC_001840(uint32_t*, uint32_t*, uint32_t*, int*, uint32_t);
void FUNC_001841(intptr_t);
void FUNC_001842(uint32_t*, uint32_t*, uint32_t*, int*, uint32_t);
void FUNC_001843(intptr_t);
void FUNC_001844(uint32_t*, uint32_t*, uint32_t*, int*, uint32_t);
void FUNC_001845(uint32_t*, uint32_t*, uint32_t*, uint32_t, uint32_t);
uint32_t FUNC_001836(uint32_t);
uint32_t FUNC_001715(uint32_t);
uint32_t FUNC_001716(uint32_t);
uint32_t FUNC_001847(uint16_t*);
void FUNC_001848(intptr_t);
uint32_t FUNC_001831(int);
uint32_t FUNC_001832(int);

// Modes 8/9 use FUNC_001836 as init (ASM identity); adapt to initFn(intptr_t).
static void PlanetModeInit836(intptr_t entry)
{
    (void)FUNC_001836(static_cast<uint32_t>(entry));
}

// ASM DATA_009274 / 009277 (ffedat.asm:29064-29071): 0x5dc 0x48-byte vertex nodes and
// 0xbb8 16-byte edge nodes, each dword a qword here.
static uintptr_t s_pool816Nodes[0x5dc * 0x12];
static uintptr_t s_pool820Nodes[0xbb8 * 4];
static uint32_t s_satTreeStorage[512];
static constexpr size_t kSatTreeBytes = sizeof(s_satTreeStorage);

// Resolve a FUNC_001823 slot word: 0/negative = none; small positive = packed
// offset into s_satTreeStorage; large = already a host pointer (defensive).
int* PlanetSatResolve(int32_t slot)
{
    if (slot <= 0)
        return nullptr;
    const uint32_t u = static_cast<uint32_t>(slot);
    if (u < kSatTreeBytes) {
        return reinterpret_cast<int*>(
            reinterpret_cast<uint8_t*>(s_satTreeStorage) + u);
    }
    if (u > 0x10000u)
        return reinterpret_cast<int*>(static_cast<uintptr_t>(u));
    return nullptr;
}
static uint32_t s_satAux[0x14];
static uint8_t s_renderModeTable[14 * 48];
static void* s_shadeFn[14]{};
static void (*s_initFn[14])(intptr_t){};
static jmp_buf s_meshJmpBuf;
static const int32_t* s_icosaVerts = nullptr;
static const int32_t* s_icosaFaces = nullptr;
static const int32_t* s_icosaEdges = nullptr;

static uintptr_t s_pool816Head = 0;
static int s_pool816Used = 0;
static void* s_pool820Head = nullptr;
static uint32_t s_pool820Used = 0;

static void InitRenderModeTable(const uint8_t* blob)
{
    if (!blob)
        return;
    // Keep the DOS 48-byte row (palette dwords at +0xC). Function pointers live
    // beside it: ASM [ebx]=shade, call [ebx+4]=init, return [ebx+8].
    // Shade/init pairs must match ffedat.asm DATA_007843 (blob zeros the
    // relocatable dwords). Mis-mapping mode 2→842 left Mercury (mesh mode=2)
    // on 808-only leaves with an empty disc center; ASM mode 2 is 840/841.
    std::memcpy(s_renderModeTable, blob + data::OFFSET_DATA_007843, sizeof(s_renderModeTable));
    static const struct { void* shade; void (*init)(intptr_t); } kFns[14] = {
        {(void*)FUNC_001840, FUNC_001841}, // 0
        {(void*)FUNC_001840, FUNC_001841}, // 1
        {(void*)FUNC_001840, FUNC_001841}, // 2 — Mercury / gray rocky
        {(void*)FUNC_001842, FUNC_001843}, // 3
        {(void*)FUNC_001840, FUNC_001841}, // 4
        {(void*)FUNC_001842, FUNC_001843}, // 5
        {(void*)FUNC_001842, FUNC_001843}, // 6
        {(void*)FUNC_001840, FUNC_001841}, // 7
        {(void*)FUNC_001844, PlanetModeInit836}, // 8
        {(void*)FUNC_001845, PlanetModeInit836}, // 9
        {(void*)FUNC_001840, FUNC_001841}, // 10
        {(void*)FUNC_001840, FUNC_001841}, // 11
        {(void*)FUNC_001840, FUNC_001841}, // 12
        {(void*)FUNC_001840, FUNC_001841}, // 13
    };
    for (int i = 0; i < 14; ++i) {
        s_shadeFn[i] = kFns[i].shade;
        s_initFn[i] = kFns[i].init;
    }
}

void InitPlanetMeshTables()
{
    const uint8_t* blob = data::GetLoadedBlob();
    if (!blob)
        return;

    s_icosaVerts = reinterpret_cast<const int32_t*>(blob + data::OFFSET_DATA_007892);
    s_icosaFaces = reinterpret_cast<const int32_t*>(blob + data::OFFSET_DATA_007893);
    s_icosaEdges = reinterpret_cast<const int32_t*>(blob + data::OFFSET_DATA_007894);
    DATA_007841 = (uint32_t)data::OFFSET_DATA_007841;
    DATA_007842 = (uint32_t)data::OFFSET_DATA_007842;
    DATA_007843 = 0;
    InitRenderModeTable(blob);
    InitPlanetDitherMixTables();
    InitPlanet877ColumnTable();

    std::memset(s_satTreeStorage, 0, sizeof(s_satTreeStorage));
    std::memset(s_satAux, 0, sizeof(s_satAux));
    DATA_009258 = (uint32_t)(uintptr_t)s_satTreeStorage;
    // ASM FUNC_000143/InitPlanetMeshTables seeds the gfx-detail words to their
    // defaults. R1116b: re-apply the harness override (both words) so the
    // -1/-2 highest-detail state survives the lazy InitGameState the walk
    // triggers; with no AB_LOD_BIAS the values are the 1/1 live defaults.
    AbApplyLodBias();
}

bool PlanetMeshTablesReady()
{
    return s_icosaVerts && s_icosaFaces && s_icosaEdges;
}

void FUNC_001816(void)
{
    if (s_pool816Head != 0)
        return;

    // ffe.asm:113446-113466: 0x5db links, node 0x5db's 0; 0xbb7 links, node 0xbb7's 0.
    s_pool816Head = (uintptr_t)&s_pool816Nodes[0];
    for (int i = 0; i < 0x5db; ++i)
        s_pool816Nodes[i * 0x12] = (uintptr_t)&s_pool816Nodes[(i + 1) * 0x12];
    s_pool816Nodes[0x5db * 0x12] = 0;

    DATA_009275 = s_pool820Nodes;
    s_pool820Head = s_pool820Nodes;
    for (int i = 0; i < 0xbb7; ++i)
        s_pool820Nodes[i * 4] = (uintptr_t)&s_pool820Nodes[(i + 1) * 4];
    s_pool820Nodes[0xbb7 * 4] = 0;

    s_pool816Used = 0;
    s_pool820Used = 0;
    DATA_009276 = 0;
}

// The call oracle's storage for the pools, their heads and counts: ASM DATA_009274
// / 009272 / 009273 (vertices) and DATA_009277 / 009275 / 009276 (edges).
void PlanetPoolOracleStorage(void** head816, void** used816, void** head820, void** used820, void** nodes816,
                             std::size_t* size816, void** nodes820, std::size_t* size820)
{
    *head816 = &s_pool816Head;
    *used816 = &s_pool816Used;
    *head820 = &s_pool820Head;
    *used820 = &s_pool820Used;
    *nodes816 = s_pool816Nodes;
    *size816 = sizeof s_pool816Nodes;
    *nodes820 = s_pool820Nodes;
    *size820 = sizeof s_pool820Nodes;
}

// ffe.asm:113486-113490: only the vertex freelist head; the counts and the edge
// freelist stand until FUNC_001816 rebuilds both.
void FUNC_001817(void)
{
    s_pool816Head = 0;
}

uint32_t* FUNC_001818(void)
{
    if (!s_pool816Head)
        FUNC_001816();
    uintptr_t* node = (uintptr_t*)s_pool816Head;
    if (!node)
        longjmp(s_meshJmpBuf, 1);
    ++s_pool816Used;
    if (s_pool816Used > 0x5db)
        longjmp(s_meshJmpBuf, 1);
    s_pool816Head = node[0];
    // ASM pops freelist only (no memset). On x64 the next-ptr leaves its low
    // 32 bits in screen xy; midpoints that skip 847 (screen-average path) then
    // paint 808 streaks. Clear screen only — full zero broke mix<=0 gates.
    *reinterpret_cast<uint32_t*>(node) = 0;
    return (uint32_t*)node;
}

uint32_t* FUNC_001820(void)
{
    if (!s_pool820Head)
        FUNC_001816();
    uintptr_t* node = (uintptr_t*)s_pool820Head;
    if (!node)
        longjmp(s_meshJmpBuf, 2);
    s_pool820Head = (void*)node[0];
    DATA_009275 = s_pool820Head;
    ++s_pool820Used;
    DATA_009276 = s_pool820Used;
    if (s_pool820Used > 2999)
        longjmp(s_meshJmpBuf, 2);
    node[1] = 0;
    node[2] = 0;
    node[3] = 0;
    return (uint32_t*)node;
}

void FUNC_001819(uint32_t* param_1)
{
    if (!param_1)
        return;
    *(uintptr_t*)param_1 = s_pool816Head;
    s_pool816Head = (uintptr_t)param_1;
    if (s_pool816Used > 0)
        --s_pool816Used;
}

// ASM FUNC_001821: free child[1] tree, free vertex[2] via 819, return node to
// 820 freelist, then walk sibling chain at [3]. Without the freelist return,
// deeper LOD exhausts the 2999-node pool and longjmps to an empty disc.
void FUNC_001821(uint32_t* param_1)
{
    uintptr_t* node = reinterpret_cast<uintptr_t*>(param_1);
    while (node) {
        if (node[1] != 0)
            FUNC_001821(reinterpret_cast<uint32_t*>(node[1]));
        if (node[2] != 0)
            FUNC_001819(reinterpret_cast<uint32_t*>(node[2]));
        if (s_pool820Used > 0)
            --s_pool820Used;
        DATA_009276 = s_pool820Used;
        uintptr_t* sibling = reinterpret_cast<uintptr_t*>(node[3]);
        node[0] = reinterpret_cast<uintptr_t>(s_pool820Head);
        s_pool820Head = node;
        DATA_009275 = node;
        node = sibling;
    }
}

static int32_t Sat823ChildFromParent(int32_t parent, const int32_t* terrainTable)
{
    if (parent > 0) {
        if (!terrainTable)
            return 0;
        const int32_t t = terrainTable[parent * 3];
        return -(t - 1);
    }
    if (parent < 0)
        return parent + 1;
    return 0;
}

static uint32_t* Sat823Ptr(const uint32_t* base, uint32_t off)
{
    if (!off)
        return nullptr;
    return reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(base)) + off);
}

static uint32_t* Sat823ChildSlot(uint32_t* node, uint32_t idx)
{
    return reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(node) + 8 + idx * 4);
}

static uint32_t* Sat823Node(const uint32_t* base, uint32_t* node, uint32_t packed)
{
    if (!packed)
        return node;
    return Sat823Ptr(base, packed);
}

int16_t* FUNC_001823(uintptr_t* param_1, int16_t* param_2)
{
    uint32_t* base = s_satTreeStorage;
    std::memset(s_satTreeStorage, 0, sizeof(s_satTreeStorage));
    std::memset(s_satAux, 0, sizeof(s_satAux));

    const uint8_t* blob = data::GetLoadedBlob();
    const int32_t* terrainTable =
        blob ? reinterpret_cast<const int32_t*>(blob + data::OFFSET_DATA_007842) : nullptr;

    int alloc = 0x58;
    const size_t storageBytes = sizeof(s_satTreeStorage);

    if (!param_2) {
        if (param_1)
            *param_1 = reinterpret_cast<uintptr_t>(base);
        jjffe_planet_sat_note_823(reinterpret_cast<uintptr_t>(base), alloc, param_2);
        return param_2;
    }

    int16_t mask = *param_2;
    if (mask == 0) {
        if (param_1)
            *param_1 = reinterpret_cast<uintptr_t>(base);
        jjffe_planet_sat_note_823(reinterpret_cast<uintptr_t>(base), alloc, param_2);
        return param_2 + 1;
    }

    int guard = 0;
    while (mask != 0 && guard++ < 4096) {
        const int16_t recType = param_2[1];
        const int msb = FUNC_001656_FindMSB((uint32_t)(uint16_t)mask);
        uint32_t norm = ((uint32_t)(uint16_t)mask << ((16 - msb) & 0x1f)) & 0xffffu;

        int depth = (msb - 3) >> 1;
        if (depth < 0 && ((msb - 3) & 1) != 0)
            ++depth;

        uint32_t* node = base;
        for (; depth > 0; --depth) {
            const uint32_t idx = norm >> 11;
            const uint32_t maxIdx = (node == base) ? 19u : 3u;
            if (idx > maxIdx)
                break;
            uint32_t* slot = Sat823ChildSlot(node, idx);
            uint32_t child = *slot;
            if (!child) {
                if ((size_t)alloc + 0x18 > storageBytes)
                    break;
                uint32_t* inode = Sat823Ptr(base, static_cast<uint32_t>(alloc));
                alloc += 0x18;
                std::memset(reinterpret_cast<uint8_t*>(inode) + 8, 0, 16);
                for (int i = 0; i < 2; ++i) {
                    const int32_t parentVal = static_cast<int32_t>(node[i]);
                    inode[i] = static_cast<uint32_t>(Sat823ChildFromParent(parentVal, terrainTable));
                }
                child = static_cast<uint32_t>(alloc - 0x18);
                *slot = child;
            }
            // ASM: uVar5 = (uVar5 & 0x7ff) << 2 — must clear bits 11+ (the
            // just-consumed child index). `(norm << 2) & 0x1fff` wrongly keeps
            // old bit 11 as new bit 13 when the index was odd, derailing the tree.
            norm = (norm & 0x7ffu) << 2;
            node = Sat823Node(base, node, child);
            if (!node)
                break;
        }

        int slot = 0;
        while (slot < 2 && node[slot] != 0)
            ++slot;
        if (slot < 2) {
            if ((size_t)alloc + 0x10 > storageBytes)
                break;
            uint32_t* leaf = Sat823Ptr(base, static_cast<uint32_t>(alloc));
            alloc += 0x10;
            node[slot] = static_cast<uint32_t>(alloc - 0x10);
            leaf[0] = static_cast<uint32_t>(static_cast<int32_t>(recType));
            std::memcpy(reinterpret_cast<int16_t*>(leaf) + 2, param_2 + 2, 6 * sizeof(int16_t));
        }

        param_2 += 8;
        mask = *param_2;
    }

    if (param_1)
        *param_1 = reinterpret_cast<uintptr_t>(base);
    jjffe_planet_sat_note_823(reinterpret_cast<uintptr_t>(base), alloc, param_2);
    return param_2 + 1;
}

uint32_t FUNC_001833(intptr_t param_1, uint32_t param_2, int param_3, int param_4)
{
    (void)param_2;
    uint8_t zoom = 0;
    if (param_1)
        zoom = (uint8_t)*(uint32_t*)(param_1 + 0x130);
    if (param_3 < 0)
        param_3 = 0;
    if (getenv("AB_1F_SHADE_BIND") && getenv("AB_1F_SHADE_BIND")[0]) {
        static int s_m;
        if (s_m < 8) {
            fprintf(stderr, "1833 mode=%d zoom=%u lodFlag=%d\n", param_3, (unsigned)zoom,
                    param_4);
            ++s_m;
        }
    }
    uint8_t* entry = s_renderModeTable + (size_t)param_3 * 48;
    if (s_initFn[param_3])
        s_initFn[param_3]((intptr_t)entry);
    PlanetBindShadeCallbacks(param_1, param_4, s_shadeFn[param_3], zoom);
    // ASM FUNC_001833: `mov eax,[ebx+8]` / ret. FUNC_001835 uses low bits of
    // that dword: (ret&1)!=0 && ret>=8 → 869, else 868. Mode-0 blob word is
    // 0x9 → 869 (living-planet tessellation). Returning 0 forced 868 and
    // capped model-126 at ~121×001807 vs ASM ~216.
    return *reinterpret_cast<uint32_t*>(entry + 8);
}

static int32_t SqrtVia723(int32_t sumSq)
{
    return (int32_t)FUNC_001467((uint32_t)sumSq);
}

bool PlanetCollisionDescent(uint8_t* recs, uint32_t* const* edges, const int32_t* faceIdx,
                            uintptr_t satRoot, const int32_t* origin, int32_t local1c,
                            uint32_t local20, int32_t* heightOut);

// FUNC_001823's pointer past the sat records: FUNC_001876's return value
// (ffe.asm:131062-131064, 132160-132161).
static int16_t* s_876ProgEnd = nullptr;

intptr_t FUNC_001876(int* param_1, int param_2, intptr_t param_3, intptr_t param_4, intptr_t param_5,
                     int* param_6, int* param_7, int param_8, int param_9, uint32_t param_10,
                     uint32_t param_11, int param_12);

int16_t* PlanetTerrainCollision876(int* heightOut, int limit, intptr_t planetObj, int16_t* prog,
                                   int* origin, int* basis, int mode)
{
    s_876ProgEnd = nullptr;
    FUNC_001876(heightOut, limit, planetObj, 0, reinterpret_cast<intptr_t>(prog), origin, basis,
                mode, 0, 0, 0, 1);
    return s_876ProgEnd;
}

intptr_t FUNC_001876(int* param_1, int param_2, intptr_t param_3, intptr_t param_4, intptr_t param_5,
                     int* param_6, int* param_7, int param_8, int param_9, uint32_t param_10,
                     uint32_t param_11, int param_12)
{
    {
        static int s_876e = 0;
        if (getenv("AB_1F_876E_DEBUG") && s_876e < 6) {
            std::fprintf(stderr, "876E p2=%d p4=%p p6=(%d,%d,%d) p7=(%d,%d,%d)(%d,%d,%d)(%d,%d,%d) "
                    "p8=%d p9=%d p10=%u p11=%u p12=%d\n",
                    param_2, (void*)param_4,
                    param_6?param_6[0]:0, param_6?param_6[1]:0, param_6?param_6[2]:0,
                    param_7?param_7[0]:0, param_7?param_7[1]:0, param_7?param_7[2]:0,
                    param_7?param_7[3]:0, param_7?param_7[4]:0, param_7?param_7[5]:0,
                    param_7?param_7[6]:0, param_7?param_7[7]:0, param_7?param_7[8]:0,
                    param_8, param_9, param_10, param_11, param_12);
            ++s_876e;
        }
    }
    (void)param_2;
    (void)param_9;
    (void)param_10;
    (void)param_11;
    const int entry_camz = param_6 ? param_6[2] : 0;

    const int32_t* icosaFaces = s_icosaFaces;
    const int32_t* icosaEdges = s_icosaEdges;
    const int32_t* icosaVerts = s_icosaVerts;
    if (!icosaFaces || !icosaEdges || !icosaVerts || !param_6 || !param_7)
        return 0;

    if (param_12 == 0) {
        int adjusted9 = param_9;
        if (param_4) {
            const int local14 = *(int32_t*)(param_4 + 0x148) - 8;
            if (local14 < 0)
                adjusted9 = param_9 >> ((-local14) & 0x1f);
            else
                adjusted9 = param_9 << (local14 & 0x1f);
        }
        uint32_t meshBits[4] = {};
        FUNC_001692(meshBits, param_11);
        FUNC_001691(reinterpret_cast<uint16_t*>(&DATA_009259), meshBits[0], (int32_t)meshBits[1]);
        FUNC_001877(param_4, param_6, param_7, DATA_009259, (int)param_10, adjusted9);
    } else {
        DATA_009283 = 0x7fffffffu;
        DATA_009285 = (uint32_t)param_2;
        DATA_009284 = 0x4000u;
    }

    int32_t faceIdx[0x78];
    int32_t edgeIdx[0x3c];
    std::memcpy(faceIdx, icosaFaces, sizeof(faceIdx));
    std::memcpy(edgeIdx, icosaEdges, sizeof(edgeIdx));

    // ASM 876 does not memset the 0x48-byte verts — only fields it writes
    // (camera, untranslated copy, dither seed at +0x3c). +0x44 LOD
    // stay stack garbage until 855/829; zeroing them blocks mixed cVar1 subdivide.
    uint8_t recs[12 * 0x48];
    uint32_t* local988[0x1e];
    std::memset(local988, 0, sizeof(local988));

    int32_t seed = param_3 ? *(int32_t*)(intptr_t)(param_3 + 0xa0) : 0;
    const int32_t seed0 = seed;
    if (getenv("AB_1F_SEED_TRACE"))
        std::fprintf(stderr, "876 seed param_3=%p init=%d\n",
                     (void*)param_3, (int)seed);

    auto colLen = [](int32_t* v) -> int32_t {
        int msb = FUNC_001825((uint32_t*)v);
        int shift = msb - 0xe;
        int32_t x = v[0], y = v[1], z = v[2];
        if (shift < 1) {
            const int64_t sum = (int64_t)x * x + (int64_t)y * y + (int64_t)z * z;
            return SqrtVia723((int32_t)(sum > 0x7fffffff ? 0x7fffffff : sum));
        }
        uint8_t b = (uint8_t)shift;
        x >>= (b & 0x1f);
        y >>= (b & 0x1f);
        z >>= (b & 0x1f);
        return SqrtVia723(x * x + y * y + z * z) << (b & 0x1f);
    };

    int32_t local10 = colLen(param_7);
    int32_t localC = colLen(param_7 + 3);
    if (local10 < localC)
        local10 = localC;
    localC = colLen(param_7 + 6);
    if (local10 < localC)
        local10 = localC;

    int32_t local_c = (int32_t)FUNC_001521(local10, 0x54000000) * 2;
    // ASM does not clamp local_c to 14 here; keep raw for MSB/LOD.
    int msbC = FUNC_001656_FindMSB((uint32_t)local_c);

    // ASM FUNC_001876 DATA_007837 distance bump (param_12 == 0 only).
    int32_t bias837 = (int32_t)DATA_008812 - 1;
    if (param_12 == 0) {
        int local3c = FUNC_001825((uint32_t*)param_6) - 0xe;
        if (local3c < 0)
            local3c = 0;
        uint8_t b = (uint8_t)local3c;
        int32_t px = param_6[0] >> (b & 0x1f);
        int32_t py = param_6[1] >> (b & 0x1f);
        int32_t pz = param_6[2] >> (b & 0x1f);
        int32_t distSq = SqrtVia723(px * px + py * py + pz * pz);
        uint32_t local38 = (uint32_t)distSq << (b & 0x1f);
        const int32_t baseBias = bias837;
        if ((uint32_t)((local10 >> 4) + local10) < local38) {
            bias837 = baseBias + 1;
            if ((uint32_t)((local10 >> 3) + local10) < local38)
                bias837 = baseBias + 2;
        }
    }
    if (getenv("AB_1F_LOD_TRACE"))
        fprintf(stderr, "LOD_ICOSA local10=%d local_c=%d msbC=%d bias=%d cam0=(%d,%d,%d)\n",
                (int)local10, (int)local_c, (int)msbC, (int)bias837,
                (int)param_6[0], (int)param_6[1], (int)param_6[2]);
    PlanetSetLodMsb(msbC, bias837);

    int vertCount = 0;
    const uint32_t* pu19 = (const uint32_t*)icosaVerts;
    do {
        uint8_t* rec = recs + vertCount * 0x48;
        int32_t* cam = reinterpret_cast<int32_t*>(rec + 4);
        int32_t i1 = FUNC_001521((int32_t)*pu19, param_7[0]);
        int32_t i2 = FUNC_001521((int32_t)pu19[1], param_7[3]);
        i1 += i2;
        i2 = FUNC_001521((int32_t)pu19[2], param_7[6]);
        cam[0] = i1 + i2;
        i1 = FUNC_001521((int32_t)*pu19, param_7[1]);
        i2 = FUNC_001521((int32_t)pu19[1], param_7[4]);
        i1 += i2;
        i2 = FUNC_001521((int32_t)pu19[2], param_7[7]);
        cam[1] = i1 + i2;
        i1 = FUNC_001521((int32_t)*pu19, param_7[2]);
        i2 = FUNC_001521((int32_t)pu19[1], param_7[5]);
        i1 += i2;
        i2 = FUNC_001521((int32_t)pu19[2], param_7[8]);
        cam[2] = i1 + i2;
        cam[9] = cam[0];
        cam[10] = cam[1];
        cam[11] = cam[2];
        cam[0] += param_6[0];
        cam[1] += param_6[1];
        cam[2] += param_6[2];
        // ASM: project only — LOD at +0x44 is filled later by FUNC_001829.
        if (param_12 != 1) {
            FUNC_001479(reinterpret_cast<uint16_t*>(rec), cam);
        }
        seed = FUNC_001824((uint32_t)seed);
        intptr_t renderFn = (intptr_t)s_shadeFn[param_8 < 0 || param_8 > 13 ? 0 : param_8];
        // ASM 876: [edi+0x38] with edi=rec+4 → dither seed at rec+0x3c (855 eff5 reads +0x3c).
        // sar eax,4 on signed seed — not logical >> on uint32_t.
        int32_t* const gateMix = reinterpret_cast<int32_t*>(rec + 0x3c);
        if (renderFn == (intptr_t)FUNC_001842)
            *gateMix = ((int32_t)pu19[1] >> 5) + 0x4000000;
        else
            *gateMix = seed >> 4;
        if (getenv("AB_1F_SEED_TRACE") && vertCount < 3)
            std::fprintf(stderr, "876 v%d seed>>4=%d gateMix=%d\n",
                         vertCount, (int)(seed >> 4), (int)*gateMix);
        ++vertCount;
        pu19 += 3;
    } while (vertCount < 0xc);
    if (const char* rootSeedPath = getenv("AB_1F_ROOT_MIX_SEED")) {
        // Optional A/B override only — native rec+0x3c seed matches ASM without this.
        if (rootSeedPath[0] && jjffe_planet_root_seed_load(rootSeedPath)) {
            for (int vi = 0; vi < 12; ++vi) {
                uint8_t* rec = recs + vi * 0x48;
                // Gate mix only: FUNC_001829 overwrites +0x44 before 869 uses LOD.
                *reinterpret_cast<int32_t*>(rec + 0x3c) =
                    jjffe_planet_root_seed_mix(vi);
            }
        }
    }
    if (getenv("AB_1F_SEED_TRACE"))
        std::fprintf(stderr, "876 post-loop rec0+0x3c=%d rec10=%d rec11=%d\n",
                     (int)*reinterpret_cast<int32_t*>(recs + 0x3c),
                     (int)*reinterpret_cast<int32_t*>(recs + 10 * 0x48 + 0x3c),
                     (int)*reinterpret_cast<int32_t*>(recs + 11 * 0x48 + 0x3c));
    jjffe_planet_root_state_write_from_env(recs, "cpp");
    jjffe_subdiv_dump_876(recs, param_6, param_7, param_12, param_8, seed0, local10, local_c,
                          (int)DATA_009269);
    jjffe_planet_set_876_recs(recs);
    if (PlanetAb869TraceEnabled()) {
        for (int vi = 0; vi < 12; ++vi) {
            int32_t* cam = reinterpret_cast<int32_t*>(recs + vi * 0x48 + 4);
            PlanetAbNote876RootVert(vi, cam[14], cam[2]);
        }
    }
    if (getenv("AB_1F_SLOT_TRACE")) {
        int32_t* cam0 = reinterpret_cast<int32_t*>(recs + 4);
        fprintf(stderr, "SLOT_876 rec0_cam=(%d,%d,%d) local_c=%d msbC=%d DATA_009269=%u\n",
                (int)cam0[0], (int)cam0[1], (int)cam0[2], (int)local_c, (int)msbC,
                (unsigned)DATA_009269);
    }
    if (getenv("AB_1F_LOD_TRACE")) {
        int zmin=0x7fffffff, zmax=-(1<<30);
        for (int vi = 0; vi < 12; ++vi) {
            int32_t* cam = reinterpret_cast<int32_t*>(recs + vi * 0x48 + 4);
            if (cam[2] < zmin) zmin = cam[2];
            if (cam[2] > zmax) zmax = cam[2];
            fprintf(stderr, "LOD_ROOTZ i=%d xyz=(%d,%d,%d)\n", vi, cam[0], cam[1], cam[2]);
        }
        fprintf(stderr, "LOD_ROOTZ_RANGE zmin=%d zmax=%d thr22=%u\n", zmin, zmax, 1u<<22);
    }

    uintptr_t local18 = 0;
    s_876ProgEnd = FUNC_001823(&local18, (int16_t*)param_5);
    jjffe_planet_sat_note_876(local18);
    int local1c = (int)(local10 * 2 < param_6[2]);
    jjffe_planet_subdiv_note_876_local1c((int)local10, (int)param_6[2], local1c);
    uint32_t local20 = 0;

    if (setjmp(s_meshJmpBuf) != 0) {
        if (getenv("AB_1F_LEAF_TRACE") || getenv("AB_1F_DRAW_DEBUG"))
            fprintf(stderr, "icosa: LONGJMP pool abort\n");
        goto cleanup;
    }

    PlanetAbLoadConfig();
    PlanetAbResetFacePass();
    PlanetAbGetTrace()->active = true;
    PlanetAbGetTrace()->lodC = local_c;
    PlanetAbGetTrace()->lodMsb = msbC;

    FUNC_001816();
    for (int i = 0; i < 0x1e; ++i) {
        local988[i] = FUNC_001820();
        *(uintptr_t*)local988[i] = reinterpret_cast<uintptr_t>(recs + edgeIdx[i * 2] * 0x48);
    }
    {
        const void* slotPtrs[30];
        for (int i = 0; i < 0x1e; ++i)
            slotPtrs[i] = local988[i];
        jjffe_planet_subdiv_set_local988(slotPtrs, edgeIdx);
    }
    local20 = FUNC_001833(param_4, (uint32_t)local_c, param_8, param_12);

    if (param_12 == 1) {
        int32_t height = 0;
        if (PlanetCollisionDescent(recs, local988, faceIdx, local18, param_6, local1c, local20,
                                   &height)) {
            DATA_009283 = static_cast<uint32_t>(height);
            DATA_009284 = 0;
        }
    } else {
        const PlanetAbConfig& ab = PlanetAbGetConfig();
        for (int fi = 0; fi < 0x14; ++fi) {
            if (ab.traceFace >= 0 && fi != ab.traceFace)
                continue;
            int base = fi * 6;
            PlanetAbBeginFace(fi, local20, (uint32_t)local1c, param_12);
            jjffe_planet_subdiv_note_876_verts(
                fi, param_12,
                recs + faceIdx[base + 0] * 0x48,
                recs + faceIdx[base + 1] * 0x48,
                recs + faceIdx[base + 2] * 0x48);
            intptr_t satArg = 0;
            if (local18) {
                const uint32_t packed =
                    *reinterpret_cast<uint32_t*>(local18 + 8 + static_cast<size_t>(fi) * 4);
                if (packed)
                    satArg = reinterpret_cast<intptr_t>(PlanetSatResolve(
                        static_cast<int32_t>(packed)));
            }
            FUNC_001835(
                (intptr_t)(recs + faceIdx[base + 0] * 0x48),
                (intptr_t)(recs + faceIdx[base + 1] * 0x48),
                (intptr_t)(recs + faceIdx[base + 2] * 0x48),
                (intptr_t)local988[faceIdx[base + 3]],
                (intptr_t)local988[faceIdx[base + 4]],
                (intptr_t)local988[faceIdx[base + 5]],
                (uint32_t)local_c,
                satArg,
                (uint32_t)local1c, local20, param_12);
            PlanetAbSnapshotPools(s_pool816Used, (int)s_pool820Used);
        }
    }

cleanup:
    {
        static int s_dumped = 0;
        if (getenv("AB_1F_869_EARTH_DUMP") && !s_dumped && param_6) {
            static int s_gdbg = 0;
            if (s_gdbg < 10) {
                std::fprintf(stderr, "869-GATE entryZ=%d\n", entry_camz);
                ++s_gdbg;
            }
            if (!(entry_camz > 200000000 && entry_camz < 280000000))
                goto cleanup_dump_skip;
            {
            PlanetAbDump869CullTrace(getenv("AB_1F_869_EARTH_DUMP"));
            std::fprintf(stderr, "869-EARTH-DUMP done camZ=%d\n",
                         entry_camz);
            s_dumped = 1;
            }
cleanup_dump_skip:;
        }
    }
    jjffe_planet_set_876_recs(nullptr);
    FUNC_001817();
    if (param_12 != 0 && param_1) {
        if (DATA_009283 == 0x7fffffff || param_2 < (int)DATA_009283)
            *param_1 = param_2;
        else
            *param_1 = (int)DATA_009283;
    }
    return reinterpret_cast<intptr_t>(s_876ProgEnd);
}

} // namespace jjffe
