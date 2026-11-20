#include "jjffe/timer.h"
#include "jjffe/platform.h"
#include "jjffe/game_clock.h"
#include "jjffe/replay_hooks.h"
#include <chrono>
#include <thread>

#ifdef JJFFE_USE_SDL
#include <SDL.h>
#endif

namespace jjffe {

// Platform timer stubs (overridden by sdl_timer.cpp when SDL is enabled)
__attribute__((weak)) void PlatformTimerInit()
{
}

__attribute__((weak)) void PlatformTimerCleanup()
{
}

// Global timer state
TimerState g_timerState = {};

ReplayTimerHook g_replayTimerHook = nullptr;

// Get current timestamp in milliseconds
static uint32_t GetCurrentTimeMs()
{
    using namespace std::chrono;
    auto now = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch());
    return static_cast<uint32_t>(ms.count());
}

// Initialization

// FUNC_001416_TimerInit
void TimerInit()
{
    // Default minimum frame time: 50ms (20 FPS)
    // This would be read from config file
    g_timerState.minFrameTime = 1000 / 50; // 20ms for 50 FPS
    
    if (g_timerState.minFrameTime < 5) {
        g_timerState.minFrameTime = 5;
    }
    
    // Initialize timestamps
    g_timerState.lastTime = TimerGetTimeStamp();
    g_timerState.next50Hz = g_timerState.lastTime;
    
    // Initialize counters
    g_timerState.gameTimeMs = 0;
    g_timerState.gameTimeTicks = 0;
    g_timerState.frameCount = 0;
    g_timerState.fpsCounter = 0;
    g_timerState.lastFpsUpdate = g_timerState.lastTime;
    g_timerState.currentFps = 0;
    
    // No callback by default
    g_timerState.callback50Hz = nullptr;
    
#ifdef JJFFE_USE_SDL
    PlatformTimerInit();
#endif
}

void TimerCleanup()
{
    // Nothing to cleanup
}

// Frame timing

// FUNC_001418_TimerAdvance
void TimerAdvance()
{
    // Platform call: TimerFrameUpdate();
    g_timerState.frameCount++;
    
    uint32_t currentTime = 0;
    uint32_t deltaTime = 0;
    if (g_replayTimerHook) {
        deltaTime = g_replayTimerHook();
        currentTime = g_timerState.lastTime + deltaTime;
    } else {
        // Wait for minimum frame time
        currentTime = TimerGetTimeStamp();
        deltaTime = currentTime - g_timerState.lastTime;

        while (deltaTime < g_timerState.minFrameTime) {
            TimerSleep();
            currentTime = TimerGetTimeStamp();
            deltaTime = currentTime - g_timerState.lastTime;
        }
    }

    // .50hzloop (ffe.asm:85849-85855), on the frame's timestamp: the replay's
    // pinned one too. The port's timestamps are epoch ms truncated to 32 bits,
    // so the comparison is on the difference.
    while (static_cast<int32_t>(currentTime - g_timerState.next50Hz) >= 0) {
        g_timerState.next50Hz += 20;
        Timer50Hz();
    }
    
    // Update last time
    g_timerState.lastTime = currentTime;
    
    // ffe.asm:85858-85872: DATA_009157 += (deltaTime * 0x3e2) / 0x14,
    // DATA_009156 += (deltaTime * DATA_007706) / 0x14
    const uint32_t clockMs = GameClockDeltaMs(deltaTime);
    DATA_009157 += GameClockUnitsForMs(clockMs);
    DATA_009156 += GameClockAccelUnitsForMs(clockMs, DATA_007706);
    g_timerState.gameTimeMs += deltaTime;
    g_timerState.gameTimeTicks += (deltaTime * 50) / 20;
    
    // Update FPS counter
    g_timerState.fpsCounter++;
    if (currentTime - g_timerState.lastFpsUpdate >= 1000) {
        g_timerState.currentFps = g_timerState.fpsCounter;
        g_timerState.fpsCounter = 0;
        g_timerState.lastFpsUpdate = currentTime;
    }
}

// FUNC_001417_TimerCleanup (stub in original)
void TimerFrameUpdate()
{
    // Platform call: TimerFrameUpdate();
    // This is called by the platform layer each frame
    g_timerState.frameCount++;
}

// 50Hz callback

// Timer50Hz (ffe.asm:85740-85772)
void Timer50Hz()
{
    Timer50HzCounters();
    g_timerState.gameTimeTicks++;
    
    // Call registered callback
    if (g_timerState.callback50Hz) {
        g_timerState.callback50Hz();
    }
}

void TimerSet50HzCallback(void (*callback)())
{
    g_timerState.callback50Hz = callback;
}

// Time queries

// FUNC_00141A_TimerGetTimeStamp
uint32_t TimerGetTimeStamp()
{
    // Platform call: return TimerGetTimeStamp();
    return GetCurrentTimeMs();
}

uint32_t TimerGetGameTime()
{
    return g_timerState.gameTimeTicks;
}

uint32_t TimerGetDeltaTime()
{
    return TimerGetTimeStamp() - g_timerState.lastTime;
}

uint32_t TimerGetFPS()
{
    return g_timerState.currentFps;
}

// Utility

void TimerSleep()
{
    // Platform call: TimerSleep();
    // Sleep for 1ms to yield CPU
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

} // namespace jjffe
