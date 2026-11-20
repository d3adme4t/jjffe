#include "jjffe/combat.h"
#include "jjffe/game_state.h"
#include "jjffe/math.h"
#include <cstdlib>
#include <cstring>

namespace jjffe {

// Module state
static Projectile s_projectiles[MAX_PROJECTILES];
static Explosion  s_explosions[MAX_EXPLOSIONS];
static int32_t    s_killCount = 0;
static int32_t    s_playerDamageThisFrame = 0;

// Explosion constants
static constexpr int32_t kExplosionMaxRadius = FP_ONE * 3;
static constexpr int32_t kExplosionGrowRate  = FP_ONE / 4;
static constexpr int32_t kExplosionLifetime  = 60;
static constexpr int32_t kExplosionAlphaMax  = 256;

static constexpr int32_t kProjectileDamage   = FP_ONE * 5;    // 5 damage per hit
static constexpr int32_t kProjectileSpeed     = FP_ONE * 8;    // Projectile speed
static constexpr int32_t kProjectileLifetime  = 120;           // 2 seconds at 60fps
static constexpr int32_t kCollisionThreshold  = FP_ONE * 2;    // Hit distance threshold
static constexpr int32_t kPositionBound       = 100 * FP_ONE;  // Projectile bounds
static constexpr int32_t kPositionScale       = 8;             // >> 8 for position integration
static constexpr int32_t kMaxSpeed            = FP_ONE * 4;    // Speed clamp for AI ships

void CombatInit()
{
    std::memset(s_projectiles, 0, sizeof(s_projectiles));
    std::memset(s_explosions, 0, sizeof(s_explosions));
    s_killCount = 0;
    s_playerDamageThisFrame = 0;
}

void CombatFireWeapon()
{
    // Find first inactive projectile slot
    int slot = -1;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;  // All slots full

    const GameObj& ship = g_gameState.playerShip;

    // Forward direction = ship's local Z axis = rot column 2 (indices 2, 5, 8)
    // Spawn position = ship position + offset in forward direction
    int64_t offsetX = static_cast<int64_t>(ship.rot[2]) * 2;  // 2 * FP_ONE = 2 units forward
    int64_t offsetY = static_cast<int64_t>(ship.rot[5]) * 2;
    int64_t offsetZ = static_cast<int64_t>(ship.rot[8]) * 2;

    s_projectiles[slot].pos[0] = ship.pos[0] + static_cast<int32_t>(offsetX);
    s_projectiles[slot].pos[1] = ship.pos[1] + static_cast<int32_t>(offsetY);
    s_projectiles[slot].pos[2] = ship.pos[2] + static_cast<int32_t>(offsetZ);

    // Velocity = forward direction * projectile speed
    s_projectiles[slot].vel[0] = static_cast<int64_t>(ship.rot[2]) * kProjectileSpeed >> 16;
    s_projectiles[slot].vel[1] = static_cast<int64_t>(ship.rot[5]) * kProjectileSpeed >> 16;
    s_projectiles[slot].vel[2] = static_cast<int64_t>(ship.rot[8]) * kProjectileSpeed >> 16;

    s_projectiles[slot].damage = kProjectileDamage;
    s_projectiles[slot].ownerId = ship.playerId;
    s_projectiles[slot].lifetime = kProjectileLifetime;
    s_projectiles[slot].active = true;
}

void CombatUpdate()
{
    // Reset per-frame tracking
    s_playerDamageThisFrame = 0;

    // Phase 1: Update projectiles (move and manage lifetime)
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) continue;

        s_projectiles[i].lifetime--;
        if (s_projectiles[i].lifetime <= 0) {
            s_projectiles[i].active = false;
            continue;
        }

        // Update position
        s_projectiles[i].pos[0] += s_projectiles[i].vel[0] >> kPositionScale;
        s_projectiles[i].pos[1] += s_projectiles[i].vel[1] >> kPositionScale;
        s_projectiles[i].pos[2] += s_projectiles[i].vel[2] >> kPositionScale;

        // Bounds check
        if (s_projectiles[i].pos[0] > kPositionBound ||
            s_projectiles[i].pos[0] < -kPositionBound ||
            s_projectiles[i].pos[1] > kPositionBound ||
            s_projectiles[i].pos[1] < -kPositionBound ||
            s_projectiles[i].pos[2] > kPositionBound ||
            s_projectiles[i].pos[2] < -kPositionBound) {
            s_projectiles[i].active = false;
        }
    }

    // Phase 2: Collision detection
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!s_projectiles[i].active) continue;

        for (int j = 0; j < g_gameState.objectCount; j++) {
            GameObj* obj = GetGameObject(j);
            if (!obj) continue;

            // Skip if projectile owner (prevent self-damage)
            if (obj->playerId == s_projectiles[i].ownerId) continue;

            // Skip already destroyed objects
            if (obj->hull <= 0) continue;

            // Manhattan distance
            int32_t dist = abs(s_projectiles[i].pos[0] - obj->pos[0]) +
                           abs(s_projectiles[i].pos[1] - obj->pos[1]) +
                           abs(s_projectiles[i].pos[2] - obj->pos[2]);

            if (dist < kCollisionThreshold) {
                // HIT!
                int32_t remainingDamage = s_projectiles[i].damage;

                // Track damage taken by player (for damage flash effect)
                if (obj->playerId == g_gameState.playerShip.playerId) {
                    s_playerDamageThisFrame += remainingDamage >> 16;
                }

                // Apply damage to shields first
                if (obj->shields > 0) {
                    obj->shields -= remainingDamage;
                    if (obj->shields < 0) {
                        // Overflow damage passes to hull
                        remainingDamage = -obj->shields;
                        obj->shields = 0;
                        obj->hull -= remainingDamage;
                    }
                } else {
                    // Shields down, direct hull hit
                    obj->hull -= remainingDamage;
                }

                // Clamp hull to 0 minimum
                if (obj->hull < 0) obj->hull = 0;

                // Update damage flags
                if (obj->shields <= 0) obj->damageFlags |= 1;
                if (obj->hull <= 0) {
                    obj->damageFlags |= 2;
                    // Spawn explosion at destroyed ship position
                    for (int e = 0; e < MAX_EXPLOSIONS; e++) {
                        if (!s_explosions[e].active) {
                            s_explosions[e].pos[0] = obj->pos[0];
                            s_explosions[e].pos[1] = obj->pos[1];
                            s_explosions[e].pos[2] = obj->pos[2];
                            s_explosions[e].radius = 0;
                            s_explosions[e].maxRadius = kExplosionMaxRadius;
                            s_explosions[e].alpha = kExplosionAlphaMax;
                            s_explosions[e].lifetime = kExplosionLifetime;
                            s_explosions[e].active = true;
                            break;
                        }
                    }
                }

                // Consume projectile
                s_projectiles[i].active = false;
                break;
            }
        }
    }

    // Phase 3: Remove destroyed NPC ships via two-pass compaction
    // Pass 1: Count survivors and mark destroyed
    int survivorCount = 0;
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = GetGameObject(i);
        if (obj && obj->hull > 0) {
            survivorCount++;
        }
    }

    // Track kills: objects that were destroyed this frame
    int destroyedThisFrame = g_gameState.objectCount - survivorCount;
    s_killCount += destroyedThisFrame;

    // If nothing destroyed, skip compaction but still update explosions
    if (survivorCount == g_gameState.objectCount) {
        for (int i = 0; i < MAX_EXPLOSIONS; i++) {
            if (!s_explosions[i].active) continue;
            s_explosions[i].lifetime--;
            s_explosions[i].radius += kExplosionGrowRate;
            s_explosions[i].alpha = (s_explosions[i].lifetime * kExplosionAlphaMax) / kExplosionLifetime;
            if (s_explosions[i].lifetime <= 0 || s_explosions[i].radius > s_explosions[i].maxRadius)
                s_explosions[i].active = false;
        }
        return;
    }

    // Pass 2: Copy survivors to front of array
    uint8_t* src = reinterpret_cast<uint8_t*>(g_gameState.objects);
    uint8_t* dst = src;
    for (int i = 0; i < g_gameState.objectCount; i++) {
        GameObj* obj = reinterpret_cast<GameObj*>(src + i * GAME_OBJECT_SIZE);
        if (obj->hull > 0) {
            if (dst != src + i * GAME_OBJECT_SIZE) {
                std::memmove(dst, src + i * GAME_OBJECT_SIZE, GAME_OBJECT_SIZE);
            }
            dst += GAME_OBJECT_SIZE;
        }
    }

    // Update object count
    g_gameState.objectCount = survivorCount;

    // Phase 4: Update explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!s_explosions[i].active) continue;
        s_explosions[i].lifetime--;
        s_explosions[i].radius += kExplosionGrowRate;
        s_explosions[i].alpha = (s_explosions[i].lifetime * kExplosionAlphaMax) / kExplosionLifetime;
        if (s_explosions[i].lifetime <= 0 || s_explosions[i].radius > s_explosions[i].maxRadius)
            s_explosions[i].active = false;
    }
}

const Projectile* GetProjectile(int index)
{
    if (index < 0 || index >= MAX_PROJECTILES) return nullptr;
    if (!s_projectiles[index].active) return nullptr;
    return &s_projectiles[index];
}

int GetMaxProjectiles()
{
    return MAX_PROJECTILES;
}

const Explosion* GetExplosion(int index)
{
    if (index < 0 || index >= MAX_EXPLOSIONS) return nullptr;
    if (!s_explosions[index].active) return nullptr;
    return &s_explosions[index];
}

int GetMaxExplosions()
{
    return MAX_EXPLOSIONS;
}

int GetKillCount()
{
    return s_killCount;
}

int GetPlayerDamageThisFrame()
{
    return s_playerDamageThisFrame;
}

} // namespace jjffe
