#ifndef JJFFE_GAME_STATE_H
#define JJFFE_GAME_STATE_H

#include <cstdint>
#include <cstddef>

#ifdef __GNUC__
#define PACKED_STRUCT __attribute__((packed))
#else
#define PACKED_STRUCT
#endif

namespace jjffe {

// Game object sizes
constexpr size_t GAME_OBJECT_SIZE = 0x152;  // 338 bytes per game object
constexpr int MAX_GAME_OBJECTS = 256;

// Game object structure (packed to match original assembly *size*).
// Named fields are Phase 60 scaffolding at offset 0, NOT ASM PhysObj
// offsets (parent +0x56, model +0x82, control +0x87 — see phys_obj.h).
struct PACKED_STRUCT GameObj {
    uint8_t  shipId;         // +0x56: Ship/object ID
    uint8_t  playerId;       // +0x86: Player ID / owner
    uint8_t  targetId;       // +0x88: Target object ID
    uint8_t  flags;          // +0x14c: Object flags (bit 3 = targeted, bit 5 = visible, etc.)
    
    int32_t  pos[3];         // +0x08: Position (3D vector)
    int32_t  vel[3];         // +0x14: Velocity (3D vector)
    int32_t  rot[9];         // +0x5a: Rotation matrix (3x3)
    
    int32_t  speed;          // +0xbe: Forward speed
    int32_t  reverseSpeed;   // +0xbc: Reverse speed
    int32_t  lateralSpeed;   // +0xc0: Left/right speed
    int32_t  verticalSpeed;  // +0xc2: Up/down speed
    
    int32_t  mass;           // +0xa8: Mass
    int32_t  fuel;           // +0xac: Fuel
    
    int32_t  shields;        // +0xad: Shield strength (fixed-point, 0 = depleted)
    int32_t  hull;           // +0xb1: Hull integrity (fixed-point, 0 = destroyed)
    int32_t  shieldMax;      // +0xb5: Maximum shield capacity
    int32_t  hullMax;        // +0xb9: Maximum hull integrity
    uint8_t  damageFlags;    // +0xbd: Damage state bits
    
    uint8_t  padding[233];   // Remainder of the 0x152-byte structure
};

static_assert(sizeof(GameObj) == GAME_OBJECT_SIZE, "GameObj size must be 338 bytes");

// System/game state
struct GameState {
    int32_t  timeAccum;        // +0x...: Time accumulator
    int32_t  timeSpeed;        // +0x...: Time speed (stardreamer)
    
    int32_t  cameraPos[3];     // +0x...: Camera position
    int32_t  cameraView[3];    // +0x...: Camera view direction
    
    int32_t  farClip;          // +0x...: Far clipping plane distance
    int32_t  frameCount;       // +0x...: Frame counter
    
    int32_t  prngState1;       // Random number generator state
    int32_t  prngState2;       // RNG state 2
    
    GameObj  playerShip;       // Player's ship object
    GameObj* objects;          // Game object array
    int32_t  objectCount;      // Number of active objects
    
    bool     isActive;         // Game is active
    bool     isDocked;         // Player is docked
    bool     isPaused;         // Game is paused
    
    uint8_t  currentMenu;      // Current menu/dialog type
    int32_t  selectedIndex;    // Selected menu item
};

// Global game state
extern GameState g_gameState;

// Display mode (FUNC_001596, DATA_009210)
extern int32_t g_displayMode;

// System lifecycle
void DisplayInit();
void SysInit();
void SysCleanup();
void SysExit();

// Random number generator (FUNC_001530)
uint16_t GameRandom();

// Game object management (FUNC_001531, FUNC_001532)
GameObj* GetGameObject(int index);                    // Get pointer to object by index
int32_t GetObjectOffset(int index, int fieldOffset);  // Get absolute offset within object

// Object operations (FUNC_001533)
void InitGameObject(GameObj* dst, const GameObj* src, int param);

// Frame update (FUNC_001534)
uint32_t UpdateGameFrame(GameObj* player, int param);

// Target selection (FUNC_001535)
void SelectTarget(GameObj* player, int param);

// Bytecode interpreter (FUNC_000041)
void RunBytecode(int* ip, GameObj* context);

// Game loop
void GameInit();
void GameUpdate();
void GameShutdown();

// State access
void SetTimeSpeed(int32_t speed);
int32_t GetTimeSpeed();
void SetPaused(bool paused);
bool IsPaused();

} // namespace jjffe

#endif // JJFFE_GAME_STATE_H
