#ifndef JJFFE_INTRO_CAST_H
#define JJFFE_INTRO_CAST_H

#include <cstdint>

namespace jjffe {

// Faithful-enough A/B of ASM title/cast (JUMP_003840 + FUNC_000961/000962):
// logo blit 0x42 + timed credits via FUNC_000962 (blob string IDs 0x941b–0x942d).
// Not a full _asmmain port — callback table / FUNC_000144 still stubbed.

// Call before IntroCastInit. true = --intro viewer (splash auto-advance, ESC
// quits, dig keys); false = default boot (_asmmain key handling only).
void IntroCastSetViewerMode(bool viewer);
void IntroCastInit();
void IntroCastDraw();
// Returns: 0=continue, 1=start the game with IntroCastGetActionId(), -1=quit
int IntroCastHandleInput();
// FUNC_000147 action id picked on the intro (DATA_007199); -1 before a pick.
int IntroCastGetActionId();
// _asmmain .splashloop is showing (before the title clock starts).
bool IntroCastInSplash();
// Leaving the intro for the game: drop intro-only render overrides.
void IntroCastShutdown();
uint32_t IntroCastGetClock();
void IntroCastSetSpeed(int ticksPerFrame); // default 937 ≈ full cast ~1–2 min wall
// Soft title constellation / bridge mesh.
void IntroCastSetShipAnim(bool enabled);
bool IntroCastGetShipAnim();

// Intro player transport (INTRO_PLAYER=1 enables strip + richer keys).
// Seek is clock-driven for digs — not a full deterministic simulator rewind.
void IntroCastSetPaused(bool paused);
bool IntroCastGetPaused();
void IntroCastSetClock(uint32_t clock);
void IntroCastStep(int frames); // ±N frames of s_ticksPerFrame via SetClock
uint32_t IntroCastGetLoopEnd(); // scrubber range (kLoopReset)
bool IntroCastPlayerEnabled();

// HW intercept (D3D .x): poses from SW title Sprite749 expand.
struct IntroCastHwInstance {
    int modelId = -1;     // soft cast child id (before stand-in remap)
    int32_t pos[3]{};     // 16.16 world
    int32_t rot[9]{};     // Sin32 / FixedMulHigh basis (MatBuildYZT scale)
    int scaleRelated = 0;
};

// Advance title ObjectStruct via FUNC_000961 (no soft ship paint) and collect
// fleet instances for the current clock. Does not advance s_clock.
int IntroCastCollectHwInstances(IntroCastHwInstance* out, int maxOut);

// Soft logo + credits + stars only (no ship mesh). Advances cast clock.
void IntroCastDrawUiOnly();

// Title camera Z used for framing (ObjectStruct+0x36 / DATA_007235).
int32_t IntroCastGetShipDist();

} // namespace jjffe

#endif
