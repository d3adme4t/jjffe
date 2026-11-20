// Task 4.162 (shadow-chain consolidation, stage 2a): the FUNC_000892 fixture tables are
// SHARED storage, not per-translation-unit statics.
//
// In the ASM these are writable .data/.bss labels the oracle can set directly
// (DATA_007136/7137/7138 are the model-index tables read at byte stride sel*4; DATA_008794/
// 8795/8797 are the 0x44-byte station templates). As `static` in data_globals.h every TU got
// its own copy, so a writable fixture could never reach the implementation under test - the
// blocker recorded in 4.152/4.155 and the reason the oracle guarded a dead duplicate
// (core.cpp:12845) instead of the live generator (sysgen_proc.cpp's Station()).
//
// One definition here; the header declares them extern, so any TU may read or set them.
#include "jjffe/data_globals.h"

namespace jjffe {

uint32_t DATA_007136[4] = {};
uint32_t DATA_007137[4] = {};
uint32_t DATA_007138[4] = {};

uint8_t DATA_008794[0x44] = {};
uint8_t DATA_008795[0x44] = {};
uint8_t DATA_008797[0x44] = {};

}  // namespace jjffe
