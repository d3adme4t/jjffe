#ifndef JJFFE_BRIEFING_H
#define JJFFE_BRIEFING_H

#include <cstdint>

namespace jjffe {

bool InitBriefing();
void DrawBriefing();
int  HandleBriefingInput();  // -1=back to menu, 0=stay, 1=start mission

} // namespace jjffe

#endif // JJFFE_BRIEFING_H
