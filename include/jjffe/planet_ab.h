#ifndef JJFFE_PLANET_AB_H
#define JJFFE_PLANET_AB_H

#include "jjffe/video.h"
#include <cstdint>

namespace jjffe {

struct PlanetAbConfig {
    int traceFace = -1;       // AB_1F_TRACE_FACE: -1 = all faces
    bool force868 = false;    // AB_1F_FORCE_1868
    bool force869 = false;    // AB_1F_FORCE_1869
    bool disableMixSkip = false; // AB_1F_DISABLE_MIX_SKIP
    int midpointVariant = -1; // AB_1F_MIDPOINT_VARIANT: -1 = default table
    bool dumpRowSpans = false; // AB_1F_DUMP_ROW_SPANS
};

struct PlanetRowSpan {
    int y = 0;
    int x0 = 0;
    int x1 = -1;
    int count = 0;
};

struct PlanetRegionDiff {
    int asmOnly = 0;
    int cppOnly = 0;
    int bothSame = 0;
    int bothDifferent = 0;
    int asmPixels = 0;
    int cppPixels = 0;
    double asmCx = 0;
    double asmCy = 0;
    double cppCx = 0;
    double cppCy = 0;
    int asmBbox[4] = {319, 199, 0, 0}; // x0,y0,x1,y1
    int cppBbox[4] = {319, 199, 0, 0};
    int largestAsmOnlyArea = 0;
    int largestAsmOnlyX0 = 0;
    int largestAsmOnlyY0 = 0;
    int largestAsmOnlyW = 0;
    int largestAsmOnlyH = 0;
    PlanetRowSpan asmRowSpans[SCREEN_HEIGHT]{};
    PlanetRowSpan cppRowSpans[SCREEN_HEIGHT]{};
};

struct PlanetFaceTrace {
    int face = 0;
    uint32_t fn833Ret = 0;
    int path835 = 0;       // 868, 869, 870, ...
    int param11 = 0;
    uint32_t param10 = 0;
    int mixSkipHits = 0;
    int calls868 = 0;
    int leafPaint868 = 0;
    int early868 = 0;
    int nodes816 = 0;
    int nodes820 = 0;
};

struct PlanetAbTrace {
    PlanetFaceTrace faces[20]{};
    int activeFace = -1;
    int lodMsb = 0;
    int lodC = 0;
    int total868Calls = 0;
    int total868Leaf = 0;
    int totalMixSkip = 0;
    int totalJobs778 = 0;
    int jobs778ByWords[32]{};
    int total842 = 0;
    int total840 = 0;
    bool active = false;
};

PlanetAbConfig PlanetAbLoadConfig();
const PlanetAbConfig& PlanetAbGetConfig();
void PlanetAbResetTrace();
void PlanetAbResetFacePass();
PlanetAbTrace* PlanetAbGetTrace();

void PlanetAbBeginFace(int faceIndex, uint32_t fn833Ret, uint32_t param10, int param11);
void PlanetAbNote835Path(int pathCode);
void PlanetAbNoteMixSkip();
void PlanetAbNote868Call();
void PlanetAbNote868EarlyReturn();
void PlanetAbNote868LeafPaint();
void PlanetAbNote868LeafDetail(int depth, int area2x, int lod0, int lod1, int lod2,
                               int reason, int sx0, int sy0, int sx1, int sy1,
                               int sx2, int sy2);
void PlanetAbDumpLeafTrace(const char* path);
void PlanetAbNote869Cull(int reason, int depth, int local20, int margin, int area48,
                         int16_t* v1, int16_t* v2, int16_t* v3);
// gateReason: 0=fallback/no-midpoint, 1=depth>13, 2=all lod<iVar4, 3=small area48 & iVar4>8
void PlanetAbNote869Paint(int depth, int effDepth, int cVar1, int area48, int gateReason,
                          int16_t* v1, int16_t* v2, int16_t* v3);
void PlanetAbNote869Subdivide(int depth, int effDepth, int cVar1, int area48,
                              int local20, int margin,
                              int16_t* v1, int16_t* v2, int16_t* v3);
void PlanetAbNote876RootVert(int index, int mix0x3c, int camZ);
void PlanetAbDump869CullTrace(const char* path);
bool PlanetAb869TraceEnabled();
void PlanetAbSnapshotPools(int nodes816, int nodes820);
void PlanetAbNoteJob778(int words);
void PlanetAbNote842();
void PlanetAbNote840();

bool PlanetAbLoadBmp320x200(const char* path, uint8_t* out);
bool PlanetAbSaveBmp320x200(const char* path, const uint8_t* pixels,
                            const uint8_t* palette384);
void PlanetAbCompareRegion(const uint8_t* cpp, const uint8_t* asmBuf,
                           PlanetRegionDiff* out);
void PlanetAbWriteDiffPpm(const char* dir, const uint8_t* cpp,
                          const uint8_t* asmBuf, const PlanetRegionDiff& diff);
void PlanetAbPrintRegionDiff(const PlanetRegionDiff& diff);
void PlanetAbDumpFaceTrace(const char* path);
void PlanetAbDumpRowSpans(const char* path, const PlanetRegionDiff& diff);

// AB_1F_MIDPOINT_TRACE: log 851/855 simple vs FUNC_001846 branch selection.
struct PlanetMidpointBranchSample {
    int fn = 0;       // 851 or 855
    int param5 = 0;
    int eff5 = 0;     // param5 after mix-index xor adjustment
    int lod2 = 0;     // parent +0x44
    int lod3 = 0;
    int mix2 = 0;     // parent +0x3c
    int mix3 = 0;
    int branch = 0;   // 0=simple avg +0x3c, 1=FUNC_001846 path
    int outLod = 0;   // midpoint +0x44 after FUNC_001829
    int outMix = 0;   // midpoint +0x3c
    int z2 = 0;       // parent camera z (+0x0c)
    int z3 = 0;
};

void PlanetAbResetMidpointTrace();
void PlanetAbNote834Midpoint(int param6, int fn278, int fn279);
void PlanetAbNoteMidpointFnCall(int fnId);
void PlanetAbNoteMidpointBranch(int fn, int param5, int eff5,
                                int lod2, int lod3, int mix2, int mix3,
                                int branch, int outLod, int outMix, int z2, int z3);
void PlanetAbDumpMidpointTrace(const char* path);

// Non-ASM quality (opt-in): close Contour/fill halo outside rocky facets.
// Default OFF — painting the Contour annulus on live intro Earth/Mercury made
// thick textured rings. AB_1F_LIMB_FILL=1 enables note+annulus fill;
// AB_1F_LIMB_INSET=1 enables outer-vert Contour inset. Apply at FUNC_001789.
void PlanetAbNoteContourLimb(int x0, int y0, int x1, int y1, int x2, int y2,
                             int x3, int y3);
void PlanetAbContourInsetVerts(int16_t* xy8);
void PlanetAbLimbFillApply();

} // namespace jjffe

#endif
