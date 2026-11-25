// combo/discord/DiscordIpcWin.cpp: the Windows transport, a named pipe (\\.\pipe\discord-ipc-N). See DiscordIpc.h.
//
// The pipe is opened for overlapped I/O so that a transfer can be given up on: a write to a Discord that stopped
// reading would otherwise block the thread, and with it the program's quit. Reads only start once PeekNamedPipe says
// bytes are waiting.
#include "DiscordIpc.h"

#include <algorithm>
#include <chrono>
#include <string>

#include "DiscordIpcPaths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ComboDiscord {

namespace {

constexpr int kPollSliceMs = 5;
constexpr int kReadyDataTimeoutMs = 100; // a read that Peek has promised completes at once; this is only a bound

enum class Direction { kRead, kWrite };

// Runs one overlapped transfer and gives up on it after timeoutMs. *done is the byte count that moved.
IoResult Transfer(HANDLE pipe, HANDLE event, Direction direction, void* buffer, DWORD size, int timeoutMs,
                  DWORD* done) {
    OVERLAPPED overlapped = {};
    overlapped.hEvent = event;
    ResetEvent(event);
    const BOOL started = (direction == Direction::kWrite) ? WriteFile(pipe, buffer, size, nullptr, &overlapped)
                                                          : ReadFile(pipe, buffer, size, nullptr, &overlapped);
    if (!started && GetLastError() != ERROR_IO_PENDING)
        return IoResult::kClosed;

    bool gaveUp = false;
    if (!started && WaitForSingleObject(event, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        gaveUp = true;
    }
    if (GetOverlappedResult(pipe, &overlapped, done, TRUE))
        return IoResult::kOk;
    const DWORD error = GetLastError();
    if (error == ERROR_MORE_DATA) // a message-mode pipe holds more of this message than was asked for
        return IoResult::kOk;
    return (gaveUp && error == ERROR_OPERATION_ABORTED) ? IoResult::kTimeout : IoResult::kClosed;
}

int MillisecondsLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return static_cast<int>(std::max<long long>(left.count(), 0));
}

} // namespace

int CurrentProcessId() {
    return static_cast<int>(GetCurrentProcessId());
}

DiscordIpc::~DiscordIpc() {
    Close();
    if (mEvent != nullptr)
        CloseHandle(mEvent);
}

bool DiscordIpc::IsOpen() const {
    return mHandle != nullptr;
}

bool DiscordIpc::Connect() {
    Close();
    for (int slot = 0; slot < kIpcSlots; ++slot) {
        const std::string name = WindowsPipeName(slot);
        const std::wstring wideName(name.begin(), name.end());
        HANDLE pipe = CreateFileW(wideName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            continue;
        if (mEvent == nullptr)
            mEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (mEvent == nullptr) {
            CloseHandle(pipe);
            return false;
        }
        mHandle = pipe;
        return true;
    }
    return false;
}

void DiscordIpc::Close() {
    if (mHandle != nullptr) {
        CloseHandle(mHandle);
        mHandle = nullptr;
    }
}

bool DiscordIpc::WaitReadable(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(mHandle, nullptr, 0, nullptr, &available, nullptr) || available > 0)
            return true;
        const int left = MillisecondsLeft(deadline);
        if (left == 0)
            return false;
        Sleep(static_cast<DWORD>(std::min(left, kPollSliceMs)));
    }
}

IoResult DiscordIpc::Read(void* buffer, size_t size, int timeoutMs) {
    char* out = static_cast<char*>(buffer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (size > 0) {
        DWORD available = 0;
        if (!PeekNamedPipe(mHandle, nullptr, 0, nullptr, &available, nullptr))
            return IoResult::kClosed;
        if (available == 0) {
            const int left = MillisecondsLeft(deadline);
            if (left == 0)
                return IoResult::kTimeout;
            Sleep(static_cast<DWORD>(std::min(left, kPollSliceMs)));
            continue;
        }
        const DWORD want = static_cast<DWORD>(std::min<size_t>(available, size));
        DWORD done = 0;
        const IoResult result = Transfer(mHandle, mEvent, Direction::kRead, out, want, kReadyDataTimeoutMs, &done);
        if (result != IoResult::kOk)
            return result;
        if (done == 0) // a transfer that completes with nothing moved would loop forever
            return IoResult::kClosed;
        out += done;
        size -= done;
    }
    return IoResult::kOk;
}

IoResult DiscordIpc::Write(const void* buffer, size_t size, int timeoutMs) {
    const char* in = static_cast<const char*>(buffer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (size > 0) {
        DWORD done = 0;
        const IoResult result = Transfer(mHandle, mEvent, Direction::kWrite, const_cast<char*>(in),
                                         static_cast<DWORD>(size), MillisecondsLeft(deadline), &done);
        if (result != IoResult::kOk)
            return result;
        if (done == 0) // a transfer that completes with nothing moved would loop forever
            return IoResult::kClosed;
        in += done;
        size -= done;
    }
    return IoResult::kOk;
}

} // namespace ComboDiscord
