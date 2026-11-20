#ifndef JJFFE_MESH_PARSER_H
#define JJFFE_MESH_PARSER_H

#include <cstdint>
#include <vector>

namespace jjffe {

// Mesh polygon types
enum class MeshPolyType : uint8_t {
    PlainLine = 0x02,       // Plain line (2 vertices)
    TexTri = 0x03,          // Textured triangle (FUNC_001707)
    TexQuad = 0x04,         // Textured quad (4 vertices + texture + normal)
    FlatQuad = 0x05,        // Flat-shaded quad (tex_info=0, distinct from TexQuad)
    ComplexFill = 0x06,     // Complex polygon fan piece (FUNC_001721) — flat single-color
                            // fill via FUNC_001692, even for 0x4xxx material words
    TexTriPair = 0x07,      // Mirrored triangle pair (FUNC_001706)
    TexQuadPair = 0x08,     // Textured quad pair (4 even vertices + mirrored)
    Thruster = 0x09,        // Engine nozzle / thruster billboard (FUNC_001738)
    SpecialPoly = 0x0A,     // Special polygon (material/transparency, 0xAB masked)
    ControlSeq = 0x0B,      // Control sequence (color/LOD state, 0x0B/0x4B/0x4C masked)
    Sprite = 0x0E,          // Billboard sprite (FUNC_001749) — airfighters e.g. Kestrel
    DirLine = 0x11,         // Directional line (2 vertices + normal + size)
    GlowPoints = 0x18,      // Glow/flare point list (FUNC_001766)
    NavLight = 0x01,        // PointSource nav light (FUNC_001698/001699)
    TextPrimitive = 0x1C,   // Text on hull (embedded string)
    EndMarker = 0x00,       // End of mesh data
};

// Parsed mesh polygon
struct MeshPoly {
    MeshPolyType type;      // Polygon type
    uint8_t vertexIdx[4];   // Vertex indices (up to 4)
    uint8_t normalIdx;      // Face normal index
    uint16_t texInfo;       // Texture info (bitmap index in bits 0-11)
    uint8_t color;          // Flat color (if texInfo == 0)
    uint8_t vertexCount;    // Number of vertices (2 for line, 4 for quad)
    bool afterSpecialPoly;  // True if this poly immediately follows a 0xAB LOD/material gate
    uint16_t specialParam;  // 0xAB threshold; thruster size; bit15=sprite child
    int streamIndex;        // Bytecode stream order (shared with MeshBarrier)
    // FUNC_001721 Contour group: pieces from one Complex721 share an id so the
    // renderer can span-fill bezier edges once (FUNC_001548→001799), not as
    // independent flat quads. -1 = not Contour.
    int16_t contourId;
    // FUNC_001749/750 child mesh expand (valid when specialParam bit15 set):
    uint16_t spriteOp;      // Orient opcode word (→ FUNC_001739/001740)
    uint8_t attachVert;     // Parent vertex the child attaches to
    // Nested sprite (e.g. Lynx #70 → #218 → #219): attach is relative to an
    // intermediate ObjectStruct, not the top-level ship. 0 = attach to ship.
    uint16_t attachParentModel;
    uint8_t attachParentVert;
    uint16_t attachParentOp;
    // FUNC_001741 Matrix immediately before this sprite (0 = none). Case-6
    // sprites copy ctx+0x9c built by Matrix: 001739(op*2) + optional 001743.
    // matrixWorking: op 0x1E negative branch committed +0x9c → +0x14 (working
    // matrix); apply Remap even when spriteOp is not case 6.
    // matrixSentinel: NormAxes 0x8019 (FUNC_001770) installed the Q31 identity
    // into that local basis before Matrix — child rot is twist(sentinel, θ),
    // parent-independent. Only #158/#327/#334 (8019→835c); not all 0x835c.
    uint16_t matrixOp;
    uint16_t matrixWord;
    bool matrixWorking;
    bool matrixSentinel;
    // Snapshot of FUNC_001758 ALU regs (renderCtx+0x54, 8 slots) at emit time.
    // Vertex flags 0x13/0xC0 read these; later ALU must not clobber earlier polys.
    uint16_t aluRegs[8];
    // Glow/flare (GlowPoints): byte-pair vertex indices stored separately.
    uint16_t glowVertCount;   // Number of byte-pair vertices (not counting first)
    uint16_t glowVertOffset;  // Index into parser's glow vertex store
    // renderCtx+0xf4 face light in effect when this poly was emitted. Nested
    // objects get their own context (FUNC_001750), so a sprite child can be lit
    // differently from the hull that spawned it.
    int8_t faceLightR;
    int8_t faceLightG;
    int8_t faceLightB;
    bool faceLightStamped;    // Set by the interpreter level that emitted it
};

// FUNC_001696 / opcode 0x06 — unified-z barrier insert (→ FUNC_001780).
struct MeshBarrier {
    uint16_t opcode;        // Full opcode word (ctrlCase = >>13)
    int16_t payload;        // Optional word after opcode (0 if none)
    int streamIndex;        // Interleaved with MeshPoly.streamIndex
    uint8_t bandMode;       // 0 = close (001780 mode 0), 1 = open band
    // When emitted from a sprite child expand (op=0x43c6 etc.):
    uint16_t spriteOp;      // 0 = parent-hull barrier
    uint8_t attachVert;
    uint16_t childModelId;  // 0xffff = none
};

// Mesh parsing result
struct MeshData {
    std::vector<MeshPoly> polygons;    // Parsed polygons
    std::vector<MeshBarrier> barriers; // 0x06 unified-z barriers
    int totalVertices;                 // Total unique vertex indices used
    int totalNormals;                  // Total unique normal indices used
    bool valid;                        // Whether parsing succeeded
    // FUNC_001762 → renderCtx+0xf4 face-light addend (RGB nibbles).
    // Added to material shade RGB before DPal (FUNC_001805). Default 0.
    int8_t faceLightR;
    int8_t faceLightG;
    int8_t faceLightB;
};

// Last mesh parse face light (for renderer; mirrors MeshData.faceLight*).
void GetMeshFaceLight(int8_t* r, int8_t* g, int8_t* b);

// Prefer a poly's own emit-time face light while drawing it (nullptr = last
// parse). Mirrors SetMeshAluOverride; see MeshPoly::faceLightR.
void SetMeshFaceLightOverride(const int8_t* rgb3);

// Glow/flare vertex store accessor (op 0x18 / GlowPoints).
const uint8_t* GetGlowVertStore();
int GetGlowVertCount();
void ClearMeshFaceLight();

// Parse mesh bytecode stream into polygon list
// meshData: pointer to mesh bytecode (from ShipModel::meshDataOffset)
// meshSize: size of mesh data in bytes
// Returns parsed mesh data
MeshData ParseMeshBytecode(const uint8_t* meshData, size_t meshSize);

// LOD distance for FUNC_001751 gates (0x0B/0x0C).
// < 0: legacy mode — do not skip gated blocks (preserves older shuttle parse).
// >= 0: faithful distance test — see SetMeshParseScaleRelated.
// Negative gate thresholds use the object-flag path (pristine demo → not far).
// Map view-space Z with MeshParseDistanceFromViewZ (ASM ctx+0xc = |viewZ|).
void SetMeshParseDistance(int32_t distance);
int32_t GetMeshParseDistance();

// Model index for JJFFE_STUB_DISPATCH_LOG attribution (-1 = unset).
// Callers that know the hull (ab_capture, inventory, demo) set this before
// ParseMeshBytecode; Sprite749/748 temporarily switch to the child id.
// Not reentrant-nested-safe unless callers save/restore GetMeshParseModelId().
void SetMeshParseModelId(int modelId);
int GetMeshParseModelId();

// ASM render ctx+0x148 = ObjectStruct.scaleRelated. FUNC_001751 compares
// (threshold << (scaleRelated + gfxDetail)) against parse distance. Positive
// LOD shifts saturate at 9 for scaleRelated < 12 (Puma #60 thr=316/488), and
// use the full shift when scaleRelated >= 12 (Gyr #35 Contour LOD). Override
// with AB_ISFAR_MAX_SHIFT. Default 0 keeps the unshifted path (sum <= 0) for
// unit tests that only set distance.
void SetMeshParseScaleRelated(int32_t scaleRelated);
int32_t GetMeshParseScaleRelated();

// ASM render ctx+0xc is the absolute view-space ship Z (GDB on dump-ab:
// distUnits=1 → 0x8000, distUnits=2 → 0x10000). Not Z>>5.
inline int32_t MeshParseDistanceFromViewZ(int32_t viewZ)
{
    if (viewZ < 0) viewZ = -viewZ;
    return viewZ;
}

// When true and parse distance >= 0, install a temporary GameObj with the three
// thruster axis words (ObjectStruct+0xb6/+0xb8/+0xba = 0x442, same value missile
// spawn uses) so BitClear/ALU gates enter FUNC_001738 bodies — matching
// `ffeasm --ship-demo` with those fields patched. Default false: idle A/B must
// not force engine glows. Pass --harvest-thrusters to ab_capture_shuttle.
// Blind raw-scan of every 0x0009 is opt-in via SetMeshHarvestThrustersBlind
// (export emitter harvest); Asp nests mutually-exclusive thruster banks behind
// bit15 of the axis word.
void SetMeshHarvestThrusters(bool enable);
bool GetMeshHarvestThrusters();
// The 0x442 axis word the synthetic thrust ObjectStruct carries (missile-spawn
// value); R1120: the walk lane seeds the same object when harvest is on.
uint16_t GetMeshThrustActiveWord();
void SetMeshHarvestThrustersBlind(bool enable);
bool GetMeshHarvestThrustersBlind();

// Title Sprite749 parents (#176/#177): enter BitClear-gated children even when
// ALU/ctx registers are pristine zero (ASM would set them via FUNC_001758).
// Prefer SetMeshParseGameObj + faithful BitClear/BitSet for title yaw orbits.
void SetMeshParseForceBitClearEnter(bool enable);
bool GetMeshParseForceBitClearEnter();

// Live ObjectStruct for FUNC_001473 kind 0x80 / ALU case 10 (obj+0x9c words).
// When set, BitClear/BitSet use ASM enter polarity (001756/001757 → 001753).
void SetMeshParseGameObj(const uint8_t* objectStruct);
const uint8_t* GetMeshParseGameObj();
uint16_t GetMeshParseGameObjWord(int index);

// Keep every child poly tex (not just mat60 tip glass). Needed for title
// planetoid constellation children #346–#361.
void SetMeshParseKeepAllSpriteChildren(bool enable);
bool GetMeshParseKeepAllSpriteChildren();

// Intro station phase: title #176 KeepAll still expands orbit card #355 → #73
// at fleet Z (DATA_007235), which parks the camera inside the Coriolis hull and
// paints screen-spanning near-plane slices. When set, Sprite749/748 skips
// expanding station-family models (72–80) so the autofit background station
// is the only hull in the frame.
void SetMeshParseSuppressStationChildren(bool enable);
bool GetMeshParseSuppressStationChildren();

// Byte length of a model's mesh bytecode using the promoted meshDataOffset
// (stub ObjectStructs like #201/#186: collision→verts span, not the empty stub).
size_t MeshBytecodeSize(int modelIndex);

// Initialize mesh opcode handlers (populates DATA_007816 jump table)
void InitMeshOpcodeHandlers();

// R1237 unit-test hook: FUNC_001763 / HandleOp12 field extract + data+1 advance.
// Does not touch parser mesh state (synthetic only). Returns data + 1.
uint16_t* TestHandleOp12Advance(uint16_t* data, uint32_t opcode,
                                uint8_t* outVertSlot, uint16_t* outStringCode);

// FUNC_001758 ALU regs (renderCtx+0x54..). Written by mesh ALU ops; read by
// FUNC_001473 kind 0xC0 during vertex transform (Skeet #45 0x13 lerp, etc.).
void ResetMeshAluRegs();
uint32_t GetMeshAluReg(int index);
void SetMeshAluReg(int index, uint32_t value);
// Prefer poly.aluRegs snapshot while transforming that poly (nullptr = bank).
void SetMeshAluOverride(const uint16_t* regs8);

// Decode Matrix angle word using the live ALU bank (ignores SetMeshAluOverride).
// Sentinel-path twist must not use Tag-clobbered poly.aluRegs stamps.
uint32_t MeshDecodeMatrixAngleLive(uint16_t word);

// FUNC_001770 + 001742→744: out9 = twist(sentinel, axis from matrixOp, θ).
// Parent-independent. Returns false if matrixOp is not 0x8000-family.
bool ApplyMeshSentinelWorkingBasis(int32_t out9[9], uint16_t matrixOp,
                                   uint16_t matrixWord);

// Get the last parsed mesh data (for testing)
const MeshData& GetLastParsedMesh();

// Silent opcode / gate counters for mesh inventory tools.
// Begin clears; each ParseMeshBytecode (RunMeshInterpreter) accumulates;
// End copies into out and disables. Nested Begin overwrites.
struct MeshWalkStats {
    int opcodeKind[32];   // executed ops, keyed by op&0x1F
    int opCount;
    int gateFarTotal;
    int gateFarSkip;
    int gateNearTotal;
    int gateNearSkip;
    int gateSkipWords;    // sum of (opcode>>5) when a gate skipped
    int specialPending;   // 0xAB window gates that armed afterSpecial
    // Raw opcode words for op 0x1E — used by census to decode FUNC_001473 mode.
    // Only populated when MeshWalkStatsActive() is true.
    static constexpr int kMaxOp1E = 64;
    int op1eCount;
    uint16_t op1eWords[kMaxOp1E];
};

void MeshWalkStatsBegin();
void MeshWalkStatsEnd(MeshWalkStats* out);
bool MeshWalkStatsActive();

// FUNC_001770 / NormAxes 0x8019: renderCtx+0x9c identity in Sin32/Q31 scale
// (±1.0 ≈ ±0x7fffffff), NOT 16.16 FP_ONE. Op 0x1E commit copies this local
// basis into working (+0x14). Angle=0 twist still runs FixedMulHigh and may
// shrink the twisted diagonal by a few ULPs (Sin32(0x4000)=0x7ffffff5).
constexpr int32_t kMeshBasisQ31One = 0x7fffffff;

// After ParseMeshBytecode: true if a NormAxes→Matrix→0x1E path committed a
// local working basis (e.g. #334 laser flare). Writes 9 column-major ints.
bool GetMeshWorkingLocalBasis(int32_t out9[9]);
bool MeshWorkingLocalBasisValid();

// Spec helper for tests: install 001770 sentinel into out9.
void InstallMeshBasisSentinel(int32_t out9[9]);

// FUNC_001743/001744: twist basis in place about axis with Sin32(angle).
// axis 0→cols1&2, 1→cols2&0, 2→cols0&1 (matches 0x835c → axis 2).
void MeshTwistLocalBasis(int32_t basis9[9], int axis, uint32_t angle);

} // namespace jjffe

#endif // JJFFE_MESH_PARSER_H
