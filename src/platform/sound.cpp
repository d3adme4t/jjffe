#include "jjffe/sound.h"
#include "jjffe/platform.h"
#include "jjffe/core/sound_callback.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace jjffe {

// R1242: shared SFX event log (Leg 1). Lives in the weak/platform seam so
// test_logic_replay (no sdl_sound.o) and ffelnxsdl both emit the same line.
void SfxMaybeLogPlay(int index, int vol, int pitch)
{
    const char* e = getenv("JJFFE_SFX_PLAY_LOG");
    if (!e || e[0] != '1') return;
    static int s_ord = 0;
    ++s_ord;
    fprintf(stderr, "SFX_PLAY ordinal=%d index=%d vol=%d pitch=%d\n",
            s_ord, index, vol, pitch);
}

// Platform sound stubs (overridden by sdl_sound.cpp when SDL is enabled)
__attribute__((weak)) void PlatformSoundInit()
{
}

__attribute__((weak)) void PlatformSoundCleanup()
{
}

__attribute__((weak)) void SoundPlaySample(int sampleId, int volume, int pitch)
{
    // Game→platform seam (matches ASM *SoundPlaySample). Log even when the
    // weak stub has no mixer — Leg 1 event parity uses test_logic_replay.
    SfxMaybeLogPlay(sampleId, volume, pitch);
}

__attribute__((weak)) void SoundStopAllSamples()
{
}

// Global sound state
SoundState g_soundState = {};

// Initialization

// FUNC_001900_SoundInit
void SoundInit()
{
    g_soundState.currentSong = -1;
    
    // Initialize all songs as available
    for (int i = 0; i < MAX_SONGS; i++) {
        g_soundState.songAvailable[i] = true;
    }
    
    // Initialize all samples as not playing
    for (int i = 0; i < MAX_SAMPLES; i++) {
        g_soundState.samplePlaying[i] = false;
    }
    
    // Initialize all samples as enabled
    for (int i = 0; i < 256; i++) {
        g_soundState.sampleEnabled[i] = true;
    }
    
    // Set default volumes
    g_soundState.masterVolume = 63;
    g_soundState.musicVolume = 63;
    g_soundState.sfxVolume = 63;
    
    // Platform call
    g_soundState.soundEnabled = true;
    g_soundState.sfxEnabled = true;
    g_soundState.musicEnabled = true;
    
    DispatchPlatformSoundInit();
    DispatchMusicInit();
}

void SoundCleanup()
{
    DispatchMusicCleanup();
    SoundStopSong();
    SoundStopAllSamples();
}

// Music functions

// FUNC_001902_SoundPlaySong
void SoundPlaySong(int songId)
{
    if (!g_soundState.musicEnabled || !g_soundState.soundEnabled) {
        return;
    }
    
    if (songId < 0) {
        // Random song selection
        songId = rand() % MAX_SONGS;
        
        // Try to find an available song
        int attempts = 0;
        while (attempts < 10 && (!g_soundState.songAvailable[songId] || songId == g_soundState.currentSong)) {
            songId = rand() % MAX_SONGS;
            attempts++;
        }
        
        if (attempts >= 10) {
            return; // No available songs
        }
    }
    
    if (songId >= MAX_SONGS) {
        return;
    }
    
    // Check if song is available and not already playing
    if (g_soundState.songAvailable[songId] && songId != g_soundState.currentSong) {
        DispatchMusicPlaySong(songId);
        g_soundState.currentSong = songId;
    }
}

// FUNC_001903_SoundStopSong
void SoundStopSong()
{
    DispatchMusicStopSong();
    g_soundState.currentSong = -1;
}

// FUNC_001904_SoundSongDone
bool SoundSongDone()
{
    return DispatchMusicSongDone();
}

// FUNC_000215_SysMenuDisableSong
void SoundDisableSong(int songId)
{
    if (songId >= 0 && songId < MAX_SONGS) {
        g_soundState.songAvailable[songId] = false;
        
        // If currently playing this song, stop it
        if (g_soundState.currentSong == songId) {
            SoundStopSong();
        }
    }
}

// Sample functions

// FUNC_001908_SoundPlaySampleLinVol
void SoundPlaySampleLinVol(int sampleId, int linVolume)
{
    if (!g_soundState.sfxEnabled || !g_soundState.soundEnabled) {
        return;
    }
    
    // Check if sample is enabled
    if (!g_soundState.sampleEnabled[sampleId]) {
        return;
    }
    
    // Special handling for certain samples
    if (sampleId == SAMPLE_ENGINE || sampleId == SAMPLE_HYPERSPACE) {
        // These samples have special behavior
        // Would check additional flags
    }
    
    // Scale volume
    int scaledVolume = linVolume * 0x200;
    if (scaledVolume > 0x7FFF) {
        scaledVolume = 0x7FFF;
    }
    
    DispatchSoundPlaySample(sampleId, scaledVolume, -1);
    
    // Mark a channel as playing (simplified)
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (!g_soundState.samplePlaying[i]) {
            g_soundState.samplePlaying[i] = true;
            break;
        }
    }
}

// FUNC_001907_SoundPlaySampleLogVol
void SoundPlaySampleLogVol(int sampleId, int logVolume)
{
    if (logVolume < 0) {
        logVolume = 0;
    }
    
    // Convert logarithmic volume to linear
    // logVolume is 0-63, shift right by (logVolume >> 2) & 0x1F
    int linVolume = 0x3F >> ((logVolume >> 2) & 0x1F);
    
    SoundPlaySampleLinVol(sampleId, linVolume);
}

// FUNC_001909_SoundStopAllSamples - implemented in sdl_sound.cpp when SDL is enabled

void SoundStopSample(int channel)
{
    if (channel >= 0 && channel < MAX_SAMPLES) {
        // Platform call: SoundStopSample(channel);
        g_soundState.samplePlaying[channel] = false;
    }
}

// Volume control

void SoundSetMasterVolume(int volume)
{
    g_soundState.masterVolume = std::clamp(volume, 0, 63);
}

void SoundSetMusicVolume(int volume)
{
    g_soundState.musicVolume = std::clamp(volume, 0, 63);
    DispatchMusicSetVolume(g_soundState.musicVolume);
}

void SoundSetSfxVolume(int volume)
{
    g_soundState.sfxVolume = std::clamp(volume, 0, 63);
}

// SoundSetSfxVolume - implemented in sdl_sound.cpp when SDL is enabled

// Sample enable/disable

void SoundEnableSample(int sampleId)
{
    if (sampleId >= 0 && sampleId < 256) {
        g_soundState.sampleEnabled[sampleId] = true;
    }
}

void SoundDisableSample(int sampleId)
{
    if (sampleId >= 0 && sampleId < 256) {
        g_soundState.sampleEnabled[sampleId] = false;
    }
}

} // namespace jjffe
