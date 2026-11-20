#include "jjffe/core/sound_callback.h"

namespace jjffe {

void RegisterSoundCallbacks(
    SoundInitProc    init,
    SoundCleanupProc cleanup,
    SoundPlayProc    play,
    SoundStopProc    stop,
    // Phase 56: Music callbacks
    MusicInitProc      musicInit,
    MusicCleanupProc    musicCleanup,
    MusicPlaySongProc   musicPlaySong,
    MusicStopSongProc   musicStopSong,
    MusicSongDoneProc   musicSongDone,
    MusicSetVolumeProc  musicSetVolume
) {
    g_soundInit    = init;
    g_soundCleanup = cleanup;
    g_soundPlay    = play;
    g_soundStop    = stop;
    
    g_musicInit      = musicInit;
    g_musicCleanup   = musicCleanup;
    g_musicPlaySong  = musicPlaySong;
    g_musicStopSong  = musicStopSong;
    g_musicSongDone  = musicSongDone;
    g_musicSetVolume = musicSetVolume;
}

} // namespace jjffe
