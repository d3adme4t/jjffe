#include "jjffe/game_state.h"
#include "jjffe/bytecode.h"
#include "jjffe/video.h"
#include "jjffe/input.h"
#include "jjffe/sound.h"
#include "jjffe/timer.h"
#include "jjffe/draw.h"
#include "jjffe/ship_model.h"
#include "jjffe/core/math.h"
#include "jjffe/core/engine.h"
#include "jjffe/core/gameplay.h"
#include "jjffe/string.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace jjffe {

// Global game state
GameState g_gameState = {};
int32_t g_displayMode = 0;

// System lifecycle

// FUNC_001596_DisplayInit
void DisplayInit()
{
    g_displayMode = 4;
    MouseInit();
    VideoInit();
}

uint32_t FUNC_001900_SoundInit(void);

// FUNC_001527_SysInit
void SysInit()
{
    // Platform init
    VideoInit();
    
    // Input, display, sound, timer
    KeybInit();
    DisplayInit();
    SoundInit();
    FUNC_001900_SoundInit();
    TimerInit();
}

// FUNC_001528_SysCleanup
void SysCleanup()
{
    // Cleanup subsystems in reverse order
    SoundStopSong();
    // TimerCleanup();
    KeybCleanup();
    // VideoCleanup();
    
    // Platform cleanup
    // SystemCleanup();
}

// FUNC_001529_SysExit
void SysExit()
{
    SysCleanup();
    exit(0);
}

// Random number generator

// FUNC_001530 - Simple pseudo-random number generator
// Uses two 32-bit state variables with XOR-shift
uint16_t GameRandom()
{
    // ffe.asm:539f4 — all four shifts are LOGICAL (shl/shr) and the add wraps
    // mod 2^32. The fields are int32_t, so the old spelling both invoked
    // signed-overflow UB (UBSan: 1660172870 + 1051537204) and arithmetic-shifted
    // where the ASM shifts logically; do the whole step on the unsigned view.
    const uint32_t s1 = static_cast<uint32_t>(g_gameState.prngState1);
    const uint32_t s2 = static_cast<uint32_t>(g_gameState.prngState2);

    const uint32_t x = s2 ^ 0x1234567Fu;
    const uint32_t mix = (x << 25) | (x >> 7);

    const uint32_t next1 = s1 + mix;                       // ASM add [DATA_008818]
    g_gameState.prngState1 = static_cast<int32_t>(next1);
    g_gameState.prngState2 = static_cast<int32_t>((s1 << 13) | (s1 >> 19));

    return static_cast<uint16_t>(next1 & 0xFFFFu);
}

// Game object management

// FUNC_001531 - Get pointer to game object by index
GameObj* GetGameObject(int index)
{
    if (index < 0 || index >= MAX_GAME_OBJECTS) return nullptr;
    return reinterpret_cast<GameObj*>(
        reinterpret_cast<uint8_t*>(g_gameState.objects) + index * GAME_OBJECT_SIZE
    );
}

// FUNC_001532 - Get absolute offset within game object
int32_t GetObjectOffset(int index, int fieldOffset)
{
    return index * GAME_OBJECT_SIZE + fieldOffset + 0x74;
    // The +0x74 comes from the base offset of the object array (DATA_007758 + 0x74)
}

// Object operations

// FUNC_001533 - Initialize/copy game object
void InitGameObject(GameObj* dst, const GameObj* src, int initType)
{
    uint8_t savedId = dst->playerId;
    
    if (dst != src) {
        // Copy entire object structure
        std::memcpy(dst, src, GAME_OBJECT_SIZE);
        
        if (initType == 0) {
            dst->playerId = 0;
        } else {
            dst->playerId = savedId;
        }
    }
    
    if (initType != 0) {
        // Initialize position to current system
        // dst->mass = g_currentTime;
        // dst->fuel = g_currentFuel;
        
        // Set object data pointer
        // dst->objectData = initType;
        
        // Get object type info
        // int objType = GetObjectType(initType);
        
        // Set ship dimensions
        // dst->boundingBox = CalculateBoundingBox(objType);
        
        // Set flags
        // uint8_t newFlags = (objType->flags >> 4) & 7;
        // if (objType->name != nullptr && objType->name[0] != '\0') newFlags |= 0x80;
        // dst->flags = newFlags;
        
        dst->targetId = 0xFF;
        
        // Initialize weapons data if available
        // if (objType->weaponData != nullptr) {
        //     dst->weaponCount = *(objType->weaponData);
        //     dst->weaponCapacity = *(objType->weaponData + 2);
        //     ...
        // }
    }
}

// Frame update

// FUNC_001534 - Update camera/view for a game object
uint32_t UpdateGameFrame(GameObj* player, int param)
{
    uint32_t farClip = g_gameState.farClip >> 10;
    
    // Set player reference
    // g_playerShipData = player;
    
    // Compute relative position to camera
    // ComputeRelativePosition(&relativeVec, param);
    
    // Decrement frame counter
    g_gameState.frameCount--;
    
    if (g_gameState.frameCount < 0) {
        // Select new target periodically
        SelectTarget(player, param);
        g_gameState.frameCount = 0;
    }
    
    // Transform position to view space
    // VecMatTMul(&viewPos, &cameraMatrix, param);
    
    // Handle player's ship (with cockpit position)
    if (param != 0) {  // If this is the player's ship
        // VecMatTMul(&cockpitPos, &cameraMatrix, param + 0x5a /* cockpit offset */);
    }
    
    return farClip;
}

// Target selection

// FUNC_001535 - Find and select best target
void SelectTarget(GameObj* player, int param)
{
    int currentIdx = param;
    
    do {
        if (currentIdx != 0 && GetGameObject(currentIdx)->playerId != 0) {
            // Navigate to previous object
            // currentIdx = GetPreviousObject(currentIdx);
            break;
        }
        // currentIdx = GetNextObject(currentIdx);
    } while (true);  // Original loops until non-targeted object found
    
    // If current object is a ship of type 0x17
    // if (GetObjectType(*GetGameObject(currentIdx)) == 0x17) {
    //     currentIdx = GetPreviousObject(currentIdx->playerId - 1);
    // }
    
    // Compute relative position
    // ComputeRelativePosition(&relVec, param);
    
    // Truncate vector
    // Vec64Truncate(&relVec, 0xD);
    
    // Set camera view direction
    // cameraView[0] = relVec[0];
    // cameraView[1] = relVec[1];
    // cameraView[2] = relVec[2];
    
    // Normalize view direction
    // VecNormalize(&cameraView, &cameraView);
    
    // Set target indicator based on target type
    // uint8_t targetType = (*GetObjectType(currentIdx)->flags >> 4) & 7;
    // memcpy(&targetIndicator, &targetTable[targetType], 0x18);
}

// Bytecode interpreter

// FUNC_000041 - Main script/bytecode interpreter
// This is a complex function (517 lines, 48 calls) that interprets a custom
// bytecode format. It uses a switch statement with cases for:
//   0: End/exit
//   1: Execute named function with parameters
//   2: Conditional skip (if condition == 0, skip ahead)
//   3: Unconditional jump
//   4: Set console buttons
//   5: Process UI interaction
void GameInit()
{
    std::memset(&g_gameState, 0, sizeof(GameState));
    
    // Initialize RNG with some seed
    g_gameState.prngState1 = 0x12345678;
    g_gameState.prngState2 = 0x9ABCDEF0;
    
    g_gameState.isActive = true;
    g_gameState.isDocked = true;  // Start docked
    g_gameState.isPaused = false;
    
    // Initialize subsystems
    SysInit();

    // Initialize ship model data from blob (Phase 1a)
    InitShipModels();
}

// GameUpdate - Main game loop update
void GameUpdate()
{
    if (!g_gameState.isActive || g_gameState.isPaused) {
        return;
    }
    
    // Advance timer
    TimerAdvance();
    
    // Update game time
    // g_gameState.timeAccum += TimerGetDeltaTime() * g_gameState.timeSpeed;
    
    // Process input
    GuiProcessInput();
    
    // Update game frame
    if (!g_gameState.isDocked) {
        // UpdateGameFrame(&g_gameState.playerShip, 0);
    }
    
    // Flip screen (present rendered frame)
    // FlipScreen();
}

// GameShutdown - Shutdown the game
void GameShutdown()
{
    g_gameState.isActive = false;
    SysCleanup();
}

// State access

void SetTimeSpeed(int32_t speed)
{
    g_gameState.timeSpeed = speed;
}

int32_t GetTimeSpeed()
{
    return g_gameState.timeSpeed;
}

void SetPaused(bool paused)
{
    g_gameState.isPaused = paused;
}

bool IsPaused()
{
    return g_gameState.isPaused;
}

} // namespace jjffe
