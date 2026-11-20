// FFE software mixer (R1241–R1243) — faithful port of sdl/sdlsound.cpp FillSound /
// SoundPlaySample / LoadSample / channel rules. Default ON after R1243 promotion;
// AB_AUDIO_SFX=0 restores the sine stub (hatch).
#ifndef JJFFE_FFE_SOUND_MIX_H
#define JJFFE_FFE_SOUND_MIX_H

#include <cstdint>
#include <cstddef>

namespace jjffe {
namespace ffe_mix {

constexpr int kNumSamples = 0x28;  // SOUNDNUMSAMPLES
constexpr int kNumChannels = 16;   // NUMCHANNELS — 0 music, 1 video/stream

struct SampleDesc {
    const char* name;
    int loopcount;
    int pitchmod;
    int voice;
    int flags;
};

struct Channel {
    int index;
    void* pData;   // Sint16* PCM, or nullptr when idle
    int len;       // length in 24.8 fixed (samples << 8)
    int vol;       // post vol>>7
    int loop;
    int pos;       // 24.8
    int step;      // 24.8
};

// Hatch: default ON (R1243); AB_AUDIO_SFX=0 restores sine stub.
bool Enabled();
bool DefaultOn();  // compile-time hatch pin (true after promotion)

// Preload all descs from data_blob/sound_raw/ (fx/ override if present).
void Init();
void Cleanup();

// sdl/sdlsound.cpp:112–161 FillSound — zeros stream, mixes, clip/pack triplet.
// `len` is byte length of the stereo S16 stream (same contract as SDL callback).
void FillSound(void* userdata, uint8_t* stream, int len);

// Channel ops (caller holds SDL_LockAudio when used from the live backend).
void PlaySample(int index, int vol, int pitch);
void StopAllSamples();  // clears channels 1..15 (channel 0 reserved)

// Post-mix coexistence: FillSound into temp, then add packed S16 onto `stream`
// (music already written). Used when SDL_mixer owns the device.
void FillSoundAdditive(uint8_t* stream, int len);

// ── Test / probe hooks (unit parity + event log) ───────────────────────────
const SampleDesc* Descs();
Channel* Channels();
void** SampleDataSlots();
int* SampleLens();  // lengths in samples

void TestClearChannels();
void TestInstallSample(int index, void* data, int lenSamples);
void TestSetSoundInit(bool on);
void TestResetEnabledCache();  // re-read AB_AUDIO_SFX on next Enabled()

}  // namespace ffe_mix
}  // namespace jjffe

#endif
