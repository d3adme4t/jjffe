#ifndef JJFFE_FRAME_POSE_H
#define JJFFE_FRAME_POSE_H

// Task 4.13: `<frame>.raw.pose` sidecar written next to every mission frame
// dump, and read back by JJFFE_REPLAY_POSE. Plain text, one key=value per
// line; see docs/FLIGHT_SCENE_GAP.md §4.13 for the key list.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace jjffe {

constexpr int kFramePoseVersion = 1;
constexpr int kFramePoseStarCount = 100;
constexpr int kFramePoseDpalRgbBytes = 128 * 3;

struct FramePoseObj {
    int32_t shipId = 0;
    int32_t flags = 0;
    int32_t hull = 0;
    int32_t pos[3] = {};
    int32_t vel[3] = {};
    int32_t rot[9] = {};
};

struct FramePoseStar {
    int32_t x = 0, y = 0, brightness = 0, speed = 0;
};

struct FramePose {
    int version = kFramePoseVersion;
    std::string source;          // live-f12 | live-auto | mission-frame
    int32_t frame = 0;           // mission frames since InitMission (s_missionTime)
    int32_t liveFrame = 0;       // JJFFE_DUMP_LIVE counter (s_liveFrame)
    bool isDocked = false;
    bool orbitalView = false;
    int32_t lookPitchFp16 = 0;

    int32_t shipPos[3] = {};
    int32_t shipVel[3] = {};
    int32_t shipRot[9] = {};
    int32_t shipFuel = 0, shipShields = 0, shipHull = 0;
    int32_t shipShieldMax = 0, shipHullMax = 0;
    int32_t shipTarget = 0xFF;

    int32_t camPos[3] = {};
    int32_t camRot[9] = {};

    std::vector<FramePoseObj> npcs;

    // Pre-frame state: what the frame started from, not what it left behind.
    std::vector<FramePoseStar> stars;           // port-only starfield
    int32_t dpalBase = 0;                        // dPalBaseIndex (flips per flip)
    uint8_t dpalRgb[kFramePoseDpalRgbBytes] = {}; // dPalRGB (persists across builds)
    // DATA_008815..008817 sky record: FUNC_001877's no-shade exit reads the
    // previous frame's (ffe.asm 133056). Optional key sky.pre (task 4.20);
    // sidecars written before it load with hasSkyPre = false.
    bool hasSkyPre = false;
    int32_t skyPre[3] = {};
    // DATA_008814 != 0 (the sky was drawn last frame): FUNC_001052 skips the
    // FUNC_001775 background-stars model when set (ffe.asm 77626-77632), and
    // that model's marks feed the DPal median cut. Optional key sky.last
    // (task 4.41); sidecars written before it load with hasSkyLast = false.
    bool hasSkyLast = false;
    int32_t skyLast = 0;

    // Render-affecting JJFFE_* / AB_* knobs in effect (name, value).
    std::vector<std::pair<std::string, std::string>> env;
    // check.* lines: recomputed every frame, written for verification only and
    // ignored on read.
    std::vector<std::pair<std::string, std::string>> checks;
};

bool FramePoseWrite(const char* path, const FramePose& pose);
// Strict: a missing key, an unknown key, a wrong version or a malformed value
// fails with a message in *err.
bool FramePoseRead(const char* path, FramePose* pose, std::string* err);

// True for env names a replay must reproduce (JJFFE_* / AB_* minus the dump,
// replay, orbital-placement, debug and *_TRACE knobs).
bool FramePoseEnvRelevant(const char* name);
// Relevant knobs from the process environment, sorted by name.
std::vector<std::pair<std::string, std::string>> FramePoseCurrentEnv();

} // namespace jjffe

#endif // JJFFE_FRAME_POSE_H
