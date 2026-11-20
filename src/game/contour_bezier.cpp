#include "jjffe/contour_bezier.h"
#include <cstdint>

namespace jjffe {
namespace {

int SoftBezierStepBitsFromSpan(int32_t spanSum)
{
    const uint32_t u = static_cast<uint32_t>(spanSum < 0 ? -spanSum : spanSum);
    if (u < 4) return 2;
    if (u < 0x1e) return 3;
    if (u < 25000) return 4;
    return 5;
}

int SoftBezierSpan2(const int32_t p0[2], const int32_t p1[2],
                    const int32_t p2[2], const int32_t p3[2])
{
    int iVar7 = (int)p0[0] + (int)p1[0] + (int)p2[0] + (int)p3[0];
    if (iVar7 < 0) iVar7 += 3;
    iVar7 = (iVar7 >> 2) - (int)p0[0];
    if (iVar7 < 0) iVar7 = -iVar7;
    int iVar8 = (int)p0[1] + (int)p1[1] + (int)p2[1] + (int)p3[1];
    if (iVar8 < 0) iVar8 += 3;
    iVar8 = (iVar8 >> 2) - (int)p0[1];
    if (iVar8 < 0) iVar8 = -iVar8;
    return iVar8 + iVar7;
}

int SoftBezierSpan3(const int32_t p0[3], const int32_t p1[3],
                    const int32_t p2[3], const int32_t p3[3])
{
    auto axis = [](int a0, int a1, int a2, int a3) {
        int s = a0 + a1 + a2 + a3;
        if (s < 0) s += 3;
        s = (s >> 2) - a0;
        if (s < 0) s = -s;
        return s;
    };
    return axis(p0[0], p1[0], p2[0], p3[0]) +
           axis(p0[1], p1[1], p2[1], p3[1]) +
           axis(p0[2], p1[2], p2[2], p3[2]);
}

// Soft coeffs use "pixel" integers. Compute in int64 to avoid <<sh3 overflow
// when those integers are large (world>>16 on big ships).
void SetupAxisCoeffs64(int32_t p0, int32_t p1, int32_t p2, int32_t p3, int sVar6,
                       int64_t& d1, int64_t& d2, int64_t& d3)
{
    const int sh3 = (sVar6 * -3 + 0x10) & 0x1f;
    const int sh2 = (sVar6 * -2 + 0x10) & 0x1f;
    const int sh1 = (0x10 - sVar6) & 0x1f;
    const int64_t b =
        static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>((int64_t)p3 - p0) +
                                                     (int64_t)p1 * 3 - (int64_t)p2 * 3)) << sh3);
    const int64_t c = static_cast<int64_t>(
        static_cast<uint64_t>((int64_t)p2 * 3 - (int64_t)p1 * 6 + (int64_t)p0 * 3) << sh2);
    // ASM shl; shift the unsigned view (UBSan: negative operands).
    d1 = b + static_cast<int64_t>(static_cast<uint64_t>((int64_t)p1 * 3 - (int64_t)p0 * 3) << sh1) + c;
    d2 = c * 2 + b * 6;
    d3 = b * 6;
}

} // namespace

void ContourBezierSample2i(const int32_t p0[2], const int32_t p1[2],
                           const int32_t p2[2], const int32_t p3[2],
                           std::vector<ContourPt2>& out)
{
    const int sVar6 = SoftBezierStepBitsFromSpan(SoftBezierSpan2(p0, p1, p2, p3));
    // Soft: c0 = pixel<<16; emit c0>>16.
    int64_t c0 = (int64_t)p0[0] << 16;
    int64_t c1 = (int64_t)p0[1] << 16;
    int64_t d1x, d2x, d3x, d1y, d2y, d3y;
    SetupAxisCoeffs64(p0[0], p1[0], p2[0], p3[0], sVar6, d1x, d2x, d3x);
    SetupAxisCoeffs64(p0[1], p1[1], p2[1], p3[1], sVar6, d1y, d2y, d3y);

    out.push_back({p0[0], p0[1]});
    int local_14 = (1 << sVar6) - 1;
    do {
        c0 += d1x;
        c1 += d1y;
        d1x += d2x;
        d1y += d2y;
        d2x += d3x;
        d2y += d3y;
        out.push_back({static_cast<int32_t>(c0 >> 16),
                       static_cast<int32_t>(c1 >> 16)});
        local_14--;
    } while (local_14 >= 0);
    out.push_back({p3[0], p3[1]});
}

void ContourBezierSample3i(const int32_t p0[3], const int32_t p1[3],
                           const int32_t p2[3], const int32_t p3[3],
                           std::vector<ContourPt3>& out)
{
    ContourBezierSample3iMin(p0, p1, p2, p3, 2, out);
}

void ContourBezierSample3iMin(const int32_t p0[3], const int32_t p1[3],
                              const int32_t p2[3], const int32_t p3[3],
                              int minStepBits, std::vector<ContourPt3>& out)
{
    // CRITICAL: subdivision heuristic uses world>>16 (soft pixel scale).
    const int32_t s0[3] = {p0[0] >> 16, p0[1] >> 16, p0[2] >> 16};
    const int32_t s1[3] = {p1[0] >> 16, p1[1] >> 16, p1[2] >> 16};
    const int32_t s2[3] = {p2[0] >> 16, p2[1] >> 16, p2[2] >> 16};
    const int32_t s3[3] = {p3[0] >> 16, p3[1] >> 16, p3[2] >> 16};
    int sVar6 = SoftBezierStepBitsFromSpan(SoftBezierSpan3(s0, s1, s2, s3));
    if (minStepBits < 2) minStepBits = 2;
    if (minStepBits > 5) minStepBits = 5;
    if (sVar6 < minStepBits) sVar6 = minStepBits;

    int64_t cx = p0[0], cy = p0[1], cz = p0[2];
    int64_t d1x, d2x, d3x, d1y, d2y, d3y, d1z, d2z, d3z;
    SetupAxisCoeffs64(s0[0], s1[0], s2[0], s3[0], sVar6, d1x, d2x, d3x);
    SetupAxisCoeffs64(s0[1], s1[1], s2[1], s3[1], sVar6, d1y, d2y, d3y);
    SetupAxisCoeffs64(s0[2], s1[2], s2[2], s3[2], sVar6, d1z, d2z, d3z);

    out.push_back({p0[0], p0[1], p0[2]});
    int local_14 = (1 << sVar6) - 1;
    do {
        cx += d1x;
        cy += d1y;
        cz += d1z;
        d1x += d2x;
        d1y += d2y;
        d1z += d2z;
        d2x += d3x;
        d2y += d3y;
        d2z += d3z;
        out.push_back({static_cast<int32_t>(cx), static_cast<int32_t>(cy),
                       static_cast<int32_t>(cz)});
        local_14--;
    } while (local_14 >= 0);
    out.push_back({p3[0], p3[1], p3[2]});
}

} // namespace jjffe
