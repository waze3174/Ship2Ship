// combo/discord/DiscordIpcPosix.cpp: the Linux and macOS transport, a Unix domain socket. See DiscordIpc.h.
//
// The socket is non-blocking and every wait is a poll() with a timeout. A send to a Discord that has gone away must
// not raise SIGPIPE: MSG_NOSIGNAL on Linux, SO_NOSIGPIPE on macOS.
#include "DiscordIpc.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "DiscordIpcPaths.h"

namespace ComboDiscord {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

int MillisecondsLeft(std::chrono::steady_clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    return static_cast<int>(std::max<long long>(left.count(), 0));
}

// True when the descriptor is ready for `events`, or has hung up or failed (the next transfer reports it).
bool PollFd(int fd, short events, int timeoutMs) {
    pollfd entry = { fd, events, 0 };
    for (;;) {
        const int ready = poll(&entry, 1, timeoutMs);
        if (ready >= 0)
            return ready > 0;
        if (errno != EINTR)
            return true;
    }
}

int OpenSocket(const std::string& path) {
    sockaddr_un address = {};
    if (path.size() >= sizeof(address.sun_path))
        return -1;
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

const char* EnvOrNull(const char* name) {
    return std::getenv(name);
}

} // namespace

int CurrentProcessId() {
    return static_cast<int>(getpid());
}

DiscordIpc::~DiscordIpc() {
    Close();
}

bool DiscordIpc::IsOpen() const {
    return mFd >= 0;
}

bool DiscordIpc::Connect() {
    Close();
    for (const std::string& path : UnixSocketPaths(EnvOrNull)) {
        const int fd = OpenSocket(path);
        if (fd >= 0) {
            mFd = fd;
            return true;
        }
    }
    return false;
}

void DiscordIpc::Close() {
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
}

bool DiscordIpc::WaitReadable(int timeoutMs) {
    return PollFd(mFd, POLLIN, timeoutMs);
}

IoResult DiscordIpc::Read(void* buffer, size_t size, int timeoutMs) {
    char* out = static_cast<char*>(buffer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (size > 0) {
        if (!PollFd(mFd, POLLIN, MillisecondsLeft(deadline)))
            return IoResult::kTimeout;
        const ssize_t got = recv(mFd, out, size, 0);
        if (got > 0) {
            out += got;
            size -= static_cast<size_t>(got);
        } else if (got == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            return IoResult::kClosed;
        }
    }
    return IoResult::kOk;
}

IoResult DiscordIpc::Write(const void* buffer, size_t size, int timeoutMs) {
    const char* in = static_cast<const char*>(buffer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (size > 0) {
        if (!PollFd(mFd, POLLOUT, MillisecondsLeft(deadline)))
            return IoResult::kTimeout;
        const ssize_t sent = send(mFd, in, size, kSendFlags);
        if (sent > 0) {
            in += sent;
            size -= static_cast<size_t>(sent);
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return IoResult::kClosed;
        }
    }
    return IoResult::kOk;
}

} // namespace ComboDiscord
