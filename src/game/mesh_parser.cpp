#include "jjffe/mesh_parser.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/math.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace jjffe {

// Last parsed mesh data (for testing)
static MeshData s_lastMesh = {};
static bool s_handlersInitialized = false;

// FUNC_001758 destination bank (ASM renderCtx+0x54, 8×uint32). Reads via
// FUNC_001473 kind 0xC0 may index past 7 into other ctx words — those stay 0
// in the ship-demo / ParseShipMesh path.
static uint32_t s_meshAluRegs[64] = {};

void ResetMeshAluRegs()
{
    std::memset(s_meshAluRegs, 0, sizeof(s_meshAluRegs));
}

// When non-null, GetMeshAluReg prefers this 8-slot poly snapshot (stream order).
static const uint16_t* s_meshAluOverride = nullptr;

void SetMeshAluOverride(const uint16_t* regs8)
{
    s_meshAluOverride = regs8;
}

void SetMeshAluReg(int index, uint32_t value)
{
    if (index >= 0 && index < 64)
        s_meshAluRegs[index] = value;
}

uint32_t GetMeshAluReg(int index)
{
    if (index < 0 || index >= 64)
        return 0;
    if (s_meshAluOverride && index < 8)
        return s_meshAluOverride[index];
    return s_meshAluRegs[index];
}

// Live ObjectStruct for FUNC_001473 kind 0x80 / ALU case 10 (title yaw, etc.).
static const uint8_t* s_meshParseGameObj = nullptr;

void SetMeshParseGameObj(const uint8_t* objectStruct)
{
    s_meshParseGameObj = objectStruct;
}

const uint8_t* GetMeshParseGameObj()
{
    return s_meshParseGameObj;
}

uint16_t GetMeshParseGameObjWord(int index)
{
    if (!s_meshParseGameObj || index < 0 || index > 0x3f)
        return 0;
    return *reinterpret_cast<const uint16_t*>(s_meshParseGameObj + 0x9c +
                                              index * 2);
}

// FUNC_001473 for mesh-parse / ship-demo (GameObj via SetMeshParseGameObj).
static uint32_t MeshEval473(uint8_t enc)
{
    const uint32_t kind = enc & 0xc0u;
    if (kind == 0)
        return enc & 0x3fu;
    if (kind == 0x40)
        return static_cast<uint32_t>(static_cast<int16_t>(enc & 0x3fu) << 10);
    if (kind == 0x80)
        return GetMeshParseGameObjWord(static_cast<int>(enc & 0x3fu));
    if (kind == 0xc0)
        return GetMeshAluReg(static_cast<int>(enc & 0x3fu));
    return 0;
}

// < 0 = legacy (no LOD body skips). >= 0 = FUNC_001751 distance units.
static int32_t s_meshParseDistance = -1;
// ASM ctx+0x148 — ObjectStruct.scaleRelated. Used by IsFar (FUNC_001751).
static int32_t s_meshParseScaleRelated = 0;
// Top-level / nested model index for JJFFE_STUB_DISPATCH_LOG attribution.
// -1 = unset (synthetic tests / callers that omit SetMeshParseModelId).
static int s_meshParseModelId = -1;
// Default off: idle A/B must not force engine glows via GameObj thrust words.
// Enable when engine/throttle is on (or A/B --harvest-thrusters).
static bool s_meshHarvestThrusters = false;
// Export emitter harvest: force-scan every 0x0009 (ignores BitClear banks).
static bool s_meshHarvestThrustersBlind = false;
// Missile-spawn thrust word (core.cpp); ALU clamp needs >= 0x140, bit15 clear
// selects the primary bank (nested bit15 banks are the alternate set).
static constexpr uint16_t kMeshThrustActive = 0x442;
// Title constellation (#176/#177): enter BitClear bodies (pristine ALU regs are
// 0 → FUNC_001756 would skip every Sprite749 gate). Also keep all child tex.
static bool s_meshForceBitClearEnter = false;
static bool s_meshKeepAllSpriteChildren = false;
// Intro: suppress Coriolis family expand under title KeepAll (see header).
static bool s_meshSuppressStationChildren = false;
// FUNC_001749/750 recurse into child ObjectStruct meshes (cockpit glass etc.).
// Depth 3 covers Lynx-style wrappers (#70 → #218 → #219). Was 2 → nested
// sprites became type=14 stubs and the outer keep loop re-tagged them as the
// intermediate model.
static int s_spriteRecurseDepth = 0;
static constexpr int kSpriteRecurseMax = 3;
struct SpriteAttachFrame {
    int modelId = 0;
    uint8_t attachVert = 0;
    uint16_t spriteOp = 0;
};
static SpriteAttachFrame s_spriteStack[4];
// Last FUNC_001741 Matrix (low/C000/8000). Stamped onto the next Sprite749/748
// expand so case-6 attach can rebuild ctx+0x9c at transform time.
static uint16_t s_pendingMatrixOp = 0;
static uint16_t s_pendingMatrixWord = 0;
// ASM renderCtx+0x14 working matrix after op 0x1E negative commit (+0x9c → +0x14).
// 0 = identity / parent frame; non-zero stamped onto subsequent PushPoly.
static uint16_t s_workingMatrixOp = 0;
static uint16_t s_workingMatrixWord = 0;
static bool s_workingMatrixLive = false;
// Q31 local basis mirroring ASM ctx+0x9c / +0x14 (Sin32-scale, not FP_ONE).
// Populated by NormAxes 0x8019 (001770) + Matrix 0x8000 twist (001742→744),
// committed by negative op 0x1E. Independent of attach/outChildRot composition.
static int32_t s_pendingLocalBasis[9] = {};
static bool s_pendingLocalValid = false;
static int32_t s_workingLocalBasis[9] = {};
static bool s_workingLocalValid = false;
// ASM renderCtx+0x150 vertex-bank skip during this interpreter walk (source verts).
// Expanded mesh indices in bytecode are relative to the bank window.
static int s_parseVertBank = 0;

static void InstallBasisSentinel(int32_t* basis9)
{
    // FUNC_001770 — diagonal 0x7fffffff, off-diagonal 0.
    basis9[0] = kMeshBasisQ31One;
    basis9[1] = 0;
    basis9[2] = 0;
    basis9[3] = 0;
    basis9[4] = kMeshBasisQ31One;
    basis9[5] = 0;
    basis9[6] = 0;
    basis9[7] = 0;
    basis9[8] = kMeshBasisQ31One;
}

// FUNC_001744 — same column pairing as vertex_transform::RotateBasisColumns.
static void RotateLocalBasisColumns(int32_t* basis, int axis, int32_t cosA, int32_t sinA)
{
    int32_t* colA = nullptr;
    int32_t* colB = nullptr;
    if (axis == 0) {
        colA = basis + 3;
        colB = basis + 6;
    } else if (axis == 1) {
        colA = basis + 6;
        colB = basis + 0;
    } else {
        colA = basis + 0;
        colB = basis + 3;
    }
    const int32_t a0 = colA[0], a1 = colA[1], a2 = colA[2];
    const int32_t b0 = colB[0], b1 = colB[1], b2 = colB[2];
    colA[0] = FixedMulHigh(a0, cosA) + FixedMulHigh(b0, sinA);
    colA[1] = FixedMulHigh(a1, cosA) + FixedMulHigh(b1, sinA);
    colA[2] = FixedMulHigh(a2, cosA) + FixedMulHigh(b2, sinA);
    colB[0] = FixedMulHigh(-a0, sinA) + FixedMulHigh(b0, cosA);
    colB[1] = FixedMulHigh(-a1, sinA) + FixedMulHigh(b1, cosA);
    colB[2] = FixedMulHigh(-a2, sinA) + FixedMulHigh(b2, cosA);
}

// FUNC_001474 angle decode for Matrix payload (parse-time ALU bank).
static uint32_t DecodeLocalMatrixAngle(uint16_t word)
{
    if (const char* ov = getenv("AB_MATRIX_ANGLE"); ov && ov[0])
        return static_cast<uint32_t>(strtoul(ov, nullptr, 0));
    if (getenv("AB_MATRIX_NO_TWIST") && getenv("AB_MATRIX_NO_TWIST")[0] == '1')
        return 0;
    const uint32_t p = word;
    const uint32_t kind = p & 0xc0u;
    const int idx = static_cast<int>(p & 0x3fu);
    if (kind == 0 || kind == 0x40) {
        const int32_t hi = static_cast<int16_t>(static_cast<uint16_t>((p & 0x7fu) << 9));
        const uint32_t lo = (p >> 7) & 0x1ffu;
        return static_cast<uint32_t>(hi) | lo;
    }
    if (kind == 0xc0)
        return GetMeshAluReg(idx);
    if (kind == 0x80)
        return GetMeshParseGameObjWord(idx);
    return 0;
}

static void ClearLocalBasisState()
{
    s_pendingLocalValid = false;
    s_workingLocalValid = false;
    std::memset(s_pendingLocalBasis, 0, sizeof(s_pendingLocalBasis));
    std::memset(s_workingLocalBasis, 0, sizeof(s_workingLocalBasis));
}

void InstallMeshBasisSentinel(int32_t out9[9])
{
    if (out9)
        InstallBasisSentinel(out9);
}

void MeshTwistLocalBasis(int32_t basis9[9], int axis, uint32_t angle)
{
    if (!basis9)
        return;
    const int32_t sinA = Sin32(angle);
    const int32_t cosA = Sin32(angle + 0x4000u);
    RotateLocalBasisColumns(basis9, axis, cosA, sinA);
}

uint32_t MeshDecodeMatrixAngleLive(uint16_t word)
{
    const uint16_t* saved = s_meshAluOverride;
    s_meshAluOverride = nullptr;
    const uint32_t ang = DecodeLocalMatrixAngle(word);
    s_meshAluOverride = saved;
    return ang;
}

bool ApplyMeshSentinelWorkingBasis(int32_t out9[9], uint16_t matrixOp,
                                   uint16_t matrixWord)
{
    if (!out9 || (matrixOp & 0xC000u) != 0x8000u)
        return false;
    InstallBasisSentinel(out9);
    const int axis = static_cast<int>((matrixOp >> 5) & 3u);
    const uint32_t ang = MeshDecodeMatrixAngleLive(matrixWord);
    MeshTwistLocalBasis(out9, axis, ang);
    return true;
}

bool MeshWorkingLocalBasisValid()
{
    return s_workingLocalValid;
}

bool GetMeshWorkingLocalBasis(int32_t out9[9])
{
    if (!out9 || !s_workingLocalValid)
        return false;
    std::memcpy(out9, s_workingLocalBasis, sizeof(s_workingLocalBasis));
    return true;
}

// Opcode handler function type
// Returns pointer to next opcode data
typedef uint16_t* (*MeshOpcodeHandler)(uint32_t renderCtx, uint16_t* data, uint32_t opcode);

// Forward declarations for opcode handlers
static uint16_t* HandleEndMarker(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandlePlainLine(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleTexTri(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleTexTriPair(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleTexQuad(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleTexQuadPair(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleDirLine(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleControlSeq(uint32_t ctx, uint16_t* data, uint32_t opcode);
// R750: the ASM's 0x06 handler (FUNC_001696) enters/leaves band render mode through
// FUNC_001780, which enqueues the 001792/001793 band markers. The port's handler below
// only pushes a local barrier, and the hull-49 job trace shows the consequence: ASM 30
// primitives vs port 22, missing 001792 x3 / 001793 x3 / 001807 x2.
//
// R752 correction: this mirrors FUNC_001696's non-zero-mode branches only, and hull 49
// never takes them - its 0x06 opcodes are 0x0006, i.e. mode 0, which in the ASM falls to
// `default: FUNC_001780(0,0)`. So for that hull the knob enqueues nothing at all, which
// is why AB_1F_792_STATS reported zero calls; that also means the earlier "the jobs are
// enqueued but never executed" reading was unsupported. The ASM does EXECute 001792/
// 001793 for this hull (keys 20483 and 1) - so something OTHER than the 0x06 handler
// enters band mode, and that entry is still unidentified.
// Kept gated default-off: it is a faithful mirror of one ASM branch, unverified.
void FUNC_001780(int param_1, uint32_t param_2);
static uint16_t* HandleGateFar(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleGateNear(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleAlu(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleBitClear(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleBitSet(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleSprite749(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleSprite748(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleNested697(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleMatrix(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleTextPrimitive(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleSpline737(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleThruster(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleComplex762(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleComplex721(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleUnknown(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandlePointSource01(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* SkipOp0F(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* SkipOp10(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleOp12(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleOp1E(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* SkipOp1F(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleOp17Gate(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleOp18GlowPoints(uint32_t ctx, uint16_t* data, uint32_t opcode);
static uint16_t* HandleNormAxes771(uint32_t ctx, uint16_t* data, uint32_t opcode);
static void EmitThruster(uint16_t texInfo, uint8_t v1, uint8_t v2, uint16_t size);
static void HarvestThrustersInRange(const uint8_t* start, const uint8_t* end);

// Jump table for mesh opcode handlers (DATA_007816)
static MeshOpcodeHandler s_opcodeHandlers[32] = {};

// AB_TRACE_MESH=1 — bytecode walk / LOD gate / sprite-expand trace for digs.
static bool MeshTraceEnabled()
{
    static const int on = []() {
        const char* e = getenv("AB_TRACE_MESH");
        return e && e[0] == '1' ? 1 : 0;
    }();
    return on != 0;
}

// Current mesh being parsed
static MeshData* s_currentMesh = nullptr;
static int s_streamIndex = 0;

// 0xAB (FUNC_001752) sets a pending LOD/material gate consumed by the next primitive.
static bool s_pendingSpecialPoly = false;
static uint16_t s_pendingSpecialParam = 0;

// Optional walk inventory (tools/mesh_opcode_inventory).
static bool s_walkStatsActive = false;
static MeshWalkStats s_walkStats = {};

// JJFFE_STUB_DISPATCH_LOG=1 — env-gated probe for R1235/R1236 reachability.
// Logs op/arg/site + model/ctx so fleet/live sweeps attribute hits; default off.
static void StubDispatchLog(uint8_t op, uint32_t arg, const char* site,
                            uint32_t ctx = 0)
{
    const char* e = getenv("JJFFE_STUB_DISPATCH_LOG");
    if (!(e && e[0] == '1'))
        return;
    fprintf(stderr,
            "STUB_DISPATCH op=0x%02x arg=0x%x site=%s model=%d ctx=%#x "
            "dist=%d scale=%d\n",
            static_cast<unsigned>(op), static_cast<unsigned>(arg), site,
            s_meshParseModelId, static_cast<unsigned>(ctx),
            s_meshParseDistance, s_meshParseScaleRelated);
}

// Glow/flare vertex store (op 0x18): byte-pair vertex indices for GlowPoints.
// Indexed by MeshPoly.glowVertOffset, count = MeshPoly.glowVertCount.
static constexpr int kMaxGlowVerts = 4096;
static uint8_t s_glowVertStore[kMaxGlowVerts];
static int s_glowVertCount = 0;

// FUNC_001762 face light (ctx+0xf4) — last parse; renderer reads via GetMeshFaceLight.
static int8_t s_faceLightR = 0;
static int8_t s_faceLightG = 0;
static int8_t s_faceLightB = 0;

void ClearMeshFaceLight()
{
    s_faceLightR = s_faceLightG = s_faceLightB = 0;
}

// When non-null, GetMeshFaceLight prefers this poly snapshot.
static const int8_t* s_faceLightOverride = nullptr;

void SetMeshFaceLightOverride(const int8_t* rgb3)
{
    s_faceLightOverride = rgb3;
}

void GetMeshFaceLight(int8_t* r, int8_t* g, int8_t* b)
{
    if (const char* e = getenv("AB_FL_ZERO")) { if (e[0]=='1') { *r=*g=*b=0; return; } }

    if (s_faceLightOverride) {
        if (r) *r = s_faceLightOverride[0];
        if (g) *g = s_faceLightOverride[1];
        if (b) *b = s_faceLightOverride[2];
        return;
    }
    if (r) *r = s_faceLightR;
    if (g) *g = s_faceLightG;
    if (b) *b = s_faceLightB;
}

const uint8_t* GetGlowVertStore() { return s_glowVertStore; }
int GetGlowVertCount() { return s_glowVertCount; }

void MeshWalkStatsBegin()
{
    memset(&s_walkStats, 0, sizeof(s_walkStats));
    s_walkStatsActive = true;
}

void MeshWalkStatsEnd(MeshWalkStats* out)
{
    if (out)
        *out = s_walkStats;
    s_walkStatsActive = false;
}

bool MeshWalkStatsActive()
{
    return s_walkStatsActive;
}

void SetMeshParseDistance(int32_t distance)
{
    s_meshParseDistance = distance;
}

int32_t GetMeshParseDistance()
{
    return s_meshParseDistance;
}

void SetMeshParseModelId(int modelId)
{
    s_meshParseModelId = modelId;
}

int GetMeshParseModelId()
{
    return s_meshParseModelId;
}

void SetMeshParseScaleRelated(int32_t scaleRelated)
{
    s_meshParseScaleRelated = scaleRelated;
}

int32_t GetMeshParseScaleRelated()
{
    return s_meshParseScaleRelated;
}

void SetMeshHarvestThrusters(bool enable)
{
    s_meshHarvestThrusters = enable;
}

bool GetMeshHarvestThrusters()
{
    return s_meshHarvestThrusters;
}

uint16_t GetMeshThrustActiveWord()
{
    return kMeshThrustActive;
}

void SetMeshHarvestThrustersBlind(bool enable)
{
    s_meshHarvestThrustersBlind = enable;
}

bool GetMeshHarvestThrustersBlind()
{
    return s_meshHarvestThrustersBlind;
}

void SetMeshParseForceBitClearEnter(bool enable)
{
    s_meshForceBitClearEnter = enable;
}

bool GetMeshParseForceBitClearEnter()
{
    return s_meshForceBitClearEnter;
}

void SetMeshParseKeepAllSpriteChildren(bool enable)
{
    s_meshKeepAllSpriteChildren = enable;
}

bool GetMeshParseKeepAllSpriteChildren()
{
    return s_meshKeepAllSpriteChildren;
}

void SetMeshParseSuppressStationChildren(bool enable)
{
    s_meshSuppressStationChildren = enable;
}

bool GetMeshParseSuppressStationChildren()
{
    return s_meshSuppressStationChildren;
}

static bool IsSuppressedStationChild(int modelId)
{
    return s_meshSuppressStationChildren && modelId >= 72 && modelId <= 80;
}

static void ClearPolyExtras(MeshPoly& poly)
{
    poly.afterSpecialPoly = false;
    poly.specialParam = 0;
    poly.spriteOp = 0;
    poly.attachVert = 0;
    poly.matrixOp = 0;
    poly.matrixWord = 0;
    poly.matrixWorking = false;
    poly.matrixSentinel = false;
}

static void ApplyPendingSpecial(MeshPoly& poly)
{
    if (s_pendingSpecialPoly) {
        poly.afterSpecialPoly = true;
        poly.specialParam = s_pendingSpecialParam;
        s_pendingSpecialPoly = false;
        s_pendingSpecialParam = 0;
        poly.spriteOp = 0;
        poly.attachVert = 0;
    } else {
        // Do not zero specialParam — DirLine / other handlers store sizes there.
        // Handlers must zero-init MeshPoly (MeshPoly{}) when they omit it.
        poly.afterSpecialPoly = false;
        poly.spriteOp = 0;
        poly.attachVert = 0;
    }
}

static void PushPoly(MeshPoly& poly)
{
    if (!s_currentMesh)
        return;
    ApplyPendingSpecial(poly);
    // Op 0x1E positive bank: bytecode indices are window-relative; store absolute
    // expanded indices (ASM: cache idx + bank*2, source = bank + idx>>1).
    // Transform uses these absolute indices with cache bank base == 0.
    if (s_parseVertBank != 0) {
        for (int i = 0; i < 4; i++) {
            const int abs = static_cast<int>(poly.vertexIdx[i]) + s_parseVertBank * 2;
            // Soft clamp — real banks on #75 stay small; overflow would need
            // uint16 vertexIdx (not yet).
            poly.vertexIdx[i] = static_cast<uint8_t>(abs > 255 ? 255 : abs);
        }
    }
    // Op 0x1E negative commit: subsequent geometry uses working matrix (+0x14).
    if (s_workingMatrixLive && poly.matrixOp == 0) {
        poly.matrixOp = s_workingMatrixOp;
        poly.matrixWord = s_workingMatrixWord;
        poly.matrixWorking = true;
        // Only when NormAxes 001770 populated the local Q31 basis.
        poly.matrixSentinel = s_workingLocalValid;
    }
    poly.streamIndex = s_streamIndex++;
    if (poly.type != MeshPolyType::ComplexFill)
        poly.contourId = -1;
    for (int i = 0; i < 8; i++)
        poly.aluRegs[i] = static_cast<uint16_t>(s_meshAluRegs[i] & 0xffffu);
    s_currentMesh->polygons.push_back(poly);
}

static void PushBarrier(uint16_t opcode, uint8_t bandMode, int16_t payload)
{
    if (!s_currentMesh)
        return;
    MeshBarrier b{};
    b.opcode = opcode;
    b.payload = payload;
    b.streamIndex = s_streamIndex++;
    b.bandMode = bandMode;
    b.spriteOp = 0;
    b.attachVert = 0;
    b.childModelId = 0xffff;
    s_currentMesh->barriers.push_back(b);
}

// FUNC_001751: "far" iff scaled threshold < parse distance (ctx+0xc).
// Positive thresholds: thr' = thr << ((ctx+0x148 + gfxDetail) & 0x1f) when
// (scaleRelated + gfxDetail) > 0; else unshifted. Shift overflow → not far.
// Empirical A/B: the effective positive shift saturates at 9. Puma #60 needs
// 316<<9 to enter the top/bottom oval body while still skipping 488<<9; the
// old cap-at-8 also entered 488 and flooded the butterfly silhouette.
// Negative thresholds: ASM FUNC_001751 — index = thr & 0x7fff into
// DATA_009201 byte3 bit0 (back-facing ⇒ far). Dig: AB_FACE_GATE=1 enables;
// lazy FUNC_001481 when byte3 bit1 set (dirty from FUNC_001480).
// R652: FUNC_001751's negative-threshold facing gate is ASM behaviour and is
// ON by default (it used to be a dig). AB_FACE_GATE=0 restores the old
// enter-everything behaviour for bisect.
static bool FaceGateOn()
{
    const char* e = getenv("AB_FACE_GATE");
    return !(e && e[0] == '0' && e[1] == '\0');
}

static bool IsFar(int32_t threshold)
{
    if (s_meshParseDistance < 0) return false;
    if (threshold < 0) {
        if (FaceGateOn()) {
            const int idx = static_cast<int>(threshold & 0x7fff);
            // R1083 dig: hull 24's per-pose re-parse takes `far` on the
            // idx==2 facing gate (thr=-32766) and skips the block holding the
            // Eagle child #184 sprite op, so its 7 tip-ray PlainLines never
            // reach the transform (R1082). Force-enter for one index to measure.
            // Dig: AB_FACE_CULL2_ENTER=1.
            if (idx == 2) {
                const char* e = getenv("AB_FACE_CULL2_ENTER");
                if (e && e[0] == '1')
                    return false;
            }
            if (FacingFlagsCullDirty(idx))
                LazyComputeFacingCull(idx);
            return GetFacingFlagsCull(idx);
        }
        return false;
    }

    uint32_t thr = static_cast<uint32_t>(threshold);
    // DATA_008812 bit0 selects DATA_008813. Default 1/1 (ASM 000228
    // fallthrough); high-detail menu can set 812=-1 / 813=-2.
    int32_t gfxDetail = 0;
    if (const char* gfxEnv = getenv("AB_GFX_DETAIL"); gfxEnv && gfxEnv[0])
        gfxDetail = static_cast<int32_t>(strtol(gfxEnv, nullptr, 10));
    const int32_t sum = s_meshParseScaleRelated + gfxDetail;
    if (sum > 0) { // ASM: jng → shift only when sum > 0
        int shift = sum & 0x1f;
        // Default cap 9: Puma #60 (scaleRelated=11) needs thr=316 enter and
        // thr=488 skip at du=7. Gyr #35 (scaleRelated=12) needs the full shift
        // so NEAR thr=438 mid-LOD flats skip and Contour LOD enters — with the
        // hard cap at 9, Gyr fleet was pure OA (~1138, cpp_nz=0 on most poses).
        // R652: ASM shifts by the FULL (ctx+0x148 + gfxDetail) & 0x1f with only
        // the overflow check -- there is no cap. The old cap-9 was a fitted
        // workaround: at scale 11 (Puma #60) it made 366<<9 < dist (far, skip)
        // while ASM's 366<<11 > dist (near, enter), flipping whole gate bodies.
        int maxShift = sum & 0x1f;
        if (const char* maxShiftEnv = getenv("AB_ISFAR_MAX_SHIFT");
            maxShiftEnv && maxShiftEnv[0]) {
            char* endp = nullptr;
            long envMax = strtol(maxShiftEnv, &endp, 10);
            if (endp != maxShiftEnv && envMax >= 0)
                maxShift = static_cast<int>(envMax);
        }
        if (shift > maxShift)
            shift = maxShift;
        const uint32_t shifted = thr << shift;
        if ((shifted >> shift) != thr)
            return false; // ASM: shift lost bits → not far
        thr = shifted;
    }
    return static_cast<int32_t>(thr) < s_meshParseDistance;
}

// FUNC_001753: if cond, skip (opcode>>5)*2 bytes from afterThreshold.
// words==0 + cond → ASM sets DATA_007824=0 (end interpreter loop) and returns
// afterThreshold; our nullptr stops ParseMeshBytecode the same way.
static uint16_t* ApplySkip(uint16_t* afterThreshold, uint32_t opcode, bool cond)
{
    if (!cond) return afterThreshold;
    int32_t words = static_cast<int16_t>(opcode) >> 5;
    if (words == 0) {
        return nullptr;
    }
    return reinterpret_cast<uint16_t*>(
        reinterpret_cast<uint8_t*>(afterThreshold) + words * 2);
}

static void RecordGate(const char* name, uint32_t opcode, bool skip)
{
    if (!s_walkStatsActive)
        return;
    const int32_t words = static_cast<int16_t>(opcode) >> 5;
    const bool isFar = (name[0] == 'F'); // "FAR" / "NEAR"
    if (isFar) {
        s_walkStats.gateFarTotal++;
        if (skip) {
            s_walkStats.gateFarSkip++;
            if (words > 0)
                s_walkStats.gateSkipWords += words;
        }
    } else {
        s_walkStats.gateNearTotal++;
        if (skip) {
            s_walkStats.gateNearSkip++;
            if (words > 0)
                s_walkStats.gateSkipWords += words;
        }
    }
}

static void DumpGate(const char* name, uint32_t opcode, int16_t thr, bool far,
                     bool skip, const uint16_t* after, const uint16_t* next)
{
    RecordGate(name, opcode, skip);
    if (!getenv("AB_DUMP_GATES") || getenv("AB_DUMP_GATES")[0] != '1')
        return;
    const int32_t words = static_cast<int16_t>(opcode) >> 5;
    fprintf(stderr,
            "GATE %s op=0x%04x thr=%d far=%d skip=%d words=%d dist=%d scale=%d "
            "nextDelta=%ld\n",
            name, opcode, thr, far ? 1 : 0, skip ? 1 : 0, words,
            s_meshParseDistance, s_meshParseScaleRelated,
            next && after
                ? (long)(reinterpret_cast<const uint8_t*>(next) -
                         reinterpret_cast<const uint8_t*>(after))
                : -1L);
}

// Initialize mesh opcode handlers
void InitMeshOpcodeHandlers()
{
    if (s_handlersInitialized) return;

    memset(s_opcodeHandlers, 0, sizeof(s_opcodeHandlers));

    // Indexed by opcode & 0x1F — matches DATA_007816 in ffedat.asm
    s_opcodeHandlers[0x00] = HandleEndMarker;       // FUNC_001754
    s_opcodeHandlers[0x02] = HandlePlainLine;        // FUNC_001702
    s_opcodeHandlers[0x03] = HandleTexTri;           // FUNC_001707
    s_opcodeHandlers[0x04] = HandleTexQuad;          // FUNC_001711
    s_opcodeHandlers[0x05] = HandleComplex721;       // FUNC_001721 complex polygon
    s_opcodeHandlers[0x06] = HandleControlSeq;       // FUNC_001696 unified-z
    s_opcodeHandlers[0x07] = HandleTexTriPair;       // FUNC_001706
    s_opcodeHandlers[0x08] = HandleTexQuadPair;      // FUNC_001710
    s_opcodeHandlers[0x09] = HandleThruster;         // FUNC_001738 thruster / nozzle
    s_opcodeHandlers[0x0A] = HandleTextPrimitive;    // FUNC_001747 text
    s_opcodeHandlers[0x0B] = HandleGateFar;          // FUNC_001752
    s_opcodeHandlers[0x0C] = HandleGateNear;         // FUNC_001755
    s_opcodeHandlers[0x0D] = HandleAlu;              // FUNC_001758
    s_opcodeHandlers[0x0E] = HandleSprite749;        // FUNC_001749
    s_opcodeHandlers[0x01] = HandlePointSource01;    // FUNC_001698 — PointSource nav light
    s_opcodeHandlers[0x0F] = SkipOp0F;               // FUNC_001759 (+0B) — sound-only side effect (001908)
    s_opcodeHandlers[0x10] = SkipOp10;               // FUNC_001815 (+12B) — census: 1 model (#72)
    s_opcodeHandlers[0x12] = HandleOp12;             // FUNC_001763 — text-at-vertex (→001764/001812)
    s_opcodeHandlers[0x18] = HandleOp18GlowPoints;   // FUNC_001766 — glow/flare point list
    s_opcodeHandlers[0x1E] = HandleOp1E;             // FUNC_001767 — bank / matrix commit
    s_opcodeHandlers[0x1F] = SkipOp1F;               // FUNC_001874 (+12B) — D1 satellite deferral
    s_opcodeHandlers[0x17] = HandleOp17Gate;         // deferred twin of FUNC_001765 (walk: MeshOp765)
    s_opcodeHandlers[0x11] = HandleDirLine;          // deferred DirLine twin of FUNC_001760/761 (COST_001761); +6 words
    s_opcodeHandlers[0x13] = HandleBitClear;         // FUNC_001756
    s_opcodeHandlers[0x14] = HandleBitSet;           // FUNC_001757
    s_opcodeHandlers[0x15] = HandleNested697;        // FUNC_001697 nested z-band
    s_opcodeHandlers[0x16] = HandleSpline737;        // FUNC_001737 spline
    s_opcodeHandlers[0x19] = HandleNormAxes771;      // FUNC_001771 (no payload)
    s_opcodeHandlers[0x1A] = HandleComplex762;       // FUNC_001762
    s_opcodeHandlers[0x1B] = HandleSprite748;        // FUNC_001748 nested object
    s_opcodeHandlers[0x1C] = HandleMatrix;           // FUNC_001741
    s_opcodeHandlers[0x1D] = HandleAlu;              // FUNC_001758

    for (int i = 0; i < 32; i++) {
        if (!s_opcodeHandlers[i]) {
            s_opcodeHandlers[i] = HandleUnknown;
        }
    }

    s_handlersInitialized = true;
}

static uint16_t* HandleEndMarker(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)data; (void)opcode;
    return nullptr;
}

static uint16_t* HandlePlainLine(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    uint8_t* byteData = reinterpret_cast<uint8_t*>(data);

    uint16_t texInfo = data[0];
    uint8_t p1 = byteData[2];
    uint8_t p2 = byteData[3];

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::PlainLine;
        poly.vertexIdx[0] = p1;
        poly.vertexIdx[1] = p2;
        poly.vertexCount = 2;
        poly.texInfo = texInfo;
        poly.normalIdx = 0;
        poly.color = 0;
        PushPoly(poly);
    }

    // R1096: the record stays 4 bytes. The Skeet #45 stream proves it: its two
    // line records sit at +0 and +8 - [tex|b2=64|b3=62] then a 2-word padding
    // opcode 0x0002, then [tex|b2=65|b3=63]. Hull 24's pair looks 6-byte pitched
    // only because there the padding is ONE word; the walk still consumes each
    // line as data+2. So `data + 2` remains correct, and the Eagle band's real
    // gap (R1094's missing second tip-ray line) is elsewhere.
    return data + 2;
}

static uint16_t* HandleTexTri(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001707: dw tex, then bytes [2]=v2,[3]=v1,[4]=n,[5]=v3 — 708(tex,v1,v2,v3,n)
    // Call order: 708(tex, byte3, byte2, byte5, byte4) => verts v1,v2,v3 + normal
    uint8_t* byteData = reinterpret_cast<uint8_t*>(data);
    uint16_t texInfo = data[0];
    uint8_t v1 = byteData[3];
    uint8_t v2 = byteData[2];
    uint8_t v3 = byteData[5];
    uint8_t normalIdx = byteData[4];

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::TexTri;
        poly.vertexIdx[0] = v1;
        poly.vertexIdx[1] = v2;
        poly.vertexIdx[2] = v3;
        poly.vertexIdx[3] = 0;
        poly.vertexCount = 3;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = (texInfo == 0) ? 0x00 : 0;
        PushPoly(poly);
    }

    return data + 3;
}

static uint16_t* HandleTexTriPair(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001706: mirrored tri then FUNC_001707
    uint8_t* byteData = reinterpret_cast<uint8_t*>(data);
    uint16_t texInfo = data[0];
    uint8_t v1 = byteData[3];
    uint8_t v2 = byteData[2];
    uint8_t v3 = byteData[5];
    uint8_t normalIdx = byteData[4];

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::TexTriPair;
        poly.vertexIdx[0] = v1;
        poly.vertexIdx[1] = v2;
        poly.vertexIdx[2] = v3;
        poly.vertexIdx[3] = 0;
        poly.vertexCount = 3;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = 0;
        PushPoly(poly);

        MeshPoly mirror{};
        mirror.type = MeshPolyType::TexTriPair;
        mirror.vertexIdx[0] = v1 ^ 1;
        mirror.vertexIdx[1] = v2 ^ 1;
        mirror.vertexIdx[2] = v3 ^ 1;
        mirror.vertexIdx[3] = 0;
        mirror.vertexCount = 3;
        mirror.texInfo = texInfo;
        mirror.normalIdx = normalIdx ^ 1;
        mirror.color = 0;
        ClearPolyExtras(mirror);
        mirror.streamIndex = s_streamIndex++;
        for (int i = 0; i < 8; i++)
            mirror.aluRegs[i] = static_cast<uint16_t>(s_meshAluRegs[i] & 0xffffu);
        s_currentMesh->polygons.push_back(mirror);
    }

    return data + 3;
}

static uint16_t* HandleTexQuad(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    uint8_t* byteData = reinterpret_cast<uint8_t*>(data);

    uint16_t texInfo = data[0];
    uint8_t p1 = byteData[2];
    uint8_t p2 = byteData[3];
    uint8_t p3 = byteData[4];
    uint8_t p4 = byteData[5];
    uint8_t normalIdx = byteData[6];

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = (texInfo == 0) ? MeshPolyType::FlatQuad : MeshPolyType::TexQuad;
        poly.vertexIdx[0] = p3;
        poly.vertexIdx[1] = p1;
        poly.vertexIdx[2] = p4;
        poly.vertexIdx[3] = p2;
        poly.vertexCount = 4;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = (texInfo == 0) ? 0x00 : 0;
        PushPoly(poly);
    }

    return data + 4;
}

static uint16_t* HandleTexQuadPair(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    uint8_t* byteData = reinterpret_cast<uint8_t*>(data);

    uint16_t texInfo = data[0];
    uint8_t p1 = byteData[2];
    uint8_t p2 = byteData[3];
    uint8_t p3 = byteData[4];
    uint8_t p4 = byteData[5];
    uint8_t normalIdx = byteData[6];

    if (s_currentMesh) {
        // mixedParity: face uses both even+odd verts (centerline-crossing panel).
        // Pure half-hull twins are all-even primary / all-odd mirror. Adder fore
        // upper/lower pairs are mixed — mirror needs V-flip at draw (see renderer).
        auto parityMixed = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
            const unsigned p = (a & 1u) + (b & 1u) + (c & 1u) + (d & 1u);
            return p != 0 && p != 4;
        };
        const uint16_t mixedBit =
            parityMixed(p3, p1, p4, p2) ? static_cast<uint16_t>(0x10) : 0;

        MeshPoly poly{};
        poly.type = MeshPolyType::TexQuadPair;
        poly.vertexIdx[0] = p3;
        poly.vertexIdx[1] = p1;
        poly.vertexIdx[2] = p4;
        poly.vertexIdx[3] = p2;
        poly.vertexCount = 4;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = 0;
        // bit0=mirror, bit1=primary, bit4=mixedParity (UV hint)
        poly.specialParam = static_cast<uint16_t>(2u | mixedBit);
        PushPoly(poly);

        // FUNC_001710 mirror: same draw order as primary (p3,p1,p4,p2) with each ^1.
        MeshPoly mirror{};
        mirror.type = MeshPolyType::TexQuadPair;
        mirror.vertexIdx[0] = p3 ^ 1;
        mirror.vertexIdx[1] = p1 ^ 1;
        mirror.vertexIdx[2] = p4 ^ 1;
        mirror.vertexIdx[3] = p2 ^ 1;
        mirror.vertexCount = 4;
        mirror.texInfo = texInfo;
        mirror.normalIdx = normalIdx ^ 1;
        mirror.color = 0;
        ClearPolyExtras(mirror);
        mirror.specialParam = static_cast<uint16_t>(1u | mixedBit);
        mirror.streamIndex = s_streamIndex++;
        for (int i = 0; i < 8; i++)
            mirror.aluRegs[i] = static_cast<uint16_t>(s_meshAluRegs[i] & 0xffffu);
        s_currentMesh->polygons.push_back(mirror);
    }

    return data + 4;
}

// Opcode 0x11 → FUNC_001760 → FUNC_001761: shaded strip between two verts
// (not a 1px line). Payload after opcode: 6 words
//   [0]=tex  [1]=vertPair (lo/hi bytes)  [2]=sizeA<<8|normPack
//   [3]=sizeB<<8|…  [4..5] extra / unused for shuttle.
static uint16_t* HandleDirLine(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)opcode;

    uint16_t texInfo = data[0];
    uint16_t vertPair = data[1];
    uint16_t sizeNorm = data[2];
    uint16_t sizeB = data[3];
    uint8_t p1 = static_cast<uint8_t>(vertPair & 0xFF);
    uint8_t p2 = static_cast<uint8_t>((vertPair >> 8) & 0xFF);
    // Low byte of sizeNorm is the packed normal index (same encoding as other
    // faces): GetNormal uses entry=(idx>>1)-1; odd idx mirrors X. Matches
    // FUNC_001761 ((param_4&0x7e)>>1) and (param_4&1) negate.
    uint8_t normalIdx = static_cast<uint8_t>(sizeNorm & 0xFF);
    uint8_t halfA = static_cast<uint8_t>((sizeNorm >> 8) & 0xFF);
    uint8_t halfB = static_cast<uint8_t>((sizeB >> 8) & 0xFF);

    // R1236: attribute deferred 0x11 hits (walk lane uses FUNC_001761).
    StubDispatchLog(0x11, vertPair, "HandleDirLine/001761-deferred", ctx);

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::DirLine;
        poly.vertexIdx[0] = p1;
        poly.vertexIdx[1] = p2;
        poly.vertexIdx[2] = 0;
        poly.vertexIdx[3] = 0;
        poly.vertexCount = 2;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = 0;
        // Pack tip half-widths for strip extrusion at transform time.
        poly.specialParam = static_cast<uint16_t>((halfA << 8) | halfB);
        PushPoly(poly);
    }

    return data + 6;
}

static uint16_t* HandleControlSeq(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;

    // Opcode slot 6 → FUNC_001696 (unified-z). Emit MeshBarrier for FUNC_001780
    // and advance the same number of payload words as ASM.
    if ((opcode & 0x1F) == 0x06) {
        uint32_t mode = opcode >> 13;
        // R750: mirror FUNC_001696's render-mode handoff (see the declaration).
        if (getenv("AB_780_BAND") && getenv("AB_780_BAND")[0] == '1') {
            // R761: key is normally FUNC_001777's depth hash computed against a live
            // render ctx (+0x130 scale) the parser does not have. AB_780_KEY overrides
            // it so the "is the key the blocker?" question can be answered directly -
            // the ASM's own trace shows bandKey=20483 for hull 67.
            unsigned key = 1;
            if (const char* k = getenv("AB_780_KEY")) key = (unsigned)strtoul(k, nullptr, 0);
            FUNC_001780(mode == 0 ? 0 : 1, key);
        }
        if (mode == 0) {
            PushBarrier(static_cast<uint16_t>(opcode), 0, 0);
            return data;
        }
        if (mode == 2) {
            PushBarrier(static_cast<uint16_t>(opcode), 1, 0);
            return data;
        }
        if (mode == 7) {
            uint32_t sub = opcode >> 5;
            if (sub == 0x7fd) {
                PushBarrier(static_cast<uint16_t>(opcode), 1,
                            static_cast<int16_t>(data[0]));
                return data + 1;
            }
            if (sub == 0x7fe || sub == 0x7ff) {
                PushBarrier(static_cast<uint16_t>(opcode), 1, 0);
                return data;
            }
            PushBarrier(static_cast<uint16_t>(opcode), 1,
                        static_cast<int16_t>(data[0]));
            return data + 1;
        }
        if (mode == 1 || mode == 3) {
            // Vertex-id list: emit open-band barrier, then skip FF-terminated list.
            // Key resolved at transform (first/seed vertex in opcode>>5 for now;
            // full max/min list keys are a follow-up if needed).
            PushBarrier(static_cast<uint16_t>(opcode), 1, 0);
            uint8_t* p = reinterpret_cast<uint8_t*>(data);
            for (;;) {
                uint8_t hi = p[1];
                p += 2;
                if (hi != 0xFF) break;
            }
            return reinterpret_cast<uint16_t*>(p);
        }
        PushBarrier(static_cast<uint16_t>(opcode), 0, 0);
        return data + 1;
    }

    // Legacy fallback for other control-looking opcodes routed here
    s_pendingSpecialPoly = false;
    s_pendingSpecialParam = 0;
    return data + 1;
}

// FUNC_001752: skip following block when FAR. 0xAB also marks window material.
static uint16_t* HandleGateFar(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    int16_t threshold = static_cast<int16_t>(data[0]);
    uint16_t* after = data + 1;

    if (MeshTraceEnabled()) {
        const bool far = (s_meshParseDistance >= 0 && threshold >= 0)
            ? ((threshold << (s_meshParseScaleRelated & 0x1f)) < s_meshParseDistance)
            : false;
        fprintf(stderr, "GATE_FAR: thr=%d dist=%d scale=%d far=%d\n",
                threshold, s_meshParseDistance, s_meshParseScaleRelated, far ? 1 : 0);
    }

    if ((opcode & 0xFF) == 0xAB) {
        // Window gate: keep poly when close / legacy; skip body when far.
        if (s_meshParseDistance >= 0 && IsFar(threshold)) {
            s_pendingSpecialPoly = false;
            RecordGate("FAR", opcode, true);
            return ApplySkip(after, opcode, true);
        }
        s_pendingSpecialPoly = true;
        s_pendingSpecialParam = static_cast<uint16_t>(threshold);
        if (s_walkStatsActive)
            s_walkStats.specialPending++;
        return after;
    }

    if (s_meshParseDistance < 0) {
        // Legacy: consume threshold only (old shuttle parse)
        s_pendingSpecialPoly = false;
        s_pendingSpecialParam = 0;
        return after;
    }

    bool far = IsFar(threshold);
    // Dig: AB_FORCE_FAR_ENTER=THR comma-list → never skip those FAR gates.
    if (const char* fe = getenv("AB_FORCE_FAR_ENTER"); fe && fe[0]) {
        const int thr = static_cast<int>(threshold);
        const char* s = fe;
        while (*s) {
            char* endp = nullptr;
            long v = strtol(s, &endp, 10);
            if (endp == s) break;
            if (static_cast<int>(v) == thr) { far = false; break; }
            s = endp;
            if (*s == ',') ++s;
        }
    }
    // Contour A/B compensation (AB_FACE_GATE): GDB IsFar=1 for Puma Contour
    // GateFar thr=-32762 (facing idx 6) on pitched poses, but ASM still
    // enqueues Contour spans (001794–99) matching this body's nested
    // Complex721 (Far -32762 → Near488 skip → Far488 → C721). C++ Contours
    // only live under that body, so literal skip holes Puma fleet (+1252)
    // while Saker −32766 / Eagle keep literal skip gains. Default: still
    // enter idx-6 Contour Far. Dig: AB_FACE_LITERAL_SKIP=1 for GDB skip.
    if (far && threshold < 0 &&
        FaceGateOn() &&
        (static_cast<int>(threshold) & 0x7fff) == 6) {
        const bool literal = getenv("AB_FACE_LITERAL_SKIP") &&
                             getenv("AB_FACE_LITERAL_SKIP")[0] == '1';
        if (!literal)
            far = false;
    }
    uint16_t* next = ApplySkip(after, opcode, far);
    DumpGate("FAR", opcode, threshold, far, far, after, next);
    return next;
}

// FUNC_001755: skip following block when CLOSE (not far).
static uint16_t* HandleGateNear(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    int16_t threshold = static_cast<int16_t>(data[0]);
    uint16_t* after = data + 1;

    if (s_meshParseDistance < 0) {
        return after;  // legacy: no body skip
    }

    const bool far = IsFar(threshold);
    bool skip = !far;
    // Dig: AB_NEAR_ENTER=1 enters every GateNear body (Courier wing Contours
    // sit behind GateNear with thr=-32762/-32761 — pristine IsFar→not-far→skip).
    if (getenv("AB_NEAR_ENTER") && getenv("AB_NEAR_ENTER")[0] == '1')
        skip = false;
    // Dig: AB_NEAR_NEG_ENTER=1 only enters when threshold < 0 (object-flag gates).
    if (threshold < 0 && getenv("AB_NEAR_NEG_ENTER") &&
        getenv("AB_NEAR_NEG_ENTER")[0] == '1')
        skip = false;
    uint16_t* next = ApplySkip(after, opcode, skip);
    DumpGate("NEAR", opcode, threshold, far, skip, after, next);
    return next;
}

static uint16_t* HandleAlu(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // FUNC_001758: two operand bytes → ALU → ctx+0x54+slot.
    if (!data)
        return data;
    const uint8_t* ops = reinterpret_cast<const uint8_t*>(data);
    const uint32_t uVar1 = MeshEval473(ops[0]);
    const uint32_t uVar2 = MeshEval473(ops[1]);
    const uint8_t bVar4 = static_cast<uint8_t>(uVar2);
    uint32_t local_c = 0;
    switch ((opcode >> 4) & 0xfu) {
    case 0: local_c = uVar1 + uVar2; break;
    case 1: local_c = uVar1 - uVar2; break;
    case 2: local_c = uVar1 * uVar2; break;
    case 3:
        local_c = (uVar2 != 0) ? static_cast<uint32_t>(static_cast<int32_t>(uVar1) /
                                                       static_cast<int32_t>(uVar2))
                               : uVar1;
        break;
    case 4: local_c = uVar1 >> (bVar4 & 0x1f); break;
    case 5: local_c = uVar1 << (bVar4 & 0x1f); break;
    case 6: local_c = (uVar1 > uVar2) ? uVar1 : uVar2; break;
    case 7: local_c = (uVar2 <= uVar1) ? (uVar2 - 1) : uVar1; break;
    case 8:
        local_c = (uVar2 != 0) ? ((uVar1 << 16) / uVar2) : 0;
        break;
    case 9: local_c = static_cast<uint32_t>(static_cast<int32_t>(uVar1) >> (bVar4 & 0x1f)); break;
    case 10:
        // FUNC_001758: *(u16*)(GameObj+0x9c+(uVar1+uVar2)*2)
        local_c = GetMeshParseGameObjWord(
            static_cast<int>((uVar1 + uVar2) & 0x3fu));
        break;
    case 0xb: local_c = (uVar1 < uVar2) ? 0 : uVar1; break;
    case 0xc: local_c = (uVar2 < uVar1) ? 0 : uVar1; break;
    case 0xd: {
        // ASM: Sin32(angle=uVar2) then FUNC_001521(sin, scale=uVar1).
        // Prior FixedMulHigh(sin, 0) forced 0 (Skeet workaround) and killed
        // title #177 Matrix orbit angles in ALU[3].
        local_c = static_cast<uint32_t>(FixedMulHigh(
            Sin32(uVar2), static_cast<int32_t>(uVar1)));
        break;
    }
    case 0xe: {
        local_c = static_cast<uint32_t>(FixedMulHigh(
            Sin32(uVar2 + 0x4000u), static_cast<int32_t>(uVar1)));
        break;
    }
    case 0xf: local_c = uVar1 & uVar2; break;
    default: local_c = 0; break;
    }
    const int slot = static_cast<int>((opcode >> 8) & 7u);
    s_meshAluRegs[slot] = local_c & 0xffffu;
    if (getenv("AB_DUMP_ALU") && getenv("AB_DUMP_ALU")[0] == '1') {
        fprintf(stderr,
                "ALU op=0x%x slot=%d ops=0x%02x,0x%02x u1=%u u2=%u -> 0x%x\n",
                (unsigned)((opcode >> 4) & 0xf), slot, ops[0], ops[1],
                (unsigned)uVar1, (unsigned)uVar2, (unsigned)s_meshAluRegs[slot]);
    }
    return reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(data) + 2);
}

// Peek a BitClear/BitSet skip body for a canopy Sprite (0x0E) without thruster
// opcodes (*09). Asp #40 +0xbc gates sprite model #66 this way; early BitClears
// and Krait stub bodies are thruster/variant gates we must keep skipping.
// Require sprite modelId to resolve a real mesh — raw kind==0x0E in operands
// (e.g. Krait 0x020e) is a common false positive.
static bool BitClearBodyIsCanopySprite(const uint16_t* after, int32_t words)
{
    if (!after || words <= 0 || words > 512)
        return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(after);
    const uint8_t* end = p + static_cast<size_t>(words) * 2;
    bool hasSprite = false;
    bool hasThruster = false;
    while (p + 1 < end) {
        const uint16_t w =
            static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
        const uint32_t kind = w & 0x1Fu;
        if (kind == 0x0Eu || kind == 0x1Bu) {
            const int modelId = static_cast<int>(w >> 5);
            if (modelId > 0 && modelId < SHIP_MODEL_COUNT) {
                const ShipModel* child = GetShipModel(modelId);
                if (child && child->meshDataOffset != 0)
                    hasSprite = true;
            }
        }
        if (kind == 0x09u)
            hasThruster = true;
        p += 2;
    }
    return hasSprite && !hasThruster;
}

// Courier wing LOD (#215): BitClear 0x0553 gates Complex721 Contours. Pristine
// ObjectStruct bits are clear → default skip, but ASM ship-demo enters these
// bodies (wing Contour lobes after 001809 flats). Only when nested (sprite
// child parse) — do not scan Contour payload for false "sprite" kinds.
static bool BitClearBodyIsContourLod(const uint16_t* after, int32_t words)
{
    if (!after || words < 2 || words > 512)
        return false;
    if (s_spriteRecurseDepth < 1)
        return false;
    return (after[0] & 0x1Fu) == 0x05u;
}

// Sprite749 6-byte attach form (Courier #215 wings): attach word + 4 index
// bytes with trailing 0x7f sentinel (FUNC_001750 bit15 layout authored even
// when opcode bit15 is clear). Without this, Spline 0x16 consumes the next
// Matrix+#215 and drops the mirrored wing. Dig: AB_SPRITE_2WORD=1.
static bool Sprite749HasSixBytePayload(const uint16_t* data)
{
    if (!data)
        return false;
    if (getenv("AB_SPRITE_2WORD") && getenv("AB_SPRITE_2WORD")[0] == '1')
        return false;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(data);
    return b[5] == 0x7f;
}

static uint16_t* HandleBitClear(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // FUNC_001756 → 001753: SKIP when (mask & MeshEval473(opnd0)) == 0.
    // Kind 0xc0 (ALU) is live in ship-demo — parent #62 sets ALU[2]=3 before
    // Sprite749 #217, so BitClear opnd0=0xc2 must MeshEval even with null
    // GameObj (Panther top body Contours). Kind 0x80 (GameObj) stays on the
    // Asp canopy / Courier Contour-LOD heuristics unless SetMeshParseGameObj
    // is set (title #176/#177). Dig: AB_MESH_NO_BITCLEAR_SKIP=1 force-enter.
    uint16_t* after = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(data) + 2);
    if (s_meshParseDistance < 0) return after;
    const char* noSkip = getenv("AB_MESH_NO_BITCLEAR_SKIP");
    if (s_meshForceBitClearEnter || (noSkip && noSkip[0] == '1'))
        return after;
    const uint8_t* opndBytes = reinterpret_cast<const uint8_t*>(data);
    const uint8_t opnd0 = opndBytes[0];
    const uint8_t bitIdx = opndBytes[1];
    const int32_t words = static_cast<int16_t>(opcode) >> 5;
    const uint32_t kind = opnd0 & 0xc0u;
    if (s_meshParseGameObj || kind == 0xc0u) {
        const uint32_t val = MeshEval473(opnd0);
        const uint32_t mask =
            (bitIdx == 0) ? 0xffffffffu : (1u << ((bitIdx - 1) & 0x1f));
        const bool skip = (mask & val) == 0;
        if (getenv("AB_DUMP_BITCLEAR") && getenv("AB_DUMP_BITCLEAR")[0] == '1') {
            fprintf(stderr,
                    "BitClear %s op=0x%04x opnd=0x%02x%02x val=0x%x words=%d\n",
                    skip ? "SKIP" : "ENTER", static_cast<unsigned>(opcode),
                    opnd0, bitIdx, val, words);
        }
        return ApplySkip(after, opcode, skip);
    }
    // FUNC_001473 type 0x80 | index 1, byte1==0 → whole-word test (Asp canopy).
    if (opnd0 == 0x81 && BitClearBodyIsCanopySprite(after, words)) {
        if (getenv("AB_DUMP_BITCLEAR") && getenv("AB_DUMP_BITCLEAR")[0] == '1') {
            fprintf(stderr, "BitClear ENTER canopy-sprite op=0x%04x words=%d\n",
                    static_cast<unsigned>(opcode), words);
        }
        return after;
    }
    if (BitClearBodyIsContourLod(after, words)) {
        if (getenv("AB_DUMP_BITCLEAR") && getenv("AB_DUMP_BITCLEAR")[0] == '1') {
            fprintf(stderr, "BitClear ENTER contour-lod op=0x%04x words=%d\n",
                    static_cast<unsigned>(opcode), words);
        }
        return after;
    }
    return ApplySkip(after, opcode, true);
}

static uint16_t* HandleBitSet(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // FUNC_001757 → 001753: SKIP when (mask & val) != 0. Pristine clear → enter
    // for kind 0x80 without GameObj. Kind 0xc0 (ALU) uses live MeshEval473.
    uint16_t* after = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(data) + 2);
    if (s_meshParseDistance < 0) return after;
    const uint8_t* opndBytes = reinterpret_cast<const uint8_t*>(data);
    const uint8_t opnd0 = opndBytes[0];
    const uint8_t bitIdx = opndBytes[1];
    const uint32_t kind = opnd0 & 0xc0u;
    if (s_meshParseGameObj || kind == 0xc0u) {
        const uint32_t val = MeshEval473(opnd0);
        const uint32_t mask =
            (bitIdx == 0) ? 0xffffffffu : (1u << ((bitIdx - 1) & 0x1f));
        const bool skip = (mask & val) != 0;
        if (getenv("AB_DUMP_BITCLEAR") && getenv("AB_DUMP_BITCLEAR")[0] == '1') {
            fprintf(stderr,
                    "BitSet %s op=0x%04x opnd=0x%02x%02x val=0x%x\n",
                    skip ? "SKIP" : "ENTER", static_cast<unsigned>(opcode),
                    opnd0, bitIdx, val);
        }
        return ApplySkip(after, opcode, skip);
    }
    return ApplySkip(after, opcode, false);
}

// Walk mesh opcodes into s_currentMesh (does not reset stream index / harvest).
static void RunMeshInterpreter(const uint8_t* meshData, size_t meshSize)
{
    if (!meshData || meshSize < 4 || !s_currentMesh)
        return;
    InitMeshOpcodeHandlers();
    // Per-walk renderCtx substrate (ASM fresh ctx each interpret level).
    // Nested walks save/restore parent pending/working/bank around the call.
    s_parseVertBank = 0;
    s_pendingMatrixOp = 0;
    s_pendingMatrixWord = 0;
    s_workingMatrixOp = 0;
    s_workingMatrixWord = 0;
    s_workingMatrixLive = false;
    ClearLocalBasisState();
    const uint16_t* data = reinterpret_cast<const uint16_t*>(meshData);
    const uint16_t* end = reinterpret_cast<const uint16_t*>(meshData + meshSize);
    const size_t baseOffset = reinterpret_cast<size_t>(meshData);
    while (data < end) {
        uint16_t opcode = *data;
        if (MeshTraceEnabled()) {
            fprintf(stderr,
                    "MESH_OP: offset=%zu opcode=0x%04x type=0x%02x arg=%u"
                    " next=%04x %04x %04x %04x\n",
                    (size_t)(data) - baseOffset, opcode, opcode & 0x1F, opcode >> 5,
                    data + 1 < end ? data[1] : 0, data + 2 < end ? data[2] : 0,
                    data + 3 < end ? data[3] : 0, data + 4 < end ? data[4] : 0);
        }
        if (s_walkStatsActive) {
            s_walkStats.opcodeKind[opcode & 0x1F]++;
            s_walkStats.opCount++;
            if ((opcode & 0x1F) == 0x1E &&
                s_walkStats.op1eCount < MeshWalkStats::kMaxOp1E) {
                s_walkStats.op1eWords[s_walkStats.op1eCount++] = opcode;
            }
        }
        if (getenv("AB_DUMP_OPS") && getenv("AB_DUMP_OPS")[0] == '1') {
            const uint8_t* base = meshData;
            fprintf(stderr, "OP +%04x op=0x%04x kind=0x%02x o=%02x%02x\n",
                    (unsigned)(reinterpret_cast<const uint8_t*>(data) - base),
                    opcode, opcode & 0x1f,
                    ((const uint8_t*)(data))[2], ((const uint8_t*)(data))[3]);
        }
        const size_t polyBegin = s_currentMesh->polygons.size();
        if (::getenv("AB_682_CENSUS") && ::getenv("AB_682_CENSUS")[0] == '1') {
            static int s_cc = 0;
            if (s_cc < 1500) { fprintf(stderr, "CSLOT %02x\n", (unsigned)(opcode & 0x1F)); ++s_cc; }
        }
        MeshOpcodeHandler handler = s_opcodeHandlers[opcode & 0x1F];
        // Deferred walker has no ASM renderCtx. Log ctx= as low32 of GameObj*
        // when SetMeshParseGameObj was used (BitClear/ALU); else 0. Gate
        // predicates here read s_meshParseDistance / scaleRelated, not this
        // pointer — walk-lane MeshOp* logs the real render ctx instead.
        const uint32_t walkCtx = s_meshParseGameObj
            ? static_cast<uint32_t>(
                  reinterpret_cast<uintptr_t>(s_meshParseGameObj))
            : 0u;
        uint16_t* nextData =
            handler ? handler(walkCtx, const_cast<uint16_t*>(data + 1), opcode)
                    : nullptr;
        // Stamp the ctx face light of *this* interpreter level. Nested objects
        // stamped themselves during their own walk (FUNC_001750 gives them a
        // separate renderCtx), so leave those alone.
        for (size_t i = polyBegin; i < s_currentMesh->polygons.size(); ++i) {
            MeshPoly& p = s_currentMesh->polygons[i];
            if (p.faceLightStamped)
                continue;
            p.faceLightR = s_faceLightR;
            p.faceLightG = s_faceLightG;
            p.faceLightB = s_faceLightB;
            p.faceLightStamped = true;
        }
        if (!nextData)
            break;
        if (getenv("AB_DUMP_OPS") && getenv("AB_DUMP_OPS")[0] == '1') {
            const uint8_t* base = meshData;
            fprintf(stderr, "  -> +%04x\n",
                    (unsigned)(reinterpret_cast<const uint8_t*>(nextData) - base));
        }
        data = nextData;
    }
}

static size_t MeshSizeForModel(int modelIndex, const ShipModel* model)
{
    if (!model || modelIndex < 0 || modelIndex >= SHIP_MODEL_COUNT)
        return 0;
    const ShipModelSubPtrs& sub = SHIP_MODEL_SUBPTRS[modelIndex];
    const uint32_t meshOff = model->meshDataOffset;
    if (meshOff == 0)
        return 0;
    // Title logo stubs #361–#363: loadShipModel promotes meshDataOffset to the
    // #346–#348 orbit-card mesh. Size must follow the card's collision label
    // (not #361's own END, which sits after the plate and would over-read).
    if (modelIndex >= 361 && modelIndex <= 363) {
        const int cardIndex = 346 + (modelIndex - 361);
        const ShipModelSubPtrs& card = SHIP_MODEL_SUBPTRS[cardIndex];
        if (meshOff == card.mesh && card.collision > card.mesh)
            return static_cast<size_t>(card.collision - card.mesh);
    }
    // Stub ObjectStructs (#201/#186): mesh promoted to the collision label;
    // real bytecode spans collision → verts.
    if (meshOff == sub.collision && sub.vertices > sub.collision)
        return static_cast<size_t>(sub.vertices - sub.collision);
    // Normal ships: bytecode ends at the collision label.
    if (sub.collision > meshOff)
        return static_cast<size_t>(sub.collision - meshOff);
    if (sub.vertices > meshOff)
        return static_cast<size_t>(sub.vertices - meshOff);
    return 1024;
}

size_t MeshBytecodeSize(int modelIndex)
{
    return MeshSizeForModel(modelIndex, GetShipModel(modelIndex));
}

// Keep attach-child faces that matter for glassbox / tip assemblies.
//   0x403c — mat60 tip glass (Saker)
//   0x4030 — mat48 tube walls (#201 nozzle body)
//   0x2000 — flat RGB punch / Asp #186 canopy panes
//   0x4009 / 0x400c / 0x400f — Explorer #65 tip-assembly (model #223).
//     Full keep floods the hollow crescent (fleet ~1930). KeepExplorerTipFace
//     retains the ASM-leaning subset (fleet ~1179 vs 1323 with none).
// Flat-RGB hull faces (bits15:14==0, nonzero) are the whole visible body for
// full-ship attach children (Imperial Trader #58 wraps Courier #56 via
// Sprite749). keepFlatRgb is set only when the parent is a pure sprite-wrapper
// stub (no hull of its own), so canopy/tip children on real hulls (Asp #40
// #66) still filter to the whitelist and don't add stray flat faces.
static bool ExplorerTipExtraMatch(uint16_t texInfo, uint8_t normalIdx, uint8_t attachVert)
{
    // Dig: AB_EXPL_TIP_EXTRA=74:0x400c:14,75:0x4009:4 (comma-separated triples).
    const char* e = getenv("AB_EXPL_TIP_EXTRA");
    if (!e || !e[0])
        return false;
    const char* p = e;
    while (*p) {
        unsigned a = 0, t = 0, n = 0;
        char* end = nullptr;
        a = static_cast<unsigned>(strtoul(p, &end, 0));
        if (!end || *end != ':')
            break;
        p = end + 1;
        t = static_cast<unsigned>(strtoul(p, &end, 0));
        if (!end || *end != ':')
            break;
        p = end + 1;
        n = static_cast<unsigned>(strtoul(p, &end, 0));
        if (!end)
            break;
        p = end;
        if (a == attachVert && t == texInfo && n == normalIdx)
            return true;
        if (*p == ',')
            ++p;
        else
            break;
    }
    return false;
}

static bool KeepExplorerTipFace(uint16_t texInfo, uint8_t normalIdx, uint8_t attachVert)
{
    // Fleet-greedy subset vs ASM dump-ab (top tip-band OA, low hollow OC).
    // Attach 74/75 are Sprite749 verts on Imperial Explorer #65 → model #223.
    // AB_EXPL_TIP_ALL=1 keeps every tip mat face (old OC-heavy dig).
    // AB_EXPL_TIP_EXTRA=… ORs additional (attach,tex,n) triples for digs.
    if (getenv("AB_EXPL_TIP_ALL") && getenv("AB_EXPL_TIP_ALL")[0] == '1')
        return texInfo == 0x4009 || texInfo == 0x400c || texInfo == 0x400f;
    // Dig: AB_EXPL_TIP_ATTACH=74|75 keeps all tip mats on that attach only.
    if (const char* ta = getenv("AB_EXPL_TIP_ATTACH")) {
        if (ta[0]) {
            const unsigned want = static_cast<unsigned>(strtoul(ta, nullptr, 0));
            if (attachVert == want)
                return texInfo == 0x4009 || texInfo == 0x400c || texInfo == 0x400f;
            return false;
        }
    }
    if (ExplorerTipExtraMatch(texInfo, normalIdx, attachVert))
        return true;
    // Dig: AB_EXPL_TIP_EXACT=1 → only EXTRA list (ignore hardcoded subset).
    if (getenv("AB_EXPL_TIP_EXACT") && getenv("AB_EXPL_TIP_EXACT")[0] == '1')
        return false;

    if (attachVert == 74) {
        // After world-midpoint attach (flag 0x0B): tip verts match ASM L1=0.
        // Keep ASM tip band without 0x400c:14 / 0x400f (those flood hollow OC).
        // Fleet ~643 (was ~1131); orbit_p90 ~12 (was 231 pure OA).
        if (texInfo == 0x4009)
            return normalIdx == 4 || normalIdx == 8 || normalIdx == 12;
        if (texInfo == 0x400c)
            return normalIdx == 16 || normalIdx == 22 || normalIdx == 24 ||
                   normalIdx == 32;
        return false;
    }
    if (attachVert == 75) {
        return (texInfo == 0x4009 && (normalIdx == 6 || normalIdx == 10)) ||
               (texInfo == 0x400f && (normalIdx == 34 || normalIdx == 36));
    }
    return texInfo == 0x4009 || texInfo == 0x400c || texInfo == 0x400f;
}

// Parent-owned polys only (spriteOp==0). After the first Sprite749/748 expands
// on a wrapper ship, polyBegin includes child faces — using polyBegin<=2 would
// then filter later children to the tip/canopy whitelist (Lynx #70 stubs).
static size_t CountParentHullPolys(size_t polyBegin)
{
    if (!s_currentMesh)
        return 0;
    size_t n = 0;
    for (size_t i = 0; i < polyBegin; ++i) {
        if (s_currentMesh->polygons[i].spriteOp == 0)
            ++n;
    }
    return n;
}

static bool KeepSpriteChildTex(uint16_t texInfo, bool keepAll, bool keepFlatRgb,
                               int childModelId = -1, uint8_t attachVert = 0,
                               uint8_t normalIdx = 0);

// Tag newly expanded child polys. Nested expands (already spriteOp!=0) keep
// their own child model / attach; only set attachParent* from the stack.
static void TagSpriteChildPolys(size_t polyBegin, int modelId, uint8_t attachVert,
                                uint16_t attachPayload, bool keepAll,
                                bool keepWrapperAll, bool keepFlatRgb)
{
    if (!s_currentMesh)
        return;
    // Imperial Trader #58 → Courier #56 nests model #215 (attach 32/33) with
    // wing flats/Contours plus garbage TexQuad v=[26,252,254,253] / DirLine.
    // Attach verts are flag 0x13 (FUNC_001477): static t=0 → tip (by=10/11),
    // which places #215 Contours at full ASM wing span. Keep flat-RGB + Contour
    // LOD; still drop garbage TexQuad/DirLine. Dig: AB_DROP_SPRITE_215=1.
    const bool keep215 = modelId == 215 &&
        !(getenv("AB_DROP_SPRITE_215") && getenv("AB_DROP_SPRITE_215")[0] == '1');
    if (modelId == 215 && !keep215 && !keepAll) {
        s_currentMesh->polygons.resize(polyBegin);
        return;
    }
    std::vector<MeshPoly> kept;
    kept.reserve(s_currentMesh->polygons.size());
    for (size_t i = 0; i < polyBegin; ++i)
        kept.push_back(s_currentMesh->polygons[i]);
    for (size_t i = polyBegin; i < s_currentMesh->polygons.size(); ++i) {
        MeshPoly p = s_currentMesh->polygons[i];
        // Nested Sprite749/748 already tagged its own child faces — do not
        // overwrite with this intermediate model's id/attach (Lynx #218→#219).
        // Only keep nested under wrapper expands: Saker #226 is whitelist-only
        // and must drop nested #365 (else OC flood). Lynx #218 is keepWrapperAll.
        if (p.spriteOp != 0) {
            if (getenv("AB_TAG_NESTED_TRACE") &&
                getenv("AB_TAG_NESTED_TRACE")[0] == '1')
                fprintf(stderr, "NESTED model=%d tex=0x%04x type=%d keep=%d\n",
                        modelId, (unsigned)p.texInfo, (int)p.type,
                        (keepWrapperAll || keepAll) ? 1 : 0);
            if (keepWrapperAll || keepAll)
                kept.push_back(p);
            continue;
        }
        if (modelId == 215 && keep215 && !keepAll && !keepWrapperAll) {
            const bool flatRgb = (p.texInfo >> 14) == 0 && p.texInfo != 0;
            const bool wingContour =
                p.type == MeshPolyType::ComplexFill &&
                (p.texInfo == 0x0336 || p.texInfo == 0x0600 ||
                 p.texInfo == 0x0666 || p.texInfo == 0x0555 ||
                 p.texInfo == 0x0444);
            const bool badVerts =
                p.vertexIdx[0] > 40 || p.vertexIdx[1] > 40 ||
                p.vertexIdx[2] > 40 || p.vertexIdx[3] > 40;
            if (badVerts || p.type == MeshPolyType::DirLine ||
                p.type == MeshPolyType::PlainLine)
                continue;
            if (!(flatRgb || wingContour))
                continue;
        } else if (modelId == 222 && p.texInfo == 0x4005) {
            // Panther #62 → #217 → #222: KeepSpriteChildTex drops 0x4005 as
            // textured; flat-RGB keep only left 0x20ee. Keep ComplexFill body
            // Contours (af4bf5f) and the Contour-adjacent TexQuad seam panels
            // (ASM 001805 after 001799 rgb16=0x222 on orbit_p270). Facing is
            // per-attach ComputeFaceFacing (attach-60 back, attach-61 front).
        } else if (!keepWrapperAll &&
                   !KeepSpriteChildTex(p.texInfo, keepAll, keepFlatRgb, modelId,
                                       attachVert,
                                       static_cast<uint8_t>(p.normalIdx))) {
            if (MeshTraceEnabled())
                fprintf(stderr, "TAG_CHILD: model=%d texInfo=0x%04x type=%d kept=0\n",
                        modelId, p.texInfo, (int)p.type);
            continue;
        }
        if (MeshTraceEnabled())
            fprintf(stderr, "TAG_CHILD: model=%d texInfo=0x%04x type=%d nv=%d kept=1\n",
                    modelId, p.texInfo, (int)p.type, (int)p.vertexCount);
        p.afterSpecialPoly = true;
        p.specialParam = PackSpriteChildParam(modelId, p.specialParam);
        p.spriteOp = attachPayload ? attachPayload : 0xFFFFu;
        p.attachVert = attachVert;
        // Preserve stream-ordered matrix from op 0x1E commit (PushPoly). Tag only
        // fills case-6 pending Matrix when the child never committed a working matrix.
        if (p.matrixOp == 0) {
            p.matrixOp = s_pendingMatrixOp;
            p.matrixWord = s_pendingMatrixWord;
            p.matrixWorking = false;
            p.matrixSentinel = false;
        }
        // Parent ALU after nested restore (Tag runs post-child). Needed for:
        // - Skeet #208 canopy 0x13 lerp (slot3=0x8400)
        // - Title constellation Matrix words (0x00c1 etc. → ALU[1] orbit twist)
        // Child emit stamped the child's bank (often zero); overwrite here.
        // keepAll = title #176/#177; matrixOp = FUNC_001741 orbit basis.
        // Trap B: do NOT clobber aluRegs on NormAxes-sentinel polys — twist
        // angle is resolved live at transform; Tag restore would break #334.
        // Non-sentinel 0x835c (11 models) still need parent ALU via Tag.
        if (modelId == 208 || keepAll ||
            (p.matrixOp != 0 && !p.matrixSentinel)) {
            for (int i = 0; i < 8; i++)
                p.aluRegs[i] = static_cast<uint16_t>(s_meshAluRegs[i] & 0xffffu);
        }
        // depth>=2 → nested under stack[depth-2] (pushed before this recurse).
        if (s_spriteRecurseDepth >= 2) {
            const SpriteAttachFrame& par = s_spriteStack[s_spriteRecurseDepth - 2];
            p.attachParentModel = static_cast<uint16_t>(par.modelId);
            p.attachParentVert = par.attachVert;
            p.attachParentOp = par.spriteOp ? par.spriteOp : 0xFFFFu;
        }
        kept.push_back(p);
    }
    s_currentMesh->polygons.swap(kept);
}

static bool KeepSpriteChildTex(uint16_t texInfo, bool keepAll, bool keepFlatRgb,
                               int childModelId, uint8_t attachVert,
                               uint8_t normalIdx)
{
    if (keepAll)
        return true;
    // Whitelist: Saker tip glass / nozzle walls / Asp canopy punch /
    // Explorer tip-assembly (subset via KeepExplorerTipFace).
    if (texInfo == 0x403c || texInfo == 0x4030 || texInfo == 0x2000)
        return true;
    if (childModelId == 223 &&
        (texInfo == 0x4009 || texInfo == 0x400c || texInfo == 0x400f)) {
        // R648 LAW (Explorer #65 tip assembly, model 223 attach 74/75): the
        // child is drawn WHOLE. probe65_1708.gdb shows ASM's FUNC_001708
        // enqueues all 18 tri records per attach at every measured pose; the
        // old KeepExplorerTipFace() "fleet-greedy subset" was a fitted filter
        // that dropped 11 of 18 and produced #65's oa (351/366 @05, 219/235
        // @06). Keep-all drops #65's oa to 0 across all 8 poses (1808->1055,
        // every other hull bit-identical: model 223 is Explorer-only).
        // Dig/bisect: AB_EXPL_TIP_SUBSET=1 restores the old subset.
        const char* subset = getenv("AB_EXPL_TIP_SUBSET");
        if (subset && subset[0] == '1')
            return KeepExplorerTipFace(texInfo, normalIdx, attachVert);
        return true;
    }
    const bool flatRgb = (texInfo >> 14) == 0 && texInfo != 0;
    if (keepFlatRgb && flatRgb)
        return true;
    // Imperial Trader #58 expands Courier #56 — keep Courier flat-RGB hull
    // even if the parent was not classified as a pure sprite wrapper.
    if (childModelId == 56 && flatRgb)
        return true;
    // Cobra Mk3 #38 Sprite749 → model #205 antenna: single flat 0x0888 quad.
    // Without this, expand filters to empty and leaves a 1px Sprite stub.
    if (childModelId == 205 && flatRgb)
        return true;
    // Skeet #45 / Osprey lineage: Sprite749 → model #208 canopy. TexQuads
    // 0x4005/0x400b are the ASM upper 001805 hull (shade0 0x222/0x666).
    // 0x0888 is the canopy top Contour (ASM 001795/96 ring + 001799
    // rgb16=0x888 → VGA8); the inner 0x0232 ring maps to VGA0 and punches it.
    // Digs: AB_C721_KEEP_208=0 drops the 0x888 ring, AB_C721_KEEP_208=2 also
    // keeps 0x0232. AB_SPRITE_CHILD_ALL still keeps both.
    if (childModelId == 208) {
        if (texInfo == 0x4005 || texInfo == 0x400b)
            return true;
        const char* k208 = getenv("AB_C721_KEEP_208");
        const char mode = (k208 && k208[0]) ? k208[0] : '1';
        if (texInfo == 0x0888)
            return mode != '0';
        if (texInfo == 0x0232)
            return mode == '2';
    }
    // Sprite749 → model #281 rocker-engine sleeve, textured 0xC25D. The
    // expand filter dropped it to a stub; ASM paints it (thargoid #51 −36).
    if (childModelId == 281 && texInfo == 0xc25d)
        return !(getenv("AB_NO_KEEP_281") && getenv("AB_NO_KEEP_281")[0] == '1');
    // Sprite748 → model #187: the quad bridging the front and rear nozzle
    // cones. texInfo 0x3000 is RGB(0,0,0) plus the FUNC_001695 face-light flag,
    // so with the child ctx light it resolves to 0x888 — the shade0 in ASM's
    // 001809 job for Saker III. DrawFlatPolygon narrows this to the hulls where
    // ASM actually emits that job.
    if (childModelId == 187 && texInfo == 0x3000)
        return !(getenv("AB_NO_KEEP_187") && getenv("AB_NO_KEEP_187")[0] == '1');
    // Osprey X #32 Sprite749 → model #202 wing Contours (flat 0x0444).
    // Without this, expand filters to empty stubs (fleet ~5.3k → ~314).
    if (childModelId == 202 && flatRgb)
        return true;
    return false;
}

static uint16_t* HandleSprite749(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // FUNC_001749: word after opcode — low byte = attach vertex / object slot.
    // FUNC_001750: if opcode bit15 set, four extra vertex-index bytes follow.
    // Then recursively FUNC_001773 the ObjectStruct at opcode>>5 (DATA_004681).
    uint8_t* bytes = reinterpret_cast<uint8_t*>(data);
    uint8_t vert = bytes[0];
    const int modelId = static_cast<int>(opcode >> 5);
    bool expanded = false;

    // R1082: why a sprite child is NOT expanded. The Eagle family expanded
    // child #184 (tip-ray PlainLines) on the load-time parse and lost it on the
    // per-pose re-parse (41/7 vs 32/0 polys, R1081), so the bail reason matters.
    if (MeshTraceEnabled())
        fprintf(stderr, "SPRITE749_ENTRY model=%d depth=%d dist=%d mesh=%d\n",
                modelId, s_spriteRecurseDepth, s_meshParseDistance,
                s_currentMesh ? 1 : 0);

    if (s_currentMesh && s_spriteRecurseDepth < kSpriteRecurseMax &&
        s_meshParseDistance >= 0 && !IsSuppressedStationChild(modelId)) {
        const ShipModel* child = GetShipModel(modelId);
        const uint8_t* childMesh =
            child ? GetShipModelPointer(child->meshDataOffset) : nullptr;
        const size_t childSize = MeshSizeForModel(modelId, child);
        if (MeshTraceEnabled())
            fprintf(stderr, "SPRITE749: model=%d child=%p mesh=%p size=%zu dist=%d\n",
                    modelId, (void*)child, (void*)childMesh, childSize,
                    s_meshParseDistance);
        if (childMesh && childSize >= 4) {
            // Child render ctx uses a tighter distance (FUNC_001750 local_8 /
            // LOD). Empirically parent>>4 matches glassbox: 0x400 → 0x40, which
            // GateNear-skips past the stub END into the mat60 canopy band.
            const int32_t savedDist = s_meshParseDistance;
            const int32_t savedScale = s_meshParseScaleRelated;
            int32_t childDist = jjffe::AttachVertexDepthForGate(vert);
            if (childDist < 1)
                childDist = s_meshParseDistance >> 4;
            if (childDist < 1)
                childDist = 1;
            const size_t polyBegin = s_currentMesh->polygons.size();
            const size_t barrierBegin = s_currentMesh->barriers.size();
            const uint16_t attachPayload = data[0];
            // Child ALU (thruster #204 zeros slot3) must not clobber parent regs:
            // Skeet canopy attach vert 44 is flag 0x13 reading slot3=0x8400.
            uint32_t savedAlu[64];
            std::memcpy(savedAlu, s_meshAluRegs, sizeof(savedAlu));
            const uint16_t savedPendMatOp = s_pendingMatrixOp;
            const uint16_t savedPendMatWord = s_pendingMatrixWord;
            const uint16_t savedWorkMatOp = s_workingMatrixOp;
            const uint16_t savedWorkMatWord = s_workingMatrixWord;
            const bool savedWorkLive = s_workingMatrixLive;
            int32_t savedPendLocal[9];
            int32_t savedWorkLocal[9];
            std::memcpy(savedPendLocal, s_pendingLocalBasis, sizeof(savedPendLocal));
            std::memcpy(savedWorkLocal, s_workingLocalBasis, sizeof(savedWorkLocal));
            const bool savedPendLocalValid = s_pendingLocalValid;
            const bool savedWorkLocalValid = s_workingLocalValid;
            const int savedVertBank = s_parseVertBank;
            // Title constellation cards (#346–#360) BitClear against child
            // ObjectStruct bits the host title object does not populate — ASM
            // spawns live children. Force-enter those gates so each orbit card
            // shows its full ship set (Thargoid/Adder/etc.), while parent #176
            // still sequences one card via faithful ALU bands.
            const bool titleNest = s_meshKeepAllSpriteChildren || s_meshParseGameObj;
            if (titleNest)
                childDist = savedDist; // keep framing distance (no >>4 LOD strip)
            const bool savedForceBC = s_meshForceBitClearEnter;
            if (titleNest)
                s_meshForceBitClearEnter = true;
            s_meshParseDistance = childDist;
            s_meshParseScaleRelated = static_cast<int32_t>(child->scaleRelated);
            s_spriteStack[s_spriteRecurseDepth] = {modelId, vert, attachPayload};
            s_spriteRecurseDepth++;
            if (FaceGateOn())
                PushFacingParseContext(child, vert, attachPayload);
            // R1099: FUNC_001750 writes childCtx[0x54] = parentCtx[0x54] + 1 right
            // before recursing (ALU reg 0 doubles as a nesting-depth counter).
            // BitSet/BitClear gates in the child mesh (opnd0=0xC0, slot 0) key off
            // this value; without the +1 the child inherits the parent's slot 0
            // unchanged and such gates evaluate ENTER where the ASM evaluates SKIP.
            s_meshAluRegs[0] = savedAlu[0] + 1;
            const int savedModelId749 = s_meshParseModelId;
            s_meshParseModelId = modelId;
            RunMeshInterpreter(childMesh, childSize);
            s_meshParseModelId = savedModelId749;
            if (FaceGateOn())
                PopFacingParseContext();
            s_meshForceBitClearEnter = savedForceBC;
            std::memcpy(s_meshAluRegs, savedAlu, sizeof(s_meshAluRegs));
            s_pendingMatrixOp = savedPendMatOp;
            s_pendingMatrixWord = savedPendMatWord;
            s_workingMatrixOp = savedWorkMatOp;
            s_workingMatrixWord = savedWorkMatWord;
            s_workingMatrixLive = savedWorkLive;
            std::memcpy(s_pendingLocalBasis, savedPendLocal, sizeof(s_pendingLocalBasis));
            std::memcpy(s_workingLocalBasis, savedWorkLocal, sizeof(s_workingLocalBasis));
            s_pendingLocalValid = savedPendLocalValid;
            s_workingLocalValid = savedWorkLocalValid;
            s_parseVertBank = savedVertBank;
            // Tag while depth still elevated (nested uses stack[depth-2]).
            // R755 LAW (thargoid #49 -> child model 209): this child is drawn WHOLE.
            // Hull 49 dropped all 74 of its faces (materials 0x403d x32, 0x402e x16,
            // 0x0000 x16, 0x4029 x10) - the missing raised superstructure. At --du 5,
            // yaw 0/pitch 0 the diff goes 3429 -> 27 when the child is kept whole,
            // which is why this is a keepAll-level rule: whitelisting one material
            // (0x402e) reached only 3381.
            // Bisect: AB_CHILD_209_DROP=1 restores the old drop.
            // Children the ASM draws WHOLE. Each was measured, not inferred:
            //  209 - thargoid #49: 74 faces, all dropped (3429 -> 27 when kept)
            //  207 - #44: 32 faces (0x4029 etc.), all dropped (967 -> 1 when kept)
            // Deliberately NOT a blanket keep: #69's child 225 goes 0 -> 8467 when
            // everything is kept, and #71's children 226/365 are the Saker OC flood.
            // Every id below was OBSERVED to have 100% of its faces dropped by the
            // whitelist (TAG_CHILD kept=0 for all of them) and the parent hull was
            // measured with the flood-all switch before being added:
            //   209  #49  74 faces   3429 -> 27
            //   207  #44  32 faces    967 -> 1
            //   153/182 (#18) were tried and REMOVED: at the record's poses they take
            //   hull 18 from 3573 px to 13577 (R764) even though a --du 5 spot sweep
            //   showed 9 -> 2. The record is the arbiter, not the spot sweep.
            //   183  #19  40 faces     13 -> 3
            //   185  #19  16 faces      (same hull)
            //   211/212 (#51) were tried and REMOVED: inert at the record's poses
            //   (5065 -> 5065), so they would only be dead weight.
            // NOT included on purpose: 225 (#69, keeps 4 faces and floods 0 -> 8467 when
            // everything is kept) and 226/365 (#71, the Saker OC flood).
            const bool keepWholeChild = modelId == 209 || modelId == 207 ||
                                        modelId == 183 || modelId == 185 ||
                                        modelId == 180 ||   // #16 at du 1: 1335 -> 0
                                        modelId == 226 ||   // #71 at du 64: 12848 -> 886
                                        modelId == 365;     // #71 (nested)
            const bool keep209 =
                keepWholeChild &&
                !(getenv("AB_CHILD_209_DROP") && getenv("AB_CHILD_209_DROP")[0] == '1');
            const bool keepAll = s_meshKeepAllSpriteChildren || keep209 ||
                                 (getenv("AB_SPRITE_CHILD_ALL") &&
                                  getenv("AB_SPRITE_CHILD_ALL")[0] == '1');
            // Nested Contours (Lynx #219 0x0888): parent-hull count sees the
            // intermediate child's still-untagged faces, so keepFlatRgb at
            // depth>=2. Do NOT force keepWrapperAll — that floods Saker #365.
            const bool keepWrapperAll = CountParentHullPolys(polyBegin) <= 2;
            const bool keepFlatRgb =
                keepWrapperAll || s_spriteRecurseDepth >= 2;
            TagSpriteChildPolys(polyBegin, modelId, vert, attachPayload, keepAll,
                                keepWrapperAll, keepFlatRgb);
            s_spriteRecurseDepth--;
            s_meshParseDistance = savedDist;
            s_meshParseScaleRelated = savedScale;
            // Optional dump of pre-filter child polys (AB_DUMP_SPRITE_CHILD=1).
            if (getenv("AB_DUMP_SPRITE_CHILD") &&
                getenv("AB_DUMP_SPRITE_CHILD")[0] == '1') {
                fprintf(stderr,
                        "SPRITE_CHILD model=%d attach=%u op=0x%04x payload=0x%04x "
                        "polys=%zu barriers=%zu depth=%d parent=%u\n",
                        modelId, vert, static_cast<unsigned>(opcode),
                        static_cast<unsigned>(attachPayload),
                        s_currentMesh->polygons.size() - polyBegin,
                        s_currentMesh->barriers.size() - barrierBegin,
                        s_spriteRecurseDepth + 1,
                        s_spriteRecurseDepth >= 1
                            ? s_spriteStack[s_spriteRecurseDepth - 1].modelId
                            : 0);
                for (size_t i = polyBegin; i < s_currentMesh->polygons.size(); ++i) {
                    const MeshPoly& p = s_currentMesh->polygons[i];
                    fprintf(stderr,
                            "  CP[%zu] type=%d tex=0x%04x n=%u vc=%d "
                            "v=[%u,%u,%u,%u] sp=0x%04x ap=%u/%u\n",
                            i - polyBegin, static_cast<int>(p.type), p.texInfo,
                            p.normalIdx, p.vertexCount, p.vertexIdx[0],
                            p.vertexIdx[1], p.vertexIdx[2], p.vertexIdx[3],
                            p.spriteOp, p.attachParentModel, p.attachVert);
                }
            }
            {
                std::vector<MeshBarrier> kept;
                kept.reserve(s_currentMesh->barriers.size());
                for (size_t i = 0; i < barrierBegin; ++i)
                    kept.push_back(s_currentMesh->barriers[i]);
                for (size_t i = barrierBegin; i < s_currentMesh->barriers.size(); ++i) {
                    MeshBarrier b = s_currentMesh->barriers[i];
                    if (!keepAll && !keepWrapperAll &&
                        b.opcode != 0x43c6 && b.opcode != 0xe3c6)
                        continue;
                    if (b.spriteOp == 0) {
                        b.spriteOp = attachPayload ? attachPayload : 0xFFFFu;
                        b.attachVert = vert;
                        b.childModelId = static_cast<uint16_t>(modelId);
                    }
                    kept.push_back(b);
                }
                s_currentMesh->barriers.swap(kept);
            }
            expanded = s_currentMesh->polygons.size() > polyBegin;
        }
    }

    if (s_currentMesh && !expanded) {
        MeshPoly poly{};
        poly.type = MeshPolyType::Sprite;
        poly.vertexIdx[0] = vert;
        poly.vertexIdx[1] = 0;
        poly.vertexIdx[2] = 0;
        poly.vertexIdx[3] = 0;
        poly.vertexCount = 1;
        poly.normalIdx = 0;
        poly.texInfo = static_cast<uint16_t>(opcode);
        poly.color = 0;
        PushPoly(poly);
    }
    if ((opcode & 0x8000) != 0) {
        return data + 3; // type word + 4 index bytes
    }
    // Courier #215 dual-wing: 6-byte attach (trailing 0x7f) even when opcode
    // bit15 is clear — otherwise Spline eats Matrix + mirrored #215.
    if (Sprite749HasSixBytePayload(data))
        return data + 3;
    return data + 1;
}

// FUNC_001697: nested unified-z band marker. Consumes opcode only (returns
// param_2 = stream after opcode). Slot 0x15 — Asp canopy BitClear starts with
// 0x0a15 before FUNC_001748. Previously HandleUnknown advanced +1 word and
// misaligned into TexTriPair garbage (IMRA #66 false-positive).
static uint16_t* HandleNested697(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    if ((static_cast<int16_t>(opcode) < 0) && s_currentMesh) {
        // Negative form: FUNC_001782 close-band. Record as barrier mode 0.
        PushBarrier(static_cast<uint16_t>(opcode), 0, 0);
    } else if (s_currentMesh) {
        PushBarrier(static_cast<uint16_t>(opcode), 1,
                    static_cast<int16_t>(opcode >> 5));
    }
    return data; // zero payload
}

// FUNC_001748 word at payload +4 is the nested object's tint: 001692 turns it
// into RGB nibbles, 001694 adds the parent light when the 0x1000 flag is set,
// and 001750 stores the result as the child renderCtx+0xf4 face light. Saker
// III's rear nozzle joins the cones with a flat 0x3000 (RGB 0 + light flag)
// quad, so without this the cylinder resolves to VGA0 and disappears.
static void ApplySprite748ChildFaceLight(const uint16_t* payload)
{
    if (getenv("AB_NO_SPRITE748_FACELIGHT") &&
        getenv("AB_NO_SPRITE748_FACELIGHT")[0] == '1')
        return;
    const uint16_t word = payload[2];
    int r = 0, g = 0, b = 0;
    if ((word >> 14) == 0) {
        r = (word >> 8) & 0xF;
        g = (word >> 4) & 0xF;
        b = word & 0xF;
    }
    if ((word & 0x1000) != 0) {
        r += s_faceLightR;
        g += s_faceLightG;
        b += s_faceLightB;
    }
    s_faceLightR = static_cast<int8_t>(r);
    s_faceLightG = static_cast<int8_t>(g);
    s_faceLightB = static_cast<int8_t>(b);
    if (MeshTraceEnabled())
        fprintf(stderr, "SPRITE748_light: word=0x%04x -> (%d,%d,%d)\n", word, r,
                g, b);
}

// FUNC_001748: alternate nested-object entry (Asp canopy → ObjectStruct #186).
// Layout after opcode: 6 bytes of attach/flags/tex, then FUNC_001750 stream
// (extra +4 bytes if opcode bit15). Recurse like 001749.
static uint16_t* HandleSprite748(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(data);
    uint8_t vert = bytes[0];
    const int modelId = static_cast<int>(opcode >> 5);
    // Attach payload / orient word at bytes[0..1] (same role as 749's data[0]).
    const uint16_t attachPayload = data[0];
    bool expanded = false;

    if (MeshTraceEnabled())
        fprintf(stderr, "SPRITE748: model=%d dist=%d recurse=%d\n",
                modelId, s_meshParseDistance, s_spriteRecurseDepth);

    if (s_currentMesh && s_spriteRecurseDepth < kSpriteRecurseMax &&
        s_meshParseDistance >= 0 && !IsSuppressedStationChild(modelId)) {
        const ShipModel* child = GetShipModel(modelId);
        const uint8_t* childMesh =
            child ? GetShipModelPointer(child->meshDataOffset) : nullptr;
        const size_t childSize = MeshSizeForModel(modelId, child);
        if (MeshTraceEnabled())
            fprintf(stderr, "SPRITE748_expand: child=%p mesh=%p size=%zu\n",
                    (void*)child, (void*)childMesh, childSize);
        if (childMesh && childSize >= 4) {
            const int32_t savedDist = s_meshParseDistance;
            const int32_t savedScale = s_meshParseScaleRelated;
            int32_t childDist = jjffe::AttachVertexDepthForGate(vert);
            if (childDist < 1)
                childDist = s_meshParseDistance >> 4;
            if (childDist < 1)
                childDist = 1;
            const size_t polyBegin = s_currentMesh->polygons.size();
            const size_t barrierBegin = s_currentMesh->barriers.size();
            uint32_t savedAlu[64];
            std::memcpy(savedAlu, s_meshAluRegs, sizeof(savedAlu));
            const uint16_t savedPendMatOp = s_pendingMatrixOp;
            const uint16_t savedPendMatWord = s_pendingMatrixWord;
            const uint16_t savedWorkMatOp = s_workingMatrixOp;
            const uint16_t savedWorkMatWord = s_workingMatrixWord;
            const bool savedWorkLive = s_workingMatrixLive;
            int32_t savedPendLocal[9];
            int32_t savedWorkLocal[9];
            std::memcpy(savedPendLocal, s_pendingLocalBasis, sizeof(savedPendLocal));
            std::memcpy(savedWorkLocal, s_workingLocalBasis, sizeof(savedWorkLocal));
            const bool savedPendLocalValid = s_pendingLocalValid;
            const bool savedWorkLocalValid = s_workingLocalValid;
            const int savedVertBank = s_parseVertBank;
            const int8_t savedFlR = s_faceLightR;
            const int8_t savedFlG = s_faceLightG;
            const int8_t savedFlB = s_faceLightB;
            ApplySprite748ChildFaceLight(data);
            const bool titleNest = s_meshKeepAllSpriteChildren || s_meshParseGameObj;
            if (titleNest)
                childDist = savedDist;
            const bool savedForceBC = s_meshForceBitClearEnter;
            if (titleNest)
                s_meshForceBitClearEnter = true;
            s_meshParseDistance = childDist;
            s_meshParseScaleRelated = static_cast<int32_t>(child->scaleRelated);
            s_spriteStack[s_spriteRecurseDepth] = {modelId, vert, attachPayload};
            s_spriteRecurseDepth++;
            if (FaceGateOn())
                PushFacingParseContext(child, vert, attachPayload);
            // R1099: FUNC_001750 writes childCtx[0x54] = parentCtx[0x54] + 1 right
            // before recursing (ALU reg 0 doubles as a nesting-depth counter).
            // BitSet/BitClear gates in the child mesh (opnd0=0xC0, slot 0) key off
            // this value; without the +1 the child inherits the parent's slot 0
            // unchanged and such gates evaluate ENTER where the ASM evaluates SKIP.
            s_meshAluRegs[0] = savedAlu[0] + 1;
            const int savedModelId748 = s_meshParseModelId;
            s_meshParseModelId = modelId;
            RunMeshInterpreter(childMesh, childSize);
            s_meshParseModelId = savedModelId748;
            if (FaceGateOn())
                PopFacingParseContext();
            s_meshForceBitClearEnter = savedForceBC;
            std::memcpy(s_meshAluRegs, savedAlu, sizeof(s_meshAluRegs));
            s_pendingMatrixOp = savedPendMatOp;
            s_pendingMatrixWord = savedPendMatWord;
            s_workingMatrixOp = savedWorkMatOp;
            s_workingMatrixWord = savedWorkMatWord;
            s_workingMatrixLive = savedWorkLive;
            std::memcpy(s_pendingLocalBasis, savedPendLocal, sizeof(s_pendingLocalBasis));
            std::memcpy(s_workingLocalBasis, savedWorkLocal, sizeof(s_workingLocalBasis));
            s_pendingLocalValid = savedPendLocalValid;
            s_workingLocalValid = savedWorkLocalValid;
            s_parseVertBank = savedVertBank;
            s_faceLightR = savedFlR;
            s_faceLightG = savedFlG;
            s_faceLightB = savedFlB;
            const bool keepAll = s_meshKeepAllSpriteChildren ||
                                 (getenv("AB_SPRITE_CHILD_ALL") &&
                                  getenv("AB_SPRITE_CHILD_ALL")[0] == '1');
            const bool keepWrapperAll = CountParentHullPolys(polyBegin) <= 2;
            const bool keepFlatRgb =
                keepWrapperAll || s_spriteRecurseDepth >= 2;
            TagSpriteChildPolys(polyBegin, modelId, vert, attachPayload, keepAll,
                                keepWrapperAll, keepFlatRgb);
            s_spriteRecurseDepth--;
            s_meshParseDistance = savedDist;
            s_meshParseScaleRelated = savedScale;
            if (getenv("AB_DUMP_SPRITE_CHILD") &&
                getenv("AB_DUMP_SPRITE_CHILD")[0] == '1') {
                fprintf(stderr,
                        "SPRITE_748 model=%d mesh=%u vert=%u attach=%u op=0x%04x "
                        "payload=0x%04x polys=%zu scale=%u\n",
                        modelId, child->meshDataOffset, child->vertexDataOffset,
                        vert, static_cast<unsigned>(opcode),
                        static_cast<unsigned>(attachPayload),
                        s_currentMesh->polygons.size() - polyBegin,
                        child->scaleRelated);
            }
            {
                std::vector<MeshBarrier> kept;
                kept.reserve(s_currentMesh->barriers.size());
                for (size_t i = 0; i < barrierBegin; ++i)
                    kept.push_back(s_currentMesh->barriers[i]);
                for (size_t i = barrierBegin; i < s_currentMesh->barriers.size(); ++i) {
                    MeshBarrier b = s_currentMesh->barriers[i];
                    if (!keepAll && !keepWrapperAll &&
                        b.opcode != 0x43c6 && b.opcode != 0xe3c6)
                        continue;
                    if (b.spriteOp == 0) {
                        b.spriteOp = attachPayload ? attachPayload : 0xFFFFu;
                        b.attachVert = vert;
                        b.childModelId = static_cast<uint16_t>(modelId);
                    }
                    kept.push_back(b);
                }
                s_currentMesh->barriers.swap(kept);
            }
            expanded = s_currentMesh->polygons.size() > polyBegin;
        }
    }

    if (s_currentMesh && !expanded) {
        MeshPoly poly{};
        poly.type = MeshPolyType::Sprite;
        poly.vertexIdx[0] = vert;
        poly.vertexCount = 1;
        poly.texInfo = static_cast<uint16_t>(opcode);
        PushPoly(poly);
    }
    // 6-byte header then FUNC_001750 stream (+0, or +4 if bit15).
    if ((opcode & 0x8000) != 0)
        return data + 5; // 6 bytes + 4 index bytes = 5 words after opcode? 
    // 6 bytes = 3 words from `data`
    return data + 3;
}

static uint16_t* HandleMatrix(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // FUNC_001741: builds ctx+0x9c sub-basis (used by sprite 001740 case 6).
    // Advance matches ASM return of param_2 (payload cursor):
    //   C000 → param_2+2;  8000 → FUNC_001742 → param_2+1;  low → param_2+1.
    // Old bug: treated 8000 like C000 (+2) and skipped Shuttle tip-bevel
    // TexQuads after 0x833c (ended at a false END word).
    // Stamp opcode+word so Transform can rebuild 001739(op*2)+001743.
    s_pendingMatrixOp = static_cast<uint16_t>(opcode);
    s_pendingMatrixWord = 0;
    if ((opcode & 0xC000) == 0xC000) {
        // Two vert-index words — angle twist unused; word0 retained for digs.
        s_pendingMatrixWord = data[0];
        if (getenv("AB_DUMP_MATRIX") && getenv("AB_DUMP_MATRIX")[0]=='1') {
            fprintf(stderr, "MATRIX op=0x%04x form=C000 w0=0x%04x w1=0x%04x\n",
                    static_cast<unsigned>(opcode),
                    static_cast<unsigned>(data[0]),
                    static_cast<unsigned>(data[1]));
        }
        return data + 2;
    }
    s_pendingMatrixWord = data[0];
    // 0x8000 family: FUNC_001742 → 001743/001744 twist in place on +0x9c.
    // When NormAxes 0x8019 just installed the Q31 sentinel, twist that local
    // basis now (parse-time snapshot; live ALU angle resolution is dig #3).
    if ((opcode & 0xC000) == 0x8000 && s_pendingLocalValid) {
        const int axis = static_cast<int>((opcode >> 5) & 3u);
        const uint32_t ang = DecodeLocalMatrixAngle(s_pendingMatrixWord);
        const int32_t sinA = Sin32(ang);
        const int32_t cosA = Sin32(ang + 0x4000u);
        RotateLocalBasisColumns(s_pendingLocalBasis, axis, cosA, sinA);
        if (getenv("AB_DUMP_MATRIX") && getenv("AB_DUMP_MATRIX")[0] == '1') {
            fprintf(stderr,
                    "MATRIX op=0x%04x form=8000 w0=0x%04x twist axis=%d ang=0x%x "
                    "local0=0x%08x\n",
                    static_cast<unsigned>(opcode),
                    static_cast<unsigned>(s_pendingMatrixWord), axis,
                    static_cast<unsigned>(ang),
                    static_cast<unsigned>(s_pendingLocalBasis[0]));
        }
    } else if (getenv("AB_DUMP_MATRIX") && getenv("AB_DUMP_MATRIX")[0]=='1') {
        fprintf(stderr, "MATRIX op=0x%04x form=%s w0=0x%04x\n",
                static_cast<unsigned>(opcode),
                ((opcode & 0xC000) == 0x8000) ? "8000" : "low",
                static_cast<unsigned>(data[0]));
    }
    return data + 1;
}

static uint16_t* HandleTextPrimitive(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001747: returns param_2 + 8 bytes (four payload words after opcode).
    return data + 4;
}

// FUNC_001737 spline: always advances param_2 + 8 bytes (culled or drawn).
static uint16_t* HandleSpline737(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    return data + 4;
}

// FUNC_001738: emit a thruster / nozzle billboard (two tip verts + size + tex).
static void EmitThruster(uint16_t texInfo, uint8_t v1, uint8_t v2, uint16_t size)
{
    if (!s_currentMesh) return;
    for (const auto& existing : s_currentMesh->polygons) {
        if (existing.type == MeshPolyType::Thruster &&
            existing.texInfo == texInfo &&
            existing.vertexIdx[0] == v1 &&
            existing.vertexIdx[1] == v2 &&
            existing.specialParam == size) {
            return; // already harvested / emitted
        }
    }
    MeshPoly poly{};
    poly.type = MeshPolyType::Thruster;
    poly.vertexIdx[0] = v1;
    poly.vertexIdx[1] = v2;
    poly.vertexIdx[2] = 0;
    poly.vertexIdx[3] = 0;
    poly.vertexCount = 2;
    poly.normalIdx = 0;
    poly.texInfo = texInfo;
    poly.color = 0;
    poly.afterSpecialPoly = false;
    poly.specialParam = size; // FUNC_001738 size word (depth-scaled at draw time)
    poly.streamIndex = s_streamIndex++;
    for (int i = 0; i < 8; i++)
        poly.aluRegs[i] = static_cast<uint16_t>(s_meshAluRegs[i] & 0xffffu);
    s_currentMesh->polygons.push_back(poly);
}

static uint16_t* HandleThruster(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // Layout after opcode: word tex, byte v1, byte v2, word size  (3 words total)
    // Only the plain 0x0009 form is FUNC_001738 thruster data (Saker nozzles).
    // Other *09 opcodes share the low 5 bits but different payloads — skip emit.
    if ((opcode & 0xFFE0) == 0) {
        uint16_t tex = data[0];
        uint8_t v1 = reinterpret_cast<const uint8_t*>(data)[2];
        uint8_t v2 = reinterpret_cast<const uint8_t*>(data)[3];
        uint16_t size = data[2];
        EmitThruster(tex, v1, v2, size);
    }
    return data + 3;
}

// Thruster ops often sit in bit-clear-gated stubs skipped on the pristine parse
// path. Scan the raw stream for opcode 0x0009 and append any not already emitted.
static void HarvestThrustersInRange(const uint8_t* start, const uint8_t* end)
{
    if (!start || end < start + 8) return;
    for (const uint8_t* p = start; p + 8 <= end; p += 2) {
        uint16_t op = static_cast<uint16_t>(p[0] | (p[1] << 8));
        if (op != 0x0009) continue;
        uint16_t tex = static_cast<uint16_t>(p[2] | (p[3] << 8));
        uint8_t v1 = p[4];
        uint8_t v2 = p[5];
        uint16_t size = static_cast<uint16_t>(p[6] | (p[7] << 8));
        EmitThruster(tex, v1, v2, size);
    }
}

// FUNC_001721 complex polygon (opcode 0x05). Header:
//   +0 word  texInfo/colour descriptor (FUNC_001692)
//   +2 byte  material byte: low7 = fill normal index, 0x80 = mirror/cull flag
//   +3 byte  sub-op stream byte length (cull-path advance)
//   +4 ...   DATA_007817 sub-op stream; each op = word, index = (int16)word>>9.
//
// The sub-op walker builds a contour of hull vertices then fills it. We port the
// contour geometry (enough for a faithful silhouette) and fan-tessellate each
// closed loop into existing TexTri / FlatQuad polys, reusing the standard
// transform + cull + DrawFlatPolygon path. Clip/resync (FUNC_001725/726/564)
// and lit-glint faces (FUNC_001736) emit two Contour digon stubs (expanded to
// billboard screen rects at transform). Clip/resync (FUNC_001725/726/564)
// still approximated.
// clipping falls back to chord endpoints.
//
// Sub-op slots (index = word>>9), DATA_007817 → handlers. Payload after the
// 2-byte word:
//   0 FUNC_001722 terminator (+0)
//   1 FUNC_001734→001735→001720 immediate quad (+4):
//        verts (payload[1], payload[0], payload[3], payload[2]); cursor:=payload[2]
//   2 FUNC_001724 edge/start (+2): begin contour corners (word&0xff), payload[0]
//   3 FUNC_001730 continue (+0): append corner (word&0xff)
//   4 FUNC_001733 (+2): when edge cursor set (all-visible bake), FUNC_001735
//        immediate quad (cursor, lo, payload[1], payload[0]); cursor:=payload[0].
//        Clip-plane path (FUNC_001725/564) still approximated by chord verts.
//   5 FUNC_001723 flush/close (+0)
//   6 FUNC_001736 glint (+2) — two digon Contour stubs (anchor=lo)
static int s_nextContourId = 1;

static uint16_t* HandleComplex721(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    uint16_t texInfo = data[0];
    uint16_t flags = data[1];
    uint8_t normalIdx = static_cast<uint8_t>(flags & 0x7F);
    uint8_t skipBytes = static_cast<uint8_t>((flags >> 8) & 0xFF);
    // R556 candidate law (gated AB_CFC_SHADEIDX): ASM FUNC_001761 passes the
    // record's shade-index byte (param_4&0x7e)>>1 into FUNC_001692's rgb slot.
    // #23's lens record head word is bare 0x2000 (rgb nibbles zero) but its
    // payload quad words carry lo bytes 6 and 3; ASM paints VGA22 there, i.e.
    // material-row shade at index lo. Store the first slot-1 payload lo byte on
    // emitted polys so the renderer can resolve it when the raw word maps to 0.
    const bool stashShade = ::getenv("AB_CFC_SHADEIDX") &&
                            ::getenv("AB_CFC_SHADEIDX")[0] == '1';
    uint8_t* sub = reinterpret_cast<uint8_t*>(data + 2);
    uint8_t* end = sub + skipBytes;

    const bool dump = getenv("AB_DUMP_COMPLEX721") &&
                      getenv("AB_DUMP_COMPLEX721")[0] == '1';
    if (dump) {
        fprintf(stderr, "C721HDR tex=0x%04x flags=0x%04x norm=%u skip=%u\n",
                texInfo, flags, normalIdx, skipBytes);
    }

    // One Contour → one span-buffer fill (FUNC_001794…001799).
    // Dig: AB_C721_SPLIT3=1 closes+splits Contour at slot3 cursor jumps so
    // left/right wing lobes fill separately (Puma wing-gap probe).
    const bool split3 = getenv("AB_C721_SPLIT3") &&
                        getenv("AB_C721_SPLIT3")[0] == '1';
    int16_t contourId = split3 ? 0 : static_cast<int16_t>(s_nextContourId++);

    uint8_t corners[64];
    int nCorners = 0;
    // FUNC_001721 ctx+0x114 edge cursor. Valid after an immediate quad; slot 4
    // with a valid cursor emits another 001735 quad (not a fan corner).
    bool haveCursor = false;
    uint8_t edgeCursor = 0;
    uint8_t curPayLo = 0xFF;  // R556: last payload low byte seen (shade index)
    uint8_t edgeOnlyColor = 0xFF;  // R557: stash for vc==2 edge pieces of same group
    uint16_t lensWordSeen = 0xFFFF;  // R562: last quad payload lo seen in this group
    uint16_t headWordObserved = 0xFFFF;  // R566: first payload word of the group
    // FUNC_001734 seeds ctx+0x110 Contour start; 001721/001723 closes to it.
    bool haveStart = false;
    uint8_t contourStart = 0;
    bool lobeHasQuad = false;

    auto emitQuad = [&](uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3) {
        if (!s_currentMesh) return;
        MeshPoly poly{};
        poly.type = MeshPolyType::ComplexFill;
        poly.vertexIdx[0] = v0;
        poly.vertexIdx[1] = v1;
        poly.vertexIdx[2] = v2;
        poly.vertexIdx[3] = v3;
        poly.vertexCount = 4;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = 0;
        if (stashShade && curPayLo != 0xFF) {
            poly.color = curPayLo;   // R556: record shade-index byte carrier
            lensWordSeen = curPayLo;
            if (::getenv("AB_STASH_VIS")) fprintf(stderr, "STASHV cid=%d lo=%u\n", contourId, (unsigned)curPayLo);
            // R566 candidate law (gated AB_C721_HEADWORD): ASM FUNC_001760 passes
            // the CONTOUR RECORD's own first word as FUNC_001761's color param
            // (#23 lens observed 0x0222 vs parent face word 0x2000). Carry that
            // record word on every piece of the group instead of the mesh header.
            if (::getenv("AB_C721_HEADWORD") && ::getenv("AB_C721_HEADWORD")[0] == '1' &&
                headWordObserved != 0xFFFF)
                poly.texInfo = headWordObserved;
            if (getenv("AB_SHADE_STASH_DBG")) fprintf(stderr, "STASH cid=%d lo=%u\n", contourId, (unsigned)curPayLo);
        } else if (stashShade && getenv("AB_SHADE_STASH_DBG")) {
            fprintf(stderr, "NOSTASH cid=%d\n", contourId);
        }
        poly.contourId = contourId;
        PushPoly(poly);
        lobeHasQuad = true;
        if (dump) {
            fprintf(stderr, "  QUAD %u %u %u %u (cursor→%u) cid=%d\n",
                    v0, v1, v2, v3, v3, contourId);
        }
    };

    auto emitEdge = [&](uint8_t v0, uint8_t v1) {
        if (!s_currentMesh) return;
        MeshPoly poly{};
        poly.type = MeshPolyType::ComplexFill;
        poly.vertexIdx[0] = v0;
        poly.vertexIdx[1] = v1;
        poly.vertexIdx[2] = 0;
        poly.vertexIdx[3] = 0;
        poly.vertexCount = 2;
        poly.texInfo = texInfo;
        poly.normalIdx = normalIdx;
        poly.color = 0;
        if (stashShade && curPayLo != 0xFF)
            poly.color = curPayLo;   // R557: edges carry the group's payload byte too
        else if (stashShade && lensWordSeen != 0xFFFF)
            poly.color = static_cast<uint8_t>(lensWordSeen);  // R565: inherit from group quads
        if (::getenv("AB_C721_HEADWORD") && ::getenv("AB_C721_HEADWORD")[0] == '1' &&
            stashShade && headWordObserved != 0xFFFF)
            poly.texInfo = headWordObserved;
        poly.contourId = contourId;
        PushPoly(poly);
        if (dump) {
            fprintf(stderr, "  EDGE %u %u cid=%d\n", v0, v1, contourId);
        }
    };

    auto emitClose = [&]() {
        if (!split3 || !haveCursor || !haveStart || !lobeHasQuad) return;
        if (edgeCursor == contourStart) return;
        emitEdge(edgeCursor, contourStart);
        if (dump)
            fprintf(stderr, "  CLOSE %u→%u cid=%d\n", edgeCursor, contourStart, contourId);
    };

    auto beginLobe = [&](uint8_t startVert) {
        contourId = static_cast<int16_t>(s_nextContourId++);
        contourStart = startVert;
        haveStart = true;
        lobeHasQuad = false;
    };

    auto flushContour = [&]() {
        // Dig: AB_C721_NO_FAN=1 skips contour fan-fill (slot1/4 quads only).
        const bool noFan = getenv("AB_C721_NO_FAN") && getenv("AB_C721_NO_FAN")[0]=='1';
        if (!noFan && s_currentMesh && nCorners >= 3) {
            if (dump) {
                fprintf(stderr, "  FLUSH n=%d hub=%u norm=%u cid=%d corners:",
                        nCorners, corners[0], normalIdx, contourId);
                for (int ci=0; ci<nCorners; ci++) fprintf(stderr, " %u", corners[ci]);
                fprintf(stderr, "\n");
            }
            auto pushPiece = [&](uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3,
                                 int vc) {
                MeshPoly poly{};
                poly.type = MeshPolyType::ComplexFill;
                poly.vertexIdx[0] = v0;
                poly.vertexIdx[1] = v1;
                poly.vertexIdx[2] = v2;
                poly.vertexIdx[3] = v3;
                poly.vertexCount = static_cast<uint8_t>(vc);
                poly.texInfo = texInfo;
                poly.normalIdx = normalIdx;
                poly.color = 0;
                poly.contourId = contourId;
                PushPoly(poly);
            };
            // Closed contour → MeshPolys: 3→tri, 4→one quad, N>4→fan tris.
            if (nCorners == 3) {
                pushPiece(corners[0], corners[1], corners[2], 0, 3);
            } else if (nCorners == 4) {
                pushPiece(corners[0], corners[1], corners[2], corners[3], 4);
            } else {
                for (int i = 1; i + 1 < nCorners; i++)
                    pushPiece(corners[0], corners[i], corners[i + 1], 0, 3);
            }
        }
        nCorners = 0;
    };

    auto addCorner = [&](uint8_t v) {
        if (nCorners < static_cast<int>(sizeof(corners)))
            corners[nCorners++] = v;
    };

    uint8_t* p = sub;
    // Bound the walk by the header length; every op consumes >= 2 bytes.
    while (p + 2 <= end) {
        uint16_t w = static_cast<uint16_t>(p[0] | (p[1] << 8));
        int slot = static_cast<int16_t>(w) >> 9;
        uint8_t lo = static_cast<uint8_t>(w & 0xFF);
        uint8_t* payload = p + 2;
        if (::getenv("AB_C721_HEADWORD") && ::getenv("AB_C721_HEADWORD")[0] == '1' &&
            slot >= 1 && headWordObserved == 0xFFFF)
            headWordObserved = static_cast<uint16_t>(texInfo | lo);  // R566: ASM 001760 color param = header | payload-lo (lens observed 0x0222)

        if (dump) {
            fprintf(stderr, "  C721 off=%ld w=0x%04x slot=%d lo=%u",
                    static_cast<long>(p - sub), w, slot, lo);
            const int nPay = (slot == 1) ? 4
                : (slot == 2 || slot == 4 || slot == 6) ? 2 : 0;
            if (nPay > 0 && payload + nPay <= end) {
                fprintf(stderr, " pay");
                for (int i = 0; i < nPay; i++)
                    fprintf(stderr, " %u", payload[i]);
            }
            fprintf(stderr, "\n");
        }

        if (slot <= 0 || slot > 6) {
            // Terminator (slot 0) or out-of-range word: stop.
            break;
        }

        switch (slot) {
            case 1: { // FUNC_001734 → 001735 → 001720 immediate quad
                flushContour();
                if (payload + 4 <= end) {
                    curPayLo = lo;   // R556: quad word's low byte = shade index
                    // ASM: 001734 seeds ctx+0x110 = vert(p1); cursor := vert(p2).
                    if (split3)
                        beginLobe(payload[1]);
                    else {
                        contourStart = payload[1];
                        haveStart = true;
                        lobeHasQuad = false;
                    }
                    emitQuad(payload[1], payload[0], payload[3], payload[2]);
                    edgeCursor = payload[2];
                    haveCursor = true;
                }
                p += 2 + 4;
                break;
            }
            case 2: { // edge / contour start — clears immediate-quad cursor
                flushContour();
                emitClose();
                haveCursor = false;
                haveStart = false;
                lobeHasQuad = false;
                if (split3 && contourId == 0)
                    contourId = static_cast<int16_t>(s_nextContourId++);
                addCorner(lo);
                if (payload < end)
                    addCorner(payload[0]);
                p += 2 + 2;
                break;
            }
            case 3: // FUNC_001730 → 001731 continue
                if (haveCursor) {
                    // After an immediate quad, 001731 updates ctx+0x114 and
                    // emits a span edge (001718). Puma jumps left→right tip;
                    // AB_C721_SPLIT3 closes the lobe and starts a new Contour.
                    if (split3) {
                        emitClose();
                        beginLobe(lo);
                        edgeCursor = lo;
                        haveCursor = true;
                        if (dump)
                            fprintf(stderr, "  SPLIT3 start=%u cid=%d\n", lo, contourId);
                    } else {
                        emitEdge(edgeCursor, lo);
                        edgeCursor = lo;
                    }
                } else {
                    addCorner(lo);
                }
                p += 2;
                break;
            case 4: { // FUNC_001733 → 001735 quad when cursor visible
                if (haveCursor && payload + 2 <= end) {
                    // Do not accumulate slot4 into the contour fan — that made
                    // Puma mega-tris 46-13-45-43 across wing gaps.
                    flushContour();
                    // 001720(cursor, vert(lo), vert(payload[1]), vert(payload[0]))
                    emitQuad(edgeCursor, lo, payload[1], payload[0]);
                    edgeCursor = payload[0];
                    haveCursor = true;
                } else if (nCorners >= 2 && payload + 2 <= end &&
                           !(getenv("AB_C721_SLOT4_CORNER") &&
                             getenv("AB_C721_SLOT4_CORNER")[0] == '1')) {
                    // Slot2 Contour start then slot4: ASM promotes the two
                    // fan corners into edge-cursor mode and emits 001735 quads
                    // (Courier body digon Contours 0x0666). The old fallback
                    // only addCorner(lo) dropped tip vert payload[0] (v22/v23
                    // → screen (94,71)/(226,71)) so Contour fill stopped short.
                    // Dig: AB_C721_SLOT4_CORNER=1 restores corner-only path.
                    const uint8_t c0 = corners[0];
                    const uint8_t c1 = corners[1];
                    nCorners = 0;
                    contourStart = c0;
                    haveStart = true;
                    edgeCursor = c1;
                    haveCursor = true;
                    emitQuad(edgeCursor, lo, payload[1], payload[0]);
                    edgeCursor = payload[0];
                    lobeHasQuad = true;
                    if (dump) {
                        fprintf(stderr,
                                "  SLOT4 promo start=%u cursor=%u→%u cid=%d\n",
                                c0, c1, edgeCursor, contourId);
                    }
                } else {
                    // No cursor: clip-fallback approx — chord endpoint only.
                    addCorner(lo);
                }
                p += 2 + 2;
                break;
            }
            case 5: // flush / close contour
                flushContour();
                emitClose();
                haveCursor = false;
                haveStart = false;
                lobeHasQuad = false;
                p += 2;
                break;
            case 6: { // FUNC_001736 lit glint → two Contour digon quads (001720×2)
                // Tiger #55 Contour 0x20ee: slot6 was skipped as "glint" and the
                // waist mat41 overdraw never got VGA0 digon punches. Payload is
                // (lightIdx, scaleByte); lo = anchor vertex. Emit two stubs;
                // transform expands to billboard screen rectangles.
                if (payload + 2 <= end) {
                    auto emitGlintHalf = [&](uint8_t half) {
                        MeshPoly poly{};
                        poly.type = MeshPolyType::ComplexFill;
                        poly.vertexIdx[0] = lo;           // anchor
                        poly.vertexIdx[1] = payload[1];   // scale byte
                        poly.vertexIdx[2] = payload[0];   // light table idx
                        poly.vertexIdx[3] = half;         // 0=upper 1=lower
                        poly.vertexCount = 4;
                        poly.texInfo = texInfo;
                        poly.normalIdx = normalIdx;
                        poly.color = 0;
                        poly.contourId = contourId;
                        // 0x7360 marker (bit15 clear — not a sprite child).
                        poly.specialParam = static_cast<uint16_t>(0x7360 | (half & 1));
                        PushPoly(poly);
                        if (dump) {
                            fprintf(stderr,
                                    "  GLINT half=%u anchor=%u scale=%u light=%u cid=%d\n",
                                    half, lo, payload[1], payload[0], contourId);
                        }
                    };
                    emitGlintHalf(0);
                    emitGlintHalf(1);
                }
                p += 2 + 2;
                break;
            }
            default:
                p += 2;
                break;
        }
    }

    // Close any open contour at stream end (ASM 001723 / terminator).
    flushContour();
    emitClose();

    return reinterpret_cast<uint16_t*>(sub + skipBytes);
}

static uint16_t* HandleComplex762(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001762: dw param (=texInfo-style word), then +2 or +9 words.
    // Flat RGB (bits15:14==0) → extract nibbles into renderCtx+0xf4 face light
    // when flag byte bit7 clear.
    // Shuttle uses 0x2444 (0x2000 | RGB444) → +4,+4,+4. Cobra also has a late
    // 0x0444 flat word; applying those nibbles as face light breaks dump-ab
    // (Cobra top idx +4 ladder → 0% with light forced off). Only treat forms
    // with the 0x2000 flag bit as face-light sources (matches Shuttle).
    uint8_t* b = reinterpret_cast<uint8_t*>(data);
    const uint16_t word0 = data[0];
    const uint8_t flagByte = b[1]; // *(byte*)(param_2+1) in ASM
    if (MeshTraceEnabled())
        fprintf(stderr, "OP1A: word0=0x%04x flagByte=0x%02x b3=0x%02x\n",
                word0, flagByte, b[3]);
    if ((flagByte & 0x80) == 0 && (word0 >> 14) == 0 && (word0 & 0x2000) != 0) {
        s_faceLightR = static_cast<int8_t>((word0 >> 8) & 0xF);
        s_faceLightG = static_cast<int8_t>((word0 >> 4) & 0xF);
        s_faceLightB = static_cast<int8_t>(word0 & 0xF);
        if (s_currentMesh) {
            s_currentMesh->faceLightR = s_faceLightR;
            s_currentMesh->faceLightG = s_faceLightG;
            s_currentMesh->faceLightB = s_faceLightB;
        }
    }
    if (b[3] == 0) {
        return data + 2;
    }
    return data + 9;
}

// FUNC_001771 — optional FUNC_001770 when opcode bit15 set; no stream payload.
// Title planetoid parents (#346…) emit 0x8019 before Sprite749; treating this
// as Unknown (data+1) ate the sprite opcode and zeroed the constellation.
// #334 / #327: 0x8019 immediately before Matrix 0x835c — installs Q31 sentinel
// into ctx+0x9c so the twist input is identity, not parent/sprite Remap.
static uint16_t* HandleNormAxes771(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    if (static_cast<int16_t>(opcode) < 0) {
        // FUNC_001770: diagonal 0x7fffffff into +0x9c/+0xac/+0xbc.
        InstallBasisSentinel(s_pendingLocalBasis);
        s_pendingLocalValid = true;
        if (getenv("AB_DUMP_MATRIX") && getenv("AB_DUMP_MATRIX")[0] == '1') {
            fprintf(stderr, "NORMAXES 0x%04x → sentinel Q31 local basis\n",
                    static_cast<unsigned>(opcode));
        }
    } else {
        // Positive path: axis from ctx+0x38/3c/40 + 001744 — not yet ported.
        // Dig ships use the bit15 reset form only.
        (void)opcode;
    }
    return data;
}

// Skip-only handlers with ASM-verified record sizes. Handler receives the
// pointer PAST the opcode word (dispatch passes data+1), matching ASM ebx;
// ASM `lea eax,[ebx+N]` == `return data + N/2`.
static uint16_t* HandlePointSource01(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001698: PointSource nav light.
    // Record: data[0]=material(word), data[1]=unused(word),
    // data[2] byte0=flags(bit7=blink), byte1=vertex index.
    // Total: 6 bytes (3 words).
    //
    // ASM FUNC_001699 looks up the vertex from the vertex cache
    // (DATA_009200). The vertex cache is now populated during
    // TransformShipVertices, so NavLight can look up from it.
    uint16_t texInfo = data[0];
    uint8_t flags = static_cast<uint8_t>(data[2] & 0xFF);
    uint8_t vertIdx = static_cast<uint8_t>((data[2] >> 8) & 0xFF);
    bool blink = (flags & 0x80) != 0;

    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::NavLight;
        poly.vertexIdx[0] = vertIdx; // vertex index for cache lookup
        poly.vertexCount = 1;
        poly.texInfo = texInfo;
        poly.normalIdx = 0;
        poly.color = 0;
        poly.contourId = -1;
        poly.streamIndex = s_streamIndex++;
        poly.afterSpecialPoly = s_pendingSpecialPoly;
        poly.specialParam = blink ? 0x80 : 0;
        s_pendingSpecialPoly = false;
        s_pendingSpecialParam = 0;
        memset(poly.aluRegs, 0, sizeof(poly.aluRegs));
        poly.glowVertCount = 0;
        poly.glowVertOffset = 0;
        PushPoly(poly);
    }
    return data + 3;
}

static uint16_t* SkipOp0F(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    return data;      // FUNC_001759: returns [ebp+0xc] unchanged — no payload
}

static uint16_t* SkipOp10(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001815: 10-byte fixed header (reads up to word [ebx+0x8], then
    // add ebx,0xa), then FUNC_001823 returns arg+2. Total payload 12 bytes.
    // Census (R1235): 1 occurrence / model #72 only — outside fleet 15-71; the
    // station-child suppress band (72..80) covers the TITLE phase only, so the
    // removal rests on ZERO DISPATCH in measured fleet/live probes, not on
    // unconditional unreachability (checker R1235).
    StubDispatchLog(0x10, opcode >> 5, "SkipOp10/001815", ctx);
    return data + 6;
}

static uint16_t* HandleOp12(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    // FUNC_001763 (ffe.asm:110798): push (opcode>>5)&0xFF as vertex slot,
    // *data as string code, 0 → FUNC_001764 → job FUNC_001812 (text at
    // projected xy). Payload: 1 word; return data+1.
    // Deferred ship walker: record TextPrimitive for transform; draw of the
    // FUNC_001812 string expansion remains latent (no string expander on this
    // path). Fleet #66 carries the only in-gate 0x12 and measures 0 px with
    // or without the emit (text not visible at gated poses).
    StubDispatchLog(0x12, (opcode >> 5) & 0xffu, "HandleOp12/001763", ctx);
    if (s_currentMesh) {
        MeshPoly poly{};
        poly.type = MeshPolyType::TextPrimitive;
        poly.vertexIdx[0] = static_cast<uint8_t>((opcode >> 5) & 0xFFu);
        poly.vertexCount = 1;
        poly.texInfo = data[0]; // string code word (ASM 001764 param_3)
        poly.normalIdx = 0;
        poly.color = 0;
        poly.contourId = -1;
        poly.streamIndex = s_streamIndex++;
        poly.afterSpecialPoly = s_pendingSpecialPoly;
        poly.specialParam = 0;
        s_pendingSpecialPoly = false;
        s_pendingSpecialParam = 0;
        memset(poly.aluRegs, 0, sizeof(poly.aluRegs));
        poly.glowVertCount = 0;
        poly.glowVertOffset = 0;
        PushPoly(poly);
    }
    return data + 1;
}

uint16_t* TestHandleOp12Advance(uint16_t* data, uint32_t opcode,
                                uint8_t* outVertSlot, uint16_t* outStringCode)
{
    if (outVertSlot)
        *outVertSlot = static_cast<uint8_t>((opcode >> 5) & 0xFFu);
    if (outStringCode)
        *outStringCode = data ? data[0] : 0;
    // Same advance as HandleOp12 / ASM FUNC_001763 (payload 1 word).
    return data + 1;
}

static uint16_t* HandleOp18GlowPoints(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001766: glow/flare point-list primitive.
    // Record: data[0]=texInfo, data[1]=first vertex (word),
    // then byte pairs terminated by 0x7F in byte[1].

    uint16_t texInfo = data[0];
    uint8_t firstVert = static_cast<uint8_t>(data[1] & 0xFF);

    // Walk byte pairs to find count and advance past terminator
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data + 2);
    int nPairs = 0;
    while (static_cast<int8_t>(p[1]) != 0x7F) {
        p += 2;
        nPairs++;
        if (nPairs > 200) break;
    }
    p += 2; // skip terminator pair

    // Emit GlowPoints poly
    if (s_currentMesh && nPairs > 0) {
        MeshPoly poly{};
        poly.type = MeshPolyType::GlowPoints;
        poly.vertexIdx[0] = firstVert;
        poly.vertexCount = 1; // first vertex only; byte pairs stored separately
        poly.texInfo = texInfo;
        poly.normalIdx = 0;
        poly.color = 0;
        poly.contourId = -1;
        poly.streamIndex = s_streamIndex++;
        poly.afterSpecialPoly = s_pendingSpecialPoly;
        poly.specialParam = s_pendingSpecialParam ? s_pendingSpecialParam : 0;
        s_pendingSpecialPoly = false;
        s_pendingSpecialParam = 0;
        memset(poly.aluRegs, 0, sizeof(poly.aluRegs));

        // Store byte-pair vertex indices in glow store
        poly.glowVertCount = static_cast<uint16_t>(nPairs * 2);
        poly.glowVertOffset = static_cast<uint16_t>(s_glowVertCount);
        const uint8_t* pairStart = reinterpret_cast<const uint8_t*>(data + 2);
        for (int i = 0; i < nPairs * 2 && s_glowVertCount < kMaxGlowVerts; i++) {
            s_glowVertStore[s_glowVertCount++] = pairStart[i];
        }
        PushPoly(poly);
    }

    return reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(p));
}

static uint16_t* HandleOp1E(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    // FUNC_001767 (ffe.asm:111245): zero payload — both branches return data
    // unchanged. Negative → FUNC_001768; positive → bank advance via 001473.
    const int16_t op16 = static_cast<int16_t>(opcode);
    if (op16 < 0) {
        // FUNC_001768: arg = (int16)opcode >> 4 (arithmetic).
        const int arg = op16 >> 4;
        if (static_cast<uint16_t>(arg) == 0xf801u) {
            // FUNC_001769 (ffe.asm:111324): title/object nav-table point emit
            // via FUNC_001703/001699/001764. Requires live render ctx (+0x14c
            // object) and DATA_009200 projections — planet MeshOp767 already
            // calls the ported FUNC_001769 body. Deferred ship walker has
            // neither; census (R1235): sole carrier is model #171 (outside
            // fleet 15-71 / live matrix). No MeshData mutation — same visual
            // effect as the prior logged stub, without the runtime warning.
            StubDispatchLog(0x1e, 0xf801u, "HandleOp1E/001769-by-design", ctx);
        } else {
            // memcpy(ctx+0x14, ctx+0x9c, 9*4) — install Matrix-built basis as
            // working matrix. Pattern on #161/#326: Matrix 0x835c then 0xc01e.
            s_workingMatrixOp = s_pendingMatrixOp;
            s_workingMatrixWord = s_pendingMatrixWord;
            s_workingMatrixLive = (s_workingMatrixOp != 0);
            if (s_pendingLocalValid) {
                std::memcpy(s_workingLocalBasis, s_pendingLocalBasis,
                            sizeof(s_workingLocalBasis));
                s_workingLocalValid = true;
            }
            if (getenv("AB_DUMP_OPS") && getenv("AB_DUMP_OPS")[0] == '1') {
                fprintf(stderr,
                        "  OP1E commit working matrix op=0x%04x word=0x%04x "
                        "local=%d local0=0x%08x\n",
                        static_cast<unsigned>(s_workingMatrixOp),
                        static_cast<unsigned>(s_workingMatrixWord),
                        (int)s_workingLocalValid,
                        s_workingLocalValid
                            ? (unsigned)s_workingLocalBasis[0]
                            : 0u);
            }
        }
    } else {
        // Positive: bank advance — source +count verts, cache +count*2 entries.
        // Parse-deferred model: rewrite bytecode indices to absolute in PushPoly
        // (idx + bank*2). Do NOT also AdvanceVertexBank here — transform resets
        // cache bank to 0 and looks up absolute indices (one offset, not two).
        const uint8_t enc = static_cast<uint8_t>((opcode >> 5) & 0xFFu);
        const int count = static_cast<int>(MeshEval473(enc));
        s_parseVertBank += count;
        if (getenv("AB_DUMP_OPS") && getenv("AB_DUMP_OPS")[0] == '1') {
            fprintf(stderr, "  OP1E bank +%d verts (parseBank=%d)\n", count,
                    s_parseVertBank);
        }
    }
    return data;
}

static uint16_t* SkipOp1F(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx; (void)opcode;
    // FUNC_001874: fixed 12-byte record — next ptr (ebx+0xc) is passed as
    // arg5 through FUNC_001876, which returns it unchanged ([ebp+0x18]).
    // Ship satellite chain deferred (D1 / R1235 P1 brief); planet path real.
    StubDispatchLog(0x1f, opcode >> 5, "SkipOp1F/001874", ctx);
    return data + 6;
}

static uint16_t* HandleOp17Gate(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    // Deferred-lane twin of FUNC_001765 (ffe.asm:110922).
    // Record: data[0] = signed threshold (bit15 invert), data[1] = vert0|vert1<<8;
    // return data+2. ASM / walk-lane port (core.cpp FUNC_001765 via MeshOp765):
    // abs(dx)+abs(dy) on DATA_009200 screen xy; setnc vs (thr&0x7fff); invert
    // when thr<0; FUNC_001753 skip. Fleet 15–71 draws that walk path (R1115).
    //
    // This handler runs only inside ParseMeshBytecode (export/classic/title),
    // before any DATA_009200 fill — so the predicate here is IsFar(thr) with
    // ASM-correct thr/invert from data[0]. See scratch/r1236/MECHANISM_001765.md.
    const int16_t thrWord = static_cast<int16_t>(data[0]);
    const int16_t threshold = static_cast<int16_t>(thrWord & 0x7FFF);
    const bool invert = thrWord < 0;
    uint16_t* after = data + 2;

    StubDispatchLog(0x17, static_cast<uint32_t>(static_cast<uint16_t>(thrWord)),
                    "HandleOp17Gate/001765", ctx);

    if (s_meshParseDistance < 0)
        return after;

    bool far = IsFar(threshold);
    bool skip = invert ? !far : far;
    uint16_t* next = ApplySkip(after, opcode, skip);
    if (s_walkStatsActive) {
        s_walkStats.gateFarTotal++;
        if (skip) {
            s_walkStats.gateFarSkip++;
            s_walkStats.gateSkipWords += static_cast<int16_t>(opcode) >> 5;
        }
    }
    return next ? next : after;
}

static uint16_t* HandleUnknown(uint32_t ctx, uint16_t* data, uint32_t opcode)
{
    (void)ctx;
    // An unknown op with a guessed advance desynchronizes the whole walk and
    // corrupts every opcode after it (see 2026-07-28 Eagle Mk3 op 0x01 bug).
    // Never guess silently.
    fprintf(stderr,
            "ParseMeshBytecode: UNKNOWN op 0x%02x (word 0x%04x) — advance "
            "unverified, walk integrity not guaranteed\n",
            (unsigned)(opcode & 0x1F), (unsigned)opcode);
    return data + 1;
}

MeshData ParseMeshBytecode(const uint8_t* meshData, size_t meshSize)
{
    MeshData result = {};
    result.valid = false;

    if (!meshData || meshSize < 4) {
        printf("ParseMeshBytecode: Invalid mesh data\n");
        return result;
    }

    InitMeshOpcodeHandlers();

    ResetMeshAluRegs();
    s_currentMesh = &result;
    s_pendingSpecialPoly = false;
    s_pendingSpecialParam = 0;
    s_streamIndex = 0;
    s_nextContourId = 1;
    s_glowVertCount = 0;
    s_spriteRecurseDepth = 0;
    ClearMeshFaceLight();
    // Per-ship renderCtx substrate — must not leak Matrix/0x1E state across models.
    s_pendingMatrixOp = 0;
    s_pendingMatrixWord = 0;
    s_workingMatrixOp = 0;
    s_workingMatrixWord = 0;
    s_workingMatrixLive = false;
    ClearLocalBasisState();
    s_parseVertBank = 0;
    SetVertexBankVerts(0);
    SetVertexCacheBankBase(0);
    result.faceLightR = result.faceLightG = result.faceLightB = 0;

    // Engine-on: install GameObj thrust words so BitClear/ALU gates enter the
    // primary FUNC_001738 banks (ffe.asm op 0x09). Matches ffeasm ship-demo
    // with +0xb6/+0xb8/+0xba patched to 0x442. Blind 0x0009 scan is wrong for
    // Asp (bit15 selects a mutually exclusive nested bank) — export uses
    // SetMeshHarvestThrustersBlind for emitter enumeration instead.
    uint8_t thrustObj[0x100];
    const uint8_t* prevGameObj = s_meshParseGameObj;
    bool installedThrustObj = false;
    const bool blindHarvest = s_meshHarvestThrustersBlind ||
                              (getenv("AB_HARVEST_BLIND") &&
                               getenv("AB_HARVEST_BLIND")[0] == '1');
    if (s_meshParseDistance >= 0 && s_meshHarvestThrusters && !blindHarvest) {
        std::memset(thrustObj, 0, sizeof(thrustObj));
        if (prevGameObj) {
            const size_t n = sizeof(thrustObj);
            std::memcpy(thrustObj, prevGameObj, n);
        } else {
            // Asp canopy BitClear opnd 0x81 (GameObj idx 1 / +0x9e): idle path
            // enters via BitClearBodyIsCanopySprite; with a live GameObj the
            // word must be non-zero or the canopy Sprite is skipped.
            *reinterpret_cast<uint16_t*>(thrustObj + 0x9e) = 1;
        }
        *reinterpret_cast<uint16_t*>(thrustObj + 0xb6) = kMeshThrustActive;
        *reinterpret_cast<uint16_t*>(thrustObj + 0xb8) = kMeshThrustActive;
        *reinterpret_cast<uint16_t*>(thrustObj + 0xba) = kMeshThrustActive;
        SetMeshParseGameObj(thrustObj);
        installedThrustObj = true;
    }

    RunMeshInterpreter(meshData, meshSize);

    if (installedThrustObj)
        SetMeshParseGameObj(prevGameObj);

    // Export emitter harvest / AB_HARVEST_BLIND: force-emit every 0x0009.
    if (s_meshParseDistance >= 0 && s_meshHarvestThrusters && blindHarvest)
        HarvestThrustersInRange(meshData, meshData + meshSize);

    bool usedVertices[256] = {};
    bool usedNormals[256] = {};

    for (const auto& poly : result.polygons) {
        for (int i = 0; i < poly.vertexCount; i++) {
            usedVertices[poly.vertexIdx[i]] = true;
        }
        if (poly.normalIdx > 0) {
            usedNormals[poly.normalIdx] = true;
        }
    }

    for (int i = 0; i < 256; i++) {
        if (usedVertices[i]) result.totalVertices++;
        if (usedNormals[i]) result.totalNormals++;
    }

    result.valid = true;
    if (getenv("AB_DUMP_MESH") && getenv("AB_DUMP_MESH")[0] == '1') {
        fprintf(stderr, "MESH_DUMP polys=%zu barriers=%zu\n",
                result.polygons.size(), result.barriers.size());
        for (size_t i = 0; i < result.polygons.size(); i++) {
            const auto& p = result.polygons[i];
            fprintf(stderr,
                    "MESH_POLY i=%zu type=%d tex=0x%04x n=%u vc=%d v=[%u,%u,%u,%u] stream=%d\n",
                    i, (int)p.type, p.texInfo, p.normalIdx, p.vertexCount,
                    p.vertexIdx[0], p.vertexIdx[1], p.vertexIdx[2], p.vertexIdx[3],
                    p.streamIndex);
        }
    }
    if (getenv("AB_DUMP_BARRIERS") && getenv("AB_DUMP_BARRIERS")[0] == '1') {
        for (size_t i = 0; i < result.barriers.size(); i++) {
            const auto& b = result.barriers[i];
            fprintf(stderr,
                    "MESH_BAR i=%zu op=0x%04x mode=%u stream=%d payload=%d "
                    "spriteOp=0x%04x attach=%u child=%d\n",
                    i, b.opcode, (unsigned)b.bandMode, b.streamIndex,
                    (int)b.payload, (unsigned)b.spriteOp, (unsigned)b.attachVert,
                    (int)(int16_t)b.childModelId);
        }
    }
    s_currentMesh = nullptr;
    s_lastMesh = result;

    return result;
}

const MeshData& GetLastParsedMesh()
{
    return s_lastMesh;
}

} // namespace jjffe
