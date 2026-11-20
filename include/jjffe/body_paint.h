#ifndef JJFFE_BODY_PAINT_H
#define JJFFE_BODY_PAINT_H

#include "jjffe/phys_obj.h"
#include "jjffe/sysgen.h"

#include <cstdint>

namespace jjffe {

// S1 table-builder modes. S4 picker must emit the same enum.
enum class BodyPaintMode : uint8_t {
    Wrap       = 0,
    Procedural = 1,
    Emissive   = 2,
    SkipMesh   = 3,
};

constexpr int kBodyPaintMaxModelId = 465;
constexpr int kTexturesCfgDefaultPathMax = 256;

struct TexturesCfgEntry {
    bool present = false;         // integer [id] section exists
    bool hasTextureKey = false;   // "texture=" seen (value may be 0)
    int  texture = 0;
    bool hasCorona = false;
    int  coronaTexture = 0;
    bool hasCoronaScale = false;
    int  coronaScale = 0;
    bool hasAtmoRatio = false;
    float atmoRatio = 0.f;
    bool hasAtmoTex = false;
    int  atmoTex = 0;
};

struct TexturesCfg {
    TexturesCfgEntry byId[kBodyPaintMaxModelId + 1];
};

struct BodyPaintClass {
    BodyPaintMode mode = BodyPaintMode::SkipMesh;
    bool overlayAtmo = false;
    bool missingCfgWarn = false;
};

struct BodyPaintDumpRow {
    int i = 0;
    char name[24] = {};
    int parent = 0;
    int type = 0;
    int32_t modelI32 = 0;
    int modelU16 = 0;
    uint8_t raw82[4] = {};
    bool hasCfgTex = false;
    int cfgTex = 0;
    bool hasCfgCorona = false;
    int cfgCorona = 0;
    bool hasCoronaScale = false;
    int coronaScale = 0;
    BodyPaintMode mode = BodyPaintMode::SkipMesh;
    bool overlayAtmo = false;
    bool hasAtmoRatio = false;
    float atmoRatio = 0.f;
    bool hasAtmoTex = false;
    int atmoTex = 0;
};

struct BodyPaintHistogram {
    int wrap = 0;
    int procedural = 0;
    int emissive = 0;
    int skipMesh = 0;
};

const char* BodyPaintModeName(BodyPaintMode mode);

// Parse integer [id] sections only ([134_Medium] is ignored).
bool TexturesCfgLoad(TexturesCfg* out, const char* path);
const TexturesCfgEntry* TexturesCfgLookup(const TexturesCfg* cfg, int32_t modelId);

// Default repo path used by S1 (ctest WORKING_DIRECTORY = source root).
const char* TexturesCfgDefaultPath();

bool BodyPaintIsStar(int32_t modelId, const TexturesCfgEntry* ent);
bool BodyPaintHasAtmosphere(const TexturesCfgEntry* ent);

// S7: procedural vertex-color class from textures.cfg bands — not KindForModel >=128.
enum class BodyProcClass : uint8_t {
    Asteroid   = 0,
    Rocky      = 1,
    OceanWorld = 2,  // Earth 129 / terra worlds with texture=0
    GasGiant   = 3,  // 134–137 only
    Star       = 4,  // 138–148
};
BodyProcClass BodyPaintProcClass(int32_t modelId);

// §2.2: SKIP_MESH if id not in 117–148; star before wrap; WRAP only if texture > 0.
BodyPaintClass BodyPaintClassify(int32_t modelId, const TexturesCfgEntry* ent);

int BodyPaintDumpWorld(const TexturesCfg* cfg, BodyPaintDumpRow* out, int cap);
BodyPaintHistogram BodyPaintCountModes(const BodyPaintDumpRow* rows, int n);

bool BodyPaintWriteGolden(const char* path, const BodyPaintDumpRow* rows, int n);
bool BodyPaintWriteMapDoc(const char* path, const BodyPaintDumpRow* rows, int n);

// --sol: true radii (Earth = 1) with a heavily compressed AU scalar.
// Moons use Earth-radii orbits around parents (not S_dist).
constexpr float kSolRadScale = 0.40f; // S_rad: world units per Earth radius
constexpr float kSolDistScale = 64.f;  // S_dist: world units per AU (2.5× tighter than 160)
// Mercury 0.38*64=24.3 sits inside Sol's 109.2*0.40=43.7 photosphere — schematic.

// S9: one UV-sphere instance per sphere-eligible WorldPhysObj (SKIP_MESH omitted).
// Positions are heliocentric (compressed AU) with moons offset from parents.
struct BodyPaintInstance {
    int index = 0;
    char name[24] = {};
    int32_t modelId = 0;
    BodyPaintMode mode = BodyPaintMode::SkipMesh;
    bool overlayAtmo = false;
    float x = 0.f, y = 0.f, z = 0.f;
    float scale = 1.f;
    int atmoTex = 0;
    float atmoRatio = 0.f;
};

int BodyPaintCollectSphereInstances(const TexturesCfg* cfg, BodyPaintInstance* out, int cap);

// --- S2: UV sphere (CPU). High lod matches D3dCreatePlanetMesh 96×64. ---
enum class BodySphereLod : int { High = 0, Mid = 1, Low = 2 };

struct BodySphereVert {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct BodySphereMesh {
    int segments = 0;
    int rings = 0;
    int vertCount = 0;
    int indexCount = 0;
    static constexpr int kMaxVerts = 8192;
    static constexpr int kMaxIndices = 49152; // 96*64*6 = 36864
    BodySphereVert verts[kMaxVerts];
    uint16_t indices[kMaxIndices];
};

int BodySphereVertCount(int segments, int rings);
int BodySphereIndexCount(int segments, int rings);
void BodySphereLodParams(BodySphereLod lod, int* segments, int* rings);
BodySphereLod BodySpherePickLod(float angularDiameterPx);
bool BodySphereGenerate(int segments, int rings, BodySphereMesh* out);

// --- S4: picker (classify + cfg ids). AB_BODYPAINT_FORCE overrides mode. ---
struct BodyMaterial {
    BodyPaintMode mode = BodyPaintMode::SkipMesh;
    int32_t modelId = 0;
    int32_t seed = 0;
    int wrapTex = 0;
    int coronaTex = 0;
    int coronaScale = 0;
    bool overlayAtmo = false;
    float atmoRatio = 0.f;
    int atmoTex = 0;
};

BodyMaterial BodyPaintPick(int32_t modelId, int32_t seed, const TexturesCfgEntry* ent,
                           const char* name = nullptr);

// S5: resolve FFED3D wrap maps (Textures/texNNN.png). Missing file → false.
bool BodyPaintResolveTexFile(int texId, char* out, int outCap, const char* extraRoot);
bool BodyPaintResolveCfgFile(char* out, int outCap, const char* extraRoot);
// If WRAP and the file is missing: WARN, flip to PROCEDURAL. Returns false on fallback.
bool BodyPaintFinalizeWrap(BodyMaterial* mat, const char* extraRoot);

// Sphere-eligible ObjectStruct ids (G3 drawn set). Stations 72–105 are not.
bool BodyPaintIsSphereId(int32_t modelId);
// S6: AB_BODYPAINT_NO_CORONA=1 keeps the 701 core and drops the billboard.
bool BodyPaintWantCorona();
// Cfg corona_scale (default 12); AB_BODYPAINT_CORONA_SCALE overrides.
int BodyPaintCoronaScale(int cfgScale);
// S8: AB_BODYPAINT_NO_ATMO=1 keeps the planet and drops the shell.
bool BodyPaintWantAtmo();
// Cfg atmosphereRatio (Earth 1.04); AB_BODYPAINT_ATMO_RATIO overrides.
float BodyPaintAtmoRatio(float cfgRatio);
// atmosphereN.png first (not texN.png). Earth 26 → Textures/atmosphere26.png.
bool BodyPaintResolveAtmoFile(int texId, char* out, int outCap, const char* extraRoot);

// Known Sol bodies: bake an orbit-view equirect (not ASM, not NASA).
// Earth 129, Mars 125 (texture=0), Venus 123 (GPU bake overrides WRAP 712).
// Moon shares model 120 with Mercury — do not key Moon here.
bool BodyPaintIsKnownPlanet(int32_t modelId);
void BodyPaintSampleKnown(int32_t modelId, float latDeg, float lonDeg, float* r, float* g, float* b);
bool BodyPaintBakeKnownEquirect(int32_t modelId, uint32_t* argb /*A8R8G8B8*/, int w, int h);

// S7: class palettes (cfg band, not KindForModel >= 128). Direction is a unit sphere normal.
void BodyPaintSampleProcedural(int32_t modelId, int32_t seed, float nx, float ny, float nz,
                               float* r, float* g, float* b);
bool BodyPaintBakeProceduralEquirect(int32_t modelId, int32_t seed, uint32_t* argb, int w, int h);

} // namespace jjffe

#endif // JJFFE_BODY_PAINT_H
