#ifndef JJFFE_D3D_DEMO_H
#define JJFFE_D3D_DEMO_H

#include <cstdint>

namespace jjffe {
namespace d3d {

bool D3dDemoInit(int width, int height);
void D3dDemoShutdown();

void D3dDrawDemo();
void D3dDrawIntroCast();
// Soft 320×200 screens blit for GPU path (parity with OgreDrawShipSelect / menu).
void D3dDrawShipSelect();
void D3dDrawMenu(uint32_t frameCount, uint32_t currentFps);

// Optional CLI override (--planet N). -1 = unset.
void D3dSetPlanetOverride(int modelId);
int D3dGetPlanetOverride();
// --sol: instance every sphere-eligible WorldPhysObj (schematic layout).
void D3dSetSolSystem(bool on);
bool D3dSolSystem();

} // namespace d3d
} // namespace jjffe

#endif
