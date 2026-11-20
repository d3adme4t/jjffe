#pragma once
// ghidra_shims.h — standard library bindings for Ghidra-emitted callee names
//

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cctype>

namespace jjffe {

// n == 0 with a null source/destination is UB for ::memcpy (the libc signature is
// `nonnull`), and the ASM's rep movs of zero bytes is a no-op. Keep it a no-op.
inline void*  memcpy(void* d, const void* s, size_t n)  { return n ? ::memcpy(d, s, n) : d; }
inline void*  memset(void* s, int c, size_t n)          { return ::memset(s, c, n); }
inline size_t strlen(const char* s)                      { return ::strlen(s); }
inline char*  strcpy(char* d, const char* s)             { return ::strcpy(d, s); }
inline int    strcmp(const char* a, const char* b)       { return ::strcmp(a, b); }
inline char*  strcat(char* d, const char* s)             { return ::strcat(d, s); }
inline int    c_abs(int n)                               { return ::abs(n); }
inline int    toupper(int c)                              { return ::toupper(c); }
inline int    sprintf(char* buf, const char* fmt, ...)   {
    va_list args; va_start(args, fmt);
    int r = ::vsprintf(buf, fmt, args);
    va_end(args); return r;
}

// BlitClipWrapper — external C helper from ffemisc.c
void BlitClipWrapper(void* src, int32_t srcX, int32_t srcY, int32_t w, int32_t h,
                      void* dst, void (*blitFn)(void))
{
    // stub — reimplement in ffemisc.c
}

} // namespace jjffe
