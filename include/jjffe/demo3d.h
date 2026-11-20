#ifndef JJFFE_DEMO3D_H
#define JJFFE_DEMO3D_H

#include <cstddef>

namespace jjffe {

// Ship picker shown before the 3D demo (Main Menu → 3D Demo).
void InitDemoShipSelect();
void DrawDemoShipSelect();
// Returns: 0=stay, 1=start demo with selection, -1=back to main menu
int HandleDemoShipSelectInput();

// Direct ship index set (for --ship N CLI flag, bypasses picker).
void SetDemoShipIndex(int idx);
// When true, ESC in Demo3D exits to Shutdown instead of ship select.
void SetDemoDirectLaunch(bool direct);
bool IsDemoDirectLaunch();
// When true, D3D draws internal mesh bytecode as GPU triangles (FFED3D
// DrawReal* role) instead of Assimp .x. Soft blit is not used.
void SetDemoLegacySoft(bool legacy);
bool IsDemoLegacySoft();

// When true, D3D draws exported ship assets (export/<idx>_name/) with
// hull → punch → overlay painter passes + normal bias (B5).
void SetDemoExportAsset(bool on);
bool IsDemoExportAsset();
// Optional root for export dirs (default "export"). Copied; may be relative.
void SetDemoExportAssetRoot(const char* root);
const char* GetDemoExportAssetRoot();

// Phase B B2: --ship prefers export/ when opted in (production colors).
// --use-export enables all ships; JJFFE_ASSET_SHIPS=all|15,31,60 selects a set.
void SetDemoUseExport(bool on);
bool IsDemoUseExport();
void SetDemoAssetShipsFromEnv();  // parse JJFFE_ASSET_SHIPS once
bool DemoShipUsesExportAsset(int shipIdx);

bool InitDemo3D();
void DrawDemo3D();
int GetDemoShipIndex();

// Ship-select list state (for D3D UI parity with soft picker).
int GetDemoShipSelectCount();
int GetDemoShipSelectCursor();
int GetDemoShipSelectScroll();
int GetDemoShipSelectAt(int row);
const char* GetDemoShipDisplayName(int shipIdx, char* buf, size_t buflen);

} // namespace jjffe

#endif // JJFFE_DEMO3D_H
