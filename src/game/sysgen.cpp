#include "jjffe/sysgen.h"

#include <cstring>

namespace jjffe {

void FUNC_000876();
void FUNC_000878(intptr_t param_1, int* param_2);

static SysObj g_handcodedSysObjs[kSysObjHandcodedCapacity];
static SysObj g_generatedSysObjs[kSysObjGeneratedCapacity];
static int g_sysgenHandcodedDirty = 1;  // DATA_007135 (ffedat.asm:17972)
static bool g_sysgenEmitActive = false;
static SysObj* g_emitDest = nullptr;
static int g_emitCap = 0;
static PhysObj* g_matDest = nullptr;
static int g_matCap = 0;

void SysObjInit(SysObj* obj)
{
    if (!obj) return;
    std::memset(obj, 0, sizeof(SysObj));
}

void SysObjSetName(SysObj* obj, const char* name)
{
    if (!obj) return;
    std::memset(obj->name, 0, sizeof(obj->name));
    if (!name) return;
    int i = 0;
    for (; i < 19 && name[i]; ++i) {
        obj->name[i] = name[i];
    }
    obj->name[i] = 0;
}

const char* SysObjName(const SysObj* obj)
{
    return obj ? obj->name : "";
}

uint16_t SysObjTypeValue(const SysObj* obj)
{
    return obj ? obj->type : 0;
}

uint16_t SysObjModel(const SysObj* obj)
{
    return obj ? obj->modelIndex : 0;
}

uint16_t SysObjParent(const SysObj* obj)
{
    return obj ? obj->parentIndex : 0;
}

SysObj* SysObjHandcodedTable()
{
    return g_handcodedSysObjs;
}

SysObj* SysObjGeneratedTable()
{
    return g_generatedSysObjs;
}

int SysObjBufferCapacity()
{
    return static_cast<int>(sizeof(g_handcodedSysObjs) / sizeof(g_handcodedSysObjs[0]));
}

int SysObjGeneratedCapacity()
{
    return static_cast<int>(sizeof(g_generatedSysObjs) / sizeof(g_generatedSysObjs[0]));
}

const SysObj* SysObjFindByName(const SysObj* table, int cap, const char* name)
{
    if (!table || !name || cap <= 0) return nullptr;
    for (int i = 0; i < cap; ++i) {
        if (std::strcmp(table[i].name, name) == 0) return &table[i];
    }
    return nullptr;
}

void SysGenRequestHandcodedInit()
{
    g_sysgenHandcodedDirty = 1;
}

bool SysGenIsHandcodedDirty()
{
    return g_sysgenHandcodedDirty != 0;
}

void SysGenClearHandcodedDirty()
{
    g_sysgenHandcodedDirty = 0;
}

void SysGenBeginHandcodedEmit(SysObj* dest, int cap)
{
    g_sysgenEmitActive = true;
    g_emitDest = dest;
    g_emitCap = cap;
}

void SysGenEndHandcodedEmit()
{
    g_sysgenEmitActive = false;
    g_emitDest = nullptr;
    g_emitCap = 0;
}

bool SysGenEmitActive()
{
    return g_sysgenEmitActive;
}

SysObj* SysGenEmitDest()
{
    return g_emitDest;
}

int SysGenEmitCapacity()
{
    return g_emitCap;
}

void SysGenSetMaterializeOutput(PhysObj* out, int cap)
{
    g_matDest = out;
    g_matCap = cap;
}

PhysObj* SysGenMaterializeDest()
{
    return g_matDest;
}

int SysGenMaterializeCapacity()
{
    return g_matCap;
}

int SysGenMaterialize(const SysObj* sys, int sysCount, PhysObj* out, int outCap)
{
    if (!sys || !out || sysCount <= 0 || outCap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < sysCount && n < outCap; ++i) {
        if (sys[i].mass == 0) break;
        PhysObj* p = &out[n];
        PhysObjInit(p);
        PhysObjSetIndex(p, static_cast<uint8_t>(n));
        PhysObjSetModel(p, sys[i].modelIndex);
        PhysObjSetSeed(p, static_cast<int32_t>(sys[i].randseed));
        PhysObjSetRotSpeed(p, sys[i].rotSpeed);
        PhysObjSetTreeLevel(p, sys[i].treeLevel);
        PhysObjSetTemp(p, sys[i].temperature);
        PhysObjSetOrientation(p, static_cast<uint16_t>(sys[i].orientation));
        PhysObjSetMass(p, sys[i].mass);
        PhysObjSetOrbRadius(p, sys[i].orbRadius);
        PhysObjSetControl(p, static_cast<uint8_t>(PhysObjControl::kOrbital));
        PhysObjSetName(p, sys[i].name);

        const uint16_t type = sys[i].type;
        const uint16_t model = sys[i].modelIndex;
        if (type >= 0x20) {
            PhysObjOrFlags(p, kPhysFlagDockable);
            if (type == 0x23) {
                PhysObjOrFlags(p, kPhysFlagSurfaceStarport);
                if (model >= 0x51 && model <= 0x5a) {
                    PhysObjOrFlags(p, kPhysFlagOpenAir);
                }
            }
        }
        if (sys[i].parentIndex != 0) {
            int parentSlot = static_cast<int>(sys[i].parentIndex) - 1;
            if (parentSlot >= 0 && parentSlot < n) {
                PhysObjSetParent(p, PhysObjGetIndex(&out[parentSlot]));
            }
        }
        ++n;
    }
    return n;
}

bool SysGenFind(const SysObj* table, int cap, const char* name, SysObj* out)
{
    const SysObj* found = SysObjFindByName(table, cap, name);
    if (!found || !out) return false;
    *out = *found;
    return true;
}

int SysGenGenerateHandcodedSol(SysObj* out, int cap)
{
    if (!out || cap < 2) return 0;
    FUNC_000876();
    SysGenBeginHandcodedEmit(out, cap);
    SysGenParams params;
    std::memset(&params, 0, sizeof(params));
    params.randseed1 = 0;
    std::memcpy(params.name, "Sol", 4);
    FUNC_000878(0, reinterpret_cast<int*>(&params));
    SysGenEndHandcodedEmit();
    int n = 0;
    while (n < cap && out[n].mass != 0) {
        ++n;
    }
    return n;
}

bool SysGenGenerateSystem(const SysGenParams& params, SysObj* out, int cap, int* outCount)
{
    if (params.randseed1 != 0) {
        if (outCount) *outCount = 0;
        return false;
    }
    int n = SysGenGenerateHandcodedSol(out, cap);
    if (outCount) *outCount = n;
    return n > 0;
}

bool SysGenMaterializeSystem(const SysObj* sys, int count, void* physOut, int physCap)
{
    if (!physOut || physCap <= 0) return false;
    int n = SysGenMaterialize(sys, count, reinterpret_cast<PhysObj*>(physOut), physCap);
    return n > 0;
}

} // namespace jjffe
