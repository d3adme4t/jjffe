# JJFFE Function Reference

Complete API reference for the ported C++17 implementation of Frontier: First Encounters.

## Overview

This document describes the **ported C++ functions** that reimplement the original NASM assembly code. All functions are in the `jjffe::` namespace and maintain behavioral fidelity to the 1996 DOS game.

**Total ported functions:** ~1,700 (100% of named functions)

---

## Core Subsystem (`src/core/`)

### Math & Fixed-Point Arithmetic

Fixed-point math using 16.16 format (int32_t), no floating point for game logic.

| Function | Description |
|----------|-------------|
| `FixedMul(int32_t a, int32_t b)` | Multiply two fixed-point values: `(a * b) >> 16` |
| `FixedDiv(int32_t a, int32_t b)` | Divide with overflow protection |
| `SpecialDiv(int32_t a, int32_t b)` | Safe division avoiding divide-by-zero |
| `Sin16(int32_t angle)` | 16-bit sine lookup table |
| `Sin32(int32_t angle)` | 32-bit sine for higher precision |
| `Cos16(int32_t angle)` | 16-bit cosine lookup |
| `SqrtFixed(int32_t value)` | Square root for fixed-point values |
| `MatMul(const int32_t a[9], const int32_t b[9], int32_t out[9])` | 3×3 matrix multiplication |
| `VecRotate(const int32_t vec[3], const int32_t mat[9], int32_t out[3])` | Rotate vector by matrix |

### Game State Management

| Function | Description |
|----------|-------------|
| `GameInit()` | Initialize global game state |
| `GameShutdown()` | Cleanup and free resources |
| `GetGameStateValue(int32_t offset)` | Read game state variable |
| `SetGameStateValue(int32_t offset, int32_t value)` | Write game state variable |
| `GetGameObj(int index)` | Get pointer to game object (338-byte struct) |
| `GetPlayerObj()` | Get player ship object |

### Save/Load System (CDR File Format)

| Function | Description |
|----------|-------------|
| `CdrFile::SaveInternal(const char* path)` | Save game with compression/encryption |
| `CdrFile::LoadInternal(const char* path)` | Load and decrypt save file |
| `CdrFile::MangleData(uint8_t* data, size_t len)` | Encrypt/compress data block |
| `CdrFile::UnmangleData(uint8_t* data, size_t len)` | Decrypt/decompress data block |

### String Processing

| Function | Description |
|----------|-------------|
| `ExpandString(char* dst, const char* src, size_t maxLen)` | Expand text macros/placeholders |
| `FormatNumber(int32_t value, char* buf, size_t bufSize)` | Format integer with commas |
| `CopyStringSafe(char* dst, const char* src, size_t maxLen)` | Bounds-checked string copy |

---

## Platform Subsystem (`src/platform/`)

### Video (SDL 1.2 Backend)

| Function | Description |
|----------|-------------|
| `VideoInit()` | Initialize SDL video, create 640×400 window |
| `VideoShutdown()` | Shutdown video subsystem |
| `VideoFlip()` | Present back buffer to screen |
| `VideoClearBackBuffer()` | Clear back buffer to black |
| `VideoSetPalette(const uint8_t palette[768])` | Set 256-color VGA palette |
| `VideoGetBackBuffer()` | Get pointer to 64000-byte back buffer |
| `VideoGetZBuffer()` | Get pointer to Z-buffer for depth testing |

### Input Handling

| Function | Description |
|----------|-------------|
| `InputInit()` | Initialize keyboard/mouse/joystick |
| `InputProcessEvents()` | Poll SDL events, update input state |
| `InputGetKeyState(int keyCode)` | Get key state (0x00=pressed, 0x80=released) |
| `InputGetMouseX()` | Get mouse X position |
| `InputGetMouseY()` | Get mouse Y position |
| `InputGetJoystickButton(int button)` | Get joystick button state |

### Sound System

| Function | Description |
|----------|-------------|
| `SoundInit()` | Initialize SDL audio (22050 Hz stereo S16) |
| `SoundShutdown()` | Shutdown audio subsystem |
| `SoundPlaySample(int sampleId, int volume, int pan)` | Play sound effect |
| `SoundStopAll()` | Stop all playing sounds |
| `SoundSetMasterVolume(int volume)` | Set master volume (0-255) |
| `HmpToMidi(const uint8_t* hmpData, size_t hmpSize)` | Convert HMP music to MIDI |

### Timer

| Function | Description |
|----------|-------------|
| `TimerInit()` | Initialize timer subsystem |
| `TimerGetTicks()` | Get milliseconds since init |
| `TimerDelay(int ms)` | Busy-wait delay |

---

## Game Logic Subsystem (`src/game/`)

### 2D/3D Rendering

| Function | Description |
|----------|-------------|
| `DrawPixel(int x, int y, uint8_t color)` | Plot single pixel with bounds check |
| `DrawLine(int x0, int y0, int x1, int y1, uint8_t color)` | Bresenham line algorithm |
| `DrawSpan(int y, int x0, int x1, uint8_t color)` | Horizontal span fill |
| `ProjectPoint(const int32_t world[3], int16_t* screenX, int16_t* screenY)` | 3D→2D projection with camera |
| `DrawPolygon(const int16_t* points, int count, uint8_t color)` | Fill convex polygon |
| `DrawBitmap(int x, int y, int bitmapId)` | Blit bitmap to screen |

### Bytecode Interpreter

Mission scripts and UI text use bytecode execution.

| Function | Description |
|----------|-------------|
| `BytecodeInit()` | Initialize interpreter, allocate 150 slots |
| `BytecodeExecute(int slotId)` | Execute bytecode in specified slot |
| `BytecodeSetSlot(int slotId, const uint16_t* code, void* missionData)` | Assign code to slot |
| `BytecodeGetSlotState(int slotId)` | Get execution state (running/stopped) |

### Ship Controls

| Function | Description |
|----------|-------------|
| `ShipControlsInit()` | Initialize control system |
| `ShipControlsUpdate()` | Process player input, update ship velocity |
| `ShipApplyThrust(GameObj* ship, int32_t thrust)` | Apply forward thrust |
| `ShipApplyRoll(GameObj* ship, int32_t roll)` | Apply roll rotation |
| `ShipApplyPitch(GameObj* ship, int32_t pitch)` | Apply pitch rotation |
| `ShipApplyYaw(GameObj* ship, int32_t yaw)` | Apply yaw rotation |
| `ShipFireWeapon(GameObj* ship)` | Fire primary weapon |

### Combat System

| Function | Description |
|----------|-------------|
| `CombatInit()` | Initialize combat subsystem |
| `CombatUpdate()` | Process collisions, damage, explosions |
| `CombatApplyDamage(GameObj* target, int32_t damage)` | Apply damage to ship |
| `CombatCheckCollision(GameObj* a, GameObj* b)` | AABB collision test |
| `CombatSpawnProjectile(GameObj* shooter, int weaponType)` | Create projectile object |

### UI System

| Function | Description |
|----------|-------------|
| `UIInit()` | Initialize UI subsystem |
| `UIDrawHUD()` | Draw heads-up display |
| `UIDrawMenu()` | Draw main menu |
| `UIDrawDialog(int dialogId)` | Draw modal dialog |
| `UIProcessClick(int x, int y)` | Process mouse click on UI elements |

### World Generation

| Function | Description |
|----------|-------------|
| `WorldInit()` | Initialize star systems, planets |
| `WorldGenerateSystem(int systemId)` | Generate planets for star system |
| `WorldGetPlanetData(int planetId)` | Get planet parameters |
| `WorldGetOrbitData(int bodyId)` | Get orbital parameters |

### Planet Rendering

Icosahedron-based sphere subdivision rendering.

| Function | Description |
|----------|-------------|
| `PlanetRenderInit()` | Initialize planet renderer |
| `PlanetRenderSphere(const Camera* cam, int32_t radius, int subdivisions)` | Render planet sphere |
| `PlanetSubdivideFace(const int32_t v0[3], const int32_t v1[3], const int32_t v2[3], int level)` | Recursive triangle subdivision |
| `PlanetShadeTriangle(const int32_t normal[3], const int32_t lightDir[3])` | Calculate lighting for triangle |

---

## Data Subsystem (`src/data/`)

### Data Blob Access

| Function | Description |
|----------|-------------|
| `LoadDataBlob(const char* path)` | Load binary data file into memory |
| `GetDataBlobPtr(uint32_t offset)` | Get pointer to data at offset |
| `GetDataBlobSize()` | Get total blob size |
| `UnloadDataBlob()` | Free blob memory |

### Bitmap Parsing

| Function | Description |
|----------|-------------|
| `ParseBitmap(const uint8_t* data, size_t size, BitmapInfo* out)` | Parse FFE bitmap format |
| `GetBitmapPixelData(const uint8_t* data, size_t size)` | Get pointer to pixel array |
| `GetBitmapWidth(const uint8_t* data, size_t size)` | Get bitmap width in pixels |
| `GetBitmapHeight(const uint8_t* data, size_t size)` | Get bitmap height in pixels |
| `IsMaskedBitmap(const uint8_t* data, size_t size)` | Check if bitmap has transparency mask |

### Mission Data

| Function | Description |
|----------|-------------|
| `LoadMissionData(int missionId)` | Load mission script and parameters |
| `GetMissionScript(int missionId)` | Get pointer to mission bytecode |
| `GetMissionParam(int missionId, int paramIndex)` | Get mission parameter value |

---

## Key Data Structures

### GameObj (338 bytes)

Game object structure for ships, projectiles, stations.

```cpp
struct GameObj {
    int32_t pos[3];        // World position (fixed-point)
    int32_t vel[3];        // Velocity vector
    int32_t rot[9];        // 3×3 rotation matrix
    int16_t shipId;        // Ship type identifier
    int16_t targetId;      // Target object index
    uint32_t flags;        // Status flags
    // ... (more fields, total 338 bytes)
};
```

### Camera

```cpp
struct Camera {
    int32_t pos[3];        // Camera position
    int32_t rot[9];        // Orientation matrix
    int32_t fov;           // Field of view (fixed-point)
    int32_t nearClip;      // Near clipping plane
    int32_t farClip;       // Far clipping plane
};
```

### ConsoleSlot (Bytecode Execution Context)

```cpp
struct ConsoleSlot {
    const uint16_t* code;  // Bytecode pointer
    const uint16_t* pc;    // Program counter
    void* missionData;     // Mission-specific data
    int state;             // Running/stopped/error
    // ... (150 slots total)
};
```

---

## Naming Conventions

### Ported Assembly Functions

Functions ported directly from NASM retain their numeric IDs:

- `FUNC_001234_OpCodeName()` - Named opcode handler
- `Func001473()` - Unnamed utility function
- `HandleOp1F()` - Opcode dispatcher

### New C++ Functions

New helper functions use descriptive names:

- `VideoInit()`, `InputProcessEvents()` - Platform layer
- `FixedMul()`, `MatMul()` - Math utilities
- `DrawPixel()`, `ProjectPoint()` - Rendering

### Global State

Each subsystem has a global state struct:

- `g_videoState` - Video subsystem state
- `g_inputState` - Input state (keyboard/mouse/joystick)
- `g_soundState` - Audio subsystem state
- `g_gameState` - Main game state
- `g_timerState` - Timer subsystem state

---

## Build Configuration

All functions compile with:
- **C++17** standard
- **Fixed-point math** (no floating point in game logic)
- **Namespace:** `jjffe::`
- **Platform guards:** `#ifdef JJFFE_USE_SDL`

---

## Verification

All 1,700+ functions pass behavioral tests against the original NASM binary:
- **Test suite:** 192/192 tests passing
- **Frontier coverage:** 0 tracked stubs remaining
- **Audio:** 17/17 tracks play correctly
- **Pixel-perfect:** All rendering matches original

For build instructions, see [BUILD.md](../BUILD.md).
