#pragma once
#include <cstdint>

namespace jjffe {

// Real-time base (ffedat.asm BSS dwords), one definition each in
// src/core/data_lod_globals.cpp. The timer callback adds
// deltaMs * 0x3e2 / 0x14 (49.7 units per ms) to DATA_009157 (ffe.asm:85858);
// the mission frame moves it into DATA_009122 and accumulates DATA_008799
// (ffe.asm:68743-68780). One second is 0xc22e units in the game's divides.
extern uint32_t DATA_009157;
extern int32_t  DATA_009122;
extern uint32_t DATA_008799;

// Game-time base: the same callback adds deltaMs * DATA_007706 / 0x14 to
// DATA_009156 (ffe.asm:85868-85872); the mission frame snapshots it into
// DATA_009155 and adds it to the time of day DATA_008804 (ffe.asm:68692-68699).
// DATA_007706 is the time-acceleration multiplier: 0x3e2 is 1x (the rate of
// DATA_009157), 0 is paused, FUNC_000207 copies the SysMenu setting DATA_009012.
extern uint32_t DATA_009155;
extern uint32_t DATA_009156;
extern uint32_t DATA_007706;

// ffe.asm:85858-85866, integer-exact (32x32->64 mul, div by 20).
inline uint32_t GameClockUnitsForMs(uint32_t deltaMs)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(deltaMs) * 0x3e2u) / 0x14u);
}

// ffe.asm:85868-85872. The ASM's div faults when the quotient needs more
// than 32 bits (deltaMs > 0x14 * 2^32 / multiplier, 329 ms at 0xf880000);
// the port keeps the low dword.
inline uint32_t GameClockAccelUnitsForMs(uint32_t deltaMs, uint32_t multiplier)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(deltaMs) * multiplier) / 0x14u);
}

// Capture mode only, the ffeasm surface walk's surface_clock_freeze()
// (tools/ffe_flow_surface.gdb: ebx = 0 at FUNC_001418_TimerAdvance.end50hz,
// and DATA_008804 written once):
//   JJFFE_CLOCK_FREEZE=1       both accumulators frozen, DATA_008804 left at
//                              FUNC_000147's 0x80000000;
//   JJFFE_CLOCK_FREEZE=0x<hex> both frozen, and the first mission frame after
//                              FUNC_000147 sets DATA_008804 = <hex>.
// The wall delta both timers feed to the accumulators: 0 when frozen.
uint32_t GameClockDeltaMs(uint32_t wallDeltaMs);

// The JJFFE_CLOCK_FREEZE=0x<hex> time of day; false for =1 and unset.
bool GameClockFrozenTimeOfDay(uint32_t* timeOfDay);

} // namespace jjffe
