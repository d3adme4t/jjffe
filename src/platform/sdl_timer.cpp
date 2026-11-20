#include "jjffe/timer.h"
#include "jjffe/platform.h"
#include <SDL.h>
#include <cstdio>
#include <thread>
#include <chrono>

namespace jjffe {

// SDL timer state
static bool g_timerInitialized = false;
static uint32_t g_timerStart = 0;

// Platform timer initialization
void PlatformTimerInit()
{
    if (g_timerInitialized) return;
    
    g_timerStart = SDL_GetTicks();
    g_timerInitialized = true;
    
    printf("SDL Timer initialized\n");
}

void PlatformTimerCleanup()
{
    g_timerInitialized = false;
}

} // namespace jjffe
