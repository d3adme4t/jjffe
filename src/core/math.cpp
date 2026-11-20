#include "jjffe/math.h"
#include "jjffe/data.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace jjffe {

// Vec64 operations

int Vec64Truncate(Vec64* v, int bits)
{
    int msb = Vec64FindMSB(v);
    int shift = msb - bits;
    if (shift < 1) return 0;
    Vec64Shift(v, shift);
    return shift;
}

void Vec64Shift(Vec64* v, int shift)
{
    if (shift > 0) {
        v->x >>= shift;
        v->y >>= shift;
        v->z >>= shift;
    } else if (shift < 0) {
        v->x <<= (-shift);
        v->y <<= (-shift);
        v->z <<= (-shift);
    }
}

void Vec64Add(Vec64* dst, const Vec64* a, const Vec64* b)
{
    dst->x = a->x + b->x;
    dst->y = a->y + b->y;
    dst->z = a->z + b->z;
}

void Vec64Sub(Vec64* dst, const Vec64* a, const Vec64* b)
{
    dst->x = a->x - b->x;
    dst->y = a->y - b->y;
    dst->z = a->z - b->z;
}

void Vec64NegAssign(Vec64* v)
{
    v->x = -v->x;
    v->y = -v->y;
    v->z = -v->z;
}

void Vec64Abs(Vec64* v)
{
    v->x = std::abs(v->x);
    v->y = std::abs(v->y);
    v->z = std::abs(v->z);
}

int Vec64FindMSB(const Vec64* v)
{
    const uint32_t* p = (const uint32_t*)v;
    bool small = ((p[1] == 0 || p[1] == 0xffffffff) &&
                  (p[3] == 0 || p[3] == 0xffffffff) &&
                  (p[5] == 0 || p[5] == 0xffffffff));
    uint32_t a, b, c;
    int base;
    if (small) {
        a = (int32_t)(p[1]) < 0 ? (uint32_t)(-(int32_t)p[0]) : p[0];
        b = (int32_t)(p[3]) < 0 ? (uint32_t)(-(int32_t)p[2]) : p[2];
        c = (int32_t)(p[5]) < 0 ? (uint32_t)(-(int32_t)p[4]) : p[4];
        base = 0;
    } else {
        a = (int32_t)(p[1]) < 0 ? (uint32_t)(-(int32_t)p[1]) : p[1];
        b = (int32_t)(p[3]) < 0 ? (uint32_t)(-(int32_t)p[3]) : p[3];
        c = (int32_t)(p[5]) < 0 ? (uint32_t)(-(int32_t)p[5]) : p[5];
        base = 32;
    }
    uint32_t val = a | b | c;
    int msb = base;
    if (val > 0xffff) { val >>= 16; msb += 16; }
    if (val > 0xff)   { val >>= 8;  msb += 8; }
    if (val > 0xf)    { val >>= 4;  msb += 4; }
    if (val > 3)      { val >>= 2;  msb += 2; }
    if (val > 1)      { msb += 1; }
    return msb;
}

// Vec32 operations

void SetIntVec(int32_t* vec, int32_t x, int32_t y, int32_t z)
{
    vec[0] = x;
    vec[1] = y;
    vec[2] = z;
}

void GetIntVec(const int32_t* vec, int32_t* x, int32_t* y, int32_t* z)
{
    *x = vec[0];
    *y = vec[1];
    *z = vec[2];
}

// Vector-Matrix multiplication (fixed-point 16.16)
// dst = v * m  (1x3 * 3x3)
void VecMatMul(Vec32* dst, const Vec32* v, const Mat33* m)
{
    Vec32 src = *v;
    // Row-vector * Matrix: result[i] = sum(v[j] * m[j*3+i])
    dst->x = ((int64_t)src.x * m->m[0] + (int64_t)src.y * m->m[3] + (int64_t)src.z * m->m[6]) >> 16;
    dst->y = ((int64_t)src.x * m->m[1] + (int64_t)src.y * m->m[4] + (int64_t)src.z * m->m[7]) >> 16;
    dst->z = ((int64_t)src.x * m->m[2] + (int64_t)src.y * m->m[5] + (int64_t)src.z * m->m[8]) >> 16;
}

// Vector-TransposeMatrix multiplication
// dst = v * m^T
void VecMatTMul(Vec32* dst, const Vec32* v, const Mat33* m)
{
    Vec32 src = *v;
    dst->x = ((int64_t)src.x * m->m[0] + (int64_t)src.y * m->m[1] + (int64_t)src.z * m->m[2]) >> 16;
    dst->y = ((int64_t)src.x * m->m[3] + (int64_t)src.y * m->m[4] + (int64_t)src.z * m->m[5]) >> 16;
    dst->z = ((int64_t)src.x * m->m[6] + (int64_t)src.y * m->m[7] + (int64_t)src.z * m->m[8]) >> 16;
}

// Special vector-matrix multiplication — FUNC_001671 (FixedMulHigh per term).
// Used with MatBuildYZT Sin32-scale matrices (~Q31), NOT 16.16.
void SpecialVecMatMul(Vec32* dst, const Vec32* v, const Mat33* m)
{
    Vec32 src = *v;
    int32_t t = FixedMulHigh(src.x, m->m[0]);
    dst->x = t + FixedMulHigh(src.y, m->m[3]) + FixedMulHigh(src.z, m->m[6]);
    t = FixedMulHigh(src.x, m->m[1]);
    dst->y = t + FixedMulHigh(src.y, m->m[4]) + FixedMulHigh(src.z, m->m[7]);
    t = FixedMulHigh(src.x, m->m[2]);
    dst->z = t + FixedMulHigh(src.y, m->m[5]) + FixedMulHigh(src.z, m->m[8]);
}

// Matrix-Matrix multiplication (3x3)
void MatMatMul(Mat33* dst, const Mat33* a, const Mat33* b)
{
    Mat33 result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            // ASM: three 32-bit imuls summed in 64 bits then sar 16. The sum can
            // leave int64 range for extreme matrices, so accumulate unsigned.
            const uint64_t sum =
                static_cast<uint64_t>(static_cast<int64_t>(a->m[i * 3 + 0]) * b->m[0 * 3 + j]) +
                static_cast<uint64_t>(static_cast<int64_t>(a->m[i * 3 + 1]) * b->m[1 * 3 + j]) +
                static_cast<uint64_t>(static_cast<int64_t>(a->m[i * 3 + 2]) * b->m[2 * 3 + j]);
            result.m[i * 3 + j] = static_cast<int32_t>(static_cast<int64_t>(sum) >> 16);
        }
    }
    *dst = result;
}

// Matrix-Matrix transpose multiply
void MatMatTMul(Mat33* dst, const Mat33* a, const Mat33* b)
{
    Mat33 result;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.m[i * 3 + j] = (
                (int64_t)a->m[i * 3 + 0] * b->m[j * 3 + 0] +
                (int64_t)a->m[i * 3 + 1] * b->m[j * 3 + 1] +
                (int64_t)a->m[i * 3 + 2] * b->m[j * 3 + 2]
            ) >> 16;
        }
    }
    *dst = result;
}

void MatAssign(Mat33* dst, const Mat33* src)
{
    dst->m[0] = src->m[0];
    dst->m[4] = src->m[4];
    dst->m[8] = src->m[8];

    int32_t tmp;
    tmp = src->m[1];
    dst->m[1] = src->m[3];
    dst->m[3] = tmp;
    tmp = src->m[2];
    dst->m[2] = src->m[6];
    dst->m[6] = tmp;
    tmp = src->m[5];
    dst->m[5] = src->m[7];
    dst->m[7] = tmp;
}

void MatBuildOdd(Mat33* dst)
{
    // Build odd-parity transformation matrix
    std::memset(dst, 0, sizeof(Mat33));
    dst->m[0] = -FP_ONE;  // -1
    dst->m[4] = FP_ONE;   // +1 (to maintain right-handedness with odd parity)
    dst->m[8] = -FP_ONE;  // -1
}

void MatBuildYZ(Mat33* dst, int32_t pitch, int32_t yaw)
{
    // Build Y-Z rotation matrix (pitch and yaw)
    // Use Sin16 which returns values in [-32768, 32767] range
    // Scale to 16.16 fixed-point by shifting left 1
    int32_t sp = (int32_t)Sin16((uint16_t)pitch) << 1;
    int32_t cp = (int32_t)Sin16((uint16_t)(pitch + 0x4000)) << 1;
    int32_t sy = (int32_t)Sin16((uint16_t)yaw) << 1;
    int32_t cy = (int32_t)Sin16((uint16_t)(yaw + 0x4000)) << 1;
    
    dst->m[0] = cy;
    dst->m[1] = 0;
    dst->m[2] = -sy;
    dst->m[3] = (int32_t)(((int64_t)sp * sy) >> 16);
    dst->m[4] = cp;
    dst->m[5] = (int32_t)(((int64_t)sp * cy) >> 16);
    dst->m[6] = (int32_t)(((int64_t)cp * sy) >> 16);
    dst->m[7] = -sp;
    dst->m[8] = (int32_t)(((int64_t)cp * cy) >> 16);
}

void MatBuildYZT(Mat33* dst, int32_t pitch, int32_t yaw)
{
    int32_t* m = dst->m;
    m[1] = Sin32((uint32_t)pitch);
    m[6] = Sin32((uint32_t)yaw);
    m[4] = Sin32((uint32_t)(pitch + 0x4000));
    m[8] = Sin32((uint32_t)(yaw + 0x4000));
    m[7] = 0;
    m[5] = FixedMulHigh(m[1], m[6]);
    m[0] = FixedMulHigh(m[4], m[8]);
    m[3] = -FixedMulHigh(m[1], m[8]);
    m[2] = -FixedMulHigh(m[6], m[4]);
}

// Special division
int32_t SpecialDiv(int32_t num, int32_t den)
{
    if (den == 0) return 0;
    return (int32_t)(((int64_t)num << 16) / den);
}

// Line gradient
int32_t CalcLineGrad(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (x2 == x1) return FP_ONE * 16; // Near-vertical line (large gradient)
    return ((y2 - y1) << 16) / (x2 - x1);
}

// Find MSB
int FindMSB(uint32_t value)
{
    if (value == 0) return 0;
    int msb = 0;
    while (value >>= 1) msb++;
    return msb;
}

// Int64 operations (simulated 64-bit on 32-bit)

void Int64Abs(Int64* v)
{
    if (v->hi < 0) {
        // Two's complement negation: -value = ~value + 1
        uint32_t lo = ~(uint32_t)v->lo + 1;
        int32_t hi = ~v->hi + (lo == 0 ? 1 : 0);  // Add carry if lo overflowed
        v->lo = lo;
        v->hi = hi;
    }
}

void Int64Add64(Int64* dst, const Int64* a, const Int64* b)
{
    uint32_t lo = (uint32_t)a->lo + (uint32_t)b->lo;
    dst->hi = a->hi + b->hi + (lo < (uint32_t)a->lo ? 1 : 0);
    dst->lo = lo;
}

void Int64Add32(Int64* dst, const Int64* a, int32_t b)
{
    Int64 tmp = { b, (b < 0 ? -1 : 0) };
    Int64Add64(dst, a, &tmp);
}

void Int64Sub64(Int64* dst, const Int64* a, const Int64* b)
{
    // Subtract by adding the negation: a - b = a + (-b)
    // Negate b: -b = ~b + 1
    Int64 neg;
    uint32_t lo = ~(uint32_t)b->lo + 1;
    int32_t hi = ~b->hi + (lo == 0 ? 1 : 0);  // Add carry if lo overflowed
    neg.lo = lo;
    neg.hi = hi;
    Int64Add64(dst, a, &neg);
}

void Int64Sub32(Int64* dst, const Int64* a, int32_t b)
{
    Int64 b64 = { b, (b < 0 ? -1 : 0) };
    Int64Sub64(dst, a, &b64);
}

void Int64Mul64(Int64* dst, const Int64* a, const Int64* b)
{
    // 64x64 multiply -> 64-bit (keeping high part)
    int64_t result = ((int64_t)a->hi << 32 | (uint32_t)a->lo) * 
                     ((int64_t)b->hi << 32 | (uint32_t)b->lo);
    dst->hi = result >> 32;
    dst->lo = result;
}

void Int64Mul32(Int64* dst, const Int64* a, int32_t b)
{
    int64_t result = ((int64_t)a->hi << 32 | (uint32_t)a->lo) * b;
    dst->hi = result >> 32;
    dst->lo = result;
}

void Int64MulCreate(Int64* dst, int32_t a, int32_t b)
{
    int64_t result = (int64_t)a * b;
    dst->hi = result >> 32;
    dst->lo = result;
}

void Int64LogicShift(Int64* v, int shift)
{
    if (shift >= 64) {
        v->hi = 0;
        v->lo = 0;
    } else if (shift > 0) {
        v->lo = ((uint32_t)v->lo >> shift) | (v->hi << (32 - shift));
        v->hi = (uint32_t)v->hi >> shift;
    }
}

void Int64ArithShift(Int64* v, int shift)
{
    if (shift >= 64) {
        v->hi = (v->hi < 0) ? -1 : 0;
        v->lo = v->hi;
    } else if (shift > 0) {
        v->lo = ((uint32_t)v->lo >> shift) | (v->hi << (32 - shift));
        v->hi >>= shift;
    }
}

// Trigonometry (lookup-table based)
// Uses DATA_007975 (65KB sine table with 16384 32-bit entries)

// The sine table contains 16384 32-bit fixed-point values for sin(0..pi/2)
// Full circle is 0..0xFFFF (65536 units), with 4 quadrants of 16384 each
// Each entry is a 32-bit value where the high 16 bits are the sine result

static const int32_t* GetSineTable32()
{
    return reinterpret_cast<const int32_t*>(data::GetSineTable());
}

int16_t Sin16(uint16_t angle)
{
    const int32_t* sineTable = GetSineTable32();
    if (!sineTable) return 0;
    
    uint16_t idx = angle & 0x3FFF;  // Index within quadrant (0-16383)
    uint8_t quadrant = (angle >> 14) & 0x3;
    
    // Original ASM: lookup -> shift -> negate (not negate -> shift)
    int16_t value;
    bool negate = false;
    
    switch (quadrant) {
        case 0:  // 0..pi/2: direct lookup
            value = (int16_t)(sineTable[idx] >> 16);
            break;
        case 1:  // pi/2..pi: sin(x) = sin(pi - x)
            value = (int16_t)(sineTable[0x3FFF - idx] >> 16);
            break;
        case 2:  // pi..3pi/2: sin(x) = -sin(x - pi)
            value = (int16_t)(sineTable[idx] >> 16);
            negate = true;
            break;
        case 3:  // 3pi/2..2pi: sin(x) = -sin(2pi - x)
            value = (int16_t)(sineTable[0x3FFF - idx] >> 16);
            negate = true;
            break;
    }
    
    return negate ? -value : value;
}

int32_t Sin32(uint32_t angle)
{
    const int32_t* sineTable = GetSineTable32();
    if (!sineTable) return 0;
    
    uint32_t idx = angle & 0x3FFF;
    uint8_t quadrant = (angle >> 14) & 0x3;
    
    int32_t value;
    bool negate = false;
    
    switch (quadrant) {
        case 0:
            value = sineTable[idx];
            break;
        case 1:
            value = sineTable[0x3FFF - idx];
            break;
        case 2:
            value = sineTable[idx];
            negate = true;
            break;
        case 3:
            value = sineTable[0x3FFF - idx];
            negate = true;
            break;
    }
    
    return negate ? -value : value;
}

// ============================================================
// 3D Projection (Phase 40A)
// ============================================================

void CameraInit(Camera3D* cam)
{
    cam->position.x = 0;
    cam->position.y = 0;
    cam->position.z = 0;
    
    // Identity rotation matrix
    cam->rotation.m[0] = FP_ONE; cam->rotation.m[1] = 0; cam->rotation.m[2] = 0;
    cam->rotation.m[3] = 0; cam->rotation.m[4] = FP_ONE; cam->rotation.m[5] = 0;
    cam->rotation.m[6] = 0; cam->rotation.m[7] = 0; cam->rotation.m[8] = FP_ONE;
    
    // Match FUNC_001479: focal = 256 ((x<<8)/z), near = 0x40
    cam->fovScale = ASM_PROJ_FOCAL;
    cam->nearClip = ASM_PROJ_NEAR;
    cam->farClip = 1000 * FP_ONE;
}

void CameraSetPosition(Camera3D* cam, int32_t x, int32_t y, int32_t z)
{
    cam->position.x = x;
    cam->position.y = y;
    cam->position.z = z;
}

void CameraSetOrientation(Camera3D* cam, int32_t pitch, int32_t yaw)
{
    // Build view matrix (world-to-camera transform)
    // Camera looks along -Z, so we negate the rotation
    MatBuildYZT(&cam->rotation, pitch, yaw);
}

void MatBuildView(Mat33* dst, int32_t pitch, int32_t yaw)
{
    // Build view matrix: transpose of YZ rotation
    // This transforms world coordinates to camera space
    MatBuildYZT(dst, pitch, yaw);
}

int32_t PerspectiveDivide(int32_t value, int32_t z)
{
    if (z <= 0) return 0;
    return (int32_t)(((int64_t)value * ASM_PROJ_FOCAL) / z);
}

// FUNC_001666_SpecialDiv — used by thrusters / LOD; distinct from legacy <<16 SpecialDiv.
int32_t SpecialDivAsm(int32_t num, int32_t den)
{
    if (den > 0) {
        while (den > 0x7fff) {
            den >>= 1;
            num >>= 1;
        }
    }
    if (den != 0)
        // ASM shl then idiv: shift as bits, then divide signed.
        return static_cast<int32_t>(static_cast<uint32_t>(num) << 8) / den;
    return num;
}

// FUNC_001479 — camera-space → screen.
bool ProjectCameraPoint(int32_t x, int32_t y, int32_t z, ProjResult* out)
{
    if (!out) return false;
    if (z < ASM_PROJ_NEAR) {
        out->visible = false;
        out->sx = 0;
        out->sy = 0;
        out->z = z;
        return false;
    }

    int32_t iVar1;
    int32_t iVar2;
    if (z < 0x10001) {
        iVar1 = (int32_t)((uint32_t)x << 8) / z;
        iVar2 = (int32_t)((uint32_t)y << 8) / z;
    } else {
        int32_t z8 = z >> 8;
        if (z8 == 0) z8 = 1;
        iVar1 = x / z8;
        iVar2 = y / z8;
    }

    int32_t sx = iVar1 + ASM_PROJ_CENTER_X;
    int32_t sy = ASM_PROJ_CENTER_Y - iVar2;

    // Overflow shrink (FUNC_001479): while |sx||sy| magnitude > 0x1f00, >>=1 both.
    int32_t ax = sx < 0 ? -sx : sx;
    int32_t ay = sy < 0 ? -sy : sy;
    for (uint32_t mag = (uint32_t)ax | (uint32_t)ay; (int32_t)mag > 0x1f00; mag >>= 1) {
        sx >>= 1;
        sy >>= 1;
    }

    if (sx < -32000) sx = -32000;
    if (sx > 32000) sx = 32000;
    if (sy < -32000) sy = -32000;
    if (sy > 32000) sy = 32000;

    out->sx = (int16_t)sx;
    out->sy = (int16_t)sy;
    out->z = z;
    out->visible = true;
    return true;
}

bool ProjectPoint(const Camera3D* cam, const Point3D* world, ProjResult* out)
{
    // Step 1: Translate to camera space
    int32_t cx = world->x - cam->position.x;
    int32_t cy = world->y - cam->position.y;
    int32_t cz = world->z - cam->position.z;
    
    // Step 2: Rotate to camera space (world-to-view transform)
    int32_t vx = ((int64_t)cx * cam->rotation.m[0] + 
                  (int64_t)cy * cam->rotation.m[3] + 
                  (int64_t)cz * cam->rotation.m[6]) >> 16;
    int32_t vy = ((int64_t)cx * cam->rotation.m[1] + 
                  (int64_t)cy * cam->rotation.m[4] + 
                  (int64_t)cz * cam->rotation.m[7]) >> 16;
    int32_t vz = ((int64_t)cx * cam->rotation.m[2] + 
                  (int64_t)cy * cam->rotation.m[5] + 
                  (int64_t)cz * cam->rotation.m[8]) >> 16;

    // Step 3–4: FUNC_001479 perspective
    (void)cam; // near/fov taken from ASM constants inside ProjectCameraPoint
    return ProjectCameraPoint(vx, vy, vz, out);
}

void ProjectVector(const Camera3D* cam, const Point3D* dir, Point3D* out)
{
    // Transform direction vector (no translation)
    out->x = ((int64_t)dir->x * cam->rotation.m[0] + 
              (int64_t)dir->y * cam->rotation.m[3] + 
              (int64_t)dir->z * cam->rotation.m[6]) >> 16;
    out->y = ((int64_t)dir->x * cam->rotation.m[1] + 
              (int64_t)dir->y * cam->rotation.m[4] + 
              (int64_t)dir->z * cam->rotation.m[7]) >> 16;
    out->z = ((int64_t)dir->x * cam->rotation.m[2] + 
              (int64_t)dir->y * cam->rotation.m[5] + 
              (int64_t)dir->z * cam->rotation.m[8]) >> 16;
}

// Fixed-point multiplication helper (FUNC_001521)
// ASM: imul; shl eax,1; rcl edx,1; return edx  → (int64_t)(a*b) >> 31
int32_t FixedMulHigh(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 31);
}

} // namespace jjffe
