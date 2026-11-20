#ifndef JJFFE_SHIP_CONTROLS_H
#define JJFFE_SHIP_CONTROLS_H

#include <cstdint>

#include "jjffe/math.h"  // FP_ONE

namespace jjffe {

// Play-test flight hull position clamp (ShipControlsUpdate). Scaled orbital
// render positions reach ~500*FP_ONE, so keep headroom above that. The bounds
// test uses this same constant so it cannot drift out of sync again.
constexpr int32_t kPlayerPositionBound = 2000 * FP_ONE;

// Mission flight controls (Phase 60 scaffolding).
//
// ROTATION INERTIA — TEMP for play-test / debug (2026-09-09):
//   Active model is accel/decel toward ±maxRate (intuitive).
//   TODO: replace with ASM-faithful path later:
//     FUNC_001038 / CalcKeyInput torque → rates at +0xb0/+0xb2/+0xb4
//     FUNC_000585 spend → FUNC_000611 / MatBuildYZ
//   See .planning/quick/260909-mission-flight-controls/ and STATE.md.

void ShipControlsInit();
// One fixed 1/60 s step (deterministic; used directly by tests).
void ShipControlsUpdate();
// Runs as many 1/60 s steps as wall-clock time since the last call allows, so
// flight speed does not depend on the (uncapped) mission frame rate.
void ShipControlsUpdateRealTime();
int ShipControlsHandleInput();  // Returns -1 for ESC, 0 for stay in mission

#ifdef JJFFE_TEST
void ShipControlsSimulateThrust(int32_t forward);
void ShipControlsSimulateYaw(int32_t yaw);
void ShipControlsSimulateLateral(int32_t lateral);
void ShipControlsSimulateVertical(int32_t vertical);
#endif

} // namespace jjffe

#endif // JJFFE_SHIP_CONTROLS_H
