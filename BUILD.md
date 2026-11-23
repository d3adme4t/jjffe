# Building JJFFE (C++ port of Frontier: First Encounters)

This document covers configure/build options for the Linux SDL port (`ffelnxsdl`).

## Prerequisites

| Package | Notes |
|---|---|
| CMake **3.24+** | Generators: Ninja recommended (`ninja`), Make also works |
| GCC or Clang with **C++17** / **C11** | |
| **SDL 1.2** (`libsdl1.2-dev`) | Required when `JJFFE_USE_SDL=ON` (default) |
| **pkg-config** | Locates SDL / optional SDL_mixer |
| OpenGL (`libGL`) | Linked by the SDL executable |
| **SDL_mixer 1.2** (optional) | Music path; auto-detected via `pkg-config SDL_mixer` |
| Python 3 | Tooling (`tools/`), data extraction, fleet/A/B scripts |
| Game data blob | `data_blob/data_blob.bin` (and typically `FFE_DIR` for live/music gates) |

## Configure and build

```bash
# Debug (gate of record — keep build/ as Debug)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

## Deploy to game directory

After building, copy the executable and extraction tool to your FFE game directory:

```bash
# Copy executable to your FFE game directory
cp build/ffelnxsdl /path/to/FFE/

# Copy extraction tool
cp tools/extract_data.py /path/to/FFE/
```

### Generate game data (required before first run)

You must extract the game data from your original FFE installation **before** running the executable:

```bash
cd /path/to/FFE/
**Note:** The script expects a directory containing `.asm` files (not individual file arguments). It processes all DATA_ labels found in the directory and generates data_blob.bin, data_blob.h, and data_offset_map.txt in the output directory (default: data_blob/).

```bash
python3 extract_data.py /path/to/nasm-directory [output-dir]
```

# This generates:
# This generates:
# - data_blob/data_blob.bin (game asset data)
# - data_blob/data_blob.h (C++ header with offsets)
# - data_blob/data_offset_map.txt (human-readable map)
```

**Important:** You must own a legitimate copy of Frontier: First Encounters. The assembly files (`ffe.asm`, `ffedat.asm`) are from the JJ-patched version of the game.

### Run the game

```bash
cd /path/to/FFE/
./ffelnxsdl
```

The executable expects to find `data_blob/data_blob.bin` in the current working directory or a parent directory.

**Note:** This release build does not include tests. The full test suite is available in the development repository.

## Build options

| CMake option | Default | Effect |
|---|---|---|
| `JJFFE_USE_SDL` | ON | Build `ffelnxsdl` with the SDL 1.2 backend |
| `JJFFE_USE_OGRE` | OFF | Build `ffelnxogre` (Ogre/Vulkan alt-backend) - **disabled in release** |
| `JJFFE_USE_D3D9` | OFF | Build `ffelnxd3d` (D3D9/DXVK) - **disabled in release** |
| SDL_mixer | auto | If `pkg-config` finds `SDL_mixer`, `JJFFE_USE_SDL_MIXER` is defined on the SDL exe (music). Not a CMake `option()` — detection-only. |

**Note:** D3D9 and Ogre backends are disabled by default in this release as they are experimental. Only the SDL backend (`ffelnxsdl`) is built.


## Game Data Requirement

**Important:** You must own a legitimate copy of Frontier: First Encounters.

The `data_blob/data_blob.h` file (offset constants) is included for compilation, but you must generate `data_blob/data_blob.bin` from your original game:

```bash
python3 tools/extract_data.py <nasm-directory> [output-dir]
```

See [data_blob/README.md](data_blob/README.md) for complete instructions.
