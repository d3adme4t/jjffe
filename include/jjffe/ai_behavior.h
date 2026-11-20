#ifndef JJFFE_AI_BEHAVIOR_H
#define JJFFE_AI_BEHAVIOR_H

#include <cstdint>

namespace jjffe {

enum class AIBehaviorType : uint8_t {
    Patrol = 0,    // Fly in a pattern around a center point
    Pursue = 1,    // Move toward player ship
    Evade = 2,     // Move away from player ship
    Count = 3
};

void AIBehaviorInit();
void AIBehaviorUpdate();  // Process all NPC ships

} // namespace jjffe

#endif // JJFFE_AI_BEHAVIOR_H
