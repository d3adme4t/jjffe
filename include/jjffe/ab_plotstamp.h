// jjffe/ab_plotstamp.h - shared per-pixel plot-site stamp for A/B digs.
// AB_PLOTBOX=x0,y0,x1,y1 (default off): every pixel written inside the box
// prints writer kind + current DrawFlatPolygon face identity + value.
#ifndef JJFFE_AB_PLOTSTAMP_H
#define JJFFE_AB_PLOTSTAMP_H
#include <cstdint>
namespace jjffe {
// True while a box is armed (cheap check for hot spans).
bool AbPlotArmed();
// Stamp one pixel (kind = short writer tag).
void AbPlotStamp(int x, int y, unsigned val, const char* kind);
// Stamp a contiguous span (fast path: checks once, stamps members in range).
void AbPlotStampSpan(int y, int x0, int n, unsigned val, const char* kind);
// Face identity feed (set by DrawFlatPolygon before dispatching fills).
void AbPlotSetFace(int meshIndex, int vertexCount, uint32_t texInfo,
                   int32_t camX, int32_t camY);
// R1068: last-writer attribution for a single pixel (AB_PLOTPIX=x,y).
void AbPlotNoteWriter(int x, int y, unsigned val, const char* kind);
const char* AbPlotLastWriter(int x, int y);
unsigned AbPlotLastVal(int x, int y);
}  // namespace jjffe
#endif
