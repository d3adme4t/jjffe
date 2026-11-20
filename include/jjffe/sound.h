#ifndef JJFFE_SOUND_H
#define JJFFE_SOUND_H

#include <cstdint>

namespace jjffe {

// Maximum number of simultaneous sound effects
constexpr int MAX_SAMPLES = 8;

// Maximum number of songs
constexpr int MAX_SONGS = 17;

// Sound sample IDs (partial list - extend as needed)
enum SampleID : int {
    SAMPLE_NONE = -1,
    SAMPLE_LASER = 0x0E,
    SAMPLE_MISSILE = 0x10,
    SAMPLE_EXPLOSION = 0x14,
    SAMPLE_ENGINE = 0x15,
    SAMPLE_HYPERSPACE = 0x25,
};

// Song IDs (partial list - extend as needed)
enum SongID : int {
    SONG_NONE = -1,
    SONG_TITLE = 0,
    SONG_GAME = 1,
    SONG_COMBAT = 2,
    SONG_DOCKED = 3,
};

// Sound state structure
struct SoundState {
    // Configuration
    bool soundEnabled;          // Sound system enabled
    bool musicEnabled;          // Music enabled
    bool sfxEnabled;            // Sound effects enabled
    
    // Music state
    int currentSong;            // Currently playing song ID
    bool songAvailable[MAX_SONGS]; // Which songs are available
    
    // Sample state
    bool samplePlaying[MAX_SAMPLES]; // Which sample slots are playing
    bool sampleEnabled[256];    // Which samples are enabled (not muted)
    
    // Volume
    int masterVolume;           // Master volume (0-63)
    int musicVolume;            // Music volume (0-63)
    int sfxVolume;              // SFX volume (0-63)
};

// Global sound state
extern SoundState g_soundState;

// Initialization
void SoundInit();
void SoundCleanup();

// Music functions
void SoundPlaySong(int songId);
void SoundStopSong();
bool SoundSongDone();
void SoundDisableSong(int songId);

// R1242: JJFFE_SFX_PLAY_LOG=1 → SFX_PLAY ordinal= index= vol= pitch=
void SfxMaybeLogPlay(int index, int vol, int pitch);

// Sample functions
void SoundPlaySample(int sampleId, int volume, int pitch);
void SoundPlaySampleLogVol(int sampleId, int logVolume);
void SoundPlaySampleLinVol(int sampleId, int linVolume);
void SoundStopAllSamples();
void SoundStopSample(int channel);

// Volume control
void SoundSetMasterVolume(int volume);
void SoundSetMusicVolume(int volume);
void SoundSetSfxVolume(int volume);

// Sample enable/disable
void SoundEnableSample(int sampleId);
void SoundDisableSample(int sampleId);

// Focus management (Phase 56)
void SoundHandleFocusChange(bool hasFocus);

} // namespace jjffe

#endif // JJFFE_SOUND_H
