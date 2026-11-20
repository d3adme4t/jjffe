#include "jjffe/frame_pose.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

extern char** environ;

namespace jjffe {

namespace {

void WriteInts(FILE* f, const char* key, const int32_t* v, int n)
{
    fprintf(f, "%s=", key);
    for (int i = 0; i < n; ++i)
        fprintf(f, i ? ",%d" : "%d", v[i]);
    fputc('\n', f);
}

struct Reader {
    std::map<std::string, std::string> kv;
    std::set<std::string> used;
    std::string err;

    const std::string* Get(const std::string& key)
    {
        auto it = kv.find(key);
        if (it == kv.end()) {
            if (err.empty()) err = "missing key '" + key + "'";
            return nullptr;
        }
        used.insert(key);
        return &it->second;
    }

    // Comma list of exactly n integers (decimal or 0x hex, int32 or uint32).
    bool Ints(const std::string& key, int32_t* out, int n)
    {
        const std::string* s = Get(key);
        if (!s) return false;
        const char* p = s->c_str();
        for (int i = 0; i < n; ++i) {
            char* end = nullptr;
            errno = 0;
            const long long v = strtoll(p, &end, 0);
            if (end == p || errno != 0 || v < INT32_MIN || v > static_cast<long long>(UINT32_MAX)) {
                if (err.empty()) err = "bad integer in '" + key + "=" + *s + "'";
                return false;
            }
            out[i] = static_cast<int32_t>(static_cast<uint32_t>(v));
            p = end;
            if (i + 1 < n) {
                if (*p != ',') {
                    if (err.empty()) err = "'" + key + "' needs " + std::to_string(n) + " values";
                    return false;
                }
                ++p;
            }
        }
        if (*p != '\0') {
            if (err.empty()) err = "'" + key + "' has more than " + std::to_string(n) + " values";
            return false;
        }
        return true;
    }

    bool Int(const std::string& key, int32_t* out) { return Ints(key, out, 1); }

    bool Bool(const std::string& key, bool* out)
    {
        int32_t v = 0;
        if (!Int(key, &v)) return false;
        if (v != 0 && v != 1) {
            if (err.empty()) err = "'" + key + "' must be 0 or 1";
            return false;
        }
        *out = (v == 1);
        return true;
    }

    bool Hex(const std::string& key, uint8_t* out, int n)
    {
        const std::string* s = Get(key);
        if (!s) return false;
        if (static_cast<int>(s->size()) != 2 * n) {
            if (err.empty()) err = "'" + key + "' needs " + std::to_string(2 * n) + " hex digits";
            return false;
        }
        for (int i = 0; i < n; ++i) {
            char byte[3] = {(*s)[2 * i], (*s)[2 * i + 1], 0};
            char* end = nullptr;
            const long v = strtol(byte, &end, 16);
            if (end != byte + 2) {
                if (err.empty()) err = "bad hex in '" + key + "'";
                return false;
            }
            out[i] = static_cast<uint8_t>(v);
        }
        return true;
    }
};

constexpr int kDpalRgbLines = 8;
constexpr int kDpalRgbLineBytes = kFramePoseDpalRgbBytes / kDpalRgbLines;  // 16 slots

} // namespace

bool FramePoseWrite(const char* path, const FramePose& p)
{
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# JJFFE frame pose sidecar (task 4.13). "
               "Replay: tools/replay_capture.sh <frame>.raw\n");
    fprintf(f, "version=%d\n", p.version);
    fprintf(f, "source=%s\n", p.source.c_str());
    fprintf(f, "frame=%d\n", p.frame);
    fprintf(f, "live_frame=%d\n", p.liveFrame);
    fprintf(f, "is_docked=%d\n", p.isDocked ? 1 : 0);
    fprintf(f, "orbital_view=%d\n", p.orbitalView ? 1 : 0);
    fprintf(f, "look_pitch_fp16=%d\n", p.lookPitchFp16);
    WriteInts(f, "ship.pos", p.shipPos, 3);
    WriteInts(f, "ship.vel", p.shipVel, 3);
    WriteInts(f, "ship.rot", p.shipRot, 9);
    fprintf(f, "ship.fuel=%d\n", p.shipFuel);
    fprintf(f, "ship.shields=%d\n", p.shipShields);
    fprintf(f, "ship.hull=%d\n", p.shipHull);
    fprintf(f, "ship.shield_max=%d\n", p.shipShieldMax);
    fprintf(f, "ship.hull_max=%d\n", p.shipHullMax);
    fprintf(f, "ship.target=0x%02x\n", static_cast<unsigned>(p.shipTarget) & 0xffu);
    WriteInts(f, "camera.pos", p.camPos, 3);
    WriteInts(f, "camera.rot", p.camRot, 9);

    fprintf(f, "npc.count=%d\n", static_cast<int>(p.npcs.size()));
    for (size_t i = 0; i < p.npcs.size(); ++i) {
        const FramePoseObj& o = p.npcs[i];
        char key[48];
        fprintf(f, "npc.%zu.state=%d,0x%02x,%d\n", i, o.shipId,
                static_cast<unsigned>(o.flags) & 0xffu, o.hull);
        snprintf(key, sizeof(key), "npc.%zu.pos", i);
        WriteInts(f, key, o.pos, 3);
        snprintf(key, sizeof(key), "npc.%zu.vel", i);
        WriteInts(f, key, o.vel, 3);
        snprintf(key, sizeof(key), "npc.%zu.rot", i);
        WriteInts(f, key, o.rot, 9);
    }

    fprintf(f, "stars.count=%d\n", static_cast<int>(p.stars.size()));
    for (size_t i = 0; i < p.stars.size(); ++i) {
        const FramePoseStar& s = p.stars[i];
        fprintf(f, "star.%02zu=%d,%d,%d,%d\n", i, s.x, s.y, s.brightness, s.speed);
    }

    fprintf(f, "dpal.base=0x%02x\n", static_cast<unsigned>(p.dpalBase));
    for (int l = 0; l < kDpalRgbLines; ++l) {
        fprintf(f, "dpal.rgb.%d=", l);
        for (int i = 0; i < kDpalRgbLineBytes; ++i)
            fprintf(f, "%02x", p.dpalRgb[l * kDpalRgbLineBytes + i]);
        fputc('\n', f);
    }
    if (p.hasSkyPre) WriteInts(f, "sky.pre", p.skyPre, 3);
    if (p.hasSkyLast) WriteInts(f, "sky.last", &p.skyLast, 1);

    for (const auto& e : p.env)
        fprintf(f, "env.%s=%s\n", e.first.c_str(), e.second.c_str());
    for (const auto& c : p.checks)
        fprintf(f, "check.%s=%s\n", c.first.c_str(), c.second.c_str());

    const bool ok = (fflush(f) == 0) && !ferror(f);
    return (fclose(f) == 0) && ok;
}

bool FramePoseRead(const char* path, FramePose* out, std::string* err)
{
    FILE* f = fopen(path, "r");
    if (!f) {
        if (err) *err = std::string("cannot open ") + path;
        return false;
    }
    Reader r;
    char line[4096];
    int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lineNo;
        size_t n = strlen(line);
        if (n + 1 == sizeof(line) && line[n - 1] != '\n') {
            r.err = "line " + std::to_string(lineNo) + " too long";
            break;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0 || line[0] == '#') continue;
        const char* eq = strchr(line, '=');
        if (!eq || eq == line) {
            r.err = "line " + std::to_string(lineNo) + " is not key=value";
            break;
        }
        const std::string key(line, static_cast<size_t>(eq - line));
        if (!r.kv.emplace(key, std::string(eq + 1)).second) {
            r.err = "duplicate key '" + key + "'";
            break;
        }
    }
    fclose(f);

    FramePose p;
    if (r.err.empty()) {
        int32_t version = 0;
        if (r.Int("version", &version) && version != kFramePoseVersion)
            r.err = "version " + std::to_string(version) + " (this build reads " +
                    std::to_string(kFramePoseVersion) + ")";
        p.version = version;
    }
    if (r.err.empty()) {
        if (const std::string* s = r.Get("source")) p.source = *s;
        r.Int("frame", &p.frame);
        r.Int("live_frame", &p.liveFrame);
        r.Bool("is_docked", &p.isDocked);
        r.Bool("orbital_view", &p.orbitalView);
        r.Int("look_pitch_fp16", &p.lookPitchFp16);
        r.Ints("ship.pos", p.shipPos, 3);
        r.Ints("ship.vel", p.shipVel, 3);
        r.Ints("ship.rot", p.shipRot, 9);
        r.Int("ship.fuel", &p.shipFuel);
        r.Int("ship.shields", &p.shipShields);
        r.Int("ship.hull", &p.shipHull);
        r.Int("ship.shield_max", &p.shipShieldMax);
        r.Int("ship.hull_max", &p.shipHullMax);
        r.Int("ship.target", &p.shipTarget);
        r.Ints("camera.pos", p.camPos, 3);
        r.Ints("camera.rot", p.camRot, 9);
        if (r.err.empty() && p.frame < 1) r.err = "frame must be >= 1";
    }
    if (r.err.empty()) {
        int32_t count = 0;
        if (r.Int("npc.count", &count) && (count < 0 || count > 256))
            r.err = "npc.count out of range";
        for (int32_t i = 0; r.err.empty() && i < count; ++i) {
            FramePoseObj o;
            const std::string k = "npc." + std::to_string(i);
            int32_t st[3] = {};
            if (r.Ints(k + ".state", st, 3)) {
                o.shipId = st[0];
                o.flags = st[1];
                o.hull = st[2];
            }
            r.Ints(k + ".pos", o.pos, 3);
            r.Ints(k + ".vel", o.vel, 3);
            r.Ints(k + ".rot", o.rot, 9);
            p.npcs.push_back(o);
        }
    }
    if (r.err.empty()) {
        int32_t count = 0;
        if (r.Int("stars.count", &count) && count != kFramePoseStarCount)
            r.err = "stars.count must be " + std::to_string(kFramePoseStarCount);
        for (int32_t i = 0; r.err.empty() && i < count; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "star.%02d", i);
            int32_t v[4] = {};
            if (r.Ints(key, v, 4)) p.stars.push_back({v[0], v[1], v[2], v[3]});
        }
    }
    if (r.err.empty()) {
        if (r.Int("dpal.base", &p.dpalBase) && p.dpalBase != 0 && p.dpalBase != 0x40)
            r.err = "dpal.base must be 0x00 or 0x40";
        for (int l = 0; r.err.empty() && l < kDpalRgbLines; ++l)
            r.Hex("dpal.rgb." + std::to_string(l), p.dpalRgb + l * kDpalRgbLineBytes,
                  kDpalRgbLineBytes);
        if (r.err.empty() && r.kv.count("sky.pre")) {
            p.hasSkyPre = r.Ints("sky.pre", p.skyPre, 3);
            for (int i = 0; r.err.empty() && i < 3; ++i)
                if (p.skyPre[i] < 0 || p.skyPre[i] > 0xff) r.err = "sky.pre values must be 0..255";
        }
        if (r.err.empty() && r.kv.count("sky.last")) {
            p.hasSkyLast = r.Ints("sky.last", &p.skyLast, 1);
            if (r.err.empty() && p.skyLast != 0 && p.skyLast != 1)
                r.err = "sky.last must be 0 or 1";
        }
    }
    if (r.err.empty()) {
        for (const auto& e : r.kv) {
            if (e.first.compare(0, 4, "env.") == 0) {
                p.env.emplace_back(e.first.substr(4), e.second);
                r.used.insert(e.first);
            } else if (e.first.compare(0, 6, "check.") == 0) {
                p.checks.emplace_back(e.first.substr(6), e.second);
                r.used.insert(e.first);
            } else if (!r.used.count(e.first)) {
                r.err = "unknown key '" + e.first + "'";
                break;
            }
        }
    }
    if (!r.err.empty()) {
        if (err) *err = std::string(path) + ": " + r.err;
        return false;
    }
    *out = p;
    return true;
}

bool FramePoseEnvRelevant(const char* name)
{
    if (!name) return false;
    const bool jj = strncmp(name, "JJFFE_", 6) == 0;
    const bool ab = strncmp(name, "AB_", 3) == 0;
    if (!jj && !ab) return false;
    static const char* const kSkip[] = {"JJFFE_DUMP_", "JJFFE_REPLAY_", "JJFFE_ORBITAL_",
                                        "JJFFE_DEBUG_", "JJFFE_INTRO_KEYS"};
    for (const char* s : kSkip)
        if (strncmp(name, s, strlen(s)) == 0) return false;
    const size_t n = strlen(name);
    if (n >= 6 && strcmp(name + n - 6, "_TRACE") == 0) return false;
    return true;
}

std::vector<std::pair<std::string, std::string>> FramePoseCurrentEnv()
{
    std::vector<std::pair<std::string, std::string>> out;
    for (char** e = environ; e && *e; ++e) {
        const char* eq = strchr(*e, '=');
        if (!eq) continue;
        const std::string name(*e, static_cast<size_t>(eq - *e));
        if (FramePoseEnvRelevant(name.c_str())) out.emplace_back(name, std::string(eq + 1));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace jjffe
