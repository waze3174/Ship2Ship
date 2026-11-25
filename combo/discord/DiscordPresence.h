// combo/discord/DiscordPresence.h
//
// ComboShip: the Discord Rich Presence controller. It owns a thread that keeps the IPC connection and sends the
// activity; the game thread only hands it a snapshot (Publish). All socket work stays on the Discord thread, and every
// wait on it is short, so Stop returns in well under a second whatever Discord is doing.
//
// Off, or Discord not running, costs nothing: no thread starts until presence is enabled, and a missing Discord is
// retried quietly. See docs/deviations/discord.md.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "DiscordActivity.h"
#include "DiscordFrame.h"
#include "DiscordIpc.h"

namespace ComboDiscord {

// Discord rate-limits activity updates; this keeps well inside it.
inline constexpr std::chrono::seconds kMinSendInterval{ 15 };
inline constexpr std::chrono::seconds kReconnectInterval{ 30 };
inline constexpr std::chrono::milliseconds kLoopInterval{ 200 };
inline constexpr std::chrono::milliseconds kHandshakeBudget{ 2000 };
inline constexpr int kPollSliceMs = 100;
inline constexpr int kWriteBudgetMs = 250;
inline constexpr int kFrameReadBudgetMs = 250;
inline constexpr int kProtocolVersion = 1;

using LogFn = void (*)(const std::string& message);

class DiscordPresence {
  public:
    DiscordPresence(std::string applicationId, std::string version, LogFn log);
    ~DiscordPresence();
    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // The game thread, a few times a second at most. Starts the worker on the first call with enabled set. Not
    // enabled means disconnected: the pipe is closed, which clears the presence, and nothing is sent.
    void Publish(bool enabled, const PresenceState& state, uint32_t settings);

    // Ends the worker and closes the connection, which clears the presence. Safe to call twice. The same thread that
    // calls Publish calls this.
    void Stop();

  private:
    struct Snapshot {
        bool enabled = false;
        PresenceState state;
        uint32_t settings = 0;
    };
    struct Frame {
        uint32_t opcode = 0;
        std::string payload;
    };
    enum class FrameRead { kNone, kFrame, kBroken };
    using Clock = std::chrono::steady_clock;

    void Run();
    void Step(const Snapshot& snapshot);
    bool Connect();
    bool Handshake();
    void Drop(const std::string& reason);
    void PumpIncoming();
    void HandleIncoming(const Frame& frame);
    void SendActivityIfDue(const Snapshot& snapshot);
    bool SendFrame(uint32_t opcode, const std::string& payload);
    FrameRead ReadFrame(int waitMs, Frame* out);
    void Log(const std::string& message) const;

    const std::string mApplicationId;
    const std::string mVersion;
    const LogFn mLog;
    const int64_t mSessionStartEpoch;
    const int mPid;

    std::mutex mMutex;
    std::condition_variable mWake;
    Snapshot mSnapshot;
    std::atomic<bool> mStopRequested{ false };
    std::thread mThread;

    // Worker thread only.
    DiscordIpc mIpc;
    Clock::time_point mNextConnectAttempt{};
    Clock::time_point mLastSend{};
    bool mHasSent = false;
    nlohmann::json mLastActivity;
    bool mLoggedFailure = false;
    bool mLoggedRejection = false;
    uint64_t mNonce = 0;
};

} // namespace ComboDiscord
