// ASM FUNC_001877 — planet shade globals (DATA_009262/263/265/267) before icosa draw.
// Float x87 path for column transform and camLen / (camLen-263) scale (263/267 tail).
#include "jjffe/data_globals.h"
#include "jjffe/data_loader.h"
#include "jjffe/planet_render.h"
#include "jjffe/planet_shade_trace.h"
#include "jjffe/video.h"
#include "data_blob.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jjffe {

// Forward declarations from core.cpp
uint32_t FUNC_001467(uint32_t);
int32_t FUNC_001521(int32_t, int32_t);
int32_t FUNC_001825(uint32_t*);
uint32_t FUNC_001479(uint16_t*, int32_t*);
int16_t FUNC_001913_Sin16(uint16_t);
void FUNC_001780(int, uint32_t);
uint32_t FUNC_001878(intptr_t, int*);
void FUNC_001879(intptr_t, const int16_t*, const int16_t*, const int16_t*,
                 const int16_t*, const int16_t*, const int16_t*, const uint8_t*, int32_t*);

namespace {

static int32_t s_col895[18];

static inline int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static int32_t isqrt723(int32_t sumSq)
{
    if (sumSq <= 0)
        return 0;
    return static_cast<int32_t>(FUNC_001467(static_cast<uint32_t>(sumSq)));
}

// FUNC_001521 → int32 (ASM ebx).
static int32_t mul521(int32_t a, int32_t b)
{
    return static_cast<int32_t>(FUNC_001521(a, b));
}

// ASM: eax = FUNC_001521(a,b); shl eax,16 — shift as unsigned before signed use.
static int32_t mul521Shift16(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(FUNC_001521(a, b)) << 16);
}

// ASM idiv path: (v << 15) / den with 32-bit shift wrap.
static int32_t shl15Div(int32_t v, int32_t den)
{
    if (den == 0)
        return 0;
    const int32_t num = static_cast<int32_t>(static_cast<uint32_t>(v) << 15);
    return num / den;
}

// x87 FISTP / FUNC_002095_fstore — chop toward zero (ASM sets RC=11 before fistp).
static int32_t fstoreFloat(float v)
{
    return static_cast<int32_t>(v);
}

static inline float fstp32(float v)
{
    volatile float f = v;
    return f;
}

// x87 fsubr mem: mem - st0
static inline float xfsubr(float mem, float st0)
{
    return fstp32(mem - st0);
}

static inline float xfdiv(float num, float den)
{
    return fstp32(num / den);
}

struct Perm877Slots {
    float s78;   // [ebp+0xffffff78] local_68 (row0.x)
    float s7c;   // [ebp+0xffffff7c] local_64 (row0.y)
    float s80;   // [ebp-0x80] local_60 (row0.z)
    float sm7c;  // [ebp-0x7c] local_5c (row1.x)
    float sm78;  // [ebp-0x78] local_58 (row1.y)
    float s74;   // [ebp-0x74] local_54 (row1.z)
    float s70;   // [ebp-0x70] local_50 (row2.x)
    float s6c;   // [ebp-0x6c] local_4c (row2.y)
    float s68;   // [ebp-0x68] local_48 (row2.z) divisor
    float s3c;   // [ebp+0xffffff3c] negCam0
    float s40;   // [ebp+0xffffff40] negCam1
    float s44;   // [ebp+0xffffff44] negCam2
};

// ASM 132297–132380: literal fild/fmul/fdiv/fstp slot sequence.
// All 9 col matrix entries — row2.x is local_50 (ebp-0x4c → ebp-0x70), not a
// duplicate of local_4c. Dropping local_50 made Contour axis (bc,b8,b4) flip
// with camera roll → Saturn-ring atmos vs limb band for A/D only.
static void solve877PermutationAsm(int32_t local_68, int32_t local_64, int32_t local_60,
                                   int32_t local_5c, int32_t local_58, int32_t local_54,
                                   int32_t local_50, int32_t local_4c, int32_t local_48,
                                   int32_t negCam0, int32_t negCam1, int32_t negCam2,
                                   int32_t* out_bc, int32_t* out_b8, int32_t* out_b4)
{
    Perm877Slots s{};
    s.s78 = fstp32(static_cast<float>(local_68));
    s.s7c = fstp32(static_cast<float>(local_64));
    s.s80 = fstp32(static_cast<float>(local_60));
    s.sm7c = fstp32(static_cast<float>(local_5c));
    s.sm78 = fstp32(static_cast<float>(local_58));
    s.s74 = fstp32(static_cast<float>(local_54));
    s.s70 = fstp32(static_cast<float>(local_50));
    s.s6c = fstp32(static_cast<float>(local_4c));
    s.s68 = fstp32(static_cast<float>(local_48));
    s.s3c = fstp32(static_cast<float>(negCam0));
    s.s40 = fstp32(static_cast<float>(negCam1));
    s.s44 = fstp32(static_cast<float>(negCam2));

    s.s80 = xfdiv(s.s80, s.s68);
    s.s74 = xfdiv(s.s74, s.s68);
    s.s44 = xfdiv(s.s44, s.s68);

    s.s7c = xfsubr(s.s7c, fstp32(s.s80 * s.s6c));
    s.s78 = xfsubr(s.s78, fstp32(s.s80 * s.s70));
    s.sm78 = xfsubr(s.sm78, fstp32(s.s74 * s.s6c));
    s.sm7c = xfsubr(s.sm7c, fstp32(s.s74 * s.s70));
    s.s40 = xfsubr(s.s40, fstp32(s.s44 * s.s6c));
    s.s3c = xfsubr(s.s3c, fstp32(s.s44 * s.s70));

    s.s7c = xfdiv(s.s7c, s.sm78);
    s.s40 = xfdiv(s.s40, s.sm78);

    s.s78 = xfsubr(s.s78, fstp32(s.s7c * s.sm7c));
    s.s3c = xfsubr(s.s3c, fstp32(s.s40 * s.sm7c));

    s.s3c = xfdiv(s.s3c, s.s78);
    s.s40 = xfsubr(s.s40, fstp32(s.s3c * s.s7c));
    s.s44 = xfsubr(s.s44, fstp32(s.s3c * s.s80));
    s.s44 = xfsubr(s.s44, fstp32(s.s40 * s.s74));

    constexpr float kData000051 = 32768.0f;
    *out_bc = fstoreFloat(s.s3c * kData000051);
    *out_b8 = fstoreFloat(s.s40 * kData000051);
    *out_b4 = fstoreFloat(s.s44 * kData000051);
}

void init877ColumnTable(const uint8_t* blob)
{
    if (!blob)
        return;
    std::memcpy(s_col895, blob + data::OFFSET_DATA_007895, sizeof(s_col895));
}

// ASM FUNC_001877 subdiv tail (ffe.asm 132333–133263): tangent frame, parametric
// sphere quad loop, FUNC_001879 dispatch. shadeR/G/B are the 57/58-path end shade.
static void planet877Tail(intptr_t ctx, const int32_t* camOff, uint32_t meshWord0,
                          int32_t param_5, int32_t shift38, int32_t iVar6,
                          int32_t local_2c, int32_t bc, int32_t b8, int32_t b4,
                          const int32_t col[9], uint8_t shadeR, uint8_t shadeG,
                          uint8_t shadeB, int32_t ebx58)
{
    // Raw args before tangent-frame / bc renormalisation (R1222 probe).
    const int32_t raw_bc = bc, raw_b8 = b8, raw_b4 = b4;

    // --- tangent frame (132333–132450) ---
    int32_t a0, a1, a2;
    if (abs32(bc) + abs32(b8) <= 0x1f4) {
        a0 = 0x8000; a1 = 0; a2 = 0;
    } else {
        const int32_t len = isqrt723(b8 * b8 + bc * bc);
        a0 = ((int32_t)((uint32_t)b8 << 15)) / len;
        a1 = ((int32_t)((uint32_t)(-bc) << 15)) / len;
        a2 = 0;
    }
    int32_t b0 = (a1 * b4 - a2 * b8) >> 15;
    int32_t b1 = (a2 * bc - a0 * b4) >> 15;
    int32_t b2 = (a0 * b8 - a1 * bc) >> 15;
    const int32_t raw_a0 = a0, raw_a1 = a1, raw_a2 = a2;
    const int32_t raw_b0 = b0, raw_b1 = b1, raw_b2 = b2;
    const int32_t normFactor =
        isqrt723(local_2c - static_cast<int32_t>(0x40000000u >> ((shift38 * 2u) & 0x1fu)));
    const int32_t denomA = iVar6 + 1;
    a0 = (a0 * normFactor) / denomA;
    a1 = (a1 * normFactor) / denomA;
    a2 = (a2 * normFactor) / denomA;
    const int32_t denomB = (local_2c >> 15) + 1;
    b0 = (b0 * normFactor) / denomB;
    b1 = (b1 * normFactor) / denomB;
    b2 = (b2 * normFactor) / denomB;

    // --- renormalise bc/b8/b4 (132741–132780) ---
    const int rs = (15 - shift38) & 0x1f;
    const int32_t denom0 = (local_2c >> rs) + 1;
    bc = ((int32_t)((uint32_t)bc << 15)) / denom0;
    b8 = ((int32_t)((uint32_t)b8 << 15)) / denom0;
    b4 = ((int32_t)((uint32_t)b4 << 15)) / denom0;

    // --- 263==0 block: basis → FUNC_001878 → shade colour adjust (132781–132925) ---
    // R1219: AB_DPAL_BPLUS_PROBE also dumps shadeB decision inputs here (once-latched).
    struct ShadeBProbe {
        int on;
        unsigned flip0, flip1;
        int cap;
        int logs;
    };
    static ShadeBProbe s_sb = {-1, 0, 0, 0, 0};
    if (s_sb.on < 0) {
        const char* e = std::getenv("AB_DPAL_BPLUS_PROBE");
        s_sb.on = (e && e[0] == '1') ? 1 : 0;
        s_sb.flip0 = 265;
        s_sb.flip1 = 275;
        s_sb.cap = 80;
        if (const char* p = std::getenv("AB_DPAL_BPLUS_FLIP0"))
            if (p[0]) s_sb.flip0 = static_cast<unsigned>(std::atoi(p));
        if (const char* p = std::getenv("AB_DPAL_BPLUS_FLIP1"))
            if (p[0]) s_sb.flip1 = static_cast<unsigned>(std::atoi(p));
        if (const char* p = std::getenv("AB_DPAL_BPLUS_CAP"))
            if (p[0]) s_sb.cap = std::atoi(p);
    }
    const uint32_t sbFlip = g_videoState ? g_videoState->flipCount : 0;
    const bool sbWin =
        s_sb.on && s_sb.logs < s_sb.cap && sbFlip >= s_sb.flip0 && sbFlip <= s_sb.flip1;
    const uint8_t in814 = static_cast<uint8_t>(DATA_008814 & 0xffu);
    const uint8_t in815 = DATA_008815;
    const uint8_t in816 = DATA_008816;
    const uint8_t in817 = static_cast<uint8_t>(DATA_008817 & 0xffu);
    const uint8_t shInR = shadeR, shInG = shadeG, shInB = shadeB;
    uint32_t nshade = 0;
    // path: 0=skip(d263!=0/57), 1=nshade0→cache, 2=nshade adjust+writeback
    int shadePath = 0;
    if (DATA_009263 == 0) {
        // ASM shl 15 on the raw words; shift the unsigned view (UBSan: -20250).
        const int32_t b0x = static_cast<int32_t>(static_cast<uint32_t>(bc) << 15);
        const int32_t b0y = static_cast<int32_t>(static_cast<uint32_t>(b8) << 15);
        const int32_t b0z = static_cast<int32_t>(static_cast<uint32_t>(b4) << 15);
        int32_t n0[3];
        n0[0] = (mul521(b0x, col[0]) + mul521(b0y, col[3]) + mul521(b0z, col[6])) * 2;
        n0[1] = (mul521(b0x, col[1]) + mul521(b0y, col[4]) + mul521(b0z, col[7])) * 2;
        n0[2] = (mul521(b0x, col[2]) + mul521(b0y, col[5]) + mul521(b0z, col[8])) * 2;
        nshade = FUNC_001878(ctx, n0);
        if (nshade == 0) {
            // ffe.asm 133056: mov ax,[DATA_008815] — R and G as one word.
            shadeR = DATA_008815;
            shadeG = DATA_008816;
            shadeB = static_cast<uint8_t>(DATA_008817 & 0xffu);
            shadePath = 1;
        } else {
            int32_t t;
            t = static_cast<int32_t>(nshade & 0xffu) * (0x8000 - ebx58);
            if (t < 0) t += 0x3fff;
            shadeR = static_cast<uint8_t>(shadeR + (t >> 14));
            t = static_cast<int32_t>((nshade >> 8) & 0xffu) * (0x8000 - ebx58);
            if (t < 0) t += 0x3fff;
            shadeG = static_cast<uint8_t>(shadeG + (t >> 14));
            t = static_cast<int32_t>((nshade >> 16) & 0xffu) * (0x8000 - ebx58);
            if (t < 0) t += 0x3fff;
            shadeB = static_cast<uint8_t>(shadeB + (t >> 14));
            if (shadeR > 0xf) shadeR = 0xf;
            if (shadeG > 0xf) shadeG = 0xf;
            if (shadeB > 0xf) shadeB = 0xf;
            DATA_008814 = 0xffffffffu;
            DATA_008815 = shadeR;
            DATA_008816 = shadeG;
            DATA_008817 = shadeB;
            shadePath = 2;
        }
    }
    if (sbWin) {
        std::fprintf(
            stderr,
            "R1219_877_SHADE n=%d flip=%u d263=%u ebx58=%d path=%d "
            "in814=%02x in815=%02x in816=%02x in817=%02x "
            "shIn=%02x%02x%02x nshade=0x%x "
            "adj=%02x%02x%02x shadeB=%u out817=%02x\n",
            s_sb.logs, static_cast<unsigned>(sbFlip),
            static_cast<unsigned>(DATA_009263), static_cast<int>(ebx58), shadePath,
            static_cast<unsigned>(in814), static_cast<unsigned>(in815),
            static_cast<unsigned>(in816), static_cast<unsigned>(in817),
            static_cast<unsigned>(shInR), static_cast<unsigned>(shInG),
            static_cast<unsigned>(shInB), static_cast<unsigned>(nshade),
            static_cast<unsigned>(shadeR), static_cast<unsigned>(shadeG),
            static_cast<unsigned>(shadeB), static_cast<unsigned>(shadeB),
            static_cast<unsigned>(DATA_008817 & 0xffu));
        ++s_sb.logs;
    }

    FUNC_001780(1, 0xffffffffu);

    // --- shadeTable gradient (132932–132977) ---
    uint8_t shadeTable[7][4] = {};
    const uint8_t m0 = static_cast<uint8_t>(meshWord0 & 0xffu);
    const uint8_t m1 = static_cast<uint8_t>((meshWord0 >> 8) & 0xffu);
    const uint8_t m2 = static_cast<uint8_t>((meshWord0 >> 16) & 0xffu);
    for (int i = 0; i < 7; i++) {
        shadeTable[i][0] = static_cast<uint8_t>((m0 * (6 - i) + shadeR * i) / 6);
        shadeTable[i][1] = static_cast<uint8_t>((m1 * (6 - i) + shadeG * i) / 6);
        shadeTable[i][2] = static_cast<uint8_t>((m2 * (6 - i) + shadeB * i) / 6);
    }

    // R1222: dump mesh-basis inputs once per 877Tail in a flip window (ordered
    // stream / rotation-input dig). Default off; once-latched getenv.
    {
        struct MeshInProbe {
            int on;
            unsigned flip0, flip1;
            int cap;
            int logs;
        };
        static MeshInProbe s_mi = {-1, 0, 0, 0, 0};
        if (s_mi.on < 0) {
            const char* e = std::getenv("R1222_877_MESH");
            s_mi.on = (e && e[0] == '1') ? 1 : 0;
            s_mi.flip0 = 470;
            s_mi.flip1 = 490;
            s_mi.cap = 64;
            if (const char* p = std::getenv("R1222_877_FLIP0"))
                if (p[0]) s_mi.flip0 = static_cast<unsigned>(std::atoi(p));
            if (const char* p = std::getenv("R1222_877_FLIP1"))
                if (p[0]) s_mi.flip1 = static_cast<unsigned>(std::atoi(p));
            if (const char* p = std::getenv("R1222_877_CAP"))
                if (p[0]) s_mi.cap = std::atoi(p);
        }
        const uint32_t miFlip = g_videoState ? g_videoState->flipCount : 0;
        if (s_mi.on && s_mi.logs < s_mi.cap && miFlip >= s_mi.flip0 &&
            miFlip <= s_mi.flip1) {
            std::fprintf(
                stderr,
                "R1222_877_MESH n=%d flip=%u clock=0x%x d263=%u path=%d "
                "param5=0x%x shift38=%d iVar6=%d local_2c=%d ebx58=%d "
                "raw_bc=%d raw_b8=%d raw_b4=%d "
                "raw_a0=%d raw_a1=%d raw_a2=%d raw_b0=%d raw_b1=%d raw_b2=%d "
                "bc=%d b8=%d b4=%d "
                "a0=%d a1=%d a2=%d b0=%d b1=%d b2=%d "
                "cam=%d,%d,%d "
                "col=[%d %d %d %d %d %d %d %d %d]\n",
                s_mi.logs, static_cast<unsigned>(miFlip),
                static_cast<unsigned>(
                    *reinterpret_cast<const uint32_t*>(static_cast<const void*>(g_bss8804))),
                static_cast<unsigned>(DATA_009263), shadePath,
                static_cast<unsigned>(param_5), static_cast<int>(shift38),
                static_cast<int>(iVar6), static_cast<int>(local_2c),
                static_cast<int>(ebx58), static_cast<int>(raw_bc),
                static_cast<int>(raw_b8), static_cast<int>(raw_b4),
                static_cast<int>(raw_a0), static_cast<int>(raw_a1),
                static_cast<int>(raw_a2), static_cast<int>(raw_b0),
                static_cast<int>(raw_b1), static_cast<int>(raw_b2),
                static_cast<int>(bc), static_cast<int>(b8), static_cast<int>(b4),
                static_cast<int>(a0), static_cast<int>(a1), static_cast<int>(a2),
                static_cast<int>(b0), static_cast<int>(b1), static_cast<int>(b2),
                camOff ? camOff[0] : 0, camOff ? camOff[1] : 0,
                camOff ? camOff[2] : 0, col[0], col[1], col[2], col[3], col[4],
                col[5], col[6], col[7], col[8]);
            ++s_mi.logs;
        }
    }

    // --- parametric sphere loop (132978–133263) ---
    int16_t last[3][2] = {};
    int16_t first[3][2] = {};
    int32_t lastTx[3] = {0, 0, 0};
    for (int ebx = 0; ebx < 0x10000; ebx += 0x800) {
        const int32_t sAng = static_cast<int32_t>(FUNC_001913_Sin16(static_cast<uint16_t>(ebx + 0x4000)));
        const int32_t sLat = static_cast<int32_t>(FUNC_001913_Sin16(static_cast<uint16_t>(ebx)));
        const int32_t vx = sAng * a0 + sLat * b0 + ((int32_t)((uint32_t)bc << 15));
        const int32_t vy = sAng * a1 + sLat * b1 + ((int32_t)((uint32_t)b8 << 15));
        const int32_t vz = sAng * a2 + sLat * b2 + ((int32_t)((uint32_t)b4 << 15));

        const int32_t tx0 = (mul521(vx, col[0]) + mul521(vy, col[3]) + mul521(vz, col[6])) * 2;
        const int32_t tx1 = (mul521(vx, col[1]) + mul521(vy, col[4]) + mul521(vz, col[7])) * 2;
        const int32_t tx2 = (mul521(vx, col[2]) + mul521(vy, col[5]) + mul521(vz, col[8])) * 2;
        lastTx[0] = tx0; lastTx[1] = tx1; lastTx[2] = tx2;

        int16_t proj0[2] = {0, 0};
        int16_t proj1[2] = {0, 0};
        int16_t proj2[2] = {0, 0};
        int32_t pt[3];

        pt[0] = tx0 + camOff[0];
        pt[1] = tx1 + camOff[1];
        pt[2] = tx2 + camOff[2];
        if (FUNC_001479(reinterpret_cast<uint16_t*>(proj0), pt)) {
            proj0[0] = proj0[1] = static_cast<int16_t>(0x8002);
        }

        pt[0] = mul521(tx0, param_5) + tx0 + camOff[0];
        pt[1] = mul521(tx1, param_5) + tx1 + camOff[1];
        pt[2] = mul521(tx2, param_5) + tx2 + camOff[2];
        if (FUNC_001479(reinterpret_cast<uint16_t*>(proj1), pt)) {
            proj1[0] = proj1[1] = static_cast<int16_t>(0x8002);
        }

        pt[0] = tx0 - (tx0 >> 3) + camOff[0];
        pt[1] = tx1 - (tx1 >> 3) + camOff[1];
        pt[2] = tx2 - (tx2 >> 3) + camOff[2];
        if (FUNC_001479(reinterpret_cast<uint16_t*>(proj2), pt)) {
            proj2[0] = proj2[1] = static_cast<int16_t>(0x8002);
        }

        if (ebx == 0) {
            first[0][0] = proj2[0]; first[0][1] = proj2[1];
            first[1][0] = proj0[0]; first[1][1] = proj0[1];
            first[2][0] = proj1[0]; first[2][1] = proj1[1];
        } else {
            FUNC_001879(ctx, last[0], last[1], last[2], proj2, proj0, proj1,
                        &shadeTable[0][0], lastTx);
        }

        last[0][0] = proj2[0]; last[0][1] = proj2[1];
        last[1][0] = proj0[0]; last[1][1] = proj0[1];
        last[2][0] = proj1[0]; last[2][1] = proj1[1];
    }

    // final closing quad (133224–133243)
    FUNC_001879(ctx, first[0], first[1], first[2], last[0], last[1], last[2],
                &shadeTable[0][0], lastTx);
    FUNC_001780(0, 0);
}

} // namespace

void InitPlanet877ColumnTable()
{
    init877ColumnTable(data::GetLoadedBlob());
}

void FUNC_001877(intptr_t ctx, int* camOff, int* colTri, uint32_t meshWord0, int param_5,
                 int param_6)
{
    (void)ctx;

    DATA_009262 = static_cast<uint32_t>(param_5);
    DATA_009267 = static_cast<uint32_t>(param_6);
    if (param_5 == 0)
        return;

    // ASM: mov [DATA_009265],ax / mov [DATA_009266],al — RG word + B byte are
    // contiguous, so dword reads of DATA_009265 see the full 24-bit mesh colour.
    DATA_009265 = meshWord0 & 0xffffffu;
    DATA_009263 = 0;

    if (!colTri || !camOff)
        return;

    int32_t local_68 = 0;
    int32_t local_64 = 0;
    int32_t local_60 = 0;
    int32_t local_5c = 0;
    int32_t local_58 = 0;
    int32_t local_54 = 0;
    int32_t local_50 = 0;
    int32_t local_4c = 0;
    int32_t local_48 = 0;

    const int32_t* pi = s_col895;
    for (int i = 0; i < 6; ++i) {
        const int idx0 = pi[0];
        const int32_t c0 = colTri[idx0 * 3 + 0];
        if (abs32(local_68) < abs32(c0)) {
            const int idx1 = pi[1];
            const int32_t c1 = colTri[idx1 * 3 + 1];
            if (abs32(local_58) < abs32(c1)) {
                const int idx2 = pi[2];
                const int32_t c2 = colTri[idx2 * 3 + 2];
                if (abs32(local_48) < abs32(c2)) {
                    local_68 = colTri[idx0 * 3 + 0];
                    local_64 = colTri[idx0 * 3 + 1];
                    local_60 = colTri[idx0 * 3 + 2];
                    local_5c = colTri[idx1 * 3 + 0];
                    local_58 = colTri[idx1 * 3 + 1];
                    local_54 = colTri[idx1 * 3 + 2];
                    local_50 = colTri[idx2 * 3 + 0];
                    local_4c = colTri[idx2 * 3 + 1];
                    local_48 = colTri[idx2 * 3 + 2];
                }
            }
        }
        pi += 3;
    }

    const int32_t negCam0 = -camOff[0];
    const int32_t negCam1 = -camOff[1];
    const int32_t negCam2 = -camOff[2];

    if (local_68 == 0)
        return;

    int32_t local_b0 = local_68;
    int32_t local_ac = local_64;
    int32_t local_a8 = local_60;
    int32_t local_a4 = local_5c;
    int32_t local_a0 = local_58;
    int32_t local_9c = local_54;
    int32_t local_98 = local_50;
    int32_t local_94 = local_4c;
    int32_t local_90 = local_48;

    int32_t local_bc = 0;
    int32_t local_b8 = 0;
    int32_t local_b4 = 0;
    solve877PermutationAsm(local_68, local_64, local_60, local_5c, local_58, local_54,
                           local_50, local_4c, local_48, negCam0, negCam1, negCam2,
                           &local_bc, &local_b8, &local_b4);
    if (getenv("AB_1F_877_PREFSTORE_TRACE"))
        jjffe_planet_877_prefstore_v("cpp840", local_bc, local_b8, local_b4);
    if (getenv("AB_1F_877_TRACE")) {
        std::fprintf(stderr, "877 cpp prefstore: bc=%d b8=%d b4=%d\n", local_bc, local_b8, local_b4);
    }

    // ASM exits (132421, 132441, 132866) clear DATA_009262 / DATA_009263 only;
    // DATA_009265 keeps the mesh colour stored at entry (132188-132190).
    if (abs32(local_bc) + abs32(local_b8) + abs32(local_b4) >= 0x800000) {
        DATA_009262 = 0;
        return;
    }

    // FUNC_001825 reads three contiguous dwords — must not pass &separate_locals.
    int32_t bvec[3] = {local_bc, local_b8, local_b4};
    int32_t local_38 = FUNC_001825(reinterpret_cast<uint32_t*>(bvec)) - 0xd;
    if (local_38 < 1) {
        DATA_009262 = 0;
        return;
    }

    const uint8_t shift38 = static_cast<uint8_t>(local_38);
    local_bc = bvec[0] >> (shift38 & 0x1f);
    local_b8 = bvec[1] >> (shift38 & 0x1f);
    local_b4 = bvec[2] >> (shift38 & 0x1f);

    int32_t local_2c = local_bc * local_bc + local_b8 * local_b8 + local_b4 * local_b4;
    int32_t iVar6 = isqrt723(local_2c);
    if ((iVar6 << (shift38 & 0x1f)) < 0x8800) {
        iVar6 <<= (shift38 & 0x1f);
        local_bc = (local_bc * 0x8800) / iVar6;
        local_b8 = (local_b8 * 0x8800) / iVar6;
        local_b4 = (local_b4 * 0x8800) / iVar6;
        local_2c = static_cast<int32_t>(0x48400000u >> ((shift38 * 2u) & 0x1fu));
        iVar6 = 0x8800 >> (shift38 & 0x1f);
    }

    int32_t iVar3 = local_2c - static_cast<int32_t>(0x40000000u >> ((shift38 * 2u) & 0x1fu));
    if (iVar3 < 0)
        return;

    const uint32_t gateLeft = static_cast<uint32_t>(iVar6 << (shift38 & 0x1f));
    const uint32_t gateRight = static_cast<uint32_t>((param_5 >> 16) + 0x8000);
    if (gateLeft <= gateRight) {
        DATA_009263 = 0;
        if (getenv("AB_1F_877_TRACE")) {
            jjffe_planet_877_trace_emit("cpp", 58, 0, 0, static_cast<int32_t>(DATA_009267),
                                        param_6, static_cast<uint32_t>(param_5), gateLeft,
                                        gateRight);
        }
        if (getenv("AB_1F_D265_TRACE"))
            jjffe_planet_d265_trace_v("cpp840", "877_exit58", DATA_009262, DATA_009265);

        // 58-path shade colour (ffe.asm 132703–132740)
        uint32_t u58 = static_cast<uint32_t>(iVar6 << (shift38 & 0x1f));
        u58 -= 0x8000u;
        u58 <<= 15;
        const int32_t ebx58 = static_cast<int32_t>(u58) / (param_5 >> 16);
        uint8_t shR = static_cast<uint8_t>(
            ((0x8000 - ebx58) * static_cast<int32_t>(meshWord0 & 0xffu) * 3) >> 15);
        uint8_t shG = static_cast<uint8_t>(
            ((0x8000 - ebx58) * static_cast<int32_t>((meshWord0 >> 8) & 0xffu) * 3) >> 15);
        uint8_t shB = static_cast<uint8_t>(
            (((0x8000 - ebx58) * static_cast<int32_t>((meshWord0 >> 16) & 0xffu) * 3) +
             ebx58 * 4) >> 15);

        const int32_t col[9] = {local_b0, local_ac, local_a8, local_a4, local_a0,
                                local_9c, local_98, local_94, local_90};
        planet877Tail(ctx, camOff, meshWord0, param_5, local_38, iVar6, local_2c,
                      local_bc, local_b8, local_b4, col, shR, shG, shB, ebx58);
        return;
    }

    const int32_t halfParam5 = (param_5 >> 1) + 0x40000000;
    const int32_t scaleEc = mul521Shift16(shl15Div(local_bc, iVar6), halfParam5);
    const int32_t scaleE8 = mul521Shift16(shl15Div(local_b8, iVar6), halfParam5);
    const int32_t scaleE4 = mul521Shift16(shl15Div(local_b4, iVar6), halfParam5);

    // ASM keeps span/cam as contiguous stack dwords for FUNC_001825.
    int32_t spanVec[3];
    spanVec[0] = (mul521(local_b0, scaleEc) + mul521(local_a4, scaleE8) +
                  mul521(local_98, scaleE4)) *
                     2 +
                 camOff[0];
    spanVec[1] = (mul521(local_ac, scaleEc) + mul521(local_a0, scaleE8) +
                  mul521(local_94, scaleE4)) *
                     2 +
                 camOff[1];
    spanVec[2] = (mul521(local_a8, scaleEc) + mul521(local_9c, scaleE8) +
                  mul521(local_90, scaleE4)) *
                     2 +
                 camOff[2];

    int32_t spanShift = FUNC_001825(reinterpret_cast<uint32_t*>(spanVec)) - 0xd;
    if (spanShift < 1)
        spanShift = 0;
    else {
        const uint8_t ss = static_cast<uint8_t>(spanShift);
        spanVec[0] >>= (ss & 0x1f);
        spanVec[1] >>= (ss & 0x1f);
        spanVec[2] >>= (ss & 0x1f);
    }
    const int32_t spanLen =
        isqrt723(spanVec[0] * spanVec[0] + spanVec[1] * spanVec[1] + spanVec[2] * spanVec[2])
        << (static_cast<uint8_t>(spanShift) & 0x1f);
    DATA_009263 = static_cast<uint32_t>(spanLen);

    int32_t camVec[3] = {camOff[0], camOff[1], camOff[2]};
    int32_t camShift = FUNC_001825(reinterpret_cast<uint32_t*>(camVec)) - 0xd;
    if (camShift < 1)
        camShift = 0;
    else {
        const uint8_t cs = static_cast<uint8_t>(camShift);
        camVec[0] >>= (cs & 0x1f);
        camVec[1] >>= (cs & 0x1f);
        camVec[2] >>= (cs & 0x1f);
    }
    const int32_t camSumSq =
        camVec[0] * camVec[0] + camVec[1] * camVec[1] + camVec[2] * camVec[2];
    const int32_t camIsqt = isqrt723(camSumSq);
    const int32_t camLen = camIsqt << (static_cast<uint8_t>(camShift) & 0x1f);
    if (getenv("AB_1F_877_CAMOFF_TRACE"))
        jjffe_planet_877_camoff_trace_v("cpp", camOff[0], camOff[1], camOff[2],
                                        camShift, camSumSq, camIsqt, camLen);

    // ASM: the x87 computes this in 80-bit extended precision before the
    // FISTP chop — plain float math loses the low bits (d267 off by one).
    const double fNum = static_cast<double>(param_6) * static_cast<double>(camLen);
    const double fDen = static_cast<double>(camLen - static_cast<int32_t>(DATA_009263));
    int32_t new267 = static_cast<int32_t>(fNum / fDen);
    if (new267 < 1)
        new267 = 1;
    DATA_009267 = static_cast<uint32_t>(new267);

    if (getenv("AB_1F_877_TRACE")) {
        std::fprintf(stderr,
                     "877 cpp span: iVar6=%d bc=%d b8=%d b4=%d shift38=%d spanShift=%d "
                     "spanLen=%u camLen=%d\n",
                     iVar6, local_bc, local_b8, local_b4, local_38, spanShift,
                     static_cast<unsigned>(DATA_009263), camLen);
        jjffe_planet_877_trace_emit("cpp", 57, static_cast<int32_t>(DATA_009263), camLen, new267,
                                    param_6, static_cast<uint32_t>(param_5), gateLeft, gateRight);
    }
    if (getenv("AB_1F_D265_TRACE"))
        jjffe_planet_d265_trace_v("cpp840", "877_exit57", DATA_009262, DATA_009265);

    // 57-path shade colour (ffe.asm 132675–132678): cached DATA_008815/008817
    const uint8_t shR = DATA_008815;
    const uint8_t shG = DATA_008816;
    const uint8_t shB = static_cast<uint8_t>(DATA_008817 & 0xffu);
    const int32_t col[9] = {local_b0, local_ac, local_a8, local_a4, local_a0,
                            local_9c, local_98, local_94, local_90};
    planet877Tail(ctx, camOff, meshWord0, param_5, local_38, iVar6, local_2c,
                  local_bc, local_b8, local_b4, col, shR, shG, shB, 0);
}

} // namespace jjffe
