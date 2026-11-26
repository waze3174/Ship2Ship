#pragma once

// The launcher's log. The Windows release build has no console, so the launcher's own messages used to go nowhere.
// Every line goes to the console as before, and to the game's logger (logs/Ship2Ship.log) with a "[Launcher]" tag.
// The game's logger lives in the SoH DLL and starts after the launcher does, so lines wait in a bounded buffer until a
// sink that can take them is attached; Log is pure logic and unit-tested (combo/tests/ComboLauncherLogTest.cpp).
#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace ComboLauncherLog {

// The values are the ABI of SOH_LauncherLog (soh/soh/Enhancements/combo/ComboLauncherLog.cpp).
enum class Level { Info = 0, Warn = 1, Error = 2 };

inline constexpr const char* kTag = "[Launcher] ";
inline constexpr std::size_t kMaxPendingLines = 4096;

struct Line {
    Level level;
    std::string text;
};

// Hands one tagged line to the game's logger. Returns false while that logger does not exist, and the line is kept.
using Sink = std::function<bool(Level, const std::string&)>;

class Log {
  public:
    // Keeps the line and delivers whatever the sink will take. Without a sink, or once closed, nothing is delivered.
    void Write(Level level, const std::string& text) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mClosed) {
            return;
        }
        mPending.push_back({ level, kTag + text });
        if (mPending.size() > kMaxPendingLines) {
            mPending.pop_front();
            ++mDropped;
        }
        DeliverLocked();
    }

    void Attach(Sink sink) {
        std::lock_guard<std::mutex> lock(mMutex);
        mSink = std::move(sink);
        mClosed = false;
        DeliverLocked();
    }

    // Tries once more to deliver, for the moment the game's logger comes up with no launcher line to follow.
    void Flush() {
        std::lock_guard<std::mutex> lock(mMutex);
        DeliverLocked();
    }

    // Ends the sink's life, before the library behind it is unloaded. Later lines are not kept. Returns what could not
    // be delivered.
    std::vector<Line> Detach() {
        std::lock_guard<std::mutex> lock(mMutex);
        DeliverLocked();
        mSink = nullptr;
        mClosed = true;
        return std::vector<Line>(mPending.begin(), mPending.end());
    }

    // What still waits for a sink, for a fatal error that happens before the game's logger exists.
    std::vector<Line> Pending() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return std::vector<Line>(mPending.begin(), mPending.end());
    }

    // True the first time a key is seen, for messages from code that runs every frame.
    bool FirstTime(const std::string& key) {
        std::lock_guard<std::mutex> lock(mMutex);
        return mSeenKeys.insert(key).second;
    }

  private:
    void DeliverLocked() {
        if (!mSink) {
            return;
        }
        if (mDropped > 0) {
            mPending.push_front({ Level::Warn, std::string(kTag) + std::to_string(mDropped) +
                                                   " earlier line(s) were dropped: the game's log was not ready" });
            mDropped = 0;
        }
        while (!mPending.empty()) {
            if (!mSink(mPending.front().level, mPending.front().text)) {
                return;
            }
            mPending.pop_front();
        }
    }

    mutable std::mutex mMutex;
    std::deque<Line> mPending;
    std::set<std::string> mSeenKeys;
    Sink mSink;
    std::size_t mDropped = 0;
    bool mClosed = false;
};

inline const char* LevelName(Level level) {
    switch (level) {
        case Level::Warn:
            return "warn";
        case Level::Error:
            return "error";
        case Level::Info:
            break;
    }
    return "info";
}

// Appends lines to a file of their own, creating its folder. Returns false when the file cannot be written.
inline bool AppendLines(const std::filesystem::path& file, const std::vector<Line>& lines) {
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    std::ofstream out(file, std::ios::app);
    if (!out) {
        return false;
    }
    for (const auto& line : lines) {
        out << '[' << LevelName(line.level) << "] " << line.text << '\n';
    }
    return static_cast<bool>(out);
}

inline Log& Instance() {
    static Log log;
    return log;
}

// One message, written like a stream: Info() << "loaded " << count << " slots". It ends the line itself, so a
// trailing newline or std::endl is ignored.
class Stream {
  public:
    explicit Stream(Level level) : mLevel(level) {
    }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

    ~Stream() {
        std::string text = mText.str();
        while (!text.empty() && (text.back() == '\n')) {
            text.pop_back();
        }
        auto& console = (mLevel == Level::Info) ? std::cout : std::cerr;
        console << text << std::endl;
        Instance().Write(mLevel, text);
    }

    template <typename T> Stream& operator<<(const T& value) {
        mText << value;
        return *this;
    }

    Stream& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
        manipulator(mText);
        return *this;
    }

  private:
    Level mLevel;
    std::ostringstream mText;
};

inline Stream Info() {
    return Stream(Level::Info);
}
inline Stream Warn() {
    return Stream(Level::Warn);
}
inline Stream Error() {
    return Stream(Level::Error);
}

// Inside a catch block: names what failed and what was thrown. `once` is for code that runs every frame.
inline void Caught(const std::string& what, bool once = false) {
    if (once && !Instance().FirstTime(what)) {
        return;
    }
    try {
        throw;
    } catch (const std::exception& e) { Warn() << "[ComboShip] " << what << " failed: " << e.what(); } catch (...) {
        Warn() << "[ComboShip] " << what << " failed: unknown exception";
    }
}

} // namespace ComboLauncherLog
