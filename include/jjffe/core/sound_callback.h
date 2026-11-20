#ifndef JJFFE_CORE_SOUND_CALLBACK_H
#define JJFFE_CORE_SOUND_CALLBACK_H

#include <cstdint>

namespace jjffe {

// ── Platform sound callback types ──
// These are the functions that platform/SDL implements and registers.
// Core/game code calls through these pointers — never calls SDL directly.

using SoundPlayProc    = void (*)(int sampleId, int volume, int pitch);
using SoundStopProc    = void (*)();
using SoundInitProc    = void (*)();
using SoundCleanupProc = void (*)();

// ── Music callback types (Phase 56) ──
using MusicInitProc      = void (*)();
using MusicCleanupProc    = void (*)();
using MusicPlaySongProc   = void (*)(int songId);
using MusicStopSongProc   = void (*)();
using MusicSongDoneProc   = bool (*)();
using MusicSetVolumeProc  = void (*)(int volume);

// ── Callback pointer storage ──
// C++17 inline variables — one instance across all translation units.
// Null until RegisterSoundCallbacks() is called.

inline SoundInitProc    g_soundInit    = nullptr;
inline SoundCleanupProc g_soundCleanup = nullptr;
inline SoundPlayProc    g_soundPlay    = nullptr;
inline SoundStopProc    g_soundStop    = nullptr;

// ── Music callback pointers ──
inline MusicInitProc      g_musicInit      = nullptr;
inline MusicCleanupProc    g_musicCleanup   = nullptr;
inline MusicPlaySongProc   g_musicPlaySong  = nullptr;
inline MusicStopSongProc   g_musicStopSong  = nullptr;
inline MusicSongDoneProc   g_musicSongDone  = nullptr;
inline MusicSetVolumeProc  g_musicSetVolume = nullptr;

// ── Registration API ──
// Called by main.cpp during init to wire platform implementations.
// Game-logic code in sound.cpp calls through these via Dispatch* helpers.

void RegisterSoundCallbacks(
    SoundInitProc    init,
    SoundCleanupProc cleanup,
    SoundPlayProc    play,
    SoundStopProc    stop,
    // Phase 56: Music callbacks (all defaulted for backward compatibility)
    MusicInitProc      musicInit     = nullptr,
    MusicCleanupProc    musicCleanup  = nullptr,
    MusicPlaySongProc   musicPlaySong = nullptr,
    MusicStopSongProc   musicStopSong = nullptr,
    MusicSongDoneProc   musicSongDone = nullptr,
    MusicSetVolumeProc  musicSetVolume = nullptr
);

// ── Dispatch helpers (inline — callable from any translation unit) ──
// Called by sound.cpp (game-logic layer) to invoke platform functions.
// Safe to call before registration — no-ops if callbacks not yet set.

inline void DispatchPlatformSoundInit() {
    if (g_soundInit) g_soundInit();
}

inline void DispatchPlatformSoundCleanup() {
    if (g_soundCleanup) g_soundCleanup();
}

inline void DispatchSoundPlaySample(int sampleId, int volume, int pitch) {
    if (g_soundPlay) g_soundPlay(sampleId, volume, pitch);
}

inline void DispatchSoundStopAllSamples() {
    if (g_soundStop) g_soundStop();
}

// ── Music dispatch helpers (Phase 56) ──
inline void DispatchMusicInit() {
    if (g_musicInit) g_musicInit();
}
inline void DispatchMusicCleanup() {
    if (g_musicCleanup) g_musicCleanup();
}
inline void DispatchMusicPlaySong(int songId) {
    if (g_musicPlaySong) g_musicPlaySong(songId);
}
inline void DispatchMusicStopSong() {
    if (g_musicStopSong) g_musicStopSong();
}
inline bool DispatchMusicSongDone() {
    if (g_musicSongDone) return g_musicSongDone();
    return true;  // default: song is "done" (no music playing)
}
inline void DispatchMusicSetVolume(int volume) {
    if (g_musicSetVolume) g_musicSetVolume(volume);
}

} // namespace jjffe

#endif
