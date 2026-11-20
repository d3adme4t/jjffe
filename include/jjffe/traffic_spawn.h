#ifndef JJFFE_TRAFFIC_SPAWN_H
#define JJFFE_TRAFFIC_SPAWN_H

// Task 4.27 — the new-game traffic ships (slots 70-86 of the default start).
//
// LP64-safe port of the two FUNC_000147 module handlers that draw from the game
// RNG (DATA_008818/8820, FUNC_001530 / FUNC_000922) between its new-game seed
// and the first frame (ffe.asm):
//   module 7, phase 1 — FUNC_000616: FUNC_000617/618 (starport records at
//     DATA_008999), FUNC_000622 (markets 619/620, adverts 621 → 636-646,
//     missions 634/635, news 623 → 626-632), with FUNC_000857/853 (neighbour
//     systems) and FUNC_000869/870/871 (location records).
//   module 11, phase 3 — FUNC_000687 → FUNC_000688: FUNC_000689/690 (inbound
//     from hyperspace), 691 (docked), 699 (near an orbital station), 698 →
//     694/695/696 (pirates after the player), 692 (empty). Ships come from
//     FUNC_000772/773 (FUNC_000927 slot, 703/791/795 registration, 774
//     equipment), placed by 702 (FUNC_001507), 650/671 (pads) and 775/776
//     (origin system: 857, 870, 852/841/856/833, 901/903).

#include "jjffe/sysgen_proc.h"

#include <cstdint>

namespace jjffe {

// FUNC_000618 caps the list at 0x12 (FUNC_000892 never places more).
constexpr int kStationRecordMax = 18;

// The 0x322-byte record at DATA_008999 + 0x322·i.
struct StationRecord {
    int32_t  market[33][2];   // +0x000: {stock (FUNC_000620), price (FUNC_000619)}
    uint8_t  slot;            // +0x130: the starport's PhysObj slot
    uint8_t  flags;           // +0x131: 0x10 orbital; 1/2/4/8 once-only adverts
    int32_t  advert[18][6];   // +0x132
    int32_t  advertCount;     // +0x2e2
    uint16_t mission[13][2];  // +0x2e6: {model, seed word}
    int32_t  missionCount;    // +0x31e
};

struct TrafficSpawnResult {
    int           stationCount = 0;  // DATA_008997
    StationRecord stations[kStationRecordMax] = {};
    uint32_t      rngAt688[2] = {};  // DATA_008818/8820 entering FUNC_000688
    uint32_t      rngAfter[2] = {};  // ... and leaving it
    int           allocated = 0;     // FUNC_000927 slots taken inside FUNC_000688
    int           freed = 0;         // FUNC_000940 releases (spawn rejected)
};

// FUNC_000147(1) from its RNG seed: FUNC_000616 (phase 1) then FUNC_000688
// (phase 3) on a table that already holds the generated system and the
// default-start player at `playerSlot` (DATA_008857). Ships go to the free
// slots below the player, highest first, as FUNC_000927 allocates them.
bool TrafficSpawnNewGame(SpawnSlotTable* table, const SysGenStartInfo& info, int playerSlot,
                         TrafficSpawnResult* out);

}  // namespace jjffe

#endif  // JJFFE_TRAFFIC_SPAWN_H
