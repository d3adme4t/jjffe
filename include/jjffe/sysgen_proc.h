#ifndef JJFFE_SYSGEN_PROC_H
#define JJFFE_SYSGEN_PROC_H

// Task 4.18 — procedural star-system generation and the cold-boot spawn table.
//
// LP64-safe port of the ASM chain that builds the 115-slot PhysObj table at
// cold boot (ffe.asm):
//   FUNC_000829 (start record, DATA_006820) → FUNC_000832 (system params)
//   → FUNC_000855/000904 (SysGenParams) → FUNC_000878 (procedural SysObj list:
//   FUNC_000881/882/883-890/891/892/893/894/896/897, names FUNC_000791/793/794/
//   800/921) → FUNC_000880 + FUNC_000927 (PhysObj slots allocated 114 → down).
// All tables come from the data blob; the SysObj templates DATA_008794-8797
// are hand-coded records 2/12/16/17 (FUNC_000875).

#include "jjffe/phys_obj.h"
#include "jjffe/sysgen.h"

#include <cstdint>

namespace jjffe {

// FUNC_000927 scans slot-type bytes 0x72..1 → slots 0..114.
constexpr int kSpawnSlotCount = 115;

struct SysGenStartInfo {
    SysGenParams params;     // FUNC_000855 layout
    int32_t      tier;       // FUNC_000871 → DATA_007128
    int32_t      locIndex;   // FUNC_000868 location-record index (<0 = default)
    uint32_t     timeLo;     // [DATA_008804] at FUNC_000880 (ffe.asm 15105)
    uint32_t     timeHi;     // [DATA_008807]
    int32_t      galPos[3];  // FUNC_000829 start record x/y/z → DATA_008950..52
    int32_t      orbitalStations;  // FUNC_000892 count DATA_008856 → DATA_009011 (FUNC_000905)
};

// FUNC_000829/000832 for new-game start `startIndex` (DATA_006820 record; the
// ASM's FUNC_000829 takes idx = (param >> 8) & 0xff of FUNC_000147's
// start << 8 | stage). The name is FUNC_000830(0, …).
bool SysGenStartParams(int startIndex, SysGenStartInfo* out);

// FUNC_000868: location-record index for a system code (<0 = one of the
// default records before DATA_007110), and that record's blob offset
// (0x36 bytes each; FUNC_000869/870/871 read it).
int32_t SysGenLocationIndex(uint32_t code);
uint32_t SysGenLocationRecord(int32_t index);

// FUNC_000878 (numstars 0/1 single-star path). Writes the SysObj list plus a
// zero-mass terminator; returns the body count (terminator not counted), or -1
// when the multi-star path (FUNC_000877) would be required. `orbitalStations`
// receives DATA_008856 (orbital starports placed by FUNC_000892).
int SysGenGenerateProcedural(const SysGenParams& params, int32_t tier,
                             SysObj* out, int cap, int32_t* orbitalStations = nullptr);

// FUNC_000878's generator globals after the procedural branch.
struct SysGenProcState {
    uint32_t d8843, d8844;  // FUNC_000894 LFSR
    int32_t  d8846;         // bodies placed by FUNC_000882/891/892
    uint8_t  d8847;         // FUNC_000881 calls (FUNC_000907)
    uint8_t  d8848;         // star letter
    uint8_t  d8849;
    uint8_t  d8850;         // numstars
    uint8_t  d8851;         // stars left
    uint8_t  d8855;         // starports + stations
    uint8_t  d8856;         // orbital stations (FUNC_000905 → DATA_009011)
};

// FUNC_000878's procedural branch (ffe.asm:64071-64255) for every numstars,
// including the FUNC_000877 binary pairs. Generates in place over `out[0..cap)`
// (cap 0: globals only) and writes the terminator when the ASM does. Returns the
// body count, or -1 when a path is not ported.
// Task 4.162: test-support bridge for the call oracle - runs the LIVE Gen::Station()
// (FUNC_000892) with the fixture routed to the storage it actually reads. Not used by the game.
int32_t SysGenStationProbe(int32_t* records, int32_t* cntInOut, uint32_t type, void* vars,
                           int32_t parent);

int SysGenRunProcedural878(const SysGenParams& params, int32_t tier, SysObj* out, int cap,
                           SysGenProcState* state);

// FUNC_000878's handcoded branch (ffe.asm:64257-64519, randseed1 == 0): the
// DATA_007133 list for `systemcode` copied from FUNC_000875's records into
// `out[0..cap)` with the terminator. Sets d8847 / d8855 / d8856 only; writes no
// ASM global. Returns the record count (starports included).
int SysGenRunHandcoded878(uint32_t systemcode, SysObj* out, int cap, SysGenProcState* state);

struct SpawnSlotTable {
    PhysObj  slots[kSpawnSlotCount];
    uint8_t  slotType[kSpawnSlotCount];  // list bytes (FUNC_000927 arg 3 / 880 retags)
    int      sysToSlot[kSysObjGeneratedCapacity];
    int      sysCount;
};

// FUNC_000880 + FUNC_000927 into an empty table. Surface starports go through
// the FUNC_000879 port, which lowers them onto the parent's terrain with two
// FUNC_000574 height queries (include/jjffe/terrain_query.h).
int SysGenMaterializeSlots(const SysObj* sys, int count, uint32_t timeLo,
                           uint32_t timeHi, SpawnSlotTable* table);

// FUNC_000880 into an ASM-layout object pool (list bytes at +0..+0x72, objects
// at +0x74 + slot*0x152) through FUNC_000927. Writes each SysObj's +0x18; starport
// +0x58 = worldGen - 1 (DATA_008811). Returns the objects placed.
int SysGenMaterializePool(intptr_t pool, SysObj* sys, int count, uint32_t timeLo,
                          uint32_t timeHi, uint8_t worldGen);

// Whole cold-boot path for new-game start `startIndex` (FUNC_000147's
// parameter). `sysOut` receives the SysObj list (cap >= kSysObjGeneratedCapacity):
// the procedural system, or for randseed1 == 0 the FUNC_000878 handcoded list.
bool SysGenBuildSpawnTable(SpawnSlotTable* table, SysObj* sysOut, int sysCap,
                           SysGenStartInfo* infoOut, int startIndex);

}  // namespace jjffe

#endif  // JJFFE_SYSGEN_PROC_H
