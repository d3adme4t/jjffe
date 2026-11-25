// Case-insensitive game-path resolution for case-sensitive filesystems.
//
// The original DOS game was case-insensitive: any casing of music/<name>.hmp
// or fx/<name>.raw resolved. The port's loaders probe lowercase paths, which
// misses original uppercase 8.3 installs (MUSIC/QQESCAPE.HMP, FX/AIROPEN.RAW).
// ResolveCasePath restores DOS semantics: an exact readable path wins (the
// common case, also authoritative on case-insensitive filesystems); otherwise
// each path component is matched case-insensitively against the real
// directory listing. Deterministic on ambiguity: the lexicographically first
// casefold-equal entry after the exact-case miss.
#pragma once
#include <stddef.h>

// Returns true if `path` exists and is readable. `out` always receives the
// true on-disk path when found, or a copy of `path` when not — safe to print
// in error messages either way. Never shells out or downloads.
bool ResolveCasePath(const char* path, char* out, size_t outSz);
