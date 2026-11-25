#pragma once

// combo/ComboContainerWriter.h — ComboShip: the thread that writes the slot save containers (Save/file{N+1}.combosav)
// to disk, so a slow disk never stalls the game that saved. On the owner's Steam Deck the SD card stalled container
// writes for up to 3 s at a time, and the game froze with them (docs/deviations/rando.md, "Saves that freeze the game
// for seconds"). Only the launcher (ComboShip.cpp) includes this.
//
// Who does what:
// - The thread that saves still updates the launcher's in-memory container under g_containerMutex and serializes it
//   there (FlushContainer). That in-memory container stays the truth for every read in the process, so a game switch,
//   the file select and every launcher query see the newest data whether it is on disk yet or not.
// - It hands the text over (Enqueue) while it still holds g_containerMutex, so the snapshots of a slot arrive in the
//   order they were taken. The writer keeps at most one waiting snapshot per slot: a newer one replaces it, keeping its
//   place in line. The file on disk only ever moves forward.
// - The writer thread writes each snapshot as FlushContainer always did (WriteContainerFile: temp file, then a rename
//   over the container, no fsync), one at a time, the slot that has waited longest first. It never takes
//   g_containerMutex. Locks are only ever taken in the order g_containerMutex, then the writer's; nothing waits on the
//   disk while it holds the writer's.
// - Every other disk step on a container waits for that slot's writes, called with g_containerMutex held so no new
//   snapshot of the slot can arrive meanwhile: the erase drops the waiting snapshot and waits out the one being written
//   (CancelAndWait), and reading a slot that is not in memory waits for both (WaitIdle), which also covers the
//   corrupt/outdated-container renames and the recovery from the temp file that read can make.
// - A write that cannot go to the thread (the kill switch gCombo.Debug.SyncSaveWrites, a thread that could not start,
//   or a writer already stopped at quit) is written by the caller, after CancelAndWait for its slot. The one write
//   nobody makes: a slot the thread was still writing when the quit deadline left it behind (see Stop).
// - Quit drains the writer before the process ends (Drain, Stop), inside the shutdown watchdog's time. Any other exit
//   that runs atexit handlers drains it too (StopAtExit).
//
// What a crash loses: the snapshots not yet on disk; normally the last 50-100 ms of saves, and while the disk is
// stalled, as long as it stalls. Each snapshot is the whole container, so a lost snapshot takes both games' changes
// with it: an OOT item and the MM check it came from are lost together, and the check can be collected again. The
// container on disk is never torn; a write cut short leaves only a partial temp file, which never parses, so nothing
// uses it.
//
// A rename over the container that fails (on Windows, a scanner or sync tool holding one of the files) is retried for a
// moment before the fallback that removes the container first. If the rename fails after that too, the slot is left in
// its temp file, which was written whole before any rename; LoadOrCreateContainer recovers the slot from it while the
// container is missing. An erase removes both files, so nothing of an erased slot can be recovered.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include "ComboSaveTiming.h"

namespace ComboContainerIo {

using Clock = std::chrono::steady_clock;

inline double MsBetween(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

struct FileWriteResult {
    bool ok = false;
    std::string error;
    int renameRetries = 0;                // tries the rename over the container took beyond the first
    bool removedFirst = false;            // it fell back to removing the container before renaming again
    std::string notice;                   // what those retries and that fallback were, for the log
    ComboSaveTiming::ContainerIoStats io; // openMs, fileWriteMs, renameMs and containerBytes
};

// Something that holds a file for a moment (on Windows, a scanner or sync tool) makes a rename or remove of it fail.
// Short retries wait that out; they are few, as the caller can hold g_containerMutex.
inline constexpr int kHeldFileRetries = 5;
inline constexpr auto kHeldFileRetryDelay = std::chrono::milliseconds(20);

// After op(ec) failed, tries it again up to kHeldFileRetries times; returns the retries made. ec holds the last error.
template <typename Op> inline int RetryWhileHeld(Op op, std::error_code& ec) {
    int retries = 0;
    while (ec && (retries < kHeldFileRetries)) {
        std::this_thread::sleep_for(kHeldFileRetryDelay);
        ++retries;
        op(ec);
    }
    return retries;
}

inline std::filesystem::path TempPathOf(const std::filesystem::path& path) {
    auto tmp = path;
    tmp += ".temp";
    return tmp;
}

// Removes a file, retrying while it is held. One that cannot be removed is emptied, so it can never be read back as a
// container. Returns what went wrong, or "" (also when there was no file).
inline std::string DiscardFile(const std::filesystem::path& file) {
    const auto removeFile = [&](std::error_code& e) { std::filesystem::remove(file, e); };
    std::error_code ec;
    removeFile(ec);
    RetryWhileHeld(removeFile, ec);
    std::error_code existsEc;
    if (!ec || (!std::filesystem::exists(file, existsEc) && !existsEc)) {
        return ""; // removed, or gone anyway: never create the file by emptying it
    }
    // filename(), not string(): the folder name can be outside the Windows code page, and then string() throws.
    std::string problem = "cannot remove " + file.filename().string() + " (" + ec.message() + ")";
    std::ofstream emptied(file, std::ios::trunc | std::ios::binary);
    problem += emptied.is_open() ? "; emptied it instead" : "; cannot empty it either, so it can be read back";
    return problem;
}

// For an erase: removes the container and its temp file, so LoadOrCreateContainer can read back neither. Returns what
// went wrong, or "".
inline std::string DiscardContainerFiles(const std::filesystem::path& path) {
    const std::string container = DiscardFile(path);
    const std::string temp = DiscardFile(TempPathOf(path));
    return container + ((!container.empty() && !temp.empty()) ? "; " : "") + temp;
}

// Writes the container text to path through path.temp and a rename over path, so the container is never torn.
inline FileWriteResult WriteContainerFile(const std::filesystem::path& path, const std::string& text) {
    FileWriteResult result;
    result.io.containerBytes = text.size();
    ComboSaveTiming::Stopwatch timer;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = TempPathOf(path);
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        result.io.openMs = timer.Lap();
        // A failure here USED TO BE a bare `return`. With the path CWD-relative and a Finder-launched
        // .app starting at "/" (read-only on macOS), that silently discarded every save while the
        // game logged "Save File Finish" — a whole playthrough lost with no on-disk trace. The path
        // is anchored now, but a save that cannot be written must be loud regardless of the reason.
        if (!out.is_open()) {
            result.error = "cannot write save container " + path.string();
            std::cerr << "[ComboShip] ERROR: " << result.error << " — THIS SLOT IS NOT BEING SAVED." << std::endl;
            return result;
        }
        out << text;
        out.close();
        result.io.fileWriteMs = timer.Lap();
        // A short write (a full disk) must not be renamed over the last good container.
        if (out.fail()) {
            result.error = "cannot write save container " + path.string() + " (the write failed; disk full?)";
            std::cerr << "[ComboShip] ERROR: " << result.error << " — THIS SLOT IS NOT BEING SAVED." << std::endl;
            return result;
        }
    }
    const auto renameTemp = [&](std::error_code& e) { std::filesystem::rename(tmp, path, e); };
    renameTemp(ec);
    if (ec) {
        const std::string firstError = ec.message();
        result.renameRetries = RetryWhileHeld(renameTemp, ec);
        // filename(), not string(): see DiscardFile.
        result.notice = path.filename().string() + ": renaming the temp file over it failed (" + firstError + ")";
        if (!ec) {
            result.notice += ", then worked on retry " + std::to_string(result.renameRetries);
        } else {
            // Some filesystems won't rename over a file. Removing the container first can leave the slot only in the
            // temp file, which is whole by now: LoadOrCreateContainer recovers it from there.
            result.removedFirst = true;
            std::error_code removeEc;
            std::filesystem::remove(path, removeEc);
            renameTemp(ec);
            result.renameRetries += 1 + RetryWhileHeld(renameTemp, ec);
            std::string outcome = "worked";
            if (ec) {
                outcome = removeEc ? "still failed, so the previous container stays"
                                   : "still failed, so the slot is only in its .temp file until a save works";
            }
            result.notice += " " + std::to_string(kHeldFileRetries + 1) + " times; " +
                             (removeEc ? "removing the container failed too (" + removeEc.message() + ")"
                                       : std::string("removed the container")) +
                             ", then the rename " + outcome;
        }
        std::cerr << "[ComboShip] WARNING: " << result.notice << std::endl;
    }
    result.io.renameMs = timer.Lap();
    if (ec) {
        result.error = "cannot commit save container " + path.string() + " (" + ec.message() + ")";
        std::cerr << "[ComboShip] ERROR: " << result.error << " — THIS SLOT IS NOT BEING SAVED." << std::endl;
        return result;
    }
    result.ok = true;
    return result;
}

class Writer {
  public:
    struct Handover {
        bool accepted = false; // false: the caller must write the file itself
        int queued = 0;        // snapshots waiting or being written right after this one was taken
        bool replaced = false; // it replaced a waiting snapshot of the same slot
    };

    // Takes the slot's newest snapshot. Call with g_containerMutex held. Moves from text only when accepted.
    Handover Enqueue(int slot, const std::filesystem::path& path, std::string&& text) {
        std::lock_guard<std::mutex> lk(mMutex);
        Handover handover;
        if (mStopped || !StartLocked()) {
            return handover;
        }
        auto it = mPending.find(slot);
        if (it != mPending.end()) {
            it->second.path = path;
            it->second.text = std::move(text);
            handover.replaced = true;
            ++mReport.replaced;
        } else {
            mPending.emplace(slot, Snapshot{ path, std::move(text), mNextSeq++, Clock::now() });
        }
        handover.accepted = true;
        handover.queued = UnwrittenLocked();
        mReport.maxQueued = (std::max)(mReport.maxQueued, handover.queued); // (): Windows.h may define max
        mWork.notify_one();
        return handover;
    }

    // Drops the slot's waiting snapshot and waits until the slot is not being written. Call with g_containerMutex held.
    // Returns false when a thread left behind at a stop is still writing the slot: a caller that wants to write the
    // slot itself must not, because both would write the one temp file and the older snapshot could land last.
    bool CancelAndWait(int slot) {
        std::unique_lock<std::mutex> lk(mMutex);
        if (mPending.erase(slot) > 0) {
            ++mReport.replaced;
            mDone.notify_all(); // a Drain may be waiting for exactly this snapshot
        }
        mDone.wait(lk, [&] { return (mWritingSlot != slot) || mDetached; });
        return mWritingSlot != slot;
    }

    // Waits until the slot has nothing waiting or being written. Call with g_containerMutex held. Returns false when a
    // thread left behind at a stop is still writing the slot, and so still owns its temp file.
    bool WaitIdle(int slot) {
        std::unique_lock<std::mutex> lk(mMutex);
        mDone.wait(lk, [&] { return ((mPending.count(slot) == 0) && (mWritingSlot != slot)) || mDetached; });
        return mWritingSlot != slot;
    }

    // Waits until every snapshot is on disk or the deadline passes; returns how many are still unwritten.
    int Drain(Clock::time_point deadline) {
        std::unique_lock<std::mutex> lk(mMutex);
        mDone.wait_until(lk, deadline, [&] { return UnwrittenLocked() == 0; });
        return UnwrittenLocked();
    }

    // Drains, then stops the thread; later writes are made by their callers. A thread still writing at the deadline is
    // left to finish that write on its own (detached) and is cut off by the process exit; it starts no other. Returns
    // the snapshots unwritten at the deadline.
    int Stop(Clock::time_point deadline) {
        std::unique_lock<std::mutex> lk(mMutex);
        if (mStopped) {
            return 0;
        }
        mStopped = true;
        if (!mThread.joinable()) {
            return 0;
        }
        mStopping = true;
        mWork.notify_all();
        const bool drained = mDone.wait_until(lk, deadline, [&] { return UnwrittenLocked() == 0; });
        const int unwritten = UnwrittenLocked();
        mDetached = !drained;
        lk.unlock();
        mDone.notify_all(); // a waiter on a detached thread must not wait for it
        if (drained) {
            mThread.join();
        } else {
            mThread.detach();
        }
        return unwritten;
    }

    // The caller's own work on a container file is reported with the thread's, so the game in play logs it: a write it
    // made itself, a write or erase that failed, a container it recovered from the temp file.
    void NoteCallerWrite(const FileWriteResult& result) {
        std::lock_guard<std::mutex> lk(mMutex);
        NoteResultLocked(result);
    }

    void NoteCallerFailed(const std::string& error) {
        std::lock_guard<std::mutex> lk(mMutex);
        NoteFailedLocked(error);
    }

    void NoteRecovered(const std::string& notice) {
        std::lock_guard<std::mutex> lk(mMutex);
        ++mReport.recovered;
        std::snprintf(mReport.lastNotice, sizeof(mReport.lastNotice), "%s", notice.c_str());
    }

    // What the thread did since the previous call.
    void TakeReport(ComboSaveTiming::ContainerWriterReport* out) {
        std::lock_guard<std::mutex> lk(mMutex);
        *out = mReport;
        mReport = {};
    }

    static void StopAtExit();

  private:
    struct Snapshot {
        std::filesystem::path path;
        std::string text;
        uint64_t seq;                     // place in line: the order the slot's oldest waiting change was handed over
        Clock::time_point oldestHandover; // kept when a newer snapshot replaces this one
    };

    static constexpr auto kAtExitBudget = std::chrono::seconds(3);

    int UnwrittenLocked() const {
        return static_cast<int>(mPending.size()) + ((mWritingSlot >= 0) ? 1 : 0);
    }

    bool StartLocked() {
        if (mThread.joinable()) {
            return true;
        }
        if (mStartFailed) {
            return false;
        }
        try {
            mThread = std::thread([this] { Run(); });
        } catch (const std::system_error& e) {
            mStartFailed = true;
            std::cerr << "[ComboShip] ERROR: cannot start the save writer thread (" << e.what()
                      << "); each save is written by the thread that saves" << std::endl;
            return false;
        }
        std::atexit(&Writer::StopAtExit);
        return true;
    }

    void Run() {
        std::unique_lock<std::mutex> lk(mMutex);
        for (;;) {
            mWork.wait(lk, [&] { return mStopping || !mPending.empty(); });
            if (mPending.empty() || mDetached) {
                return; // stopping, and everything is on disk; or left behind at a stop, so no new write as the
                        // process exits
            }
            auto next = std::min_element(mPending.begin(), mPending.end(),
                                         [](const auto& a, const auto& b) { return a.second.seq < b.second.seq; });
            const int slot = next->first;
            Snapshot snapshot = std::move(next->second);
            mPending.erase(next);
            mWritingSlot = slot;
            lk.unlock();
            const auto started = Clock::now();
            FileWriteResult result;
            try {
                result = WriteContainerFile(snapshot.path, snapshot.text);
            } catch (const std::exception& e) {
                // Named by its slot: path.string() throws on Windows for a folder name outside the code page, and an
                // exception that leaves this thread ends the process.
                result.error = "writing the save container of slot " + std::to_string(slot) + " threw: " + e.what();
                std::cerr << "[ComboShip] ERROR: " << result.error << " — THIS SLOT IS NOT BEING SAVED." << std::endl;
            }
            const auto done = Clock::now();
            if (MsBetween(started, done) >= ComboSaveTiming::kSlowWriteMs) {
                ComboSaveTiming::SampleKernelWriteback(result.io);
            }
            std::string().swap(snapshot.text); // free the text before taking the lock again
            lk.lock();
            mWritingSlot = -1;
            RecordLocked(slot, snapshot.oldestHandover, started, done, result);
            mDone.notify_all();
        }
    }

    void RecordLocked(int slot, Clock::time_point oldestHandover, Clock::time_point started, Clock::time_point done,
                      const FileWriteResult& result) {
        ++mReport.written;
        NoteResultLocked(result);
        const double handoverToDiskMs = MsBetween(oldestHandover, done);
        if ((mReport.slowestSlot < 0) || (handoverToDiskMs > mReport.maxHandoverToDiskMs)) {
            mReport.maxHandoverToDiskMs = handoverToDiskMs;
            mReport.slowestSlot = slot;
            mReport.slowestWaitMs = MsBetween(oldestHandover, started);
            mReport.slowestFile = result.io;
        }
    }

    void NoteResultLocked(const FileWriteResult& result) {
        if (!result.ok) {
            NoteFailedLocked(result.error);
        }
        if (result.renameRetries > 0) {
            ++mReport.renameRetried;
        }
        if (result.removedFirst) {
            ++mReport.removedFirst;
        }
        if (!result.notice.empty()) {
            std::snprintf(mReport.lastNotice, sizeof(mReport.lastNotice), "%s", result.notice.c_str());
        }
    }

    void NoteFailedLocked(const std::string& error) {
        ++mReport.failed;
        std::snprintf(mReport.lastError, sizeof(mReport.lastError), "%s", error.c_str());
    }

    std::mutex mMutex;
    std::condition_variable mWork; // a snapshot arrived, or the writer is stopping
    std::condition_variable mDone; // a write finished, or the thread was left behind at a stop
    std::map<int, Snapshot> mPending;
    int mWritingSlot = -1;
    uint64_t mNextSeq = 0;
    bool mStartFailed = false;
    bool mStopping = false; // the thread exits once nothing is waiting
    bool mStopped = false;  // no more hand-overs
    bool mDetached = false; // stopped before it was done; nobody waits for it any more
    std::thread mThread;
    ComboSaveTiming::ContainerWriterReport mReport;
};

// The process's one writer. Never destroyed: a thread left writing at exit must not find its mutex gone.
inline Writer& SharedWriter() {
    static Writer* writer = new Writer();
    return *writer;
}

inline void Writer::StopAtExit() {
    const int unwritten = SharedWriter().Stop(Clock::now() + kAtExitBudget);
    if (unwritten > 0) {
        std::cerr << "[ComboShip] ERROR: " << unwritten << " save snapshot(s) not on disk at exit" << std::endl;
    }
}

} // namespace ComboContainerIo
