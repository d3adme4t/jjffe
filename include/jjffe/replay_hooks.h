#pragma once
#include <cstdint>

namespace jjffe {

// Observation points for the deterministic logic replay
// (test/test_logic_replay.cpp, task 4.48). Null outside the test.

// Called on entry to each stepper of the DATA_008818 / DATA_008820 RNG with
// the state it is about to read and its return address. stepper is the ASM
// FUNC id: 922, 1476 or 1530.
using ReplayRngHook = void (*)(int stepper, uint32_t s818, uint32_t s820, const void* caller);
extern ReplayRngHook g_replayRngHook;

// FUNC_001418_TimerAdvance takes the frame's elapsed ms from this hook
// instead of the wall clock: no frame-rate wait and no 50 Hz catch-up.
using ReplayTimerHook = uint32_t (*)(void);
extern ReplayTimerHook g_replayTimerHook;

// When set, FUNC_000152 dispatches module 3's action 3 (FUNC_001010, the 3D
// view frame whose FUNC_001536 object pass sets every object's LOD +0x88 /
// +0x8a through FUNC_001680) as the ASM does. The game build holds it: its
// DrawMission is that pass. FUNC_001680 enqueues its jobs as in the ASM
// (FUNC_001773 evaluates the model, and a model's random vertices draw the
// RNG through FUNC_001478 -> FUNC_001476); the replay runs no render half
// (FUNC_001789), and the next frame's FUNC_001786 drops the jobs.
extern bool g_replayDispatchViewFrame;

// The replay's frame number, for env-gated traces (P465_TRACE). 0 outside the test.
extern uint32_t g_replayFrame;

// FUNC_001343_StringExpandFFCodeInternal calls the entry hook first thing (before
// its null-dst return) and the return hook at its completed return (`return next;`)
// with the text it left at dst and its FF code: the two points
// captures/4.118/ff1343_count.py breaks on (task 4.138).
using ReplayFF1343EntryHook = void (*)(void);
using ReplayFF1343ReturnHook = void (*)(const uint8_t* dst, uint32_t code);
extern ReplayFF1343EntryHook g_replayFF1343EntryHook;
extern ReplayFF1343ReturnHook g_replayFF1343ReturnHook;

// The system-view pool DATA_008801, mapped on first use. No replay scenario reaches the
// system view, so the lowmem replay maps it after its last checkpoint to check that it
// lands at ffeasm's address (task 4.145).
uint8_t* ReplaySysViewPool();

} // namespace jjffe
