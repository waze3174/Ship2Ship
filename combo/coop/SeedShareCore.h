#pragma once
// ComboShip co-op SeedShare core (plan task 1.6): splitting a compressed seed into COMBO_SEED_CHUNK
// payloads and reassembling them on the joiner. Pure logic (no sockets, no zlib) so coop_tests can
// cover dropped / duplicated / out-of-order / foreign chunks.
//
// Chunk size (Findings log 0.1): the relay accepts frames up to 8 MiB but disconnects a client whose
// 256-packet send queue overflows, so fewer, larger chunks are safer than many small ones. 256 KiB of
// base64 per chunk keeps each frame far under the cap and a 2 MiB seed at ~11 packets.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Coop {

constexpr size_t kSeedChunkBytes = 256 * 1024; // base64 characters per chunk
constexpr int kSeedMaxChunks = 64;             // 16 MiB of base64: anything bigger is refused

inline std::vector<std::string> SplitChunks(const std::string& b64, size_t chunk = kSeedChunkBytes) {
    std::vector<std::string> out;
    if (chunk == 0)
        return out;
    for (size_t i = 0; i < b64.size(); i += chunk)
        out.push_back(b64.substr(i, chunk));
    if (out.empty())
        out.push_back(std::string());
    return out;
}

class SeedAssembler {
  public:
    // Starts (or restarts) collecting `seedId` in `n` chunks. Any partial state for another id is dropped.
    void Begin(uint64_t seedId, int n) {
        mSeedId = seedId;
        mParts.assign(n > 0 && n <= kSeedMaxChunks ? static_cast<size_t>(n) : 0, std::nullopt);
        mHave = 0;
    }

    uint64_t SeedId() const {
        return mSeedId;
    }
    bool Active() const {
        return !mParts.empty();
    }

    // Returns false for a chunk that doesn't belong (wrong id, bad index, n disagrees). Duplicates are
    // accepted idempotently (the first copy wins).
    bool Add(uint64_t seedId, int i, int n, const std::string& blob) {
        if (!Active() || seedId != mSeedId || n != static_cast<int>(mParts.size()) || i < 0 || i >= n)
            return false;
        auto& slot = mParts[static_cast<size_t>(i)];
        if (!slot) {
            slot = blob;
            ++mHave;
        }
        return true;
    }

    bool Complete() const {
        return Active() && mHave == mParts.size();
    }

    int Received() const {
        return static_cast<int>(mHave);
    }
    int Total() const {
        return static_cast<int>(mParts.size());
    }

    std::string Join() const {
        std::string out;
        for (const auto& p : mParts)
            if (p)
                out += *p;
        return out;
    }

    void Reset() {
        mSeedId = 0;
        mParts.clear();
        mHave = 0;
    }

  private:
    uint64_t mSeedId = 0;
    std::vector<std::optional<std::string>> mParts;
    size_t mHave = 0;
};

} // namespace Coop
