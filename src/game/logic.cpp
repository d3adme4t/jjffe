#include "jjffe/logic.h"
#include "jjffe/game_state.h"
#include "jjffe/math.h"

namespace jjffe {

void GameLogicInit()
{
    // Initialize mission-specific game logic
    g_gameState.isActive = true;
    g_gameState.isDocked = false;
    g_gameState.isPaused = false;
    g_gameState.timeSpeed = FP_ONE;  // Normal time speed
    g_gameState.frameCount = 0;
    // Future: initialize AI, physics, combat systems
}

} // namespace jjffe
