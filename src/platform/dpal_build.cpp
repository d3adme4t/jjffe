// FIX_REV=3 2026-09-18: dense split uses ASM 1-based max-heap (extent @ +0xc);
// always 64 boxes — do not drop empty halves / early-exit on extent<=1 (that
// was an incorrect "oracle" shortcut; Mercury marks @ 0x160000 only matched
// 38/64 AABBs under linear FindLargestExtent).
// FIX_REV=2 2026-08-10: Pass2 stores at slot (not slot+base); direct table
// seeded pre-Pass2; VGA install via SetPaletteEntry (no DPalSet XOR); empty
// split halves dropped like ASM.
//
// FUNC_001329 ScanBox + FUNC_001330 DPalBuild — Heckbert split-largest-extent
// to 64 boxes, stamp DATA_009139 / 009140 / 009144 (dPalSecondary / Reverse / RGB).
//
// Cell-diff recipe (GDB after ASM FUNC_001330):
//   dump binary memory dpal_009139.bin &DATA_009139 &DATA_009139+0x1000
//   dump binary memory dpal_009140.bin &DATA_009140 &DATA_009140+0x41
//   dump binary memory dpal_007770.bin &DATA_007770 &DATA_007770+0x1000
//
// Prefer the *composed* lookup for the meaningful diff:
//   for k in 0..4095:  out[k] = reverse[secondary[k]]   (base-agnostic;
//   this is what DPal16toPal returns on a map miss). Raw secondary ids are
//   ASM 1-based (stamp writes box+1 — see FUNC_001330); if a dump ever looks
//   0-based, normalize ±1 before comparing ids, don't "fix" the stamp.
//
// dPalReverse[0] is the unseeded / non-direct-hit bucket (secondary==0) → VGA0.
// Flagged oracle gate: reverse slots + allocated dPalRGB use Q2 centroids
// ((rMin+rMax-1)*2+1). If dumps disagree only there, adjust Q2Centroid6.

#include "jjffe/dpal_build.h"
#include "jjffe/draw.h"
#include "jjffe/video.h"
#include "jjffe/data_globals.h"  // DATA_008630 (R1214 WHY)
#include "jjffe/span_arena.h"    // g_bss8804[0xc] = DATA_008809 (R1214 WHY)

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__GNUC__)
#  include <dlfcn.h>
#endif
#if defined(__GNUC__) && defined(__linux__)
#  include <execinfo.h>
#endif


// 4.408: set by the game loop (see DPalSetSceneBuilds) - true on the branch
// that actually runs DPalBuild, false on the DrawMission branch where the
// palette is leftover state (water, 4.402).
namespace jjffe {
static bool s_dpalSceneBuilds = true;
void DPalSetSceneBuilds(bool buildsHere) { s_dpalSceneBuilds = buildsHere; }
void DPalClearReverse()
{
    if (g_videoState) memset(g_videoState->dPalReverse, 0, sizeof(g_videoState->dPalReverse));
}
namespace {

struct Box {
    int16_t r0, r1, g0, g1, b0, b1; // half-open
    int16_t extent;                   // +0xc
    int16_t axis;                     // +0xe: 0=R 1=G 2=B
};

// FUNC_001329 — tighten box to occupied sub-range; longest axis R→G→B.
int ScanBox(Box& box, const uint8_t* occ)
{
    const int r0 = box.r0, r1 = box.r1;
    const int g0 = box.g0, g1 = box.g1;
    const int b0 = box.b0, b1 = box.b1;

    box.r0 = 0x10;
    box.g0 = 0x10;
    box.b0 = 0x10;
    box.r1 = 0;
    box.g1 = 0;
    box.b1 = 0;

    int count = 0;
    for (int r = r0; r < r1; r++) {
        for (int g = g0; g < g1; g++) {
            for (int b = b0; b < b1; b++) {
                const int idx = (r << 8) | (g << 4) | b;
                if (occ[idx] == 0)
                    continue;
                count++;
                if (r < box.r0) box.r0 = static_cast<int16_t>(r);
                if (r + 1 > box.r1) box.r1 = static_cast<int16_t>(r + 1);
                if (g < box.g0) box.g0 = static_cast<int16_t>(g);
                if (g + 1 > box.g1) box.g1 = static_cast<int16_t>(g + 1);
                if (b < box.b0) box.b0 = static_cast<int16_t>(b);
                if (b + 1 > box.b1) box.b1 = static_cast<int16_t>(b + 1);
            }
        }
    }

    int ext = box.r1 - box.r0;
    int axis = 0;
    const int extG = box.g1 - box.g0;
    if (ext < extG) {
        ext = extG;
        axis = 1;
    }
    const int extB = box.b1 - box.b0;
    if (ext < extB) {
        ext = extB;
        axis = 2;
    }
    box.extent = static_cast<int16_t>(ext);
    box.axis = static_cast<int16_t>(axis);
    return count;
}

// JUMP_005271 — 6-bit DAC centroid for a half-open [lo, hi) nibble range.
uint8_t Q2Centroid6(int lo, int hi)
{
    return static_cast<uint8_t>(((lo + hi - 1) * 2) + 1);
}

uint8_t Expand6to8(uint8_t v6)
{
    // Match ASM VideoSetPalValue (sdlvideo.cpp): pPalTable[v] << 2.
    return static_cast<uint8_t>(v6 << 2);
}

// Pass1 cell from DATA_009144: ASM uses (b0>>2)<<8|(b1>>2)<<4|(b2>>2) on the
// 6-bit DAC store — NOT 8-bit >>4 (that was the Expand6to8-in-store bug).
uint16_t Dac6To444Index(uint8_t c0, uint8_t c1, uint8_t c2)
{
    return static_cast<uint16_t>((((c0 >> 2) & 0xF) << 8) |
                                 (((c1 >> 2) & 0xF) << 4) |
                                 ((c2 >> 2) & 0xF));
}

uint16_t Rgb888To444Index(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>((r >> 4) * 256 + (g >> 4) * 16 + (b >> 4));
}

// ASM FUNC_001330 dense path uses a 1-based max-heap of box pointers ordered
// by extent (+0xc). Linear FindLargestExtent + "drop empty half" diverged on
// tie-break and left only ~38/64 AABBs matching Mercury marks @ 0x160000.
// Faithful heap: always emit exactly 64 boxes (empty halves kept; extent can
// go negative after ScanBox on an emptied parent — heap ignores them).

} // namespace

void DPalBuildMedianCut(const uint8_t* palette, int /*baseArg*/)
{
    if (!g_videoState || !palette)
        return;

    auto& vs = *g_videoState;
    // ASM: the base = the DATA_007634 flip state (DPalSet XORs 0x40 per call),
    // NOT the caller argument. FUNC_001330 reads the working bank at
    // palette[base..base+63] and indexes the RGB store by (VGA-0x80) ∈ 0..127.
    const int baseIndex = vs.dPalBaseIndex;
    // R1208: env-gated build-entry bank probe (inert by default; capped).
    {
        static int s_buildLogs = 0;
        const char* phase = getenv("AB_DPAL_PHASE");
        if (phase && phase[0] == '1' && s_buildLogs < 600) {
            fprintf(stderr, "AB_DPAL_PHASE BUILD n=%d base=%#x\n", s_buildLogs, baseIndex);
            ++s_buildLogs;
        }
    }
    // R1209: AB_DPAL_BUILD_COUNT=1 logs entry base + exit changed-count (capped).
    // Cached getenv — not re-read on the hot path after first check.
    static int s_bcEnv = -1;
    static int s_bcLogs = 0;
    if (s_bcEnv < 0) {
        const char* e = getenv("AB_DPAL_BUILD_COUNT");
        s_bcEnv = (e && e[0] == '1') ? 1 : 0;
    }
    const bool logBc = s_bcEnv && s_bcLogs < 600;
    const uint32_t bcFlip = vs.flipCount;
    if (logBc) {
        fprintf(stderr, "AB_DPAL_BUILD_COUNT ENTER n=%d flip=%u base=%#x\n",
                s_bcLogs, bcFlip, baseIndex);
    }
    // R1214: AB_DPAL_BUILD_WHY=1 logs return addresses + render-state.
    // Windows via env (defaults cover flip≈7/10 + twin≈91): AB_DPAL_BUILD_WHY_A0/A1
    // and optional second window B0/B1. Cap via AB_DPAL_BUILD_WHY_CAP (default 40).
    // Only __builtin_return_address(0) (defined); deeper frames via backtrace().
    {
        static int s_whyEnv = -1;
        static int s_whyLogs = 0;
        static unsigned s_whyA0 = 5, s_whyA1 = 12, s_whyB0 = 88, s_whyB1 = 95;
        static int s_whyCap = 40;
        if (s_whyEnv < 0) {
            const char* e = getenv("AB_DPAL_BUILD_WHY");
            s_whyEnv = (e && e[0] == '1') ? 1 : 0;
            if (const char* p = getenv("AB_DPAL_BUILD_WHY_A0")) s_whyA0 = (unsigned)atoi(p);
            if (const char* p = getenv("AB_DPAL_BUILD_WHY_A1")) s_whyA1 = (unsigned)atoi(p);
            if (const char* p = getenv("AB_DPAL_BUILD_WHY_B0")) s_whyB0 = (unsigned)atoi(p);
            if (const char* p = getenv("AB_DPAL_BUILD_WHY_B1")) s_whyB1 = (unsigned)atoi(p);
            if (const char* p = getenv("AB_DPAL_BUILD_WHY_CAP")) s_whyCap = atoi(p);
            if (s_whyCap < 1) s_whyCap = 1;
        }
        const bool inWindow =
            (bcFlip >= s_whyA0 && bcFlip <= s_whyA1) ||
            (bcFlip >= s_whyB0 && bcFlip <= s_whyB1);
        if (s_whyEnv && inWindow && s_whyLogs < s_whyCap) {
            void* ra0 = __builtin_return_address(0);
            void* frames[4] = {ra0, nullptr, nullptr, nullptr};
#if defined(__GNUC__) && defined(__linux__)
            void* bt[5];
            const int nbt = backtrace(bt, 5);
            // bt[0] = here; bt[1] ≈ caller of DPalBuild (matches ra0); deeper = 2..4
            for (int i = 0; i < 3 && (i + 2) < nbt; ++i)
                frames[i + 1] = bt[i + 2];
#endif
            const unsigned rs = static_cast<unsigned>(g_bss8804[0xc]);
            const int m630 = static_cast<int>(static_cast<int8_t>(DATA_008630));
            fprintf(stderr,
                    "AB_DPAL_BUILD_WHY n=%d flip=%u rs809=%#x m630=%d "
                    "ra0=%p ra1=%p ra2=%p ra3=%p",
                    s_bcLogs, bcFlip, rs, m630,
                    frames[0], frames[1], frames[2], frames[3]);
#if defined(__GNUC__)
            Dl_info inf;
            auto note = [&](const char* tag, void* ra) {
                if (ra && dladdr(ra, &inf) && inf.dli_sname)
                    fprintf(stderr, " %s=%s", tag, inf.dli_sname);
            };
            note("s0", frames[0]);
            note("s1", frames[1]);
            note("s2", frames[2]);
            note("s3", frames[3]);
#endif
            fputc('\n', stderr);
            ++s_whyLogs;
        }
    }
    // R1209: ASM clears DATA_009143 after reverse allocation / before compare
    // (ffe.asm:83490). Default keeps historical start-of-build clear; HOLD_FIX
    // moves the reset to the ASM site (see Pass-2 prelude below).
    static int s_holdEnv = -1;
    if (s_holdEnv < 0) {
        const char* e = getenv("AB_DPAL_HOLD_FIX");
        s_holdEnv = (e && e[0] == '1') ? 1 : 0;
    }
    if (!s_holdEnv)
        vs.dPalChangedCount = 0;
    vs.dPalAllocCount = DPAL_ENTRIES;

    // ASM FUNC_001330 entry (83147-83152): DATA_009222 = DPal32to16(
    // DATA_009217) — the record's rgb16, which MARKS the cell BEFORE the
    // occupancy scan.
    const uint16_t fillRgb16 =
        static_cast<uint16_t>(ComputeRenderFillRgb16());
    DPalMarksDumpIfTarget();

    // R1212: AB_DPAL_OCC_DUMP=<dir> writes the pre-ScanBox occupancy grid
    // (dPalSecondary, 4096 B, 0/1 marks) after the fill stamp. Cap via
    // AB_DPAL_OCC_N (build indices, comma-separated; default first 4 builds).
    // Own counter — does not require BUILD_COUNT. Path env is read once.
    {
        static int s_occDumpEnv = -1;
        static char s_occDumpDir[512];
        static bool s_occNAny = true;
        // R1227: allow build indices past 64 (panel-return first-3D is ~n=101).
        static bool s_occNSet[256];
        static int s_occDumpBuild = 0;
        static int s_occDumpWrites = 0;
        if (s_occDumpEnv < 0) {
            s_occDumpEnv = 0;
            s_occDumpDir[0] = 0;
            memset(s_occNSet, 0, sizeof(s_occNSet));
            if (const char* d = getenv("AB_DPAL_OCC_DUMP")) {
                if (d[0]) {
                    strncpy(s_occDumpDir, d, sizeof(s_occDumpDir) - 1);
                    s_occDumpDir[sizeof(s_occDumpDir) - 1] = 0;
                    s_occDumpEnv = 1;
                }
            }
            if (const char* e = getenv("AB_DPAL_OCC_N")) {
                if (e[0]) {
                    s_occNAny = false;
                    const char* p = e;
                    while (*p) {
                        while (*p == ',' || *p == ' ')
                            ++p;
                        if (!*p)
                            break;
                        char* end = nullptr;
                        long v = strtol(p, &end, 0);
                        if (end == p)
                            break;
                        if (v >= 0 && v < 256)
                            s_occNSet[v] = true;
                        p = end;
                    }
                }
            }
        }
        const int occN = s_occDumpBuild++;
        if (s_occDumpEnv && s_occDumpWrites < 8 &&
            (s_occNAny ? (occN < 4) : (occN < 256 && s_occNSet[occN]))) {
            char path[640];
            snprintf(path, sizeof(path), "%s/port_occ_n%d.bin", s_occDumpDir,
                     occN);
            if (FILE* f = fopen(path, "wb")) {
                fwrite(vs.dPalSecondary, 1, DPAL_MAP_SIZE, f);
                fclose(f);
                int nz = 0;
                for (int i = 0; i < DPAL_MAP_SIZE; i++)
                    if (vs.dPalSecondary[i])
                        ++nz;
                fprintf(stderr,
                        "AB_DPAL_OCC_DUMP n=%d flip=%u nz=%d path=%s\n", occN,
                        vs.flipCount, nz, path);
                ++s_occDumpWrites;
            }
        }
    }

    // Dig: AB_1F_DPAL_FORCE_MARKS=<path> replaces occupancy with a pre-build
    // ASM marks grid (4096 bytes, 0/1) after the fill mark — isolates median
    // cut / install from mark-set drift (Mercury @ 0x160000: +7 cells from
    // FUNC_001839).
    // R1217: AB_1F_DPAL_FORCE_MARKS_N=<build[,...]> limits replacement to those
    // build indices (own counter); empty/absent = every build (historical).
    if (const char* fm = getenv("AB_1F_DPAL_FORCE_MARKS")) {
        if (fm[0]) {
            static int s_fmBuild = 0;
            static int s_fmNFilter = -1;
            static bool s_fmNSet[128];
            if (s_fmNFilter < 0) {
                s_fmNFilter = 0;
                memset(s_fmNSet, 0, sizeof(s_fmNSet));
                if (const char* e = getenv("AB_1F_DPAL_FORCE_MARKS_N")) {
                    if (e[0]) {
                        s_fmNFilter = 1;
                        const char* p = e;
                        while (*p) {
                            while (*p == ',' || *p == ' ')
                                ++p;
                            if (!*p)
                                break;
                            char* end = nullptr;
                            long v = strtol(p, &end, 0);
                            if (end == p)
                                break;
                            if (v >= 0 && v < 128)
                                s_fmNSet[v] = true;
                            p = end;
                        }
                    }
                }
            }
            const int fmBuild = s_fmBuild++;
            const bool doForce =
                !s_fmNFilter || (fmBuild < 128 && s_fmNSet[fmBuild]);
            if (doForce) {
                if (FILE* f = fopen(fm, "rb")) {
                    uint8_t grid[DPAL_MAP_SIZE];
                    const size_t n = fread(grid, 1, DPAL_MAP_SIZE, f);
                    fclose(f);
                    if (n == DPAL_MAP_SIZE) {
                        memcpy(vs.dPalSecondary, grid, DPAL_MAP_SIZE);
                        // Keep the fill mark (ASM entry always stamps it).
                        const uint16_t fi =
                            static_cast<uint16_t>(fillRgb16 & 0xfffu);
                        if (vs.dPalMap[fi] == 0)
                            vs.dPalSecondary[fi] = 1;
                        static int s_fmN;
                        if (s_fmN < 4) {
                            int occC = 0;
                            for (int i = 0; i < DPAL_MAP_SIZE; i++)
                                if (vs.dPalSecondary[i])
                                    ++occC;
                            fprintf(stderr,
                                    "AB-DPAL force-marks: n=%d %s occ=%d "
                                    "fill=0x%x\n",
                                    fmBuild, fm, occC, (unsigned)fi);
                            ++s_fmN;
                        }
                    }
                }
            }
        }
    }

    // The occupancy (dPalSecondary) = the marks made by DPal32to16 since the
    // last DPalClear (FUNC_001786 zeroes it per frame). Do NOT clear it here,
    // and do NOT clear dPalMap: the ASM's DATA_007770 is a static seed table
    // (126 entries) that gates the marks and is never written at runtime.

    // DATA_009144 (dPalRGB) PERSISTS across builds — ASM FUNC_001330 never
    // reloads it from the static 128. Re-seeding from g_videoState->palette
    // every frame made Pass 1 (base=0x40) first-hit against static[64..127]
    // oranges, so the high-bank fill slot became (166,85,52) while dumps at
    // base=0 stayed dark. Initial seed is LoadGamePalette → dPalRGB.
    (void)palette;
    // Dig: AB_DPAL_SEED_RGB=<file> loads 384 B of 6-bit triples into dPalRGB[0..63]
    // (and [64..127] if file longer) once, to emulate ASM warm history under
    // INTRO_START_CLOCK cold start.
    if (const char* seed = getenv("AB_DPAL_SEED_RGB")) {
        static int s_seeded;
        if (!s_seeded && seed[0]) {
            FILE* f = fopen(seed, "rb");
            if (f) {
                uint8_t buf[384];
                const size_t n = fread(buf, 1, sizeof(buf), f);
                fclose(f);
                if (n >= 192) {
                    const int slots = (int)(n / 3);
                    for (int i = 0; i < slots && i < 128; i++) {
                        vs.dPalRGB[i * 3 + 0] = buf[i * 3 + 0];
                        vs.dPalRGB[i * 3 + 1] = buf[i * 3 + 1];
                        vs.dPalRGB[i * 3 + 2] = buf[i * 3 + 2];
                    }
                    fprintf(stderr, "AB-DPAL seed-rgb: %d slots from %s\n", slots, seed);
                    s_seeded = 1;
                }
            }
        }
    }

    // ASM dense stamp (JUMP_005257) and sparse box alloc (JUMP_005265) zero
    // DATA_009141[cluster] before (re)assignment. Without this, reverse slots
    // freeze after the first build and bank ping-pong cannot retarget fill.
    // 4.380 FIX: the ASM keeps the reverse-map TAIL across builds - cells marked
    // in earlier frames retain their assignment (measured: 63 entries vs this
    // port's 33 at the same cell count, 4.379). Clearing it here made the grid
    // paint 184 instead of the reference's 248 and cost 6902 residual pixels;
    // retaining it gives grid 3534 px at 248 and residual 1756.
    // The per-build refresh the ASM does perform is the per-box flag byte at
    // DATA_009141 (zeroed for every box in the stamping walk, 4.336), which
    // DPalReverse's boxing loop already handles via dPalReverse[bi + 1].
    //
    // 4.390 SCOPE: retaining the tail across a SCENE CHANGE degrades the water
    // oracle (tv 0.0054->0.1147, nearest 0.52->3.93 against limits 0.25/3,
    // capture_port_landed.sh:400). Keep the tail only while the marked-cell set
    // is stable; a different cell count means the scene changed and the stale
    // cluster ids no longer describe it, so reset there.
    {
        // 4.404: the discriminator is BIT 0 of g_bss8804[0x0c] - the same bit
        // the game loop uses to choose DrawMission over the DPalBuild chain.
        // Measured: the water capture runs with 0xFF (bit 0 set) and never
        // calls DPalBuild at all (0 hits, 4.402), so its palette is leftover
        // state and the tail must not persist; the star map runs 0xFE (bit 0
        // clear) and rebuilds every frame, where the tail is what the ASM keeps
        // (4.380: F3 1756/3534@248). Retain only on the build path.
        const bool buildsHere = s_dpalSceneBuilds;
        if (!buildsHere) {
            memset(vs.dPalReverse, 0, sizeof(vs.dPalReverse));
        }
    }
    // Dig: AB_DPAL_COLD_RGB=1 zeroes dPalRGB once so the first Pass1 misses
    // and Pass2 LIFO writes reverse bf…80 (ASM warm end-state under cold
    // INTRO_START_CLOCK). Later builds keep persist.
    // R1210: AB_DPAL_REV_FIX=1 also forces a one-shot cold clear here so any
    // pre-build seed path (LoadGamePalette or otherwise) cannot create the
    // first high-bank Pass1 hit (port nFree=63 vs ASM nFree=64 @ frame 1).
    // R1211: AB_DPAL_BOOT_LAW=1 includes the same cold clear.
    {
        static int s_coldEnv = -1;
        if (s_coldEnv < 0) {
            const char* c = getenv("AB_DPAL_COLD_RGB");
            const char* r = getenv("AB_DPAL_REV_FIX");
            const char* b = getenv("AB_DPAL_BOOT_LAW");
            s_coldEnv = ((c && c[0] == '1') || (r && r[0] == '1') ||
                         (b && b[0] == '1'))
                            ? 1
                            : 0;
        }
        if (s_coldEnv) {
            static int s_cold;
            if (!s_cold) {
                memset(vs.dPalRGB, 0, sizeof(vs.dPalRGB));
                s_cold = 1;
            }
        }
    }

    uint8_t* occ = vs.dPalSecondary;

    Box boxes[64];
    int nBoxes = 0;

    Box root{};
    root.r0 = 0;
    root.r1 = 0x10;
    root.g0 = 0;
    root.g1 = 0x10;
    root.b0 = 0;
    root.b1 = 0x10;
    const int occupied = ScanBox(root, occ);

    if (occupied < 0x41) {
        // Sparse path: one 1×1×1 box per occupied cell.
        for (int idx = 0; idx < DPAL_MAP_SIZE && nBoxes < DPAL_ENTRIES; idx++) {
            if (occ[idx] == 0)
                continue;
            const int r = (idx >> 8) & 0xF;
            const int g = (idx >> 4) & 0xF;
            const int b = idx & 0xF;
            Box& box = boxes[nBoxes];
            box.r0 = static_cast<int16_t>(r);
            box.r1 = static_cast<int16_t>(r + 1);
            box.g0 = static_cast<int16_t>(g);
            box.g1 = static_cast<int16_t>(g + 1);
            box.b0 = static_cast<int16_t>(b);
            box.b1 = static_cast<int16_t>(b + 1);
            box.extent = 1;
            box.axis = 0;
            occ[idx] = static_cast<uint8_t>(nBoxes + 1); // 1-based cluster
            nBoxes++;
        }
    } else {
        boxes[0] = root;
        // 1-based max-heap of box indices (ASM [edi+esi*4] pointer heap).
        int heap[DPAL_ENTRIES + 1];
        heap[1] = 0;
        int n = 1; // box count (== ebp)

        while (n < DPAL_ENTRIES) {
            const int parentIdx = heap[1];
            Box& parent = boxes[parentIdx];

            // Sift hole from root (ASM JUMP_005243..005246): promote larger
            // child; on equal extents take the left child (jl → else branch).
            int hole = 1;
            while (hole * 2 <= n) {
                int child = hole * 2;
                if (child < n) {
                    const int16_t le = boxes[heap[child]].extent;
                    const int16_t re = boxes[heap[child + 1]].extent;
                    if (le < re)
                        child = child + 1;
                }
                heap[hole] = heap[child];
                hole = child;
            }

            // Child slot = boxes[n]; split parent on axis (ASM JUMP_005247-9).
            Box& child = boxes[n];
            child = parent;
            if (parent.axis == 0) {
                const int16_t mid =
                    static_cast<int16_t>((parent.r0 + parent.r1) >> 1);
                child.r0 = mid;
                parent.r1 = mid;
            } else if (parent.axis == 1) {
                const int16_t mid =
                    static_cast<int16_t>((parent.g0 + parent.g1) >> 1);
                child.g0 = mid;
                parent.g1 = mid;
            } else {
                const int16_t mid =
                    static_cast<int16_t>((parent.b0 + parent.b1) >> 1);
                child.b0 = mid;
                parent.b1 = mid;
            }

            ScanBox(parent, occ);
            ScanBox(child, occ); // keep empty halves — ASM always allocates

            // Re-insert parent at hole, bubble up.
            heap[hole] = parentIdx;
            int esi = hole;
            while (esi > 1) {
                const int par = esi >> 1;
                if (boxes[heap[par]].extent < boxes[heap[esi]].extent) {
                    const int tmp = heap[par];
                    heap[par] = heap[esi];
                    heap[esi] = tmp;
                    esi = par;
                } else {
                    break;
                }
            }

            // Insert child at heap[n+1], bubble up.
            esi = n + 1;
            heap[esi] = n;
            while (esi > 1) {
                const int par = esi >> 1;
                if (boxes[heap[par]].extent < boxes[heap[esi]].extent) {
                    const int tmp = heap[par];
                    heap[par] = heap[esi];
                    heap[esi] = tmp;
                    esi = par;
                } else {
                    break;
                }
            }
            n++;
        }
        nBoxes = DPAL_ENTRIES;

        // Stamp cluster id (1-based) into every cell of each box.
        for (int bi = 0; bi < nBoxes; bi++) {
            const Box& box = boxes[bi];
            const uint8_t cid = static_cast<uint8_t>(bi + 1);
            for (int r = box.r0; r < box.r1; r++) {
                for (int g = box.g0; g < box.g1; g++) {
                    for (int b = box.b0; b < box.b1; b++) {
                        occ[(r << 8) | (g << 4) | b] = cid;
                    }
                }
            }
        }
    }

    // Pass 1: map each working palette entry into its cluster's reverse slot.
    // ASM: DATA_009140[cluster] = 0x80 | (base + i) (first-hit marking, scan
    // ascending over the active half palette[base..base+63]). Entries whose
    // cluster is 0 or already assigned go to the free list (ascending push →
    // LIFO pops descending, ASM JUMP_005269).
    // R1129: DATA_009140 is `resb 1` and DATA_009141 (`resb 64`) follows it, so
    // the reverse map's slot c >= 1 IS DATA_009141[c-1]. Both box walks clear
    // `byte [DATA_009141 + box]` (sparse JUMP_005265 `mov byte [ebx],0x0`, dense
    // JUMP_005257 `mov byte [eax],0x0`) - i.e. reverse[box+1] for every box of
    // this build - before Pass 1. Only slots above nBoxes survive (the 4.380
    // tail). Without this a stale slot made Pass 1 skip a matching entry.
    for (int bi = 0; bi < nBoxes && bi + 1 < static_cast<int>(sizeof(vs.dPalReverse)); bi++)
        vs.dPalReverse[bi + 1] = 0;
    // R1217: AB_DPAL_PASS1_FIX=1 is reserved/default-off. Pass1 claim law already
    // matches ASM (ffe.asm:83452-83478 / claim iff occ!=0 && reverse[occ]==0).
    // The flip-270 nFree=14 fork is pre-ScanBox: 6 B↔B+1 marks from
    // JUMP_008083 (FUNC_001879). No Pass1-side rewrite until that mark B is
    // corrected — enabling the knob is currently a no-op (dig latch only).
    {
        static int s_p1Fix = -1;
        if (s_p1Fix < 0) {
            const char* e = getenv("AB_DPAL_PASS1_FIX");
            s_p1Fix = (e && e[0] == '1') ? 1 : 0;
        }
        (void)s_p1Fix;
    }
    // R1217: AB_DPAL_PASS1_DUMP=<dir> dumps Pass1 inputs after cid stamp +
    // reverse-box clear, before any claim writes: post-cid occ[4096],
    // reverse[65], per-slot claim/free decisions. Window via AB_DPAL_PASS1_N
    // (build indices; default 6). Cap 4 dumps. Own counter (no BUILD_COUNT).
    {
        static int s_p1Env = -1;
        static char s_p1Dir[512];
        static bool s_p1NSet[128];
        static int s_p1Build = 0;
        static int s_p1Writes = 0;
        if (s_p1Env < 0) {
            s_p1Env = 0;
            s_p1Dir[0] = 0;
            memset(s_p1NSet, 0, sizeof(s_p1NSet));
            if (const char* d = getenv("AB_DPAL_PASS1_DUMP")) {
                if (d[0]) {
                    strncpy(s_p1Dir, d, sizeof(s_p1Dir) - 1);
                    s_p1Dir[sizeof(s_p1Dir) - 1] = 0;
                    s_p1Env = 1;
                }
            }
            // Default window = build 6 (F1-restore under GUARD+BOOT+SHIFT).
            s_p1NSet[6] = true;
            if (const char* e = getenv("AB_DPAL_PASS1_N")) {
                if (e[0]) {
                    memset(s_p1NSet, 0, sizeof(s_p1NSet));
                    const char* p = e;
                    while (*p) {
                        while (*p == ',' || *p == ' ')
                            ++p;
                        if (!*p)
                            break;
                        char* end = nullptr;
                        long v = strtol(p, &end, 0);
                        if (end == p)
                            break;
                        if (v >= 0 && v < 128)
                            s_p1NSet[v] = true;
                        p = end;
                    }
                }
            }
        }
        const int p1N = s_p1Build++;
        if (s_p1Env && s_p1Writes < 4 && p1N < 128 && s_p1NSet[p1N]) {
            char path[640];
            snprintf(path, sizeof(path), "%s/port_pass1_occ_n%d.bin", s_p1Dir,
                     p1N);
            if (FILE* f = fopen(path, "wb")) {
                fwrite(occ, 1, DPAL_MAP_SIZE, f);
                fclose(f);
            }
            snprintf(path, sizeof(path), "%s/port_pass1_rev_n%d.bin", s_p1Dir,
                     p1N);
            if (FILE* f = fopen(path, "wb")) {
                fwrite(vs.dPalReverse, 1, 65, f);
                fclose(f);
            }
            snprintf(path, sizeof(path), "%s/port_pass1_boxes_n%d.txt", s_p1Dir,
                     p1N);
            if (FILE* f = fopen(path, "w")) {
                fprintf(f, "nBoxes=%d occupied=%d base=%d flip=%u\n", nBoxes,
                        occupied, baseIndex, bcFlip);
                for (int bi = 0; bi < nBoxes; bi++) {
                    const Box& box = boxes[bi];
                    fprintf(f,
                            "bi=%d ext=%d axis=%d r=%d:%d g=%d:%d b=%d:%d\n", bi,
                            (int)box.extent, (int)box.axis, (int)box.r0,
                            (int)box.r1, (int)box.g0, (int)box.g1, (int)box.b0,
                            (int)box.b1);
                }
                fclose(f);
            }
            // Per-slot decisions with reverse state *entering* each iteration
            // (before this slot's claim write).
            uint8_t revSnap[65];
            memcpy(revSnap, vs.dPalReverse, 65);
            int revNz = 0;
            for (int i = 0; i < 65; i++)
                if (revSnap[i])
                    ++revNz;
            fprintf(stderr,
                    "AB_DPAL_PASS1_DUMP n=%d flip=%u base=%#x nBoxes=%d "
                    "revNZ=%d path=%s\n",
                    p1N, bcFlip, baseIndex, nBoxes, revNz, s_p1Dir);
            for (int i = 0; i < DPAL_ENTRIES; i++) {
                const int src = baseIndex + i;
                const uint8_t r6 = vs.dPalRGB[src * 3 + 0];
                const uint8_t g6 = vs.dPalRGB[src * 3 + 1];
                const uint8_t b6 = vs.dPalRGB[src * 3 + 2];
                const uint16_t idx = Dac6To444Index(r6, g6, b6);
                const uint8_t cluster = occ[idx];
                const uint8_t revIn =
                    (cluster < 65) ? revSnap[cluster] : 0;
                const char* why;
                if (cluster == 0)
                    why = "a_occ0";
                else if (revIn != 0)
                    why = "b_revNZ";
                else
                    why = "claim";
                fprintf(stderr,
                        "AB_DPAL_PASS1_SLOT n=%d i=%d idx=%u cid=%u revIn=%02x "
                        "rgb=%02x%02x%02x %s\n",
                        p1N, i, (unsigned)idx, (unsigned)cluster,
                        (unsigned)revIn, r6, g6, b6, why);
                // Advance snap as Pass1 would (so later slots see prior claims).
                if (cluster != 0 && revIn == 0 && cluster < 65)
                    revSnap[cluster] =
                        static_cast<uint8_t>(0x80 + (baseIndex + i));
            }
            ++s_p1Writes;
        }
    }
    uint8_t freeList[64];
    int nFree = 0;
    for (int i = 0; i < DPAL_ENTRIES; i++) {
        const int src = baseIndex + i; // working bank = palette[base..base+63]
        // DATA_009144 is a 6-bit DAC store {R,G,B}; ASM Pass1 uses >>2 per
        // channel (ffe.asm JUMP_005269 / Ghidra 001330).
        const uint16_t idx =
            Dac6To444Index(vs.dPalRGB[src * 3 + 0], vs.dPalRGB[src * 3 + 1],
                           vs.dPalRGB[src * 3 + 2]);
        const uint8_t cluster = occ[idx];
        if (cluster == 0 || vs.dPalReverse[cluster] != 0) {
            freeList[nFree++] = static_cast<uint8_t>(i);
        } else {
            // ASM ffe.asm:83478-83481: `mov ecx,esi; add cl,[DATA_007634];
            // add cl,0x80; mov [eax+DATA_009140],cl` — WRAPPING byte adds,
            // not an OR: for bank+slot >= 0x80 the two forms diverge.
            vs.dPalReverse[cluster] =
                static_cast<uint8_t>(0x80 + (baseIndex + i));
        }
    }

    // Pass 2: unassigned boxes take free slots (LIFO — ASM dec [esp+0x14]);
    // then EVERY box, kept by Pass 1 or not, gets its Q2 centroid written into
    // dPalRGB at its RELATIVE slot (ASM JUMP_005271 assigns only when
    // DATA_009141[box] == 0 and falls through to JUMP_005272 for all boxes).
    // The ASM indexes DATA_009144[(VGA-0x80)*3], and its changed list
    // (DATA_009142) stores that relative slot too.
    // JJFFE_PRE444_DPAL=1 (A/B): skip the boxes Pass 1 kept, so a kept slot
    // shows its history colour (pre-4.44).
    static const bool s_pre444 = [] {
        const char* e = getenv("JJFFE_PRE444_DPAL");
        return e && e[0] == '1';
    }();
    // R1210: AB_DPAL_BUILD_FILL=1 dumps Pass2 inputs (freeList + per-box
    // extent/centroid/occ/reverse). Frame window via AB_DPAL_FILL_N (build
    // indices, comma-separated); empty = every BUILD_COUNT-logged EXIT.
    static int s_fillEnv = -1;
    static int s_fillLogs = 0;
    if (s_fillEnv < 0) {
        const char* e = getenv("AB_DPAL_BUILD_FILL");
        s_fillEnv = (e && e[0] == '1') ? 1 : 0;
    }
    static bool s_fillNInit = false;
    static bool s_fillNAny = true;
    static bool s_fillNSet[600];
    if (!s_fillNInit) {
        s_fillNInit = true;
        memset(s_fillNSet, 0, sizeof(s_fillNSet));
        const char* e = getenv("AB_DPAL_FILL_N");
        if (e && e[0]) {
            s_fillNAny = false;
            const char* p = e;
            while (*p) {
                while (*p == ',' || *p == ' ')
                    ++p;
                if (!*p)
                    break;
                char* end = nullptr;
                long v = strtol(p, &end, 0);
                if (end == p)
                    break;
                if (v >= 0 && v < 600)
                    s_fillNSet[v] = true;
                p = end;
            }
        }
    }
    // Build index for FILL matches BUILD_COUNT's n (same counter).
    const int fillN = s_bcLogs;
    const bool logFill = s_fillEnv && s_fillLogs < 32 &&
                         (s_fillNAny || (fillN < 600 && s_fillNSet[fillN]));
    if (logFill) {
        fprintf(stderr, "AB_DPAL_BUILD_FILL n=%d flip=%u base=%#x nFree=%d free=",
                fillN, bcFlip, baseIndex, nFree);
        for (int i = 0; i < nFree; i++) {
            if (i)
                fputc(',', stderr);
            fprintf(stderr, "%u", static_cast<unsigned>(freeList[i]));
        }
        fputc('\n', stderr);
        // Working-bank dPalRGB (Pass1 input) — 64×RGB6 hex.
        fputs("AB_DPAL_BUILD_RGB n=", stderr);
        fprintf(stderr, "%d ", fillN);
        for (int i = 0; i < DPAL_ENTRIES; i++) {
            const int src = baseIndex + i;
            fprintf(stderr, "%02x%02x%02x", vs.dPalRGB[src * 3 + 0],
                    vs.dPalRGB[src * 3 + 1], vs.dPalRGB[src * 3 + 2]);
        }
        fputc('\n', stderr);
        for (int bi = 0; bi < nBoxes; bi++) {
            const Box& box = boxes[bi];
            const uint8_t cr6 = Q2Centroid6(box.r0, box.r1);
            const uint8_t cg6 = Q2Centroid6(box.g0, box.g1);
            const uint8_t cb6 = Q2Centroid6(box.b0, box.b1);
            const uint16_t cidx = Dac6To444Index(cr6, cg6, cb6);
            const uint8_t oc = occ[cidx];
            fprintf(stderr,
                    "AB_DPAL_BUILD_BOX n=%d bi=%d ext=%d "
                    "r=%d:%d g=%d:%d b=%d:%d c444=%u occ=%u revP1=%02x\n",
                    fillN, bi, (int)box.extent, (int)box.r0, (int)box.r1,
                    (int)box.g0, (int)box.g1, (int)box.b0, (int)box.b1,
                    (unsigned)cidx, (unsigned)oc,
                    (unsigned)vs.dPalReverse[bi + 1]);
        }
        ++s_fillLogs;
    }
    // R1209: ASM ffe.asm:83490 — `xor eax,eax; mov [DATA_009143],eax` sits
    // AFTER the reverse-bank allocation loop and IMMEDIATELY before the
    // per-box compare/mark. AB_DPAL_HOLD_FIX=1 matches that site (default off).
    if (s_holdEnv)
        vs.dPalChangedCount = 0;
    // R1211: AB_DPAL_BUILD_MARK=1 logs each changed-mark store (rel, compared
    // triple, previous triple, stored triple). Cap via AB_DPAL_MARK_N (default
    // 30). Own build counter — does not require BUILD_COUNT.
    static int s_markEnv = -1;
    static int s_markLogs = 0;
    static int s_markCap = 30;
    static int s_markBuildN = 0;
    if (s_markEnv < 0) {
        const char* e = getenv("AB_DPAL_BUILD_MARK");
        s_markEnv = (e && e[0] == '1') ? 1 : 0;
        if (const char* c = getenv("AB_DPAL_MARK_N")) {
            if (c[0]) {
                char* end = nullptr;
                long v = strtol(c, &end, 0);
                if (end != c && v > 0 && v < 10000)
                    s_markCap = static_cast<int>(v);
            }
        }
    }
    const int markN = s_markBuildN++;
    // R1211 Candidate A dig: AB_DPAL_MARK_LAW=1 stores R|G as a 16-bit word
    // then B alone (ffe.asm:83550-83553). Default keeps three explicit stores
    // (identical when cmp R/G are the stack bytes the word would load).
    static int s_markLaw = -1;
    if (s_markLaw < 0) {
        const char* e = getenv("AB_DPAL_MARK_LAW");
        s_markLaw = (e && e[0] == '1') ? 1 : 0;
    }
    int freeCursor = nFree;
    for (int bi = 0; bi < nBoxes; bi++) {
        if (vs.dPalReverse[bi + 1] == 0) {
            if (freeCursor <= 0)
                break;
            const uint8_t slot = freeList[--freeCursor]; // free working entry 0..63
            vs.dPalReverse[bi + 1] =
                static_cast<uint8_t>(0x80 + (slot + baseIndex)); // ASM wrapping add (r28 same class as 83478 fix)
        } else if (s_pre444) {
            continue;
        }
        const uint8_t rel =
            static_cast<uint8_t>((vs.dPalReverse[bi + 1] & 0x7f)); // 0..127

        const Box& box = boxes[bi];
        // The store is {R,G,B}: the box's r0/r1 bounds are the <<8 component
        // (R), g0/g1 the <<4 one, b0/b1 the low nibble — matching the cell
        // convention (R<<8 | G<<4 | B) that ScanBox and DPal32to16 both use.
        const uint8_t cr6 = Q2Centroid6(box.r0, box.r1);
        const uint8_t cg6 = Q2Centroid6(box.g0, box.g1);
        const uint8_t cb6 = Q2Centroid6(box.b0, box.b1);
        // ASM writes the 6-bit DAC centroid straight into DATA_009144; expand
        // only when installing into the VGA bank (VideoSetPalValue << 2).
        if (vs.dPalRGB[rel * 3 + 0] != cr6 ||
            vs.dPalRGB[rel * 3 + 1] != cg6 ||
            vs.dPalRGB[rel * 3 + 2] != cb6) {
            // The changed list stores the RELATIVE slot (0..127) — DPalSet
            // installs it at rel + 0x80.
            vs.dPalChanged[vs.dPalChangedCount++] = rel;
            const uint8_t prev0 = vs.dPalRGB[rel * 3 + 0];
            const uint8_t prev1 = vs.dPalRGB[rel * 3 + 1];
            const uint8_t prev2 = vs.dPalRGB[rel * 3 + 2];
            if (s_markLaw) {
                // ffe.asm:83550-83553 — mov ax,[esp+0x10]; mov [DATA_009144],ax
                // then mov al,[esp+0x12]; mov [DATA_009146],al. With stack
                // bytes = (cr6,cg6,cb6) this equals the three-byte store.
                const uint16_t rg = static_cast<uint16_t>(cr6) |
                                    (static_cast<uint16_t>(cg6) << 8);
                memcpy(&vs.dPalRGB[rel * 3 + 0], &rg, sizeof(rg));
                vs.dPalRGB[rel * 3 + 2] = cb6;
            } else {
                vs.dPalRGB[rel * 3 + 0] = cr6;
                vs.dPalRGB[rel * 3 + 1] = cg6;
                vs.dPalRGB[rel * 3 + 2] = cb6;
            }
            if (s_markEnv && s_markLogs < s_markCap) {
                fprintf(stderr,
                        "AB_DPAL_BUILD_MARK n=%d flip=%u bi=%d rel=%u "
                        "cmp=%02x%02x%02x prev=%02x%02x%02x store=%02x%02x%02x\n",
                        markN, bcFlip, bi, static_cast<unsigned>(rel),
                        cr6, cg6, cb6, prev0, prev1, prev2,
                        vs.dPalRGB[rel * 3 + 0], vs.dPalRGB[rel * 3 + 1],
                        vs.dPalRGB[rel * 3 + 2]);
                ++s_markLogs;
            }
        }
    }
    // Unassigned boxes after freeCursor exhaustion keep reverse[bi+1]==0 → VGA0.

    // Install allocated RGB into the dynamic VGA bank without DPalSet's
    // baseIndex ^= 0x40 flip (Build is not a frame toggle). The changed
    // entries = relative slots (0..127); VGA byte = rel + 0x80. The store is
    // {R,G,B} 6-bit — expand to 8-bit for SetPaletteEntry (ASM << 2).
    for (int i = 0; i < vs.dPalChangedCount; i++) {
        const uint8_t rel = vs.dPalChanged[i];
        SetPaletteEntry(static_cast<uint8_t>(rel + 0x80),
                        Expand6to8(vs.dPalRGB[rel * 3 + 0]),
                        Expand6to8(vs.dPalRGB[rel * 3 + 1]),
                        Expand6to8(vs.dPalRGB[rel * 3 + 2]));
    }

    // ASM FUNC_001330 tail (JUMP_005275): resolve the entry's rgb16 through
    // the freshly built tables: DATA_009222 = DPal16toPal(rgb16).
    SetRenderFillColor(DPal16toPal(fillRgb16));

    if (logBc) {
        fprintf(stderr, "AB_DPAL_BUILD_COUNT EXIT n=%d flip=%u base=%#x chg=%d",
                s_bcLogs, bcFlip, baseIndex, vs.dPalChangedCount);
        // Dump the changed-slot list when AB_DPAL_BUILD_LIST=1 (still capped).
        static int s_listEnv = -1;
        if (s_listEnv < 0) {
            const char* e = getenv("AB_DPAL_BUILD_LIST");
            s_listEnv = (e && e[0] == '1') ? 1 : 0;
        }
        if (s_listEnv) {
            fputs(" list=", stderr);
            for (int i = 0; i < vs.dPalChangedCount; i++) {
                if (i) fputc(',', stderr);
                fprintf(stderr, "%u", static_cast<unsigned>(vs.dPalChanged[i]));
            }
        }
        // AB_DPAL_BUILD_REV=1: dump reverse[0..64] hex on every logged EXIT.
        static int s_revEnv = -1;
        if (s_revEnv < 0) {
            const char* e = getenv("AB_DPAL_BUILD_REV");
            s_revEnv = (e && e[0] == '1') ? 1 : 0;
        }
        if (s_revEnv) {
            fputs(" rev=", stderr);
            for (int i = 0; i < 65; i++)
                fprintf(stderr, "%02x", vs.dPalReverse[i]);
        }
        fputc('\n', stderr);
        ++s_bcLogs;
    }

    if (getenv("AB_1F_DPAL_TRACE")) {
        fprintf(stderr,
                "AB-DPAL build: clock=0x%x occ=%d boxes=%d base=%d sec4=%d rev24=0x%02x "
                "toPal4=0x%02x 9222=0x%02x\n",
                (unsigned)GetGameClock(),
                occupied, nBoxes, baseIndex, (int)vs.dPalSecondary[4],
                (int)vs.dPalReverse[24], (int)DPal16toPal(4),
                (int)(uint8_t)GetRenderFillColor());
    }
}

} // namespace jjffe
