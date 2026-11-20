#include "jjffe/mission.h"
#include "jjffe/game_state.h"
#include "jjffe/math.h"
#include "jjffe/draw.h"
#include "jjffe/draw_main.h"
#include "jjffe/video.h"
#include "jjffe/platform.h"
#include "jjffe/input.h"
#include "jjffe/mission_data.h"
#include "jjffe/logic.h"
#include "jjffe/ship_controls.h"
#include "jjffe/ai_behavior.h"
#include "jjffe/combat.h"
#include "jjffe/core/gameplay.h"
#include "jjffe/world.h"
#include "jjffe/player_spawn.h"
#include "jjffe/data.h"
#include "jjffe/data_loader.h"
#include "jjffe/bitmap_parser.h"
#include "jjffe/bitmap_table.h"
#include "jjffe/ship_model.h"
#include "jjffe/ab_auto_zoom.h"
#include "jjffe/mesh_parser.h"
#include "jjffe/vertex_transform.h"
#include "jjffe/ship_renderer.h"
#include "jjffe/core/gameplay.h"
#include "jjffe/core/engine.h"
#include "jjffe/core/utils.h"
#include "jjffe/ui.h"
#include "jjffe/phys_obj.h"
#include "jjffe/planet_render.h"
#include "jjffe/data_globals.h"
#include "jjffe/frame_pose.h"
#include "jjffe/game_loop.h"   // GameLoopGetFrameCount (R1145 body dump)
#include "jjffe/planet_shade_trace.h"
#include "jjffe/planet_midpoint_trace.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <utility>

namespace jjffe {

void FUNC_001575(uint16_t* param_1);
void FUNC_001786(void);
void FUNC_001789(void);
void MissionSetStarLight(const int64_t starToCam[3], const int32_t camRot16[9],
                         uint8_t starFlags);
void MissionRenderObject1680(uint8_t* obj, const uint8_t* cam, const int64_t vec[3]);
void FUNC_000956(int32_t param_1);   // ship/cabin asset render (ffe.asm:3e55c)
void SetDpalColourTap(uint32_t* hist4096);

static Camera3D s_missionCamera;

// DATA_008838 semantics, refreshed by UpdateMission from MissionViewCamera():
// true when the current view is first-person and the player hull must NOT be
// drawn (the camera sits at the ship's own eye); false in every chase view
// (rear / side / map / orbital), where the ASM's object pass does paint the
// player.
static bool s_viewCameraSkipsPlayer = true;

// R1152: case 6's scanner-grid object, produced with the camera above and drawn once the scene is
// enqueued (JUMP_004338 draws it last, through FUNC_001680).
static MissionViewGrid s_mode6Grid = {};

// First row of the console band; ClearUpperBuf clears the 0x9E rows above it.
constexpr int kConsoleBandY = 0x9E;

// Integer isqrt (Newton) for altitude / look-at.
static int32_t MissionISqrt64(uint64_t n)
{
    if (n == 0) return 0;
    uint64_t x = n;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return static_cast<int32_t>(x);
}

static constexpr int64_t kQ30 = int64_t{1} << 30;

static int64_t MissionAbs64(int64_t v) { return v < 0 ? -v : v; }

// n / d rounded half away from zero (d > 0).
static int64_t MissionDivRound(int64_t n, int64_t d)
{
    const int64_t h = d / 2;
    return (n >= 0) ? (n + h) / d : -((-n + h) / d);
}

// Hope terrain-sphere radius in the same units as WorldGetPos / 0x1F cam space
// (90<<14 = 1474560). Not ShipModel +0x1C (99<<14): that bounding radius put
// the "landed" camera ~10% of R up, outside the atmosphere shell (877 "57").
static int32_t HopeSurfaceRadius(const PhysObj* hope)
{
    if (!hope) return 50 * FP_ONE;
    const int32_t r = PlanetSurfaceRadiusUnits(PhysObjGetModel(hope));
    return (r > 0) ? r : 50 * FP_ONE;
}

static int HopeWorldIndex(const PhysObj* hope)
{
    if (!hope) return -1;
    const PhysObj* table = WorldPhysObjs();
    const int n = WorldBodyCount();
    if (!table || n <= 0) return -1;
    const ptrdiff_t i = hope - table;
    if (i < 0 || i >= n) return -1;
    return static_cast<int>(i);
}

// Spawn mode: default landed (alt 0, horizon). JJFFE_ORBITAL_HOPE=1 restores
// the ae605ca space view (meshR+2500, nose at planet centre) for A/B.
static bool s_orbitalHopeView = false;
static int32_t s_missionLookPitchFp16 = 0;

// Aloft-pose heading knobs (degrees, decimals allowed; unset = 0):
//   JJFFE_ORBITAL_PITCH  tilt off nadir about local right (0 = nose at the
//                        planet centre, 90 = level with the horizon)
//   JJFFE_ORBITAL_YAW    compass heading about the radial (planet) axis
//   JJFFE_ORBITAL_ROLL   about the view axis
// rot = Rz(yaw)·Rx(pitch)·Rz(roll) on the identity pose (whose forward is the
// radial axis), so with all three unset the pose is unchanged.
//   JJFFE_ORBITAL_DIR    "x,y,z" integers: Hope→camera direction in system
//                        axes (e.g. an ffeasm relative_vec64) instead of −Z;
//                        nose at the centre, basis built like the Q31 poses of
//                        tools/ffe_flow_surface.gdb. The heading knobs rebuild
//                        rot from identity, so don't combine them with it.
//   JJFFE_ORBITAL_VEC64  "x,y,z" FFE integers: the camera's Vec64 about Hope
//                        (an ffeasm .meta relative_vec64), direction AND
//                        magnitude. The camera holds it exactly
//                        (MissionSetCameraFfe) and the ship sits at its render
//                        cell. With JJFFE_ORBITAL_HOPE=1 it replaces _DIR and
//                        _ALT: basis as _DIR (nose at the centre), then the
//                        heading knobs turn that basis in its own frame,
//                        rot = B·Rz(yaw)·Rx(pitch)·Rz(roll). With
//                        JJFFE_REPLAY_POSE it replaces the sidecar's
//                        camera.pos / ship.pos every frame and keeps its rot.
static bool EnvVec64(const char* name, int64_t out[3])
{
    const char* s = getenv(name);
    if (!s || !s[0]) return false;
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        out[i] = strtoll(s, &end, 10);
        if (end == s) return false;
        if (i < 2) {
            if (*end != ',') return false;
            s = end + 1;
        }
    }
    return out[0] != 0 || out[1] != 0 || out[2] != 0;
}

static bool OrbitalDirEnv(int64_t out[3]) { return EnvVec64("JJFFE_ORBITAL_DIR", out); }
static bool OrbitalVec64Env(int64_t out[3]) { return EnvVec64("JJFFE_ORBITAL_VEC64", out); }

static double OrbitalAngleEnvDeg(const char* name)
{
    const char* s = getenv(name);
    if (!s || !s[0]) return 0.0;
    char* end = nullptr;
    const double v = strtod(s, &end);
    return (end != s) ? v : 0.0;
}

// onBasis: turn the current ship.rot (m = rot·H) instead of rebuilding it
// from identity (m = H).
static void ApplyOrbitalHeading(GameObj& ship, bool onBasis = false)
{
    const double yaw = OrbitalAngleEnvDeg("JJFFE_ORBITAL_YAW");
    const double pitch = OrbitalAngleEnvDeg("JJFFE_ORBITAL_PITCH");
    const double roll = OrbitalAngleEnvDeg("JJFFE_ORBITAL_ROLL");
    if (yaw == 0.0 && pitch == 0.0 && roll == 0.0) return;

    const double k = 3.14159265358979323846 / 180.0;
    const double cy = std::cos(yaw * k), sy = std::sin(yaw * k);
    const double cp = std::cos(pitch * k), sp = std::sin(pitch * k);
    const double cr = std::cos(roll * k), sr = std::sin(roll * k);
    // Row-major 3x3; columns are the right/up/forward axes in world space
    // (same layout as ship.rot / ProjectPoint).
    const double rh[9] = {cy, -sy, 0, sy, cy, 0, 0, 0, 1};
    const double rx[9] = {1, 0, 0, 0, cp, sp, 0, -sp, cp};
    const double rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    double t[9], m[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            t[r * 3 + c] = 0;
            for (int i = 0; i < 3; i++) t[r * 3 + c] += rh[r * 3 + i] * rx[i * 3 + c];
        }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            m[r * 3 + c] = 0;
            for (int i = 0; i < 3; i++) m[r * 3 + c] += t[r * 3 + i] * rz[i * 3 + c];
        }
    if (onBasis) {
        double b[9];
        for (int i = 0; i < 9; i++) b[i] = static_cast<double>(ship.rot[i]) / FP_ONE;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                t[r * 3 + c] = 0;
                for (int i = 0; i < 3; i++) t[r * 3 + c] += b[r * 3 + i] * m[i * 3 + c];
            }
        std::memcpy(m, t, sizeof(m));
    }
    for (int i = 0; i < 9; i++)
        ship.rot[i] = static_cast<int32_t>(std::lround(m[i] * FP_ONE));
    fprintf(stderr, "InitMission: orbital heading yaw=%g pitch=%g roll=%g (deg)%s\n",
            yaw, pitch, roll, onBasis ? " on the vec64 basis" : "");
}

// The mission camera in FFE units about the render anchor (Hope): the source
// of truth for the FUNC_001680 passes. s_missionCamera.position is derived
// from it (FFE >> WorldRenderShift; Hope is the render origin), so a pose
// finer than one render unit — the pad, 4000 FFE (+0x140) above the pad
// (task 4.20) — keeps its exact FFE position.
static int64_t s_missionCameraFfe[3] = {};

static void MissionSetCameraFfe(const int64_t ffe[3])
{
    const int shift = WorldRenderShift();
    std::memcpy(s_missionCameraFfe, ffe, sizeof(s_missionCameraFfe));
    CameraSetPosition(&s_missionCamera, static_cast<int32_t>(ffe[0] >> shift),
                      static_cast<int32_t>(ffe[1] >> shift),
                      static_cast<int32_t>(ffe[2] >> shift));
}

// Render-unit writers (ship follow, poses, .pose sidecars) name a render cell,
// FFE = render << WorldRenderShift. A held FFE already inside that cell is
// kept, so the per-frame follow of a ship at rest does not round it away.
static void MissionSetCameraRender(int32_t x, int32_t y, int32_t z)
{
    const int shift = WorldRenderShift();
    const int32_t pos[3] = {x, y, z};
    bool inCell = true;
    for (int a = 0; a < 3; ++a)
        if ((s_missionCameraFfe[a] >> shift) != pos[a]) inCell = false;
    if (!inCell)
        for (int a = 0; a < 3; ++a)
            s_missionCameraFfe[a] = static_cast<int64_t>(pos[a]) * (int64_t{1} << shift);
    CameraSetPosition(&s_missionCamera, x, y, z);
}

// Space view: sit outside Hope at (meshR + alt), nose toward planet centre.
static bool PlacePlayerSpaceAboveHope(int32_t altitudeAboveSurface)
{
    const PhysObj* hope = WorldFind("Hope");
    if (!hope) return false;
    const int hopeIdx = HopeWorldIndex(hope);
    if (hopeIdx < 0) return false;

    int32_t hx = 0, hy = 0, hz = 0;
    WorldGetPos(hopeIdx, &hx, &hy, &hz);
    const int32_t meshR = HopeSurfaceRadius(hope);
    int32_t dist = meshR + altitudeAboveSurface;
    if (dist < meshR + 1) dist = meshR + 1;

    // Sit on −Z from Hope; identity rot looks +Z → planet centred in view.
    GameObj& ship = g_gameState.playerShip;
    ship.pos[0] = hx;
    ship.pos[1] = hy;
    ship.pos[2] = hz - dist;
    ship.vel[0] = ship.vel[1] = ship.vel[2] = 0;

    ship.rot[0] = FP_ONE; ship.rot[1] = 0;       ship.rot[2] = 0;
    ship.rot[3] = 0;      ship.rot[4] = FP_ONE;  ship.rot[5] = 0;
    ship.rot[6] = 0;      ship.rot[7] = 0;       ship.rot[8] = FP_ONE;

    int64_t vec[3] = {0, 0, 0};
    const bool atVec64 = OrbitalVec64Env(vec);
    int64_t d[3] = {vec[0], vec[1], vec[2]};
    if (atVec64 || OrbitalDirEnv(d)) {
        // Fixed point only: scale into ±2^30, unit vectors in Q30, round to 16.16.
        int64_t v[3] = {d[0], d[1], d[2]};
        while (MissionAbs64(v[0]) >= kQ30 || MissionAbs64(v[1]) >= kQ30 ||
               MissionAbs64(v[2]) >= kQ30)
            for (int64_t& c : v) c /= 2;
        const int64_t len = MissionISqrt64(static_cast<uint64_t>(
            v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
        if (len > 0) {
            ship.pos[0] = hx + static_cast<int32_t>(MissionDivRound(v[0] * dist, len));
            ship.pos[1] = hy + static_cast<int32_t>(MissionDivRound(v[1] * dist, len));
            ship.pos[2] = hz + static_cast<int32_t>(MissionDivRound(v[2] * dist, len));
            const int64_t f[3] = {-MissionDivRound(v[0] * kQ30, len),
                                  -MissionDivRound(v[1] * kQ30, len),
                                  -MissionDivRound(v[2] * kQ30, len)};
            // right = up_hint × forward, up_hint = +Y unless |f.y| >= 0.9, then +X.
            int64_t r[3];
            if (10 * MissionAbs64(f[1]) < 9 * kQ30) {
                r[0] = f[2]; r[1] = 0; r[2] = -f[0];
            } else {
                r[0] = 0; r[1] = -f[2]; r[2] = f[1];
            }
            const int64_t rl = MissionISqrt64(static_cast<uint64_t>(
                r[0] * r[0] + r[1] * r[1] + r[2] * r[2]));
            for (int64_t& c : r) c = MissionDivRound(c * kQ30, rl);
            const int64_t u[3] = {MissionDivRound(f[1] * r[2] - f[2] * r[1], kQ30),
                                  MissionDivRound(f[2] * r[0] - f[0] * r[2], kQ30),
                                  MissionDivRound(f[0] * r[1] - f[1] * r[0], kQ30)};
            const int64_t toFp16 = kQ30 / FP_ONE;
            for (int i = 0; i < 3; ++i) {
                ship.rot[3 * i + 0] = static_cast<int32_t>(MissionDivRound(r[i], toFp16));
                ship.rot[3 * i + 1] = static_cast<int32_t>(MissionDivRound(u[i], toFp16));
                ship.rot[3 * i + 2] = static_cast<int32_t>(MissionDivRound(f[i], toFp16));
            }
            fprintf(stderr, "InitMission: orbital %s=(%lld,%lld,%lld)\n",
                    atVec64 ? "vec64" : "dir", static_cast<long long>(d[0]),
                    static_cast<long long>(d[1]), static_cast<long long>(d[2]));
        }
    }
    ApplyOrbitalHeading(ship, atVec64);

    if (atVec64) {
        MissionSetCameraFfe(vec);
        ship.pos[0] = s_missionCamera.position.x;
        ship.pos[1] = s_missionCamera.position.y;
        ship.pos[2] = s_missionCamera.position.z;
        const int64_t cx = ship.pos[0], cy = ship.pos[1], cz = ship.pos[2];
        dist = static_cast<int32_t>(
            MissionISqrt64(static_cast<uint64_t>(cx * cx + cy * cy + cz * cz)));
        altitudeAboveSurface = dist - meshR;
    } else {
        MissionSetCameraRender(ship.pos[0], ship.pos[1], ship.pos[2]);
    }
    for (int i = 0; i < 9; i++)
        s_missionCamera.rotation.m[i] = ship.rot[i];

    s_orbitalHopeView = true;
    s_missionLookPitchFp16 = 0;  // nose at centre, not horizon
    g_gameState.isDocked = false;
    fprintf(stderr,
            "InitMission: space view above Hope dist=%d meshR=%d alt=%d "
            "hope=(%d,%d,%d) ship=(%d,%d,%d)\n",
            dist, meshR, altitudeAboveSurface, hx, hy, hz,
            ship.pos[0], ship.pos[1], ship.pos[2]);
    return true;
}

static bool InstallationsShipFlat()
{
    const char* e = getenv("JJFFE_INSTALLATIONS_SHIPFLAT");
    return e && e[0] == '1';
}

// A/B only: JJFFE_PLANET_PARKED=1 restores the pre-4.24 flight planets
// (render-unit camD into FUNC_001683's scale-14 parking, zoom s14+s18−14).
// JJFFE_WORL_FIXED_ZOOM=1 (the pre-4.12 zoom 2) exists only on that path and
// implies it.
static bool PlanetsParked()
{
    const char* e = getenv("JJFFE_PLANET_PARKED");
    const char* z = getenv("JJFFE_WORL_FIXED_ZOOM");
    return (e && e[0] == '1') || (z && z[0] == '1');
}

// Mission camera relative to the render anchor (Hope) in FFE units.
static void MissionCameraAnchorRel(int64_t out[3])
{
    std::memcpy(out, s_missionCameraFfe, sizeof(s_missionCameraFfe));
}

// Camera object for FUNC_001680: the pad player while the camera sits at its
// +0x3e, else the mission camera as a child of Hope (Q31 rows = the camera's
// right / up / forward columns).
// On the pad, match WorldSpawnPlayer (cold-boot copy PlacePlayerOnPad used) so
// CameraFfeIdentity landed hashes stay put. In flight SyncPlayerShipFromPool
// moves s_missionCameraFfe off that copy, this compare fails, and the synthetic
// camera is built from the synced GameObj / FFE (live pool authority).
static void MissionCameraObj(int hopeIdx, const int64_t anchorRel[3], PhysObj* out)
{
    const PhysObj* player = WorldSpawnPlayer();
    if (player && std::memcmp(player->raw + 0x3e, anchorRel, 3 * sizeof(int64_t)) == 0) {
        *out = *player;
        return;
    }
    std::memset(out, 0, sizeof(*out));
    int32_t m[9];
    const int32_t* r = s_missionCamera.rotation.m;
    for (int axis = 0; axis < 3; ++axis)
        for (int c = 0; c < 3; ++c)
            m[3 * axis + c] = static_cast<int32_t>(
                (static_cast<int64_t>(r[3 * c + axis]) * 0x7fffffff) / FP_ONE);
    std::memcpy(out->raw, m, sizeof(m));
    std::memcpy(out->raw + 0x3e, anchorRel, 3 * sizeof(int64_t));
    PhysObjSetParent(out, static_cast<uint8_t>(hopeIdx));
    out->raw[0x57] = 1;
}

// R1245: physics writes DATA_008861 (pool ship). g_gameState.playerShip was
// only filled at InitMission pad/orbital placement, so first-person camera
// (MissionSetCameraRender + ship.rot) stayed at the pad while the HUD altitude
// (DATA_008867) and external views (MissionViewScratch from the pool) moved.
// Mirror the pool into GameObj + s_missionCameraFfe the same way PlacePlayerOnPad
// did at spawn — but ONLY in the airborne state-machine states. CameraFfeIdentity
// pins landed frames with JJFFE_NO_START_STAGES (DATA_008870 stays 0) and
// JJFFE_SYNTH_CONSOLE_PLAYER (DATA_008861 is a zeroed synth record); syncing on
// "not 0x30" wiped the pad pose. Positive list = surface-launch climb/flight.
static void SyncPlayerShipFromPool()
{
    if (s_orbitalHopeView) return;
    if (ConsolePlayerSynthesised()) return;
    const int st = static_cast<int>(MissionGameModeState());
    // 0x24 = surface launch climb (TAB from 0x30); 0x26 = post-climb flight;
    // 2 = free-flight module (external views / later play). All other states
    // keep the InitMission GameObj / FFE placement untouched.
    if (!(st == 0x24 || st == 0x26 || st == 2)) {
        if (st == 0x30) g_gameState.isDocked = true;
        return;
    }
    const auto* player = static_cast<const PhysObj*>(GetMissionPlayerObject());
    if (!player) return;

    int64_t rel[3];
    std::memcpy(rel, player->raw + 0x3e, sizeof(rel));
    MissionSetCameraFfe(rel);

    GameObj& ship = g_gameState.playerShip;
    ship.pos[0] = s_missionCamera.position.x;
    ship.pos[1] = s_missionCamera.position.y;
    ship.pos[2] = s_missionCamera.position.z;
    std::memcpy(ship.vel, player->raw + 0x8c, sizeof(ship.vel));

    int32_t m[9];
    std::memcpy(m, player->raw, sizeof(m));
    for (int axis = 0; axis < 3; ++axis)
        for (int c = 0; c < 3; ++c)
            ship.rot[3 * c + axis] = m[3 * axis + c] >> 15;

    g_gameState.isDocked = false;
}

// Drawing matrix for MissionRenderObject1680 (FUNC_001680's MatMatTMul inputs):
// Hope-local via WorldMatrixInAnchorFrame (R1140).
static void MissionDrawMatrixForCam(const PhysObj* body, const uint8_t* /*cam*/,
                                    int32_t out[9])
{
    WorldMatrixInAnchorFrame(body, out);
}

// FUNC_001536 (ffe.asm:91478): before each FUNC_001680, stamp
//   obj+0x9c = (uint16)(DATA_008804 >> 10)
// Mesh Matrix/ALU ops (FUNC_001474 kind 0x80 / ALU derived from +0x9c) use that
// word as a per-frame phase. Skipping the stamp freezes Old Blackelk's
// left-edge PlainLine obj0 (TargetingPixels 520/650/698, R1202).
static void MissionStampLightPhase(uint8_t* obj)
{
    if (!obj) return;
    *reinterpret_cast<uint16_t*>(obj + 0x9c) =
        static_cast<uint16_t>(GetGameClock() >> 10);
}

// FUNC_001054's front view (DATA_008804+0xea == 0, JUMP_004341, ffe.asm
// 77892-77914) renders from a copy of the player whose position FUNC_001053 /
// FUNC_001052 move to the cockpit eye: the player model's ship specs (header
// +0x38) give the forward word +0x18 and the up word +0x1a, scaled by the Q31
// forward (+0x18) and up (+0x0c) rows >> 16, summed and >> 12 in 32 bits, then
// sign-extended into +0x3e/+0x46/+0x4e. Only the render copy moves; the player
// object keeps its pad position. JJFFE_NO_EYE_OFFSET=1 skips it (pre-4.42 A/B).
static void MissionFrontViewEye(PhysObj* cam, int64_t camRel[3])
{
    const char* off = getenv("JJFFE_NO_EYE_OFFSET");
    if (off && off[0] == '1') return;
    const PhysObj* player = WorldSpawnPlayer();
    const ShipModel* sm = player ? GetShipModel(PhysObjGetModel(player)) : nullptr;
    const uint8_t* blob = data::GetDataBlob();
    if (!sm || !blob || sm->specsDataOffset == 0 ||
        sm->specsDataOffset + 0x1c > data::GetDataBlobSize())
        return;
    int16_t fwd, up;
    std::memcpy(&fwd, blob + sm->specsDataOffset + 0x18, sizeof(fwd));
    std::memcpy(&up, blob + sm->specsDataOffset + 0x1a, sizeof(up));
    int32_t m[9];
    std::memcpy(m, cam->raw, sizeof(m));
    for (int c = 0; c < 3; ++c) {
        const int32_t d = (fwd * (m[6 + c] >> 16) + up * (m[3 + c] >> 16)) >> 12;
        int64_t p;
        std::memcpy(&p, cam->raw + 0x3e + 8 * c, sizeof(p));
        p += d;
        std::memcpy(cam->raw + 0x3e + 8 * c, &p, sizeof(p));
        camRel[c] += d;
    }
}

// Landed on the Old Blackelk pad: the FUNC_000998 default start (slot 87,
// include/jjffe/player_pad.h). The camera holds the player's +0x3e
// (Hope-relative FFE) and the ship sits at its render cell; orientation = the
// player's Q31 matrix, whose rows are its right / up / forward axes in Hope's
// frame.
static bool PlacePlayerOnPad(const PhysObj* hope, int32_t hx, int32_t hy, int32_t hz,
                             int32_t meshR)
{
    const PhysObj* player = WorldSpawnPlayer();
    const int shift = WorldRenderShift();
    if (!player || shift <= 0) return false;

    int64_t rel[3];
    std::memcpy(rel, player->raw + 0x3e, sizeof(rel));
    MissionSetCameraFfe(rel);
    GameObj& ship = g_gameState.playerShip;
    ship.pos[0] = s_missionCamera.position.x;
    ship.pos[1] = s_missionCamera.position.y;
    ship.pos[2] = s_missionCamera.position.z;
    ship.vel[0] = ship.vel[1] = ship.vel[2] = 0;

    // Columns of ship.rot = right / up / forward (16.16), rows of +0x00 (Q31).
    int32_t m[9];
    std::memcpy(m, player->raw, sizeof(m));
    for (int axis = 0; axis < 3; ++axis)
        for (int c = 0; c < 3; ++c)
            ship.rot[3 * c + axis] = m[3 * axis + c] >> 15;

    for (int i = 0; i < 9; i++)
        s_missionCamera.rotation.m[i] = ship.rot[i];

    s_orbitalHopeView = false;
    s_missionLookPitchFp16 = 0;
    g_gameState.isDocked = true;
    fprintf(stderr,
            "InitMission: landed on Hope meshR=%d alt=0 pitch=0 "
            "hope=(%d,%d,%d) ship=(%d,%d,%d) pad rel=(%lld,%lld,%lld) parent=%s\n",
            meshR, hx, hy, hz, ship.pos[0], ship.pos[1], ship.pos[2],
            static_cast<long long>(rel[0]), static_cast<long long>(rel[1]),
            static_cast<long long>(rel[2]), PhysObjGetName(hope));
    return true;
}

// Landed cockpit: on the starport pad when the cold-boot table placed the
// player (task 4.20); otherwise on Hope's surface along −Z, looking at the
// horizon (basis right −Y, up −Z radial out, forward +X).
static bool PlacePlayerLandedOnHope()
{
    const PhysObj* hope = WorldFind("Hope");
    if (!hope) return false;
    const int hopeIdx = HopeWorldIndex(hope);
    if (hopeIdx < 0) return false;

    int32_t hx = 0, hy = 0, hz = 0;
    WorldGetPos(hopeIdx, &hx, &hy, &hz);
    const int32_t meshR = HopeSurfaceRadius(hope);

    const char* legacyPose = getenv("JJFFE_LEGACY_LANDED_POSE");
    if (!(legacyPose && legacyPose[0] == '1') && PlacePlayerOnPad(hope, hx, hy, hz, meshR))
        return true;

    GameObj& ship = g_gameState.playerShip;
    ship.pos[0] = hx;
    ship.pos[1] = hy;
    ship.pos[2] = hz - meshR;  // on surface along −Z
    ship.vel[0] = ship.vel[1] = ship.vel[2] = 0;

    // Columns = right / up / forward (16.16), as ProjectPoint / camD read
    // them: right=(0,-1,0), up=(0,0,-1) radial out, forward=(1,0,0) tangent.
    // Must be a proper rotation (right × up = forward, like the identity
    // orbital pose): the old right=+Y basis had det −1, a mirror that flips
    // every 0x1F triangle's winding so FUNC_001869's area / backface tests
    // culled the near ground.
    ship.rot[0] = 0;        ship.rot[1] = 0;        ship.rot[2] = FP_ONE;
    ship.rot[3] = -FP_ONE;  ship.rot[4] = 0;        ship.rot[5] = 0;
    ship.rot[6] = 0;        ship.rot[7] = -FP_ONE;  ship.rot[8] = 0;

    MissionSetCameraRender(ship.pos[0], ship.pos[1], ship.pos[2]);
    for (int i = 0; i < 9; i++)
        s_missionCamera.rotation.m[i] = ship.rot[i];

    s_orbitalHopeView = false;
    s_missionLookPitchFp16 = 0;  // level with local horizon
    g_gameState.isDocked = true;
    fprintf(stderr,
            "InitMission: landed on Hope meshR=%d alt=0 pitch=0 "
            "hope=(%d,%d,%d) ship=(%d,%d,%d) "
            "(cam-space planet ≈ (0,-meshR,0))\n",
            meshR, hx, hy, hz, ship.pos[0], ship.pos[1], ship.pos[2]);
    return true;
}

static int32_t AltitudeAboveHope()
{
    const PhysObj* hope = WorldFind("Hope");
    if (!hope) {
        const int32_t y = g_gameState.playerShip.pos[1];
        return (y < 0) ? -y : y;
    }
    const int hopeIdx = HopeWorldIndex(hope);
    if (hopeIdx < 0) {
        const int32_t y = g_gameState.playerShip.pos[1];
        return (y < 0) ? -y : y;
    }
    int32_t hx = 0, hy = 0, hz = 0;
    WorldGetPos(hopeIdx, &hx, &hy, &hz);
    const GameObj& ps = g_gameState.playerShip;
    const int64_t dx = static_cast<int64_t>(ps.pos[0]) - hx;
    const int64_t dy = static_cast<int64_t>(ps.pos[1]) - hy;
    const int64_t dz = static_cast<int64_t>(ps.pos[2]) - hz;
    const uint64_t sumSq =
        static_cast<uint64_t>(dx * dx) + static_cast<uint64_t>(dy * dy) +
        static_cast<uint64_t>(dz * dz);
    const int32_t dist = MissionISqrt64(sumSq);
    const int32_t meshR = HopeSurfaceRadius(hope);
    const int32_t alt = dist - meshR;
    return (alt > 0) ? alt : 0;
}

int32_t MissionAltitudeAboveHope()
{
    return AltitudeAboveHope();
}

int32_t MissionLookPitchFp16()
{
    return s_missionLookPitchFp16;
}

bool MissionIsOrbitalHopeView()
{
    return s_orbitalHopeView;
}

// File-scope statics
static bool s_keyHeld[256] = {};
static int32_t s_missionTime = 0;

// A/B flight-frame capture (tools/capture_flight_frame.sh): dump once, then
// MissionFrameDumpedExit tells the game loop to shut down cleanly.
static bool s_missionFrameDumped = false;

static int s_liveFrame = 0;

// WorldGetPos / WorldGetAnchorRel calls made by DrawMission's body draw
// passes in the last frame (logged with each dump, "frame draw space:").
static uint64_t s_drawRenderReads = 0;
static uint64_t s_drawAnchorReads = 0;

// Every dump also writes <path>.pose (task 4.13, frame_pose.h).
static void WriteFramePoseSidecar(const char* rawPath, const char* source);

// AB_DPAL_DUMP_DIR=<dir>: DPal tables for cell-diffs against ffeasm.
// "marks" = the occupancy before this frame's DPalBuild (the ASM's
// DATA_009139 at FUNC_001330 entry, less the fill mark it stamps itself);
// "post" = the built sec/rev/rgb, same files as the intro dump, plus the
// static map and cpp_lookups.tsv: every cell DPal16toPal resolved since this
// frame's DPalBuild ("cell<TAB>calls", the frame's pre-DPal colours; task 4.41).
static uint32_t s_dpalLookupHist[DPAL_MAP_SIZE];

static void ArmMissionDpalLookupTap()
{
    const char* dd = getenv("AB_DPAL_DUMP_DIR");
    if (!dd || !dd[0]) return;
    std::memset(s_dpalLookupHist, 0, sizeof(s_dpalLookupHist));
    SetDpalLookupTap(s_dpalLookupHist);
}

static void DumpMissionDpalTables(const char* stage)
{
    const char* dd = getenv("AB_DPAL_DUMP_DIR");
    if (!dd || !dd[0] || !g_videoState) return;
    const bool marks = std::strcmp(stage, "marks") == 0;
    struct { const char* ext; const void* p; size_t n; } parts[] = {
        {marks ? "marks" : "sec", g_videoState->dPalSecondary, sizeof(g_videoState->dPalSecondary)},
        {"rev", g_videoState->dPalReverse, sizeof(g_videoState->dPalReverse)},
        {"rgb", g_videoState->dPalRGB, sizeof(g_videoState->dPalRGB)},
        {"map", g_videoState->dPalMap, sizeof(g_videoState->dPalMap)},
    };
    for (size_t k = 0; k < (marks ? 1u : 4u); ++k) {
        char fpath[1200];
        snprintf(fpath, sizeof(fpath), "%s/cpp_%s.bin", dd, parts[k].ext);
        if (FILE* pf = fopen(fpath, "wb")) {
            fwrite(parts[k].p, 1, parts[k].n, pf);
            fclose(pf);
        }
    }
    if (marks) return;
    char fpath[1200];
    snprintf(fpath, sizeof(fpath), "%s/cpp_lookups.tsv", dd);
    if (FILE* pf = fopen(fpath, "w")) {
        for (int c = 0; c < DPAL_MAP_SIZE; ++c)
            if (s_dpalLookupHist[c]) fprintf(pf, "%#x\t%u\n", c, s_dpalLookupHist[c]);
        fclose(pf);
    }
}

// JJFFE_DUMP_PLAYER=1: <dump>.player, the 0x152 bytes DATA_008861 points at
// (the layout of the ffeasm walk's ffe_flow_spawn.player).
static void MaybeDumpMissionPlayer(const char* path)
{
    const char* e = getenv("JJFFE_DUMP_PLAYER");
    const void* obj = GetMissionPlayerObject();
    if (!e || e[0] != '1' || !obj) return;
    char ppath[1024];
    snprintf(ppath, sizeof(ppath), "%s.player", path);
    FILE* f = fopen(ppath, "wb");
    if (!f) return;
    fwrite(obj, 1, sizeof(PhysObj::raw), f);
    fclose(f);
}

// R1145: JJFFE_DUMP_BODIES=<path> writes every Gateway world body's FFE global position (+0x26) and
// local offset (+0x3e) as text, so a check can compare the port's composition against the ASM's own
// recorded +0x26 instead of re-deriving the port's rule (which is what UnitsAudit's parent-chain test
// does, and is self-consistency). Name, parent and the +0x57 rotating-frame flag are included so a
// reader can tell which bodies FUNC_001677 rotates. Written at the same frame as the other live
// dumps; run with JJFFE_FIXED_DT_MS=20 to share the ASM harness's clock.
static void MaybeDumpWorldBodies()
{
    const char* path = getenv("JJFFE_DUMP_BODIES");
    if (!path || !path[0] || !g_videoState) return;
    const int n = WorldBodyCount();
    const int gw = WorldGatewayIndex();
    const PhysObj* phys = WorldPhysObjs();
    if (!phys || gw < 0) return;
    FILE* f = fopen(path, "w");
    if (!f) return;
    // R1148 piece 1 verification: the render-state byte DATA_008809 (g_bss8804+0xc) and the view
    // mode DATA_008872 (+0xea), so a dump records which frame branch the loop took. The ASM probe
    // reads the same two bytes; ViewModeAB's poked rows compare them before any pixel.
    // e= is DATA_008811, the game's own frame stamp - the value +0x25 caches compare against
    // (FUNC_001677) and the one ffeasm's traces key on. Carried beside the loop's counter so the
    // two clocks can be lined up without a debugger (R1165 needed exactly that).
    fprintf(f, "frame=%u bodies=%d gateway=%d renderstate=0x%02x viewmode=%d e=%d\n",
            GameLoopGetFrameCount(), n, gw, g_bss8804[0xc],
            static_cast<int>(static_cast<int8_t>(g_bss8804[0xea])),
            static_cast<int>(DATA_008811));
    for (int i = gw; i < n; ++i) {
        int64_t g[3], loc[3];
        std::memcpy(g, phys[i].raw + 0x26, sizeof(g));
        std::memcpy(loc, phys[i].raw + 0x3e, sizeof(loc));
        fprintf(f, "body\t%d\t%s\tparent=%d\trot57=%d\tg26=%lld,%lld,%lld\tl3e=%lld,%lld,%lld\n",
                i, PhysObjGetName(&phys[i]), static_cast<int>(PhysObjGetParent(&phys[i])),
                static_cast<int>(phys[i].raw[0x57]), static_cast<long long>(g[0]),
                static_cast<long long>(g[1]), static_cast<long long>(g[2]),
                static_cast<long long>(loc[0]), static_cast<long long>(loc[1]),
                static_cast<long long>(loc[2]));
    }
    fclose(f);
}

// JJFFE_DUMP_BUTTONS=1: <dump>.buttons, DATA_005100 + the 82 ConButton
// records FUNC_000261 draws (the layout of tools/ffe_console_buttons_probe440.gdb).
static void MaybeDumpConsoleButtons(const char* path)
{
    const char* e = getenv("JJFFE_DUMP_BUTTONS");
    if (!e || e[0] != '1') return;
    uint8_t table[kConsoleButtonTableBytes];
    GetConsoleButtonTable(table);
    char bpath[1024];
    snprintf(bpath, sizeof(bpath), "%s.buttons", path);
    FILE* f = fopen(bpath, "wb");
    if (!f) return;
    fwrite(table, 1, sizeof(table), f);
    fclose(f);
}

// Live (NON-exiting) frame dump for interactive diagnosis of the flight view.
//   JJFFE_DUMP_LIVE=<path>        press F12 to write <path> + <path>.pal
//                                 + <path>.pose
//   JJFFE_DUMP_LIVE_AFTER=<n>     also auto-write once at mission frame n
//   JJFFE_DUMP_LIVE_EXIT=1        quit after that frame-n auto dump (scripts)
// Unlike JJFFE_DUMP_MISSION_FRAME this never sets s_missionFrameDumped (unless
// JJFFE_DUMP_LIVE_EXIT), so play continues and the caller can capture the
// exact pose/DPal state they see.
void MissionFrameDumpHooks();        // task 4.217 (defined after DrawMission)
static bool s_firstDrawDone = false;   // a real DrawMission frame was presented

static void MaybeDumpMissionFrameLive()
{
    const char* path = getenv("JJFFE_DUMP_LIVE");
    if (!path || !path[0] || !g_videoState) return;

    static bool s_liveAutoDone = false;
    ++s_liveFrame;

    bool dumpNow = false;
    bool exitAfter = false;
    const char* afterEnv = getenv("JJFFE_DUMP_LIVE_AFTER");
    if (!s_liveAutoDone && afterEnv && afterEnv[0] && s_liveFrame >= atoi(afterEnv)) {
        s_liveAutoDone = true;
        dumpNow = true;
        const char* exitEnv = getenv("JJFFE_DUMP_LIVE_EXIT");
        exitAfter = exitEnv && exitEnv[0] == '1';
    }
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);
    static bool prevF12 = true;
    const bool f12 = (keyStates[KEY_F12] == 0);   // DOS convention: 0 = pressed
    const bool f12Edge = f12 && !prevF12;
    if (f12Edge) dumpNow = true;
    prevF12 = f12;

    if (!dumpNow) return;
    DumpMissionDpalTables("post");
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "live dump: cannot open %s\n", path);
        return;
    }
    fwrite(g_videoState->backBuffer, 1, SCREEN_WIDTH * SCREEN_HEIGHT, f);
    fclose(f);
    char ppath[1024];
    snprintf(ppath, sizeof(ppath), "%s.pal", path);
    PlatformDumpPalette(ppath);
    WriteFramePoseSidecar(path, f12Edge ? "live-f12" : "live-auto");
    MaybeDumpMissionPlayer(path);
    MaybeDumpConsoleButtons(path);
    MaybeDumpWorldBodies();
    fprintf(stderr, "MISSION-DUMP(live, frame %d) -> %s\n", s_liveFrame, path);
    fprintf(stderr, "LIVE-VIEW-MODE frame=%d mode=%d\n", s_liveFrame, MissionViewMode());
    // R1201: orbital pan/zoom at dump (mode-4 camera; TargetingPixels left-edge mark).
    fprintf(stderr,
            "LIVE-ORBIT frame=%d angX=0x%x angY=0x%x scale=0x%x\n",
            s_liveFrame, (unsigned)MissionOrbitalAngleX(),
            (unsigned)MissionOrbitalAngleY(), (unsigned)MissionOrbitalScale());
    if (exitAfter) s_missionFrameDumped = true;
}

// JJFFE_DUMP_MISSION_FRAME_AT=<n> (default 1) dumps the n-th mission frame instead of the first.
// The ASM flow oracle ffe_flow_postintro is the SECOND flip after mission start ($postFlip == 2,
// tools/ffe_flow_walk.gdb): its star reads the sky record Hope's FUNC_001877 left in frame 1,
// which frame 1 itself cannot have (FUNC_001536 draws the star, slot 114, before Hope, 108).
static void MaybeDumpMissionFrame()
{
    if (s_missionFrameDumped) return;
    const char* path = getenv("JJFFE_DUMP_MISSION_FRAME");
    if (!path || !path[0] || !g_videoState) return;
    static int s_frames = 0;
    const char* at = getenv("JJFFE_DUMP_MISSION_FRAME_AT");
    if (++s_frames < ((at && at[0]) ? atoi(at) : 1)) return;
    DumpMissionDpalTables("post");
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mission frame dump: cannot open %s\n", path);
        return;
    }
    fwrite(g_videoState->backBuffer, 1, SCREEN_WIDTH * SCREEN_HEIGHT, f);
    fclose(f);
    {
        char ppath[1024];
        snprintf(ppath, sizeof(ppath), "%s.pal", path);
        PlatformDumpPalette(ppath);
    }
    WriteFramePoseSidecar(path, "mission-frame");
    MaybeDumpMissionPlayer(path);
    MaybeDumpConsoleButtons(path);
    MaybeDumpWorldBodies();
    s_missionFrameDumped = true;
    fprintf(stderr, "MISSION-DUMP -> %s\n", path);
}

// Paint objects for planet mesh rendering — must outlive FUNC_001789 flush.
alignas(16) static uint8_t s_missionPlanetPaint[kWorldBodyCapacity][0x152];

// Temporary GameObjs for station mesh rendering (DrawObject3D / RenderShipFlat).
static GameObj s_missionStationObj[kWorldBodyCapacity];

// Pre-4.39 A/B only (JJFFE_SYNTH_CONSOLE_PLAYER=1): an ASM-layout player made
// up from the GameObj, which that knob binds as DATA_008861. By default
// DATA_008861 is FUNC_000996's pool ship, as in the ASM.
// Below 4 GiB: DATA_008861 is a 32-bit slot, as in the ASM.
static PhysObj& s_consolePlayer = Below4G<PhysObj, 724>();

// Re-initialised every call, with the gauge fields made up from the GameObj.
static void SynthesiseConsolePlayer(const GameObj& ps)
{
    PhysObjInit(&s_consolePlayer);
    PhysObjSetU8(&s_consolePlayer, 0xd0, 0);
    const uint32_t shieldFP = static_cast<uint32_t>(
        ps.shieldMax > 0
            ? (static_cast<int64_t>(ps.shields) << 16) / ps.shieldMax
            : 0);
    PhysObjSetI32(&s_consolePlayer, 0x11e, static_cast<int32_t>(shieldFP));
    const uint16_t fuelGage = static_cast<uint16_t>(
        ps.fuel > 0 ? (ps.fuel / FP_ONE) & 0xffff : 0);
    PhysObjSetU16(&s_consolePlayer, 0xea, fuelGage);
    PhysObjSetU8(&s_consolePlayer, 0xca, 4);
}

// DATA_008861 stays on the pool ship FUNC_000996 binds (ffe.asm 74093; the
// ASM's other writers, FUNC_001098 / FUNC_001105, also store pool objects), so
// FUNC_001099, FUNC_000270 / 000274 and the logic read the ASM's player.
// R1180/R1181: expose DATA_008867 (the altitude-about-Hope dword) for the state trace; static in core.cpp.
int32_t MissionAltitudeCounter(void);

static void SyncConsolePlayerFromShip()
{
    if (!ConsolePlayerSynthesised()) return;

    const GameObj& ps = g_gameState.playerShip;
    SynthesiseConsolePlayer(ps);
    PhysObjSetParent(&s_consolePlayer, ps.shipId);
    PhysObjSetIndex(&s_consolePlayer, ps.playerId);

    // ASM FUNC_001099: Act speed = FUNC_001466(FUNC_001475([ebx+0xc8]+0x8c))
    // where [ebx+0xc8] is the player PhysObj (DATA_008861). +0x8c is the
    // 3×int32 velocity vector (not the GameObj scaffolding vel at +0x14).
    for (int i = 0; i < 3; ++i) {
        PhysObjSetI32(&s_consolePlayer, 0x8c + static_cast<size_t>(i) * 4, ps.vel[i]);
    }

    // Overlay ASM-faithful spawn identity (TY-198 / Hope) when available.
    if (const PhysObj* spawned = PlayerSpawnPhysObj()) {
        PhysObjSetName(&s_consolePlayer, PhysObjGetName(spawned));
        PhysObjSetModel(&s_consolePlayer, PhysObjGetModel(spawned));
        PhysObjSetControl(&s_consolePlayer, PhysObjGetControl(spawned));
        PhysObjSetFlags(&s_consolePlayer, PhysObjGetFlags(spawned));
        PhysObjSetAiMode(&s_consolePlayer, PhysObjGetAiMode(spawned));
        PhysObjSetParent(&s_consolePlayer, PhysObjGetParent(spawned));
        BindMissionObjectTable(WorldPhysObjs(), WorldBodyCount());
    } else {
        BindMissionObjectTable(nullptr, 0);
    }

    BindMissionPlayerObject(&s_consolePlayer);
}

// Starfield (portable from title screen)
struct Star {
    int x, y;
    uint8_t brightness;
    int speed;
};
static Star s_stars[100];
static bool s_starsInit = false;

// The Phase-60 weapon cooldown is retired: fire now runs through the ASM's own
// FUNC_001056 -> FUNC_001040 -> FUNC_000931 chain, whose recharge gates the rate.

// Frame state as DrawMission found it, for the .pose sidecar: the starfield
// and DPal are rewritten during the frame, and their pre-frame values depend
// on the whole session history (DPal Pass1 reuses persisted dPalRGB slots;
// dPalBaseIndex flips on every FlipScreen, title and menu frames included).
// The DATA_008815..817 sky record carries over too: FUNC_001877's no-shade
// exit reuses the previous frame's (ffe.asm 133056).
static_assert(sizeof(s_stars) / sizeof(s_stars[0]) == kFramePoseStarCount,
              "pose sidecar star count");
static Star s_preFrameStars[kFramePoseStarCount];
static int s_preFrameDpalBase = 0;
static uint8_t s_preFrameDpalRgb[kFramePoseDpalRgbBytes];
static int32_t s_preFrameSky[3] = {};
static bool s_preFrameSkyLast = false;
static bool s_frameDrewStars = false;

// Task 4.13 frame replay:
//   JJFFE_REPLAY_POSE=<frame.raw.pose>  after the normal spawn placement,
//       InitMission loads the sidecar and every mission frame then holds its
//       state: ship/camera/NPC pose, the docked / orbital-view / look-pitch
//       flags (pinned after UpdateMission's physics and AI), and the
//       pre-frame starfield + DPal (restored when DrawMission starts). Run the
//       recorded `frame` count (JJFFE_DUMP_LIVE_AFTER) to land on the same
//       world state; tools/replay_capture.sh does this and diffs the result.
//       An unreadable / malformed / mismatched sidecar exits with status 3 —
//       never a silent fallback to the default pose.
//   JJFFE_REPLAY_POSE_LATE=1  apply it only from the recorded frame on, so the
//       frames before it render the normal spawn. Diagnostic: proves the
//       sidecar carries every history-dependent input a live F12 capture has.
static bool s_replayActive = false;
static bool s_replayLate = false;
static FramePose s_replayPose;

[[noreturn]] static void ReplayPoseFatal(const std::string& msg)
{
    fprintf(stderr, "REPLAY-POSE: FATAL %s\n", msg.c_str());
    std::exit(3);
}

static void LoadReplayPose()
{
    s_replayActive = false;
    const char* path = getenv("JJFFE_REPLAY_POSE");
    if (!path || !path[0]) return;

    std::string err;
    if (!FramePoseRead(path, &s_replayPose, &err)) ReplayPoseFatal(err);
    // Sidecars before task 4.27 record the ten placeholder GameObj NPCs, which
    // no longer exist. Like an env knob, a different NPC set is reported but
    // allowed; its entries are not applied and the camera/ship pose replays
    // exactly.
    if (static_cast<int>(s_replayPose.npcs.size()) != g_gameState.objectCount) {
        fprintf(stderr, "REPLAY-POSE: WARNING %s: npc.count %zu, this mission has %d; "
                "NPC entries not applied\n", path, s_replayPose.npcs.size(),
                g_gameState.objectCount);
        s_replayPose.npcs.clear();
    }
    // The env knobs are the replay tool's job; a mismatch is reported (and
    // shows up again in the replayed sidecar) but allowed, so a knob can be
    // flipped on purpose against a recorded pose.
    const auto now = FramePoseCurrentEnv();
    for (const auto& e : s_replayPose.env) {
        const auto it = std::find_if(now.begin(), now.end(),
                                     [&](const auto& n) { return n.first == e.first; });
        if (it == now.end() || it->second != e.second)
            fprintf(stderr, "REPLAY-POSE: WARNING recorded %s=%s, now %s\n", e.first.c_str(),
                    e.second.c_str(), it == now.end() ? "unset" : it->second.c_str());
    }
    for (const auto& n : now) {
        const bool recorded = std::any_of(s_replayPose.env.begin(), s_replayPose.env.end(),
                                          [&](const auto& e) { return e.first == n.first; });
        if (!recorded)
            fprintf(stderr, "REPLAY-POSE: WARNING %s=%s is set but was not recorded\n",
                    n.first.c_str(), n.second.c_str());
    }
    const char* late = getenv("JJFFE_REPLAY_POSE_LATE");
    s_replayLate = late && late[0] == '1';
    s_replayActive = true;
    fprintf(stderr, "REPLAY-POSE: %s source=%s frame=%d%s\n", path,
            s_replayPose.source.c_str(), s_replayPose.frame,
            s_replayLate ? " (late: from the recorded frame only)" : "");
}

static bool ReplayPoseDue()
{
    return s_replayActive && (!s_replayLate || s_missionTime >= s_replayPose.frame);
}

static void ApplyReplayPose()
{
    const FramePose& p = s_replayPose;
    GameObj& ship = g_gameState.playerShip;
    std::memcpy(ship.pos, p.shipPos, sizeof(ship.pos));
    std::memcpy(ship.vel, p.shipVel, sizeof(ship.vel));
    std::memcpy(ship.rot, p.shipRot, sizeof(ship.rot));
    ship.fuel = p.shipFuel;
    ship.shields = p.shipShields;
    ship.hull = p.shipHull;
    ship.shieldMax = p.shipShieldMax;
    ship.hullMax = p.shipHullMax;
    ship.targetId = static_cast<uint8_t>(p.shipTarget);
    g_gameState.isDocked = p.isDocked;
    s_orbitalHopeView = p.orbitalView;
    s_missionLookPitchFp16 = p.lookPitchFp16;
    MissionSetCameraRender(p.camPos[0], p.camPos[1], p.camPos[2]);
    int64_t vec[3];
    if (OrbitalVec64Env(vec)) {
        MissionSetCameraFfe(vec);
        ship.pos[0] = s_missionCamera.position.x;
        ship.pos[1] = s_missionCamera.position.y;
        ship.pos[2] = s_missionCamera.position.z;
    }
    std::memcpy(s_missionCamera.rotation.m, p.camRot, sizeof(p.camRot));
    for (int i = 0; i < static_cast<int>(p.npcs.size()) && i < g_gameState.objectCount; ++i) {
        GameObj* obj = GetGameObject(i);
        if (!obj) continue;
        const FramePoseObj& o = p.npcs[static_cast<size_t>(i)];
        obj->shipId = static_cast<uint8_t>(o.shipId);
        obj->flags = static_cast<uint8_t>(o.flags);
        obj->hull = o.hull;
        std::memcpy(obj->pos, o.pos, sizeof(obj->pos));
        std::memcpy(obj->vel, o.vel, sizeof(obj->vel));
        std::memcpy(obj->rot, o.rot, sizeof(obj->rot));
    }
}

static void ApplyReplayPreFrame()
{
    const FramePose& p = s_replayPose;
    if (p.hasSkyPre) {
        DATA_008815 = static_cast<uint8_t>(p.skyPre[0]);
        DATA_008816 = static_cast<uint8_t>(p.skyPre[1]);
        DATA_008817 = static_cast<uint16_t>(p.skyPre[2]);
    }
    if (p.hasSkyLast) DATA_008814 = p.skyLast ? 0xffffffffu : 0u;
    for (int i = 0; i < kFramePoseStarCount; ++i) {
        const FramePoseStar& s = p.stars[static_cast<size_t>(i)];
        s_stars[i] = {s.x, s.y, static_cast<uint8_t>(s.brightness), s.speed};
    }
    if (!g_videoState) return;
    g_videoState->dPalBaseIndex = p.dpalBase;
    std::memcpy(g_videoState->dPalRGB, p.dpalRgb, sizeof(g_videoState->dPalRGB));
    // The dynamic VGA bank mirrors dPalRGB (LoadGamePalette seeds both; the
    // build installs every slot it changes).
    for (int i = 0; i < 128; ++i)
        SetPaletteEntry(0x80 + i, static_cast<uint8_t>(p.dpalRgb[i * 3 + 0] << 2),
                        static_cast<uint8_t>(p.dpalRgb[i * 3 + 1] << 2),
                        static_cast<uint8_t>(p.dpalRgb[i * 3 + 2] << 2));
}

static void SnapshotPreFrameState()
{
    std::memcpy(s_preFrameStars, s_stars, sizeof(s_stars));
    s_preFrameSky[0] = DATA_008815;
    s_preFrameSky[1] = DATA_008816;
    s_preFrameSky[2] = DATA_008817 & 0xff;
    s_preFrameSkyLast = DATA_008814 != 0;
    if (!g_videoState) return;
    s_preFrameDpalBase = g_videoState->dPalBaseIndex;
    std::memcpy(s_preFrameDpalRgb, g_videoState->dPalRGB, sizeof(s_preFrameDpalRgb));
}

static std::string PoseFmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string PoseFmt(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

static void WriteFramePoseSidecar(const char* rawPath, const char* source)
{
    FramePose p;
    p.source = source;
    p.frame = s_missionTime;
    p.liveFrame = s_liveFrame;
    p.isDocked = g_gameState.isDocked;
    p.orbitalView = s_orbitalHopeView;
    p.lookPitchFp16 = s_missionLookPitchFp16;
    const GameObj& ship = g_gameState.playerShip;
    std::memcpy(p.shipPos, ship.pos, sizeof(p.shipPos));
    std::memcpy(p.shipVel, ship.vel, sizeof(p.shipVel));
    std::memcpy(p.shipRot, ship.rot, sizeof(p.shipRot));
    p.shipFuel = ship.fuel;
    p.shipShields = ship.shields;
    p.shipHull = ship.hull;
    p.shipShieldMax = ship.shieldMax;
    p.shipHullMax = ship.hullMax;
    p.shipTarget = ship.targetId;
    p.camPos[0] = s_missionCamera.position.x;
    p.camPos[1] = s_missionCamera.position.y;
    p.camPos[2] = s_missionCamera.position.z;
    std::memcpy(p.camRot, s_missionCamera.rotation.m, sizeof(p.camRot));
    for (int i = 0; i < g_gameState.objectCount; ++i) {
        const GameObj* obj = GetGameObject(i);
        FramePoseObj o;
        if (obj) {
            o.shipId = obj->shipId;
            o.flags = obj->flags;
            o.hull = obj->hull;
            std::memcpy(o.pos, obj->pos, sizeof(o.pos));
            std::memcpy(o.vel, obj->vel, sizeof(o.vel));
            std::memcpy(o.rot, obj->rot, sizeof(o.rot));
        }
        p.npcs.push_back(o);
    }
    for (const Star& s : s_preFrameStars)
        p.stars.push_back({s.x, s.y, s.brightness, s.speed});
    p.dpalBase = s_preFrameDpalBase;
    std::memcpy(p.dpalRgb, s_preFrameDpalRgb, sizeof(p.dpalRgb));
    p.hasSkyPre = true;
    std::memcpy(p.skyPre, s_preFrameSky, sizeof(p.skyPre));
    p.hasSkyLast = true;
    p.skyLast = s_preFrameSkyLast ? 1 : 0;
    p.env = FramePoseCurrentEnv();

    // Recomputed every frame from the state above; a replay must reproduce them.
    int32_t hx = 0, hy = 0, hz = 0;
    int64_t hopeG[3] = {}, starG[3] = {};
    if (const PhysObj* hope = WorldFind("Hope")) {
        const int hopeIdx = HopeWorldIndex(hope);
        if (hopeIdx >= 0) {
            WorldGetPos(hopeIdx, &hx, &hy, &hz);
            WorldGetFfeGlobal(hopeIdx, hopeG);
        }
    }
    if (WorldGatewayStarIndex() >= 0) WorldGetFfeGlobal(WorldGatewayStarIndex(), starG);
    int liveProj = 0, liveExpl = 0;
    for (int i = 0; i < GetMaxProjectiles(); ++i)
        if (GetProjectile(i)) ++liveProj;
    for (int i = 0; i < GetMaxExplosions(); ++i)
        if (GetExplosion(i)) ++liveExpl;
    p.checks = {
        {"hope_pos", PoseFmt("%d,%d,%d", hx, hy, hz)},
        {"star_to_hope", PoseFmt("%lld,%lld,%lld", static_cast<long long>(hopeG[0] - starG[0]),
                                 static_cast<long long>(hopeG[1] - starG[1]),
                                 static_cast<long long>(hopeG[2] - starG[2]))},
        {"altitude", PoseFmt("%d", AltitudeAboveHope())},
        {"stars_drawn", PoseFmt("%d", s_frameDrewStars ? 1 : 0)},
        {"sky", PoseFmt("0x%x,%u,%u,%u", DATA_008814, DATA_008815, DATA_008816,
                        static_cast<unsigned>(DATA_008817))},
        {"haze", PoseFmt("0x%x,%d,0x%x,%d", DATA_009262, static_cast<int32_t>(DATA_009263),
                         DATA_009265, static_cast<int32_t>(DATA_009267))},
        {"lod", PoseFmt("%u", DATA_008812)},
        {"fill", PoseFmt("0x%02x", GetRenderFillColor() & 0xffu)},
        {"hud", PoseFmt("0x%08x,0x%x", GetGameClock(), GetConsoleModeIconBmp())},
        {"prng", PoseFmt("0x%08x,0x%08x", static_cast<uint32_t>(g_gameState.prngState1),
                         static_cast<uint32_t>(g_gameState.prngState2))},
        {"combat", PoseFmt("%d,%d,%d", liveProj, liveExpl, GetPlayerDamageThisFrame())},
    };

    char path[1024];
    snprintf(path, sizeof(path), "%s.pose", rawPath);
    if (!FramePoseWrite(path, p))
        fprintf(stderr, "frame pose: cannot write %s\n", path);
    fprintf(stderr, "frame pose: frame=%d camera_ffe=%lld,%lld,%lld\n", s_missionTime,
            static_cast<long long>(s_missionCameraFfe[0]),
            static_cast<long long>(s_missionCameraFfe[1]),
            static_cast<long long>(s_missionCameraFfe[2]));
    fprintf(stderr, "frame draw space: frame=%d render_reads=%llu anchor_reads=%llu\n",
            s_missionTime, static_cast<unsigned long long>(s_drawRenderReads),
            static_cast<unsigned long long>(s_drawAnchorReads));
}

// Ship mesh rendering - uses the 3D mesh pipeline
// Each ship's model index is stored in the object's shipId field
// (mapped via DATA_004681 to ShipModel structs)

// Helper: render a game object using the 3D mesh pipeline
// lodViewZ: view-space depth for the LOD select; INT32_MIN keeps the
// historical obj->pos[2] (NPC ships under the unrotated mission camera).
static void DrawObject3D(const Camera3D* camera, GameObj* obj, bool isPlayer, int objIndex,
                         int32_t lodViewZ = INT32_MIN)
{
    const bool dbg = getenv("JJFFE_DEBUG_MISSION") != nullptr;
    static int dbgCount = 0;

    // Get ship model from the object's model index
    // For now, use shipId as model index (will be refined during integration)
    int modelIndex = obj->shipId;
    const ShipModel* model = GetShipModel(modelIndex);

    if (!model) {
        // No model found - draw a small indicator dot instead
        Point3D objPos;
        objPos.x = obj->pos[0];
        objPos.y = obj->pos[1];
        objPos.z = obj->pos[2];

        ProjResult proj;
        if (ProjectPoint(camera, &objPos, &proj) && proj.visible) {
            DrawPixel(proj.sx, proj.sy, 0x0B);
        }
        return;
    }

    // Parse mesh bytecode
    const uint8_t* meshData = GetShipModelPointer(model->meshDataOffset);
    if (!meshData) return;

    // Set the LOD scale/distance context before parsing (ASM render
    // ctx+0x148 = scaleRelated, ctx+0xc = |viewZ|), matching Demo3D, and use
    // the model's real bytecode size (a hard 512 truncates vertex data).
    SetMeshParseScaleRelated(static_cast<int32_t>(model->scaleRelated));
    SetMeshParseDistance(MeshParseDistanceFromViewZ(
        lodViewZ != INT32_MIN ? lodViewZ : static_cast<int32_t>(obj->pos[2])));
    const int prevParseModel = GetMeshParseModelId();
    SetMeshParseModelId(modelIndex);
    size_t meshSize = MeshBytecodeSize(modelIndex);
    if (meshSize == 0) meshSize = 512;
    MeshData mesh = ParseMeshBytecode(meshData, meshSize);
    SetMeshParseModelId(prevParseModel);
    SetMeshParseDistance(-1);
    SetMeshParseScaleRelated(0);
    if (!mesh.valid || mesh.polygons.empty()) return;

    // Transform vertices
    TransformedPoly polys[128];
    int32_t rotTmp[9]; int32_t posTmp[3]; std::memcpy(rotTmp, obj->rot, sizeof(rotTmp)); std::memcpy(posTmp, obj->pos, sizeof(posTmp));
    int visibleCount = TransformShipVertices(model, mesh, rotTmp, posTmp, camera, polys, 128);

    if (dbg && dbgCount < 20) {
        Point3D p{obj->pos[0], obj->pos[1], obj->pos[2]};
        ProjResult pr; ProjectPoint(camera, &p, &pr);
        fprintf(stderr, "NPC[%d] shipId=%d: polys=%zu vis=%d proj=(%d,%d)\n",
                objIndex, modelIndex, mesh.polygons.size(), visibleCount,
                (int)pr.sx, (int)pr.sy);
        dbgCount++;
    }

    // Render visible polygons
    if (visibleCount > 0) {
        RenderShipFlat(polys, visibleCount, model, rotTmp);
    }
}

void SetBackBufferPtr(uint8_t* ptr);   // core.cpp

bool InitMission(int32_t startParam)
{
    // The ASM's buffer structures DATA_007773/7774 are initialised to the back buffer
    // (ffedat.asm:24838-24845). The port sets them in the intro, so a --mission start left
    // DATA_007774 null and every FUNC_001623_BlitIndex (the F2 page's cabin backdrop) drew nothing.
    if (g_videoState) SetBackBufferPtr(g_videoState->backBuffer);

    // T-59-07: Check allocation success
    g_gameState.objects = static_cast<GameObj*>(
        std::malloc(MAX_GAME_OBJECTS * GAME_OBJECT_SIZE));
    if (!g_gameState.objects) {
        fprintf(stderr, "InitMission: failed to allocate game objects array\n");
        return false;
    }

    // Zero-initialize all game objects
    std::memset(g_gameState.objects, 0, MAX_GAME_OBJECTS * GAME_OBJECT_SIZE);
    g_gameState.objectCount = 0;

    // Set mission state — landed spawn (AI mode 0x24) is applied below.
    g_gameState.isDocked = true;
    g_gameState.isPaused = false;
    g_gameState.farClip = 100 * FP_ONE;

    // Initialize player ship (GameObj scaffolding; ASM identity via SpawnPlayerLanded).
    std::memset(&g_gameState.playerShip, 0, sizeof(GameObj));
    g_gameState.playerShip.pos[0] = 0;
    g_gameState.playerShip.pos[1] = 0;
    g_gameState.playerShip.pos[2] = 0;
    g_gameState.playerShip.shipId = 0x1f;  // TY-198 model
    g_gameState.playerShip.playerId = 1;
    g_gameState.playerShip.targetId = 0xFF;
    g_gameState.playerShip.fuel = 1000 * FP_ONE;
    g_gameState.playerShip.mass = 100 * FP_ONE;
    g_gameState.playerShip.shields = 100 * FP_ONE;
    g_gameState.playerShip.hull = 100 * FP_ONE;
    g_gameState.playerShip.shieldMax = 100 * FP_ONE;
    g_gameState.playerShip.hullMax = 100 * FP_ONE;
    g_gameState.playerShip.damageFlags = 0;

    // Identity rotation matrix
    g_gameState.playerShip.rot[0] = FP_ONE; g_gameState.playerShip.rot[1] = 0;
    g_gameState.playerShip.rot[2] = 0;
    g_gameState.playerShip.rot[3] = 0;     g_gameState.playerShip.rot[4] = FP_ONE;
    g_gameState.playerShip.rot[5] = 0;
    g_gameState.playerShip.rot[6] = 0;     g_gameState.playerShip.rot[7] = 0;
    g_gameState.playerShip.rot[8] = FP_ONE;

    // Initialize camera
    CameraInit(&s_missionCamera);
    MissionSetCameraRender(0, 0, -3 * FP_ONE);

    // Load mission data
    data::LoadMissionData();

    // Task 4.27: no GameObj NPCs. The new-game traffic ships (FUNC_000688)
    // live in the world ship list (WorldShipObjs) and are drawn by the
    // FUNC_001680 object pass in DrawMission; their AI is not ported.

    // Assign AI behaviors to spawned NPCs
    AIBehaviorInit();
    CombatInit();  // Initialize projectile system

    // Initialize mesh parser for ship rendering
    InitMeshOpcodeHandlers();

    // Ensure flight view uses the game palette (not a leftover title/scene palette)
    LoadGamePalette();
    // Build the dynamic palette from the freshly-loaded game palette so ship
    // materials resolve to non-black VGA indices (DPal16toPal returns 0 on miss).
    DPalClear();
    DPalBuild(g_videoState->palette, 0);

    // Initialize starfield
    if (!s_starsInit) {
        for (int i = 0; i < 100; i++) {
            s_stars[i].x = rand() % 320;
            s_stars[i].y = rand() % 200;
            s_stars[i].brightness = (rand() % 4) + 1;
            s_stars[i].speed = (rand() % 3) + 1;
        }
        s_starsInit = true;
    }

    // Initialize ship controls (zeros velocity, resets input state)
    ShipControlsInit();

    if (!WorldLoadSol(startParam)) {
        fprintf(stderr, "InitMission: WorldLoadSol failed (continuing with ships only)\n");
    }

    // Initialize game logic for mission context
    GameLogicInit();

    // ASM landed spawn identity (TY-198 parented to Hope). Default pose is
    // alt 0 looking at the horizon (task 4.6 / ffe_flow_postintro). Set
    // JJFFE_ORBITAL_HOPE=1 for the ae605ca space view (nose at planet centre).
    // Optional JJFFE_ORBITAL_ALT=<int> overrides the default altitude 2500
    // (same units as HopeSurfaceRadius / WorldGetPos) for limb-matched A/B;
    // 0 sits on the terrain sphere (PlacePlayerSpaceAboveHope clamps to +1).
    // JJFFE_ORBITAL_YAW/PITCH/ROLL (degrees) turn that pose; see
    // ApplyOrbitalHeading.
    if (!SpawnPlayerLanded()) {
        fprintf(stderr, "InitMission: SpawnPlayerLanded failed (Hope missing?)\n");
    }
    const char* orbital = getenv("JJFFE_ORBITAL_HOPE");
    if (orbital && orbital[0] == '1') {
        int32_t orbitalAlt = 2500;
        if (const char* altEnv = getenv("JJFFE_ORBITAL_ALT")) {
            char* end = nullptr;
            const long parsed = strtol(altEnv, &end, 10);
            if (end != altEnv && parsed >= 0 && parsed <= 2000000000L) {
                orbitalAlt = static_cast<int32_t>(parsed);
            }
        }
        if (!PlacePlayerSpaceAboveHope(orbitalAlt)) {
            fprintf(stderr, "InitMission: PlacePlayerSpaceAboveHope failed\n");
            g_gameState.isDocked = true;
        }
    } else if (!PlacePlayerLandedOnHope()) {
        fprintf(stderr, "InitMission: PlacePlayerLandedOnHope failed\n");
        g_gameState.isDocked = true;
    }
    LoadReplayPose();
    if (ReplayPoseDue()) ApplyReplayPose();

    // Reset mission time and key states
    s_missionTime = 0;
    std::memset(s_keyHeld, 0, sizeof(s_keyHeld));

    // Back-buffer ptr for FUNC_001111; its mark gates stay with the ASM writers.
    EnableMissionHud();

    // The flight buttons come from FUNC_000147's stage 1 (FUNC_001000 ->
    // FUNC_001003). FUNC_000331 opens a text panel and is not on the ASM's
    // new-game path; JJFFE_CONSOLE_PANEL_SEED=1 restores the pre-4.40 call.
    if (ConsolePanelSeeded()) InitConsolePanel(0, 0, 0, 0, 0, 0);
    SyncConsolePlayerFromShip();

    // FUNC_000919's timebase stores precede its title loop and FUNC_000147.
    ResetMissionTimebase();

    // FUNC_000147 (ffe.asm:68669): registry, new-game seed, clock / date, then
    // module action 0 for stages 1-4. The ASM's world setup happens inside
    // those stages, so they run after the port's own setup above and are the
    // last writes before the first frame, as in the ASM.
    InitGameStateWithParam(startParam);

    // R1233: JJFFE_LIVE_ACCEL=<u32> seeds DATA_009012 (+ DATA_007706) the same way
    // REPLAY_ACCEL / asm_replay_scenario do after FUNC_000147. Live --mission leaves
    // DATA_009012 at 0, so a two-Esc SysMenu script (stop-time then open) opens on the
    // first Esc and closes on the second. Inert unset; DialogPixels sets 0x3e2 from the
    // scenario accel field. Static getenv once.
    {
        static int s_liveAccelOnce = 0;
        if (!s_liveAccelOnce) {
            s_liveAccelOnce = 1;
            if (const char* e = std::getenv("JJFFE_LIVE_ACCEL"); e && e[0]) {
                const uint32_t a = static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
                DATA_007706 = a;
                DATA_009012 = a;
            }
        }
    }

    return true;
}

// R1148 test hook: JJFFE_POKE_PLAYER="<frame>:<off>=<val>" or "<frame>:<off>|<mask>" edits one byte of
// the player object (DATA_008861) at the start of that frame's update - the same spec string that
// tools/asm_player_poke.py applies to ffeasm at that frame's TimerAdvance, so the two sides make
// demonstrably the same edit. Used by ViewModeAB to unlock the views the start ship's spec /
// equipment gate (F8: +0xd1 > 2, F11: +0xc8 & 0x40). Inert unless set; needs JJFFE_DUMP_LIVE, whose
// hook counts the frames (s_liveFrame).
static void MaybePokePlayer()
{
    static bool s_done = false;
    const char* e = getenv("JJFFE_POKE_PLAYER");
    if (s_done || !e || !e[0]) return;
    unsigned frame = 0, off = 0, val = 0;
    char op = 0;
    if (std::sscanf(e, "%u:%i%c%i", &frame, &off, &op, &val) != 4 || (op != '=' && op != '|') ||
        off >= 0x152) {
        fprintf(stderr, "JJFFE_POKE_PLAYER: cannot parse '%s'\n", e);
        s_done = true;
        return;
    }
    if (static_cast<unsigned>(s_liveFrame) + 1 != frame) return;   // the frame being run now
    uint8_t* pl = const_cast<uint8_t*>(static_cast<const uint8_t*>(GetMissionPlayerObject()));
    if (!pl) return;
    pl[off] = (op == '=') ? static_cast<uint8_t>(val) : static_cast<uint8_t>(pl[off] | val);
    fprintf(stderr, "POKE-PLAYER frame=%u +%#x%c%#x -> %#x\n", frame, off, op, val, pl[off]);
    s_done = true;
}


void UpdateMission()
{
    MaybePokePlayer();
    s_missionTime++;
    g_gameState.frameCount++;
    TimerAdvance();
    // Once per frame only: GameLoopUpdate's trailing TimerAdvance() just adds
    // its wall delta to DATA_009157, which the next frame's tick takes.
    MissionFrameClockTick();
    // R1177: FUNC_001056 now runs inside FUNC_000152 at module 3's held slot, before the action-10
    // modules, matching ffeasm's ordering. No longer called here.
    ShipControlsUpdateRealTime();
    AIBehaviorUpdate();    // AI moves after player
    CombatUpdate();        // Process projectiles, collisions, destruction

    // Orbital bodies: advance angle (+0xba) by angVel (+0xc2) and refresh pose.
    WorldAdvanceOrbits(1);

    // Integrate NPC positions from AI-computed velocities
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = GetGameObject(i);
        if (!obj || obj->playerId != 0) continue;
        obj->pos[0] += obj->vel[0] >> 4;
        obj->pos[1] += obj->vel[1] >> 4;
        obj->pos[2] += obj->vel[2] >> 4;
    }

    // R1245: pull GameObj + FFE camera from DATA_008861 once airborne. Without
    // this the cockpit sat on the InitMission pad pose forever.
    SyncPlayerShipFromPool();

    // Camera: FUNC_001054 (core.cpp) is the ASM's own per-view-mode transform.
    // MissionViewCamera() mirrors its scratch-object math exactly (the eye
    // offset via FUNC_001053, the negated rear basis, the orbital pans and the
    // spec-driven side offsets) without its render side effect, so every mode
    // now moves the camera the way the ASM does it. First-person follows the
    // synced pool ship (above); modes 1..4 take MissionViewCamera's deltas.
    const GameObj& ship = g_gameState.playerShip;
    // SyncPlayerShipFromPool already called MissionSetCameraFfe when airborne;
    // otherwise keep the render-cell follow of ship.pos (pad / orbital A/B /
    // CameraFfeIdentity knobs where DATA_008870 never reaches 0x24).
    {
        const int st = static_cast<int>(MissionGameModeState());
        if (!(st == 0x24 || st == 0x26 || st == 2))
            MissionSetCameraRender(ship.pos[0], ship.pos[1], ship.pos[2]);
    }

    int32_t viewCam[12] = {};
    int32_t skipPlayerSlot = 0;
    MissionViewCamera(viewCam, &skipPlayerSlot);

    // First-person is mode 0: the camera sits at the ship's own eye. 1..6 are the views
    // FUNC_001054 transforms (5 = the missile's own camera, R1158; 6 = combat view, R1152).
    const int vmode = MissionViewMode();
    const bool firstPerson = !(vmode >= 1 && vmode <= 6);
    (void)skipPlayerSlot;

    if (firstPerson) {
        // First-person basis = pool ship's Q31 >> 15 (synced above when flying;
        // pad placement otherwise). Chase views use MissionViewCamera instead.
        for (int i = 0; i < 9; ++i) s_missionCamera.rotation.m[i] = ship.rot[i];
    } else {
        // Chase views: the rows arrive already in 16.16 (right, up, forward);
        // lay them out column-major for Camera3D.
        for (int axis = 0; axis < 3; ++axis)
            for (int c = 0; c < 3; ++c)
                s_missionCamera.rotation.m[3 * c + axis] = viewCam[3 * axis + c];
    }

    // The camera's own position delta (orbital zoom / side-view / rear-view
    // offset), applied on top of the ship pivot. First-person returns a zero
    // delta: its cockpit eye is MissionFrontViewEye's job for the ASM passes.
    if (!firstPerson) {
        s_missionCamera.position.x += viewCam[9];
        s_missionCamera.position.y += viewCam[10];
        s_missionCamera.position.z += viewCam[11];
    }

    // DATA_008838: the slot whose hull the object pass must skip. Any mode
    // other than first-person leaves it 0, so the player hull is drawn - except
    // mode 5, whose JUMP_004340 never writes it: the previous frame's value stands.
    if (vmode != 5) s_viewCameraSkipsPlayer = firstPerson;

    if (ReplayPoseDue()) ApplyReplayPose();
}

// For tests: the camera rotation UpdateMission() just computed, independent
// of the ship's own (MissionViewMode() == 0 in every mode but matters once
// the view switches away from first-person).
void GetMissionCameraRotation(int32_t out[9])
{
    for (int i = 0; i < 9; i++) out[i] = s_missionCamera.rotation.m[i];
}

// Port-only drifting starfield (no counterpart in the ASM flight frame).
static void DrawMissionStarfield(uint8_t* backBuffer)
{
    {
        const auto& ps = g_gameState.playerShip;

        // Project velocity into ship-local axes (right, up, forward)
        int32_t vDotRight = (int32_t)(((int64_t)ps.vel[0] * ps.rot[0] +
                                       (int64_t)ps.vel[1] * ps.rot[3] +
                                       (int64_t)ps.vel[2] * ps.rot[6]) >> 16);
        int32_t vDotUp    = (int32_t)(((int64_t)ps.vel[0] * ps.rot[1] +
                                       (int64_t)ps.vel[1] * ps.rot[4] +
                                       (int64_t)ps.vel[2] * ps.rot[7]) >> 16);
        int32_t vDotFwd   = (int32_t)(((int64_t)ps.vel[0] * ps.rot[2] +
                                       (int64_t)ps.vel[1] * ps.rot[5] +
                                       (int64_t)ps.vel[2] * ps.rot[8]) >> 16);

        // Clip stars to 3D viewport (y < 0x9E / 158) — console band is below
        constexpr int kViewportBottom = 0x9E;
        for (int i = 0; i < 100; i++) {
            int sx = s_stars[i].x;
            int sy = s_stars[i].y;
            if (sx >= 0 && sx < 320 && sy >= 0 && sy < kViewportBottom)
                backBuffer[sy * 320 + sx] = s_stars[i].brightness;

            // Lateral drift: stars move opposite to lateral velocity
            int driftX = -(int)(((int64_t)vDotRight * s_stars[i].speed) >> 14);
            int driftY =  (int)(((int64_t)vDotUp    * s_stars[i].speed) >> 14);

            // Forward zoom: stars radiate outward from screen center
            int dx = sx - 160;
            int dy = sy - 100;
            int zoomX = (int)(((int64_t)dx * vDotFwd * s_stars[i].speed) >> 22);
            int zoomY = (int)(((int64_t)dy * vDotFwd * s_stars[i].speed) >> 22);

            s_stars[i].x += driftX + zoomX;
            s_stars[i].y += driftY + zoomY;

            if (s_stars[i].x < 0 || s_stars[i].x >= 320 ||
                s_stars[i].y < 0 || s_stars[i].y >= kViewportBottom) {
                s_stars[i].x = 160 + (rand() % 40) - 20;
                s_stars[i].y = 100 + (rand() % 30) - 15;
                s_stars[i].brightness = (rand() % 4) + 1;
                s_stars[i].speed = (rand() % 3) + 1;
            }
        }
    }
}

// Passes that paint straight into the back buffer instead of enqueueing
// FUNC_001789 jobs. They run after ClearUpperBuf (which would wipe them) and
// before the flush, so terrain jobs still paint over them.
static void DrawMissionDirectPaint(uint8_t* backBuffer, bool drawStars)
{
    if (drawStars)
        DrawMissionStarfield(backBuffer);

    // 1st-person cockpit: don't draw the player ship (camera IS the ship)
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = GetGameObject(i);
        if (!obj) continue;
        if (obj->hull <= 0) continue;
        DrawObject3D(&s_missionCamera, obj, false, i);
    }
}

void DrawMission()
{
    // R1180: with JJFFE_TRACE_STATE=1, record the game-mode state and DATA_008867 each drawn mission frame.
    // A surface launch must climb out of state 0x24 once DATA_008867 passes 200000 (FUNC_001013); if the HUD
    // sync clobbers that counter the state never advances. tools/state_sequence_ctest.sh compares this against
    // ffeasm's own per-frame sequence for the targeting scenario.
    {
        static const bool s_traceState = getenv("JJFFE_TRACE_STATE") != nullptr;
        if (s_traceState)
            std::fprintf(stderr, "STATE-TRACE frame=%d state=%#x 8867=%d target=%d\n",
                         s_liveFrame + 1, static_cast<int>(MissionGameModeState()), MissionAltitudeCounter(),
                         static_cast<int>(static_cast<int8_t>(g_bss8804[0xed])));
    }
    uint8_t* backBuffer = g_videoState->backBuffer;
    if (ReplayPoseDue()) ApplyReplayPreFrame();
    SnapshotPreFrameState();
    jjffe_planet_840_gate_trace_frame_begin();
    jjffe_subdiv_dump_frame_begin();

    // Black first; the sky index is applied via ClearUpperBuf after the 0x1F
    // enqueues (ASM main-loop order: DPalBuild → ClearUpperBuf → FUNC_001789).
    // Viewport only: the console band (y >= 0x9E) holds module 4's FUNC_000272
    // pass from UpdateMission (FUNC_000152's action 3), which the ASM's view
    // never clears. JJFFE_DUP_CONSOLE=1 is the pre-4.36 A/B: clear the whole
    // screen and redraw the shell / chrome inline below.
    const char* dupConsoleEnv = getenv("JJFFE_DUP_CONSOLE");
    const bool dupConsole = dupConsoleEnv && dupConsoleEnv[0] == '1';
    // JUMP_003855 tests bit 0 of the render state before ClearUpperBuf, so a mode-6 frame (state
    // 0xfe) must not clear the viewport: FUNC_000956(0)'s cabin blit has already filled it, and the
    // scene paints on top. Measured: the ASM's cabin asset is 7196 rows x 158 tall.
    // Bit 0 alone, as the ASM tests it. Panel pages (F2/F3) never reach DrawMission: the caller
    // draws the scene only when bit 0 is set or DATA_008630 < 0. R1153 had added "&& mode == 6"
    // here to work around the caller reading the wrong byte for DATA_008630 (fixed in R1155).
    const bool skipUpperClear = (g_bss8804[0xc] & 1) == 0;
    if (!skipUpperClear)
        memset(backBuffer, 0, dupConsole ? SCREEN_SIZE : kConsoleBandY * SCREEN_WIDTH);

    const int32_t altNow = AltitudeAboveHope();
    constexpr int32_t kSurfaceSkyMaxAlt = 64;
    const bool nearSurface = altNow <= kSurfaceSkyMaxAlt;
    // The port-only starfield keeps its old visibility; it is not part of the
    // 3D frame order below.
    const bool drawStars = !nearSurface;
    s_frameDrewStars = drawStars;

    // Enable Z-buffer
    ZBufferEnable(true);
    ZBufferClear(0x7FFF);

    // Planet bodies (models 117–148): mesh via EnqueuePlanetInterpreter1680
    // from the FFE WorldGetAnchorRel − held camera (WorldGetPos only on the
    // JJFFE_PLANET_PARKED A/B path).
    // Capture workaround: JJFFE_NO_PLANET_MESH=1 skips this pass. Env gated for
    // tools/capture_flight_frame.sh only — default runs the real mesh path.
    //
    // ASM flight frame order (JUMP_003855, ffe.asm 68807-68813), run on every
    // 3D frame regardless of altitude: enqueue all bodies → sky record →
    // FUNC_001575 → DPalBuild → ClearUpperBuf → FUNC_001789. ClearUpperBuf
    // fills the viewport (50560 bytes / y<0x9E) with the sky index; terrain
    // jobs then paint over it. A single DPalBuild per frame matters: the
    // median cut reallocates the upper bank, so building again between bodies
    // re-colours pixels already painted by the earlier ones.
    const uint64_t renderReads0 = WorldRenderPosReads();
    const uint64_t anchorReads0 = WorldAnchorRelReads();
    const char* noPlanetMesh = getenv("JJFFE_NO_PLANET_MESH");
    if (noPlanetMesh && noPlanetMesh[0] == '1') {
        DrawMissionDirectPaint(backBuffer, drawStars);
    } else {
        const int n = WorldBodyCount();
        const PhysObj* phys = WorldPhysObjs();
        const int32_t* rot = s_missionCamera.rotation.m;
        // Task 4.5: flight view is the Gateway spawn system, not Sol.
        const int flightBegin = WorldGatewayIndex();
        const int flightStar = WorldGatewayStarIndex();
        const int i0 = (flightBegin >= 0) ? flightBegin : 0;
        // AB_SLOT_ORDER_FIX: bodies sit at ASM slots (star@114..Hope@108); walk
        // high→low like FUNC_001536 so equal-key jobs flush in ASM order.
        const bool slotWalkDesc = (flightStar >= 0 && flightStar != flightBegin);
        // FUNC_001052 reads DATA_008814 before clearing it: the previous
        // frame's FUNC_001877 sky flag (ffe.asm 77628-77635).
        const bool skyLastFrame = DATA_008814 != 0;
        DATA_008814 = 0;

        const PhysObj* lightHope = WorldFind("Hope");
        const int lightHopeIdx = lightHope ? HopeWorldIndex(lightHope) : -1;

        FUNC_001786();

        // FUNC_001678 / FUNC_001680 inputs: the held FFE camera about Hope and
        // the camera object (the planets, the installations and the star).
        int64_t camRel[3] = {};
        PhysObj camObj;
        std::memset(&camObj, 0, sizeof(camObj));
        if (lightHopeIdx >= 0) {
            MissionCameraAnchorRel(camRel);
            MissionCameraObj(lightHopeIdx, camRel, &camObj);
            MissionFrontViewEye(&camObj, camRel);
            // Views 1..6: the scene renders from FUNC_001054's transformed scratch copy (rear
            // basis + rear eye, orbital pans, zoom), not the forward player copy above - with
            // that copy the rear view drew the front scene.
            const int vmode = MissionViewMode();
            MissionViewGrid grid;
            grid.active = false;
            if (vmode >= 1 && vmode <= 6 &&
                MissionViewScratch(camObj.raw, vmode == 6 ? &grid : nullptr))
                std::memcpy(camRel, camObj.raw + 0x3e, sizeof(int64_t) * 3);
            s_mode6Grid = grid;   // drawn after the scene, below
            if (const char* tr = std::getenv("JJFFE_TRACE_CAMOBJ")) {
                if (tr[0] && tr[0] != '0') {
                    static int s_p = 0;
                    const int mode = static_cast<int>(MissionGameModeState());
                    // s_liveFrame is bumped at dump-hook end-of-frame, so the
                    // draw of dump-after N still sees N-1 here.
                    if (s_p < 4 || s_liveFrame == 519 || s_liveFrame == 649 ||
                        s_liveFrame == 697) {
                        const int32_t* cm = reinterpret_cast<const int32_t*>(camObj.raw);
                        const int32_t* sr = g_gameState.playerShip.rot;
                        const uint8_t* pl = static_cast<const uint8_t*>(
                            GetMissionPlayerObject());
                        const int32_t* pm =
                            pl ? reinterpret_cast<const int32_t*>(pl) : nullptr;
                        std::fprintf(stderr,
                                     "CAMPOST frame=%d vmode=%d mode=%#x cam57=%u "
                                     "cam0=%d pool0=%d shipR0=%d shipR4=%d\n",
                                     s_liveFrame + 1, vmode, mode,
                                     (unsigned)camObj.raw[0x57], (int)cm[0],
                                     pm ? (int)pm[0] : 0, (int)sr[0], (int)sr[4]);
                        ++s_p;
                    }
                }
            }
        }

        // Task 4.41: FUNC_001052 (ffe.asm 77626-77632) draws FUNC_001775's
        // model 0x13b on the camera before FUNC_001536, when the "Background
        // stars" option byte DATA_009019 is set and there was no atmosphere sky
        // last frame. Its textured faces mark their DATA_007811 ramps even
        // off screen, so they take part in this frame's median cut.
        // JJFFE_NO_FUNC_001775=1 skips it (pre-4.41 A/B).
        const char* no1775 = getenv("JJFFE_NO_FUNC_001775");
        if (lightHopeIdx >= 0 && !(no1775 && no1775[0] == '1') &&
            SysMenuFlag(kSysMenuFlagBackgroundStars) != 0 && !skyLastFrame)
            MissionFUNC_001775(camObj.raw);

        // FUNC_001536 → FUNC_001534/1535: light vector + star shade ramp for
        // this frame, before any body reaches FUNC_001683 (and after
        // FUNC_001775, which draws with the previous frame's light).
        // The camera's FFE position is taken as Hope's global Vec64: the
        // landed/aloft offset (<1e10 of |Hope| ~9.3e14) is below one LSB of
        // the Q15 direction. JJFFE_NO_STAR_LIGHT=1 skips it (pre-4.11 zero
        // light, for tools/capture_port_landed.sh terrain before/after).
        const char* noStarLight = getenv("JJFFE_NO_STAR_LIGHT");
        int64_t starG[3] = {}, camG[3] = {};
        if (!(noStarLight && noStarLight[0] == '1') && flightStar >= 0 && lightHopeIdx >= 0 &&
            WorldGetFfeGlobal(flightStar, starG) && WorldGetFfeGlobal(lightHopeIdx, camG)) {
            const int64_t starToCam[3] = {camG[0] - starG[0], camG[1] - starG[1],
                                          camG[2] - starG[2]};
            MissionSetStarLight(starToCam, rot, PhysObjGetFlags(&phys[flightStar]));
        }
        const bool planets1680 = lightHopeIdx >= 0 && !PlanetsParked();

        // FUNC_001536 draws every pool object in ONE descending slot walk, and the flight
        // system's bodies sit at slots 114 down in body order (star 114, ..., Hope 108, the
        // starports below it), so the ASM enqueues the bodies in ascending body order with
        // planets, the star and installations interleaved (measured at JUMP_005729). The
        // planet body below therefore runs from the object loop further down at its own
        // index; equal-key jobs (the star's glow ring and Hope's horizon band are both
        // 0xffffffff) flush in allocation order, so a separate planets-first pass painted the
        // star's ring over the band the ASM paints over it.
        int paintIdx = 0;
        auto planetBody = [&](int i) {
            const int model = PhysObjGetModel(&phys[i]);
            if (!IsPlanetBodyModel(model)) return;
            // Stars (Gateway 139, Sol 141, …): emissive — not rocky 0x1F.
            // Drawing them here ghosted a second disc on Hope.
            if (model >= 138) return;
            if (paintIdx >= kWorldBodyCapacity) return;

            // Task 4.24: the ASM's FUNC_001680 for the 0x1F body. The FFE
            // body − camera Vec64 picks shift = msb − 0x1c, and the scale /
            // zoom pair follows (Hope near the planet: FFE >> 4, scale 22,
            // zoom 4), so local10, the vertex depths, DATA_009269,
            // FUNC_001877's adjusted9 and the FUNC_001777 depth key all move
            // together.
            if (planets1680) {
                int64_t rel[3];
                if (!WorldGetAnchorRel(i, rel)) return;
                const int64_t vec[3] = {rel[0] - camRel[0], rel[1] - camRel[1],
                                        rel[2] - camRel[2]};
                if (EnqueuePlanetInterpreter1680(s_missionPlanetPaint[paintIdx], model,
                                                 camObj.raw, vec, PhysObjGetSeed(&phys[i]),
                                                 PhysObjGetI32(&phys[i], 0x7e),
                                                 &phys[i]))
                    ++paintIdx;
                return;
            }

            // A/B parked path (PlanetsParked): render-unit camD.
            int32_t planetX = 0, planetY = 0, planetZ = 0;
            WorldGetPos(i, &planetX, &planetY, &planetZ);

            const int32_t dx = planetX - s_missionCamera.position.x;
            const int32_t dy = planetY - s_missionCamera.position.y;
            const int32_t dz = planetZ - s_missionCamera.position.z;

            // camD = rot^T * worldD (same layout as ProjectPoint, 16.16 >> 16)
            const int32_t camDx = static_cast<int32_t>(
                ((int64_t)rot[0] * dx + (int64_t)rot[3] * dy + (int64_t)rot[6] * dz) >> 16);
            const int32_t camDy = static_cast<int32_t>(
                ((int64_t)rot[1] * dx + (int64_t)rot[4] * dy + (int64_t)rot[7] * dz) >> 16);
            const int32_t camDz = static_cast<int32_t>(
                ((int64_t)rot[2] * dx + (int64_t)rot[5] * dy + (int64_t)rot[8] * dz) >> 16);

            // Near-surface / horizon: planet centre often has camDz<=0 while the
            // sphere still fills the view. Only skip when the whole ball is behind.
            const ShipModel* sm = GetShipModel(model);
            const int32_t meshR = (sm && sm->radius != 0)
                ? static_cast<int32_t>(static_cast<uint32_t>(sm->radius) << 14)
                : FP_ONE;
            if (camDz < -meshR) return;

            // Cull only when deeply inside the sphere (near the centre). The
            // landed pose sits on the surface (|camD| ≈ meshR, often camDz≈0)
            // — that used to hit distSq < rSq and skip Hope entirely.
            const int64_t distSq = (int64_t)camDx * camDx + (int64_t)camDy * camDy +
                                   (int64_t)camDz * camDz;
            const int64_t deepR = static_cast<int64_t>(meshR >> 1);
            if (distSq < deepR * deepR) return;

            // Position is camera-space; body is Q31 camera R (SpecialVecMatMul
            // applies R^T) so Hope keeps world axes — pitch banks about right,
            // not a vertical-axis spin.
            uint8_t* paint = s_missionPlanetPaint[paintIdx];
            EnqueuePlanetInterpreterAt(paint, model, camDx, camDy, camDz,
                                       PhysObjGetSeed(&phys[i]), rot);
            ++paintIdx;
        };
        // JJFFE_PRE1136_PASSES=1 restores the planets-first pass (the frames CameraFfeIdentity
        // pins under its pre-* knobs were captured with it).
        const char* noInstEnv = getenv("JJFFE_NO_INSTALLATIONS");
        const char* pre1136 = getenv("JJFFE_PRE1136_PASSES");
        const bool planetsMerged = lightHopeIdx >= 0 && !InstallationsShipFlat() &&
                                   !(noInstEnv && noInstEnv[0] == '1') &&
                                   !(pre1136 && pre1136[0] == '1');
        auto forFlightBodies = [&](auto&& fn) {
            if (slotWalkDesc) {
                for (int i = n - 1; i >= i0; --i) fn(i);
            } else {
                for (int i = i0; i < n; ++i) fn(i);
            }
        };
        if (!planetsMerged) forFlightBodies(planetBody);

        // Task 4.20: Gateway installations (type >= 0x20: surface starports /
        // cities and orbital stations) through FUNC_001680, the ASM's
        // per-object view setup, in the same job frame as the planets. Their
        // face jobs (FUNC_001712 / FUNC_001708 via FUNC_001773) mark each face
        // colour with DPal32to16 here, before this frame's DPalBuild, and
        // FUNC_001789 depth-sorts them with the terrain. FUNC_001678 = body
        // minus camera, both relative to Hope in FFE units.
        //
        // Task 4.21: the flight system's star goes through the same pass. The
        // ASM's object loop (FUNC_001536, JUMP_005729, ffe.asm 91547-91551)
        // calls FUNC_001680 for every object but the camera, the star
        // included; its model's 0x1F op reaches FUNC_001876 → FUNC_001877,
        // whose glow gradient runs from the star's colour to the sky record,
        // and render mode 9 (FUNC_001845) fills the disc. Those marks shape
        // this frame's median cut, i.e. the sky and ground slots.
        // JJFFE_NO_STAR1680=1 skips it (pre-4.21 A/B).
        //
        // Task 4.26: every other non-planet body takes this pass as well,
        // whatever its SysObj type (the ASM loop does not filter on it). In
        // the generated Gateway table every non-planet model is an
        // installation (type >= 0x20), so this draws the same set; bodies
        // like Sol's type-0x01 moons with the asteroid model 112 would be
        // meshes here, not the retired WorldGetPos dots.
        if (lightHopeIdx >= 0 && !InstallationsShipFlat()) {
            const char* noInst = getenv("JJFFE_NO_INSTALLATIONS");
            const bool skipInst = noInst && noInst[0] == '1';
            static int s_instTrace = 0;
            const bool trace = getenv("JJFFE_INSTALLATION_TRACE") && s_instTrace < 2;
            const char* noStar = getenv("JJFFE_NO_STAR1680");
            const bool star1680 = !(noStar && noStar[0] == '1');
            auto installBody = [&](int i) {
                if (skipInst) return;
                const int model = PhysObjGetModel(&phys[i]);
                const bool isStar = star1680 && IsPlanetBodyModel(model) && model >= 138;
                if (!isStar && IsPlanetBodyModel(model)) {
                    if (planetsMerged && model < 138) planetBody(i);  // planets at their slot's turn
                    return;
                }
                int64_t rel[3];
                if (!WorldGetAnchorRel(i, rel)) return;
                const int64_t vec[3] = {rel[0] - camRel[0], rel[1] - camRel[1],
                                        rel[2] - camRel[2]};
                PhysObj body = phys[i];
                // R1140: the drawing matrix in Hope's rotating frame.
                MissionDrawMatrixForCam(&phys[i], camObj.raw,
                                       reinterpret_cast<int32_t*>(body.raw));
                MissionStampLightPhase(body.raw);
                static uint32_t s_colourHist[4096];
                if (trace) {
                    std::memset(s_colourHist, 0, sizeof(s_colourHist));
                    SetDpalColourTap(s_colourHist);
                }
                MissionRenderObject1680(body.raw, camObj.raw, vec);
                SetDpalColourTap(nullptr);
                if (trace) {
                    fprintf(stderr, "INSTALL[%d] '%s' model=%d vec=(%lld,%lld,%lld) lod=%d far=%#x colours:",
                            i, PhysObjGetName(&phys[i]), model, static_cast<long long>(vec[0]),
                            static_cast<long long>(vec[1]), static_cast<long long>(vec[2]),
                            static_cast<int8_t>(body.raw[0x88]), body.raw[0x8a]);
                    for (int c = 0; c < 4096; ++c)
                        if (s_colourHist[c]) fprintf(stderr, " %03x*%u", c, s_colourHist[c]);
                    fprintf(stderr, "\n");
                }
            };
            if (!skipInst) forFlightBodies(installBody);
            // The player's own hull in every chase view (slot 87: after the bodies, before the
            // traffic ships 86..70 in FUNC_001536's descending walk). FUNC_001536's object
            // pass draws the player like any other pool object; DATA_008838 =
            // the player's slot is what suppresses it in first-person, where
            // the camera sits at the ship's own eye. In modes 1..6 the camera
            // is behind/beside the hull, so it must be painted or the view
            // change shows an empty starfield.
            if (!s_viewCameraSkipsPlayer) {
                const uint8_t* playerPool =
                    static_cast<const uint8_t*>(GetMissionPlayerObject());
                if (playerPool) {
                    int64_t rel[3];
                    std::memcpy(rel, playerPool + 0x3e, sizeof(rel));
                    const int64_t vec[3] = {rel[0] - camRel[0], rel[1] - camRel[1],
                                            rel[2] - camRel[2]};
                    PhysObj body;
                    std::memcpy(body.raw, playerPool, sizeof(body.raw));
                    MissionStampLightPhase(body.raw);
                    MissionRenderObject1680(body.raw, camObj.raw, vec);
                    if (trace)
                        fprintf(stderr, "PLAYERHULL model=%d vec=(%lld,%lld,%lld)\n",
                                PhysObjGetModel(&body), static_cast<long long>(vec[0]),
                                static_cast<long long>(vec[1]),
                                static_cast<long long>(vec[2]));
                }
            }

            // Task 4.27: the traffic ships (slots 86 down to 70 — FUNC_001536
            // walks the table downward, bodies 114..88 first). JJFFE_NO_TRAFFIC=1
            // skips them (pre-4.27 A/B).
            const char* noTraffic = getenv("JJFFE_NO_TRAFFIC");
            const PhysObj* ships = WorldShipObjs();
            for (int s = (noTraffic && noTraffic[0] == '1') ? -1 : WorldShipCount() - 1; s >= 0;
                 --s) {
                int64_t rel[3];
                if (!WorldGetShipAnchorRel(s, rel)) continue;
                const int64_t vec[3] = {rel[0] - camRel[0], rel[1] - camRel[1],
                                        rel[2] - camRel[2]};
                PhysObj body = ships[s];
                MissionDrawMatrixForCam(&ships[s], camObj.raw,
                                       reinterpret_cast<int32_t*>(body.raw));
                MissionStampLightPhase(body.raw);
                MissionRenderObject1680(body.raw, camObj.raw, vec);
                if (trace)
                    fprintf(stderr, "TRAFFIC[%d] '%s' model=%d vec=(%lld,%lld,%lld) lod=%d far=%#x\n",
                            PhysObjGetIndex(&ships[s]), PhysObjGetName(&ships[s]),
                            PhysObjGetModel(&ships[s]), static_cast<long long>(vec[0]),
                            static_cast<long long>(vec[1]), static_cast<long long>(vec[2]),
                            static_cast<int8_t>(body.raw[0x88]), body.raw[0x8a]);
            }
            if (trace) ++s_instTrace;
        }

        {
            // ASM view setup tail (FUNC_001052, ffe.asm 77641-77654): when no
            // body's 877 "58" path set DATA_008814, the sky record is the JJ
            // space colour {0, 0, [DATA_008804+0x3f88]} (default 4, ".nofudge"
            // ffe.asm 16583). JUMP_004336 then ALWAYS copies DATA_008815/817
            // into DATA_009136/009137 (panel FUNC_001002/1003 restore from
            // that latch). Missing latch + per-TU 9136/7 left F1-restore
            // shadeB stuck at ambient 9 while ASM held the 58-path's 10 (R1219).
            if (DATA_008814 == 0) {
                constexpr uint8_t kSpaceColourB = 4;
                DATA_008815 = 0;
                DATA_008816 = 0;
                DATA_008817 = kSpaceColourB;
            }
            // R1152: case 6 draws its scanner grid LAST, after the scene enqueues (JUMP_004338's
            // tail: FUNC_000802 fills the outputs, then FUNC_001680 on a player copy carrying model
            // 0xa7 and +0x9e from DATA_008712 < 0x7a1200). The object is already built with the
            // camera; render it through the same helper the bodies use.
            if (s_mode6Grid.active) {
                int64_t rel[3];
                std::memcpy(rel, s_mode6Grid.obj + 0x3e, sizeof(rel));
                const int64_t vec[3] = {rel[0] - camRel[0], rel[1] - camRel[1], rel[2] - camRel[2]};
                MissionRenderObject1680(s_mode6Grid.obj, camObj.raw, vec);
            }
            // JUMP_004336 (ffe.asm 77648-77651): mov ax,[815]; mov [9136],ax /
            // mov al,[817]; mov [9137],al — after the final sky record is known.
            DATA_009136 = static_cast<uint32_t>(DATA_008815) |
                          (static_cast<uint32_t>(DATA_008816) << 8);
            DATA_009137 = static_cast<uint32_t>(DATA_008817 & 0xffu);
            FUNC_001575(reinterpret_cast<uint16_t*>(&DATA_008815));
            DumpMissionDpalTables("marks");
            if (g_videoState)
                DPalBuild(g_videoState->palette, 0);
            ArmMissionDpalLookupTap();

            // Diagnostic A/B: JJFFE_NO_SKY_CLEAR=1 skips the real-sky fill so
            // terrain coverage / seams can be measured against the raw index-0
            // background (without it a missing patch still reads as sky, which
            // would mask a paint dropout).
            const char* noSkyClear = getenv("JJFFE_NO_SKY_CLEAR");
            const bool skipSkyClear = noSkyClear && noSkyClear[0] == '1';
            if (!skipSkyClear)
                ClearUpperBuf();
            DrawMissionDirectPaint(backBuffer, drawStars);
            FUNC_001789();
            if (getenv("JJFFE_LIGHT_TRACE")) {
                static int s_hazeTrace = 0;
                if (s_hazeTrace++ < 4)
                    fprintf(stderr,
                            "HAZE 9262=%#x 9263=%d 9265=%#x 9267=%d 8812=%d 8814=%#x "
                            "8815..817=%u,%u,%u\n",
                            DATA_009262, static_cast<int32_t>(DATA_009263),
                            DATA_009265 & 0xffffffu, static_cast<int32_t>(DATA_009267),
                            static_cast<int32_t>(DATA_008812), DATA_008814,
                            DATA_008815, DATA_008816, static_cast<unsigned>(DATA_008817));
            }
        }
    }

    // Pre-4.20 installation pass (task 4.18): the shared DrawObject3D ship
    // path after DPalBuild, so face colours were never marked and resolved to
    // whatever the terrain palette held. Kept for A/B
    // (JJFFE_INSTALLATIONS_SHIPFLAT=1) and for the JJFFE_NO_PLANET_MESH
    // capture mode, which has no job frame. Vertices come out in FFE units,
    // drawn camera-relative; bodies whose offset leaves int32 are skipped.
    if (InstallationsShipFlat() || (noPlanetMesh && noPlanetMesh[0] == '1')) {
        const int n = WorldBodyCount();
        const SysObj* sys = WorldSysObjs();
        const PhysObj* phys = WorldPhysObjs();
        const int flightBegin = WorldGatewayIndex();
        const PhysObj* hope = WorldFind("Hope");
        const int hopeIdx = hope ? HopeWorldIndex(hope) : -1;
        const char* noInst = getenv("JJFFE_NO_INSTALLATIONS");
        const bool skip = (noInst && noInst[0] == '1') || flightBegin < 0 || hopeIdx < 0;
        int64_t camRel[3];
        MissionCameraAnchorRel(camRel);
        Camera3D ffeCam = s_missionCamera;
        CameraSetPosition(&ffeCam, 0, 0, 0);
        static int s_instTrace = 0;
        const bool trace = getenv("JJFFE_INSTALLATION_TRACE") && s_instTrace < 2;
        // Vertex offsets (<= radius << scaleRelated, ~2^22 for cities) must not
        // wrap the int32 world add in TransformVertex.
        constexpr int64_t kInstallMaxOffset = int64_t{0x7c000000};
        const int32_t* crot = ffeCam.rotation.m;
        int stationIdx = 0;
        for (int i = skip ? n : flightBegin; i < n; ++i) {
            const int model = PhysObjGetModel(&phys[i]);
            if (IsPlanetBodyModel(model)) continue;
            if (sys[i].type < 0x20) continue;
            if (stationIdx >= kWorldBodyCapacity) break;
            int64_t rel[3];
            if (!WorldGetAnchorRel(i, rel)) continue;
            int64_t d[3];
            bool fits = true;
            for (int a = 0; a < 3; ++a) {
                d[a] = rel[a] - camRel[a];
                if (d[a] > kInstallMaxOffset || d[a] < -kInstallMaxOffset) fits = false;
            }
            if (trace)
                fprintf(stderr, "INSTALL[%d] '%s' model=%d d=(%lld,%lld,%lld)%s\n", i,
                        PhysObjGetName(&phys[i]), model, static_cast<long long>(d[0]),
                        static_cast<long long>(d[1]), static_cast<long long>(d[2]),
                        fits ? "" : " (out of range)");
            if (!fits) continue;

            GameObj* obj = &s_missionStationObj[stationIdx++];
            std::memset(obj, 0, sizeof(GameObj));
            obj->shipId = static_cast<uint8_t>(model);
            for (int a = 0; a < 3; ++a) obj->pos[a] = static_cast<int32_t>(d[a]);
            std::memcpy(obj->rot, phys[i].raw, sizeof(obj->rot));
            const int64_t viewZ =
                ((int64_t)crot[2] * d[0] + (int64_t)crot[5] * d[1] + (int64_t)crot[8] * d[2]) >> 16;
            DrawObject3D(&ffeCam, obj, false, i,
                         static_cast<int32_t>(std::max<int64_t>(std::min<int64_t>(viewZ, INT32_MAX),
                                                                -INT32_MAX)));
        }
        if (trace) ++s_instTrace;
    }

    // Task 4.26: the port-only WorldGetPos dots for non-planet type < 0x20
    // bodies are retired; those bodies now take the FUNC_001680 object pass.
    // Only the A/B paths (JJFFE_PLANET_PARKED) still read render space here.
    s_drawRenderReads = WorldRenderPosReads() - renderReads0;
    s_drawAnchorReads = WorldAnchorRelReads() - anchorReads0;

    // Draw target indicator box around current target
    if (g_gameState.playerShip.targetId != 0xFF) {
        for (int i = 0; i < g_gameState.objectCount; i++) {
            GameObj* obj = GetGameObject(i);
            if (!obj || obj->hull <= 0) continue;
            if (obj->shipId != g_gameState.playerShip.targetId) continue;

            Point3D objPos;
            objPos.x = obj->pos[0];
            objPos.y = obj->pos[1];
            objPos.z = obj->pos[2];

            ProjResult proj;
            if (ProjectPoint(&s_missionCamera, &objPos, &proj) && proj.visible) {
                int16_t sx = proj.sx, sy = proj.sy;
                uint8_t tc = 0x1F;
                // Draw corner brackets
                for (int d = 2; d <= 4; d++) {
                    DrawPixel(static_cast<int16_t>(sx - d), static_cast<int16_t>(sy - d), tc);
                    DrawPixel(static_cast<int16_t>(sx + d), static_cast<int16_t>(sy - d), tc);
                    DrawPixel(static_cast<int16_t>(sx - d), static_cast<int16_t>(sy + d), tc);
                    DrawPixel(static_cast<int16_t>(sx + d), static_cast<int16_t>(sy + d), tc);
                }
            }
            break;
        }
    }

    // Draw projectile trails
    for (int i = 0; i < GetMaxProjectiles(); i++) {
        const Projectile* proj = GetProjectile(i);
        if (!proj) continue;

        Point3D projPos;
        projPos.x = proj->pos[0];
        projPos.y = proj->pos[1];
        projPos.z = proj->pos[2];

        ProjResult projResult;
        if (ProjectPoint(&s_missionCamera, &projPos, &projResult) && projResult.visible) {
            // Draw a 4-pixel trail in the opposite direction of velocity
            int16_t trailX = projResult.sx;
            int16_t trailY = projResult.sy;

            // Simple approach: draw a 4-pixel X pattern at the projectile position
            DrawPixel(trailX, trailY, 0x1F);
            if (trailX > 1 && trailY > 1) {
                DrawPixel(static_cast<int16_t>(trailX - 1), trailY, 0x1E);
                DrawPixel(static_cast<int16_t>(trailX + 1), trailY, 0x1E);
                DrawPixel(trailX, static_cast<int16_t>(trailY - 1), 0x1E);
                DrawPixel(trailX, static_cast<int16_t>(trailY + 1), 0x1E);
            }
        }
    }

    // Draw explosion effects (expanding circles)
    for (int i = 0; i < GetMaxExplosions(); i++) {
        const Explosion* expl = GetExplosion(i);
        if (!expl) continue;

        Point3D explPos;
        explPos.x = expl->pos[0];
        explPos.y = expl->pos[1];
        explPos.z = expl->pos[2];

        ProjResult explResult;
        if (ProjectPoint(&s_missionCamera, &explPos, &explResult) && explResult.visible) {
            int radiusPx = (expl->radius >> 16) * 3;
            if (radiusPx < 1) radiusPx = 1;
            if (radiusPx > 20) radiusPx = 20;

            // Fade color from white to red as it ages
            int alpha = expl->alpha;
            uint8_t color = (alpha > 128) ? 0x1F : ((alpha > 64) ? 0x1C : 0x08);

            // Draw expanding circle using midpoint algorithm
            int cx = explResult.sx, cy = explResult.sy;
            for (int r = 1; r <= radiusPx; r++) {
                int x = r, y = 0;
                int err = 1 - r;
                while (x >= y) {
                    DrawPixel(cx + x, cy + y, color);
                    DrawPixel(cx + y, cy + x, color);
                    DrawPixel(cx - y, cy + x, color);
                    DrawPixel(cx - x, cy + y, color);
                    DrawPixel(cx - x, cy - y, color);
                    DrawPixel(cx - y, cy - x, color);
                    DrawPixel(cx + y, cy - x, color);
                    DrawPixel(cx + x, cy - y, color);
                    y++;
                    if (err <= 0) {
                        err += 2 * y + 1;
                    } else {
                        x--;
                        err += 2 * (y - x) + 1;
                    }
                }
            }
        }
    }

    // Damage flash effect — red overlay when player takes hits (viewport only)
    if (GetPlayerDamageThisFrame() > 0) {
        int flashAlpha = GetPlayerDamageThisFrame() * 8;
        if (flashAlpha > 16) flashAlpha = 16;
        for (int y = 0; y < 0x9E; y += 4) {
            for (int x = 0; x < 320; x += 4) {
                uint8_t existing = backBuffer[y * 320 + x];
                if (existing >= 4 && existing < 0x20) {  // Only affect non-black pixels
                    uint8_t newColor = existing + flashAlpha;
                    if (newColor > 0x1F) newColor = 0x1C;  // Clamp to red range
                    backBuffer[y * 320 + x] = newColor;
                }
            }
        }
    }

    // ASM HUD: scanner overlay bitmap + aiming cross / circle (FUNC_001111).
    // Drawn after 3D world, before FlipScreen (called from GameLoopUpdate).
    // ffe.asm:80766-80780: not in view modes 4/6, and only when the player has a gun on the
    // mounting this view looks along (player +0xd2 + mode: front, rear, ...) - the rear view
    // of a ship with an empty rear mount shows no sight. Mode 5 (the missile view) never gets
    // here in the ASM: FUNC_001099 returns right after its first FUNC_001100 (cmp [ebx+0xea],5 /
    // jz JUMP_004541, the epilogue) - R1158 measured the port drawing the sight there.
    {
        const int vm = MissionViewMode();
        const uint8_t* pl = static_cast<const uint8_t*>(GetMissionPlayerObject());
        // JJFFE_SYNTH_CONSOLE_PLAYER=1 (the pre-4.39 synthesised player, no mount bytes)
        // keeps the old unconditional sight so the frames pinned under it still hold.
        static const bool s_synthPlayer = [] {
            const char* e = getenv("JJFFE_SYNTH_CONSOLE_PLAYER");
            return e && e[0] == '1';
        }();
        if (s_synthPlayer || (vm != 4 && vm != 5 && vm != 6 && pl && pl[0xd2 + vm] != 0))
            FUNC_001111();
    }

    SyncConsolePlayerFromShip();
    if (dupConsole) {
        // Pre-4.36: FUNC_000272's shell (bmp 0x2b at y 0x9E) and chrome again,
        // over its log text.
        BlitIndexToBuf(0x2b, 0, 0x9e);
        BlitIndexToBuf(static_cast<int>(GetConsoleModeIconBmp()), 0x65, 0xaf);
        ConsoleDrawButtons();
        if (const uint8_t* player = static_cast<const uint8_t*>(GetMissionPlayerObject())) {
            FUNC_000270();
            if ((player[0xca] & 4) != 0) {
                FUNC_000274();
            }
        }
    }

    // Phase 3: combat HUD text (Set/Act, location, date-driven globals).
    // DATA_008861 is FUNC_000996's pool ship; FUNC_001099 null-guards it.
    // Keep FUNC_001111 above — FUNC_001099 may nest another call when weapon mode is set.
    // Capture workaround: JJFFE_NO_HUD=1 skips FUNC_001099 (StringExpand NPE on
    // --mission). Env-gated only — not a permanent HUD removal.
    // Pre-4.37 A/B: 0x1000 per frame instead of MissionFrameClockTick's
    // DATA_009155 add.
    if (MissionClockFrameStepped()) AdvanceMissionClock(0x1000u);
    // R1186: run the ASM's action-4 pass (FUNC_000154) here, replacing the old direct FUNC_001099() call. This
    // dispatches every open module's action 4 in module order (2 before 3, as ffeasm does) and gates each on its
    // DATA_008625 flag. Crucially, when a panel like F3 (module 2) is open, module 3's flag is 0 (verified against
    // ffeasm: the m2/m3 action-4 flags are mutually exclusive), so the HUD handler FUNC_001099 does NOT run - the
    // galactic map shows module 2's sector text WITHOUT the cockpit HUD overprinted on it, matching ffeasm. The
    // previous except-module-3 wiring kept a direct FUNC_001099 call, which drew that HUD onto the map frame (wrong).
    // JJFFE_NO_HUD (capture LEGACY mode) suppresses just FUNC_001099 inside that handler now.
    ProcessActiveObjects();
    MissionFpsOverlay();

    MissionFrameDumpHooks();
    s_firstDrawDone = true;
}

// Task 4.217 (re-lands 4.214b/c): the capture hooks used to ride only at the end of DrawMission.
// The render-state branch (ffe.asm:68798-68814) skips DrawMission on map/panel-open frames, which
// darkened every capture tool (18 ctest failures, measured in 4.214). Exposed so the game loop's
// clear branch can fire them at the same end-of-frame point. The guard: the one-shot mission-frame
// dump must not fire on an early clear-branch frame before any real 3D frame was presented (that
// captured a black pre-scene buffer in 4.214 - BootFlow ground 97% black, measured).
static void MaybeTraceFlipFirst(const char* path);   // defined below (R1170)
static bool s_pageWasOpen = false;                   // R1172: page_closed only after a page_open

void MissionFrameDumpHooks()
{
    // One-shot back-buffer dump for flight A/B (JJFFE_DUMP_MISSION_FRAME=path).
    MaybeDumpMissionFrame();
    // Interactive F12 dump (JJFFE_DUMP_LIVE=path) — keeps playing.
    MaybeDumpMissionFrameLive();
    MaybeTraceFlipFirst(getenv("JJFFE_TRACE_FLIP_FIRST"));
}

void MissionFrameDumpHooksIfDrawing()
{
    if (s_firstDrawDone) MissionFrameDumpHooks();
}

// R1170: the FIRST flip at which each input's effect is on screen, keyed on the game's own stamp.
// The flight trace proves what happens once a missile exists but not WHEN an input took effect, and
// with a static scene no value check can see a one-frame ordering error (R1169 found exactly that).
// Names match ffeasm's probe (tools/missile_view_timing_asm.py) so the two files compare directly.
// Sampled at the dump hooks, which run every drawn mission frame, so this is the same point both
// sides call it: after the draw, before the next frame's input.
static int s_flipFirstCount = 0;
static void MaybeTraceFlipFirst(const char* path)
{
    static FILE* f = nullptr;
    if (!f && path && path[0]) { f = std::fopen(path, "w"); if (!f) return; }
    if (!f) return;
    ++s_flipFirstCount;
    const int e = g_bss8804[0xe];                                  // DATA_008811 frame stamp
    const int tgt = static_cast<int8_t>(g_bss8804[0xed]);          // DATA_008875 combat target slot
    const int mode = static_cast<int8_t>(g_bss8804[0xea]);         // DATA_008872 view mode
    const void* rec = GetMissionPlayerObject();      // DATA_008861, the player record
    const int bit = rec ? (static_cast<const uint8_t*>(rec)[0xc9] >> 1) & 1 : 0;   // equipment bit
    struct Row { const char* name; bool hit; };
    // NOT static: a static array of this shape initialises once, and every later frame would compare
    // against frame 1's values forever. (That is exactly what it did here before R1170 caught it.)
    // R1172: DATA_008630 (+0x1e of the e3 blob) is the shipyard page's own state byte, 0x7f while a page
    // is up and -1 otherwise; it exists identically on both sides, which is what makes these rows
    // comparable. The render-state byte DATA_008809 used before is NOT comparable (ffeasm boots 0x00,
    // the port 0xff), and DATA_008965 is only ever written 0 in ffeasm.
    const int page = static_cast<int>(DATA_008630);
    if (page == 0x7f) s_pageWasOpen = true;          // a close is only meaningful after an open
    const Row rows[] = {
        {"shipyard_equipped", bit == 1}, {"page_open", page == 0x7f},
        {"page_closed", s_pageWasOpen && page == -1},
        {"target_114", tgt == 114}, {"mode_5", mode == 5}};
    // Bound by the array, not a literal: adding a row must not silently drop the last one off the end
    // of a hand-written count (it did, for mode_5, until R1171's gate caught the missing row).
    static bool seen[sizeof rows / sizeof *rows] = {};
    for (size_t i = 0; i < sizeof rows / sizeof *rows; ++i) {
        if (rows[i].hit && !seen[i]) {
            seen[i] = true;
            std::fprintf(f, "FLIP-FIRST %s flip=%d e=%d\n", rows[i].name, s_flipFirstCount, e);
            std::fflush(f);
        }
    }
}

bool MissionFrameDumpedExit() { return s_missionFrameDumped; }

int HandleMissionInput()
{
    uint8_t keyStates[256];
    InputKeybReadStates(keyStates);

    // Every flight key now goes through the ASM's own dispatcher FUNC_001056,
    // reached via the DATA_007688 scancode→action table: fire (Space ->
    // FUNC_001040 -> FUNC_000931), ECM, missile, energy bomb, escape capsule,
    // mining, radar mapper, map centre, FPS, target ('t'), undercarriage ('u'),
    // labels ('l'), navcomp ('n'), Tab (0x09) and the view/time keys 0xf5-0xff.
    // The Phase-60 Space debounce and Tab target cycle that used to live here are
    // gone: FUNC_001040 does its own recharge gating and 't' is the ASM's target
    // key (the manual's Tab is the engine).
    PlayerActionDispatch(keyStates);

    return ShipControlsHandleInput();  // Returns -1 for ESC, 0 for stay
}

void ShutdownMission()
{
    // R1178/R1179: with JJFFE_TRACE_FIRE=1, report the three fire-path counters at shutdown. entries is
    // every FUNC_001040 call (== mission frames in ffeasm, one per frame from FUNC_001056 regardless of
    // any button); dispatch is the subset that passed the fire gate into FUNC_000931 (rises only when a
    // key/button is held); shots is bolts actually spawned.
    {
        extern uint32_t g_fire1040Entries;
        extern uint32_t g_fireDispatchCount;
        extern uint32_t g_shotSpawnCount;
        extern uint32_t g_view1056Entries;
        if (getenv("JJFFE_TRACE_FIRE"))
            std::fprintf(stderr, "FIRE-TRACE entries=%u dispatch=%u shots=%u view1056=%u\n",
                         g_fire1040Entries, g_fireDispatchCount, g_shotSpawnCount, g_view1056Entries);
    }
    // T-59-10: Null out objects after free to prevent use-after-free
    if (g_gameState.objects) {
        std::free(g_gameState.objects);
        g_gameState.objects = nullptr;
    }
    g_gameState.objectCount = 0;

    WorldShutdown();

    // Reset state
    g_gameState.isDocked = true;
    g_gameState.isActive = false;

    // Zero player ship
    std::memset(&g_gameState.playerShip, 0, sizeof(GameObj));
}

} // namespace jjffe
