#include "jjffe/world.h"

#include "jjffe/core/gameplay.h"
#include "jjffe/data.h"
#include "jjffe/data_loader.h"
#include "jjffe/math.h"
#include "jjffe/player_pad.h"
#include "jjffe/ship_model.h"
#include "jjffe/sysgen_proc.h"
#include "jjffe/traffic_spawn.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cmath>

namespace jjffe {

// R1226: place spawned bodies at their FUNC_000927 slot indices (Hope@108) so
// world index == +0x86 == cam+0x56, matching ASM's pool (no dense n+k remap).
// Default ON; escape hatch AB_SLOT_ORDER_FIX=0 restores dense n+k remap.
// (R1224/R1225 keep-+0x86 opt-in retired in R1228 — ORDER supersedes; alone
// it failed CameraFfeIdentity 4.43.)
static bool AbSlotOrderFix()
{
    static int s = -1;
    if (s < 0) {
        const char* e = std::getenv("AB_SLOT_ORDER_FIX");
        s = (e && e[0] == '0') ? 0 : 1;
    }
    return s != 0;
}

static void R1224LogW86World(const char* site, PhysObj* obj, uint8_t oldv, uint8_t newv)
{
    struct St {
        int on;
        int modelFilt;
        int cap;
        int logs;
    };
    static St s = {-1, -1, 0, 0};
    if (s.on < 0) {
        const char* e = std::getenv("R1224_W86");
        s.on = (e && e[0] == '1') ? 1 : 0;
        s.cap = 4096;
        s.modelFilt = -1;
        if (const char* p = std::getenv("R1224_W86_CAP"))
            if (p[0]) s.cap = std::atoi(p);
        if (const char* p = std::getenv("R1224_W86_MODEL"))
            if (p[0]) s.modelFilt = std::atoi(p);
    }
    if (!s.on || s.logs >= s.cap || !obj) return;
    const int32_t model = PhysObjGetModel(obj);
    if (s.modelFilt >= 0 && static_cast<int>(model) != s.modelFilt) return;
    std::fprintf(stderr,
                 "R1224_W86 n=%d clock=0x0 site=%s obj=%p model=%d %u->%u\n",
                 s.logs, site, static_cast<void*>(obj->raw), static_cast<int>(model),
                 static_cast<unsigned>(oldv), static_cast<unsigned>(newv));
    ++s.logs;
}

// ASM helpers (core.cpp) — orbital materialize / advance (FUNC_000880 / 000866).
void FUNC_001506(int32_t* param_1, int32_t param_2, int32_t param_3);
void FUNC_001507(intptr_t param_1, uint32_t param_2, uint32_t param_3, uint32_t param_4);
void FUNC_001675_MatBuildYZT(int32_t* param_1, int32_t param_2, int32_t param_3);
uint32_t FUNC_001658_Vec64Truncate(uint32_t* param_1, int param_2);
void FUNC_001669_VecMatMul(int* param_1, uint32_t* param_2, uint32_t* param_3);
void FUNC_001517(uint32_t* param_1, int32_t* param_2, int32_t param_3);
void FUNC_001672_MatMatMul(int* param_1, uint32_t* param_2, uint32_t* param_3);
void FUNC_001673_MatMatTMul(int* param_1, uint32_t* param_2, uint32_t* param_3);
uint32_t* FUNC_001510(uint32_t* param_1, uint32_t param_2, uint32_t param_3,
                      uint32_t param_4, int32_t param_5, uint32_t param_6);
uint32_t FUNC_001496(uint32_t param_1);
uint32_t FUNC_001483(uint32_t param_1, uint32_t param_2);
uint32_t FUNC_001494(uint32_t param_1);
void FUNC_001661_Vec64Add(intptr_t param_1, intptr_t param_2);

static SysObj  g_worldSys[kWorldBodyCapacity];
static PhysObj g_worldPhys[kWorldBodyCapacity];
static int     g_worldCount = 0;

// Task 4.5: int32 16.16 render-space positions (parent-chain accumulated after
// scaling). Separate from FFE Vec64 at PhysObj+0x26/+0x3e which stay planet-scale.
static int32_t g_renderPos[kWorldBodyCapacity][3];
static int     g_gatewayIndex = -1;  // first index of spawn-system body range
static int     g_gatewayStarIndex = -1;  // Gateway star PhysObj index (light vector)
static int32_t g_maxOrbRadius = 0;

// Task 4.18: the Gateway system is the generated cold-boot table (sysgen_proc).
// Its render space is FFE >> g_renderShift about the spawn planet (Hope at the
// origin, as before). The shift is the planet renderer's own convention:
// PlanetSurfaceRadiusUnits = radius << 14 while FFE radius = radius << (s14+s18).
static int     g_anchorIndex = -1;
static int     g_renderShift = 0;
static int64_t g_anchorRel[kWorldBodyCapacity][3];
static PhysObj g_spawnPlayer;
static bool    g_spawnPlayerValid = false;
static PhysObj g_worldShips[kSpawnSlotCount];
static int     g_worldShipCount = 0;
static uint64_t g_renderPosReads = 0;
static uint64_t g_anchorRelReads = 0;
// Beyond this render distance a body keeps its direction only (int32 space).
static constexpr int64_t kMaxGatewayRender = int64_t{1} << 30;

// Largest orbital radius maps to this render distance (system-wide factor).
static constexpr int32_t kMaxRenderOrbit = 500 * FP_ONE;

// FUNC_001507 → FUNC_001506 → Sin32 needs the blob sine table.
static bool EnsureOrbitMathReady()
{
    if (data::GetSineTable()) return true;
    static const char* kPaths[] = {
        "data_blob/data_blob.bin",
        "../data_blob/data_blob.bin",
        "../../data_blob/data_blob.bin",
    };
    for (const char* path : kPaths) {
        if (data::LoadDataBlob(path) && data::GetSineTable()) return true;
    }
    return false;
}

// Port orbit step per frame (the pre-4.37 DATA_008804 step; the time of day
// now follows DATA_009155, the orbits do not yet).
static constexpr uint32_t kOrbitFrameTicks = 0x1000u;

// FUNC_000880 angular-velocity derivation into PhysObj+0xc2
// (ffe.asm JUMP_003675 / csysgen F880 / decompiled FUNC_000880.c):
//   1496(orbVel) → 1483(0xffee5460, ·) → hi+=0x40 → 1494 → +0xc2
// With generated orbVel==0 this resolves to 0xffffffff (via 1483→0x7f7fff).
static uint32_t DeriveAngularVelocity(int32_t orbVel)
{
    uint32_t u = FUNC_001496(static_cast<uint32_t>(orbVel));
    u = FUNC_001483(0xffee5460u, u);
    // ASM: `add word [ebp-0xe],byte +0x40` after storing 1483 result.
    const uint16_t hi = static_cast<uint16_t>((u >> 16) + 0x40);
    const uint16_t lo = static_cast<uint16_t>(u);
    u = (static_cast<uint32_t>(hi) << 16) | lo;
    return FUNC_001494(u);
}

// Per-body orbital fields + relative pose (FUNC_000880 non-starport path).
// Still writes FFE-scale Vec64 via FUNC_001507 (4.4 structure). Render positions
// are rebuilt separately in RefreshRenderPositions (task 4.5).
static void MaterializeBodyOrbit(PhysObj* phys, const SysObj* sys,
                                 uint32_t timeLo, uint32_t timeHi)
{
    if (!phys || !sys) return;

    // Surface starports: ASM forces +0xc2=0 and uses FUNC_000879 (skipped on
    // LP64). Leave relative Vec64 at 0 — matches orbRadius==0 / ~0 offset.
    if (sys->type == static_cast<uint16_t>(SysObjType::kStarport)) {
        PhysObjSetI32(phys, 0xc2, 0);
        PhysObjSetI32(phys, 0xa8, static_cast<int32_t>(timeLo));
        PhysObjSetI32(phys, 0xac, static_cast<int32_t>(timeHi));
        return;
    }

    PhysObjSetOrbRadius(phys, sys->orbRadius);
    const uint32_t angVel = DeriveAngularVelocity(sys->orbVel);
    PhysObjSetI32(phys, 0xc2, static_cast<int32_t>(angVel));
    PhysObjSetI32(phys, 0xa8, static_cast<int32_t>(timeLo));
    PhysObjSetI32(phys, 0xac, static_cast<int32_t>(timeHi));

    // Initial angle at +0xba (Int64) via FUNC_001510, epoch (0,0) → (timeHi,timeLo).
    uint32_t* angle = reinterpret_cast<uint32_t*>(phys->raw + 0xba);
    angle[0] = 0;
    angle[1] = 0;
    FUNC_001510(angle, angVel, 0, 0, static_cast<int32_t>(timeHi), timeLo);

    // Latitude folded into angle high dword (+0xbe), longitude at +0xc6.
    const int32_t be = PhysObjGetI32(phys, 0xbe) +
                       (static_cast<uint32_t>(static_cast<uint16_t>(sys->latitude)) << 16);
    PhysObjSetI32(phys, 0xbe, be);
    PhysObjSetU16(phys, 0xc6, static_cast<uint16_t>(sys->longitude));

    FUNC_001507(reinterpret_cast<intptr_t>(phys),
                static_cast<uint32_t>(PhysObjGetOrbRadius(phys)),
                static_cast<uint32_t>(static_cast<int16_t>(sys->longitude)),
                static_cast<uint32_t>(PhysObjGetI32(phys, 0xbe)));
}

// R1140 - which frame each field is in (the port lost this; FUNC_001677 / FUNC_001678 keep it):
//   +0x3e  local position: in the PARENT's rotating frame when +0x57 is set (surface starports, a
//          landed player, ships parked on a planet), else in the parent's axes without its spin;
//   +0x26  global position (ASM-faithful): parent.+0x26 + local, the local first rotated by the
//          parent's matrix (VecMatMul = M^T v) when +0x57 is set - FUNC_001677;
//   g_anchorRel / WorldGetShipAnchorRel: relative to the anchor (Hope) IN HOPE'S ROTATING FRAME,
//          the frame the camera (the player, a +0x57 child of Hope) lives in. A body in that same
//          frame takes its local offset (FUNC_001678's same-parent path); any other body takes
//          Hope * (global - Hope.global). Drawing matrices follow: WorldMatrixInAnchorFrame.
// Until R1139 the planets did not spin, Hope's matrix stayed identity and all three frames
// coincided, which is how the translation-only arithmetic passed every frame-120 check.

// JJFFE_PRE1140_FRAMES=1: the pre-R1140 translation-only frames (ViewModeAB's frame-300 teeth).
static bool Pre1140Frames()
{
    static const bool s_on = [] {
        const char* e = std::getenv("JJFFE_PRE1140_FRAMES");
        return e && e[0] == '1';
    }();
    return s_on;
}

// FUNC_001677's +0x57 step: out = VecMatMul(local, parent matrix) with the ASM's precision (the
// local truncated to 28 bits, rotated, then scaled back by FUNC_001517).
static void ComposeInParentFrame(uint8_t* out26, const uint8_t* local3e, const PhysObj& parent)
{
    uint32_t v64[6];
    std::memcpy(v64, local3e, sizeof(v64));
    const uint32_t sh = FUNC_001658_Vec64Truncate(v64, 0x1c);
    int32_t v[3] = {static_cast<int32_t>(v64[0]), static_cast<int32_t>(v64[2]),
                    static_cast<int32_t>(v64[4])};
    uint32_t m[9];
    std::memcpy(m, parent.raw, sizeof(m));
    FUNC_001669_VecMatMul(v, reinterpret_cast<uint32_t*>(v), m);
    FUNC_001517(v64, v, static_cast<int32_t>(sh));
    std::memcpy(out26, v64, sizeof(v64));
}

// Global -> anchor frame: out = M * v (the inverse of VecMatMul's M^T v), FUNC_001521's floor Q31
// products, exact in 128 bits for FFE-scale vectors.
static void ToAnchorFrame(const int32_t m[9], const int64_t v[3], int64_t out[3])
{
    if (Pre1140Frames()) {
        for (int r = 0; r < 3; ++r) out[r] = v[r];
        return;
    }
    for (int r = 0; r < 3; ++r) {
        const __int128 acc = static_cast<__int128>(m[3 * r]) * v[0] +
                             static_cast<__int128>(m[3 * r + 1]) * v[1] +
                             static_cast<__int128>(m[3 * r + 2]) * v[2];
        out[r] = static_cast<int64_t>(acc >> 31);
    }
}

static bool InAnchorFrame(const PhysObj& p, int anchor)
{
    if (Pre1140Frames()) return false;
    return anchor >= 0 && p.raw[0x57] != 0 && static_cast<int>(PhysObjGetParent(&p)) == anchor;
}

static void RefreshGlobalPositions()
{
    // Sol bodies are dense with parent index < child. Spawn slots (AB_SLOT_ORDER_FIX)
    // allocate high→low so parents have HIGHER indices — process that range reverse.
    const int split = (AbSlotOrderFix() && g_gatewayIndex >= 0) ? g_gatewayIndex : g_worldCount;
    auto applyOne = [](int i, bool requireParentBefore) {
        PhysObj* p = &g_worldPhys[i];
        // global (+0x26) = relative (+0x3e) — FFE planet-scale Vec64 (kept for
        // ASM-faithful state; WorldGetPos uses g_renderPos instead).
        std::memcpy(p->raw + 0x26, p->raw + 0x3e, 24);
        p->raw[0x25] = 1;  // global position valid

        const uint16_t parentIdx = g_worldSys[i].parentIndex;
        if (parentIdx == 0) return;
        const int parentSlot = static_cast<int>(parentIdx) - 1;
        if (parentSlot < 0 || parentSlot >= g_worldCount) return;
        if (requireParentBefore && parentSlot >= i) return;
        if (p->raw[0x57] != 0 && !Pre1140Frames())
            ComposeInParentFrame(p->raw + 0x26, p->raw + 0x3e, g_worldPhys[parentSlot]);
        FUNC_001661_Vec64Add(reinterpret_cast<intptr_t>(p->raw + 0x26),
                             reinterpret_cast<intptr_t>(g_worldPhys[parentSlot].raw + 0x26));
    };
    for (int i = 0; i < split; ++i) applyOne(i, true);
    for (int i = g_worldCount - 1; i >= split; --i) applyOne(i, false);

    // Task 4.5: never feed FFE Vec64 into int32 render space. Report once if
    // any global component would saturate int32 — silent clamp shipped 4.4.
    static bool s_loggedFfeOverflow = false;
    if (!s_loggedFfeOverflow) {
        for (int i = 0; i < g_worldCount; ++i) {
            const PhysObj* p = &g_worldPhys[i];
            for (int axis = 0; axis < 3; ++axis) {
                int64_t v = 0;
                std::memcpy(&v, p->raw + 0x26 + axis * 8, sizeof(v));
                if (v > static_cast<int64_t>(INT32_MAX) ||
                    v < static_cast<int64_t>(INT32_MIN)) {
                    s_loggedFfeOverflow = true;
                    std::fprintf(stderr,
                                 "World: FFE Vec64 global pos overflows int32 "
                                 "(body %d axis %d raw=%lld) — render uses "
                                 "scaled g_renderPos (task 4.5)\n",
                                 i, axis, static_cast<long long>(v));
                    break;
                }
            }
            if (s_loggedFfeOverflow) break;
        }
    }
}

// Task 4.18: Gateway bodies relative to the spawn planet in FFE units, then
// >> g_renderShift into render space. Surface starports use their FUNC_000879
// +0x3e like every other body (already lowered onto the terrain by the
// FUNC_000574 height query, Task 4.19).
static void RefreshGatewayRender()
{
    if (g_gatewayIndex < 0 || g_anchorIndex < 0) return;
    int64_t anchorG[3];
    std::memcpy(anchorG, g_worldPhys[g_anchorIndex].raw + 0x26, sizeof(anchorG));
    for (int i = g_gatewayIndex; i < g_worldCount; ++i) {
        const PhysObj* p = &g_worldPhys[i];
        int64_t rel[3];
        if (InAnchorFrame(*p, g_anchorIndex)) {
            std::memcpy(rel, p->raw + 0x3e, sizeof(rel));        // FUNC_001678 same frame
        } else {
            int64_t d[3];
            for (int a = 0; a < 3; ++a) {
                int64_t g = 0;
                std::memcpy(&g, p->raw + 0x26 + a * 8, sizeof(g));
                d[a] = g - anchorG[a];
            }
            int32_t m[9];
            std::memcpy(m, g_worldPhys[g_anchorIndex].raw, sizeof(m));
            ToAnchorFrame(m, d, rel);
        }
        std::memcpy(g_anchorRel[i], rel, sizeof(rel));

        int64_t r[3] = {rel[0] >> g_renderShift, rel[1] >> g_renderShift, rel[2] >> g_renderShift};
        const double len = std::sqrt(static_cast<double>(r[0]) * r[0] + static_cast<double>(r[1]) * r[1] +
                                     static_cast<double>(r[2]) * r[2]);
        if (len > static_cast<double>(kMaxGatewayRender)) {
            for (int a = 0; a < 3; ++a)
                r[a] = static_cast<int64_t>(static_cast<double>(r[a]) * kMaxGatewayRender / len);
        }
        for (int a = 0; a < 3; ++a) g_renderPos[i][a] = static_cast<int32_t>(r[a]);
    }
}

// Direction from the same yaw/pitch FUNC_001507 feeds FUNC_001506, then scale
// orbRadius into int32 render space and accumulate the parent chain there.
static void RefreshRenderPositions()
{
    std::memset(g_renderPos, 0, sizeof(g_renderPos));
    if (g_worldCount <= 0) return;

    const int32_t maxR = g_maxOrbRadius;
    const int solEnd = (g_gatewayIndex >= 0) ? g_gatewayIndex : g_worldCount;
    for (int i = 0; i < solEnd; ++i) {
        const SysObj* sys = &g_worldSys[i];
        const PhysObj* p = &g_worldPhys[i];
        int32_t rel[3] = {0, 0, 0};

        const bool surface =
            sys->type == static_cast<uint16_t>(SysObjType::kStarport) ||
            sys->orbRadius <= 0;
        if (!surface && maxR > 0) {
            // Match FUNC_001507: pitch = (high16(+0xbe) & ~0xf), yaw = +0xc6.
            const int32_t be = PhysObjGetI32(p, 0xbe);
            const int32_t pitch =
                static_cast<int32_t>((static_cast<uint32_t>(be) >> 16) & 0xfffffff0u);
            const int32_t yaw =
                static_cast<int32_t>(static_cast<int16_t>(PhysObjGetU16(p, 0xc6)));
            int32_t dir[3] = {};
            FUNC_001506(dir, yaw, pitch);

            const int64_t scaledR =
                (static_cast<int64_t>(sys->orbRadius) * kMaxRenderOrbit) / maxR;
            // FUNC_001506 returns Q31 unit dirs (Sin32 / FUNC_001521); >>31.
            rel[0] = static_cast<int32_t>((static_cast<int64_t>(dir[0]) * scaledR) >> 31);
            rel[1] = static_cast<int32_t>((static_cast<int64_t>(dir[1]) * scaledR) >> 31);
            rel[2] = static_cast<int32_t>((static_cast<int64_t>(dir[2]) * scaledR) >> 31);
        }

        int32_t gx = rel[0], gy = rel[1], gz = rel[2];
        const uint16_t parentIdx = sys->parentIndex;
        if (parentIdx != 0) {
            const int parentSlot = static_cast<int>(parentIdx) - 1;
            if (parentSlot >= 0 && parentSlot < i) {
                gx += g_renderPos[parentSlot][0];
                gy += g_renderPos[parentSlot][1];
                gz += g_renderPos[parentSlot][2];
            }
        }
        g_renderPos[i][0] = gx;
        g_renderPos[i][1] = gy;
        g_renderPos[i][2] = gz;
    }
    RefreshGatewayRender();
}

// Orbit time base (worldgen R1/R2, docs/PHASE_64_WORLDGEN_CHARTER.md): a body's
// +0xa8/+0xac is stamped with the live clock and the angle advances by the real
// delta — the law measured on the ASM (findings_r1.md, 17/17 bodies).
// AB_WORLD_ORBIT_DELTA=0 reverts to the pre-4.37 constant step (0x1000) for
// bisect; the default is the ASM behaviour. Capture harnesses that isolate
// another variable (e.g. CameraFfeIdentity's 4.37 clock check) pin =0.
static bool OrbitUsesClock()
{
    static const bool s_on = []() {
        const char* e = std::getenv("AB_WORLD_ORBIT_DELTA");
        return !(e && e[0] == '0');
    }();
    return s_on;
}

// Advance one orbital body from its stored time (+0xa8/+0xac) to
// (timeLo,timeHi). Law measured on the ASM (scratch/worldgen/findings_r1.md):
// delta_angle = 8 * angVel * delta_time, exact mod 2^64 via FUNC_001510.
// Starports are inert; a body with no angular velocity (+0xc2 == 0) does not orbit but still
// spins (FUNC_000866's tail).
void WorldOrbitAdvanceBody(PhysObj* p, uint32_t timeLo, uint32_t timeHi)
{
    if (!p) return;
    if ((PhysObjGetFlags(p) & kPhysFlagSurfaceStarport) != 0) return;

    const uint32_t oldLo = static_cast<uint32_t>(PhysObjGetI32(p, 0xa8));
    const uint32_t oldHi = static_cast<uint32_t>(PhysObjGetI32(p, 0xac));
    if (oldLo == timeLo && oldHi == timeHi) return;
    // ffe.asm:60759-60762: FUNC_000866 stamps the new time first, for every body - the spin
    // below compares against the old stamp, so a body with no orbit must be stamped too.
    PhysObjSetI32(p, 0xa8, static_cast<int32_t>(timeLo));
    PhysObjSetI32(p, 0xac, static_cast<int32_t>(timeHi));

    const uint32_t angVel = static_cast<uint32_t>(PhysObjGetI32(p, 0xc2));
    if (angVel != 0) {
        // FUNC_000866: delta = FUNC_001510(angVel, oldTime -> newTime); add to +0xba.
        // Argument pairing is (oldHi, oldLo, newHi, newLo) — see the materialize call.
        uint32_t delta[2] = {};
        FUNC_001510(delta, angVel, oldHi, oldLo,
                    static_cast<int32_t>(timeHi), timeLo);
        // FUNC_001333_Int64Add64 wraps mod 2^64 (ASM add/adc). Adding on the
        // unsigned view gives the identical bits without signed-overflow UB
        // (UBSan: -7461898989076665088 + -9101258617084968960 at this line).
        uint64_t angle = 0;
        uint64_t dlt = 0;
        std::memcpy(&angle, p->raw + 0xba, sizeof(angle));
        std::memcpy(&dlt, delta, sizeof(dlt));
        angle += dlt;
        std::memcpy(p->raw + 0xba, &angle, sizeof(angle));

        FUNC_001507(reinterpret_cast<intptr_t>(p),
                    static_cast<uint32_t>(PhysObjGetOrbRadius(p)),
                    static_cast<uint32_t>(static_cast<int16_t>(PhysObjGetU16(p, 0xc6))),
                    static_cast<uint32_t>(PhysObjGetI32(p, 0xbe)));
    }

    // R1139: the day spin, FUNC_000866's tail (ffe.asm:60832-60871). With a spin shift at +0xc8,
    // the old and new times are shifted left by (+0xc8 - 1); when bits 20 and up differ, the
    // body's matrix is rebuilt from the new time: MatBuildYZT(obj, tilt +0xcc, time >> 16), the
    // two angles swapped when +0x14c bit 0x10 is set. The port had only the orbit half, so the
    // planets never turned - a landed ship's sky (the star) stood still while the ASM's moved.
    // JJFFE_PRE1139_SPIN=1 drops the spin again (WorldOrbitReplay's spin teeth).
    static const bool s_noSpin = [] {
        const char* e = std::getenv("JJFFE_PRE1139_SPIN");
        return e && e[0] == '1';
    }();
    const int8_t spinShift = static_cast<int8_t>(p->raw[0xc8]);
    if (spinShift != 0 && !s_noSpin) {
        uint32_t was = oldLo;
        uint32_t now = timeLo;
        const int sh = spinShift - 1;
        if (sh >= 0) {
            was <<= (sh & 0x1f);
            now <<= (sh & 0x1f);
        }
        if ((was >> 0x14) != (now >> 0x14)) {
            const int32_t tilt = static_cast<int16_t>(PhysObjGetU16(p, 0xcc));
            int32_t* m = reinterpret_cast<int32_t*>(p->raw);
            if ((p->raw[0x14c] & 0x10) == 0)
                FUNC_001675_MatBuildYZT(m, tilt, static_cast<int32_t>(now >> 0x10));
            else
                FUNC_001675_MatBuildYZT(m, static_cast<int32_t>(now >> 0x10), tilt);
        }
    }
}

static void AdvanceBodyOrbit(PhysObj* p, const SysObj* sys)
{
    if (!p || !sys) return;
    if (sys->type == static_cast<uint16_t>(SysObjType::kStarport)) return;

    const uint32_t oldLo = static_cast<uint32_t>(PhysObjGetI32(p, 0xa8));
    const uint32_t newLo = oldLo + kOrbitFrameTicks;
    const uint32_t newHi = static_cast<uint32_t>(PhysObjGetI32(p, 0xac)) +
                           (newLo < oldLo ? 1u : 0u);
    WorldOrbitAdvanceBody(p, newLo, newHi);
}

bool WorldLoadSol(int startIndex)
{
    if (!EnsureOrbitMathReady()) {
        std::fprintf(stderr, "WorldLoadSol: sine table unavailable (load data_blob)\n");
        return false;
    }

    std::memset(g_worldSys, 0, sizeof(g_worldSys));
    std::memset(g_worldPhys, 0, sizeof(g_worldPhys));
    std::memset(g_renderPos, 0, sizeof(g_renderPos));
    std::memset(g_anchorRel, 0, sizeof(g_anchorRel));
    g_worldCount = 0;
    g_worldShipCount = 0;
    g_gatewayIndex = -1;
    g_gatewayStarIndex = -1;
    g_anchorIndex = -1;
    g_renderShift = 0;
    g_maxOrbRadius = 0;

    int n = SysGenGenerateHandcodedSol(g_worldSys, kSysObjGeneratedCapacity);
    if (n <= 0) return false;
    int m = SysGenMaterialize(g_worldSys, n, g_worldPhys, kSysObjGeneratedCapacity);
    if (m != n) return false;

    // Materialize Sol orbits (radius → angVel → angle → lat/lon → FUNC_001507).
    // Epoch (0,0) — mission clock not required; angVel still derived.
    for (int i = 0; i < n; ++i) {
        MaterializeBodyOrbit(&g_worldPhys[i], &g_worldSys[i], 0, 0);
        if (g_worldSys[i].orbRadius > g_maxOrbRadius)
            g_maxOrbRadius = g_worldSys[i].orbRadius;
    }

    // Task 4.18: the cold-boot spawn system from the game's own data —
    // FUNC_000829/832 start params, FUNC_000878 generation, FUNC_000880/927
    // materialize (sysgen_proc). FUNC_000829 takes the record index
    // idx = (param >> 8) & 0xff of FUNC_000147's start << 8 | stage
    // (ffe.asm:56403-56405), i.e. the start's low byte. Appended after the 53 Sol
    // bodies (kept for the body-paint / orbital tests), in SysObj order; the
    // world index replaces the ASM slot (114 - k) so PhysObj +0x86/+0x56 keep
    // indexing this array — unless AB_SLOT_ORDER_FIX places at spawn slots
    // (R1226: allocation order already matches ASM; the fork is this dense
    // remap — Hope 108→59).
    static SpawnSlotTable s_spawn;
    static SysObj s_spawnSys[kSysObjGeneratedCapacity];
    SysGenStartInfo info{};
    const int start = startIndex & 0xff;
    if (!SysGenBuildSpawnTable(&s_spawn, s_spawnSys, kSysObjGeneratedCapacity, &info, start)) {
        std::fprintf(stderr, "WorldLoadSol: start %d spawn system generation failed\n", start);
        return false;
    }
    const int gen = s_spawn.sysCount;
    const bool slotOrder = AbSlotOrderFix();
    int maxSpawnSlot = -1;
    int minSpawnSlot = kSpawnSlotCount;
    for (int k = 0; k < gen; ++k) {
        const int sl = s_spawn.sysToSlot[k];
        if (sl > maxSpawnSlot) maxSpawnSlot = sl;
        if (sl >= 0 && sl < minSpawnSlot) minSpawnSlot = sl;
    }
    if (gen <= 0) return false;
    if (slotOrder) {
        if (maxSpawnSlot < 0 || maxSpawnSlot >= kWorldBodyCapacity || minSpawnSlot >= kSpawnSlotCount)
            return false;
    } else if (n + gen > kWorldBodyCapacity) {
        return false;
    }
    // Task 4.20: the default-start player on the starport pad (FUNC_000998
    // JUMP_004144). Starts 1-3 take FUNC_000998's other cases (DATA_008881 =
    // the start, ffe.asm:74235-74243), which the scaffold does not place.
    const bool defaultStart = start < 1 || start > 3;
    PlayerPadResult pad;
    int playerParentWorld = -1;
    if (defaultStart && PlayerPadSpawnDefault(&s_spawn, &pad)) {
        g_spawnPlayer = s_spawn.slots[pad.slot];
        const int hopeSlot = PhysObjGetParent(&g_spawnPlayer);  // ASM: 0x6c
        if (slotOrder) {
            playerParentWorld = hopeSlot;
        } else {
            for (int k = 0; k < gen; ++k)
                if (s_spawn.sysToSlot[k] == hopeSlot) playerParentWorld = n + k;
        }
    }
    // Dense: generated bodies start at n (Gateway star at n). ORDER: bodies sit
    // at ASM slots [minSpawnSlot..maxSpawnSlot] (star = sysToSlot[0], typically 114).
    g_gatewayIndex = slotOrder ? minSpawnSlot : n;
    g_gatewayStarIndex = slotOrder ? s_spawn.sysToSlot[0] : n;
    for (int k = 0; k < gen; ++k) {
        const int slot = s_spawn.sysToSlot[k];
        SysObj so = s_spawnSys[k];
        PhysObj po = s_spawn.slots[slot];
        const uint8_t old86 = PhysObjGetIndex(&po);
        if (slotOrder) {
            // World index == ASM pool slot. Keep +0x86/+0x56 from spawn/927.
            if (so.parentIndex != 0) {
                const int pslot = s_spawn.sysToSlot[so.parentIndex - 1];
                so.parentIndex = static_cast<uint16_t>(pslot + 1);
            }
            g_worldSys[slot] = so;
            R1224LogW86World("WorldLoadSol_slotOrder", &po, old86, old86);
            g_worldPhys[slot] = po;
        } else {
            if (so.parentIndex != 0) so.parentIndex = static_cast<uint16_t>(so.parentIndex + n);
            g_worldSys[n + k] = so;
            const uint8_t world86 = static_cast<uint8_t>(n + k);
            PhysObjSetIndex(&po, world86);
            R1224LogW86World("WorldLoadSol_PhysObjSetIndex", &po, old86, world86);
            PhysObjSetParent(&po, static_cast<uint8_t>(so.parentIndex != 0 ? so.parentIndex - 1 : 0));
            g_worldPhys[n + k] = po;
        }
    }
    // Task 4.27: FUNC_000147 phase 1 after the player is placed — stations
    // (FUNC_000616) then traffic (FUNC_000688) into the same slot table.
    g_worldShipCount = 0;
    if (playerParentWorld >= 0) {
        static TrafficSpawnResult s_traffic;
        if (TrafficSpawnNewGame(&s_spawn, info, pad.slot, &s_traffic)) {
            int slotToWorld[kSpawnSlotCount];
            for (int s = 0; s < kSpawnSlotCount; ++s) slotToWorld[s] = -1;
            for (int k = 0; k < gen; ++k) {
                const int sl = s_spawn.sysToSlot[k];
                slotToWorld[sl] = slotOrder ? sl : (n + k);
            }
            for (int s = 1; s < kSpawnSlotCount; ++s) {
                if (s == pad.slot || slotToWorld[s] >= 0 || s_spawn.slotType[s] == 0) continue;
                PhysObj po = s_spawn.slots[s];
                const int parent = PhysObjGetParent(&po);
                if (parent != 0 && (parent >= kSpawnSlotCount || slotToWorld[parent] < 0)) {
                    std::fprintf(stderr, "WorldLoadSol: ship slot %d parent %d is not a body\n", s,
                                 parent);
                    continue;
                }
                PhysObjSetParent(&po, static_cast<uint8_t>(parent ? slotToWorld[parent] : 0));
                g_worldShips[g_worldShipCount++] = po;
            }
        } else {
            std::fprintf(stderr, "WorldLoadSol: traffic spawn failed\n");
        }
    }

    if (slotOrder) {
        g_worldCount = (maxSpawnSlot + 1 > n) ? (maxSpawnSlot + 1) : n;
    } else {
        n += gen;
        g_worldCount = n;
    }

    // Render anchor: the spawn planet (Hope), or for starts 1-3 the slot
    // FUNC_000998 parents the player to (+0x56: 0x6f / 0x60 / 0x70,
    // ffe.asm:74234-74330). Shift from its model header.
    const PhysObj* anchor = nullptr;
    if (defaultStart) {
        if (slotOrder)
            anchor = PhysObjFindByName(g_worldPhys, g_worldCount, "Hope");
        else
            anchor = PhysObjFindByName(g_worldPhys + g_gatewayIndex, gen, "Hope");
    } else {
        static const int kParentSlot[4] = {0, 0x6f, 0x60, 0x70};
        for (int k = 0; k < gen; ++k) {
            if (s_spawn.sysToSlot[k] != kParentSlot[start]) continue;
            anchor = slotOrder ? &g_worldPhys[s_spawn.sysToSlot[k]]
                               : &g_worldPhys[g_gatewayIndex + k];
        }
    }
    if (anchor) {
        g_anchorIndex = static_cast<int>(anchor - g_worldPhys);
        if (const ShipModel* sm = GetShipModel(PhysObjGetModel(anchor))) {
            const int sh = static_cast<int>(sm->scaleRelated + sm->scaleFactor) - 14;
            g_renderShift = (sh > 0 && sh < 40) ? sh : 0;
        }
    }
    g_spawnPlayerValid = playerParentWorld >= 0 && playerParentWorld == g_anchorIndex;
    if (g_spawnPlayerValid)
        PhysObjSetParent(&g_spawnPlayer, static_cast<uint8_t>(g_anchorIndex));
    else if (defaultStart)
        std::fprintf(stderr, "WorldLoadSol: player pad placement unavailable (parent %d, anchor %d)\n",
                     playerParentWorld, g_anchorIndex);

    RefreshGlobalPositions();
    RefreshRenderPositions();
    if (getenv("JJFFE_LIGHT_TRACE") && g_anchorIndex >= 0) {
        int64_t g[3] = {};
        WorldGetFfeGlobal(g_anchorIndex, g);
        std::fprintf(stderr, "WorldLoadSol: Hope FFE global=(%lld,%lld,%lld)\n",
                     static_cast<long long>(g[0]), static_cast<long long>(g[1]),
                     static_cast<long long>(g[2]));
    }
    int stations = 0;
    for (int k = 0; k < gen; ++k)
        if (s_spawnSys[k].type >= static_cast<uint16_t>(SysObjType::kTradingPost)) ++stations;
    std::fprintf(stderr,
                 "WorldLoadSol: %d bodies (53 Sol + %d generated '%s' start %d: code=%#x "
                 "tier=%d, %d stations/starports); render-scale maxOrb=%d → %d (FP); "
                 "Gateway index>=%d, anchor %d '%s', FFE>>%d; %d traffic ships%s\n",
                 g_worldCount, gen, info.params.name, start,
                 static_cast<unsigned>(info.params.systemcode), info.tier, stations, g_maxOrbRadius,
                 kMaxRenderOrbit, g_gatewayIndex, g_anchorIndex,
                 g_anchorIndex >= 0 ? PhysObjGetName(&g_worldPhys[g_anchorIndex]) : "-", g_renderShift,
                 g_worldShipCount, slotOrder ? " [AB_SLOT_ORDER_FIX]" : "");
    if (slotOrder)
        std::fprintf(stderr,
                     "WorldLoadSol: AB_SLOT_ORDER_FIX gateway body range [%d..%d] star=%d "
                     "anchor=%d Hope+0x86=%u\n",
                     g_gatewayIndex, g_worldCount - 1, g_gatewayStarIndex, g_anchorIndex,
                     g_anchorIndex >= 0
                         ? static_cast<unsigned>(PhysObjGetIndex(&g_worldPhys[g_anchorIndex]))
                         : 0u);
    return true;
}

void WorldAdvanceOrbitsTo(uint32_t timeLo, uint32_t timeHi)
{
    if (g_worldCount <= 0) return;
    for (int i = 0; i < g_worldCount; ++i) {
        WorldOrbitAdvanceBody(&g_worldPhys[i], timeLo, timeHi);
    }
    RefreshGlobalPositions();
    RefreshRenderPositions();
}

void WorldAdvanceOrbits(int steps)
{
    if (steps <= 0 || g_worldCount <= 0) return;
    if (OrbitUsesClock()) {
        // R1 law path: the body time IS the live clock (DATA_008804/008807),
        // so a frame advances every body to "now" rather than by a constant.
        for (int s = 0; s < steps; ++s) {
            const GameStateTickStats st = GetGameStateTickStats();
            WorldAdvanceOrbitsTo(st.clock8804, st.date8807);
        }
        return;
    }
    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < g_worldCount; ++i) {
            AdvanceBodyOrbit(&g_worldPhys[i], &g_worldSys[i]);
        }
        RefreshGlobalPositions();
        RefreshRenderPositions();
    }
}

void WorldMatrixInAnchorFrame(const PhysObj* p, int32_t out[9])
{
    if (!p) return;
    std::memcpy(out, p->raw, 9 * sizeof(int32_t));
    if (Pre1140Frames() || g_anchorIndex < 0 || InAnchorFrame(*p, g_anchorIndex)) return;
    // Global matrix: a +0x57 child's own matrix composed with its parent's (FUNC_001679's
    // MatMatMul(local, parent)); then into Hope's frame, M * Hope^T (FUNC_001673).
    uint32_t mg[9];
    std::memcpy(mg, p->raw, sizeof(mg));
    const int parent = PhysObjGetParent(p);
    if (p->raw[0x57] != 0 && parent > 0 && parent < g_worldCount) {
        int32_t comp[9];
        uint32_t pm[9];
        std::memcpy(pm, g_worldPhys[parent].raw, sizeof(pm));
        FUNC_001672_MatMatMul(comp, mg, pm);
        std::memcpy(mg, comp, sizeof(mg));
    }
    uint32_t hm[9];
    std::memcpy(hm, g_worldPhys[g_anchorIndex].raw, sizeof(hm));
    FUNC_001673_MatMatTMul(out, mg, hm);
}

const PhysObj* WorldSpawnPlayer()
{
    return g_spawnPlayerValid ? &g_spawnPlayer : nullptr;
}

int WorldShipCount()
{
    return g_worldShipCount;
}

const PhysObj* WorldShipObjs()
{
    return g_worldShips;
}

bool WorldGetShipAnchorRel(int ship, int64_t out[3])
{
    ++g_anchorRelReads;
    if (ship < 0 || ship >= g_worldShipCount || g_anchorIndex < 0) return false;
    const PhysObj* p = &g_worldShips[ship];
    if (InAnchorFrame(*p, g_anchorIndex)) {           // parked on Hope: FUNC_001678 same frame
        std::memcpy(out, p->raw + 0x3e, 3 * sizeof(int64_t));
        return true;
    }
    const int parent = PhysObjGetParent(p);
    int64_t base[3] = {}, anchor[3], rel[3];
    if (parent != 0) std::memcpy(base, g_worldPhys[parent].raw + 0x26, sizeof(base));
    std::memcpy(anchor, g_worldPhys[g_anchorIndex].raw + 0x26, sizeof(anchor));
    if (parent != 0 && p->raw[0x57] != 0 && !Pre1140Frames()) {
        uint8_t g26[24];
        ComposeInParentFrame(g26, p->raw + 0x3e, g_worldPhys[parent]);
        std::memcpy(rel, g26, sizeof(rel));
    } else {
        std::memcpy(rel, p->raw + 0x3e, sizeof(rel));
    }
    int64_t d[3];
    for (int a = 0; a < 3; ++a) d[a] = rel[a] + base[a] - anchor[a];
    int32_t m[9];
    std::memcpy(m, g_worldPhys[g_anchorIndex].raw, sizeof(m));
    ToAnchorFrame(m, d, out);
    return true;
}

void WorldShutdown()
{
    g_spawnPlayerValid = false;
    g_worldShipCount = 0;
    g_worldCount = 0;
    g_gatewayIndex = -1;
    g_gatewayStarIndex = -1;
    g_anchorIndex = -1;
    g_renderShift = 0;
    g_maxOrbRadius = 0;
    std::memset(g_anchorRel, 0, sizeof(g_anchorRel));
    std::memset(g_worldSys, 0, sizeof(g_worldSys));
    std::memset(g_worldPhys, 0, sizeof(g_worldPhys));
    std::memset(g_renderPos, 0, sizeof(g_renderPos));
}

int WorldBodyCount()
{
    return g_worldCount;
}

int WorldGatewayIndex()
{
    return g_gatewayIndex;
}

int WorldGatewayStarIndex()
{
    return g_gatewayStarIndex;
}

int WorldRenderShift()
{
    return g_renderShift;
}

bool WorldGetAnchorRel(int index, int64_t out[3])
{
    ++g_anchorRelReads;
    if (index < g_gatewayIndex || index < 0 || index >= g_worldCount || g_anchorIndex < 0)
        return false;
    std::memcpy(out, g_anchorRel[index], sizeof(g_anchorRel[index]));
    return true;
}

const SysObj* WorldSysObjs()
{
    return g_worldSys;
}

const PhysObj* WorldPhysObjs()
{
    return g_worldPhys;
}

void WorldGetPos(int index, int32_t* x, int32_t* y, int32_t* z)
{
    ++g_renderPosReads;
    if (index < 0 || index >= g_worldCount) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (z) *z = 0;
        return;
    }
    // Task 4.5: scaled int32 render-space (not FFE Vec64 — that overflows).
    if (x) *x = g_renderPos[index][0];
    if (y) *y = g_renderPos[index][1];
    if (z) *z = g_renderPos[index][2];
}

bool WorldGetFfeGlobal(int index, int64_t out[3])
{
    if (index < 0 || index >= g_worldCount) return false;
    std::memcpy(out, g_worldPhys[index].raw + 0x26, 24);
    return true;
}

uint64_t WorldRenderPosReads()
{
    return g_renderPosReads;
}

uint64_t WorldAnchorRelReads()
{
    return g_anchorRelReads;
}

const PhysObj* WorldFind(const char* name)
{
    return PhysObjFindByName(g_worldPhys, g_worldCount, name);
}

const char* WorldNearestName(int32_t px, int32_t py, int32_t pz)
{
    if (g_worldCount <= 0) return "";
    int best = 0;
    int64_t bestD = -1;
    for (int i = 0; i < g_worldCount; ++i) {
        int32_t bx = 0, by = 0, bz = 0;
        WorldGetPos(i, &bx, &by, &bz);
        const int64_t dx = static_cast<int64_t>(bx) - px;
        const int64_t dy = static_cast<int64_t>(by) - py;
        const int64_t dz = static_cast<int64_t>(bz) - pz;
        const int64_t d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
        if (bestD < 0 || d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return PhysObjGetName(&g_worldPhys[best]);
}

} // namespace jjffe
