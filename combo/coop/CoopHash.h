#pragma once
// ComboShip co-op (full-coop-sync plan, Phase 0/1): stable 64-bit hashing shared by the launcher and
// both game DLLs. Header-only, no game includes, so `coop_tests` can link it without a game DLL.
//
// Canonical JSON: nlohmann::json stores objects in a std::map, so dump() emits keys sorted and the
// text is a pure function of the value. That makes FNV-1a over dump() a stable identity across
// machines and compilers (numbers are integers or round-trippable doubles in every hashed document).

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Coop {

constexpr uint64_t kFnvOffset64 = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime64 = 1099511628211ULL;

inline uint64_t Fnv1a64(std::string_view data, uint64_t h = kFnvOffset64) {
    for (unsigned char c : data) {
        h ^= c;
        h *= kFnvPrime64;
    }
    return h;
}

inline uint64_t HashJson(const nlohmann::json& j) {
    return Fnv1a64(j.dump());
}

// splitmix64: cheap, well-mixed derivation step (also the CoopRng derivation, Phase 2).
inline uint64_t SplitMix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

inline uint64_t HashCombine(uint64_t a, uint64_t b) {
    return SplitMix64(a ^ (b + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2)));
}

// u64 travels as a fixed 16-digit lowercase hex string: JSON numbers above 2^53 are not safe through
// every relay/parser (the Anchor server is Go + gjson; old clients may parse to double).
inline std::string HexU64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf, 16);
}

inline uint64_t ParseHexU64(const std::string& s) {
    uint64_t v = 0;
    if (s.empty() || s.size() > 16)
        return 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<uint64_t>(c - 'A' + 10);
        else
            return 0;
    }
    return v;
}

// Reads a u64 written by HexU64; tolerates a plain JSON number (hand-edited / older writers).
inline uint64_t JsonU64(const nlohmann::json& obj, const char* key) {
    if (!obj.is_object())
        return 0;
    auto it = obj.find(key);
    if (it == obj.end())
        return 0;
    if (it->is_string())
        return ParseHexU64(it->get<std::string>());
    if (it->is_number_unsigned())
        return it->get<uint64_t>();
    if (it->is_number_integer())
        return static_cast<uint64_t>(it->get<int64_t>());
    return 0;
}

} // namespace Coop
