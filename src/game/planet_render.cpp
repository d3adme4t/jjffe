#include "jjffe/planet_render.h"
#include "jjffe/ship_model.h"
#include "jjffe/math.h"
#include "jjffe/video.h"
#include "jjffe/world.h"
#include "jjffe/phys_obj.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/planet_ab.h"
#include "jjffe/planet_midpoint_trace.h"
#include "jjffe/planet_mid_scr_trace.h"
#include "jjffe/planet_mid_else_trace.h"
#include "jjffe/planet_792_chain_trace.h"
#include "jjffe/planet_paint_gate_trace.h"
#include "jjffe/planet_subdiv_trace.h"
#include "jjffe/planet_root_seed.h"
#include "jjffe/job_trace.h"
#include "jjffe/planet_shade_trace.h"
#include "jjffe/data.h"
#include "jjffe/draw.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>

namespace jjffe {

constexpr size_t kPaintObjSize = 0x152;

// R1226: SetupPlanetPaintGameObj memset zeroes +0x86; ASM draws the pool
// obj whose +0x86 FUNC_000927 wrote. Stamp paint from PhysObjGetIndex when
// WorldLoadSol places bodies at spawn slots (AB_SLOT_ORDER_FIX, default ON).
// (R1224/R1225 keep-+0x86 opt-in retired in R1228.)
static bool AbSlotOrderFix()
{
    static int s = -1;
    if (s < 0) {
        const char* e = std::getenv("AB_SLOT_ORDER_FIX");
        s = (e && e[0] == '0') ? 0 : 1;
    }
    return s != 0;
}

static bool AbStampPaintSlot86() { return AbSlotOrderFix(); }

static void R1224LogW86Paint(const char* site, uint8_t* obj, uint8_t oldv, uint8_t newv,
                             bool force = false)
{
    struct St {
        int on;
        uintptr_t filt;
        int modelFilt;
        int cap;
        int logs;
        uint32_t clock0;
    };
    static St s = {-1, 0, -1, 0, 0, 0};
    if (s.on < 0) {
        const char* e = std::getenv("R1224_W86");
        s.on = (e && e[0] == '1') ? 1 : 0;
        s.cap = 4096;
        s.clock0 = 0;
        s.modelFilt = -1;
        if (const char* p = std::getenv("R1224_W86_CAP"))
            if (p[0]) s.cap = std::atoi(p);
        if (const char* p = std::getenv("R1224_W86_CLOCK0"))
            if (p[0]) s.clock0 = static_cast<uint32_t>(std::strtoul(p, nullptr, 0));
        if (const char* p = std::getenv("R1224_W86_OBJ"))
            if (p[0]) s.filt = static_cast<uintptr_t>(std::strtoull(p, nullptr, 0));
        if (const char* p = std::getenv("R1224_W86_MODEL"))
            if (p[0]) s.modelFilt = std::atoi(p);
    }
    if (!s.on || s.logs >= s.cap || !obj) return;
    const uint32_t clock = GetGameClock();
    if (s.clock0 && clock < s.clock0) return;
    if (s.filt && reinterpret_cast<uintptr_t>(obj) != s.filt) return;
    const int32_t model = *reinterpret_cast<const int32_t*>(obj + 0x82);
    if (s.modelFilt >= 0 && static_cast<int>(model) != s.modelFilt) return;
    // Same-value: allow when forced, or when model-filtered (baseline olive
    // paint stays 0 every frame — still need boot→fr-482 coverage).
    if (!force && oldv == newv && s.modelFilt < 0) return;
    fprintf(stderr,
            "R1224_W86 n=%d clock=0x%x site=%s obj=%p model=%d %u->%u\n",
            s.logs, static_cast<unsigned>(clock), site,
            static_cast<void*>(obj), static_cast<int>(model),
            static_cast<unsigned>(oldv), static_cast<unsigned>(newv));
    ++s.logs;
}

void FUNC_001682(intptr_t param_1);
uint8_t* FUNC_001538(int32_t param_1);
void FUNC_001675_MatBuildYZT(int32_t* param_1, int32_t param_2, int32_t param_3);
void FUNC_001786(void);
void FUNC_001789(void);

bool IsPlanetBodyModel(int modelIndex)
{
    return modelIndex >= 117 && modelIndex <= 148;
}

bool IsPlanetBodyScale(uint32_t scaleRelated)
{
    return scaleRelated >= 16;
}

int PlanetBodyFitDistUnits(uint32_t radius, uint32_t scaleRelated)
{
    if (!IsPlanetBodyScale(scaleRelated) || radius == 0)
        return AbAutoFitDistUnits(radius, scaleRelated, kAbAutoZoomTargetPx,
                                  kAbAutoZoomMaxDistUnitsBody);
    const uint64_t z = (static_cast<uint64_t>(radius) << 14) * 7 / 2;
    int du = static_cast<int>(z / (FP_ONE / 2));
    if (du < 1)
        du = 1;
    return du;
}

int32_t PlanetSurfaceRadiusUnits(int modelIndex)
{
    const ShipModel* model = GetShipModel(modelIndex);
    if (!model || model->radius == 0)
        return 0;
    const int32_t bounding = static_cast<int32_t>(model->radius << 14);

    const uint8_t* blob = data::GetDataBlob();
    const size_t blobSize = data::GetDataBlobSize();
    if (!blob || model->meshDataOffset == 0 || model->vertexDataOffset == 0 ||
        static_cast<size_t>(model->meshDataOffset) + 8 > blobSize)
        return bounding;

    // Op word, colour word, then the four vertex bytes FUNC_001874 reads as
    // (int8)param_2[1], byte+3, (int8)param_2[2], byte+5.
    const uint8_t* op = blob + model->meshDataOffset;
    if ((op[0] & 0x1f) != 0x1f)
        return bounding;
    const int8_t idx[4] = {static_cast<int8_t>(op[4]), static_cast<int8_t>(op[5]),
                           static_cast<int8_t>(op[6]), static_cast<int8_t>(op[7])};

    // FUNC_001472: slot n transforms vertex entry n>>1 (6 bytes, xyz at +2),
    // odd slots mirror X. Only plain (type 0/1) entries are resolvable here.
    int32_t v[4][3];
    for (int i = 0; i < 4; ++i) {
        if (idx[i] < 0)
            return bounding;
        const size_t off = model->vertexDataOffset + static_cast<size_t>(idx[i] >> 1) * 6;
        if (off + 6 > blobSize || blob[off] > 1)
            return bounding;
        const int32_t x = static_cast<int8_t>(blob[off + 2]);
        v[i][0] = (idx[i] & 1) ? -x : x;
        v[i][1] = static_cast<int8_t>(blob[off + 3]);
        v[i][2] = static_cast<int8_t>(blob[off + 4]);
    }

    int64_t maxSq = 0;
    for (int c = 1; c < 4; ++c) {
        int64_t sq = 0;
        for (int k = 0; k < 3; ++k) {
            const int64_t d = v[c][k] - v[0][k];
            sq += d * d;
        }
        if (sq > maxSq)
            maxSq = sq;
    }
    if (maxSq == 0)
        return bounding;
    int64_t len = static_cast<int64_t>(std::sqrt(static_cast<double>(maxSq << 28)));
    while (len * len > (maxSq << 28)) --len;
    while ((len + 1) * (len + 1) <= (maxSq << 28)) ++len;
    return static_cast<int32_t>(len);
}

void SetupPlanetPaintGameObj(uint8_t* obj, int modelIndex, int32_t yaw, int32_t pitch,
                             int32_t dist)
{
    if (!obj) return;
    const uint8_t old86 = obj[0x86];
    std::memset(obj, 0, kPaintObjSize);
    *reinterpret_cast<int32_t*>(obj + 0x82) = modelIndex;
    // Model filter (R1224_W86_MODEL) logs even 0→0 so baseline olive paint
    // appears through fr-482; unfiltered runs still skip same-value spam.
    R1224LogW86Paint("SetupPlanetPaintGameObj", obj, old86, 0, /*force=*/old86 != 0);

    const ShipModel* model = GetShipModel(modelIndex);
    if (model)
        *reinterpret_cast<uint32_t*>(obj + 0x14) = model->scaleRelated;

    *reinterpret_cast<int64_t*>(obj + 0x26) = 0;
    *reinterpret_cast<int64_t*>(obj + 0x2e) = 0;
    *reinterpret_cast<int64_t*>(obj + 0x36) = static_cast<int64_t>(dist);

    *reinterpret_cast<uint16_t*>(obj + 0x98) = 0x8000;
    *reinterpret_cast<uint16_t*>(obj + 0x9a) = 0x8000;
    *reinterpret_cast<int32_t*>(obj + 0xa0) = 0x789a3456;

    FUNC_001675_MatBuildYZT(reinterpret_cast<int32_t*>(obj), yaw, pitch);
}

void SetupPlanetPaintGameObjAt(uint8_t* obj, int modelIndex, int32_t camX, int32_t camY,
                               int32_t camZ, int32_t seed, const int32_t* camRot16)
{
    // Start from demo setup (Q31 identity via MatBuildYZT), then overwrite pose.
    SetupPlanetPaintGameObj(obj, modelIndex, 0, 0, camZ);
    if (!obj) return;
    *reinterpret_cast<int64_t*>(obj + 0x26) = static_cast<int64_t>(camX);
    *reinterpret_cast<int64_t*>(obj + 0x2e) = static_cast<int64_t>(camY);
    *reinterpret_cast<int64_t*>(obj + 0x36) = static_cast<int64_t>(camZ);
    *reinterpret_cast<int32_t*>(obj + 0xa0) = seed;

    if (camRot16) {
        // Camera R is 16.16 with columns = right/up/fwd (same as ship.rot).
        // 0x1F uses SpecialVecMatMul: (v·col0, v·col1, v·col2) == R^T * v, so
        // store R itself — writing R^T here double-transposes and turns pitch
        // into a vertical-axis spin on a centred planet.
        // Q31: do NOT use <<15 (FP_ONE<<15 == INT_MIN).
        int32_t* m = reinterpret_cast<int32_t*>(obj);
        auto toQ31 = [](int32_t v16) -> int32_t {
            return static_cast<int32_t>(
                (static_cast<int64_t>(v16) * 0x7fffffff) / static_cast<int64_t>(FP_ONE));
        };
        for (int i = 0; i < 9; ++i)
            m[i] = toQ31(camRot16[i]);
    }
}

void EnqueuePlanetInterpreter(uint8_t* obj, int modelIndex, int32_t yaw, int32_t pitch,
                              int32_t dist)
{
    if (!obj) return;
    InitMeshInterpreterTable();
    PlanetAbLoadConfig();
    PlanetAbResetTrace();
    PlanetAbGetTrace()->active = true;

    SetupPlanetPaintGameObj(obj, modelIndex, yaw, pitch, dist);
    jjffe_planet_mat_trace_mat9(reinterpret_cast<const int32_t*>(obj), "cppobj");

    PlanetResetDrawState();
    FUNC_001682(reinterpret_cast<intptr_t>(obj));
}

void EnqueuePlanetInterpreterAt(uint8_t* obj, int modelIndex, int32_t camX, int32_t camY,
                                int32_t camZ, int32_t seed, const int32_t* camRot16)
{
    if (!obj) return;
    InitMeshInterpreterTable();
    PlanetAbLoadConfig();
    PlanetAbResetTrace();
    PlanetAbGetTrace()->active = true;

    SetupPlanetPaintGameObjAt(obj, modelIndex, camX, camY, camZ, seed, camRot16);
    jjffe_planet_mat_trace_mat9(reinterpret_cast<const int32_t*>(obj), "cppobj");

    PlanetResetDrawState();
    SetWorldPlacePaintObj(obj);
    FUNC_001682(reinterpret_cast<intptr_t>(obj));
    SetWorldPlacePaintObj(nullptr);
}

bool EnqueuePlanetInterpreter1680(uint8_t* obj, int modelIndex, const uint8_t* cam,
                                  const int64_t vec[3], int32_t seed, int32_t zoomBias,
                                  const PhysObj* phys)
{
    if (!obj || !cam || !vec) return false;
    const uint8_t* ship = FUNC_001538(modelIndex);
    if (!ship) return false;

    SetupPlanetPaintGameObj(obj, modelIndex, 0, 0, 0);
    *reinterpret_cast<int32_t*>(obj + 0xa0) = seed;
    *reinterpret_cast<int32_t*>(obj + 0x7e) = zoomBias;
    // FUNC_001536 stamps obj+0x9c = DATA_008804>>10 before FUNC_001680; mesh
    // Matrix/ALU phase reads it. Same stamp for the flight planet paint obj.
    *reinterpret_cast<uint16_t*>(obj + 0x9c) =
        static_cast<uint16_t>(GetGameClock() >> 10);
    // R1226: ASM FUNC_001536 draws the DATA_009133 pool object whose +0x86
    // was set by FUNC_000927. Port paint scratch is memset above → +0x86 stays 0.
    // With AB_SLOT_ORDER_FIX, PhysObj carries the spawn/927 slot, so stamping
    // PhysObjGetIndex replicates the ASM object byte (no cam special-case, no
    // hard-coded 108).
    if (AbStampPaintSlot86() && phys) {
        const uint8_t old86 = obj[0x86];
        const uint8_t idx = PhysObjGetIndex(phys);
        obj[0x86] = idx;
        R1224LogW86Paint("AB_SLOT_ORDER_FIX", obj, old86, idx);
    }
    {
        // Identity once per paint buf: model / phys index / paint +0x86.
        static uintptr_t s_idSeen[8] = {};
        static int s_nid = 0;
        struct IdSt {
            int on;
            int cap;
            int logs;
        };
        static IdSt id = {-1, 0, 0};
        if (id.on < 0) {
            const char* e = std::getenv("R1224_W86");
            id.on = (e && e[0] == '1') ? 1 : 0;
            id.cap = 32;
        }
        if (id.on && id.logs < id.cap && obj) {
            const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
            bool seen = false;
            for (int i = 0; i < s_nid; ++i)
                if (s_idSeen[i] == key) {
                    seen = true;
                    break;
                }
            if (!seen) {
                if (s_nid < 8) s_idSeen[s_nid++] = key;
                const int phys86 = phys ? static_cast<int>(PhysObjGetIndex(phys)) : -1;
                fprintf(stderr,
                        "R1224_ID n=%d clock=0x%x obj=%p model=%d paint86=%u phys86=%d\n",
                        id.logs, static_cast<unsigned>(GetGameClock()),
                        static_cast<void*>(obj), modelIndex,
                        static_cast<unsigned>(obj[0x86]), phys86);
                ++id.logs;
            }
        }
    }
    // Day-spin / parent-frame matrix into Hope's axes (installations already
    // do this via MissionRenderObject1680). Hope itself stays Q31 identity:
    // WorldMatrixInAnchorFrame(Hope) would write Hope*Hope^T with Q31 drift.
    if (phys && phys != WorldFind("Hope"))
        WorldMatrixInAnchorFrame(phys, reinterpret_cast<int32_t*>(obj));

    // JJFFE_PLANET1680_TRACE=1: one line per body, the first 64 bodies.
    static int s_traceLines = 0;
    const bool trace = getenv("JJFFE_PLANET1680_TRACE") && s_traceLines < 64;
    auto note = [&](const char* what, const Mission1680View* v) {
        if (!trace) return;
        ++s_traceLines;
        fprintf(stderr, "PLANET1680 model=%d vec=(%lld,%lld,%lld) lod=%d far=%d star=%d", modelIndex,
                static_cast<long long>(vec[0]), static_cast<long long>(vec[1]),
                static_cast<long long>(vec[2]), static_cast<int8_t>(obj[0x88]),
                *reinterpret_cast<const int32_t*>(ship + 0x20),
                *reinterpret_cast<const int32_t*>(ship + 0x28));
        if (v)
            fprintf(stderr, " pos=(%d,%d,%d) scale=%d zoom=%d", v->pos[0], v->pos[1], v->pos[2],
                    v->scale, v->zoom);
        fprintf(stderr, " %s\n", what);
    };

    Mission1680View view;
    if (!MissionView1680(obj, ship, cam, vec, &view)) {
        note("far", nullptr);
        return false;
    }

    // Port-only culls, in the same body space as the position: the bounding
    // radius is model+0x1c << scale (FUNC_001686's edge). Near the surface the
    // centre is often behind the camera while the sphere fills the view, so
    // only a sphere wholly behind it is skipped; the landed camera sits on the
    // surface (|pos| ≈ radius), so only a camera deep inside is.
    const int64_t meshR = static_cast<int64_t>(*reinterpret_cast<const uint32_t*>(ship + 0x1c))
                          << (view.scale & 0x1f);
    const int64_t distSq = static_cast<int64_t>(view.pos[0]) * view.pos[0] +
                           static_cast<int64_t>(view.pos[1]) * view.pos[1] +
                           static_cast<int64_t>(view.pos[2]) * view.pos[2];
    const int64_t deepR = meshR >> 1;
    if (view.pos[2] < -meshR || distSq < deepR * deepR) {
        note("culled", &view);
        return false;
    }
    note("drawn", &view);

    InitMeshInterpreterTable();
    PlanetAbLoadConfig();
    PlanetAbResetTrace();
    PlanetAbGetTrace()->active = true;
    jjffe_planet_mat_trace_mat9(reinterpret_cast<const int32_t*>(obj), "cppobj");

    PlanetResetDrawState();
    SetRenderGameObj(obj);
    MissionDraw1680(obj, ship, cam, view);
    return true;
}

void DrawPlanetInterpreter(int modelIndex, int32_t yaw, int32_t pitch, int32_t dist)
{
    if (const char* midTrace = getenv("AB_1F_MIDPOINT_TRACE")) {
        if (midTrace[0])
            jjffe_planet_mid_trace_open(midTrace);
    }
    if (const char* midScrTrace = getenv("AB_1F_MID_SCR_TRACE")) {
        if (midScrTrace[0])
            jjffe_planet_mid_scr_trace_open(midScrTrace);
    }
    if (const char* midElseTrace = getenv("AB_1F_MID_ELSE_TRACE")) {
        if (midElseTrace[0])
            jjffe_planet_mid_else_trace_open(midElseTrace);
    }
    if (const char* chain792 = getenv("AB_1F_792_CHAIN_TRACE")) {
        if (chain792[0])
            jjffe_planet_792_chain_open(chain792);
    }
    if (const char* pgTrace = getenv("AB_1F_869_PAINT_GATE_TRACE")) {
        if (pgTrace[0])
            jjffe_planet_paint_gate_trace_open(pgTrace);
    }

    const char* jobTrace = getenv("AB_JOB_TRACE");
    if (jobTrace && jobTrace[0])
        jjffe_job_trace_open(jobTrace);

    uint8_t obj[kPaintObjSize];
    FUNC_001786();
    EnqueuePlanetInterpreter(obj, modelIndex, yaw, pitch, dist);
    FUNC_001789();

    if (getenv("AB_1F_SHADE_STRIP_TRACE"))
        jjffe_planet_shade807_session_report();

    if (getenv("AB_1F_840_GATE_TRACE"))
        jjffe_planet_840_gate_trace_close();

    if (getenv("AB_1F_LOCAL14B_TRACE"))
        jjffe_planet_local14b_trace_close();

    if (getenv("AB_1F_807_STRIP_TRACE"))
        jjffe_planet_807_strip_trace_close();

    if (getenv("AB_1F_838_PACK_TRACE"))
        jjffe_planet_838_pack_trace_close();

    if (getenv("AB_1F_838_OPERAND_TRACE"))
        jjffe_planet_838_operand_trace_close();

    if (getenv("AB_1F_D265_TRACE"))
        jjffe_planet_d265_trace_close();

    if (getenv("AB_1F_877_TRACE"))
        jjffe_planet_877_trace_close();
    if (getenv("AB_1F_877_PREFSTORE_TRACE"))
        jjffe_planet_877_prefstore_close();
    if (getenv("AB_1F_877_CAMOFF_TRACE"))
        jjffe_planet_877_camoff_trace_close();

    if (const char* faceTrace = getenv("AB_1F_FACE_TRACE")) {
        if (faceTrace[0])
            PlanetAbDumpFaceTrace(faceTrace);
    }
    if (const char* leafTrace = getenv("AB_1F_LEAF_TRACE")) {
        if (leafTrace[0])
            PlanetAbDumpLeafTrace(leafTrace);
    }
    if (const char* midTrace = getenv("AB_1F_MIDPOINT_TRACE")) {
        if (midTrace[0]) {
            jjffe_planet_mid_trace_flush();
            char legPath[512];
            std::snprintf(legPath, sizeof(legPath), "%s.legacy", midTrace);
            PlanetAbDumpMidpointTrace(legPath);
        }
    }
    if (getenv("AB_1F_MID_SCR_TRACE"))
        jjffe_planet_mid_scr_trace_close();
    if (getenv("AB_1F_MID_ELSE_TRACE"))
        jjffe_planet_mid_else_trace_close();
    if (getenv("AB_1F_792_CHAIN_TRACE"))
        jjffe_planet_792_chain_close();
    if (const char* cull869 = getenv("AB_1F_869_CULL_TRACE")) {
        if (cull869[0])
            PlanetAbDump869CullTrace(cull869);
    } else if (const char* paint270 = getenv("AB_1F_270_PAINT_TRACE")) {
        if (paint270[0])
            PlanetAbDump869CullTrace(paint270);
    }
    if (const char* pgTrace = getenv("AB_1F_869_PAINT_GATE_TRACE")) {
        if (pgTrace[0])
            jjffe_planet_paint_gate_trace_close();
    }
    if (const char* subdivTrace = getenv("AB_1F_SUBDIV_TRACE")) {
        if (subdivTrace[0])
            jjffe_planet_subdiv_trace_close();
    }
    if (const char* dispatch834 = getenv("AB_1F_834_DISPATCH_TRACE")) {
        if (dispatch834[0])
            jjffe_planet_subdiv_trace_close();
    }

    jjffe_job_trace_flush();
    PlanetAbGetTrace()->active = false;
}

} // namespace jjffe
