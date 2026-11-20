#ifndef JJFFE_D3D_BACKEND_H
#define JJFFE_D3D_BACKEND_H

#include <cstdint>

struct SDL_Window;
struct IDirect3DDevice9;

namespace jjffe {
namespace d3d {

bool D3dInit();
void D3dShutdown();

// Present one frame for the current game phase (called from PlatformVideoBlit).
void D3dPresentFrame();

SDL_Window* D3dGetWindow();
IDirect3DDevice9* D3dGetDevice();
int D3dGetWidth();
int D3dGetHeight();

// Asset root: JJFFE_D3D_DATA, cwd, or paths next to the binary / ffe106/FFED3D.
const char* D3dGetDataRoot();

void D3dBeginFrame(float r, float g, float b);
void D3dEndFrame();

} // namespace d3d
} // namespace jjffe

#endif
