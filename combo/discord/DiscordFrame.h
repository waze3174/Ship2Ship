// combo/discord/DiscordFrame.h
//
// ComboShip: Discord's local IPC framing. A frame is a u32 opcode and a u32 payload length, both little-endian, then
// the payload (JSON). https://docs.discord.com/developers/topics/rpc. Pure and header-only.
// See docs/deviations/discord.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ComboDiscord {

enum Opcode : uint32_t {
    kOpHandshake = 0,
    kOpFrame = 1,
    kOpClose = 2,
    kOpPing = 3,
    kOpPong = 4,
};

inline constexpr size_t kFrameHeaderBytes = 8;
inline constexpr uint32_t kMaxFramePayloadBytes = 64 * 1024; // far above anything Discord sends; bounds a bad length
inline constexpr unsigned kBitsPerByte = 8;
inline constexpr size_t kU32Bytes = 4;

struct FrameHeader {
    uint32_t opcode = 0;
    uint32_t length = 0;
};

inline void AppendU32LittleEndian(std::string& out, uint32_t value) {
    for (size_t i = 0; i < kU32Bytes; ++i)
        out.push_back(static_cast<char>((value >> (i * kBitsPerByte)) & 0xFF));
}

inline uint32_t ReadU32LittleEndian(const unsigned char* bytes) {
    uint32_t value = 0;
    for (size_t i = 0; i < kU32Bytes; ++i)
        value |= static_cast<uint32_t>(bytes[i]) << (i * kBitsPerByte);
    return value;
}

inline std::string EncodeFrame(uint32_t opcode, const std::string& payload) {
    std::string frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    AppendU32LittleEndian(frame, opcode);
    AppendU32LittleEndian(frame, static_cast<uint32_t>(payload.size()));
    frame += payload;
    return frame;
}

// False when the length is beyond kMaxFramePayloadBytes: the stream is not Discord's, or it is out of step.
inline bool DecodeFrameHeader(const std::array<unsigned char, kFrameHeaderBytes>& bytes, FrameHeader* out) {
    out->opcode = ReadU32LittleEndian(bytes.data());
    out->length = ReadU32LittleEndian(bytes.data() + kU32Bytes);
    return out->length <= kMaxFramePayloadBytes;
}

} // namespace ComboDiscord
