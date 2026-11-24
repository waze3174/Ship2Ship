#pragma once
// ComboShip co-op: RFC 4648 base64 for binary blobs inside JSON packets (seed chunks, actor snapshots).
// Header-only; Decode rejects malformed input instead of guessing.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Coop {

inline std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | uint32_t(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }
    if (i < len) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len)
            v |= uint32_t(data[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(i + 1 < len ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

inline std::string Base64Encode(std::string_view s) {
    return Base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

inline bool Base64Decode(std::string_view in, std::vector<uint8_t>& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    out.clear();
    if (in.size() % 4 != 0)
        return false;
    out.reserve(in.size() / 4 * 3);
    for (size_t i = 0; i < in.size(); i += 4) {
        int a = val(in[i]), b = val(in[i + 1]);
        if (a < 0 || b < 0)
            return false;
        bool pad2 = in[i + 2] == '=', pad3 = in[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != in.size()))
            return false;
        int c = pad2 ? 0 : val(in[i + 2]);
        int d = pad3 ? 0 : val(in[i + 3]);
        if (c < 0 || d < 0)
            return false;
        uint32_t v = (uint32_t(a) << 18) | (uint32_t(b) << 12) | (uint32_t(c) << 6) | uint32_t(d);
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        if (!pad2)
            out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        if (!pad3)
            out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    return true;
}

} // namespace Coop
