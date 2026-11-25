// combo/discord/DiscordIpc.h
//
// ComboShip: the byte transport to the Discord desktop app. A named pipe on Windows (DiscordIpcWin.cpp), a Unix domain
// socket elsewhere (DiscordIpcPosix.cpp). Every call is bounded by a timeout, so the thread that owns it can always be
// stopped quickly. One thread uses an instance; it is not thread safe. See docs/deviations/discord.md.
#pragma once

#include <cstddef>

namespace ComboDiscord {

enum class IoResult { kOk, kTimeout, kClosed };

// This process's id, which SET_ACTIVITY names so that Discord ties the presence to the running program.
int CurrentProcessId();

class DiscordIpc {
  public:
    DiscordIpc() = default;
    ~DiscordIpc();
    DiscordIpc(const DiscordIpc&) = delete;
    DiscordIpc& operator=(const DiscordIpc&) = delete;

    // Opens the first Discord endpoint that answers. False when Discord isn't running or isn't reachable.
    bool Connect();
    bool IsOpen() const;
    void Close();

    // True when bytes can be read, or the other side is gone (the next Read then reports kClosed).
    bool WaitReadable(int timeoutMs);
    // Reads exactly `size` bytes. kTimeout means some or none of them arrived in time.
    IoResult Read(void* buffer, size_t size, int timeoutMs);
    IoResult Write(const void* buffer, size_t size, int timeoutMs);

  private:
#ifdef _WIN32
    void* mHandle = nullptr; // HANDLE, INVALID_HANDLE_VALUE when closed
    void* mEvent = nullptr;  // the event overlapped transfers wait on
#else
    int mFd = -1;
#endif
};

} // namespace ComboDiscord
