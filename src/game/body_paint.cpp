#include "jjffe/body_paint.h"
#include "jjffe/world.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cmath>

#if defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

namespace jjffe {

const char* BodyPaintModeName(BodyPaintMode mode)
{
    switch (mode) {
    case BodyPaintMode::Wrap:       return "WRAP";
    case BodyPaintMode::Procedural: return "PROCEDURAL";
    case BodyPaintMode::Emissive:   return "EMISSIVE";
    case BodyPaintMode::SkipMesh:   return "SKIP_MESH";
    }
    return "SKIP_MESH";
}

const char* TexturesCfgDefaultPath()
{
    return "reference/ffed3d/textures.cfg";
}

static void TrimInPlace(char* s)
{
    if (!s) return;
    char* start = s;
    while (*start && std::isspace(static_cast<unsigned char>(*start))) ++start;
    if (start != s) std::memmove(s, start, std::strlen(start) + 1);
    size_t n = std::strlen(s);
    while (n > 0 && std::isspace(static_cast<unsigned char>(s[n - 1]))) {
        s[--n] = 0;
    }
    char* hash = std::strchr(s, '#');
    if (hash) {
        *hash = 0;
        n = std::strlen(s);
        while (n > 0 && std::isspace(static_cast<unsigned char>(s[n - 1]))) {
            s[--n] = 0;
        }
    }
}

bool TexturesCfgLoad(TexturesCfg* out, const char* path)
{
    if (!out || !path) return false;
    *out = {};
    FILE* f = std::fopen(path, "r");
    if (!f) return false;

    int current = -1;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        TrimInPlace(line);
        if (line[0] == 0) continue;
        if (line[0] == '[') {
            char* end = nullptr;
            long id = std::strtol(line + 1, &end, 10);
            if (end > line + 1 && *end == ']' && id >= 0 && id <= kBodyPaintMaxModelId) {
                current = static_cast<int>(id);
                out->byId[current].present = true;
            } else {
                current = -1; // skip [134_Medium] and non-integer tags
            }
            continue;
        }
        if (current < 0) continue;
        TexturesCfgEntry& e = out->byId[current];
        char key[64] = {};
        char val[64] = {};
        if (std::sscanf(line, "%63[^=]=%63s", key, val) != 2) continue;
        if (std::strcmp(key, "texture") == 0) {
            e.hasTextureKey = true;
            e.texture = std::atoi(val);
        } else if (std::strcmp(key, "corona_texture") == 0) {
            e.hasCorona = true;
            e.coronaTexture = std::atoi(val);
        } else if (std::strcmp(key, "corona_scale") == 0) {
            e.hasCoronaScale = true;
            e.coronaScale = std::atoi(val);
        } else if (std::strcmp(key, "atmosphereRatio") == 0) {
            e.hasAtmoRatio = true;
            e.atmoRatio = std::strtof(val, nullptr);
        } else if (std::strcmp(key, "atmosphereTextureId") == 0) {
            e.hasAtmoTex = true;
            e.atmoTex = std::atoi(val);
        }
    }
    std::fclose(f);
    return true;
}

const TexturesCfgEntry* TexturesCfgLookup(const TexturesCfg* cfg, int32_t modelId)
{
    if (!cfg || modelId < 0 || modelId > kBodyPaintMaxModelId) return nullptr;
    const TexturesCfgEntry* e = &cfg->byId[modelId];
    return e->present ? e : nullptr;
}

// textures.cfg [125] is "terraformed" (LUT 1, blue) and [129] is "polluted"
// (LUT 26, orange). Sol uses those ids for Mars and Earth, so the limbs look
// swapped against the surface bakes. Pick/dump remap Sol Earth↔Mars atmo only.
static int RemapSolAtmoTex(int32_t modelId, int cfgTex)
{
    if (modelId == 129) return 1;
    if (modelId == 125) return 26;
    return cfgTex;
}

bool BodyPaintIsStar(int32_t modelId, const TexturesCfgEntry* ent)
{
    if (modelId >= 137 && modelId <= 148) return true;
    return ent && ent->hasCorona && ent->coronaTexture > 0;
}

bool BodyPaintHasAtmosphere(const TexturesCfgEntry* ent)
{
    if (!ent) return false;
    if (ent->hasAtmoTex && ent->atmoTex > 0) return true;
    if (ent->hasAtmoRatio && ent->atmoRatio > 1.0f) return true;
    return false;
}

BodyProcClass BodyPaintProcClass(int32_t modelId)
{
    // textures.cfg: [122]–[133] terra/ocean/desert (texture=0); [134]–[137] giants;
    // [138]–[148] stars. Earth 129 must not fall in the giant band.
    if (modelId >= 138) return BodyProcClass::Star;
    if (modelId >= 134) return BodyProcClass::GasGiant;
    if (modelId == 133) return BodyProcClass::Rocky; // cfg "Red Planet"
    if (modelId == 125) return BodyProcClass::Rocky; // Mars (texture=0)
    if (modelId >= 122) return BodyProcClass::OceanWorld;
    if (modelId >= 119) return BodyProcClass::Rocky;
    return BodyProcClass::Asteroid;
}

// Cassini / New Horizons / Voyager / Hubble: detached shell only when a
// surface sits under an optically distinct envelope. Gas giants already
// dropped in Classify. Io 122 = SO2 exosphere, no global halo. Pluto shares
// model 119 with airless rocks — name-keyed blue haze.
static bool NameIs(const char* name, const char* want)
{
    return name && want && std::strcmp(name, want) == 0;
}

static void ApplyObservationalLimb(BodyPaintClass* c, int32_t modelId, const char* name)
{
    if (!c) return;
    if (c->mode == BodyPaintMode::SkipMesh || c->mode == BodyPaintMode::Emissive)
        return;
    if (modelId == 122) c->overlayAtmo = false; // Io
    if (NameIs(name, "Pluto")) c->overlayAtmo = true;
}

// Schematic (R+h)/R from visible haze altitude. Pack LUT colours (not cfg):
// 1=blue Rayleigh, 20=cream, 21=violet (Titan detached haze), 22=teal Triton,
// 26=warm dust, 27=Io orange. textures.cfg 21 is violet, not orange.
static bool ApplyTelemetryAtmo(bool overlay, int32_t modelId, const char* name,
                               bool* hasRatio, float* ratio, bool* hasTex, int* tex)
{
    if (!overlay) return false;
    float r = 0.f;
    int t = 0;
    if (NameIs(name, "Pluto")) {
        r = 1.06f;
        t = 1;
    } else if (NameIs(name, "Titan") || modelId == 124) {
        r = 1.15f;
        t = 21;
    } else if (NameIs(name, "Venus") || modelId == 123) {
        r = 1.015f;
        t = 20;
    } else if (NameIs(name, "Mars") || modelId == 125) {
        r = 1.02f;
        t = 26; // warm dust at V=1 (surface); Hubble violet on V=0 day limb
    } else if (NameIs(name, "Earth") || modelId == 129) {
        r = 1.04f;
        t = 1;
    } else if (NameIs(name, "Triton") || modelId == 121) {
        r = 1.02f;
        t = 22;
    } else {
        return false;
    }
    if (hasRatio) *hasRatio = true;
    if (ratio) *ratio = r;
    if (hasTex) *hasTex = true;
    if (tex) *tex = t;
    return true;
}

BodyPaintClass BodyPaintClassify(int32_t modelId, const TexturesCfgEntry* ent)
{
    BodyPaintClass c;
    if (modelId < 0) {
        c.mode = BodyPaintMode::SkipMesh;
        c.missingCfgWarn = true;
        return c;
    }
    // Sphere band only. Stations/starports/ships/city sit outside 117–148.
    if (modelId < 117 || modelId > 148) {
        c.mode = BodyPaintMode::SkipMesh;
        return c;
    }
    if (BodyPaintIsStar(modelId, ent)) {
        c.mode = BodyPaintMode::Emissive;
        return c;
    }
    if (ent && ent->hasTextureKey && ent->texture > 0) {
        c.mode = BodyPaintMode::Wrap;
        // Gas giants: the WRAP disc *is* the cloud deck. A 1.04× crown is an
        // Earth-style Rayleigh shell (Voyager/Cassini: limb darkening, no halo).
        c.overlayAtmo = BodyPaintHasAtmosphere(ent) &&
                        BodyPaintProcClass(modelId) != BodyProcClass::GasGiant;
        ApplyObservationalLimb(&c, modelId, nullptr);
        return c;
    }
    c.mode = BodyPaintMode::Procedural;
    c.overlayAtmo = BodyPaintHasAtmosphere(ent) &&
                    BodyPaintProcClass(modelId) != BodyProcClass::GasGiant;
    ApplyObservationalLimb(&c, modelId, nullptr);
    c.missingCfgWarn = !ent;
    return c;
}

static void FillRow(int i, const SysObj* sys, const PhysObj* p,
                    const TexturesCfg* cfg, BodyPaintDumpRow* row)
{
    *row = {};
    row->i = i;
    const char* name = PhysObjGetName(p);
    int n = 0;
    for (; n < 23 && name && name[n]; ++n) row->name[n] = name[n];
    row->name[n] = 0;
    row->parent = SysObjParent(sys);
    row->type = SysObjTypeValue(sys);
    row->modelI32 = PhysObjGetModel(p);
    row->modelU16 = row->modelI32 & 0xffff;
    std::memcpy(row->raw82, p->raw + 0x82, 4);

    const TexturesCfgEntry* ent = TexturesCfgLookup(cfg, row->modelI32);
    if (ent) {
        if (ent->hasTextureKey) {
            row->hasCfgTex = true;
            row->cfgTex = ent->texture;
        }
        if (ent->hasCorona) {
            row->hasCfgCorona = true;
            row->cfgCorona = ent->coronaTexture;
        }
        if (ent->hasCoronaScale) {
            row->hasCoronaScale = true;
            row->coronaScale = ent->coronaScale;
        }
        if (ent->hasAtmoRatio) {
            row->hasAtmoRatio = true;
            row->atmoRatio = ent->atmoRatio;
        }
        if (ent->hasAtmoTex) {
            row->hasAtmoTex = true;
            row->atmoTex = RemapSolAtmoTex(row->modelI32, ent->atmoTex);
        }
    }

    BodyPaintClass cls = BodyPaintClassify(row->modelI32, ent);
    ApplyObservationalLimb(&cls, row->modelI32, row->name);
    row->mode = cls.mode;
    row->overlayAtmo = cls.overlayAtmo;
    ApplyTelemetryAtmo(row->overlayAtmo, row->modelI32, row->name,
                       &row->hasAtmoRatio, &row->atmoRatio, &row->hasAtmoTex, &row->atmoTex);
    if (!row->overlayAtmo) {
        row->hasAtmoRatio = false;
        row->hasAtmoTex = false;
        row->atmoRatio = 0.f;
        row->atmoTex = 0;
    }
    if (cls.missingCfgWarn) {
        std::fprintf(stderr, "BodyPaint: missing textures.cfg for model %d (%s) → PROCEDURAL\n",
                     row->modelI32, row->name);
    }
}

int BodyPaintDumpWorld(const TexturesCfg* cfg, BodyPaintDumpRow* out, int cap)
{
    if (!cfg || !out || cap <= 0) return 0;
    const int n = WorldBodyCount();
    const PhysObj* phys = WorldPhysObjs();
    const SysObj* sys = WorldSysObjs();
    if (!phys || !sys || n <= 0) return 0;
    int w = 0;
    for (int i = 0; i < n && w < cap; ++i) {
        FillRow(i, &sys[i], &phys[i], cfg, &out[w]);
        ++w;
    }
    return w;
}

BodyPaintHistogram BodyPaintCountModes(const BodyPaintDumpRow* rows, int n)
{
    BodyPaintHistogram h{};
    for (int i = 0; i < n; ++i) {
        switch (rows[i].mode) {
        case BodyPaintMode::Wrap:       h.wrap++; break;
        case BodyPaintMode::Procedural: h.procedural++; break;
        case BodyPaintMode::Emissive:   h.emissive++; break;
        case BodyPaintMode::SkipMesh:   h.skipMesh++; break;
        }
    }
    return h;
}

// Radius in Earth radii. au>0 → heliocentric (compressed). orbitRe>0 → parent-relative.
struct SolBodyDef {
    const char* name;
    float radiusEarth;
    float au;
    float orbitRe;
};

static const SolBodyDef kSolBodyDefs[] = {
    {"Sol",      109.2f,  0.f,     0.f},
    {"Mercury",    0.38f,  0.38f,   0.f},
    {"Venus",      0.94f,  0.72f,   0.f},
    {"Earth",      1.00f,  1.00f,   0.f},
    {"Moon",       0.27f,  0.f,    60.3f},
    {"Mars",       0.53f,  1.52f,   0.f},
    {"Phobos",     0.0018f, 0.f,    1.47f},
    {"Deimos",     0.0010f, 0.f,    3.68f},
    {"Ceres",      0.074f, 2.77f,   0.f},
    {"Jupiter",   10.97f,  5.20f,   0.f},
    {"Amalthea",   0.013f, 0.f,    28.5f},
    {"Io",         0.28f,  0.f,    66.2f},
    {"Europa",     0.24f,  0.f,   105.3f},
    {"Ganymede",   0.41f,  0.f,   167.9f},
    {"Callisto",   0.37f,  0.f,   295.5f},
    {"Saturn",     9.14f,  9.58f,   0.f},
    {"Mimas",      0.031f, 0.f,    29.1f},
    {"Enceladus",  0.040f, 0.f,    37.3f},
    {"Tethys",     0.083f, 0.f,    46.2f},
    {"Dione",      0.088f, 0.f,    59.2f},
    {"Rhea",       0.120f, 0.f,    82.7f},
    {"Titan",      0.40f,  0.f,   191.8f},
    {"Hyperion",   0.021f, 0.f,   232.5f},
    {"Iapetus",    0.115f, 0.f,   558.9f},
    {"Uranus",     3.98f, 19.18f,   0.f},
    {"Miranda",    0.037f, 0.f,    20.3f},
    {"Ariel",      0.091f, 0.f,    30.0f},
    {"Umbriel",    0.092f, 0.f,    41.7f},
    {"Titania",    0.124f, 0.f,    68.4f},
    {"Oberon",     0.119f, 0.f,    91.5f},
    {"Neptune",    3.86f, 30.07f,   0.f},
    {"Triton",     0.21f,  0.f,    55.6f},
    {"Nereid",     0.027f, 0.f,   865.5f},
    {"Pluto",      0.18f, 39.48f,   0.f},
    {"Charon",     0.09f,  0.f,     3.0f},
};

static const SolBodyDef* FindSolBodyDef(const char* name)
{
    if (!name) return nullptr;
    for (const SolBodyDef& d : kSolBodyDefs) {
        if (std::strcmp(d.name, name) == 0) return &d;
    }
    return nullptr;
}

static float FallbackRadiusEarth(int32_t modelId)
{
    switch (BodyPaintProcClass(modelId)) {
    case BodyProcClass::Star:       return 109.2f;
    case BodyProcClass::GasGiant:   return 10.0f;
    case BodyProcClass::OceanWorld: return 1.0f;
    case BodyProcClass::Rocky:      return 0.20f;
    default:                        return 0.05f;
    }
}

static int SolChildRank(const SysObj* sys, int i)
{
    const uint16_t parent = sys[i].parentIndex;
    int rank = 0;
    for (int j = 0; j < i; ++j) {
        if (sys[j].parentIndex == parent) ++rank;
    }
    return rank;
}

int BodyPaintCollectSphereInstances(const TexturesCfg* cfg, BodyPaintInstance* out, int cap)
{
    if (!out || cap <= 0) return 0;
    const int n = WorldBodyCount();
    const PhysObj* phys = WorldPhysObjs();
    const SysObj* sys = WorldSysObjs();
    if (n <= 0 || !phys || !sys) return 0;

    float wx[kWorldBodyCapacity] = {};
    float wy[kWorldBodyCapacity] = {};
    float wz[kWorldBodyCapacity] = {};
    float rad[kWorldBodyCapacity] = {};

    for (int i = 0; i < n; ++i) {
        const char* name = PhysObjGetName(&phys[i]);
        const int32_t id = PhysObjGetModel(&phys[i]);
        const SolBodyDef* def = FindSolBodyDef(name);
        rad[i] = (def ? def->radiusEarth : FallbackRadiusEarth(id)) * kSolRadScale;

        float px = 0.f, py = 0.f, pz = 0.f;
        const int parentSlot = static_cast<int>(sys[i].parentIndex) - 1;
        if (parentSlot >= 0 && parentSlot < i) {
            px = wx[parentSlot];
            py = wy[parentSlot];
            pz = wz[parentSlot];
        }

        if (sys[i].parentIndex == 0 || NameIs(name, "Sol")) {
            wx[i] = wy[i] = wz[i] = 0.f;
        } else if (def && def->au > 0.f) {
            wx[i] = def->au * kSolDistScale;
            wy[i] = 0.f;
            wz[i] = 0.f;
        } else if (def && def->orbitRe > 0.f) {
            wx[i] = px;
            wy[i] = py + def->orbitRe * kSolRadScale;
            wz[i] = pz;
        } else if (sys[i].parentIndex <= 1) {
            const int rank = SolChildRank(sys, i);
            wx[i] = (2.2f + static_cast<float>(rank) * 0.12f) * kSolDistScale;
            wy[i] = 0.f;
            wz[i] = 0.f;
        } else {
            const int rank = SolChildRank(sys, i);
            wx[i] = px;
            wy[i] = py + (6.f + static_cast<float>(rank) * 4.f) * kSolRadScale;
            wz[i] = pz;
        }
    }

    int w = 0;
    for (int i = 0; i < n && w < cap; ++i) {
        const int32_t id = PhysObjGetModel(&phys[i]);
        const TexturesCfgEntry* ent = cfg ? TexturesCfgLookup(cfg, id) : nullptr;
        BodyPaintClass cls = BodyPaintClassify(id, ent);
        if (cls.mode == BodyPaintMode::SkipMesh) continue;
        BodyPaintInstance& inst = out[w];
        inst = {};
        inst.index = i;
        inst.modelId = id;
        inst.mode = cls.mode;
        const char* name = PhysObjGetName(&phys[i]);
        int ni = 0;
        for (; ni < 23 && name && name[ni]; ++ni) inst.name[ni] = name[ni];
        inst.name[ni] = 0;
        ApplyObservationalLimb(&cls, id, inst.name);
        inst.overlayAtmo = cls.overlayAtmo;
        if (inst.overlayAtmo) {
            if (ent && ent->hasAtmoTex)
                inst.atmoTex = RemapSolAtmoTex(id, ent->atmoTex);
            if (ent && ent->hasAtmoRatio) inst.atmoRatio = ent->atmoRatio;
            ApplyTelemetryAtmo(true, id, inst.name, nullptr, &inst.atmoRatio, nullptr,
                               &inst.atmoTex);
        }
        inst.x = wx[i];
        inst.y = wy[i];
        inst.z = wz[i];
        inst.scale = rad[i];
        ++w;
    }
    return w;
}

static void WriteJsonNullOrInt(FILE* f, const char* key, bool present, int v, bool comma)
{
    if (present) std::fprintf(f, "      \"%s\": %d%s\n", key, v, comma ? "," : "");
    else std::fprintf(f, "      \"%s\": null%s\n", key, comma ? "," : "");
}

static void WriteJsonNullOrFloat(FILE* f, const char* key, bool present, float v, bool comma)
{
    if (present) std::fprintf(f, "      \"%s\": %.4g%s\n", key, static_cast<double>(v), comma ? "," : "");
    else std::fprintf(f, "      \"%s\": null%s\n", key, comma ? "," : "");
}

bool BodyPaintWriteGolden(const char* path, const BodyPaintDumpRow* rows, int n)
{
    if (!path || !rows || n <= 0) return false;
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    BodyPaintHistogram h = BodyPaintCountModes(rows, n);
    std::fprintf(f,
        "{\n"
        "  \"version\": 1,\n"
        "  \"system\": \"Sol\",\n"
        "  \"count\": %d,\n"
        "  \"histogram\": { \"WRAP\": %d, \"PROCEDURAL\": %d, \"EMISSIVE\": %d, \"SKIP_MESH\": %d },\n"
        "  \"bodies\": [\n",
        n, h.wrap, h.procedural, h.emissive, h.skipMesh);
    for (int i = 0; i < n; ++i) {
        const BodyPaintDumpRow& r = rows[i];
        std::fprintf(f,
            "    {\n"
            "      \"i\": %d,\n"
            "      \"name\": \"%s\",\n"
            "      \"parent\": %d,\n"
            "      \"type\": %d,\n"
            "      \"model_i32\": %d,\n"
            "      \"model_u16\": %d,\n"
            "      \"raw82\": [%u, %u, %u, %u],\n",
            r.i, r.name, r.parent, r.type, r.modelI32, r.modelU16,
            r.raw82[0], r.raw82[1], r.raw82[2], r.raw82[3]);
        WriteJsonNullOrInt(f, "cfg_tex", r.hasCfgTex, r.cfgTex, true);
        WriteJsonNullOrInt(f, "cfg_corona", r.hasCfgCorona, r.cfgCorona, true);
        WriteJsonNullOrInt(f, "corona_scale", r.hasCoronaScale, r.coronaScale, true);
        std::fprintf(f, "      \"mode\": \"%s\",\n", BodyPaintModeName(r.mode));
        std::fprintf(f, "      \"overlay\": \"%s\",\n", r.overlayAtmo ? "ATMO_SHELL" : "");
        WriteJsonNullOrFloat(f, "atmo_ratio", r.hasAtmoRatio, r.atmoRatio, true);
        WriteJsonNullOrInt(f, "atmo_tex", r.hasAtmoTex, r.atmoTex, false);
        std::fprintf(f, "    }%s\n", (i + 1 < n) ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
    std::fclose(f);
    return true;
}

bool BodyPaintWriteMapDoc(const char* path, const BodyPaintDumpRow* rows, int n)
{
    if (!path || !rows || n <= 0) return false;
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    BodyPaintHistogram h = BodyPaintCountModes(rows, n);
    std::fprintf(f,
        "# PhysObj +0x82 map (Sol body-paint S1)\n\n"
        "Generated by `BodyPaintDumpWorld` after `WorldLoadSol()`. "
        "Canonical read is signed dword `PhysObjGetModel()`. "
        "Dispatch: `docs/PHASE_64_BODY_PAINT_DISPATCH.md`.\n\n"
        "## Histogram (n=%d)\n\n"
        "| Mode | Count |\n|------|-------|\n"
        "| WRAP | %d |\n"
        "| PROCEDURAL | %d |\n"
        "| EMISSIVE | %d |\n"
        "| SKIP_MESH | %d |\n\n"
        "Anchors: Sol 141 EMISSIVE · Mercury 120 WRAP(711) · Earth 129 PROCEDURAL · London 84 SKIP_MESH.\n\n"
        "## Rows\n\n"
        "| i | Name | Parent | Type | model_i32 | cfg_tex | mode | overlay |\n"
        "|---|------|--------|------|-----------|---------|------|--------|\n",
        n, h.wrap, h.procedural, h.emissive, h.skipMesh);
    for (int i = 0; i < n; ++i) {
        const BodyPaintDumpRow& r = rows[i];
        if (r.hasCfgTex)
            std::fprintf(f, "| %d | %s | %d | %d | %d | %d | %s | %s |\n",
                         r.i, r.name, r.parent, r.type, r.modelI32, r.cfgTex,
                         BodyPaintModeName(r.mode), r.overlayAtmo ? "ATMO_SHELL" : "");
        else
            std::fprintf(f, "| %d | %s | %d | %d | %d | | %s | %s |\n",
                         r.i, r.name, r.parent, r.type, r.modelI32,
                         BodyPaintModeName(r.mode), r.overlayAtmo ? "ATMO_SHELL" : "");
    }
    std::fprintf(f, "\nT2: SKIP_MESH listed first; drawn WRAP/PROCEDURAL/EMISSIVE ids are in 117–148.\n");
    std::fclose(f);
    return true;
}

int BodySphereVertCount(int segments, int rings)
{
    if (segments < 3 || rings < 2) return 0;
    return (rings + 1) * (segments + 1);
}

int BodySphereIndexCount(int segments, int rings)
{
    if (segments < 3 || rings < 2) return 0;
    return rings * segments * 6;
}

void BodySphereLodParams(BodySphereLod lod, int* segments, int* rings)
{
    int s = 96, r = 64;
    if (lod == BodySphereLod::Mid) { s = 48; r = 32; }
    else if (lod == BodySphereLod::Low) { s = 24; r = 16; }
    if (segments) *segments = s;
    if (rings) *rings = r;
}

BodySphereLod BodySpherePickLod(float angularDiameterPx)
{
    if (angularDiameterPx >= 64.f) return BodySphereLod::High;
    if (angularDiameterPx >= 20.f) return BodySphereLod::Mid;
    return BodySphereLod::Low;
}

bool BodySphereGenerate(int segments, int rings, BodySphereMesh* out)
{
    if (!out) return false;
    *out = {};
    const int nv = BodySphereVertCount(segments, rings);
    const int ni = BodySphereIndexCount(segments, rings);
    if (nv <= 0 || ni <= 0) return false;
    if (nv > BodySphereMesh::kMaxVerts || ni > BodySphereMesh::kMaxIndices) return false;

    out->segments = segments;
    out->rings = rings;
    out->vertCount = nv;
    out->indexCount = ni;

    int vi = 0;
    for (int y = 0; y <= rings; y++) {
        const float vv = static_cast<float>(y) / static_cast<float>(rings);
        const float phi = vv * 3.14159265f;
        const float sy = std::cos(phi);
        const float rr = std::sin(phi);
        for (int x = 0; x <= segments; x++) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float theta = u * 6.2831853f;
            BodySphereVert& v = out->verts[vi++];
            v.nx = rr * std::cos(theta);
            v.ny = sy;
            v.nz = rr * std::sin(theta);
            v.x = v.nx;
            v.y = v.ny;
            v.z = v.nz;
            v.u = u;
            v.v = vv;
        }
    }

    int ii = 0;
    for (int y = 0; y < rings; y++) {
        for (int x = 0; x < segments; x++) {
            const uint16_t i0 = static_cast<uint16_t>(y * (segments + 1) + x);
            const uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            const uint16_t i2 = static_cast<uint16_t>(i0 + segments + 1);
            const uint16_t i3 = static_cast<uint16_t>(i2 + 1);
            out->indices[ii++] = i0;
            out->indices[ii++] = i1;
            out->indices[ii++] = i2;
            out->indices[ii++] = i1;
            out->indices[ii++] = i3;
            out->indices[ii++] = i2;
        }
    }
    return vi == nv && ii == ni;
}

static BodyPaintMode ModeFromForceEnv()
{
    const char* f = std::getenv("AB_BODYPAINT_FORCE");
    if (!f || !f[0]) return static_cast<BodyPaintMode>(0xff);
    if (std::strcmp(f, "WRAP") == 0) return BodyPaintMode::Wrap;
    if (std::strcmp(f, "PROCEDURAL") == 0) return BodyPaintMode::Procedural;
    if (std::strcmp(f, "EMISSIVE") == 0) return BodyPaintMode::Emissive;
    if (std::strcmp(f, "SKIP_MESH") == 0) return BodyPaintMode::SkipMesh;
    return static_cast<BodyPaintMode>(0xff);
}

BodyMaterial BodyPaintPick(int32_t modelId, int32_t seed, const TexturesCfgEntry* ent,
                           const char* name)
{
    BodyMaterial m;
    m.modelId = modelId;
    m.seed = seed ^ modelId;
    BodyPaintClass cls = BodyPaintClassify(modelId, ent);
    ApplyObservationalLimb(&cls, modelId, name);
    m.mode = cls.mode;
    m.overlayAtmo = cls.overlayAtmo;
    const BodyPaintMode forced = ModeFromForceEnv();
    if (static_cast<uint8_t>(forced) != 0xff) {
        m.mode = forced;
        if (m.mode == BodyPaintMode::Emissive || m.mode == BodyPaintMode::SkipMesh) {
            m.overlayAtmo = false;
        }
    }
    if (ent) {
        if (ent->hasTextureKey) m.wrapTex = ent->texture;
        if (ent->hasCorona) m.coronaTex = ent->coronaTexture;
        if (ent->hasCoronaScale) m.coronaScale = ent->coronaScale;
        if (m.overlayAtmo) {
            if (ent->hasAtmoRatio) m.atmoRatio = ent->atmoRatio;
            if (ent->hasAtmoTex)
                m.atmoTex = RemapSolAtmoTex(modelId, ent->atmoTex);
        }
    }
    if (m.overlayAtmo)
        ApplyTelemetryAtmo(true, modelId, name, nullptr, &m.atmoRatio, nullptr, &m.atmoTex);
    if (m.mode == BodyPaintMode::Emissive) {
        if (m.wrapTex <= 0) m.wrapTex = 701;
        if (m.coronaTex <= 0) m.coronaTex = 760;
        if (m.coronaScale <= 0) m.coronaScale = 12;
        m.overlayAtmo = false;
    }
    if (m.mode != BodyPaintMode::Wrap) {
        if (m.mode != BodyPaintMode::Emissive) m.wrapTex = 0;
    }
    return m;
}

static bool TexFileReadable(const char* path)
{
    if (!path || !path[0]) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static bool TryTexPattern(char* out, int cap, const char* fmt, const char* root, int texId)
{
    char path[1024];
    if (root && root[0])
        std::snprintf(path, sizeof(path), fmt, root, texId);
    else
        return false;
    if (!TexFileReadable(path)) return false;
    if (out && cap > 0) {
        std::snprintf(out, cap, "%s", path);
    }
    return true;
}

static bool TryPlainPath(char* out, int cap, const char* path)
{
    if (!TexFileReadable(path)) return false;
    if (out && cap > 0) std::snprintf(out, cap, "%s", path);
    return true;
}

// cwd is often ffe106/FFE (MODELS/ present → D3D root="."), which has no
// tex760.png. Always also search the JJFFE reference tree and exe-relative paths.
static int CollectPaintRoots(const char* extraRoot, const char** roots, int maxRoots,
                             char exeRef[][4096], int nExeSlot)
{
    int n = 0;
    auto add = [&](const char* r) {
        if (!r || !r[0] || n >= maxRoots) return;
        roots[n++] = r;
    };
    if (extraRoot && extraRoot[0]) add(extraRoot);
    const char* env = std::getenv("JJFFE_BODYPAINT_TEX");
    if (env && env[0]) add(env);
    add("reference/ffed3d");
    add(".");
    add("..");
    add("../../jjffe/reference/ffed3d");
    add("../jjffe/reference/ffed3d");
    add("/home/alex/Src/Reverse/games/jjffe/reference/ffed3d");
    add("/home/alex/Src/Reverse/games/ffe106/FFED3D");
    // FFED3DAJ ships the higher-numbered body textures (e.g. tex760) that the
    // plain FFED3D TEXTURES directory lacks.
    add("/home/alex/Src/Reverse/games/ffe106/FFED3DAJ/ffed3d_install");
    add("../ffe106/FFED3DAJ/ffed3d_install");
    add("../../ffe106/FFED3DAJ/ffed3d_install");

#if defined(__linux__)
    char exe[PATH_MAX];
    const ssize_t nr = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (nr > 0 && nExeSlot >= 3) {
        exe[nr] = '\0';
        char* slash = std::strrchr(exe, '/');
        if (slash) *slash = '\0';
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        std::snprintf(exeRef[0], 4096, "%s/../reference/ffed3d", exe);
        std::snprintf(exeRef[1], 4096, "%s/../../jjffe/reference/ffed3d", exe);
        std::snprintf(exeRef[2], 4096, "%s/../../reference/ffed3d", exe);
#pragma GCC diagnostic pop
        add(exeRef[0]);
        add(exeRef[1]);
        add(exeRef[2]);
    }
#else
    (void)exeRef;
    (void)nExeSlot;
#endif
    return n;
}

bool BodyPaintResolveTexFile(int texId, char* out, int outCap, const char* extraRoot)
{
    if (texId <= 0) return false;

    static const char* kFmts[] = {
        "%s/Textures/tex%d.png",
        // FFED3D ships the directory as TEXTURES; keep both spellings because
        // the lookup is a plain open() on a case-sensitive filesystem.
        "%s/TEXTURES/tex%d.png",
        "%s/textures/tex%d.png",
        "%s/Textures_Hi/tex%d.png",
        "%s/tex%d.png",
    };

    char exeRef[3][4096] = {};
    const char* roots[16] = {};
    const int nRoot = CollectPaintRoots(extraRoot, roots, 16, exeRef, 3);

    for (int r = 0; r < nRoot; ++r) {
        for (const char* fmt : kFmts) {
            if (TryTexPattern(out, outCap, fmt, roots[r], texId)) return true;
        }
    }
    return false;
}

bool BodyPaintResolveCfgFile(char* out, int outCap, const char* extraRoot)
{
    char exeRef[3][4096] = {};
    const char* roots[16] = {};
    const int nRoot = CollectPaintRoots(extraRoot, roots, 16, exeRef, 3);
    if (TryPlainPath(out, outCap, TexturesCfgDefaultPath())) return true;
    char path[1024];
    for (int r = 0; r < nRoot; ++r) {
        if (!roots[r] || !roots[r][0]) continue;
        std::snprintf(path, sizeof(path), "%s/textures.cfg", roots[r]);
        if (TryPlainPath(out, outCap, path)) return true;
    }
    return false;
}

bool BodyPaintFinalizeWrap(BodyMaterial* mat, const char* extraRoot)
{
    if (!mat) return false;
    if (mat->mode != BodyPaintMode::Wrap) return true;
    if (BodyPaintResolveTexFile(mat->wrapTex, nullptr, 0, extraRoot)) return true;
    std::fprintf(stderr,
                 "BodyPaint: missing wrap tex %d for model %d → PROCEDURAL\n",
                 mat->wrapTex, mat->modelId);
    mat->mode = BodyPaintMode::Procedural;
    mat->wrapTex = 0;
    return false;
}

bool BodyPaintIsSphereId(int32_t modelId)
{
    return modelId >= 117 && modelId <= 148;
}

bool BodyPaintWantCorona()
{
    const char* e = std::getenv("AB_BODYPAINT_NO_CORONA");
    return !(e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y'));
}

int BodyPaintCoronaScale(int cfgScale)
{
    const char* e = std::getenv("AB_BODYPAINT_CORONA_SCALE");
    if (e && e[0]) {
        const int v = std::atoi(e);
        if (v > 0) return v;
    }
    return cfgScale > 0 ? cfgScale : 12;
}

bool BodyPaintWantAtmo()
{
    const char* e = std::getenv("AB_BODYPAINT_NO_ATMO");
    return !(e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y'));
}

float BodyPaintAtmoRatio(float cfgRatio)
{
    const char* e = std::getenv("AB_BODYPAINT_ATMO_RATIO");
    if (e && e[0]) {
        const float v = std::strtof(e, nullptr);
        if (v > 1.0f) return v;
    }
    return cfgRatio > 1.0f ? cfgRatio : 1.04f;
}

bool BodyPaintResolveAtmoFile(int texId, char* out, int outCap, const char* extraRoot)
{
    if (texId <= 0) return false;
    static const char* kFmts[] = {
        "%s/Textures/atmosphere%d.png",
        "%s/textures/atmosphere%d.png",
        "%s/Textures_Hi/atmosphere%d.png",
        "%s/atmosphere%d.png",
    };
    char exeRef[3][4096] = {};
    const char* roots[16] = {};
    const int nRoot = CollectPaintRoots(extraRoot, roots, 16, exeRef, 3);
    for (int r = 0; r < nRoot; ++r) {
        for (const char* fmt : kFmts) {
            if (TryTexPattern(out, outCap, fmt, roots[r], texId)) return true;
        }
    }
    return BodyPaintResolveTexFile(texId, out, outCap, extraRoot);
}

bool BodyPaintIsKnownPlanet(int32_t modelId)
{
    // Unique Sol model ids. Moon shares 120 with Mercury — not keyed here.
    return modelId == 129 || modelId == 125 || modelId == 123;
}

static float WrapLon(float d)
{
    while (d > 180.f) d -= 360.f;
    while (d < -180.f) d += 360.f;
    return d;
}

static float Hash01(float x, float y, int seed)
{
    float s = std::sin(x * 12.9898f + y * 78.233f + (float)seed * 0.137f) * 43758.5453f;
    return s - std::floor(s);
}

static float ValueNoise(float x, float y, int seed)
{
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float fx = x - ix;
    const float fy = y - iy;
    const float ux = fx * fx * (3.f - 2.f * fx);
    const float uy = fy * fy * (3.f - 2.f * fy);
    const float a = Hash01(ix, iy, seed);
    const float b = Hash01(ix + 1.f, iy, seed);
    const float c = Hash01(ix, iy + 1.f, seed);
    const float d = Hash01(ix + 1.f, iy + 1.f, seed);
    const float ab = a + (b - a) * ux;
    const float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
}

static float Fbm2(float x, float y, int seed)
{
    float a = 0.5f * ValueNoise(x, y, seed);
    a += 0.25f * ValueNoise(x * 2.03f, y * 2.03f, seed + 1);
    a += 0.125f * ValueNoise(x * 4.07f, y * 4.07f, seed + 2);
    return a * 2.f - 1.f;
}

static float LandBlob(float lat, float lon, float cLat, float cLon, float rLat, float rLon)
{
    const float dy = (lat - cLat) / rLat;
    const float dx = WrapLon(lon - cLon) / rLon;
    const float d = dx * dx + dy * dy;
    if (d >= 1.f) return 0.f;
    const float t = 1.f - d;
    return t * t;
}

static void SampleEarth(float lat, float lon, float nCoast, float nDet, float* r, float* g, float* b)
{
    float land = 0.f;
    land += LandBlob(lat, lon, 8.f, 22.f, 36.f, 20.f);      // Africa
    land += LandBlob(lat, lon, 48.f, 18.f, 20.f, 20.f);     // Europe
    land += LandBlob(lat, lon, 55.f, 90.f, 28.f, 55.f);     // Asia
    land += LandBlob(lat, lon, 22.f, 78.f, 14.f, 12.f);     // India
    land += LandBlob(lat, lon, 32.f, 108.f, 16.f, 18.f);    // China / SE
    land += LandBlob(lat, lon, 45.f, -100.f, 28.f, 40.f);   // N America
    land += LandBlob(lat, lon, 64.f, -150.f, 12.f, 22.f);   // Alaska
    land += LandBlob(lat, lon, 16.f, -90.f, 12.f, 14.f);    // Central
    land += LandBlob(lat, lon, -14.f, -58.f, 34.f, 16.f);   // S America
    land += LandBlob(lat, lon, -24.f, 134.f, 16.f, 16.f);   // Australia
    land += LandBlob(lat, lon, -42.f, 172.f, 10.f, 8.f);    // NZ
    land += LandBlob(lat, lon, 72.f, -42.f, 14.f, 16.f);    // Greenland
    land += LandBlob(lat, lon, -8.f, 118.f, 10.f, 20.f);    // Indonesia
    land += LandBlob(lat, lon, 36.f, 138.f, 8.f, 8.f);      // Japan
    land += LandBlob(lat, lon, 25.f, 45.f, 12.f, 12.f);     // Arabia
    land += LandBlob(lat, lon, -20.f, 47.f, 8.f, 6.f);      // Madagascar
    if (lat < -60.f) land = std::fmax(land, (-60.f - lat) / 28.f);
    land = std::fmin(1.f, land + 0.16f * nCoast);

    const float absLat = std::fabs(lat);
    if (land < 0.32f) {
        const float shallow = std::fmax(0.f, land / 0.32f);
        *r = 0.04f + 0.10f * shallow;
        *g = 0.16f + 0.22f * shallow;
        *b = 0.42f + 0.28f * shallow;
    } else {
        const float elev = std::fmin(1.f, (land - 0.32f) / 0.7f);
        const bool desert =
            (lat > 12.f && lat < 32.f && lon > -18.f && lon < 52.f) ||
            (lat > 22.f && lat < 42.f && lon > -118.f && lon < -102.f) ||
            (lat > 18.f && lat < 32.f && lon > 44.f && lon < 64.f) ||
            (lat > -32.f && lat < -18.f && lon > 118.f && lon < 140.f);
        if (desert) {
            *r = 0.72f + 0.08f * nDet;
            *g = 0.55f + 0.06f * nDet;
            *b = 0.28f;
        } else if (absLat > 55.f) {
            *r = 0.38f + 0.10f * elev;
            *g = 0.42f + 0.08f * elev;
            *b = 0.28f;
        } else {
            *r = 0.14f + 0.12f * elev;
            *g = 0.36f + 0.22f * elev + 0.06f * nDet;
            *b = 0.10f;
        }
        const float himalaya = LandBlob(lat, lon, 28.f, 84.f, 8.f, 16.f);
        const float rockies = LandBlob(lat, lon, 48.f, -114.f, 14.f, 10.f);
        const float andes = LandBlob(lat, lon, -20.f, -70.f, 28.f, 6.f);
        const float alpine = std::fmax(himalaya, std::fmax(rockies, andes));
        if (alpine > 0.25f) {
            const float t = std::fmin(1.f, alpine);
            *r = *r + (0.62f - *r) * t;
            *g = *g + (0.62f - *g) * t;
            *b = *b + (0.60f - *b) * t;
        }
    }
    float ice = 0.f;
    if (lat > 65.f) ice = (lat - 65.f) / 18.f;
    if (lat < -60.f) ice = std::fmax(ice, (-60.f - lat) / 28.f);
    ice = std::fmin(1.f, ice);
    if (ice > 0.f) {
        *r = *r + (0.93f - *r) * ice;
        *g = *g + (0.95f - *g) * ice;
        *b = *b + (0.98f - *b) * ice;
    }
    if (nDet > 0.55f && ice < 0.4f && land < 0.5f) {
        const float c = (nDet - 0.55f) * 1.2f;
        *r = *r + (0.95f - *r) * c * 0.45f;
        *g = *g + (0.96f - *g) * c * 0.45f;
        *b = *b + (0.98f - *b) * c * 0.45f;
    }
}

static void SampleMars(float lat, float lon, float n, float n2, float* r, float* g, float* b)
{
    *r = 0.58f + 0.16f * n;
    *g = 0.28f + 0.08f * n;
    *b = 0.14f + 0.05f * n2;
    if (lat < -10.f) {
        *r *= 0.88f;
        *g *= 0.90f;
    }
    const float valles = LandBlob(lat, lon, -10.f, -70.f, 12.f, 55.f);
    if (valles > 0.15f) {
        *r = 0.32f + 0.08f * n2;
        *g = 0.16f + 0.04f * n2;
        *b = 0.10f;
    }
    const float olympus = LandBlob(lat, lon, 18.f, -133.f, 10.f, 12.f);
    if (olympus > 0.2f) {
        *r = 0.70f + 0.08f * n;
        *g = 0.38f;
        *b = 0.20f;
    }
    if (n < -0.28f) {
        *r = 0.34f + 0.08f * n2;
        *g = 0.18f;
        *b = 0.10f;
    }
    float ice = 0.f;
    if (lat > 62.f) ice = (lat - 62.f) / 28.f;
    if (lat < -62.f) ice = std::fmax(ice, (-62.f - lat) / 28.f);
    ice = std::fmin(1.f, ice);
    if (ice > 0.f) {
        *r = *r + (0.92f - *r) * ice;
        *g = *g + (0.90f - *g) * ice;
        *b = *b + (0.88f - *b) * ice;
    }
}

static void SampleVenus(float lat, float lon, float n, float n2, float* r, float* g, float* b)
{
    const float absLat = std::fabs(lat);
    const float band = 0.5f + 0.5f * std::sin(lat * 0.065f + n * 0.4f);
    const float yFeat = LandBlob(lat, lon, 10.f, -20.f, 28.f, 70.f);
    *r = 0.82f - 0.12f * band + 0.06f * n;
    *g = 0.70f - 0.10f * band + 0.05f * n2;
    *b = 0.38f - 0.08f * band;
    if (yFeat > 0.2f) {
        *r *= 0.88f;
        *g *= 0.86f;
        *b *= 0.80f;
    }
    if (absLat > 70.f) {
        const float p = (absLat - 70.f) / 20.f;
        *r = *r + (0.90f - *r) * p;
        *g = *g + (0.82f - *g) * p;
        *b = *b + (0.55f - *b) * p;
    }
}

void BodyPaintSampleKnown(int32_t modelId, float latDeg, float lonDeg, float* r, float* g, float* b)
{
    if (!r || !g || !b) return;
    *r = *g = *b = 0.5f;
    const float n1 = Fbm2(lonDeg * 0.04f, latDeg * 0.04f, (int)modelId);
    const float n2 = Fbm2(lonDeg * 0.11f, latDeg * 0.11f, (int)modelId + 11);
    if (modelId == 129) SampleEarth(latDeg, lonDeg, n1, n2, r, g, b);
    else if (modelId == 125) SampleMars(latDeg, lonDeg, n1, n2, r, g, b);
    else if (modelId == 123) SampleVenus(latDeg, lonDeg, n1, n2, r, g, b);
}

bool BodyPaintBakeKnownEquirect(int32_t modelId, uint32_t* argb, int w, int h)
{
    if (!BodyPaintIsKnownPlanet(modelId) || !argb || w < 8 || h < 4) return false;
    for (int y = 0; y < h; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        const float lat = 90.f - v * 180.f;
        for (int x = 0; x < w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            const float lon = u * 360.f - 180.f;
            float r = 0, g = 0, b = 0;
            BodyPaintSampleKnown(modelId, lat, lon, &r, &g, &b);
            if (r < 0) r = 0;
            if (r > 1) r = 1;
            if (g < 0) g = 0;
            if (g > 1) g = 1;
            if (b < 0) b = 0;
            if (b > 1) b = 1;
            const uint32_t R = static_cast<uint32_t>(r * 255.f + 0.5f);
            const uint32_t G = static_cast<uint32_t>(g * 255.f + 0.5f);
            const uint32_t B = static_cast<uint32_t>(b * 255.f + 0.5f);
            argb[y * w + x] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
    return true;
}

static float Hash3(float x, float y, float z, int seed)
{
    const float s = static_cast<float>(seed) * 0.1031f;
    float n = std::sin(x * 127.1f + y * 311.7f + z * 74.7f + s) * 43758.5453f;
    n = n - std::floor(n);
    return n * 2.f - 1.f;
}

static float ValueNoise3(float x, float y, float z, int seed)
{
    const float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    const float fx = x - ix, fy = y - iy, fz = z - iz;
    const float u = fx * fx * (3.f - 2.f * fx);
    const float v = fy * fy * (3.f - 2.f * fy);
    const float w = fz * fz * (3.f - 2.f * fz);
    auto h = [&](int ox, int oy, int oz) {
        return Hash3(ix + static_cast<float>(ox), iy + static_cast<float>(oy),
                     iz + static_cast<float>(oz), seed);
    };
    const float n000 = h(0, 0, 0), n100 = h(1, 0, 0), n010 = h(0, 1, 0), n110 = h(1, 1, 0);
    const float n001 = h(0, 0, 1), n101 = h(1, 0, 1), n011 = h(0, 1, 1), n111 = h(1, 1, 1);
    const float nx00 = n000 + (n100 - n000) * u;
    const float nx10 = n010 + (n110 - n010) * u;
    const float nx01 = n001 + (n101 - n001) * u;
    const float nx11 = n011 + (n111 - n011) * u;
    const float nxy0 = nx00 + (nx10 - nx00) * v;
    const float nxy1 = nx01 + (nx11 - nx01) * v;
    return nxy0 + (nxy1 - nxy0) * w;
}

static float Fbm3(float x, float y, float z, int seed, int octaves)
{
    float sum = 0, amp = 0.5f, freq = 1.f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * ValueNoise3(x * freq, y * freq, z * freq, seed + i * 17);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.f;
    }
    return (norm > 0.f) ? (sum / norm) : 0.f;
}

static void ClampRgb(float* r, float* g, float* b)
{
    auto c = [](float& v) {
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
    };
    c(*r);
    c(*g);
    c(*b);
}

void BodyPaintSampleProcedural(int32_t modelId, int32_t seed, float nx, float ny, float nz,
                               float* r, float* g, float* b)
{
    if (!r || !g || !b) return;
    *r = *g = *b = 0.5f;
    const int s = seed * 131 + 7;
    const float absLat = std::fabs(ny);
    const float lon = std::atan2(nz, nx);
    const BodyProcClass cls = BodyPaintProcClass(modelId);

    switch (cls) {
    case BodyProcClass::Asteroid: {
        const float n = Fbm3(nx * 4.f, ny * 4.f, nz * 4.f, s, 4);
        const float crater = std::fmax(0.f, 0.55f - std::fabs(n));
        const float shade = 0.28f + 0.35f * (0.5f + 0.5f * n) - crater * 0.25f;
        *r = *g = *b = shade;
        if (n > 0.55f) {
            *r = 0.45f;
            *g = 0.42f;
            *b = 0.38f;
        }
        break;
    }
    case BodyProcClass::Rocky: {
        const float n = Fbm3(nx * 3.2f, ny * 3.2f, nz * 3.2f, s, 5);
        const float n2 = Fbm3(nx * 8.f, ny * 8.f, nz * 8.f, s + 3, 3);
        const bool warmDust = (modelId == 125 || modelId == 133);
        if (warmDust) {
            *r = 0.52f + 0.18f * n;
            *g = 0.30f + 0.10f * n;
            *b = 0.18f + 0.08f * n2;
            if (n > 0.25f) {
                *r = 0.42f + 0.12f * n2;
                *g = 0.34f + 0.08f * n2;
                *b = 0.28f + 0.06f * n2;
            }
        } else {
            *r = 0.42f + 0.10f * n;
            *g = 0.40f + 0.08f * n;
            *b = 0.36f + 0.10f * n2;
        }
        const float iceStart = warmDust ? 0.72f : 0.55f;
        const float ice = std::fmax(0.f, (absLat - iceStart) / (1.f - iceStart));
        if (ice > 0.f) {
            const float frost = ice * ice;
            *r = *r + (0.92f - *r) * frost;
            *g = *g + (0.94f - *g) * frost;
            *b = *b + (0.98f - *b) * frost;
        }
        break;
    }
    case BodyProcClass::OceanWorld: {
        float wx = nx + 0.35f * Fbm3(nx * 1.5f, ny * 1.5f, nz * 1.5f, s, 3);
        float wy = ny + 0.35f * Fbm3(nx * 1.5f + 20.f, ny * 1.5f, nz * 1.5f, s + 1, 3);
        float wz = nz + 0.35f * Fbm3(nx * 1.5f, ny * 1.5f + 20.f, nz * 1.5f, s + 2, 3);
        const float landNoise = Fbm3(wx * 2.8f, wy * 2.8f, wz * 2.8f, s + 9, 5);
        float seaLevel = -0.05f + 0.04f * Hash3(static_cast<float>(modelId), 1.f, 2.f, s);
        float iceStart = 0.62f + 0.02f * static_cast<float>((modelId - 122) % 5);
        float oceanR = 0.05f, oceanG = 0.18f, oceanB = 0.45f;
        float landLowR = 0.18f, landLowG = 0.42f, landLowB = 0.12f;
        if (modelId == 127) {
            seaLevel = 0.22f;
            iceStart = 0.38f;
            oceanR = 0.12f;
            oceanG = 0.28f;
            oceanB = 0.42f;
        } else if (modelId == 128) {
            oceanR = 0.18f;
            oceanG = 0.32f;
            oceanB = 0.16f;
            landLowR = 0.38f;
            landLowG = 0.28f;
            landLowB = 0.10f;
        } else if (modelId == 130 || modelId == 132) {
            seaLevel = 0.28f;
            landLowR = 0.55f;
            landLowG = 0.38f;
            landLowB = 0.16f;
            oceanR = 0.10f;
            oceanG = 0.28f;
            oceanB = 0.40f;
        } else if (modelId == 129) {
            landLowR = 0.28f;
            landLowG = 0.32f;
            landLowB = 0.12f;
        }
        const bool land = landNoise > seaLevel;
        if (!land) {
            const float shallow = std::fmax(0.f, 1.f - (seaLevel + 0.15f - landNoise) * 4.f);
            *r = oceanR + 0.10f * shallow;
            *g = oceanG + 0.25f * shallow;
            *b = oceanB + 0.35f * shallow;
        } else {
            const float elev = (landNoise - seaLevel) / (1.f - seaLevel + 1e-3f);
            if (elev < 0.35f) {
                *r = landLowR + 0.15f * elev;
                *g = landLowG + 0.20f * elev;
                *b = landLowB;
            } else if (elev < 0.7f) {
                *r = 0.35f + 0.25f * elev;
                *g = 0.32f + 0.10f * elev;
                *b = 0.14f;
            } else {
                *r = *g = *b = 0.45f + 0.25f * elev;
            }
            const float veg = Fbm3(nx * 10.f, ny * 10.f, nz * 10.f, s + 11, 2);
            if (elev < 0.4f && veg > 0.2f && modelId != 130 && modelId != 132 && modelId != 128) {
                *g = std::fmin(0.75f, *g + 0.15f);
                *r *= 0.85f;
            }
        }
        float ice = std::fmax(0.f, (absLat - iceStart) / (1.f - iceStart + 1e-3f));
        ice = ice * ice;
        if (ice > 0.f) {
            *r = *r + (0.95f - *r) * ice;
            *g = *g + (0.97f - *g) * ice;
            *b = *b + (1.00f - *b) * ice;
        }
        const float clouds = Fbm3(nx * 5.f + lon * 0.3f, ny * 2.f, nz * 5.f, s + 21, 3);
        if (clouds > 0.45f && ice < 0.5f && modelId != 130 && modelId != 132) {
            const float c = (clouds - 0.45f) / 0.55f;
            *r = *r + (0.95f - *r) * c * 0.55f;
            *g = *g + (0.95f - *g) * c * 0.55f;
            *b = *b + (0.98f - *b) * c * 0.55f;
        }
        break;
    }
    case BodyProcClass::GasGiant: {
        const float band = std::sin(ny * 14.f + 0.6f * Fbm3(nx * 2.f, ny * 2.f, nz * 2.f, s, 2));
        const float band2 = std::sin(ny * 7.f + 1.2f);
        if (modelId >= 135) {
            *r = 0.28f + 0.12f * band;
            *g = 0.48f + 0.18f * band2;
            *b = 0.72f + 0.18f * band;
        } else {
            *r = 0.62f + 0.22f * band;
            *g = 0.42f + 0.14f * band2;
            *b = 0.24f + 0.08f * band;
            const float sx = nx - 0.55f, sy = ny - 0.15f, sz = nz * 0.3f;
            const float spot = std::exp(-(sx * sx * 8.f + sy * sy * 18.f + sz * sz * 8.f));
            *r = *r + (0.85f - *r) * spot;
            *g = *g + (0.35f - *g) * spot * 0.8f;
            *b = *b * (1.f - spot * 0.5f);
        }
        const float pole = absLat * absLat * 0.25f;
        *r *= 1.f - pole;
        *g *= 1.f - pole;
        *b *= 1.f - pole;
        break;
    }
    case BodyProcClass::Star: {
        const float gran = 0.5f + 0.5f * Fbm3(nx * 12.f, ny * 12.f, nz * 12.f, s, 3);
        if (modelId >= 140) {
            *r = 0.55f + 0.35f * gran;
            *g = 0.70f + 0.25f * gran;
            *b = 1.00f;
        } else if (modelId >= 137) {
            *r = 1.00f;
            *g = 0.95f + 0.05f * gran;
            *b = 0.75f + 0.15f * gran;
        } else {
            *r = 1.00f;
            *g = 0.55f + 0.25f * gran;
            *b = 0.25f + 0.10f * gran;
        }
        break;
    }
    }
    ClampRgb(r, g, b);
}

bool BodyPaintBakeProceduralEquirect(int32_t modelId, int32_t seed, uint32_t* argb, int w, int h)
{
    if (!argb || w < 8 || h < 4) return false;
    for (int y = 0; y < h; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        const float phi = v * 3.14159265f;
        const float ny = std::cos(phi);
        const float rr = std::sin(phi);
        for (int x = 0; x < w; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            const float theta = u * 6.2831853f;
            const float nx = rr * std::cos(theta);
            const float nz = rr * std::sin(theta);
            float r = 0, g = 0, b = 0;
            BodyPaintSampleProcedural(modelId, seed, nx, ny, nz, &r, &g, &b);
            const uint32_t R = static_cast<uint32_t>(r * 255.f + 0.5f);
            const uint32_t G = static_cast<uint32_t>(g * 255.f + 0.5f);
            const uint32_t B = static_cast<uint32_t>(b * 255.f + 0.5f);
            argb[y * w + x] = 0xFF000000u | (R << 16) | (G << 8) | B;
        }
    }
    return true;
}

} // namespace jjffe
