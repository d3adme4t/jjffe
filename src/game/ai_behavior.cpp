#include <cstring>
#include "jjffe/ai_behavior.h"
#include "jjffe/game_state.h"
#include "jjffe/math.h"
#include <cstdlib>

namespace jjffe {

// Module state
static AIBehaviorType s_shipBehaviors[MAX_GAME_OBJECTS];

// Patrol parameters per ship
struct PatrolParams {
    int32_t centerX, centerY, centerZ;
    int32_t radius;
    int32_t angle;  // Current patrol angle (fixed-point)
};
static PatrolParams s_patrolParams[MAX_GAME_OBJECTS];

// Constants
static constexpr int32_t kPatrolRadius = 10 * FP_ONE;
static constexpr int32_t kPatrolAngleIncrement = FP_ONE / 256;
static constexpr int32_t kPursueSpeed = FP_ONE / 4;
static constexpr int32_t kEvadeSpeed = FP_ONE / 6;
static constexpr int32_t kEvadeThreshold = 5 * FP_ONE;
static constexpr int32_t kMaxSpeed = 4 * FP_ONE;

// FaceDirection helper: update rotation matrix to face (dx, dz) direction
[[maybe_unused]] static void FaceDirection(GameObj* obj, int32_t dx, int32_t dz)
{
    // 8-directional angle approximation
    uint32_t yaw = 0;

    if (dx > 0) {
        if (dz > 0) {
            // NE quadrant
            yaw = (dz > dx) ? 0x2000 : 0x1000;
        } else {
            // SE quadrant
            yaw = (dz < dx) ? 0x6000 : 0x7000;
        }
    } else if (dx < 0) {
        if (dz > 0) {
            // NW quadrant
            yaw = (dz > -dx) ? 0xE000 : 0xF000;
        } else {
            // SW quadrant
            yaw = (dz < dx) ? 0xA000 : 0x9000;
        }
    } else {
        // dx == 0
        yaw = (dz > 0) ? 0x4000 : 0xC000;
    }

    Mat33 rotTmp; std::memcpy(&rotTmp, obj->rot, sizeof(rotTmp)); MatBuildYZ(&rotTmp, 0, yaw); std::memcpy(obj->rot, &rotTmp, sizeof(rotTmp));
}

// Speed clamping helper
static void ClampVelocity(int32_t vel[3])
{
    for (int i = 0; i < 3; i++) {
        if (vel[i] > kMaxSpeed) vel[i] = kMaxSpeed;
        if (vel[i] < -kMaxSpeed) vel[i] = -kMaxSpeed;
    }
}

// Manhattan distance (fixed-point)
static int32_t ManhattanDistance(int32_t x1, int32_t y1, int32_t z1,
                                  int32_t x2, int32_t y2, int32_t z2)
{
    return abs(x1 - x2) + abs(y1 - y2) + abs(z1 - z2);
}

// Patrol behavior: orbit around center point
static void UpdatePatrol(int idx, GameObj* obj)
{
    PatrolParams& pp = s_patrolParams[idx];

    // Increment patrol angle
    pp.angle += kPatrolAngleIncrement;

    // Compute target position on orbit
    int32_t sinVal = Sin32(static_cast<uint32_t>(pp.angle));
    int32_t cosVal = Sin32(static_cast<uint32_t>(pp.angle + 0x4000));  // cos = sin(angle + pi/2)

    int32_t targetX = pp.centerX + (static_cast<int64_t>(sinVal) * pp.radius >> 16);
    int32_t targetZ = pp.centerZ + (static_cast<int64_t>(cosVal) * pp.radius >> 16);

    // Set velocity toward target
    obj->vel[0] = (targetX - obj->pos[0]) >> 4;
    obj->vel[1] = 0;  // Keep Y flat
    obj->vel[2] = (targetZ - obj->pos[2]) >> 4;

    { int32_t velTmp[3]; std::memcpy(velTmp, obj->vel, sizeof(velTmp)); ClampVelocity(velTmp); std::memcpy(obj->vel, velTmp, sizeof(velTmp)); }
}

// Pursue behavior: move toward player ship
static void UpdatePursue(GameObj* obj)
{
    const GameObj& player = g_gameState.playerShip;

    // Direction to player
    int32_t dx = player.pos[0] - obj->pos[0];
    int32_t dy = player.pos[1] - obj->pos[1];
    int32_t dz = player.pos[2] - obj->pos[2];

    // Normalize (approximate using Manhattan distance)
    int32_t magnitude = abs(dx) + abs(dy) + abs(dz);
    if (magnitude == 0) {
        obj->vel[0] = 0;
        obj->vel[1] = 0;
        obj->vel[2] = 0;
        return;
    }

    // Set velocity toward player (quarter speed)
    obj->vel[0] = static_cast<int64_t>(dx) * kPursueSpeed / magnitude;
    obj->vel[1] = static_cast<int64_t>(dy) * kPursueSpeed / magnitude;
    obj->vel[2] = static_cast<int64_t>(dz) * kPursueSpeed / magnitude;

    { int32_t velTmp[3]; std::memcpy(velTmp, obj->vel, sizeof(velTmp)); ClampVelocity(velTmp); std::memcpy(obj->vel, velTmp, sizeof(velTmp)); }
}

// Evade behavior: move away from player when close
static void UpdateEvade(int idx, GameObj* obj)
{
    const GameObj& player = g_gameState.playerShip;

    // Distance to player
    int32_t dist = ManhattanDistance(
        obj->pos[0], obj->pos[1], obj->pos[2],
        player.pos[0], player.pos[1], player.pos[2]
    );

    // Only evade when player is close
    if (dist < kEvadeThreshold) {
        // Direction away from player (negated)
        int32_t dx = player.pos[0] - obj->pos[0];
        int32_t dy = player.pos[1] - obj->pos[1];
        int32_t dz = player.pos[2] - obj->pos[2];

        int32_t magnitude = abs(dx) + abs(dy) + abs(dz);
        if (magnitude == 0) {
            // Player at same position — flee in random direction
            obj->vel[0] = -kEvadeSpeed;
            obj->vel[1] = 0;
            obj->vel[2] = 0;
        } else {
            obj->vel[0] = -static_cast<int64_t>(dx) * kEvadeSpeed / magnitude;
            obj->vel[1] = -static_cast<int64_t>(dy) * kEvadeSpeed / magnitude;
            obj->vel[2] = -static_cast<int64_t>(dz) * kEvadeSpeed / magnitude;
        }

        { int32_t velTmp[3]; std::memcpy(velTmp, obj->vel, sizeof(velTmp)); ClampVelocity(velTmp); std::memcpy(obj->vel, velTmp, sizeof(velTmp)); }
    } else {
        // Fall back to patrol
        UpdatePatrol(idx, obj);
    }
}

void AIBehaviorInit()
{
    // Assign behaviors round-robin for variety
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = GetGameObject(i);
        if (!obj || obj->playerId != 0) continue;  // Skip player ship

        // Round-robin assignment: 0=Patrol, 1=Pursue, 2=Evade, 3=Patrol, ...
        int behaviorIdx = 0;
        int npcCount = 0;
        for (int j = 0; j <= i; j++) {
            GameObj* checkObj = GetGameObject(j);
            if (checkObj && checkObj->playerId == 0) {
                if (j == i) {
                    behaviorIdx = npcCount % 3;
                }
                npcCount++;
            }
        }

        s_shipBehaviors[i] = static_cast<AIBehaviorType>(behaviorIdx);

        // Initialize patrol parameters
        s_patrolParams[i].centerX = obj->pos[0];
        s_patrolParams[i].centerY = obj->pos[1];
        s_patrolParams[i].centerZ = obj->pos[2];
        s_patrolParams[i].radius = kPatrolRadius;
        s_patrolParams[i].angle = static_cast<int32_t>(GameRandom()) & 0xFFFF;
    }
}

void AIBehaviorUpdate()
{
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = GetGameObject(i);
        if (!obj || obj->playerId != 0) continue;  // Skip player ship

        // Skip destroyed ships
        if (obj->flags & 0x80) continue;

        AIBehaviorType behavior = s_shipBehaviors[i];

        switch (behavior) {
        case AIBehaviorType::Patrol:
            UpdatePatrol(i, obj);
            break;
        case AIBehaviorType::Pursue:
            UpdatePursue(obj);
            break;
        case AIBehaviorType::Evade:
            UpdateEvade(i, obj);
            break;
        default:
            UpdatePatrol(i, obj);
            break;
        }
    }
}

} // namespace jjffe
