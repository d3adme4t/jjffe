#include "case_path.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>
#ifndef R_OK
#define R_OK 4
#endif
#define jj_access _access
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define jj_access access
#endif

namespace {

bool IEquals(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

#ifndef _WIN32
std::string JoinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    if (dir == "/") return dir + name;
    return dir + "/" + name;
}

// Case-insensitive match of one path component inside `parent` ("" = cwd).
// The exact-case miss already happened (caller stats first); pick the
// lexicographically first casefold-equal entry so resolution is
// deterministic even when both QQESCAPE.HMP and qqescape.hmp exist.
bool ResolveComponent(const std::string& parent, const std::string& comp,
                      std::string& found)
{
    DIR* d = opendir(parent.empty() ? "." : parent.c_str());
    if (!d) return false;
    std::string best;
    while (const dirent* e = readdir(d)) {
        if (IEquals(e->d_name, comp.c_str())) {
            std::string name = e->d_name;
            if (best.empty() || name < best) best = name;
        }
    }
    closedir(d);
    if (best.empty()) return false;
    found = best;
    return true;
}
#endif

}  // namespace

bool ResolveCasePath(const char* path, char* out, size_t outSz)
{
    if (!path || !out || outSz == 0) return false;
    snprintf(out, outSz, "%s", path);
    if (jj_access(path, R_OK) == 0) return true;  // exact hit: the common case

#ifdef _WIN32
    return false;  // case-insensitive FS: exact access() is authoritative
#else
    std::string cur;
    const char* p = path;
    if (*p == '/') {
        cur = "/";
        ++p;
    }
    while (*p == '/') ++p;
    while (*p) {
        const char* slash = strchr(p, '/');
        const std::string comp(p, slash ? static_cast<size_t>(slash - p)
                                       : strlen(p));
        p = slash ? slash + 1 : p + strlen(p);
        if (comp.empty()) continue;
        struct stat st;
        const std::string exact = JoinPath(cur, comp);
        if (stat(exact.c_str(), &st) == 0) {
            cur = exact;
        } else {
            std::string hit;
            if (!ResolveComponent(cur, comp, hit)) return false;
            cur = JoinPath(cur, hit);
        }
    }
    if (cur.empty() || jj_access(cur.c_str(), R_OK) != 0) return false;
    snprintf(out, outSz, "%s", cur.c_str());
    return true;
#endif
}
