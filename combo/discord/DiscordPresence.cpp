// combo/discord/DiscordPresence.cpp: see DiscordPresence.h.
#include "DiscordPresence.h"

#include <algorithm>
#include <ctime>
#include <exception>
#include <utility>

namespace ComboDiscord {

namespace {

constexpr const char* kSetActivityCommand = "SET_ACTIVITY";
constexpr const char* kReadyEvent = "READY";
constexpr const char* kErrorEvent = "ERROR";
constexpr const char* kNoncePrefix = "ship2ship-";

// Discord's JSON, or null when it isn't JSON; the stream is not ours to trust.
nlohmann::json ParsePayload(const std::string& payload) {
    return nlohmann::json::parse(payload, nullptr, false);
}

std::string StringField(const nlohmann::json& object, const char* key) {
    return (object.is_object() && object.contains(key) && object[key].is_string()) ? object[key].get<std::string>()
                                                                                   : std::string();
}

std::string EventOf(const nlohmann::json& message) {
    return StringField(message, "evt");
}

std::string ErrorMessageOf(const nlohmann::json& message) {
    const std::string reason =
        (message.is_object() && message.contains("data")) ? StringField(message["data"], "message") : std::string();
    return reason.empty() ? "no reason given" : reason;
}

// A game name or an area is never invalid UTF-8, but a dump that threw would end the worker.
std::string Dump(const nlohmann::json& message) {
    return message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace

DiscordPresence::DiscordPresence(std::string applicationId, std::string version, LogFn log)
    : mApplicationId(std::move(applicationId)), mVersion(std::move(version)), mLog(log),
      mSessionStartEpoch(static_cast<int64_t>(std::time(nullptr))), mPid(CurrentProcessId()) {
}

DiscordPresence::~DiscordPresence() {
    Stop();
}

void DiscordPresence::Log(const std::string& message) const {
    if (mLog != nullptr)
        mLog(message);
}

void DiscordPresence::Publish(bool enabled, const PresenceState& state, uint32_t settings) {
    if (mStopRequested)
        return;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSnapshot.enabled = enabled;
        mSnapshot.state = state;
        mSnapshot.settings = settings;
    }
    if (enabled && !mThread.joinable())
        mThread = std::thread(&DiscordPresence::Run, this);
}

void DiscordPresence::Stop() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopRequested = true;
    }
    mWake.notify_all();
    if (mThread.joinable())
        mThread.join();
}

void DiscordPresence::Run() try {
    std::unique_lock<std::mutex> lock(mMutex);
    while (!mStopRequested) {
        const Snapshot snapshot = mSnapshot;
        lock.unlock();
        Step(snapshot);
        lock.lock();
        mWake.wait_for(lock, kLoopInterval, [this] { return mStopRequested.load(); });
    }
    lock.unlock();
    Drop("the program is closing");
} catch (const std::exception& e) {
    Log(std::string("presence thread stopped by an exception: ") + e.what());
} catch (...) { Log("presence thread stopped by an unknown exception"); }

void DiscordPresence::Step(const Snapshot& snapshot) {
    if (!snapshot.enabled) {
        Drop("presence is switched off");
        mNextConnectAttempt = Clock::time_point{};
        return;
    }
    if (!mIpc.IsOpen()) {
        if (Clock::now() < mNextConnectAttempt)
            return;
        if (!Connect()) {
            mNextConnectAttempt = Clock::now() + kReconnectInterval;
            return;
        }
    }
    PumpIncoming();
    if (mIpc.IsOpen())
        SendActivityIfDue(snapshot);
}

bool DiscordPresence::Connect() {
    if (!mIpc.Connect()) {
        if (!mLoggedFailure)
            Log("Discord isn't reachable (not running, or no local IPC); trying again every " +
                std::to_string(kReconnectInterval.count()) + " s");
        mLoggedFailure = true;
        return false;
    }
    if (!Handshake()) {
        mIpc.Close();
        if (!mLoggedFailure)
            Log("Discord did not accept the handshake; trying again every " +
                std::to_string(kReconnectInterval.count()) + " s");
        mLoggedFailure = true;
        return false;
    }
    mLoggedFailure = false;
    mLoggedRejection = false;
    mHasSent = false;
    mLastActivity = nullptr;
    Log("connected to Discord");
    return true;
}

bool DiscordPresence::Handshake() {
    const nlohmann::json hello = { { "v", kProtocolVersion }, { "client_id", mApplicationId } };
    if (!SendFrame(kOpHandshake, Dump(hello)))
        return false;
    const auto deadline = Clock::now() + kHandshakeBudget;
    while (!mStopRequested && Clock::now() < deadline) {
        Frame frame;
        const FrameRead read = ReadFrame(kPollSliceMs, &frame);
        if (read == FrameRead::kBroken)
            return false;
        if (read == FrameRead::kNone)
            continue;
        if (frame.opcode == kOpClose)
            return false;
        if (frame.opcode == kOpPing) {
            SendFrame(kOpPong, frame.payload);
            continue;
        }
        const std::string event = EventOf(ParsePayload(frame.payload));
        if (event == kReadyEvent)
            return true;
        if (event == kErrorEvent)
            return false;
    }
    return false;
}

void DiscordPresence::Drop(const std::string& reason) {
    if (!mIpc.IsOpen())
        return;
    mIpc.Close();
    mNextConnectAttempt = Clock::now() + kReconnectInterval;
    Log("disconnected from Discord (" + reason + ")");
}

void DiscordPresence::PumpIncoming() {
    while (mIpc.IsOpen()) {
        Frame frame;
        const FrameRead read = ReadFrame(0, &frame);
        if (read == FrameRead::kNone)
            return;
        if (read == FrameRead::kBroken) {
            Drop("the connection was lost");
            return;
        }
        HandleIncoming(frame);
    }
}

void DiscordPresence::HandleIncoming(const Frame& frame) {
    if (frame.opcode == kOpPing) {
        if (!SendFrame(kOpPong, frame.payload))
            Drop("a write failed");
    } else if (frame.opcode == kOpClose) {
        Drop("Discord closed the connection");
    } else if (frame.opcode == kOpFrame && !mLoggedRejection) {
        const nlohmann::json message = ParsePayload(frame.payload);
        if (EventOf(message) == kErrorEvent) {
            mLoggedRejection = true;
            Log("Discord rejected the presence: " + ErrorMessageOf(message));
        }
    }
}

void DiscordPresence::SendActivityIfDue(const Snapshot& snapshot) {
    const Clock::time_point now = Clock::now();
    if (mHasSent && now - mLastSend < kMinSendInterval)
        return;
    const nlohmann::json activity = FormatActivity(snapshot.state, snapshot.settings, mVersion, mSessionStartEpoch);
    if (mHasSent && activity == mLastActivity)
        return;
    const nlohmann::json command = { { "cmd", kSetActivityCommand },
                                     { "args", { { "pid", mPid }, { "activity", activity } } },
                                     { "nonce", kNoncePrefix + std::to_string(++mNonce) } };
    if (!SendFrame(kOpFrame, Dump(command))) {
        Drop("a write failed");
        return;
    }
    mHasSent = true;
    mLastActivity = activity;
    mLastSend = now;
}

bool DiscordPresence::SendFrame(uint32_t opcode, const std::string& payload) {
    const std::string frame = EncodeFrame(opcode, payload);
    return mIpc.Write(frame.data(), frame.size(), kWriteBudgetMs) == IoResult::kOk;
}

DiscordPresence::FrameRead DiscordPresence::ReadFrame(int waitMs, Frame* out) {
    if (!mIpc.WaitReadable(waitMs))
        return FrameRead::kNone;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(kFrameReadBudgetMs);
    const auto msLeft = [&deadline] {
        return static_cast<int>(std::max<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count(), 0));
    };
    std::array<unsigned char, kFrameHeaderBytes> header;
    if (mIpc.Read(header.data(), header.size(), msLeft()) != IoResult::kOk)
        return FrameRead::kBroken;
    FrameHeader decoded;
    if (!DecodeFrameHeader(header, &decoded))
        return FrameRead::kBroken;
    out->opcode = decoded.opcode;
    out->payload.assign(decoded.length, '\0');
    if (decoded.length > 0 && mIpc.Read(&out->payload[0], decoded.length, msLeft()) != IoResult::kOk)
        return FrameRead::kBroken;
    return FrameRead::kFrame;
}

} // namespace ComboDiscord
