#ifndef JJFFE_TIMER_H
#define JJFFE_TIMER_H

#include <cstdint>

namespace jjffe {

// Timer state structure
struct TimerState {
    uint32_t lastTime;          // Last frame timestamp (ms)
    uint32_t next50Hz;          // Next 50Hz tick time (ms)
    uint32_t minFrameTime;      // Minimum frame time (ms)
    
    // Game time counters
    uint32_t gameTimeMs;        // Game time in milliseconds
    uint32_t gameTimeTicks;     // Game time in 50Hz ticks
    
    // Frame timing
    uint32_t frameCount;        // Total frames rendered
    uint32_t fpsCounter;        // FPS counter (reset every second)
    uint32_t lastFpsUpdate;     // Last FPS update time
    uint32_t currentFps;        // Current FPS
    
    // 50Hz callback
    void (*callback50Hz)();     // Function called at 50Hz
};

// Global timer state
extern TimerState g_timerState;

// Initialization
void TimerInit();
void TimerCleanup();

// Frame timing
void TimerAdvance();            // Advance timer, sleep if needed
void TimerFrameUpdate();        // Update frame counter

// 50Hz callback
void Timer50Hz();               // Called at 50Hz (set by game)
void Timer50HzCounters();       // The ASM body's counters (core.cpp)
void TimerSet50HzCallback(void (*callback)());

// Time queries
uint32_t TimerGetTimeStamp();   // Get current timestamp (ms)
uint32_t TimerGetGameTime();    // Get game time in ticks
uint32_t TimerGetDeltaTime();   // Get delta time since last frame (ms)
uint32_t TimerGetFPS();         // Get current FPS

// Utility
void TimerSleep();              // Sleep until next frame

} // namespace jjffe

#endif // JJFFE_TIMER_H
