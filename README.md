# JJFFE — Frontier: First Encounters (ASM → C++17)

Faithful behavioral reimplementation of the 1996 DOS game **Frontier: First Encounters**
(JJ-patched build) as portable C++17 with an SDL 1.2 backend on Linux.

## Quick start

See **[`BUILD.md`](BUILD.md)** for prerequisites, configure/build commands, and the full
CMake options table.

```bash
# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Deploy to your FFE game directory
cp build/ffelnxsdl /path/to/FFE/
cp tools/extract_data.py /path/to/FFE/

# Extract game data (REQUIRED before first run)
cd /path/to/FFE/
python3 extract_data.py /path/to/nasm-files/

# Run the game
./ffelnxsdl
```

**Note:** This release build does not include tests. You must own a legitimate copy of Frontier: First Encounters and extract the game data using `extract_data.py` before running.

## Docs

| Doc | Role |
|---|---|
| [`BUILD.md`](BUILD.md) | Build instructions + CMake options |
| [`data_blob/README.md`](data_blob/README.md) | Game data extraction instructions |

## Game Data Requirement

**Important:** This project requires game data from the original Frontier: First Encounters (FFE). You must own a legitimate copy of the game.

The `data_blob/` directory contains a README with instructions on how to extract data from your original game files using the provided `tools/extract_data.py` script.

See [data_blob/README.md](data_blob/README.md) for details.

## API Reference

See [`docs/FUNCTION_REFERENCE.md`](docs/FUNCTION_REFERENCE.md) for complete documentation of all 1,700+ ported C++ functions organized by subsystem.
