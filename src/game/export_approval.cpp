#include "jjffe/export_approval.h"

#include "jjffe/sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace jjffe {

namespace {

bool EnsureDir(const char* path)
{
    if (!path || !path[0]) return false;
    struct stat st {};
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

void FeedU32(Sha256* ctx, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v), (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 24)};
    Sha256Update(ctx, b, 4);
}

void FeedI32(Sha256* ctx, int32_t v) { FeedU32(ctx, (uint32_t)v); }

void FeedF32(Sha256* ctx, float v)
{
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    FeedU32(ctx, u);
}

void FeedStr(Sha256* ctx, const std::string& s)
{
    FeedU32(ctx, (uint32_t)s.size());
    if (!s.empty()) Sha256Update(ctx, s.data(), s.size());
}

bool FileExists(const char* path)
{
    struct stat st {};
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

std::string JsonEscape(const std::string& in)
{
    std::string o;
    o.reserve(in.size() + 8);
    for (char c : in) {
        if (c == '"' || c == '\\') {
            o.push_back('\\');
            o.push_back(c);
        } else if (c == '\n') {
            o += "\\n";
        } else {
            o.push_back(c);
        }
    }
    return o;
}

const char* ClassForFinding(FindingType t)
{
    switch (t) {
    case FindingType::CoplanarOverlay: return "OverlayCoplanar";
    case FindingType::PunchDepthRisk: return "PunchDepthBias";
    case FindingType::EmbeddedSurface: return "EmbeddedReview";
    case FindingType::NonplanarContour: return "NonplanarContour";
    case FindingType::Sliver: return "SliverReview";
    default: return "Review";
    }
}

int LayerForFinding(FindingType t)
{
    switch (t) {
    case FindingType::CoplanarOverlay: return 3;
    case FindingType::PunchDepthRisk: return 2;
    case FindingType::EmbeddedSurface: return 4;
    default: return 1;
    }
}

// Minimal JSON string / array extractors (approval sidecars are ours).
bool ExtractJsonString(const std::string& json, const char* key, std::string* out)
{
    if (!out) return false;
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p = json.find('"', p + 1);
    if (p == std::string::npos) return false;
    size_t e = p + 1;
    while (e < json.size()) {
        if (json[e] == '\\' && e + 1 < json.size()) {
            e += 2;
            continue;
        }
        if (json[e] == '"') break;
        e++;
    }
    if (e >= json.size()) return false;
    *out = json.substr(p + 1, e - p - 1);
    return true;
}

bool ExtractJsonInt(const std::string& json, const char* key, int* out)
{
    if (!out) return false;
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
    *out = std::atoi(json.c_str() + p);
    return true;
}

bool ExtractJsonFloat(const std::string& json, const char* key, float* out)
{
    if (!out) return false;
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
    *out = (float)std::atof(json.c_str() + p);
    return true;
}

std::vector<std::string> ExtractJsonStringArray(const std::string& json,
                                                const char* key)
{
    std::vector<std::string> out;
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return out;
    p = json.find('[', p);
    if (p == std::string::npos) return out;
    size_t e = json.find(']', p);
    if (e == std::string::npos) return out;
    const std::string arr = json.substr(p + 1, e - p - 1);
    size_t i = 0;
    while (i < arr.size()) {
        while (i < arr.size() && (arr[i] == ' ' || arr[i] == ',' || arr[i] == '\n' ||
                                  arr[i] == '\r' || arr[i] == '\t'))
            i++;
        if (i >= arr.size() || arr[i] != '"') break;
        size_t s = ++i;
        while (i < arr.size() && arr[i] != '"') {
            if (arr[i] == '\\') i++;
            i++;
        }
        out.push_back(arr.substr(s, i - s));
        if (i < arr.size()) i++;
    }
    return out;
}

bool CopyFile(const char* src, const char* dst)
{
    FILE* in = std::fopen(src, "rb");
    if (!in) return false;
    FILE* out = std::fopen(dst, "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    uint8_t buf[8192];
    bool ok = true;
    while (true) {
        const size_t n = std::fread(buf, 1, sizeof(buf), in);
        if (n && std::fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
        if (n < sizeof(buf)) break;
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    std::fclose(out);
    return ok;
}

}  // namespace

std::string ComputeShipExportAssetHash(const ShipExportMesh& mesh)
{
    Sha256 ctx;
    Sha256Init(&ctx);
    const char magic[] = "JJFFE_EXPORT_ASSET_V1";
    Sha256Update(&ctx, magic, sizeof(magic) - 1);
    FeedI32(&ctx, mesh.shipIndex);
    FeedI32(&ctx, (int32_t)mesh.shipDist);
    FeedU32(&ctx, (uint32_t)mesh.verts.size());
    for (const ShipExportVert& v : mesh.verts) {
        FeedF32(&ctx, v.x);
        FeedF32(&ctx, v.y);
        FeedF32(&ctx, v.z);
        FeedF32(&ctx, v.u);
        FeedF32(&ctx, v.v);
    }
    FeedU32(&ctx, (uint32_t)mesh.indices.size());
    for (uint32_t ix : mesh.indices) FeedU32(&ctx, ix);
    FeedU32(&ctx, (uint32_t)mesh.triMeta.size());
    for (const ShipExportTriMeta& m : mesh.triMeta) {
        FeedU32(&ctx, m.primitiveId);
        FeedI32(&ctx, m.sourceOrder);
        FeedU32(&ctx, m.reverseSide ? 1u : 0u);
    }
    FeedU32(&ctx, (uint32_t)mesh.materials.size());
    for (const ShipExportMaterial& m : mesh.materials) {
        FeedStr(&ctx, m.name);
        FeedU32(&ctx, m.punch ? 1u : 0u);
        FeedU32(&ctx, m.decal ? 1u : 0u);
        FeedU32(&ctx, m.kdArgb);
    }
    FeedU32(&ctx, (uint32_t)mesh.emitters.size());
    for (const ShipExportEmitter& e : mesh.emitters) {
        FeedU32(&ctx, (uint32_t)e.kind);
        FeedF32(&ctx, e.x);
        FeedF32(&ctx, e.y);
        FeedF32(&ctx, e.z);
        FeedF32(&ctx, e.size);
    }
    uint8_t dig[32];
    Sha256Final(&ctx, dig);
    return std::string("sha256:") + Sha256Hex(dig);
}

std::string ComputeExportDirAssetHash(const char* exportShipDir)
{
    if (!exportShipDir) return {};
    char glb[1200];
    std::snprintf(glb, sizeof(glb), "%s/model.glb", exportShipDir);
    if (FileExists(glb)) {
        const std::string h = Sha256FilePrefixed(glb);
        if (!h.empty()) return h;
    }
    // Fallback: length-prefixed concat of stable text assets.
    const char* names[] = {"model.obj", "primitives.json", "emitters.json",
                           "materials.json"};
    Sha256 ctx;
    Sha256Init(&ctx);
    const char magic[] = "JJFFE_EXPORT_DIR_V1";
    Sha256Update(&ctx, magic, sizeof(magic) - 1);
    bool any = false;
    for (const char* name : names) {
        char path[1200];
        std::snprintf(path, sizeof(path), "%s/%s", exportShipDir, name);
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        any = true;
        FeedStr(&ctx, name);
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        FeedU32(&ctx, (uint32_t)std::max(0L, sz));
        uint8_t buf[8192];
        while (true) {
            const size_t n = std::fread(buf, 1, sizeof(buf), f);
            if (n) Sha256Update(&ctx, buf, n);
            if (n < sizeof(buf)) break;
        }
        std::fclose(f);
    }
    if (!any) return {};
    uint8_t dig[32];
    Sha256Final(&ctx, dig);
    return std::string("sha256:") + Sha256Hex(dig);
}

bool WriteExportShipManifest(const ShipExportMesh& mesh, const char* dir,
                             const char* sourceAssetHash)
{
    if (!mesh.valid || !dir) return false;
    if (!EnsureDir(dir)) return false;
    char path[1200];
    std::snprintf(path, sizeof(path), "%s/manifest.json", dir);
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    const std::string hash =
        sourceAssetHash && sourceAssetHash[0]
            ? std::string(sourceAssetHash)
            : ComputeShipExportAssetHash(mesh);
    fprintf(f,
            "{\n"
            "  \"shipIndex\": %d,\n"
            "  \"name\": \"%s\",\n"
            "  \"policyVersion\": %d,\n"
            "  \"sourceAssetHash\": \"%s\",\n"
            "  \"triangleCount\": %d,\n"
            "  \"materialCount\": %d,\n"
            "  \"emitterCount\": %d,\n"
            "  \"hullTriCount\": %d,\n"
            "  \"punchTriCount\": %d,\n"
            "  \"decalTriCount\": %d,\n"
            "  \"lineTriCount\": %d,\n"
            "  \"contourCurveCount\": %d\n"
            "}\n",
            mesh.shipIndex, mesh.name.c_str(), kRenderPolicyVersion, hash.c_str(),
            (int)(mesh.indices.size() / 3), (int)mesh.materials.size(),
            (int)mesh.emitters.size(), mesh.hullTriCount, mesh.punchTriCount,
            mesh.decalTriCount, mesh.lineTriCount, mesh.contourCurveCount);
    std::fclose(f);
    return true;
}

bool WriteExportMaterialsJson(const ShipExportMesh& mesh, const char* dir)
{
    if (!mesh.valid || !dir) return false;
    if (!EnsureDir(dir)) return false;
    char path[1200];
    std::snprintf(path, sizeof(path), "%s/materials.json", dir);
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n  \"shipIndex\": %d,\n  \"materials\": [\n", mesh.shipIndex);
    for (size_t i = 0; i < mesh.materials.size(); i++) {
        const ShipExportMaterial& m = mesh.materials[i];
        fprintf(f,
                "    {\"name\": \"%s\", \"punch\": %s, \"decal\": %s, "
                "\"textured\": %s, \"kdArgb\": %u}%s\n",
                JsonEscape(m.name).c_str(), m.punch ? "true" : "false",
                m.decal ? "true" : "false", m.textured ? "true" : "false",
                m.kdArgb, (i + 1 < mesh.materials.size()) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    std::fclose(f);
    return true;
}

void CollectExportPrimitiveIds(const ShipExportMesh& mesh,
                               std::unordered_set<uint32_t>* out)
{
    if (!out) return;
    out->clear();
    for (const ShipExportTriMeta& m : mesh.triMeta) out->insert(m.primitiveId);
}

bool BuildApprovalSidecars(const ShipAnalysis& analysis,
                           const std::string& sourceAssetHash,
                           const char* approvedBy, RenderPolicySidecar* policy,
                           RemediationSidecar* rem)
{
    if (!policy || !rem || !analysis.valid) return false;
    *policy = RenderPolicySidecar{};
    *rem = RemediationSidecar{};
    policy->sourceAssetHash = sourceAssetHash;
    policy->policyVersion = kRenderPolicyVersion;
    policy->approvedBy = approvedBy ? approvedBy : "ci";
    rem->sourceAssetHash = sourceAssetHash;
    rem->policyVersion = kRenderPolicyVersion;
    rem->approvedBy = policy->approvedBy;

    for (const Finding& f : analysis.findings) {
        // Only actionable render/geometry hints become policy classes.
        if (f.hint != RemediationHint::RenderSidecar &&
            f.hint != RemediationHint::GeometryCow &&
            f.hint != RemediationHint::ContourFix &&
            f.hint != RemediationHint::Review)
            continue;
        if (f.id.empty()) continue;
        policy->createdFromFindings.push_back(f.id);
        rem->createdFromFindings.push_back(f.id);
        rem->remediations[f.id] = RemediationHintName(f.hint);

        if (f.a == 0 && f.type != FindingType::NonplanarContour) continue;
        RenderClassEntry e;
        e.cls = ClassForFinding(f.type);
        e.receiver = f.b;
        e.layer = LayerForFinding(f.type);
        e.biasConstant = (f.type == FindingType::CoplanarOverlay ||
                          f.type == FindingType::PunchDepthRisk)
                             ? kShipExportPainterDepthBias
                             : 0.f;
        e.biasSlope = (f.type == FindingType::CoplanarOverlay ||
                       f.type == FindingType::PunchDepthRisk)
                          ? kShipExportPainterSlopeScale
                          : 0.f;
        e.depthWrite = false;
        if (f.a != 0) policy->renderClasses[f.a] = e;
    }
    policy->valid = !policy->createdFromFindings.empty();
    rem->valid = policy->valid;
    if (!policy->valid) {
        policy->error = "no findings to approve";
        rem->error = policy->error;
        return false;
    }
    return true;
}

bool WriteRenderPolicyJson(const RenderPolicySidecar& p, const char* path)
{
    if (!path) return false;
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    fprintf(f,
            "{\n  \"sourceAssetHash\": \"%s\",\n  \"policyVersion\": %d,\n"
            "  \"approvedBy\": \"%s\",\n  \"punchAftNudgeObj\": %.6g,\n"
            "  \"createdFromFindings\": [",
            JsonEscape(p.sourceAssetHash).c_str(), p.policyVersion,
            JsonEscape(p.approvedBy).c_str(), p.punchAftNudgeObj);
    for (size_t i = 0; i < p.createdFromFindings.size(); i++) {
        fprintf(f, "%s\"%s\"", i ? ", " : "",
                JsonEscape(p.createdFromFindings[i]).c_str());
    }
    fprintf(f, "],\n  \"renderClasses\": {\n");
    size_t n = 0;
    const size_t total = p.renderClasses.size();
    for (const auto& kv : p.renderClasses) {
        const RenderClassEntry& e = kv.second;
        fprintf(f,
                "    \"primitive:%u\": {\"class\": \"%s\", \"receiver\": "
                "\"primitive:%u\", \"layer\": %d, \"depthBias\": {\"constant\": "
                "%.6g, \"slope\": %.6g}, \"depthWrite\": %s}%s\n",
                kv.first, JsonEscape(e.cls).c_str(), e.receiver, e.layer,
                e.biasConstant, e.biasSlope, e.depthWrite ? "true" : "false",
                (++n < total) ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    std::fclose(f);
    return true;
}

bool WriteRemediationSidecarJson(const RemediationSidecar& r, const char* path)
{
    if (!path) return false;
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    fprintf(f,
            "{\n  \"sourceAssetHash\": \"%s\",\n  \"policyVersion\": %d,\n"
            "  \"approvedBy\": \"%s\",\n  \"createdFromFindings\": [",
            JsonEscape(r.sourceAssetHash).c_str(), r.policyVersion,
            JsonEscape(r.approvedBy).c_str());
    for (size_t i = 0; i < r.createdFromFindings.size(); i++) {
        fprintf(f, "%s\"%s\"", i ? ", " : "",
                JsonEscape(r.createdFromFindings[i]).c_str());
    }
    fprintf(f, "],\n  \"remediations\": {\n");
    size_t n = 0;
    const size_t total = r.remediations.size();
    for (const auto& kv : r.remediations) {
        fprintf(f, "    \"%s\": \"%s\"%s\n", JsonEscape(kv.first).c_str(),
                JsonEscape(kv.second).c_str(), (++n < total) ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    std::fclose(f);
    return true;
}

bool LoadRenderPolicyJson(const char* path, RenderPolicySidecar* out)
{
    if (!out || !path) return false;
    *out = RenderPolicySidecar{};
    out->path = path;
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        out->error = "open failed";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 2 || sz > 8 * 1024 * 1024) {
        std::fclose(f);
        out->error = "bad size";
        return false;
    }
    std::string json((size_t)sz, '\0');
    if (std::fread(&json[0], 1, (size_t)sz, f) != (size_t)sz) {
        std::fclose(f);
        out->error = "read failed";
        return false;
    }
    std::fclose(f);

    if (!ExtractJsonString(json, "sourceAssetHash", &out->sourceAssetHash) ||
        out->sourceAssetHash.empty()) {
        out->error = "missing sourceAssetHash";
        return false;
    }
    ExtractJsonInt(json, "policyVersion", &out->policyVersion);
    ExtractJsonString(json, "approvedBy", &out->approvedBy);
    ExtractJsonFloat(json, "punchAftNudgeObj", &out->punchAftNudgeObj);
    out->createdFromFindings = ExtractJsonStringArray(json, "createdFromFindings");

    // Parse renderClasses keys "primitive:N"
    size_t rc = json.find("\"renderClasses\"");
    if (rc != std::string::npos) {
        size_t brace = json.find('{', rc);
        size_t end = brace;
        int depth = 0;
        for (size_t i = brace; i < json.size(); i++) {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') {
                depth--;
                if (depth == 0) {
                    end = i;
                    break;
                }
            }
        }
        if (brace != std::string::npos && end > brace) {
            const std::string body = json.substr(brace + 1, end - brace - 1);
            size_t i = 0;
            while (i < body.size()) {
                size_t k = body.find("\"primitive:", i);
                if (k == std::string::npos) break;
                k += 11;
                uint32_t prim = (uint32_t)std::strtoul(body.c_str() + k, nullptr, 10);
                size_t obj = body.find('{', k);
                if (obj == std::string::npos) break;
                size_t objEnd = obj;
                int d = 0;
                for (size_t j = obj; j < body.size(); j++) {
                    if (body[j] == '{') d++;
                    else if (body[j] == '}') {
                        d--;
                        if (d == 0) {
                            objEnd = j;
                            break;
                        }
                    }
                }
                const std::string entry = body.substr(obj, objEnd - obj + 1);
                RenderClassEntry e;
                ExtractJsonString(entry, "class", &e.cls);
                std::string recv;
                if (ExtractJsonString(entry, "receiver", &recv)) {
                    if (recv.rfind("primitive:", 0) == 0)
                        e.receiver = (uint32_t)std::strtoul(recv.c_str() + 10,
                                                            nullptr, 10);
                }
                ExtractJsonInt(entry, "layer", &e.layer);
                // depthBias { constant, slope } — nested object, search locally.
                ExtractJsonFloat(entry, "constant", &e.biasConstant);
                ExtractJsonFloat(entry, "slope", &e.biasSlope);
                // depthWrite
                if (entry.find("\"depthWrite\": true") != std::string::npos ||
                    entry.find("\"depthWrite\":true") != std::string::npos)
                    e.depthWrite = true;
                out->renderClasses[prim] = e;
                i = objEnd + 1;
            }
        }
    }

    out->valid = true;
    return true;
}

ApprovalValidateResult ValidateRenderPolicy(
    const RenderPolicySidecar& policy, const std::string& currentAssetHash,
    const std::unordered_set<uint32_t>& knownPrims)
{
    ApprovalValidateResult r;
    if (!policy.valid && policy.error.empty() == false &&
        policy.sourceAssetHash.empty()) {
        r.code = ApprovalValidateResult::Code::Parse;
        r.error = policy.error.empty() ? "invalid policy" : policy.error;
        return r;
    }
    if (policy.sourceAssetHash.empty()) {
        r.code = ApprovalValidateResult::Code::MissingField;
        r.error = "missing sourceAssetHash";
        return r;
    }
    if (policy.createdFromFindings.empty()) {
        r.code = ApprovalValidateResult::Code::NoFindings;
        r.error = "createdFromFindings empty";
        return r;
    }
    if (policy.approvedBy.empty()) {
        r.code = ApprovalValidateResult::Code::MissingField;
        r.error = "missing approvedBy";
        return r;
    }
    if (!currentAssetHash.empty() && policy.sourceAssetHash != currentAssetHash) {
        r.code = ApprovalValidateResult::Code::HashMismatch;
        r.error = "sourceAssetHash mismatch: sidecar=" + policy.sourceAssetHash +
                  " current=" + currentAssetHash;
        return r;
    }
    for (const auto& kv : policy.renderClasses) {
        if (knownPrims.find(kv.first) == knownPrims.end()) {
            r.code = ApprovalValidateResult::Code::UnknownPrimitive;
            r.error = "unknown primitive id " + std::to_string(kv.first);
            return r;
        }
        if (kv.second.receiver != 0 &&
            knownPrims.find(kv.second.receiver) == knownPrims.end()) {
            r.code = ApprovalValidateResult::Code::UnknownPrimitive;
            r.error = "unknown receiver primitive id " +
                      std::to_string(kv.second.receiver);
            return r;
        }
    }
    r.ok = true;
    r.code = ApprovalValidateResult::Code::Ok;
    return r;
}

bool ApproveShipAnalysis(const ShipAnalysis& analysis, const char* exportShipDir,
                         const char* approvedDir, const char* approvedBy,
                         bool writeFixedGlb, std::string* errOut)
{
    auto fail = [&](const char* msg) {
        if (errOut) *errOut = msg;
        return false;
    };
    if (!analysis.valid) return fail("invalid analysis");
    if (!approvedDir) return fail("no approved dir");
    if (!EnsureDir(approvedDir)) return fail("mkdir approved");

    std::string hash;
    ShipExportMesh mesh;
    bool haveMesh = BuildExportMesh(analysis.shipIndex, &mesh) && mesh.valid;
    if (exportShipDir && exportShipDir[0]) {
        hash = ComputeExportDirAssetHash(exportShipDir);
        if (hash.empty() && haveMesh) hash = ComputeShipExportAssetHash(mesh);
        if (haveMesh) {
            WriteExportShipManifest(mesh, exportShipDir, hash.c_str());
            WriteExportMaterialsJson(mesh, exportShipDir);
        }
    } else if (haveMesh) {
        hash = ComputeShipExportAssetHash(mesh);
    } else {
        return fail("no mesh/export for hash");
    }

    RenderPolicySidecar policy;
    RemediationSidecar rem;
    if (!BuildApprovalSidecars(analysis, hash, approvedBy, &policy, &rem))
        return fail(policy.error.empty() ? "build sidecars failed"
                                         : policy.error.c_str());

    if (haveMesh) {
        std::unordered_set<uint32_t> prims;
        CollectExportPrimitiveIds(mesh, &prims);
        const auto v = ValidateRenderPolicy(policy, hash, prims);
        if (!v.ok) return fail(v.error.c_str());
    }

    char base[512];
    std::snprintf(base, sizeof(base), "%03d_%s", analysis.shipIndex,
                  analysis.name.c_str());
    char polPath[1200], remPath[1200];
    std::snprintf(polPath, sizeof(polPath), "%s/%s.render_policy.json",
                  approvedDir, base);
    std::snprintf(remPath, sizeof(remPath), "%s/%s.remediation.sidecar.json",
                  approvedDir, base);
    // Preserve viewer-tuned punchAftNudgeObj across re-approve (gap is
    // ship-specific and not derived from analysis findings).
    {
        RenderPolicySidecar prev;
        if (LoadRenderPolicyJson(polPath, &prev) &&
            prev.punchAftNudgeObj != 0.f)
            policy.punchAftNudgeObj = prev.punchAftNudgeObj;
    }
    if (!WriteRenderPolicyJson(policy, polPath)) return fail("write policy");
    if (!WriteRemediationSidecarJson(rem, remPath)) return fail("write rem");

    if (writeFixedGlb && exportShipDir) {
        char src[1200], dst[1200];
        std::snprintf(src, sizeof(src), "%s/model.glb", exportShipDir);
        std::snprintf(dst, sizeof(dst), "%s/%s.fixed.glb", approvedDir, base);
        if (FileExists(src) && !CopyFile(src, dst))
            return fail("cow geometry copy failed");
    }
    return true;
}

int ExportApprovalCheckGates()
{
    // Self-contained: synthetic mesh hash + crafted sidecars.
    ShipExportMesh mesh;
    mesh.valid = true;
    mesh.shipIndex = 27;
    mesh.name = "krait";
    mesh.shipDist = 100;
    ShipExportVert v{};
    v.x = 1.f;
    v.y = 2.f;
    v.z = 3.f;
    mesh.verts.push_back(v);
    mesh.verts.push_back(v);
    mesh.verts.push_back(v);
    mesh.indices = {0, 1, 2};
    ShipExportTriMeta tm;
    tm.primitiveId = 0x100;
    tm.sourceOrder = 1;
    mesh.triMeta.push_back(tm);
    ShipExportMaterial mat;
    mat.name = "hull";
    mesh.materials.push_back(mat);

    const std::string hash = ComputeShipExportAssetHash(mesh);
    if (hash.size() < 10 || hash.rfind("sha256:", 0) != 0) return 1;

    std::unordered_set<uint32_t> prims;
    CollectExportPrimitiveIds(mesh, &prims);

    RenderPolicySidecar good;
    good.valid = true;
    good.sourceAssetHash = hash;
    good.approvedBy = "ctest";
    good.createdFromFindings = {"finding-0001"};
    RenderClassEntry e;
    e.cls = "OverlayCoplanar";
    e.receiver = 0x100;
    e.layer = 3;
    good.renderClasses[0x100] = e;
    if (!ValidateRenderPolicy(good, hash, prims).ok) return 2;

    // Hash mismatch must fail.
    {
        RenderPolicySidecar bad = good;
        bad.sourceAssetHash = "sha256:deadbeef";
        const auto v = ValidateRenderPolicy(bad, hash, prims);
        if (v.ok || v.code != ApprovalValidateResult::Code::HashMismatch)
            return 3;
    }
    // Unknown primitive must fail.
    {
        RenderPolicySidecar bad = good;
        bad.renderClasses.clear();
        RenderClassEntry x = e;
        bad.renderClasses[0xDEAD] = x;
        const auto v = ValidateRenderPolicy(bad, hash, prims);
        if (v.ok || v.code != ApprovalValidateResult::Code::UnknownPrimitive)
            return 4;
    }
    // Empty createdFromFindings must fail.
    {
        RenderPolicySidecar bad = good;
        bad.createdFromFindings.clear();
        const auto v = ValidateRenderPolicy(bad, hash, prims);
        if (v.ok || v.code != ApprovalValidateResult::Code::NoFindings) return 5;
    }

    // Round-trip write/load.
    const char* dir = "analysis";
    EnsureDir(dir);
    char path[256];
    std::snprintf(path, sizeof(path), "%s/_w4_policy_roundtrip.json", dir);
    if (!WriteRenderPolicyJson(good, path)) return 6;
    RenderPolicySidecar loaded;
    if (!LoadRenderPolicyJson(path, &loaded)) return 7;
    if (loaded.sourceAssetHash != hash) return 8;
    if (loaded.createdFromFindings.empty()) return 9;
    if (loaded.renderClasses.find(0x100) == loaded.renderClasses.end()) return 10;
    // Optional punchAftNudgeObj round-trip (default 0 when absent).
    good.punchAftNudgeObj = 1.09375f;
    if (!WriteRenderPolicyJson(good, path)) return 13;
    if (!LoadRenderPolicyJson(path, &loaded)) return 14;
    if (std::fabs(loaded.punchAftNudgeObj - 1.09375f) > 1e-4f) return 15;
    std::remove(path);

    // Matching hash with empty current check skipped still requires findings.
    loaded.punchAftNudgeObj = 0.f;
    if (!ValidateRenderPolicy(loaded, hash, prims).ok) return 11;

    // Determinism: hash twice.
    if (ComputeShipExportAssetHash(mesh) != hash) return 12;

    return 0;
}

}  // namespace jjffe
