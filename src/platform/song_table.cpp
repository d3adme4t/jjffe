// The game's songs, by id. Taken from the legacy SDL port's table
// (sdl/sdlsound.cpp, ppSoundSongNames): the order IS the id - 0 = qqfront ...
// 16 = ggofkiev - and the FFE music directory ships exactly these names as .hmp
// (a few also as .mid).
//
// It lives in its own translation unit, free of SDL, for two reasons: the music
// loader only compiles when SDL_mixer is present (it is not, in some builds), and
// the table itself is data worth testing in every build.
#include "jjffe/platform.h"

namespace jjffe {

namespace {
const char* const kSongNames[] = {
    "qqfront", "qqatmosp", "qqtravel", "qqdrama", "qqsuspen", "qqrock",
    "qqescape", "qqparadi", "qqfront2", "hallking", "intro", "babayaga",
    "barem", "jupalt", "valkries", "bluedan", "ggofkiev",
};
constexpr int kSongCount = static_cast<int>(sizeof(kSongNames) / sizeof(kSongNames[0]));
}  // namespace

const char* PlatformMusicSongName(int songId)
{
    if (songId < 0 || songId >= kSongCount) return nullptr;
    return kSongNames[songId];
}

int PlatformMusicSongCount() { return kSongCount; }

}  // namespace jjffe
