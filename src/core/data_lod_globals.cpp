// Single definitions for BSS words that must be shared across TUs (planet LOD /
// shade, the real-time base, the module registry).
#include "jjffe/data_globals.h"
#include <cstdio>
#include <cstdlib>

namespace jjffe {

uint32_t DATA_007837 = 0;
uint32_t DATA_007838[16] = {};
// ASM FUNC_000228 fallthrough (no menu detail flags) sets both to 1. Zero left
// model #455 stuck on the near IsFar band (op 0x04cc) which Ends before the
// far-LOD PointSource — intro police blue never painted.
uint32_t& DATA_008812 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x10);
uint32_t& DATA_008813 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x14);
// R1115 dig: the ASM *ship demo* never runs FUNC_000228, so its 8812/8813 stay
// BSS 0 while the port's live-init state is 1/1. The walk interpreter's
// FUNC_001751 gate reads them (bias = 8812&1 ? 8813 : 8812), so the A/B oracle
// and the port were running different detail states. AB_LOD_BIAS=a,b lets the
// harness match the demo without changing the shipped default.
//
// R1116: the override must survive InitPlanetMeshTables (planet_icosa_draw.cpp
// writes 8812 during FUNC_000143, which InitGameState runs lazily on the first
// walk), so that writer re-applies both words from AbLodBiasA/B instead of the
// literal 1. The values are the player's graphics-detail setting (FUNC_000228
// SysMenuUpdateGfxDetail): -1/-2 = highest, 0/0 = middle, 1/1 = default.
//
// R1116b: read the env on every call (it is consulted only at init), so the
// capture tool can default AB_LOD_BIAS=0,0 at the top of main and have the lazy
// init see it. Caching the value at load made the tool's setenv too late and
// left the hybrid 8812=1/8813=0 state.
namespace {
struct LodBiasOverride {
    uint32_t a = 1, b = 1;
};

LodBiasOverride ReadLodBiasOverride()
{
    LodBiasOverride r;
    const char* e = getenv("AB_LOD_BIAS");
    if (!e || !e[0]) return r;
    // strtol so the negative high-detail states (-1/-2) parse too.
    char* end = nullptr;
    const long va = std::strtol(e, &end, 0);
    long vb = va;
    if (end && *end == ',') vb = std::strtol(end + 1, &end, 0);
    r.a = static_cast<uint32_t>(va);
    r.b = static_cast<uint32_t>(vb);
    return r;
}
} // namespace

uint32_t AbLodBiasA() { return ReadLodBiasOverride().a; }
uint32_t AbLodBiasB() { return ReadLodBiasOverride().b; }

void AbApplyLodBias()
{
    DATA_008812 = AbLodBiasA();
    DATA_008813 = AbLodBiasB();
}

[[maybe_unused]] static const bool s_lodBiasInit = (AbApplyLodBias(), true);

// Title object +0x1c..+0x23 (ffedat DATA_008814..008817): dword + 3-byte record.
// Contiguous so FUNC_001575's (uint16_t*)&DATA_008815 + [2] reads match the ASM.
uint32_t& DATA_008814 = *reinterpret_cast<uint32_t*>(g_bss8804 + 0x1c);
uint8_t&  DATA_008815 = g_bss8804[0x20];
uint8_t&  DATA_008816 = g_bss8804[0x21];
uint16_t& DATA_008817 = *reinterpret_cast<uint16_t*>(g_bss8804 + 0x22);

// Sky-record latch (ffe.asm JUMP_004336 / FUNC_001052). Shared so mission's
// view-setup tail and core's FUNC_001002/1003 see the same bytes (R1219).
uint32_t DATA_009136 = 0;
uint32_t DATA_009137 = 0;

// Written by FUNC_001815 / FUNC_001876 (planet_icosa_draw.cpp), read by the
// render-mode 8/9 face emitters FUNC_001844 / FUNC_001845 (core.cpp).
uint32_t DATA_009259 = 0;
uint32_t DATA_009262 = 0;
uint32_t DATA_009263 = 0;
uint32_t DATA_009265 = 0;
uint32_t DATA_009267 = 0;
uint32_t DATA_009269 = 0;
// Set up by FUNC_001876 (planet_icosa_draw.cpp), updated by the height walkers
// FUNC_001870-1872 (core.cpp): one store, as in the ASM.
uint32_t DATA_009283 = 0;
uint32_t DATA_009284 = 0;
uint32_t DATA_009285 = 0;

// Real-time base (jjffe/game_clock.h): filled by TimerAdvance (platform) and
// FUNC_001418_TimerAdvance (core), consumed by the mission frame in core.
uint32_t DATA_009157 = 0;
int32_t  DATA_009122 = 0;
uint32_t DATA_008799 = 0;

// Game-time base. DATA_007706 is ffedat's db 0xe8,0x3 (0x3e8); FUNC_000919
// stores 0x3e2 before its loop (ffe.asm:68576), ResetMissionTimebase here.
uint32_t DATA_009155 = 0;
uint32_t DATA_009156 = 0;
uint32_t DATA_007706 = 0x3e8;

namespace {
struct ClockFreeze {
    bool frozen = false;
    bool hasTimeOfDay = false;
    uint32_t timeOfDay = 0;
};

const ClockFreeze& CaptureClockFreeze()
{
    static const ClockFreeze s_freeze = []() {
        ClockFreeze f;
        const char* e = getenv("JJFFE_CLOCK_FREEZE");
        if (!e) return f;
        if (e[0] == '0' && (e[1] == 'x' || e[1] == 'X')) {
            char* end = nullptr;
            const unsigned long v = strtoul(e + 2, &end, 16);
            if (end == e + 2 || *end != '\0' || v > 0xffffffffUL) {
                fprintf(stderr, "JJFFE_CLOCK_FREEZE=%s: not a 32-bit hex time of day; clock not frozen\n", e);
                return f;
            }
            f.frozen = true;
            f.hasTimeOfDay = true;
            f.timeOfDay = static_cast<uint32_t>(v);
        } else {
            f.frozen = e[0] == '1';
        }
        return f;
    }();
    return s_freeze;
}
} // namespace

uint32_t GameClockDeltaMs(uint32_t wallDeltaMs)
{
    return CaptureClockFreeze().frozen ? 0u : wallDeltaMs;
}

bool GameClockFrozenTimeOfDay(uint32_t* timeOfDay)
{
    const ClockFreeze& f = CaptureClockFreeze();
    if (f.hasTimeOfDay && timeOfDay) *timeOfDay = f.timeOfDay;
    return f.hasTimeOfDay;
}

// ffedat.asm DATA_008622..DATA_008652: one BSS region, offsets from its resb chain.
alignas(8) uint8_t DATA_008622[0x200] = {};
uint8_t& DATA_008623 = DATA_008622[0x05];
uint8_t& DATA_008624 = DATA_008622[0x06];
uint8_t& DATA_008625 = DATA_008622[0x07];
uint8_t& DATA_008626 = DATA_008622[0x10];
int8_t&  DATA_008627 = reinterpret_cast<int8_t&>(DATA_008622[0x16]);
uint8_t& DATA_008628 = DATA_008622[0x17];
uint8_t& DATA_008629 = DATA_008622[0x18];
int8_t&  DATA_008630 = reinterpret_cast<int8_t&>(DATA_008622[0x1e]);
uint8_t& DATA_008631 = DATA_008622[0x1f];
uint8_t& DATA_008632 = DATA_008622[0x20];
uint8_t& DATA_008633 = DATA_008622[0x2c];
uint8_t& DATA_008634 = DATA_008622[0x2e];
uint8_t& DATA_008635 = DATA_008622[0x2f];
uint8_t& DATA_008636 = DATA_008622[0x3d];
uint8_t& DATA_008637 = DATA_008622[0x50];
uint8_t& DATA_008638 = DATA_008622[0x5d];
uint8_t& DATA_008639 = DATA_008622[0x60];
uint8_t& DATA_008640 = DATA_008622[0x66];
uint8_t& DATA_008641 = DATA_008622[0x67];
uint8_t& DATA_008642 = DATA_008622[0x6e];
uint8_t& DATA_008643 = DATA_008622[0x70];
uint8_t& DATA_008644 = DATA_008622[0x75];
uint8_t& DATA_008645 = DATA_008622[0x76];
uint8_t& DATA_008646 = DATA_008622[0x7d];
int8_t&  DATA_008647 = reinterpret_cast<int8_t&>(DATA_008622[0x7e]);
uint8_t& DATA_008648 = DATA_008622[0x84];
uint8_t& DATA_008649 = DATA_008622[0x8d];
uint8_t& DATA_008650 = DATA_008622[0x98];
uint8_t& DATA_008651 = DATA_008622[0x9e];
uint8_t& DATA_008652 = DATA_008622[0xa0];

} // namespace jjffe
