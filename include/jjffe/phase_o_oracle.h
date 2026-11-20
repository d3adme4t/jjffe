#pragma once

#include <cstdint>

namespace jjffe {

/* Phase O experiment: ASM draw-only EXEC order for Saker III (ship 31) front
 * (yaw=180), derived from ab_shots/job_trace/multi/asm/31/manual.tsv.
 * Skips FUNC_001792/001793 barriers and material-60 thruster/sprite jobs.
 * sortKey is the ASM arena/list key; C++ match target is sortKey * 8 (±64).
 */
struct PhaseOOracleEvent {
    int32_t material;   /* -1 = flat / non-textured */
    uint32_t asmSortKey;
};

inline constexpr PhaseOOracleEvent kPhaseOOracleFront[] = {
    {11, 4743u},
    {9, 4635u},
    {9, 4635u},
    {9, 4635u},
    {9, 4635u},
    {-1, 4327u},
    {-1, 4327u},
    {9, 4227u},
    {9, 4227u},
    {50, 4227u},
    {-1, 3884u},
    {-1, 3884u},
    {48, 3748u},
    {48, 3748u},
    {-1, 3008u},
    {11, 2964u},
    {-1, 2964u},
    {11, 2964u},
    {11, 2964u},
    {-1, 2964u},
    {11, 2964u},
    {11, 2963u},
    {-1, 2963u},
    {11, 2963u},
    {-1, 2963u},
};

inline constexpr int kPhaseOOracleFrontCount =
    (int)(sizeof(kPhaseOOracleFront) / sizeof(kPhaseOOracleFront[0]));

inline constexpr int kPhaseOKeyTol = 64;

}  // namespace jjffe
