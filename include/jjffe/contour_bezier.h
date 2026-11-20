#ifndef JJFFE_CONTOUR_BEZIER_H
#define JJFFE_CONTOUR_BEZIER_H

#include <cstdint>
#include <vector>

namespace jjffe {

struct ContourPt2 {
    int32_t x = 0;
    int32_t y = 0;
};

struct ContourPt3 {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

// FUNC_001549 / 001547 cubic DDA (same step tables as soft C721SpanAddBezier).
// Control points are integer-ish coords (screen pixels, or world>>16).
// out includes p0, interior samples, and p3.
void ContourBezierSample2i(const int32_t p0[2], const int32_t p1[2],
                           const int32_t p2[2], const int32_t p3[2],
                           std::vector<ContourPt2>& out);

// 3-axis world sampling: control points are 16.16 world. Step count uses
// (coord>>16) like soft screen pixels; DDA state stays in 16.16 world units.
void ContourBezierSample3i(const int32_t p0[3], const int32_t p1[3],
                           const int32_t p2[3], const int32_t p3[3],
                           std::vector<ContourPt3>& out);

// Same as ContourBezierSample3i but never below minStepBits (2..5). Contours
// need denser samples than soft's tiny world>>16 spans would choose.
void ContourBezierSample3iMin(const int32_t p0[3], const int32_t p1[3],
                              const int32_t p2[3], const int32_t p3[3],
                              int minStepBits, std::vector<ContourPt3>& out);

} // namespace jjffe

#endif
