#include "jjffe/ship_model.h"
#include "jjffe/ship_model_table.h"
#include "jjffe/data.h"
#include <cstdio>
#include <cstring>

namespace jjffe {

// Accessor functions for DATA_004681 in core.cpp
extern void SetShipModelPtr(int32_t index, uint8_t* ptr);
extern uint8_t* GetShipModelPtr(int32_t index);

// Static storage for resolved ship models
static ShipModel s_shipModels[SHIP_MODEL_COUNT] = {};
static bool s_initialized = false;

static uint32_t readU32(const uint8_t* blob, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, blob + offset, sizeof(v));
    return v;
}

const uint8_t* GetShipModelPointer(uint32_t offset)
{
    const uint8_t* blob = data::GetDataBlob();
    if (!blob || offset >= data::GetDataBlobSize()) return nullptr;
    return blob + offset;
}

static void loadShipModel(int index, const uint8_t* blob, size_t blobSize)
{
    if (index < 0 || index >= SHIP_MODEL_COUNT) return;

    uint32_t objOffset = SHIP_MODEL_OFFSETS[index];
    if (objOffset == 0 || objOffset + 68 > blobSize) return;

    const uint8_t* obj = blob + objOffset;
    ShipModel& model = s_shipModels[index];

    model.vertexCount = readU32(obj, 0x08);
    model.normalCount = readU32(obj, 0x10);
    model.scaleRelated = readU32(obj, 0x14);
    model.scaleFactor = readU32(obj, 0x18);
    model.radius = readU32(obj, 0x1C);
    model.vanishingSize = readU32(obj, 0x20);
    model.starColor[0] = obj[0x24];
    model.starColor[1] = obj[0x25];
    model.starColor[2] = obj[0x26];
    model.starColor[3] = obj[0x27];
    model.starSize = readU32(obj, 0x28);
    model.viewZOffset = readU32(obj, 0x2C);
    model.viewYOffset = readU32(obj, 0x30);
    model.reserved1 = readU32(obj, 0x3C);
    model.reserved2 = readU32(obj, 0x40);

    // Blob ObjectStruct pointer fields are zeroed at extract time; resolve via
    // SHIP_MODEL_SUBPTRS built from ffebmp.asm ObjectStruct `dd` (mesh, verts).
    const ShipModelSubPtrs& sub = SHIP_MODEL_SUBPTRS[index];
    model.meshDataOffset = 0;
    model.vertexDataOffset = 0;
    model.normalDataOffset = 0;
    model.collisionDataOffset = 0;
    model.specsDataOffset = 0;

    auto inRange = [&](uint32_t off, uint32_t minBytes) -> bool {
        return off != 0 && off + minBytes <= blobSize;
    };

    if (inRange(sub.mesh, 4) && inRange(sub.vertices, 6)) {
        uint32_t meshOff = sub.mesh;
        uint32_t vertOff = sub.vertices;
        uint32_t normOff = sub.normals;
        uint32_t collOff = sub.collision;
        uint32_t specsOff = sub.specs;

        // Legacy stub promotion: only when ASM mesh head is END (0) and the
        // collision→verts span holds real opcodes (#201 glass). Title #176
        // mesh starts with ALU (0xc15d) — must NOT promote.
        if (inRange(sub.collision, 8) && sub.collision < sub.vertices &&
            sub.vertices - sub.collision >= 16) {
            const uint16_t head =
                static_cast<uint16_t>(blob[meshOff] | (blob[meshOff + 1] << 8));
            if (head == 0) {
                bool looksLikeMesh = false;
                const uint32_t span = sub.vertices - sub.collision;
                const uint32_t peek = span < 128 ? span : 128;
                for (uint32_t i = 0; i + 1 < peek; i += 2) {
                    const uint16_t w = static_cast<uint16_t>(
                        blob[sub.collision + i] | (blob[sub.collision + i + 1] << 8));
                    const uint32_t slot = w & 0x1Fu;
                    if (slot == 0x06 || slot == 0x03 || slot == 0x04 || slot == 0x07 ||
                        slot == 0x08 || slot == 0x14 || slot == 0x13 || slot == 0x0D ||
                        slot == 0x1D || slot == 0x0E) {
                        looksLikeMesh = true;
                        break;
                    }
                }
                if (looksLikeMesh) {
                    meshOff = sub.collision;
                    vertOff = sub.normals;
                    normOff = sub.specs;
                    collOff = sub.vertices;
                    specsOff = 0;
                }
            }
        }

        // NOTE: the former "#361–#363 title logo stub" promotion (redirect
        // to the #346–#348 orbit-card meshes) was REMOVED: the ASM's title
        // walk uses #361's own mesh (0x383dc — the mid band with edge-paint
        // resolves 4/6/8/2/0 and the basis), and the promotion sent the C++
        // walk to #346's mesh (0x377a4) instead.

        model.meshDataOffset = meshOff;
        model.vertexDataOffset = vertOff;
        if (inRange(normOff, 2)) model.normalDataOffset = normOff;
        if (inRange(collOff, 2)) model.collisionDataOffset = collOff;
        /* Stub ObjectStructs often alias specs→collision/verts (e.g. type
         * 0x9b: specs==collision). ASM leaves +0x38 null; never treat those
         * streams as ShipSpecs. */
        if (specsOff != 0 && specsOff != vertOff && specsOff != meshOff &&
            specsOff != normOff && specsOff != collOff &&
            inRange(specsOff, 48))
            model.specsDataOffset = specsOff;
    }

    SetShipModelPtr(index, reinterpret_cast<uint8_t*>(&model));
}

void InitShipModels()
{
    if (s_initialized) return;

    const uint8_t* blob = data::GetDataBlob();
    size_t blobSize = data::GetDataBlobSize();

    if (!blob || blobSize == 0) {
        fprintf(stderr, "InitShipModels: data blob not loaded\n");
        return;
    }

    printf("Initializing ship models from %d entries...\n", SHIP_MODEL_COUNT);

    int withMesh = 0;
    for (int i = 0; i < SHIP_MODEL_COUNT; i++) {
        loadShipModel(i, blob, blobSize);
        if (s_shipModels[i].meshDataOffset != 0) withMesh++;
    }

    const ShipModel* shuttle = GetShipModel(SHIP_INDEX_SHUTTLE);
    if (shuttle) {
        printf("  Shuttle (index %d): verts=%u, norms=%u, radius=%u mesh=%u\n",
               SHIP_INDEX_SHUTTLE, shuttle->vertexCount, shuttle->normalCount, shuttle->radius,
               shuttle->meshDataOffset);
    }

    const ShipModel* saker = GetShipModel(SHIP_INDEX_SAKER_III);
    if (saker) {
        printf("  Saker III (index %d): verts=%u, norms=%u, radius=%u mesh=%u\n",
               SHIP_INDEX_SAKER_III, saker->vertexCount, saker->normalCount, saker->radius,
               saker->meshDataOffset);
    }

    const ShipModel* cobra = GetShipModel(SHIP_INDEX_COBRA_MK3);
    if (cobra) {
        printf("  Cobra Mk3 (index %d): verts=%u, norms=%u, radius=%u mesh=%u\n",
               SHIP_INDEX_COBRA_MK3, cobra->vertexCount, cobra->normalCount, cobra->radius,
               cobra->meshDataOffset);
    }

    printf("  Ship model init complete: %d entries, %d with mesh data\n",
           SHIP_MODEL_COUNT, withMesh);
    s_initialized = true;
}

const ShipModel* GetShipModel(int index)
{
    if (index < 0 || index >= SHIP_MODEL_COUNT) return nullptr;

    uint8_t* ptr = GetShipModelPtr(index);
    if (!ptr) return nullptr;

    ShipModel* model = reinterpret_cast<ShipModel*>(ptr);
    if (model >= &s_shipModels[0] && model < &s_shipModels[SHIP_MODEL_COUNT]) {
        return model;
    }

    return nullptr;
}

int GetShipModelIndex(const ShipModel* model)
{
    if (!model) return -1;
    if (model < &s_shipModels[0] || model >= &s_shipModels[SHIP_MODEL_COUNT])
        return -1;
    return static_cast<int>(model - s_shipModels);
}

const ShipModel* GetShipModelForGameObj(const void* gameObj)
{
    if (!gameObj) return nullptr;

    const uint8_t* objBytes = static_cast<const uint8_t*>(gameObj);
    int modelIndex = *reinterpret_cast<const int*>(objBytes + 0x82);

    return GetShipModel(modelIndex);
}

} // namespace jjffe
