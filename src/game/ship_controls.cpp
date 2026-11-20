#include "jjffe/ship_controls.h"
#include "jjffe/game_state.h"
#include "jjffe/math.h"
#include "jjffe/input.h"
#include "jjffe/platform.h"
#include "jjffe/core/gameplay.h"
#include <cstdlib>
#include <cstdint>

namespace jjffe {

// --- Linear thrust (Phase 60 scaffolding, TEMP play-test tuning) ---
//
// One ShipControlsUpdate() = one step of kStepsPerSecond. pos += vel >> 8, so
// kThrustPower 36 is ~506 render units/s^2 (the HUD shows render units as m:
// ~50 g). kDampingFactor 0xFFC9 is a ~20 s time constant, capping speed near
// 10 km/s. Not ASM: the real model is thrust from the ship spec (module 3).
static constexpr int32_t kThrustPower = 36;
static constexpr int32_t kDampingFactor = 0xFFC9;
static constexpr int32_t kFuelBurnRate = FP_ONE / 100;
static constexpr int32_t kPositionScale = 8;

static constexpr uint32_t kStepsPerSecond = 60;
static constexpr uint32_t kMaxFrameMs = 250;  // cap catch-up after a stall
// Bound lives in ship_controls.h (kPlayerPositionBound) so the bounds test
// shares it — see the 4.5 render-space orbits (up to ~500*FP_ONE).
static constexpr int32_t kPositionBound = kPlayerPositionBound;

// --- Rotation inertia (TEMP play-test model, 2026-09-09) ---
//
// KEEP ACTIVE for mission testing/debug. Do NOT treat as final parity.
//
// Active algorithm (intuitive accel/decel):
//   hold key  → rate += kAccel  (clamp ±kMaxRate)
//   release   → rate moves toward 0 by kDecel / frame
//   each tick → apply `rate` as the orientation step (MatBuildYZ / roll)
//
// TODO(asm-rotation): switch to original FFE model when wiring real physics:
//   FUNC_001038 + CalcKeyInput  → fill int16 rates at obj+0xb0/+0xb2/+0xb4
//   FUNC_000585 + FUNC_000584   → spend rate into rot via FUNC_000611
//   FUNC_001041                 → two half-steps with DATA_009155
// Gate behind a define when restoring, e.g. JJFFE_ASM_ROTATION_INERTIA.
// Tracking: .planning/quick/260909-mission-flight-controls/, STATE.md
//
// Angle units = Sin16 / MatBuildYZ (0x10000 = full turn).
// Instant Phase-60 step was FP_ONE/64 = 0x400; that is now kMaxRate.
static constexpr int32_t kMaxRate = 0x400;   // max turn rate
static constexpr int32_t kAccel   = 0x40;    // ~16 frames to reach max
static constexpr int32_t kDecel   = 0x28;    // ~25 frames to stop after release

static int32_t s_yawRate = 0;
static int32_t s_pitchRate = 0;
static int32_t s_rollRate = 0;

static bool s_escHeld = false;

static bool s_clockStarted = false;
static uint32_t s_lastTicks = 0;
static uint32_t s_stepAccum = 0;  // ms * kStepsPerSecond

#ifdef JJFFE_TEST
static int32_t s_testThrustLatch = 0;
static int32_t s_testLateralLatch = 0;
static int32_t s_testVerticalLatch = 0;
#endif

// input: -1, 0, or +1
static void IntegrateAxis(int32_t& rate, int input)
{
    if (input > 0) {
        rate += kAccel;
        if (rate > kMaxRate) rate = kMaxRate;
    } else if (input < 0) {
        rate -= kAccel;
        if (rate < -kMaxRate) rate = -kMaxRate;
    } else if (rate > 0) {
        rate -= kDecel;
        if (rate < 0) rate = 0;
    } else if (rate < 0) {
        rate += kDecel;
        if (rate > 0) rate = 0;
    }
}

static void ApplyRotationDelta(GameObj& ship, int32_t dPitch, int32_t dYaw, int32_t dRoll)
{
    if (dPitch == 0 && dYaw == 0 && dRoll == 0) return;

    Mat33 currentRot;
    for (int i = 0; i < 9; i++) {
        currentRot.m[i] = ship.rot[i];
    }

    Mat33 yawMat;
    MatBuildYZ(&yawMat, dPitch, dYaw);

    Mat33 newRot;
    MatMatMul(&newRot, &currentRot, &yawMat);

    if (dRoll != 0) {
        // Roll around the ship's forward axis (column 2).
        const int32_t c = static_cast<int32_t>(Sin16(static_cast<uint16_t>(dRoll + 0x4000))) << 1;
        const int32_t s = static_cast<int32_t>(Sin16(static_cast<uint16_t>(dRoll))) << 1;
        const int32_t r0 = newRot.m[0], r1 = newRot.m[3], r2 = newRot.m[6];
        const int32_t u0 = newRot.m[1], u1 = newRot.m[4], u2 = newRot.m[7];
        newRot.m[0] = static_cast<int32_t>(((int64_t)r0 * c + (int64_t)u0 * s) >> 16);
        newRot.m[3] = static_cast<int32_t>(((int64_t)r1 * c + (int64_t)u1 * s) >> 16);
        newRot.m[6] = static_cast<int32_t>(((int64_t)r2 * c + (int64_t)u2 * s) >> 16);
        newRot.m[1] = static_cast<int32_t>(((int64_t)u0 * c - (int64_t)r0 * s) >> 16);
        newRot.m[4] = static_cast<int32_t>(((int64_t)u1 * c - (int64_t)r1 * s) >> 16);
        newRot.m[7] = static_cast<int32_t>(((int64_t)u2 * c - (int64_t)r2 * s) >> 16);
        // m[2],m[5],m[8] (forward) unchanged
    }

    for (int i = 0; i < 9; i++) {
        ship.rot[i] = newRot.m[i];
    }
}

static void SampleFlightInput(const uint8_t* keyStates, int* yawIn, int* pitchIn, int* rollIn,
                              int32_t* thrustOut, int32_t* lateralOut, int32_t* verticalOut)
{
    *yawIn = 0;
    *pitchIn = 0;
    *rollIn = 0;
    *thrustOut = 0;
    *lateralOut = 0;
    *verticalOut = 0;

    // Original FFE key assignment. ffe.asm FUNC_001038 (76868-76873) folds the
    // hold accumulators as:  dy(pitch) = +A - Z,  dx = +',' - '.',  droll = +'[' - ']'
    // and stores rot[0]=dy, rot[2]=-dx, rot[1]=droll. The manual matches: A/Z are
    // pitch down/up, '<'/'>' (the same physical ','/'.' keys) are yaw, '['/']' are
    // a ~30 degree roll. Speed is Enter (up) / RShift (down), not ','/'.'.
    //
    // A = nose down, Z = nose up   (ASM: dy += A, dy -= Z)
    if (keyStates[KEY_A] == 0) *pitchIn += 1;
    if (keyStates[KEY_Z] == 0) *pitchIn -= 1;

    // ',' = yaw left, '.' = yaw right  (ASM: dx += ',', dx -= '.'; scaffold +1 = right)
    if (keyStates[KEY_COMMA] == 0) *yawIn -= 1;
    if (keyStates[KEY_PERIOD] == 0) *yawIn += 1;

    // '[' = roll left, ']' = roll right  (ASM: droll += '[', droll -= ']')
    if (keyStates[KEY_LEFTBRACKET] == 0) *rollIn -= 1;
    if (keyStates[KEY_RIGHTBRACKET] == 0) *rollIn += 1;

    // Enter = increase speed, RShift = decrease (manual: "<Enter>" / "<RShift>").
    if (keyStates[KEY_ENTER] == 0) {
        *thrustOut = 1;
    } else if (keyStates[KEY_RSHIFT] == 0) {
        *thrustOut = -1;
    }

    // Numpad manual thrust (Phase-60 scaffolding, not an ASM port: the manual
    // describes direct thrust control, but the ASM itself routes these scancodes
    // to console button IDs, not a flight-loop thrust axis - see
    // MISSING_CONTROLS_PLAN.md item 2). Num8/Num5 add to the same forward axis
    // as Enter/RShift; Num7/Num9 strafe; Num6/Num4 ("lower thruster") and
    // Num3/Num1 ("upper thruster") both feed the one vertical axis this
    // scaffolding model has, so holding either pair moves the ship vertically.
    if (keyStates[KEY_KP8] == 0) *thrustOut = 1;
    else if (keyStates[KEY_KP5] == 0) *thrustOut = -1;

    if (keyStates[KEY_KP9] == 0) *lateralOut += 1;
    if (keyStates[KEY_KP7] == 0) *lateralOut -= 1;

    if (keyStates[KEY_KP6] == 0) *verticalOut += 1;
    if (keyStates[KEY_KP4] == 0) *verticalOut -= 1;
    if (keyStates[KEY_KP1] == 0) *verticalOut += 1;
    if (keyStates[KEY_KP3] == 0) *verticalOut -= 1;
}

void ShipControlsInit()
{
    s_yawRate = 0;
    s_pitchRate = 0;
    s_rollRate = 0;
    s_escHeld = false;
    s_clockStarted = false;
    s_stepAccum = 0;
#ifdef JJFFE_TEST
    s_testThrustLatch = 0;
    s_testLateralLatch = 0;
    s_testVerticalLatch = 0;
#endif

    g_gameState.playerShip.vel[0] = 0;
    g_gameState.playerShip.vel[1] = 0;
    g_gameState.playerShip.vel[2] = 0;
}

void ShipControlsUpdate()
{
    GameObj& ship = g_gameState.playerShip;

    int yawIn = 0, pitchIn = 0, rollIn = 0;
    int32_t thrustForward = 0, thrustLateral = 0, thrustVertical = 0;
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);
    SampleFlightInput(keyStates, &yawIn, &pitchIn, &rollIn, &thrustForward, &thrustLateral,
                       &thrustVertical);
#ifdef JJFFE_TEST
    if (s_testThrustLatch != 0) {
        thrustForward = s_testThrustLatch;
        s_testThrustLatch = 0;
    }
    if (s_testLateralLatch != 0) {
        thrustLateral = s_testLateralLatch;
        s_testLateralLatch = 0;
    }
    if (s_testVerticalLatch != 0) {
        thrustVertical = s_testVerticalLatch;
        s_testVerticalLatch = 0;
    }
#endif

    // Rotation: the ASM's own physics owns it by default (PlayerSteerAsm runs
    // FUNC_001038 + FUNC_000585 on the ship's matrix; JJFFE_ASM_STEER=0 restores
    // the Phase-60 scaffolding integrator). The matrix goes through an aligned
    // local because GameObj is a packed struct.
    int32_t rot9[9];
    for (int i = 0; i < 9; ++i) rot9[i] = ship.rot[i];
    if (PlayerSteerAsm(keyStates, rot9)) {
        for (int i = 0; i < 9; ++i) ship.rot[i] = rot9[i];
    } else {
        IntegrateAxis(s_yawRate, yawIn);
        IntegrateAxis(s_pitchRate, pitchIn);
        IntegrateAxis(s_rollRate, rollIn);

        ApplyRotationDelta(ship, s_pitchRate, s_yawRate, s_rollRate);
    }

    if (thrustForward != 0 && ship.fuel > 0) {
        int32_t thrust = thrustForward * kThrustPower;
        ship.vel[0] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[2]) * thrust) >> 16);
        ship.vel[1] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[5]) * thrust) >> 16);
        ship.vel[2] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[8]) * thrust) >> 16);
        ship.fuel -= kFuelBurnRate;
        if (ship.fuel < 0) ship.fuel = 0;
    }
    // Numpad strafe/vertical thrust (scaffolding, see SampleFlightInput): same
    // power and fuel model as forward thrust, applied along the ship's right
    // (rot column 0) and up (rot column 1) axes instead of forward (column 2).
    if (thrustLateral != 0 && ship.fuel > 0) {
        int32_t thrust = thrustLateral * kThrustPower;
        ship.vel[0] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[0]) * thrust) >> 16);
        ship.vel[1] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[3]) * thrust) >> 16);
        ship.vel[2] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[6]) * thrust) >> 16);
        ship.fuel -= kFuelBurnRate;
        if (ship.fuel < 0) ship.fuel = 0;
    }
    if (thrustVertical != 0 && ship.fuel > 0) {
        int32_t thrust = thrustVertical * kThrustPower;
        ship.vel[0] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[1]) * thrust) >> 16);
        ship.vel[1] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[4]) * thrust) >> 16);
        ship.vel[2] += static_cast<int32_t>((static_cast<int64_t>(ship.rot[7]) * thrust) >> 16);
        ship.fuel -= kFuelBurnRate;
        if (ship.fuel < 0) ship.fuel = 0;
    }

    ship.pos[0] += ship.vel[0] >> kPositionScale;
    ship.pos[1] += ship.vel[1] >> kPositionScale;
    ship.pos[2] += ship.vel[2] >> kPositionScale;

    // Damp the magnitude: an arithmetic shift of a negative product rounds
    // away from zero, so small negative velocities would never decay.
    for (int i = 0; i < 3; i++) {
        const int64_t v = ship.vel[i];
        const int64_t mag = ((v < 0 ? -v : v) * kDampingFactor) >> 16;
        ship.vel[i] = static_cast<int32_t>(v < 0 ? -mag : mag);
    }

    if (ship.pos[0] > kPositionBound) ship.pos[0] = kPositionBound;
    if (ship.pos[0] < -kPositionBound) ship.pos[0] = -kPositionBound;
    if (ship.pos[1] > kPositionBound) ship.pos[1] = kPositionBound;
    if (ship.pos[1] < -kPositionBound) ship.pos[1] = -kPositionBound;
    if (ship.pos[2] > kPositionBound) ship.pos[2] = kPositionBound;
    if (ship.pos[2] < -kPositionBound) ship.pos[2] = -kPositionBound;
}

void ShipControlsUpdateRealTime()
{
    const uint32_t now = PlatformGetTicks();
    if (!s_clockStarted) {
        s_clockStarted = true;
        s_lastTicks = now;
        s_stepAccum = 0;
        ShipControlsUpdate();
        return;
    }
    uint32_t dt = now - s_lastTicks;
    s_lastTicks = now;
    if (dt > kMaxFrameMs) dt = kMaxFrameMs;
    s_stepAccum += dt * kStepsPerSecond;
    while (s_stepAccum >= 1000) {
        s_stepAccum -= 1000;
        ShipControlsUpdate();
    }
}

int ShipControlsHandleInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    if (keyStates[KEY_ESCAPE] == 0) {
        if (!s_escHeld) {
            s_escHeld = true;
            return -1;
        }
    } else {
        s_escHeld = false;
    }
    return 0;
}

#ifdef JJFFE_TEST
void ShipControlsSimulateThrust(int32_t forward)
{
    s_testThrustLatch = forward;
}

void ShipControlsSimulateLateral(int32_t lateral)
{
    s_testLateralLatch = lateral;
}

void ShipControlsSimulateVertical(int32_t vertical)
{
    s_testVerticalLatch = vertical;
}

void ShipControlsSimulateYaw(int32_t yaw)
{
    // Inject as a one-shot rate bump for tests.
    s_yawRate += yaw;
    if (s_yawRate > kMaxRate) s_yawRate = kMaxRate;
    if (s_yawRate < -kMaxRate) s_yawRate = -kMaxRate;
}
#endif

} // namespace jjffe
