#ifndef JJFFE_COMBAT_H
#define JJFFE_COMBAT_H

#include <cstdint>

namespace jjffe {

// Projectile structure
struct Projectile {
    int32_t  pos[3];         // Current position (fixed-point)
    int32_t  vel[3];         // Velocity (fixed-point)
    int32_t  damage;         // Damage value (fixed-point)
    uint8_t  ownerId;        // Who fired it (playerId)
    int32_t  lifetime;       // Frames remaining (0 = expired)
    bool     active;         // Is this projectile slot active?
};

// Explosion particle
struct Explosion {
    int32_t  pos[3];         // World position (fixed-point)
    int16_t  px, py;         // Projected screen position
    int32_t  radius;         // Current expansion radius (fixed-point)
    int32_t  maxRadius;      // Maximum radius
    int32_t  alpha;          // Fade (0 = invisible, 256 = fully visible)
    int32_t  lifetime;       // Frames remaining
    bool     active;
};

constexpr int MAX_PROJECTILES = 64;
constexpr int MAX_EXPLOSIONS  = 16;

void CombatInit();
void CombatUpdate();
void CombatFireWeapon();  // Spawn projectile from player ship

// Accessors for rendering (read-only)
const Projectile* GetProjectile(int index);  // Returns nullptr if out of bounds or inactive
int GetMaxProjectiles();

// Explosion accessors
const Explosion* GetExplosion(int index);
int GetMaxExplosions();

// Combat stats
int GetKillCount();
int GetPlayerDamageThisFrame();  // Damage player took this frame (for flash FX)

} // namespace jjffe

#endif // JJFFE_COMBAT_H
