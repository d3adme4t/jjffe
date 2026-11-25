#include "jjffe/sound.h"
#include "jjffe/platform.h"
#include "jjffe/core/sound_callback.h"
#include "jjffe/ffe_sound_mix.h"
#include <SDL.h>
#ifdef JJFFE_USE_SDL_MIXER
#include <SDL_mixer.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <unistd.h>

#include "case_path.h"

#ifdef JJFFE_USE_SDL_MIXER
// In-tree HMP→MIDI converter (hmp.cpp). SDL_mixer 1.2.13 aborts with
// `free(): double free` if Mix_LoadMUS is handed a .hmp — never do that.
extern "C" void* load_hmp(uint8_t* buf, uint32_t br, uint32_t* out_size);
#endif

namespace jjffe {

// SDL audio state
static bool g_soundInitialized = false;
// R1240 M6: true only while THIS module owns a device opened via SDL_OpenAudio.
// Mix_OpenAudio success must NOT set this — mixer-owned path closes via Mix_CloseAudio.
static bool g_deviceOpen = false;
static SDL_AudioSpec g_audioSpec = {};
static bool g_audioPaused = true;

// Sample state for mixing
struct Sample {
    uint8_t* data;
    uint32_t size;
    uint32_t position;
    bool playing;
    int volume;
};

static Sample g_samples[32];
static int g_sfxVolume = SDL_MIX_MAXVOLUME;
static void MaybeSfxSmoke();

// Music state (Phase 56)
#ifdef JJFFE_USE_SDL_MIXER
static Mix_Music* g_currentMusic = nullptr;
static bool g_musicInitialized = false;
static bool g_musicPausedByFocusLoss = false;
// R1239: SDL_mixer and SDL_OpenAudio cannot both own the device. When the
// music backend is registered, Mix_OpenAudio owns it and SFX ride Mix_SetPostMix.
static bool g_mixerOwnsAudio = false;

static void ProbeAndSetSoundFonts()
{
    const char* env = getenv("SDL_SOUNDFONTS");
    if (env && env[0]) {
        Mix_SetSoundFonts(env);
        // R1240 M3: always log the chosen path (including env override).
        fprintf(stderr, "SDL_mixer: using soundfont %s (from SDL_SOUNDFONTS)\n", env);
        return;
    }
    // Fluidsynth MIDI needs an explicit soundfont; without one Mix_LoadMUS
    // succeeds then FluidSynth errors at render ("No SoundFonts have been
    // requested"). Probe a few common distro paths — never invent a cache.
    static const char* kCandidates[] = {
        "/usr/share/minuet/soundfonts/GeneralUser-v1.47.sf2",
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/default.sf2",
    };
    for (const char* c : kCandidates) {
        if (access(c, R_OK) == 0) {
            Mix_SetSoundFonts(c);
            fprintf(stderr, "SDL_mixer: using soundfont %s (set SDL_SOUNDFONTS to override)\n", c);
            return;
        }
    }
    fprintf(stderr, "SDL_mixer: no soundfont found — set SDL_SOUNDFONTS to a .sf2 path\n");
}
#endif

// Audio callback — runs under SDL's audio lock; main-thread sample mutators
// must SDL_LockAudio() the same way (see SoundStopAllSamples / SoundPlaySample).
// Also installed as Mix_SetPostMix when SDL_mixer owns the device: in that path
// `stream` already holds rendered music and must NOT be cleared.
static void AudioCallback(void* userdata, uint8_t* stream, int len)
{
    (void)userdata;

    // R1241: faithful FFE FillSound when AB_AUDIO_SFX is active.
    if (ffe_mix::Enabled()) {
#ifdef JJFFE_USE_SDL_MIXER
        if (g_mixerOwnsAudio) {
            // Music already in stream — additive post-mix (do not zero).
            ffe_mix::FillSoundAdditive(stream, len);
            return;
        }
#endif
        ffe_mix::FillSound(nullptr, stream, len);
        return;
    }

#ifdef JJFFE_USE_SDL_MIXER
    // Post-mix: Mix has already written music into stream. Memset would mute it
    // (R1239 .dc_review blocker — disk-driver PCM was pure silence).
    if (!g_mixerOwnsAudio)
#endif
    {
        memset(stream, 0, len);
    }

    // Legacy sine-stub mixer (kept while AB_AUDIO_SFX hatch is off; drop next batch).
    for (int i = 0; i < 32; i++) {
        Sample& sample = g_samples[i];
        uint8_t* data = sample.data;
        if (sample.playing && data && sample.position < sample.size) {
            // One 8-bit sample byte feeds one 16-bit stream sample. The old loop
            // indexed sample.data[position + j] *and* advanced position, so the
            // read ran 1.5x ahead of the bound and walked one byte past the
            // allocation (ASan: heap-buffer-overflow in AudioCallback when a
            // launch sound played).
            const uint32_t frames = static_cast<uint32_t>(len / 2);
            const uint32_t avail = sample.size - sample.position;
            const uint32_t samplesToMix = std::min(frames, avail);

            for (uint32_t j = 0; j < samplesToMix; j++) {
                int16_t sampleVal = (int16_t)((data[sample.position] - 128) * 256);
                int16_t streamVal = ((int16_t*)stream)[j];
                int32_t mixed = (int32_t)streamVal + (int32_t)(sampleVal * sample.volume / SDL_MIX_MAXVOLUME);
                mixed = std::max((int32_t)-32768, std::min((int32_t)32767, mixed));
                ((int16_t*)stream)[j] = (int16_t)mixed;
                sample.position++;
            }
            
            // Check if sample is done
            if (sample.position >= sample.size) {
                sample.playing = false;
            }
        }
    }
}

// ── Music Backend (Phase 56) ──

#ifdef JJFFE_USE_SDL_MIXER

// PlatformMusicInit — called by RegisterSoundCallbacks during init
void PlatformMusicInit()
{
    if (g_musicInitialized) return;

    // R1239 first-run: Mix_OpenAudio fails with "Audio device already opened"
    // if PlatformSoundInit already called SDL_OpenAudio. SoundInit dispatches
    // platform SFX init before music init; when music is registered the SFX
    // path defers the device so Mix owns it (see PlatformSoundInit).
    // R1240 M6: close only when we actually opened via SDL_OpenAudio.
    if (g_deviceOpen) {
        SDL_PauseAudio(1);
        SDL_CloseAudio();
        g_deviceOpen = false;
    }

    ProbeAndSetSoundFonts();

    // 22050 Hz, S16SYS stereo, 4096 chunk size — match the SFX path.
    int result = Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 4096);
    if (result < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        // PlatformSoundInit may have deferred the device to us. Fall back to
        // SDL_OpenAudio + AudioCallback so SFX still works; music stays off.
        g_mixerOwnsAudio = false;
        g_musicInitialized = false;
        SDL_AudioSpec desired = {};
        desired.freq = 22050;
        desired.format = AUDIO_S16SYS;
        desired.channels = 2;
        desired.samples = 4096;
        desired.callback = AudioCallback;
        desired.userdata = nullptr;
        if (SDL_OpenAudio(&desired, &g_audioSpec) < 0) {
            fprintf(stderr, "SDL_OpenAudio fallback after Mix_OpenAudio failed: %s\n",
                    SDL_GetError());
            g_soundInitialized = false;
            g_deviceOpen = false;
            return;
        }
        g_soundInitialized = true;
        g_deviceOpen = true;
        g_audioPaused = true;
        printf("SDL Audio initialized (Mix fallback): %d Hz, %d channels, %d samples\n",
               g_audioSpec.freq, g_audioSpec.channels, g_audioSpec.samples);
        return;
    }

    // 0 Mix chunk channels — SFX stay on our software mixer via post-mix.
    // AudioCallback must additive-mix only (see memset guard above).
    Mix_AllocateChannels(0);
    Mix_SetPostMix(AudioCallback, nullptr);

    g_audioSpec.freq = 22050;
    g_audioSpec.format = AUDIO_S16SYS;
    g_audioSpec.channels = 2;
    g_audioSpec.samples = 4096;
    g_mixerOwnsAudio = true;
    g_soundInitialized = true;
    g_audioPaused = false;  // Mix_OpenAudio starts the device playing
    g_musicInitialized = true;
    printf("SDL_mixer initialized for music playback\n");

    // R1241: SFX smoke after Mix owns the device so post-mix hears real PCM.
    MaybeSfxSmoke();

    // R1239: JJFFE_MUSIC_SMOKE=id[,id...] plays each id once via the real
    // PlatformMusicPlaySong path (HMP convert + Mix_LoadMUS). Gate/probe only.
    if (const char* smoke = getenv("JJFFE_MUSIC_SMOKE")) {
        const char* p = smoke;
        while (*p) {
            while (*p == ',' || *p == ' ') ++p;
            if (!*p) break;
            char* end = nullptr;
            const long id = strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            fprintf(stderr, "JJFFE_MUSIC_SMOKE: play song %ld\n", id);
            PlatformMusicPlaySong(static_cast<int>(id));
            fprintf(stderr, "JJFFE_MUSIC_SMOKE: SongDone=%d PlayingMusic=%d\n",
                    PlatformMusicSongDone() ? 1 : 0, Mix_PlayingMusic());
            PlatformMusicStopSong();
            fprintf(stderr, "JJFFE_MUSIC_SMOKE: after stop SongDone=%d\n",
                    PlatformMusicSongDone() ? 1 : 0);
        }
    }
}

void PlatformMusicCleanup()
{
    if (!g_musicInitialized) return;
    
    Mix_HaltMusic();
    if (g_currentMusic) {
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
    }
    Mix_SetPostMix(nullptr, nullptr);
    Mix_CloseAudio();
    // Leave g_mixerOwnsAudio true so PlatformSoundCleanup (called after us
    // from main) skips SDL_CloseAudio on the already-closed Mix device.
    g_musicInitialized = false;
}

// Win32 ConvertHMP counterpart (w32sound.cpp): HMP → on-disk MIDI via load_hmp.
// Writes the .mid at the caller-supplied path (next to the source .hmp) so
// subsequent plays hit Mix_LoadMUS directly.
// Returns true if midiPath exists and is readable after the call.
static bool ConvertHmpToMidi(const char* hmpPath, const char* midiPath)
{
    FILE* hmp = fopen(hmpPath, "rb");
    if (!hmp) {
        fprintf(stderr, "HMP convert: cannot open %s\n", hmpPath);
        return false;
    }
    if (fseek(hmp, 0, SEEK_END) != 0) { fclose(hmp); return false; }
    const long hmpsize = ftell(hmp);
    if (hmpsize <= 0) { fclose(hmp); return false; }
    if (fseek(hmp, 0, SEEK_SET) != 0) { fclose(hmp); return false; }

    uint8_t* hmpImage = static_cast<uint8_t*>(malloc(static_cast<size_t>(hmpsize)));
    if (!hmpImage) { fclose(hmp); return false; }
    if (fread(hmpImage, 1, static_cast<size_t>(hmpsize), hmp) != static_cast<size_t>(hmpsize)) {
        free(hmpImage);
        fclose(hmp);
        fprintf(stderr, "HMP convert: short read on %s\n", hmpPath);
        return false;
    }
    fclose(hmp);

    uint32_t midisize = 0;
    void* midiImage = load_hmp(hmpImage, static_cast<uint32_t>(hmpsize), &midisize);
    free(hmpImage);
    if (!midiImage || midisize == 0) {
        fprintf(stderr, "HMP convert: load_hmp failed for %s\n", hmpPath);
        free(midiImage);
        return false;
    }

    // R1240 M7: write music/<name>.mid.tmp then rename() so a crash mid-write
    // cannot leave a truncated .mid that Mix_LoadMUS would choke on forever.
    char tmpPath[560];  // R1244: midiPath now up to 512 (FFE_DIR-prefixed); keep .tmp suffix headroom
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", midiPath);
    FILE* midi = fopen(tmpPath, "wb");
    if (!midi) {
        fprintf(stderr, "HMP convert: cannot write %s (will try in-memory load)\n", tmpPath);
        free(midiImage);
        return false;
    }
    const size_t wrote = fwrite(midiImage, 1, midisize, midi);
    fclose(midi);
    free(midiImage);
    if (wrote != midisize) {
        fprintf(stderr, "HMP convert: short write on %s\n", tmpPath);
        unlink(tmpPath);
        return false;
    }
    if (rename(tmpPath, midiPath) != 0) {
        fprintf(stderr, "HMP convert: rename %s -> %s failed\n", tmpPath, midiPath);
        unlink(tmpPath);
        return false;
    }
    // Wording intentionally omits "not found" — also used on M7 corrupt-mid retry.
    fprintf(stderr, "Music: converting from HMP version → %s (source %s)\n",
            midiPath, hmpPath);
    return true;
}

// Load a just-converted HMP from memory when the music/ dir is not writable.
static Mix_Music* LoadHmpViaMemory(const char* hmpPath)
{
    FILE* hmp = fopen(hmpPath, "rb");
    if (!hmp) return nullptr;
    if (fseek(hmp, 0, SEEK_END) != 0) { fclose(hmp); return nullptr; }
    const long hmpsize = ftell(hmp);
    if (hmpsize <= 0) { fclose(hmp); return nullptr; }
    if (fseek(hmp, 0, SEEK_SET) != 0) { fclose(hmp); return nullptr; }

    uint8_t* hmpImage = static_cast<uint8_t*>(malloc(static_cast<size_t>(hmpsize)));
    if (!hmpImage) { fclose(hmp); return nullptr; }
    if (fread(hmpImage, 1, static_cast<size_t>(hmpsize), hmp) != static_cast<size_t>(hmpsize)) {
        free(hmpImage);
        fclose(hmp);
        return nullptr;
    }
    fclose(hmp);

    uint32_t midisize = 0;
    void* midiImage = load_hmp(hmpImage, static_cast<uint32_t>(hmpsize), &midisize);
    free(hmpImage);
    if (!midiImage || midisize == 0) {
        free(midiImage);
        return nullptr;
    }

    // Mix_LoadMUSType_RW with freesrc=1 closes the RWops; we still own midiImage
    // for the lifetime of the Mix_Music. Keep it in a static until the next load
    // (PlatformMusicPlaySong always frees the previous Mix_Music first).
    static void* s_hmpMidiKeepalive = nullptr;
    free(s_hmpMidiKeepalive);
    s_hmpMidiKeepalive = midiImage;

    SDL_RWops* rw = SDL_RWFromMem(midiImage, static_cast<int>(midisize));
    if (!rw) return nullptr;
    Mix_Music* mus = Mix_LoadMUSType_RW(rw, MUS_MID, 1);
    if (!mus) {
        fprintf(stderr, "HMP convert: Mix_LoadMUSType_RW failed for %s: %s\n",
                hmpPath, Mix_GetError());
    }
    return mus;
}

// MIDI cache path next to the source .hmp: same directory, same stem,
// ".mid" extension (MUSIC/QQESCAPE.HMP -> MUSIC/QQESCAPE.MID).
static void DeriveMidiBeside(const char* hmpPath, char* out, size_t outSz)
{
    snprintf(out, outSz, "%s", hmpPath);
    char* dot = strrchr(out, '.');
    const char* slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash)) *dot = '\0';
    const size_t n = strlen(out);
    snprintf(out + n, outSz - n, ".mid");
}

void PlatformMusicPlaySong(int songId)
{
    if (!g_musicInitialized) return;
    // The table itself lives in song_table.cpp so it compiles (and is tested)
    // even when SDL_mixer is absent and this whole block is stubbed out.
    const char* songName = PlatformMusicSongName(songId);
    if (!songName) {
        fprintf(stderr, "SoundPlayMusic: song id %d out of range (0..%d)\n", songId,
                PlatformMusicSongCount() - 1);
        return;
    }
    
    // Stop current song if playing
    Mix_HaltMusic();
    if (g_currentMusic) {
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
    }
    
    // Game ships music/<name>.hmp; a few titles also have .mid. SDL_mixer must
    // only ever see MIDI — Mix_LoadMUS(.hmp) aborts on 1.2.13 (R1239 blocker).
    // R1244: resolve like the SFX loader (ResolveSamplePath, ffe_sound_mix.cpp).
    // R1246: ResolveCasePath restores DOS case-insensitive lookup — original
    // installs ship MUSIC/QQESCAPE.HMP, which a cwd-only lowercase "music/…"
    // probe cannot see on a case-sensitive FS. Exact paths win and the cwd
    // candidate is tried before $FFE_DIR/music/, so lowercase dev layouts and
    // the MusicHmpSmoke fixture behave byte-identically.
    char midiPath[512];
    char hmpPath[512];
    char cand[512];
    snprintf(cand, sizeof(cand), "music/%s.mid", songName);
    bool haveMid = ResolveCasePath(cand, midiPath, sizeof(midiPath));
    snprintf(cand, sizeof(cand), "music/%s.hmp", songName);
    bool haveHmp = ResolveCasePath(cand, hmpPath, sizeof(hmpPath));
    if (!haveMid && !haveHmp) {
        const char* ffe = getenv("FFE_DIR");
        if (ffe && ffe[0]) {
            snprintf(cand, sizeof(cand), "%s/music/%s.mid", ffe, songName);
            haveMid = ResolveCasePath(cand, midiPath, sizeof(midiPath));
            snprintf(cand, sizeof(cand), "%s/music/%s.hmp", ffe, songName);
            haveHmp = ResolveCasePath(cand, hmpPath, sizeof(hmpPath));
        }
    }

    if (!haveMid && haveHmp) {
        // Convert once, next to the .hmp (same dir, same stem); the next play
        // resolves the .mid directly.
        char midiOut[512];
        DeriveMidiBeside(hmpPath, midiOut, sizeof(midiOut));
        if (ConvertHmpToMidi(hmpPath, midiOut)) {
            snprintf(midiPath, sizeof(midiPath), "%s", midiOut);
            haveMid = true;
        } else {
            // dir not writable — convert in memory for this play only.
            g_currentMusic = LoadHmpViaMemory(hmpPath);
            if (g_currentMusic) {
                snprintf(midiPath, sizeof(midiPath), "%s->mid(mem)", hmpPath);
            }
        }
    }

    if (!g_currentMusic) {
        g_currentMusic = Mix_LoadMUS(midiPath);
    }
    // R1240 M7: corrupt/truncated .mid cache — unlink, re-convert from .hmp, retry once.
    if (!g_currentMusic && haveHmp) {
        fprintf(stderr,
                "Music: Mix_LoadMUS(%s) failed (%s); unlinking suspect .mid and re-converting from %s\n",
                midiPath, Mix_GetError(), hmpPath);
        unlink(midiPath);
        char midiOut[512];
        DeriveMidiBeside(hmpPath, midiOut, sizeof(midiOut));
        if (ConvertHmpToMidi(hmpPath, midiOut)) {
            g_currentMusic = Mix_LoadMUS(midiOut);
            if (g_currentMusic) {
                fprintf(stderr, "Music: re-convert retry succeeded for %s\n", midiOut);
            }
        }
    }
    if (!g_currentMusic) {
        fprintf(stderr, "Mix_LoadMUS(%s) failed: %s — no music available for song %d (%s)\n",
                midiPath, Mix_GetError(), songId, songName);
        return;
    }
    
    // Play the music — loop indefinitely (-1)
    if (Mix_PlayMusic(g_currentMusic, -1) < 0) {
        fprintf(stderr, "Mix_PlayMusic failed: %s\n", Mix_GetError());
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
        return;
    }
    
    // Set initial volume based on g_soundState.musicVolume
    // musicVolume is 0-63, scale to 0-MIX_MAX_VOLUME (128)
    int sdlVolume = (g_soundState.musicVolume * MIX_MAX_VOLUME) / 63;
    Mix_VolumeMusic(sdlVolume);
    
    // stderr: survives redirect (stdout is fully buffered when not a tty).
    fprintf(stderr, "Music: playing song %d (%s)\n", songId, midiPath);
}

void PlatformMusicStopSong()
{
    if (!g_musicInitialized) return;
    
    Mix_HaltMusic();
    if (g_currentMusic) {
        Mix_FreeMusic(g_currentMusic);
        g_currentMusic = nullptr;
    }
}

bool PlatformMusicSongDone()
{
    if (!g_musicInitialized) return true;
    return !Mix_PlayingMusic();
}

void PlatformMusicSetVolume(int volume)
{
    if (!g_musicInitialized) return;
    
    // volume is 0-63, scale to SDL_mixer 0-MIX_MAX_VOLUME (128)
    int sdlVolume = (volume * MIX_MAX_VOLUME) / 63;
    if (sdlVolume < 0) sdlVolume = 0;
    if (sdlVolume > MIX_MAX_VOLUME) sdlVolume = MIX_MAX_VOLUME;
    Mix_VolumeMusic(sdlVolume);
}

// PlatformMusicHandleFocus — called from main.cpp on SDL_ACTIVEEVENT
void SoundHandleFocusChange(bool hasFocus)
{
    if (!g_musicInitialized) return;
    
    if (!hasFocus && Mix_PlayingMusic() && !g_musicPausedByFocusLoss) {
        Mix_PauseMusic();
        g_musicPausedByFocusLoss = true;
    } else if (hasFocus && g_musicPausedByFocusLoss) {
        Mix_ResumeMusic();
        g_musicPausedByFocusLoss = false;
    }
}

#else // !JJFFE_USE_SDL_MIXER — stub implementations

void PlatformMusicInit()
{
    // SDL_mixer not available — music playback disabled
}

void PlatformMusicCleanup()
{
    // SDL_mixer not available — no cleanup needed
}

void PlatformMusicPlaySong(int songId)
{
    // SDL_mixer not available — music playback disabled
    (void)songId;
}

void PlatformMusicStopSong()
{
    // SDL_mixer not available — no-op
}

bool PlatformMusicSongDone()
{
    // SDL_mixer not available — always report done
    return true;
}

void PlatformMusicSetVolume(int volume)
{
    // SDL_mixer not available — volume setting ignored
    (void)volume;
}

void SoundHandleFocusChange(bool hasFocus)
{
    // SDL_mixer not available — focus change ignored
    (void)hasFocus;
}

#endif // JJFFE_USE_SDL_MIXER

static void MaybeSfxSmoke()
{
    // R1241: JJFFE_SFX_SMOKE=id[,id...] plays each sample once (MusicHmp M2
    // nonzero-control fixture + dig probe). Mixer path only.
    // Optional JJFFE_SFX_SMOKE_VOL (linear, pre vol>>7); default 0x1800 keeps
    // M2 music RMS able to clear MARGIN=1.25 against an SFX-bearing control.
    if (!ffe_mix::Enabled()) return;
    const char* smoke = getenv("JJFFE_SFX_SMOKE");
    if (!smoke || !smoke[0]) return;
    int vol = 0x1800;
    if (const char* v = getenv("JJFFE_SFX_SMOKE_VOL")) {
        char* end = nullptr;
        long parsed = strtol(v, &end, 0);
        if (end != v && parsed > 0 && parsed <= 0x7fff) vol = static_cast<int>(parsed);
    }
    const char* p = smoke;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        char* end = nullptr;
        const long id = strtol(p, &end, 10);
        if (end == p) break;
        p = end;
        fprintf(stderr, "JJFFE_SFX_SMOKE: play sample %ld vol=%d\n", id, vol);
        SDL_LockAudio();
        ffe_mix::PlaySample(static_cast<int>(id), vol, -1);
        SDL_UnlockAudio();
    }
}

// Platform sound initialization
void PlatformSoundInit()
{
    if (g_soundInitialized) return;

    g_sfxVolume = SDL_MIX_MAXVOLUME;
    memset(g_samples, 0, sizeof(g_samples));

    if (ffe_mix::Enabled()) {
        ffe_mix::Init();
        fprintf(stderr, "FFE SFX mixer enabled (AB_AUDIO_SFX)\n");
    }

#ifdef JJFFE_USE_SDL_MIXER
    // Music backend registered (not JJFFE_NO_MUSIC): defer the device to
    // PlatformMusicInit → Mix_OpenAudio + Mix_SetPostMix(AudioCallback).
    if (g_musicInit) {
        g_audioSpec.freq = 22050;
        g_audioSpec.format = AUDIO_S16SYS;
        g_audioSpec.channels = 2;
        g_audioSpec.samples = 4096;
        g_soundInitialized = true;
        g_deviceOpen = false;  // R1240 M6: deferred — Mix will own the device
        g_audioPaused = true;
        printf("SDL Audio deferred to SDL_mixer (music backend registered)\n");
        // Smoke after Mix owns the device (PlatformMusicInit) so post-mix hears it.
        return;
    }
#endif

    // JJFFE_NO_MUSIC / no-mixer builds: own the device with the SFX callback.
    SDL_AudioSpec desired = {};
    desired.freq = 22050;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 4096;
    desired.callback = AudioCallback;
    desired.userdata = nullptr;

    if (SDL_OpenAudio(&desired, &g_audioSpec) < 0) {
        fprintf(stderr, "SDL_OpenAudio failed: %s\n", SDL_GetError());
        g_soundInitialized = false;
        g_deviceOpen = false;
        return;
    }

    g_soundInitialized = true;
    g_deviceOpen = true;  // R1240 M6: SDL_OpenAudio succeeded
    g_audioPaused = true;

    printf("SDL Audio initialized: %d Hz, %d channels, %d samples\n",
           g_audioSpec.freq, g_audioSpec.channels, g_audioSpec.samples);

    MaybeSfxSmoke();
    if (ffe_mix::Enabled() && g_audioPaused) {
        SDL_PauseAudio(0);
        g_audioPaused = false;
    }
}

void PlatformSoundCleanup()
{
    if (!g_soundInitialized) return;
    // Pause + drain samples under the audio lock before CloseAudio. Without
    // this, SoundCleanup → SoundStopAllSamples can free buffers while the
    // mixer callback is mid-read (flyb intermittent SIGSEGV at shutdown).
    SDL_PauseAudio(1);
    g_audioPaused = true;
    SoundStopAllSamples();
    if (ffe_mix::Enabled()) {
        ffe_mix::Cleanup();
    }
#ifdef JJFFE_USE_SDL_MIXER
    if (g_mixerOwnsAudio) {
        // Mix_CloseAudio already ran from PlatformMusicCleanup. Do not
        // double-close the shared device; clear the ownership flag here.
        g_mixerOwnsAudio = false;
        g_soundInitialized = false;
        g_deviceOpen = false;
        return;
    }
#endif
    // R1240 M6: only SDL_CloseAudio when we opened via SDL_OpenAudio.
    if (g_deviceOpen) {
        SDL_CloseAudio();
        g_deviceOpen = false;
    }
    g_soundInitialized = false;
}

// Play sound effect using SDL audio callback
void SoundPlaySample(int sampleId, int volume, int pitch)
{
    // R1242 Leg 1: log at the same seam as ASM *SoundPlaySample (pre-filter).
    SfxMaybeLogPlay(sampleId, volume, pitch);

    if (!g_soundInitialized) return;

    if (ffe_mix::Enabled()) {
        SDL_LockAudio();
        ffe_mix::PlaySample(sampleId, volume, pitch);
        SDL_UnlockAudio();
        if (g_audioPaused) {
            SDL_PauseAudio(0);
            g_audioPaused = false;
        }
        return;
    }

    // Legacy sine stub (AB_AUDIO_SFX hatch off).
    const uint32_t size = 22050;  // 1 second at 22050 Hz
    uint8_t* data = new uint8_t[size];
    const double frequency = 440.0 * (1.0 + pitch / 100.0);
    const double sampleRate = g_audioSpec.freq > 0 ? g_audioSpec.freq : 22050;
    for (uint32_t j = 0; j < size; j++) {
        double t = (double)j / sampleRate;
        double value = sin(2.0 * 3.14159265 * frequency * t);
        data[j] = (uint8_t)((value * 0.5 + 0.5) * 255);
    }
    const int slotVolume = (volume * g_sfxVolume) / 255;

    SDL_LockAudio();
    for (int i = 0; i < 32; i++) {
        if (!g_samples[i].playing) {
            Sample& sample = g_samples[i];
            delete[] sample.data;
            sample.data = data;
            sample.size = size;
            sample.position = 0;
            sample.volume = slotVolume;
            sample.playing = true;
            data = nullptr;
            break;
        }
    }
    SDL_UnlockAudio();
    delete[] data;  // no free slot — drop the unused buffer

    if (g_audioPaused) {
        SDL_PauseAudio(0);
        g_audioPaused = false;
    }
}

// Stop all sound effects
void SoundStopAllSamples()
{
    if (!g_soundInitialized) return;

    if (ffe_mix::Enabled()) {
        SDL_LockAudio();
        ffe_mix::StopAllSamples();
        SDL_UnlockAudio();
        return;
    }

    SDL_LockAudio();
    for (int i = 0; i < 32; i++) {
        g_samples[i].playing = false;
        uint8_t* p = g_samples[i].data;
        g_samples[i].data = nullptr;
        g_samples[i].size = 0;
        g_samples[i].position = 0;
        delete[] p;
    }
    SDL_UnlockAudio();
}

// Set SFX volume
void SoundSetSFXVolume(int volume)
{
    g_sfxVolume = (volume * SDL_MIX_MAXVOLUME) / 255;
}

} // namespace jjffe
