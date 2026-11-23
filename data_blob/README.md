# Game Data Blob

This directory contains the game data extracted from the original Frontier: First Encounters (FFE) game files.

## Important Notice

**You must own a legitimate copy of Frontier: First Encounters to use this software.**

The `data_blob.bin` file and associated headers are derived from the original game data and are **not included** in this repository for copyright reasons.

## How to Generate Your Own Data Blob

### Prerequisites
- Original FFE game installation (JJ-patched build recommended)
- Python 3
- NASM assembly files (`ffe.asm`, `ffebmp.asm`, `ffedat.asm`)

### Steps

1. **Extract data from NASM assembly:**

**Note:** Provide a directory path containing .asm files, not individual file paths.
   ```bash
   python3 tools/extract_data.py <nasm-directory> [output-dir]
   ```

2. **This will generate:**
   - `data_blob/data_blob.bin` - Binary data file
   - `data_blob/data_blob.h` - C++ header with offsets
   - `data_blob/data_offset_map.txt` - Human-readable offset map

3. **Build the project:**
   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j$(nproc)
   ```

## Alternative: Use Original Game Directory

If you have the original FFE game installed, you can point the executable to your game directory at runtime using environment variables or configuration files.

## Legal

This project is a clean-room reimplementation of the game logic only. All game assets (bitmaps, sounds, music, mission data) remain the property of the original copyright holders. You must provide your own legally obtained game data.
