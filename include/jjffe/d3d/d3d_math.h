#ifndef JJFFE_D3D_MATH_H
#define JJFFE_D3D_MATH_H

#include <cmath>
#include <cstring>
#include <d3d9.h>

namespace jjffe {
namespace d3d {

inline void MatIdentity(D3DMATRIX* m)
{
    std::memset(m, 0, sizeof(*m));
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}

inline void MatMultiply(D3DMATRIX* out, const D3DMATRIX& a, const D3DMATRIX& b)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                       a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    }
    *out = r;
}

inline void MatTranslation(D3DMATRIX* m, float x, float y, float z)
{
    MatIdentity(m);
    m->_41 = x;
    m->_42 = y;
    m->_43 = z;
}

inline void MatScaling(D3DMATRIX* m, float x, float y, float z)
{
    MatIdentity(m);
    m->_11 = x;
    m->_22 = y;
    m->_33 = z;
}

inline void MatRotationY(D3DMATRIX* m, float rad)
{
    MatIdentity(m);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    m->_11 = c;
    m->_13 = -s;
    m->_31 = s;
    m->_33 = c;
}

inline void MatRotationX(D3DMATRIX* m, float rad)
{
    MatIdentity(m);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    m->_22 = c;
    m->_23 = s;
    m->_32 = -s;
    m->_33 = c;
}

inline void MatLookAtLH(D3DMATRIX* m, float ex, float ey, float ez,
                        float tx, float ty, float tz, float ux, float uy, float uz)
{
    float zx = tx - ex, zy = ty - ey, zz = tz - ez;
    float len = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (len < 1e-8f) len = 1.0f;
    zx /= len;
    zy /= len;
    zz /= len;

    float xx = uy * zz - uz * zy;
    float xy = uz * zx - ux * zz;
    float xz = ux * zy - uy * zx;
    len = std::sqrt(xx * xx + xy * xy + xz * xz);
    if (len < 1e-8f) len = 1.0f;
    xx /= len;
    xy /= len;
    xz /= len;

    const float yx = zy * xz - zz * xy;
    const float yy = zz * xx - zx * xz;
    const float yz = zx * xy - zy * xx;

    MatIdentity(m);
    m->_11 = xx;
    m->_12 = yx;
    m->_13 = zx;
    m->_21 = xy;
    m->_22 = yy;
    m->_23 = zy;
    m->_31 = xz;
    m->_32 = yz;
    m->_33 = zz;
    m->_41 = -(xx * ex + xy * ey + xz * ez);
    m->_42 = -(yx * ex + yy * ey + yz * ez);
    m->_43 = -(zx * ex + zy * ey + zz * ez);
}

inline void MatPerspectiveFovLH(D3DMATRIX* m, float fovY, float aspect, float zn, float zf)
{
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    const float xScale = yScale / aspect;
    std::memset(m, 0, sizeof(*m));
    m->_11 = xScale;
    m->_22 = yScale;
    m->_33 = zf / (zf - zn);
    m->_34 = 1.0f;
    m->_43 = -zn * zf / (zf - zn);
}

// ASM FUNC_001479 film: focal 256 px, principal point (160,79) on 320×200.
// Off-center LH perspective so hull WVP matches soft ProjectPoint / SoftToScreen
// (no Contour Ybias hack). Row-vector: clip = v * M.
inline void MatPerspectiveAsmExact(D3DMATRIX* m, float zn, float zf)
{
    constexpr float f = 256.0f;
    constexpr float W = 320.0f;
    constexpr float H = 200.0f;
    constexpr float cx = 160.0f;
    constexpr float cy = 79.0f;
    const float xScale = 2.0f * f / W;           // 1.60
    const float yScale = 2.0f * f / H;           // 2.56
    const float xOff = 1.0f - 2.0f * cx / W;     // 0
    // Soft sy = cy - f*y/z (Y-down film). D3D LH NDC +Y-up / viewport Y-down:
    // AB_PROJ_DELTA showed yOff=2*cy/H-1 (−0.21) *doubled* the Contour gap
    // (~84 px ≈ 2×21 soft). Negate so hull rises to SoftToScreen Contours.
    const float yOff = -(2.0f * cy / H - 1.0f);  // +0.21
    std::memset(m, 0, sizeof(*m));
    m->_11 = xScale;
    m->_22 = yScale;
    // Principal point via Z→XY (row-vector clip = v*M).
    m->_31 = xOff;
    m->_32 = yOff; // +0.21; dig AB_D3D_ASM_YOFF= to override
    m->_33 = zf / (zf - zn);
    m->_34 = 1.0f;
    m->_43 = -zn * zf / (zf - zn);
}

} // namespace d3d
} // namespace jjffe

#endif
