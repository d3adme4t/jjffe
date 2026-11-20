#include "jjffe/ship_face_semantic.h"

#include "jjffe/ab_auto_zoom.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/ship_export.h"
#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace jjffe {

const char* FaceSemanticName(FaceSemantic s)
{
    switch (s) {
    case FaceSemantic::Hull: return "Hull";
    case FaceSemantic::Overlay: return "Overlay";
    case FaceSemantic::Punch: return "Punch";
    case FaceSemantic::MissileHull: return "MissileHull";
    case FaceSemantic::Line: return "Line";
    case FaceSemantic::Contour: return "Contour";
    case FaceSemantic::SpriteChild: return "SpriteChild";
    case FaceSemantic::ScreenHybrid: return "ScreenHybrid";
    case FaceSemantic::EngineCap: return "EngineCap";
    case FaceSemantic::Window: return "Window";
    default: return "Unknown";
    }
}

const char* FaceExportBucketName(FaceSemantic s)
{
    switch (s) {
    case FaceSemantic::Hull:
    case FaceSemantic::MissileHull:
        return "Hull";
    case FaceSemantic::Overlay:
        return "Decal";
    case FaceSemantic::Punch:
        return "Punch";
    case FaceSemantic::Line:
        return "Line";
    case FaceSemantic::Contour:
        return "Contour";
    case FaceSemantic::SpriteChild:
        return "Sprite";
    case FaceSemantic::ScreenHybrid:
        return "Skip";
    default:
        return "Skip";
    }
}

const char* MeshPolyTypeName(MeshPolyType t)
{
    switch (t) {
    case MeshPolyType::PlainLine: return "PlainLine";
    case MeshPolyType::TexTri: return "TexTri";
    case MeshPolyType::TexQuad: return "TexQuad";
    case MeshPolyType::FlatQuad: return "FlatQuad";
    case MeshPolyType::ComplexFill: return "ComplexFill";
    case MeshPolyType::TexTriPair: return "TexTriPair";
    case MeshPolyType::TexQuadPair: return "TexQuadPair";
    case MeshPolyType::Thruster: return "Thruster";
    case MeshPolyType::SpecialPoly: return "SpecialPoly";
    case MeshPolyType::ControlSeq: return "ControlSeq";
    case MeshPolyType::Sprite: return "Sprite";
    case MeshPolyType::DirLine: return "DirLine";
    case MeshPolyType::GlowPoints: return "GlowPoints";
    case MeshPolyType::NavLight: return "NavLight";
    case MeshPolyType::TextPrimitive: return "TextPrimitive";
    case MeshPolyType::EndMarker: return "EndMarker";
    default: return "Other";
    }
}

bool FaceSemanticMissileFlat(const TransformedPoly* polys, int nPoly)
{
    if (!polys || nPoly <= 0) return false;
    int nHull = 0, n1000 = 0, n0 = 0, n5 = 0, nOther = 0;
    for (int i = 0; i < nPoly; i++) {
        if (polys[i].vertexCount < 3) continue;
        nHull++;
        const uint16_t t = polys[i].texInfo;
        if (t == 0)
            n0++;
        else if (t == 0x1000)
            n1000++;
        else if (t == 0x2000 || t == 0x3000 || t == 0x4029 || t == 0x20ee)
            ;
        else if ((t & 0xF000) == 0x5000)
            n5++;
        else
            nOther++;
    }
    return nHull > 0 && n1000 > 0 && nOther == 0 && n5 == 0 && n0 == 0;
}

bool FaceSemanticCapsuleSilhouette(const TransformedPoly* polys, int nPoly)
{
    if (!polys || nPoly <= 0) return false;
    int nHull = 0, n0 = 0, n5 = 0, n1000 = 0, nOther = 0;
    for (int i = 0; i < nPoly; i++) {
        if (polys[i].vertexCount < 3) continue;
        nHull++;
        const uint16_t t = polys[i].texInfo;
        if (t == 0)
            n0++;
        else if (t == 0x1000)
            n1000++;
        else if ((t & 0xF000) == 0x5000)
            n5++;
        else if (t == 0x2000 || t == 0x3000 || t == 0x4029 || t == 0x20ee)
            ;
        else
            nOther++;
    }
    return nHull > 0 && nHull <= 8 && n0 > 0 && n5 > 0 && nOther == 0 &&
           n1000 == 0;
}

void ClassifyFaceParticipation(const WorldFace& f, const TransformedPoly* src,
                               bool missileFlat, bool capsule,
                               bool* submittedSoft, bool* culledSoft,
                               bool* submittedOgre, bool* culledOgre)
{
    const bool vis = src ? src->visible : false;
    bool back = src ? src->backFacing : f.backFacing;
    const uint16_t ti = src ? src->texInfo : f.texInfo;
    if (capsule && (ti & 0xF000) == 0x5000)
        back = !back;
    // Soft: only missile 0x1000 ignores FUNC_001481. Ogre: all faces on a
    // missile-flat mesh skip the backface continue.
    bool softBack = back;
    if (missileFlat && ti == 0x1000)
        softBack = false;
    const bool soft = vis && !softBack;
    if (submittedSoft) *submittedSoft = soft;
    if (culledSoft) *culledSoft = !soft;

    const bool ogreBuilt = vis;  // BuildWorldShipFaces includeHidden=false
    const bool ogreDoor =
        ogreBuilt && f.worldValid && f.vertexCount >= 2 &&
        f.kind != WorldFaceKind::ScreenHybrid &&
        !(f.backFacing && !missileFlat);
    if (submittedOgre) *submittedOgre = ogreDoor;
    if (culledOgre) *culledOgre = !ogreDoor;
}

FaceSemantic ClassifyFaceSemantic(const WorldFace& f, bool missileFlat)
{
    if (f.kind == WorldFaceKind::ScreenHybrid) {
        if (f.type == MeshPolyType::ComplexFill && f.contourId >= 0)
            return FaceSemantic::Contour;
        return FaceSemantic::ScreenHybrid;
    }
    if (f.kind == WorldFaceKind::ContourHull)
        return FaceSemantic::Contour;
    if (f.kind == WorldFaceKind::Punch)
        return FaceSemantic::Punch;
    if (f.kind == WorldFaceKind::Hull && f.vertexCount == 2)
        return FaceSemantic::Line;
    if (f.texInfo == 0x1000 && f.vertexCount >= 3) {
        if (missileFlat) return FaceSemantic::MissileHull;
        return FaceSemantic::Overlay;
    }
    if (f.kind == WorldFaceKind::Hull)
        return FaceSemantic::Hull;
    return FaceSemantic::Unknown;
}

bool FaceExportTriDegenerate(float ax, float ay, float az,
                             float bx, float by, float bz,
                             float cx, float cy, float cz)
{
    const float ux = bx - ax, uy = by - ay, uz = bz - az;
    const float vx = cx - ax, vy = cy - ay, vz = cz - az;
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    return (nx * nx + ny * ny + nz * nz) < 1e-16f;
}

bool FaceExportTriSliver(float ax, float ay, float az,
                         float bx, float by, float bz,
                         float cx, float cy, float cz)
{
    const float e0 = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay) +
                               (bz - az) * (bz - az));
    const float e1 = std::sqrt((cx - bx) * (cx - bx) + (cy - by) * (cy - by) +
                               (cz - bz) * (cz - bz));
    const float e2 = std::sqrt((ax - cx) * (ax - cx) + (ay - cy) * (ay - cy) +
                               (az - cz) * (az - cz));
    const float mx = std::max(e0, std::max(e1, e2));
    const float mn = std::min(e0, std::min(e1, e2));
    if (mx > 0.50f) return true;
    return mn > 1e-8f && mx / mn > 12.f;
}

bool CollectShipFaceCensus(int shipIndex, std::vector<FaceCensusRow>* out,
                           bool* missileFlat, std::string* error)
{
    if (!out) return false;
    out->clear();
    if (missileFlat) *missileFlat = false;

    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) {
        if (error) *error = "missing model";
        return false;
    }
    const uint8_t* meshPtr = GetShipModelPointer(model->meshDataOffset);
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;
    if (!meshPtr) {
        if (error) *error = "missing mesh pointer";
        return false;
    }

    const int32_t shipDist = ShipExportCanonicalDist(model);
    int32_t shipPos[3] = {0, 0, shipDist};
    Mat33 rotMat{};
    MatBuildYZT(&rotMat, 0, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];

    Camera3D camera;
    CameraInit(&camera);

    const bool prevHarvest = GetMeshHarvestThrusters();
    const int32_t prevDist = GetMeshParseDistance();
    const int32_t prevScale = GetMeshParseScaleRelated();
    SetMeshHarvestThrusters(true);
    SetMeshHarvestThrustersBlind(true); // all 0x0009 emitters
    SetMeshParseScaleRelated((int32_t)model->scaleRelated);
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    SetVertexCacheBankBase(0);
    SetVertexBankVerts(0);
    MeshData mesh = ParseMeshBytecode(meshPtr, meshSize);
    SetMeshHarvestThrustersBlind(false);
    SetMeshHarvestThrusters(prevHarvest);
    SetMeshParseDistance(prevDist);
    SetMeshParseScaleRelated(prevScale);
    if (!mesh.valid) {
        if (error) *error = "parse failed";
        return false;
    }

    std::vector<TransformedPoly> polys(8192);
    const int nPoly = TransformShipVertices(model, mesh, shipRot, shipPos, &camera,
                                            polys.data(), (int)polys.size());
    std::vector<WorldFace> faces(32768);
    const int nFace = BuildWorldShipFaces(polys.data(), nPoly, model, shipRotLit,
                                          faces.data(), (int)faces.size(),
                                          /*includeHidden=*/true);
    const bool miss = FaceSemanticMissileFlat(polys.data(), nPoly);
    const bool capsule = FaceSemanticCapsuleSilhouette(polys.data(), nPoly);
    if (missileFlat) *missileFlat = miss;

    out->reserve((size_t)nFace + 64);
    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        FaceCensusRow row;
        row.face = i;
        row.meshIndex = f.meshIndex;
        row.sourceIndex = f.sourceIndex;
        row.texInfo = f.texInfo;
        row.type = f.type;
        row.kind = f.kind;
        row.vertexCount = f.vertexCount;
        row.backFacing = f.backFacing;
        row.worldValid = f.worldValid;
        row.normalIdx = f.normalIdx;
        row.sortKey = f.sortKey;
        row.spriteChild = f.spriteChildModel;
        row.contourId = f.contourId;
        const TransformedPoly* src = nullptr;
        if (f.sourceIndex >= 0 && f.sourceIndex < nPoly) {
            src = &polys[f.sourceIndex];
            row.visible = src->visible;
            row.afterSpecialPoly = src->afterSpecialPoly;
        }
        row.semantic = ClassifyFaceSemantic(f, miss);
        ClassifyFaceParticipation(f, src, miss, capsule, &row.submittedSoft,
                                  &row.culledSoft, &row.submittedOgre,
                                  &row.culledOgre);
        out->push_back(row);
    }

    // Contours that never became WorldFaces (ScreenHybrid skip) — still census.
    int synth = -1;
    for (int i = 0; i < nPoly; i++) {
        const TransformedPoly& p = polys[i];
        if (p.type != MeshPolyType::ComplexFill || p.contourId < 0) continue;
        if (p.texInfo == 0x1000 || p.texInfo == 0x4028) continue;
        if (p.vertexCount != 4 && p.vertexCount != 2) continue;
        bool already = false;
        for (const FaceCensusRow& r : *out) {
            if (r.sourceIndex == i && r.semantic == FaceSemantic::Contour) {
                already = true;
                break;
            }
        }
        if (already) continue;
        FaceCensusRow row;
        row.face = synth--;
        row.meshIndex = p.meshIndex;
        row.sourceIndex = i;
        row.texInfo = p.texInfo;
        row.type = p.type;
        row.semantic = FaceSemantic::Contour;
        row.kind = WorldFaceKind::ScreenHybrid;
        row.vertexCount = p.vertexCount;
        row.backFacing = p.backFacing;
        row.visible = p.visible;
        row.worldValid = false;
        row.afterSpecialPoly = p.afterSpecialPoly;
        row.normalIdx = p.normalIdx;
        row.sortKey = p.sortKey;
        row.contourId = p.contourId;
        WorldFace tmp{};
        tmp.kind = WorldFaceKind::ScreenHybrid;
        tmp.texInfo = p.texInfo;
        tmp.vertexCount = (uint8_t)std::min(p.vertexCount, 4);
        tmp.backFacing = p.backFacing;
        tmp.worldValid = false;
        tmp.type = p.type;
        ClassifyFaceParticipation(tmp, &p, miss, capsule, &row.submittedSoft,
                                  &row.culledSoft, &row.submittedOgre,
                                  &row.culledOgre);
        out->push_back(row);
    }

    SpriteChildInstance kids[64];
    const int nKids = CollectSpriteChildInstances(
        model, mesh, shipRot, shipPos, &camera, (int)model->scaleRelated, kids, 64);
    for (int i = 0; i < nKids; i++) {
        FaceCensusRow row;
        row.face = synth--;
        row.semantic = FaceSemantic::SpriteChild;
        row.spriteChild = kids[i].modelId;
        row.vertexCount = 0;
        out->push_back(row);
    }
    return true;
}

bool CollectShipExportReconcile(int shipIndex, ShipExportReconcile* out)
{
    if (!out) return false;
    *out = ShipExportReconcile{};
    out->shipIndex = shipIndex;

    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) {
        out->error = "missing model";
        return false;
    }
    const uint8_t* meshPtr = GetShipModelPointer(model->meshDataOffset);
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;
    if (!meshPtr) {
        out->error = "missing mesh pointer";
        return false;
    }

    const int32_t shipDist = ShipExportCanonicalDist(model);
    int32_t shipPos[3] = {0, 0, shipDist};
    Mat33 rotMat{};
    MatBuildYZT(&rotMat, 0, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];
    Camera3D camera;
    CameraInit(&camera);

    const bool prevHarvest = GetMeshHarvestThrusters();
    const int32_t prevDist = GetMeshParseDistance();
    const int32_t prevScale = GetMeshParseScaleRelated();
    SetMeshHarvestThrusters(true);
    SetMeshHarvestThrustersBlind(true); // all 0x0009 emitters
    SetMeshParseScaleRelated((int32_t)model->scaleRelated);
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    SetVertexCacheBankBase(0);
    SetVertexBankVerts(0);
    MeshData mesh = ParseMeshBytecode(meshPtr, meshSize);
    SetMeshHarvestThrustersBlind(false);
    SetMeshHarvestThrusters(prevHarvest);
    SetMeshParseDistance(prevDist);
    SetMeshParseScaleRelated(prevScale);
    if (!mesh.valid) {
        out->error = "parse failed";
        return false;
    }

    std::vector<TransformedPoly> polys(8192);
    const int nPoly = TransformShipVertices(model, mesh, shipRot, shipPos, &camera,
                                            polys.data(), (int)polys.size());
    std::vector<WorldFace> faces(32768);
    const int nFace = BuildWorldShipFaces(polys.data(), nPoly, model, shipRotLit,
                                          faces.data(), (int)faces.size(),
                                          /*includeHidden=*/true);
    const bool miss = FaceSemanticMissileFlat(polys.data(), nPoly);
    out->missileFlat = miss;

    const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        const FaceSemantic sem = ClassifyFaceSemantic(f, miss);
        const int si = (int)sem;
        if (si < 0 || si >= 16) continue;
        SemanticReconcileBucket& b = out->bySem[si];
        b.faces++;

        auto drop = [&](const char* reason) {
            if (sem != FaceSemantic::Overlay && sem != FaceSemantic::Punch &&
                sem != FaceSemantic::Hull)
                return;
            FaceReconcileDrop d;
            d.face = i;
            d.meshIndex = f.meshIndex;
            d.texInfo = f.texInfo;
            d.semantic = sem;
            d.reason = reason;
            out->drops.push_back(d);
        };

        const int nTri = f.vertexCount == 4 ? 2 : (f.vertexCount >= 3 ? 1 : 0);
        if (!f.worldValid) {
            b.skipped += nTri > 0 ? nTri : 1;
            drop("not_world_valid");
            continue;
        }
        if (f.kind == WorldFaceKind::ScreenHybrid ||
            f.kind == WorldFaceKind::ContourHull) {
            b.skipped += nTri > 0 ? nTri : 1;
            drop("skip_kind");
            continue;
        }
        if (f.kind != WorldFaceKind::Hull && f.kind != WorldFaceKind::Punch) {
            b.skipped += nTri > 0 ? nTri : 1;
            drop("skip_kind");
            continue;
        }
        if (nTri <= 0) {
            b.skipped++;
            drop("vc_lt_3");
            continue;
        }

        for (int t = 0; t < nTri; t++) {
            const int i0 = fan[t][0], i1 = fan[t][1], i2 = fan[t][2];
            if (i2 >= f.vertexCount) {
                b.skipped++;
                drop("fan_oob");
                continue;
            }
            b.tris++;
            const WorldFaceVert& a = f.verts[i0];
            const WorldFaceVert& bv = f.verts[i1];
            const WorldFaceVert& c = f.verts[i2];
            if (FaceExportTriDegenerate(a.x, a.y, a.z, bv.x, bv.y, bv.z,
                                        c.x, c.y, c.z)) {
                b.degenerate++;
                drop("degenerate");
                continue;
            }
            if (sem == FaceSemantic::Contour &&
                FaceExportTriSliver(a.x, a.y, a.z, bv.x, bv.y, bv.z,
                                    c.x, c.y, c.z)) {
                b.sliver++;
                drop("sliver");
                continue;
            }
            b.exported++;
        }
    }

    for (int s = 0; s < 16; s++) {
        SemanticReconcileBucket& b = out->bySem[s];
        b.lost = b.tris - b.exported - b.degenerate - b.sliver;
        if (b.lost < 0) b.lost = 0;
    }

    ShipExportMesh meshOut;
    if (BuildExportMesh(shipIndex, &meshOut) && meshOut.valid) {
        out->meshHull = meshOut.hullTriCount;
        out->meshPunch = meshOut.punchTriCount;
        out->meshDecal = meshOut.decalTriCount;
        out->meshLine = meshOut.lineTriCount;
        out->mismatchHull =
            out->bySem[(int)FaceSemantic::Hull].exported +
            out->bySem[(int)FaceSemantic::MissileHull].exported - out->meshHull;
        out->mismatchPunch =
            out->bySem[(int)FaceSemantic::Punch].exported - out->meshPunch;
        out->mismatchDecal =
            out->bySem[(int)FaceSemantic::Overlay].exported - out->meshDecal;
        out->mismatchLine =
            out->bySem[(int)FaceSemantic::Line].exported - out->meshLine;
    } else {
        out->error = meshOut.error.empty() ? "export failed" : meshOut.error;
        return false;
    }
    out->valid = true;
    return true;
}

static bool FacePlane(const WorldFace& f, float n[3], float origin[3])
{
    if (f.vertexCount < 3) return false;
    const float ax = f.verts[1].x - f.verts[0].x;
    const float ay = f.verts[1].y - f.verts[0].y;
    const float az = f.verts[1].z - f.verts[0].z;
    const float bx = f.verts[2].x - f.verts[0].x;
    const float by = f.verts[2].y - f.verts[0].y;
    const float bz = f.verts[2].z - f.verts[0].z;
    n[0] = ay * bz - az * by;
    n[1] = az * bx - ax * bz;
    n[2] = ax * by - ay * bx;
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-12f) return false;
    n[0] /= len;
    n[1] /= len;
    n[2] /= len;
    origin[0] = f.verts[0].x;
    origin[1] = f.verts[0].y;
    origin[2] = f.verts[0].z;
    return true;
}

static void FaceCentroid(const WorldFace& f, float c[3])
{
    c[0] = c[1] = c[2] = 0.f;
    const int n = f.vertexCount > 0 ? f.vertexCount : 1;
    for (int i = 0; i < f.vertexCount && i < 4; i++) {
        c[0] += f.verts[i].x;
        c[1] += f.verts[i].y;
        c[2] += f.verts[i].z;
    }
    c[0] /= (float)n;
    c[1] /= (float)n;
    c[2] /= (float)n;
}

static float MinVertVert(const WorldFace& a, const WorldFace& b)
{
    float best = 1e30f;
    for (int i = 0; i < a.vertexCount && i < 4; i++) {
        for (int j = 0; j < b.vertexCount && j < 4; j++) {
            const float dx = a.verts[i].x - b.verts[j].x;
            const float dy = a.verts[i].y - b.verts[j].y;
            const float dz = a.verts[i].z - b.verts[j].z;
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < best) best = d;
        }
    }
    return best;
}

bool CollectShipCoplanarCensus(int shipIndex, ShipCoplanarCensus* out)
{
    if (!out) return false;
    *out = ShipCoplanarCensus{};
    out->shipIndex = shipIndex;

    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) {
        out->error = "missing model";
        return false;
    }
    const uint8_t* meshPtr = GetShipModelPointer(model->meshDataOffset);
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;
    if (!meshPtr) {
        out->error = "missing mesh pointer";
        return false;
    }

    const int32_t shipDist = ShipExportCanonicalDist(model);
    int32_t shipPos[3] = {0, 0, shipDist};
    Mat33 rotMat{};
    MatBuildYZT(&rotMat, 0, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];

    Camera3D camera;
    CameraInit(&camera);

    const bool prevHarvest = GetMeshHarvestThrusters();
    const int32_t prevDist = GetMeshParseDistance();
    const int32_t prevScale = GetMeshParseScaleRelated();
    SetMeshHarvestThrusters(true);
    SetMeshHarvestThrustersBlind(true); // all 0x0009 emitters
    SetMeshParseScaleRelated((int32_t)model->scaleRelated);
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    SetVertexCacheBankBase(0);
    SetVertexBankVerts(0);
    MeshData mesh = ParseMeshBytecode(meshPtr, meshSize);
    SetMeshHarvestThrustersBlind(false);
    SetMeshHarvestThrusters(prevHarvest);
    SetMeshParseDistance(prevDist);
    SetMeshParseScaleRelated(prevScale);
    if (!mesh.valid) {
        out->error = "parse failed";
        return false;
    }

    std::vector<TransformedPoly> polys(8192);
    const int nPoly = TransformShipVertices(model, mesh, shipRot, shipPos, &camera,
                                            polys.data(), (int)polys.size());
    std::vector<WorldFace> faces(32768);
    const int nFace = BuildWorldShipFaces(polys.data(), nPoly, model, shipRotLit,
                                          faces.data(), (int)faces.size(),
                                          /*includeHidden=*/true);
    const bool miss = FaceSemanticMissileFlat(polys.data(), nPoly);
    out->missileFlat = miss;

    std::vector<int> hulls, probes;
    hulls.reserve((size_t)nFace);
    probes.reserve((size_t)nFace);
    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        if (!f.worldValid || f.vertexCount < 3) continue;
        const FaceSemantic sem = ClassifyFaceSemantic(f, miss);
        if (sem == FaceSemantic::Hull || sem == FaceSemantic::MissileHull) {
            hulls.push_back(i);
            out->hullFaces++;
        } else if (sem == FaceSemantic::Overlay) {
            probes.push_back(i);
            out->overlayFaces++;
        } else if (sem == FaceSemantic::Punch) {
            probes.push_back(i);
            out->punchFaces++;
        }
    }

    out->hits.reserve(probes.size());
    for (int pi : probes) {
        const WorldFace& a = faces[pi];
        const FaceSemantic sem = ClassifyFaceSemantic(a, miss);
        CoplanarHit hit;
        hit.probeFace = pi;
        hit.meshIndex = a.meshIndex;
        hit.texInfo = a.texInfo;
        hit.semantic = sem;
        hit.vertexCount = a.vertexCount;
        float cA[3];
        FaceCentroid(a, cA);

        // Per-vert nearest hull plane (faceted roof / chevron overlays).
        // Face distance = worst vert residual; nearestHull = hull of that vert.
        bool allVertsNear = a.vertexCount >= 3;
        float worst = 0.f;
        int worstHull = -1;
        for (int vi = 0; vi < a.vertexCount && vi < 4; vi++) {
            float bestD = 1e30f;
            int bestH = -1;
            for (int hi : hulls) {
                const WorldFace& b = faces[hi];
                if (MinVertVert(a, b) > kCoplanarNearVert) continue;
                float n[3], o[3];
                if (!FacePlane(b, n, o)) continue;
                const float dx = a.verts[vi].x - o[0];
                const float dy = a.verts[vi].y - o[1];
                const float dz = a.verts[vi].z - o[2];
                const float d = std::fabs(n[0] * dx + n[1] * dy + n[2] * dz);
                if (d < bestD) {
                    bestD = d;
                    bestH = hi;
                }
            }
            if (bestH < 0) {
                allVertsNear = false;
                break;
            }
            if (bestD >= worst) {
                worst = bestD;
                worstHull = bestH;
            }
        }
        if (!allVertsNear || worstHull < 0) continue;
        hit.distance = worst;
        hit.nearestHull = worstHull;
        float cB[3];
        FaceCentroid(faces[worstHull], cB);
        const float dx = cA[0] - cB[0], dy = cA[1] - cB[1], dz = cA[2] - cB[2];
        hit.centroidDist = std::sqrt(dx * dx + dy * dy + dz * dz);
        hit.flagged = hit.distance < kCoplanarEpsilon;
        if (hit.flagged) {
            if (sem == FaceSemantic::Overlay) out->overlayFlagged++;
            else if (sem == FaceSemantic::Punch) out->punchFlagged++;
        }
        out->hits.push_back(hit);
    }
    out->valid = true;
    return true;
}

bool CollectShipRasterCensus(int shipIndex, ShipRasterCensus* out)
{
    if (!out) return false;
    *out = ShipRasterCensus{};
    out->shipIndex = shipIndex;

    const ShipModel* model = GetShipModel(shipIndex);
    if (!model || !model->meshDataOffset) {
        out->error = "missing model";
        return false;
    }
    const uint8_t* meshPtr = GetShipModelPointer(model->meshDataOffset);
    size_t meshSize = MeshBytecodeSize(shipIndex);
    if (meshSize == 0) meshSize = 1024;
    if (!meshPtr) {
        out->error = "missing mesh pointer";
        return false;
    }

    const int32_t shipDist = ShipExportCanonicalDist(model);
    int32_t shipPos[3] = {0, 0, shipDist};
    Mat33 rotMat{};
    MatBuildYZT(&rotMat, 0, 0);
    int32_t shipRot[9];
    for (int i = 0; i < 9; i++) shipRot[i] = rotMat.m[i];
    Mat33 litMat = rotMat;
    MatScaleSin32ToFp16(&litMat);
    int32_t shipRotLit[9];
    for (int i = 0; i < 9; i++) shipRotLit[i] = litMat.m[i];

    Camera3D camera;
    CameraInit(&camera);

    const bool prevHarvest = GetMeshHarvestThrusters();
    const int32_t prevDist = GetMeshParseDistance();
    const int32_t prevScale = GetMeshParseScaleRelated();
    SetMeshHarvestThrusters(true);
    SetMeshHarvestThrustersBlind(true); // all 0x0009 emitters
    SetMeshParseScaleRelated((int32_t)model->scaleRelated);
    SetMeshParseDistance(MeshParseDistanceFromViewZ(shipDist));
    SetVertexCacheBankBase(0);
    SetVertexBankVerts(0);
    MeshData mesh = ParseMeshBytecode(meshPtr, meshSize);
    SetMeshHarvestThrustersBlind(false);
    SetMeshHarvestThrusters(prevHarvest);
    SetMeshParseDistance(prevDist);
    SetMeshParseScaleRelated(prevScale);
    if (!mesh.valid) {
        out->error = "parse failed";
        return false;
    }

    std::vector<TransformedPoly> polys(8192);
    const int nPoly = TransformShipVertices(model, mesh, shipRot, shipPos, &camera,
                                            polys.data(), (int)polys.size());
    std::vector<WorldFace> faces(32768);
    const int nFace = BuildWorldShipFaces(polys.data(), nPoly, model, shipRotLit,
                                          faces.data(), (int)faces.size(),
                                          /*includeHidden=*/true);
    const bool miss = FaceSemanticMissileFlat(polys.data(), nPoly);
    const bool capsule = FaceSemanticCapsuleSilhouette(polys.data(), nPoly);
    out->missileFlat = miss;

    auto projectVert = [&](const WorldFaceVert& v, float* sx, float* sy,
                           float* z) -> bool {
        Point3D p;
        p.x = (int32_t)std::lround(v.x * (float)FP_ONE);
        p.y = (int32_t)std::lround(v.y * (float)FP_ONE);
        p.z = (int32_t)std::lround(v.z * (float)FP_ONE);
        ProjResult pr{};
        if (!ProjectPoint(&camera, &p, &pr) || !pr.visible) return false;
        *sx = (float)pr.sx;
        *sy = (float)pr.sy;
        *z = (float)pr.z;
        return true;
    };

    auto edge = [](float ax, float ay, float bx, float by, float px,
                   float py) -> float {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };

    auto forEachPixel = [&](float sx0, float sy0, float z0, float sx1, float sy1,
                            float z1, float sx2, float sy2, float z2,
                            auto&& fn) -> int {
        const float area = edge(sx0, sy0, sx1, sy1, sx2, sy2);
        if (std::fabs(area) < 1e-6f) return 0;
        const float invA = 1.f / area;
        int minx = (int)std::floor(std::min(std::min(sx0, sx1), sx2));
        int maxx = (int)std::ceil(std::max(std::max(sx0, sx1), sx2));
        int miny = (int)std::floor(std::min(std::min(sy0, sy1), sy2));
        int maxy = (int)std::ceil(std::max(std::max(sy0, sy1), sy2));
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx > kRasterWidth - 1) maxx = kRasterWidth - 1;
        if (maxy > kRasterHeight - 1) maxy = kRasterHeight - 1;
        if (minx > maxx || miny > maxy) return 0;
        int n = 0;
        for (int y = miny; y <= maxy; y++) {
            for (int x = minx; x <= maxx; x++) {
                const float px = (float)x + 0.5f;
                const float py = (float)y + 0.5f;
                const float w0 = edge(sx1, sy1, sx2, sy2, px, py) * invA;
                const float w1 = edge(sx2, sy2, sx0, sy0, px, py) * invA;
                const float w2 = edge(sx0, sy0, sx1, sy1, px, py) * invA;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                const float z = w0 * z0 + w1 * z1 + w2 * z2;
                fn(x, y, z);
                n++;
            }
        }
        return n;
    };

    auto projectFace = [&](const WorldFace& f, float sx[4], float sy[4],
                           float z[4]) -> bool {
        if (f.vertexCount < 3) return false;
        for (int v = 0; v < f.vertexCount && v < 4; v++) {
            if (!projectVert(f.verts[v], &sx[v], &sy[v], &z[v])) return false;
        }
        return true;
    };

    std::vector<float> zbuf((size_t)kRasterWidth * (size_t)kRasterHeight, 1e30f);

    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        if (!f.worldValid || f.vertexCount < 3) continue;
        const FaceSemantic sem = ClassifyFaceSemantic(f, miss);
        if (sem != FaceSemantic::Hull && sem != FaceSemantic::MissileHull)
            continue;
        out->hullFaces++;
        float sx[4], sy[4], zv[4];
        if (!projectFace(f, sx, sy, zv)) continue;
        int wrote = 0;
        const int nTri = f.vertexCount == 4 ? 2 : 1;
        const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int t = 0; t < nTri; t++) {
            const int a = fan[t][0], b = fan[t][1], c = fan[t][2];
            wrote += forEachPixel(sx[a], sy[a], zv[a], sx[b], sy[b], zv[b],
                                  sx[c], sy[c], zv[c], [&](int x, int y, float z) {
                                      const size_t idx =
                                          (size_t)y * (size_t)kRasterWidth + (size_t)x;
                                      if (z < zbuf[idx]) zbuf[idx] = z;
                                  });
        }
        if (wrote > 0) out->hullRasterized++;
    }

    out->hits.reserve(32);
    for (int i = 0; i < nFace; i++) {
        const WorldFace& f = faces[i];
        if (!f.worldValid || f.vertexCount < 3) continue;
        const FaceSemantic sem = ClassifyFaceSemantic(f, miss);
        if (sem != FaceSemantic::Overlay && sem != FaceSemantic::Punch) continue;

        RasterFaceHit hit;
        hit.face = i;
        hit.meshIndex = f.meshIndex;
        hit.texInfo = f.texInfo;
        hit.semantic = sem;
        hit.vertexCount = f.vertexCount;
        hit.backFacing = f.backFacing;
        const TransformedPoly* src = nullptr;
        if (f.sourceIndex >= 0 && f.sourceIndex < nPoly) src = &polys[f.sourceIndex];
        bool culledS = false, culledO = false;
        ClassifyFaceParticipation(f, src, miss, capsule, &hit.submittedSoft,
                                  &culledS, &hit.submittedOgre, &culledO);

        if (sem == FaceSemantic::Overlay) {
            out->overlayFaces++;
            if (hit.submittedSoft || hit.submittedOgre) out->overlaySubmitted++;
        } else {
            out->punchFaces++;
            if (hit.submittedSoft || hit.submittedOgre) out->punchSubmitted++;
        }

        float sx[4], sy[4], zv[4];
        if (projectFace(f, sx, sy, zv)) {
            const int nTri = f.vertexCount == 4 ? 2 : 1;
            const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int t = 0; t < nTri; t++) {
                const int a = fan[t][0], b = fan[t][1], c = fan[t][2];
                forEachPixel(sx[a], sy[a], zv[a], sx[b], sy[b], zv[b], sx[c],
                             sy[c], zv[c], [&](int x, int y, float z) {
                                 const size_t idx =
                                     (size_t)y * (size_t)kRasterWidth + (size_t)x;
                                 const float hz = zbuf[idx];
                                 hit.pixCovered++;
                                 if (z < hz - kRasterZEps) hit.pixPassLess++;
                                 if (z <= hz + kRasterZEps) hit.pixPassLEqual++;
                                 if (z - kRasterPainterZBias < hz - kRasterZEps)
                                     hit.pixPassBias++;
                             });
            }
        }

        hit.rasterized = hit.pixCovered > 0;
        hit.rejectedLess = hit.rasterized && hit.pixPassLess == 0;
        hit.rejectedLEqual = hit.rasterized && hit.pixPassLEqual == 0;
        hit.rejectedBias = hit.rasterized && hit.pixPassBias == 0;
        if (sem == FaceSemantic::Overlay) {
            out->overlayPix += hit.pixCovered;
            out->overlayPixLess += hit.pixPassLess;
            out->overlayPixLEqual += hit.pixPassLEqual;
            out->overlayPixBias += hit.pixPassBias;
        } else {
            out->punchPix += hit.pixCovered;
            out->punchPixLess += hit.pixPassLess;
            out->punchPixLEqual += hit.pixPassLEqual;
            out->punchPixBias += hit.pixPassBias;
        }
        if (hit.rasterized) {
            if (sem == FaceSemantic::Overlay) {
                out->overlayRasterized++;
                if (hit.rejectedLess) out->overlayRejectedLess++;
                if (hit.rejectedLEqual) out->overlayRejectedLEqual++;
                if (hit.rejectedBias) out->overlayRejectedBias++;
            } else {
                out->punchRasterized++;
                if (hit.rejectedLess) out->punchRejectedLess++;
                if (hit.rejectedLEqual) out->punchRejectedLEqual++;
                if (hit.rejectedBias) out->punchRejectedBias++;
            }
        }
        out->hits.push_back(hit);
    }

    out->valid = true;
    return true;
}

} // namespace jjffe
