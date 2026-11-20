#ifndef JJFFE_MISSION_H
#define JJFFE_MISSION_H

#include <cstdint>

namespace jjffe {

// Mission lifecycle API. startParam is FUNC_000147's argument: the title
// menu's DATA_007199 entry (0 = any key but 1-5 / V: new game landed at Hope).
bool InitMission(int32_t startParam);
void UpdateMission();
void DrawMission();
int  HandleMissionInput();  // -1=back to menu, 0=stay
bool MissionFrameDumpedExit();  // a one-shot frame dump is done: quit (end of the frame)
void ShutdownMission();

// Spawn pose probes (task 4.6). Altitude is mesh-surface distance in the same
// units as WorldGetPos / HopeSurfaceRadius; pitch is the camera/ship look pitch
// in 16.16 (0 = level / horizon). Orbital view is JJFFE_ORBITAL_HOPE=1;
// optional JJFFE_ORBITAL_ALT=<int >= 0> overrides the default alt 2500.
int32_t MissionAltitudeAboveHope();
// The camera rotation UpdateMission() computed this frame (for tests - see
// MissionViewMode() in core/gameplay.h for the mode it was computed from).
void GetMissionCameraRotation(int32_t out[9]);
int32_t MissionLookPitchFp16();
bool    MissionIsOrbitalHopeView();

} // namespace jjffe

#endif // JJFFE_MISSION_H
