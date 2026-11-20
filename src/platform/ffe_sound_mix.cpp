// R1241 — FFE S16 software mixer. Algorithm source: sdl/sdlsound.cpp
//   descs :19–60, FillSound :112–161, StopAll :169–176, PlaySample :178–237,
//   LoadSample :239–266.
#include "jjffe/ffe_sound_mix.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace jjffe {
namespace ffe_mix {

// sdl/sdlsound.cpp:19–60 — value-for-value.
static const SampleDesc kDescs[kNumSamples] = {
    { "nowt", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },           // 0x4
    { "shipexp", 0, 0, -1, 0 },
    { "missexpl", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },           // 0x8
    { "nowt", 0, 0, -1, 0 },
    { "laser1", 0, 0, -1, 0 },
    { "laser2", 0, 0, -1, 0 },
    { "airopen", 0, 0, -1, 0 },        // 0xc
    { "airopen", 0, 0, -1, 0 },
    { "wind", 0, -0x4000, 8, 0 },
    { "station", 0x32, 0, 9, 0 },
    { "station", 0x32, 0x1000, 2, 0 },  // 0x10 — engine (index 16)
    { "nowt", 0, 0, -1, 0 },
    { "airopen", 0, 0, -1, 0 },
    { "airopen", 0, 0, -1, 0 },
    { "bip", 0, 0, -1, 0 },            // 0x14
    { "bip", 0, 0, -1, 0 },
    { "bong", 0, 0, -1, 0 },
    { "chime", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },           // 0x18
    { "ecm", 2, 0, 3, 0 },
    { "necm", 2, 0, 4, 0 },
    { "shipexp", 0, 0, -1, 0 },
    { "missexpl", 0, 0, -1, 0 },       // 0x1c
    { "damage", 0, 0, -1, 0 },
    { "siren", 3, 0, 5, 0 },
    { "boom", 0, 0, 6, 0 },
    { "launch", 0, 0, -1, 0 },         // 0x20
    { "hype", 0, 0, -1, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "laser3", 0, 0, -1, 0 },
    { "laser4", 0, 0, -1, 0 },         // 0x24
    { "wind", 0, -0x4000, 7, 0 },
    { "nowt", 0, 0, -1, 0 },
    { "camera", 0, 0, -1, 0 },
};

static void* g_sampleData[kNumSamples] = {};
static int g_sampleLen[kNumSamples] = {};  // samples (bytes/2)
static Channel g_channel[kNumChannels] = {};
static bool g_soundInit = false;
static bool g_ownsSampleHeap[kNumSamples] = {};

// R1243: promoted default-ON after Leg 1–3 green (in-flight event parity +
// real-sample unit + event-anchored live stream). AB_AUDIO_SFX=0 restores the
// sine stub (hatch; retires after upstream review).
#ifndef JJFFE_AB_AUDIO_SFX_DEFAULT
#define JJFFE_AB_AUDIO_SFX_DEFAULT 1
#endif

bool DefaultOn() { return JJFFE_AB_AUDIO_SFX_DEFAULT != 0; }

static int s_enabledCache = -1;

void TestResetEnabledCache() { s_enabledCache = -1; }

bool Enabled()
{
    if (s_enabledCache < 0) {
        const char* e = getenv("AB_AUDIO_SFX");
#if JJFFE_AB_AUDIO_SFX_DEFAULT
        // Promoted: default ON; AB_AUDIO_SFX=0 restores sine stub.
        s_enabledCache = (e && e[0] == '0') ? 0 : 1;
#else
        // Pre-promotion: default OFF; AB_AUDIO_SFX=1 activates the mixer.
        s_enabledCache = (e && e[0] == '1') ? 1 : 0;
#endif
    }
    return s_enabledCache != 0;
}

const SampleDesc* Descs() { return kDescs; }
Channel* Channels() { return g_channel; }
void** SampleDataSlots() { return g_sampleData; }
int* SampleLens() { return g_sampleLen; }

void TestClearChannels()
{
    for (int i = 0; i < kNumChannels; i++) {
        g_channel[i] = Channel{};
    }
}

void TestInstallSample(int index, void* data, int lenSamples)
{
    if (index < 0 || index >= kNumSamples) return;
    if (g_ownsSampleHeap[index] && g_sampleData[index]) {
        free(g_sampleData[index]);
    }
    g_sampleData[index] = data;
    g_sampleLen[index] = lenSamples;
    g_ownsSampleHeap[index] = false;
}

void TestSetSoundInit(bool on) { g_soundInit = on; }

static bool FileReadable(const char* path)
{
    return path && path[0] && access(path, R_OK) == 0;
}

// Resolve <name>.raw: fx/ override if present, else data_blob/sound_raw/,
// else FFE_DIR/fx/. Never shell out / download.
static bool ResolveSamplePath(const char* name, char* out, size_t outSz)
{
    char cand[512];

    snprintf(cand, sizeof(cand), "fx/%s.raw", name);
    if (FileReadable(cand)) {
        snprintf(out, outSz, "%s", cand);
        return true;
    }

    snprintf(cand, sizeof(cand), "data_blob/sound_raw/%s.raw", name);
    if (FileReadable(cand)) {
        snprintf(out, outSz, "%s", cand);
        return true;
    }

    snprintf(cand, sizeof(cand), "../data_blob/sound_raw/%s.raw", name);
    if (FileReadable(cand)) {
        snprintf(out, outSz, "%s", cand);
        return true;
    }

    if (const char* ffe = getenv("FFE_DIR")) {
        snprintf(cand, sizeof(cand), "%s/fx/%s.raw", ffe, name);
        if (FileReadable(cand)) {
            snprintf(out, outSz, "%s", cand);
            return true;
        }
    }

    out[0] = '\0';
    return false;
}

static int LoadSample(int index)
{
    char path[512];
    void* pSample = nullptr;
    int size = 0;

    if (!ResolveSamplePath(kDescs[index].name, path, sizeof(path))) {
        fprintf(stderr, "Sample %s not found\n", kDescs[index].name);
        g_sampleData[index] = nullptr;
        g_sampleLen[index] = 0;
        g_ownsSampleHeap[index] = false;
        return 1;
    }

    FILE* pFile = fopen(path, "rb");
    if (pFile != nullptr) {
        fseek(pFile, 0, SEEK_END);
        size = static_cast<int>(ftell(pFile));
        fseek(pFile, 0, SEEK_SET);
        if (size != 0) {
            pSample = malloc(static_cast<size_t>(size));
            if (pSample) {
                if (fread(pSample, 1, static_cast<size_t>(size), pFile) !=
                    static_cast<size_t>(size)) {
                    free(pSample);
                    pSample = nullptr;
                    size = 0;
                }
            } else {
                size = 0;
            }
        }
        fclose(pFile);
    } else {
        fprintf(stderr, "Sample %s not found\n", path);
    }

    g_sampleData[index] = pSample;
    g_sampleLen[index] = size / 2;  // sdl/sdlsound.cpp:264 — samples, not bytes
    g_ownsSampleHeap[index] = (pSample != nullptr);
    return 1;
}

void Init()
{
    for (int i = 0; i < kNumSamples; i++) LoadSample(i);
    for (int i = 0; i < kNumChannels; i++) g_channel[i].pData = nullptr;
    g_soundInit = true;
}

void Cleanup()
{
    StopAllSamples();
    for (int i = 0; i < kNumSamples; i++) {
        if (g_ownsSampleHeap[i] && g_sampleData[i]) free(g_sampleData[i]);
        g_sampleData[i] = nullptr;
        g_sampleLen[i] = 0;
        g_ownsSampleHeap[i] = false;
    }
    g_soundInit = false;
}

// sdl/sdlsound.cpp:112–161
void FillSound(void* /*userdata*/, uint8_t* stream, int len)
{
    int* pAccum = reinterpret_cast<int*>(stream);
    len /= 4;  // stereo S16 frames

    for (int i = 0; i < len; i++) pAccum[i] = 0;

    for (int n = 0; n < kNumChannels; n++) {
        Channel* pChan = g_channel + n;
        if (pChan->pData == nullptr) continue;
        int16_t* pData = static_cast<int16_t*>(pChan->pData);

        for (int i = 0; i < len; i++) {
            if (pChan->vol > 250)
                pAccum[i] += pData[pChan->pos >> 8];
            else
                pAccum[i] += (pData[pChan->pos >> 8] * pChan->vol) >> 8;
            pChan->pos += pChan->step;

            if (pChan->pos >= pChan->len) {
                pChan->pos -= pChan->len;
                if (pChan->loop-- == 0) {
                    pChan->pData = nullptr;
                    break;  // break FRAME loop for this channel
                }
            }
        }
    }

    // Clip/pack triplet — sdl/sdlsound.cpp:153–160
    for (int i = 0; i < len; i++) {
        if (pAccum[i] < -32768)
            pAccum[i] = -2147450880;
        else if (pAccum[i] > 0x7fff)
            pAccum[i] = 0x7fff7fff;
        else
            pAccum[i] = (pAccum[i] & 0xffff) | (pAccum[i] << 16);
    }
}

void FillSoundAdditive(uint8_t* stream, int len)
{
    if (len <= 0) return;
    // FillSound consumes frames of 4 bytes (stereo S16 packed as int32). Round
    // down so we never read past the initialized region when len % 4 != 0.
    const int mixLen = len & ~3;
    if (mixLen <= 0) return;
    // R1241 closeout (checker MINOR): no malloc/free in the realtime post-mix
    // callback. FillSound consumes whole 4-byte frames, so round the scratch up
    // to a multiple of 4 and grow-on-demand; cap at one second of stereo S16.
    static uint8_t* s_scratch = nullptr;
    static int s_cap = 0;
    const int want = (mixLen + 3) & ~3;
    if (want > s_cap) {
        if (want > 22050 * 4) { s_scratch = nullptr; s_cap = 0; return; } // refuse >1s
        free(s_scratch);
        s_scratch = static_cast<uint8_t*>(malloc(static_cast<size_t>(want)));
        s_cap = s_scratch ? want : 0;
        if (!s_scratch) return;
    }
    FillSound(nullptr, s_scratch, mixLen);
    int16_t* dst = reinterpret_cast<int16_t*>(stream);
    const int16_t* src = reinterpret_cast<const int16_t*>(s_scratch);
    const int n = mixLen / 2;
    for (int i = 0; i < n; i++) {
        int32_t m = static_cast<int32_t>(dst[i]) + static_cast<int32_t>(src[i]);
        if (m < -32768) m = -32768;
        else if (m > 32767) m = 32767;
        dst[i] = static_cast<int16_t>(m);
    }
}

// sdl/sdlsound.cpp:169–176 — channels 1..15 (0 reserved for music)
void StopAllSamples()
{
    for (int i = 1; i < kNumChannels; i++) g_channel[i].pData = nullptr;
}

// sdl/sdlsound.cpp:178–237
void PlaySample(int index, int vol, int pitch)
{
    if (!g_soundInit) return;
    if (index < 0 || index >= kNumSamples) return;   // ASM guards only >=; negative is OOB UB
    if (g_sampleData[index] == nullptr) return;

    if (pitch == -1) pitch = 22050;
    pitch += kDescs[index].pitchmod;
    if (pitch > 100000) pitch = 100000;
    if (pitch < 100) pitch = 100;

    int i;
    const int fixednum = kDescs[index].voice;
    if (fixednum != -1) {
        Channel* pChan = g_channel + fixednum;
        if (pChan->pData == nullptr) {
            i = fixednum;
        } else if (index != 16) {
            return;  // fixed voice busy — drop (non-engine)
        } else {
            // engine noise (index 16) — in-place update
            pChan->vol = vol >> 7;
            pChan->step = (pitch << 8) / 22050;
            pChan->loop = 0x32;
            return;
        }
    } else {
        // free-scan channels 10..15
        for (i = 10; i < kNumChannels; i++) {
            if (g_channel[i].pData == nullptr) break;
        }
        // second pass: reuse a beam-laser (stored index 0xb) slot
        if (i == kNumChannels) {
            for (i = 10; i < kNumChannels; i++) {
                if (g_channel[i].index == 0xb) break;
            }
        }
        if (i == kNumChannels) return;  // no free slots
    }

    Channel* pChan = g_channel + i;
    pChan->index = index;
    pChan->len = g_sampleLen[index] << 8;
    pChan->loop = kDescs[index].loopcount;
    pChan->pos = 0;
    pChan->step = (pitch << 8) / 22050;
    pChan->vol = vol >> 7;
    pChan->pData = g_sampleData[index];
}

}  // namespace ffe_mix
}  // namespace jjffe
