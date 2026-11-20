#ifndef JJFFE_MATH_H
#define JJFFE_MATH_H

#include <cstdint>

namespace jjffe {

// 64-bit signed integer vector operations (used for 3D fixed-point math)
struct Vec64 {
    int64_t x, y, z;
};

// 32-bit signed integer vector
struct Vec32 {
    int32_t x, y, z;
};

// 3x3 rotation matrix (fixed-point)
struct Mat33 {
    int32_t m[9];  // Row-major: m00,m01,m02, m10,m11,m12, m20,m21,m22
};

// Vec64 operations
int  Vec64Truncate(Vec64* v, int bits);
void Vec64Shift(Vec64* v, int shift);
void Vec64Add(Vec64* dst, const Vec64* a, const Vec64* b);
void Vec64Sub(Vec64* dst, const Vec64* a, const Vec64* b);
void Vec64NegAssign(Vec64* v);
void Vec64Abs(Vec64* v);
int  Vec64FindMSB(const Vec64* v);

// Vec32 operations
void SetIntVec(int32_t* vec, int32_t x, int32_t y, int32_t z);
void GetIntVec(const int32_t* vec, int32_t* x, int32_t* y, int32_t* z);

// Vector-Matrix multiplication (fixed-point 16.16)
void VecMatMul(Vec32* dst, const Vec32* v, const Mat33* m);
void VecMatTMul(Vec32* dst, const Vec32* v, const Mat33* m);
void SpecialVecMatMul(Vec32* dst, const Vec32* v, const Mat33* m);

// Matrix-Matrix multiplication
void MatMatMul(Mat33* dst, const Mat33* a, const Mat33* b);
void MatMatTMul(Mat33* dst, const Mat33* a, const Mat33* b);
void MatAssign(Mat33* dst, const Mat33* src);
void MatBuildOdd(Mat33* dst);
void MatBuildYZ(Mat33* dst, int32_t pitch, int32_t yaw);
// Build Y-Z-T rotation (matches ASM FUNC_001675). Elements are Sin32-scale
// (~±2^31 for ±1.0), NOT 16.16. Vertex transform uses FixedMulHigh
// (SpecialVecMatMul / FUNC_001671). MatScaleSin32ToFp16 is only for the
// legacy AB_FP16_VERT=1 >>16 path.
void MatBuildYZT(Mat33* dst, int32_t pitch, int32_t yaw);

// Convert FUNC_001675 / MatBuildYZT elements (~Q31) to ~16.16 for >>16 muls.
// Prefer FixedMulHigh on Sin32-scale matrices (default) over this + >>16.
inline void MatScaleSin32ToFp16(Mat33* m)
{
    for (int i = 0; i < 9; i++)
        m->m[i] = m->m[i] >> 15;
}

// Special division (fixed-point)
int32_t SpecialDiv(int32_t num, int32_t den);

// Line gradient calculation
int32_t CalcLineGrad(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

// Find most significant bit
int FindMSB(uint32_t value);

// Int64 operations (64-bit arithmetic on 32-bit platforms)
struct Int64 { int32_t lo, hi; };

void   Int64Abs(Int64* v);
void   Int64Add64(Int64* dst, const Int64* a, const Int64* b);
void   Int64Add32(Int64* dst, const Int64* a, int32_t b);
void   Int64Sub64(Int64* dst, const Int64* a, const Int64* b);
void   Int64Sub32(Int64* dst, const Int64* a, int32_t b);
void   Int64Mul64(Int64* dst, const Int64* a, const Int64* b);
void   Int64Mul32(Int64* dst, const Int64* a, int32_t b);
void   Int64MulCreate(Int64* dst, int32_t a, int32_t b);
void   Int64LogicShift(Int64* v, int shift);
void   Int64ArithShift(Int64* v, int shift);

// Trigonometry (lookup-table based)
int16_t Sin16(uint16_t angle);  // 0..16383 maps to 0..2pi
int32_t Sin32(uint32_t angle);  // 0..65535 maps to 0..2pi

// Fixed-point multiplication helper (FUNC_001521)
// Returns high 32 bits of 64-bit product, shifted left 1, with sign bit
// Used by 71 functions for fixed-point arithmetic
int32_t FixedMulHigh(int32_t a, int32_t b);

// 3D Projection (Phase 40A)
// Fixed-point constants
constexpr int32_t FP_ONE = 0x10000;  // 1.0 in 16.16 fixed-point
constexpr int32_t FP_HALF = 0x8000;  // 0.5 in 16.16

// Screen dimensions for projection
constexpr int PROJ_SCREEN_W = 320;
constexpr int PROJ_SCREEN_H = 200;
constexpr int PROJ_CENTER_X = PROJ_SCREEN_W / 2;
constexpr int PROJ_CENTER_Y = PROJ_SCREEN_H / 2;

// FUNC_001479 centers / focal (not the old 90° fovScale=160 guess).
constexpr int ASM_PROJ_CENTER_X = 0xA0; // 160
constexpr int ASM_PROJ_CENTER_Y = 0x4F; // 79
constexpr int ASM_PROJ_FOCAL = 256;     // (x << 8) / z
constexpr int ASM_PROJ_NEAR = 0x40;

// Projection result
struct ProjResult {
    int16_t sx, sy;  // Screen coordinates
    int32_t z;       // Depth (for Z-buffer)
    bool visible;    // True if point is in front of camera
};

// 3D point in world space
struct Point3D {
    int32_t x, y, z;  // Fixed-point 16.16
};

// Camera/View state
struct Camera3D {
    Point3D position;    // Camera position in world space
    Mat33   rotation;    // Camera orientation (world-to-view matrix)
    int32_t fovScale;    // Perspective focal (ASM FUNC_001479: 256)
    int32_t nearClip;    // Near clip (ASM: 0x40)
    int32_t farClip;     // Far clipping plane distance
};

// Initialize camera with default values
void CameraInit(Camera3D* cam);

// Set camera position
void CameraSetPosition(Camera3D* cam, int32_t x, int32_t y, int32_t z);

// Set camera orientation from pitch/yaw (fixed-point angles)
void CameraSetOrientation(Camera3D* cam, int32_t pitch, int32_t yaw);

// FUNC_001479: project camera-space (x,y,z) → screen.
bool ProjectCameraPoint(int32_t x, int32_t y, int32_t z, ProjResult* out);

// Project a 3D point to 2D screen space (view transform + FUNC_001479)
bool ProjectPoint(const Camera3D* cam, const Point3D* world, ProjResult* out);

// Project a 3D vector (direction, no translation)
void ProjectVector(const Camera3D* cam, const Point3D* dir, Point3D* out);

// Build view matrix (world-to-camera transform)
void MatBuildView(Mat33* dst, int32_t pitch, int32_t yaw);

// Perspective divide helper
int32_t PerspectiveDivide(int32_t value, int32_t z);

// FUNC_001666: (num<<8)/den after den normalization (ASM SpecialDiv).
int32_t SpecialDivAsm(int32_t num, int32_t den);

} // namespace jjffe

#endif // JJFFE_MATH_H
